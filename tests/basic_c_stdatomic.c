#include <stdatomic.h>

static atomic_uint value = ATOMIC_VAR_INIT(4);
static atomic_flag flag = ATOMIC_FLAG_INIT;

int main(void)
{
    atomic_init(&value, 10);
    unsigned int previous = atomic_fetch_add(&value, 2);
    unsigned int expected = 12;
    int exchanged = atomic_compare_exchange_strong(
        &value,
        &expected,
        15);
    int was_set = atomic_flag_test_and_set(&flag);
    atomic_flag_clear(&flag);
    atomic_thread_fence(memory_order_acquire);
    atomic_signal_fence(memory_order_seq_cst);
    return !(
        previous == 10 &&
        exchanged &&
        expected == 12 &&
        atomic_load(&value) == 15 &&
        !was_set &&
        atomic_is_lock_free(&value) &&
        ATOMIC_INT_LOCK_FREE == 2 &&
        ATOMIC_POINTER_LOCK_FREE == 2);
}
