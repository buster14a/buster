// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

typedef float Float4 __attribute__((vector_size(16)));

__attribute__((target("f16c"))) unsigned short invalid_rounding(float input, int rounding)
{
    __auto_type result = ({
        __builtin_ia32_vcvtps2ph((Float4){ input, 0, 0, 0 }, rounding);
    });
    return result[0];
}
