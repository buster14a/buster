int f(short* s, long* l, int* i, unsigned* u, void* v, _Bool* b)
{
    _Static_assert(_Generic(__sync_fetch_and_add_4(s, 1), short: 1, default: 0), "sized by the object");
    _Static_assert(_Generic(__sync_val_compare_and_swap_1(l, 1, 2), long: 1, default: 0), "val cas");
    _Static_assert(_Generic(__sync_bool_compare_and_swap_16(i, 1, 2), _Bool: 1, default: 0), "bool cas");
    _Static_assert(_Generic(__sync_swap(l, 3), long: 1, default: 0), "swap");
    _Static_assert(_Generic(__sync_fetch_and_umax(i, 1), unsigned: 1, default: 0), "umax");
    __sync_lock_release_8(b);
    return __sync_fetch_and_max(i, 5) + (int)__sync_fetch_and_umin(u, 2) + __sync_fetch_and_min(v, 3) + (int)__sync_swap_2(s, 1);
}
