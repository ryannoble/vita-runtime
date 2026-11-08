/* Backend-independent software mixer and WAV decoding. Backends provide the
 * lock and the output device (see audio_internal.h). */

#include <stdlib.h>
#include <string.h>

#include "vr/vr_audio.h"
#include "audio_internal.h"

#define MAX_VOICES   255
#define STREAM_FRAMES 1024
#define FP_ONE       4294967296.0 /* 32.32 fixed point */

struct vr_sound {
    int      channels, sample_rate;
    size_t   frames;
    int16_t *data;
};

typedef struct {
    uint8_t  active, generation, paused, loop;
    vr_sound *sound;          /* NULL for streams */
    float    volume, pan, pitch;
    uint64_t pos;             /* source frame position, 32.32 */
    int      channels, sample_rate;
    /* Streams */
    vr_stream_fill fill;
    void          *user;
    int16_t       *buf;
    uint64_t       buf_first; /* absolute frame index of buf[0] */
    size_t         buf_count;
    int            ended;
} voice;

static struct {
    int   ready;
    int   rate;
    int   max_voices;
    float master;
    voice voices[MAX_VOICES];
} g;

/* ---- Voice helpers (call with the lock held) ---------------------------------------- */

static voice *lookup(vr_voice id)
{
    unsigned index = id & 0xFFu, gen = (id >> 8) & 0xFFu;
    voice *v;

    if (!g.ready || !id || index == 0 || (int)index > g.max_voices)
        return NULL;
    v = &g.voices[index - 1];
    return v->active && v->generation == gen ? v : NULL;
}

static void release(voice *v)
{
    free(v->buf);
    v->buf = NULL;
    v->active = 0;
    v->sound = NULL;
    v->fill = NULL;
}

static vr_voice start_voice(vr_sound *sound, const vr_stream_desc *stream,
                            const vr_play_params *params)
{
    vr_play_params def;
    vr_voice id = 0;
    int i;

    if (!params) {
        vr_play_params_default(&def);
        params = &def;
    }
    if (!(params->pitch > 0.0f))
        return 0;
    vr__audio_lock();
    for (i = 0; i < g.max_voices; i++) {
        voice *v = &g.voices[i];
        if (v->active)
            continue;
        if (stream) {
            v->buf = malloc(STREAM_FRAMES * (size_t)stream->channels * sizeof(int16_t));
            if (!v->buf)
                break;
            v->fill = stream->fill;
            v->user = stream->user;
            v->channels = stream->channels;
            v->sample_rate = stream->sample_rate;
            v->buf_first = 0;
            v->buf_count = 0;
            v->ended = 0;
            v->sound = NULL;
        } else {
            v->sound = sound;
            v->channels = sound->channels;
            v->sample_rate = sound->sample_rate;
        }
        v->generation++;
        v->active = 1;
        v->paused = params->paused != 0;
        v->loop = stream ? 0 : params->loop != 0;
        v->volume = params->volume;
        v->pan = params->pan;
        v->pitch = params->pitch;
        v->pos = 0;
        id = (vr_voice)(i + 1) | ((vr_voice)v->generation << 8);
        break;
    }
    vr__audio_unlock();
    return id;
}

/* Fetches source frame idx into lr[2]. Returns 0 if past the end. */
static int sound_frame(const voice *v, uint64_t idx, float lr[2])
{
    const vr_sound *s = v->sound;
    const int16_t *p;

    if (idx >= s->frames) {
        if (!v->loop)
            return 0;
        idx %= s->frames;
    }
    p = s->data + idx * (uint64_t)s->channels;
    lr[0] = p[0];
    lr[1] = s->channels == 2 ? p[1] : p[0];
    return 1;
}

static int stream_frame(voice *v, uint64_t idx, float lr[2])
{
    const int16_t *p;

    while (idx >= v->buf_first + v->buf_count && !v->ended) {
        /* Keep frames from idx - 1 on (for interpolation), refill the rest. */
        uint64_t keep_from = idx > v->buf_first ? idx - 1 : v->buf_first;
        size_t drop = (size_t)(keep_from - v->buf_first), want, got;
        size_t ch = (size_t)v->channels;

        if (drop > v->buf_count)
            drop = v->buf_count;
        memmove(v->buf, v->buf + drop * ch, (v->buf_count - drop) * ch * sizeof(int16_t));
        v->buf_count -= drop;
        v->buf_first += drop;
        if (v->buf_first + v->buf_count < keep_from) {
            /* Skipped ahead (high pitch): discard everything buffered. */
            v->buf_first = keep_from;
            v->buf_count = 0;
        }
        want = STREAM_FRAMES - v->buf_count;
        got = v->fill(v->user, v->buf + v->buf_count * ch, want);
        if (got > want)
            got = want;
        v->buf_count += got;
        if (got < want)
            v->ended = 1;
    }
    if (idx < v->buf_first || idx >= v->buf_first + v->buf_count)
        return 0;
    p = v->buf + (size_t)(idx - v->buf_first) * (size_t)v->channels;
    lr[0] = p[0];
    lr[1] = v->channels == 2 ? p[1] : p[0];
    return 1;
}

static void mix_voice(voice *v, float *acc, size_t frames)
{
    uint64_t step = (uint64_t)((double)v->pitch * v->sample_rate / g.rate * FP_ONE + 0.5);
    float vol = v->volume * g.master;
    float gl = vol * (v->pan > 0.0f ? 1.0f - v->pan : 1.0f);
    float gr = vol * (v->pan < 0.0f ? 1.0f + v->pan : 1.0f);
    size_t f;

    for (f = 0; f < frames; f++) {
        uint64_t idx = v->pos >> 32;
        float t = (float)((double)(v->pos & 0xFFFFFFFFu) / FP_ONE);
        float a[2], b[2];
        int ok = v->sound ? sound_frame(v, idx, a) : stream_frame(v, idx, a);

        if (!ok) {
            release(v);
            return;
        }
        if (!(v->sound ? sound_frame(v, idx + 1, b) : stream_frame(v, idx + 1, b))) {
            b[0] = a[0];
            b[1] = a[1];
        }
        acc[2 * f] += (a[0] + (b[0] - a[0]) * t) * gl;
        acc[2 * f + 1] += (a[1] + (b[1] - a[1]) * t) * gr;
        v->pos += step;
        if (v->loop && v->sound && (v->pos >> 32) >= v->sound->frames)
            v->pos -= (uint64_t)v->sound->frames << 32;
    }
}

/* ---- Public API ---------------------------------------------------------------------- */

vr_result vr_audio_init(const vr_audio_desc *desc)
{
    int rate = desc && desc->sample_rate ? desc->sample_rate : 48000;
    int voices = desc && desc->max_voices ? desc->max_voices : 32;
    vr_result r;

    if (rate < 8000 || rate > 192000 || voices < 1 || voices > MAX_VOICES)
        return VR_ERR_INVALID_ARG;
    if (g.ready)
        vr_audio_shutdown();
    memset(&g, 0, sizeof(g));
    g.rate = rate;
    g.max_voices = voices;
    g.master = 1.0f;
    g.ready = 1;
    r = vr__audio_backend_start(rate);
    if (r != VR_OK)
        g.ready = 0;
    return r;
}

void vr_audio_shutdown(void)
{
    int i;

    if (!g.ready)
        return;
    vr__audio_backend_stop();
    for (i = 0; i < MAX_VOICES; i++)
        if (g.voices[i].active)
            release(&g.voices[i]);
    g.ready = 0;
}

void vr_audio_set_master_volume(float volume)
{
    vr__audio_lock();
    g.master = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    vr__audio_unlock();
}

void vr__audio_mix_locked(int16_t *out, size_t frames)
{
    float acc[512 * 2];
    size_t done = 0, i;
    int v;

    while (done < frames) {
        size_t n = frames - done > 512 ? 512 : frames - done;

        memset(acc, 0, n * 2 * sizeof(float));
        if (g.ready)
            for (v = 0; v < g.max_voices; v++)
                if (g.voices[v].active && !g.voices[v].paused)
                    mix_voice(&g.voices[v], acc, n);
        for (i = 0; i < n * 2; i++) {
            float s = acc[i];
            out[done * 2 + i] = (int16_t)(s > 32767.0f ? 32767 : (s < -32768.0f ? -32768 : (int)s));
        }
        done += n;
    }
}

void vr_audio_render(int16_t *out, size_t frames)
{
    if (!out)
        return;
    vr__audio_lock();
    vr__audio_mix_locked(out, frames);
    vr__audio_unlock();
}

void vr_play_params_default(vr_play_params *p)
{
    if (!p)
        return;
    p->volume = 1.0f;
    p->pan = 0.0f;
    p->pitch = 1.0f;
    p->loop = 0;
    p->paused = 0;
}

vr_voice vr_audio_play(vr_sound *sound, const vr_play_params *params)
{
    if (!g.ready || !sound || !sound->frames)
        return 0;
    return start_voice(sound, NULL, params);
}

vr_voice vr_audio_play_stream(const vr_stream_desc *desc, const vr_play_params *params)
{
    if (!g.ready || !desc || !desc->fill || (desc->channels != 1 && desc->channels != 2) ||
        desc->sample_rate <= 0)
        return 0;
    return start_voice(NULL, desc, params);
}

#define WITH_VOICE(id, body)                \
    do {                                    \
        voice *v_;                          \
        vr__audio_lock();                   \
        v_ = lookup(id);                    \
        if (v_) {                           \
            body;                           \
        }                                   \
        vr__audio_unlock();                 \
    } while (0)

void vr_voice_stop(vr_voice id)
{
    WITH_VOICE(id, release(v_));
}

void vr_voice_set_paused(vr_voice id, int paused)
{
    WITH_VOICE(id, v_->paused = paused != 0);
}

void vr_voice_set_volume(vr_voice id, float volume)
{
    WITH_VOICE(id, v_->volume = volume);
}

void vr_voice_set_pan(vr_voice id, float pan)
{
    WITH_VOICE(id, v_->pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan));
}

void vr_voice_set_pitch(vr_voice id, float pitch)
{
    WITH_VOICE(id, if (pitch > 0.0f) v_->pitch = pitch);
}

int vr_voice_is_playing(vr_voice id)
{
    int playing = 0;
    WITH_VOICE(id, playing = 1);
    return playing;
}

/* ---- Sounds -------------------------------------------------------------------------- */

static vr_result sound_alloc(int channels, int rate, size_t frames, vr_sound **out)
{
    vr_sound *s;

    if (!out || (channels != 1 && channels != 2) || rate <= 0 || !frames ||
        frames > SIZE_MAX / 4)
        return VR_ERR_INVALID_ARG;
    s = malloc(sizeof(*s));
    if (!s)
        return VR_ERR_NO_MEMORY;
    s->data = malloc(frames * (size_t)channels * sizeof(int16_t));
    if (!s->data) {
        free(s);
        return VR_ERR_NO_MEMORY;
    }
    s->channels = channels;
    s->sample_rate = rate;
    s->frames = frames;
    *out = s;
    return VR_OK;
}

vr_result vr_sound_create(const vr_sound_desc *desc, vr_sound **out)
{
    vr_result r;

    if (!desc || !desc->data)
        return VR_ERR_INVALID_ARG;
    r = sound_alloc(desc->channels, desc->sample_rate, desc->frames, out);
    if (r == VR_OK)
        memcpy((*out)->data, desc->data, desc->frames * (size_t)desc->channels * sizeof(int16_t));
    return r;
}

void vr_sound_destroy(vr_sound *sound)
{
    int i;

    if (!sound)
        return;
    vr__audio_lock();
    for (i = 0; i < MAX_VOICES; i++)
        if (g.voices[i].active && g.voices[i].sound == sound)
            release(&g.voices[i]);
    vr__audio_unlock();
    free(sound->data);
    free(sound);
}

size_t vr_sound_frames(const vr_sound *sound)
{
    return sound ? sound->frames : 0;
}

/* ---- WAV ------------------------------------------------------------------------------ */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int16_t clamp16(int v)
{
    return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

static const int ima_steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
    60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767
};

static int16_t ima_nibble(int *pred, int *index, unsigned n)
{
    int step = ima_steps[*index], diff = step >> 3;
    static const int adj[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

    if (n & 1) diff += step >> 2;
    if (n & 2) diff += step >> 1;
    if (n & 4) diff += step;
    *pred = clamp16((n & 8) ? *pred - diff : *pred + diff);
    *index += adj[n & 7];
    *index = *index < 0 ? 0 : (*index > 88 ? 88 : *index);
    return (int16_t)*pred;
}

/* Decodes one IMA ADPCM block of len bytes; returns frames written. */
static size_t ima_block(const uint8_t *b, size_t len, int ch, size_t max_frames, int16_t *out)
{
    int pred[2], index[2], c;
    size_t frames = 1, pos = 4 * (size_t)ch;

    if (len < pos || !max_frames)
        return 0;
    for (c = 0; c < ch; c++) {
        pred[c] = (int16_t)le16(b + 4 * c);
        index[c] = b[4 * c + 2] > 88 ? 88 : b[4 * c + 2];
        out[c] = (int16_t)pred[c];
    }
    /* Groups of 4 bytes (8 samples) per channel, interleaved by channel. */
    while (pos + 4 * (size_t)ch <= len && frames + 8 <= max_frames) {
        for (c = 0; c < ch; c++) {
            int k;
            for (k = 0; k < 8; k++) {
                unsigned byte = b[pos + 4 * (size_t)c + (size_t)(k / 2)];
                unsigned n = (k & 1) ? byte >> 4 : byte & 15u;
                out[(frames + (size_t)k) * (size_t)ch + (size_t)c] = ima_nibble(&pred[c], &index[c], n);
            }
        }
        pos += 4 * (size_t)ch;
        frames += 8;
    }
    return frames;
}

static const int ms_adapt[16] = { 230, 230, 230, 230, 307, 409, 512, 614,
                                  768, 614, 512, 409, 307, 230, 230, 230 };

static size_t ms_block(const uint8_t *b, size_t len, int ch, const int16_t (*coef)[2], int ncoef,
                       size_t max_frames, int16_t *out)
{
    int c1[2], c2[2], delta[2], s1[2], s2[2], c;
    size_t frames, pos, hdr = 7 * (size_t)ch;

    if (len < hdr || max_frames < 2)
        return 0;
    for (c = 0; c < ch; c++) {
        int pi = b[c] < ncoef ? b[c] : 0;
        c1[c] = coef[pi][0];
        c2[c] = coef[pi][1];
        delta[c] = (int16_t)le16(b + ch + 2 * c);
        s1[c] = (int16_t)le16(b + 3 * ch + 2 * c);
        s2[c] = (int16_t)le16(b + 5 * ch + 2 * c);
        out[c] = (int16_t)s2[c];
        out[ch + c] = (int16_t)s1[c];
    }
    frames = 2;
    pos = hdr;
    c = 0;
    while (pos < len && frames < max_frames) {
        unsigned byte = b[pos++];
        int half;
        for (half = 0; half < 2; half++) {
            unsigned n = half ? byte & 15u : byte >> 4;
            int sn = n >= 8 ? (int)n - 16 : (int)n;
            int pred = (s1[c] * c1[c] + s2[c] * c2[c]) / 256 + sn * delta[c];
            pred = clamp16(pred);
            s2[c] = s1[c];
            s1[c] = pred;
            delta[c] = (ms_adapt[n] * delta[c]) / 256;
            if (delta[c] < 16)
                delta[c] = 16;
            out[frames * (size_t)ch + (size_t)c] = (int16_t)pred;
            if (++c == ch) {
                c = 0;
                if (++frames >= max_frames)
                    break;
            }
        }
    }
    return frames;
}

vr_result vr_sound_load_wav(const void *data, size_t size, vr_sound **out)
{
    static const int16_t ms_default[7][2] = {
        { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 }, { 240, 0 }, { 460, -208 }, { 392, -232 }
    };
    const uint8_t *p = data, *fmt = NULL, *pcm = NULL;
    size_t pos = 12, fmt_len = 0, pcm_len = 0, frames, fact = 0, done = 0;
    unsigned tag, ch, rate, align, bits;
    int16_t coef[32][2];
    int ncoef = 7;
    vr_sound *s;
    vr_result r;

    if (!data || !out || size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
        return VR_ERR_INVALID_ARG;
    while (pos + 8 <= size) {
        size_t len = le32(p + pos + 4), body = pos + 8;
        if (len > size - body)
            len = size - body; /* tolerate truncated files */
        if (!memcmp(p + pos, "fmt ", 4)) {
            fmt = p + body;
            fmt_len = len;
        } else if (!memcmp(p + pos, "data", 4)) {
            pcm = p + body;
            pcm_len = len;
        } else if (!memcmp(p + pos, "fact", 4) && len >= 4) {
            fact = le32(p + body);
        }
        pos = body + len + (len & 1);
    }
    if (!fmt || fmt_len < 16 || !pcm)
        return VR_ERR_INVALID_ARG;
    tag = le16(fmt);
    ch = le16(fmt + 2);
    rate = le32(fmt + 4);
    align = le16(fmt + 12);
    bits = le16(fmt + 14);
    if (tag == 0xFFFE && fmt_len >= 26)
        tag = le16(fmt + 24); /* WAVE_FORMAT_EXTENSIBLE sub-format */
    if ((ch != 1 && ch != 2) || !rate || !align)
        return VR_ERR_INVALID_ARG;

    if (tag == 1) { /* PCM */
        size_t i, n;
        if (bits != 8 && bits != 16)
            return VR_ERR_UNSUPPORTED;
        frames = pcm_len / (ch * (bits / 8));
        r = sound_alloc((int)ch, (int)rate, frames, &s);
        if (r != VR_OK)
            return r;
        n = frames * ch;
        for (i = 0; i < n; i++)
            s->data[i] = bits == 8 ? (int16_t)((pcm[i] - 128) << 8) : (int16_t)le16(pcm + 2 * i);
        *out = s;
        return VR_OK;
    }
    if (tag == 0x11 || tag == 2) { /* IMA ADPCM, MS ADPCM */
        size_t per_block, blocks, b;
        if (bits != 4)
            return VR_ERR_UNSUPPORTED;
        if (tag == 0x11) {
            if (align <= 4 * ch)
                return VR_ERR_INVALID_ARG;
            per_block = (align - 4 * ch) * 2 / ch + 1;
        } else {
            if (align <= 7 * ch)
                return VR_ERR_INVALID_ARG;
            per_block = (align - 7 * ch) * 2 / ch + 2;
            if (fmt_len >= 22) {
                unsigned n = le16(fmt + 20), i;
                if (n >= 1 && n <= 32 && fmt_len >= 22 + 4 * (size_t)n) {
                    for (i = 0; i < n; i++) {
                        coef[i][0] = (int16_t)le16(fmt + 22 + 4 * i);
                        coef[i][1] = (int16_t)le16(fmt + 24 + 4 * i);
                    }
                    ncoef = (int)n;
                } else {
                    memcpy(coef, ms_default, sizeof(ms_default));
                }
            } else {
                memcpy(coef, ms_default, sizeof(ms_default));
            }
        }
        blocks = (pcm_len + align - 1) / align;
        frames = blocks * per_block;
        if (fact && fact < frames)
            frames = fact;
        if (!frames)
            return VR_ERR_INVALID_ARG;
        r = sound_alloc((int)ch, (int)rate, frames, &s);
        if (r != VR_OK)
            return r;
        for (b = 0; b < blocks && done < frames; b++) {
            size_t len = pcm_len - b * align < align ? pcm_len - b * align : align;
            int16_t *dst = s->data + done * ch;
            done += tag == 0x11 ? ima_block(pcm + b * align, len, (int)ch, frames - done, dst)
                                : ms_block(pcm + b * align, len, (int)ch,
                                           (const int16_t(*)[2])coef, ncoef, frames - done, dst);
        }
        if (!done) {
            vr_sound_destroy(s);
            return VR_ERR_INVALID_ARG;
        }
        s->frames = done;
        *out = s;
        return VR_OK;
    }
    return VR_ERR_UNSUPPORTED;
}
