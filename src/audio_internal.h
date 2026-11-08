#ifndef VR_AUDIO_INTERNAL_H
#define VR_AUDIO_INTERNAL_H

/* Contract between the shared mixer (audio_common.c) and an audio backend. */

#include <stddef.h>
#include <stdint.h>

#include "vr/vr_platform.h"

/* Backend: open the output device (or none) at rate Hz. The device thread
 * calls vr__audio_lock, vr__audio_mix_locked, vr__audio_unlock. */
vr_result vr__audio_backend_start(int rate);
void      vr__audio_backend_stop(void);
void      vr__audio_lock(void);
void      vr__audio_unlock(void);

/* Mixer: fills frames of interleaved stereo; the caller holds the lock. */
void vr__audio_mix_locked(int16_t *out, size_t frames);

#endif
