// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The deal of weighed items to bins that the parallel runner's shards (parallel_shards, --shard K/N) and
// test-preprocessor's parts (pp_test_deal, --part K/N) share, so that both deal alike: the heaviest item first (of two
// that weigh the same, the earlier), each to the bin whose items weigh the least so far (of those, the first). It
// depends on the weights and the bins alone, never on the machine, so that N processes that each run one bin's items
// run every item once.
#ifndef BUSTER_TEST_DEAL_INCLUDED
#define BUSTER_TEST_DEAL_INCLUDED

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

#endif
