unsigned f(unsigned* u, int* i, float* x, _Bool* b)
{
    _Static_assert(_Generic(__atomic_fetch_min(u, 1, __ATOMIC_RELAXED), unsigned: 1, default: 0), "fetch_min");
    _Static_assert(_Generic(__atomic_max_fetch(x, 1, __ATOMIC_RELAXED), float: 1, default: 0), "max_fetch");
    return __atomic_fetch_max(i, 1, 5) + __atomic_min_fetch(u, 2, 0) + __atomic_fetch_min(b, 1, 5);
}
