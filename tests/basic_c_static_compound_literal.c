// C11 6.5.2.5p5: a compound literal written outside a function body has static
// storage duration, so `&(int){0}` names an object with a lifetime as long as
// the program's and is an address constant a static initializer may hold.
//
// Nothing created that object, so the constant folder saw an initializer
// element it could not reduce and refused the whole declaration. libc-test's
// `functional/pthread_cancel-points` is where that surfaced: its file-scope
// `scenarios[]` table is an array of structs of function pointers and string
// literals, and one row passes `&(int){0}` as its argument.
//
// Every check reads the object back through the pointer rather than trusting
// the diagnostic, because a wrong offset, a shared object where two were
// wanted, or a literal folded into the wrong bytes all still produce a
// program. Two literals of the same type and value are two objects, so the
// pointers must differ and a store through one must not be visible through the
// other.

struct Inner
{
    int x;
    int y;
};

struct Outer
{
    struct Inner in;
    int *p;
    const char *s;
};

struct Scenario
{
    int want_cancel;
    void (*prepare)(void *);
    void (*execute)(void *);
    void *arg;
    const char *descr;
};

static void prepare_nothing(void *arg)
{
    (void)arg;
}

static void execute_nothing(void *arg)
{
    (void)arg;
}

// The pthread_cancel-points shape: an array of structs whose members are
// function pointers, string literals and one address of a compound literal,
// with a second declarator in the same declaration pointing at the array.
static struct Scenario scenarios[] = {
    {1, prepare_nothing, execute_nothing, 0, "blocking"},
    {1, prepare_nothing, execute_nothing, (void *)1, "non-blocking"},
    {0, prepare_nothing, execute_nothing, &(int){17}, "compound literal"},
    {0},
}, *current_scenario = scenarios;

// A scalar object, which reaches the folder through a different path than an
// aggregate element does.
static int *scalar_pointer = &(int){7};
static int *other_pointer = &(int){7};

// An array compound literal converts to a pointer to its first element without
// an `&`, and `&` over the whole array gives a pointer to the array.
static int *decayed = (int[]){11, 12, 13};
static char (*whole_array)[4] = &(char[4]){'a', 'b', 'c', 0};

// A member designator after the literal reaches a subobject of the same
// object, which is that symbol with the member's offset as the addend.
static int *member_pointer = &(struct Inner){.x = 5, .y = 6}.y;

// A literal that itself holds a literal, a string and a designator.
static struct Outer *nested = &(struct Outer){.in = {.y = 3}, .p = &(int){44}, .s = "deep"};

int main(void)
{
    if (current_scenario != scenarios)
    {
        return 1;
    }
    if (scenarios[0].arg != 0 || scenarios[1].arg != (void *)1)
    {
        return 2;
    }
    if (scenarios[2].arg == 0 || *(int *)scenarios[2].arg != 17)
    {
        return 3;
    }
    // The neighbours the compound literal sits between are untouched: an
    // element written at the wrong offset would take one of them.
    if (scenarios[2].want_cancel != 0 || scenarios[2].prepare != prepare_nothing || scenarios[2].execute != execute_nothing ||
        scenarios[2].descr[0] != 'c')
    {
        return 4;
    }
    if (scenarios[3].prepare != 0 || scenarios[3].descr != 0)
    {
        return 5;
    }

    if (*scalar_pointer != 7 || *other_pointer != 7 || scalar_pointer == other_pointer)
    {
        return 6;
    }
    // Two literals, two objects: the store is not visible through the other.
    *scalar_pointer = 99;
    if (*scalar_pointer != 99 || *other_pointer != 7)
    {
        return 7;
    }

    if (decayed[0] != 11 || decayed[1] != 12 || decayed[2] != 13)
    {
        return 8;
    }
    if ((*whole_array)[0] != 'a' || (*whole_array)[2] != 'c' || (*whole_array)[3] != 0)
    {
        return 9;
    }

    // The designator names `y`, so the pointer is the object plus the offset of
    // `y`; reading it as `x` would give 5.
    if (*member_pointer != 6)
    {
        return 10;
    }

    if (nested->in.x != 0 || nested->in.y != 3)
    {
        return 11;
    }
    if (nested->p == 0 || *nested->p != 44)
    {
        return 12;
    }
    if (nested->s[0] != 'd' || nested->s[3] != 'p' || nested->s[4] != 0)
    {
        return 13;
    }

    return 0;
}
