// `noreturn` written on a function pointer type, a typedef, or a struct or
// union member declarator rather than on a function declaration.  The call site
// sees only the type -- there is no declaration behind a call through a pointer
// -- so the marker has to ride on the function type, and a call through one has
// to end control flow the same way a call to a noreturn declaration does.
//
// The defect this guards is dead code only, so the runtime half below cannot
// see it: every one of these calls really does exit.  The driver test compiles
// this file with -S as well and proves that the code after each call was never
// emitted; keep `must_not_be_reached` called exactly once per shape, from a
// spot the noreturn call dominates, because that call is what the assembly
// assertion looks for.
__attribute__((noreturn)) void exit(int status);

extern void must_not_be_reached(void);

typedef __attribute__((noreturn)) void (*die_pointer)(int);
typedef __attribute__((noreturn)) void die_function(int);

static void through_typedef_pointer(int status, die_pointer die)
{
    die(status);
    must_not_be_reached();
}

static void through_typedef_function(int status, die_function* die)
{
    die(status);
    must_not_be_reached();
}

static void through_parameter(int status, __attribute__((noreturn)) void (*die)(int))
{
    die(status);
    must_not_be_reached();
}

static void through_local_typedef(int status, void (*die)(int))
{
    // The cast is what clang requires: a noreturn function pointer type is not
    // compatible with a plain one, so the plain parameter cannot initialize the
    // local directly.  It is also what leaves the block-scope typedef as the
    // only thing that can end control flow here.
    typedef __attribute__((noreturn)) void (*local_die)(int);
    local_die local = (local_die)die;
    local(status);
    must_not_be_reached();
}

// The marker on a struct or union member declarator rides on the member's own
// function type, which is the only thing the call through the member can read.
// A list of members shares one set of declaration specifiers, so a marker there
// reaches every declarator of the list.
struct member_ops
{
    __attribute__((noreturn)) void (*fail)(int);
};

union member_union
{
    __attribute__((noreturn)) void (*fail)(int);
    int tag;
};

struct member_list
{
    __attribute__((noreturn)) void (*first)(int), (*second)(int);
};

static void through_struct_member(int status, struct member_ops* ops)
{
    ops->fail(status);
    must_not_be_reached();
}

static void through_union_member(int status, union member_union* ops)
{
    ops->fail(status);
    must_not_be_reached();
}

static void through_first_of_member_list(int status, struct member_list* ops)
{
    ops->first(status);
    must_not_be_reached();
}

static void through_second_of_member_list(int status, struct member_list* ops)
{
    ops->second(status);
    must_not_be_reached();
}

// A member declarator may also spell the marker after a parenthesized
// declarator, where it follows the whole derivation rather than the shared
// specifiers.  Written there it belongs to that declarator alone, so the
// sibling beside it keeps the code after a call through it: the declarator
// boundary the segment scans is the top-level comma, and reading past it would
// mark `ok` as well and delete the return `through_declarator_sibling` needs.
struct declarator_list
{
    void (*fail)(int) __attribute__((noreturn)), (*ok)(int);
};

static void through_marked_declarator(int status, struct declarator_list* ops)
{
    ops->fail(status);
    must_not_be_reached();
}

static int through_declarator_sibling(int status, struct declarator_list* ops)
{
    ops->ok(status);
    return status + 4;
}

// A plain function pointer initialized from a noreturn function is not itself
// noreturn -- the marker is on the function, not on the type the call reads --
// and clang agrees.  The counterexamples below must keep falling through, so
// each one runs to a return that the runtime half observes.
typedef void plain_pointer(int);

static int through_plain_pointer(int status, plain_pointer* other)
{
    other(status);
    return status + 1;
}

// `noreturn` on one declarator of a shared typedef must not reach the typedef
// itself: the marker would follow every other declaration written with it and
// silently delete the live code after those calls.
__attribute__((noreturn)) plain_pointer marked_by_its_own_declaration;

// The same guard for a member declarator: the member's type is a pointer to the
// shared typedef, which the member did not build, so the marker has nothing of
// its own to ride on and `through_shared_typedef` below must still fall through.
struct member_over_shared_typedef
{
    __attribute__((noreturn)) plain_pointer* marked;
};

static int through_shared_typedef(int status, plain_pointer* other)
{
    other(status);
    return status + 2;
}

// A member whose type carries no marker keeps the code after a call through it.
struct plain_member_ops
{
    void (*other)(int);
};

static int through_plain_member(int status, struct plain_member_ops* ops)
{
    ops->other(status);
    return status + 3;
}

static void returns_normally(int status) { (void)status; }

int main(int argc, char** argv)
{
    (void)argv;
    if (through_plain_pointer(1, returns_normally) != 2)
    {
        return 1;
    }
    if (through_shared_typedef(1, returns_normally) != 3)
    {
        return 2;
    }
    struct plain_member_ops plain_member = {returns_normally};
    if (through_plain_member(1, &plain_member) != 4)
    {
        return 3;
    }
    struct declarator_list sibling_list = {exit, returns_normally};
    if (through_declarator_sibling(1, &sibling_list) != 5)
    {
        return 4;
    }
    struct member_ops member_ops = {exit};
    union member_union member_union = {exit};
    struct member_list member_list = {exit, exit};
    // Only one of these ever runs, and each ends in exit(0); the point of the
    // fixture is that all nine bodies lower at all.
    if (argc > 8)
    {
        through_marked_declarator(0, &sibling_list);
    }
    if (argc > 7)
    {
        through_typedef_pointer(0, exit);
    }
    if (argc > 6)
    {
        through_typedef_function(0, exit);
    }
    if (argc > 5)
    {
        through_parameter(0, exit);
    }
    if (argc > 4)
    {
        through_struct_member(0, &member_ops);
    }
    if (argc > 3)
    {
        through_union_member(0, &member_union);
    }
    if (argc > 2)
    {
        through_first_of_member_list(0, &member_list);
    }
    if (argc > 1)
    {
        through_second_of_member_list(0, &member_list);
    }
    through_local_typedef(0, exit);
    return 5;
}
