// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The deal of weighed items to bins that the parallel runner's shards (parallel_shards, --shard K/N) and
// test-preprocessor's parts (pp_test_deal, --part K/N) share, so that both deal alike: the heaviest item first (of two
// that weigh the same, the earlier), each to the bin whose items weigh the least so far (of those, the first). It
// depends on the weights and the bins alone, never on the machine, so that N processes that each run one bin's items
// run every item once. The runner's shards also have a deal that simulates each shard's schedule
// (test_deal_scheduled), for costs that name the runner's CPUs (#2031), and its memory where they name it too.
#ifndef BUSTER_TEST_DEAL_INCLUDED
#define BUSTER_TEST_DEAL_INCLUDED

#include <stdbool.h>
#include <stdint.h>

// Deals `count` items, item i weighing weights[i], to `bins` bins: item i's bin, from 0, goes in assigned[i]. `order`
// takes `count` entries and `loads` min(bins, count) of scratch. While fewer than `count` items have been dealt, one of
// the first `count` bins has none and so weighs the least, the first such weighing no more than any bin after it: no item
// goes past them, so the loads need room for those alone, however many bins are asked for.
static inline __attribute__((unused)) void test_deal(const uint64_t* weights, uint64_t count, uint64_t bins, uint64_t* order, uint64_t* loads, uint64_t* assigned)
{
    const uint64_t used = bins < count ? bins : count;
    for (uint64_t i = 0; i < count; i += 1)
    {
        // Insertion by weight, descending; equal weights keep the items' order.
        uint64_t at = i;
        while (at > 0 && weights[order[at - 1]] < weights[i])
        {
            order[at] = order[at - 1];
            at -= 1;
        }
        order[at] = i;
    }
    for (uint64_t b = 0; b < used; b += 1)
    {
        loads[b] = 0;
    }
    for (uint64_t k = 0; k < count; k += 1)
    {
        uint64_t lightest = 0;
        for (uint64_t b = 1; b < used; b += 1)
        {
            lightest = loads[b] < loads[lightest] ? b : lightest;
        }
        assigned[order[k]] = lightest;
        loads[lightest] += weights[order[k]];
    }
}

// Whether an item of `peak` memory may start beside `running` items that hold `used` of a `budget` (#2031): always
// alone, so that an item past the budget still runs; beside others, only within it. A budget of 0 is no limit, and an
// item of no known peak (0) holds none.
static inline __attribute__((unused)) bool test_deal_memory_fits(uint64_t peak, uint64_t used, uint64_t running, uint64_t budget)
{
    return budget == 0 || running == 0 || (used <= budget && peak <= budget - used);
}

// The time that bin `bin`'s items take as the parallel runner runs a shard (#2031): in their order (by index), each
// holding held[i] of `cpus` CPUs (at most `cpus`) and peaks[i] of `budget` memory for weights[i] / held[i], and
// starting as soon as the items running leave it its CPUs and its memory (test_deal_memory_fits); one that doesn't fit
// yet holds back the ones after it. Items whose bin is another, or none (UINT64_MAX), aren't run. `ends`, `holds` and
// `memories` take `cpus` entries of scratch: each running item holds a CPU at least. Without peaks (null), or with a
// budget of 0, memory doesn't bound the schedule.
static inline __attribute__((unused)) uint64_t test_deal_makespan(const uint64_t* weights, const uint64_t* held, const uint64_t* peaks, uint64_t count, const uint64_t* assigned, uint64_t bin, uint64_t cpus, uint64_t budget, uint64_t* ends, uint64_t* holds, uint64_t* memories)
{
    uint64_t running = 0;
    uint64_t free_cpus = cpus;
    uint64_t used = 0;
    uint64_t now = 0;
    uint64_t next = 0;
    while (next < count && assigned[next] != bin)
    {
        next += 1;
    }
    while (next < count || running != 0)
    {
        const uint64_t peak = next < count && peaks != 0 ? peaks[next] : 0;
        if (next < count && held[next] <= free_cpus && test_deal_memory_fits(peak, used, running, budget))
        {
            ends[running] = now + weights[next] / held[next];
            holds[running] = held[next];
            memories[running] = peak;
            running += 1;
            free_cpus -= held[next];
            used += peak;
            next += 1;
            while (next < count && assigned[next] != bin)
            {
                next += 1;
            }
        }
        else
        {
            // The earliest end, and every item that ends then gives its CPUs and its memory back.
            uint64_t earliest = ends[0];
            for (uint64_t i = 1; i < running; i += 1)
            {
                earliest = ends[i] < earliest ? ends[i] : earliest;
            }
            now = earliest;
            uint64_t kept = 0;
            for (uint64_t i = 0; i < running; i += 1)
            {
                free_cpus += ends[i] == earliest ? holds[i] : 0;
                used -= ends[i] == earliest ? memories[i] : 0;
                ends[kept] = ends[i];
                holds[kept] = holds[i];
                memories[kept] = memories[i];
                kept += ends[i] != earliest;
            }
            running = kept;
        }
    }
    return now;
}

// Deals as test_deal does, the heaviest item first (of two that weigh the same, the earlier), but each to the bin that,
// run as the runner runs a shard of `cpus` CPUs (test_deal_makespan), would end the earliest with it (of those, the one
// whose items weigh the least so far, then the first): the deal that minds a long item's start behind items that hold
// every CPU, which the weights alone don't see. Bin 0 ends `first` later than its items alone: the time of the steps
// that a row's shard 1 alone runs besides them (#2031), so that it gets less of the deal. held[i] is item i's CPUs, at most `cpus`, and weights[i] its time times
// them; peaks[i] its memory, of the runner's `budget` (null, or a budget of 0, for no memory bound). It depends on these
// and the bins alone, never on the machine. `order` and `assigned` take `count` entries, `loads` min(bins, count),
// `ends`, `holds` and `memories` `cpus`; at most min(bins, count) bins are used, as test_deal's.
static inline __attribute__((unused)) void test_deal_scheduled(const uint64_t* weights, const uint64_t* held, const uint64_t* peaks, uint64_t count, uint64_t bins, uint64_t cpus, uint64_t budget, uint64_t first, uint64_t* order, uint64_t* loads,
                                                               uint64_t* assigned, uint64_t* ends, uint64_t* holds, uint64_t* memories)
{
    const uint64_t used = bins < count ? bins : count;
    for (uint64_t i = 0; i < count; i += 1)
    {
        uint64_t at = i;
        while (at > 0 && weights[order[at - 1]] < weights[i])
        {
            order[at] = order[at - 1];
            at -= 1;
        }
        order[at] = i;
        assigned[i] = UINT64_MAX;
    }
    for (uint64_t b = 0; b < used; b += 1)
    {
        loads[b] = 0;
    }
    for (uint64_t k = 0; k < count; k += 1)
    {
        const uint64_t item = order[k];
        uint64_t best = 0;
        uint64_t best_time = UINT64_MAX;
        for (uint64_t b = 0; b < used; b += 1)
        {
            assigned[item] = b;
            const uint64_t time = test_deal_makespan(weights, held, peaks, count, assigned, b, cpus, budget, ends, holds, memories) + (b == 0 ? first : 0);
            const bool better = time < best_time || (time == best_time && loads[b] < loads[best]);
            best = better ? b : best;
            best_time = better ? time : best_time;
        }
        assigned[item] = best;
        loads[best] += weights[item];
    }
}

#endif
