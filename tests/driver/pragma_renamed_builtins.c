// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#ifdef WITH_ASM_LABEL
extern int unrelated asm("other_name");
#endif

void* memcpy(void*, const void*, unsigned long);
#pragma redefine_extname memcpy renamed_copy

#pragma redefine_extname abs renamed_abs
int abs(int);
long labs(long);
double fabs(double);
void* memmove(void*, const void*, unsigned long);

// Both static functions are emitted at the end of the unit. Only move calls the declaration the pragma labels.
static double magnitude(double x)
{
    return fabs(x);
}

static void move(char* to, const char* from)
{
    memmove(to, from, 8);
}

double fabs(double);
#pragma redefine_extname fabs renamed_fabs
#pragma redefine_extname memmove renamed_move

long exercise(char* to, const char* from, int n)
{
    memcpy(to, from, 8);
    move(to, from);
    return abs(n) + labs(n) + (long)magnitude(n);
}

// The external definition of exercise has already been emitted and its labs call folded.
#pragma redefine_extname labs renamed_labs
