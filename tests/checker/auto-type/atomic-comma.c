void f(int x)
{
    _Atomic __auto_type a = (x == 3, (_Atomic int)x);
}

void g(int x)
{
    _Atomic __auto_type b = (1, (2, (_Atomic int)x));
}
