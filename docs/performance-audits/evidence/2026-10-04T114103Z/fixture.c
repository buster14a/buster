/* Independent C witnesses. These were syntax-checked, NOT run through Buster. */
_Static_assert(sizeof(unsigned) == 4, "witness requires 32-bit unsigned");

unsigned dce_dead_accumulator(unsigned n, unsigned seed)
{
    unsigned dead = seed;
    for (unsigned i = 0; i < n; ++i) dead = dead * 33u + 1u;
    return n;
}

unsigned dce_live_accumulator(unsigned n, unsigned seed)
{
    unsigned value = seed;
    for (unsigned i = 0; i < n; ++i) value = value * 33u + 1u;
    return value;
}

unsigned dce_volatile_control(unsigned n, unsigned seed, volatile unsigned *sink)
{
    unsigned value = seed;
    for (unsigned i = 0; i < n; ++i)
    {
        value = value * 33u + 1u;
        *sink = value;
    }
    return n;
}
