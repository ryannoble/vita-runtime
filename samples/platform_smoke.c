#include <stddef.h>

#include "vr/vr_fs.h"
#include "vr/vr_input.h"
#include "vr/vr_platform.h"

/* Original API smoke sample: no game code or data required. */
int main(void)
{
    vr_platform_desc desc = { "vr-platform-smoke", NULL };
    vr_input_state input;
    vr_file *file = NULL;
    vr_result result;
    uint64_t start, elapsed;

    if (vr_platform_init(&desc) != VR_OK)
        return 1;
    start = vr_time_us();
    vr_sleep_us(2000);
    elapsed = vr_time_us() - start;
    vr_log(VR_LOG_INFO, "elapsed after 2000 us sleep: %llu us",
           (unsigned long long)elapsed);
    vr_input_set_deadzone(0.15f);
    vr_input_poll(&input);
    result = vr_file_open("missing.txt", &file);
    vr_log(VR_LOG_INFO, "file open result: %d; input buttons: %lu",
           (int)result, (unsigned long)input.buttons);
    vr_file_close(file);
    vr_platform_shutdown();
    return elapsed >= 2000 ? 0 : 1;
}
