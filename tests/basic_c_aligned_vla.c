// Rounding a size to the declaration's alignment does not align the address.
// Two invocations separated by live alloca storage cannot both pass merely
// because the incoming stack happened to have the requested alignment.
static volatile unsigned long long runtime_sizes[] = {17, 4095, 4096, 4097, 8193};

#define DEFINE_ALIGNED_VLA_CHECK(alignment) \
    __attribute__((noinline)) int check_vla_##alignment(unsigned long long count) \
    { \
        _Alignas(alignment) volatile unsigned char first[count]; \
        first[0] = 17; \
        first[count - 1] = 23; \
        int failures = ((unsigned long long)first & (alignment - 1)) != 0; \
        { \
            _Alignas(alignment) volatile unsigned char second[count + 13]; \
            second[0] = 29; \
            second[count + 12] = 31; \
            failures += ((unsigned long long)second & (alignment - 1)) != 0; \
            failures += second[0] != 29 || second[count + 12] != 31; \
            failures += first[0] != 17 || first[count - 1] != 23; \
        } \
        failures += first[0] != 17 || first[count - 1] != 23; \
        return failures; \
    }

DEFINE_ALIGNED_VLA_CHECK(32)
DEFINE_ALIGNED_VLA_CHECK(64)
DEFINE_ALIGNED_VLA_CHECK(256)
DEFINE_ALIGNED_VLA_CHECK(4096)

int main(void)
{
    int failures = 0;
    for (unsigned int index = 0; index < sizeof(runtime_sizes) / sizeof(runtime_sizes[0]); index += 1)
    {
        unsigned long long count = runtime_sizes[index];
        failures += check_vla_32(count);
        failures += check_vla_64(count);
        failures += check_vla_256(count);
        failures += check_vla_4096(count);
        volatile unsigned char* padding = __builtin_alloca(1);
        *padding = 37;
        failures += check_vla_32(count);
        failures += check_vla_64(count);
        failures += check_vla_256(count);
        failures += check_vla_4096(count);
        failures += *padding != 37;
    }
    return failures != 0;
}
