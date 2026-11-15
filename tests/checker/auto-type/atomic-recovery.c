int f(int x)
{
    _Atomic __auto_type a = (_Atomic int)x;
    int *p = &a;
    return a + *p;
}
int g(int x)
{
    _Atomic __auto_type b = (_Atomic int)x;
    return missing;
}
