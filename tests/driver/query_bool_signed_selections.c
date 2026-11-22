// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// One unary sign can surround a selected floating literal or an extension wrapper; two signs or a cast stop it.
typedef int NegativeGeneric[-_Generic(0, int: 0.5) ? 3 : 4];
typedef int PositiveChoose[+__builtin_choose_expr(1, 0.5, 0) ? 3 : 4];
int negative_generic(void) { return sizeof(-_Generic(0, int: 0.5) ? 3 : 4); }
int positive_choose(void) { return sizeof(+__builtin_choose_expr(1, 0.5, 0) ? 3 : 4); }
int negative_extension(void) { return sizeof(-__extension__ 0.5 ? 3 : 4); }
int double_sign(void) { return sizeof(- -0.5 ? 3 : 4); }
int selected_cast(void) { return sizeof(-_Generic(0, int: (double)0.5) ? 3 : 4); }
