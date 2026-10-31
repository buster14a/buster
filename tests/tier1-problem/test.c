// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Original #938 interchange and current-allocator adapter tests.
// The default build exercises the codec alone. BUSTER_TP_INTEGRATION additionally
// uses the real lane arena, production allocator and its scalar twin; no stubs.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../../src/buster/compiler/tier1_problem.c"

#ifdef BUSTER_TP_INTEGRATION
#include "../../src/buster/compiler/lane.c"
#include "../../src/buster/compiler/tier1_allocate.c"
#include "../../src/buster/compiler/tier1_problem_intervals.c"
#endif

#include "fixtures.h"

static uint64_t checks;
static uint64_t failures;

static void expect(bool condition, const char* name)
{
    checks += 1;
    if (!condition)
    {
        failures += 1;
        if (failures < 30)
        {
            fprintf(stderr, "tier1-problem: %s\n", name);
        }
    }
}

static size_t round_trip(const Tier1Problem* p, uint8_t* bytes, size_t capacity)
{
    size_t size = 0;
    uint8_t again[8192];
    Tier1Problem decoded = { 0 };
    const Tier1ProblemReason encoded = tier1_problem_encode(p, bytes, capacity, &size);
    expect(encoded == TIER1_PROBLEM_OK, "encode");
    if (encoded == TIER1_PROBLEM_OK)
    {
        const Tier1ProblemReason parsed = tier1_problem_decode(bytes, size, &decoded);
        expect(parsed == TIER1_PROBLEM_OK, "decode");
        if (parsed == TIER1_PROBLEM_OK)
        {
            size_t other = 0;
            expect(tier1_problem_encode(&decoded, again, sizeof(again), &other) == TIER1_PROBLEM_OK, "re-encode borrowed wire view");
            expect(other == size && memcmp(bytes, again, size) == 0, "canonical round trip preserves every byte");
            for (uint32_t t = 0; t < BUSTER_TP_TABLE_COUNT; t += 1)
            {
                expect(decoded.tables[t].count == p->tables[t].count, "round-trip table count");
                for (uint32_t r = 0; r < p->tables[t].count; r += 1)
                {
                    for (uint32_t c = 0; c < tier1_problem_widths[t]; c += 1)
                    {
                        expect(tier1_problem_word(p, t, r, c) == tier1_problem_word(&decoded, t, r, c), "round-trip field");
                    }
                }
            }
        }
    }
    return size;
}

static void rejection(const Tier1Problem* p, Tier1ProblemReason wanted, const char* name)
{
    uint8_t output[8192];
    memset(output, 0x5a, sizeof(output));
    size_t written = 12345;
    expect(tier1_problem_validate(p) == wanted, name);
    expect(tier1_problem_encode(p, output, sizeof(output), &written) == wanted, "encode shares structural validation");
    bool intact = written == 12345;
    for (size_t i = 0; i < sizeof(output); i += 1)
    {
        intact = intact && output[i] == 0x5a;
    }
    expect(intact, "failed encode is transactional");
}

static void codec_tests(void)
{
    ProblemFixture f;
    problem_fixture(&f);
    uint8_t bytes[8192];
    uint8_t copy[8192];
    const size_t size = round_trip(&f.problem, bytes, sizeof(bytes));
    expect(size > 96 && size < 4096, "bounded fixture size");
    expect(memcmp(bytes, "BMAP", 4) == 0, "wire magic and byte order");
    Tier1Problem sentinel;
    memset(&sentinel, 0x5a, sizeof(sentinel));
    for (size_t n = 0; n < size; n += 1)
    {
        Tier1Problem out = sentinel;
        expect(tier1_problem_decode(bytes, n, &out) != TIER1_PROBLEM_OK, "every truncated prefix is refused");
        expect(memcmp(&out, &sentinel, sizeof(out)) == 0, "failed decode preserves result");
    }
    for (size_t alignment = 0; alignment < 64; alignment += 1)
    {
        memcpy(copy + alignment, bytes, size);
        Tier1Problem out = { 0 };
        expect(tier1_problem_decode(copy + alignment, size, &out) == TIER1_PROBLEM_OK, "unaligned wire view");
    }
    memcpy(copy, bytes, size);
    copy[size] = 0;
    Tier1Problem out = { 0 };
    expect(tier1_problem_decode(copy, size + 1, &out) == TIER1_PROBLEM_LENGTH, "trailing bytes");
    tier1_problem_store(copy + 8, (uint32_t)size + 1);
    expect(tier1_problem_decode(copy, size + 1, &out) == TIER1_PROBLEM_LENGTH, "self-consistent size cannot hide trailing bytes");
    memcpy(copy, bytes, size);
    tier1_problem_store(copy + 4, 2);
    expect(tier1_problem_decode(copy, size, &out) == TIER1_PROBLEM_VERSION, "future version");
    memcpy(copy, bytes, size);
    tier1_problem_store(copy + 72, 1);
    expect(tier1_problem_decode(copy, size, &out) == TIER1_PROBLEM_VERSION, "reserved header word");
    memcpy(copy, bytes, size);
    tier1_problem_store(copy + 24, UINT32_MAX);
    expect(tier1_problem_decode(copy, size, &out) == TIER1_PROBLEM_LIMIT, "oversized row count");
    memcpy(copy, bytes, size);
    tier1_problem_store(copy + 96, UINT32_MAX);
    expect(tier1_problem_decode(copy, size, &out) == TIER1_PROBLEM_LENGTH, "oversized metadata length");
    size_t written = 123;
    memset(copy, 0x5a, sizeof(copy));
    expect(tier1_problem_encode(&f.problem, copy, size - 1, &written) == TIER1_PROBLEM_CAPACITY, "short output");
    expect(copy[0] == 0x5a && written == 123, "capacity failure preserves destination");
    expect(tier1_problem_decode(bytes, size, &out) == TIER1_PROBLEM_OK, "alias fixture");
    expect(tier1_problem_encode(&out, bytes, sizeof(bytes), &written) == TIER1_PROBLEM_ARGUMENT, "encode refuses overlapping borrowed storage");
    expect(tier1_problem_decode(bytes, size, (Tier1Problem*)(void*)bytes) == TIER1_PROBLEM_ARGUMENT, "decode refuses overlapping result");

    problem_fixture_cfg(&f);
    round_trip(&f.problem, bytes, sizeof(bytes));
    f.phis[0][2] = 1;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "phi edge ownership");
    problem_fixture_cfg(&f);
    f.edges[1][7] = 0;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "missing probability denominator");
    problem_fixture_cfg(&f);
    f.edges[1][6] = 2;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "probability greater than one");
    problem_fixture_form(&f);
    round_trip(&f.problem, bytes, sizeof(bytes));
    f.forms[0][0] = 0;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "alternative belongs to another instruction");
    problem_fixture(&f);
    f.operands[1][5] = 2;
    f.operands[2][5] = 1;
    round_trip(&f.problem, bytes, sizeof(bytes));
    f.operands[2][5] = BUSTER_TP_NONE;
    rejection(&f.problem, TIER1_PROBLEM_CONSTRAINT, "one-sided tie");
    problem_fixture(&f);
    f.operands[0][5] = 2;
    f.operands[2][5] = 0;
    rejection(&f.problem, TIER1_PROBLEM_CONSTRAINT, "cross-instruction tie");
    problem_fixture(&f);
    f.operands[1][2] = 1;
    f.operands[1][3] |= BUSTER_TP_EARLY_CLOBBER;
    round_trip(&f.problem, bytes, sizeof(bytes));
    problem_fixture(&f);
    f.operands[2][0] = 2;
    f.operands[2][1] = 0;
    f.operands[2][3] = BUSTER_TP_WRITE | BUSTER_TP_IMPLICIT;
    f.operands[2][4] = BUSTER_TP_NONE;
    f.operands[2][6] = 0;
    round_trip(&f.problem, bytes, sizeof(bytes));
    problem_fixture(&f);
    f.units[1] = 0;
    round_trip(&f.problem, bytes, sizeof(bytes)); // Aliases must survive serialization.

    problem_fixture(&f);
    f.problem.metadata[0].size = 39;
    rejection(&f.problem, TIER1_PROBLEM_METADATA, "unpinned LLVM revision");
    problem_fixture(&f);
    f.problem.metadata[9].size = 0;
    rejection(&f.problem, TIER1_PROBLEM_METADATA, "missing frequency provenance");
    problem_fixture(&f);
    f.problem.flags |= 0x100;
    rejection(&f.problem, TIER1_PROBLEM_VERSION, "unknown flags");
    problem_fixture(&f);
    f.blocks[0][2] = 3;
    rejection(&f.problem, TIER1_PROBLEM_LAYOUT, "orphan instruction");
    problem_fixture(&f);
    f.instructions[1][3] = 0;
    rejection(&f.problem, TIER1_PROBLEM_LAYOUT, "overlapping operand ranges");
    problem_fixture(&f);
    f.instructions[1][1] = 1;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "wrong instruction block");
    problem_fixture(&f);
    f.values[0][1] = 7;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "unknown value class");
    problem_fixture(&f);
    f.operands[0][1] = 3;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "unknown value");
    problem_fixture(&f);
    f.members[0] = 9;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "unknown physical register");
    problem_fixture(&f);
    f.members[1] = 0;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "duplicate class member");
    problem_fixture(&f);
    f.units[0] = 3;
    rejection(&f.problem, TIER1_PROBLEM_REFERENCE, "unknown alias unit");
    problem_fixture(&f);
    f.registers[2][6] = BUSTER_TP_SCRATCH;
    rejection(&f.problem, TIER1_PROBLEM_CONSTRAINT, "scratch is not secretly an extra register");

    Tier1ProblemTally tally = { 0 };
    for (uint32_t r = 0; r < TIER1_PROBLEM_REASON_COUNT; r += 1)
    {
        tier1_problem_tally(&tally, (Tier1ProblemReason)r);
        expect(strcmp(tier1_problem_reason_name((Tier1ProblemReason)r), "invalid-reason") != 0, "named rejection reason");
        expect(tally.reasons[r] == 1, "rejection counted once");
    }
    expect(tally.functions == TIER1_PROBLEM_REASON_COUNT && tally.accepted == 1 && tally.rejected == TIER1_PROBLEM_REASON_COUNT - 1, "no dropped functions in accounting");
    expect(strcmp(tier1_problem_reason_name((Tier1ProblemReason)-1), "invalid-reason") == 0, "invalid diagnostic enum");
}

static void mutation_tests(void)
{
    ProblemFixture f;
    problem_fixture(&f);
    uint8_t bytes[8192];
    uint8_t mutant[8192];
    uint8_t encoded[8192];
    size_t size = 0;
    expect(tier1_problem_encode(&f.problem, bytes, sizeof(bytes), &size) == TIER1_PROBLEM_OK, "mutation seed");
    uint64_t state = 1;
    for (uint32_t iteration = 0; iteration < 20000; iteration += 1)
    {
        memcpy(mutant, bytes, size);
        state = state * UINT64_C(6364136223846793005) + 1;
        const size_t at = (size_t)(state >> 32) % size;
        mutant[at] ^= (uint8_t)(1u << (state & 7));
        Tier1Problem p = { 0 };
        const Tier1ProblemReason reason = tier1_problem_decode(mutant, size, &p);
        expect((unsigned)reason < TIER1_PROBLEM_REASON_COUNT, "mutation returns a named outcome");
        if (reason == TIER1_PROBLEM_OK)
        {
            size_t n = 0;
            expect(tier1_problem_encode(&p, encoded, sizeof(encoded), &n) == TIER1_PROBLEM_OK, "accepted mutant re-encodes");
            expect(n == size && memcmp(encoded, mutant, size) == 0, "accepted mutant loses no fields");
        }
    }
    const long page = sysconf(_SC_PAGESIZE);
    expect(page > 0 && size < (size_t)page, "guard-page setup");
    if (page > 0 && size < (size_t)page)
    {
        uint8_t* memory = mmap(0, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        expect(memory != MAP_FAILED, "guard-page mapping");
        if (memory != MAP_FAILED)
        {
            const int protected = mprotect(memory + page, (size_t)page, PROT_NONE);
            expect(protected == 0, "guard page");
            if (protected == 0)
            {
                for (size_t n = 0; n <= size; n += 1)
                {
                    uint8_t* tail = memory + page - n;
                    memcpy(tail, bytes, n);
                    Tier1Problem p = { 0 };
                    const Tier1ProblemReason reason = tier1_problem_decode(tail, n, &p);
                    expect((reason == TIER1_PROBLEM_OK) == (n == size), "unpadded page-edge input");
                }
            }
            munmap(memory, (size_t)page * 2);
        }
    }
}

#ifdef BUSTER_TP_INTEGRATION
static void adapter_rejection(ProblemFixture* f, Tier1ProblemReason wanted, const char* name)
{
    _Alignas(64) uint8_t memory[16384];
    memset(memory, 0x5a, sizeof(memory));
    Arena arena = { .base = memory, .length = 17, .capacity = sizeof(memory) };
    Tier1AllocationInput input;
    Tier1ProblemAdapterStats stats;
    memset(&input, 0x5a, sizeof(input));
    memset(&stats, 0x5a, sizeof(stats));
    const Tier1AllocationInput old_input = input;
    const Tier1ProblemAdapterStats old_stats = stats;
    expect(tier1_problem_intervals(&f->problem, &arena, &input, &stats) == wanted, name);
    expect(arena.length == 17 && memcmp(&input, &old_input, sizeof(input)) == 0 && memcmp(&stats, &old_stats, sizeof(stats)) == 0, "adapter rollback and unchanged publication");
    for (uint32_t i = 0; i < 17; i += 1)
    {
        expect(memory[i] == 0x5a, "adapter preserves arena prefix");
    }
}

static void integration_tests(void)
{
    ProblemFixture f;
    problem_fixture(&f);
    _Alignas(64) uint8_t memory[65536];
    Arena arena = { .base = memory, .length = 17, .capacity = sizeof(memory) };
    Tier1AllocationInput input = { 0 };
    Tier1ProblemAdapterStats stats = { 0 };
    const Tier1ProblemReason reason = tier1_problem_intervals(&f.problem, &arena, &input, &stats);
    expect(reason == TIER1_PROBLEM_OK, "native-table adapter");
    if (reason == TIER1_PROBLEM_OK)
    {
        const uint32_t starts[3] = { 3, 7, 11 };
        const uint32_t ends[3] = { 9, 9, 13 };
        const uint32_t offsets[4] = { 0, 3, 5, 7 };
        const uint32_t positions[7] = { 3, 4, 8, 7, 8, 11, 12 };
        expect(input.registers[0] == 3 && input.registers[1] == 0, "reserved scratch excluded on both sides");
        expect(memcmp(input.starts, starts, sizeof(starts)) == 0 && memcmp(input.ends, ends, sizeof(ends)) == 0, "exact four-phase lifetimes");
        expect(memcmp(input.use_offsets, offsets, sizeof(offsets)) == 0 && memcmp(input.use_positions, positions, sizeof(positions)) == 0, "definitions and uses are register-required events");
        expect(stats.original_instructions == 4 && stats.original_operands == 7 && stats.retained_bytes == arena.length - 17 && stats.scratch_bytes != 0, "adapter reports original denominators and memory");
        Tier1Allocation vector = { 0 };
        Tier1Allocation scalar = { 0 };
        expect(tier1_allocate(&input, &arena, &vector) == TIER1_ALLOCATION_OK, "real allocator accepts imported input");
        expect(tier1_allocate_scalar(&input, &arena, &scalar) == TIER1_ALLOCATION_OK, "real scalar twin accepts imported input");
        expect(tier1_allocation_verify(&input, &vector) && tier1_allocation_verify(&input, &scalar), "occupancy smoke only, NOT a #939 certificate");
        expect(vector.spill_count == 0 && scalar.spill_count == 0, "fixture fits identical register bank");
    }
    uint8_t wire[8192];
    size_t length = 0;
    Tier1Problem decoded = { 0 };
    expect(tier1_problem_encode(&f.problem, wire, sizeof(wire), &length) == TIER1_PROBLEM_OK, "wire adapter seed");
    expect(tier1_problem_decode(wire, length, &decoded) == TIER1_PROBLEM_OK, "wire adapter decode");
    arena.length = 17;
    expect(tier1_problem_intervals(&decoded, &arena, &input, &stats) == TIER1_PROBLEM_OK, "wire view directly adapts without a second IR");
    const uint64_t required_capacity = arena.length + stats.scratch_bytes;
    for (uint64_t capacity = 17; capacity < required_capacity; capacity += 1)
    {
        arena.length = 17;
        arena.capacity = capacity;
        Tier1AllocationInput old = input;
        Tier1ProblemAdapterStats old_stats = stats;
        expect(tier1_problem_intervals(&decoded, &arena, &input, &stats) == TIER1_PROBLEM_CAPACITY, "every insufficient arena capacity");
        expect(arena.length == 17 && memcmp(&input, &old, sizeof(input)) == 0 && memcmp(&stats, &old_stats, sizeof(stats)) == 0, "capacity rollback");
    }
    arena.length = 17;
    arena.capacity = sizeof(memory);
    expect(tier1_problem_intervals(&f.problem, &arena, &input, (Tier1ProblemAdapterStats*)(void*)&input) == TIER1_PROBLEM_ARGUMENT, "overlapping output descriptors");
    expect(tier1_problem_intervals(&f.problem, &arena, (Tier1AllocationInput*)(void*)&f.problem, &stats) == TIER1_PROBLEM_ARGUMENT, "output cannot overwrite source descriptor");
    expect(tier1_problem_intervals(&f.problem, &arena, (Tier1AllocationInput*)(void*)f.operands, &stats) == TIER1_PROBLEM_ARGUMENT, "output cannot overwrite source table");
    expect(arena.length == 17, "alias refusals allocate nothing");
    problem_fixture_cfg(&f);
    adapter_rejection(&f, TIER1_PROBLEM_PHI, "CFG/phi input is not linearized silently");
    f.problem.tables[BUSTER_TP_PHI].count = 0;
    f.edges[0][3] = 0;
    f.edges[1][2] = 0;
    adapter_rejection(&f, TIER1_PROBLEM_CFG, "CFG without phi also refuses");
    problem_fixture_form(&f);
    adapter_rejection(&f, TIER1_PROBLEM_FORM, "alternative forms are not discarded");
    problem_fixture(&f);
    f.units[1] = 0;
    adapter_rejection(&f, TIER1_PROBLEM_ALIAS, "overlapping physical alias units");
    problem_fixture(&f);
    f.units[2] = 0;
    adapter_rejection(&f, TIER1_PROBLEM_ALIAS, "reserved alias is not available");
    problem_fixture(&f);
    f.operands[1][5] = 2;
    f.operands[2][5] = 1;
    adapter_rejection(&f, TIER1_PROBLEM_TIE, "tied definition");
    problem_fixture(&f);
    f.operands[1][2] = 1;
    f.operands[1][3] |= BUSTER_TP_EARLY_CLOBBER;
    adapter_rejection(&f, TIER1_PROBLEM_PHASE, "early clobber");
    problem_fixture(&f);
    f.operands[2][3] |= BUSTER_TP_IMPLICIT;
    adapter_rejection(&f, TIER1_PROBLEM_IMPLICIT, "implicit operand");
    problem_fixture(&f);
    f.operands[2][7] = 1;
    adapter_rejection(&f, TIER1_PROBLEM_SUBREGISTER, "subregister");
    problem_fixture(&f);
    f.values[0][3] = 1;
    adapter_rejection(&f, TIER1_PROBLEM_BOUNDARY, "ABI boundary value");
    problem_fixture(&f);
    f.operands[2][1] = 2;
    adapter_rejection(&f, TIER1_PROBLEM_SSA, "use before definition");
    problem_fixture(&f);
    f.operands[3][1] = 1;
    adapter_rejection(&f, TIER1_PROBLEM_SSA, "duplicate definition");
    problem_fixture(&f);
    f.problem.tables[BUSTER_TP_CLASS].count = 2;
    f.problem.tables[BUSTER_TP_MEMBER].count = 4;
    f.classes[1][0] = 301;
    f.classes[1][2] = 64;
    f.classes[1][3] = 3;
    f.classes[1][4] = 1;
    f.members[3] = 0;
    f.operands[2][4] = 1;
    adapter_rejection(&f, TIER1_PROBLEM_MASK_CHANGE, "changing per-use masks cannot be intersected away");
    problem_fixture(&f);
    f.classes[0][1] = 2;
    adapter_rejection(&f, TIER1_PROBLEM_CLASS, "unsupported register bank");
    problem_fixture(&f);
    f.registers[0][2] = 4;
    adapter_rejection(&f, TIER1_PROBLEM_CLASS, "RSP is never allocatable");
    problem_fixture(&f);
    f.problem.metadata[8].data = (const uint8_t*)"win64";
    f.problem.metadata[8].size = 5;
    adapter_rejection(&f, TIER1_PROBLEM_TARGET, "ABI mismatch");
}
#endif

int main(void)
{
    codec_tests();
    mutation_tests();
#ifdef BUSTER_TP_INTEGRATION
    integration_tests();
    const char* mode = "codec + production interval adapter";
#else
    const char* mode = "codec only";
#endif
    printf("tier1-problem (%s): %" PRIu64 " checks, %" PRIu64 " failures\n", mode, checks, failures);
    return failures == 0 ? 0 : 1;
}
