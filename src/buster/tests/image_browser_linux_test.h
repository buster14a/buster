#pragma once

#include <buster/lib/base.h>

#if BUSTER_INCLUDE_TESTS
// Linux loader/file ownership checks; no native rendering is exercised.
BUSTER_F_DECL bool image_browser_run_linux_tests(void);
#endif
