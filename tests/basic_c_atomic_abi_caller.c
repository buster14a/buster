// The caller half of the atomic-argument pair.  Linked against a callee the
// other compiler built, so every check here is a question about where the
// platform puts a record holding an atomic member rather than about this
// compiler's internal consistency: a single translation unit that classifies
// such a record its own way agrees with itself and passes.  Neither half of
// that is theoretical.  Linking these same objects against a Clang-built
// System V half fails at return 10, the first record, which is the divergence
// this pair records rather than a fault to fix; and restoring the type
// identity comparison that made `struct { float a; volatile float b; }` an
// integer pair instead of a homogeneous float aggregate fails the
// Clang-paired AArch64 link at return 40, in both directions.
//
// Freestanding on purpose: the AArch64 lane links this against a tiny `_start`
// and runs it under qemu, so nothing here may reach for libc.  Every float
// constant is exactly representable, so the comparisons are exact.
#include "basic_c_atomic_abi_shapes.h"

int main(void)
{
    // A layout disagreement would fail the argument checks below for a reason
    // this pair is not about, so it is caught first and answered separately.
    if (sizeof(struct atomic_scalar_member) != 8 || _Alignof(struct atomic_scalar_member) != 4) return 1;
    if (sizeof(struct atomic_pair_member) != 16 || _Alignof(struct atomic_pair_member) != 8) return 2;
    if (sizeof(struct atomic_record_member) != 16 || _Alignof(struct atomic_record_member) != 8) return 3;
    if (sizeof(struct volatile_float_member) != 8 || _Alignof(struct volatile_float_member) != 4) return 4;

    struct atomic_scalar_member scalar;
    scalar.c = 'k';
    scalar.v = 0x0badf00d;
    if (atomic_abi_scalar_member_sum(scalar) != 'k' + 0x0badf00d) return 10;
    if (atomic_abi_scalar_member_after_five(1, 2, 3, 4, 5, scalar) != 15 + 'k' + 0x0badf00d) return 11;
    struct atomic_scalar_member made_scalar = atomic_abi_make_scalar_member('z', -4242);
    if (made_scalar.c != 'z' || made_scalar.v != -4242) return 12;

    struct atomic_pair_member pair;
    pair.a = 0x1122334455667788LL;
    pair.b = -0x0123456789abcdefLL;
    if (atomic_abi_pair_member_sum(pair) != 0x1122334455667788LL - 0x0123456789abcdefLL) return 20;
    if (atomic_abi_pair_member_after_five(1, 2, 3, 4, 5, pair) != 15 + 0x1122334455667788LL - 0x0123456789abcdefLL) return 21;
    struct atomic_pair_member made_pair = atomic_abi_make_pair_member(-7LL, 0x7fedcba987654321LL);
    if (made_pair.a != -7LL || made_pair.b != 0x7fedcba987654321LL) return 22;

    struct atomic_int_pair built;
    built.a = 31337;
    built.b = -987654321;
    struct atomic_record_member record;
    record.c = 'm';
    record.v = built;
    if (atomic_abi_record_member_sum(record) != 'm' + 31337 - 987654321) return 30;
    if (atomic_abi_record_member_after_five(1, 2, 3, 4, 5, record) != 15 + 'm' + 31337 - 987654321) return 31;
    struct atomic_record_member made_record = atomic_abi_make_record_member('n', 24680, -13579);
    struct atomic_int_pair made_record_copy = made_record.v;
    if (made_record.c != 'n' || made_record_copy.a != 24680 || made_record_copy.b != -13579) return 32;

    struct volatile_float_member volatile_floats;
    volatile_floats.a = 1.5f;
    volatile_floats.b = -2.25f;
    if (atomic_abi_volatile_float_second(volatile_floats) != -2.25f) return 40;
    if (atomic_abi_volatile_float_after_five(1.0f, 2.0f, 4.0f, 8.0f, 16.0f, volatile_floats) != 31.0f - 2.25f) return 41;
    struct volatile_float_member made_volatile = atomic_abi_make_volatile_float(0.75f, 96.5f);
    if (made_volatile.a != 0.75f || made_volatile.b != 96.5f) return 42;

#ifndef ATOMIC_ABI_REFERENCE_DECLINES_ATOMIC_HFA
    if (sizeof(struct atomic_float_member) != 8 || _Alignof(struct atomic_float_member) != 4) return 50;
    struct atomic_float_member atomic_floats;
    atomic_floats.a = 3.5f;
    atomic_floats.b = -6.125f;
    if (atomic_abi_atomic_float_second(atomic_floats) != -6.125f) return 51;
    if (atomic_abi_atomic_float_after_five(1.0f, 2.0f, 4.0f, 8.0f, 16.0f, atomic_floats) != 31.0f - 6.125f) return 52;
    struct atomic_float_member made_atomic_float = atomic_abi_make_atomic_float(-0.5f, 128.25f);
    if (made_atomic_float.a != -0.5f || made_atomic_float.b != 128.25f) return 53;
#endif

    return 0;
}
