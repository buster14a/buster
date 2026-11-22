// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Existing logical conversion boundaries remain unchanged by retaining conditional source facts.
int choose_selected(int x) { return __builtin_choose_expr(1, x && (1 << 2), 0); }
int choose_float_selected(int x) { return __builtin_choose_expr(1, x && 0.5, 0); }
int choose_unselected(int x) { return __builtin_choose_expr(1, 0, x && (1 << 2)); }
int choose_float_unselected(int x) { return __builtin_choose_expr(1, 0, x && 0.5); }
int choose_condition(void) { return __builtin_choose_expr(1 && (1 << 2), 0, 1); }
int choose_sizeof(int x) { return sizeof(__builtin_choose_expr(1, x && (1 << 2), 0)); }
int generic_outer_sizeof(int x) { return sizeof(_Generic(0, int: x && (1 << 2), default: 0)); }
int generic_condition_nested(int x) { return _Generic(_Generic(0, int: x && (1 << 2), default: 0), int: 1); }
int generic_unselected_nested(int x) { return _Generic(0, int: 1, default: _Generic(0, int: x && (1 << 2), default: 0)); }
int cond_unselected_shift(int x) { return 0 ? (x && (1 << 2)) : 0; }
int cond_unselected_float(int x) { return 0 ? (x && 0.5) : 0; }
int constant_shift(void) { return __builtin_constant_p((1 << 2) ? 3 : 4); }
int constant_float(void) { return __builtin_constant_p(0.5 ? 3 : 4); }
int constant_and(void) { return __builtin_constant_p(1 && (1 << 2)); }
int constant_float_and(void) { return __builtin_constant_p(1 && 0.5); }
