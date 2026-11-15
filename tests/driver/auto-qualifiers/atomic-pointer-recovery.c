int object;

int recovery(void)
{
    _Atomic __auto_type *pointer = &object;
    int copied = *pointer;
    *pointer = copied;
    pointer++;
    _Atomic __auto_type **nested = (int **)0;
    int other = **nested;
    _Atomic __auto_type later = 7;
    _Static_assert(__builtin_types_compatible_p(__typeof__(later), _Atomic int), "later declaration deduces");
    return independent_error + other + later;
}
