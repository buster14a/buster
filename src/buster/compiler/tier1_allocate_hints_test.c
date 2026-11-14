// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Value hints (Tier1AllocationInput.hint_values, #140, and hint_intervals, #1216): literal traces of two values that
// end and restart at one position, which the level rule alone gives each other's registers, of a range that takes an
// earlier one's opening register over its own value's last, and of one that takes an earlier one's closing register
// (BUSTER_TIER1_ALLOCATE_HINT_EXIT, #2553) after the scan moved it; and random inputs on both paths against the interval
// verifier, the position occupancy oracle and an independent reference of the common path's hint rule; and traces of a
// recipe that gives its register to a value its hint names (#2618). Included by tier1_allocate_test.c.

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
                    const uint8_t follow = opening != UINT8_MAX ? opening : own != UINT8_MAX ? own : hinted != UINT32_MAX ? last[hinted] : UINT8_MAX;
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
// eviction by a later required value, and a stored value's unused continuation. Restricted masks force the scan on
// both production paths, so the regular-class coloring path cannot hide a regression in the required-use policy.
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
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_SPILL, BUSTER_TIER1_ALLOCATE_REGISTER },
    };
    const uint8_t expected_registers[][5] = {
        { 0, 0, 0, 0 },
        { 0, 1, 1, 0, 1 },
        { 0, UINT8_MAX, 0 },
        { 0, 0, UINT8_MAX, 0, 0 },
        { 0, UINT8_MAX, 0, UINT8_MAX, 0 },
    };
    const uint32_t counts[] = { 4, 5, 3, 4, 4 };
    const uint32_t fragments[] = { 4, 5, 3, 5, 5 };
    const uint8_t classes[5] = { 0 };
    const uint8_t required[4] = { 1, 1, 1, 1 };
    const uint8_t hints[5] = { UINT8_MAX, UINT8_MAX, 0, UINT8_MAX, UINT8_MAX };
    const char* names[] = {
        "an unused recipe preserves its required definition through two quiet ranges",
        "an unused recipe's physical hint cannot take another start's claimed register",
        "an unused recipe with no free register never evicts an active value",
        "a recipe kept through a quiet range remains evictable at a required use",
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
        for (uint32_t path = 0; path < 3; path += 1)
        {
            const uint64_t saved = arena->length;
            Tier1Allocation output = { 0 };
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : path == 1 ? tier1_allocate_scalar(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
            const bool valid = error == TIER1_ALLOCATION_OK && output.scalar_intervals == input.count && output.fragment_count == fragments[fixture] && output.splits == fragments[fixture] - input.count && output.spill_count == (fixture == 4 ? 1u : 0u) && tier1_allocation_verify(&input, &output) && tier1_allocation_test_occupancy(&input, &output) && memcmp(output.kinds, expected_kinds[fixture], fragments[fixture]) == 0 && memcmp(output.registers, expected_registers[fixture], fragments[fixture]) == 0;
            tier1_allocation_test_expect(tally, valid, names[fixture]);
            arena_pop_to(arena, saved);
        }
    }
}

// A value without a recipe takes its hinted register from a recipe that no use requires there (#2618): value 0 leaves r0
// at 4 and starts again at 8, where its hint names r0, which recipe value 1, defined at 5, holds. In the first trace the
// recipe leaves r0 at 8, recomputed into r1 for its required use at 11; in the second its use at 8 requires it there, so
// it keeps r0 and value 0 takes r1. The masks keep both production paths on the scan.
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
        Tier1Allocation outputs[3] = { { 0 } };
        bool produced = true;
        const uint64_t saved = arena->length;
        for (uint32_t path = 0; path < 3; path += 1)
        {
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &outputs[path]) : path == 1 ? tier1_allocate_scalar(&input, arena, &outputs[path]) : tier1_allocate_scan_reference(&input, arena, &outputs[path]);
            const Tier1Allocation* output = &outputs[path];
            bool valid = error == TIER1_ALLOCATION_OK && output->fragment_count == fragments[fixture] && output->spill_count == 0 && tier1_allocation_verify(&input, output) && tier1_allocation_test_occupancy(&input, output);
            valid = valid && memcmp(output->kinds, expected_kinds[fixture], fragments[fixture]) == 0 && memcmp(output->registers, expected_registers[fixture], fragments[fixture]) == 0;
            valid = valid && memcmp(output->starts, expected_starts[fixture], fragments[fixture] * sizeof(uint32_t)) == 0;
            produced &= error == TIER1_ALLOCATION_OK;
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
        tier1_allocation_test_expect(tally, produced && tier1_allocation_test_equal(&input, &outputs[0], &outputs[1]), "both production paths give a recipe's register to a hinted value alike");
        arena_pop_to(arena, saved);
    }
}

static void tier1_allocation_test_hints(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed)
{
    tier1_allocation_test_idle(tally, arena);
    tier1_allocation_test_yields(tally, arena);
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
            for (uint32_t path = 0; path < 3; path += 1)
            {
                const uint64_t saved = arena->length;
                Tier1Allocation output = { 0 };
                const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : path == 1 ? tier1_allocate_scalar(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
                const bool produced = error == TIER1_ALLOCATION_OK && output.fragment_count == 4;
                tier1_allocation_test_expect(tally, produced && tier1_allocation_verify(&input, &output), "hint trace allocates");
                if (produced)
                {
                    const bool kept = output.registers[2] == output.registers[0] && output.registers[3] == output.registers[1];
                    // Both production paths swap them without hints; the independent scan, lowest free first, keeps
                    // them anyway. The fixture distinguishes assignment policies even on a baseline host (#1395).
                    const bool swapped = output.registers[2] == output.registers[1] && output.registers[3] == output.registers[0];
                    tier1_allocation_test_expect(tally, hinted != 0 ? kept : (path < 2 ? swapped : kept), hinted != 0 ? "a hinted range keeps its value's register" : "the level rule alone gives the ranges each other's registers");
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
        tier1_allocation_test_expect(tally, tier1_allocate_scalar(&input, arena, &refused) == TIER1_ALLOCATION_INVALID && arena->length == saved, "a hint past the values is refused");
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
            for (uint32_t path = 0; path < 3; path += 1)
            {
                const uint64_t saved = arena->length;
                Tier1Allocation output = { 0 };
                const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : path == 1 ? tier1_allocate_scalar(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
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
            const Tier1AllocationError error = k % 2 == 0 ? tier1_allocate(&input, arena, &refused) : tier1_allocate_scalar(&input, arena, &refused);
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
            for (uint32_t path = 0; path < 3; path += 1)
            {
                const uint64_t saved = arena->length;
                Tier1Allocation output = { 0 };
                const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : path == 1 ? tier1_allocate_scalar(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
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
        tier1_allocation_test_expect(tally, tier1_allocate_scalar(&input, arena, &refused) == TIER1_ALLOCATION_INVALID && arena->length == saved, "the scalar path refuses that exit hint too");
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
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = constrained ? required : 0,
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
        Tier1Allocation scalar = { 0 };
        Tier1Allocation scan = { 0 };
        const Tier1AllocationError vector_error = tier1_allocate(&input, arena, &vector);
        const Tier1AllocationError scalar_error = tier1_allocate_scalar(&input, arena, &scalar);
        const Tier1AllocationError scan_error = tier1_allocate_scan_reference(&input, arena, &scan);
        tier1_allocation_test_expect(tally, vector_error == scalar_error && vector_error == scan_error && (vector_error == TIER1_ALLOCATION_OK || (constrained && vector_error == TIER1_ALLOCATION_CONSTRAINT)), "hinted ranges allocate on both paths and the scan alike");
        if (vector_error == TIER1_ALLOCATION_OK && scalar_error == TIER1_ALLOCATION_OK && scan_error == TIER1_ALLOCATION_OK)
        {
            tier1_allocation_test_expect(tally, tier1_allocation_verify(&input, &vector) && tier1_allocation_verify(&input, &scalar) && tier1_allocation_verify(&input, &scan), "hinted ranges pass the interval verifier");
            tier1_allocation_test_expect(tally, tier1_allocation_test_occupancy(&input, &vector) && tier1_allocation_test_occupancy(&input, &scalar) && tier1_allocation_test_occupancy(&input, &scan), "hinted ranges pass the occupancy oracle");
            tier1_allocation_test_expect(tally, tier1_allocation_test_equal(&input, &vector, &scalar), "hinted production paths choose identical allocation output");
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
