// The portable `offsetof` every pre-C11 header falls back to:
//
//   #define offsetof(type, member) \
//           ((size_t)((char *)&(((type *)0)->member) - (char *)0))
//
// musl's <stddef.h> spells it that way whenever the compiler does not claim
// `__GNUC__ > 3`, which Buster does not under `-std=c99`, so the whole musl
// build takes this branch: `src/errno/strerror.c` builds its message index
// out of 134 of them and `ldso/dynlink.c` computes MIN_TLS_ALIGN with one,
// both in static storage. The constant evaluator already folded `&object.m`
// into a symbol plus an addend and already differenced two pointers into the
// same object; what it could not do was follow `->` from a constant pointer,
// so the null-based form never reached the member at all.
//
// Every offset below is also computed with __builtin_offsetof and compared
// against it, and the members are then written through at the folded offsets,
// so a fold that answered a plausible-but-wrong number fails here.

struct Simple
{
    int first;
    char second[6];
    long third;
};

struct Nested
{
    char lead;
    struct Simple inner;
    int trailing[3];
};

union Overlaid
{
    long wide;
    struct Simple parts;
};

#define OFFSET(type, member) ((unsigned long)((char *)&(((type *)0)->member) - (char *)0))

static unsigned long simple_first = OFFSET(struct Simple, first);
static unsigned long simple_second = OFFSET(struct Simple, second);
static unsigned long simple_third = OFFSET(struct Simple, third);

// A table of them, which is the shape strerror.c builds.
static const unsigned short table[] = {
    [2] = (unsigned short)OFFSET(struct Simple, third),
    [1] = (unsigned short)OFFSET(struct Simple, second),
    [0] = (unsigned short)OFFSET(struct Simple, first),
};

// Nested members, an array member's element, and a union member.
static unsigned long nested_inner_third = OFFSET(struct Nested, inner.third);
static unsigned long nested_trailing_two = OFFSET(struct Nested, trailing[2]);
static unsigned long union_parts_second = OFFSET(union Overlaid, parts.second);

// Arithmetic around one, and one used as an array bound.
static unsigned long adjusted = OFFSET(struct Simple, third) + 1;
static char sized[OFFSET(struct Simple, third)];

int main(void)
{
    if (simple_first != __builtin_offsetof(struct Simple, first) ||
        simple_second != __builtin_offsetof(struct Simple, second) ||
        simple_third != __builtin_offsetof(struct Simple, third))
    {
        return 1;
    }
    if (table[0] != simple_first || table[1] != simple_second || table[2] != simple_third)
    {
        return 2;
    }
    if (nested_inner_third != __builtin_offsetof(struct Nested, inner.third) ||
        nested_trailing_two != __builtin_offsetof(struct Nested, trailing[2]) ||
        union_parts_second != __builtin_offsetof(union Overlaid, parts.second))
    {
        return 3;
    }
    if (adjusted != simple_third + 1 || sizeof sized != simple_third)
    {
        return 4;
    }

    // The offsets name the members they claim to: writing through each one
    // and reading the member back is the check a wrong-but-plausible number
    // does not survive.
    struct Simple subject = {0, {0, 0, 0, 0, 0, 0}, 0};
    char *base = (char *)&subject;
    *(int *)(base + simple_first) = 11;
    *(base + simple_second) = 22;
    *(long *)(base + simple_third) = 33;
    if (subject.first != 11 || subject.second[0] != 22 || subject.third != 33)
    {
        return 5;
    }

    struct Nested outer;
    char *outer_base = (char *)&outer;
    *(long *)(outer_base + nested_inner_third) = 44;
    *(int *)(outer_base + nested_trailing_two) = 55;
    if (outer.inner.third != 44 || outer.trailing[2] != 55)
    {
        return 6;
    }

    // The same expression in function scope, where it already folded, must
    // agree with the static one.
    unsigned long local = OFFSET(struct Simple, third);
    if (local != simple_third)
    {
        return 7;
    }

    return 0;
}
