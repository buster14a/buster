// C labels precede their dispatch, but the selector may reorder MIR blocks.
// Twelve values stay live across loops, shared destinations and fallthrough;
// the companion synthetic MIR fixture fixes actual table-backedge layout.
// The registered test stringifies these exact tokens for mobile portability;
// standalone compilation keeps them as ordinary C functions.
#ifndef BUSTER_QUALITY_SWITCH_FIXTURE
#define BUSTER_QUALITY_SWITCH_FIXTURE(...) __VA_ARGS__
#endif
BUSTER_QUALITY_SWITCH_FIXTURE(
unsigned long long quality_switch_cfg(unsigned seed, unsigned rounds)
{
    unsigned long long a = seed;
    unsigned long long v0 = seed + 1, v1 = seed + 2, v2 = seed + 3;
    unsigned long long v3 = seed + 4, v4 = seed + 5, v5 = seed + 6;
    unsigned long long v6 = seed + 7, v7 = seed + 8, v8 = seed + 9;
    unsigned long long v9 = seed + 10, v10 = seed + 11, v11 = seed + 12;
    unsigned round = 0;
    if (!rounds) goto done;
    goto dispatch;
zero:
    a += v0 + v4 + v8;
    goto step;
two:
    a += v1 + v5 + v9;
    goto step;
three:
    a += v2 + v6 + v10;
    goto step;
other:
    a += v3 + v7 + v11;
    goto step;
dispatch:
    switch ((seed + round) & 7)
    {
        case 0: case 1: goto zero;
        case 2: goto two;
        case 3: goto three;
        case 4: a += 3; /* fall through */
        case 5: a += 5; goto other;
        default: goto other;
    }
step:
    round += 1;
    if (round < rounds) goto dispatch;
done:
    return a;
}

unsigned long long quality_switch_forward(unsigned key, unsigned long long a)
{
    switch (key)
    {
        case 0: a += 3; break;
        case 1: case 99: a ^= 5; break;
        case 2: a *= 7; break;
        case 3: a -= 9; break;
        default: a += 11;
    }
    return a;
}

unsigned long long quality_switch_default_only(unsigned key, unsigned long long a)
{
    switch (key)
    {
        default: a += 11;
    }
    return a;
}
)
#undef BUSTER_QUALITY_SWITCH_FIXTURE
