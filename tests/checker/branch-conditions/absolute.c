// A condition on an lvalue at an absolute address (#3028): its truth is its value's, which the program loads, and its
// address says nothing of it. The unit's first bodies are the forms, each with one such lvalue: an `if`'s condition, a
// volatile's and a null address's; the first operand of `&&` and of `||`, in a condition and as a value; a
// conditional's condition, of an int, a char, a pointer and a double, through a member and through a subscript; then
// the forms that read it before the fix too: a second operand, under `!`, a loop's three conditions, a comparison and a
// cast to _Bool. Each of those bodies' entries has a load (check_test_branch_condition_lowered). The last two before
// `tested` have none: an array at an absolute address decays to that address, and `&` of the lvalue is it. `tested`
// calls none of them, as the addresses are no memory of the test's: `never` is zero, and no constant.
struct pair
{
    int first;
    int second;
};

int in_if(void)
{
    if (*(int *)4096)
    {
        return 1;
    }
    return 0;
}

int in_if_volatile(void)
{
    if (*(volatile int *)4096)
    {
        return 1;
    }
    return 0;
}

int in_if_null(void)
{
    if (*(int *)0)
    {
        return 1;
    }
    return 0;
}

int first_of_and(int a)
{
    if (*(int *)4096 && a)
    {
        return 1;
    }
    return 0;
}

int first_of_or(int a)
{
    if (*(int *)4096 || a)
    {
        return 1;
    }
    return 0;
}

int value_of_and(void)
{
    return *(int *)4096 && 1;
}

int value_of_or(void)
{
    return *(int *)4096 || 0;
}

int conditional(void)
{
    return *(int *)4096 ? 3 : 4;
}

int conditional_char(void)
{
    return *(char *)4096 ? 1 : 0;
}

int conditional_pointer(void)
{
    return *(int **)4096 ? 1 : 0;
}

int conditional_double(void)
{
    return *(double *)4096 ? 1 : 0;
}

int through_member(void)
{
    return ((struct pair *)4096)->second ? 1 : 0;
}

int through_subscript(void)
{
    return ((int *)4096)[3] ? 1 : 0;
}

int second_of_and(int a)
{
    if (a && *(int *)4096)
    {
        return 1;
    }
    return 0;
}

int under_not(void)
{
    return !*(int *)4096;
}

int in_while(void)
{
    while (*(int *)4096)
    {
    }
    return 0;
}

int in_do(void)
{
    int n = 0;
    do
    {
        n += 1;
    } while (*(int *)4096);
    return n;
}

int in_for(void)
{
    int n = 0;
    for (; *(int *)4096;)
    {
        n += 1;
    }
    return n;
}

int compared(void)
{
    return *(int *)4096 != 0;
}

int as_bool(void)
{
    return (_Bool)*(int *)4096;
}

int array_there(void)
{
    return *(int (*)[4])4096 ? 1 : 0;
}

int address_there(void)
{
    return &*(int *)4096 ? 1 : 0;
}

static int never;

int tested(int n)
{
    int h = n;
    if (never != 0)
    {
        h += in_if() + in_if_volatile() + in_if_null() + first_of_and(n) + first_of_or(n) + value_of_and() + value_of_or() + conditional() + conditional_char() +
             conditional_pointer() + conditional_double() + through_member() + through_subscript() + second_of_and(n) + under_not() + in_while() + in_do() + in_for() +
             compared() + as_bool() + array_there() + address_there();
    }
    return h;
}
