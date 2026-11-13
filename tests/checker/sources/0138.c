auto g = 1L;
int f(void) {
    int sum = 0;
    for (auto i = 0u; i < 2; i++) sum += (int)i;
    auto (p) = &sum;
    auto int n = 3;
    auto int o;
    o = n;
    _Static_assert(__builtin_types_compatible_p(__typeof__(g), long), "file scope");
    _Static_assert(__builtin_types_compatible_p(__typeof__(p), int *), "parentheses");
    return *p + o;
}
