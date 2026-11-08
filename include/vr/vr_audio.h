#ifndef VR_AUDIO_H
#define VR_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "vr_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Software mixer with a fixed number of voices. Output is interleaved
 * stereo int16. On the Vita a thread feeds sceAudioOut; on the host the
 * default backend has no device and output is pulled with vr_audio_render
 * (an SDL2 device is used when built with VR_HOST_SDL2). */

typedef struct vr_sound vr_sound;
typedef uint32_t        vr_voice; /* 0 is never a valid voice */

typedef struct {
    int sample_rate; /* output rate; 0 selects 48000 */
    int max_voices;  /* 0 selects 32; at most 255 */
} vr_audio_desc;

vr_result vr_audio_init(const vr_audio_desc *desc);
void      vr_audio_shutdown(void);
void      vr_audio_set_master_volume(float volume); /* 0..1, default 1 */

/* Mixes frames of interleaved stereo into out. Used by backends; on the
 * host without a device, call it to advance playback (e.g. in tests). */
void vr_audio_render(int16_t *out, size_t frames);

/* ---- Sounds (PCM held in memory) --------------------------------------------------- */

typedef struct {
    int            channels;    /* 1 or 2 */
    int            sample_rate;
    size_t         frames;
    const int16_t *data;        /* interleaved; copied by vr_sound_create */
} vr_sound_desc;

vr_result vr_sound_create(const vr_sound_desc *desc, vr_sound **out);
/* Parses a RIFF WAVE file: 8/16-bit PCM, IMA ADPCM or MS ADPCM, mono or
 * stereo. The data is decoded to 16-bit PCM and copied. */
vr_result vr_sound_load_wav(const void *data, size_t size, vr_sound **out);
/* Stops any voices still playing the sound. */
void      vr_sound_destroy(vr_sound *sound);
size_t    vr_sound_frames(const vr_sound *sound);

/* ---- Voices -------------------------------------------------------------------------- */

typedef struct {
    float volume; /* 0..1 */
    float pan;    /* -1 (left) .. 1 (right) */
    float pitch;  /* playback-rate multiplier, > 0 */
    int   loop;
    int   paused; /* start paused */
} vr_play_params;

void vr_play_params_default(vr_play_params *p); /* 1, 0, 1, no loop, playing */

/* Returns 0 when no voice is free or on bad arguments. params may be NULL. */
vr_voice vr_audio_play(vr_sound *sound, const vr_play_params *params);

/* Streamed source: fill writes up to frame_count frames of interleaved
 * int16 (channels per frame) and returns how many it wrote. Returning fewer
 * ends the stream. It runs on the audio thread with the mixer locked, so it
 * must not call vr_audio functions. */
typedef size_t (*vr_stream_fill)(void *user, int16_t *frames, size_t frame_count);

typedef struct {
    int            channels;
    int            sample_rate;
    vr_stream_fill fill;
    void          *user;
} vr_stream_desc;

vr_voice vr_audio_play_stream(const vr_stream_desc *desc, const vr_play_params *params);

/* Operations on a voice that has finished or been stopped are ignored. */
void vr_voice_stop(vr_voice voice);
void vr_voice_set_paused(vr_voice voice, int paused);
void vr_voice_set_volume(vr_voice voice, float volume);
void vr_voice_set_pan(vr_voice voice, float pan);
void vr_voice_set_pitch(vr_voice voice, float pitch);
int  vr_voice_is_playing(vr_voice voice); /* nonzero while active (paused counts) */

#ifdef __cplusplus
}
#endif

#endif
