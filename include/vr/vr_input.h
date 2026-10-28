#ifndef VR_INPUT_H
#define VR_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VR_BTN_UP       = 1u << 0,
    VR_BTN_DOWN     = 1u << 1,
    VR_BTN_LEFT     = 1u << 2,
    VR_BTN_RIGHT    = 1u << 3,
    VR_BTN_CROSS    = 1u << 4,
    VR_BTN_CIRCLE   = 1u << 5,
    VR_BTN_SQUARE   = 1u << 6,
    VR_BTN_TRIANGLE = 1u << 7,
    VR_BTN_L        = 1u << 8,
    VR_BTN_R        = 1u << 9,
    VR_BTN_START    = 1u << 10,
    VR_BTN_SELECT   = 1u << 11
} vr_button;

#define VR_MAX_TOUCHES 6

typedef struct {
    float x, y; /* normalized 0..1 */
} vr_touch_point;

typedef struct {
    uint32_t       buttons;          /* vr_button bitmask */
    float          left_x, left_y;   /* -1..1, deadzone applied */
    float          right_x, right_y; /* -1..1, deadzone applied */
    int            front_count;
    vr_touch_point front[VR_MAX_TOUCHES];
    int            rear_count;
    vr_touch_point rear[VR_MAX_TOUCHES];
} vr_input_state;

/* Fills *state with the current input. Zeroes it when no device is present. */
void vr_input_poll(vr_input_state *state);

/* Radius is clamped to 0..0.9; default 0.15. Applies to both sticks.
 * Non-finite values restore the default. */
void vr_input_set_deadzone(float radius);

#ifdef __cplusplus
}
#endif

#endif
