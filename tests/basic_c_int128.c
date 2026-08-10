typedef unsigned __int128 WideUnsigned;
typedef __int128 WideSigned;

static volatile unsigned long long seed = 1;
static WideUnsigned constant_high = ((WideUnsigned)1 << 100) | 11;
static _Atomic(WideUnsigned) atomic_wide;

static int test_atomic_wide(WideUnsigned high)
{
    WideUnsigned initial = high | 0x55;
    __c11_atomic_store(&atomic_wide, initial, __ATOMIC_SEQ_CST);
    if (__c11_atomic_load(&atomic_wide, __ATOMIC_ACQUIRE) != initial)
        return 0;
    if (__c11_atomic_fetch_add(&atomic_wide, 3, __ATOMIC_ACQ_REL) != initial)
        return 0;
    if (__c11_atomic_fetch_sub(&atomic_wide, 1, __ATOMIC_RELAXED) != initial + 3)
        return 0;
    WideUnsigned value = initial + 2;
    if (__c11_atomic_fetch_and(&atomic_wide, high | 0x5f, __ATOMIC_SEQ_CST) != value)
        return 0;
    value &= high | 0x5f;
    if (__c11_atomic_fetch_or(&atomic_wide, 0x20, __ATOMIC_RELEASE) != value)
        return 0;
    value |= 0x20;
    if (__c11_atomic_fetch_xor(&atomic_wide, high | 3, __ATOMIC_ACQUIRE) != value)
        return 0;
    value ^= high | 3;
    if (__c11_atomic_exchange(&atomic_wide, initial, __ATOMIC_SEQ_CST) != value)
        return 0;
    WideUnsigned expected = 0;
    if (__c11_atomic_compare_exchange_strong(&atomic_wide, &expected, high | 9, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) || expected != initial)
        return 0;
    if (!__c11_atomic_compare_exchange_weak(&atomic_wide, &expected, high | 9, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        return 0;
    return __c11_atomic_load(&atomic_wide, __ATOMIC_RELAXED) == (high | 9);
}

int main(void)
{
    WideUnsigned one = seed;
    if ((unsigned long long)(constant_high >> 100) != 1 || (unsigned long long)constant_high != 11)
        return 8;
    WideUnsigned high = one << 64;
    if ((unsigned long long)(high >> 64) != 1 || (unsigned long long)high != 0)
        return 1;
    WideUnsigned carry = (high - one) + one;
    if (carry != high)
        return 2;
    WideUnsigned product = (high + 3) * 3;
    if ((unsigned long long)(product >> 64) != 3 || (unsigned long long)product != 9)
        return 3;
    if (!(high > one) || !(one < high) || high == one || high != carry)
        return 4;
    WideUnsigned bits = (high | 7) ^ 2;
    if ((unsigned long long)(bits >> 64) != 1 || (unsigned long long)bits != 5)
        return 5;
    WideSigned negative = -(WideSigned)(high + one);
    if (!(negative < 0) || !((WideUnsigned)(~negative) == high))
        return 6;
    if ((unsigned long long)(((WideSigned)-2) >> 1) != ~0ULL)
        return 7;
    WideUnsigned dividend = (high << 17) + 12345;
    WideUnsigned quotient = dividend / 37;
    WideUnsigned remainder = dividend % 37;
    if (quotient * 37 + remainder != dividend || remainder >= 37)
        return 9;
    WideSigned signed_dividend = -(WideSigned)dividend;
    WideSigned signed_quotient = signed_dividend / 37;
    WideSigned signed_remainder = signed_dividend % 37;
    if (signed_quotient * 37 + signed_remainder != signed_dividend || signed_remainder > 0)
        return 10;
    if (!test_atomic_wide(high))
        return 11;
    return 0;
}
