typedef _Atomic int AtomicInt;

void f(void)
{
    AtomicInt x;
    _Atomic __auto_type a = x;
}
