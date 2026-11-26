// Dynamic orders exercise the out-of-line helpers. Equal signatures in four spaces must stay distinct.
#define SS __attribute__((address_space(258)))
long syscall(long, ...);

static _Thread_local long values[6];
static _Thread_local unsigned char flags[4];
static _Thread_local _Float16 half;
static _Thread_local double number;

#define OPERATIONS(name, space) \
static __attribute__((noinline)) unsigned long name(long space* p, long space* e, unsigned char space* flag, int order) \
{ \
    unsigned long h = __atomic_load_n(p, order); \
    __atomic_store_n(p, (long)h + 5, order); \
    h += __atomic_exchange_n(p, 19, order); \
    h += __atomic_fetch_add(p, 3, order); \
    h += __atomic_xor_fetch(p, 7, order); \
    h += __atomic_compare_exchange_n(p, e, 23, 0, order, order); \
    h += *e; \
    h += __atomic_compare_exchange_n(p, e, 29, 1, order, order); \
    h += *p; \
    __sync_lock_release(p); \
    h += *p; \
    __atomic_clear(flag, order); \
    h += __atomic_test_and_set(flag, order); \
    h += __atomic_test_and_set(flag, order); \
    return h + *flag; \
}

OPERATIONS(flat, )
OPERATIONS(fs, __seg_fs)
OPERATIONS(gs, __seg_gs)
OPERATIONS(ss, SS)

static __attribute__((noinline)) int cross(long __seg_fs* p, long __seg_gs* e, int order)
{
    return __atomic_compare_exchange_n(p, e, 73, 0, order, order);
}

int tested(int n)
{
    char* thread = __builtin_thread_pointer();
    unsigned long old_gs = 0;
    unsigned long h = -1;
    if (syscall(158, 0x1004, &old_gs) == 0 && syscall(158, 0x1001, (unsigned long)thread) == 0)
    {
        long __seg_fs* p = (long __seg_fs*)((char*)values - thread);
        long __seg_gs* g = (long __seg_gs*)((char*)values - thread);
        unsigned char __seg_fs* f = (unsigned char __seg_fs*)((char*)flags - thread);
        unsigned char __seg_gs* k = (unsigned char __seg_gs*)((char*)flags - thread);
        _Float16 __seg_fs* a = (_Float16 __seg_fs*)((char*)&half - thread);
        double __seg_fs* d = (double __seg_fs*)((char*)&number - thread);
        h = 0;
        for (unsigned iteration = 0; iteration < 2; iteration += 1)
        {
            for (unsigned i = 0; i < 6; i += 1)
            {
                values[i] = ((unsigned)n & 255) + i + 11;
            }
            for (unsigned i = 0; i < 4; i += 1)
            {
                flags[i] = 0xff;
            }
            const int order = iteration == 0 ? 0 : 5;
            h = h * 33 + flat(values, values + 4, flags, order);
            h = h * 33 + fs(p + 1, p + 4, f + 1, order);
            h = h * 33 + gs(g + 2, g + 4, k + 2, order);
            h = h * 33 + ss((long SS*)(values + 3), (long SS*)(values + 4), (unsigned char SS*)(flags + 3), order);
            values[0] = 31;
            values[4] = -1;
            h += cross(p, g + 4, order);
            h = h * 33 + values[4];
            h += cross(p, g + 4, order);
            h = h * 33 + values[0];
            h += __atomic_compare_exchange_n(p, g + 4, 97, 0, 5, 5);
            h = h * 33 + values[4];
            h += __atomic_compare_exchange_n(p, g + 4, 97, 1, 5, 0);
            h = h * 33 + values[0];
            half = 1.25f16;
            number = 2.5;
            h += (unsigned long)__atomic_fetch_fmaximum(a, 3.5f16, order);
            h += (unsigned long)__atomic_fetch_fminimum(d, 1.25, order);
            h = h * 33 + (unsigned long)(half * 4) + (unsigned long)(number * 4);
        }
        if (syscall(158, 0x1001, old_gs) != 0)
        {
            h = 0;
        }
    }
    return (int)h;
}
