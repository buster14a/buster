// C11 6.7.6.3p8: a parameter declared with function type adjusts to a
// pointer to that function, in both spellings.  CPython's Parser/pegen.c is
// the parenthesized shape -- its LOOKAHEAD helpers take the grammar rule as
// `RES_TYPE (func)(Parser *)` and call `func(p)` -- which parsed to a
// parameter entity of bare function type that every call through was
// refused; the unparenthesized spelling never recorded the parameter at all.
static int add_one(int x)
{
    return x + 1;
}

int call_parenthesized(int (func)(int), int x)
{
    return func(x);
}

int call_plain(int func(int), int x)
{
    return func(x);
}

int main(void)
{
    if (call_parenthesized(add_one, 1) != 2)
    {
        return 1;
    }
    if (call_plain(add_one, 2) != 3)
    {
        return 2;
    }
    return 0;
}
