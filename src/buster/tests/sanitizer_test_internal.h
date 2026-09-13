#pragma once

#include <buster/tests/test.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL ProcessResult sanitizer_test_canary_run(String8 mode);
#endif
