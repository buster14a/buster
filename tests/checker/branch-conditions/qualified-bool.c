// #3185. The two conditions have the same typed entries. Reading a volatile or atomic bool still loads it.
int plain(_Bool a, int b)
{
    if (a && b)
    {
        return 1;
    }
    return 0;
}

int qualified(const _Bool a, int b)
{
    if (a && b)
    {
        return 1;
    }
    return 0;
}

_Bool read_volatile(const volatile _Bool* p)
{
    return *p;
}

_Bool read_atomic(const volatile _Atomic(_Bool)* p)
{
    return *p;
}

_Bool from_byte(unsigned char a)
{
    return a;
}

int tested(int n)
{
    volatile _Bool v = n;
    _Atomic(_Bool) a = n;
    return plain(v, n) + qualified(v, n) + read_volatile(&v) + read_atomic(&a) + from_byte(0) + from_byte(2) + from_byte(255);
}
