// Branch layout (docs/machine-rewrite-campaign.md): a jump to the next block
// falls through, and a conditional branch whose taken target is the next
// block branches on the inverted condition to its fallthrough. Every signed
// and unsigned relation takes both directions here, through if/else ladders,
// loops, early exits and switches; main returns the first failing check.

static volatile int sink;

__attribute__((noinline)) static int opaque(int value)
{
    sink = value;
    return value;
}

__attribute__((noinline)) static int relations(int a, int b)
{
    int bits = 0;
    if (a < b) bits |= 1;
    if (a <= b) bits |= 2;
    if (a > b) bits |= 4;
    if (a >= b) bits |= 8;
    if (a == b) bits |= 16;
    if (a != b) bits |= 32;
    if ((unsigned)a < (unsigned)b) bits |= 64;
    if ((unsigned)a <= (unsigned)b) bits |= 128;
    if ((unsigned)a > (unsigned)b) bits |= 256;
    if ((unsigned)a >= (unsigned)b) bits |= 512;
    return bits;
}

__attribute__((noinline)) static int else_relations(long long a, long long b)
{
    int bits = 0;
    if (!(a < b)) bits |= 1; else bits |= 2;
    if (!(a <= b)) bits |= 4; else bits |= 8;
    if (!((unsigned long long)a > (unsigned long long)b)) bits |= 16; else bits |= 32;
    if (!((unsigned long long)a >= (unsigned long long)b)) bits |= 64; else bits |= 128;
    return bits;
}

__attribute__((noinline)) static int ladder(int value)
{
    int result;
    if (value < -10) result = 1;
    else if (value < 0) result = 2;
    else if (value == 0) result = 3;
    else if (value <= 10) result = 4;
    else if (value != 42) result = 5;
    else result = 6;
    return result;
}

__attribute__((noinline)) static int loops(int count)
{
    int total = 0;
    for (int outer = 0; outer < count; outer += 1)
    {
        int inner = outer;
        while (inner > 0)
        {
            if (inner & 1) total += inner;
            else total -= 1;
            inner -= 1;
        }
        do
        {
            total += 2;
        } while (total < outer);
        if (total > 1000) break;
    }
    return total;
}

__attribute__((noinline)) static int switched(int value)
{
    int result = 0;
    switch (value)
    {
        case 0: result = 10; break;
        case 1: result = 11;
        case 2: result += 12; break;
        case 5: result = 15; break;
        case 100: result = 20; break;
        default: result = -1; break;
    }
    return result;
}

__attribute__((noinline)) static int nested(int a, int b, int c)
{
    int result = 0;
    if (a)
    {
        if (b) result = c ? 1 : 2;
        else result = c ? 3 : 4;
    }
    else if (b || c)
    {
        result = (b && c) ? 5 : 6;
    }
    return result;
}

static int expected_relations(int a, int b)
{
    int bits = 0;
    bits |= (a < b) ? 1 : 0;
    bits |= (a <= b) ? 2 : 0;
    bits |= (a > b) ? 4 : 0;
    bits |= (a >= b) ? 8 : 0;
    bits |= (a == b) ? 16 : 0;
    bits |= (a != b) ? 32 : 0;
    bits |= ((unsigned)a < (unsigned)b) ? 64 : 0;
    bits |= ((unsigned)a <= (unsigned)b) ? 128 : 0;
    bits |= ((unsigned)a > (unsigned)b) ? 256 : 0;
    bits |= ((unsigned)a >= (unsigned)b) ? 512 : 0;
    return bits;
}

int main(void)
{
    int result = 0;
    int inputs[] = {-11, -10, -1, 0, 1, 10, 11, 42, 0x7fffffff, (int)0x80000000};
    unsigned count = sizeof(inputs) / sizeof(inputs[0]);
    for (unsigned x = 0; x < count && !result; x += 1)
    {
        int a = opaque(inputs[x]);
        int expected_ladder = a < -10 ? 1 : a < 0 ? 2 : a == 0 ? 3 : a <= 10 ? 4 : a != 42 ? 5 : 6;
        if (ladder(a) != expected_ladder) result = 1;
        for (unsigned y = 0; y < count && !result; y += 1)
        {
            int b = opaque(inputs[y]);
            if (relations(a, b) != expected_relations(a, b)) result = 2;
            long long wa = (long long)a << 3;
            long long wb = (long long)b << 3;
            int expected_else = (wa < wb ? 2 : 1) | (wa <= wb ? 8 : 4) |
                                ((unsigned long long)wa > (unsigned long long)wb ? 32 : 16) |
                                ((unsigned long long)wa >= (unsigned long long)wb ? 128 : 64);
            if (!result && else_relations(wa, wb) != expected_else) result = 3;
        }
    }
    int expected_loops = 0;
    for (int outer = 0; outer < 30; outer += 1)
    {
        int inner = outer;
        while (inner > 0)
        {
            expected_loops += (inner & 1) ? inner : -1;
            inner -= 1;
        }
        do expected_loops += 2; while (expected_loops < outer);
        if (expected_loops > 1000) break;
    }
    if (!result && loops(opaque(30)) != expected_loops) result = 4;
    int switch_inputs[] = {0, 1, 2, 3, 5, 100, -7};
    int switch_expected[] = {10, 23, 12, -1, 15, 20, -1};
    for (unsigned index = 0; index < 7 && !result; index += 1)
    {
        if (switched(opaque(switch_inputs[index])) != switch_expected[index]) result = 5;
    }
    for (int bits = 0; bits < 8 && !result; bits += 1)
    {
        int a = bits & 1, b = (bits >> 1) & 1, c = (bits >> 2) & 1;
        int expected = a ? (b ? (c ? 1 : 2) : (c ? 3 : 4)) : ((b || c) ? ((b && c) ? 5 : 6) : 0);
        if (nested(opaque(a), opaque(b), opaque(c)) != expected) result = 6;
    }
    return result;
}
