// The callee half of the atomic-argument pair.  It reads each record out of
// wherever its own compiler's classifier put it and builds each one for the
// result direction, which is where a disagreement about the class surfaces:
// a half that expects the record in memory reads the caller's stack while the
// other half left the value in a register.  See basic_c_atomic_abi_caller.c
// for what each answer pins down, and the header for the decision itself.
//
// Freestanding on purpose: the AArch64 lane links this against a tiny `_start`
// and runs it under qemu, so nothing here may reach for libc.
#include "basic_c_atomic_abi_shapes.h"

int atomic_abi_scalar_member_sum(struct atomic_scalar_member x)
{
    return (int)x.c + x.v;
}

int atomic_abi_scalar_member_after_five(int a, int b, int c, int d, int e, struct atomic_scalar_member x)
{
    return a + b + c + d + e + (int)x.c + x.v;
}

struct atomic_scalar_member atomic_abi_make_scalar_member(char c, int v)
{
    struct atomic_scalar_member result;
    result.c = c;
    result.v = v;
    return result;
}

long long atomic_abi_pair_member_sum(struct atomic_pair_member x)
{
    return x.a + x.b;
}

long long atomic_abi_pair_member_after_five(int a, int b, int c, int d, int e, struct atomic_pair_member x)
{
    return (long long)(a + b + c + d + e) + x.a + x.b;
}

struct atomic_pair_member atomic_abi_make_pair_member(long long a, long long b)
{
    struct atomic_pair_member result;
    result.a = a;
    result.b = b;
    return result;
}

// The atomic member is read through a copy rather than field by field: an
// atomic aggregate is loaded as one access of its promoted width (#762), and
// the copy is what makes that the access the member is read with.
int atomic_abi_record_member_sum(struct atomic_record_member x)
{
    struct atomic_int_pair copy = x.v;
    return (int)x.c + copy.a + copy.b;
}

int atomic_abi_record_member_after_five(int a, int b, int c, int d, int e, struct atomic_record_member x)
{
    struct atomic_int_pair copy = x.v;
    return a + b + c + d + e + (int)x.c + copy.a + copy.b;
}

struct atomic_record_member atomic_abi_make_record_member(char c, int a, int b)
{
    struct atomic_int_pair built;
    built.a = a;
    built.b = b;
    struct atomic_record_member result;
    result.c = c;
    result.v = built;
    return result;
}

float atomic_abi_volatile_float_second(struct volatile_float_member x)
{
    return x.b;
}

float atomic_abi_volatile_float_after_five(float a, float b, float c, float d, float e, struct volatile_float_member x)
{
    return a + b + c + d + e + x.b;
}

struct volatile_float_member atomic_abi_make_volatile_float(float a, float b)
{
    struct volatile_float_member result;
    result.a = a;
    result.b = b;
    return result;
}

#ifndef ATOMIC_ABI_REFERENCE_DECLINES_ATOMIC_HFA
float atomic_abi_atomic_float_second(struct atomic_float_member x)
{
    return x.b;
}

float atomic_abi_atomic_float_after_five(float a, float b, float c, float d, float e, struct atomic_float_member x)
{
    return a + b + c + d + e + x.b;
}

struct atomic_float_member atomic_abi_make_atomic_float(float a, float b)
{
    struct atomic_float_member result;
    result.a = a;
    result.b = b;
    return result;
}
#endif
