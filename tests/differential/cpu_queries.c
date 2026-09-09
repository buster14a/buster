void query_four(unsigned leaf, unsigned subleaf, unsigned *out)
{
    unsigned a, b, c, d;
#if defined(__x86_64__) || defined(_M_X64) || defined(BUSTER_CPU_QUERY_TEST)
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(subleaf));
#else
    a = leaf; b = 0; c = subleaf; d = 0;
#endif
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
}
unsigned long long query_tied(unsigned leaf, unsigned subleaf, unsigned salt)
{
    unsigned a, b, c, d;
#if defined(__x86_64__) || defined(_M_X64) || defined(BUSTER_CPU_QUERY_TEST)
    __asm__ volatile("cpuid" : "=c"(c), "=d"(d), "=a"(a), "=b"(b) : "0"(subleaf), "2"(leaf) : "memory", "cc");
#else
    a = leaf; b = 0; c = subleaf; d = 0;
#endif
    return (unsigned long long)a + 3ull*b + 5ull*c + 7ull*d + 11ull*leaf + 13ull*subleaf + 17ull*salt;
}
void query_places(unsigned leaf, unsigned subleaf, unsigned *out)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(BUSTER_CPU_QUERY_TEST)
    __asm__ volatile("cpuid" : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3]) : "a"(leaf), "c"(subleaf));
#else
    out[0] = leaf; out[1] = 0; out[2] = subleaf; out[3] = 0;
#endif
}

#ifdef BUSTER_CPU_QUERY_TEST
unsigned long long query_xcr(unsigned index)
{
    unsigned low, high;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(index));
    return (unsigned long long)low | ((unsigned long long)high << 32);
}
#endif
