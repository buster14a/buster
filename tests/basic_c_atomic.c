_Atomic(unsigned char) atomic_byte = 3;
_Atomic(unsigned short) atomic_half = 5;
_Atomic(unsigned int) atomic_word = 7;
_Atomic(unsigned long long) atomic_double = 11;

static int local_atomic_round_trip(void)
{
    _Atomic(unsigned int) value = 13;
    value = value + 4;
    return value == 17;
}

static int atomic_read_modify_write(void)
{
    _Atomic(unsigned int) value = 10;
    unsigned int previous = value++;
    unsigned int prefix = ++value;
    unsigned int assigned = (value += 2);
    value -= 4;
    value &= 14;
    value |= 1;
    value ^= 3;
    return previous == 10 &&
        prefix == 12 &&
        assigned == 14 &&
        value == 8;
}

static int atomic_builtins(void)
{
    _Atomic(unsigned int) value;
    __c11_atomic_init(&value, 20);
    unsigned int loaded = __c11_atomic_load(
        &value,
        __ATOMIC_RELAXED);
    __c11_atomic_store(
        &value,
        21,
        __ATOMIC_RELEASE);
    unsigned int added = __c11_atomic_fetch_add(
        &value,
        2,
        __ATOMIC_ACQUIRE);
    unsigned int subtracted =
        __c11_atomic_fetch_sub(
            &value,
            1,
            __ATOMIC_ACQ_REL);
    unsigned int masked = __c11_atomic_fetch_and(
        &value,
        30,
        __ATOMIC_SEQ_CST);
    unsigned int ored = __c11_atomic_fetch_or(
        &value,
        1,
        __ATOMIC_RELAXED);
    unsigned int xored = __c11_atomic_fetch_xor(
        &value,
        3,
        __ATOMIC_SEQ_CST);
    unsigned int exchanged = __c11_atomic_exchange(
        &value,
        30,
        __ATOMIC_ACQ_REL);
    unsigned int expected = 29;
    int failed = __c11_atomic_compare_exchange_strong(
        &value,
        &expected,
        31,
        __ATOMIC_ACQ_REL,
        __ATOMIC_ACQUIRE);
    int succeeded = __c11_atomic_compare_exchange_strong(
        &value,
        &expected,
        31,
        __ATOMIC_SEQ_CST,
        __ATOMIC_SEQ_CST);
    expected = 31;
    int weak_succeeded =
        __c11_atomic_compare_exchange_weak(
            &value,
            &expected,
            32,
            __ATOMIC_RELEASE,
            __ATOMIC_RELAXED);
    __c11_atomic_thread_fence(__ATOMIC_ACQUIRE);
    __c11_atomic_thread_fence(__ATOMIC_SEQ_CST);
    __c11_atomic_signal_fence(__ATOMIC_SEQ_CST);
    return loaded == 20 &&
        added == 21 &&
        subtracted == 23 &&
        masked == 22 &&
        ored == 22 &&
        xored == 23 &&
        exchanged == 20 &&
        !failed &&
        expected == 31 &&
        succeeded &&
        weak_succeeded &&
        __c11_atomic_is_lock_free(4) &&
        !__c11_atomic_is_lock_free(3) &&
        __c11_atomic_load(
            &value,
            __ATOMIC_SEQ_CST) == 32;
}

static int atomic_pointer_builtins(void)
{
    unsigned int first = 1;
    unsigned int second = 2;
    _Atomic(unsigned int*) pointer = &first;
    unsigned int* exchanged = __c11_atomic_exchange(
        &pointer,
        &second,
        __ATOMIC_SEQ_CST);
    unsigned int* expected = &first;
    int failed = __c11_atomic_compare_exchange_strong(
        &pointer,
        &expected,
        &first,
        __ATOMIC_ACQUIRE,
        __ATOMIC_RELAXED);
    int succeeded = __c11_atomic_compare_exchange_weak(
        &pointer,
        &expected,
        &first,
        __ATOMIC_RELEASE,
        __ATOMIC_RELAXED);
    unsigned int values[3] = {3, 5, 7};
    _Atomic(unsigned int*) cursor = values;
    unsigned int* previous = __c11_atomic_fetch_add(
        &cursor,
        2,
        __ATOMIC_RELAXED);
    unsigned int* subtracted = __c11_atomic_fetch_sub(
        &cursor,
        1,
        __ATOMIC_RELAXED);
    return exchanged == &first &&
        !failed &&
        expected == &second &&
        succeeded &&
        __c11_atomic_load(
            &pointer,
            __ATOMIC_RELAXED) == &first &&
        previous == values &&
        subtracted == values + 2 &&
        __c11_atomic_load(
            &cursor,
            __ATOMIC_RELAXED) == values + 1;
}

int main(void)
{
    atomic_byte = atomic_byte + 2;
    atomic_half = atomic_half + 2;
    atomic_word = atomic_word + 2;
    atomic_double = atomic_double + 2;
    return !(
        atomic_byte == 5 &&
        atomic_half == 7 &&
        atomic_word == 9 &&
        atomic_double == 13 &&
        local_atomic_round_trip() &&
        atomic_read_modify_write() &&
        atomic_builtins() &&
        atomic_pointer_builtins());
}
