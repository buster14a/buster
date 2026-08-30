// The GNU `__atomic_*` builtin family (issue #829). The frontend carried the
// `__c11_atomic_*` family only, so CPython's configure probe for
// HAVE_BUILTIN_ATOMIC failed and pyconfig.h diverged from the Clang reference;
// glibc's headers and most Linux userland reach for this spelling too.
//
// The two families share every operation and lower to the same IR. Three things
// differ, and each has a check here:
//
//   - the first argument is an ordinary pointer, not a pointer to an `_Atomic`
//     object;
//   - `__atomic_add_fetch` and its four siblings answer the updated value where
//     `__atomic_fetch_add` answers the previous one;
//   - arithmetic on a pointer object is in *bytes*, not elements. Both clang
//     and gcc leave `p` unmoved by `__atomic_add_fetch(&p, 2, ...)` on an
//     `int*` -- measured 2026-08-30 -- where the C11 spelling on an
//     `_Atomic(int*)` advances it by two elements. tests/basic_c_atomic.c is
//     the C11 side of that pair.
//
// `__atomic_compare_exchange_n` carries the `weak` flag the C11 name spells;
// a strong exchange satisfies a weak request, so the flag is read and
// discarded and both spellings lower to the strong form.

static int shared;
static char flag;
static int array[8];

int main(void)
{
    int value = 0;
    __atomic_store_n(&value, 5, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&value, __ATOMIC_SEQ_CST) != 5)
    {
        return 1;
    }
    if (__atomic_exchange_n(&value, 9, __ATOMIC_SEQ_CST) != 5 || value != 9)
    {
        return 2;
    }
    if (__atomic_fetch_add(&value, 3, __ATOMIC_SEQ_CST) != 9 || value != 12)
    {
        return 3;
    }
    if (__atomic_add_fetch(&value, 3, __ATOMIC_SEQ_CST) != 15 || value != 15)
    {
        return 4;
    }
    if (__atomic_fetch_sub(&value, 2, __ATOMIC_SEQ_CST) != 15 || value != 13)
    {
        return 5;
    }
    if (__atomic_sub_fetch(&value, 3, __ATOMIC_SEQ_CST) != 10 || value != 10)
    {
        return 6;
    }
    if (__atomic_fetch_and(&value, 6, __ATOMIC_SEQ_CST) != 10 || value != 2)
    {
        return 7;
    }
    if (__atomic_and_fetch(&value, 3, __ATOMIC_SEQ_CST) != 2 || value != 2)
    {
        return 8;
    }
    if (__atomic_fetch_or(&value, 5, __ATOMIC_SEQ_CST) != 2 || value != 7)
    {
        return 9;
    }
    if (__atomic_or_fetch(&value, 8, __ATOMIC_SEQ_CST) != 15 || value != 15)
    {
        return 10;
    }
    if (__atomic_fetch_xor(&value, 1, __ATOMIC_SEQ_CST) != 15 || value != 14)
    {
        return 11;
    }
    if (__atomic_xor_fetch(&value, 2, __ATOMIC_SEQ_CST) != 12 || value != 12)
    {
        return 12;
    }
    {
        int expected = 12;
        if (!__atomic_compare_exchange_n(&value, &expected, 42, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || value != 42)
        {
            return 13;
        }
    }
    {
        // A failing exchange writes the observed value back through the
        // expected pointer, and the `weak` flag changes nothing here.
        int expected = 1;
        if (__atomic_compare_exchange_n(&value, &expected, 0, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || expected != 42 || value != 42)
        {
            return 14;
        }
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    if (!__atomic_is_lock_free(sizeof(int), 0) || !__atomic_always_lock_free(sizeof(int), 0))
    {
        return 15;
    }
    if (__atomic_test_and_set(&flag, __ATOMIC_SEQ_CST))
    {
        return 16;
    }
    if (!__atomic_test_and_set(&flag, __ATOMIC_SEQ_CST))
    {
        return 17;
    }
    __atomic_clear(&flag, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&flag, __ATOMIC_SEQ_CST) != 0)
    {
        return 18;
    }
    // The relaxed end of the order vocabulary, and a static object rather than
    // a local one.
    __atomic_store_n(&shared, 77, __ATOMIC_RELEASE);
    if (__atomic_load_n(&shared, __ATOMIC_ACQUIRE) != 77)
    {
        return 19;
    }
    __atomic_store_n(&shared, 78, __ATOMIC_RELAXED);
    if (__atomic_load_n(&shared, __ATOMIC_RELAXED) != 78)
    {
        return 20;
    }
    {
        // Two bytes, not two elements: the answer lands inside array[0]. The
        // distance is read as an address rather than as a pointer difference,
        // which is only defined between elements of one array.
        int* cursor = array;
        int* answer = __atomic_add_fetch(&cursor, 2, __ATOMIC_SEQ_CST);
        if (answer != cursor)
        {
            return 21;
        }
        if ((unsigned long long)(void*)cursor - (unsigned long long)(void*)array != 2u)
        {
            return 22;
        }
        int* previous = __atomic_fetch_add(&cursor, 2, __ATOMIC_SEQ_CST);
        if ((unsigned long long)(void*)previous - (unsigned long long)(void*)array != 2u ||
            (unsigned long long)(void*)cursor - (unsigned long long)(void*)array != 4u)
        {
            return 23;
        }
    }
    {
        long long wide = 0;
        __atomic_store_n(&wide, 1ll << 40, __ATOMIC_SEQ_CST);
        if (__atomic_fetch_add(&wide, 1, __ATOMIC_SEQ_CST) != (1ll << 40) || wide != (1ll << 40) + 1)
        {
            return 24;
        }
    }
    {
        char narrow = 0;
        if (__atomic_add_fetch(&narrow, 3, __ATOMIC_SEQ_CST) != 3 || narrow != 3)
        {
            return 25;
        }
        short half = 1000;
        if (__atomic_sub_fetch(&half, 1, __ATOMIC_SEQ_CST) != 999 || half != 999)
        {
            return 26;
        }
    }
    return 0;
}
