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

enum
{
    BOOTSTRAP_VARIADIC_COUNT = 10,
    BOOTSTRAP_VARIADIC_CHECKPOINT = 3,
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

static int bootstrap_variadic(int marker, double named_real, int count, ...)
{
    // Direct builtins keep physical resource-header differences out of the
    // existing exact per-probe token and source-metric comparisons.
    __builtin_va_list arguments;
    __builtin_va_list initial;
    __builtin_va_list checkpoint;
    __builtin_va_start(arguments, count);
    __builtin_va_copy(initial, arguments);

    int valid = marker == 17 && named_real == 1.25 && count == BOOTSTRAP_VARIADIC_COUNT;
    int promoted_integer = __builtin_va_arg(arguments, int);
    double promoted_real = __builtin_va_arg(arguments, double);
    valid = valid && promoted_integer == -7 && promoted_real == 1.5;

    // On SysV x86-64 this checkpoint exhausts the integer register cursor
    // while floating arguments still have register slots. Later pairs exhaust
    // both register files. Check ordered values rather than a commutative sum.
    for (int index = 0; index < BOOTSTRAP_VARIADIC_CHECKPOINT; index += 1)
    {
        BootstrapU64 integer = __builtin_va_arg(arguments, BootstrapU64);
        double real = __builtin_va_arg(arguments, double);
        valid = valid && integer == 0x100000000ull + (BootstrapU64)index && real == (double)index + 0.5;
    }
    __builtin_va_copy(checkpoint, arguments);
    for (int index = BOOTSTRAP_VARIADIC_CHECKPOINT; index < BOOTSTRAP_VARIADIC_COUNT; index += 1)
    {
        BootstrapU64 integer = __builtin_va_arg(arguments, BootstrapU64);
        double real = __builtin_va_arg(arguments, double);
        valid = valid && integer == 0x100000000ull + (BootstrapU64)index && real == (double)index + 0.5;
    }
    __builtin_va_end(arguments);

    // Ending or consuming the original list must not invalidate either copy.
    for (int index = BOOTSTRAP_VARIADIC_CHECKPOINT; index < BOOTSTRAP_VARIADIC_COUNT; index += 1)
    {
        BootstrapU64 integer = __builtin_va_arg(checkpoint, BootstrapU64);
        double real = __builtin_va_arg(checkpoint, double);
        valid = valid && integer == 0x100000000ull + (BootstrapU64)index && real == (double)index + 0.5;
    }
    __builtin_va_end(checkpoint);

    promoted_integer = __builtin_va_arg(initial, int);
    promoted_real = __builtin_va_arg(initial, double);
    valid = valid && promoted_integer == -7 && promoted_real == 1.5;
    for (int index = 0; index < BOOTSTRAP_VARIADIC_COUNT; index += 1)
    {
        BootstrapU64 integer = __builtin_va_arg(initial, BootstrapU64);
        double real = __builtin_va_arg(initial, double);
        valid = valid && integer == 0x100000000ull + (BootstrapU64)index && real == (double)index + 0.5;
    }
    __builtin_va_end(initial);
    return valid;
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
    // Keep the variadic call observable to optimized independent compilers.
    int (*volatile variadic)(int, double, int, ...) = bootstrap_variadic;
    int variadic_valid = variadic(17, 1.25, BOOTSTRAP_VARIADIC_COUNT, (signed char)-7, 1.5f,
                                 0x100000000ull, 0.5, 0x100000001ull, 1.5, 0x100000002ull, 2.5,
                                 0x100000003ull, 3.5, 0x100000004ull, 4.5, 0x100000005ull, 5.5,
                                 0x100000006ull, 6.5, 0x100000007ull, 7.5, 0x100000008ull, 8.5,
                                 0x100000009ull, 9.5);
    int valid = pointer == &a && *pointer == 17 && *bootstrap_join(0, &a, &b) == 23 &&
                pair.integer == 0x100000024ull && pair.real == 2.0 && (high >> 63) == 1 &&
                (unsigned char)0x1ff == 255 && (signed char)0xff == -1 && variadic_valid;
    if (valid)
    {
        puts("self-host bootstrap probe ok");
    }
    return valid ? 0 : 1;
}
