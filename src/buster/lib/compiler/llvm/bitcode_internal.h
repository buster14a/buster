#pragma once

#include <buster/lib/compiler/llvm/bitcode.h>

#if BUSTER_INCLUDE_TESTS
// Private seam for exhaustive wire-encoding tests; absent from production-only builds.
BUSTER_F_DECL u64 llvm_bitcode_test_encode_integer_bits(u64 bits, u32 width);
#endif
