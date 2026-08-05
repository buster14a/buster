#pragma once

#include <buster/tests/test.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL String16 string16_from_pointer_length(const char16* pointer, u64 length);
BUSTER_F_DECL String16 string16_from_pointer(const char16* pointer);
BUSTER_F_DECL UnitTestResult string_tests(UnitTestArguments* arguments);
#endif
