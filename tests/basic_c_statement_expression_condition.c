// A GNU statement expression as the operand of a lazily lowered control
// expression was refused in every control position, while the eagerly lowered
// ones took it (issue #820). The parentheses of `({ ... })` are part of the
// operator's own spelling, and the walks that peel redundant parentheses off a
// range peeled that pair too, leaving a bare compound statement no expression
// walker can name.
//
// Every position that peels is here: an `if` and a `while` condition, either
// arm's condition of `?:`, and an operand of `&&` and `||`. The eager
// positions -- a `for` condition, an equality operand -- are the controls that
// were already working and must stay working. Bodies with side effects prove
// the branch still runs the statements exactly once.

static int calls;
static int hits;

static int bump(void)
{
    calls += 1;
    return calls;
}

int main(void)
{
    int result = 0;
    if (!({ 1; }))
    {
        result = 1;
    }
    else if (!({ bump(); calls % 2 == 1; }))
    {
        result = 2;
    }
    else if (calls != 1)
    {
        result = 3;
    }
    if (!result)
    {
        int steps = 0;
        while (({ steps < 3; }))
        {
            steps += 1;
        }
        result = steps == 3 ? 0 : 4;
    }
    if (!result)
    {
        int selected = ({ 2; }) ? 5 : 9;
        result = selected == 5 ? 0 : 5;
    }
    if (!result)
    {
        int conjunction = ({ 1; }) && (hits = 4);
        result = conjunction == 1 && hits == 4 ? 0 : 6;
    }
    if (!result)
    {
        int disjunction = ({ 0; }) || ({ 7; });
        result = disjunction == 1 ? 0 : 7;
    }
    if (!result)
    {
        // The eager positions, unchanged: a `for` condition is re-evaluated
        // per iteration through the same range and never peeled, and an
        // equality operand reaches the expression core directly.
        int total = 0;
        for (int index = 0; index < ({ 3; }); index += 1)
        {
            total += index;
        }
        result = total == 3 && ({ 5; }) == 5 ? 0 : 8;
    }
    if (!result)
    {
        // Both arms of a conditional are lazy positions too, and the merged
        // type is what the arms predict: a body whose last statement is an
        // expression statement has that statement's type, not void.
        int arms = ({ int probe = 4; probe; }) > 3 ? ({ 11; }) : ({ 22; });
        result = arms == 11 ? 0 : 11;
    }
    if (!result)
    {
        // A body whose last statement is a declaration, or is not a statement
        // that produces a value at all, is void -- and a void arm merges with
        // a void one. This is the control on the tail walk above.
        (void)(hits > 0 ? (void)0 : ({ int unused = 1; (void)unused; }));
    }
    if (!result)
    {
        // The body runs once per evaluation, not once per peel attempt.
        calls = 0;
        if (({ bump(); 1; }) && ({ bump(); 1; }))
        {
            result = calls == 2 ? 0 : 9;
        }
        else
        {
            result = 10;
        }
    }
    return result;
}
