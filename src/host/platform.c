#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "vr/vr_platform.h"
#include "../internal.h"

static uint64_t    g_start_us;
static const char *g_app_name = "vita-runtime";
static char        g_data_root[VR_MAX_PATH] = ".";

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

vr_result vr_platform_init(const vr_platform_desc *desc)
{
    if (desc && desc->app_name)
        g_app_name = desc->app_name;
    if (desc && desc->data_root) {
        size_t len = strlen(desc->data_root);
        if (len == 0 || len >= sizeof(g_data_root))
            return VR_ERR_INVALID_ARG;
        memcpy(g_data_root, desc->data_root, len + 1);
    }
    g_start_us = now_us();
    return VR_OK;
}

void vr_platform_shutdown(void)
{
    vr__input_shutdown();
}

uint64_t vr_time_us(void)
{
    return now_us() - g_start_us;
}

void vr_sleep_us(uint64_t us)
{
    struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000 };
    nanosleep(&ts, NULL);
}

void vr_log(vr_log_level level, const char *fmt, ...)
{
    static const char *names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    va_list args;

    fprintf(stderr, "[%s] %s: ", g_app_name, names[level]);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

const char *vr__data_root(void)
{
    return g_data_root;
}
