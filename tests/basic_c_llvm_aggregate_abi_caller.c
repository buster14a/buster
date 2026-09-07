#include "basic_c_llvm_aggregate_abi_shapes.h"

// Volatile function pointers keep the second call indirect even when Clang
// optimizes the bitcode. Each half is compiled independently and mixed with a
// host C object, so two copies of the same incorrect ABI cannot agree.
static struct llvm_abi_small (*volatile small_call)(struct llvm_abi_small, int) LLVM_ABI_CC = llvm_abi_small_transform;
static struct llvm_abi_integer_pair (*volatile integer_pair_call)(struct llvm_abi_integer_pair, int) LLVM_ABI_CC = llvm_abi_integer_pair_transform;
static struct llvm_abi_mixed (*volatile mixed_call)(struct llvm_abi_mixed, int) LLVM_ABI_CC = llvm_abi_mixed_transform;
static struct llvm_abi_two_double (*volatile two_double_call)(struct llvm_abi_two_double, int) LLVM_ABI_CC = llvm_abi_two_double_transform;
static struct llvm_abi_aligned_double (*volatile aligned_double_call)(struct llvm_abi_aligned_double, int) LLVM_ABI_CC = llvm_abi_aligned_double_transform;
static struct llvm_abi_aligned_integer (*volatile aligned_integer_call)(struct llvm_abi_aligned_integer, int) LLVM_ABI_CC = llvm_abi_aligned_integer_transform;
static struct llvm_abi_triple_float (*volatile triple_float_call)(struct llvm_abi_triple_float, int) LLVM_ABI_CC = llvm_abi_triple_float_transform;
static struct llvm_abi_large (*volatile large_call)(struct llvm_abi_large, int) LLVM_ABI_CC = llvm_abi_large_transform;
static struct llvm_abi_packed (*volatile packed_call)(struct llvm_abi_packed, int) LLVM_ABI_CC = llvm_abi_packed_transform;
static struct llvm_abi_integer_pair (*volatile after_scalars_call)(int, int, int, int, int, struct llvm_abi_integer_pair) LLVM_ABI_CC = llvm_abi_after_scalars;

int main(void)
{
    int failures = 0;
    struct llvm_abi_small small = {12, 29};
    struct llvm_abi_small small_result = llvm_abi_small_transform(small, 3);
    failures += small_result.first != 15 || small_result.second != 26;
    small_result = small_call(small, 5);
    failures += small_result.first != 17 || small_result.second != 24;

    struct llvm_abi_integer_pair integer_pair = {0x123456789abcdefLL, 47};
    struct llvm_abi_integer_pair integer_pair_result = llvm_abi_integer_pair_transform(integer_pair, 3);
    failures += integer_pair_result.first != 0x123456789abcdf2LL || integer_pair_result.second != 44;
    integer_pair_result = integer_pair_call(integer_pair, 5);
    failures += integer_pair_result.first != 0x123456789abcdf4LL || integer_pair_result.second != 42;

    struct llvm_abi_mixed mixed = {1.5, 29};
    struct llvm_abi_mixed mixed_result = llvm_abi_mixed_transform(mixed, 3);
    failures += mixed_result.first != 4.5 || mixed_result.second != 26;
    mixed_result = mixed_call(mixed, 5);
    failures += mixed_result.first != 6.5 || mixed_result.second != 24;

    struct llvm_abi_two_double two_double = {1.5, 2.5};
    struct llvm_abi_two_double two_double_result = llvm_abi_two_double_transform(two_double, 3);
    failures += two_double_result.first != 4.5 || two_double_result.second != -0.5;
    two_double_result = two_double_call(two_double, 5);
    failures += two_double_result.first != 6.5 || two_double_result.second != -2.5;

    struct llvm_abi_aligned_double aligned_double = {1.5};
    struct llvm_abi_aligned_double aligned_double_result = llvm_abi_aligned_double_transform(aligned_double, 3);
    failures += aligned_double_result.value != 4.5;
    aligned_double_result = aligned_double_call(aligned_double, 5);
    failures += aligned_double_result.value != 6.5;

    struct llvm_abi_aligned_integer aligned_integer = {0x123456789abcdefLL};
    struct llvm_abi_aligned_integer aligned_integer_result = llvm_abi_aligned_integer_transform(aligned_integer, 3);
    failures += aligned_integer_result.value != 0x123456789abcdf2LL;
    aligned_integer_result = aligned_integer_call(aligned_integer, 5);
    failures += aligned_integer_result.value != 0x123456789abcdf4LL;

    struct llvm_abi_triple_float triple_float = {1.5f, 2.5f, 4.5f};
    struct llvm_abi_triple_float triple_float_result = llvm_abi_triple_float_transform(triple_float, 3);
    failures += triple_float_result.first != 4.5f || triple_float_result.second != -0.5f || triple_float_result.third != 7.5f;
    triple_float_result = triple_float_call(triple_float, 5);
    failures += triple_float_result.first != 6.5f || triple_float_result.second != -2.5f || triple_float_result.third != 9.5f;

    struct llvm_abi_large large = {0x123456789abcdefLL, 0x23456789abcdefLL, 0x3456789abcdefLL};
    struct llvm_abi_large large_result = llvm_abi_large_transform(large, 3);
    failures += large_result.first != 0x123456789abcdf2LL || large_result.second != 0x23456789abcdecLL || large_result.third != 0x3456789abcdf2LL;
    large_result = large_call(large, 5);
    failures += large_result.first != 0x123456789abcdf4LL || large_result.second != 0x23456789abcdeaLL || large_result.third != 0x3456789abcdf4LL;

    struct llvm_abi_packed packed = {17, 0x12345678};
    struct llvm_abi_packed packed_result = llvm_abi_packed_transform(packed, 3);
    failures += packed_result.tag != 20 || packed_result.value != 0x12345675;
    packed_result = packed_call(packed, 5);
    failures += packed_result.tag != 22 || packed_result.value != 0x12345673;

    // Only one integer argument register remains. The two-register aggregate
    // must move wholly to memory instead of splitting between register/stack.
    integer_pair_result = llvm_abi_after_scalars(1, 2, 3, 4, 5, integer_pair);
    failures += integer_pair_result.first != 0x123456789abcdfeLL || integer_pair_result.second != 32;
    integer_pair_result = after_scalars_call(2, 4, 6, 8, 10, integer_pair);
    failures += integer_pair_result.first != 0x123456789abce0dLL || integer_pair_result.second != 17;
    return failures;
}
