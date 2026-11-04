// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original input for #1115's whole-program -S corpus (assembler_programs_test.c): narrow and wide string literals,
// which compilers place in merged string sections of 1-, 2- and 4-byte characters (.rodata.str4.4,"aMS",@progbits,4),
// and stdio. No generated assembly is checked in.

#include <stdio.h>
#include <uchar.h>
#include <wchar.h>

const wchar_t* strings_wide = L"hello wide";
const char16_t* strings_sixteen = u"sixteen bits";
const char32_t* strings_thirty_two = U"thirty-two";
static const wchar_t strings_array[] = L"array of wide characters";

unsigned long strings_sum_wide(const wchar_t* text)
{
    unsigned long sum = 0;
    for (const wchar_t* at = text; *at != 0; at += 1)
    {
        sum = sum * 31 + (unsigned long)*at;
    }
    return sum;
}

unsigned long strings_sum_sixteen(const char16_t* text)
{
    unsigned long sum = 0;
    for (const char16_t* at = text; *at != 0; at += 1)
    {
        sum = sum * 37 + *at;
    }
    return sum;
}

unsigned long strings_sum_thirty_two(const char32_t* text)
{
    unsigned long sum = 0;
    for (const char32_t* at = text; *at != 0; at += 1)
    {
        sum = sum * 41 + *at;
    }
    return sum;
}

const wchar_t* strings_pick(int which)
{
    return which == 0 ? L"zero" : which == 1 ? L"one" : L"many";
}

int main(void)
{
    const unsigned long sums[] = {
        strings_sum_wide(strings_wide),
        strings_sum_sixteen(strings_sixteen),
        strings_sum_thirty_two(strings_thirty_two),
        strings_sum_wide(strings_array),
        strings_sum_wide(strings_pick(0)) + strings_sum_wide(strings_pick(1)) + strings_sum_wide(strings_pick(7)),
    };
    printf("%s: %lu %lu %lu %lu %lu %zu\n", "strings", sums[0], sums[1], sums[2], sums[3], sums[4], sizeof(strings_array) / sizeof(strings_array[0]));
    puts("narrow literal");
    return strings_wide[1] == L'e' && strings_sixteen[2] == u'x' && strings_thirty_two[9] == U'o' && strings_pick(1)[2] == L'e' ? 0 : 1;
}
