int g(void);
const char *h(void) {
    const int c = 1;
    int a[3] = { 0 };
    volatile short v = 2;
    _Atomic int t = 3;
    __auto_type x = c;
    __auto_type p = a;
    __auto_type q = g;
    __auto_type s = "abc";
    __auto_type w = v;
    __auto_type z = t;
    const __auto_type k = 2.0f;
    __auto_type volatile u = (char)1 + (char)2;
    _Static_assert(__builtin_types_compatible_p(__typeof__(x), int), "lvalue conversion");
    _Static_assert(__builtin_types_compatible_p(__typeof__(p), int *), "an array's decay");
    _Static_assert(__builtin_types_compatible_p(__typeof__(q), int (*)(void)), "a function's decay");
    _Static_assert(__builtin_types_compatible_p(__typeof__(s), char *), "a string's decay");
    _Static_assert(__builtin_types_compatible_p(__typeof__(w), short), "volatile dropped");
    _Static_assert(__builtin_types_compatible_p(__typeof__(z), _Atomic int), "_Atomic kept");
    _Static_assert(__builtin_types_compatible_p(__typeof__(k), const float), "the specifiers' const");
    _Static_assert(__builtin_types_compatible_p(__typeof__(u), volatile int), "promoted, and volatile");
    x = 3;
    p[0] = x + q() + w + z + (int)k + u;
    return s;
}
