// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Logical conversions have their own unevaluated suppression; nested ordinary conditionals still warn.
int plain_and(int x) { return x && (1 << 2); }
int plain_or(int x) { return x || (1 << 2); }
int plain_not(void) { return !(1 << 2); }
int plain_float_and(int x) { return x && 0.5; }
int plain_float_not(void) { return !0.5; }
int size_and(int x) { return sizeof(x && (1 << 2)); }
int size_float(int x) { return sizeof(x || 0.5); }
int size_not(void) { return sizeof(!(1 << 2)) + sizeof(!0.5); }
int align_and(int x) { return __alignof__(x && (1 << 2)); }
int generic_and(int x) { return _Generic(x && (1 << 2), int: 1); }
int generic_float(int x) { return _Generic(x && 0.5, int: 1); }
int selected_and(int x) { return _Generic(0, int: x && (1 << 2), default: 0); }
int selected_float(int x) { return _Generic(0, int: x && 0.5, default: 0); }
int unselected_and(int x) { return _Generic(0, int: 1, default: x && (1 << 2)); }
int unselected_float(int x) { return _Generic(0, int: 1, default: x && 0.5); }
typedef __typeof__(1 && (1 << 2)) LogicalType;
typedef __typeof__(1 && 0.5) FloatLogicalType;
typedef int LogicalBound[1 && (1 << 2)];
typedef int FloatLogicalBound[1 && 0.5];
int size_bound(void) { return sizeof(int[1 && (1 << 2)]) + sizeof(int[1 && 0.5]); }
_Static_assert(1 && (1 << 2), "logical assertion");
_Static_assert(1 && 0.5, "floating logical assertion");
_Static_assert(sizeof(1 && (1 << 2)), "unevaluated logical assertion");
int short_and(void) { return 0 && (1 << 2); }
int short_float(void) { return 1 || 0.5; }
int inner_logical(int x) { return sizeof((x && (1 << 2)) ? 4 : 5); }
int inner_conditional(int x) { return sizeof(x && ((1 << 2) ? 4 : 5)); }
int inner_float(int x) { return sizeof(x && (0.5 ? 4 : 5)); }
