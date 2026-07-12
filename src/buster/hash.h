#pragma once

#include <buster/base.h>

BUSTER_F_DECL u64 buster_hash_64(u8* pointer, u64 length);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult hash_tests(UnitTestArguments* arguments);
#endif
