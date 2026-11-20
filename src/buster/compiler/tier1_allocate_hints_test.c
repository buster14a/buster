// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Value hints (Tier1AllocationInput.hint_values, #140, and hint_intervals, #1216): literal traces of two values that
// end and restart at one position, which the level rule alone gives each other's registers, of a range that takes an
// earlier one's opening register over its own value's last, and of one that takes an earlier one's closing register
// (BUSTER_TIER1_ALLOCATE_HINT_EXIT, #2553) after the scan moved it; and random inputs on tier1_allocate and the scan
// reference against the interval verifier, the position occupancy oracle and an independent reference of the common
// path's hint rule; traces of a recipe that gives its register to a value its hint names (#2618); traces of a range
// that waits in its value's slot for its next required use (#2768); and traces of a victim that continues in its slot
// and of an interval evicted where it starts, whose opening is its slot (#2774), whose random inputs take continuations
// too. Included by tier1_allocate_test.c.

// The common path's rule with hints, written over positions as tier1_allocation_test_levels writes it, with the free
// registers as a set per level: a start whose hinted register is free takes it, from the level above that holds it
// (which then holds what the level rule would have taken) or as a fresh one (which makes a register taken from this
// level fresh again). The hinted register is the hinted interval's opening one (`openings`, per interval) or, named by
// its exit, its closing one (`closings`, which the common path's never moving intervals make their registers), then the
// own value's last, then the hinted value's. False when the class doesn't fit.
static bool tier1_allocation_test_hinted_levels(const Tier1AllocationInput* input, uint32_t class, uint8_t* regs, uint8_t* last, uint8_t* openings, uint8_t* closings)
{
    uint8_t levels[33];
    memset(levels, UINT8_MAX, sizeof(levels));
    uint32_t fresh = input->registers[class];
    const uint32_t capacity = (uint32_t)__builtin_popcount(fresh);
    uint32_t height = 0;
    bool fits = true;
    for (uint32_t p = 0; fits && p <= input->position_count; p += 1)
    {
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            if (input->classes[i] == class && input->ends[i] == p)
            {
                height -= 1;
                levels[height] = regs[i];
                closings[i] = regs[i];
            }
        }
        for (uint32_t i = 0; fits && i < input->count; i += 1)
        {
            if (input->classes[i] == class && input->starts[i] == p)
            {
                fits = height < capacity;
                if (fits)
                {
                    uint8_t r = levels[height];
                    const bool waiting = r != UINT8_MAX;
                    if (!waiting)
                    {
                        r = (uint8_t)__builtin_ctz(fresh);
                    }
                    const uint32_t hinted = input->hint_values[i];
                    // A hinted interval's own value's register from an earlier interval first, then the hinted value's.
                    const uint8_t own = hinted != UINT32_MAX ? last[input->values[i]] : UINT8_MAX;
                    const uint32_t earlier = input->hint_intervals != 0 ? input->hint_intervals[i] : UINT32_MAX;
                    const bool exit = earlier != UINT32_MAX && (earlier & BUSTER_TIER1_ALLOCATE_HINT_EXIT) != 0;
                    const uint8_t opening = earlier == UINT32_MAX ? UINT8_MAX : exit ? closings[earlier & ~BUSTER_TIER1_ALLOCATE_HINT_EXIT] : openings[earlier];
                    // An alternate interval's opening register before them, where its value holds it last and it is free:
                    // fresh, or deposited at this level or one above (#2734).
                    const uint32_t alternate = input->alternate_intervals != 0 ? input->alternate_intervals[i] : UINT32_MAX;
                    const uint8_t opened = alternate != UINT32_MAX ? openings[alternate] : UINT8_MAX;
                    const bool settled = opened < 32 && last[input->values[alternate]] == opened;
                    bool spare = settled && ((fresh >> opened) & 1) != 0;
                    for (uint32_t k = height; settled && k < 33; k += 1)
                    {
                        spare = spare || levels[k] == opened;
                    }
                    const uint8_t follow = spare ? opened : opening != UINT8_MAX ? opening : own != UINT8_MAX ? own : hinted != UINT32_MAX ? last[hinted] : UINT8_MAX;
                    uint32_t holder = UINT32_MAX;
                    for (uint32_t k = height + 1; follow != UINT8_MAX && k < 33; k += 1)
                    {
                        holder = levels[k] == follow ? k : holder;
                    }
                    const bool fresh_hint = follow != UINT8_MAX && ((fresh >> follow) & 1) != 0;
                    if (follow != UINT8_MAX && follow != r && holder != UINT32_MAX)
                    {
                        levels[holder] = r;
                        fresh &= waiting ? fresh : ~(1u << r);
                        r = follow;
                    }
                    else if (follow != UINT8_MAX && follow != r && fresh_hint)
                    {
                        fresh = (fresh & ~(1u << follow)) | (waiting ? 1u << r : 0);
                        r = follow;
                    }
                    else if (!waiting)
                    {
                        fresh &= fresh - 1;
                    }
                    levels[height] = UINT8_MAX;
                    regs[i] = r;
                    last[input->values[i]] = r;
                    openings[i] = r;
                    height += 1;
                }
            }
        }
    }
    return fits;
}

// An unused recipe can preserve a hoisted definition through quiet blocks, but cannot displace a loop value.
// Literal traces cover spare registers, another start's claim despite a conflicting physical hint, no free register,
// eviction by a later required value before any use, which leaves the range recomputed from its start (#2618), and a
// stored value's unused continuation. Restricted masks force the scan on
// the production path, so the regular-class coloring path cannot hide a regression in the required-use policy.
static void tier1_allocation_test_idle(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[][5] = {
        { 0, 4, 8, 12 },
        { 0, 1, 4, 4, 8 },
        { 0, 4, 8 },
        { 0, 4, 6, 12 },
        { 0, 1, 4, 8 },
    };
    const uint32_t ends[][5] = {
        { 4, 8, 12, 16 },
        { 4, 4, 8, 8, 12 },
        { 8, 8, 16 },
        { 4, 12, 10, 16 },
        { 4, 4, 8, 12 },
    };
    const uint32_t values[][5] = {
        { 0, 0, 0, 0 },
        { 0, 1, 1, 0, 1 },
        { 0, 1, 1 },
        { 0, 0, 1, 0 },
        { 0, 1, 0, 0 },
    };
    const uint32_t allowed[][5] = {
        { 1, 1, 1, 1 },
        { 3, 3, 3, 3, 3 },
        { 1, 1, 1 },
        { 1, 1, 1, 1 },
        { 1, 1, 1, 1 },
    };
    const uint8_t rematerializable[][5] = {
        { 1, 1, 1, 1 },
        { 0, 1, 1, 0, 1 },
        { 1, 1, 1 },
        { 1, 1, 0, 1 },
        { 0, 0, 0, 0 },
    };
    const uint32_t use_offsets[][6] = {
        { 0, 1, 1, 1, 2 },
        { 0, 1, 2, 2, 3, 4 },
        { 0, 1, 1, 3 },
        { 0, 1, 1, 2, 3 },
        { 0, 1, 2, 2, 3 },
    };
    const uint32_t use_positions[][4] = {
        { 0, 14 },
        { 0, 1, 6, 10 },
        { 0, 8, 14 },
        { 0, 6, 12 },
        { 0, 1, 8 },
    };
    const uint8_t expected_kinds[][5] = {
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER },
    };
    const uint8_t expected_registers[][5] = {
        { 0, 0, 0, 0 },
        { 0, 1, 1, 0, 1 },
        { 0, UINT8_MAX, 0 },
        { 0, UINT8_MAX, 0, 0 },
        { 0, UINT8_MAX, 0, UINT8_MAX, 0 },
    };
    const uint32_t counts[] = { 4, 5, 3, 4, 4 };
    const uint32_t fragments[] = { 4, 5, 3, 4, 5 };
    const uint8_t classes[5] = { 0 };
    const uint8_t required[4] = { 1, 1, 1, 1 };
    const uint8_t hints[5] = { UINT8_MAX, UINT8_MAX, 0, UINT8_MAX, UINT8_MAX };
    const char* names[] = {
        "an unused recipe preserves its required definition through two quiet ranges",
        "an unused recipe's physical hint cannot take another start's claimed register",
        "an unused recipe with no free register never evicts an active value",
        "a recipe kept through a quiet range, evicted before any use, gives its register up from the range's start (#2618)",
        "an unused stored value stays in its slot despite a free register",
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(counts); fixture += 1)
    {
        const Tier1AllocationInput input = {
            .starts = starts[fixture],
            .ends = ends[fixture],
            .values = values[fixture],
            .allowed_registers = allowed[fixture],
            .classes = classes,
            .hints = fixture == 1 ? hints : 0,
            .hint_values = values[fixture],
            .rematerializable = rematerializable[fixture],
            .use_offsets = use_offsets[fixture],
            .use_positions = use_positions[fixture],
            .use_required = required,
            .use_count = use_offsets[fixture][counts[fixture]],
            .count = counts[fixture],
            .value_count = fixture == 0 ? 1 : 2,
            .position_count = 16,
            .registers = { 7 },
        };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const uint64_t saved = arena->length;
            Tier1Allocation output = { 0 };
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
            const bool valid = error == TIER1_ALLOCATION_OK && output.scalar_intervals == input.count && output.fragment_count == fragments[fixture] && output.splits == fragments[fixture] - input.count && output.spill_count == (fixture == 4 ? 1u : 0u) && tier1_allocation_verify(&input, &output) && tier1_allocation_test_occupancy(&input, &output) && memcmp(output.kinds, expected_kinds[fixture], fragments[fixture]) == 0 && memcmp(output.registers, expected_registers[fixture], fragments[fixture]) == 0;
            tier1_allocation_test_expect(tally, valid, names[fixture]);
            arena_pop_to(arena, saved);
        }
    }
}

// A value without a recipe takes its hinted register from a recipe that no use requires there (#2618): value 0 leaves r0
// at 4 and starts again at 8, where its hint names r0, which recipe value 1, defined at 5, holds. In the first trace the
// recipe leaves r0 at 8, recomputed into r1 for its required use at 11; in the second its use at 8 requires it there, so
// it keeps r0 and value 0 takes r1. The masks keep the production path on the scan.
static void tier1_allocation_test_yields(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[3] = { 0, 5, 8 };
    const uint32_t ends[3] = { 4, 12, 12 };
    const uint32_t values[3] = { 0, 1, 0 };
    const uint32_t allowed[3] = { 3, 3, 3 };
    const uint8_t classes[3] = { 0 };
    const uint8_t rematerializable[3] = { 0, 1, 0 };
    const uint32_t use_offsets[][4] = {
        { 0, 1, 3, 4 },
        { 0, 1, 4, 5 },
    };
    const uint32_t use_positions[][5] = {
        { 0, 5, 11, 10 },
        { 0, 5, 8, 11, 10 },
    };
    const uint8_t required[5] = { 1, 1, 1, 1, 1 };
    const uint32_t fragments[] = { 5, 3 };
    const uint8_t expected_kinds[][5] = {
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
    };
    const uint8_t expected_registers[][5] = {
        { 0, 0, UINT8_MAX, 1, 0 },
        { 0, 0, 1 },
    };
    const uint32_t expected_starts[][5] = {
        { 0, 5, 8, 11, 8 },
        { 0, 5, 8 },
    };
    const char* names[] = {
        "a recipe gives a value without one the register its hint names",
        "a recipe a use requires keeps its register from a hinted value",
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(names); fixture += 1)
    {
        const Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = values,
            .rematerializable = rematerializable,
            .use_offsets = use_offsets[fixture],
            .use_positions = use_positions[fixture],
            .use_required = required,
            .use_count = use_offsets[fixture][3],
            .count = 3,
            .value_count = 2,
            .position_count = 12,
            .registers = { 7 },
        };
        Tier1Allocation outputs[2] = { { 0 } };
        const uint64_t saved = arena->length;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &outputs[path]) : tier1_allocate_scan_reference(&input, arena, &outputs[path]);
            const Tier1Allocation* output = &outputs[path];
            bool valid = error == TIER1_ALLOCATION_OK && output->fragment_count == fragments[fixture] && output->spill_count == 0 && tier1_allocation_verify(&input, output) && tier1_allocation_test_occupancy(&input, output);
            valid = valid && memcmp(output->kinds, expected_kinds[fixture], fragments[fixture]) == 0 && memcmp(output->registers, expected_registers[fixture], fragments[fixture]) == 0;
            valid = valid && memcmp(output->starts, expected_starts[fixture], fragments[fixture] * sizeof(uint32_t)) == 0;
            tier1_allocation_test_expect(tally, valid, names[fixture]);
            if (!valid && error == TIER1_ALLOCATION_OK)
            {
                fprintf(stderr, "  path %u: %u fragments:", path, output->fragment_count);
                for (uint32_t f = 0; f < output->fragment_count; f += 1)
                {
                    fprintf(stderr, " %u@%u/r%u", output->kinds[f], output->starts[f], output->registers[f]);
                }
                fprintf(stderr, "\n");
            }
        }
        arena_pop_to(arena, saved);
    }
}

// A recipe evicted before any use read its register gives the register up from its start (#2618): with one register,
// recipe value 0 starts at 0 and is next read at 8; value 1, without a recipe, starts at 2 and takes r0. In the first
// trace value 0 is recomputed from 0, its opening the memory hint, so that an unused range of it at 10, hinted to that
// opening, rests without a register; in the second a use at 1 read r0 first, so it keeps r0 until 2, and the unused
// range at 10 takes r0, free there. In the third a call's clobber at 2 takes r0 from value 0: it keeps its register
// fragment there, as the dispatch loop's tables need (tier1_allocation_evict). Value 1 ends at 6, and value 0 takes r0
// again at its use at 8.
static void tier1_allocation_test_unread(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[3] = { 0, 2, 10 };
    const uint32_t ends[3] = { 10, 6, 14 };
    const uint32_t values[3] = { 0, 1, 0 };
    const uint32_t allowed[3] = { 1, 1, 1 };
    const uint8_t classes[3] = { 0 };
    const uint8_t rematerializable[3] = { 1, 0, 1 };
    const uint32_t hint_intervals[3] = { UINT32_MAX, UINT32_MAX, 0 };
    const uint32_t use_offsets[][4] = {
        { 0, 1, 2, 2 },
        { 0, 2, 3, 3 },
        { 0, 1, 2, 2 },
    };
    const uint32_t use_positions[][3] = {
        { 8, 2 },
        { 1, 8, 2 },
        { 8, 2 },
    };
    const uint32_t clobbers[1] = { 2 };
    const uint8_t required[3] = { 1, 1, 1 };
    const uint32_t fragments[] = { 4, 5, 5 };
    const uint8_t expected_kinds[][5] = {
        { BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
    };
    const uint8_t expected_registers[][5] = {
        { UINT8_MAX, 0, 0, UINT8_MAX },
        { 0, UINT8_MAX, 0, 0, 0 },
        { 0, UINT8_MAX, 0, 0, 0 },
    };
    const uint32_t expected_starts[][5] = {
        { 0, 8, 2, 10 },
        { 0, 2, 8, 2, 10 },
        { 0, 2, 8, 2, 10 },
    };
    const char* names[] = {
        "a recipe evicted before any use read it is recomputed from its start, and a range hinted to it rests",
        "a recipe a use read keeps its register until its eviction",
        "a call's clobber keeps a recipe's register fragment before it",
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(names); fixture += 1)
    {
        const Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = values,
            .hint_intervals = hint_intervals,
            .rematerializable = rematerializable,
            .use_offsets = use_offsets[fixture],
            .use_positions = use_positions[fixture],
            .use_required = required,
            .use_count = use_offsets[fixture][3],
            .clobbers = fixture == 2 ? clobbers : 0,
            .clobber_count = fixture == 2 ? 1 : 0,
            .clobber_registers = { fixture == 2 ? 1u : 0u },
            .count = 3,
            .value_count = 2,
            .position_count = 14,
            .registers = { 1 },
        };
        Tier1Allocation outputs[2] = { { 0 } };
        const uint64_t saved = arena->length;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &outputs[path]) : tier1_allocate_scan_reference(&input, arena, &outputs[path]);
            const Tier1Allocation* output = &outputs[path];
            bool valid = error == TIER1_ALLOCATION_OK && output->fragment_count == fragments[fixture] && output->spill_count == 0 && tier1_allocation_verify(&input, output) && tier1_allocation_test_occupancy(&input, output);
            valid = valid && memcmp(output->kinds, expected_kinds[fixture], fragments[fixture]) == 0 && memcmp(output->registers, expected_registers[fixture], fragments[fixture]) == 0;
            valid = valid && memcmp(output->starts, expected_starts[fixture], fragments[fixture] * sizeof(uint32_t)) == 0;
            tier1_allocation_test_expect(tally, valid, names[fixture]);
            if (!valid && error == TIER1_ALLOCATION_OK)
            {
                fprintf(stderr, "  path %u: %u fragments:", path, output->fragment_count);
                for (uint32_t f = 0; f < output->fragment_count; f += 1)
                {
                    fprintf(stderr, " %u@%u/r%u", output->kinds[f], output->starts[f], output->registers[f]);
                }
                fprintf(stderr, "\n");
            }
        }
        arena_pop_to(arena, saved);
    }
}

// A range that continues its value where the value was last in its slot waits there for its next required use, unless the
// value's first register is free at its start (#2768). With r0 and r1, b [0, 5) takes r0 and a [1, 6) r1, a's first
// register; c starts at 2 and evicts a, whose next use is farthest, into its slot. a's next range starts at 6, hinted to
// its own value and to the closing of a's first range, its slot:
// - c holds r1 there until 8: the range waits in a's slot until its required use at 16, which reloads a into r1, its
//   first register, though r0 is free too (before, it took r0 at 6, the lowest free register);
// - c ends at 5 instead: r1 is free at 6, and the range takes it there;
// - hinted to b, as a phi's range is to its operand, the range takes r0, free at 6, as before;
// - required at 6, it takes r0 there too;
// - a range of another value d, hinted to its own value but to the closing of a's first range, as a loop's body before
//   its header is to its phi's operand (#2784): a's slot is no place of d's, and d takes r0 at 6.
// The masks keep the production path on the scan.
static void tier1_allocation_test_slot_waits(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[4] = { 0, 1, 2, 6 };
    const uint32_t ends[][4] = {
        { 5, 6, 8, 20 },
        { 5, 6, 5, 20 },
        { 5, 6, 8, 20 },
        { 5, 6, 8, 20 },
        { 5, 6, 8, 20 },
    };
    const uint32_t values[][4] = {
        { 1, 0, 2, 0 },
        { 1, 0, 2, 0 },
        { 1, 0, 2, 0 },
        { 1, 0, 2, 0 },
        { 1, 0, 2, 3 },
    };
    const uint32_t hint_values[][4] = {
        { 1, 0, 2, 0 },
        { 1, 0, 2, 0 },
        { 1, 0, 2, 1 },
        { 1, 0, 2, 0 },
        { 1, 0, 2, 3 },
    };
    const uint32_t hint_intervals[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, 1 | BUSTER_TIER1_ALLOCATE_HINT_EXIT };
    const uint32_t allowed[4] = { 3, 3, 3, 3 };
    const uint8_t classes[4] = { 0 };
    const uint32_t use_offsets[][5] = {
        { 0, 2, 3, 5, 6 },
        { 0, 2, 3, 5, 6 },
        { 0, 2, 3, 5, 6 },
        { 0, 2, 3, 5, 7 },
        { 0, 2, 3, 5, 6 },
    };
    const uint32_t use_positions[][7] = {
        { 0, 4, 1, 2, 7, 16 },
        { 0, 4, 1, 2, 4, 16 },
        { 0, 4, 1, 2, 7, 16 },
        { 0, 4, 1, 2, 7, 6, 16 },
        { 0, 4, 1, 2, 7, 16 },
    };
    const uint8_t required[7] = { 1, 1, 1, 1, 1, 1, 1 };
    const uint32_t fragments[] = { 6, 5, 5, 5, 5 };
    const uint8_t expected_kinds[][6] = {
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
    };
    const uint8_t expected_registers[][6] = {
        { 0, 1, UINT8_MAX, 1, UINT8_MAX, 1 },
        { 0, 1, UINT8_MAX, 1, 1 },
        { 0, 1, UINT8_MAX, 1, 0 },
        { 0, 1, UINT8_MAX, 1, 0 },
        { 0, 1, UINT8_MAX, 1, 0 },
    };
    const uint32_t expected_starts[][6] = {
        { 0, 1, 2, 2, 6, 16 },
        { 0, 1, 2, 2, 6 },
        { 0, 1, 2, 2, 6 },
        { 0, 1, 2, 2, 6 },
        { 0, 1, 2, 2, 6 },
    };
    const char* names[] = {
        "a range continuing its value from its slot waits there for its next required use, then takes its first register",
        "a range continuing its value from its slot takes its first register where that is free at its start",
        "a range hinted to another value takes a free register at its start",
        "a range required at its start takes a free register there",
        "a range hinted to another value's range in that value's slot takes a free register at its start",
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(names); fixture += 1)
    {
        const Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends[fixture],
            .values = values[fixture],
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = hint_values[fixture],
            .hint_intervals = hint_intervals,
            .use_offsets = use_offsets[fixture],
            .use_positions = use_positions[fixture],
            .use_required = required,
            .use_count = use_offsets[fixture][4],
            .count = 4,
            .value_count = 4,
            .position_count = 20,
            .registers = { 7 },
        };
        Tier1Allocation outputs[2] = { { 0 } };
        const uint64_t saved = arena->length;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &outputs[path]) : tier1_allocate_scan_reference(&input, arena, &outputs[path]);
            const Tier1Allocation* output = &outputs[path];
            bool valid = error == TIER1_ALLOCATION_OK && output->fragment_count == fragments[fixture] && output->spill_count == 1 && tier1_allocation_verify(&input, output) && tier1_allocation_test_occupancy(&input, output);
            valid = valid && memcmp(output->kinds, expected_kinds[fixture], fragments[fixture]) == 0 && memcmp(output->registers, expected_registers[fixture], fragments[fixture]) == 0;
            valid = valid && memcmp(output->starts, expected_starts[fixture], fragments[fixture] * sizeof(uint32_t)) == 0;
            tier1_allocation_test_expect(tally, valid, names[fixture]);
            if (!valid && error == TIER1_ALLOCATION_OK)
            {
                fprintf(stderr, "  path %u: %u fragments:", path, output->fragment_count);
                for (uint32_t f = 0; f < output->fragment_count; f += 1)
                {
                    fprintf(stderr, " %u@%u/r%u", output->kinds[f], output->starts[f], output->registers[f]);
                }
                fprintf(stderr, "\n");
            }
        }
        arena_pop_to(arena, saved);
    }
}

// The allocation's outputs against one fixture's expected fragments, by tier1_allocate and by the scan reference.
static void tier1_allocation_test_fragments(Tier1AllocationTestTally* tally, Arena* arena, const Tier1AllocationInput* input, uint32_t fragments, const uint8_t* kinds, const uint8_t* registers, const uint32_t* starts, const char* name)
{
    Tier1Allocation outputs[2] = { { 0 } };
    const uint64_t saved = arena->length;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const Tier1AllocationError error = path == 0 ? tier1_allocate(input, arena, &outputs[path]) : tier1_allocate_scan_reference(input, arena, &outputs[path]);
        const Tier1Allocation* output = &outputs[path];
        bool valid = error == TIER1_ALLOCATION_OK && output->fragment_count == fragments && output->spill_count == 0 && tier1_allocation_verify(input, output) && tier1_allocation_test_occupancy(input, output);
        valid = valid && memcmp(output->kinds, kinds, fragments) == 0 && memcmp(output->registers, registers, fragments) == 0 && memcmp(output->starts, starts, fragments * sizeof(uint32_t)) == 0;
        tier1_allocation_test_expect(tally, valid, name);
        if (!valid && error == TIER1_ALLOCATION_OK)
        {
            fprintf(stderr, "  path %u: %u fragments:", path, output->fragment_count);
            for (uint32_t f = 0; f < output->fragment_count; f += 1)
            {
                fprintf(stderr, " %u@%u/r%u", output->kinds[f], output->starts[f], output->registers[f]);
            }
            fprintf(stderr, "\n");
        }
    }
    arena_pop_to(arena, saved);
}

// A recipe's range after a closing that held no register (#2618, #2735), with one register: recipe value 0 takes r0 at 0,
// read at 1, and leaves it at 4; its range at 10, named by the first one's closing, is read at 15. Where a required value
// 1 took r0 at 4, the closing is memory, and the range at 10 waits for its read at 15 (tier1_allocation's `waits`, where
// it took r0 at once before). Where a call's clobber at 4 took it, and value 1 then r0, the closing gives no hint
// (tier1_allocation_note_closing), and the range at 10 takes r0, value 0's last register, at its start.
static void tier1_allocation_test_waits(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[3] = { 0, 4, 10 };
    const uint32_t ends[3] = { 10, 8, 20 };
    const uint32_t values[3] = { 0, 1, 0 };
    const uint32_t allowed[3] = { 1, 1, 1 };
    const uint8_t classes[3] = { 0 };
    const uint8_t rematerializable[3] = { 1, 0, 1 };
    const uint32_t hint_intervals[3] = { UINT32_MAX, UINT32_MAX, 0 | BUSTER_TIER1_ALLOCATE_HINT_EXIT };
    const uint32_t use_offsets[4] = { 0, 1, 2, 3 };
    const uint32_t use_positions[3] = { 1, 4, 15 };
    const uint32_t clobbers[1] = { 4 };
    const uint8_t required[3] = { 1, 1, 1 };
    const uint8_t kinds[][5] = {
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
    };
    const uint8_t registers[][5] = {
        { 0, UINT8_MAX, 0, UINT8_MAX, 0 },
        { 0, UINT8_MAX, 0, 0 },
    };
    const uint32_t fragment_starts[][5] = {
        { 0, 4, 4, 10, 15 },
        { 0, 4, 4, 10 },
    };
    const uint32_t fragments[2] = { 5, 4 };
    const char* names[2] = {
        "a recipe's range after a closing in memory waits for its read",
        "a recipe's range after a closing a call emptied takes its register at its start",
    };
    for (uint32_t fixture = 0; fixture < 2; fixture += 1)
    {
        const Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = values,
            .hint_intervals = hint_intervals,
            .rematerializable = rematerializable,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = required,
            .use_count = 3,
            .clobbers = fixture == 1 ? clobbers : 0,
            .clobber_count = fixture == 1 ? 1 : 0,
            .clobber_registers = { fixture == 1 ? 1u : 0u },
            .count = 3,
            .value_count = 2,
            .position_count = 20,
            .registers = { 1 },
        };
        tier1_allocation_test_fragments(tally, arena, &input, fragments[fixture], kinds[fixture], registers[fixture], fragment_starts[fixture], names[fixture]);
    }
}

// One trace by tier1_allocate and by the scan reference: its fragments' kinds, registers and starts, and its slots.
static void tier1_allocation_test_trace(Tier1AllocationTestTally* tally, Arena* arena, const Tier1AllocationInput* input, uint32_t fragments, const uint8_t* kinds, const uint8_t* registers, const uint32_t* starts, uint32_t spills,
                                        const char* name)
{
    Tier1Allocation outputs[2] = { { 0 } };
    const uint64_t saved = arena->length;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const Tier1AllocationError error = path == 0 ? tier1_allocate(input, arena, &outputs[path]) : tier1_allocate_scan_reference(input, arena, &outputs[path]);
        const Tier1Allocation* output = &outputs[path];
        bool valid = error == TIER1_ALLOCATION_OK && output->fragment_count == fragments && output->spill_count == spills && tier1_allocation_verify(input, output) && tier1_allocation_test_occupancy(input, output);
        valid = valid && memcmp(output->kinds, kinds, fragments) == 0 && memcmp(output->registers, registers, fragments) == 0 && memcmp(output->starts, starts, fragments * sizeof(uint32_t)) == 0;
        tier1_allocation_test_expect(tally, valid, name);
        if (!valid && error == TIER1_ALLOCATION_OK)
        {
            fprintf(stderr, "  path %u: %u fragments:", path, output->fragment_count);
            for (uint32_t f = 0; f < output->fragment_count; f += 1)
            {
                fprintf(stderr, " %u@%u/r%u", output->kinds[f], output->starts[f], output->registers[f]);
            }
            fprintf(stderr, "\n");
        }
    }
    arena_pop_to(arena, saved);
}

// #2774: a definition that finds every register held, where no held value has a use left before its interval's end,
// evicts the one that continues in its slot after the interval (continuation_intervals: an interval of its value whose
// opening took no register), though another is lighter. Two registers; values 0 and 1 hold them over [0, 4), where value
// 2's first interval (2) finds none and opens in its slot. Over [4, 10) value 2 (interval 3, two uses) and value 3
// (interval 4, one use) hold them, and value 4's definition at 6 takes one. Each fixture names interval 3's continuation:
// interval 2, none, interval 3 itself (its opening a register), and interval 2 where value 2 still has a use at 7.
static void tier1_allocation_test_stored_victims(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[6] = { 0, 0, 1, 4, 4, 6 };
    const uint32_t ends[6] = { 4, 4, 4, 10, 10, 8 };
    const uint32_t values[6] = { 0, 1, 2, 2, 3, 4 };
    const uint32_t hint_values[6] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    const uint32_t hint_intervals[6] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    const uint32_t continuations[][6] = {
        { UINT32_MAX, UINT32_MAX, UINT32_MAX, 2, UINT32_MAX, UINT32_MAX },
        { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX },
        { UINT32_MAX, UINT32_MAX, UINT32_MAX, 3, UINT32_MAX, UINT32_MAX },
        { UINT32_MAX, UINT32_MAX, UINT32_MAX, 2, UINT32_MAX, UINT32_MAX },
    };
    const uint32_t allowed[6] = { 3, 3, 3, 3, 3, 3 };
    const uint8_t classes[6] = { 0 };
    const uint32_t use_offsets[7] = { 0, 2, 4, 4, 6, 7, 9 };
    const uint32_t use_positions[][9] = {
        { 0, 3, 0, 3, 4, 5, 4, 6, 7 },
        { 0, 3, 0, 3, 4, 5, 4, 6, 7 },
        { 0, 3, 0, 3, 4, 5, 4, 6, 7 },
        { 0, 3, 0, 3, 4, 7, 4, 6, 7 },
    };
    const uint8_t required[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    const bool stored[] = { true, false, false, false };
    const uint8_t kinds[2][7] = {
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER },
    };
    const uint8_t registers[2][7] = {
        { 0, 1, UINT8_MAX, 0, UINT8_MAX, 1, 0 },
        { 0, 1, UINT8_MAX, 0, 1, UINT8_MAX, 1 },
    };
    const uint32_t fragment_starts[2][7] = {
        { 0, 0, 1, 4, 6, 4, 6 },
        { 0, 0, 1, 4, 4, 6, 6 },
    };
    const char* names[] = {
        "a definition evicts a value whose continuation is its slot, though another is lighter",
        "without a continuation the definition evicts the lighter value",
        "a continuation that opened in a register is no reason to evict",
        "a value with a use left keeps its register though its continuation is its slot",
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(names); fixture += 1)
    {
        const Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = hint_values,
            .hint_intervals = hint_intervals,
            .continuation_intervals = continuations[fixture],
            .use_offsets = use_offsets,
            .use_positions = use_positions[fixture],
            .use_required = required,
            .use_count = use_offsets[6],
            .count = 6,
            .value_count = 5,
            .position_count = 10,
            .registers = { 3 },
        };
        const uint32_t k = stored[fixture] ? 0 : 1;
        tier1_allocation_test_trace(tally, arena, &input, 7, kinds[k], registers[k], fragment_starts[k], stored[fixture] ? 1 : 2, names[fixture]);
    }
}

// #2774: an interval evicted at its own start never held the register it took there, so its opening, which a later range
// that names it follows, is its slot. Two registers; value 0's interval 0 [0, 10) takes r0 at 0, and value 1 holds r1
// until 2. Value 2's definition, required at 0 in the first fixture and at 1 in the second, evicts it; it comes back at 8
// into r1. Value 0's next range, interval 3 [10, 20), names interval 0's opening, where value 2 holds r0 and r1 is free:
// after the eviction at its start it waits in its slot until its use at 15; after one at 1 it takes r1 at 10.
static void tier1_allocation_test_empty_openings(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[][4] = {
        { 0, 0, 0, 10 },
        { 0, 0, 1, 10 },
    };
    const uint32_t ends[4] = { 10, 2, 20, 20 };
    const uint32_t values[4] = { 0, 1, 2, 0 };
    const uint32_t hint_values[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, 0 };
    const uint32_t hint_intervals[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, 0 };
    const uint32_t allowed[4] = { 3, 3, 3, 3 };
    const uint8_t classes[4] = { 0 };
    const uint32_t use_offsets[5] = { 0, 1, 2, 4, 5 };
    const uint32_t use_positions[][5] = {
        { 8, 0, 0, 19, 15 },
        { 8, 0, 1, 19, 15 },
    };
    const uint8_t required[5] = { 1, 1, 1, 1, 1 };
    const uint8_t kinds[][6] = {
        { BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
    };
    const uint8_t registers[][6] = {
        { UINT8_MAX, 1, 1, 0, UINT8_MAX, 1 },
        { 0, UINT8_MAX, 1, 1, 0, 1 },
    };
    const uint32_t fragment_starts[][6] = {
        { 0, 8, 0, 0, 10, 15 },
        { 0, 1, 8, 0, 1, 10 },
    };
    const char* names[] = {
        "a range following an interval evicted at its start waits in its slot",
        "a range following an interval evicted after its start takes a free register there",
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(names); fixture += 1)
    {
        const Tier1AllocationInput input = {
            .starts = starts[fixture],
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = hint_values,
            .hint_intervals = hint_intervals,
            .use_offsets = use_offsets,
            .use_positions = use_positions[fixture],
            .use_required = required,
            .use_count = use_offsets[4],
            .count = 4,
            .value_count = 3,
            .position_count = 20,
            .registers = { 3 },
        };
        tier1_allocation_test_trace(tally, arena, &input, 6, kinds[fixture], registers[fixture], fragment_starts[fixture], 1, names[fixture]);
    }
}

static void tier1_allocation_test_hints(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed)
{
    tier1_allocation_test_idle(tally, arena);
    tier1_allocation_test_yields(tally, arena);
    tier1_allocation_test_unread(tally, arena);
    tier1_allocation_test_waits(tally, arena);
    tier1_allocation_test_slot_waits(tally, arena);
    tier1_allocation_test_stored_victims(tally, arena);
    tier1_allocation_test_empty_openings(tally, arena);
    // Two values, each with two adjacent ranges: a [0, 10) [10, 20), b [2, 10) [10, 20). Ends come first in input
    // order, a's then b's, so the level rule gives a's second range b's register and b's a's.
    {
        const uint32_t starts[] = {
            0,
            2,
            10,
            10,
        };
        const uint32_t ends[] = {
            10,
            10,
            20,
            20,
        };
        const uint32_t values[] = {
            0,
            1,
            0,
            1,
        };
        const uint32_t allowed[] = {
            0xffff,
            0xffff,
            0xffff,
            0xffff,
        };
        const uint8_t classes[] = {
            0,
            0,
            0,
            0,
        };
        const uint32_t hint_values[] = {
            UINT32_MAX,
            UINT32_MAX,
            0,
            1,
        };
        Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .count = 4,
            .value_count = 2,
            .position_count = 20,
            .registers = {
                0xffff,
                0xffff,
            },
        };
        for (uint32_t hinted = 0; hinted < 2; hinted += 1)
        {
            input.hint_values = hinted != 0 ? hint_values : 0;
            for (uint32_t path = 0; path < 2; path += 1)
            {
                const uint64_t saved = arena->length;
                Tier1Allocation output = { 0 };
                const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
                const bool produced = error == TIER1_ALLOCATION_OK && output.fragment_count == 4;
                tier1_allocation_test_expect(tally, produced && tier1_allocation_verify(&input, &output), "hint trace allocates");
                if (produced)
                {
                    const bool kept = output.registers[2] == output.registers[0] && output.registers[3] == output.registers[1];
                    // The production path swaps them without hints; the independent scan, lowest free first, keeps
                    // them anyway. The fixture distinguishes assignment policies (#1395).
                    const bool swapped = output.registers[2] == output.registers[1] && output.registers[3] == output.registers[0];
                    tier1_allocation_test_expect(tally, hinted != 0 ? kept : (path == 0 ? swapped : kept), hinted != 0 ? "a hinted range keeps its value's register" : "the level rule alone gives the ranges each other's registers");
                }
                arena_pop_to(arena, saved);
            }
        }
        // A hinted value of the other class is invalid input.
        const uint8_t mixed[] = {
            0,
            1,
            0,
            0,
        };
        const uint32_t cross[] = {
            UINT32_MAX,
            UINT32_MAX,
            1,
            UINT32_MAX,
        };
        input.classes = mixed;
        input.hint_values = cross;
        Tier1Allocation refused = { 0 };
        const uint64_t saved = arena->length;
        tier1_allocation_test_expect(tally, tier1_allocate(&input, arena, &refused) == TIER1_ALLOCATION_INVALID && arena->length == saved, "a hint across classes is refused");
        const uint32_t outside[] = {
            UINT32_MAX,
            UINT32_MAX,
            2,
            UINT32_MAX,
        };
        input.classes = classes;
        input.hint_values = outside;
        tier1_allocation_test_expect(tally, tier1_allocate(&input, arena, &refused) == TIER1_ALLOCATION_INVALID && arena->length == saved, "a hint past the values is refused");
    }
    // Three values over [0, 10), then the first one's again over [10, 20), hinted to its own value and to the second
    // range's opening register: a dispatch loop's handler taking its sibling's entry convention. Without the interval
    // hint every path gives it its own value's register; with it, the second range's.
    {
        const uint32_t starts[] = {
            0,
            0,
            0,
            10,
        };
        const uint32_t ends[] = {
            10,
            10,
            10,
            20,
        };
        const uint32_t values[] = {
            0,
            1,
            2,
            0,
        };
        const uint32_t allowed[] = {
            0xffff,
            0xffff,
            0xffff,
            0xffff,
        };
        const uint8_t classes[] = {
            0,
            0,
            0,
            0,
        };
        const uint32_t hint_values[] = {
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            0,
        };
        const uint32_t hint_intervals[] = {
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            1,
        };
        Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = hint_values,
            .count = 4,
            .value_count = 3,
            .position_count = 20,
            .registers = {
                0xffff,
                0xffff,
            },
        };
        for (uint32_t hinted = 0; hinted < 2; hinted += 1)
        {
            input.hint_intervals = hinted != 0 ? hint_intervals : 0;
            for (uint32_t path = 0; path < 2; path += 1)
            {
                const uint64_t saved = arena->length;
                Tier1Allocation output = { 0 };
                const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
                const bool produced = error == TIER1_ALLOCATION_OK && output.fragment_count == 4;
                tier1_allocation_test_expect(tally, produced && tier1_allocation_verify(&input, &output), "interval hint trace allocates");
                if (produced)
                {
                    const uint32_t model = hinted != 0 ? 1 : 0;
                    tier1_allocation_test_expect(tally, output.registers[3] == output.registers[model], hinted != 0 ? "a range takes its hinted interval's opening register first" : "a range without an interval hint keeps its value's register");
                }
                arena_pop_to(arena, saved);
            }
        }
        // The hinted interval must start before the range and have its class.
        const uint32_t refused_hints[][4] = {
            {
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
                3,
            },
            {
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
                4,
            },
            {
                UINT32_MAX,
                0,
                UINT32_MAX,
                UINT32_MAX,
            },
            {
                3,
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
            },
        };
        const char* refused_names[] = {
            "an interval hint to itself is refused",
            "an interval hint past the intervals is refused",
            "an interval hint to one starting with it is refused",
            "an interval hint to a later one is refused",
        };
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(refused_hints); k += 1)
        {
            input.hint_intervals = refused_hints[k];
            Tier1Allocation refused = { 0 };
            const uint64_t saved = arena->length;
            const Tier1AllocationError error = tier1_allocate(&input, arena, &refused);
            tier1_allocation_test_expect(tally, error == TIER1_ALLOCATION_INVALID && arena->length == saved, refused_names[k]);
        }
        const uint8_t mixed[] = {
            0,
            1,
            0,
            0,
        };
        input.classes = mixed;
        input.hint_intervals = hint_intervals;
        Tier1Allocation refused = { 0 };
        const uint64_t saved = arena->length;
        tier1_allocation_test_expect(tally, tier1_allocate(&input, arena, &refused) == TIER1_ALLOCATION_INVALID && arena->length == saved, "an interval hint across classes is refused");
    }
    // An alternate (#2734): a [0, 10) and b [0, 10) take r0 and r1, and c [10, 20), another value's, hinted to b's value,
    // takes b's r1 without its alternate and a's r0 with it, where a's value holds r0 last and r0 is free. Where a's value
    // moved since (d [5, 10), only r3, after a ends at 5) or another range holds r0 when c starts (e [8, 20), only r0,
    // after a ends at 8), c keeps its hint's r1. Every path alike.
    {
        const uint32_t starts[][4] = {
            { 0, 0, 10 },
            { 0, 0, 5, 10 },
            { 0, 0, 8, 10 },
        };
        const uint32_t ends[][4] = {
            { 10, 10, 20 },
            { 5, 10, 10, 20 },
            { 8, 10, 20, 20 },
        };
        const uint32_t values[][4] = {
            { 0, 1, 2 },
            { 0, 1, 0, 2 },
            { 0, 1, 3, 2 },
        };
        const uint32_t allowed[][4] = {
            { 0xffff, 0xffff, 0xffff },
            { 0xffff, 0xffff, 1u << 3, 0xffff },
            { 0xffff, 0xffff, 1u << 0, 0xffff },
        };
        const uint32_t hint_values[][4] = {
            { UINT32_MAX, UINT32_MAX, 1 },
            { UINT32_MAX, UINT32_MAX, UINT32_MAX, 1 },
            { UINT32_MAX, UINT32_MAX, UINT32_MAX, 1 },
        };
        const uint32_t hint_intervals[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
        const uint32_t alternate_intervals[][4] = {
            { UINT32_MAX, UINT32_MAX, 0 },
            { UINT32_MAX, UINT32_MAX, UINT32_MAX, 0 },
            { UINT32_MAX, UINT32_MAX, UINT32_MAX, 0 },
        };
        const uint32_t counts[] = { 3, 4, 4 };
        const uint32_t value_counts[] = { 3, 3, 4 };
        const uint8_t classes[4] = { 0 };
        const char* names[] = {
            "a range takes its alternate's settled, free opening register over its value hint",
            "a range whose alternate's value moved since keeps its value hint",
            "a range whose alternate's register another holds keeps its value hint",
        };
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(counts); k += 1)
        {
            const uint32_t c = counts[k] - 1;
            for (uint32_t alternated = 0; alternated < 2; alternated += 1)
            {
                const Tier1AllocationInput input = {
                    .starts = starts[k],
                    .ends = ends[k],
                    .values = values[k],
                    .allowed_registers = allowed[k],
                    .classes = classes,
                    .hint_values = hint_values[k],
                    .hint_intervals = hint_intervals,
                    .alternate_intervals = alternated != 0 ? alternate_intervals[k] : 0,
                    .count = counts[k],
                    .value_count = value_counts[k],
                    .position_count = 20,
                    .registers = {
                        0xffff,
                    },
                };
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    const uint64_t saved = arena->length;
                    Tier1Allocation output = { 0 };
                    const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
                    const bool produced = error == TIER1_ALLOCATION_OK && output.fragment_count == counts[k];
                    tier1_allocation_test_expect(tally, produced && tier1_allocation_verify(&input, &output), "alternate trace allocates");
                    if (produced)
                    {
                        const bool taken = alternated != 0 && k == 0;
                        tier1_allocation_test_expect(tally, output.registers[c] == output.registers[taken ? 0 : 1], alternated != 0 ? names[k] : "a range without an alternate keeps its value hint");
                    }
                    arena_pop_to(arena, saved);
                }
            }
        }
        // The alternate must start before the range and have its class.
        const uint32_t refused[][3] = {
            { UINT32_MAX, UINT32_MAX, 2 },
            { UINT32_MAX, UINT32_MAX, 3 },
            { 2, UINT32_MAX, UINT32_MAX },
        };
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(refused); k += 1)
        {
            const Tier1AllocationInput input = {
                .starts = starts[0],
                .ends = ends[0],
                .values = values[0],
                .allowed_registers = allowed[0],
                .classes = classes,
                .hint_values = hint_values[0],
                .hint_intervals = hint_intervals,
                .alternate_intervals = refused[k],
                .count = 3,
                .value_count = 3,
                .position_count = 20,
                .registers = {
                    0xffff,
                },
            };
            Tier1Allocation output = { 0 };
            const uint64_t saved = arena->length;
            const Tier1AllocationError error = tier1_allocate(&input, arena, &output);
            tier1_allocation_test_expect(tally, error == TIER1_ALLOCATION_INVALID && arena->length == saved, "an alternate to itself, past the intervals or to a later range is refused");
        }
    }
    // A range named by its exit (#2553): with two registers, a [0, 10) takes r0, c [3, 6) evicts it, d [8, 10) takes r0
    // again, and a's required use at 9 reloads it into r1. e [10, 20), another value's, takes the register a ends in,
    // r1, where a hint to a's opening gives it r0 and none the lowest free one, r0. The exit must end by e's start.
    {
        const uint32_t starts[] = {
            0,
            2,
            3,
            8,
            10,
        };
        const uint32_t ends[] = {
            10,
            6,
            6,
            10,
            20,
        };
        const uint32_t values[] = {
            0,
            1,
            2,
            3,
            4,
        };
        const uint32_t allowed[] = {
            3,
            3,
            3,
            3,
            3,
        };
        const uint8_t classes[5] = { 0 };
        const uint32_t hint_values[] = {
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
        };
        const uint32_t use_offsets[] = {
            0,
            2,
            4,
            6,
            7,
            8,
        };
        const uint32_t use_positions[] = {
            0,
            9,
            2,
            5,
            3,
            5,
            8,
            10,
        };
        const uint8_t required[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        const uint32_t named[][5] = {
            {
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
                BUSTER_TIER1_ALLOCATE_HINT_EXIT,
            },
            {
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
                0,
            },
            {
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
            },
        };
        const uint8_t expected[] = {
            1,
            0,
            0,
        };
        const char* expected_names[] = {
            "a range named by its exit takes the register its interval ends in",
            "a range named by its opening takes the register its interval starts in",
            "a range without an interval hint takes the lowest free register",
        };
        Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = hint_values,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = required,
            .use_count = 8,
            .count = 5,
            .value_count = 5,
            .position_count = 20,
            .registers = { 3 },
        };
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(expected); k += 1)
        {
            input.hint_intervals = named[k];
            for (uint32_t path = 0; path < 2; path += 1)
            {
                const uint64_t saved = arena->length;
                Tier1Allocation output = { 0 };
                const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
                const bool produced = error == TIER1_ALLOCATION_OK && tier1_allocation_verify(&input, &output) && tier1_allocation_test_occupancy(&input, &output);
                // a's fragments: r0, then its slot from 3, then r1 from 9.
                const uint32_t a = output.fragment_offsets[0];
                const bool moved = produced && output.fragment_offsets[1] == a + 3 && output.registers[a] == 0 && output.kinds[a + 1] == BUSTER_TIER1_ALLOCATE_SPILL && output.registers[a + 2] == 1;
                const uint32_t e = produced ? output.fragment_offsets[4] : 0;
                tier1_allocation_test_expect(tally, moved && output.fragment_offsets[5] == e + 1 && output.kinds[e] == BUSTER_TIER1_ALLOCATE_REGISTER && output.registers[e] == expected[k], expected_names[k]);
                arena_pop_to(arena, saved);
            }
        }
        // d, which starts before a ends, can't take a's exit.
        const uint32_t early[] = {
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            BUSTER_TIER1_ALLOCATE_HINT_EXIT,
            UINT32_MAX,
        };
        input.hint_intervals = early;
        Tier1Allocation refused = { 0 };
        const uint64_t saved = arena->length;
        tier1_allocation_test_expect(tally, tier1_allocate(&input, arena, &refused) == TIER1_ALLOCATION_INVALID && arena->length == saved, "an exit hint to an interval that ends after the range starts is refused");
    }
    // Random ranges of a few values each, adjacent and apart, every one hinted to a random value of its class or to
    // none, at pressures to the class's registers and past them, with required uses and fixed ranges in some.
    uint32_t starts[128];
    uint32_t ends[128];
    uint32_t values[128];
    uint32_t allowed[128];
    uint8_t classes[128];
    uint32_t hint_values[128];
    uint32_t hint_intervals[128];
    uint32_t alternate_intervals[128];
    uint32_t continuation_intervals[128];
    uint32_t use_offsets[129];
    uint32_t use_positions[128];
    uint8_t required[128];
    const uint32_t fixed_start[] = {
        31,
        120,
        60,
    };
    const uint32_t fixed_end[] = {
        33,
        121,
        62,
    };
    const uint32_t fixed_regs[] = {
        0x3,
        0x1,
        0x2,
    };
    const uint8_t fixed_classes[] = {
        BUSTER_TIER1_ALLOCATE_GPR,
        BUSTER_TIER1_ALLOCATE_VECTOR,
        BUSTER_TIER1_ALLOCATE_MASK,
    };
    uint64_t compared = 0;
    for (uint32_t iteration = 0; iteration < 3000; iteration += 1)
    {
        const uint32_t value_count = 1 + tier1_allocation_test_random(&seed) % 24;
        const uint32_t gpr_mask = (1u << (1 + iteration % 9)) - 1;
        const uint32_t vector_mask = (1u << (1 + iteration % 5)) - 1;
        // k1 up to k7: no mask value takes k0, the class's scratch register.
        const uint32_t mask_mask = ((1u << (1 + iteration % 7)) - 1) << 1;
        const bool constrained = iteration % 4 == 0;
        // An eighth of the iterations take the scan with required uses with the hinted ranges, and continuations (#2774).
        const bool continued = iteration % 8 == 3;
        uint8_t value_classes[24];
        uint32_t count = 0;
        for (uint32_t v = 0; v < value_count; v += 1)
        {
            // A third vector values, a sixth masks, the rest general.
            const uint32_t draw = (uint32_t)(tier1_allocation_test_random(&seed) % 6);
            value_classes[v] = draw < 2 ? BUSTER_TIER1_ALLOCATE_VECTOR : draw == 2 ? BUSTER_TIER1_ALLOCATE_MASK : BUSTER_TIER1_ALLOCATE_GPR;
        }
        // Each value's ranges, in position order: the next starts where the last ended, or later.
        for (uint32_t v = 0; v < value_count && count < 120; v += 1)
        {
            uint32_t position = tier1_allocation_test_random(&seed) % 64;
            const uint32_t ranges = 1 + tier1_allocation_test_random(&seed) % 4;
            for (uint32_t r = 0; r < ranges && count < 128 && position < 190; r += 1)
            {
                starts[count] = position;
                ends[count] = position + 1 + tier1_allocation_test_random(&seed) % 24;
                ends[count] = ends[count] > 200 ? 200 : ends[count];
                values[count] = v;
                classes[count] = value_classes[v];
                allowed[count] = classes[count] == BUSTER_TIER1_ALLOCATE_GPR ? gpr_mask : classes[count] == BUSTER_TIER1_ALLOCATE_VECTOR ? vector_mask : mask_mask;
                position = ends[count] + (tier1_allocation_test_random(&seed) % 3 == 0 ? tier1_allocation_test_random(&seed) % 8 : 0);
                count += 1;
            }
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            // A random value of the class, often its own.
            uint32_t hinted = UINT32_MAX;
            const uint32_t pick = tier1_allocation_test_random(&seed) % 4;
            if (pick == 0)
            {
                hinted = values[i];
            }
            else if (pick == 1)
            {
                const uint32_t other = tier1_allocation_test_random(&seed) % value_count;
                hinted = value_classes[other] == classes[i] ? other : UINT32_MAX;
            }
            hint_values[i] = hinted;
            // Every other iteration, a third of the ranges also hinted to a random earlier range of their class, half of
            // those that end before it starts by its exit (#2553).
            const uint32_t earlier = tier1_allocation_test_random(&seed) % count;
            const bool model = iteration % 2 == 1 && tier1_allocation_test_random(&seed) % 3 == 0 && classes[earlier] == classes[i] && starts[earlier] < starts[i];
            const bool exit = model && ends[earlier] <= starts[i] && (tier1_allocation_test_random(&seed) & 1) != 0;
            hint_intervals[i] = model ? earlier | (exit ? BUSTER_TIER1_ALLOCATE_HINT_EXIT : 0) : UINT32_MAX;
            // And a quarter of those iterations' ranges an alternate (#2734): another random earlier range of their class.
            const uint32_t other = tier1_allocation_test_random(&seed) % count;
            const bool alternate = iteration % 4 == 1 && tier1_allocation_test_random(&seed) % 4 == 0 && classes[other] == classes[i] && starts[other] < starts[i];
            alternate_intervals[i] = alternate ? other : UINT32_MAX;
            // A continuation: the range itself or an earlier one of its value, or none, at least a third of the time.
            uint32_t back = 0;
            while (back < i && values[i - back - 1] == values[i])
            {
                back += 1;
            }
            const uint32_t draw = (uint32_t)(tier1_allocation_test_random(&seed) % (3 * (back + 1)));
            continuation_intervals[i] = draw <= 2 * back ? i - draw / 2 : UINT32_MAX;
            use_offsets[i] = i;
            use_positions[i] = ends[i] - 1;
            required[i] = (uint8_t)(tier1_allocation_test_random(&seed) & 1);
        }
        use_offsets[count] = count;
        Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = hint_values,
            .hint_intervals = iteration % 2 == 1 ? hint_intervals : 0,
            .alternate_intervals = iteration % 4 == 1 ? alternate_intervals : 0,
            .continuation_intervals = continued ? continuation_intervals : 0,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = constrained || continued ? required : 0,
            .use_count = count,
            .count = count,
            .value_count = value_count,
            .position_count = 200,
            .registers = {
                gpr_mask,
                vector_mask,
                mask_mask,
            },
            .fixed = {
                .starts = fixed_start,
                .ends = fixed_end,
                .registers = fixed_regs,
                .classes = fixed_classes,
                .count = constrained ? BUSTER_ARRAY_LENGTH(fixed_classes) : 0,
            },
        };
        const uint64_t saved = arena->length;
        Tier1Allocation vector = { 0 };
        Tier1Allocation scan = { 0 };
        const Tier1AllocationError vector_error = tier1_allocate(&input, arena, &vector);
        const Tier1AllocationError scan_error = tier1_allocate_scan_reference(&input, arena, &scan);
        tier1_allocation_test_expect(tally, vector_error == scan_error && (vector_error == TIER1_ALLOCATION_OK || ((constrained || continued) && vector_error == TIER1_ALLOCATION_CONSTRAINT)), "hinted ranges allocate on the production path and the scan alike");
        if (vector_error == TIER1_ALLOCATION_OK && scan_error == TIER1_ALLOCATION_OK)
        {
            tier1_allocation_test_expect(tally, tier1_allocation_verify(&input, &vector) && tier1_allocation_verify(&input, &scan), "hinted ranges pass the interval verifier");
            tier1_allocation_test_expect(tally, tier1_allocation_test_occupancy(&input, &vector) && tier1_allocation_test_occupancy(&input, &scan), "hinted ranges pass the occupancy oracle");
            // A class the common path colors, against the reference; the classes are colored in order, sharing `last`.
            uint8_t reference[128];
            uint8_t last[25];
            uint8_t openings[128];
            uint8_t closings[128];
            memset(last, UINT8_MAX, sizeof(last));
            memset(openings, UINT8_MAX, sizeof(openings));
            memset(closings, UINT8_MAX, sizeof(closings));
            for (uint32_t class = 0; !constrained && class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
            {
                if (tier1_allocation_test_hinted_levels(&input, class, reference, last, openings, closings))
                {
                    bool same = true;
                    for (uint32_t i = 0; i < count; i += 1)
                    {
                        const uint32_t f = vector.fragment_offsets[i];
                        same &= classes[i] != class || (vector.fragment_offsets[i + 1] == f + 1 && vector.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && vector.registers[f] == reference[i]);
                    }
                    tier1_allocation_test_expect(tally, same, "the common path's hints match the independent reference");
                    compared += 1;
                }
            }
        }
        arena_pop_to(arena, saved);
    }
    tier1_allocation_test_expect(tally, compared > 1000, "the hint reference compares many colored classes");
}
