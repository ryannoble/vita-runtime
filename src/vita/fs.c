#include "vr/vr_fs.h"

/* Placeholder until the Vita filesystem backend is implemented. */
vr_result vr_file_open(const char *path, vr_file **out)
{
    (void)path;
    if (!out)
        return VR_ERR_INVALID_ARG;
    *out = NULL;
    return VR_ERR_UNSUPPORTED;
}

void vr_file_close(vr_file *file)
{
    (void)file;
}

vr_result vr_file_read(vr_file *file, void *buf, size_t size, size_t *bytes_read)
{
    (void)file;
    (void)buf;
    (void)size;
    if (bytes_read)
        *bytes_read = 0;
    return VR_ERR_UNSUPPORTED;
}

vr_result vr_file_seek(vr_file *file, int64_t offset, vr_seek_origin origin)
{
    (void)file;
    (void)offset;
    (void)origin;
    return VR_ERR_UNSUPPORTED;
}

int64_t vr_file_tell(vr_file *file)
{
    (void)file;
    return -1;
}

int64_t vr_file_size(vr_file *file)
{
    (void)file;
    return -1;
}
