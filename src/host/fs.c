#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "vr/vr_fs.h"
#include "../internal.h"

struct vr_file {
    FILE *fp;
};

vr_result vr_file_open(const char *path, vr_file **out)
{
    char full[VR_MAX_PATH];
    vr_file *file;
    FILE *fp;
    int n;

    if (!out || !vr__path_is_safe(path))
        return VR_ERR_INVALID_ARG;
    n = snprintf(full, sizeof(full), "%s/%s", vr__data_root(), path);
    if (n < 0 || (size_t)n >= sizeof(full))
        return VR_ERR_INVALID_ARG;

    fp = fopen(full, "rb");
    if (!fp)
        return VR_ERR_NOT_FOUND;
    file = malloc(sizeof(*file));
    if (!file) {
        fclose(fp);
        return VR_ERR_NO_MEMORY;
    }
    file->fp = fp;
    *out = file;
    return VR_OK;
}

void vr_file_close(vr_file *file)
{
    if (!file)
        return;
    fclose(file->fp);
    free(file);
}

vr_result vr_file_read(vr_file *file, void *buf, size_t size, size_t *bytes_read)
{
    size_t n;

    if (!file || (!buf && size))
        return VR_ERR_INVALID_ARG;
    n = fread(buf, 1, size, file->fp);
    if (bytes_read)
        *bytes_read = n;
    return (n < size && ferror(file->fp)) ? VR_ERR_IO : VR_OK;
}

vr_result vr_file_seek(vr_file *file, int64_t offset, vr_seek_origin origin)
{
    static const int whence[] = { SEEK_SET, SEEK_CUR, SEEK_END };

    if (!file || origin < VR_SEEK_SET || origin > VR_SEEK_END)
        return VR_ERR_INVALID_ARG;
    return fseeko(file->fp, (off_t)offset, whence[origin]) == 0 ? VR_OK : VR_ERR_IO;
}

int64_t vr_file_tell(vr_file *file)
{
    return file ? (int64_t)ftello(file->fp) : -1;
}

int64_t vr_file_size(vr_file *file)
{
    off_t pos, end;

    if (!file)
        return -1;
    pos = ftello(file->fp);
    if (fseeko(file->fp, 0, SEEK_END) != 0)
        return -1;
    end = ftello(file->fp);
    fseeko(file->fp, pos, SEEK_SET);
    return (int64_t)end;
}
