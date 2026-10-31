// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Allocator qualification on original generated intervals and native straight-line reductions. These fixtures do
// not claim source/CFG/SQLite coverage. Include after tier1_allocate.c and tier1_rematerialize.c.

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

typedef struct Tier1AllocationTestTally Tier1AllocationTestTally;
struct Tier1AllocationTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static void tier1_allocation_test_expect(Tier1AllocationTestTally* tally, bool condition, const char* name)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 20)
        {
            fprintf(stderr, "test-tier1-allocation: %s\n", name);
        }
        tally->failures += 1;
    }
}

static uint32_t tier1_allocation_test_random(uint64_t* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(*state >> 32);
}

// The occupancy oracle expands every fragment to individual positions; it shares neither event sorting nor
// interval-overlap tests with the allocator. All generated inputs below fit this deliberately small domain.
static bool tier1_allocation_test_occupancy(const Tier1AllocationInput* input, const Tier1Allocation* output)
{
    uint32_t owners[2][32][256];
    memset(owners, 0xff, sizeof(owners));
    bool valid = true;
    for (uint32_t i = 0; valid && i < input->count; i += 1)
    {
        for (uint32_t p = input->starts[i]; valid && p < input->ends[i]; p += 1)
        {
            Tier1AllocationLocation location = { 0 };
            valid = p < 256 && tier1_allocation_location(input, output, i, p, &location);
            if (valid && location.kind == BUSTER_TIER1_ALLOCATE_REGISTER)
            {
                valid = location.reg < 32 && owners[input->classes[i]][location.reg][p] == UINT32_MAX;
                if (valid)
                {
                    owners[input->classes[i]][location.reg][p] = i;
                }
            }
        }
    }
    for (uint32_t f = 0; valid && f < input->fixed.count; f += 1)
    {
        for (uint32_t r = 0; valid && r < 32; r += 1)
        {
            if (((input->fixed.registers[f] >> r) & 1) != 0)
            {
                for (uint32_t p = input->fixed.starts[f]; valid && p < input->fixed.ends[f]; p += 1)
                {
                    valid = p < 256 && owners[input->fixed.classes[f]][r][p] == UINT32_MAX;
                }
            }
        }
    }
    return valid;
}

static bool tier1_allocation_test_equal(const Tier1AllocationInput* input, const Tier1Allocation* a, const Tier1Allocation* b)
{
    bool same = a->fragment_count == b->fragment_count && a->spill_count == b->spill_count && a->vector_intervals == b->vector_intervals && a->scalar_intervals == b->scalar_intervals;
    if (same)
    {
        same = memcmp(a->fragment_offsets, b->fragment_offsets, ((uint64_t)input->count + 1) * 4) == 0 && memcmp(a->starts, b->starts, (uint64_t)a->fragment_count * 4) == 0 && memcmp(a->ends, b->ends, (uint64_t)a->fragment_count * 4) == 0;
        same = same && memcmp(a->kinds, b->kinds, a->fragment_count) == 0 && memcmp(a->registers, b->registers, a->fragment_count) == 0 && memcmp(a->value_registers, b->value_registers, input->value_count) == 0;
        same = same && memcmp(a->spill_ids, b->spill_ids, (uint64_t)input->value_count * 4) == 0 && memcmp(a->spill_values, b->spill_values, (uint64_t)a->spill_count * 4) == 0;
    }
    return same;
}

// tier1_allocation_color's LIFO level rule written independently: positions in order, each position's ends and then
// its starts in input order, without the event sort or the class's event column. An end at height h + 1 deposits
// its register at level h; a start at height h takes level h's register, or a fresh one. False when the class's
// pressure exceeds its registers, which sends it to the scan instead.
static bool tier1_allocation_test_levels(const Tier1AllocationInput* input, uint32_t class, uint8_t* regs)
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
                    levels[height] = UINT8_MAX;
                    if (r == UINT8_MAX)
                    {
                        r = (uint8_t)__builtin_ctz(fresh);
                        fresh &= fresh - 1;
                    }
                    regs[i] = r;
                    height += 1;
                }
            }
        }
    }
    return fits;
}

static void tier1_allocation_test_generated(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed, uint32_t iterations)
{
    uint32_t starts[96];
    uint32_t ends[96];
    uint32_t values[96];
    uint32_t allowed[96];
    uint8_t classes[96];
    uint8_t remat[96];
    uint8_t hints[96];
    uint8_t required[96];
    uint32_t use_offsets[97];
    uint32_t use_positions[96];
    const uint32_t fixed_start[] = { 23, 70, 90, 24, 0, 90, 128 };
    const uint32_t fixed_end[] = { 25, 71, 96, 26, 2, 97, 129 };
    const uint32_t fixed_regs[] = { 0x3, 0xf, 0x80000000u, 0x2, 0, 0x80000001u, 0x1 };
    const uint8_t fixed_classes[] = { 0, 0, 1, 0, 1, 1, 0 };
    for (uint32_t iteration = 0; iteration < iterations; iteration += 1)
    {
        const uint64_t saved = arena->length;
        const uint32_t count = iteration < 96 ? iteration : tier1_allocation_test_random(&seed) % 96;
        const bool constrained = (iteration % 3) == 0;
        const bool sparse = (iteration % 3) == 1;
        const uint32_t gpr_mask = sparse ? 0xaaaa : (1u << (1 + iteration % 8)) - 1;
        for (uint32_t i = 0; i < count; i += 1)
        {
            starts[i] = sparse ? i * 2 : tier1_allocation_test_random(&seed) % 96;
            ends[i] = sparse ? starts[i] + 2 : starts[i] + 1 + tier1_allocation_test_random(&seed) % 64;
            values[i] = i;
            classes[i] = (uint8_t)(tier1_allocation_test_random(&seed) & 1);
            allowed[i] = classes[i] == 0 ? gpr_mask : UINT32_MAX;
            if (constrained && (i & 3) == 0)
            {
                allowed[i] &= tier1_allocation_test_random(&seed);
            }
            remat[i] = (uint8_t)(i & 1);
            hints[i] = allowed[i] != 0 ? (uint8_t)__builtin_ctz(allowed[i]) : UINT8_MAX;
            use_offsets[i] = i;
            use_positions[i] = ends[i] - 1;
            required[i] = (uint8_t)(i & 1);
        }
        use_offsets[count] = count;
        Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .hints = hints,
            .rematerializable = remat,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = iteration % 5 == 0 ? required : 0,
            .use_count = count,
            .count = count,
            .value_count = count,
            .position_count = 192,
            .registers = { gpr_mask, UINT32_MAX },
            .fixed = {
                .starts = fixed_start,
                .ends = fixed_end,
                .registers = fixed_regs,
                .classes = fixed_classes,
                .count = constrained ? 7 : 0,
            },
        };
        Tier1Allocation vector = { 0 };
        Tier1Allocation scalar = { 0 };
        Tier1Allocation again = { 0 };
        const Tier1AllocationError vector_error = tier1_allocate(&input, arena, &vector);
        const Tier1AllocationError scalar_error = tier1_allocate_scalar(&input, arena, &scalar);
        const Tier1AllocationError again_error = tier1_allocate(&input, arena, &again);
        const bool produced = vector_error == TIER1_ALLOCATION_OK && scalar_error == TIER1_ALLOCATION_OK && again_error == TIER1_ALLOCATION_OK;
        tier1_allocation_test_expect(tally, produced || (input.use_required != 0 && vector_error == TIER1_ALLOCATION_CONSTRAINT && scalar_error == vector_error && again_error == vector_error), "generated allocation or explicit simultaneous-use conflict");
        if (produced)
        {
            tier1_allocation_test_expect(tally, tier1_allocation_verify(&input, &vector) && tier1_allocation_verify(&input, &scalar), "independent interval verifier");
            tier1_allocation_test_expect(tally, tier1_allocation_test_occupancy(&input, &vector) && tier1_allocation_test_occupancy(&input, &scalar), "position occupancy oracle");
            tier1_allocation_test_expect(tally, tier1_allocation_test_equal(&input, &vector, &again), "deterministic repeat");
            tier1_allocation_test_expect(tally, vector.vector_intervals + vector.scalar_intervals == count, "fallback accounting");
            // Without fixed ranges every interval has its class's full mask, so a class that fits is colored.
            for (uint32_t class = 0; BUSTER_HOST_VECTOR && !constrained && class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
            {
                uint8_t reference[96];
                if (tier1_allocation_test_levels(&input, class, reference))
                {
                    bool same = true;
                    for (uint32_t i = 0; i < count; i += 1)
                    {
                        const uint32_t f = vector.fragment_offsets[i];
                        same &= classes[i] != class || (vector.fragment_offsets[i + 1] == f + 1 && vector.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && vector.registers[f] == reference[i]);
                    }
                    tier1_allocation_test_expect(tally, same, "LIFO reuse matches the independent level-rule reference");
                }
            }
            if (sparse)
            {
                tier1_allocation_test_expect(tally, vector.spill_count == 0 &&
                                                    vector.vector_intervals == (BUSTER_HOST_VECTOR ? count : 0) &&
                                                    vector.scalar_intervals == (BUSTER_HOST_VECTOR ? 0 : count), "pressure-fitting reuse chains");
            }
        }
        arena_pop_to(arena, saved);
    }
}

// A position bitmap is independent of the prefix-maximum query and of the scalar overlap predicate. Compare
// masks directly, then require the whole allocator to match an explicitly prefiltered no-fixed input exactly.
static void tier1_allocation_test_fixed(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed)
{
    uint32_t starts[96];
    uint32_t ends[96];
    uint32_t values[96];
    uint32_t allowed[96];
    uint8_t classes[96];
    uint32_t fixed_starts[96];
    uint32_t fixed_ends[96];
    uint32_t fixed_registers[96];
    uint8_t fixed_classes[96];
    uint32_t use_offsets[97];
    uint32_t use_positions[96];
    uint8_t use_required[96];
    for (uint32_t iteration = 0; iteration < 388; iteration += 1)
    {
        const uint64_t saved = arena->length;
        const uint32_t count = iteration % 97;
        const uint32_t fixed_count = (iteration * 17) % 97;
        for (uint32_t i = 0; i < count; i += 1)
        {
            starts[i] = iteration % 4 == 0 ? i * 2 : tier1_allocation_test_random(&seed) % 192;
            ends[i] = iteration % 4 == 0 ? starts[i] + 1 : starts[i] + 1 + tier1_allocation_test_random(&seed) % (192 - starts[i]);
            values[i] = i;
            classes[i] = (uint8_t)(tier1_allocation_test_random(&seed) & 1);
            allowed[i] = tier1_allocation_test_random(&seed) & (classes[i] == 0 ? 0xffff : UINT32_MAX);
            use_offsets[i] = i;
            use_positions[i] = starts[i];
            use_required[i] = allowed[i] != 0;
        }
        use_offsets[count] = count;
        uint32_t occupied[BUSTER_TIER1_ALLOCATE_CLASSES][192] = { 0 };
        for (uint32_t f = 0; f < fixed_count; f += 1)
        {
            fixed_starts[f] = iteration % 4 == 0 ? f * 2 + 1 : tier1_allocation_test_random(&seed) % 192;
            fixed_ends[f] = iteration % 4 == 0 ? fixed_starts[f] + 1 : fixed_starts[f] + 1 + tier1_allocation_test_random(&seed) % (192 - fixed_starts[f]);
            if (iteration % 4 == 1)
            {
                fixed_starts[f] = 23;
                fixed_ends[f] = 24 + f;
            }
            fixed_classes[f] = (uint8_t)(tier1_allocation_test_random(&seed) & 1);
            fixed_registers[f] = tier1_allocation_test_random(&seed) & (fixed_classes[f] == 0 ? 0xffff : UINT32_MAX);
            for (uint32_t p = fixed_starts[f]; p < fixed_ends[f]; p += 1)
            {
                occupied[fixed_classes[f]][p] |= fixed_registers[f];
            }
        }
        uint32_t expected[96];
        uint32_t actual[96];
        uint32_t scalar[96];
        for (uint32_t i = 0; i < count; i += 1)
        {
            uint32_t blocked = 0;
            for (uint32_t p = starts[i]; p < ends[i]; p += 1)
            {
                blocked |= occupied[classes[i]][p];
            }
            expected[i] = allowed[i] & ~blocked;
            actual[i] = allowed[i];
            scalar[i] = allowed[i];
        }
        uint32_t events[288];
        uint32_t event_count = 0;
        for (uint32_t p = 0; p <= 192; p += 1)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                if (ends[i] == p)
                {
                    events[event_count++] = i * 2;
                }
                if (starts[i] == p)
                {
                    events[event_count++] = i * 2 + 1;
                    events[event_count++] = BUSTER_TIER1_ALLOCATE_USE_EVENT | i;
                }
            }
        }
        uint32_t buckets[194];
        Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .use_offsets = iteration % 4 == 0 ? use_offsets : 0,
            .use_positions = iteration % 4 == 0 ? use_positions : 0,
            .use_required = iteration % 4 == 0 ? use_required : 0,
            .use_count = iteration % 4 == 0 ? count : 0,
            .count = count,
            .value_count = count,
            .position_count = 192,
            .registers = { 0xffff, UINT32_MAX },
            .fixed = {
                .starts = fixed_starts,
                .ends = fixed_ends,
                .registers = fixed_registers,
                .classes = fixed_classes,
                .count = fixed_count,
            },
        };
        const bool filtered = tier1_allocation_filter_fixed(&input, arena, true, events, event_count, buckets, actual);
        tier1_allocation_filter_fixed_simple(&input, false, scalar);
        tier1_allocation_test_expect(tally, filtered && memcmp(expected, actual, count * 4) == 0 && memcmp(expected, scalar, count * 4) == 0, "fixed masks match independent position bitmap and cross-product");
        arena_pop_to(arena, saved);
        Tier1AllocationInput prefiltered = input;
        prefiltered.allowed_registers = expected;
        prefiltered.fixed.count = 0;
        Tier1Allocation output = { 0 };
        Tier1Allocation reference = { 0 };
        const bool produced = tier1_allocate(&input, arena, &output) == TIER1_ALLOCATION_OK && tier1_allocate(&prefiltered, arena, &reference) == TIER1_ALLOCATION_OK;
        tier1_allocation_test_expect(tally, produced && tier1_allocation_test_equal(&input, &output, &reference), "fixed filtering preserves complete allocation decisions");
        tier1_allocation_test_expect(tally, produced && tier1_allocation_test_occupancy(&input, &output), "fixed allocation satisfies independent position occupancy");
        arena_pop_to(arena, saved);
        if (count == 96)
        {
            uint8_t memory[32768];
            bool succeeded = false;
            memset(memory, 0x6d, 19);
            for (uint32_t capacity = 19; capacity <= sizeof(memory); capacity += 31)
            {
                Arena small = {
                    .base = memory,
                    .length = 19,
                    .capacity = capacity,
                };
                Tier1Allocation before = { .splits = 1234 };
                Tier1Allocation limited = before;
                const Tier1AllocationError error = tier1_allocate(&input, &small, &limited);
                succeeded |= error == TIER1_ALLOCATION_OK;
                bool prefix = true;
                for (uint32_t i = 0; i < 19; i += 1)
                {
                    prefix &= memory[i] == 0x6d;
                }
                tier1_allocation_test_expect(tally, prefix && (error == TIER1_ALLOCATION_OK || (error == TIER1_ALLOCATION_CAPACITY && small.length == 19 && memcmp(&limited, &before, sizeof(before)) == 0)), "fixed-index capacity failure preserves prefix, arena and result");
            }
            tier1_allocation_test_expect(tally, succeeded, "fixed-index capacity sweep reaches success");
        }
    }
}

static void tier1_allocation_test_fixed_sparse(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint64_t saved = arena->length;
    const uint32_t count = 8192;
    uint32_t* starts = arena_push_array(arena, count, 4, 4);
    uint32_t* ends = arena_push_array(arena, count, 4, 4);
    uint32_t* values = arena_push_array(arena, count, 4, 4);
    uint32_t* allowed = arena_push_array(arena, count, 4, 4);
    uint32_t* fixed_ends = arena_push_array(arena, count, 4, 4);
    uint32_t* fixed_registers = arena_push_array(arena, count, 4, 4);
    uint8_t* classes = arena_push_array(arena, count, 1, 1);
    bool valid = starts != 0 && ends != 0 && values != 0 && allowed != 0 && fixed_ends != 0 && fixed_registers != 0 && classes != 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        starts[i] = i * 2;
        ends[i] = i * 2 + 1;
        values[i] = i;
        allowed[i] = 3;
        fixed_ends[i] = i * 2 + 2;
        fixed_registers[i] = 1;
        classes[i] = 0;
    }
    const Tier1AllocationInput input = {
        .starts = starts,
        .ends = ends,
        .values = values,
        .allowed_registers = allowed,
        .classes = classes,
        .count = count,
        .value_count = count,
        .position_count = count * 2,
        .registers = { 3, 0 },
        .fixed = {
            .starts = ends,
            .ends = fixed_ends,
            .registers = fixed_registers,
            .classes = classes,
            .count = count,
        },
    };
    Tier1Allocation output = { 0 };
    valid = valid && tier1_allocate(&input, arena, &output) == TIER1_ALLOCATION_OK &&
            output.vector_intervals == (BUSTER_HOST_VECTOR ? count : 0) && output.scalar_intervals == (BUSTER_HOST_VECTOR ? 0 : count) &&
            output.spill_count == 0 && output.fragment_count == count;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        valid = output.value_registers[i] == 0 && output.starts[i] == starts[i] && output.ends[i] == ends[i];
    }
    tier1_allocation_test_expect(tally, valid, "8192 adjacent half-open fixed ranges remove no registers");
    arena_pop_to(arena, saved);
}

static void tier1_allocation_test_failures(Tier1AllocationTestTally* tally, Arena* arena)
{
    uint32_t starts[] = { 0, 1, 2 };
    uint32_t ends[] = { 8, 5, 4 };
    uint32_t values[] = { 0, 1, 2 };
    uint32_t allowed[] = { 1, 1, 1 };
    uint8_t classes[] = { 0, 0, 0 };
    uint8_t flags[] = { 0, 0, 0 };
    uint32_t use_offsets[] = { 0, 1, 2, 3 };
    uint32_t use_positions[] = { 7, 4, 3 };
    Tier1AllocationInput input = {
        .starts = starts,
        .ends = ends,
        .values = values,
        .allowed_registers = allowed,
        .classes = classes,
        .flags = flags,
        .use_offsets = use_offsets,
        .use_positions = use_positions,
        .use_count = 3,
        .count = 3,
        .value_count = 3,
        .position_count = 8,
        .registers = { 1, 0 },
    };
    const uint64_t saved = arena->length;
    Tier1Allocation output = { 0 };
    tier1_allocation_test_expect(tally, tier1_allocate(&input, arena, &output) == TIER1_ALLOCATION_OK && output.splits == 2 && output.spill_count == 2, "next-use evictions split prefixes");
    if (output.fragment_count != 0)
    {
        const uint32_t old = output.ends[0];
        output.ends[0] = 8;
        tier1_allocation_test_expect(tally, !tier1_allocation_verify(&input, &output), "verifier rejects corrupted overlap");
        output.ends[0] = old;
    }
    arena_pop_to(arena, saved);
    for (uint32_t failure = 0; failure < 12; failure += 1)
    {
        Tier1AllocationInput broken = input;
        Tier1Allocation before;
        memset(&before, 0xa5, sizeof(before));
        output = before;
        switch (failure)
        {
        case 0:
            broken.count = UINT32_MAX;
            break;
        case 1:
            broken.position_count = UINT32_MAX;
            break;
        case 2:
            broken.classes = 0;
            break;
        case 3:
            ends[0] = 0;
            break;
        case 4:
            classes[0] = 2;
            break;
        case 5:
            values[1] = 0;
            break;
        case 6:
            allowed[0] = 2;
            break;
        case 7:
            use_positions[0] = 8;
            break;
        case 8:
            use_offsets[2] = 4;
            break;
        case 9:
            flags[0] = flags[1] = BUSTER_TIER1_ALLOCATE_REQUIRED;
            break;
        case 10:
            broken.fixed.count = 1;
            break;
        default:
            broken.registers[0] = 0x10000;
            break;
        }
        const Tier1AllocationError error = tier1_allocate(&broken, arena, &output);
        tier1_allocation_test_expect(tally, error == (failure == 9 ? TIER1_ALLOCATION_CONSTRAINT : TIER1_ALLOCATION_INVALID) && arena->length == saved && memcmp(&output, &before, sizeof(output)) == 0, "invalid/constraint transaction");
        ends[0] = 8;
        classes[0] = 0;
        values[1] = 1;
        allowed[0] = 1;
        use_positions[0] = 7;
        use_offsets[2] = 2;
        flags[0] = flags[1] = 0;
    }
    uint8_t memory[1024];
    for (uint32_t capacity = 0; capacity < sizeof(memory); capacity += 7)
    {
        Arena small = {
            .base = memory,
            .capacity = capacity,
        };
        Tier1Allocation before = { .splits = 1234 };
        output = before;
        const Tier1AllocationError error = tier1_allocate(&input, &small, &output);
        tier1_allocation_test_expect(tally, error == TIER1_ALLOCATION_OK || (error == TIER1_ALLOCATION_CAPACITY && small.length == 0 && memcmp(&output, &before, sizeof(output)) == 0), "capacity transaction at every allocation boundary");
    }
}

static void tier1_allocation_test_moves(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed)
{
    uint32_t sources[32];
    uint32_t destinations[32];
    for (uint32_t iteration = 0; iteration < 256; iteration += 1)
    {
        const uint64_t saved = arena->length;
        const uint32_t count = iteration % 33;
        uint64_t original[33];
        uint64_t actual[33];
        for (uint32_t i = 0; i < 33; i += 1)
        {
            original[i] = UINT64_C(0x0123456789abcdef) * (i + 1);
            actual[i] = original[i];
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            destinations[i] = i;
            sources[i] = iteration % 2 == 0 ? (i + 1) % count : tier1_allocation_test_random(&seed) % 32;
        }
        for (uint32_t i = count; i > 1; i -= 1)
        {
            const uint32_t other = tier1_allocation_test_random(&seed) % i;
            const uint32_t swap = destinations[i - 1];
            destinations[i - 1] = destinations[other];
            destinations[other] = swap;
        }
        const Tier1ParallelMoves input = {
            .sources = sources,
            .destinations = destinations,
            .count = count,
            .scratch = 32,
        };
        Tier1ResolvedMoves output = { 0 };
        bool valid = tier1_resolve_moves(&input, arena, &output) == TIER1_ALLOCATION_OK && output.count <= count * 2;
        Tier1ResolvedMoves oracle = { 0 };
        valid = valid && tier1_resolve_moves_scalar(&input, arena, &oracle) == TIER1_ALLOCATION_OK;
        valid = valid && oracle.count == output.count && memcmp(oracle.sources, output.sources, output.count * 4) == 0 && memcmp(oracle.destinations, output.destinations, output.count * 4) == 0;
        for (uint32_t i = 0; valid && i < output.count; i += 1)
        {
            valid = output.sources[i] < 33 && output.destinations[i] < 33;
            if (valid)
            {
                actual[output.destinations[i]] = actual[output.sources[i]];
            }
        }
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            valid = actual[destinations[i]] == original[sources[i]];
        }
        tier1_allocation_test_expect(tally, valid, "parallel move snapshot semantics");
        arena_pop_to(arena, saved);
    }
    sources[0] = 0;
    sources[1] = 1;
    destinations[0] = destinations[1] = 2;
    const Tier1ParallelMoves broken = {
        .sources = sources,
        .destinations = destinations,
        .count = 2,
        .scratch = 32,
    };
    Tier1ResolvedMoves output = { 0 };
    tier1_allocation_test_expect(tally, tier1_resolve_moves(&broken, arena, &output) == TIER1_ALLOCATION_INVALID, "duplicate move destinations rejected");
}

static void tier1_allocation_test_required(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint32_t starts[] = { 0, 1 };
    const uint32_t ends[] = { 10, 10 };
    const uint32_t ids[] = { 0, 1 };
    const uint32_t allowed[] = { 2, 2 };
    const uint8_t classes[] = { 0, 0 };
    const uint32_t offsets[] = { 0, 4, 8 };
    uint32_t uses[] = { 2, 4, 6, 8, 3, 5, 7, 9 };
    const uint8_t required[] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    const Tier1AllocationInput input = {
        .starts = starts,
        .ends = ends,
        .values = ids,
        .allowed_registers = allowed,
        .classes = classes,
        .use_offsets = offsets,
        .use_positions = uses,
        .use_required = required,
        .use_count = 8,
        .count = 2,
        .value_count = 2,
        .position_count = 10,
        .registers = { 2, 0 },
    };
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        const uint64_t saved = arena->length;
        Tier1Allocation allocation = { 0 };
        const Tier1AllocationError error = variant == 0 ? tier1_allocate(&input, arena, &allocation) : tier1_allocate_scalar(&input, arena, &allocation);
        bool valid = error == TIER1_ALLOCATION_OK && tier1_allocation_verify(&input, &allocation) && tier1_allocation_test_occupancy(&input, &allocation) && allocation.fragment_count > 4 && allocation.spill_count == 2;
        uint64_t registers[32] = { 0 };
        uint64_t slots[2] = { 0 };
        const uint64_t values[] = { UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210) };
        for (uint32_t p = 0; valid && p < 10; p += 1)
        {
            // Apply stores as one phase, then reloads: a freed register may hold a different value after the latter.
            for (uint32_t i = 0; i < 2; i += 1)
            {
                for (uint32_t f = allocation.fragment_offsets[i] + 1; f < allocation.fragment_offsets[i + 1]; f += 1)
                {
                    if (allocation.starts[f] == p && allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_SPILL)
                    {
                        slots[allocation.spill_ids[i]] = registers[allocation.registers[f - 1]];
                    }
                }
            }
            for (uint32_t i = 0; i < 2; i += 1)
            {
                for (uint32_t f = allocation.fragment_offsets[i]; f < allocation.fragment_offsets[i + 1]; f += 1)
                {
                    if (allocation.starts[f] == p)
                    {
                        if (allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER)
                        {
                            registers[allocation.registers[f]] = p == starts[i] ? values[i] : slots[allocation.spill_ids[i]];
                        }
                        else if (p == starts[i])
                        {
                            slots[allocation.spill_ids[i]] = values[i];
                        }
                    }
                }
                for (uint32_t use = offsets[i]; use < offsets[i + 1]; use += 1)
                {
                    if (uses[use] == p)
                    {
                        Tier1AllocationLocation location = { 0 };
                        valid = valid && tier1_allocation_location(&input, &allocation, i, p, &location) && location.kind == BUSTER_TIER1_ALLOCATE_REGISTER && registers[location.reg] == values[i];
                    }
                }
            }
        }
        tier1_allocation_test_expect(tally, valid, "repeated required-use reload/resplit preserves values");
        arena_pop_to(arena, saved);
    }
    uses[4] = 2;
    const uint64_t saved = arena->length;
    Tier1Allocation allocation = { .splits = 987 };
    tier1_allocation_test_expect(tally, tier1_allocate(&input, arena, &allocation) == TIER1_ALLOCATION_CONSTRAINT && allocation.splits == 987 && arena->length == saved, "impossible simultaneous required operands fail transactionally");
}

static void tier1_allocation_test_guarded(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping[12] = { 0 };
    bool mapped = page >= 4096;
    for (uint32_t c = 0; c < 12; c += 1)
    {
        mapping[c] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mapped = mapped && mapping[c] != MAP_FAILED;
        if (mapping[c] != MAP_FAILED)
        {
            mapped = mapped && mprotect(mapping[c] + page, page, PROT_NONE) == 0;
        }
    }
    tier1_allocation_test_expect(tally, mapped, "guard mappings");
    for (uint32_t count = 0; mapped && count <= 65; count += 1)
    {
        const uint64_t saved = arena->length;
        uint32_t* starts = (uint32_t*)(mapping[0] + page - count * 4);
        uint32_t* ends = (uint32_t*)(mapping[1] + page - count * 4);
        uint32_t* values = (uint32_t*)(mapping[2] + page - count * 4);
        uint32_t* allowed = (uint32_t*)(mapping[3] + page - count * 4);
        uint8_t* classes = mapping[4] + page - count;
        uint8_t* operations = mapping[5] + page - count;
        uint8_t* widths = mapping[6] + page - count;
        uint64_t* constants = (uint64_t*)(mapping[7] + page - count * 8);
        uint32_t* fixed_starts = (uint32_t*)(mapping[8] + page - count * 4);
        uint32_t* fixed_ends = (uint32_t*)(mapping[9] + page - count * 4);
        uint32_t* fixed_registers = (uint32_t*)(mapping[10] + page - count * 4);
        uint8_t* fixed_classes = mapping[11] + page - count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            starts[i] = i;
            ends[i] = i + 1;
            values[i] = i;
            allowed[i] = 3;
            classes[i] = 0;
            operations[i] = BUSTER_TIER1_REMAT_INTEGER;
            widths[i] = 8;
            constants[i] = i;
            fixed_starts[i] = i;
            fixed_ends[i] = i + 1;
            fixed_registers[i] = 1;
            fixed_classes[i] = 0;
        }
        const Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .count = count,
            .value_count = count,
            .position_count = 66,
            .registers = { 3, 0 },
            .fixed = {
                .starts = fixed_starts,
                .ends = fixed_ends,
                .registers = fixed_registers,
                .classes = fixed_classes,
                .count = count,
            },
        };
        Tier1Allocation output = { 0 };
        tier1_allocation_test_expect(tally, tier1_allocate(&input, arena, &output) == TIER1_ALLOCATION_OK && tier1_allocation_verify(&input, &output), "allocation input columns end at guard pages");
        const Tier1RematerializationInput recipes = {
            .operations = operations,
            .widths = widths,
            .flags = classes,
            .values = constants,
            .count = count,
        };
        uint8_t marks[65];
        tier1_allocation_test_expect(tally, tier1_rematerialization_marks(&recipes, marks) && tier1_rematerialization_marks_scalar(&recipes, marks), "rematerialization input columns end at guard pages");
        arena_pop_to(arena, saved);
    }
    for (uint32_t c = 0; c < 12; c += 1)
    {
        if (mapping[c] != MAP_FAILED)
        {
            munmap(mapping[c], page * 2);
        }
    }
}

static void tier1_allocation_test_rematerialization(Tier1AllocationTestTally* tally)
{
    uint8_t ops[129];
    uint8_t widths[129];
    uint8_t flags[129];
    uint64_t values[129];
    uint16_t costs[129];
    uint16_t reloads[129];
    uint8_t scalar[129];
    uint8_t vector[129];
    const uint64_t samples[] = { 0, 1, UINT32_MAX, UINT64_MAX, UINT64_C(0x123456789abcdef0), INT32_MAX, (uint64_t)(int64_t)INT32_MIN, UINT64_C(0x80000000) };
    for (uint32_t i = 0; i < 129; i += 1)
    {
        ops[i] = (uint8_t)(i % 7);
        widths[i] = i % 3 == 0 ? 4 : 8;
        flags[i] = (uint8_t)(i & 1);
        values[i] = samples[i % 8];
        costs[i] = (uint16_t)(i % 4);
        reloads[i] = (uint16_t)(i % 3);
    }
    Tier1RematerializationInput input = {
        .operations = ops,
        .widths = widths,
        .flags = flags,
        .values = values,
        .costs = costs,
        .reload_costs = reloads,
    };
    for (uint32_t count = 0; count <= 129; count += 1)
    {
        input.count = count;
        memset(scalar, 0x5a, sizeof(scalar));
        memset(vector, 0x5a, sizeof(vector));
        tier1_allocation_test_expect(tally, tier1_rematerialization_marks(&input, vector) && tier1_rematerialization_marks_scalar(&input, scalar) && memcmp(scalar, vector, sizeof(scalar)) == 0, "rematerialization scalar/vector and tail canary");
    }
    input.count = 1;
    input.costs = input.reload_costs = 0;
    ops[0] = BUSTER_TIER1_REMAT_INTEGER;
    widths[0] = 8;
    values[0] = 0;
    Tier1Rematerialized copy = { 0 };
    tier1_allocation_test_expect(tally, tier1_rematerialize(&input, 0, 10, 4, 0, &copy) && copy.instruction.form == X86_FORM_XOR_RM32_R32 && copy.instruction.rm == 10 && copy.instruction.reg == 10, "zero idiom with dead flags");
    for (uint32_t live = 1; live < 256; live += 1)
    {
        tier1_allocation_test_expect(tally, tier1_rematerialize(&input, 0, 10, 4, (uint8_t)live, &copy) && copy.instruction.form == X86_FORM_MOV_R32_IMM32, "every nonempty live-flags mask preserves flags");
    }
    ops[0] = BUSTER_TIER1_REMAT_SYMBOL;
    values[0] = 123;
    flags[0] = BUSTER_TIER1_REMAT_STABLE;
    tier1_allocation_test_expect(tally, tier1_rematerialize(&input, 0, 9, 4, 255, &copy) && copy.symbol == 123 && (copy.instruction.form & BUSTER_X86_FORM_FIXUP) != 0 && copy.instruction.mode == BUSTER_X86_MEMORY_RIP, "symbol copies retain fixup identity");
    flags[0] = 0;
    tier1_allocation_test_expect(tally, !tier1_rematerialize(&input, 0, 9, 4, 0, &copy), "unstable symbol rejected");
    ops[0] = BUSTER_TIER1_REMAT_STACK;
    flags[0] = BUSTER_TIER1_REMAT_STABLE;
    values[0] = (uint64_t)(int64_t)-32;
    tier1_allocation_test_expect(tally, tier1_rematerialize(&input, 0, 9, 5, 255, &copy) && copy.instruction.rm == 5 && copy.instruction.displacement == -32, "stable negative frame displacement");
    values[0] = (uint64_t)INT32_MAX + 1;
    tier1_allocation_test_expect(tally, !tier1_rematerialize(&input, 0, 9, 5, 0, &copy), "frame displacement overflow rejected");
    input.count = UINT32_MAX;
    vector[0] = 0xa5;
    tier1_allocation_test_expect(tally, !tier1_rematerialization_marks(&input, vector) && !tier1_rematerialization_marks_scalar(&input, vector) && vector[0] == 0xa5, "oversized recipe count rejected before reading columns");
    input.count = BUSTER_TIER1_REMAT_MAX_VALUES + 1;
    tier1_allocation_test_expect(tally, !tier1_rematerialization_marks(&input, vector) && !tier1_rematerialize(&input, 0, 0, 4, 0, &copy), "recipe limit shared by marking and materialization");
}

// Generated machine code has no Clang UBSan function-type prefix before its first byte. Only this actual JIT call
// bypasses that metadata check; address/undefined sanitizers still cover the producer, allocator and test driver.
static __attribute__((no_sanitize("function"))) uint64_t tier1_allocation_test_call(void* code)
{
    uint64_t (*function)(void) = (uint64_t (*)(void))code;
    return function();
}

static void tier1_allocation_test_native_recipes(Tier1AllocationTestTally* tally)
{
    const uint64_t values[] = {
        0,
        1,
        UINT32_MAX,
        UINT64_MAX,
        (uint64_t)(int64_t)INT32_MIN,
        UINT64_C(0x123456789abcdef0),
        0,
        (uint64_t)(int64_t)-32,
        123,
    };
    for (uint32_t fixture = 0; fixture < 9; fixture += 1)
    {
        const uint8_t operation = fixture == 7 ? BUSTER_TIER1_REMAT_STACK : fixture == 8 ? BUSTER_TIER1_REMAT_SYMBOL : BUSTER_TIER1_REMAT_INTEGER;
        const uint8_t width = 8;
        const uint8_t stable = BUSTER_TIER1_REMAT_STABLE;
        const Tier1RematerializationInput input = {
            .operations = &operation,
            .widths = &width,
            .flags = &stable,
            .values = values + fixture,
            .count = 1,
        };
        Tier1Rematerialized copy = { 0 };
        const uint8_t destination = fixture == 6 || fixture == 7 ? 10 : 0;
        bool valid = tier1_rematerialize(&input, 0, destination, 4, fixture == 6 ? 1 : 0, &copy);
        X86Instruction records[8] = { 0 };
        uint32_t count = 0;
        uint64_t expected = values[fixture];
        if (fixture == 6)
        {
            records[count] = (X86Instruction){
                .immediate = 1,
                .form = X86_FORM_MOV_R32_IMM32,
            };
            count += 1;
            records[count] = (X86Instruction){
                .form = X86_FORM_CMP_RM32_IMM8,
            };
            count += 1;
            expected = 0;
        }
        else if (fixture == 7)
        {
            records[count] = (X86Instruction){
                .rm = 4,
                .form = X86_FORM_MOV_R64_RM64,
            };
            count += 1;
        }
        records[count] = copy.instruction;
        count += 1;
        if (fixture == 6)
        {
            records[count] = (X86Instruction){
                .form = X86_FORM_SETE_RM8,
            };
            count += 1;
        }
        else if (fixture == 7)
        {
            records[count] = (X86Instruction){
                .rm = 10,
                .form = X86_FORM_SUB_RM64_R64,
            };
            count += 1;
            records[count] = (X86Instruction){
                .rm = 10,
                .form = X86_FORM_MOV_R64_RM64,
            };
            count += 1;
        }
        records[count] = (X86Instruction){
            .form = X86_FORM_RET,
        };
        count += 1;
        uint8_t* code = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        valid = valid && code != MAP_FAILED && x86_encoder_initialize() && x86_validate(records, count) == count;
        uint32_t fixup_ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(1)] = { 0 };
        if (valid)
        {
            valid = x86_encode(records, count, code, 4096, 0, fixup_ends) > 0;
            if (valid && fixture == 8)
            {
                // This fixture resolves its single PC-relative LEA to bytes in the same mapping. Production copies
                // instead pass their distinct ends, symbol and addend to the linker's ordinary fixup path.
                const uint32_t fixup_end = fixup_ends[0];
                valid = fixup_end >= 4 && fixup_end < 64 && copy.symbol == 123;
                if (valid)
                {
                    const int32_t displacement = (int32_t)(64 - fixup_end);
                    memcpy(code + fixup_end - 4, &displacement, sizeof(displacement));
                    expected = (uintptr_t)(code + 64);
                }
            }
            valid = valid && mprotect(code, 4096, PROT_READ | PROT_EXEC) == 0;
        }
        if (valid)
        {
            const pid_t child = fork();
            if (child == 0)
            {
                _exit(tier1_allocation_test_call(code) == expected ? 0 : 1);
            }
            int status = 0;
            valid = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (code != MAP_FAILED)
        {
            munmap(code, 4096);
        }
        tier1_allocation_test_expect(tally, valid, "native constant/flags/stable-stack/symbol rematerialization");
    }
}

static void tier1_allocation_test_native(Tier1AllocationTestTally* tally, Arena* arena, uint64_t seed)
{
    uint32_t starts[48];
    uint32_t ends[48];
    uint32_t ids[48];
    uint32_t allowed[48];
    uint32_t use_offsets[49];
    uint32_t uses[96];
    uint8_t required[96];
    uint8_t classes[48] = { 0 };
    uint8_t remat[48];
    uint8_t ops[48];
    uint8_t widths[48];
    uint8_t flags[48] = { 0 };
    uint64_t values[48];
    const uint32_t mask = (1u << 1) | (1u << 2) | (1u << 8) | (1u << 9);
    for (uint32_t i = 0; i < 48; i += 1)
    {
        starts[i] = i * 2;
        ends[i] = 161 + (i * 17) % 48;
        ids[i] = i;
        allowed[i] = mask;
        use_offsets[i] = i * 2;
        uses[i * 2] = 96 + (i * 17) % 48;
        uses[i * 2 + 1] = ends[i] - 1;
        required[i * 2] = 1;
        required[i * 2 + 1] = 1;
        remat[i] = i % 3 == 0 ? 1 : 0;
        ops[i] = BUSTER_TIER1_REMAT_INTEGER;
        widths[i] = 8;
        values[i] = ((uint64_t)tier1_allocation_test_random(&seed) << 32) | tier1_allocation_test_random(&seed);
    }
    use_offsets[48] = 96;
    const Tier1RematerializationInput recipes = {
        .operations = ops,
        .widths = widths,
        .flags = flags,
        .values = values,
        .count = 48,
    };
    Tier1AllocationInput input = {
        .starts = starts,
        .ends = ends,
        .values = ids,
        .allowed_registers = allowed,
        .classes = classes,
        .rematerializable = remat,
        .use_offsets = use_offsets,
        .use_positions = uses,
        .use_count = 96,
        .count = 48,
        .value_count = 48,
        .position_count = 208,
        .registers = { mask, 0 },
    };
    for (uint32_t variant = 0; variant < 6; variant += 1)
    {
        const uint64_t saved = arena->length;
        input.rematerializable = variant < 2 || variant >= 4 ? remat : 0;
        input.use_required = variant >= 4 ? required : 0;
        Tier1Allocation allocation = { 0 };
        const Tier1AllocationError error = (variant & 1) == 0 ? tier1_allocate(&input, arena, &allocation) : tier1_allocate_scalar(&input, arena, &allocation);
        bool valid = error == TIER1_ALLOCATION_OK && tier1_allocation_verify(&input, &allocation);
        X86Instruction records[1024];
        uint32_t count = 0;
        const uint32_t frame = (allocation.spill_count * 8 + 15) & ~15u;
        records[count] = (X86Instruction){
            .immediate = (int32_t)frame,
            .rm = 4,
            .form = X86_FORM_SUB_RM64_IMM32,
        };
        count += 1;
        records[count] = (X86Instruction){ .form = X86_FORM_XOR_RM32_R32 };
        count += 1;
        uint64_t expected = 0;
        for (uint32_t position = 0; valid && position < input.position_count; position += 1)
        {
            // Eviction stores must precede the definition that reuses their register.
            for (uint32_t i = 0; i < input.count; i += 1)
            {
                const uint32_t begin = allocation.fragment_offsets[i];
                for (uint32_t f = begin + 1; f < allocation.fragment_offsets[i + 1]; f += 1)
                {
                    if (allocation.starts[f] == position && allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_SPILL)
                    {
                        records[count] = (X86Instruction){
                            .displacement = (int32_t)(allocation.spill_ids[i] * 8),
                            .reg = allocation.registers[f - 1],
                            .rm = 4,
                            .mode = BUSTER_X86_MEMORY_BASE,
                            .form = X86_FORM_MOV_RM64_R64,
                        };
                        count += 1;
                    }
                }
            }
            for (uint32_t i = 0; valid && i < input.count; i += 1)
            {
                Tier1AllocationLocation location = { 0 };
                for (uint32_t f = allocation.fragment_offsets[i] + 1; f < allocation.fragment_offsets[i + 1]; f += 1)
                {
                    if (allocation.starts[f] == position && allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER)
                    {
                        if (allocation.kinds[f - 1] == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
                        {
                            Tier1Rematerialized copy = { 0 };
                            valid = tier1_rematerialize(&recipes, i, allocation.registers[f], 4, 0, &copy);
                            records[count] = copy.instruction;
                        }
                        else
                        {
                            records[count] = (X86Instruction){
                                .displacement = (int32_t)(allocation.spill_ids[i] * 8),
                                .reg = allocation.registers[f],
                                .rm = 4,
                                .mode = BUSTER_X86_MEMORY_BASE,
                                .form = X86_FORM_MOV_R64_RM64,
                            };
                        }
                        count += 1;
                    }
                }
                if (starts[i] == position)
                {
                    valid = tier1_allocation_location(&input, &allocation, i, position, &location);
                    if (valid && location.kind != BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
                    {
                        Tier1Rematerialized copy = { 0 };
                        const uint8_t reg = location.kind == BUSTER_TIER1_ALLOCATE_REGISTER ? location.reg : 7;
                        valid = tier1_rematerialize(&recipes, i, reg, 4, 0, &copy);
                        records[count] = copy.instruction;
                        count += 1;
                        if (location.kind == BUSTER_TIER1_ALLOCATE_SPILL)
                        {
                            records[count] = (X86Instruction){
                                .displacement = (int32_t)(location.spill_id * 8),
                                .reg = reg,
                                .rm = 4,
                                .mode = BUSTER_X86_MEMORY_BASE,
                                .form = X86_FORM_MOV_RM64_R64,
                            };
                            count += 1;
                        }
                    }
                }
                const bool at_use = uses[i * 2] == position || uses[i * 2 + 1] == position;
                if (at_use)
                {
                    valid = valid && tier1_allocation_location(&input, &allocation, i, position, &location);
                    uint8_t source = location.reg;
                    if (location.kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
                    {
                        Tier1Rematerialized copy = { 0 };
                        valid = valid && tier1_rematerialize(&recipes, i, 7, 4, 0, &copy);
                        records[count] = copy.instruction;
                        count += 1;
                        source = 7;
                    }
                    records[count] = (X86Instruction){
                        .displacement = location.kind == BUSTER_TIER1_ALLOCATE_SPILL ? (int32_t)(location.spill_id * 8) : 0,
                        .rm = location.kind == BUSTER_TIER1_ALLOCATE_SPILL ? 4 : source,
                        .mode = location.kind == BUSTER_TIER1_ALLOCATE_SPILL ? BUSTER_X86_MEMORY_BASE : 0,
                        .form = X86_FORM_ADD_R64_RM64,
                    };
                    count += 1;
                    expected += values[i];
                }
            }
        }
        records[count] = (X86Instruction){
            .immediate = (int32_t)frame,
            .rm = 4,
            .form = X86_FORM_ADD_RM64_IMM32,
        };
        count += 1;
        records[count] = (X86Instruction){ .form = X86_FORM_RET };
        count += 1;
        valid = valid && count < 1024 && x86_encoder_initialize() && x86_validate(records, count) == count;
        const uint64_t capacity = 32768;
        uint8_t* code = mmap(0, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        valid = valid && code != MAP_FAILED;
        if (valid)
        {
            valid = x86_encode(records, count, code, capacity, 0, 0) > 0 && mprotect(code, capacity, PROT_READ | PROT_EXEC) == 0;
        }
        if (valid)
        {
            const pid_t child = fork();
            if (child == 0)
            {
                _exit(tier1_allocation_test_call(code) == expected ? 0 : 1);
            }
            int status = 0;
            valid = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (code != MAP_FAILED)
        {
            munmap(code, capacity);
        }
        tier1_allocation_test_expect(tally, valid, "native allocated/spilled/rematerialized reduction matches independent sum");
        arena_pop_to(arena, saved);
    }
}

#include "buster/compiler/tier1_moves_test.c"
#include "buster/compiler/tier1_allocate_expiry_test.c"

static int run_tier1_allocate_tests(int argc, char** argv)
{
    uint64_t seed = 140;
    uint32_t count = 2000;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool is_seed = strcmp(argv[i], "--seed") == 0;
        const bool is_count = strcmp(argv[i], "--count") == 0;
        valid = (is_seed || is_count) && i + 1 < argc;
        if (valid)
        {
            i += 1;
            char* end = 0;
            const unsigned long long value = strtoull(argv[i], &end, 10);
            valid = argv[i][0] >= '0' && argv[i][0] <= '9' && end != argv[i] && *end == 0 && (is_seed || value <= 100000);
            if (is_seed)
            {
                seed = value;
            }
            else
            {
                count = (uint32_t)value;
            }
        }
    }
    Tier1AllocationTestTally tally = { 0 };
    Arena arena = arena_reserve(16u << 20);
    valid = valid && arena.base != 0;
    if (valid)
    {
        tier1_allocation_test_generated(&tally, &arena, seed, count);
        tier1_allocation_test_fixed(&tally, &arena, seed);
        tier1_allocation_test_fixed_sparse(&tally, &arena);
        tier1_allocation_test_failures(&tally, &arena);
        tier1_allocation_test_moves(&tally, &arena, seed);
        tier1_allocation_test_move_scaling(&tally);
        tier1_allocation_test_required(&tally, &arena);
        tier1_allocation_test_expiry(&tally);
        tier1_allocation_test_guarded(&tally, &arena);
        tier1_allocation_test_rematerialization(&tally);
        tier1_allocation_test_native(&tally, &arena, seed);
        tier1_allocation_test_native_recipes(&tally);
        printf("test-tier1-allocation: %" PRIu64 " checks, %" PRIu64 " failures, seed %" PRIu64 "\n", tally.checks, tally.failures, seed);
    }
    else
    {
        fprintf(stderr, "usage: test-tier1-allocation [--seed N] [--count N]\n");
    }
    arena_release(&arena);
    return valid && tally.failures == 0 ? 0 : 1;
}
