#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.h>

BUSTER_TEST_F_DECL u64 link_read_u64(u8 const* bytes, u64 offset);
BUSTER_TEST_F_DECL u32 link_read_u32(u8 const* bytes, u64 offset);
BUSTER_TEST_F_DECL void link_sha256(Arena* arena, u8 const* input, u64 length, u8* output);

enum
{
    LINK_TEST_PE_STACK_RESERVE = 8 * 1024 * 1024,
};

BUSTER_TEST_F_DECL UnitTestResult link_tests(UnitTestArguments* arguments);
