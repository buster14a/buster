// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

__attribute__((target("default"))) int selected(int value)
{
    return value + 11;
}

__attribute__((target("sse4.2"))) int selected(int value)
{
    return value + 11;
}

__attribute__((target("arch=haswell"))) int selected(int value)
{
    return value + 11;
}

__attribute__((target_clones("bmi2", "default"))) int cloned(int value)
{
    return value + 20;
}

__attribute__((cpu_specific(generic))) int dispatched(int value)
{
    return value + 7;
}

__attribute__((cpu_specific(haswell))) int dispatched(int value)
{
    return value + 7;
}

__attribute__((cpu_dispatch(generic, haswell))) int dispatched(int value);

int main(void)
{
    int (*address)(int) = selected;
    return address(11) + cloned(0) == 42 && dispatched(35) == 42 ? 0 : 1;
}
