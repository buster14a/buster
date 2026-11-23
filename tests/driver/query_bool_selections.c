// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// A conditional's source condition passes through static selections; explicit casts still stop the warning.
int shift_generic(void) { return _Generic(0, int: (1 << 2)) ? 3 : 4; }
int float_generic(void) { return _Generic(0, int: 0.5) ? 3 : 4; }
int shift_choose(void) { return __builtin_choose_expr(1, (1 << 2), 0) ? 3 : 4; }
int float_choose(void) { return __builtin_choose_expr(1, 0.5, 0) ? 3 : 4; }
int shift_generic_query(void) { return sizeof(_Generic(0, int: (1 << 2)) ? 3 : 4); }
int float_generic_query(void) { return sizeof(_Generic(0, int: 0.5) ? 3 : 4); }
int shift_choose_query(void) { return sizeof(__builtin_choose_expr(1, (1 << 2), 0) ? 3 : 4); }
int float_choose_query(void) { return sizeof(__builtin_choose_expr(1, 0.5, 0) ? 3 : 4); }
int shift_generic_boolcast(void) { return sizeof((_Bool)_Generic(0, int: (1 << 2)) ? 3 : 4); }
int float_generic_boolcast(void) { return sizeof((_Bool)_Generic(0, int: 0.5) ? 3 : 4); }
int shift_choose_boolcast(void) { return sizeof((_Bool)__builtin_choose_expr(1, (1 << 2), 0) ? 3 : 4); }
int float_choose_boolcast(void) { return sizeof((_Bool)__builtin_choose_expr(1, 0.5, 0) ? 3 : 4); }
int shift_generic_innercast(void) { return sizeof(_Generic(0, int: (int)(1 << 2)) ? 3 : 4); }
int float_generic_innercast(void) { return sizeof(_Generic(0, int: (double)0.5) ? 3 : 4); }
int nested_generic(void) { return sizeof(_Generic(0, int: _Generic(0, int: (1 << 2))) ? 3 : 4); }
int nested_choose(void) { return sizeof(__builtin_choose_expr(1, _Generic(0, int: (1 << 2)), 0) ? 3 : 4); }
int nested_condition(void) { return sizeof(_Generic(0, int: (1 ? 2 : 3)) ? 3 : 4); }
int float_generic_unselected(void) { return sizeof(_Generic(0, int: 0, default: 0.5) ? 3 : 4); }
