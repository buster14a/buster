// Clang compiles this answer table independently of the Buster bitcode.
extern unsigned bit_counts32(unsigned value);
extern unsigned long long bit_counts64(unsigned long long value);
extern int bit_first32(int value);
extern int bit_first64(long long value);

int main(void)
{
    unsigned inputs32[] = {0, 1, 0x80000000u, 0xaaaaaaaau, 0xffffffffu};
    unsigned expected32[] = {0, 32, 32, 17, 32};
    unsigned long long inputs64[] = {0, 1, 0x8000000000000000ull, 0xaaaaaaaaaaaaaaaaull, 0xffffffffffffffffull};
    unsigned long long expected64[] = {0, 64, 64, 33, 64};
    int failures = 0;
    for (unsigned index = 0; index < 5; index += 1)
    {
        failures += bit_counts32(inputs32[index]) != expected32[index];
        failures += bit_counts64(inputs64[index]) != expected64[index];
    }
    failures += bit_first32(0) != 0;
    failures += bit_first32(1) != 1;
    failures += bit_first32(0x80000000u) != 32;
    failures += bit_first64(0) != 0;
    failures += bit_first64(1) != 1;
    failures += bit_first64(0x8000000000000000ull) != 64;
    return failures;
}
