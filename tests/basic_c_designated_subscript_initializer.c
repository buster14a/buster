// C11 6.7.9p6: a designator is a chain, and `[index]` steps may follow and be
// followed by `.member` steps. musl reaches for this through its headers'
// member macros -- <pthread_impl.h> spells `_m_type` as `__u.__i[0]` and
// `_b_limit` as `__u.__i[2]`, so `*m = (mtx_t){ ._m_type = ... }` is a
// designator with a subscript in it -- and `src/misc/setrlimit.c` writes
// `struct ctx c = { .lim[0] = ..., .lim[1] = ... }` directly.
//
// Two things were missing. The initializer walker that can follow a chain
// only accepted `.member` steps, and the scan that decides which walker runs
// only recognized a chain when it saw a second `.`, so a subscript step went
// to the flat one-operand-per-slot machine that cannot represent a path into
// a slot at all.
//
// Every check reads the initialized object back, and the objects carry
// neighbours the initializers do not name, because a designator applied at
// the wrong offset writes a real member and only the untouched ones say so.

struct Limits
{
    long lim[2];
    int resource;
    int error;
};

union Storage
{
    int words[4];
    long wide;
};

struct Holder
{
    union Storage storage;
    int trailing;
};

struct Grid
{
    int cells[2][2];
    int trailing;
};

static struct Limits from_compound(long low, long high)
{
    return (struct Limits){
        .lim[0] = low,
        .lim[1] = high,
        .resource = 7,
        .error = -1,
    };
}

int main(void)
{
    // The shape setrlimit.c writes: two subscript designators and two plain
    // member designators in one initializer.
    struct Limits limits = {
        .lim[0] = 11,
        .lim[1] = 22,
        .resource = 3,
        .error = -1,
    };
    if (limits.lim[0] != 11 || limits.lim[1] != 22 || limits.resource != 3 || limits.error != -1)
    {
        return 1;
    }

    // Only the designated element is written; the rest of the object is zero.
    struct Limits sparse = {.lim[1] = 5};
    if (sparse.lim[0] != 0 || sparse.lim[1] != 5 || sparse.resource != 0 || sparse.error != 0)
    {
        return 2;
    }

    // Through a union member, which is the mutex and barrier shape.
    struct Holder holder = {
        .storage.words[2] = 6,
        .trailing = 9,
    };
    if (holder.storage.words[2] != 6 || holder.storage.words[0] != 0 || holder.storage.words[3] != 0 || holder.trailing != 9)
    {
        return 3;
    }

    // Two subscripts in one chain, and a brace-wrapped row beside them.
    struct Grid grid = {
        .cells[0][1] = 4,
        .trailing = 8,
    };
    if (grid.cells[0][0] != 0 || grid.cells[0][1] != 4 || grid.cells[1][0] != 0 || grid.cells[1][1] != 0 || grid.trailing != 8)
    {
        return 4;
    }
    struct Grid rows = {.cells[1] = {7, 8}};
    if (rows.cells[0][0] != 0 || rows.cells[1][0] != 7 || rows.cells[1][1] != 8 || rows.trailing != 0)
    {
        return 5;
    }

    // A compound literal assigned through a pointer, which is how
    // `*m = (mtx_t){ ._m_type = ... }` reaches the object.
    struct Holder assigned;
    assigned.trailing = 123;
    struct Holder *target = &assigned;
    *target = (union Storage){0}.words[0] ? assigned : (struct Holder){.storage.words[1] = 12};
    if (assigned.storage.words[1] != 12 || assigned.storage.words[0] != 0 || assigned.trailing != 0)
    {
        return 6;
    }

    // The same designators in a returned compound literal.
    struct Limits returned = from_compound(31, 32);
    if (returned.lim[0] != 31 || returned.lim[1] != 32 || returned.resource != 7 || returned.error != -1)
    {
        return 7;
    }

    // A constant expression as the subscript, and an out-of-order pair.
    enum
    {
        SECOND = 1,
    };
    struct Limits ordered = {
        .lim[SECOND] = 44,
        .lim[1 - 1] = 33,
    };
    if (ordered.lim[0] != 33 || ordered.lim[1] != 44)
    {
        return 8;
    }

    return 0;
}
