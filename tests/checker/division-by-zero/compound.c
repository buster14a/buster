static const int zero = 0;

int update(int a, unsigned u, long l, double d, int variable)
{
    a /= 0;
    a %= 0;
    a /= (int)1e-4900L;
    a /= (int)0.5;
    a /= 1 - 1;
    a = a / 0;
    a = a % 0;
    a = a / (int)1e-4900L;
    u /= 0;
    l %= 0L;
    a %= zero;
    a /= 0.0;
    d /= 0;
    a /= variable;
    a %= 2;
    if (0)
    {
        a /= 0;
    }
    (void)(0 && (a %= 0));
    _Static_assert(sizeof(a /= 0) == sizeof(a), "int size");
    return a;
}
