// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 boundary-width diagnostics and invalid record containment.
typedef int Function(void);
struct Limits
{
    int unsigned_maximum : 18446744073709551615ULL;
    int target_limit : 2305843009213693952ULL;
    Function function_field : 1;
    void void_field : 1;
    int retained;
};
struct Inner
{
    int invalid : -1;
    int retained;
};
struct Outer
{
    struct Inner inner;
    int retained;
};
struct Outer outer = { { 2 }, "quiet" };
struct Inner array[2] = { { 2 }, { "quiet" } };
struct Anonymous
{
    struct { int invalid[-1]; int retained; };
    int retained;
};
struct Anonymous anonymous = { "quiet" };
void uses(struct Outer *p)
{
    int *invalid_subobject = p->inner.retained;
    int *valid_sibling = p->retained;
    (void)invalid_subobject;
    (void)valid_sibling;
}
int after_propagation = unknown_zeta;
