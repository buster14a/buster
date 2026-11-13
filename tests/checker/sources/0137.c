int g(void);
const char *h(void) {
    const int c = 1;
    int a[3] = { 0 };
    auto x = c;
    auto p = a;
    auto q = g;
    auto s = "abc";
    auto *t = &a[0];
    auto **u = &t;
    auto v = 1, w = 2;
    auto *i = &a[0], *j = &a[1];
    static auto k = 3;
    _Static_assert(__builtin_types_compatible_p(__typeof__(x), int), "lvalue conversion");
    _Static_assert(__builtin_types_compatible_p(__typeof__(p), int *), "array decay");
    _Static_assert(__builtin_types_compatible_p(__typeof__(q), int (*)(void)), "function decay");
    _Static_assert(__builtin_types_compatible_p(__typeof__(s), char *), "string decay");
    _Static_assert(__builtin_types_compatible_p(__typeof__(t), int *), "pointer declarator");
    _Static_assert(__builtin_types_compatible_p(__typeof__(u), int **), "two pointers");
    _Static_assert(__builtin_types_compatible_p(__typeof__(v), int), "a later declarator");
    _Static_assert(__builtin_types_compatible_p(__typeof__(i), int *), "each declarator's");
    x = 3;
    p[0] = x + q() + w + (int)k + *t + **u;
    return s;
}
