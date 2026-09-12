// Register-pressure corpus. The self-host sources keep few values live at
// once, so they cannot tell a global allocator from a local one; these
// bodies deliberately hold more values live than the machine has
// registers, across loops and across calls, which is where allocation
// policy decides the generated code. Correctness is self-checking so the
// file doubles as an execution test under every allocator mode.

static unsigned long long mix(unsigned long long value)
{
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 29;
    return value;
}

// Sixteen values live simultaneously across a loop: more than the
// allocatable file, so something must spill and the choice of what
// separates a good allocator from a bad one.
static unsigned long long wide_live_loop(unsigned long long seed, unsigned int rounds)
{
    unsigned long long a0 = seed + 1;
    unsigned long long a1 = seed + 2;
    unsigned long long a2 = seed + 3;
    unsigned long long a3 = seed + 4;
    unsigned long long a4 = seed + 5;
    unsigned long long a5 = seed + 6;
    unsigned long long a6 = seed + 7;
    unsigned long long a7 = seed + 8;
    unsigned long long a8 = seed + 9;
    unsigned long long a9 = seed + 10;
    unsigned long long a10 = seed + 11;
    unsigned long long a11 = seed + 12;
    unsigned long long a12 = seed + 13;
    unsigned long long a13 = seed + 14;
    unsigned long long a14 = seed + 15;
    unsigned long long a15 = seed + 16;
    for (unsigned int round = 0; round < rounds; round += 1)
    {
        a0 += a1 ^ a15;
        a1 += a2 ^ a0;
        a2 += a3 ^ a1;
        a3 += a4 ^ a2;
        a4 += a5 ^ a3;
        a5 += a6 ^ a4;
        a6 += a7 ^ a5;
        a7 += a8 ^ a6;
        a8 += a9 ^ a7;
        a9 += a10 ^ a8;
        a10 += a11 ^ a9;
        a11 += a12 ^ a10;
        a12 += a13 ^ a11;
        a13 += a14 ^ a12;
        a14 += a15 ^ a13;
        a15 += a0 ^ a14;
    }
    return a0 ^ a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6 ^ a7 ^ a8 ^ a9 ^ a10 ^ a11 ^ a12 ^ a13 ^ a14 ^ a15;
}

// Eight values live across a call in every iteration. Caller-saved
// registers cannot hold them, so each one either occupies a callee-saved
// register for the loop or pays a store and a load per call.
static unsigned long long call_crossing_loop(unsigned long long seed, unsigned int rounds)
{
    unsigned long long b0 = seed | 1;
    unsigned long long b1 = seed | 2;
    unsigned long long b2 = seed | 4;
    unsigned long long b3 = seed | 8;
    unsigned long long b4 = seed | 16;
    unsigned long long b5 = seed | 32;
    unsigned long long b6 = seed | 64;
    unsigned long long b7 = seed | 128;
    unsigned long long total = 0;
    for (unsigned int round = 0; round < rounds; round += 1)
    {
        total += mix(total ^ round);
        b0 += total ^ b7;
        b1 += total ^ b0;
        b2 += total ^ b1;
        b3 += total ^ b2;
        b4 += total ^ b3;
        b5 += total ^ b4;
        b6 += total ^ b5;
        b7 += total ^ b6;
    }
    return total ^ b0 ^ b1 ^ b2 ^ b3 ^ b4 ^ b5 ^ b6 ^ b7;
}

// A deep expression tree: every intermediate is live until the final
// combine, so the peak is set by the shape of the tree rather than by any
// loop.
static unsigned long long deep_tree(unsigned long long seed)
{
    unsigned long long c0 = mix(seed + 1);
    unsigned long long c1 = mix(seed + 2);
    unsigned long long c2 = mix(seed + 3);
    unsigned long long c3 = mix(seed + 4);
    unsigned long long c4 = mix(seed + 5);
    unsigned long long c5 = mix(seed + 6);
    unsigned long long c6 = mix(seed + 7);
    unsigned long long c7 = mix(seed + 8);
    unsigned long long d0 = (c0 * 3) + (c1 * 5);
    unsigned long long d1 = (c2 * 7) + (c3 * 11);
    unsigned long long d2 = (c4 * 13) + (c5 * 17);
    unsigned long long d3 = (c6 * 19) + (c7 * 23);
    unsigned long long e0 = (d0 ^ d1) + (c0 ^ c7);
    unsigned long long e1 = (d2 ^ d3) + (c1 ^ c6);
    return (e0 * 29) + (e1 * 31) + (d0 ^ d3) + (c2 ^ c5) + (c3 ^ c4);
}

int main(void)
{
    int result = 0;
    if (wide_live_loop(0x9e3779b97f4a7c15ULL, 0) != 0ULL) result = 1;
    if (wide_live_loop(0x9e3779b97f4a7c15ULL, 1) != 982ULL) result = 2;
    if (wide_live_loop(0x9e3779b97f4a7c15ULL, 64) != 13132513170701207948ULL) result = 3;
    if (call_crossing_loop(0xc2b2ae3d27d4eb4fULL, 0) != 176ULL) result = 4;
    if (call_crossing_loop(0xc2b2ae3d27d4eb4fULL, 1) != 415753958466312104ULL) result = 5;
    if (call_crossing_loop(0xc2b2ae3d27d4eb4fULL, 64) != 5876330639124109335ULL) result = 6;
    if (deep_tree(0ULL) != 10029734777502406277ULL) result = 7;
    if (deep_tree(1ULL) != 15439450406776879265ULL) result = 8;
    if (deep_tree(18446744073709551615ULL) != 11627460155176994222ULL) result = 9;
    return result;
}
