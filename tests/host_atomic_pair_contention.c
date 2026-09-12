// POSIX host-only harness: independent C11 atomics contend with Buster-generated
// operations. A process alarm makes a broken retry/progress path fail boundedly.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "atomic_pair_contention.h"

enum { THREADS = 4, UPDATES = 4096, MESSAGES = 2048, SC_ROUNDS = 512 };
static _Atomic(AtomicPairWide) cell;
static _Atomic(unsigned int) ready;
static _Atomic(unsigned int) start;
static _Atomic(unsigned int) failed;
static _Atomic(unsigned char) tickets[THREADS * UPDATES];
static AtomicPairMailbox mailbox;
static _Atomic(AtomicPairWide) sc_cells[2];
static _Atomic(unsigned int) sc_epoch;
static _Atomic(unsigned int) sc_done;
static AtomicPairLimb sc_observed[2];

typedef struct Worker Worker;
struct Worker { unsigned int id; unsigned int phase; };

static void* worker(void* opaque)
{
    Worker* work = opaque;
    unsigned int id = work->id;
    atomic_fetch_add_explicit(&ready, 1, memory_order_release);
    while (!atomic_load_explicit(&start, memory_order_acquire))
    {
    }
    if (work->phase == 0)
    {
        AtomicPairLimb first = ~0ULL - 1023;
        for (unsigned int index = 0; index < UPDATES; index += 1)
        {
            AtomicPairLimb low;
            AtomicPairLimb high;
            if (id & 1)
            {
                AtomicPairWide old = atomic_fetch_add_explicit(&cell, 1, memory_order_relaxed);
                low = (AtomicPairLimb)old;
                high = (AtomicPairLimb)(old >> 64);
            }
            else
            {
                atomic_pair_increment(&cell, &low, &high);
            }
            AtomicPairLimb ticket = low - first;
            if (ticket >= THREADS * UPDATES || high != 7 + (low < first))
            {
                atomic_store_explicit(&failed, 1, memory_order_relaxed);
            }
            else if (atomic_fetch_add_explicit(tickets + ticket, 1, memory_order_relaxed) != 0)
            {
                atomic_store_explicit(&failed, 1, memory_order_relaxed);
            }
        }
    }
    else if (work->phase == 1)
    {
        for (unsigned int index = 0; index < UPDATES * 4; index += 1)
        {
            AtomicPairLimb value = ((AtomicPairLimb)id << 32) | index;
            if (id == 0)
            {
                atomic_pair_replace(&cell, value);
            }
            else if (id == 1)
            {
                atomic_store_explicit(&cell, (AtomicPairWide)value | ((AtomicPairWide)~value << 64), memory_order_release);
            }
            else
            {
                AtomicPairLimb low;
                AtomicPairLimb high;
                int changed;
                if (id == 2)
                {
                    changed = atomic_pair_mismatch(&cell, &low, &high);
                }
                else
                {
                    AtomicPairWide expected = 0;
                    changed = atomic_compare_exchange_strong_explicit(&cell, &expected, 0, memory_order_acquire, memory_order_acquire);
                    low = (AtomicPairLimb)expected;
                    high = (AtomicPairLimb)(expected >> 64);
                }
                if (changed || high != ~low)
                    atomic_store_explicit(&failed, 1, memory_order_relaxed);
            }
        }
    }
    else if (work->phase == 2 || work->phase == 3)
    {
        for (AtomicPairLimb index = 1; index <= MESSAGES; index += 1)
        {
            if (id == 0)
            {
                if (work->phase == 2)
                {
                    atomic_pair_publish(&mailbox, index);
                }
                else
                {
                    while (atomic_load_explicit(&mailbox.token, memory_order_acquire) != 0)
                    {
                    }
                    mailbox.first = index;
                    mailbox.second = ~index;
                    atomic_store_explicit(&mailbox.token, (AtomicPairWide)1 | ((AtomicPairWide)1 << 64), memory_order_release);
                }
            }
            else if (work->phase == 3)
            {
                if (atomic_pair_consume(&mailbox, index))
                    atomic_store_explicit(&failed, 1, memory_order_relaxed);
            }
            else
            {
                AtomicPairWide expected = 0;
                while (atomic_compare_exchange_strong_explicit(&mailbox.token, &expected, 0, memory_order_acquire, memory_order_acquire))
                {
                    expected = 0;
                }
                if (expected != ((AtomicPairWide)1 | ((AtomicPairWide)1 << 64)) || mailbox.first != index || mailbox.second != ~index)
                    atomic_store_explicit(&failed, 1, memory_order_relaxed);
                atomic_store_explicit(&mailbox.token, 0, memory_order_release);
            }
        }
    }
    else
    {
        for (unsigned int round = 1; round <= SC_ROUNDS; round += 1)
        {
            while (atomic_load_explicit(&sc_epoch, memory_order_acquire) < round)
            {
            }
            atomic_pair_sc(sc_cells + id, sc_cells + (1 - id), sc_observed + id);
            atomic_fetch_add_explicit(&sc_done, 1, memory_order_release);
        }
    }
    return 0;
}

int main(void)
{
    alarm(30);
    for (unsigned int phase = 0; phase < 5; phase += 1)
    {
        pthread_t threads[THREADS];
        Worker work[THREADS];
        unsigned int count = phase < 2 ? THREADS : 2;
        atomic_store(&ready, 0);
        atomic_store(&start, 0);
        AtomicPairWide initial = phase == 0 ? (AtomicPairWide)(~0ULL - 1023) | ((AtomicPairWide)7 << 64) : ((AtomicPairWide)~0ULL << 64);
        atomic_store(&cell, initial);
        for (unsigned int id = 0; id < count; id += 1)
        {
            work[id] = (Worker){id, phase};
            if (pthread_create(threads + id, 0, worker, work + id) != 0)
                _Exit(20);
        }
        while (atomic_load_explicit(&ready, memory_order_acquire) != count)
        {
        }
        atomic_store_explicit(&start, 1, memory_order_release);
        if (phase == 4)
        {
            for (unsigned int round = 1; round <= SC_ROUNDS; round += 1)
            {
                atomic_store(&sc_cells[0], 0);
                atomic_store(&sc_cells[1], 0);
                atomic_store(&sc_done, 0);
                atomic_store_explicit(&sc_epoch, round, memory_order_release);
                while (atomic_load_explicit(&sc_done, memory_order_acquire) != 2)
                {
                }
                if (sc_observed[0] == 0 && sc_observed[1] == 0)
                    atomic_store(&failed, 1);
            }
        }
        for (unsigned int id = 0; id < count; id += 1)
        {
            if (pthread_join(threads[id], 0) != 0)
                _Exit(21);
        }
        if (phase == 0)
        {
            if (atomic_load(&cell) != initial + THREADS * UPDATES)
                atomic_store(&failed, 1);
            for (unsigned int index = 0; index < THREADS * UPDATES; index += 1)
            {
                if (atomic_load(tickets + index) != 1)
                    atomic_store(&failed, 1);
            }
        }
    }
    int status = atomic_load(&failed) != 0;
    printf("ATOMIC_PAIR_CONTENTION:%s\n", status ? "FAIL" : "PASS");
    return status;
}
