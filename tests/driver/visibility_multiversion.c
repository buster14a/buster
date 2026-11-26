// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// #3578: every symbol made for a multiversioned function keeps its declaration's pragma visibility.

int outside(void)
{
    return 1;
}

#pragma GCC visibility push(hidden)
__attribute__((target_clones("default", "avx2"))) int later_clones(int x);
__attribute__((target("default"))) int later_target(void);
__attribute__((target("avx2"))) int later_target(void);
__attribute__((cpu_specific(generic))) int later_cpu(void);
__attribute__((cpu_specific(haswell))) int later_cpu(void);
__attribute__((cpu_dispatch(generic, haswell))) int later_cpu(void);

__attribute__((target_clones("default", "avx2"))) int hidden_clones(int x)
{
    return x + 2;
}

__attribute__((target("default"))) int hidden_target(void)
{
    return 3;
}

__attribute__((target("avx2"))) int hidden_target(void)
{
    return 3;
}

__attribute__((cpu_specific(generic))) int hidden_cpu(void)
{
    return 4;
}

__attribute__((cpu_specific(haswell))) int hidden_cpu(void)
{
    return 4;
}

__attribute__((cpu_dispatch(generic, haswell))) int hidden_cpu(void);

#pragma GCC visibility push(protected)
__attribute__((target_clones("avx2", "default"))) int protected_clones(int x)
{
    return x + 5;
}

__attribute__((target("default"))) int protected_target(void)
{
    return 6;
}

__attribute__((target("avx2"))) int protected_target(void)
{
    return 6;
}

__attribute__((cpu_specific(generic, haswell))) int protected_cpu(void)
{
    return 7;
}

__attribute__((cpu_dispatch(generic, haswell))) int protected_cpu(void);

#pragma GCC visibility push(default)
__attribute__((target_clones("default", "avx2"))) int default_clones(int x)
{
    return x + 8;
}

__attribute__((target("default"))) int default_target(void)
{
    return 9;
}

__attribute__((target("avx2"))) int default_target(void)
{
    return 9;
}

__attribute__((cpu_specific(generic))) int default_cpu(void)
{
    return 10;
}

__attribute__((cpu_specific(haswell))) int default_cpu(void)
{
    return 10;
}

__attribute__((cpu_dispatch(generic, haswell))) int default_cpu(void);
#pragma GCC visibility pop

int protected_after(void)
{
    return 11;
}
#pragma GCC visibility pop

int hidden_after(void)
{
    return 12;
}
#pragma GCC visibility pop

// The declarations' visibility survives the pop before their definitions and synthetic symbols are emitted.
__attribute__((target_clones("default", "avx2"))) int later_clones(int x)
{
    return x + 13;
}

__attribute__((target("default"))) int later_target(void)
{
    return 14;
}

__attribute__((target("avx2"))) int later_target(void)
{
    return 14;
}

__attribute__((cpu_specific(generic))) int later_cpu(void)
{
    return 15;
}

__attribute__((cpu_specific(haswell))) int later_cpu(void)
{
    return 15;
}

int main(void)
{
    const int hidden = hidden_clones(1) + hidden_target() + hidden_cpu();
    const int protected = protected_clones(1) + protected_target() + protected_cpu();
    const int visible = default_clones(1) + default_target() + default_cpu();
    const int later = later_clones(1) + later_target() + later_cpu();
    return hidden != 10 || protected != 19 || visible != 28 || later != 43 || outside() != 1 || protected_after() != 11 || hidden_after() != 12;
}
