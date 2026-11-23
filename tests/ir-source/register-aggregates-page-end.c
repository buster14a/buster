// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// #3311's call sites: a by-value struct argument in registers is passed as its parts, each loaded from the object at
// its own width (ir_sroa_call_part_type). Each object here ends where an inaccessible page starts, so a part loaded as
// a whole eightbyte (the second of a 12- or 10-byte struct, or the only one of a 1- or 4-byte one) faults. Native only: the
// IR interpreter has no mmap.
#include <stdio.h>
#include <sys/mman.h>

typedef struct { int a, b, c; } I3;
typedef struct { char c; } One;
typedef struct { short a; char b; } ShortChar;
typedef struct { float x, y, z; } F3;
typedef struct { long a; int b; } LongInt;
typedef struct { short s[5]; } Shorts;

volatile int vz;

__attribute__((noinline)) static long take_i3(I3 v)
{
    return v.a + v.b * 10 + v.c * 100;
}

__attribute__((noinline)) static long take_one(One v)
{
    return v.c;
}

__attribute__((noinline)) static long take_short_char(ShortChar v)
{
    return v.a * 10 + v.b;
}

__attribute__((noinline)) static long take_f3(F3 v)
{
    return (long)(v.x + v.y * 10.0f + v.z * 100.0f);
}

__attribute__((noinline)) static long take_long_int(LongInt v)
{
    return v.a * 3 + v.b;
}

__attribute__((noinline)) static long take_shorts(Shorts v)
{
    return v.s[0] + v.s[4] * 10;
}

// Each object passed from a pointer to the page's last bytes, in a loop, so that the call's own code scalarizes it.
__attribute__((noinline)) static long page_end(char* end, int n)
{
    long total = 0;
    for (int i = 0; i < n; i++)
    {
        I3* i3 = (I3*)(end - sizeof(I3));
        *i3 = (I3){ i, 2, 3 };
        total += take_i3(*i3);
        One* one = (One*)(end - sizeof(One));
        one->c = (char)i;
        total += take_one(*one);
        ShortChar* sc = (ShortChar*)(end - sizeof(ShortChar));
        *sc = (ShortChar){ (short)i, 3 };
        total += take_short_char(*sc);
        F3* f3 = (F3*)(end - sizeof(F3));
        *f3 = (F3){ 1.0f, (float)i, 3.0f };
        total += take_f3(*f3);
        LongInt* li = (LongInt*)(end - sizeof(LongInt));
        *li = (LongInt){ i, 7 };
        total += take_long_int(*li);
        Shorts* shorts = (Shorts*)(end - sizeof(Shorts));
        *shorts = (Shorts){ { (short)i, 1, 2, 3, 4 } };
        total += take_shorts(*shorts);
    }
    return total;
}

int main(void)
{
    char* page = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int status = 1;
    if (page != MAP_FAILED && mprotect(page + 4096, 4096, PROT_NONE) == 0)
    {
        // main is an inline candidate passing aggregates: its own code too (c_check_tier1.c).
        I3* i3 = (I3*)(page + 4096 - sizeof(I3));
        *i3 = (I3){ 4, 5, 6 };
        const long own = take_i3(*i3);
        printf("page end %ld %ld\n", page_end(page + 4096, 5 + vz), own);
        status = 0;
    }
    return status;
}
