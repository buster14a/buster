// Prepared control expressions are found by token position (open slot, emitted
// mark, lowering stack) instead of by scanning the function's list. Each shape
// below reaches one of those answers: a parenthesized assignment hoisted by the
// prepass, a comma group owning a nested assignment, a call argument whose
// interior sits inside an already emitted group, a group nested in one that is
// still lowering, and a typeof operand with an assignment replayed once per
// declarator. A stale or missing table answer re-runs a side effect or skips
// one, which is observable in the counters, so the fixture runs.

// Values and call counts as GCC and Clang compute them for the code below.
#define EXPECT_CONDITIONS 12
#define EXPECT_COMMA 17
#define EXPECT_COMMA_CALLS 3
#define EXPECT_NESTED 34
#define EXPECT_NESTED_CALLS 3
#define EXPECT_ENCLOSING 12
#define EXPECT_TYPEOF 35

static int calls;

static int bump(int value)
{
    calls += 1;
    return value;
}

static int sum(int first, int second)
{
    return first + second;
}

static int conditions(const int* values, int count)
{
    int total = 0;
    int t;
    for (int index = 0; index < count; index += 1)
    {
        if ((t = values[index]) != 0)
        {
            total += t;
        }
    }
    return total;
}

static int comma_groups(int seed)
{
    int a = 0;
    int b = 0;
    int result = (a = bump(seed), (b = bump(a + 1)), a + b);
    return result + (a = bump(2), b = a + 1, a * b);
}

static int nested_calls(int seed)
{
    int t = 0;
    int u = 0;
    return sum((t = bump(seed), t + 1), bump((u = bump(seed + 10), u * 2 + 1)));
}

static int enclosing_group(int seed)
{
    int a = seed;
    int b = 0;
    int c = (bump(a), (b = (a = a + 1) * 2), b + (a = a + 2));
    return c + a;
}

static int typeof_replay(int n)
{
    int k = 0;
    __typeof__(int[(k = n) + 1]) first, second;
    first[0] = (int)sizeof first;
    second[k] = (int)sizeof second;
    return first[0] + second[k] + k;
}

int main(void)
{
    int values[5] = {3, 0, 4, 0, 5};
    int failures = 0;
    int first = conditions(values, 5);
    int second = comma_groups(5);
    int calls_after_comma = calls;
    calls = 0;
    int third = nested_calls(4);
    int calls_after_nested = calls;
    int fourth = enclosing_group(1);
    int fifth = typeof_replay(3);
    failures += first != EXPECT_CONDITIONS;
    failures += second != EXPECT_COMMA || calls_after_comma != EXPECT_COMMA_CALLS;
    failures += third != EXPECT_NESTED || calls_after_nested != EXPECT_NESTED_CALLS;
    failures += fourth != EXPECT_ENCLOSING;
    failures += fifth != EXPECT_TYPEOF;
    return failures;
}
