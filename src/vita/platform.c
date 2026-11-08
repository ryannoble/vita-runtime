#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "vr/vr_platform.h"
#include "../internal.h"

static uint64_t g_start_us;
static char g_app_name[VR_MAX_PATH] = "vita-runtime";
static char g_data_root[VR_MAX_PATH] = "ux0:data/vita-runtime";
static SceUID g_log_file = -1;

vr_result vr_platform_init(const vr_platform_desc *desc)
{
    const char *app = desc && desc->app_name ? desc->app_name : "vita-runtime";
    const char *root = desc ? desc->data_root : NULL;
    char data_root[VR_MAX_PATH];
    char log_path[VR_MAX_PATH];
    int len;

    if (!*app || strlen(app) >= sizeof(g_app_name))
        return VR_ERR_INVALID_ARG;
    if (root) {
        if (!*root || strlen(root) >= sizeof(data_root))
            return VR_ERR_INVALID_ARG;
        memcpy(data_root, root, strlen(root) + 1);
    } else {
        /* The application name must be a single directory component. */
        if (strpbrk(app, "/\\:") || !strcmp(app, ".") || !strcmp(app, ".."))
            return VR_ERR_INVALID_ARG;
        len = snprintf(data_root, sizeof(data_root), "ux0:data/%s", app);
        if (len < 0 || (size_t)len >= sizeof(data_root))
            return VR_ERR_INVALID_ARG;
    }
    len = snprintf(log_path, sizeof(log_path), "%s/log.txt", data_root);
    if (len < 0 || (size_t)len >= sizeof(log_path))
        return VR_ERR_INVALID_ARG;

    if (g_log_file >= 0)
        sceIoClose(g_log_file);
    memcpy(g_app_name, app, strlen(app) + 1);
    memcpy(g_data_root, data_root, strlen(data_root) + 1);
    /* Failure is non-fatal: console logging remains available. */
    sceIoMkdir(g_data_root, 0777);
    g_log_file = sceIoOpen(log_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND,
                           0666);
    g_start_us = sceKernelGetProcessTimeWide();
    return VR_OK;
}

void vr_platform_shutdown(void)
{
    vr__input_shutdown();
    if (g_log_file >= 0) {
        sceIoClose(g_log_file);
        g_log_file = -1;
    }
}

uint64_t vr_time_us(void)
{
    return sceKernelGetProcessTimeWide() - g_start_us;
}

void vr_sleep_us(uint64_t us)
{
    while (us) {
        unsigned int chunk = us > UINT32_MAX ? UINT32_MAX : (unsigned int)us;
        if (sceKernelDelayThread(chunk) < 0)
            return;
        us -= chunk;
    }
}

void vr_log(vr_log_level level, const char *fmt, ...)
{
    static const char *names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    char message[2048];
    const char *name = (unsigned int)level < 4 ? names[level] : "UNKNOWN";
    va_list args;
    size_t used;
    int len;

    if (!fmt)
        return;
    len = snprintf(message, sizeof(message), "[%s] %s: ", g_app_name, name);
    if (len < 0)
        return;
    used = (size_t)len;
    if (used >= sizeof(message) - 2)
        used = sizeof(message) - 2;
    va_start(args, fmt);
    vsnprintf(message + used, sizeof(message) - used - 1, fmt, args);
    va_end(args);
    used = strlen(message);
    message[used++] = '\n';
    message[used] = '\0';
    sceClibPrintf("%s", message);
    if (g_log_file >= 0) {
        size_t written = 0;
        while (written < used) {
            int count = sceIoWrite(g_log_file, message + written, used - written);
            if (count <= 0)
                break;
            written += (size_t)count;
        }
    }
}

const char *vr__data_root(void)
{
    return g_data_root;
}
