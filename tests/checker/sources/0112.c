#define MAX(a, b) ({ __auto_type a_ = (a); __auto_type b_ = (b); a_ > b_ ? a_ : b_; })
long f(int x, long y) {
    __auto_type m = MAX(x, y);
    _Static_assert(__builtin_types_compatible_p(__typeof__(m), long), "the conditional's type");
    return m + MAX(MAX(x, 1), 2);
}
