// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Avoided registers (Tier1AllocationInput.avoided_registers, #2767): literal traces of a range that takes another register
// than one it avoids, of one that takes an avoided register where no other is free without evicting the value in the
// other, of a hint to an avoided register that yields to a free one, and of the path a class with avoided registers takes;
// then random inputs on all three entry points against the interval verifier, the position occupancy oracle and two
// independent properties: a range starts in a register it avoids only where every register it may take and doesn't avoid
// is held there, and a column that avoids nothing changes no allocation. Included by tier1_allocate_test_cases.c.

// The paths' allocations of `input`, into `outputs`; their errors.
static void tier1_allocation_test_avoided_paths(const Tier1AllocationInput* input, Arena* arena, Tier1Allocation* outputs, Tier1AllocationError* errors)
{
    for (uint32_t path = 0; path < 3; path += 1)
    {
        outputs[path] = (Tier1Allocation){ 0 };
        errors[path] = path == 0 ? tier1_allocate(input, arena, &outputs[path]) : path == 1 ? tier1_allocate_scalar(input, arena, &outputs[path]) : tier1_allocate_scan_reference(input, arena, &outputs[path]);
    }
}

// The literal traces, each with use columns (a required use at each start, the scan with uses) and without (the plain
// scan): r0 to r2, or r0 and r1 where a trace needs every register held.
static void tier1_allocation_test_avoided_traces(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[][2] = {
        { 0, 0 },
        { 0, 2 },
        { 0, 6 },
        { 0, 0 },
    };
    const uint32_t ends[][2] = {
        { 10, 0 },
        { 10, 10 },
        { 4, 10 },
        { 10, 0 },
    };
    const uint32_t counts[] = { 1, 2, 2, 1 };
    const uint32_t values[][2] = {
        { 0, 0 },
        { 0, 1 },
        { 0, 0 },
        { 0, 0 },
    };
    const uint32_t registers[] = { 7, 3, 7, 7 };
    const uint32_t allowed[][2] = {
        { 7, 0 },
        { 3, 3 },
        { 7, 7 },
        { 7, 0 },
    };
    // The fourth avoids only r3, which the class hasn't.
    const uint32_t avoided[][2] = {
        { 1, 0 },
        { 0, 2 },
        { 0, 1 },
        { 8, 0 },
    };
    // The second interval of the third trace is its value's again, hinted to the register the first one left.
    const bool hinted[] = { false, false, true, false };
    const uint8_t expected[][2] = {
        { 1, 0 },
        { 0, 1 },
        { 0, 1 },
        { 0, 0 },
    };
    const char* names[] = {
        "a range takes another register than one it avoids",
        "a range takes a register it avoids where no other is free, and evicts no value from another",
        "a hint to a register a range avoids yields to a free one",
        "a range that avoids no register it may take",
    };
    const uint32_t hint_values[2] = { UINT32_MAX, 0 };
    const uint8_t classes[2] = { BUSTER_TIER1_ALLOCATE_GPR, BUSTER_TIER1_ALLOCATE_GPR };
    for (uint32_t trace = 0; trace < BUSTER_ARRAY_LENGTH(names); trace += 1)
    {
        for (uint32_t uses = 0; uses < 2; uses += 1)
        {
            const uint32_t count = counts[trace];
            const uint32_t use_offsets[3] = { 0, 1, count };
            const uint32_t use_positions[2] = { starts[trace][0], starts[trace][1] };
            const uint8_t use_required[2] = { 1, 1 };
            const Tier1AllocationInput input = {
                .starts = starts[trace],
                .ends = ends[trace],
                .values = values[trace],
                .allowed_registers = allowed[trace],
                .classes = classes,
                .hint_values = hinted[trace] ? hint_values : 0,
                .avoided_registers = avoided[trace],
                .use_offsets = uses != 0 ? use_offsets : 0,
                .use_positions = uses != 0 ? use_positions : 0,
                .use_required = uses != 0 ? use_required : 0,
                .use_count = uses != 0 ? count : 0,
                .count = count,
                .value_count = values[trace][count - 1] + 1,
                .position_count = 12,
                .registers = { registers[trace] },
            };
            const uint64_t saved = arena->length;
            Tier1Allocation outputs[3];
            Tier1AllocationError errors[3];
            tier1_allocation_test_avoided_paths(&input, arena, outputs, errors);
            for (uint32_t path = 0; path < 3; path += 1)
            {
                const Tier1Allocation* output = &outputs[path];
                bool valid = errors[path] == TIER1_ALLOCATION_OK && output->fragment_count == count && output->spill_count == 0 && tier1_allocation_verify(&input, output) && tier1_allocation_test_occupancy(&input, output);
                for (uint32_t i = 0; valid && i < count; i += 1)
                {
                    const uint32_t f = output->fragment_offsets[i];
                    valid = output->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && output->registers[f] == expected[trace][i] && output->starts[f] == starts[trace][i] && output->ends[f] == ends[trace][i];
                }
                tier1_allocation_test_expect(tally, valid, names[trace]);
                if (!valid && tally->failures < 20)
                {
                    fprintf(stderr, "  avoided trace %u uses=%u path %u: error %u, %u fragments, r%u r%u\n", trace, uses, path, errors[path], output->fragment_count, output->fragment_count > 0 ? output->registers[0] : 99, output->fragment_count > 1 ? output->registers[1] : 99);
                }
            }
            // A class that avoids a register some interval may take is the scan's; one that avoids none colors as before.
            const bool scanned = trace != 3;
            for (uint32_t path = 0; errors[0] == TIER1_ALLOCATION_OK && errors[1] == TIER1_ALLOCATION_OK && path < 2; path += 1)
            {
                const bool colored = outputs[path].vector_intervals == count && outputs[path].scalar_intervals == 0;
                const bool scan = outputs[path].vector_intervals == 0 && outputs[path].scalar_intervals == count;
                tier1_allocation_test_expect(tally, scanned ? scan : trace == 3 && colored, scanned ? "a class with an avoided register is the scan's" : "a class that avoids no register it may take colors as before");
            }
            tier1_allocation_test_expect(tally, errors[0] == TIER1_ALLOCATION_OK && errors[1] == TIER1_ALLOCATION_OK && tier1_allocation_test_equal(&input, &outputs[0], &outputs[1]), "both production paths avoid alike");
            arena_pop_to(arena, saved);
        }
    }
}

// Whether each interval that starts in a register it avoids does so only where every register it may take and doesn't
// avoid is held at its start by another interval's register fragment, read from the fragments alone.
static bool tier1_allocation_test_avoided_held(const Tier1AllocationInput* input, const Tier1Allocation* output)
{
    bool valid = true;
    for (uint32_t i = 0; valid && i < input->count; i += 1)
    {
        const uint32_t f = output->fragment_offsets[i];
        const uint32_t p = input->starts[i];
        const bool avoiding = f < output->fragment_offsets[i + 1] && output->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && output->starts[f] == p && ((input->avoided_registers[i] >> output->registers[f]) & 1) != 0;
        uint32_t held = 0;
        for (uint32_t j = 0; avoiding && j < input->count; j += 1)
        {
            for (uint32_t g = output->fragment_offsets[j]; j != i && input->classes[j] == input->classes[i] && g < output->fragment_offsets[j + 1]; g += 1)
            {
                held |= output->kinds[g] == BUSTER_TIER1_ALLOCATE_REGISTER && output->starts[g] <= p && p < output->ends[g] ? 1u << output->registers[g] : 0;
            }
        }
        valid = !avoiding || (input->allowed_registers[i] & ~input->avoided_registers[i] & ~held) == 0;
    }
    return valid;
}

static void tier1_allocation_test_avoided(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed)
{
    tier1_allocation_test_avoided_traces(tally, arena);
    uint32_t starts[48];
    uint32_t ends[48];
    uint32_t values[48];
    uint32_t allowed[48];
    uint32_t avoided[48];
    uint32_t none[48];
    uint32_t hints[48];
    uint8_t classes[48];
    uint8_t remat[48];
    uint32_t use_offsets[49];
    uint32_t use_positions[48 * 3];
    uint8_t use_required[48 * 3];
    uint64_t avoiding = 0;
    for (uint32_t iteration = 0; iteration < 600; iteration += 1)
    {
        const uint64_t saved = arena->length;
        const uint32_t count = 1 + tier1_allocation_test_random(&seed) % 48;
        // Two to seven general registers, of which each interval avoids some, a third of them none.
        const uint32_t registers = (1u << (2 + iteration % 6)) - 1;
        const bool uses = iteration % 3 != 0;
        uint32_t use_count = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            starts[i] = tier1_allocation_test_random(&seed) % 96;
            ends[i] = starts[i] + 1 + tier1_allocation_test_random(&seed) % 48;
            values[i] = i;
            classes[i] = BUSTER_TIER1_ALLOCATE_GPR;
            allowed[i] = registers;
            avoided[i] = tier1_allocation_test_random(&seed) % 3 == 0 ? 0 : registers & tier1_allocation_test_random(&seed);
            none[i] = 0;
            hints[i] = iteration % 2 == 0 ? (uint32_t)(tier1_allocation_test_random(&seed) % count) : UINT32_MAX;
            remat[i] = (uint8_t)(tier1_allocation_test_random(&seed) & 1);
            use_offsets[i] = use_count;
            // A definition's required use at the start, then up to two later uses, each required or not.
            const uint32_t drawn = 1 + tier1_allocation_test_random(&seed) % 3;
            uint32_t position = starts[i];
            for (uint32_t u = 0; uses && u < drawn && position < ends[i]; u += 1)
            {
                use_positions[use_count] = position;
                use_required[use_count] = (uint8_t)(u == 0 || (tier1_allocation_test_random(&seed) & 1) != 0);
                use_count += 1;
                position += 1 + tier1_allocation_test_random(&seed) % 16;
            }
        }
        use_offsets[count] = use_count;
        const Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hint_values = hints[0] != UINT32_MAX ? hints : 0,
            .avoided_registers = avoided,
            .rematerializable = remat,
            .use_offsets = uses ? use_offsets : 0,
            .use_positions = uses ? use_positions : 0,
            .use_required = uses ? use_required : 0,
            .use_count = use_count,
            .count = count,
            .value_count = count,
            .position_count = 144,
            .registers = { registers },
        };
        Tier1Allocation outputs[3];
        Tier1AllocationError errors[3];
        tier1_allocation_test_avoided_paths(&input, arena, outputs, errors);
        const bool produced = errors[0] == TIER1_ALLOCATION_OK && errors[1] == TIER1_ALLOCATION_OK && errors[2] == TIER1_ALLOCATION_OK;
        tier1_allocation_test_expect(tally, produced || (errors[0] == TIER1_ALLOCATION_CONSTRAINT && errors[1] == errors[0] && errors[2] == errors[0]), "an allocation with avoided registers, or an explicit simultaneous-use conflict");
        for (uint32_t path = 0; produced && path < 3; path += 1)
        {
            tier1_allocation_test_expect(tally, tier1_allocation_verify(&input, &outputs[path]) && tier1_allocation_test_occupancy(&input, &outputs[path]), "an allocation with avoided registers passes the verifier and the occupancy oracle");
            tier1_allocation_test_expect(tally, tier1_allocation_test_avoided_held(&input, &outputs[path]), "a range starts in a register it avoids only where every other it may take is held");
        }
        tier1_allocation_test_expect(tally, !produced || tier1_allocation_test_equal(&input, &outputs[0], &outputs[1]), "both production paths avoid alike");
        for (uint32_t i = 0; produced && i < count; i += 1)
        {
            const uint32_t f = outputs[0].fragment_offsets[i];
            avoiding += f < outputs[0].fragment_offsets[i + 1] && outputs[0].kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && ((avoided[i] >> outputs[0].registers[f]) & 1) != 0;
        }
        // A column that avoids nothing changes nothing: each path's allocation is the one without it.
        Tier1AllocationInput empty = input;
        empty.avoided_registers = none;
        Tier1AllocationInput absent = input;
        absent.avoided_registers = 0;
        Tier1Allocation with[3];
        Tier1Allocation without[3];
        Tier1AllocationError a[3];
        Tier1AllocationError b[3];
        tier1_allocation_test_avoided_paths(&empty, arena, with, a);
        tier1_allocation_test_avoided_paths(&absent, arena, without, b);
        for (uint32_t path = 0; path < 3; path += 1)
        {
            tier1_allocation_test_expect(tally, a[path] == b[path] && (a[path] != TIER1_ALLOCATION_OK || tier1_allocation_test_equal(&empty, &with[path], &without[path])), "a column that avoids nothing changes no allocation");
        }
        arena_pop_to(arena, saved);
    }
    // Some ranges must have taken a register they avoid, where every other was held: the property's own case.
    tier1_allocation_test_expect(tally, avoiding != 0, "the random inputs reach ranges in registers they avoid");
}
