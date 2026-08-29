// A GNU statement expression's value is that of its final expression
// statement. The body lowering hands the tail of a region to a "remainder"
// task when a control statement splits it, and that task did not inherit the
// enclosing task's claim that its last statement may be the trailing
// expression -- so as soon as the body contained an `if`, a loop, a `switch`
// or a nested block, the final expression statement was lowered as an
// ordinary discarded statement and the whole expression read as 0.
//
// It was silent in every context, and it survived because the only coverage
// was `(void)({ ... })`, which discards the value. So every check here uses
// the value, and each control shape appears in a value position: a wrong
// answer is a number, not a diagnostic.
//
// The expected answers are Clang's; this file compiles and returns 0 under
// Clang and under Buster alike.

#include <stdarg.h>

static int accept_int(int value)
{
    return value;
}

static int sum_of(int count, ...)
{
    va_list arguments;
    int total = 0;
    va_start(arguments, count);
    for (int index = 0; index < count; index += 1)
    {
        total += va_arg(arguments, int);
    }
    va_end(arguments);
    return total;
}

// The five contexts the value can appear in, each over a body whose control
// flow opens new blocks.
static int in_return(int x)
{
    return ({
        if (x > 100)
        {
            x = 1;
        }
        7;
    });
}

static int in_initializer(int x)
{
    int value = ({
        if (x > 100)
        {
            x = 1;
        }
        7;
    });
    return value;
}

static int in_assignment(int x)
{
    int value = 0;
    value = ({
        if (x > 100)
        {
            x = 1;
        }
        7;
    });
    return value;
}

static int in_arithmetic(int x)
{
    return ({
        if (x > 100)
        {
            x = 1;
        }
        7;
    }) + 1;
}

static int in_call_argument(int x)
{
    return accept_int(({
        if (x > 100)
        {
            x = 1;
        }
        7;
    }));
}

static int in_variadic_argument(int x)
{
    return sum_of(2, 10, ({
                      if (x > 100)
                      {
                          x = 1;
                      }
                      7;
                  }));
}

// The tail expression reads state the controlled statement wrote, so a value
// taken from the wrong block is visible as a stale number rather than as a
// constant.
static int tail_reads_body(int x)
{
    return ({
        if (x > 0)
        {
            x += 2;
        }
        x + 6;
    });
}

// One shape per statement that splits the enclosing region.
static int after_if_else(int x)
{
    return ({
        if (x > 100)
        {
            x = 5;
        }
        else
        {
            x = 9;
        }
        x + 1;
    });
}

static int after_while(int x)
{
    return ({
        while (x < 4)
        {
            x += 1;
        }
        x + 1;
    });
}

static int after_do_while(int x)
{
    return ({
        do
        {
            x += 1;
        } while (x < 3);
        x + 1;
    });
}

static int after_for(int x)
{
    return ({
        for (int index = 0; index < 3; index += 1)
        {
            x += index;
        }
        x + 1;
    });
}

static int after_switch(int x)
{
    return ({
        switch (x)
        {
            case 1:
                x = 10;
                break;
            default:
                x = 20;
                break;
        }
        x + 1;
    });
}

static int after_block(int x)
{
    return ({
        {
            x += 1;
        }
        x + 1;
    });
}

static int after_goto(int x)
{
    return ({
        if (x > 100)
        {
            goto done;
        }
        x += 1;
    done:
        x + 1;
    });
}

// Nesting: a control statement inside a controlled body, several statements
// between the control statement and the tail, and a statement expression
// whose own tail is another statement expression.
static int nested_control(int x)
{
    return ({
        if (x > 0)
        {
            if (x > 100)
            {
                x = 5;
            }
            x += 2;
        }
        x + 1;
    });
}

static int statements_after_control(int x)
{
    return ({
        if (x > 100)
        {
            x = 5;
        }
        x += 1;
        x += 2;
        x;
    });
}

static int nested_statement_expression(int x)
{
    return ({
        if (x > 0)
        {
            x += 1;
        }
        ({
            if (x > 0)
            {
                x += 1;
            }
            x + 3;
        });
    });
}

// A statement expression used inside a loop body still produces its value on
// every iteration, and one used as a condition operand still branches on it.
static int inside_loop(int x)
{
    int total = 0;
    while (x < 3)
    {
        total += ({
            if (x > 100)
            {
                x = 0;
            }
            x + 1;
        });
        x += 1;
    }
    return total;
}

static int as_condition(int x)
{
    return ({
        if (x > 0)
        {
            x += 1;
        }
        x;
    }) > 1
               ? 100
               : 200;
}

// The value-discarding form still runs its body: the fix must not turn a
// void-valued statement expression into one that drops its statements.
static int discarded_value(int x)
{
    int written = 0;
    (void)({
        if (x > 0)
        {
            written = 4;
        }
    });
    return written;
}

// Types other than int travel the same path.
static double floating_tail(int x)
{
    return ({
        if (x > 0)
        {
            x += 1;
        }
        x * 0.5;
    });
}

static const char *pointer_tail(int x)
{
    static const char text[] = "abc";
    return ({
        if (x > 0)
        {
            x += 1;
        }
        text + x;
    });
}

int main(void)
{
    if (in_return(1) != 7)
    {
        return 1;
    }
    if (in_initializer(1) != 7)
    {
        return 2;
    }
    if (in_assignment(1) != 7)
    {
        return 3;
    }
    if (in_arithmetic(1) != 8)
    {
        return 4;
    }
    if (in_call_argument(1) != 7)
    {
        return 5;
    }
    if (in_variadic_argument(1) != 17)
    {
        return 6;
    }
    if (tail_reads_body(1) != 9)
    {
        return 7;
    }
    if (after_if_else(1) != 10)
    {
        return 8;
    }
    if (after_while(1) != 5)
    {
        return 9;
    }
    if (after_do_while(1) != 4)
    {
        return 10;
    }
    if (after_for(1) != 5)
    {
        return 11;
    }
    if (after_switch(1) != 11)
    {
        return 12;
    }
    if (after_switch(2) != 21)
    {
        return 13;
    }
    if (after_block(1) != 3)
    {
        return 14;
    }
    if (after_goto(1) != 3)
    {
        return 15;
    }
    if (after_goto(200) != 201)
    {
        return 16;
    }
    if (nested_control(1) != 4)
    {
        return 17;
    }
    if (statements_after_control(1) != 4)
    {
        return 18;
    }
    if (nested_statement_expression(1) != 6)
    {
        return 19;
    }
    if (inside_loop(0) != 6)
    {
        return 20;
    }
    if (as_condition(1) != 100)
    {
        return 21;
    }
    if (as_condition(-5) != 200)
    {
        return 22;
    }
    if (discarded_value(1) != 4)
    {
        return 23;
    }
    if (discarded_value(-1) != 0)
    {
        return 24;
    }
    if (floating_tail(1) != 1.0)
    {
        return 25;
    }
    if (pointer_tail(1)[0] != 'c')
    {
        return 26;
    }

    // The taken arm of the control statement is the one that runs: the same
    // shapes with the other predicate answer.
    if (in_return(200) != 7)
    {
        return 27;
    }
    if (tail_reads_body(-1) != 5)
    {
        return 28;
    }
    if (after_if_else(200) != 6)
    {
        return 29;
    }
    if (nested_control(200) != 8)
    {
        return 30;
    }

    return 0;
}
