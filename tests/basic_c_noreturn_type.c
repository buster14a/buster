// `noreturn` written on a function pointer type or a typedef rather than on a
// function declaration.  The call site sees only the type -- there is no
// declaration behind a call through a pointer -- so the marker has to ride on
// the function type, and a call through one has to end control flow the same
// way a call to a noreturn declaration does.
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

static int through_shared_typedef(int status, plain_pointer* other)
{
    other(status);
    return status + 2;
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
    // Only one of these ever runs, and each ends in exit(0); the point of the
    // fixture is that all four bodies lower at all.
    if (argc > 3)
    {
        through_typedef_pointer(0, exit);
    }
    if (argc > 2)
    {
        through_typedef_function(0, exit);
    }
    if (argc > 1)
    {
        through_parameter(0, exit);
    }
    through_local_typedef(0, exit);
    return 3;
}
