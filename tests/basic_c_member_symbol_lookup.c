// Lowering finds the member a `.` or `->` names by comparing the member
// token's interned symbol with each candidate IrField's, probed once per field
// from the preprocess symbol table, and compares spellings only for a token
// that carries no symbol (#1594). The search order is unchanged -- breadth
// first through anonymous members, the shallowest match wins -- so every
// access here must land on the same field, at the same offset, as before.
//
// Each case exits with its own code on a wrong answer: the offsets are checked
// against a layout computed by hand, and the stores are read back through a
// second route to the same storage so a wrong field index cannot pass.

typedef unsigned long size_type;

#define OFFSET_OF(type, member) ((size_type)(char*)&(((type*)0)->member))
#define PASTE(a, b) a##b
#define MEMBER_NAME value
#define SELECT(object, name) (object).name

typedef int type_t;

// Nested anonymous members, a struct inside a union inside a struct, so the
// search walks two anonymous levels before it finds `deep`.
struct Nested
{
    int head;
    union
    {
        struct
        {
            short low;
            short deep;
        };
        int whole;
    };
    int tail;
};

// The same name at different depths through named members: `o.x` is the
// outer field and `o.in.x` the inner one, and neither access may see the
// other's.
struct Inner
{
    int pad;
    int x;
};

struct Outer
{
    int x;
    struct Inner in;
};

// Members spelled like a typedef, a preprocessor operator and the program's
// own entry point: identifiers the symbol table also knows for other reasons.
struct Names
{
    int type_t;
    int defined;
    int main;
    type_t value;
};

// Bit-fields share a storage unit; the index the search returns decides which
// bits a store touches.
struct Bits
{
    unsigned first : 3;
    unsigned second : 5;
    unsigned : 0;
    unsigned third : 7;
};

// An aggregate reached only through a typedef name.
typedef struct
{
    long left;
    struct
    {
        long anonymous_right;
    };
} Pair;

int main(void)
{
    struct Nested nested = {0};
    nested.deep = 9;
    nested.low = 4;
    if (OFFSET_OF(struct Nested, deep) != sizeof(int) + sizeof(short) || OFFSET_OF(struct Nested, low) != sizeof(int))
    {
        return 1;
    }
    if (nested.deep != 9 || nested.low != 4 || (&nested)->deep != 9)
    {
        return 2;
    }
    nested.tail = 17;
    if (OFFSET_OF(struct Nested, tail) != 2 * sizeof(int) || nested.head != 0 || nested.tail != 17)
    {
        return 3;
    }

    struct Outer outer = {0};
    outer.x = 1;
    outer.in.x = 2;
    struct Outer* outer_pointer = &outer;
    if (outer_pointer->x != 1 || outer_pointer->in.x != 2 || outer.in.pad != 0)
    {
        return 4;
    }
    if (OFFSET_OF(struct Outer, x) != 0 || OFFSET_OF(struct Outer, in.x) != 2 * sizeof(int))
    {
        return 5;
    }

    struct Names names = {0};
    names.type_t = 3;
    names.defined = 5;
    names.main = 7;
    names.value = 11;
    type_t typed = names.type_t;
    if (typed != 3 || names.defined != 5 || names.main != 7 || names.value != 11)
    {
        return 6;
    }
    if (OFFSET_OF(struct Names, main) != 2 * sizeof(int) || OFFSET_OF(struct Names, value) != 3 * sizeof(int))
    {
        return 7;
    }

    // A member name built by token pasting, and one reached through a macro
    // that expands to it: both must name `value`, not a neighbour.
    names.PASTE(val, ue) = 13;
    if (names.value != 13 || names.MEMBER_NAME != 13 || SELECT(names, PASTE(ma, in)) != 7)
    {
        return 8;
    }
    (&names)->PASTE(def, ined) = 19;
    if (names.defined != 19 || OFFSET_OF(struct Names, PASTE(def, ined)) != sizeof(int))
    {
        return 9;
    }

    struct Bits bits = {0};
    bits.second = 21;
    bits.first = 5;
    bits.third = 100;
    if (bits.first != 5 || bits.second != 21 || bits.third != 100)
    {
        return 10;
    }
    bits.first = 0;
    if (bits.second != 21 || bits.third != 100)
    {
        return 11;
    }

    Pair pair = {0};
    Pair* pair_pointer = &pair;
    pair_pointer->anonymous_right = 23;
    pair.left = 29;
    if (pair.anonymous_right != 23 || pair_pointer->left != 29 || OFFSET_OF(Pair, anonymous_right) != sizeof(long))
    {
        return 12;
    }

    return 0;
}
