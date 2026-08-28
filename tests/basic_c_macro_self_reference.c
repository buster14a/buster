// C11 6.10.3.4p2: a macro name met while its own replacement is being
// rescanned is not replaced again, and the standard makes that refusal
// permanent -- "even if it is later (re)examined in contexts in which that
// nested replacement list preprocessing token would otherwise have been
// replaced".
//
// The expansion machinery keeps one `disabled` bit per macro rather than a
// hide set per token, which covers the rescan that happens in place but not a
// token that leaves it alive. A macro argument is expanded in a nested
// context, substituted, and rescanned again in the caller, by which time the
// bit is clear. musl's <signal.h> writes `#define si_pid __si_fields...si_pid`
// and passes `si.si_pid` through `__syscall`'s argument macros, and each
// nesting level expanded the name once more:
//
//   si.__si_fields.__si_common.__first.__piduid.
//      __si_fields.__si_common.__first.__piduid. ... si_pid
//
// which failed as "type '' has no member named '__si_fields'". Every check
// below reads a value rather than only compiling, because an over-expansion
// that still names a real member is a wrong answer, not a diagnostic.

struct Inner
{
    int value;
    int spare;
};

struct Outer
{
    struct Inner fields;
    int value;
    int spare;
};

static struct Outer subject = {{7, 8}, 9, 10};

static int recursive_call(int x)
{
    return x + 1;
}

// The object-like shape: the replacement ends in the macro's own name, so the
// macro selects the inner member of the same spelling.
#define value fields.value
// A function-like macro, to expand the name inside an argument.
#define wrap(x) ((long)(x))
#define outer_arg(a, b) ((a) + (b))
// Mutually recursive names: each stops when the other is being replaced, so
// `ping` preprocesses back to `ping`.
#define ping pong
#define pong ping
// A function-like macro that names itself, which must stay a call.
#define recursive_call(x) recursive_call((x) + 10)

// The macro reaches the inner member at file scope too.
static int mirrored = 0;

int main(void)
{
    // One level: `subject.value` is `subject.fields.value`, and the trailing
    // `value` is painted rather than expanded again.
    if (subject.value != 7)
    {
        return 1;
    }

    // Through one function-like macro's argument. The argument is expanded in
    // its own pass; the paint has to survive substitution into the
    // replacement and the rescan that follows.
    if (wrap(subject.value) != 7)
    {
        return 2;
    }

    // Two nesting levels, which is where the count of extra expansions used
    // to grow with the depth.
    if (outer_arg(wrap(subject.value), 0) != 7)
    {
        return 3;
    }

    // The painted token is a member name in the ordinary sense: writing
    // through the macro reaches the object the dotted spelling names.
    subject.value = 21;
    mirrored = subject.value;

    // Mutual recursion terminates, leaving the name it started from.
    int ping = 5;
    if (ping != 5)
    {
        return 4;
    }

    // A self-naming function-like macro expands once and the inner name stays
    // a call to the function: 1 + 10, then the function's own + 1.
    if (recursive_call(1) != 12)
    {
        return 5;
    }
    if (wrap(recursive_call(1)) != 12)
    {
        return 6;
    }

#undef value
    // With the macro gone, the plain spellings show which object each check
    // above actually reached: the inner member was written, the outer one was
    // left alone, and neither struct grew a second level of `fields`.
    if (subject.fields.value != 21 || mirrored != 21)
    {
        return 7;
    }
    if (subject.value != 9 || subject.spare != 10 || subject.fields.spare != 8)
    {
        return 8;
    }

    return 0;
}
