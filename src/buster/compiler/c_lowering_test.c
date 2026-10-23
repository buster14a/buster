// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tests of the selective-lowering declaration/edge contract, independent of source extraction. The reachability
// oracle scans the original unsorted references with a plain work queue; it reads no production CSR or bitsets.
// c_lowering_test_fixtures also pairs original C with MANUALLY modeled columns and checks both kernels' selected
// definitions against Clang's -O0 ELF symbols, in C17/GNU17 with C99/GNU89 inline semantics. These fixtures test
// emission policy, not a parser/checker adapter. Such an adapter must separately resolve redeclarations, inline
// eligibility, attributes and actual emitted dependencies (including initializer and evaluated VLA references).
// No external source, fixture corpus or generated table is incorporated. Include after c_lowering.c.

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_C_LOWERING_TEST_NODES 32u
#define BUSTER_C_LOWERING_TEST_REFERENCES 96u
#define BUSTER_C_LOWERING_TEST_PATH 4096u
#define BUSTER_C_LOWERING_TEST_LOCAL BUSTER_C_LOWERING_FUNCTION
#define BUSTER_C_LOWERING_TEST_ROOT (BUSTER_C_LOWERING_FUNCTION | BUSTER_C_LOWERING_EXTERNAL)
#define BUSTER_C_LOWERING_TEST_INLINE (BUSTER_C_LOWERING_FUNCTION | BUSTER_C_LOWERING_INLINE_ONLY)
#define BUSTER_C_LOWERING_TEST_DATA BUSTER_C_LOWERING_OBJECT
#define BUSTER_C_LOWERING_TEST_DATA_ROOT (BUSTER_C_LOWERING_OBJECT | BUSTER_C_LOWERING_EXTERNAL)

typedef struct CLoweringTest CLoweringTest;
struct CLoweringTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t state;
    uint32_t fixtures;
    uint32_t clang_comparisons;
    const char* clang;
    bool keep;
    char directory[BUSTER_C_LOWERING_TEST_PATH];
};

typedef struct CLoweringTestFixture CLoweringTestFixture;
struct CLoweringTestFixture
{
    const char* name;
    const char* source;
    const char* names[BUSTER_C_LOWERING_TEST_NODES];
    uint32_t flags[2][BUSTER_C_LOWERING_TEST_NODES];
    uint8_t expected[2][BUSTER_C_LOWERING_TEST_NODES];
    uint32_t count;
    uint32_t source_id[BUSTER_C_LOWERING_TEST_REFERENCES];
    uint32_t target_id[BUSTER_C_LOWERING_TEST_REFERENCES];
    uint32_t kind[BUSTER_C_LOWERING_TEST_REFERENCES];
    uint32_t references;
    // An alias has linker metadata, not another body to lower. Its target is one of the modeled definitions.
    const char* alias;
    uint32_t alias_target;
};

static void c_lowering_test_check(CLoweringTest* test, bool condition, const char* description)
{
    test->checks += 1;
    test->failures += !condition;
    if (!condition && test->failures <= 24)
    {
        fprintf(stderr, "lowering: %s\n", description);
    }
}

static uint32_t c_lowering_test_random(CLoweringTest* test)
{
    test->state = test->state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(test->state >> 32);
}

// Deliberately separate from the production root classifier. Emission eligibility precedes every root reason.
static bool c_lowering_test_root(uint32_t flags)
{
    const bool eligible = (flags & BUSTER_C_LOWERING_INLINE_ONLY) == 0;
    const bool external = (flags & BUSTER_C_LOWERING_EXTERNAL) != 0;
    const bool retained = (flags & BUSTER_C_LOWERING_USED) != 0;
    const bool constructor = (flags & BUSTER_C_LOWERING_CONSTRUCTOR) != 0;
    const bool destructor = (flags & BUSTER_C_LOWERING_DESTRUCTOR) != 0;
    const bool alias_target = (flags & BUSTER_C_LOWERING_ALIAS_TARGET) != 0;
    return eligible && (external || retained || constructor || destructor || alias_target);
}

static void c_lowering_test_oracle(CLoweringInput input, uint8_t* expected, uint32_t* queue)
{
    uint32_t queued = 0;
    for (uint32_t i = 0; i < input.definition_count; i += 1)
    {
        expected[i] = c_lowering_test_root(input.definition_flags[i]);
        if (expected[i])
        {
            queue[queued++] = i;
        }
    }
    for (uint32_t next = 0; next < queued; next += 1)
    {
        const uint32_t source = queue[next];
        for (uint32_t edge = 0; edge < input.reference_count; edge += 1)
        {
            if (input.reference_kind[edge] == C_LOWERING_REFERENCE_EVALUATED && input.reference_source[edge] == source)
            {
                const uint32_t target = input.reference_target[edge];
                if (!expected[target] && (input.definition_flags[target] & BUSTER_C_LOWERING_INLINE_ONLY) == 0)
                {
                    expected[target] = 1;
                    queue[queued++] = target;
                }
            }
        }
    }
}

static void c_lowering_test_result(CLoweringTest* test, const CLowering* lowering, CLoweringInput input, const uint8_t* expected, bool all)
{
    uint32_t count = 0;
    uint32_t roots = 0;
    uint64_t functions = 0;
    uint64_t objects = 0;
    for (uint32_t i = 0; i < input.definition_count; i += 1)
    {
        const bool selected = all ? (input.definition_flags[i] & BUSTER_C_LOWERING_INLINE_ONLY) == 0 : expected[i] != 0;
        const bool reached = (lowering->reached[i / 64] >> (i % 64)) & 1;
        c_lowering_test_check(test, reached == selected, "selection differs from independent expectation");
        roots += c_lowering_test_root(input.definition_flags[i]);
        if (selected)
        {
            c_lowering_test_check(test, count < lowering->selected_count && lowering->selected[count] == i, "selected IDs lost source order or contain duplicates");
            count += 1;
            functions += (input.definition_flags[i] & BUSTER_C_LOWERING_FUNCTION) != 0;
            objects += (input.definition_flags[i] & BUSTER_C_LOWERING_OBJECT) != 0;
        }
    }
    c_lowering_test_check(test, lowering->selected_count == count, "selected count differs");
    c_lowering_test_check(test, lowering->root_count == roots, "root count differs");
    c_lowering_test_check(test, lowering->definition_count == input.definition_count && lowering->reference_count == input.reference_count, "input counts not retained");
    c_lowering_test_check(test, lowering->word_count == ((uint64_t)input.definition_count + 63) / 64, "bitset word count differs");
    c_lowering_test_check(test, lowering->statistics.selected_functions == functions && lowering->statistics.selected_objects == objects, "function/object statistics differ");
    if (input.definition_count % 64 != 0)
    {
        const uint64_t tail = lowering->reached[lowering->word_count - 1] >> (input.definition_count % 64);
        c_lowering_test_check(test, tail == 0, "inactive reached bits are set");
    }
    if (all)
    {
        c_lowering_test_check(test, lowering->statistics.rounds == 0 && lowering->statistics.visited_edges == 0, "all-definition mode traversed edges");
    }
}

static void c_lowering_test_compare(CLoweringTest* test, CLoweringInput input, const uint8_t* supplied_expected, Arena* arena)
{
    const uint64_t mark = arena->length;
    CLowering vector = { 0 };
    CLowering scalar = { 0 };
    const CLoweringStatus vector_status = c_lowering_prepare(arena, input, &vector);
    const CLoweringStatus scalar_status = c_lowering_prepare(arena, input, &scalar);
    uint8_t* expected = arena_push_array(arena, (uint64_t)input.definition_count + 1, sizeof(uint8_t), 1);
    uint32_t* queue = arena_push_array(arena, (uint64_t)input.definition_count + 1, sizeof(uint32_t), 4);
    const bool ready = vector_status == C_LOWERING_OK && scalar_status == C_LOWERING_OK && expected != 0 && queue != 0;
    c_lowering_test_check(test, ready, "prepare/test allocation failed");
    if (ready)
    {
        if (supplied_expected != 0)
        {
            memcpy(expected, supplied_expected, input.definition_count);
        }
        else
        {
            c_lowering_test_oracle(input, expected, queue);
        }
        // Reusing the same prepared graph across modes must clear a preceding all-definition result.
        for (uint32_t pass = 0; pass < 3; pass += 1)
        {
            const bool all = pass == 1;
            const CLoweringMode mode = all ? C_LOWERING_ALL : C_LOWERING_REACHABLE;
            const uint64_t solve_mark = arena->length;
            const bool solved = c_lowering_vector(&vector, mode) == C_LOWERING_OK && c_lowering_scalar(&scalar, mode) == C_LOWERING_OK;
            c_lowering_test_check(test, solved, "solver refused a valid graph");
            c_lowering_test_check(test, arena->length == solve_mark, "solver changed the preparation arena");
            if (solved)
            {
                c_lowering_test_result(test, &vector, input, expected, all);
                c_lowering_test_result(test, &scalar, input, expected, all);
                c_lowering_test_check(test, vector.selected_count == scalar.selected_count, "scalar/vector selected counts differ");
                if (vector.word_count != 0)
                {
                    c_lowering_test_check(test, memcmp(vector.reached, scalar.reached, vector.word_count * sizeof(uint64_t)) == 0, "scalar/vector reached bitsets differ");
                }
                if (vector.selected_count == scalar.selected_count && vector.selected_count != 0)
                {
                    c_lowering_test_check(test, memcmp(vector.selected, scalar.selected, (uint64_t)vector.selected_count * sizeof(uint32_t)) == 0, "scalar/vector selected IDs differ");
                }
            }
        }
    }
    arena_pop_to(arena, mark);
}

static void c_lowering_test_graphs(CLoweringTest* test, Arena* arena)
{
    CLoweringInput empty = { 0 };
    c_lowering_test_compare(test, empty, 0, arena);
    uint32_t flags[257];
    uint32_t sources[2049];
    uint32_t targets[2049];
    uint32_t kinds[2049];
    for (uint32_t world = 0; world < 600; world += 1)
    {
        const uint32_t count = 1 + c_lowering_test_random(test) % 257;
        const uint32_t references = c_lowering_test_random(test) % 2049;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t choice = c_lowering_test_random(test) % 20;
            flags[i] = choice < 16 ? BUSTER_C_LOWERING_FUNCTION : BUSTER_C_LOWERING_OBJECT;
            if (choice == 0)
            {
                flags[i] |= BUSTER_C_LOWERING_INLINE_ONLY;
            }
            else if (choice == 1 || choice == 16)
            {
                flags[i] |= BUSTER_C_LOWERING_EXTERNAL;
            }
            else if (choice == 2 || choice == 17)
            {
                flags[i] |= BUSTER_C_LOWERING_USED;
            }
            else if (choice == 3)
            {
                flags[i] |= BUSTER_C_LOWERING_CONSTRUCTOR;
            }
            else if (choice == 4)
            {
                flags[i] |= BUSTER_C_LOWERING_DESTRUCTOR;
            }
            else if (choice == 18)
            {
                flags[i] |= BUSTER_C_LOWERING_ALIAS_TARGET;
            }
            // Regular root-free worlds must keep every cycle and self-edge dead.
            if (world % 19 == 0)
            {
                flags[i] &= BUSTER_C_LOWERING_FUNCTION | BUSTER_C_LOWERING_OBJECT | BUSTER_C_LOWERING_INLINE_ONLY;
            }
        }
        for (uint32_t edge = 0; edge < references; edge += 1)
        {
            sources[edge] = c_lowering_test_random(test) % count;
            targets[edge] = c_lowering_test_random(test) % count;
            kinds[edge] = c_lowering_test_random(test) % 5 == 0 ? 1 + c_lowering_test_random(test) % 2 : C_LOWERING_REFERENCE_EVALUATED;
            if (kinds[edge] != C_LOWERING_REFERENCE_EVALUATED && edge % 3 == 0)
            {
                targets[edge] = BUSTER_C_LOWERING_NONE;
            }
            if (edge != 0 && edge % 7 == 0)
            {
                sources[edge] = sources[edge - 1];
                targets[edge] = targets[edge - 1];
                kinds[edge] = kinds[edge - 1];
            }
        }
        const CLoweringInput input = {
            .definition_count = count,
            .reference_count = references,
            .definition_flags = flags,
            .reference_source = sources,
            .reference_target = targets,
            .reference_kind = kinds,
        };
        c_lowering_test_compare(test, input, 0, arena);
        // The producer is free to partition/reorder references. A reversed stream has exactly the same meaning.
        if (world % 17 == 0)
        {
            for (uint32_t edge = 0; edge < references / 2; edge += 1)
            {
                const uint32_t other = references - 1 - edge;
                const uint32_t source = sources[edge];
                const uint32_t target = targets[edge];
                const uint32_t kind = kinds[edge];
                sources[edge] = sources[other];
                targets[edge] = targets[other];
                kinds[edge] = kinds[other];
                sources[other] = source;
                targets[other] = target;
                kinds[other] = kind;
            }
            c_lowering_test_compare(test, input, 0, arena);
        }
    }
}

static void c_lowering_test_deep(CLoweringTest* test, Arena* arena)
{
    const uint64_t mark = arena->length;
    const uint32_t count = 100003;
    uint32_t* flags = arena_push_array(arena, count, sizeof(uint32_t), 4);
    uint32_t* sources = arena_push_array(arena, count, sizeof(uint32_t), 4);
    uint32_t* targets = arena_push_array(arena, count, sizeof(uint32_t), 4);
    uint32_t* kinds = arena_push_array(arena, count, sizeof(uint32_t), 4);
    uint8_t* expected = arena_push_array(arena, count, sizeof(uint8_t), 1);
    const bool ready = flags != 0 && sources != 0 && targets != 0 && kinds != 0 && expected != 0;
    c_lowering_test_check(test, ready, "deep graph allocation");
    if (ready)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            flags[i] = BUSTER_C_LOWERING_FUNCTION;
            sources[i] = i;
            targets[i] = i == 0 ? count - 1 : i - 1;
            kinds[i] = C_LOWERING_REFERENCE_EVALUATED;
            expected[i] = 1;
        }
        flags[count - 1] |= BUSTER_C_LOWERING_EXTERNAL;
        const CLoweringInput input = {
            .definition_count = count,
            .reference_count = count,
            .definition_flags = flags,
            .reference_source = sources,
            .reference_target = targets,
            .reference_kind = kinds,
        };
        // A reverse-source-order ring needs a bounded worklist, and its output still has forward source order.
        c_lowering_test_compare(test, input, expected, arena);
        flags[count - 1] = BUSTER_C_LOWERING_FUNCTION;
        memset(expected, 0, count);
        c_lowering_test_compare(test, input, expected, arena);
        // A wide fan-out exercises duplicate destinations in neighboring lanes without a long frontier chain.
        flags[count - 1] |= BUSTER_C_LOWERING_EXTERNAL;
        for (uint32_t i = 0; i < count; i += 1)
        {
            sources[i] = count - 1;
            targets[i] = i / 2;
            expected[i] = i <= (count - 1) / 2 || i == count - 1;
        }
        c_lowering_test_compare(test, input, expected, arena);
    }
    arena_pop_to(arena, mark);
}

static void c_lowering_test_reject(CLoweringTest* test, CLoweringInput input, Arena* arena, const char* description)
{
    const uint64_t mark = arena->length;
    CLowering output;
    memset(&output, 0xa5, sizeof(output));
    uint8_t before[sizeof(output)];
    memcpy(before, &output, sizeof(output));
    const CLoweringStatus status = c_lowering_prepare(arena, input, &output);
    c_lowering_test_check(test, status == C_LOWERING_INVALID, description);
    c_lowering_test_check(test, arena->length == mark, "malformed input leaked preparation storage");
    c_lowering_test_check(test, memcmp(&output, before, sizeof(output)) == 0, "malformed input published a partial graph");
    arena_pop_to(arena, mark);
}

static void c_lowering_test_limits(CLoweringTest* test, Arena* arena)
{
    uint32_t flags[3] = {
        BUSTER_C_LOWERING_TEST_ROOT,
        BUSTER_C_LOWERING_TEST_LOCAL,
        BUSTER_C_LOWERING_TEST_DATA,
    };
    uint32_t sources[3] = { 0 };
    uint32_t targets[3] = { 0 };
    uint32_t kinds[3] = { 0 };
    sources[0] = 0;
    sources[1] = 1;
    sources[2] = 2;
    targets[0] = 1;
    targets[1] = 2;
    targets[2] = 0;
    const CLoweringInput valid = {
        .definition_count = 3,
        .reference_count = 3,
        .definition_flags = flags,
        .reference_source = sources,
        .reference_target = targets,
        .reference_kind = kinds,
    };
    const uint32_t invalid_flags[] = {
        0,
        BUSTER_C_LOWERING_FUNCTION | BUSTER_C_LOWERING_OBJECT,
        BUSTER_C_LOWERING_FUNCTION | UINT32_C(0x80000000),
        BUSTER_C_LOWERING_OBJECT | BUSTER_C_LOWERING_INLINE_ONLY,
        BUSTER_C_LOWERING_OBJECT | BUSTER_C_LOWERING_CONSTRUCTOR,
        BUSTER_C_LOWERING_OBJECT | BUSTER_C_LOWERING_DESTRUCTOR,
        BUSTER_C_LOWERING_FUNCTION | BUSTER_C_LOWERING_EXTERNAL | BUSTER_C_LOWERING_INLINE_ONLY,
    };
    for (uint32_t i = 0; i < sizeof(invalid_flags) / sizeof(invalid_flags[0]); i += 1)
    {
        flags[1] = invalid_flags[i];
        c_lowering_test_reject(test, valid, arena, "invalid definition flags accepted");
    }
    flags[1] = BUSTER_C_LOWERING_FUNCTION;
    for (uint32_t column = 0; column < 4; column += 1)
    {
        CLoweringInput broken = valid;
        if (column == 0)
        {
            broken.definition_flags = 0;
        }
        else if (column == 1)
        {
            broken.reference_source = 0;
        }
        else if (column == 2)
        {
            broken.reference_target = 0;
        }
        else
        {
            broken.reference_kind = 0;
        }
        c_lowering_test_reject(test, broken, arena, "missing nonempty column accepted");
    }
    for (uint32_t kind = 0; kind <= C_LOWERING_REFERENCE_EXTERNAL; kind += 1)
    {
        kinds[0] = kind;
        sources[0] = 3;
        c_lowering_test_reject(test, valid, arena, "out-of-range source accepted");
        sources[0] = BUSTER_C_LOWERING_NONE;
        c_lowering_test_reject(test, valid, arena, "absent source accepted");
        sources[0] = 0;
        targets[0] = 3;
        c_lowering_test_reject(test, valid, arena, "out-of-range target accepted");
        targets[0] = BUSTER_C_LOWERING_NONE;
        if (kind == C_LOWERING_REFERENCE_EVALUATED)
        {
            c_lowering_test_reject(test, valid, arena, "absent evaluated target accepted");
        }
        else
        {
            c_lowering_test_compare(test, valid, 0, arena);
        }
        targets[0] = 1;
    }
    kinds[0] = C_LOWERING_REFERENCE_EXTERNAL + 1;
    c_lowering_test_reject(test, valid, arena, "unknown reference kind accepted");
    kinds[0] = C_LOWERING_REFERENCE_EVALUATED;
    CLoweringInput broken = valid;
    broken.definition_count = 0;
    c_lowering_test_reject(test, broken, arena, "reference in an empty graph accepted");
    broken = valid;
    broken.definition_count = UINT32_MAX;
    c_lowering_test_reject(test, broken, arena, "definition sentinel overflow accepted");
    broken = valid;
    broken.reference_count = UINT32_MAX;
    c_lowering_test_reject(test, broken, arena, "reference count overflow accepted");

    const uint64_t mark = arena->length;
    CLowering complete = { 0 };
    const bool prepared = c_lowering_prepare(arena, valid, &complete) == C_LOWERING_OK;
    const uint64_t bytes_needed = arena->length - mark;
    c_lowering_test_check(test, prepared, "capacity control preparation failed");
    arena_pop_to(arena, mark);
    bool saw_failure = false;
    bool saw_success = false;
    for (uint64_t bytes = 0; prepared && bytes <= bytes_needed + 64; bytes += 7)
    {
        Arena limited = *arena;
        limited.capacity = limited.length + bytes;
        CLowering output;
        memset(&output, 0x5a, sizeof(output));
        uint8_t before[sizeof(output)];
        memcpy(before, &output, sizeof(output));
        const CLoweringStatus status = c_lowering_prepare(&limited, valid, &output);
        saw_failure |= status == C_LOWERING_OUT_OF_MEMORY;
        saw_success |= status == C_LOWERING_OK;
        c_lowering_test_check(test, status == C_LOWERING_OK || status == C_LOWERING_OUT_OF_MEMORY, "capacity failure has wrong status");
        if (status == C_LOWERING_OUT_OF_MEMORY)
        {
            c_lowering_test_check(test, limited.length == mark && memcmp(&output, before, sizeof(output)) == 0, "capacity failure did not roll back");
        }
        else if (status == C_LOWERING_OK)
        {
            c_lowering_test_check(test, c_lowering_vector(&output, C_LOWERING_REACHABLE) == C_LOWERING_OK && output.selected_count == 3, "bounded arena produced an incomplete graph");
        }
    }
    c_lowering_test_check(test, saw_failure && saw_success, "capacity sweep missed success or exhaustion");
    Arena unavailable = { 0 };
    CLowering no_storage = { 0 };
    c_lowering_test_check(test, c_lowering_prepare(&unavailable, valid, &no_storage) == C_LOWERING_OUT_OF_MEMORY && unavailable.length == 0, "empty arena accepted a nonempty graph");
    c_lowering_test_check(test, c_lowering_prepare(&unavailable, (CLoweringInput){ 0 }, &no_storage) == C_LOWERING_OK, "empty graph requires storage");
    c_lowering_test_check(test, no_storage.workspace_bytes == 0 && c_lowering_vector(&no_storage, C_LOWERING_REACHABLE) == C_LOWERING_OK && c_lowering_scalar(&no_storage, C_LOWERING_ALL) == C_LOWERING_OK, "empty prepared graph cannot solve");

    CLowering owned = { 0 };
    if (c_lowering_prepare(arena, valid, &owned) == C_LOWERING_OK)
    {
        memset(flags, 0, sizeof(flags));
        memset(sources, 0xff, sizeof(sources));
        memset(targets, 0xff, sizeof(targets));
        memset(kinds, 0xff, sizeof(kinds));
        c_lowering_test_check(test, c_lowering_vector(&owned, C_LOWERING_REACHABLE) == C_LOWERING_OK && owned.selected_count == 3, "prepared vector graph borrows input storage");
        c_lowering_test_check(test, c_lowering_scalar(&owned, C_LOWERING_REACHABLE) == C_LOWERING_OK && owned.selected_count == 3, "prepared scalar graph borrows input storage");
        c_lowering_test_check(test, c_lowering_vector(&owned, (CLoweringMode)99) == C_LOWERING_INVALID && c_lowering_scalar(&owned, (CLoweringMode)99) == C_LOWERING_INVALID, "unknown solve mode accepted");
    }
    else
    {
        c_lowering_test_check(test, false, "owned-input preparation failed");
    }
    arena_pop_to(arena, mark);
}

static void c_lowering_test_guards(CLoweringTest* test, Arena* arena)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const bool page_valid = page_value >= 4096;
    c_lowering_test_check(test, page_valid, "guard page size unavailable");
    const uint64_t page = page_valid ? (uint64_t)page_value : 4096;
    uint8_t* mappings[10] = { 0 };
    bool mapped = page_valid;
    for (uint32_t column = 0; column < 10; column += 1)
    {
        mappings[column] = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mapped &= mappings[column] != MAP_FAILED;
        if (mappings[column] != MAP_FAILED)
        {
            mapped &= mprotect(mappings[column] + page, page, PROT_READ | PROT_WRITE) == 0;
        }
    }
    c_lowering_test_check(test, mapped, "guard page mappings failed");
    const uint32_t extra_lengths[] = {
        127,
        128,
        129,
        255,
        256,
        257,
    };
    for (uint32_t side = 0; mapped && side < 2; side += 1)
    {
        for (uint32_t case_id = 0; case_id < 98 + sizeof(extra_lengths) / sizeof(extra_lengths[0]); case_id += 1)
        {
            const uint32_t count = case_id < 98 ? case_id : extra_lengths[case_id - 98];
            const uint32_t references = count == 0 ? 0 : (count * 3 + 1) % 509;
            const uint64_t words = ((uint64_t)count + 63) / 64;
            uint32_t* flags = (uint32_t*)(mappings[0] + page * (side ? 2 : 1)) - (side ? count : 0);
            uint32_t* sources = (uint32_t*)(mappings[1] + page * (side ? 2 : 1)) - (side ? references : 0);
            uint32_t* targets = (uint32_t*)(mappings[2] + page * (side ? 2 : 1)) - (side ? references : 0);
            uint32_t* kinds = (uint32_t*)(mappings[3] + page * (side ? 2 : 1)) - (side ? references : 0);
            uint64_t* reached = (uint64_t*)(mappings[4] + page * (side ? 2 : 1)) - (side ? words : 0);
            uint32_t* selected = (uint32_t*)(mappings[5] + page * (side ? 2 : 1)) - (side ? count : 0);
            uint8_t expected[257];
            uint32_t queue[257];
            for (uint32_t i = 0; i < count; i += 1)
            {
                flags[i] = i % 5 == 0 ? BUSTER_C_LOWERING_OBJECT : BUSTER_C_LOWERING_FUNCTION;
                flags[i] |= i % 17 == 0 ? BUSTER_C_LOWERING_EXTERNAL : 0;
            }
            for (uint32_t i = 0; i < references; i += 1)
            {
                sources[i] = (i * 7 + 1) % count;
                targets[i] = i % 3 == 0 ? count - 1 : (i * 11 + 3) % count;
                kinds[i] = i % 11 == 0 ? C_LOWERING_REFERENCE_UNEVALUATED : C_LOWERING_REFERENCE_EVALUATED;
            }
            const CLoweringInput input = {
                .definition_count = count,
                .reference_count = references,
                .definition_flags = flags,
                .reference_source = sources,
                .reference_target = targets,
                .reference_kind = kinds,
            };
            c_lowering_test_oracle(input, expected, queue);
            const uint64_t mark = arena->length;
            CLowering lowering = { 0 };
            const bool prepared = c_lowering_prepare(arena, input, &lowering) == C_LOWERING_OK;
            c_lowering_test_check(test, prepared, "guarded input preparation failed");
            if (prepared)
            {
                // Deliberate instrumentation of copied columns too: guarding only the original input cannot
                // catch an overread of the solver's arena-backed copy. None of these columns require padding.
                const uint32_t edges = lowering.edge_count;
                uint32_t* edge_targets = (uint32_t*)(mappings[6] + page * (side ? 2 : 1)) - (side ? edges : 0);
                uint32_t* pending = (uint32_t*)(mappings[7] + page * (side ? 2 : 1)) - (side ? count : 0);
                uint8_t* visited = mappings[8] + page * (side ? 2 : 1) - (side ? count : 0);
                uint32_t* offsets = (uint32_t*)(mappings[9] + page * (side ? 2 : 1)) - (side && count != 0 ? count + 1 : 0);
                if (edges != 0)
                {
                    memcpy(edge_targets, lowering.edge_targets, (uint64_t)edges * sizeof(uint32_t));
                }
                if (count != 0)
                {
                    memcpy(offsets, lowering.edge_offsets, ((uint64_t)count + 1) * sizeof(uint32_t));
                }
                lowering.definition_flags = flags;
                lowering.edge_targets = edge_targets;
                lowering.edge_offsets = offsets;
                lowering.queue = pending;
                lowering.visited = visited;
                lowering.reached = reached;
                lowering.selected = selected;
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    for (uint32_t mode = 0; mode < 2; mode += 1)
                    {
                        if (count != 0)
                        {
                            memset(selected, 0xa5, (uint64_t)count * sizeof(uint32_t));
                            memset(reached, 0xff, words * sizeof(uint64_t));
                        }
                        const CLoweringMode selected_mode = mode ? C_LOWERING_ALL : C_LOWERING_REACHABLE;
                        const CLoweringStatus status = path ? c_lowering_scalar(&lowering, selected_mode) : c_lowering_vector(&lowering, selected_mode);
                        c_lowering_test_check(test, status == C_LOWERING_OK, "guarded output solve failed");
                        if (status == C_LOWERING_OK)
                        {
                            c_lowering_test_result(test, &lowering, input, expected, mode != 0);
                        }
                    }
                }
            }
            arena_pop_to(arena, mark);
        }
    }
    for (uint32_t column = 0; column < 10; column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], page * 3);
        }
    }
}

static bool c_lowering_test_range(uint64_t size, uint64_t offset, uint64_t length)
{
    return offset <= size && length <= size - offset;
}

// The oracle reads the actual object, not compiler diagnostics or assembly text. Every defined function/object
// symbol must be accounted for; an absent, duplicate or unexpected symbol is a failure. Alias metadata is checked
// separately against its selected target's section and value, because an alias is not an additional lowering body.
static bool c_lowering_test_symbols(CLoweringTest* test, const CLoweringTestFixture* fixture, uint32_t mode, const char* path)
{
    bool valid = false;
    uint8_t* bytes = 0;
    uint64_t size = 0;
    FILE* file = fopen(path, "rb");
    if (file != 0)
    {
        struct stat information;
        if (fstat(fileno(file), &information) == 0 && information.st_size >= (off_t)sizeof(Elf64_Ehdr) && information.st_size <= (16 << 20))
        {
            size = (uint64_t)information.st_size;
            bytes = malloc((size_t)size);
            valid = bytes != 0 && fread(bytes, 1, (size_t)size, file) == size;
        }
        valid = fclose(file) == 0 && valid;
    }
    Elf64_Ehdr header = { 0 };
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_ident[EI_CLASS] == ELFCLASS64 && header.e_ident[EI_DATA] == ELFDATA2LSB;
        valid = valid && header.e_type == ET_REL && header.e_machine == EM_X86_64 && header.e_shentsize == sizeof(Elf64_Shdr) && header.e_shnum != 0;
        valid = valid && c_lowering_test_range(size, header.e_shoff, (uint64_t)header.e_shnum * sizeof(Elf64_Shdr));
    }
    uint32_t seen[BUSTER_C_LOWERING_TEST_NODES] = { 0 };
    Elf64_Sym definitions[BUSTER_C_LOWERING_TEST_NODES] = { 0 };
    Elf64_Sym alias = { 0 };
    uint32_t aliases = 0;
    uint32_t symbol_tables = 0;
    for (uint32_t section = 0; valid && section < header.e_shnum; section += 1)
    {
        Elf64_Shdr symbols;
        memcpy(&symbols, bytes + header.e_shoff + (uint64_t)section * sizeof(symbols), sizeof(symbols));
        if (symbols.sh_type == SHT_SYMTAB)
        {
            symbol_tables += 1;
            valid = symbols.sh_entsize == sizeof(Elf64_Sym) && symbols.sh_size % sizeof(Elf64_Sym) == 0 && symbols.sh_link < header.e_shnum;
            valid = valid && c_lowering_test_range(size, symbols.sh_offset, symbols.sh_size);
            Elf64_Shdr strings = { 0 };
            if (valid)
            {
                memcpy(&strings, bytes + header.e_shoff + (uint64_t)symbols.sh_link * sizeof(strings), sizeof(strings));
                valid = strings.sh_type == SHT_STRTAB && c_lowering_test_range(size, strings.sh_offset, strings.sh_size);
            }
            for (uint64_t offset = 0; valid && offset < symbols.sh_size; offset += sizeof(Elf64_Sym))
            {
                Elf64_Sym symbol;
                memcpy(&symbol, bytes + symbols.sh_offset + offset, sizeof(symbol));
                const uint32_t type = ELF64_ST_TYPE(symbol.st_info);
                if (symbol.st_shndx != SHN_UNDEF && (type == STT_FUNC || type == STT_OBJECT || type == STT_TLS))
                {
                    valid = symbol.st_name < strings.sh_size;
                    const char* name = valid ? (const char*)bytes + strings.sh_offset + symbol.st_name : "";
                    valid = valid && memchr(name, 0, (size_t)(strings.sh_size - symbol.st_name)) != 0;
                    bool matched = false;
                    for (uint32_t i = 0; valid && i < fixture->count; i += 1)
                    {
                        if (strcmp(name, fixture->names[i]) == 0)
                        {
                            seen[i] += 1;
                            definitions[i] = symbol;
                            matched = true;
                        }
                    }
                    if (valid && fixture->alias != 0 && strcmp(name, fixture->alias) == 0)
                    {
                        aliases += 1;
                        alias = symbol;
                        matched = true;
                    }
                    if (valid && !matched)
                    {
                        fprintf(stderr, "lowering: %s (%s): unexpected Clang definition %s\n", fixture->name, mode ? "GNU89 inline" : "C99 inline", name);
                        valid = false;
                    }
                }
            }
        }
    }
    valid = valid && symbol_tables == 1;
    if (valid)
    {
        for (uint32_t i = 0; i < fixture->count; i += 1)
        {
            const uint32_t expected = fixture->expected[mode][i] != 0;
            if (seen[i] != expected)
            {
                fprintf(stderr, "lowering: %s (%s): %s expected %u definition, Clang wrote %u\n", fixture->name, mode ? "GNU89 inline" : "C99 inline", fixture->names[i], expected, seen[i]);
                valid = false;
            }
        }
        if (fixture->alias != 0)
        {
            const uint32_t target = fixture->alias_target;
            const bool same = target < fixture->count && seen[target] == 1 && aliases == 1 && alias.st_shndx == definitions[target].st_shndx && alias.st_value == definitions[target].st_value && ELF64_ST_TYPE(alias.st_info) == ELF64_ST_TYPE(definitions[target].st_info);
            c_lowering_test_check(test, same, "Clang alias does not identify its selected target");
            valid &= same;
        }
    }
    free(bytes);
    return valid;
}

static bool c_lowering_test_compile(const char* clang, const char* standard, const char* inline_mode, const char* source, const char* object, const char* log)
{
    bool good = false;
    const int descriptor = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor >= 0)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            const bool redirected = dup2(descriptor, STDOUT_FILENO) >= 0 && dup2(descriptor, STDERR_FILENO) >= 0;
            if (descriptor > STDERR_FILENO)
            {
                close(descriptor);
            }
            if (redirected)
            {
                // argv is passed directly: a path is never interpolated into a shell command.
                char* const arguments[] = {
                    (char*)clang,
                    (char*)standard,
                    (char*)inline_mode,
                    "--target=x86_64-unknown-linux-gnu",
                    "-march=x86-64",
                    "-O0",
                    "-fno-lto",
                    "-fno-common",
                    "-c",
                    (char*)source,
                    "-o",
                    (char*)object,
                    0,
                };
                execvp(clang, arguments);
            }
            _exit(127);
        }
        close(descriptor);
        if (child > 0)
        {
            int status = 0;
            pid_t waited = 0;
            const struct timespec pause = {
                .tv_sec = 0,
                .tv_nsec = 1000000,
            };
            for (uint32_t milliseconds = 0; waited == 0 && milliseconds < 30000; milliseconds += 1)
            {
                waited = waitpid(child, &status, WNOHANG);
                if (waited < 0 && errno == EINTR)
                {
                    waited = 0;
                }
                if (waited == 0)
                {
                    nanosleep(&pause, 0);
                }
            }
            if (waited == 0)
            {
                kill(child, SIGKILL);
                do
                {
                    waited = waitpid(child, &status, 0);
                }
                while (waited < 0 && errno == EINTR);
            }
            else
            {
                good = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
        }
    }
    return good;
}

static void c_lowering_test_node(CLoweringTestFixture* fixture, const char* name, uint32_t c99_flags, uint32_t gnu_flags, uint32_t expected_modes)
{
    const uint32_t id = fixture->count++;
    fixture->names[id] = name;
    fixture->flags[0][id] = c99_flags;
    fixture->flags[1][id] = gnu_flags;
    fixture->expected[0][id] = (expected_modes & 1) != 0;
    fixture->expected[1][id] = (expected_modes & 2) != 0;
}

static void c_lowering_test_edge(CLoweringTestFixture* fixture, uint32_t source, uint32_t target, CLoweringReferenceKind kind)
{
    const uint32_t id = fixture->references++;
    fixture->source_id[id] = source;
    fixture->target_id[id] = target;
    fixture->kind[id] = kind;
}

static void c_lowering_test_fixture(CLoweringTest* test, CLoweringTestFixture* fixture, Arena* arena)
{
    const uint32_t id = test->fixtures++;
    const uint64_t failures_before = test->failures;
    char source[BUSTER_C_LOWERING_TEST_PATH];
    char object[BUSTER_C_LOWERING_TEST_PATH];
    char log[BUSTER_C_LOWERING_TEST_PATH];
    const int source_length = snprintf(source, sizeof(source), "%s/fixture-%u.c", test->directory, id);
    bool source_written = test->clang == 0;
    if (test->clang != 0 && source_length > 0 && (uint64_t)source_length < sizeof(source))
    {
        FILE* file = fopen(source, "wb");
        if (file != 0)
        {
            const size_t length = strlen(fixture->source);
            source_written = fwrite(fixture->source, 1, length, file) == length;
            source_written = fclose(file) == 0 && source_written;
        }
    }
    c_lowering_test_check(test, source_written, "could not write Clang fixture");
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        const CLoweringInput input = {
            .definition_count = fixture->count,
            .reference_count = fixture->references,
            .definition_flags = fixture->flags[mode],
            .reference_source = fixture->source_id,
            .reference_target = fixture->target_id,
            .reference_kind = fixture->kind,
        };
        c_lowering_test_compare(test, input, fixture->expected[mode], arena);
        if (test->clang != 0 && source_written)
        {
            for (uint32_t dialect = 0; dialect < 2; dialect += 1)
            {
                const char* standard = dialect ? "-std=gnu17" : "-std=c17";
                const char* inline_mode = mode ? "-fgnu89-inline" : "-fno-gnu89-inline";
                const int object_length = snprintf(object, sizeof(object), "%s/fixture-%u-%u-%u.o", test->directory, id, mode, dialect);
                const int log_length = snprintf(log, sizeof(log), "%s/fixture-%u-%u-%u.log", test->directory, id, mode, dialect);
                const bool paths = object_length > 0 && (uint64_t)object_length < sizeof(object) && log_length > 0 && (uint64_t)log_length < sizeof(log);
                const bool compiled = paths && c_lowering_test_compile(test->clang, standard, inline_mode, source, object, log);
                const bool compared = compiled && c_lowering_test_symbols(test, fixture, mode, object);
                test->clang_comparisons += compared;
                c_lowering_test_check(test, compiled, "requested Clang failed or could not run");
                c_lowering_test_check(test, !compiled || compared, "Clang definitions differ from modeled lowering selection");
                if (!compiled || !compared)
                {
                    fprintf(stderr, "lowering: fixture %s %s %s; source %s; log %s\n", fixture->name, standard, inline_mode, source, paths ? log : "(path too long)");
                }
                else if (!test->keep)
                {
                    unlink(object);
                    unlink(log);
                }
            }
        }
    }
    if (test->clang != 0 && test->failures == failures_before && !test->keep)
    {
        unlink(source);
    }
}

static void c_lowering_test_fixtures(CLoweringTest* test, Arena* arena)
{
    const uint32_t function = BUSTER_C_LOWERING_TEST_LOCAL;
    const uint32_t root = BUSTER_C_LOWERING_TEST_ROOT;
    const uint32_t inline_only = BUSTER_C_LOWERING_TEST_INLINE;
    const uint32_t object = BUSTER_C_LOWERING_TEST_DATA;
    const uint32_t data_root = BUSTER_C_LOWERING_TEST_DATA_ROOT;
    CLoweringTestFixture fixture = {
        .name = "empty-definition-set",
        .source = "typedef int no_emitted_definition;\n",
    };
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "static-inline-chain-and-unused-bodies",
        .source = "static inline int chain_leaf(void) { return 3; }\n"
                  "static inline int chain_middle(void) { return chain_leaf(); }\n"
                  "int chain_root(void) { return chain_middle() + chain_middle(); }\n"
                  "static inline int unused_inline(void) { return 4; }\n"
                  "static int unused_plain(void) { return unused_inline(); }\n",
    };
    c_lowering_test_node(&fixture, "chain_leaf", function, function, 3);
    c_lowering_test_node(&fixture, "chain_middle", function, function, 3);
    c_lowering_test_node(&fixture, "chain_root", root, root, 3);
    c_lowering_test_node(&fixture, "unused_inline", function, function, 0);
    c_lowering_test_node(&fixture, "unused_plain", function, function, 0);
    c_lowering_test_edge(&fixture, 2, 1, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 1, 0, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 2, 1, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 4, 3, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "object-roots-and-initializer-references",
        .source = "static int initializer_object;\n"
                  "static int initializer_function(void) { return initializer_object; }\n"
                  "int external_data = 4;\n"
                  "int external_tentative;\n"
                  "int* object_pointer = &initializer_object;\n"
                  "int (*function_pointer)(void) = initializer_function;\n"
                  "extern int imported_object;\n"
                  "int* imported_pointer = &imported_object;\n"
                  "static int unused_object;\n",
    };
    c_lowering_test_node(&fixture, "initializer_object", object, object, 3);
    c_lowering_test_node(&fixture, "initializer_function", function, function, 3);
    c_lowering_test_node(&fixture, "external_data", data_root, data_root, 3);
    c_lowering_test_node(&fixture, "external_tentative", data_root, data_root, 3);
    c_lowering_test_node(&fixture, "object_pointer", data_root, data_root, 3);
    c_lowering_test_node(&fixture, "function_pointer", data_root, data_root, 3);
    c_lowering_test_node(&fixture, "imported_pointer", data_root, data_root, 3);
    c_lowering_test_node(&fixture, "unused_object", object, object, 0);
    c_lowering_test_edge(&fixture, 4, 0, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 5, 1, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 1, 0, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 6, BUSTER_C_LOWERING_NONE, C_LOWERING_REFERENCE_EXTERNAL);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "inline-dialects-calls-and-addresses",
        .source = "inline int plain_unused(void) { return 1; }\n"
                  "inline int plain_called(void) { return 2; }\n"
                  "inline int plain_addressed(void) { return 3; }\n"
                  "int plain_caller(void) { return plain_called(); }\n"
                  "int (*plain_address)(void) = plain_addressed;\n"
                  "extern inline int extern_unused(void) { return 4; }\n"
                  "extern inline int extern_called(void) { return 5; }\n"
                  "extern inline int extern_addressed(void) { return 6; }\n"
                  "int extern_caller(void) { return extern_called(); }\n"
                  "int (*extern_address)(void) = extern_addressed;\n",
    };
    c_lowering_test_node(&fixture, "plain_unused", inline_only, root, 2);
    c_lowering_test_node(&fixture, "plain_called", inline_only, root, 2);
    c_lowering_test_node(&fixture, "plain_addressed", inline_only, root, 2);
    c_lowering_test_node(&fixture, "plain_caller", root, root, 3);
    c_lowering_test_node(&fixture, "plain_address", data_root, data_root, 3);
    c_lowering_test_node(&fixture, "extern_unused", root, inline_only, 1);
    c_lowering_test_node(&fixture, "extern_called", root, inline_only, 1);
    c_lowering_test_node(&fixture, "extern_addressed", root, inline_only, 1);
    c_lowering_test_node(&fixture, "extern_caller", root, root, 3);
    c_lowering_test_node(&fixture, "extern_address", data_root, data_root, 3);
    c_lowering_test_edge(&fixture, 3, 1, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 4, 2, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 8, 6, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 9, 7, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "inline-redeclarations",
        .source = "extern int prior_extern(void);\n"
                  "inline int prior_extern(void) { return 1; }\n"
                  "inline int after_extern(void) { return 2; }\n"
                  "extern int after_extern(void);\n"
                  "extern inline int extern_then_inline(void) { return 3; }\n"
                  "inline int extern_then_inline(void);\n"
                  "inline int plain_then_extern_inline(void) { return 4; }\n"
                  "extern inline int plain_then_extern_inline(void);\n"
                  "inline int repeated_plain(void) { return 5; }\n"
                  "inline int repeated_plain(void);\n"
                  "extern inline int repeated_extern(void) { return 6; }\n"
                  "extern inline int repeated_extern(void);\n",
    };
    c_lowering_test_node(&fixture, "prior_extern", root, root, 3);
    c_lowering_test_node(&fixture, "after_extern", root, root, 3);
    c_lowering_test_node(&fixture, "extern_then_inline", root, root, 3);
    c_lowering_test_node(&fixture, "plain_then_extern_inline", root, root, 3);
    c_lowering_test_node(&fixture, "repeated_plain", inline_only, root, 2);
    c_lowering_test_node(&fixture, "repeated_extern", root, inline_only, 1);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "per-function-gnu-inline-attribute",
        .source = "inline __attribute__((gnu_inline)) int gnu_plain(void) { return 1; }\n"
                  "extern inline __attribute__((gnu_inline)) int gnu_extern(void) { return 2; }\n"
                  "static inline __attribute__((gnu_inline)) int gnu_static(void) { return 3; }\n"
                  "int gnu_caller(void) { return gnu_plain() + gnu_extern() + gnu_static(); }\n",
    };
    c_lowering_test_node(&fixture, "gnu_plain", root, root, 3);
    c_lowering_test_node(&fixture, "gnu_extern", inline_only, inline_only, 0);
    c_lowering_test_node(&fixture, "gnu_static", function, function, 3);
    c_lowering_test_node(&fixture, "gnu_caller", root, root, 3);
    c_lowering_test_edge(&fixture, 3, 0, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 3, 1, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 3, 2, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "retention-attributes",
        .source = "static int used_helper(void) { return 1; }\n"
                  "static __attribute__((used)) int used_function(void) { return used_helper(); }\n"
                  "static __attribute__((used)) int used_object = 2;\n"
                  "static void ctor_helper(void) { used_object += 1; }\n"
                  "static __attribute__((constructor)) void retained_constructor(void) { ctor_helper(); }\n"
                  "static void dtor_helper(void) { used_object -= 1; }\n"
                  "static __attribute__((destructor)) void retained_destructor(void) { dtor_helper(); }\n",
    };
    c_lowering_test_node(&fixture, "used_helper", function, function, 3);
    c_lowering_test_node(&fixture, "used_function", function | BUSTER_C_LOWERING_USED, function | BUSTER_C_LOWERING_USED, 3);
    c_lowering_test_node(&fixture, "used_object", object | BUSTER_C_LOWERING_USED, object | BUSTER_C_LOWERING_USED, 3);
    c_lowering_test_node(&fixture, "ctor_helper", function, function, 3);
    c_lowering_test_node(&fixture, "retained_constructor", function | BUSTER_C_LOWERING_CONSTRUCTOR, function | BUSTER_C_LOWERING_CONSTRUCTOR, 3);
    c_lowering_test_node(&fixture, "dtor_helper", function, function, 3);
    c_lowering_test_node(&fixture, "retained_destructor", function | BUSTER_C_LOWERING_DESTRUCTOR, function | BUSTER_C_LOWERING_DESTRUCTOR, 3);
    c_lowering_test_edge(&fixture, 1, 0, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 4, 3, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 3, 2, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 6, 5, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 5, 2, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "function-alias-target",
        .source = "static int aliased_function(void) { return 7; }\n"
                  "extern int exported_alias(void) __attribute__((alias(\"aliased_function\")));\n",
        .alias = "exported_alias",
        .alias_target = 0,
    };
    c_lowering_test_node(&fixture, "aliased_function", function | BUSTER_C_LOWERING_ALIAS_TARGET, function | BUSTER_C_LOWERING_ALIAS_TARGET, 3);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "object-alias-target",
        .source = "static int aliased_object = 9;\n"
                  "extern int exported_object_alias __attribute__((alias(\"aliased_object\")));\n",
        .alias = "exported_object_alias",
        .alias_target = 0,
    };
    c_lowering_test_node(&fixture, "aliased_object", object | BUSTER_C_LOWERING_ALIAS_TARGET, object | BUSTER_C_LOWERING_ALIAS_TARGET, 3);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "unevaluated-operands-and-generic-selection",
        .source = "static int sizeof_unused(void) { return 1; }\n"
                  "static int alignof_unused(void) { return 2; }\n"
                  "static int generic_control_unused(void) { return 3; }\n"
                  "static int generic_default_unused(void) { return 4; }\n"
                  "static int generic_selected(void) { return 5; }\n"
                  "int unevaluated_root(void) { return sizeof(sizeof_unused()) + __alignof__(alignof_unused()) + _Generic(generic_control_unused(), int: generic_selected(), default: generic_default_unused()); }\n",
    };
    c_lowering_test_node(&fixture, "sizeof_unused", function, function, 0);
    c_lowering_test_node(&fixture, "alignof_unused", function, function, 0);
    c_lowering_test_node(&fixture, "generic_control_unused", function, function, 0);
    c_lowering_test_node(&fixture, "generic_default_unused", function, function, 0);
    c_lowering_test_node(&fixture, "generic_selected", function, function, 3);
    c_lowering_test_node(&fixture, "unevaluated_root", root, root, 3);
    for (uint32_t i = 0; i < 4; i += 1)
    {
        c_lowering_test_edge(&fixture, 5, i, C_LOWERING_REFERENCE_UNEVALUATED);
    }
    c_lowering_test_edge(&fixture, 5, 4, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "evaluated-vla-size-and-unevaluated-alignment",
        .source = "static int vla_bound(void) { return 2; }\n"
                  "static int vla_alignment_unused(void) { return 3; }\n"
                  "unsigned long vla_root(void) { return sizeof(int[vla_bound()]) + _Alignof(int[vla_alignment_unused()]); }\n",
    };
    c_lowering_test_node(&fixture, "vla_bound", function, function, 3);
    c_lowering_test_node(&fixture, "vla_alignment_unused", function, function, 0);
    c_lowering_test_node(&fixture, "vla_root", root, root, 3);
    c_lowering_test_edge(&fixture, 2, 0, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 2, 1, C_LOWERING_REFERENCE_UNEVALUATED);
    c_lowering_test_fixture(test, &fixture, arena);

    // This intentionally uses Clang's accepted static-in-inline extension. The warning does not change the -O0
    // emission rule: an inline-only body's references must not retain either helper, even when its address is used.
    fixture = (CLoweringTestFixture){
        .name = "inline-only-body-dependencies",
        .source = "static int plain_dependency(void) { return 1; }\n"
                  "inline int plain_body(void) { return plain_dependency(); }\n"
                  "int (*plain_body_address)(void) = plain_body;\n"
                  "static int extern_dependency(void) { return 2; }\n"
                  "extern inline int extern_body(void) { return extern_dependency(); }\n"
                  "int (*extern_body_address)(void) = extern_body;\n",
    };
    c_lowering_test_node(&fixture, "plain_dependency", function, function, 2);
    c_lowering_test_node(&fixture, "plain_body", inline_only, root, 2);
    c_lowering_test_node(&fixture, "plain_body_address", data_root, data_root, 3);
    c_lowering_test_node(&fixture, "extern_dependency", function, function, 1);
    c_lowering_test_node(&fixture, "extern_body", root, inline_only, 1);
    c_lowering_test_node(&fixture, "extern_body_address", data_root, data_root, 3);
    c_lowering_test_edge(&fixture, 1, 0, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 2, 1, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 4, 3, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_edge(&fixture, 5, 4, C_LOWERING_REFERENCE_EVALUATED);
    c_lowering_test_fixture(test, &fixture, arena);

    fixture = (CLoweringTestFixture){
        .name = "inline-only-retention-attributes",
        .source = "static int plain_used_dependency(void) { return 1; }\n"
                  "inline __attribute__((used)) int plain_used(void) { return plain_used_dependency(); }\n"
                  "static int extern_used_dependency(void) { return 2; }\n"
                  "extern inline __attribute__((used)) int extern_used(void) { return extern_used_dependency(); }\n"
                  "static void plain_ctor_dependency(void) { }\n"
                  "inline __attribute__((constructor)) void plain_ctor(void) { plain_ctor_dependency(); }\n"
                  "static void extern_ctor_dependency(void) { }\n"
                  "extern inline __attribute__((constructor)) void extern_ctor(void) { extern_ctor_dependency(); }\n"
                  "static void plain_dtor_dependency(void) { }\n"
                  "inline __attribute__((destructor)) void plain_dtor(void) { plain_dtor_dependency(); }\n"
                  "static void extern_dtor_dependency(void) { }\n"
                  "extern inline __attribute__((destructor)) void extern_dtor(void) { extern_dtor_dependency(); }\n",
    };
    c_lowering_test_node(&fixture, "plain_used_dependency", function, function, 2);
    c_lowering_test_node(&fixture, "plain_used", inline_only | BUSTER_C_LOWERING_USED, root | BUSTER_C_LOWERING_USED, 2);
    c_lowering_test_node(&fixture, "extern_used_dependency", function, function, 1);
    c_lowering_test_node(&fixture, "extern_used", root | BUSTER_C_LOWERING_USED, inline_only | BUSTER_C_LOWERING_USED, 1);
    c_lowering_test_node(&fixture, "plain_ctor_dependency", function, function, 2);
    c_lowering_test_node(&fixture, "plain_ctor", inline_only | BUSTER_C_LOWERING_CONSTRUCTOR, root | BUSTER_C_LOWERING_CONSTRUCTOR, 2);
    c_lowering_test_node(&fixture, "extern_ctor_dependency", function, function, 1);
    c_lowering_test_node(&fixture, "extern_ctor", root | BUSTER_C_LOWERING_CONSTRUCTOR, inline_only | BUSTER_C_LOWERING_CONSTRUCTOR, 1);
    c_lowering_test_node(&fixture, "plain_dtor_dependency", function, function, 2);
    c_lowering_test_node(&fixture, "plain_dtor", inline_only | BUSTER_C_LOWERING_DESTRUCTOR, root | BUSTER_C_LOWERING_DESTRUCTOR, 2);
    c_lowering_test_node(&fixture, "extern_dtor_dependency", function, function, 1);
    c_lowering_test_node(&fixture, "extern_dtor", root | BUSTER_C_LOWERING_DESTRUCTOR, inline_only | BUSTER_C_LOWERING_DESTRUCTOR, 1);
    for (uint32_t i = 1; i < fixture.count; i += 2)
    {
        c_lowering_test_edge(&fixture, i, i - 1, C_LOWERING_REFERENCE_EVALUATED);
    }
    c_lowering_test_fixture(test, &fixture, arena);

    // Semantic identifier use is broader than an emitted dependency. Clang folds this const object's value even
    // at -O0, and never emits the dead call. The hand-authored graph consequently contains neither dependency.
    fixture = (CLoweringTestFixture){
        .name = "frontend-folded-references",
        .source = "static const int folded_value = 17;\n"
                  "static int folded_call(void) { return 2; }\n"
                  "int folded_root(void) { if (0) return folded_call(); return folded_value; }\n",
    };
    c_lowering_test_node(&fixture, "folded_value", object, object, 0);
    c_lowering_test_node(&fixture, "folded_call", function, function, 0);
    c_lowering_test_node(&fixture, "folded_root", root, root, 3);
    c_lowering_test_fixture(test, &fixture, arena);
}

static int run_c_lowering_tests(int argc, char** argv)
{
    CLoweringTest test = {
        .state = 1,
    };
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            test.state = strtoull(argv[++i], &end, 0);
            valid = argv[i][0] != '-' && end != argv[i] && *end == 0 && errno == 0;
        }
        else if (strcmp(argv[i], "--clang") == 0)
        {
            const bool path = i + 1 < argc && argv[i + 1][0] != '-';
            test.clang = path ? argv[++i] : "clang";
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            test.keep = true;
        }
        else
        {
            valid = false;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: test-lowering [--seed N] [--clang [PATH]] [--keep]\n");
    }
    Arena arena = arena_reserve(64ull << 20);
    bool ready = valid && arena.base != 0;
    if (ready && test.clang != 0)
    {
        const char* temporary = getenv("TMPDIR");
        temporary = temporary != 0 && temporary[0] != 0 ? temporary : "/tmp";
        const int length = snprintf(test.directory, sizeof(test.directory), "%s/buster-lowering-XXXXXX", temporary);
        ready = length > 0 && (uint64_t)length < sizeof(test.directory) && mkdtemp(test.directory) != 0;
    }
    c_lowering_test_check(&test, ready, "test setup failed");
    if (ready)
    {
        c_lowering_test_graphs(&test, &arena);
        c_lowering_test_deep(&test, &arena);
        c_lowering_test_limits(&test, &arena);
        c_lowering_test_guards(&test, &arena);
        c_lowering_test_fixtures(&test, &arena);
        if (test.clang != 0)
        {
            c_lowering_test_check(&test, test.clang_comparisons == test.fixtures * 4, "requested Clang did not complete every fixture/mode comparison");
        }
    }
    if (test.clang != 0 && test.directory[0] != 0)
    {
        if (test.keep || test.failures != 0)
        {
            printf("lowering: retained fixture files in %s\n", test.directory);
        }
        else
        {
            c_lowering_test_check(&test, rmdir(test.directory) == 0, "temporary fixture directory cleanup failed");
        }
    }
    arena_release(&arena);
    printf("lowering: %llu checks, %llu failures, %u original modeled fixtures, %u Clang object comparisons%s\n", (unsigned long long)test.checks, (unsigned long long)test.failures, test.fixtures, test.clang_comparisons, test.clang != 0 ? "" : " (Clang not requested)");
    return test.failures != 0;
}
