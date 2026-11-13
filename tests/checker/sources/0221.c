struct Opaque;
long cell;
enum { E = __atomic_always_lock_free(8, &cell) + __atomic_is_lock_free(2, 0) };
_Static_assert(!__atomic_always_lock_free(3, 0), "odd");
int f(unsigned char* c, _Bool* b, void* v, struct Opaque* o)
{
    _Static_assert(_Generic(__atomic_fetch_uinc(c, 3, __ATOMIC_SEQ_CST), unsigned char: 1, default: 0), "uinc");
    _Static_assert(_Generic(__atomic_test_and_set(v, __ATOMIC_ACQUIRE), _Bool: 1, default: 0), "tas");
    __atomic_clear(o, __ATOMIC_RELEASE);
    return __atomic_fetch_udec(b, 1, __ATOMIC_RELAXED) + E + __atomic_is_lock_free(16, v);
}
