#pragma once

// Shared test-only ABI. Only the host harness owns threads and timing.
typedef unsigned long long AtomicPairLimb;
typedef unsigned __int128 AtomicPairWide;
typedef struct AtomicPairMailbox AtomicPairMailbox;
struct AtomicPairMailbox
{
    _Atomic(AtomicPairWide) token;
    AtomicPairLimb first;
    AtomicPairLimb second;
};

void atomic_pair_increment(_Atomic(AtomicPairWide)* cell, AtomicPairLimb* low, AtomicPairLimb* high);
void atomic_pair_replace(_Atomic(AtomicPairWide)* cell, AtomicPairLimb value);
int atomic_pair_mismatch(_Atomic(AtomicPairWide)* cell, AtomicPairLimb* low, AtomicPairLimb* high);
void atomic_pair_publish(AtomicPairMailbox* box, AtomicPairLimb value);
int atomic_pair_consume(AtomicPairMailbox* box, AtomicPairLimb value);
void atomic_pair_sc(_Atomic(AtomicPairWide)* own, _Atomic(AtomicPairWide)* other, AtomicPairLimb* observed);
