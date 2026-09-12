#include "atomic_pair_contention.h"

#if !defined(__clang__) && !defined(__BUSTER__)
#include <stdatomic.h>
#define __c11_atomic_load(p, o) atomic_load_explicit(p, o)
#define __c11_atomic_fetch_add(p, v, o) atomic_fetch_add_explicit(p, v, o)
#define __c11_atomic_exchange(p, v, o) atomic_exchange_explicit(p, v, o)
#define __c11_atomic_compare_exchange_strong(p, e, v, s, f) atomic_compare_exchange_strong_explicit(p, e, v, s, f)
#endif

// This subject is compiled separately by Buster and by the independent host
// compiler. Scalar/pointer signatures keep the test about shared atomic objects.
void atomic_pair_increment(_Atomic(AtomicPairWide)* cell, AtomicPairLimb* low, AtomicPairLimb* high)
{
    AtomicPairWide old = __c11_atomic_fetch_add(cell, 1, __ATOMIC_RELAXED);
    *low = (AtomicPairLimb)old;
    *high = (AtomicPairLimb)(old >> 64);
}

void atomic_pair_replace(_Atomic(AtomicPairWide)* cell, AtomicPairLimb value)
{
    AtomicPairWide bits = (AtomicPairWide)value | ((AtomicPairWide)~value << 64);
    (void)__c11_atomic_exchange(cell, bits, __ATOMIC_RELEASE);
}

int atomic_pair_mismatch(_Atomic(AtomicPairWide)* cell, AtomicPairLimb* low, AtomicPairLimb* high)
{
    // The host writers maintain high == ~low, so zero can never compare equal.
    AtomicPairWide expected = 0;
    int changed = __c11_atomic_compare_exchange_strong(cell, &expected, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
    *low = (AtomicPairLimb)expected;
    *high = (AtomicPairLimb)(expected >> 64);
    return changed;
}

void atomic_pair_publish(AtomicPairMailbox* box, AtomicPairLimb value)
{
    while (__c11_atomic_load(&box->token, __ATOMIC_ACQUIRE) != 0)
    {
    }
    box->first = value;
    box->second = ~value;
    (void)__c11_atomic_exchange(&box->token, (AtomicPairWide)1 | ((AtomicPairWide)1 << 64), __ATOMIC_RELEASE);
}

int atomic_pair_consume(AtomicPairMailbox* box, AtomicPairLimb value)
{
    AtomicPairWide expected = 0;
    // Observing ready is a *failed* acquire CAS. The payload is non-atomic;
    // the release publication and acquire failure must order these reads.
    while (__c11_atomic_compare_exchange_strong(&box->token, &expected, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
    {
        expected = 0;
    }
    int failed = expected != ((AtomicPairWide)1 | ((AtomicPairWide)1 << 64)) || box->first != value || box->second != ~value;
    (void)__c11_atomic_exchange(&box->token, 0, __ATOMIC_RELEASE);
    return failed;
}

void atomic_pair_sc(_Atomic(AtomicPairWide)* own, _Atomic(AtomicPairWide)* other, AtomicPairLimb* observed)
{
    (void)__c11_atomic_exchange(own, 1, __ATOMIC_SEQ_CST);
    *observed = (AtomicPairLimb)__c11_atomic_load(other, __ATOMIC_SEQ_CST);
}
