const _Atomic __auto_type constant = 3;
_Atomic __auto_type value = 4;

int use_qualifiers(int x)
{
    __auto_type restrict pointer = &x;
    __auto_type *restrict declarator = &x;
    volatile _Atomic __auto_type atomic = x, next = x + 1;
    _Static_assert(__builtin_types_compatible_p(__typeof__(pointer), int *), "deduced pointer");
    _Static_assert(__builtin_types_compatible_p(__typeof__(atomic), _Atomic(int)), "deduced atomic");
    *pointer += atomic;
    *declarator += next;
    return x + constant + value;
}
