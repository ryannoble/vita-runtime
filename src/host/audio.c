/* Host audio backend. Without SDL2 there is no device: playback advances
 * only when the application calls vr_audio_render. With VR_HOST_SDL2 the
 * mixer feeds the default SDL audio device (set VR_HEADLESS=1 to disable). */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdlib.h>

#include "../audio_internal.h"

#ifdef VR_HOST_SDL2
#include <SDL.h>
static SDL_AudioDeviceID g_device;
#endif

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

void vr__audio_lock(void)
{
    pthread_mutex_lock(&g_lock);
}

void vr__audio_unlock(void)
{
    pthread_mutex_unlock(&g_lock);
}

#ifdef VR_HOST_SDL2
static void sdl_callback(void *user, Uint8 *stream, int len)
{
    (void)user;
    vr__audio_lock();
    vr__audio_mix_locked((int16_t *)stream, (size_t)len / 4);
    vr__audio_unlock();
}
#endif

vr_result vr__audio_backend_start(int rate)
{
#ifdef VR_HOST_SDL2
    const char *headless = getenv("VR_HEADLESS");
    SDL_AudioSpec want, have;

    if ((headless && *headless && *headless != '0') || SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return VR_OK; /* run without a device */
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = sdl_callback;
    g_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_device) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return VR_OK;
    }
    SDL_PauseAudioDevice(g_device, 0);
#else
    (void)rate;
#endif
    return VR_OK;
}

void vr__audio_backend_stop(void)
{
#ifdef VR_HOST_SDL2
    if (g_device) {
        SDL_CloseAudioDevice(g_device);
        g_device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
#endif
}
