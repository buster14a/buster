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

static int check_designator_continuation(void);

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

    return check_designator_continuation();
}

// C17 6.7.9p17-20: after a chained designator, the next item continues
// within its innermost selected aggregate before advancing outward.
struct Tail
{
    int values[2];
    int tail;
};

struct Two
{
    int values[2];
};

struct Pair
{
    int first;
    int second;
};

struct Named
{
    struct Pair pair;
    int tail;
};

struct ContinuationGrid
{
    int rows[2][2];
    int tail;
};

struct Promoted
{
    struct
    {
        int values[2];
    };
    int tail;
};

struct UnionOuter
{
    union
    {
        struct Pair pair;
        int raw;
    } choice;
    int tail;
};

static struct Tail static_tail = {.values[0] = 1, 2};
static struct Two static_two = {.values[0] = 3, 4};
static struct Named static_named = {.pair.first = 5, 6, 7};
static struct ContinuationGrid static_grid = {.rows[0][0] = 8, 9, 10, 11, 12};
static int static_root_array[2][2] = {[0][0] = 19, 20, 21, 22};
static struct Promoted static_promoted = {.values[0] = 13, 14, 15};
static struct UnionOuter static_union = {.choice.pair.first = 16, 17, 18};

static int check_designator_continuation(void)
{
    int result = 0;
    struct Tail wrong_field = {.values[0] = 1, 2};
    if (wrong_field.values[0] != 1 || wrong_field.values[1] != 2 || wrong_field.tail != 0) result = 1;

    struct Two no_outer_field = {.values[0] = 3, 4};
    if (no_outer_field.values[0] != 3 || no_outer_field.values[1] != 4) result = 2;

    struct Named named = {.pair.first = 5, 6, 7};
    if (named.pair.first != 5 || named.pair.second != 6 || named.tail != 7) result = 3;

    struct ContinuationGrid grid = {.rows[0][0] = 8, 9, 10, 11, 12};
    if (grid.rows[0][0] != 8 || grid.rows[0][1] != 9 || grid.rows[1][0] != 10 || grid.rows[1][1] != 11 || grid.tail != 12) result = 4;

    int root_array[2][2] = {[0][0] = 19, 20, 21, 22};
    if (root_array[0][0] != 19 || root_array[0][1] != 20 || root_array[1][0] != 21 || root_array[1][1] != 22) result = 5;

    struct Promoted promoted = {.values[0] = 13, 14, 15};
    if (promoted.values[0] != 13 || promoted.values[1] != 14 || promoted.tail != 15) result = 6;

    struct UnionOuter union_member = {.choice.pair.first = 16, 17, 18};
    if (union_member.choice.pair.first != 16 || union_member.choice.pair.second != 17 || union_member.tail != 18) result = 7;

    struct Tail end_of_inner = {.values[1] = 23, 24};
    if (end_of_inner.values[0] != 0 || end_of_inner.values[1] != 23 || end_of_inner.tail != 24) result = 8;

    struct Tail reset_designator = {.values[0] = 25, .tail = 26};
    if (reset_designator.values[0] != 25 || reset_designator.values[1] != 0 || reset_designator.tail != 26) result = 9;

    struct Tail braced = {.values = {27, 28}, 29};
    if (braced.values[0] != 27 || braced.values[1] != 28 || braced.tail != 29) result = 10;

    // Disjoint deferred braces and omitted zero members remain intact.
    struct Tail deferred_disjoint = {.values = {34, 35}, .tail = 36};
    struct Tail omitted_zero = {.values = {37}, .tail = 38};
    if (deferred_disjoint.values[0] != 34 || deferred_disjoint.values[1] != 35 || deferred_disjoint.tail != 36 ||
        omitted_zero.values[0] != 37 || omitted_zero.values[1] != 0 || omitted_zero.tail != 38) result = 13;

    struct Tail literal = (struct Tail){.values[0] = 30, 31};
    struct Two literal_two = (struct Two){.values[0] = 32, 33};
    if (literal.values[0] != 30 || literal.values[1] != 31 || literal.tail != 0 || literal_two.values[0] != 32 ||
        literal_two.values[1] != 33) result = 11;

    if (static_tail.values[0] != 1 || static_tail.values[1] != 2 || static_tail.tail != 0 ||
        static_two.values[0] != 3 || static_two.values[1] != 4 ||
        static_named.pair.first != 5 || static_named.pair.second != 6 || static_named.tail != 7 ||
        static_grid.rows[0][0] != 8 || static_grid.rows[0][1] != 9 || static_grid.rows[1][0] != 10 ||
        static_grid.rows[1][1] != 11 || static_grid.tail != 12 ||
        static_root_array[0][0] != 19 || static_root_array[0][1] != 20 || static_root_array[1][0] != 21 || static_root_array[1][1] != 22 ||
        static_promoted.values[0] != 13 || static_promoted.values[1] != 14 || static_promoted.tail != 15 ||
        static_union.choice.pair.first != 16 || static_union.choice.pair.second != 17 || static_union.tail != 18) result = 12;

    return result;
}
