#ifndef VR_FS_H
#define VR_FS_H

#include <stddef.h>
#include <stdint.h>

#include "vr_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Paths are relative to the data root given to vr_platform_init and use '/'.
 * Absolute paths and ".." components are rejected. */

typedef struct vr_file vr_file;

typedef enum { VR_SEEK_SET, VR_SEEK_CUR, VR_SEEK_END } vr_seek_origin;

vr_result vr_file_open(const char *path, vr_file **out);
void      vr_file_close(vr_file *file);
vr_result vr_file_read(vr_file *file, void *buf, size_t size, size_t *bytes_read);
vr_result vr_file_seek(vr_file *file, int64_t offset, vr_seek_origin origin);
int64_t   vr_file_tell(vr_file *file);
int64_t   vr_file_size(vr_file *file);

#ifdef __cplusplus
}
#endif

#endif
