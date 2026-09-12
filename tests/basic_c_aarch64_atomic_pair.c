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

    struct AtomicNine nine = {0x8877665544332211ull, 0x5a};
    unsigned char nine_expected[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x5a, 0, 0, 0, 0, 0, 0, 0};
    __c11_atomic_store(&nine_cell, nine, __ATOMIC_SEQ_CST);
    struct AtomicNine nine_loaded = __c11_atomic_load(&nine_cell, __ATOMIC_SEQ_CST);

    return !(pair_loaded.low == pair.low && pair_loaded.high == pair.high && nine_loaded.low == nine.low &&
             nine_loaded.high == nine.high && sizeof(pair_cell) == 16 && _Alignof(_Atomic AtomicPair) == 16 &&
             sizeof(nine_cell) == 16 && _Alignof(_Atomic struct AtomicNine) == 16 &&
             bytes_equal(&nine_cell, nine_expected, sizeof(nine_expected)));
}
