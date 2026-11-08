#include <string.h>

#include "vr/vr_input.h"
#include "../internal.h"

/* Headless host backend: no devices. */
void vr_input_poll(vr_input_state *state)
{
    if (state)
        memset(state, 0, sizeof(*state));
}

void vr__input_shutdown(void)
{
}
