// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

__attribute__((noinline, patchable_function_entry(18, 4))) int first(int x)
{
    return x + 3;
}

__attribute__((noinline, patchable_function_entry(6, 6), section(".text.patchable"))) int second(int x)
{
    return first(x) + 1;
}

__attribute__((noinline, patchable_function_entry(3))) int third(int x)
{
    return second(x) * 2;
}

__attribute__((noinline, patchable_function_entry(7, 1, "custom,patch"))) static int fourth(int x)
{
    return third(x) + 2;
}

__attribute__((noinline, patchable_function_entry(5, 0, "patch$entries"))) int fifth(int x)
{
    return fourth(x) + 3;
}

int main(int argc, char** argv)
{
    return fifth(argc) != 15;
}
