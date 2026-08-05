#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/codegen/codegen_internal.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/interpreter.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
typedef struct CodegenTestX64BodyScan CodegenTestX64BodyScan;
struct CodegenTestX64BodyScan
{
    bool valid;
    bool has_call;
    bool has_indirect_call;
    bool has_stack_store;
    u32 maximum_stack_store_end;
};

BUSTER_F_DECL CodegenTestX64BodyScan codegen_test_x64_scan_body(ByteSlice code, u64 start, u64 end, u32 allocation, u32 frame_restore_displacement);
BUSTER_F_DECL UnitTestResult codegen_tests(UnitTestArguments* arguments);
#endif
