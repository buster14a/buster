// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// GNU's saved opaque condition and explicit casts do not ask for these source conversion warnings.
int omitted_shift(void) { return sizeof((1 << 2) ?: 4); }
int omitted_float(void) { return sizeof(0.5 ?: 4); }
int omitted_selected(void) { return _Generic(0, int: (1 << 2) ?: 4, default: 0.5 ?: 4); }
typedef int OmittedBound[(1 << 2) ?: 4];
typedef __typeof__(0.5 ?: 4) OmittedQuery;
int cast_int(void) { return sizeof((int)(1 << 2) ? 3 : 4); }
int cast_bool(void) { return sizeof((_Bool)(1 << 2) ? 3 : 4); }
int cast_double(void) { return sizeof((double)0.5 ? 3 : 4); }
int cast_float(void) { return sizeof((float)0.5 ? 3 : 4); }
int cast_inner(void) { return sizeof(((int)1 << 2) ? 3 : 4); }
int literal_arms(void) { return sizeof((1 ? 1 : 0) ? 3 : 4) + sizeof((1 ? 0 : 3) ? 3 : 4); }
int arithmetic(void) { return sizeof((1 * 2) ? 3 : 4) + sizeof((1 + 2) ? 3 : 4); }
_Static_assert(sizeof((_Bool)(1 << 2) ? 3 : 4) != 0, "cast passes");
