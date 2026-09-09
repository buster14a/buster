#include <stdio.h>
void query_four(unsigned, unsigned, unsigned *);
void query_places(unsigned, unsigned, unsigned *);
unsigned long long query_tied(unsigned, unsigned, unsigned);

int main(void)
{
    int result = 0;
    unsigned leaves[] = {0, 0x80000000u};
    for (unsigned index = 0; index < 32; index += 1)
    {
        unsigned leaf = leaves[index & 1];
        unsigned subleaf = index;
        unsigned expected[4] = {leaf, 0, subleaf, 0};
#if defined(__x86_64__) || defined(_M_X64)
        __asm__ volatile("cpuid" : "=a"(expected[0]), "=b"(expected[1]), "=c"(expected[2]), "=d"(expected[3]) : "a"(leaf), "c"(subleaf));
        // Pin a caller value to the callee-saved register that CPUID writes.
        // Empty assembly on each side keeps the host compiler from replacing
        // the post-call observation with the known initializer.
        register unsigned long long saved __asm__("rbx") = 0x13579bdf2468ace0ull + index;
        __asm__ volatile("" : "+r"(saved));
#endif
        unsigned actual[4] = {0};
        query_four(leaf, subleaf, actual);
#if defined(__x86_64__) || defined(_M_X64)
        __asm__ volatile("" : "+r"(saved));
        result |= saved != 0x13579bdf2468ace0ull + index;
#endif
        for (unsigned part = 0; part < 4; part += 1) { result |= actual[part] != expected[part]; }
        query_places(leaf, subleaf, actual);
        for (unsigned part = 0; part < 4; part += 1) { result |= actual[part] != expected[part]; }
        unsigned long long value = query_tied(leaf, subleaf, index);
        unsigned long long wanted = (unsigned long long)expected[0] + 3ull*expected[1] + 5ull*expected[2] + 7ull*expected[3] +
                                    11ull*leaf + 13ull*subleaf + 17ull*index;
        result |= value != wanted;
        printf("%u %u %u %u %llu\n", actual[0], actual[1], actual[2], actual[3], value);
    }
    return result;
}
