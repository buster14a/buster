// Short-circuit operands inside a call argument.  The right operand of `&&`
// and `||`, and either arm of `?:`, run only when the branch that selects them
// is taken (C11 6.5.13p4, 6.5.14p4, 6.5.15p4).  Two prepasses run over a
// statement before its expression is lowered -- one hoists calls, the other
// lowers parenthesized control groups -- and either one runs such an operand
// unconditionally unless it is left alone.  Only the call prepass had that
// rule, so a parenthesized group in a lazy operand was evaluated ahead of the
// operand that decides whether it runs at all.
//
// A call argument is where this showed: an argument is lowered by the
// arithmetic core, which is what runs the control prepass, while an
// assignment, an `if` or a `while` condition reaches the condition machine
// directly and never asked the prepass for the group.  libc-test's
// `functional/fcntl` is the shape at the bottom of this file: its child reads
// `errno` in the right operand of `||`, so with the read hoisted ahead of the
// `fcntl` call in the left operand it saw the value from before the call and
// reported a lock the parent held as not held.
//
// Everything observable here is a store to a file-scope object made by an
// out-of-line function, so nothing folds at the call site: the fixture reads
// what code generation actually emitted.  It exits non-zero on the first wrong
// answer, naming the case.

static int witness;
static int other_witness;
static int state;

// Returns false, and records that it ran by moving `state`.  The point of the
// move is that an operand hoisted ahead of this call reads the old value.
static int move_state(void)
{
    state = 7;
    return 0;
}

static int constant_zero(void)
{
    return 0;
}

static int constant_one(void)
{
    return 1;
}

// The argument arrives here already evaluated; taking it through a function
// keeps the caller from folding the expression into a constant.
static int observe(int value)
{
    return value;
}

static int observe_two(int first, int second)
{
    return first + second;
}

int main(void)
{
    // The right operand of `||` may not read `state` until the left operand
    // has run.  `move_state()` returns 0, so the left operand is false and the
    // right one runs -- with `state` at 7, which makes it false as well.
    state = 0;
    if (observe(move_state() != 0 || (state != 7 && state != 13)) != 0)
    {
        return 1;
    }
    // The same for `&&`: the left operand is true, so the right one runs after
    // the move.
    state = 0;
    if (observe(move_state() == 0 && (state == 7 || state == 13)) != 1)
    {
        return 2;
    }
    // A short-circuited right operand must not store at all.
    witness = 0;
    if (observe(constant_zero() == 0 || (witness = 1)) != 1 || witness != 0)
    {
        return 3;
    }
    witness = 0;
    if (observe(constant_one() == 0 && (witness = 1)) != 0 || witness != 0)
    {
        return 4;
    }
    // Neither may the arm of a conditional that is not selected.
    witness = 0;
    other_witness = 0;
    if (observe(constant_zero() ? (witness = 1) : (other_witness = 2)) != 2 || witness != 0 || other_witness != 2)
    {
        return 5;
    }
    // A conditional nested inside a short-circuited operand runs neither arm.
    witness = 0;
    other_witness = 0;
    if (observe(constant_zero() == 0 || (constant_one() ? (witness = 1) : (other_witness = 2))) != 1 || witness != 0 ||
        other_witness != 0)
    {
        return 6;
    }
    // A call in the lazy operand is the rule the call prepass already had;
    // it has to keep holding now that both prepasses share one scan.
    state = 0;
    if (observe(constant_zero() == 0 || (move_state() == 0 && state == 7)) != 1 || state != 0)
    {
        return 7;
    }
    // A group that is *not* in a lazy operand is still prepared ahead of the
    // expression, and still runs.  The comma between two arguments ends the
    // deferral the `||` in the first one began, so the second argument's group
    // is eager again.
    witness = 0;
    other_witness = 0;
    if (observe_two(constant_zero() == 0 || (witness = 1), (other_witness = 2)) != 3 || witness != 0 || other_witness != 2)
    {
        return 8;
    }
    // An unconditional group in an argument, with no short circuit anywhere.
    witness = 0;
    if (observe((witness = 1) + 1) != 2 || witness != 1)
    {
        return 9;
    }
    // The `functional/fcntl` shape: a call in the left operand sets the value
    // the right operand reads.  Written with `state` standing in for `errno`,
    // 11 for EAGAIN and 13 for EACCES.
    state = 0;
    if (observe(move_state() != 0 || (state != 7 && state != 11 && state != 13)) != 0)
    {
        return 10;
    }
    return 0;
}
