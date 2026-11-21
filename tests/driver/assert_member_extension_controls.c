// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
__extension__ _Static_assert(1, "file assertion");
__extension__ __extension__ _Static_assert(1, "file repeated extension");
struct Accepted {
    __extension__ int first;
    _Static_assert(sizeof(int) > 0, "member assertion");
    __extension__ __extension__ int second;
};
void accepted(void)
{
    __extension__ _Static_assert(1, "block assertion");
    __extension__ __extension__ _Static_assert(1, "block repeated extension");
    struct Local { __extension__ int member; _Static_assert(1, "member assertion"); };
}
int members(struct Accepted value) { return value.first + value.second; }
