/* Direct frontend SSA and the independent memory/shared-promotion path must
   agree under every allocator. Exercise scalar joins, expression temporaries,
   loops, irreducible control flow and precisely owned memory fallbacks. */
int ssa_diamond(int x, int condition)
{
    if (condition) x += 3;
    else x -= 2;
    return x;
}

int ssa_sum(int n)
{
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += i;
    return sum;
}

int ssa_rotate(int a, int b, int c, int n)
{
    while (n-- > 0)
    {
        int temporary = a;
        a = b;
        b = c;
        c = temporary;
    }
    return a * 100 + b * 10 + c;
}

int ssa_lvalues(int x)
{
    (x) = 4;
    ((x)) += 2;
    ++(x);
    (x)++;
    return x;
}

int ssa_nested(int n)
{
    int sum = 0;
    for (int i = 0; i < n; ++i)
    {
        if (i == 2) continue;
        for (int j = 0; j < 4; ++j)
        {
            if (j == 3) break;
            sum += i + j;
        }
    }
    return sum;
}

int ssa_chain(int n)
{
    int x = 0, y = 0;
    x = y = n + 1;
    return x * 10 + y;
}

int ssa_do(int n)
{
    int value = 0;
    do { value += n; --n; } while (n > 0);
    return value;
}

int ssa_or(int x, int y)
{
    int result = x++ || y++;
    return result * 100 + x * 10 + y;
}

double ssa_float(double x, int condition)
{
    double y = 2.5;
    if (condition) y = x;
    return y;
}

int* ssa_pointer(int* a, int* b, int condition)
{
    int* p = a;
    if (condition) p = b;
    return p;
}

int ssa_narrow(signed char x)
{
    x += 1;
    return x;
}

int ssa_volatile(int x)
{
    volatile int y = x;
    y += 2;
    return y;
}

int ssa_escape(int x)
{
    int* p = &x;
    *p += 7;
    return x;
}

int ssa_dead(int x)
{
    return x;
    x = 99;
}

/* Taking an address late must restore every earlier write, not only the last
   block's value. Calls may change that owner, but not unrelated SSA locals. */
static void ssa_mutate(int* p)
{
    *p = *p * 3 + 1;
}

int ssa_late_escape(int n)
{
    int x = 1;
    int y = 5;
    for (int i = 0; i < n; ++i)
    {
        x += i;
        y += x;
    }
    ssa_mutate(&x);
    y += x;
    x += y;
    return x * 7 + y;
}

int ssa_switch(int n)
{
    int x = 3;
    for (int i = 0; i < n; ++i)
    {
        switch (i & 3)
        {
            case 0: x += 2; break;
            case 1: x *= 2;
            case 2: x -= 1; break;
            default: x += i;
        }
    }
    return x;
}

int ssa_irreducible(int n, int c)
{
    int x = 1;
    if (c)
    {
        goto second;
    }
first:
    x += n;
    if (--n > 0)
    {
        goto second;
    }
    goto done;
second:
    x += n * 2;
    if (--n > 0)
    {
        goto first;
    }
done:
    return x;
}

int ssa_initialized_join(int c)
{
    int x;
    if (c)
    {
        x = 7;
    }
    else
    {
        x = 19;
    }
    return x;
}

int ssa_scalar_temporaries(int n)
{
    int x = 3;
    int y = 5;
    for (int i = 0; i < n; ++i)
    {
        x = (i & 1) ? x + y : y - x;
        int step = (x && ++x) || ++y;
        y += step;
    }
    return x * 7 + y;
}

int ssa_array_parameter(int values[4], int n)
{
    int sum = 0;
    for (int i = 0; i < n; ++i)
    {
        sum += values[i & 3];
    }
    return sum;
}

int ssa_bitfields(int n)
{
    struct Bits { unsigned int a : 5; unsigned int b : 5; } bits = {3, 7};
    int result = 0;
    for (int i = 0; i < n; ++i)
    {
        bits.a = (i & 1) ? bits.b : bits.a + 1;
        bits.b += i;
        result += bits.a + bits.b;
    }
    return result;
}

int ssa_dynamic_fallback(int n)
{
    int values[n];
    int sum = 0;
    for (int i = 0; i < n; ++i)
    {
        values[i] = i + 1;
        sum += values[i];
    }
    return sum;
}

int ssa_entry_definition(int n, int condition)
{
    int seed = n + 5;
    int sum = 0;
    while (n-- > 0)
    {
        if (condition) sum += seed;
        else sum -= seed;
    }
    return sum;
}

int ssa_entry_reassigned(int n)
{
    int value = 7;
    int sum = 0;
    while (n-- > 0)
    {
        sum += value;
        value += 2;
    }
    return sum;
}

int ssa_entry_escaped(int n)
{
    int value = n + 5;
    int sum = 0;
    for (int i = 0; i < 3; ++i) sum += value;
    ssa_mutate(&value);
    return sum + value;
}

int ssa_entry_irreducible(int n, int condition)
{
    int seed = n + 3;
    int sum = 0;
    if (condition) goto second;
first:
    sum += seed;
    if (--n > 0) goto second;
    goto done;
second:
    sum += seed * 2;
    if (--n > 0) goto first;
done:
    return sum;
}

int main(void)
{
    int a = 7, b = 9;
    int values[4] = {2, 3, 5, 7};
    int result = 0;
    if (ssa_diamond(5, 1) != 8 || ssa_diamond(5, 0) != 3) result = 1;
    else if (ssa_sum(100) != 4950 || ssa_sum(-1) != 0) result = 2;
    else if (ssa_rotate(1, 2, 3, 5) != 312 || ssa_rotate(1, 2, 3, 0) != 123) result = 3;
    else if (ssa_lvalues(99) != 8) result = 4;
    else if (ssa_nested(5) != 36) result = 5;
    else if (ssa_chain(6) != 77) result = 6;
    else if (ssa_do(5) != 15) result = 7;
    else if (ssa_or(0, 1) != 112 || ssa_or(1, 1) != 121) result = 8;
    else if (ssa_float(7.25, 0) != 2.5 || ssa_float(7.25, 1) != 7.25) result = 9;
    else if (ssa_pointer(&a, &b, 0) != &a || ssa_pointer(&a, &b, 1) != &b) result = 10;
    else if (ssa_narrow(127) != -128 || ssa_volatile(4) != 6) result = 11;
    else if (ssa_escape(4) != 11 || ssa_dead(4) != 4) result = 12;
    else if (ssa_late_escape(7) != 1549) result = 13;
    else if (ssa_switch(9) != 33) result = 14;
    else if (ssa_irreducible(9, 0) != 66 || ssa_irreducible(9, 1) != 71) result = 15;
    else if (ssa_initialized_join(0) != 19 || ssa_initialized_join(1) != 7) result = 16;
    else if (ssa_scalar_temporaries(8) != 90) result = 17;
    else if (ssa_array_parameter(values, 11) != 44) result = 18;
    else if (ssa_bitfields(7) != 180) result = 19;
    else if (ssa_dynamic_fallback(9) != 45) result = 20;
    else if (ssa_entry_definition(5, 1) != 50 || ssa_entry_definition(5, 0) != -50) result = 21;
    else if (ssa_entry_reassigned(4) != 40 || ssa_entry_reassigned(5) != 55) result = 22;
    else if (ssa_entry_escaped(4) != 55 || ssa_entry_escaped(0) != 31) result = 23;
    else if (ssa_entry_irreducible(5, 0) != 56 || ssa_entry_irreducible(5, 1) != 64) result = 24;
    return result;
}
