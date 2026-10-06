#pragma once

#include <buster/lib/base.h>

#if BUSTER_INCLUDE_TESTS
// Standalone image-browser test target only; not registered in headless ide.
BUSTER_F_DECL s32 image_browser_run_state_tests(void);
#endif
