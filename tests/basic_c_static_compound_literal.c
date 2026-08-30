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

struct Cells
{
    int cell[2];
};

struct Rows
{
    struct Cells rows[2];
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

// A subscript reaches one too, at the element's stride, and the two steps
// compose in either order and to any depth. Every addend here was checked
// against Clang's with `llvm-readelf -r`: the symbol is the literal object and
// the addend is the offset of the subobject inside it.
static int *element_pointer = &(int[]){21, 22, 23}[2];
static int *member_element = &(struct Rows){{{31, 32}, {33, 34}}}.rows[1].cell[0];
static struct Inner *element_member = &(struct Inner[]){{41, 42}, {43, 44}}[1];

// A subscript whose index is a constant expression rather than a literal
// number, and one that names the element just past the end -- an address C
// 6.5.6p8 allows a program to form.
static int *computed_element = &(int[]){51, 52, 53, 54}[1 + 2 * 1];
static int *past_the_end = &(int[]){61, 62}[2];

// Without an `&` the subscripted literal is still an address when what the
// subscript lands on is itself an array: it decays to its first element the
// way the whole literal does. `(int[]){...}[1]` beside it is an `int` instead,
// and is refused rather than folded to an address.
static int *decayed_row = (int[2][3]){{71, 72, 73}, {74, 75, 76}}[1];

// A literal that itself holds a literal, a string and a designator.
static struct Outer *nested = &(struct Outer){.in = {.y = 3}, .p = &(int){44}, .s = "deep"};

// The other half of the same construct: a compound literal used for its value
// rather than its address. `&(T){...}` needs an object to point at, so it
// synthesizes one and holds its symbol; `(T){...}` needs only the bytes, which
// belong to the object being initialized -- no second object, no relocation to
// one. A scalar destination never looked for a literal at all and refused
// every one of these.
static int value_scalar = (int){5};
static void *value_null = (void *){0};
static const char *value_string = (const char *){"lit"};
static double value_double = (double){1.5};
static int value_parenthesized = ((int){23});

struct Holder
{
    void *a;
    void *b;
};

int value_target = 41;

// The same literal as an element of an aggregate, which reaches the scalar
// folder through the initializer walk rather than through the object's own
// declaration. The neighbours pin that the bytes land at the element's offset.
static struct Holder value_holder = {(void *){0}, (void *){&value_target}};
static int value_elements[4] = {(int){1}, 2, (int){3}, 4};

// A literal of array type without an `&` is still an address: it decays to a
// pointer to its first element, and `decayed` above is the same shape. This
// one pins that the value fold does not claim it -- an array type is never
// compatible with the pointer it is initializing.
static const char *value_decayed = (char[]){'x', 'y', 0};

// A function-local static reaches the same fold. Its address form is refused
// -- an in-body literal has automatic storage duration -- but its value form
// is only bytes, so it folds like any other.
static int value_local(void)
{
    static int local = (int){31};
    return local;
}

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

    // The subscript steps, read back through the pointer: a wrong stride, a
    // wrong member offset or the two composed in the wrong order all still
    // produce a program, and each of these lands on a distinct value.
    if (*element_pointer != 23)
    {
        return 22;
    }
    if (*member_element != 33)
    {
        return 23;
    }
    if (element_member->x != 43 || element_member->y != 44)
    {
        return 24;
    }
    if (*computed_element != 54)
    {
        return 25;
    }
    // One past the end is an address rather than an object, so only the
    // arithmetic is checked: stepping back from it names the last element.
    if (past_the_end[-1] != 62)
    {
        return 26;
    }
    if (decayed_row[0] != 74 || decayed_row[2] != 76)
    {
        return 27;
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

    if (value_scalar != 5 || value_parenthesized != 23)
    {
        return 14;
    }
    if (value_null != 0)
    {
        return 15;
    }
    if (value_string == 0 || value_string[0] != 'l' || value_string[2] != 't' || value_string[3] != 0)
    {
        return 16;
    }
    if (value_double != 1.5)
    {
        return 17;
    }
    // The relocation belongs to the object being initialized, so `b` holds the
    // address of `value_target` itself rather than of a copy of it.
    if (value_holder.a != 0 || value_holder.b != &value_target || *(int *)value_holder.b != 41)
    {
        return 18;
    }
    if (value_elements[0] != 1 || value_elements[1] != 2 || value_elements[2] != 3 || value_elements[3] != 4)
    {
        return 19;
    }
    if (value_decayed[0] != 'x' || value_decayed[1] != 'y' || value_decayed[2] != 0)
    {
        return 20;
    }
    if (value_local() != 31)
    {
        return 21;
    }

    return 0;
}
