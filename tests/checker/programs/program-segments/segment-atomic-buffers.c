// Native representation plans retain the independent spaces of their object, desired, expected and result pointers.
#define SS __attribute__((address_space(258)))

typedef struct __attribute__((aligned(8))) Pair Pair;
struct Pair
{
    int a;
    int b;
};

static _Thread_local long values[5];
static _Thread_local unsigned char bytes[7];
static _Thread_local Pair pairs[3];
static _Thread_local _Atomic double floating;
static _Thread_local double expected_double;

int tested(int n)
{
    char* thread = __builtin_thread_pointer();
    long __seg_fs* p = (long __seg_fs*)((char*)values - thread);
    unsigned char __seg_fs* b = (unsigned char __seg_fs*)((char*)bytes - thread);
    Pair __seg_fs* q = (Pair __seg_fs*)((char*)pairs - thread);
    _Atomic double __seg_fs* f = (_Atomic double __seg_fs*)((char*)&floating - thread);
    double __seg_fs* e = (double __seg_fs*)((char*)&expected_double - thread);
    const long seed = (unsigned)n & 255;
    values[0] = seed + 11;
    values[1] = seed + 23;
    values[2] = -7;
    values[3] = -9;
    values[4] = 0x1234;
    unsigned long h = 0;
    long result = 0;

    __atomic_load(p, &result, 5);
    h += result;
    __atomic_load(p, (long SS*)&result, 2);
    h = h * 33 + result;
    __atomic_load(&values[0], p + 2, 0);
    h = h * 33 + values[2];
    __atomic_store(p, p + 1, 3);
    __atomic_exchange(p, p + 1, p + 2, 5);
    h = h * 33 + values[0] + values[2];
    values[1] = seed + 37;
    __atomic_exchange(p, p + 1, p + 1, 5);
    h = h * 33 + values[0] + values[1];

    values[3] = -1;
    h += __atomic_compare_exchange(p, p + 3, p + 1, 0, 5, 5);
    h = h * 33 + values[3];
    h += __atomic_compare_exchange(p, p + 3, p + 1, 1, 5, 0);
    h = h * 33 + values[0] + values[3];
    h += __atomic_compare_exchange_n(p, p, seed + 41, 0, 5, 5);
    h = h * 33 + values[0];

    for (unsigned i = 0; i < 7; i += 1)
    {
        bytes[i] = (unsigned char)(i + 17);
    }
    __atomic_load(b + 1, b + 3, 5);
    __atomic_store(b + 1, b + 5, 5);
    __atomic_exchange(b + 1, b + 3, b + 5, 5);
    h += __atomic_compare_exchange(b + 1, b + 5, b + 3, 0, 5, 5);
    for (unsigned i = 0; i < 7; i += 1)
    {
        h = h * 33 + bytes[i];
    }

    pairs[0] = (Pair){ (int)seed + 3, 17 };
    pairs[1] = (Pair){ (int)seed + 7, 23 };
    pairs[2] = (Pair){ -1, -1 };
    __atomic_load(q, q + 2, 5);
    __atomic_exchange(q, q + 1, q + 2, 5);
    h += __atomic_compare_exchange(q, q + 2, q + 1, 0, 5, 5);
    h += __atomic_compare_exchange(q, q + 2, q + 1, 1, 5, 0);
    h = h * 33 + pairs[0].a + pairs[1].b + pairs[2].a;

    floating = (double)seed + 1.25;
    expected_double = -1.0;
    h += __c11_atomic_compare_exchange_strong(f, e, 2.5, 5, 5);
    h = h * 33 + (unsigned long)(expected_double * 4);
    h += __c11_atomic_compare_exchange_weak(f, e, 3.5, 5, 0);
    h = h * 33 + (unsigned long)(floating * 4);
    return (int)(h + values[4]);
}
