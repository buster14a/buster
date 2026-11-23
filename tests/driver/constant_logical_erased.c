// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

const int logical_erased_integer = 8;
const _Complex int logical_erased_complex = 8 + 4i;
const _Complex double logical_erased_floating = 16.0 + 2.0i;

int logical_erased_sizeof(int x)
{
    return sizeof(x && __builtin_bit_cast(unsigned int, logical_erased_integer)) +
           sizeof(x && __real__ logical_erased_complex) +
           sizeof(x || __imag__ logical_erased_complex) +
           sizeof(x && (int)__real__ logical_erased_floating);
}

int logical_erased_typeof(int x)
{
    __typeof__(x && __builtin_bit_cast(unsigned int, logical_erased_integer)) cast = 0;
    __typeof__(x && __real__ logical_erased_complex) real = 0;
    __typeof__(x || __imag__ logical_erased_complex) imaginary = 0;
    __typeof__(x && (int)__real__ logical_erased_floating) floating = 0;
    return cast + real + imaginary + floating;
}

int logical_erased_component_designators(void)
{
    return sizeof(&__real__ logical_erased_complex) + sizeof(&__imag__ logical_erased_complex);
}

int logical_nested_component_addresses(int x)
{
    return x && (&__real__ (__real__ logical_erased_complex) - &__real__ logical_erased_complex + 4);
}

int logical_erased_runtime_bit_cast(int x)
{
    return sizeof(x && __builtin_bit_cast(unsigned int, (x, 4)));
}

int logical_runtime_builtin_comma(int x)
{
    return (x && __builtin_ctz((x, 16))) + (x && (__builtin_ctz((x, 16)) + 4));
}
