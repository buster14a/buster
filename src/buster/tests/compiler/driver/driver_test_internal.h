#pragma once

#include <buster/tests/compiler/driver/driver_test.h>

#if BUSTER_INCLUDE_TESTS && !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_F_DECL bool compiler_driver_test_run_cc(UnitTestArguments* arguments, SliceString8 cc_arguments, String8* output, String8* error);
#endif
