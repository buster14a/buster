// sizeof over a compound literal: the parenthesis after sizeof is the
// literal's type, not the operand, so the operand extends through the braced
// initializer. The expression core used to stop at the type parenthesis and
// fail on the trailing braces ("could not lower logical expression core").
// The operand stays unevaluated, so calls inside the initializer must not
// run; each check pairs the folded size with the call counter.
static int calls;

static int counted(void)
{
    calls += 1;
    return 5;
}

struct Pair
{
    int a;
    long b;
};

int main(void)
{
    // The flagged shape: an array compound literal.
    if (sizeof (int[3]){1, 2, 3} != 3 * sizeof(int))
    {
        return 1;
    }
    // Struct, scalar, and char-array literal types.
    if (sizeof (struct Pair){1, 2} != sizeof(struct Pair))
    {
        return 2;
    }
    if (sizeof (int){7} != sizeof(int) || sizeof (char[2]){1} != 2)
    {
        return 3;
    }
    // A call inside the literal's initializer stays unevaluated.
    calls = 0;
    if (sizeof (int[3]){counted(), 2, 3} != 3 * sizeof(int) || calls != 0)
    {
        return 4;
    }
    // The literal folds inside larger arithmetic, and an evaluated call
    // after the operand still runs (over-skip check).
    calls = 0;
    if (3 + (int)sizeof (int[2]){1, 2} + counted() != 3 + 8 + 5 || calls != 1)
    {
        return 5;
    }
    // A function-scope array bound sized by a compound literal is constant,
    // not a VLA.
    char bounded[sizeof (int[2]){1, 2}];
    bounded[0] = 1;
    if (sizeof(bounded) != 2 * sizeof(int) || bounded[0] != 1)
    {
        return 6;
    }
    return 0;
}
