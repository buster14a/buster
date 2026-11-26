// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

__attribute__((selectany, section(".shared"))) int selected_first = 17;
__attribute__((selectany, section(".shared"))) int selected_second = 25;
__attribute__((section(".shared"))) int ordinary = 9;
__attribute__((selectany, section(".shared"))) int* selected_pointer = &selected_first;
__attribute__((selectany)) int selected_zero = 0;
__attribute__((selectany)) const int selected_constant = 7;
__attribute__((selectany)) __thread int selected_tls = 3;
extern __attribute__((alias("selected_first"), selectany)) int selected_alias;
__attribute__((used, retain, section(".retained"))) static int retained = 11;
__attribute__((used, retain, section(".data"))) static int retained_default = 13;

int main(void)
{
    return selected_first != 17 || selected_second != 25 || ordinary != 9 ||
           selected_pointer != &selected_first || *selected_pointer != 17 ||
           selected_zero != 0 || selected_constant != 7 || selected_tls != 3 ||
           selected_alias != 17 || retained != 11 || retained_default != 13;
}
