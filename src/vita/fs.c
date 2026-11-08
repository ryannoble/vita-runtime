#include <stdio.h>
#include <stdlib.h>

#include <psp2/io/fcntl.h>

#include "vr/vr_fs.h"
#include "../internal.h"

struct vr_file {
    SceUID fd;
};

vr_result vr_file_open(const char *path, vr_file **out)
{
    char full[VR_MAX_PATH];
    vr_file *file;
    SceUID fd;
    int n;

    if (!out || !vr__path_is_safe(path))
        return VR_ERR_INVALID_ARG;
    n = snprintf(full, sizeof(full), "%s/%s", vr__data_root(), path);
    if (n < 0 || (size_t)n >= sizeof(full))
        return VR_ERR_INVALID_ARG;

    fd = sceIoOpen(full, SCE_O_RDONLY, 0);
    if (fd < 0)
        return VR_ERR_NOT_FOUND;
    file = malloc(sizeof(*file));
    if (!file) {
        sceIoClose(fd);
        return VR_ERR_NO_MEMORY;
    }
    file->fd = fd;
    *out = file;
    return VR_OK;
}

void vr_file_close(vr_file *file)
{
    if (!file)
        return;
    sceIoClose(file->fd);
    free(file);
}

vr_result vr_file_read(vr_file *file, void *buf, size_t size, size_t *bytes_read)
{
    /* sceIoRead takes a 32-bit size; read in bounded chunks. */
    const size_t max_chunk = 1u << 30;
    unsigned char *dst = buf;
    size_t total = 0;
    vr_result result = VR_OK;

    if (!file || (!buf && size))
        return VR_ERR_INVALID_ARG;
    while (total < size) {
        size_t want = size - total;
        int got;

        if (want > max_chunk)
            want = max_chunk;
        got = sceIoRead(file->fd, dst + total, (SceSize)want);
        if (got < 0) {
            result = VR_ERR_IO;
            break;
        }
        if (got == 0)
            break; /* end of file */
        total += (size_t)got;
    }
    if (bytes_read)
        *bytes_read = total;
    return result;
}

vr_result vr_file_seek(vr_file *file, int64_t offset, vr_seek_origin origin)
{
    static const int whence[] = { SCE_SEEK_SET, SCE_SEEK_CUR, SCE_SEEK_END };

    if (!file || origin < VR_SEEK_SET || origin > VR_SEEK_END)
        return VR_ERR_INVALID_ARG;
    return sceIoLseek(file->fd, (SceOff)offset, whence[origin]) >= 0 ? VR_OK : VR_ERR_IO;
}

int64_t vr_file_tell(vr_file *file)
{
    SceOff pos;

    if (!file)
        return -1;
    pos = sceIoLseek(file->fd, 0, SCE_SEEK_CUR);
    return pos < 0 ? -1 : (int64_t)pos;
}

int64_t vr_file_size(vr_file *file)
{
    SceOff pos, end;

    if (!file)
        return -1;
    pos = sceIoLseek(file->fd, 0, SCE_SEEK_CUR);
    if (pos < 0)
        return -1;
    end = sceIoLseek(file->fd, 0, SCE_SEEK_END);
    sceIoLseek(file->fd, pos, SCE_SEEK_SET);
    return end < 0 ? -1 : (int64_t)end;
}
