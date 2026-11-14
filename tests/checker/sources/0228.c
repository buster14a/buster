_Atomic int a;
int f(int* p, int* e, int s)
{
    __scoped_atomic_thread_fence(__ATOMIC_SEQ_CST, 9);
    __opencl_atomic_init(&a, 1);
    return __scoped_atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST, s) + __scoped_atomic_compare_exchange_n(p, e, 2, 0, 5, 5, __MEMORY_SCOPE_DEVICE) +
           __hip_atomic_load(p, 5, 1) + __hip_atomic_compare_exchange_strong(p, e, 3, 5, 5, 6) + __opencl_atomic_fetch_add(&a, 1, 5, 4);
}
