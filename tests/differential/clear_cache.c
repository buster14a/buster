void clear_range(char *begin, char *end)
{
    __builtin___clear_cache(begin, end);
}
int clear_arguments(char *p, int n, int *first, int *second)
{
    __builtin___clear_cache((++*first, p), (++*second, p + n));
    return *first + 7 * *second;
}

unsigned long long clear_live(char *p, unsigned long long a, unsigned long long b, unsigned long long c,
    unsigned long long d, unsigned long long e, unsigned long long f, unsigned long long g)
{
    a = a * 3 + 1;
    b = b * 5 + 2;
    c = c * 7 + 3;
    d = d * 11 + 4;
    e = e * 13 + 5;
    f = f * 17 + 6;
    g = g * 19 + 7;
    __builtin___clear_cache(p + 3, p + 65);
    return a + b + c + d + e + f + g;
}
