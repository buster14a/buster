// A sizeof (or _Alignof) operand is unevaluated, but it is still an
// expression, and a member chain that reaches a resolved aggregate with no
// member of that name is a constraint violation -- not an operand the type
// prediction may guess int for.  The silent guess is what every autoconf
// AC_CHECK_MEMBER fallback probe rests on: `if (sizeof ac_aggr.st_birthtime)`
// must fail to compile on a struct stat that has no st_birthtime, or
// configure defines HAVE_STRUCT_STAT_ST_BIRTHTIME on Linux and the build
// reads a member that does not exist.  CPython's configure hit this for
// st_birthtime, st_flags and st_gen.
struct stat_like
{
    unsigned long st_dev;
    unsigned long st_ino;
};

// The valid spellings stay valid: a diagnostic that reached past the missing
// name would trip on these first and change the count below.
unsigned long valid_dot(void)
{
    static struct stat_like aggregate;
    return sizeof aggregate.st_ino;
}

unsigned long valid_arrow(struct stat_like* pointer)
{
    return sizeof pointer->st_dev;
}

// The AC_CHECK_MEMBER fallback shape, verbatim.
int probe(void)
{
    static struct stat_like ac_aggr;
    if (sizeof ac_aggr.st_birthtime)
        return 0;
    return 0;
}
