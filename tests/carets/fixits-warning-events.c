int abs(int);
double fabs(double);

int logical_and(int value)
{
    return value && (2 + 3);
}

int logical_or(int value)
{
    return value || (2 + 3);
}

unsigned absolute_unsigned(unsigned value)
{
    return abs(value + 1);
}

double absolute_real(double value)
{
    return abs(value);
}

long absolute_wide(long value)
{
    return abs(value);
}

long absolute_parenthesized(long value)
{
    return (abs)(value);
}

int needs_internal_linkage = 1;

inline int local_in_inline(void)
{
    static int local;
    return local;
}

int annotated(int value)
    __attribute__((diagnose_if((value == 1, 0), "disabled", "warning")));

#define LOGICAL_VALUE (2 + 3)
int logical_macro_operand(int value)
{
    return value && LOGICAL_VALUE;
}

#define ABSOLUTE abs
unsigned absolute_macro(unsigned value)
{
    return ABSOLUTE(value);
}

extern extern int duplicate_extern;
typedef typedef int DuplicateType;
const const int duplicate_qualifier = 1;

struct Designated { int member; };
struct Designated old_designator = { member: 1 };
int missing_equals[3] = { [1] 2 };
