#include <string.h>

#include <SDL.h>

#include "vr/vr_input.h"
#include "../internal.h"

static SDL_GameController *g_controller;
static int g_init_attempted;
static int g_ready;

void vr__input_shutdown(void)
{
    if (g_controller) {
        SDL_GameControllerClose(g_controller);
        g_controller = NULL;
    }
    if (g_ready)
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    g_ready = 0;
    g_init_attempted = 0;
}

static float axis_value(SDL_GameControllerAxis axis)
{
    float value = SDL_GameControllerGetAxis(g_controller, axis) / 32767.0f;

    if (value < -1.0f)
        value = -1.0f;
    return value;
}

void vr_input_poll(vr_input_state *state)
{
    static const struct {
        SDL_GameControllerButton sdl;
        uint32_t vr;
    } buttons[] = {
        { SDL_CONTROLLER_BUTTON_A, VR_BTN_CROSS },
        { SDL_CONTROLLER_BUTTON_B, VR_BTN_CIRCLE },
        { SDL_CONTROLLER_BUTTON_X, VR_BTN_SQUARE },
        { SDL_CONTROLLER_BUTTON_Y, VR_BTN_TRIANGLE },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, VR_BTN_L },
        { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, VR_BTN_R },
        { SDL_CONTROLLER_BUTTON_START, VR_BTN_START },
        { SDL_CONTROLLER_BUTTON_BACK, VR_BTN_SELECT },
        { SDL_CONTROLLER_BUTTON_DPAD_UP, VR_BTN_UP },
        { SDL_CONTROLLER_BUTTON_DPAD_DOWN, VR_BTN_DOWN },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT, VR_BTN_LEFT },
        { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, VR_BTN_RIGHT }
    };
    size_t i;

    if (!state)
        return;
    memset(state, 0, sizeof(*state));

    if (!g_init_attempted) {
        g_init_attempted = 1;
        g_ready = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0;
    }
    if (!g_ready)
        return;

    SDL_PumpEvents();
    if (g_controller && !SDL_GameControllerGetAttached(g_controller)) {
        SDL_GameControllerClose(g_controller);
        g_controller = NULL;
        return;
    }
    if (!g_controller) {
        int count = SDL_NumJoysticks();
        int index;

        for (index = 0; index < count; index++) {
            if (SDL_IsGameController(index)) {
                g_controller = SDL_GameControllerOpen(index);
                if (g_controller)
                    break;
            }
        }
    }
    if (!g_controller)
        return;

    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (SDL_GameControllerGetButton(g_controller, buttons[i].sdl))
            state->buttons |= buttons[i].vr;
    }
    state->left_x = axis_value(SDL_CONTROLLER_AXIS_LEFTX);
    state->left_y = axis_value(SDL_CONTROLLER_AXIS_LEFTY);
    state->right_x = axis_value(SDL_CONTROLLER_AXIS_RIGHTX);
    state->right_y = axis_value(SDL_CONTROLLER_AXIS_RIGHTY);
    vr__apply_deadzone(&state->left_x, &state->left_y, vr__input_deadzone());
    vr__apply_deadzone(&state->right_x, &state->right_y, vr__input_deadzone());
}
