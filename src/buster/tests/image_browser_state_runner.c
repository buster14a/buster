// Standalone headless state/ownership tests; this target has no windows,
// rendering or compiler modules. Pixel/source fixtures are authored in tests.
#define BUSTER_USE_GRAPHICS 0
#include <buster/lib/base.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/target.h>
#include <buster/tests/image_browser_state_test.h>
#include <buster/tests/image_browser_linux_test.h>
BUSTER_GLOBAL_LOCAL ProgramState image_browser_test_program;
BUSTER_V_IMPL ProgramState* program_state = &image_browser_test_program;
#if BUSTER_UNITY_BUILD
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/file.c>
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
#include <buster/lib/float.c>
#include <buster/lib/target.c>
#include <buster/lib/image.c>
#include <buster/apps/image_browser/image_browser_state.c>
#include <buster/apps/image_browser/image_browser_linux.c>
#include <buster/tests/image_browser_state_test.c>
#include <buster/tests/image_browser_linux_test.c>
#include <buster/lib/entry_point.c>
#endif

ProcessResult process_arguments(void)
{
    return PROCESS_RESULT_SUCCESS;
}
ProcessResult entry_point(void)
{
    s32 result = image_browser_run_state_tests();
    bool linux_success = image_browser_run_linux_tests();
    result = result || !linux_success;
    return result ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS;
}
