// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// The shared literal-source helper keeps ordinary conversion locations while peeling static selections.
int selected_return(void) { return _Generic(0, int: 1.5); }
int chosen_return(void) { return __builtin_choose_expr(1, 1.5, 0); }
int signed_return(void) { return -_Generic(0, int: 1.5); }
int extension_return(void) { return __extension__ 1.5; }
_Bool selected_bool(void) { return _Generic(0, int: 1.5); }
_Bool chosen_bool(void) { return __builtin_choose_expr(1, 1.5, 0); }
int selected_not(void) { return !_Generic(0, int: 1.5); }
int selected_double_not(void) { return !!_Generic(0, int: 1.5); }
int selected_and(int x) { return _Generic(0, int: 1.5) && x; }
int selected_shift_and(int x) { return _Generic(0, int: (1 << 2)) && x; }
void selected_if(void) { if (_Generic(0, int: 1.5)) { } }
void selected_shift_if(void) { if (_Generic(0, int: (1 << 2))) { } }
int selected_initializer(void) { int x = _Generic(0, int: 1.5); return x; }
int selected_assignment(void) { int x = 0; x = _Generic(0, int: 1.5); return x; }
int takes_int(int);
int selected_argument(void) { return takes_int(_Generic(0, int: 1.5)); }
