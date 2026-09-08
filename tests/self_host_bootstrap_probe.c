// Behavioral contract checked by every independently generated compiler in
// test_self_host_audit, under all four register-allocation modes. No UB,
// external files, clocks, addresses in output, or target-dependent checksums.
extern int puts(char const* text);
typedef unsigned long long BootstrapU64;
struct BootstrapPair
{
    BootstrapU64 integer;
    double real;
};

static struct BootstrapPair bootstrap_abi(struct BootstrapPair pair, BootstrapU64 a, BootstrapU64 b, BootstrapU64 c,
                                          BootstrapU64 d, BootstrapU64 e, BootstrapU64 f, BootstrapU64 g, BootstrapU64 h)
{
    pair.integer += a + b + c + d + e + f + g + h;
    pair.real += 0.5;
    return pair;
}

static int* bootstrap_join(int condition, int* a, int* b)
{
    int* result;
    if (condition)
    {
        result = a;
    }
    else
    {
        result = b;
    }
    return result;
}

int main(void)
{
    volatile int condition = 1;
    int a = 17;
    int b = 23;
    int* pointer = bootstrap_join(condition, &a, &b);
    struct BootstrapPair pair = {0x100000000ull, 1.5};
    pair = bootstrap_abi(pair, 1, 2, 3, 4, 5, 6, 7, 8);
    BootstrapU64 high = 0x8000000000000000ull;
    int valid = pointer == &a && *pointer == 17 && *bootstrap_join(0, &a, &b) == 23 &&
                pair.integer == 0x100000024ull && pair.real == 2.0 && (high >> 63) == 1 &&
                (unsigned char)0x1ff == 255 && (signed char)0xff == -1;
    if (valid)
    {
        puts("self-host bootstrap probe ok");
    }
    return valid ? 0 : 1;
}
