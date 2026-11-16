// Conditions written as branches (#2883): each function before `joined` has `&&` or `||` only in a statement's or a
// conditional's condition, under `!`s too, and must have no JUMP_VALUE but its conditionals' joins of their arms and no
// value tested again where a jump arrives (check_test_branch_condition_lowered); the functions from `joined` on use them
// as values, which keep their joins.
int both(int a, int b, int c)
{
    if (a < b && b < c)
    {
        return 1;
    }
    return 2;
}

int either(int a, int b)
{
    if (a || b)
    {
        return 1;
    }
    return 2;
}

int neither(int a, int b)
{
    if (!(a || b))
    {
        return 1;
    }
    return 2;
}

int deep(int a, int b, int c, int d)
{
    if ((a && (b || !c)) || (!d && a != b))
    {
        return 1;
    }
    return 2;
}

int deeper(int a, int b, int c, int d)
{
    if (!(!(a > 0 && (b > 0 || (c > 0 && !(d > 0)))) || (a < -5 && !b)))
    {
        return 3;
    }
    return 4;
}

int loop_while(int a, int b)
{
    int n = 0;
    while (a < b && n < 100)
    {
        a += 2;
        n += 1;
    }
    return n;
}

int loop_for(int a, int b)
{
    int n = 0;
    for (; !(a >= b || n >= 100); a += 3)
    {
        n += 1;
    }
    return n;
}

int loop_do(int a, int b)
{
    int n = 0;
    do
    {
        a += 1;
        n += 1;
    } while ((a < b && n < 100) || (n & 1) != 0);
    return n;
}

int pick(int a, int b, int c)
{
    return (a < b || !c) ? 5 : 7;
}

int pick_nested(int a, int b, int c)
{
    return !(a && (b || c)) ? a : (b && !c ? b : c);
}

int joined(int a, int b)
{
    return a && b;
}

int joined_or(int a, int b)
{
    int x = a || b;
    return x + 1;
}

int joined_chain(int a, int b, int c)
{
    return (a && b) || c;
}

int tested(int n)
{
    int h = 0;
    for (int k = 0; k < 16; k += 1)
    {
        const int a = (n >> (k & 3)) - 2;
        const int b = (k & 1) != 0 ? n - k : k - n;
        const int c = (k & 2) != 0 ? 0 : n + k;
        const int d = (k & 4) != 0 ? n * k : 0;
        h = h * 31 + both(a, b, c);
        h = h * 31 + either(a & 1, b & 2);
        h = h * 31 + neither(a & 1, c & 1);
        h = h * 31 + deep(a & 1, b & 1, c & 1, d & 1);
        h = h * 31 + deeper(a, b, c, d);
        h = h * 31 + loop_while(a, b);
        h = h * 31 + loop_for(a, b);
        h = h * 31 + loop_do(a, b);
        h = h * 31 + pick(a, b, c);
        h = h * 31 + pick_nested(a & 1, b & 1, c & 1);
        h = h * 31 + joined(a & 1, b & 1) + joined_or(a & 2, c & 1) + joined_chain(a & 1, b & 2, d & 1);
    }
    return h & 0x7fffffff;
}
