// A statement's extent is measured over tokens, and a controlled substatement
// may itself be a control statement whose own body is a brace group:
// `if (c) for (...) if (c2) { ... }` ends at that closing brace and not at a
// semicolon, because there is no semicolon after it.  Measuring it as ending
// at the next one swallowed whatever followed into the `if`, which is how
// libc-test's regression/flockfile-list.c stopped the function-body walk.
//
// The peel that finds the brace has to see through every header, including the
// unbraced `if` between the loop and the body -- and it must not claim a brace
// group that belongs to an expression, which is what a statement-expression
// call in the same position writes.

static int calls = 0;

static int touch(int value)
{
    calls += 1;
    return value;
}

// The exact shape from libc-test: an unbraced `if` controlling a loop whose
// own unbraced `if` carries the compound body.
static int check_if_loop_if(int rows, int columns)
{
    int total = 0;
    for (int row = 0; row < rows; row += 1)
        if ((row & 1) == 0)
            for (int column = 0; column < columns; column += 1)
                if (column == row)
                {
                    total += row * 10 + column;
                    break;
                }
    // Nothing above may swallow this statement.
    total += 1;
    return total;
}

static int check_while_and_switch(int limit)
{
    int total = 0;
    int index = 0;
    if (limit > 0)
        while (index < limit)
            if ((index += 1) & 1)
            {
                total += index;
            }
    if (limit > 0)
        switch (limit)
        {
        case 3:
            total += 100;
            break;
        default:
            total += 200;
            break;
        }
    total += 1;
    return total;
}

// `do` runs past the `while (...) ;` that closes it rather than ending at the
// first semicolon inside its body, and it may stand as a controlled
// substatement of an `if` with an `else` after it.
static int check_do_while(int limit)
{
    int total = 0;
    int index = 0;
    if (limit > 0)
        do
            total += (index += 1);
        while (index < limit);
    else
        total = -1;
    total += 1;
    return total;
}

// A brace group inside an expression is not a body: the statement ends at the
// semicolon after it.  An assert() written as a GNU statement expression is
// the shape this protects.
static int check_statement_expression(int rows)
{
    int total = 0;
    for (int row = 0; row < rows; row += 1)
        if (row > 0)
            total += ({ int doubled = touch(row) * 2; doubled; });
    total += 1;
    return total;
}

int main(void)
{
    if (check_if_loop_if(5, 5) != 1 + 0 + 22 + 44)
    {
        return 1;
    }
    if (check_while_and_switch(3) != 1 + (1 + 3) + 100)
    {
        return 2;
    }
    if (check_do_while(4) != 1 + 1 + 2 + 3 + 4)
    {
        return 3;
    }
    if (check_do_while(0) != 0)
    {
        return 4;
    }
    if (check_statement_expression(4) != 1 + 2 + 4 + 6 || calls != 3)
    {
        return 5;
    }
    return 0;
}
