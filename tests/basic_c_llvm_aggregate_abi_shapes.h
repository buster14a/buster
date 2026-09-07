#ifndef BASIC_C_LLVM_AGGREGATE_ABI_SHAPES_H
#define BASIC_C_LLVM_AGGREGATE_ABI_SHAPES_H

#if defined(BUSTER_LLVM_ABI_WIN64)
#define LLVM_ABI_CC __attribute__((ms_abi))
#else
#define LLVM_ABI_CC
#endif

// One integer register, two integer registers, mixed SSE/integer registers,
// homogeneous floats, and an indirect aggregate in the supported C ABIs.
struct llvm_abi_small
{
    int first;
    int second;
};

struct llvm_abi_integer_pair
{
    long long first;
    long long second;
};

struct llvm_abi_mixed
{
    double first;
    int second;
};

struct llvm_abi_two_double
{
    double first;
    double second;
};

// Trailing alignment padding carries no additional ABI register class.
struct __attribute__((aligned(16))) llvm_abi_aligned_double
{
    double value;
};

struct __attribute__((aligned(16))) llvm_abi_aligned_integer
{
    long long value;
};

// The canonical object is twelve bytes, while a {double, float} LLVM
// coercion can occupy sixteen: conversions must reserve the whole temporary.
struct llvm_abi_triple_float
{
    float first;
    float second;
    float third;
};

struct llvm_abi_large
{
    long long first;
    long long second;
    long long third;
};

struct __attribute__((packed)) llvm_abi_packed
{
    char tag;
    int value;
};

extern struct llvm_abi_small LLVM_ABI_CC llvm_abi_small_transform(struct llvm_abi_small value, int delta);
extern struct llvm_abi_integer_pair LLVM_ABI_CC llvm_abi_integer_pair_transform(struct llvm_abi_integer_pair value, int delta);
extern struct llvm_abi_mixed LLVM_ABI_CC llvm_abi_mixed_transform(struct llvm_abi_mixed value, int delta);
extern struct llvm_abi_two_double LLVM_ABI_CC llvm_abi_two_double_transform(struct llvm_abi_two_double value, int delta);
extern struct llvm_abi_aligned_double LLVM_ABI_CC llvm_abi_aligned_double_transform(struct llvm_abi_aligned_double value, int delta);
extern struct llvm_abi_aligned_integer LLVM_ABI_CC llvm_abi_aligned_integer_transform(struct llvm_abi_aligned_integer value, int delta);
extern struct llvm_abi_triple_float LLVM_ABI_CC llvm_abi_triple_float_transform(struct llvm_abi_triple_float value, int delta);
extern struct llvm_abi_large LLVM_ABI_CC llvm_abi_large_transform(struct llvm_abi_large value, int delta);
extern struct llvm_abi_packed LLVM_ABI_CC llvm_abi_packed_transform(struct llvm_abi_packed value, int delta);
extern struct llvm_abi_integer_pair LLVM_ABI_CC llvm_abi_after_scalars(int a, int b, int c, int d, int e, struct llvm_abi_integer_pair value);

#endif
