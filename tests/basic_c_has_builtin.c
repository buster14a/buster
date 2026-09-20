// #665: every newly advertised native builtin has an executable witness.
// Keep these exact-name expectations independent of the implementation table.
#if __has_builtin(__builtin_offsetof) != 1
#error __builtin_offsetof must be advertised on native targets
#endif
#if __has_builtin(__builtin_complex) != 1
#error __builtin_complex must be advertised on native targets
#endif
#if __has_builtin(__builtin_ffs) != 1
#error __builtin_ffs must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_load) != 1
#error __c11_atomic_load must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_store) != 1
#error __c11_atomic_store must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_init) != 1
#error __c11_atomic_init must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_fetch_add) != 1
#error __c11_atomic_fetch_add must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_fetch_sub) != 1
#error __c11_atomic_fetch_sub must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_fetch_and) != 1
#error __c11_atomic_fetch_and must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_fetch_or) != 1
#error __c11_atomic_fetch_or must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_fetch_xor) != 1
#error __c11_atomic_fetch_xor must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_exchange) != 1
#error __c11_atomic_exchange must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_compare_exchange_strong) != 1
#error __c11_atomic_compare_exchange_strong must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_compare_exchange_weak) != 1
#error __c11_atomic_compare_exchange_weak must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_is_lock_free) != 1
#error __c11_atomic_is_lock_free must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_thread_fence) != 1
#error __c11_atomic_thread_fence must be advertised on native targets
#endif
#if __has_builtin(__c11_atomic_signal_fence) != 1
#error __c11_atomic_signal_fence must be advertised on native targets
#endif
#if __has_builtin(__sync_synchronize) != 1
#error __sync_synchronize must be advertised on native targets
#endif
#if __has_builtin(__atomic_load_n) != 1
#error __atomic_load_n must be advertised on native targets
#endif
#if __has_builtin(__atomic_store_n) != 1
#error __atomic_store_n must be advertised on native targets
#endif
#if __has_builtin(__atomic_exchange_n) != 1
#error __atomic_exchange_n must be advertised on native targets
#endif
#if __has_builtin(__atomic_fetch_add) != 1
#error __atomic_fetch_add must be advertised on native targets
#endif
#if __has_builtin(__atomic_fetch_sub) != 1
#error __atomic_fetch_sub must be advertised on native targets
#endif
#if __has_builtin(__atomic_fetch_and) != 1
#error __atomic_fetch_and must be advertised on native targets
#endif
#if __has_builtin(__atomic_fetch_or) != 1
#error __atomic_fetch_or must be advertised on native targets
#endif
#if __has_builtin(__atomic_fetch_xor) != 1
#error __atomic_fetch_xor must be advertised on native targets
#endif
#if __has_builtin(__atomic_add_fetch) != 1
#error __atomic_add_fetch must be advertised on native targets
#endif
#if __has_builtin(__atomic_sub_fetch) != 1
#error __atomic_sub_fetch must be advertised on native targets
#endif
#if __has_builtin(__atomic_and_fetch) != 1
#error __atomic_and_fetch must be advertised on native targets
#endif
#if __has_builtin(__atomic_or_fetch) != 1
#error __atomic_or_fetch must be advertised on native targets
#endif
#if __has_builtin(__atomic_xor_fetch) != 1
#error __atomic_xor_fetch must be advertised on native targets
#endif
#if __has_builtin(__atomic_compare_exchange_n) != 1
#error __atomic_compare_exchange_n must be advertised on native targets
#endif
#if __has_builtin(__atomic_thread_fence) != 1
#error __atomic_thread_fence must be advertised on native targets
#endif
#if __has_builtin(__atomic_signal_fence) != 1
#error __atomic_signal_fence must be advertised on native targets
#endif
#if __has_builtin(__atomic_is_lock_free) != 1
#error __atomic_is_lock_free must be advertised on native targets
#endif
#if __has_builtin(__atomic_always_lock_free) != 1
#error __atomic_always_lock_free must be advertised on native targets
#endif
#if __has_builtin(__atomic_test_and_set) != 1
#error __atomic_test_and_set must be advertised on native targets
#endif
#if __has_builtin(__atomic_clear) != 1
#error __atomic_clear must be advertised on native targets
#endif

struct QueryRecord
{
    char tag;
    int values[3];
};

_Static_assert(__builtin_offsetof(struct QueryRecord, values) == 4, "member offset");
_Static_assert(__builtin_offsetof(struct QueryRecord, values[2]) == 12, "element offset");

static int check_offset(void)
{
    struct QueryRecord object = {0};
    unsigned long long offset = (unsigned long long)((char*)&object.values[2] - (char*)&object);
    return offset == __builtin_offsetof(struct QueryRecord, values[2]);
}

static int check_complex(void)
{
    double _Complex constant_complex = __builtin_complex(3.5, -6.25);
    volatile double real = 2.25;
    volatile double imaginary = -7.5;
    volatile float real_float = -1.25f;
    volatile float imaginary_float = 2.5f;
    volatile double negative_zero = -0.0;
    double _Complex value = __builtin_complex(real, imaginary);
    float _Complex value_float = __builtin_complex(real_float, imaginary_float);
    double _Complex zero = __builtin_complex(negative_zero, negative_zero);
    union DoubleBits
    {
        double value;
        unsigned long long bits;
    } real_bits, imaginary_bits;
    real_bits.value = __real__ zero;
    imaginary_bits.value = __imag__ zero;
    int valid = __real__ constant_complex == 3.5 && __imag__ constant_complex == -6.25;
    valid &= __real__ value == 2.25 && __imag__ value == -7.5;
    valid &= __real__ value_float == -1.25f && __imag__ value_float == 2.5f;
    valid &= real_bits.bits == 0x8000000000000000ULL;
    valid &= imaginary_bits.bits == 0x8000000000000000ULL;
    return valid;
}

static int ffs_calls;

static unsigned int ffs_next(void)
{
    ffs_calls += 1;
    return 0x100u;
}

static int check_ffs(void)
{
    unsigned char narrow = 0x80u;
    unsigned long long wide_zero = 1ull << 40;
    unsigned long long wide_low = (1ull << 40) | 4ull;
    int valid = 1;
    valid &= __builtin_ffs(0) == 0;
    valid &= __builtin_ffs(1u) == 1;
    valid &= __builtin_ffs(-1) == 1;
    valid &= __builtin_ffs(-128.75) == 8;
    valid &= __builtin_ffs(__builtin_complex(-128.75, 16.0)) == 8;
    valid &= __builtin_ffs(0x1000u) == 13;
    valid &= __builtin_ffs((-2147483647 - 1)) == 32;
    valid &= __builtin_ffs(narrow) == 8;
    valid &= __builtin_ffs(wide_zero) == 0;
    valid &= __builtin_ffs(wide_low) == 3;
    valid &= __builtin_ffsl(0) == 0;
    valid &= __builtin_ffsll(0) == 0;
    valid &= __builtin_ffsl(-1) == 1;
    valid &= __builtin_ffsll(-1) == 1;
    valid &= __builtin_ffsl(wide_zero) == (sizeof(long) == 8 ? 41 : 0);
    valid &= __builtin_ffsl(1ull << 63) == (sizeof(long) == 8 ? 64 : 0);
    valid &= __builtin_ffsll(1ull << 63) == 64;
    valid &= __builtin_ffsll(wide_zero) == 41;
    valid &= __builtin_ffsl(wide_low) == 3;
    valid &= __builtin_ffsll(wide_low) == 3;
    valid &= __builtin_ffsl(-128.75) == 8;
    valid &= __builtin_ffsll(__builtin_complex(-128.75, 16.0)) == 8;
    ffs_calls = 0;
    valid &= __builtin_ffs(ffs_next()) == 9;
    valid &= ffs_calls == 1;
    valid &= __builtin_ffsl(ffs_next()) == 9;
    valid &= ffs_calls == 2;
    valid &= __builtin_ffsll(ffs_next()) == 9;
    valid &= ffs_calls == 3;
    return valid;
}
static int check_gnu_atomics(void)
{
    unsigned value = 0;
    unsigned expected = 0;
    unsigned char flag = 0;
    int valid = 1;
    __atomic_store_n(&value, 12, __ATOMIC_SEQ_CST);
    valid &= __atomic_load_n(&value, __ATOMIC_SEQ_CST) == 12;
    valid &= __atomic_exchange_n(&value, 8, __ATOMIC_SEQ_CST) == 12;
    valid &= __atomic_fetch_add(&value, 5, __ATOMIC_SEQ_CST) == 8;
    valid &= __atomic_fetch_sub(&value, 3, __ATOMIC_SEQ_CST) == 13;
    valid &= __atomic_fetch_and(&value, 6, __ATOMIC_SEQ_CST) == 10;
    valid &= __atomic_fetch_or(&value, 8, __ATOMIC_SEQ_CST) == 2;
    valid &= __atomic_fetch_xor(&value, 3, __ATOMIC_SEQ_CST) == 10;
    valid &= __atomic_add_fetch(&value, 2, __ATOMIC_SEQ_CST) == 11;
    valid &= __atomic_sub_fetch(&value, 3, __ATOMIC_SEQ_CST) == 8;
    valid &= __atomic_and_fetch(&value, 6, __ATOMIC_SEQ_CST) == 0;
    valid &= __atomic_or_fetch(&value, 9, __ATOMIC_SEQ_CST) == 9;
    valid &= __atomic_xor_fetch(&value, 3, __ATOMIC_SEQ_CST) == 10;
    expected = 7;
    valid &= !__atomic_compare_exchange_n(&value, &expected, 20, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    valid &= expected == 10 && value == 10;
    valid &= __atomic_compare_exchange_n(&value, &expected, 20, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    valid &= expected == 10 && value == 20;
    expected = 7;
    valid &= !__atomic_compare_exchange_n(&value, &expected, 30, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    valid &= expected == 20 && value == 20;
    valid &= !__atomic_test_and_set(&flag, __ATOMIC_SEQ_CST);
    valid &= __atomic_test_and_set(&flag, __ATOMIC_SEQ_CST);
    __atomic_clear(&flag, __ATOMIC_SEQ_CST);
    valid &= flag == 0;
    valid &= __atomic_is_lock_free(sizeof(value), 0);
    valid &= __atomic_always_lock_free(sizeof(value), 0);
    valid &= !__atomic_always_lock_free(3, 0);
    volatile unsigned volatile_value = 0;
    __atomic_store_n(&volatile_value, 17, __ATOMIC_SEQ_CST);
    valid &= __atomic_load_n(&volatile_value, __ATOMIC_SEQ_CST) == 17;
    // GNU pointer arithmetic is in bytes, unlike the C11 element count.
    int array[2];
    int* cursor = array;
    int* previous = __atomic_fetch_add(&cursor, 1, __ATOMIC_SEQ_CST);
    valid &= previous == array && (char*)cursor == (char*)array + 1;
    int* restored = __atomic_sub_fetch(&cursor, 1, __ATOMIC_SEQ_CST);
    valid &= restored == array && cursor == array;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    __sync_synchronize();
    return valid;
}

static int check_c11_atomics(void)
{
    _Atomic(unsigned) value;
    unsigned expected = 0;
    int valid = 1;
    __c11_atomic_init(&value, 4);
    valid &= __c11_atomic_load(&value, __ATOMIC_SEQ_CST) == 4;
    __c11_atomic_store(&value, 12, __ATOMIC_SEQ_CST);
    valid &= __c11_atomic_exchange(&value, 8, __ATOMIC_SEQ_CST) == 12;
    valid &= __c11_atomic_fetch_add(&value, 5, __ATOMIC_SEQ_CST) == 8;
    valid &= __c11_atomic_fetch_sub(&value, 3, __ATOMIC_SEQ_CST) == 13;
    valid &= __c11_atomic_fetch_and(&value, 6, __ATOMIC_SEQ_CST) == 10;
    valid &= __c11_atomic_fetch_or(&value, 8, __ATOMIC_SEQ_CST) == 2;
    valid &= __c11_atomic_fetch_xor(&value, 3, __ATOMIC_SEQ_CST) == 10;
    expected = 7;
    valid &= !__c11_atomic_compare_exchange_strong(&value, &expected, 20, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    valid &= expected == 9;
    valid &= __c11_atomic_compare_exchange_strong(&value, &expected, 20, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    valid &= expected == 9;
    // A stale expected value must fail even for weak CAS. Do not require a
    // weak CAS to succeed without spurious failure on any particular target.
    expected = 7;
    valid &= !__c11_atomic_compare_exchange_weak(&value, &expected, 30, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    valid &= expected == 20;
    valid &= __c11_atomic_load(&value, __ATOMIC_SEQ_CST) == 20;
    valid &= __c11_atomic_is_lock_free(sizeof(value));
    __c11_atomic_thread_fence(__ATOMIC_SEQ_CST);
    __c11_atomic_signal_fence(__ATOMIC_SEQ_CST);
    return valid;
}

int main(void)
{
    int result = 0;
    if (!check_offset())
    {
        result |= 1;
    }
    if (!check_complex())
    {
        result |= 2;
    }
    if (!check_gnu_atomics())
    {
        result |= 4;
    }
    if (!check_c11_atomics())
    {
        result |= 8;
    }
    if (!check_ffs())
    {
        result |= 16;
    }
    return result;
}
