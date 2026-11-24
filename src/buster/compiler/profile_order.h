/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
#ifndef BUSTER_PROFILE_ORDER_H
#define BUSTER_PROFILE_ORDER_H

#include <stdint.h>

// The tree that run `position` of a round runs (#1311): round r takes row r of a Williams design, a Latin square
// whose rows each run every tree once, and over its period every tree runs at every place and directly after every
// other tree equally often. A rotation ran the trees in one cyclic order from different starts, so each tree followed
// the same one in every round, and a process starts on the caches and branch predictors the one before it left. The
// first row is 0, 1, n - 1, 2, n - 2, ..., whose steps from one place to the next are each of 1 to n - 1 once; row r
// adds r to each, modulo n. With an even count the n rows are the period, each tree following each other once; with
// an odd count the n rows and then each of them reversed, 2n rows, each following each other twice. Two trees still
// alternate, and three take the rotation's first three rounds.
static uint64_t profile_ab_tree(uint64_t round, uint64_t position, uint64_t tree_count)
{
    const uint64_t period = tree_count % 2 == 0 ? tree_count : 2 * tree_count;
    const uint64_t row = round % period;
    const uint64_t place = row < tree_count ? position : tree_count - 1 - position;
    const uint64_t first = place == 0 ? 0 : place % 2 == 1 ? (place + 1) / 2 : tree_count - place / 2;
    return (first + row) % tree_count;
}

#endif
