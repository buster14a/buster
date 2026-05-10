#pragma once
#include <buster/integer.h>

u64 align_forward(u64 n, u64 a)
{
    u64 mask = a - 1;
    u64 result = (n + mask) & ~mask;
    return result;
}

u64 next_power_of_two(u64 n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;
    return n;
}

u8 trailing_zeroes_u32(u32 n)
{
#if defined(__clang__)
    return (u8)__builtin_ctzg(n);
#else
    u32 result;

    __asm__ volatile (
        "tzcnt %1, %0"
        : "=r"(result)
        : "rm"(n)
        : "cc"
    );

    return (u8)result;
#endif
}

u8 trailing_zeroes_u64(u64 n)
{
#if defined(__clang__)
    return (u8)__builtin_ctzg(n);
#else
    u64 result;

    __asm__ volatile (
        "tzcntq %1, %0"
        : "=r"(result)
        : "rm"(n)
        : "cc"
    );

    return (u8)result;
#endif
}

u8 leading_zeroes_u32(u32 n)
{
#if defined(__clang__)
    return (u8)__builtin_clzg(n);
#else
    u32 result;

    __asm__ volatile (
        "lzcnt %1, %0"
        : "=r"(result)
        : "rm"(n)
        : "cc"
    );

    return (u8)result;
#endif
}

u8 leading_zeroes_u64(u64 n)
{
#if defined(__clang__)
    return (u8)__builtin_clzg(n);
#else
    u64 result;

    __asm__ volatile (
        "lzcntq %1, %0"
        : "=r"(result)
        : "rm"(n)
        : "cc"
    );

    return (u8)result;
#endif
}
