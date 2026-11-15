int object;
_Atomic __auto_type *first = &object, *second = &object, value = 3, *third = &object;
_Static_assert(__builtin_types_compatible_p(__typeof__(value), _Atomic int), "later group member deduces");

int group(void)
{
    _Atomic __auto_type before = 4, *pointer = &object, after = 5;
    _Static_assert(__builtin_types_compatible_p(__typeof__(before), _Atomic int), "earlier member keeps deduction");
    _Static_assert(__builtin_types_compatible_p(__typeof__(after), _Atomic int), "later member keeps deduction");
    return before + after + value;
}
