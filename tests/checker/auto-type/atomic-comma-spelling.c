typedef _Atomic int AtomicInt;
void f(void)
{
    _Atomic __auto_type a = ((AtomicInt)1, (_Atomic int)2);
    _Atomic __auto_type b = ((_Atomic int)1, (AtomicInt)2);
}
