// A subscript holding `?:`, `&&`, or a comma inside a parenthesized comma
// group is lowered with the group, not by the prepass that normally hoists it.
// Both a plain subscript and an indirect call through a subscripted function
// table must still lower, evaluate the subscript once, and keep the order of
// the comma operands.

static long long table[4] = {10, 20, 30, 40};
static long long calls;
static long long trace;

static long long pick(long long x)
{
    calls += 1;
    trace = trace * 10 + x;
    return x;
}

static long long add(long long a, long long b)
{
    return a * 3 + b;
}

static long long sub(long long a, long long b)
{
    return a - b;
}

static long long (*const ops[2])(long long, long long) = {add, sub};

static long long plain_index(long long w)
{
    long long r = 1 + (w, table[w ? 1 : 2]);
    long long s = (long long)(w = 3, table[w ? 3 : 0]);
    return r * 100 + s;
}

static long long cast_index(long long w)
{
    return (long long)(trace = 7, table[w && w > 1]) + (w, table[(w, 3)]);
}

static long long postfix_index(long long w)
{
    long long before = table[1];
    long long r = (long long)(w, table[w ? 1 : 2]++);
    return r * 1000 + before + table[1] * 10000;
}

static long long indirect_conditional(long long v)
{
    calls = 0;
    trace = 0;
    long long r = (long long)(trace = 5, ops[v ? 0 : pick(1)](6, 2)) + 1;
    return r * 100 + calls * 10 + trace;
}

static long long indirect_logical(long long v)
{
    calls = 0;
    trace = 0;
    long long r = 1 + (v, ops[v && pick(1)](9, 4));
    return r * 100 + calls * 10 + trace;
}

static long long indirect_comma(long long v)
{
    calls = 0;
    trace = 0;
    long long r = (long long)(v, ops[(v, pick(1))](9, 4)) + 1;
    return r * 100 + calls * 10 + trace;
}

int main(void)
{
    if (plain_index(0) != 31 * 100 + 40)
    {
        return 1;
    }
    if (plain_index(5) != 21 * 100 + 40)
    {
        return 2;
    }
    if (cast_index(0) != 10 + 40)
    {
        return 3;
    }
    if (cast_index(2) != 20 + 40)
    {
        return 4;
    }
    if (postfix_index(1) != 20 * 1000 + 20 + 21 * 10000)
    {
        return 5;
    }
    // v == 0: pick(1) runs once, selecting sub: 6 - 2 + 1 = 5.
    if (indirect_conditional(0) != 5 * 100 + 1 * 10 + 51)
    {
        return 6;
    }
    // v != 0: pick is not called, add: 6 * 3 + 2 + 1 = 21.
    if (indirect_conditional(1) != 21 * 100 + 0 * 10 + 5)
    {
        return 7;
    }
    // v == 0 short-circuits: add: 9 * 3 + 4 + 1 = 32.
    if (indirect_logical(0) != 32 * 100)
    {
        return 8;
    }
    // v != 0: pick(1) runs once and yields 1: sub: 9 - 4 + 1 = 6.
    if (indirect_logical(1) != 6 * 100 + 1 * 10 + 1)
    {
        return 9;
    }
    if (indirect_comma(0) != (9 - 4 + 1) * 100 + 1 * 10 + 1)
    {
        return 10;
    }
    return 0;
}
