// The expression core skips its control prepass, its call prepass and its
// postfix probe when the body's token prefix table proves that they cannot
// find anything (c_ir_range_may_prepare_control, c_ir_range_may_prepare_calls,
// c_ir_range_may_hold_update). Each shape below sits on one side of those
// answers:
// - an update as the last token of the range, and one that belongs to a later
//   operand;
// - a cast over an updated operand;
// - calls nested in arguments, where the inner range runs under
//   preparing_calls;
// - a control group inside an otherwise plain argument;
// - selections that must run only their chosen arm;
// - an update inside an unevaluated sizeof operand.
// A wrong skip drops, repeats or reorders a side effect, which the counters
// observe, so the fixture runs. It exits non-zero on the first wrong answer.

static int calls;
static int order;

static __attribute__((noinline)) int f(int value)
{
    calls += 1;
    order = order * 10 + value;
    return value;
}

static __attribute__((noinline)) int g(int left, int right)
{
    calls += 1;
    order = order * 10 + left;
    return left * 100 + right;
}

int main(void)
{
    int result = 0;
    int a = 3;
    int b = 4;
    int c = 7;
    int values[3] = {10, 20, 30};
    int* p = values;

    // An update as the range's last token, and one belonging to a later
    // operand: the first operand's probe still runs and finds no postfix.
    result = a + b++;
    if (result != 7 || b != 5)
    {
        return 1;
    }
    result = a * c + b--;
    if (result != 26 || b != 4)
    {
        return 2;
    }
    // A cast over a postfix update, and a dereference of one.
    result = (int)(char)c-- + *p++;
    if (result != 17 || c != 6 || p != values + 1)
    {
        return 3;
    }
    // No `++` or `--` anywhere: every probe is skipped.
    result = -a + ~b + !c + values[2];
    if (result != 22)
    {
        return 4;
    }

    // Calls nested in arguments, with no control token in the range: the
    // inner argument ranges run under preparing_calls. Argument and operand
    // order is unspecified, so only the results and call counts are checked.
    calls = 0;
    result = g(f(1), f(2)) + f(3);
    if (result != 105 || calls != 4)
    {
        return 5;
    }
    // A control group inside an otherwise plain argument must still be
    // prepared, exactly once.
    calls = 0;
    result = f((b = 5)) + f(a);
    if (result != 8 || calls != 2 || b != 5)
    {
        return 6;
    }

    // Selections run only their chosen arm, with or without a control token
    // elsewhere in the range.
    calls = 0;
    order = 0;
    result = _Generic(a, int: f(1), default: f(2)) + f(4);
    if (result != 5 || calls != 2 || (order != 14 && order != 41))
    {
        return 7;
    }
    calls = 0;
    order = 0;
    result = __builtin_choose_expr(1, f(6), f(7)) + (b = 2);
    if (result != 8 || calls != 1 || order != 6 || b != 2)
    {
        return 8;
    }

    // An unevaluated operand holds the range's only update.
    result = (int)sizeof(a++) + a;
    if (result != (int)sizeof(int) + 3 || a != 3)
    {
        return 9;
    }
    return 0;
}
