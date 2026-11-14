restrict _Atomic __auto_type first = 1, second = 2, third = 3;

int sum(int x)
{
    const _Atomic restrict __auto_type a = x, b = x + 1, c = x + 2;
    _Static_assert(__builtin_types_compatible_p(__typeof__(a), _Atomic(int)), "atomic type after dropping restrict");
    return a + b + c;
}
