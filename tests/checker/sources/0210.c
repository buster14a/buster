typedef unsigned long U64;
int f(U64* u, int* i, unsigned char* c, _Bool* b, int** p, double* d)
{
    _Static_assert(_Generic(__atomic_fetch_add(u, 1, __ATOMIC_RELAXED), U64: 1, default: 0), "fetch_add");
    _Static_assert(_Generic(__atomic_add_fetch(i, 1, __ATOMIC_SEQ_CST), int: 1, default: 0), "add_fetch");
    _Static_assert(_Generic(__atomic_exchange_n(c, 1, __ATOMIC_ACQ_REL), unsigned char: 1, default: 0), "exchange_n");
    _Static_assert(_Generic(__atomic_compare_exchange_n(i, i, 3, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED), _Bool: 1, default: 0), "cas");
    _Static_assert(_Generic(__atomic_fetch_add(p, 4, __ATOMIC_RELAXED), int*: 1, default: 0), "pointer add");
    _Static_assert(_Generic(__atomic_fetch_sub(d, 1, __ATOMIC_RELAXED), double: 1, default: 0), "fetch_sub double");
    _Static_assert(_Generic(__sync_val_compare_and_swap(u, 1, 2), U64: 1, default: 0), "sync val cas");
    _Static_assert(_Generic(__sync_bool_compare_and_swap(u, 1, 2), _Bool: 1, default: 0), "sync bool cas");
    __sync_lock_release(b);
    (void)__sync_fetch_and_or(u, 1, i, c);
    (void)__atomic_fetch_nand(u, 1, __ATOMIC_RELAXED);
    (void)__atomic_xor_fetch(b, 1, __ATOMIC_RELAXED);
    return __sync_fetch_and_add(i, 1) + __sync_lock_test_and_set(c, 1);
}
