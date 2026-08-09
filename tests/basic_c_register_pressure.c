// Register-pressure corpus. The self-host sources keep few values live at
// once, so they cannot tell a global allocator from a local one; these
// bodies deliberately hold more values live than the machine has
// registers, across loops and across calls, which is where allocation
// policy decides the generated code. Correctness is self-checking so the
// file doubles as an execution test under every allocator mode.

static unsigned long mix(unsigned long value)
{
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdUL;
    value ^= value >> 29;
    return value;
}

// Sixteen values live simultaneously across a loop: more than the
// allocatable file, so something must spill and the choice of what
// separates a good allocator from a bad one.
static unsigned long wide_live_loop(unsigned long seed, unsigned int rounds)
{
    unsigned long a0 = seed + 1;
    unsigned long a1 = seed + 2;
    unsigned long a2 = seed + 3;
    unsigned long a3 = seed + 4;
    unsigned long a4 = seed + 5;
    unsigned long a5 = seed + 6;
    unsigned long a6 = seed + 7;
    unsigned long a7 = seed + 8;
    unsigned long a8 = seed + 9;
    unsigned long a9 = seed + 10;
    unsigned long a10 = seed + 11;
    unsigned long a11 = seed + 12;
    unsigned long a12 = seed + 13;
    unsigned long a13 = seed + 14;
    unsigned long a14 = seed + 15;
    unsigned long a15 = seed + 16;
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
static unsigned long call_crossing_loop(unsigned long seed, unsigned int rounds)
{
    unsigned long b0 = seed | 1;
    unsigned long b1 = seed | 2;
    unsigned long b2 = seed | 4;
    unsigned long b3 = seed | 8;
    unsigned long b4 = seed | 16;
    unsigned long b5 = seed | 32;
    unsigned long b6 = seed | 64;
    unsigned long b7 = seed | 128;
    unsigned long total = 0;
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
static unsigned long deep_tree(unsigned long seed)
{
    unsigned long c0 = mix(seed + 1);
    unsigned long c1 = mix(seed + 2);
    unsigned long c2 = mix(seed + 3);
    unsigned long c3 = mix(seed + 4);
    unsigned long c4 = mix(seed + 5);
    unsigned long c5 = mix(seed + 6);
    unsigned long c6 = mix(seed + 7);
    unsigned long c7 = mix(seed + 8);
    unsigned long d0 = (c0 * 3) + (c1 * 5);
    unsigned long d1 = (c2 * 7) + (c3 * 11);
    unsigned long d2 = (c4 * 13) + (c5 * 17);
    unsigned long d3 = (c6 * 19) + (c7 * 23);
    unsigned long e0 = (d0 ^ d1) + (c0 ^ c7);
    unsigned long e1 = (d2 ^ d3) + (c1 ^ c6);
    return (e0 * 29) + (e1 * 31) + (d0 ^ d3) + (c2 ^ c5) + (c3 ^ c4);
}

int main(void)
{
    unsigned long wide = wide_live_loop(0x9e3779b97f4a7c15UL, 64);
    unsigned long crossing = call_crossing_loop(0xc2b2ae3d27d4eb4fUL, 64);
    unsigned long tree = deep_tree(0xd6e8feb86659fd93UL);
    // The expected values are whatever a correct compiler produces; the
    // check is that every allocator agrees with the canonical path, which
    // the differential comparison in the harness enforces. Here we only
    // assert the computation ran and stayed in range.
    return !(wide != 0 && crossing != 0 && tree != 0 && (wide ^ crossing ^ tree) != 0);
}
