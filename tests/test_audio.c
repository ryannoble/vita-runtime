/* Mixer and WAV decoding tests, using the host backend without a device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vr/vr_audio.h"

static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static int16_t g_out[4096 * 2];

static vr_sound *constant_sound(int16_t value, size_t frames, int rate)
{
    int16_t *pcm = malloc(frames * sizeof(int16_t));
    vr_sound_desc d = { 1, rate, frames, pcm };
    vr_sound *s = NULL;
    size_t i;

    for (i = 0; i < frames; i++)
        pcm[i] = value;
    CHECK(vr_sound_create(&d, &s) == VR_OK);
    free(pcm);
    return s;
}

static void test_basic_mix(void)
{
    vr_sound *s = constant_sound(1000, 10, 48000);
    vr_play_params p;
    vr_voice v;

    vr_audio_render(g_out, 4);
    CHECK(g_out[0] == 0 && g_out[7] == 0);

    v = vr_audio_play(s, NULL);
    CHECK(v != 0 && vr_voice_is_playing(v));
    vr_audio_render(g_out, 4);
    CHECK(g_out[0] == 1000 && g_out[1] == 1000);

    vr_voice_set_pan(v, 1.0f);
    vr_voice_set_volume(v, 0.5f);
    vr_audio_render(g_out, 2);
    CHECK(g_out[0] == 0 && g_out[1] == 500);

    /* 4 of 10 frames remain; the rest is silence and the voice ends. */
    vr_audio_render(g_out, 10);
    CHECK(g_out[7] == 500 && g_out[8] == 0 && g_out[9] == 0);
    CHECK(!vr_voice_is_playing(v));
    vr_voice_set_volume(v, 1.0f); /* stale handle: ignored */

    vr_play_params_default(&p);
    p.loop = 1;
    v = vr_audio_play(s, &p);
    vr_audio_render(g_out, 25);
    CHECK(g_out[48] == 1000 && vr_voice_is_playing(v));
    vr_voice_set_paused(v, 1);
    vr_audio_render(g_out, 2);
    CHECK(g_out[0] == 0 && vr_voice_is_playing(v));
    vr_voice_stop(v);
    CHECK(!vr_voice_is_playing(v));
    vr_sound_destroy(s);
}

static void test_resample_and_clip(void)
{
    int16_t ramp[8] = { 0, 100, 200, 300, 400, 500, 600, 700 };
    vr_sound_desc d = { 1, 24000, 8, ramp };
    vr_sound *s = NULL, *loud;
    vr_play_params p;
    int i;

    CHECK(vr_sound_create(&d, &s) == VR_OK);
    vr_audio_play(s, NULL); /* half rate: interpolates between samples */
    vr_audio_render(g_out, 6);
    for (i = 0; i < 6; i++)
        CHECK(g_out[2 * i] == 50 * i);
    vr_audio_render(g_out, 32);

    vr_play_params_default(&p);
    p.pitch = 4.0f; /* 24 kHz * 4 / 48 kHz = 2 source frames per output frame */
    vr_audio_play(s, &p);
    vr_audio_render(g_out, 5);
    CHECK(g_out[0] == 0 && g_out[2] == 200 && g_out[6] == 600 && g_out[8] == 0);
    vr_sound_destroy(s);

    loud = constant_sound(30000, 4, 48000);
    vr_audio_play(loud, NULL);
    vr_audio_play(loud, NULL);
    vr_audio_render(g_out, 2);
    CHECK(g_out[0] == 32767);
    vr_audio_set_master_volume(0.5f);
    vr_audio_render(g_out, 2);
    CHECK(g_out[0] == 30000);
    vr_audio_set_master_volume(1.0f);
    vr_sound_destroy(loud);
}

typedef struct {
    int16_t next;
    size_t  remaining;
} ramp_stream;

static size_t ramp_fill(void *user, int16_t *frames, size_t count)
{
    ramp_stream *r = user;
    size_t i, n = count < r->remaining ? count : r->remaining;

    for (i = 0; i < n; i++) {
        frames[2 * i] = r->next;
        frames[2 * i + 1] = (int16_t)-r->next;
        r->next++;
    }
    r->remaining -= n;
    return n;
}

static void test_stream(void)
{
    ramp_stream r = { 0, 3000 };
    vr_stream_desc d = { 2, 48000, ramp_fill, &r };
    vr_voice v = vr_audio_play_stream(&d, NULL);
    int i, ok = 1;

    CHECK(v != 0);
    vr_audio_render(g_out, 3500); /* spans several 1024-frame refills */
    for (i = 0; i < 3000; i++)
        if (g_out[2 * i] != i || g_out[2 * i + 1] != -i)
            ok = 0;
    CHECK(ok);
    CHECK(g_out[2 * 3000] == 0);
    CHECK(!vr_voice_is_playing(v));
}

static void test_voice_limit(void)
{
    vr_audio_desc desc = { 48000, 2 };
    vr_sound *s;
    vr_voice a, b, c;

    vr_audio_shutdown();
    CHECK(vr_audio_init(&desc) == VR_OK);
    s = constant_sound(1, 100, 48000);
    a = vr_audio_play(s, NULL);
    b = vr_audio_play(s, NULL);
    c = vr_audio_play(s, NULL);
    CHECK(a && b && !c);
    vr_voice_stop(a);
    c = vr_audio_play(s, NULL); /* reuses a's slot with a new generation */
    CHECK(c && c != a && !vr_voice_is_playing(a) && vr_voice_is_playing(c));
    vr_sound_destroy(s); /* stops b and c */
    CHECK(!vr_voice_is_playing(b) && !vr_voice_is_playing(c));
}

static size_t wav(uint8_t *buf, unsigned tag, unsigned ch, unsigned rate, unsigned align,
                  unsigned bits, const uint8_t *extra, size_t extra_len, const void *data,
                  size_t len)
{
    size_t fmt_len = 16 + (extra_len ? 2 + extra_len : 0), n = 0;
    unsigned bps = rate * align;

#define PUT16(v) (buf[n] = (uint8_t)(v), buf[n + 1] = (uint8_t)((v) >> 8), n += 2)
#define PUT32(v) (PUT16((v) & 0xFFFFu), PUT16((v) >> 16))
    memcpy(buf, "RIFF", 4); n = 4;
    PUT32((unsigned)(4 + 8 + fmt_len + 8 + len));
    memcpy(buf + n, "WAVEfmt ", 8); n += 8;
    PUT32((unsigned)fmt_len);
    PUT16(tag); PUT16(ch); PUT32(rate); PUT32(bps); PUT16(align); PUT16(bits);
    if (extra_len) {
        PUT16((unsigned)extra_len);
        memcpy(buf + n, extra, extra_len);
        n += extra_len;
    }
    memcpy(buf + n, "data", 4); n += 4;
    PUT32((unsigned)len);
    memcpy(buf + n, data, len);
    return n + len;
#undef PUT16
#undef PUT32
}

static void decode_all(vr_sound *s, size_t frames, int16_t *out)
{
    vr_audio_desc desc = { 48000, 4 };
    vr_audio_shutdown();
    vr_audio_init(&desc);
    vr_audio_play(s, NULL);
    vr_audio_render(g_out, frames);
    for (size_t i = 0; i < frames; i++)
        out[i] = g_out[2 * i];
}

static void test_wav(void)
{
    uint8_t file[256];
    int16_t out[16];
    vr_sound *s = NULL;
    size_t n;

    {
        const uint8_t pcm16[6] = { 0x10, 0x00, 0xF0, 0xFF, 0x00, 0x40 };
        n = wav(file, 1, 1, 48000, 2, 16, NULL, 0, pcm16, sizeof(pcm16));
        CHECK(vr_sound_load_wav(file, n, &s) == VR_OK);
        CHECK(vr_sound_frames(s) == 3);
        decode_all(s, 3, out);
        CHECK(out[0] == 16 && out[1] == -16 && out[2] == 0x4000);
        vr_sound_destroy(s);
    }
    {
        const uint8_t pcm8[2] = { 128, 255 };
        n = wav(file, 1, 1, 48000, 1, 8, NULL, 0, pcm8, sizeof(pcm8));
        CHECK(vr_sound_load_wav(file, n, &s) == VR_OK);
        decode_all(s, 2, out);
        CHECK(out[0] == 0 && out[1] == 127 << 8);
        vr_sound_destroy(s);
    }
    {
        /* IMA: predictor 0, index 0; nibbles 4, 0, 0, ... -> 0, 7, 8, 9, 9, ... */
        const uint8_t ima[8] = { 0, 0, 0, 0, 0x04, 0, 0, 0 };
        const uint8_t extra[2] = { 9, 0 };
        n = wav(file, 0x11, 1, 48000, 8, 4, extra, 2, ima, sizeof(ima));
        CHECK(vr_sound_load_wav(file, n, &s) == VR_OK);
        CHECK(vr_sound_frames(s) == 9);
        decode_all(s, 9, out);
        CHECK(out[0] == 0 && out[1] == 7 && out[2] == 8 && out[3] == 9 && out[8] == 9);
        vr_sound_destroy(s);
    }
    {
        /* MS ADPCM: coef 0 (256, 0), delta 16, s1 100, s2 50, nibbles 1, 1. */
        const uint8_t ms[8] = { 0, 16, 0, 100, 0, 50, 0, 0x11 };
        const uint8_t extra[4] = { 4, 0, 0, 0 }; /* samples per block, 0 coefs */
        n = wav(file, 2, 1, 48000, 8, 4, extra, 4, ms, sizeof(ms));
        CHECK(vr_sound_load_wav(file, n, &s) == VR_OK);
        CHECK(vr_sound_frames(s) == 4);
        decode_all(s, 4, out);
        CHECK(out[0] == 50 && out[1] == 100 && out[2] == 116 && out[3] == 132);
        vr_sound_destroy(s);
    }
    CHECK(vr_sound_load_wav("RIFFxxxxWAVE", 12, &s) == VR_ERR_INVALID_ARG);
    n = wav(file, 0x55, 1, 48000, 1, 8, NULL, 0, "x", 1); /* MP3: unsupported */
    CHECK(vr_sound_load_wav(file, n, &s) == VR_ERR_UNSUPPORTED);
}

int main(void)
{
    CHECK(vr_audio_play(NULL, NULL) == 0);
    CHECK(vr_audio_init(NULL) == VR_OK);
    test_basic_mix();
    test_resample_and_clip();
    test_stream();
    test_voice_limit();
    test_wav();
    vr_audio_shutdown();

    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all audio tests passed\n");
    return 0;
}
