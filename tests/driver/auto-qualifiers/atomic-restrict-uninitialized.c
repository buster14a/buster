void uninitialized(void)
{
    restrict _Atomic __auto_type value;
    (void)value;
    (void)independent_error;
}
