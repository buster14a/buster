// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original input for #1115's whole-program -S corpus (assembler_programs_test.c): dense switches, which GCC and Clang
// compile at -O2 to jump tables of label differences across sections (`.long .L45-.L35` in .rodata, of .text's
// labels), and a table of string pointers. No generated assembly is checked in.

#include <stdio.h>

static const char* const switch_names[] = { "zero", "one", "two", "three", "four", "five", "six", "seven" };

__attribute__((noinline)) int switch_weight(int which)
{
    int weight;
    switch (which)
    {
    case 0:
        weight = 17;
        break;
    case 1:
        weight = 23;
        break;
    case 2:
        weight = 5;
        break;
    case 3:
        weight = 91;
        break;
    case 4:
        weight = 44;
        break;
    case 5:
        weight = 8;
        break;
    case 6:
        weight = 63;
        break;
    case 7:
        weight = 12;
        break;
    case 9:
        weight = 70;
        break;
    default:
        weight = -1;
        break;
    }
    return weight;
}

__attribute__((noinline)) const char* switch_name(unsigned which)
{
    const char* name;
    switch (which & 7)
    {
    case 0:
        name = switch_names[3];
        break;
    case 1:
        name = "first";
        break;
    case 2:
        name = switch_names[5];
        break;
    case 3:
        name = "third";
        break;
    case 4:
        name = switch_names[0];
        break;
    case 5:
        name = "fifth";
        break;
    case 6:
        name = switch_names[7];
        break;
    default:
        name = "last";
        break;
    }
    return name;
}

__attribute__((noinline)) long switch_step(long state, int op)
{
    switch (op)
    {
    case 10:
        return state * 3;
    case 11:
        return state + 7;
    case 12:
        return state - 2;
    case 13:
        return state ^ 0x55;
    case 14:
        return state << 2;
    case 15:
        return state >> 1;
    case 16:
        return -state;
    default:
        return state;
    }
}

int main(void)
{
    long weights = 0;
    for (int i = -1; i < 12; i += 1)
    {
        weights = weights * 3 + switch_weight(i);
    }
    long state = 1;
    for (int op = 9; op < 18; op += 1)
    {
        state = switch_step(state, op);
    }
    unsigned names = 0;
    for (unsigned i = 0; i < 9; i += 1)
    {
        for (const char* at = switch_name(i); *at != 0; at += 1)
        {
            names = names * 31 + (unsigned char)*at;
        }
    }
    printf("switch: %ld %ld %u %s\n", weights, state, names, switch_name(5));
    // Each result is exact: the program checks them, so that a wrong table entry fails it, not only its output.
    return weights == 4652591 && state == -186 && names == 3753832295u ? 0 : 1;
}
