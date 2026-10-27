#include <stdio.h>
#include <string.h>

#include "vr/vr_fs.h"
#include "vr/vr_input.h"
#include "vr/vr_platform.h"
#include "internal.h"

static int g_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                    __LINE__, #cond);                                   \
            g_failures++;                                               \
        }                                                               \
    } while (0)

static void test_path_safety(void)
{
    CHECK(vr__path_is_safe("a.txt"));
    CHECK(vr__path_is_safe("dir/sub/a.txt"));
    CHECK(vr__path_is_safe("dir/..x"));
    CHECK(!vr__path_is_safe(""));
    CHECK(!vr__path_is_safe(NULL));
    CHECK(!vr__path_is_safe("/etc/passwd"));
    CHECK(!vr__path_is_safe(".."));
    CHECK(!vr__path_is_safe("dir/../x"));
    CHECK(!vr__path_is_safe("dir\\x"));
    CHECK(!vr__path_is_safe("ux0:data/x"));
}

static void test_file_read(void)
{
    vr_file *file = NULL;
    char buf[32] = { 0 };
    size_t n = 0;

    CHECK(vr_file_open("hello.txt", &file) == VR_OK);
    if (!file)
        return;
    CHECK(vr_file_size(file) == 11);
    CHECK(vr_file_read(file, buf, sizeof(buf), &n) == VR_OK);
    CHECK(n == 11);
    CHECK(memcmp(buf, "hello vita\n", 11) == 0);
    CHECK(vr_file_seek(file, 6, VR_SEEK_SET) == VR_OK);
    CHECK(vr_file_tell(file) == 6);
    CHECK(vr_file_read(file, buf, 4, &n) == VR_OK && n == 4);
    CHECK(memcmp(buf, "vita", 4) == 0);
    vr_file_close(file);

    CHECK(vr_file_open("missing.txt", &file) == VR_ERR_NOT_FOUND);
    CHECK(vr_file_open("../hello.txt", &file) == VR_ERR_INVALID_ARG);
}

static void test_time(void)
{
    uint64_t t0 = vr_time_us();
    vr_sleep_us(2000);
    CHECK(vr_time_us() >= t0 + 2000);
}

static void test_input(void)
{
    vr_input_state state;
    memset(&state, 0xff, sizeof(state));
    vr_input_poll(&state);
    CHECK(state.buttons == 0 && state.front_count == 0);
}

int main(int argc, char **argv)
{
    vr_platform_desc desc = { "vr_tests", argc > 1 ? argv[1] : "tests/data" };

    if (vr_platform_init(&desc) != VR_OK) {
        fprintf(stderr, "vr_platform_init failed\n");
        return 1;
    }
    test_path_safety();
    test_file_read();
    test_time();
    test_input();
    vr_platform_shutdown();

    if (g_failures)
        fprintf(stderr, "%d check(s) failed\n", g_failures);
    else
        printf("all tests passed\n");
    return g_failures ? 1 : 0;
}
