// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

typedef float Float4 __attribute__((vector_size(16)));
typedef short Short8 __attribute__((vector_size(16)));

__attribute__((target("f16c"))) static unsigned short local_result(float input)
{
    __auto_type result = ({
        Short8 halves = __builtin_ia32_vcvtps2ph((Float4){ input, 0, 0, 0 }, 1);
        (unsigned short)halves[0];
    });
    _Static_assert(__builtin_types_compatible_p(__typeof__(result), unsigned short), "local result type");
    return result;
}

__attribute__((target("f16c"))) static unsigned short expression_result(float input)
{
    __auto_type result = ({
        (unsigned short)(((Short8)__builtin_ia32_vcvtps2ph((Float4){ input, 0, 0, 0 }, 1))[0]);
    });
    _Static_assert(__builtin_types_compatible_p(__typeof__(result), unsigned short), "expression result type");
    return result;
}

__attribute__((target("f16c"))) static unsigned short nested_result(float input)
{
    __auto_type result = ({
        __auto_type halves = ({ __builtin_ia32_vcvtps2ph((Float4){ input, 0, 0, 0 }, 1); });
        (unsigned short)halves[0];
    });
    return result;
}

__attribute__((target("f16c"))) static unsigned short direct_result(float input)
{
    __auto_type result = (unsigned short)(((Short8)__builtin_ia32_vcvtps2ph((Float4){ input, 0, 0, 0 }, 1))[0]);
    return result;
}

__attribute__((target("f16c"))) static unsigned short typed_result(float input)
{
    unsigned short result = ({
        Short8 halves = __builtin_ia32_vcvtps2ph((Float4){ input, 0, 0, 0 }, 1);
        (unsigned short)halves[0];
    });
    return result;
}

int tested(int input)
{
    const float value = (float)input * 1.125f;
    const unsigned short direct = direct_result(value);
    const unsigned short local = local_result(value);
    const unsigned short expression = expression_result(value);
    const unsigned short nested = nested_result(value);
    const unsigned short typed = typed_result(value);
    return direct + (local != direct) * 65536 + (expression != direct) * 131072 +
           (nested != direct) * 262144 + (typed != direct) * 524288;
}
