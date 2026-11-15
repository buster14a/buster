void recovered(void)
{
    _Atomic restrict __auto_type first = 1, second = first + 1;
    _Static_assert(__builtin_types_compatible_p(__typeof__(first), _Atomic(int)), "first is atomic");
    _Static_assert(__builtin_types_compatible_p(__typeof__(second), _Atomic(int)), "second is atomic");
    const _Atomic __auto_type valid = 3;
    (void)(first + second + valid);
    (void)independent_error;
}

restrict _Atomic __auto_type later = 4;
