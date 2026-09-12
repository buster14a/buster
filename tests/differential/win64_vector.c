// AVX-512 Win64 passes each vector through one caller-owned pointer and
// returns its complete sixty-four-byte value in ZMM0.
typedef unsigned long long Win64Vector __attribute__((vector_size(64)));

Win64Vector win64_vector_mix(volatile Win64Vector a, double b, volatile Win64Vector c, unsigned long long d, volatile Win64Vector e, unsigned long long tail)
{
    Win64Vector lane = {1, 2, 3, 4, 5, 6, 7, 8};
    Win64Vector error = {0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull};
    if (b != 9.0 || d != 13 || tail != 17) { a = a ^ error; }
    Win64Vector result = a + c + c + e + e + e + lane;
    a = (Win64Vector){0, 0, 0, 0, 0, 0, 0, 0};
    c = (Win64Vector){0, 0, 0, 0, 0, 0, 0, 0};
    e = (Win64Vector){0, 0, 0, 0, 0, 0, 0, 0};
    return result;
}

Win64Vector win64_vector_dispatch(Win64Vector (*function)(Win64Vector, double, Win64Vector, unsigned long long, Win64Vector, unsigned long long), Win64Vector value)
{
    // No alloca: exercise the fixed outgoing copy area independently of the
    // dynamic outgoing area used by the variadic dispatcher below.
    Win64Vector lane = {1, 2, 3, 4, 5, 6, 7, 8};
    Win64Vector second = value + lane;
    Win64Vector third = value + lane + lane;
    Win64Vector result = function(value, 9.0, second, 13, third, 17);
    return result + value + second + third;
}

#if !defined(WIN64_VECTOR_FIXED_ONLY)
Win64Vector win64_vector_variadic(Win64Vector named, int marker, ...)
{
    __builtin_va_list cursor;
    __builtin_va_list copy;
    __builtin_va_start(cursor, marker);
    __builtin_va_copy(copy, cursor);
    volatile Win64Vector a = __builtin_va_arg(cursor, Win64Vector);
    double b = __builtin_va_arg(cursor, double);
    volatile Win64Vector c = __builtin_va_arg(cursor, Win64Vector);
    unsigned long long d = __builtin_va_arg(cursor, unsigned long long);
    volatile Win64Vector e = __builtin_va_arg(cursor, Win64Vector);
    Win64Vector duplicate = __builtin_va_arg(copy, Win64Vector);
    __builtin_va_end(copy);
    __builtin_va_end(cursor);
    Win64Vector lane = {1, 2, 3, 4, 5, 6, 7, 8};
    Win64Vector error = {0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull};
    if (marker != 7 || b != 9.0 || d != 13) { a = a ^ error; }
    Win64Vector result = a + c + c + e + e + e + duplicate + named + lane;
    a = (Win64Vector){0, 0, 0, 0, 0, 0, 0, 0};
    c = (Win64Vector){0, 0, 0, 0, 0, 0, 0, 0};
    e = (Win64Vector){0, 0, 0, 0, 0, 0, 0, 0};
    return result;
}

Win64Vector win64_vector_variadic_dispatch(Win64Vector (*function)(Win64Vector, int, ...), Win64Vector value, unsigned long long seed)
{
    Win64Vector lane = {1, 2, 3, 4, 5, 6, 7, 8};
    Win64Vector second = value + lane;
    Win64Vector third = value + lane + lane;
    unsigned long long allocation_size = (seed & 127) + 33;
    volatile unsigned char* live = __builtin_alloca(allocation_size);
    live[0] = 37;
    live[allocation_size - 1] = 19;
    volatile Win64Vector result = function(value, 7, value, 9.0, second, 13ull, third);
    Win64Vector error = {0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull};
    if (live[0] != 37 || live[allocation_size - 1] != 19) { result = result ^ error; }
    return result + value + second + third + (Win64Vector){56, 56, 56, 56, 56, 56, 56, 56};
}
#endif
