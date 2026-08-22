// A sizeof or _Alignof operand is never evaluated, but the call-preparation
// prepass (c_ir_prepare_calls_discover in c_gen.c) used to hoist calls out of
// one and emit them anyway: the sizeof itself still folded to the right
// constant, so only the orphaned side effect made the miscompile visible.
// Each check pairs a sizeof shape with a call counter; the counter must not
// move for the unevaluated shape and must move for the evaluated control.
// The skip has to end exactly where the operand ends, so several checks put
// an evaluated call right after a sizeof to catch over-skipping.
static int calls;

static short counted(void)
{
    calls += 1;
    return 1;
}

static int counted_int(int payload)
{
    calls += 1;
    return payload;
}

static int arr[7];
static int* arr_pointer = arr;

int main(void)
{
    // A plain call operand must not run.
    calls = 0;
    if (sizeof(counted()) != sizeof(short) || calls != 0)
    {
        return 1;
    }
    // Binary expressions around the call must not run it either.
    if (sizeof(counted() + 1) != sizeof(int) || calls != 0)
    {
        return 2;
    }
    // Both arms of a conditional stay unevaluated.
    if (sizeof(calls ? counted() : counted()) != sizeof(int) || calls != 0)
    {
        return 3;
    }
    // _Alignof is unevaluated the same way.
    if (_Alignof(int) != 4 || calls != 0)
    {
        return 4;
    }
    // The unparenthesized form: the operand is one unary expression.
    if (sizeof counted() != sizeof(short) || calls != 0)
    {
        return 5;
    }
    // A subscript operand with a call inside the brackets must not run it.
    if (sizeof arr[counted_int(0)] != sizeof(int) || calls != 0)
    {
        return 6;
    }
    // Over-skip check: a call in the same expression after the operand must
    // still run, whether the operand was parenthesized or not.
    calls = 0;
    if ((int)sizeof(arr) + counted_int(2) != 28 + 2 || calls != 1)
    {
        return 7;
    }
    calls = 0;
    if ((int)sizeof *arr_pointer + counted_int(3) != 4 + 3 || calls != 1)
    {
        return 8;
    }
    // Calls on both sides of an unevaluated operand in one statement list:
    // exactly the two evaluated calls run.
    calls = 0;
    int first = counted_int(9);
    int width = (int)sizeof(counted());
    int second = counted_int(10);
    if (first != 9 || width != (int)sizeof(short) || second != 10 || calls != 2)
    {
        return 9;
    }
    return 0;
}
