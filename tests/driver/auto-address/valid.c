// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define SPACE(N) __attribute__((address_space(N)))
SPACE(1) __auto_type integer = 7L;
__auto_type real SPACE(2) = 2.5;
const SPACE(3) __auto_type pointer = (int *)0;
SPACE(4) __auto_type *pointee = (int SPACE(4) *)0;
SPACE(5) __auto_type **nested = (int SPACE(5) **)0;
__auto_type *SPACE(6) pointer_level = (int *)0;
SPACE(0) __auto_type zero = 0;
SPACE(8388580) __auto_type highest = 1;
__attribute__((opencl_global)) __auto_type global = 1;
volatile SPACE(9) __auto_type qualified = 2;
const SPACE(10) _Atomic __auto_type atomic = 3;
__auto_type restrict restricted SPACE(11) = (int *)0;
SPACE(4) __auto_type *restrict restricted_pointer = (int SPACE(4) *)0;
extern SPACE(7) long merged;
SPACE(7) __auto_type merged = 3;
extern long SPACE(4) *const merged_pointer;
SPACE(4) __auto_type *const merged_pointer = (long SPACE(4) *)0;
_Static_assert(_Generic(&integer, long SPACE(1) *: 1, default: 0), "specifier space");
_Static_assert(_Generic(&real, double SPACE(2) *: 1, default: 0), "declarator space");
_Static_assert(_Generic(&pointer, int *const SPACE(3) *: 1, default: 0), "qualified pointer value");
_Static_assert(_Generic(pointee, int SPACE(4) *: 1, default: 0), "pointee space");
_Static_assert(_Generic(nested, int SPACE(5) **: 1, default: 0), "nested pointee space");
_Static_assert(_Generic(&pointer_level, int *SPACE(6) *: 1, default: 0), "pointer level space");
_Static_assert(_Generic(&zero, int SPACE(0) *: 1, default: 0), "target space zero");
_Static_assert(_Generic(&highest, int SPACE(8388580) *: 1, default: 0), "highest target space");
_Static_assert(_Generic(&global, __attribute__((opencl_global)) int *: 1, default: 0), "OpenCL space");
_Static_assert(_Generic(&qualified, volatile int SPACE(9) *: 1, default: 0), "volatile space");
_Static_assert(_Generic(&atomic, const _Atomic(int) SPACE(10) *: 1, default: 0), "atomic space");
_Static_assert(_Generic(&restricted, int *restrict SPACE(11) *: 1, default: 0), "deduced pointer restrict");
_Static_assert(_Generic(&restricted_pointer, int SPACE(4) *restrict *: 1, default: 0), "pointer declarator restrict");
_Static_assert(_Generic(&merged, long SPACE(7) *: 1, default: 0), "redeclaration merge");
_Static_assert(_Generic(&merged_pointer, long SPACE(4) *const *: 1, default: 0), "qualified pointer merge");
void local(void)
{
    static SPACE(8) __auto_type kept = 4.0;
    SPACE(4) __auto_type *p = (int SPACE(4) *)0;
    __auto_type value = kept;
    _Static_assert(_Generic(&kept, double SPACE(8) *: 1, default: 0), "static local space");
    _Static_assert(_Generic(p, int SPACE(4) *: 1, default: 0), "automatic pointer");
    _Static_assert(_Generic(&value, double *: 1, default: 0), "deduced value drops space");
}
