// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Nested conditions publish in construction order, even when the graph's selection keeps only one operand.
int nested_ternary(void) { return sizeof((1 << 2) ? ((1 << 3) ? 4 : 5) : ((1 << 4) ? 6 : 7)); }
int nested_generic(void) { return _Generic((1 << 2) ? 3 : 4, int: (1 << 3) ? 3 : 4, default: (1 << 4) ? 3 : 4); }
int nested_all(void) { return sizeof((1 << 2) ? _Generic((1 << 3) ? 3 : 4, int: (1 << 4) ? 3 : 4, default: 0.5 ? 5 : 6) : ((1 << 5) ? 3 : 4)); }
typedef int NestedBound[sizeof((1 << 2) ? 3 : 4) + ((1 << 3) ? 3 : 4)];
typedef __typeof__((0.5 ? 2 : 3) ? 4 : 5) NestedQuery;
int outer_cast(void) { return sizeof((int)((1 << 2) ? 3 : 4)); }
int association_type(void) { return _Generic(0, __typeof__((1 << 2) ? 3 : 4): 1); }
_Static_assert(_Generic((1 << 2) ? 3 : 4, int: (1 << 3) ? 3 : 4, default: (1 << 4) ? 3 : 4) != 0, "nested selection passes");
