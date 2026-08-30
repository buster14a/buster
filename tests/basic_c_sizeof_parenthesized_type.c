// `sizeof ((T))` is not C: the operand grammar is a unary expression or
// `( type-name )` with exactly one pair of parentheses, and `(T)` as an
// expression names a type where a value is required.  autoconf's
// AC_CHECK_TYPE compiles exactly this shape and requires it to fail --
// a compiler that accepts it reports every type as absent, which is how
// CPython's configure lost clock_t, socklen_t, ssize_t, mode_t, off_t,
// pid_t and size_t at once and fell back to `#define size_t unsigned int`.
typedef unsigned long word_type;

// The legal forms stay legal: the type form with its one pair, and a
// parenthesized expression wrapped as deep as it likes.
unsigned long type_form(void)
{
    return sizeof (word_type);
}

unsigned long expression_form(void)
{
    int object = 3;
    return sizeof ((object));
}

// The AC_CHECK_TYPE shape, verbatim.
int probe(void)
{
    if (sizeof ((word_type)))
        return 0;
    return 0;
}
