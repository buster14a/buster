#pragma once

#include <buster/lib/compiler/llvm/bitcode.h>

#if BUSTER_INCLUDE_TESTS
// Private access to the production CST_CODE_INTEGER signed-rotation operand.
// Tests supply widths 1 through 64; no module or arena allocation is needed.
BUSTER_F_DECL u64 llvm_bitcode_test_integer_operand(u64 bits, u32 width);
#endif
