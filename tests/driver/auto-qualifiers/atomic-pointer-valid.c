int target;
_Atomic __auto_type atomic = 3;

int valid_pointers(void)
{
    __auto_type *pointer = &target;
    __auto_type **nested = &pointer;
    const __auto_type *constant = &target;
    _Atomic __auto_type atomic_pointer = pointer;
    _Static_assert(__builtin_types_compatible_p(__typeof__(atomic), _Atomic int), "atomic scalar deduction");
    _Static_assert(__builtin_types_compatible_p(__typeof__(nested), int **), "nested pointer deduction");
    _Static_assert(__builtin_types_compatible_p(__typeof__(constant), const int *), "qualified pointer deduction");
    _Static_assert(__builtin_types_compatible_p(__typeof__(atomic_pointer), _Atomic(int *)), "atomic pointer object deduction");
    return **nested + *constant + *atomic_pointer + atomic;
}
