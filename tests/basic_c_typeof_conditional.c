// `__typeof__` over a dereferenced conditional -- the shape musl's
// <tgmath.h> is built out of, and the shape that resolved to no type at all.
//
//   #define __type1(c,t)      __typeof__(*(0?(t*)0:(void*)!(c)))
//   #define __type2(c,t1,t2)  __typeof__(*(0?(__type1(c,t1)*)0:(__type1(!(c),t2)*)0))
//
// The two are spelled `type1_of`/`type2_of` below only because a leading
// double underscore is reserved to the implementation and every reference
// compiler warns about defining one; the token sequences they expand to are
// musl's.
//
// Two independent gaps sat under this. The parse-side walk that resolves a
// `__typeof__` operand had no unary `*` or `&` at all, so it could only
// answer for a prefix over a plain identifier chain -- `__typeof__(*(double
// *)0)` already failed, with no conditional anywhere in the spelling. And its
// conditional merge compared type ids only, so two pointer arms that were not
// the same id resolved to nothing. A declaration written on either therefore
// declared no name, and the first *use* of the name -- not the `__typeof__`
// -- was the reported error.
//
// The rule the merge now follows is C11 6.5.15p6 in its own order: a null
// pointer constant on either side yields the *other* operand's type, and only
// if neither is one does a `void *` operand pull an object pointer to
// `void *`. That order is the whole of `__type1`: `(void *)!(c)` is a null
// pointer constant exactly when `c` holds, so the macro names `t` when it
// does and `void` when it does not.
//
// Every answer below is observable at run time -- a wrong type is a wrong
// size or a wrong value -- because a declaration that resolves to the wrong
// type still compiles and still links.

#define type1_of(c, t) __typeof__(*(0 ? (t *)0 : (void *)!(c)))
#define type2_of(c, t1, t2) __typeof__(*(0 ? (type1_of(c, t1) *)0 : (type1_of(!(c), t2) *)0))

static double object = 2.5;
static int numbers[4] = {10, 20, 30, 40};

struct pair
{
    int first;
    long second;
};

static struct pair pair_object = {7, 9};

int main(void)
{
    // A prefix `*` over something that is not an identifier: a cast. No
    // conditional is involved, and this alone used to declare nothing.
    __typeof__(*(double *)0) from_cast = 1.5;
    if (from_cast != 1.5 || sizeof from_cast != sizeof(double))
    {
        return 1;
    }

    // The same through a conditional whose arms are the same pointer type.
    __typeof__(*(0 ? (double *)0 : (double *)0)) same_arms = 3.5;
    if (same_arms != 3.5 || sizeof same_arms != sizeof(double))
    {
        return 2;
    }

    // A `void *` arm that *is* a null pointer constant selects the other
    // arm's type -- clang and gcc both make this `double`.
    __typeof__(*(0 ? (double *)0 : (void *)0)) selected = 4.5;
    if (selected != 4.5 || sizeof selected != sizeof(double))
    {
        return 3;
    }

    // Qualifiers on one arm's element do not change which type is selected.
    __typeof__(*(0 ? (const double *)0 : (double *)0)) qualified = 5.5;
    if (qualified != 5.5 || sizeof qualified != sizeof(double))
    {
        return 4;
    }

    // A pointer against a plain integer null pointer constant.
    __typeof__(0 ? (long *)0 : 0) integer_arm = 0;
    if (sizeof integer_arm != sizeof(long *) || integer_arm != 0)
    {
        return 5;
    }

    // Unary `&` under the same walk, and a prefix over a member chain rather
    // than over a bare name.
    __typeof__(&object) address = &object;
    __typeof__(*&pair_object) whole = pair_object;
    if (*address != 2.5 || whole.first != 7 || whole.second != 9)
    {
        return 6;
    }

    // A prefix binds tighter than any binary operator, so the walk has to
    // split at the `+` first and take the address only of `numbers[1]`.
    __typeof__(&numbers[1] + 1) stepped = numbers + 3;
    if (*stepped != 40 || sizeof(*stepped) != sizeof(int))
    {
        return 7;
    }

    // musl's `__type1`, with a literal condition and with the `sizeof`
    // comparison <tgmath.h> actually writes. A true condition names the type;
    // a false one names `void`, so only the true side can declare an object
    // -- the false side is what `__type2` composes with, below.
    type1_of(1, float) narrow = 1;
    type1_of(sizeof(object) == sizeof(double), long) wide = 1;
    if (sizeof narrow != sizeof(float) || sizeof wide != sizeof(long))
    {
        return 8;
    }

    // musl's `__type2` selects between two types, and the composition is what
    // exercises the `void *` arm in *both* positions: with a false condition
    // the outer conditional receives its `(void *)0` on the left instead of
    // on the right.
    type2_of(1, float, double) first_branch = 1;
    type2_of(0, float, double) second_branch = 1;
    if (sizeof first_branch != sizeof(float) || sizeof second_branch != sizeof(double))
    {
        return 9;
    }

    float single = 1;
    type2_of(sizeof(single) == sizeof(float), float, double) matched = 1;
    type2_of(sizeof(object) == sizeof(float), float, double) unmatched = 1;
    if (sizeof matched != sizeof(float) || sizeof unmatched != sizeof(double))
    {
        return 10;
    }

    // The selected type is used, not merely sized: a `long` holding a value
    // no `int` can, and a `float` narrowing one a `double` would keep.
    type2_of(1, long, int) held = 0x1FFFFFFFFLL;
    type2_of(1, float, double) rounded = 16777217.0;
    if (held != 0x1FFFFFFFFLL || rounded != 16777216.0f)
    {
        return 11;
    }

    return 0;
}
