// First-party caller: external prototypes keep each boundary across translation units.
// Exact FP constants and distinct signed integers avoid rounding and commutative checks.
#include "scalar_boundary.h"

volatile int abi_boundary_calls;

int main(void)
{
    int bad = 0;
    volatile int effect;
    bad |= abi_boundary_gp3(0x12345678, -0x102030405ll, -73) != 0;
    bad |= abi_boundary_gp4(0x12345678, -0x102030405ll, -73, 0x506070809ll) != 0;
    bad |= abi_boundary_gp5(0x12345678, -0x102030405ll, -73, 0x506070809ll, 13579) != 0;
    bad |= abi_boundary_gp6(0x12345678, -0x102030405ll, -73, 0x506070809ll, 13579, -0x304050607ll) != 0;
    bad |= abi_boundary_gp7(0x12345678, -0x102030405ll, -73, 0x506070809ll, 13579, -0x304050607ll, -24680) != 0;
    bad |= abi_boundary_gp8(0x12345678, -0x102030405ll, -73, 0x506070809ll, 13579, -0x304050607ll, -24680, 0x708090a0bll) != 0;
    bad |= abi_boundary_gp9(0x12345678, -0x102030405ll, -73, 0x506070809ll, 13579, -0x304050607ll, -24680, 0x708090a0bll, 97531) != 0;
    bad |= abi_boundary_fp3(1.25f, -2.5, 3.75f) != 0;
    bad |= abi_boundary_fp4(1.25f, -2.5, 3.75f, -4.125) != 0;
    bad |= abi_boundary_fp5(1.25f, -2.5, 3.75f, -4.125, 5.5f) != 0;
    bad |= abi_boundary_fp7(1.25f, -2.5, 3.75f, -4.125, 5.5f, -6.75, 7.25f) != 0;
    bad |= abi_boundary_fp8(1.25f, -2.5, 3.75f, -4.125, 5.5f, -6.75, 7.25f, -8.5) != 0;
    bad |= abi_boundary_fp9(1.25f, -2.5, 3.75f, -4.125, 5.5f, -6.75, 7.25f, -8.5, 9.75f) != 0;
    bad |= abi_boundary_positional5(-0x102030405ll, -2.5, -73, 3.75f, 13579) != 0;
    bad |= abi_boundary_gp_fp_tail(0x12345678, -0x102030405ll, -73, 0x506070809ll, 13579, -0x304050607ll, 1.25f, -2.5, -24680) != 0;
    bad |= abi_boundary_fp_gp_tail(1.25f, -2.5, 3.75f, -4.125, 5.5f, -6.75, 7.25f, -8.5, 0x12345678, -0x102030405ll, 9.75f) != 0;
    bad |= abi_boundary_packed_stack(0x12345678, -0x102030405ll, -73, 0x506070809ll, 13579, -0x304050607ll, -24680, 0x708090a0bll, 27182, -31415, -0.625f, 0x112233445566ll, 13.25) != 0;
    effect = 85;
    bad |= abi_boundary_pointer4(0x12345678, -0x102030405ll, -73, 0x506070809ll, &effect) != 0;
    bad |= effect != 324;
    effect = 85;
    bad |= abi_boundary_pointer6(0x12345678, -0x102030405ll, -73, 0x506070809ll, 13579, -0x304050607ll, &effect) != 0;
    bad |= effect != 326;
    effect = 85;
    bad |= abi_boundary_pointer8(0x12345678, -0x102030405ll, -73, 0x506070809ll, 13579, -0x304050607ll, -24680, 0x708090a0bll, &effect) != 0;
    bad |= effect != 328;
    bad |= abi_boundary_calls != 20;
#if defined(ABI_BOUNDARY_BAD_EXPECTATION)
    // One deliberately wrong value proves failures survive linking and process accounting.
    bad |= effect != 329;
#endif
    return bad ? 73 : 0;
}
