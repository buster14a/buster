// The three compiler facts musl's <tgmath.h> is built out of.  Its
// type-generic macros do not use `_Generic`: they pick a function with a
// chain of `?:` over `sizeof` predicates and then *cast* the result back to
// the type the argument selected, and that cast is spelled with `__typeof__`
// over a conditional whose arms are pointers.  Three separate defects each
// made `sizeof pow(2.0, 0.5)` come back as `long double _Complex`, which is
// what failed libc-test's `functional/tgmath` against a Buster-built musl.
//
//   1. `__GNUC__` was predefined only in a GNU dialect.  Both clang and gcc
//      predefine it in every standard mode -- `clang -std=c99 -dM -E` reports
//      `__GNUC__ 4` beside `__STRICT_ANSI__ 1` -- and <tgmath.h> drops all of
//      its return casts without it, so `ide cc -std=c99` and `clang -std=c99`
//      read different source out of the same header.
//   2. `0 ? (t *)0 : (void *)1` kept `t *`.  C11 6.5.15p6 makes it `void *`:
//      the `void *` arm is not a null pointer constant, so the two pointers
//      merge on the void side.  That conditional *is* musl's `__type1(c,t)`,
//      the switch that names `t` when `c` holds and `void` when it does not,
//      so without the rule every arm of every return cast named its first
//      type.
//   3. The strict operand type walk did not strip GNU `__extension__`, and
//      musl spells `I` as `(__extension__ (0.0f+1.0fi))` under `__GNUC__`.
//      The marker made the whole parenthesized group resolve as its leading
//      `0.0f`, so `0*I` was a float and a complex argument selected the real
//      function.
//
// Written the way <tgmath.h> writes it, because the shapes are the test.
// Everything is a compile-time type question, so nothing here is a call: the
// fixture returns a distinct exit code per wrong answer and nothing else.

#ifdef __GNUC__
#define GNUC_PREDEFINED 1
#else
#define GNUC_PREDEFINED 0
#endif

#ifdef __STRICT_ANSI__
#define STRICT_ANSI_PREDEFINED 1
#else
#define STRICT_ANSI_PREDEFINED 0
#endif

// musl's own spellings, verbatim.
#define IS_FP(x) (sizeof((x) + 1ULL) == sizeof((x) + 1.0f))
// if c then t else void
#define TYPE1(c, t) __typeof__(*(0 ? (t*)0 : (void*)!(c)))
// if c then t1 else t2
#define TYPE2(c, t1, t2) __typeof__(*(0 ? (TYPE1(c, t1)*)0 : (TYPE1(!(c), t2)*)0))
// cast to double when x is integral, otherwise use typeof(x)
#define RETCAST(x) (TYPE2(IS_FP(x), __typeof__(x), double))
// the 2-argument form, which is what pow() uses
#define RETCAST_2(x, y) (TYPE2(IS_FP(x) && IS_FP(y), __typeof__((x) + (y)), __typeof__((x) + (y) + 1.0)))

#define IMAGINARY_UNIT (__extension__(0.0f + 1.0fi))

int main(void)
{
    // 1. The dialect predefines.  This file is compiled -std=c99, so both
    // macros have to be visible at once, the way both reference compilers
    // report them.
    if (!GNUC_PREDEFINED)
    {
        return 1;
    }
    if (!STRICT_ANSI_PREDEFINED)
    {
        return 2;
    }

    // 2. The conditional's two pointer clauses.  A null pointer constant on
    // one arm yields the other arm's type -- and `(void *)0` is one of the
    // two spellings of a null pointer constant, not merely a `void *`.
    if (sizeof(TYPE1(1, double)) != sizeof(double))
    {
        return 3;
    }
    if (sizeof(TYPE1(1, double _Complex)) != sizeof(double _Complex))
    {
        return 4;
    }
    // Neither arm is a null pointer constant here, so the `void *` arm wins
    // and the type is `void`.  `sizeof(void)` cannot say so -- it is a GNU
    // extension and not what is under test -- so ask for the type itself.
    if (!__builtin_types_compatible_p(TYPE1(0, double), void) || !__builtin_types_compatible_p(TYPE1(1, double), double))
    {
        return 5;
    }
    if (sizeof(TYPE2(0, double, float)) != sizeof(float))
    {
        return 6;
    }
    if (sizeof(TYPE2(1, double, float)) != sizeof(double))
    {
        return 7;
    }

    // 3. `__extension__` in front of a parenthesized group contributes no
    // type of its own.
    if (sizeof(IMAGINARY_UNIT) != sizeof(float _Complex))
    {
        return 8;
    }
    if (sizeof(2.0 + 0 * IMAGINARY_UNIT) != sizeof(double _Complex))
    {
        return 9;
    }

    // The three together: the return cast <tgmath.h> puts in front of its
    // selection chain, for the four argument shapes libc-test checks.
    if (sizeof(RETCAST(2.0f) 0) != sizeof(float))
    {
        return 10;
    }
    if (sizeof(RETCAST(2) 0) != sizeof(double))
    {
        return 11;
    }
    if (sizeof(RETCAST_2(2.0, 0.5) 0) != sizeof(double))
    {
        return 12;
    }
    if (sizeof(RETCAST_2(2.0f, 0.5f) 0) != sizeof(float))
    {
        return 13;
    }
    if (sizeof(RETCAST_2(2.0, 0.5 + 0 * IMAGINARY_UNIT) 0) != sizeof(double _Complex))
    {
        return 14;
    }
    // The integer argument, whose cast is the whole reason `__type2` takes a
    // second type at all: `sqrt(8)` is the double overload.
    if (sizeof(RETCAST_2(8, 0.5f) 0) != sizeof(double))
    {
        return 15;
    }
    return 0;
}
