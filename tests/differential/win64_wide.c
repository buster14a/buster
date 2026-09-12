// Win64 i128 arguments occupy one pointer slot each; results use XMM0.
// Keep callees independent of their callers so mixed host/MIR tests validate
// both halves of that contract, including indirect calls and va_list cursors.
typedef unsigned __int128 Win64Wide;
struct Win64WideBig { unsigned long long words[5]; };

Win64Wide win64_wide_mix(volatile Win64Wide a, double b, Win64Wide c, unsigned long long d, Win64Wide e, Win64Wide f)
{
    Win64Wide result = a + 2 * c + 3 * e + 4 * f + (unsigned long long)b + d;
    a = 0;
    return result;
}

Win64Wide win64_wide_dispatch(Win64Wide (*function)(Win64Wide, double, Win64Wide, unsigned long long, Win64Wide, Win64Wide), Win64Wide value)
{
    Win64Wide result = function(value, 9.0, value + 1, 13, value + 2, value + 3);
    return result + value;
}

Win64Wide win64_wide_variadic(int marker, ...)
{
    __builtin_va_list cursor;
    __builtin_va_list copy;
    __builtin_va_start(cursor, marker);
    __builtin_va_copy(copy, cursor);
    Win64Wide a = __builtin_va_arg(cursor, Win64Wide);
    struct Win64WideBig b = __builtin_va_arg(cursor, struct Win64WideBig);
    Win64Wide c = __builtin_va_arg(cursor, Win64Wide);
    struct Win64WideBig d = __builtin_va_arg(cursor, struct Win64WideBig);
    Win64Wide e = __builtin_va_arg(cursor, Win64Wide);
    unsigned long long tail = __builtin_va_arg(cursor, unsigned long long);
    Win64Wide duplicate = __builtin_va_arg(copy, Win64Wide);
    __builtin_va_end(copy);
    __builtin_va_end(cursor);
    Win64Wide result = a + 2 * c + 3 * e + duplicate + (unsigned long long)marker + tail;
    for (int index = 0; index < 5; index += 1) { result += (index + 1) * b.words[index] + (index + 6) * d.words[index]; }
    return result;
}

Win64Wide win64_wide_variadic_dispatch(Win64Wide (*function)(int, ...), Win64Wide value)
{
    struct Win64WideBig a = {{1, 2, 3, 4, 5}};
    struct Win64WideBig b = {{6, 7, 8, 9, 10}};
    Win64Wide result = function(7, value, a, value + 1, b, value + 2, 11ull);
    return result + value + a.words[0] + b.words[4];
}
