// A member access that another `.` walks straight through designates a place,
// not a value.  `((T *)p)->a.b` names b; a is only the route to it, and C
// 6.5.2.3 gives that route no read of its own.  The expression walk loaded the
// intermediate aggregate anyway -- a whole-object copy into a frame temporary
// -- and then recovered the place it needed from that load's operand, so every
// answer was right and the copy was dead.  A dead copy is still a read of
// memory.
//
// That is what offsetof is written on.  A compiler without
// __builtin_offsetof gets musl's other spelling,
//
//     #define offsetof(type, member) ((size_t)( (char *)&(((type *)0)->member) - (char *)0 ))
//
// and a member named through two accesses -- musl's `_m_next` is
// `__u.__p[4]` -- then copied a whole `pthread_mutex_t` out of the null
// pointer the expression was never going to dereference.  musl's
// `__pthread_exit` computes exactly that offset while walking the robust-mutex
// list, so libc-test's `functional/pthread_robust` and
// `regression/pthread-robust-detach` died in the exiting thread with SIGSEGV
// (#737).  Predefining `__GNUC__` in every dialect took musl to the builtin
// and closed both units on its own, so this fixture spells the pointer form
// itself rather than including a header: the shape has to stay pinned for the
// programs that still write it, whichever offsetof a libc picks.
//
// The array arm this rule sits beside is C 6.5.3.2p4, pinned in
// tests/basic_c_pointer_to_array_place.c.  What is here is the aggregate half:
// the null-pointer offsetof that faults when the copy comes back, the same
// chains through a live pointer so the place still reads and writes the object
// the expression names, and the member that really is a value, so keeping a
// place does not swallow the load a by-value read needs.
//
// The last cases are the parenthesized spellings, `(*o).a.b` and
// `&(((T *)0)->a).b[i]`.  The walk cannot see the `.` that follows a group
// from inside it, so those two emitted the copy whatever the peek decided, and
// dropping it is the load-recovery every `E.m` behind a just-emitted load now
// performs (#741).

typedef unsigned long size_type;

// musl's spelling, for the compiler that has no __builtin_offsetof.  The null
// pointer is never dereferenced: `&` and the member designators produce an
// address and nothing reads through it.
#define OFFSET_OF(type, member) ((size_type)(char*)&(((type*)0)->member))

// The shape of musl's pthread_mutex_t: one union member, reached through a
// second access, holding the array whose element the robust list walks to.
typedef struct
{
    union
    {
        int i[10];
        volatile void* volatile p[5];
    } u;
} MutexLike;

struct Inner
{
    long v[5];
    int x;
};

struct Outer
{
    int a;
    struct Inner in;
};

struct Deep
{
    int head;
    struct Outer out;
};

static struct Outer object;
static MutexLike mutex_object;
static struct Deep deep_object;

// Out of line and by value, so the argument is a real copy of the member: the
// load a chain that ends here needs must still be emitted.
static long inner_sum(struct Inner value)
{
    long total = value.x;
    for (size_type index = 0; index < 5; index += 1)
    {
        total += value.v[index];
    }
    return total;
}

int main(void)
{
    // The offsets, against the same distances measured on a live object.  The
    // comparison is what makes this a test of the arithmetic and not only of
    // the fault: dropping the member walk entirely would answer zero here.
    void* base = &object;
    if (OFFSET_OF(struct Outer, in.x) != (size_type)((char*)&object.in.x - (char*)base))
    {
        return 1;
    }
    if (OFFSET_OF(struct Outer, in.v[3]) != (size_type)((char*)&object.in.v[3] - (char*)base))
    {
        return 2;
    }
    // musl's own: a union reached through the second access, then the element
    // the robust-mutex list walks to.
    if (OFFSET_OF(MutexLike, u.p[4]) != (size_type)((char*)&mutex_object.u.p[4] - (char*)&mutex_object))
    {
        return 3;
    }
    // Three accesses, so the middle one is both walked into and walked out of.
    if (OFFSET_OF(struct Deep, out.in.x) != (size_type)((char*)&deep_object.out.in.x - (char*)&deep_object))
    {
        return 4;
    }

    // The same chains through a pointer that does name an object: the place
    // has to be the object's own storage, not a copy of it.
    void* pointer = &object;
    ((struct Outer*)pointer)->in.x = 7;
    ((struct Outer*)pointer)->in.v[3] = 41;
    if (object.in.x != 7 || object.in.v[3] != 41)
    {
        return 5;
    }
    if (((struct Outer*)pointer)->in.x != 7 || ((struct Outer*)pointer)->in.v[3] != 41)
    {
        return 6;
    }
    // An address taken out of the middle of the chain names that storage too.
    if (&((struct Outer*)pointer)->in.v[3] != &object.in.v[3])
    {
        return 7;
    }
    // The parenthesized dereference spelling of the same chain.
    if ((*(struct Outer*)pointer).in.x != 7)
    {
        return 8;
    }
    // A base that is pointer arithmetic rather than a cast.
    struct Outer pair[2];
    pair[0].in.x = 11;
    pair[1].in.x = 12;
    if ((pair + 1)->in.x != 12 || (pair + 0)->in.x != 11)
    {
        return 9;
    }

    // Three accesses through a live pointer, storing at the deepest one.
    void* deep_pointer = &deep_object;
    ((struct Deep*)deep_pointer)->out.in.v[1] = 23;
    if (deep_object.out.in.v[1] != 23 || ((struct Deep*)deep_pointer)->out.in.v[1] != 23)
    {
        return 10;
    }

    // The member that is the answer rather than the route: a by-value read of
    // the aggregate itself, which is the load this rule must not swallow.
    object.in.v[0] = 1;
    object.in.v[1] = 2;
    object.in.v[2] = 3;
    object.in.v[4] = 5;
    struct Inner copy = ((struct Outer*)pointer)->in;
    if (copy.x != 7 || copy.v[0] != 1 || copy.v[3] != 41)
    {
        return 11;
    }
    // 1 + 2 + 3 + 41 + 5 + 7
    if (inner_sum(((struct Outer*)pointer)->in) != 59)
    {
        return 12;
    }
    // Writing through the copy leaves the object alone, which is what says the
    // by-value read produced a copy and not the place.
    copy.x = 99;
    if (object.in.x != 7)
    {
        return 13;
    }

    // The parenthesized spellings of the same walk.  The `.` that continues
    // the chain sits outside the group the intermediate member was produced
    // in, so the token peek in the member arm cannot see it and the walk
    // emitted the copy anyway, recovering the place it needed from the load's
    // own operand (#741).  These two are offsetof again, so the copy faults on
    // the null pointer exactly the way #737 did.
    if ((size_type)(char*)&(((struct Outer*)0)->in).v[3] != (size_type)((char*)&object.in.v[3] - (char*)base))
    {
        return 14;
    }
    if ((size_type)(char*)&(*(struct Outer*)0).in.x != (size_type)((char*)&object.in.x - (char*)base))
    {
        return 15;
    }
    // A group around the middle of three accesses, walked into and out of.
    if ((size_type)(char*)&(((struct Deep*)0)->out).in.x != (size_type)((char*)&deep_object.out.in.x - (char*)&deep_object))
    {
        return 16;
    }

    // The same groups through a pointer that does name an object: the place
    // has to be the object's own storage, not the copy the group emitted.
    (((struct Outer*)pointer)->in).v[2] = 31;
    (*(struct Outer*)pointer).in.x = 29;
    if (object.in.v[2] != 31 || object.in.x != 29)
    {
        return 17;
    }
    if ((((struct Outer*)pointer)->in).v[2] != 31 || (*(struct Outer*)pointer).in.x != 29)
    {
        return 18;
    }
    // A chain that ends at the aggregate still reads it, group or no group.
    struct Inner group_copy = (*(struct Outer*)pointer).in;
    group_copy.x = 71;
    if (group_copy.v[2] != 31 || object.in.x != 29)
    {
        return 19;
    }

    return 0;
}
