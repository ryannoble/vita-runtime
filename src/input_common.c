#include <math.h>

#include "vr/vr_input.h"
#include "internal.h"

static float g_deadzone = 0.15f;

void vr_input_set_deadzone(float radius)
{
    if (!isfinite(radius))
        radius = 0.15f;
    if (radius < 0.0f)
        radius = 0.0f;
    if (radius > 0.9f)
        radius = 0.9f;
    g_deadzone = radius;
}

float vr__input_deadzone(void)
{
    return g_deadzone;
}

void vr__apply_deadzone(float *x, float *y, float radius)
{
    float len = sqrtf(*x * *x + *y * *y);
    float scale;

    if (len <= radius) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }
    scale = ((fminf(len, 1.0f) - radius) / (1.0f - radius)) / len;
    *x *= scale;
    *y *= scale;
}
