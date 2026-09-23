// Compiled by Buster into bitcode; the independent expected answers are in
// basic_c_llvm_bit_counts_main.c.
unsigned bit_counts32(unsigned value)
{
    unsigned count = (unsigned)__builtin_popcount(value);
    if (value != 0)
    {
        count += (unsigned)__builtin_clz(value);
        count += (unsigned)__builtin_ctz(value);
    }
    return count;
}

unsigned long long bit_counts64(unsigned long long value)
{
    unsigned long long count = (unsigned)__builtin_popcountll(value);
    if (value != 0)
    {
        count += (unsigned)__builtin_clzll(value);
        count += (unsigned)__builtin_ctzll(value);
    }
    return count;
}

int bit_first32(int value)
{
    return __builtin_ffs(value);
}

int bit_first64(long long value)
{
    return __builtin_ffsll(value);
}
