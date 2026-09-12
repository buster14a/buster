// Win64 passes each aggregate through one pointer slot. The caller owns an
// aligned private copy even when the pointee requires more than sixteen bytes.
struct Win64Aligned32 { _Alignas(32) unsigned long long words[4]; };
struct Win64Aligned64 { _Alignas(64) unsigned long long words[8]; };
struct Win64Aligned128 { _Alignas(128) unsigned long long words[16]; };

unsigned long long win64_aligned_mix(volatile struct Win64Aligned32 a, double b, volatile struct Win64Aligned64 c, unsigned long long d, volatile struct Win64Aligned128 e, unsigned long long tail)
{
    // Volatile integer snapshots prevent the host oracle from replacing these
    // observations with the alignment guaranteed by each parameter's type.
    volatile unsigned long long a_address = (unsigned long long)&a;
    volatile unsigned long long c_address = (unsigned long long)&c;
    volatile unsigned long long e_address = (unsigned long long)&e;
    unsigned long long invalid_alignment = (a_address & 31) != 0 || (c_address & 63) != 0 || (e_address & 127) != 0;
    unsigned long long result = (unsigned long long)b + d + tail;
    for (int index = 0; index < 4; index += 1) { result += (index + 1) * a.words[index]; }
    for (int index = 0; index < 8; index += 1) { result += (index + 5) * c.words[index]; }
    for (int index = 0; index < 16; index += 1) { result += (index + 13) * e.words[index]; }
    a.words[0] = 0;
    c.words[7] = 0;
    e.words[15] = 0;
    return result | (invalid_alignment << 63);
}

unsigned long long win64_aligned_dispatch(unsigned long long (*function)(struct Win64Aligned32, double, struct Win64Aligned64, unsigned long long, struct Win64Aligned128, unsigned long long), unsigned long long seed)
{
    struct Win64Aligned32 a;
    struct Win64Aligned64 b;
    struct Win64Aligned128 c;
    for (int index = 0; index < 4; index += 1) { a.words[index] = seed + index + 1; }
    for (int index = 0; index < 8; index += 1) { b.words[index] = seed + index + 5; }
    for (int index = 0; index < 16; index += 1) { c.words[index] = seed + index + 13; }
    unsigned long long allocation_size = (seed & 127) + 33;
    volatile unsigned char* live = __builtin_alloca(allocation_size);
    live[0] = 37;
    live[allocation_size - 1] = 19;
    unsigned long long result = function(a, 9.0, b, 13, c, 17);
    return result + a.words[0] + b.words[7] + c.words[15] + live[0] + live[allocation_size - 1];
}

#if !defined(WIN64_ALIGNED_FIXED_ONLY)
unsigned long long win64_aligned_variadic(int marker, ...)
{
    __builtin_va_list cursor;
    __builtin_va_list copy;
    __builtin_va_start(cursor, marker);
    __builtin_va_copy(copy, cursor);
    volatile struct Win64Aligned32 a = __builtin_va_arg(cursor, struct Win64Aligned32);
    double b = __builtin_va_arg(cursor, double);
    volatile struct Win64Aligned64 c = __builtin_va_arg(cursor, struct Win64Aligned64);
    unsigned long long d = __builtin_va_arg(cursor, unsigned long long);
    volatile struct Win64Aligned128 e = __builtin_va_arg(cursor, struct Win64Aligned128);
    unsigned long long tail = __builtin_va_arg(cursor, unsigned long long);
    struct Win64Aligned32 duplicate = __builtin_va_arg(copy, struct Win64Aligned32);
    __builtin_va_end(copy);
    __builtin_va_end(cursor);
    unsigned long long result = (unsigned long long)marker + (unsigned long long)b + d + tail + duplicate.words[0] + duplicate.words[3];
    for (int index = 0; index < 4; index += 1) { result += (index + 1) * a.words[index]; }
    for (int index = 0; index < 8; index += 1) { result += (index + 5) * c.words[index]; }
    for (int index = 0; index < 16; index += 1) { result += (index + 13) * e.words[index]; }
    a.words[0] = 0;
    c.words[7] = 0;
    e.words[15] = 0;
    return result;
}

unsigned long long win64_aligned_variadic_dispatch(unsigned long long (*function)(int, ...), unsigned long long seed)
{
    // Keep this caller free of alloca so the same alignment contract is also
    // checked in the fixed outgoing frame area, independently of the VLA path.
    struct Win64Aligned32 a;
    struct Win64Aligned64 b;
    struct Win64Aligned128 c;
    for (int index = 0; index < 4; index += 1) { a.words[index] = seed + index + 1; }
    for (int index = 0; index < 8; index += 1) { b.words[index] = seed + index + 5; }
    for (int index = 0; index < 16; index += 1) { c.words[index] = seed + index + 13; }
    unsigned long long result = function(7, a, 9.0, b, 13ull, c, 17ull);
    return result + a.words[0] + b.words[7] + c.words[15];
}
#endif
