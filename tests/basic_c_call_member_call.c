// A call whose callee is a member read off another call's result:
// `Py_TYPE(self)->tp_free(self)` is how CPython frees every object.  A chain
// call keys on its base call's argument list in the token->call index --
// two calls cannot share one first token -- so the callee lowering widens
// its expression to the base call's own chain root, and the expression core
// hops from a consumed base call to the outermost chained call it still
// contains.  Both links of the multi-link chain exercise the hop twice, and
// the answers are checked so a callee read off the wrong base -- `(self)`
// alone was the failure shape -- returns the wrong number rather than
// compiling quietly.

struct table
{
    int tag;
    int (*method)(int);
};

static int double_it(int x)
{
    return x * 2;
}

static struct table the_table = {7, double_it};

static struct table* get_table(int unused)
{
    (void)unused;
    return &the_table;
}

struct outer_table
{
    struct table* (*get)(int);
};

static struct table* get_table_indirect(int unused)
{
    (void)unused;
    return &the_table;
}

static struct outer_table the_outer = {get_table_indirect};

static struct outer_table* get_outer(void)
{
    return &the_outer;
}

typedef int (*fn_t)(int);

static fn_t pick(int unused)
{
    (void)unused;
    return double_it;
}

int main(void)
{
    if (get_table(0)->method(21) != 42)
    {
        return 1;
    }
    if (get_outer()->get(0)->method(10) != 20)
    {
        return 2;
    }
    // The adjacent spelling: a call returning a function pointer, called
    // directly.  Discovery classifies the outer call at the `)` of `(0)`
    // while the base is still on the active stack; the base is its
    // continuation, not its argument, so the parent walk skips it and the
    // base's result is emitted first.
    if (pick(0)(5) != 10)
    {
        return 3;
    }
    return 0;
}
