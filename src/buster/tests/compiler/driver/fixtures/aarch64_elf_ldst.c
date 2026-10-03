// External compiler fixture for direct AArch64 ELF memory relocations.
// The default is the independently confirmed volatile-int import failure.
#if defined(AARCH64_LDST_SCALE_FAMILY)
typedef unsigned char LdstBytes16 __attribute__((vector_size(16)));
volatile unsigned char observed_byte[4113];
volatile unsigned short observed_half[2057];
volatile unsigned int observed_word[1029];
volatile unsigned long long observed_doubleword[515];
volatile LdstBytes16 observed_vector[258];

int main(void)
{
    LdstBytes16 expected = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31};
    observed_byte[4112] = 37;
    observed_half[2056] = 1234;
    observed_word[1028] = 123456;
    observed_doubleword[514] = 0x1122334455667788ULL;
    observed_vector[257] = expected;
    LdstBytes16 actual = observed_vector[257];
    int result = observed_byte[4112] != 37 || observed_half[2056] != 1234 || observed_word[1028] != 123456 ||
                 observed_doubleword[514] != 0x1122334455667788ULL;
    for (int index = 0; index < 16; index += 1)
    {
        result |= actual[index] != expected[index];
    }
    return result;
}
#else
volatile int observed;

int main(void)
{
    observed = 1;
    return observed - 1;
}
#endif
