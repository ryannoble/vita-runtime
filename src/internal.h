#ifndef VR_INTERNAL_H
#define VR_INTERNAL_H

/* Shared between backends; not part of the public API. */

#define VR_MAX_PATH 512

const char *vr__data_root(void);

/* Returns nonzero if path is relative, non-empty and has no ".." component. */
int vr__path_is_safe(const char *path);

float vr__input_deadzone(void);
void vr__apply_deadzone(float *x, float *y, float radius);
void vr__input_shutdown(void);

#endif
