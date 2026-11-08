#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include "vr/vr_input.h"
#include "../internal.h"

typedef struct {
    float min_x, min_y, range_x, range_y;
} panel_area;

static int        g_initialized;
static panel_area g_panels[2];

static void init_panel(SceUInt32 port, panel_area *area)
{
    SceTouchPanelInfo info;

    sceTouchSetSamplingState(port, SCE_TOUCH_SAMPLING_STATE_START);
    memset(&info, 0, sizeof(info));
    if (sceTouchGetPanelInfo(port, &info) < 0 || info.maxAaX <= info.minAaX ||
        info.maxAaY <= info.minAaY) {
        /* Leave a zero range: touches on this panel are then ignored. */
        memset(area, 0, sizeof(*area));
        return;
    }
    area->min_x = (float)info.minAaX;
    area->min_y = (float)info.minAaY;
    area->range_x = (float)(info.maxAaX - info.minAaX);
    area->range_y = (float)(info.maxAaY - info.minAaY);
}

static void input_init(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    init_panel(SCE_TOUCH_PORT_FRONT, &g_panels[0]);
    init_panel(SCE_TOUCH_PORT_BACK, &g_panels[1]);
    g_initialized = 1;
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Maps 0..255 (centre 128) to -1..1. Up is already negative on the Vita. */
static float stick_axis(unsigned char v)
{
    float f = ((float)v - 128.0f) / 127.0f;
    return f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
}

static int read_touch(SceUInt32 port, const panel_area *area, vr_touch_point *out)
{
    SceTouchData data;
    int count, i;

    if (area->range_x <= 0.0f || area->range_y <= 0.0f)
        return 0;
    memset(&data, 0, sizeof(data));
    if (sceTouchPeek(port, &data, 1) < 1)
        return 0;
    count = (int)data.reportNum;
    if (count > VR_MAX_TOUCHES)
        count = VR_MAX_TOUCHES;
    for (i = 0; i < count; i++) {
        out[i].x = clamp01(((float)data.report[i].x - area->min_x) / area->range_x);
        out[i].y = clamp01(((float)data.report[i].y - area->min_y) / area->range_y);
    }
    return count;
}

void vr_input_poll(vr_input_state *state)
{
    static const struct {
        unsigned int sce;
        uint32_t     vr;
    } buttons[] = {
        { SCE_CTRL_UP, VR_BTN_UP },
        { SCE_CTRL_DOWN, VR_BTN_DOWN },
        { SCE_CTRL_LEFT, VR_BTN_LEFT },
        { SCE_CTRL_RIGHT, VR_BTN_RIGHT },
        { SCE_CTRL_CROSS, VR_BTN_CROSS },
        { SCE_CTRL_CIRCLE, VR_BTN_CIRCLE },
        { SCE_CTRL_SQUARE, VR_BTN_SQUARE },
        { SCE_CTRL_TRIANGLE, VR_BTN_TRIANGLE },
        { SCE_CTRL_LTRIGGER, VR_BTN_L },
        { SCE_CTRL_RTRIGGER, VR_BTN_R },
        { SCE_CTRL_START, VR_BTN_START },
        { SCE_CTRL_SELECT, VR_BTN_SELECT }
    };
    SceCtrlData pad;
    size_t i;

    if (!state)
        return;
    memset(state, 0, sizeof(*state));
    if (!g_initialized)
        input_init();

    memset(&pad, 0, sizeof(pad));
    if (sceCtrlPeekBufferPositive(0, &pad, 1) >= 1) {
        for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
            if (pad.buttons & buttons[i].sce)
                state->buttons |= buttons[i].vr;
        }
        state->left_x = stick_axis(pad.lx);
        state->left_y = stick_axis(pad.ly);
        state->right_x = stick_axis(pad.rx);
        state->right_y = stick_axis(pad.ry);
        vr__apply_deadzone(&state->left_x, &state->left_y, vr__input_deadzone());
        vr__apply_deadzone(&state->right_x, &state->right_y, vr__input_deadzone());
    }

    state->front_count = read_touch(SCE_TOUCH_PORT_FRONT, &g_panels[0], state->front);
    state->rear_count = read_touch(SCE_TOUCH_PORT_BACK, &g_panels[1], state->rear);
}

void vr__input_shutdown(void)
{
    g_initialized = 0;
}
