// GNU `aligned(N)` on a bit-field was refused outright, where clang and gcc
// accept it and lay it out (issue #824). C11 6.7.5p2 does forbid `_Alignas`
// there and both reference compilers refuse that spelling, so only the standard
// half stays refused; the two spellings share one table, so the run is
// partitioned by spelling the way a typedef's specifier-position run is.
//
// The placement rule, measured against clang and gcc on 2026-08-30: the field
// starts at the next multiple of N bytes -- unconditionally, not only when it
// would straddle its storage unit, and against the attribute's operand rather
// than the alignment the declared type raises it to. `Lowered` is the case that
// pins the second half: `aligned(1)` puts the field at bit 24 where the unit
// rule alone puts it at 20, so this request is the one thing that moves a
// bit-field down. A named field raises the aggregate's alignment with it; an
// unnamed one does not, which `Unnamed` and `NarrowUnnamed` pin.

struct Raised { unsigned a : 3; unsigned b : 5 __attribute__((aligned(4))); };
struct Halved { unsigned a : 3; unsigned b : 5 __attribute__((aligned(2))); };
struct Widened { unsigned a : 3; unsigned b : 5 __attribute__((aligned(8))); };
struct AfterByte { char c; unsigned b : 5 __attribute__((aligned(4))); };
struct Lowered { unsigned a : 20; unsigned b : 5 __attribute__((aligned(1))); };
struct Followed { unsigned a : 3; unsigned b : 5 __attribute__((aligned(4))); unsigned c : 4; };
struct Unnamed { unsigned a : 3; unsigned : 5 __attribute__((aligned(4))); unsigned c : 4; };
struct WideUnnamed { unsigned a : 3; unsigned : 5 __attribute__((aligned(8))); unsigned c : 4; };
struct NarrowUnnamed { char a : 3; char : 2 __attribute__((aligned(4))); char c : 4; };
struct NarrowType { char a; int b : 5 __attribute__((aligned(16))); };
struct NarrowUnit { unsigned a : 3; unsigned char b : 5 __attribute__((aligned(4))); };
// `aligned` raises what `packed` lowered, so the field is still pushed out.
struct Packed { unsigned a : 3; unsigned b : 5 __attribute__((aligned(4), packed)); };
// The attribute may also sit among the specifiers rather than after the width.
struct Specifier { unsigned a : 3; __attribute__((aligned(4))) unsigned b : 5; };
union Member { unsigned a : 3; unsigned b : 5 __attribute__((aligned(8))); };

static struct Raised raised_global = {1, 2};
static struct Followed followed_global = {1, 2, 3};

#define CHECK_LAYOUT(type, expected_size, expected_alignment)                                                                                        \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (sizeof(type) != (expected_size) || _Alignof(type) != (expected_alignment))                                                                \
        {                                                                                                                                            \
            return step;                                                                                                                              \
        }                                                                                                                                            \
        step += 1;                                                                                                                                    \
    } while (0)

int main(void)
{
    int step = 1;
    struct Raised raised_local = {1, 2};
    struct Followed followed_local = {1, 2, 3};
    CHECK_LAYOUT(struct Raised, 8, 4);
    CHECK_LAYOUT(struct Halved, 4, 4);
    CHECK_LAYOUT(struct Widened, 16, 8);
    CHECK_LAYOUT(struct AfterByte, 8, 4);
    CHECK_LAYOUT(struct Lowered, 4, 4);
    CHECK_LAYOUT(struct Followed, 8, 4);
    CHECK_LAYOUT(struct Unnamed, 8, 4);
    CHECK_LAYOUT(struct WideUnnamed, 12, 4);
    CHECK_LAYOUT(struct NarrowUnnamed, 5, 1);
    CHECK_LAYOUT(struct NarrowType, 32, 16);
    CHECK_LAYOUT(struct NarrowUnit, 8, 4);
    CHECK_LAYOUT(struct Packed, 8, 4);
    CHECK_LAYOUT(struct Specifier, 8, 4);
    CHECK_LAYOUT(union Member, 8, 8);
    if (raised_global.a != 1 || raised_global.b != 2)
    {
        return 90;
    }
    if (followed_global.a != 1 || followed_global.b != 2 || followed_global.c != 3)
    {
        return 91;
    }
    if (raised_local.a != 1 || raised_local.b != 2)
    {
        return 92;
    }
    if (followed_local.a != 1 || followed_local.b != 2 || followed_local.c != 3)
    {
        return 93;
    }
    return 0;
}
