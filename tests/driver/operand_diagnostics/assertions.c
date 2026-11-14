// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
typedef _Atomic(int) Atomic;
_Static_assert((_Atomic int)1, "atomic");
_Static_assert((Atomic)1, "alias");
void f(void) { _Static_assert((_Atomic int)1, "block"); }
struct S { int member; _Static_assert((Atomic)1, "member"); };
_Static_assert((_Atomic int)0, "atomic zero");
_Static_assert((_Atomic float)0.5, "atomic fraction");
_Static_assert((_Atomic int)(1 / 0), "type precedes evaluation");
void cases(int x)
{
    switch (x) { case (Atomic)1: break; }
    switch (x) { case (_Atomic int)2: break; }
}
_Static_assert((((Atomic)1)), "grouped");
_Static_assert((0, (Atomic)1), "comma");
void comma_block(void) { _Static_assert((0, (Atomic)1), "block comma"); }
struct T { int member; _Static_assert((0, (Atomic)1), "member comma"); };
_Static_assert(_Generic(0, int: (Atomic)1), "selected atomic");
_Static_assert(1 ? (Atomic)1 : (Atomic)2, "conditional atomic");
