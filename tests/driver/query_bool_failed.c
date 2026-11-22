// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Retained query warnings precede failed-assertion errors and obey their limits without replacing old output.
_Static_assert(sizeof((1 << 2) ? 3 : 4) == 0, "size fails");
_Static_assert(_Generic((1 << 2) ? 3 : 4, int: 1, default: 0.5 ? 3 : 4) == 0, "selection fails");
void failed_body(void)
{
    _Static_assert(sizeof(__typeof__((1 << 2) ? 3 : 4)) == 0, "body typeof fails");
}
_Static_assert(sizeof(__typeof__(0.5 ? 3 : 4)) == 0, "floating type fails");
