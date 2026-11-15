typedef _Atomic int AtomicInt;

void f(int x)
{
    _Atomic __auto_type a = (AtomicInt)x;
}
