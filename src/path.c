#include <string.h>

#include "internal.h"

int vr__path_is_safe(const char *path)
{
    const char *p = path;

    if (!path || !*path || *path == '/' || strchr(path, '\\') || strchr(path, ':'))
        return 0;
    while (*p) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.')
            return 0;
        if (!end)
            break;
        p = end + 1;
    }
    return 1;
}
