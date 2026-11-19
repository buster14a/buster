unsigned long common_alloc_flags(const unsigned long *flags, unsigned n, unsigned long s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        unsigned long x = flags[i];
        s = (x & 2ul) ? (s & x) : s;
    }
    return s;
}

unsigned long common_nonalloc_flags(const unsigned long *flags, unsigned n, unsigned long s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        unsigned long x = flags[i];
        s = (x & 2ul) ? s : (x & s);
    }
    return s;
}

unsigned common32(const unsigned *flags, unsigned n, unsigned s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        unsigned x = flags[i];
        if (x & 2u) s &= x;
    }
    return s;
}

unsigned shared32(const unsigned *flags, unsigned n, unsigned s, unsigned t)
{
    for (unsigned i = 0; i < n; ++i)
    {
        unsigned x = flags[i];
        if (x & 2u)
        {
            s &= x;
            t ^= s;
        }
    }
    return s ^ t;
}

unsigned volatile32(const volatile unsigned *flags, unsigned n, unsigned s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        if (flags[i] & 2u) s &= flags[i];
    }
    return s;
}

unsigned char common8(const unsigned char *flags, unsigned n, unsigned char s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        unsigned char x = flags[i];
        if (x & 2u) s = (unsigned char)(s & x);
    }
    return s;
}

unsigned short common16(const unsigned short *flags, unsigned n, unsigned short s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        unsigned short x = flags[i];
        if (x & 2u) s = (unsigned short)(s & x);
    }
    return s;
}

unsigned guarded(const unsigned *active, const volatile unsigned *masks, unsigned n, unsigned s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        if (active[i]) s &= masks[i];
    }
    return s;
}

unsigned quotient(const unsigned *divisors, unsigned n, unsigned s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        unsigned d = divisors[i];
        if (d) s &= 0x7fffffffU / d;
    }
    return s;
}

unsigned float_condition(const double *p, unsigned n, unsigned s, unsigned x)
{
    for (unsigned i = 0; i < n; ++i)
    {
        if (p[i] == 2.0) s &= x;
    }
    return s;
}

unsigned plain_reload(const unsigned *p, unsigned n, unsigned s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        if (p[i] & 2u) s &= p[i];
    }
    return s;
}
