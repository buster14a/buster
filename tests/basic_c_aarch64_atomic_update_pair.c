// Wide atomic updates use an independent two-u64 reference, not the compiler's
// i128 arithmetic. Keep this fixture strict on AArch64; Win64 x86-64 i128 ABI
// exclusions are unrelated. The structural machine tests cover retry paths.
typedef unsigned long long Limb;
typedef unsigned __int128 Wide;
typedef __int128 SignedWide;
typedef struct Pair Pair;
struct Pair
{
    Limb low;
    Limb high;
};

static volatile Limb runtime_seed = 0x13579bdf2468ace0ULL;

static Wide pair_bits(Pair value)
{
    return (Wide)value.low | ((Wide)value.high << 64);
}

static int pair_matches(Wide value, Pair expected)
{
    return (Limb)value == expected.low && (Limb)(value >> 64) == expected.high;
}

static void reference_update(Pair* value, Pair operand, int operation)
{
    Limb old_low = value->low;
    switch (operation)
    {
        case 0:
            value->low = old_low + operand.low;
            value->high += operand.high + (value->low < old_low);
            break;
        case 1:
            value->low = old_low - operand.low;
            value->high -= operand.high + (old_low < operand.low);
            break;
        case 2:
            value->low &= operand.low;
            value->high &= operand.high;
            break;
        case 3:
            value->low |= operand.low;
            value->high |= operand.high;
            break;
        case 4:
            value->low ^= operand.low;
            value->high ^= operand.high;
            break;
        case 5:
            *value = operand;
            break;
    }
}

static Wide atomic_update(_Atomic(Wide)* cell, Wide operand, int operation)
{
    Wide old;
    switch (operation)
    {
        case 0: old = __c11_atomic_fetch_add(cell, operand, __ATOMIC_ACQ_REL); break;
        case 1: old = __c11_atomic_fetch_sub(cell, operand, __ATOMIC_RELAXED); break;
        case 2: old = __c11_atomic_fetch_and(cell, operand, __ATOMIC_SEQ_CST); break;
        case 3: old = __c11_atomic_fetch_or(cell, operand, __ATOMIC_RELEASE); break;
        case 4: old = __c11_atomic_fetch_xor(cell, operand, __ATOMIC_ACQUIRE); break;
        default: old = __c11_atomic_exchange(cell, operand, __ATOMIC_SEQ_CST); break;
    }
    return old;
}

static int test_updates(Limb seed)
{
    Pair starts[] = {{0, 0}, {~0ULL, 0}, {0, ~0ULL}, {~0ULL, ~0ULL},
                     {0, 0x8000000000000000ULL}, {seed, ~seed}};
    int failed = 0;
    for (unsigned int start = 0; start < sizeof(starts) / sizeof(starts[0]); start += 1)
    {
        for (int operation = 0; operation < 6; operation += 1)
        {
            _Atomic(Wide) cell;
            Pair value = starts[start];
            __c11_atomic_store(&cell, pair_bits(value), __ATOMIC_RELAXED);
            for (unsigned int bit = 0; bit < 128; bit += 1)
            {
                Pair operand = {0, 0};
                if (bit < 64)
                    operand.low = 1ULL << bit;
                else
                    operand.high = 1ULL << (bit - 64);
                Pair before = value;
                Wide old = atomic_update(&cell, pair_bits(operand), operation);
                reference_update(&value, operand, operation);
                failed |= !pair_matches(old, before);
                failed |= !pair_matches(__c11_atomic_load(&cell, __ATOMIC_ACQUIRE), value);
                // Also consume both nonzero input limbs and arbitrary bit patterns.
                operand.low = seed ^ (Limb)bit;
                operand.high = ~seed ^ (Limb)bit;
                before = value;
                old = atomic_update(&cell, pair_bits(operand), operation);
                reference_update(&value, operand, operation);
                failed |= !pair_matches(old, before);
                failed |= !pair_matches(__c11_atomic_load(&cell, __ATOMIC_SEQ_CST), value);
            }
        }
    }
    return failed;
}

static int test_compare_exchange(Limb seed)
{
    int failed = 0;
    for (unsigned int bit = 0; bit < 128; bit += 1)
    {
        Pair before = {seed, ~seed};
        Pair replacement = {~seed + bit, seed - bit};
        Pair mismatch = before;
        if (bit < 64)
            mismatch.low ^= 1ULL << bit;
        else
            mismatch.high ^= 1ULL << (bit - 64);
        _Atomic(Wide) cell;
        __c11_atomic_store(&cell, pair_bits(before), __ATOMIC_RELAXED);
        Wide expected = pair_bits(mismatch);
        int changed = __c11_atomic_compare_exchange_strong(&cell, &expected, pair_bits(replacement),
                                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        failed |= changed || !pair_matches(expected, before);
        failed |= !pair_matches(__c11_atomic_load(&cell, __ATOMIC_RELAXED), before);
        changed = __c11_atomic_compare_exchange_strong(&cell, &expected, pair_bits(replacement),
                                                      __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        failed |= !changed || !pair_matches(expected, before);
        failed |= !pair_matches(__c11_atomic_load(&cell, __ATOMIC_ACQUIRE), replacement);

        // Equal low or high halves are insufficient. A weak mismatch must
        // update expected too; a weak match may legitimately fail spuriously.
        mismatch = replacement;
        if (bit < 64)
            mismatch.low ^= 1ULL << bit;
        else
            mismatch.high ^= 1ULL << (bit - 64);
        expected = pair_bits(mismatch);
        changed = __c11_atomic_compare_exchange_weak(&cell, &expected, pair_bits(before),
                                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        failed |= changed || !pair_matches(expected, replacement);
        failed |= !pair_matches(__c11_atomic_load(&cell, __ATOMIC_RELAXED), replacement);
        changed = __c11_atomic_compare_exchange_weak(&cell, &expected, pair_bits(before),
                                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
        failed |= !pair_matches(expected, replacement);
        if (!changed)
        {
            // Do not impose a stronger progress promise on weak CAS.
            failed |= !pair_matches(__c11_atomic_load(&cell, __ATOMIC_RELAXED), replacement);
            changed = __c11_atomic_compare_exchange_strong(&cell, &expected, pair_bits(before),
                                                           __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
        }
        failed |= !changed || !pair_matches(__c11_atomic_load(&cell, __ATOMIC_ACQUIRE), before);
    }
    return failed;
}

struct __attribute__((packed)) Nine
{
    Limb low;
    unsigned char high;
};

static int test_aggregate(Limb seed)
{
    _Atomic(struct Nine) cell;
    struct Nine first = {seed, 0x81};
    struct Nine second = {~seed, 0x7e};
    struct Nine expected = {0, 0};
    __c11_atomic_store(&cell, first, __ATOMIC_SEQ_CST);
    struct Nine old = __c11_atomic_exchange(&cell, second, __ATOMIC_ACQ_REL);
    int failed = old.low != first.low || old.high != first.high || sizeof(cell) != 16;
    int changed = __c11_atomic_compare_exchange_strong(&cell, &expected, first, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    failed |= changed || expected.low != second.low || expected.high != second.high;
    changed = __c11_atomic_compare_exchange_strong(&cell, &expected, first, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    failed |= !changed || expected.low != second.low || expected.high != second.high;
    old = __c11_atomic_load(&cell, __ATOMIC_RELAXED);
    failed |= old.low != first.low || old.high != first.high;
    unsigned char* bytes = (unsigned char*)&cell;
    for (unsigned int byte = 9; byte < sizeof(cell); byte += 1)
        failed |= bytes[byte] != 0;
    return failed;
}

static int test_signed_and_large_frame(Limb seed)
{
    volatile unsigned char frame[40000];
    frame[0] = (unsigned char)seed;
    frame[39999] = (unsigned char)(seed >> 8);
    Pair maximum = {~0ULL, 0x7fffffffffffffffULL};
    Pair minimum = {0, 0x8000000000000000ULL};
    _Atomic(SignedWide) cell;
    __c11_atomic_store(&cell, (SignedWide)pair_bits(maximum), __ATOMIC_RELAXED);
    SignedWide old = __c11_atomic_fetch_add(&cell, 1, __ATOMIC_SEQ_CST);
    int failed = !pair_matches((Wide)old, maximum);
    failed |= !pair_matches((Wide)__c11_atomic_load(&cell, __ATOMIC_RELAXED), minimum);
    old = __c11_atomic_fetch_sub(&cell, 1, __ATOMIC_SEQ_CST);
    failed |= !pair_matches((Wide)old, minimum);
    failed |= !pair_matches((Wide)__c11_atomic_load(&cell, __ATOMIC_RELAXED), maximum);
    SignedWide expected = (SignedWide)pair_bits(minimum);
    int changed = __c11_atomic_compare_exchange_strong(&cell, &expected, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    failed |= changed || !pair_matches((Wide)expected, maximum);
    changed = __c11_atomic_compare_exchange_strong(&cell, &expected, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    failed |= !changed || __c11_atomic_load(&cell, __ATOMIC_RELAXED) != 0;
    failed |= frame[0] != (unsigned char)seed || frame[39999] != (unsigned char)(seed >> 8);
    return failed;
}

int main(void)
{
    Limb seed = runtime_seed;
    int failed = test_updates(seed);
    failed |= test_compare_exchange(seed) << 1;
    failed |= test_aggregate(seed) << 2;
    failed |= test_signed_and_large_frame(seed) << 3;
    return failed;
}
