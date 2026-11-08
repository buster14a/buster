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
                    uint8_t follow = hinted != UINT32_MAX ? last[hinted] : UINT8_MAX;
                    uint32_t preferred = input->preferred_registers != 0 ? fresh & input->preferred_registers[i] : 0;
                    for (uint32_t k = height; input->preferred_registers != 0 && k < 33; k += 1)
                    {
                        preferred |= levels[k] < 32 ? (1u << levels[k]) & input->preferred_registers[i] : 0;
                    }
                    if (preferred != 0)
                    {
                        const uint8_t hint = input->hints != 0 ? input->hints[i] : UINT8_MAX;
                        follow = hint < 32 && ((preferred >> hint) & 1) != 0 ? hint : follow < 32 && ((preferred >> follow) & 1) != 0 ? follow : (uint8_t)__builtin_ctz(preferred);
                    }
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

// Soft register sets must not create spills when another legal register is free. These three-register cases cover
// the level matcher, constrained scan, fixed-clobber filtering and simultaneous required uses in all three classes.
static void tier1_allocation_test_preferences(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint8_t registers[3][3] = {
        { 0, 2, 5 },
        { 0, 17, 31 },
        { 1, 3, 7 },
    };
    for (uint32_t class = 0; class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
    {
        const uint32_t low = 1u << registers[class][0];
        const uint32_t middle = 1u << registers[class][1];
        const uint32_t high = 1u << registers[class][2];
        const uint32_t mask = low | middle | high;
        for (uint32_t shape = 0; shape < 5; shape += 1)
        {
            const bool sequential = shape >= 3;
            const uint32_t count = shape == 1 || sequential ? 2 : 3;
            const uint32_t starts[] = { 0, sequential ? 2 : 1, 2 };
            const uint32_t ends[] = { sequential ? 2 : 5, 5, 5 };
            const uint32_t values[] = { 0, sequential ? 0 : 1, 2 };
            const uint8_t classes[] = { (uint8_t)class, (uint8_t)class, (uint8_t)class };
            const uint32_t allowed[] = { shape == 2 || shape == 3 ? low : shape == 4 ? high : mask, mask, mask };
            const uint32_t preferred[] = { shape == 4 ? middle | high : high, shape == 4 ? middle | high : high, high };
            const uint32_t hints[] = { UINT32_MAX, sequential ? 0 : UINT32_MAX, UINT32_MAX };
            const uint8_t static_hints[] = { shape == 3 ? registers[class][0] : registers[class][2], shape == 4 ? registers[class][1] : registers[class][0], UINT8_MAX };
            const uint32_t use_offsets[] = { 0, 1, 2, 3 };
            const uint32_t use_positions[] = { sequential ? 1 : 3, 3, 3 };
            const uint8_t required[] = { 1, 1, 1 };
            const uint32_t fixed_start[] = { 0 };
            const uint32_t fixed_end[] = { 5 };
            const uint32_t fixed_registers[] = { high };
            const uint8_t fixed_classes[] = { (uint8_t)class };
            Tier1AllocationInput input = {
                .starts = starts,
                .ends = ends,
                .values = values,
                .allowed_registers = allowed,
                .preferred_registers = preferred,
                .classes = classes,
                .hints = sequential ? static_hints : 0,
                .hint_values = hints,
                .use_offsets = use_offsets,
                .use_positions = use_positions,
                .use_required = required,
                .use_count = count,
                .count = count,
                .value_count = sequential ? 1 : count,
                .position_count = 5,
                .fixed = {
                    .starts = fixed_start,
                    .ends = fixed_end,
                    .registers = fixed_registers,
                    .classes = fixed_classes,
                    .count = shape == 1 ? 1 : 0,
                },
            };
            input.registers[class] = mask;
            for (uint32_t variant = 0; variant < 6; variant += 1)
            {
                const uint32_t path = variant % 3;
                input.hint_values = variant < 3 ? hints : 0;
                const uint64_t saved = arena->length;
                Tier1Allocation output = { 0 };
                const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : path == 1 ? tier1_allocate_scalar(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
                const bool ready = error == TIER1_ALLOCATION_OK && output.spill_count == 0 && output.fragment_count == count;
                tier1_allocation_test_expect(tally, ready && tier1_allocation_verify(&input, &output) && tier1_allocation_test_occupancy(&input, &output), "preferred registers never force a spill with a legal free fallback");
                if (ready)
                {
                    const uint8_t first = output.registers[output.fragment_offsets[0]];
                    const uint8_t second = output.registers[output.fragment_offsets[1]];
                    const bool expected = shape == 0 ? first == registers[class][2] : shape == 1 ? first != registers[class][2] && second != registers[class][2] :
                                          shape == 2 || shape == 3 ? first == registers[class][0] && second == registers[class][2] :
                                                                   first == registers[class][2] && second == registers[class][1];
                    tier1_allocation_test_expect(tally, expected, "free preferences precede hints, while definition masks and fixed clobbers stay mandatory");
                    tier1_allocation_test_expect(tally, shape != 0 || path == 2 || output.vector_intervals == count, "soft preferences retain the regular level-coloring path");
                }
                if (shape == 0)
                {
                    // A present but empty preference column must retain the old assignment policy as well as validity.
                    const uint32_t neutral[] = { 0, 0, 0 };
                    Tier1AllocationInput plain = input;
                    plain.preferred_registers = 0;
                    Tier1Allocation absent = { 0 };
                    const Tier1AllocationError absent_error = path == 0 ? tier1_allocate(&plain, arena, &absent) : path == 1 ? tier1_allocate_scalar(&plain, arena, &absent) : tier1_allocate_scan_reference(&plain, arena, &absent);
                    plain.preferred_registers = neutral;
                    Tier1Allocation empty = { 0 };
                    const Tier1AllocationError empty_error = path == 0 ? tier1_allocate(&plain, arena, &empty) : path == 1 ? tier1_allocate_scalar(&plain, arena, &empty) : tier1_allocate_scan_reference(&plain, arena, &empty);
                    tier1_allocation_test_expect(tally, absent_error == TIER1_ALLOCATION_OK && empty_error == TIER1_ALLOCATION_OK && tier1_allocation_test_equal(&plain, &absent, &empty), "zero and absent preference columns give identical allocation output");
                }
                arena_pop_to(arena, saved);
            }
        }
        // More live intervals than physical registers forces reload/resplit. The two simultaneous operands must
        // remain pinned even though every interval prefers the same register; a third simultaneous use must fail.
        const uint32_t starts[] = { 0, 0, 1 };
        const uint32_t ends[] = { 10, 10, 8 };
        const uint32_t values[] = { 0, 1, 2 };
        const uint8_t classes[] = { (uint8_t)class, (uint8_t)class, (uint8_t)class };
        const uint32_t allowed[] = { low | high, low | high, low | high };
        const uint32_t preferred[] = { high, high, high };
        const uint32_t offsets[] = { 0, 3, 6, 8 };
        uint32_t uses[] = { 0, 5, 9, 0, 5, 9, 1, 7 };
        const uint8_t required[] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .preferred_registers = preferred,
            .classes = classes,
            .use_offsets = offsets,
            .use_positions = uses,
            .use_required = required,
            .use_count = BUSTER_ARRAY_LENGTH(uses),
            .count = BUSTER_ARRAY_LENGTH(values),
            .value_count = BUSTER_ARRAY_LENGTH(values),
            .position_count = 10,
        };
        input.registers[class] = low | high;
        for (uint32_t simultaneous = 0; simultaneous < 2; simultaneous += 1)
        {
            uses[6] = simultaneous != 0 ? 5 : 1;
            for (uint32_t path = 0; path < 3; path += 1)
            {
                const uint64_t saved = arena->length;
                Tier1Allocation output = { .splits = 987 };
                const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : path == 1 ? tier1_allocate_scalar(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
                if (simultaneous != 0)
                {
                    tier1_allocation_test_expect(tally, error == TIER1_ALLOCATION_CONSTRAINT && output.splits == 987 && arena->length == saved, "preferences cannot evict a simultaneous required operand and failure remains transactional");
                }
                else
                {
                    const bool ready = error == TIER1_ALLOCATION_OK && output.scalar_intervals == input.count && output.spill_count != 0 && output.fragment_count > input.count;
                    tier1_allocation_test_expect(tally, ready && tier1_allocation_verify(&input, &output) && tier1_allocation_test_occupancy(&input, &output), "preferred required-use reloads preserve pinned operands under genuine register pressure");
                }
                arena_pop_to(arena, saved);
            }
        }
    }
}

static void tier1_allocation_test_hints(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed)
{
    tier1_allocation_test_preferences(tally, arena);
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
    // Random ranges of a few values each, adjacent and apart, every one hinted to a random value of its class or to
    // none, at pressures to the class's registers and past them, with required uses and fixed ranges in some.
    uint32_t starts[128];
    uint32_t ends[128];
    uint32_t values[128];
    uint32_t allowed[128];
    uint32_t preferred[128];
    uint8_t classes[128];
    uint32_t hint_values[128];
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
            preferred[i] = allowed[i] & (uint32_t)tier1_allocation_test_random(&seed);
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
            .preferred_registers = iteration % 3 == 1 ? preferred : 0,
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
            memset(last, UINT8_MAX, sizeof(last));
            for (uint32_t class = 0; !constrained && class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
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
    tier1_allocation_test_expect(tally, compared > 1000, "the hint reference compares many colored classes");
}
