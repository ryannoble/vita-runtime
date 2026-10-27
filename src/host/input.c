#include <string.h>

#include "vr/vr_input.h"

/* Headless host backend: no devices. An SDL2 backend is planned. */
void vr_input_poll(vr_input_state *state)
{
    if (state)
        memset(state, 0, sizeof(*state));
}
