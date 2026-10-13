// Every statement kind lowered from its statement node (GitHub #4060):
// blocks, if/else chains, while/do/for in every header shape, switch with
// ranged and substatement case labels, GNU case ranges, nested switches,
// labels and goto across blocks (forward, backward, into a loop after dead
// code, out of nested blocks), computed goto, break/continue/return, block
// declarations, attribute statements and GNU statement expressions with
// their tails. Each check returns its own nonzero status.

static int calls;

static int touch(int value)
{
    calls += 1;
    return value;
}

static int blocks(int x)
{
    int r = 0;
    {
        int y = x + 1;
        {
            int z = y * 2;
            r += z;
        }
        ;
        r += y;
    }
    {}
    return r;
}

static int conditions(int x)
{
    int r = 0;
    if (x == 0) r = 10;
    else if (x == 1) r = 11;
    else if (x == 2) { r = 12; }
    else r = 13;
    if (x > 0) if (x > 1) r += 100; else r += 200;
    if ((x)) r += 1000;
    return r;
}

static int loops(int n)
{
    int r = 0;
    int i = 0;
    while (i < n) i += 1;
    r += i;
    do { r += 1; } while (r < 20);
    do r += 2; while (r < 25);
    for (int j = 0; j < n; j++) r += j;
    for (i = 0; i < 3; i++, r++) {}
    for (;;) { r += 1; if (r > 40) break; }
    int k = 0;
    for (; k < 4;) k += 1;
    for (int a = 0, b = 10; a < b; a += 2, b -= 1) { if (a == 4) continue; r += 1; }
    i = 0;
    while (1) { i += 1; if (i < 5) continue; break; }
    return r + i + k;
}

static int dispatch(int x)
{
    int r = 0;
    switch (x)
    {
    case 0:
        r = 1;
        break;
    case 1:
    case 2:
        r = 2;
        /* fallthrough */
    case 3 ... 5:
        r += 3;
        break;
    case 6: {
        int t = 6;
        r = t;
    }
        break;
    default:
        r = -1;
        break;
    case 7:
        r = 7;
    }
    return r;
}

// Duff's device: a case label inside a loop body is a dispatch target inside
// the statements of the case that opens the loop.
static int duff(int count)
{
    int total = 0;
    int n = (count + 3) / 4;
    switch (count % 4)
    {
    case 0:
        do
        {
            total += 1;
        case 3:
            total += 1;
        case 2:
            total += 1;
        case 1:
            total += 1;
        } while (--n > 0);
    }
    return total;
}

static int case_substatement(int x)
{
    int r = 0;
    switch (x)
    {
    case 1:
        if (x) case 2: r += 2;
        r += 1;
        break;
    labelled:
    case 3:
        r = 30;
        break;
    }
    if (x == 9) goto labelled_out;
    return r;
labelled_out:
    return -9;
}

// A switch whose first case label stands in a control substatement: that
// statement and what follows it lower from a block no edge enters, and the
// dispatch reaches the label inside it.
static int leading_case(int x, int y)
{
    int r = 0;
    switch (x)
    {
        if (y) case 1: r = 1;
    case 2:
        r += 2;
    }
    return r;
}

static int nested_switch(int x, int y)
{
    int r = 0;
    switch (x)
    {
    case 0:
        switch (y)
        {
        case 0: r = 1; break;
        default: r = 2; break;
        }
        break;
    case 1:
        switch (y) case 0: r = 3;
        break;
    default:
        r = 4;
    }
    return r;
}

static int jumps(int x)
{
    int r = 0;
    int i = 0;
again:
    i += 1;
    if (i < 3) goto again;
    r += i;
    {
        {
            if (x) goto out;
            r += 100;
        }
    }
out:
    r += 10;
    goto forward;
    r += 1000;
forward:
    {
        int depth = 0;
    inner:
        depth += 1;
        if (depth < 4) goto inner;
        r += depth;
    }
    return r;
}

// A goto into the body of a loop that dead code precedes: the loop still
// loops once entered at its label.
static int into_loop(int c)
{
    int r = 0;
    if (c) goto entry;
    return -1;
    while (c)
    {
    entry:
        r += 3;
        if (r > 9) break;
    }
    return r;
}

static int into_block(int c)
{
    int r = 0;
    goto entry;
    return -1;
    {
        r = 100;
    entry:
        r += 5;
    }
    return r;
}

static int computed(int x)
{
    static void* const targets[] = {&&zero, &&one};
    int r = 0;
    goto *targets[x & 1];
zero:
    r = 20;
    goto done;
one:
    r = 21;
done:
    return r;
}

static int returns(int x)
{
    while (1)
    {
        if (x > 3) return x * 2;
        x += 1;
    }
}

static int declarations(int x)
{
    int a = x, b = a + 1, c[3] = {a, b, 3};
    typedef int local_int;
    local_int d = c[2];
    struct pair { int left; int right; };
    struct pair p = {a, b};
    static int counter;
    counter += 1;
    enum { SEVEN = 7 };
    return a + b + c[0] + d + p.left + p.right + SEVEN + counter;
}

static int statement_expressions(int x)
{
    int a = ({ int t = x; t + 1; });
    int b = ({ ({ x += 1; }); x * 2; });
    int c = ({ int s = 0; for (int i = 0; i < 3; i++) s += i; s; });
    int d = ({ touch(1), touch(2); });
    int e = ({ if (x > 100) x = 0; else x += 1; x; });
    return a + b + c + d + e;
}

static int attributes(int x)
{
    int r = 0;
    switch (x)
    {
    case 1:
        r += 1;
        __attribute__((fallthrough));
    case 2:
        r += 2;
        break;
    }
    __attribute__((unused)) int unused_local = 4;
    return r;
}

int main(void)
{
    if (blocks(1) != 6) return 1;
    if (conditions(0) != 10 || conditions(1) != 1211 || conditions(2) != 1112 || conditions(3) != 1113) return 2;
    if (loops(4) != 53) return 3;
    if (dispatch(0) != 1 || dispatch(1) != 5 || dispatch(2) != 5 || dispatch(4) != 3 || dispatch(6) != 6 || dispatch(7) != 7 ||
        dispatch(9) != -1)
        return 4;
    if (duff(0) != 4 || duff(1) != 1 || duff(5) != 5 || duff(8) != 8) return 5;
    if (case_substatement(1) != 3 || case_substatement(2) != 3 || case_substatement(3) != 30 || case_substatement(9) != -9) return 6;
    if (nested_switch(0, 0) != 1 || nested_switch(0, 1) != 2 || nested_switch(1, 0) != 3 || nested_switch(2, 0) != 4) return 7;
    if (jumps(0) != 117 || jumps(1) != 17) return 8;
    if (into_loop(1) != 12 || into_loop(0) != -1) return 9;
    if (into_block(0) != 5) return 10;
    if (computed(0) != 20 || computed(1) != 21) return 11;
    if (returns(1) != 8) return 12;
    if (declarations(1) != 1 + 2 + 1 + 3 + 1 + 2 + 7 + 1) return 13;
    calls = 0;
    if (statement_expressions(1) != 2 + 4 + 3 + 2 + 3 || calls != 2) return 14;
    if (attributes(1) != 3 || attributes(2) != 2) return 15;
    if (leading_case(1, 0) != 3 || leading_case(2, 0) != 2 || leading_case(3, 1) != 0) return 16;
    return 0;
}
