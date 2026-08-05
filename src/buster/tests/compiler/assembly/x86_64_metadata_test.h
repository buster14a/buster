#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#if !BUSTER_SINGLE_THREADED
#include <buster/lib/os.h>
#include <stdatomic.h>
#endif

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult x86_64_metadata_tests(UnitTestArguments* arguments);
#endif
