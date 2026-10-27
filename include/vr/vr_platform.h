#ifndef VR_PLATFORM_H
#define VR_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VR_OK = 0,
    VR_ERR_INVALID_ARG = -1,
    VR_ERR_NOT_FOUND = -2,
    VR_ERR_IO = -3,
    VR_ERR_NO_MEMORY = -4,
    VR_ERR_UNSUPPORTED = -5
} vr_result;

typedef enum { VR_LOG_DEBUG, VR_LOG_INFO, VR_LOG_WARN, VR_LOG_ERROR } vr_log_level;

typedef struct {
    const char *app_name;  /* used in log output */
    const char *data_root; /* root for vr_fs paths; NULL selects the backend default */
} vr_platform_desc;

vr_result vr_platform_init(const vr_platform_desc *desc);
void      vr_platform_shutdown(void);

/* Monotonic time since vr_platform_init. */
uint64_t vr_time_us(void);
void     vr_sleep_us(uint64_t us);

void vr_log(vr_log_level level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
