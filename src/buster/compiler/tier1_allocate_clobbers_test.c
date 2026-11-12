// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clobbers (Tier1AllocationInput.clobbers, #2574): literal traces of a recomputable value that a clobber evicts from its
// register and that comes back at its next required use, and of the values it leaves alone; the refusals of malformed
// clobbers; and random inputs on all three entry points against the interval verifier, the position occupancy oracle and
// two independent properties: no recomputable value holds a clobbered register across a clobber but where a use requires
// it there, and clobbers change no allocation of values without recipes. Included by tier1_allocate_test_cases.c.

// The literal traces: one general class of r0 and r1, two values, a clobber at position 4.
static void tier1_allocation_test_clobber_traces(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[][2] = {
        { 0, 0 },
        { 0, 0 },
        { 4, 0 },
        { 0, 0 },
        { 0, 0 },
    };
    const uint32_t ends[][2] = {
        { 10, 11 },
        { 10, 11 },
        { 10, 11 },
        { 10, 11 },
        { 10, 11 },
    };
    const uint32_t use_offsets[][3] = {
        { 0, 2, 4 },
        { 0, 2, 4 },
        { 0, 2, 4 },
        { 0, 3, 5 },
        { 0, 2, 4 },
    };
    const uint32_t use_positions[][5] = {
        { 0, 8, 0, 10 },
        { 0, 8, 0, 10 },
        { 4, 8, 0, 10 },
        { 0, 4, 8, 0, 10 },
        { 0, 8, 0, 10 },
    };
    const uint8_t use_required[][5] = {
        { 1, 1, 1, 1 },
        { 1, 1, 1, 1 },
        { 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1 },
        { 1, 0, 1, 1 },
    };
    const uint32_t clobber_masks[] = { 3, 2, 3, 3, 3 };
    const uint32_t fragments[] = { 4, 2, 2, 2, 3 };
    const uint8_t expected_kinds[][4] = {
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REGISTER },
        { BUSTER_TIER1_ALLOCATE_REGISTER, BUSTER_TIER1_ALLOCATE_REMATERIALIZE, BUSTER_TIER1_ALLOCATE_REGISTER },
    };
    const uint8_t expected_registers[][4] = {
        { 0, UINT8_MAX, 0, 1 },
        { 0, 1 },
        { 1, 0 },
        { 0, 1 },
        { 0, UINT8_MAX, 1 },
    };
    const uint32_t expected_starts[][4] = {
        { 0, 4, 8, 0 },
        { 0, 0 },
        { 4, 0 },
        { 0, 0 },
        { 0, 4, 0 },
    };
    const char* names[] = {
        "a clobber evicts a recipe from its register, which its next required use takes back",
        "a clobber leaves a recipe in a register outside its mask",
        "a clobber doesn't reach a recipe that starts at it",
        "a recipe a use requires at the clobber keeps its register",
        "a recipe with no required use after the clobber stays its recipe to its end",
    };
    const uint32_t values[2] = { 0, 1 };
    const uint32_t allowed[2] = { 3, 3 };
    const uint8_t classes[2] = { BUSTER_TIER1_ALLOCATE_GPR, BUSTER_TIER1_ALLOCATE_GPR };
    // The first value has a recipe, the second none: a clobber leaves the second in its register, which its consumer saves.
    const uint8_t rematerializable[2] = { 1, 0 };
    const uint32_t clobbers[1] = { 4 };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(names); fixture += 1)
    {
        const Tier1AllocationInput input = {
            .starts = starts[fixture],
            .ends = ends[fixture],
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .rematerializable = rematerializable,
            .use_offsets = use_offsets[fixture],
            .use_positions = use_positions[fixture],
            .use_required = use_required[fixture],
            .clobbers = clobbers,
            .clobber_count = 1,
            .clobber_registers = { clobber_masks[fixture] },
            .use_count = use_offsets[fixture][2],
            .count = 2,
            .value_count = 2,
            .position_count = 12,
            .registers = { 3 },
        };
        Tier1Allocation outputs[3] = { { 0 } };
        bool produced = true;
        const uint64_t saved = arena->length;
        for (uint32_t path = 0; path < 3; path += 1)
        {
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &outputs[path]) : path == 1 ? tier1_allocate_scalar(&input, arena, &outputs[path]) : tier1_allocate_scan_reference(&input, arena, &outputs[path]);
            const Tier1Allocation* output = &outputs[path];
            bool valid = error == TIER1_ALLOCATION_OK && output->fragment_count == fragments[fixture] && tier1_allocation_verify(&input, output) && tier1_allocation_test_occupancy(&input, output);
            valid = valid && memcmp(output->kinds, expected_kinds[fixture], fragments[fixture]) == 0 && memcmp(output->registers, expected_registers[fixture], fragments[fixture]) == 0;
            valid = valid && memcmp(output->starts, expected_starts[fixture], fragments[fixture] * sizeof(uint32_t)) == 0 && output->spill_count == 0;
            produced &= error == TIER1_ALLOCATION_OK;
            tier1_allocation_test_expect(tally, valid, names[fixture]);
        }
        tier1_allocation_test_expect(tally, produced && tier1_allocation_test_equal(&input, &outputs[0], &outputs[1]), "both production paths take the clobbers alike");
        arena_pop_to(arena, saved);
    }
    // Malformed clobbers: without required uses (nothing would take the value back), out of order, past the positions,
    // and over a register outside the class's.
    const uint32_t unordered[2] = { 5, 4 };
    const uint32_t outside[1] = { 12 };
    for (uint32_t refusal = 0; refusal < 4; refusal += 1)
    {
        Tier1AllocationInput input = {
            .starts = starts[0],
            .ends = ends[0],
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .rematerializable = rematerializable,
            .use_offsets = use_offsets[0],
            .use_positions = use_positions[0],
            .use_required = refusal == 0 ? 0 : use_required[0],
            .clobbers = refusal == 1 ? unordered : refusal == 2 ? outside : clobbers,
            .clobber_count = refusal == 1 ? 2 : 1,
            .clobber_registers = { refusal == 3 ? 7u : 3u },
            .use_count = use_offsets[0][2],
            .count = 2,
            .value_count = 2,
            .position_count = 12,
            .registers = { 3 },
        };
        const uint64_t saved = arena->length;
        Tier1Allocation before;
        memset(&before, 0x5a, sizeof(before));
        bool refused = true;
        for (uint32_t path = 0; path < 3; path += 1)
        {
            Tier1Allocation output = before;
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, arena, &output) : path == 1 ? tier1_allocate_scalar(&input, arena, &output) : tier1_allocate_scan_reference(&input, arena, &output);
            refused &= error == TIER1_ALLOCATION_INVALID && arena->length == saved && memcmp(&output, &before, sizeof(output)) == 0;
        }
        tier1_allocation_test_expect(tally, refused, "malformed clobbers are refused, the arena and the result untouched");
    }
}

// Whether no recomputable interval holds a clobbered register at a clobber strictly inside it, but where one of its
// uses requires a register there: each such clobber's position is looked up in the fragments, independently of the scan.
static bool tier1_allocation_test_clobbered_free(const Tier1AllocationInput* input, const Tier1Allocation* output)
{
    bool valid = true;
    for (uint32_t i = 0; valid && i < input->count; i += 1)
    {
        for (uint32_t k = 0; valid && input->rematerializable != 0 && input->rematerializable[i] != 0 && k < input->clobber_count; k += 1)
        {
            const uint32_t q = input->clobbers[k];
            Tier1AllocationLocation location = { 0 };
            if (input->starts[i] < q && q < input->ends[i] && tier1_allocation_location(input, output, i, q, &location) && location.kind == BUSTER_TIER1_ALLOCATE_REGISTER &&
                ((input->clobber_registers[input->classes[i]] >> location.reg) & 1) != 0)
            {
                bool required = false;
                for (uint32_t u = input->use_offsets[i]; u < input->use_offsets[i + 1]; u += 1)
                {
                    required |= input->use_positions[u] == q && input->use_required[u] != 0;
                }
                valid = required;
            }
        }
    }
    return valid;
}

static void tier1_allocation_test_clobbers(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed)
{
    tier1_allocation_test_clobber_traces(tally, arena);
    uint32_t starts[48];
    uint32_t ends[48];
    uint32_t values[48];
    uint32_t allowed[48];
    uint8_t classes[48];
    uint8_t remat[48];
    uint32_t use_offsets[49];
    uint32_t use_positions[48 * 3];
    uint8_t use_required[48 * 3];
    uint32_t clobbers[8];
    for (uint32_t iteration = 0; iteration < 600; iteration += 1)
    {
        const uint64_t saved = arena->length;
        const uint32_t count = 1 + tier1_allocation_test_random(&seed) % 48;
        // Two to seven general registers, some of them clobbered at each clobber.
        const uint32_t registers = (1u << (2 + iteration % 6)) - 1;
        uint32_t use_count = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            starts[i] = tier1_allocation_test_random(&seed) % 96;
            ends[i] = starts[i] + 1 + tier1_allocation_test_random(&seed) % 48;
            values[i] = i;
            classes[i] = BUSTER_TIER1_ALLOCATE_GPR;
            allowed[i] = registers;
            remat[i] = (uint8_t)(tier1_allocation_test_random(&seed) & 1);
            use_offsets[i] = use_count;
            // A definition's required use at the start, then up to two later uses, each required or not.
            const uint32_t uses = 1 + tier1_allocation_test_random(&seed) % 3;
            uint32_t position = starts[i];
            for (uint32_t u = 0; u < uses && position < ends[i]; u += 1)
            {
                use_positions[use_count] = position;
                use_required[use_count] = (uint8_t)(u == 0 || (tier1_allocation_test_random(&seed) & 1) != 0);
                use_count += 1;
                position += 1 + tier1_allocation_test_random(&seed) % 16;
            }
        }
        use_offsets[count] = use_count;
        const uint32_t clobber_count = tier1_allocation_test_random(&seed) % (BUSTER_ARRAY_LENGTH(clobbers) + 1);
        for (uint32_t k = 0; k < clobber_count; k += 1)
        {
            clobbers[k] = (k == 0 ? 0 : clobbers[k - 1]) + tier1_allocation_test_random(&seed) % 24;
            clobbers[k] = clobbers[k] < 144 ? clobbers[k] : 143;
        }
        Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .rematerializable = remat,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = use_required,
            .clobbers = clobbers,
            .clobber_count = clobber_count,
            .clobber_registers = { registers & tier1_allocation_test_random(&seed) },
            .use_count = use_count,
            .count = count,
            .value_count = count,
            .position_count = 144,
            .registers = { registers },
        };
        Tier1Allocation outputs[3] = { { 0 } };
        Tier1AllocationError errors[3];
        for (uint32_t path = 0; path < 3; path += 1)
        {
            errors[path] = path == 0 ? tier1_allocate(&input, arena, &outputs[path]) : path == 1 ? tier1_allocate_scalar(&input, arena, &outputs[path]) : tier1_allocate_scan_reference(&input, arena, &outputs[path]);
        }
        const bool produced = errors[0] == TIER1_ALLOCATION_OK && errors[1] == TIER1_ALLOCATION_OK && errors[2] == TIER1_ALLOCATION_OK;
        tier1_allocation_test_expect(tally, produced || (errors[0] == TIER1_ALLOCATION_CONSTRAINT && errors[1] == errors[0] && errors[2] == errors[0]), "clobbered allocation or explicit simultaneous-use conflict");
        for (uint32_t path = 0; produced && path < 3; path += 1)
        {
            tier1_allocation_test_expect(tally, tier1_allocation_verify(&input, &outputs[path]) && tier1_allocation_test_occupancy(&input, &outputs[path]), "clobbered allocation passes the verifier and the occupancy oracle");
            tier1_allocation_test_expect(tally, tier1_allocation_test_clobbered_free(&input, &outputs[path]), "no recipe holds a clobbered register across a clobber but where a use requires it");
        }
        tier1_allocation_test_expect(tally, !produced || tier1_allocation_test_equal(&input, &outputs[0], &outputs[1]), "both production paths take the clobbers alike");
        // Without recipes the clobbers reach no value: each path's allocation is the one without them.
        Tier1AllocationInput plain = input;
        plain.rematerializable = 0;
        Tier1AllocationInput unclobbered = plain;
        unclobbered.clobbers = 0;
        unclobbered.clobber_count = 0;
        unclobbered.clobber_registers[BUSTER_TIER1_ALLOCATE_GPR] = 0;
        for (uint32_t path = 0; path < 3; path += 1)
        {
            Tier1Allocation with = { 0 };
            Tier1Allocation without = { 0 };
            const Tier1AllocationError a = path == 0 ? tier1_allocate(&plain, arena, &with) : path == 1 ? tier1_allocate_scalar(&plain, arena, &with) : tier1_allocate_scan_reference(&plain, arena, &with);
            const Tier1AllocationError b = path == 0 ? tier1_allocate(&unclobbered, arena, &without) : path == 1 ? tier1_allocate_scalar(&unclobbered, arena, &without) : tier1_allocate_scan_reference(&unclobbered, arena, &without);
            tier1_allocation_test_expect(tally, a == b && (a != TIER1_ALLOCATION_OK || tier1_allocation_test_equal(&plain, &with, &without)), "clobbers change no allocation of values without recipes");
        }
        arena_pop_to(arena, saved);
    }
}
