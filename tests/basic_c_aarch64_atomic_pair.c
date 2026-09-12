typedef struct AtomicPair AtomicPair;
struct AtomicPair
{
    unsigned long long low;
    unsigned long long high;
};

struct __attribute__((packed)) AtomicNine
{
    unsigned long long low;
    unsigned char high;
};

static _Atomic AtomicPair pair_cell;
static _Atomic struct AtomicNine nine_cell;
static _Atomic unsigned __int128 wide_cell;

static int bytes_equal(void const* value, unsigned char const* expected, unsigned long count)
{
    unsigned char const* bytes = value;
    int equal = 1;
    for (unsigned long index = 0; index < count; index += 1)
    {
        equal &= bytes[index] == expected[index];
    }
    return equal;
}

int main(void)
{
    AtomicPair pair = {0x1122334455667788ull, 0x0102030405060708ull};
    __c11_atomic_store(&pair_cell, pair, __ATOMIC_RELEASE);
    AtomicPair pair_loaded = __c11_atomic_load(&pair_cell, __ATOMIC_ACQUIRE);
    AtomicPair pair_replacement = {0x8877665544332211ull, 0x1020304050607080ull};
    AtomicPair pair_previous = __c11_atomic_exchange(&pair_cell, pair_replacement, __ATOMIC_ACQ_REL);
    AtomicPair pair_expected = pair;
    int pair_stale_changed = __c11_atomic_compare_exchange_strong(&pair_cell, &pair_expected, pair, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    int pair_changed = __c11_atomic_compare_exchange_strong(&pair_cell, &pair_expected, pair, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);

    struct AtomicNine nine = {0x8877665544332211ull, 0x5a};
    unsigned char nine_expected[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x5a, 0, 0, 0, 0, 0, 0, 0};
    __c11_atomic_store(&nine_cell, nine, __ATOMIC_SEQ_CST);
    struct AtomicNine nine_loaded = __c11_atomic_load(&nine_cell, __ATOMIC_SEQ_CST);
    struct AtomicNine nine_replacement = {0x0123456789abcdefull, 0xa5};
    struct AtomicNine nine_previous = __c11_atomic_exchange(&nine_cell, nine_replacement, __ATOMIC_ACQ_REL);
    struct AtomicNine nine_comparison = nine;
    int nine_stale_changed = __c11_atomic_compare_exchange_strong(
        &nine_cell, &nine_comparison, nine, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    int nine_changed = __c11_atomic_compare_exchange_strong(
        &nine_cell, &nine_comparison, nine, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);

    // Literal limb expectations are independent of the wide arithmetic being
    // migrated: addition carries and subtraction borrows across bit 64.
    unsigned __int128 wide = ((unsigned __int128)0x1122334455667788ull << 64) | 0xfffffffffffffff0ull;
    unsigned __int128 operand = ((unsigned __int128)0x0102030405060708ull << 64) | 0x35ull;
    __c11_atomic_store(&wide_cell, wide, __ATOMIC_SEQ_CST);
    int wide_ok = __c11_atomic_fetch_add(&wide_cell, operand, __ATOMIC_ACQ_REL) == wide;
    wide = ((unsigned __int128)0x122436485a6c7e91ull << 64) | 0x25ull;
    wide_ok &= __c11_atomic_fetch_sub(&wide_cell, 0x36, __ATOMIC_RELAXED) == wide;
    wide = ((unsigned __int128)0x122436485a6c7e90ull << 64) | 0xffffffffffffffefull;
    wide_ok &= __c11_atomic_fetch_and(&wide_cell, operand | 0xff, __ATOMIC_SEQ_CST) == wide;
    wide &= operand | 0xff;
    wide_ok &= __c11_atomic_fetch_or(&wide_cell, (unsigned __int128)0x8000 << 64, __ATOMIC_RELEASE) == wide;
    wide |= (unsigned __int128)0x8000 << 64;
    wide_ok &= __c11_atomic_fetch_xor(&wide_cell, 3, __ATOMIC_ACQUIRE) == wide;
    wide ^= 3;
    wide_ok &= __c11_atomic_exchange(&wide_cell, operand, __ATOMIC_SEQ_CST) == wide;
    wide = operand;
    unsigned __int128 wide_expected = wide + 1;
    wide_ok &= !__c11_atomic_compare_exchange_strong(&wide_cell, &wide_expected, wide - 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) &&
               wide_expected == wide;
    wide_expected = wide ^ ((unsigned __int128)1 << 100);
    wide_ok &= !__c11_atomic_compare_exchange_strong(&wide_cell, &wide_expected, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE) &&
               wide_expected == wide;
    wide_expected = ~wide;
    wide_ok &= !__c11_atomic_compare_exchange_strong(&wide_cell, &wide_expected, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED) &&
               wide_expected == wide;
    wide_ok &= __c11_atomic_compare_exchange_weak(&wide_cell, &wide_expected, wide - 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) &&
               __c11_atomic_load(&wide_cell, __ATOMIC_RELAXED) == wide - 1;

    return !(pair_loaded.low == pair.low && pair_loaded.high == pair.high && pair_previous.low == pair.low &&
             pair_previous.high == pair.high && !pair_stale_changed && pair_expected.low == pair_replacement.low &&
             pair_expected.high == pair_replacement.high && pair_changed && nine_loaded.low == nine.low &&
             nine_loaded.high == nine.high && nine_previous.low == nine.low && nine_previous.high == nine.high &&
             !nine_stale_changed && nine_comparison.low == nine_replacement.low && nine_comparison.high == nine_replacement.high &&
             nine_changed && sizeof(pair_cell) == 16 && _Alignof(_Atomic AtomicPair) == 16 &&
             sizeof(nine_cell) == 16 && _Alignof(_Atomic struct AtomicNine) == 16 &&
             bytes_equal(&nine_cell, nine_expected, sizeof(nine_expected)) && wide_ok);
}
