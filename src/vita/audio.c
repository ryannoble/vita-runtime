/* Vita audio backend: a high-priority thread mixes GRAIN frames at a time
 * and blocks in sceAudioOutOutput, which paces it to the hardware. */

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include "../audio_internal.h"

#define GRAIN 512

static SceUID       g_mutex = -1;
static SceUID       g_thread = -1;
static int          g_port = -1;
static volatile int g_running;
static int16_t      g_buf[2][GRAIN * 2];

void vr__audio_lock(void)
{
    if (g_mutex >= 0)
        sceKernelLockMutex(g_mutex, 1, NULL);
}

void vr__audio_unlock(void)
{
    if (g_mutex >= 0)
        sceKernelUnlockMutex(g_mutex, 1);
}

static int audio_thread(SceSize args, void *argp)
{
    int which = 0;

    (void)args;
    (void)argp;
    while (g_running) {
        vr__audio_lock();
        vr__audio_mix_locked(g_buf[which], GRAIN);
        vr__audio_unlock();
        sceAudioOutOutput(g_port, g_buf[which]);
        which ^= 1;
    }
    return 0;
}

vr_result vr__audio_backend_start(int rate)
{
    int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };

    /* The main port only runs at 48 kHz; the BGM port accepts 8..48 kHz. */
    if (rate > 48000)
        return VR_ERR_UNSUPPORTED;
    g_mutex = sceKernelCreateMutex("vr_audio_lock", 0, 0, NULL);
    if (g_mutex < 0)
        return VR_ERR_NO_MEMORY;
    g_port = sceAudioOutOpenPort(rate == 48000 ? SCE_AUDIO_OUT_PORT_TYPE_MAIN
                                               : SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                 GRAIN, rate, SCE_AUDIO_OUT_MODE_STEREO);
    if (g_port < 0) {
        vr__audio_backend_stop();
        return VR_ERR_UNSUPPORTED;
    }
    sceAudioOutSetVolume(g_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    g_running = 1;
    g_thread = sceKernelCreateThread("vr_audio", audio_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (g_thread < 0 || sceKernelStartThread(g_thread, 0, NULL) < 0) {
        vr__audio_backend_stop();
        return VR_ERR_NO_MEMORY;
    }
    return VR_OK;
}

void vr__audio_backend_stop(void)
{
    g_running = 0;
    if (g_thread >= 0) {
        sceKernelWaitThreadEnd(g_thread, NULL, NULL);
        sceKernelDeleteThread(g_thread);
        g_thread = -1;
    }
    if (g_port >= 0) {
        sceAudioOutReleasePort(g_port);
        g_port = -1;
    }
    if (g_mutex >= 0) {
        sceKernelDeleteMutex(g_mutex);
        g_mutex = -1;
    }
}
