// Windows ARM64 variadic arguments occupy an imaginary stack whose first
// eight slots travel in X0-X7. An eight-aligned pair can straddle X7 and SP;
// a bare __int128 instead rounds its address to sixteen-byte alignment.
// Clang 18 disagrees with itself at these boundaries, so the optional assembly
// companion is an independent producer/consumer oracle for the ABI layout.
#ifndef BUSTER_PLATFORM_VA_ABI_PROBE
#define BUSTER_PLATFORM_VA_ABI_PROBE 0
#endif

typedef __builtin_va_list WindowsBoundaryVaList;
struct WindowsBoundaryPair { long long low; long long high; };

int windows_va_split(int count, ...)
{
    WindowsBoundaryVaList arguments;
    __builtin_va_start(arguments, count);
    int good = count == 6;
    for (int index = 0; index < count; index += 1)
    {
        good &= __builtin_va_arg(arguments, long long) == 11 * (index + 1ll);
    }
    struct WindowsBoundaryPair pair = __builtin_va_arg(arguments, struct WindowsBoundaryPair);
    long long tail = __builtin_va_arg(arguments, long long);
    __builtin_va_end(arguments);
    return good && pair.low == 71 && pair.high == 83 && tail == 97;
}

int windows_va_i128(int count, ...)
{
    WindowsBoundaryVaList arguments;
    __builtin_va_start(arguments, count);
    int good = count == 0 || count == 6 || count == 8;
    for (int index = 0; index < count; index += 1)
    {
        good &= __builtin_va_arg(arguments, long long) == 11 * (index + 1ll);
    }
    __int128 value = __builtin_va_arg(arguments, __int128);
    long long tail = __builtin_va_arg(arguments, long long);
    __builtin_va_end(arguments);
    return good && (long long)value == 71 && (long long)(value >> 64) == 83 && tail == 97;
}

int windows_va_named_pair(long long a, long long b, long long c, long long d,
    long long e, long long f, long long g, struct WindowsBoundaryPair pair, int marker, ...)
{
    WindowsBoundaryVaList arguments;
    __builtin_va_start(arguments, marker);
    long long integer = __builtin_va_arg(arguments, long long);
    double real = __builtin_va_arg(arguments, double);
    __builtin_va_end(arguments);
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7
        && pair.low == 71 && pair.high == 83 && marker == 11 && integer == 97 && real == 2.5;
}

#if BUSTER_PLATFORM_VA_ABI_PROBE
int windows_va_asm_split(int count, ...);
int windows_va_asm_i128_register(int count, ...);
int windows_va_asm_i128_overflow(int count, ...);
int windows_va_asm_i128_stack(int count, ...);
int windows_va_asm_named_pair(long long a, long long b, long long c, long long d,
    long long e, long long f, long long g, struct WindowsBoundaryPair pair, int marker, ...);
int windows_va_probe_all(void);
#endif

int main(void)
{
    struct WindowsBoundaryPair pair;
    pair.low = 71;
    pair.high = 83;
    __int128 value = ((__int128)83 << 64) | 71;
    int bad = windows_va_split(6, 11ll, 22ll, 33ll, 44ll, 55ll, 66ll, pair, 97ll) != 1;
    bad |= windows_va_i128(0, value, 97ll) != 1;
    bad |= windows_va_i128(6, 11ll, 22ll, 33ll, 44ll, 55ll, 66ll, value, 97ll) != 1;
    bad |= windows_va_i128(8, 11ll, 22ll, 33ll, 44ll, 55ll, 66ll, 77ll, 88ll, value, 97ll) != 1;
    bad |= windows_va_named_pair(1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, pair, 11, 97ll, 2.5) != 1;
#if BUSTER_PLATFORM_VA_ABI_PROBE
    bad |= windows_va_asm_split(6, 11ll, 22ll, 33ll, 44ll, 55ll, 66ll, pair, 97ll) != 1;
    bad |= windows_va_asm_i128_register(0, value, 97ll) != 1;
    bad |= windows_va_asm_i128_overflow(6, 11ll, 22ll, 33ll, 44ll, 55ll, 66ll, value, 97ll) != 1;
    bad |= windows_va_asm_i128_stack(8, 11ll, 22ll, 33ll, 44ll, 55ll, 66ll, 77ll, 88ll, value, 97ll) != 1;
    bad |= windows_va_asm_named_pair(1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, pair, 11, 97ll, 2.5) != 1;
    bad |= windows_va_probe_all();
#endif
    return bad;
}
