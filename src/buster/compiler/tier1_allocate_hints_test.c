// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Value hints (Tier1AllocationInput.hint_values, #140): a literal trace of two values that end and restart at one
// position, which the level rule alone gives each other's registers, and random inputs on both paths against the
// interval verifier, the position occupancy oracle and an independent reference of the common path's hint rule.
// Included by tier1_allocate_test.c.

// The common path's rule with hints, written over positions as tier1_allocation_test_levels writes it, with the free
// registers as a set per level: a start whose hinted value's last register is free takes it, from the level above
// that holds it (which then holds what the level rule would have taken) or as a fresh one (which makes a register
// taken from this level fresh again). False when the class doesn't fit.
static bool tier1_allocation_test_hinted_levels(const Tier1AllocationInput* input, uint32_t class, uint8_t* regs, uint8_t* last)
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
                    const uint8_t follow = hinted != UINT32_MAX ? last[hinted] : UINT8_MAX;
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
                    height += 1;
                }
            }
        }
    }
    return fits;
}

static void tier1_allocation_test_hints(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed)
{
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
                const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : tier1_allocate_scalar(&input, arena, &output);
                const bool produced = error == TIER1_ALLOCATION_OK && output.fragment_count == 4;
                tier1_allocation_test_expect(tally, produced && tier1_allocation_verify(&input, &output), "hint trace allocates");
                if (produced)
                {
                    const bool kept = output.registers[2] == output.registers[0] && output.registers[3] == output.registers[1];
                    // The common path swaps them without hints; the scan, lowest free first, keeps them anyway.
                    const bool swapped = output.registers[2] == output.registers[1] && output.registers[3] == output.registers[0];
                    tier1_allocation_test_expect(tally, hinted != 0 ? kept : (path == 0 && BUSTER_HOST_VECTOR ? swapped : kept), hinted != 0 ? "a hinted range keeps its value's register" : "the level rule alone gives the ranges each other's registers");
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
    // Random ranges of a few values each, adjacent and apart, every one hinted to a random value of its class or to
    // none, at pressures to the class's registers and past them, with required uses and fixed ranges in some.
    uint32_t starts[128];
    uint32_t ends[128];
    uint32_t values[128];
    uint32_t allowed[128];
    uint8_t classes[128];
    uint32_t hint_values[128];
    uint32_t use_offsets[129];
    uint32_t use_positions[128];
    uint8_t required[128];
    const uint32_t fixed_start[] = {
        31,
        120,
    };
    const uint32_t fixed_end[] = {
        33,
        121,
    };
    const uint32_t fixed_regs[] = {
        0x3,
        0x1,
    };
    const uint8_t fixed_classes[] = {
        0,
        1,
    };
    uint64_t compared = 0;
    for (uint32_t iteration = 0; iteration < 3000; iteration += 1)
    {
        const uint32_t value_count = 1 + tier1_allocation_test_random(&seed) % 24;
        const uint32_t gpr_mask = (1u << (1 + iteration % 9)) - 1;
        const uint32_t vector_mask = (1u << (1 + iteration % 5)) - 1;
        const bool constrained = iteration % 4 == 0;
        uint8_t value_classes[24];
        uint32_t count = 0;
        for (uint32_t v = 0; v < value_count; v += 1)
        {
            value_classes[v] = (uint8_t)(tier1_allocation_test_random(&seed) % 3 == 0);
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
                allowed[count] = classes[count] == 0 ? gpr_mask : vector_mask;
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
            },
            .fixed = {
                .starts = fixed_start,
                .ends = fixed_end,
                .registers = fixed_regs,
                .classes = fixed_classes,
                .count = constrained ? 2 : 0,
            },
        };
        const uint64_t saved = arena->length;
        Tier1Allocation vector = { 0 };
        Tier1Allocation scalar = { 0 };
        const Tier1AllocationError vector_error = tier1_allocate(&input, arena, &vector);
        const Tier1AllocationError scalar_error = tier1_allocate_scalar(&input, arena, &scalar);
        tier1_allocation_test_expect(tally, vector_error == scalar_error && (vector_error == TIER1_ALLOCATION_OK || (constrained && vector_error == TIER1_ALLOCATION_CONSTRAINT)), "hinted ranges allocate on both paths alike");
        if (vector_error == TIER1_ALLOCATION_OK && scalar_error == TIER1_ALLOCATION_OK)
        {
            tier1_allocation_test_expect(tally, tier1_allocation_verify(&input, &vector) && tier1_allocation_verify(&input, &scalar), "hinted ranges pass the interval verifier");
            tier1_allocation_test_expect(tally, tier1_allocation_test_occupancy(&input, &vector) && tier1_allocation_test_occupancy(&input, &scalar), "hinted ranges pass the occupancy oracle");
            // A class the common path colors, against the reference; the classes are colored in order, sharing `last`.
            uint8_t reference[128];
            uint8_t last[25];
            memset(last, UINT8_MAX, sizeof(last));
            for (uint32_t class = 0; BUSTER_HOST_VECTOR && !constrained && class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
            {
                if (tier1_allocation_test_hinted_levels(&input, class, reference, last))
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
    tier1_allocation_test_expect(tally, !BUSTER_HOST_VECTOR || compared > 1000, "the hint reference compares many colored classes");
}
