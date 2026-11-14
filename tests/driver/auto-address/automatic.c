// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
void first(void)
{
    __attribute__((address_space(1))) __auto_type local = 1;
}
void second(int *value)
{
    __auto_type local __attribute__((address_space(2))) = value;
}
void third(int *value)
{
    __auto_type *__attribute__((address_space(3))) local = value;
}
