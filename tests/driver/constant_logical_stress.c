// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Long constant-object expressions and shared-middle conditional values retain many dependencies.

const int logical_stress_value = 4;

int logical_deep_constant(int x)
{
    return x && (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value)))))))))))))))))))))))))))))))))))))))))))))))));
}

int logical_shared_constant(int x)
{
    return x || ((((logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value + (logical_stress_value))))))))))))))))))))))))))))))))))))))))))))))))) ?: 0) ?: (logical_stress_value + 1)) ?: 0);
}

int logical_shared_zero(int x)
{
    const int zero = 0;
    const int value = 8;
    return x && ((zero ?: value) ?: ((zero ?: (value + 1)) ?: 0));
}

int logical_shared_fallback(int x)
{
    const int zero = 0;
    const int value = 8;
    return x || ((zero + 0) ?: ((zero ?: value) ?: (value + 1)));
}

int logical_shared_boolean(int x)
{
    const int zero = 0;
    return x && ((zero ?: zero) ?: 1);
}
