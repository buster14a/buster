// #3338: operator warnings whose token a system header's macro writes into user code are given, at the expansion, with
// its note (the operator's spelling is the system header's): division by zero, shift counts, precedence, chained
// comparisons, null dereferences, string arithmetic and dangling else. An array index past the end is quiet where the
// `]` and the index are both spelled in the system header (CheckArrayAccess).
# 1 "system-operators.h" 3
#define SYSTEM_ZERO 0
#define SYSTEM_DIVIDE(a) ((a) / 0)
#define SYSTEM_SHIFT(a) ((a) << 40)
#define SYSTEM_SHIFT_ADD(a, b, c) (a << b + c)
#define SYSTEM_CHAIN(a, b, c) a < b < c
#define SYSTEM_NULL *(int *)0
#define SYSTEM_STRING_PLUS "abc" + 1
#define SYSTEM_DANGLING(c, d) if (c) if (d) ; else ;
#define SYSTEM_NOT(a) (!(a) == 1)
#define SYSTEM_ELEMENT(a) a[5]
#define SYSTEM_FIVE 5
#define SYSTEM_BOOL(x) (x << 1)
#define SYSTEM_EMPTY(c) if (c);
# 18 "operators.c"
int operate(int a, int b, int c)
{
    int array[5];
    const char *s = "x";
    int i = a / SYSTEM_ZERO;
    i += SYSTEM_DIVIDE(a);
    i += SYSTEM_SHIFT(a);
    i += SYSTEM_SHIFT_ADD(a, b, c);
    i += SYSTEM_CHAIN(a, b, c);
    i += SYSTEM_NULL;
    s = SYSTEM_STRING_PLUS;
    SYSTEM_DANGLING(a, b)
    i += SYSTEM_NOT(a);
    i += SYSTEM_ELEMENT(array);
    i += array[SYSTEM_FIVE];
    if (SYSTEM_BOOL(a)) {}
    SYSTEM_EMPTY(a)
    return i + *s;
}
# 1 "system-comparisons.h" 3
#define SYSTEM_LESS(a, b) ((a) < (b))
#define SYSTEM_PRECEDENCE(a, b, c) (a & b == c)
#define SYSTEM_SAME(x) ((x) == (x))
# 40 "operators.c"
#pragma clang diagnostic warning "-Wsign-compare"
int compare(int a, unsigned u, int b, int c)
{
    int i = SYSTEM_LESS(a, u);
    i += SYSTEM_PRECEDENCE(a, b, c);
    i += SYSTEM_SAME(a);
    return i;
}
