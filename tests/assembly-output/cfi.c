// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

extern int backtrace(void** frames, int capacity);

__attribute__((noinline)) int capture(int amount)
{
    volatile unsigned char* allocation = __builtin_alloca((unsigned int)amount);
    void* frames[32];
    allocation[0] = 42;
    const int count = backtrace(frames, 32);
    return count >= 4 && allocation[0] == 42;
}

__attribute__((noinline)) int caller(int amount)
{
    const int captured = capture(amount);
    volatile int retained = amount;
    return captured && retained == amount;
}

int main(void)
{
    return caller(97) ? 0 : 1;
}
