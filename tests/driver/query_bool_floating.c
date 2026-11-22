// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Floating literal conversions survive the same source erasure, including unary signs and both selection arms.
int size_float(void) { return sizeof(0.5 ? 3 : 4); }
int align_float(void) { return __alignof__(-0.5 ? 3 : 4); }
int selected_float(void) { return _Generic(0.5 ? 3 : 4, int: -0.5 ? 3 : 4, default: 2.0 ? 5 : 6); }
int chosen_float(void) { return __builtin_choose_expr(1, 0.5 ? 3 : 4, -0.5 ? 5 : 6); }
typedef int FloatBound[0.5 ? 3 : 4];
typedef __typeof__(-0.5 ? 3 : 4) FloatQuery;
int bound_float(void) { return sizeof(int[2.0 ? 3 : 4]); }
_Static_assert(sizeof(__typeof__(0.5 ? 3 : 4)) != 0, "floating typeof passes");
_Static_assert(__builtin_types_compatible_p(__typeof__(-0.5 ? 3 : 4), int), "floating type passes");
int floating_controls(double d)
{
    return sizeof(0.0 ? 3 : 4) + sizeof(1.0 ? 3 : 4) + sizeof(d ? 3 : 4) + sizeof((0.25 + 0.25) ? 3 : 4);
}
