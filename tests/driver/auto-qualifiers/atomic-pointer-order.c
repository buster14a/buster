typedef int Integer;
Integer object;

void order(void)
{
    _Atomic __auto_type *first = (1, &object);
    _Atomic __auto_type *second = (0, (1, &object));
    restrict _Atomic __auto_type *third = (2, &object);
    int next = independent_error;
}
