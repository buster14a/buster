// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Parallel-assignment qualification for #608. Included after the allocation test helpers. Snapshot execution is
// independent of dependency construction, and every case's exact move order is tier1_move_test_definition's, the
// policy written as its plainest scan (#2924: no resolver is another's oracle).

// The order by its definition (docs/tier1-allocation.md): repeatedly emit the lowest pending input index whose
// destination no pending move still reads; when none is ready, save the lowest pending destination to scratch, and
// its readers read scratch from then on. Identity moves disappear. Cubic, for test sizes only; `sources` is the
// caller's copy, rewritten. Returns the emitted count, or UINT32_MAX past `capacity`.
static uint32_t tier1_move_test_definition(uint32_t* sources, const uint32_t* destinations, uint32_t count, uint32_t scratch, uint32_t* out_sources,
    uint32_t* out_destinations, uint32_t capacity)
{
    uint32_t emitted = 0;
    uint32_t remaining = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        remaining += sources[i] != destinations[i];
    }
    bool room = true;
    while (room && remaining != 0)
    {
        uint32_t chosen = UINT32_MAX;
        uint32_t lowest = UINT32_MAX;
        for (uint32_t i = 0; chosen == UINT32_MAX && i < count; i += 1)
        {
            if (sources[i] != destinations[i] && sources[i] != UINT32_MAX - 1)
            {
                lowest = lowest == UINT32_MAX ? i : lowest;
                bool read = false;
                for (uint32_t j = 0; j < count; j += 1)
                {
                    read |= sources[j] != destinations[j] && sources[j] != UINT32_MAX - 1 && sources[j] == destinations[i];
                }
                chosen = read ? UINT32_MAX : i;
            }
        }
        room = emitted < capacity;
        if (room && chosen != UINT32_MAX)
        {
            out_sources[emitted] = sources[chosen];
            out_destinations[emitted] = destinations[chosen];
            // Done: no location id is UINT32_MAX - 1 in these tests.
            sources[chosen] = UINT32_MAX - 1;
            remaining -= 1;
        }
        else if (room)
        {
            const uint32_t preserved = destinations[lowest];
            out_sources[emitted] = preserved;
            out_destinations[emitted] = scratch;
            for (uint32_t j = 0; j < count; j += 1)
            {
                sources[j] = sources[j] == preserved && sources[j] != destinations[j] ? scratch : sources[j];
            }
        }
        emitted += 1;
    }
    return room ? emitted : UINT32_MAX;
}

// Whether `output` is exactly the definition's order of `input`'s moves.
static bool tier1_move_test_defined(const Tier1ParallelMoves* input, const Tier1ResolvedMoves* output)
{
    const uint64_t count = input->count;
    uint32_t* sources = malloc(count * 4 + 4);
    uint32_t* out = malloc(count * 16 + 8);
    bool same = sources != 0 && out != 0;
    if (same)
    {
        memcpy(sources, input->sources, count * 4);
        const uint32_t emitted = tier1_move_test_definition(sources, input->destinations, input->count, input->scratch, out, out + count * 2 + 1, input->count * 2);
        same = emitted == output->count && memcmp(out, output->sources, (uint64_t)emitted * 4) == 0 && memcmp(out + count * 2 + 1, output->destinations, (uint64_t)emitted * 4) == 0;
    }
    free(out);
    free(sources);
    return same;
}

static uint32_t tier1_move_test_location(uint32_t index)
{
    return UINT32_MAX - index * 1024u;
}

static void tier1_move_test_shape(Tier1AllocationTestTally* tally, Arena* arena, uint32_t count, uint32_t shape)
{
    const uint64_t saved = arena->length;
    uint32_t* sources = malloc((uint64_t)count * 4 + 4);
    uint32_t* destinations = malloc((uint64_t)count * 4 + 4);
    uint32_t* actual = malloc(((uint64_t)count * 2 + 1) * 4);
    bool valid = sources != 0 && destinations != 0 && actual != 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        uint32_t source = i;
        uint32_t destination = i + 1;
        if (shape == 1)
        {
            source = count - i - 1;
            destination = count - i;
        }
        else if (shape == 2)
        {
            source = count + i;
            destination = i;
        }
        else if (shape == 3)
        {
            source = (i + 1) % count;
            destination = i;
        }
        else if (shape == 4)
        {
            source = i % 3 == 0 ? (i + 1) % count : 0;
            destination = i;
        }
        else if (shape == 5)
        {
            source = i;
            destination = i;
        }
        else if (shape == 6)
        {
            source = (i ^ 1u) < count ? i ^ 1u : i;
            destination = i;
        }
        sources[i] = tier1_move_test_location(source);
        destinations[i] = tier1_move_test_location(destination);
    }
    const Tier1ParallelMoves input = {
        .sources = sources,
        .destinations = destinations,
        .count = count,
        .scratch = tier1_move_test_location(count * 2),
    };
    Tier1ResolvedMoves output = { 0 };
    valid = valid && tier1_resolve_moves(&input, arena, &output) == TIER1_ALLOCATION_OK && output.count <= count * 2;
    if (valid && count <= 1024)
    {
        valid = tier1_move_test_defined(&input, &output);
    }
    for (uint32_t i = 0; valid && i <= count * 2; i += 1)
    {
        actual[i] = i;
    }
    for (uint32_t i = 0; valid && i < output.count; i += 1)
    {
        const uint32_t source = (UINT32_MAX - output.sources[i]) / 1024u;
        const uint32_t destination = (UINT32_MAX - output.destinations[i]) / 1024u;
        valid = source <= count * 2 && destination <= count * 2 && output.sources[i] == tier1_move_test_location(source) && output.destinations[i] == tier1_move_test_location(destination);
        if (valid)
        {
            actual[destination] = actual[source];
        }
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        valid = actual[(UINT32_MAX - destinations[i]) / 1024u] == (UINT32_MAX - sources[i]) / 1024u;
        if (shape == 0)
        {
            valid = valid && output.count == count && output.sources[i] == sources[count - i - 1] && output.destinations[i] == destinations[count - i - 1];
        }
    }
    tier1_allocation_test_expect(tally, valid, "move shapes: snapshot, opaque ids, exact deterministic order and scalability");
    free(actual);
    free(destinations);
    free(sources);
    arena_pop_to(arena, saved);
}

static void tier1_move_test_failures(Tier1AllocationTestTally* tally)
{
    uint32_t sources[17];
    uint32_t destinations[17];
    for (uint32_t i = 0; i < 17; i += 1)
    {
        sources[i] = i;
        destinations[i] = i + 1;
    }
    Tier1ParallelMoves input = {
        .sources = sources,
        .destinations = destinations,
        .count = 17,
        .scratch = 32,
    };
    uint8_t bytes[2048] __attribute__((aligned(64)));
    memset(bytes, 0x5a, sizeof(bytes));
    bool valid = true;
    for (uint32_t capacity = 7; capacity <= 8 + 44 * 17; capacity += 1)
    {
        Arena arena = {
            .base = bytes,
            .length = 7,
            .capacity = capacity,
        };
        Tier1ResolvedMoves output = {
            .sources = sources,
            .destinations = destinations,
            .count = 123,
        };
        const Tier1AllocationError error = tier1_resolve_moves(&input, &arena, &output);
        if (capacity < 8 + 44 * 17)
        {
            valid = valid && error == TIER1_ALLOCATION_CAPACITY && arena.length == 7 && output.sources == sources && output.destinations == destinations && output.count == 123;
        }
        else
        {
            valid = valid && error == TIER1_ALLOCATION_OK && arena.length == 8 + 16 * 17 && output.count == 17;
        }
        for (uint32_t i = 0; i < 7; i += 1)
        {
            valid = valid && bytes[i] == 0x5a;
        }
    }
    tier1_allocation_test_expect(tally, valid, "move capacity sweep rolls back and retains only output, preserving prior arena data");
    for (uint32_t kind = 0; kind < 6; kind += 1)
    {
        Arena arena = {
            .base = bytes,
            .length = 7,
            .capacity = sizeof(bytes),
        };
        sources[8] = kind == 0 ? input.scratch : 8;
        destinations[8] = kind == 1 ? input.scratch : kind == 2 ? destinations[0] : 9;
        input.count = kind == 3 ? BUSTER_TIER1_ALLOCATE_MAX_INTERVALS + 1 : 17;
        input.sources = kind == 4 ? 0 : sources;
        input.destinations = kind == 5 ? 0 : destinations;
        Tier1ResolvedMoves output = {
            .sources = sources,
            .destinations = destinations,
            .count = 123,
        };
        valid = tier1_resolve_moves(&input, &arena, &output) == TIER1_ALLOCATION_INVALID && arena.length == 7 && output.sources == sources && output.destinations == destinations && output.count == 123;
        tier1_allocation_test_expect(tally, valid, "invalid moves reject scratch, duplicates, excessive counts and missing columns transactionally");
    }
}

static void tier1_move_test_guarded(Tier1AllocationTestTally* tally, Arena* arena)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 4, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = mapping != MAP_FAILED;
    for (uint32_t count = 1; valid && count <= 33; count += 1)
    {
        const uint64_t saved = arena->length;
        valid = mprotect(mapping, page, PROT_READ | PROT_WRITE) == 0 && mprotect(mapping + page * 2, page, PROT_READ | PROT_WRITE) == 0;
        uint32_t* sources = (uint32_t*)(mapping + page) - count;
        uint32_t* destinations = (uint32_t*)(mapping + page * 3) - count;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            sources[i] = i;
            destinations[i] = i + 1;
        }
        valid = valid && mprotect(mapping, page, PROT_READ) == 0 && mprotect(mapping + page * 2, page, PROT_READ) == 0;
        const Tier1ParallelMoves input = {
            .sources = sources,
            .destinations = destinations,
            .count = count,
            .scratch = count + 1,
        };
        Tier1ResolvedMoves output = { 0 };
        valid = valid && tier1_resolve_moves(&input, arena, &output) == TIER1_ALLOCATION_OK && tier1_move_test_defined(&input, &output);
        arena_pop_to(arena, saved);
    }
    tier1_allocation_test_expect(tally, valid, "move columns: immutable guarded tails at every small and vector boundary");
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 4);
    }
}

static void tier1_allocation_test_move_scaling(Tier1AllocationTestTally* tally)
{
    Arena arena = arena_reserve(64u << 20);
    tier1_allocation_test_expect(tally, arena.base != 0, "move scalability arena");
    if (arena.base != 0)
    {
        const uint32_t counts[] = { 0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 64, 128, 256, 512, 1024 };
        for (uint32_t shape = 0; shape < 7; shape += 1)
        {
            for (uint32_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i += 1)
            {
                tier1_move_test_shape(tally, &arena, counts[i], shape);
            }
        }
        tier1_move_test_shape(tally, &arena, BUSTER_TIER1_ALLOCATE_MAX_INTERVALS, 0);
        tier1_move_test_failures(tally);
        tier1_move_test_guarded(tally, &arena);
    }
    arena_release(&arena);
}
