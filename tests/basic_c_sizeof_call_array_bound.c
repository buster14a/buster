// An array bound of sizeof over a direct call is a constant expression: the
// call is never evaluated and the operand's type is the declared return
// type. The type-mapping fixed point used to fail the bound (no signatures
// exist that early), classifying the local as a VLA — observable as a
// rejection once the local is static, and as a wrongly dynamic frame
// otherwise. The counter proves the calls never run.
static int calls;

static long counted_long(void)
{
    calls += 1;
    return 7;
}

static short counted_short(int payload)
{
    calls += 1;
    return (short)payload;
}

static char file_scope[sizeof(counted_long())];

int main(void)
{
    // The flagged shape: a static local whose bound sizes a call — a VLA
    // classification rejects this outright.
    static char stationary[sizeof(counted_long())];
    stationary[0] = 1;
    if (sizeof(stationary) != sizeof(long) || stationary[0] != 1)
    {
        return 1;
    }
    // The automatic sibling is a constant-sized array, not a VLA.
    char automatic[sizeof(counted_long())];
    automatic[7] = 2;
    if (sizeof(automatic) != sizeof(long) || automatic[7] != 2)
    {
        return 2;
    }
    // File scope, a second dimension, and the bound inside arithmetic.
    file_scope[7] = 3;
    static short two_d[sizeof(counted_long())][2];
    two_d[7][1] = 4;
    char combined[sizeof(counted_short(9)) * 2 + 1];
    combined[4] = 5;
    if (sizeof(file_scope) != sizeof(long) || file_scope[7] != 3)
    {
        return 3;
    }
    if (sizeof(two_d) != sizeof(long) * 2 * sizeof(short) || two_d[7][1] != 4)
    {
        return 4;
    }
    if (sizeof(combined) != sizeof(short) * 2 + 1 || combined[4] != 5)
    {
        return 5;
    }
    // None of the bounded calls ever ran; an evaluated control still does.
    if (calls != 0)
    {
        return 6;
    }
    if (counted_short(3) != 3 || calls != 1)
    {
        return 7;
    }
    return 0;
}
