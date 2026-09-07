#include "basic_c_llvm_aggregate_abi_shapes.h"

struct llvm_abi_small LLVM_ABI_CC llvm_abi_small_transform(struct llvm_abi_small value, int delta)
{
    value.first += delta;
    value.second -= delta;
    return value;
}

struct llvm_abi_integer_pair LLVM_ABI_CC llvm_abi_integer_pair_transform(struct llvm_abi_integer_pair value, int delta)
{
    value.first += delta;
    value.second -= delta;
    return value;
}

struct llvm_abi_mixed LLVM_ABI_CC llvm_abi_mixed_transform(struct llvm_abi_mixed value, int delta)
{
    value.first += delta;
    value.second -= delta;
    return value;
}

struct llvm_abi_two_double LLVM_ABI_CC llvm_abi_two_double_transform(struct llvm_abi_two_double value, int delta)
{
    value.first += delta;
    value.second -= delta;
    return value;
}

struct llvm_abi_aligned_double LLVM_ABI_CC llvm_abi_aligned_double_transform(struct llvm_abi_aligned_double value, int delta)
{
    value.value += delta;
    return value;
}

struct llvm_abi_aligned_integer LLVM_ABI_CC llvm_abi_aligned_integer_transform(struct llvm_abi_aligned_integer value, int delta)
{
    value.value += delta;
    return value;
}

struct llvm_abi_triple_float LLVM_ABI_CC llvm_abi_triple_float_transform(struct llvm_abi_triple_float value, int delta)
{
    value.first += delta;
    value.second -= delta;
    value.third += delta;
    return value;
}

struct llvm_abi_large LLVM_ABI_CC llvm_abi_large_transform(struct llvm_abi_large value, int delta)
{
    value.first += delta;
    value.second -= delta;
    value.third += delta;
    return value;
}

struct llvm_abi_packed LLVM_ABI_CC llvm_abi_packed_transform(struct llvm_abi_packed value, int delta)
{
    value.tag += delta;
    value.value -= delta;
    return value;
}

struct llvm_abi_integer_pair LLVM_ABI_CC llvm_abi_after_scalars(int a, int b, int c, int d, int e, struct llvm_abi_integer_pair value)
{
    value.first += a + b + c + d + e;
    value.second -= a + b + c + d + e;
    return value;
}
