// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The assertion is not read as a member assertion after __extension__.
struct Single { int before; __extension__ _Static_assert(1, "ignored"); };
union Repeated { __extension__ __extension__ _Static_assert(0, "unread failure"); };
struct Multiline {
    int before;
    __extension__
    _Static_assert(1, "ignored");
};
void local(void)
{
    struct Local { int before; __extension__ _Static_assert(1, "ignored"); };
}
