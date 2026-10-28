#include <math.h>
#include <stdio.h>

#include <SDL.h>

#include "vr/vr_input.h"
#include "vr/vr_platform.h"

static int g_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s (%s)\n", __FILE__, \
                    __LINE__, #cond, SDL_GetError());                    \
            g_failures++;                                               \
        }                                                               \
    } while (0)

int main(void)
{
    static const uint32_t buttons[] = {
        VR_BTN_CROSS, VR_BTN_CIRCLE, VR_BTN_SQUARE, VR_BTN_TRIANGLE,
        VR_BTN_SELECT, 0, VR_BTN_START, 0, 0, VR_BTN_L, VR_BTN_R,
        VR_BTN_UP, VR_BTN_DOWN, VR_BTN_LEFT, VR_BTN_RIGHT
    };
    SDL_Joystick *joystick;
    vr_input_state state;
    int device;
    int i;

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                      SDL_CONTROLLER_AXIS_MAX,
                                      SDL_CONTROLLER_BUTTON_MAX, 0);
    if (device < 0) {
        fprintf(stderr, "Virtual controller failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    joystick = SDL_JoystickOpen(device);
    if (!joystick) {
        fprintf(stderr, "Virtual controller open failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    CHECK(SDL_IsGameController(device));
    CHECK(vr_platform_init(NULL) == VR_OK);
    vr_input_poll(NULL);
    vr_input_poll(&state);
    CHECK(state.buttons == 0);
    CHECK(state.front_count == 0 && state.rear_count == 0);

    for (i = 0; i < (int)(sizeof(buttons) / sizeof(buttons[0])); i++) {
        CHECK(SDL_JoystickSetVirtualButton(joystick, i, 1) == 0);
        SDL_JoystickUpdate();
        vr_input_poll(&state);
        CHECK(state.buttons == buttons[i]);
        CHECK(SDL_JoystickSetVirtualButton(joystick, i, 0) == 0);
    }
    CHECK(SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX,
                                   32767) == 0);
    CHECK(SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_RIGHTY,
                                   -32768) == 0);
    SDL_JoystickUpdate();
    vr_input_poll(&state);
    CHECK(fabsf(state.left_x - 1.0f) < 0.0001f);
    CHECK(fabsf(state.right_y + 1.0f) < 0.0001f);

    CHECK(SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX,
                                   3276) == 0);
    SDL_JoystickUpdate();
    vr_input_poll(&state);
    CHECK(state.left_x == 0.0f && state.left_y == 0.0f);
    vr_input_set_deadzone(0.0f);
    vr_input_poll(&state);
    CHECK(fabsf(state.left_x - 3276.0f / 32767.0f) < 0.0001f);
    vr_input_set_deadzone(0.15f);

    CHECK(SDL_JoystickDetachVirtual(device) == 0);
    vr_input_poll(&state);
    CHECK(state.buttons == 0 && state.left_x == 0.0f && state.right_y == 0.0f);
    SDL_JoystickClose(joystick);

    device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                      SDL_CONTROLLER_AXIS_MAX,
                                      SDL_CONTROLLER_BUTTON_MAX, 0);
    CHECK(device >= 0);
    joystick = device >= 0 ? SDL_JoystickOpen(device) : NULL;
    CHECK(joystick != NULL);
    if (joystick) {
        CHECK(SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A,
                                         1) == 0);
        SDL_JoystickUpdate();
        vr_input_poll(&state);
        CHECK(state.buttons == VR_BTN_CROSS);
        vr_platform_shutdown();
        CHECK(vr_platform_init(NULL) == VR_OK);
        vr_input_poll(&state);
        CHECK(state.buttons == VR_BTN_CROSS);
        CHECK(SDL_JoystickDetachVirtual(device) == 0);
        SDL_JoystickClose(joystick);
    }
    vr_platform_shutdown();
    SDL_Quit();
    return g_failures ? 1 : 0;
}
