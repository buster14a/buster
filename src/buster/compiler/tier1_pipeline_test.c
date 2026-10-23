// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original typed-stream fixtures through the real Tier 1 pipeline. Include after tier0_generate.c, tier0_test.c,
// ir_lower.c, ssa_ir.c, ir_optimize.c and tier1_emit.c, with their dependencies. No alternate frontend or IR producer
// is used: tier0_emit appends the existing typed columns and tier0_program_functions supplies the actual ABI view.
// The existing typed interpreter supplies expected return bits independently of lowering, selection and allocation.
// Optional Clang -O0/-O2 oracles compile separately written C formulas, never the emitted machine or optimized IR.
//
// Defined integer straight-line programs and bounded direct local calls execute. Branches, joins, loops and
// floating point traverse the upstream stages and must stop explicitly before native execution. These fixtures do not qualify a
// source corpus. Native calls and external tools run in bounded child processes; all temporary files use TMPDIR.

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_TIER1_PIPELINE_CASES 8
#define BUSTER_TIER1_PIPELINE_CALL_CASES 2
#define BUSTER_TIER1_PIPELINE_FUNCTIONS 2
#define BUSTER_TIER1_PIPELINE_INPUTS 32
#define BUSTER_TIER1_PIPELINE_ENTRIES 512
#define BUSTER_TIER1_PIPELINE_ARENAS 8

typedef enum Tier1PipelineArena
{
    TIER1_PIPELINE_ABI,
    TIER1_PIPELINE_LOWERED,
    TIER1_PIPELINE_PROMOTED,
    TIER1_PIPELINE_OPTIMIZED,
    TIER1_PIPELINE_DERIVED,
    TIER1_PIPELINE_SELECTED,
    TIER1_PIPELINE_EMITTED,
    TIER1_PIPELINE_SCRATCH,
} Tier1PipelineArena;

typedef struct Tier1PipelineFixture Tier1PipelineFixture;
struct Tier1PipelineFixture
{
    Tier0Program program;
    uint8_t kinds[BUSTER_TIER1_PIPELINE_ENTRIES];
    uint8_t classes[BUSTER_TIER1_PIPELINE_ENTRIES];
    uint64_t values[BUSTER_TIER1_PIPELINE_ENTRIES];
    uint8_t local_classes[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint8_t local_flags[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    // Tier0Program's objects' columns, all 0: the fixture's locals are values.
    uint32_t local_sizes[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint8_t local_alignments[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint32_t call_callees[1];
    uint32_t call_first[1];
    uint32_t call_argument_counts[1];
    uint8_t call_indirect[1];
    uint8_t argument_classes[3];
};

typedef struct Tier1PipelineTest Tier1PipelineTest;
struct Tier1PipelineTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t seed;
    uint64_t stores;
    uint64_t loads;
    uint64_t rematerializations;
    uint64_t call_stores;
    uint64_t call_loads;
    uint32_t native_results;
    uint32_t clang_results;
    uint32_t resolved_calls;
    uint32_t refused;
    uint32_t fixture;
    bool vector;
    bool rematerialize;
    Arena arenas[BUSTER_TIER1_PIPELINE_ARENAS];
    Tier0Tables tables;
    uint8_t* allowed;
    uint64_t inputs[BUSTER_TIER1_PIPELINE_INPUTS][3];
    uint64_t expected[BUSTER_TIER1_PIPELINE_CASES][BUSTER_TIER1_PIPELINE_INPUTS];
};

static bool tier1_pipeline_expect(Tier1PipelineTest* test, bool passed, const char* message)
{
    test->checks += 1;
    if (!passed)
    {
        if (test->failures < 20)
        {
            fprintf(stderr, "tier1-pipeline: %s (fixture=%u, vector=%u, remat=%u, seed=%" PRIu64 ")\n", message, test->fixture, test->vector, test->rematerialize, test->seed);
        }
        test->failures += 1;
    }
    return passed;
}

static void tier1_pipeline_reset(Tier1PipelineTest* test)
{
    for (uint32_t a = 0; a < BUSTER_TIER1_PIPELINE_ARENAS; a += 1)
    {
        arena_pop_to(&test->arenas[a], 0);
    }
}

static uint64_t tier1_pipeline_constant(uint32_t term)
{
    return UINT64_C(0x9e3779b97f4a7c15) + term * UINT64_C(0xd1b54a32d192ed03);
}

static void tier1_pipeline_fixture(Tier1PipelineFixture* fixture, uint32_t shape)
{
    memset(fixture, 0, sizeof(*fixture));
    Tier0Program* program = &fixture->program;
    *program = (Tier0Program){
        .kinds = fixture->kinds,
        .classes = fixture->classes,
        .values = fixture->values,
        .entry_capacity = BUSTER_TIER1_PIPELINE_ENTRIES,
        .local_classes = fixture->local_classes,
        .local_flags = fixture->local_flags,
        .local_sizes = fixture->local_sizes,
        .local_alignments = fixture->local_alignments,
        .local_count = 4,
        .local_capacity = BUSTER_TIER0_GENERATE_MAX_LOCALS,
        .call_callees = fixture->call_callees,
        .call_first = fixture->call_first,
        .call_argument_counts = fixture->call_argument_counts,
        .call_indirect = fixture->call_indirect,
        .call_capacity = 1,
        .argument_classes = fixture->argument_classes,
        .argument_capacity = BUSTER_ARRAY_LENGTH(fixture->argument_classes),
        .function_count = 1,
        .defined = true,
    };
    memset(fixture->local_classes, BUSTER_TYPED_CLASS_U64, sizeof(fixture->local_classes));
    uint32_t entry_first = 0;
    uint32_t entry_local_first = 0;
    if (shape == 0)
    {
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_NOT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 3);
        tier0_emit(program, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
    }
    else if (shape == 1 || shape == 2)
    {
        const uint32_t terms = shape == 1 ? 12 : 16;
        program->local_count = 3 + terms;
        // All definitions precede all consuming loads. Promotion must preserve these long live ranges.
        for (uint32_t term = 0; term < terms; term += 1)
        {
            if (shape == 1)
            {
                tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            }
            tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, tier1_pipeline_constant(term));
            if (shape == 1)
            {
                tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
                tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
                tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, term % 5 + 1);
                tier0_emit(program, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
                tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
            }
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 3 + term);
        }
        if (shape == 2)
        {
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
            tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        }
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        if (shape == 2)
        {
            tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        }
        for (uint32_t term = 0; term < terms; term += 1)
        {
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3 + term);
            tier0_emit(program, shape == 1 || term % 2 == 0 ? BUSTER_TYPED_ADD : BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        }
    }
    else if (shape == 3)
    {
        fixture->local_classes[3] = BUSTER_TYPED_CLASS_U32;
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U32, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U32, 3);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U32, 3);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 11);
        tier0_emit(program, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U32, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U32, 3);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, UINT32_C(0x92d68ca7));
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U32, 0);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U32, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
    }
    else if (shape == 4)
    {
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_U64, 4);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
    }
    else if (shape == 5)
    {
        // The original entry is a backedge target. Lowering must supply its own ABI initialization preheader.
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
    }
    else if (shape == 6)
    {
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_U64, 5);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 17);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        tier0_emit(program, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, 7);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 29);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
    }
    else if (shape == 7)
    {
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 7);
        tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        program->function_count = 2;
        program->functions[0] = (Tier0GeneratedFunction){
            .entry_count = 2,
            .result_class = BUSTER_TYPED_CLASS_U64,
        };
        program->call_count = 1;
        entry_first = program->entry_count;
        tier0_emit(program, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, 0);
    }
    else if (shape == 9)
    {
        // The helper receives three nonconstant arguments. Its local range is disjoint from the entry's.
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 3);
        tier0_emit(program, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        program->function_count = 2;
        program->functions[0] = (Tier0GeneratedFunction){
            .entry_count = program->entry_count,
            .local_count = 3,
            .parameter_count = 3,
            .result_class = BUSTER_TYPED_CLASS_U64,
        };
        entry_first = program->entry_count;
        entry_local_first = 3;
        program->local_count = entry_local_first + 4;
        program->call_count = 1;
        program->argument_count = 3;
        fixture->call_argument_counts[0] = 3;
        memset(fixture->argument_classes, BUSTER_TYPED_CLASS_U64, sizeof(fixture->argument_classes));
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        // Reordered c,a,b arguments exercise argument moves; c and a^b must also survive the call.
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
    }
    else if (shape == 10)
    {
        // The U64 butterfly and final U16 result follow cbuiltin_lower_unary's BSWAP16 recipe at cebc0bb.
        // This is a typed-stream fixture: the source producer is neither invoked nor replaced here.
        program->local_count = 7;
        fixture->local_classes[3] = BUSTER_TYPED_CLASS_U16;
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U16, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U16, 3);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U16, 3);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, UINT16_MAX);
        tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, 6);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
        const uint64_t masks[] = {
            UINT64_C(0x00ff00ff00ff00ff),
            UINT64_C(0x0000ffff0000ffff),
            UINT64_C(0x00000000ffffffff),
        };
        for (uint32_t stage = 0; stage < BUSTER_ARRAY_LENGTH(masks); stage += 1)
        {
            const uint32_t shift = 8u << stage;
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
            tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, shift);
            tier0_emit(program, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 5);
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 5);
            tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, masks[stage]);
            tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 5);
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
            tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, masks[stage]);
            tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
            tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, shift);
            tier0_emit(program, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 5);
            tier0_emit(program, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
        }
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 48);
        tier0_emit(program, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U16, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
    }
    else if (shape == 11)
    {
        program->local_count = 7;
        fixture->local_classes[3] = BUSTER_TYPED_CLASS_I8;
        fixture->local_classes[4] = BUSTER_TYPED_CLASS_I16;
        fixture->local_classes[5] = BUSTER_TYPED_CLASS_I16;
        fixture->local_classes[6] = BUSTER_TYPED_CLASS_I32;
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I8, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I8, 3);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I8, 3);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I16, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I16, 4);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I16, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I16, 5);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, 6);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I16, 4);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I16, 5);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 6);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I8, 3);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 17);
        tier0_emit(program, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
    }
    else
    {
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F64, 0);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, UINT64_C(0x4008000000000000));
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_F64, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    program->functions[program->function_count - 1] = (Tier0GeneratedFunction){
        .first_entry = entry_first,
        .entry_count = program->entry_count - entry_first,
        .first_local = entry_local_first,
        .local_count = program->local_count - entry_local_first,
        .parameter_count = 3,
        .call_count = program->call_count,
        .result_class = BUSTER_TYPED_CLASS_U64,
    };
}

static bool tier1_pipeline_unchanged(const Tier1PipelineFixture* fixture, const Tier1PipelineFixture* original)
{
    // The snapshot includes every owned source column and the borrowed pointers in its descriptor. It is never
    // executed: copied descriptor pointers deliberately still identify the original fixture's column addresses.
    return memcmp(fixture, original, sizeof(*fixture)) == 0;
}

static bool tier1_pipeline_freeze(Tier1PipelineTest* test, const Tier1PipelineFixture* fixture, Tier1PipelineFixture* original)
{
    memcpy(original, fixture, sizeof(*fixture));
    Tier0Functions functions;
    bool ok = tier1_pipeline_expect(test, !fixture->program.overflow && tier0_program_functions(&fixture->program, &test->tables, &test->arenas[TIER1_PIPELINE_ABI], &functions), "prepare original typed interpreter before compilation");
    if (ok)
    {
        const Tier0TestData data = tier0_test_layout(&fixture->program);
        uint8_t globals[1] = {0};
        for (uint32_t input = 0; input < BUSTER_TIER1_PIPELINE_INPUTS; input += 1)
        {
            bool fault = false;
            test->expected[test->fixture][input] = tier0_test_interpret(&fixture->program, &functions, &data, globals, test->inputs[input], &fault);
            ok = tier1_pipeline_expect(test, !fault, "freeze independent typed-interpreter bits before compilation") && ok;
        }
    }
    ok = tier1_pipeline_expect(test, tier1_pipeline_unchanged(fixture, original), "pre-compilation oracle preserves every original source column") && ok;
    return ok;
}

static void tier1_pipeline_mutation_regression(Tier1PipelineTest* test)
{
    test->fixture = 0;
    tier1_pipeline_reset(test);
    Tier1PipelineFixture fixture;
    Tier1PipelineFixture original;
    tier1_pipeline_fixture(&fixture, 0);
    if (tier1_pipeline_freeze(test, &fixture, &original))
    {
        const uint64_t expected = test->expected[0][0];
        // Simulate an incorrect borrowed-input rewrite: change b << 3 to b << 2 after freezing the oracle.
        fixture.values[4] = 2;
        tier1_pipeline_expect(test, !tier1_pipeline_unchanged(&fixture, &original), "a deliberate borrowed typed-column mutation is detected");
        Tier0Functions functions;
        const bool ready = tier0_program_functions(&fixture.program, &test->tables, &test->arenas[TIER1_PIPELINE_ABI], &functions);
        tier1_pipeline_expect(test, ready, "mutation regression retains a valid typed signature");
        if (ready)
        {
            const Tier0TestData data = tier0_test_layout(&fixture.program);
            uint8_t globals[1] = {0};
            bool fault = false;
            const uint64_t changed = tier0_test_interpret(&fixture.program, &functions, &data, globals, test->inputs[0], &fault);
            tier1_pipeline_expect(test, !fault && changed != expected && test->expected[0][0] == expected, "a post-compilation reinterpretation cannot redefine the frozen oracle");
        }
        fixture.values[4] = original.values[4];
        tier1_pipeline_expect(test, tier1_pipeline_unchanged(&fixture, &original), "restoring the source restores the exact fixture snapshot");
    }
}

static bool tier1_pipeline_upstream(Tier1PipelineTest* test, const Tier0Program* program, uint32_t function_index, Tier0Functions* functions, IrFunction* optimized, IrDominance* dominance)
{
    Arena* scratch = &test->arenas[TIER1_PIPELINE_SCRATCH];
    bool ok = tier1_pipeline_expect(test, !program->overflow && tier0_program_functions(program, &test->tables, &test->arenas[TIER1_PIPELINE_ABI], functions), "typed producer and ABI batch succeed");
    IrFunction lowered;
    IrFunction promoted;
    if (ok)
    {
        const Tier0Function* entry = &functions->functions[function_index];
        const uint32_t symbols = program->global_count + program->function_count + 1;
        const IrCheck check = test->vector ? ir_lower_typed(entry, IR_I64, symbols, &test->arenas[TIER1_PIPELINE_LOWERED], scratch, &lowered) : ir_lower_typed_scalar(entry, IR_I64, symbols, &test->arenas[TIER1_PIPELINE_LOWERED], scratch, &lowered);
        ok = tier1_pipeline_expect(test, check.error == IR_OK, "real typed-stream lowering succeeds");
        if (!ok)
        {
            fprintf(stderr, "  lowering error=%u instruction=%u detail=%u\n", check.error, check.instruction, check.detail);
        }
    }
    if (ok)
    {
        const IrCheck check = test->vector ? ssa_ir_promote(&lowered, &test->arenas[TIER1_PIPELINE_PROMOTED], scratch, &promoted) : ssa_ir_promote_scalar(&lowered, &test->arenas[TIER1_PIPELINE_PROMOTED], scratch, &promoted);
        ok = tier1_pipeline_expect(test, check.error == IR_OK, "real shared-IR local promotion succeeds");
        if (ok)
        {
            tier1_pipeline_expect(test, (program->functions[function_index].local_count == 0 || lowered.slot_count != 0) && promoted.slot_count == 0, "promotion eliminates the private parameter and local slots");
        }
    }
    if (ok)
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.scalar = !test->vector;
        IrOptimizeResult result;
        const IrCheck check = ir_optimize(&promoted, &test->arenas[TIER1_PIPELINE_OPTIMIZED], scratch, options, &result);
        ok = tier1_pipeline_expect(test, check.error == IR_OK, "real SCCP, DCE and CFG optimization succeeds");
        if (ok)
        {
            *optimized = result.function;
        }
    }
    IrCfg cfg;
    if (ok)
    {
        ok = tier1_pipeline_expect(test, ir_build_cfg(optimized, &test->arenas[TIER1_PIPELINE_DERIVED], scratch, &cfg) == IR_OK, "rebuild the optimized CFG");
    }
    if (ok)
    {
        const DominatorCfg graph = {
            .block_count = cfg.block_count,
            .edge_count = cfg.edge_count,
            .entry = 0,
            .successor_offsets = cfg.successor_offsets,
            .successors = cfg.successors,
            .predecessor_offsets = cfg.predecessor_offsets,
            .predecessors = cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.path = test->vector ? DOMINATOR_VECTOR : DOMINATOR_SCALAR;
        options.outputs = 0;
        Dominators result;
        ok = tier1_pipeline_expect(test, dominators_analyze(&test->arenas[TIER1_PIPELINE_DERIVED], scratch, &graph, options, &result) == DOMINATOR_OK, "rebuild dominance after optimization");
        if (ok)
        {
            *dominance = (IrDominance){
                .enter = result.preorder,
                .leave = result.subtree_end,
                .block_count = result.block_count,
            };
            const IrCheck vector = ir_verify(optimized, scratch, dominance);
            const IrCheck scalar = ir_verify_scalar(optimized, scratch, dominance);
            ok = tier1_pipeline_expect(test, vector.error == IR_OK && scalar.error == IR_OK && vector.dominance_checked && scalar.dominance_checked, "both IR verifiers accept the optimized dominance handoff");
        }
    }
    tier1_pipeline_expect(test, scratch->length == 0, "upstream stages return scratch");
    return ok;
}

static bool tier1_pipeline_narrow_handoff(Tier1PipelineTest* test, uint32_t shape, const IrFunction* function)
{
    uint32_t trunc8 = 0;
    uint32_t trunc16 = 0;
    uint32_t trunc32 = 0;
    uint32_t zext16_64 = 0;
    uint32_t sext8_16 = 0;
    uint32_t sext8_64 = 0;
    uint32_t sext16_64 = 0;
    uint32_t sext32_64 = 0;
    for (uint32_t i = 0; i < function->instruction_count; i += 1)
    {
        const uint8_t op = function->op[i];
        if (op == IR_TRUNC || op == IR_ZEXT || op == IR_SEXT)
        {
            const uint8_t from = function->type[function->a[i]];
            const uint8_t to = function->type[i];
            trunc8 += op == IR_TRUNC && from == IR_I64 && to == IR_I8;
            trunc16 += op == IR_TRUNC && from == IR_I64 && to == IR_I16;
            trunc32 += op == IR_TRUNC && from == IR_I64 && to == IR_I32;
            zext16_64 += op == IR_ZEXT && from == IR_I16 && to == IR_I64;
            sext8_16 += op == IR_SEXT && from == IR_I8 && to == IR_I16;
            sext8_64 += op == IR_SEXT && from == IR_I8 && to == IR_I64;
            sext16_64 += op == IR_SEXT && from == IR_I16 && to == IR_I64;
            sext32_64 += op == IR_SEXT && from == IR_I32 && to == IR_I64;
        }
    }
    bool ok = tier1_pipeline_expect(test, function->result_type == IR_I64 && function->parameter_count == 3 && function->parameter_types[0] == IR_I64 && function->parameter_types[1] == IR_I64 && function->parameter_types[2] == IR_I64, "narrow primitive fixtures retain only U64 parameters and return ABI");
    if (shape == 10)
    {
        ok = tier1_pipeline_expect(test, trunc16 >= 2 && zext16_64 >= 2, "nonconstant BSWAP16 input and result narrowing/widening reach selection") && ok;
    }
    else
    {
        ok = tier1_pipeline_expect(test, trunc8 != 0 && trunc16 != 0 && trunc32 != 0 && sext8_16 != 0 && sext8_64 != 0 && sext16_64 >= 2 && sext32_64 != 0, "nonconstant signed narrow locals and intermediate widening reach selection") && ok;
    }
    return ok;
}

static bool tier1_pipeline_wait(pid_t child)
{
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        }
        while (waited < 0 && errno == EINTR);
    }
    return waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

typedef struct Tier1PipelineProcess Tier1PipelineProcess;
struct Tier1PipelineProcess
{
    bool supervised;
    bool finished;
    bool timed_out;
    bool cleanup_complete;
    int exit_code;
    int signal_number;
    uint32_t reaped;
    // Only the deliberate timeout regression writes these two known descendant identities.
    pid_t descendant;
    pid_t detached;
};

static volatile sig_atomic_t tier1_pipeline_stop;

static void tier1_pipeline_signal(int number)
{
    tier1_pipeline_stop = number;
}

static uint64_t tier1_pipeline_milliseconds(void)
{
    struct timespec time;
    const bool ok = clock_gettime(CLOCK_MONOTONIC, &time) == 0;
    return ok ? (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000 : 0;
}

// Called only by a private supervisor, never by the compiler parent. The group leader remains unreaped until this
// kill, preventing PID reuse. The subreaper also owns orphan descendants that changed groups with setsid/setpgid.
static bool tier1_pipeline_reap(pid_t command, Tier1PipelineProcess* result)
{
    kill(-command, SIGKILL);
    kill(command, SIGKILL);
    const uint64_t deadline = tier1_pipeline_milliseconds() + 2000;
    bool complete = false;
    for (uint32_t attempt = 0; !complete && attempt < 400; attempt += 1)
    {
        int status = 0;
        pid_t child = waitpid(-1, &status, WNOHANG);
        while (child > 0)
        {
            result->reaped += 1;
            child = waitpid(-1, &status, WNOHANG);
        }
        complete = child < 0 && errno == ECHILD;
        if (!complete)
        {
            // This procfs file lists only this single-threaded supervisor's direct children. Killing an adopted
            // parent exposes its surviving descendants on the next pass, without inspecting unrelated processes.
            char path[128];
            const int length = snprintf(path, sizeof(path), "/proc/self/task/%ld/children", (long)getpid());
            FILE* children = length > 0 && (uint64_t)length < sizeof(path) ? fopen(path, "r") : 0;
            if (children != 0)
            {
                long adopted = 0;
                while (fscanf(children, "%ld", &adopted) == 1)
                {
                    if (adopted > 0 && adopted <= INT_MAX)
                    {
                        kill((pid_t)adopted, SIGKILL);
                    }
                }
                fclose(children);
            }
            const uint64_t now = tier1_pipeline_milliseconds();
            if (now == 0 || now >= deadline)
            {
                break;
            }
            poll(0, 0, 5);
        }
    }
    return complete;
}

static void tier1_pipeline_hang_tree(Tier1PipelineProcess* result)
{
    const pid_t descendant = fork();
    if (descendant < 0)
    {
        _exit(125);
    }
    if (descendant == 0)
    {
        result->descendant = getpid();
        const pid_t detached = fork();
        if (detached < 0)
        {
            _exit(125);
        }
        if (detached == 0)
        {
            if (setsid() < 0)
            {
                _exit(125);
            }
            result->detached = getpid();
        }
    }
    for (;;)
    {
        pause();
    }
}

static void tier1_pipeline_supervise(pid_t owner, char* const* arguments, const char* output, uint32_t timeout_ms, Tier1PipelineProcess* result)
{
    const uint64_t start = tier1_pipeline_milliseconds();
    struct sigaction action = {0};
    action.sa_handler = tier1_pipeline_signal;
    sigemptyset(&action.sa_mask);
    struct sigaction normal = {0};
    normal.sa_handler = SIG_DFL;
    sigemptyset(&normal.sa_mask);
    sigset_t empty;
    sigemptyset(&empty);
    tier1_pipeline_stop = 0;
    bool valid = start != 0 && prctl(PR_SET_CHILD_SUBREAPER, 1) == 0 && sigaction(SIGCHLD, &normal, 0) == 0;
    valid = valid && sigaction(SIGTERM, &action, 0) == 0 && sigaction(SIGINT, &action, 0) == 0 && sigaction(SIGHUP, &action, 0) == 0;
    valid = valid && sigprocmask(SIG_SETMASK, &empty, 0) == 0 && prctl(PR_SET_PDEATHSIG, SIGTERM) == 0 && getppid() == owner;
    const pid_t command = valid ? fork() : -1;
    if (command == 0)
    {
        bool ready = setpgid(0, 0) == 0 && sigaction(SIGTERM, &normal, 0) == 0 && sigaction(SIGINT, &normal, 0) == 0 && sigaction(SIGHUP, &normal, 0) == 0;
        if (ready && output != 0)
        {
            const int fd = open(output, O_CREAT | O_TRUNC | O_WRONLY, 0600);
            ready = fd >= 0 && dup2(fd, STDOUT_FILENO) >= 0;
            if (fd >= 0 && fd != STDOUT_FILENO)
            {
                close(fd);
            }
        }
        if (ready && arguments == 0)
        {
            tier1_pipeline_hang_tree(result);
        }
        if (ready)
        {
            execvp(arguments[0], arguments);
        }
        _exit(127);
    }
    valid = valid && command > 0;
    while (valid && !result->finished && !result->timed_out && tier1_pipeline_stop == 0)
    {
        siginfo_t information = {0};
        const int waited = waitid(P_PID, (id_t)command, &information, WEXITED | WNOHANG | WNOWAIT);
        if (waited == 0 && information.si_pid == command)
        {
            result->finished = true;
            result->exit_code = information.si_code == CLD_EXITED ? information.si_status : 0;
            result->signal_number = information.si_code == CLD_EXITED ? 0 : information.si_status;
        }
        else if (waited < 0 && errno != EINTR)
        {
            valid = false;
        }
        const uint64_t now = tier1_pipeline_milliseconds();
        valid = valid && now != 0;
        result->timed_out = !result->finished && now >= start && now - start >= timeout_ms;
        if (valid && !result->finished && !result->timed_out)
        {
            poll(0, 0, 5);
        }
    }
    result->supervised = valid && tier1_pipeline_stop == 0;
    if (command > 0)
    {
        // Clean up even after a successful driver exit: a child may still own output files or run in the background.
        result->cleanup_complete = tier1_pipeline_reap(command, result);
    }
}

static Tier1PipelineProcess tier1_pipeline_process(char* const* arguments, const char* output, uint32_t timeout_ms)
{
    Tier1PipelineProcess result = {0};
    Tier1PipelineProcess* shared = mmap(0, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared != MAP_FAILED)
    {
        fflush(0);
        const pid_t owner = getpid();
        const pid_t supervisor = fork();
        if (supervisor == 0)
        {
            tier1_pipeline_supervise(owner, arguments, output, timeout_ms, shared);
            _exit(0);
        }
        if (tier1_pipeline_wait(supervisor))
        {
            memcpy(&result, shared, sizeof(result));
        }
        munmap(shared, sizeof(*shared));
    }
    return result;
}

static bool tier1_pipeline_process_ok(const Tier1PipelineProcess* result)
{
    return result->supervised && result->finished && !result->timed_out && result->cleanup_complete && result->signal_number == 0 && result->exit_code == 0;
}

static void tier1_pipeline_deadline_regression(Tier1PipelineTest* test)
{
    const uint64_t start = tier1_pipeline_milliseconds();
    const Tier1PipelineProcess result = tier1_pipeline_process(0, 0, 200);
    const uint64_t finish = tier1_pipeline_milliseconds();
    tier1_pipeline_expect(test, result.supervised && result.timed_out && !result.finished, "parent deadline stops an intentionally hanging command tree");
    tier1_pipeline_expect(test, result.cleanup_complete && result.reaped == 3, "private subreaper collects the command, child and detached grandchild");
    tier1_pipeline_expect(test, result.descendant > 0 && kill(result.descendant, 0) < 0 && errno == ESRCH, "timed-out child no longer exists");
    tier1_pipeline_expect(test, result.detached > 0 && kill(result.detached, 0) < 0 && errno == ESRCH, "setsid grandchild is also killed and reaped");
    tier1_pipeline_expect(test, start != 0 && finish >= start && finish - start < 5000, "deadline and descendant cleanup remain bounded");
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_pipeline_call(const void* code, const uint64_t* inputs)
{
    uint64_t (*function)(uint64_t, uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(inputs[0], inputs[1], inputs[2]);
}

static void tier1_pipeline_execute(Tier1PipelineTest* test, const Tier0Program* program, Tier1EmitResult* emitted)
{
    uint64_t capacity = 0;
    uint64_t starts[BUSTER_TIER1_PIPELINE_FUNCTIONS] = {0};
    uint64_t capacities[BUSTER_TIER1_PIPELINE_FUNCTIONS] = {0};
    uint32_t maximum_records = 0;
    uint32_t maximum_references = 0;
    bool ok = tier1_pipeline_expect(test, program->global_count == 0 && program->function_count != 0 && program->function_count <= BUSTER_TIER1_PIPELINE_FUNCTIONS, "native fixture contains only its bounded original local functions");
    for (uint32_t f = 0; ok && f < program->function_count; f += 1)
    {
        starts[f] = (capacity + 15) & ~UINT64_C(15);
        capacities[f] = BUSTER_X86_ENCODER_CAPACITY(emitted[f].code.count);
        capacity = starts[f] + capacities[f];
        maximum_records = emitted[f].code.count > maximum_records ? emitted[f].code.count : maximum_records;
        maximum_references = emitted[f].reference_count > maximum_references ? emitted[f].reference_count : maximum_references;
    }
    const uint64_t mapped = (capacity + 4095) & ~UINT64_C(4095);
    uint8_t* machine = mmap(0, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* scalar = malloc(capacity);
    uint32_t* offsets = malloc(((uint64_t)maximum_records + 1) * sizeof(uint32_t));
    uint32_t* ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(maximum_references) * sizeof(uint32_t));
    uint32_t* scalar_ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(maximum_references) * sizeof(uint32_t));
    uint64_t* actual = mmap(0, sizeof(test->expected[0]), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ok = tier1_pipeline_expect(test, capacity < INT32_MAX && machine != MAP_FAILED && scalar != 0 && offsets != 0 && ends != 0 && scalar_ends != 0 && actual != MAP_FAILED, "allocate native encoder and symbolic-fixup buffers") && ok;
    for (uint32_t f = 0; ok && f < program->function_count; f += 1)
    {
        Tier1SelectCode* code = &emitted[f].code;
        const BranchSizingCheck checked = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        ok = tier1_pipeline_expect(test, checked.error == BRANCH_SIZING_OK, "frame and body branches validate together");
        if (ok)
        {
            const BranchSizing sized = size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets);
            ok = tier1_pipeline_expect(test, sized.error == BRANCH_SIZING_OK && x86_target_validate(test->allowed, code->records, code->count) == code->count, "complete records obey the baseline target and branch layout");
        }
        uint32_t marked = 0;
        for (uint32_t record = 0; ok && record < code->count; record += 1)
        {
            if ((code->records[record].form & BUSTER_X86_FORM_FIXUP) != 0)
            {
                const bool present = marked < emitted[f].reference_count;
                ok = tier1_pipeline_expect(test, present && emitted[f].references[marked].instruction == record && code->records[record].form == (X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP), "each marked call record has exactly one ordered symbolic reference");
                marked += 1;
            }
        }
        ok = tier1_pipeline_expect(test, marked == emitted[f].reference_count, "symbolic reference count equals the encoded marked-record count") && ok;
        int64_t vector_size = -1;
        if (ok)
        {
            vector_size = x86_encode(code->records, code->count, machine + starts[f], capacities[f], 0, ends);
            const int64_t scalar_size = x86_encode_scalar(code->records, code->count, scalar + starts[f], capacities[f], 0, scalar_ends);
            ok = tier1_pipeline_expect(test, vector_size > 0 && (uint64_t)vector_size <= capacities[f] && vector_size == scalar_size && memcmp(machine + starts[f], scalar + starts[f], (uint64_t)vector_size) == 0 && memcmp(ends, scalar_ends, (uint64_t)marked * sizeof(uint32_t)) == 0, "scalar and vector encoders agree on bytes and symbolic field ends");
        }
        for (uint32_t reference = 0; ok && reference < marked; reference += 1)
        {
            const LinkReference* fixup = &emitted[f].references[reference];
            const uint32_t end = ends[reference];
            const uint32_t original = program->functions[f].first_call + reference;
            ok = tier1_pipeline_expect(test, fixup->kind == BUSTER_LINK_FIXUP_CALL && fixup->field == X86_FIXUP_DISPLACEMENT && fixup->addend == 0 && fixup->target != 0 && fixup->target <= program->function_count && reference < program->functions[f].call_count && original < program->call_count && fixup->target == program->call_callees[original] + 1, "direct call keeps the original typed stream's local function symbol");
            if (ok)
            {
                const uint32_t distance = x86_fixup_distance(code->records[fixup->instruction].form, fixup->field);
                const int64_t displacement = (int64_t)starts[fixup->target - 1] - (int64_t)(starts[f] + end);
                ok = tier1_pipeline_expect(test, distance == sizeof(int32_t) && end >= distance && end <= (uint64_t)vector_size && displacement >= INT32_MIN && displacement <= INT32_MAX, "local call has a bounded rel32 field and an in-range function target");
                if (ok)
                {
                    const int32_t relative = (int32_t)displacement;
                    memcpy(machine + starts[f] + end - distance, &relative, sizeof(relative));
                    memcpy(scalar + starts[f] + end - distance, &relative, sizeof(relative));
                    test->resolved_calls += 1;
                }
            }
        }
    }
    if (ok)
    {
        ok = tier1_pipeline_expect(test, mprotect(machine, mapped, PROT_READ | PROT_EXEC) == 0, "make generated code executable");
    }
    if (ok)
    {
        fflush(0);
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(10);
            for (uint32_t input = 0; input < BUSTER_TIER1_PIPELINE_INPUTS; input += 1)
            {
                actual[input] = tier1_pipeline_call(machine + starts[program->function_count - 1], test->inputs[input]);
            }
            _exit(0);
        }
        ok = tier1_pipeline_expect(test, tier1_pipeline_wait(child), "generated native code exits normally within its budget");
    }
    for (uint32_t input = 0; ok && input < BUSTER_TIER1_PIPELINE_INPUTS; input += 1)
    {
        const bool equal = actual[input] == test->expected[test->fixture][input];
        tier1_pipeline_expect(test, equal, "native return bits equal the independent typed interpreter");
        if (!equal && test->failures < 20)
        {
            fprintf(stderr, "  input=%u a=%016" PRIx64 " b=%016" PRIx64 " c=%016" PRIx64 " expected=%016" PRIx64 " actual=%016" PRIx64 "\n", input, test->inputs[input][0], test->inputs[input][1], test->inputs[input][2], test->expected[test->fixture][input], actual[input]);
        }
        test->native_results += 1;
    }
    if (machine != MAP_FAILED)
    {
        munmap(machine, mapped);
    }
    if (actual != MAP_FAILED)
    {
        munmap(actual, sizeof(test->expected[0]));
    }
    free(scalar);
    free(offsets);
    free(ends);
    free(scalar_ends);
}

static void tier1_pipeline_supported(Tier1PipelineTest* test)
{
    const uint32_t shapes[BUSTER_TIER1_PIPELINE_CASES] = {
        0,
        1,
        2,
        3,
        7,
        9,
        10,
        11,
    };
    for (uint32_t fixture_index = 0; fixture_index < BUSTER_TIER1_PIPELINE_CASES; fixture_index += 1)
    {
        const uint32_t shape = shapes[fixture_index];
        test->fixture = fixture_index;
        Tier1PipelineFixture fixture;
        Tier1PipelineFixture original;
        tier1_pipeline_fixture(&fixture, shape);
        tier1_pipeline_reset(test);
        const bool frozen = tier1_pipeline_freeze(test, &fixture, &original);
        for (uint32_t path = 0; frozen && path < 2; path += 1)
        {
            test->vector = path != 0;
            for (uint32_t remat = 0; remat < 2; remat += 1)
            {
                test->rematerialize = remat != 0;
                tier1_pipeline_reset(test);
                const Tier1EmitOptions options = {
                    .register_mask = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI),
                    .frame_flags = remat != 0 ? BUSTER_TIER1_FRAME_KEEP_POINTER | BUSTER_TIER1_FRAME_NO_RED_ZONE : 0,
                    .vector = test->vector,
                    .rematerialize = test->rematerialize,
                };
                Tier1EmitResult bodies[BUSTER_TIER1_PIPELINE_FUNCTIONS];
                bool ok = tier1_pipeline_expect(test, fixture.program.function_count != 0 && fixture.program.function_count <= BUSTER_TIER1_PIPELINE_FUNCTIONS, "supported fixture has a bounded function table");
                for (uint32_t f = 0; ok && f < fixture.program.function_count; f += 1)
                {
                    // Every helper uses the same real typed-to-native stages; its bytes are kept beside the entry.
                    Tier0Functions functions;
                    IrFunction function;
                    IrDominance dominance;
                    ok = tier1_pipeline_upstream(test, &fixture.program, f, &functions, &function, &dominance);
                    ok = tier1_pipeline_expect(test, tier1_pipeline_unchanged(&fixture, &original), "lowering, promotion and optimization preserve the frozen typed fixture") && ok;
                    if (ok && (shape == 10 || shape == 11))
                    {
                        ok = tier1_pipeline_narrow_handoff(test, shape, &function);
                    }
                    Tier1SelectPlan plan;
                    uint32_t node = BUSTER_IR_NONE;
                    if (ok)
                    {
                        const Tier1SelectError error = tier1_select_plan(&function, &dominance, &test->arenas[TIER1_PIPELINE_SELECTED], &test->arenas[TIER1_PIPELINE_SCRATCH], test->vector, &plan, &node);
                        ok = tier1_pipeline_expect(test, error == TIER1_SELECT_OK, "select the actual optimized SSA function");
                        if (!ok)
                        {
                            fprintf(stderr, "  selection error=%u node=%u function=%u\n", error, node, f);
                        }
                    }
                    Tier1EmitResult* emitted = &bodies[f];
                    if (ok)
                    {
                        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->arenas[TIER1_PIPELINE_EMITTED], &test->arenas[TIER1_PIPELINE_SCRATCH], emitted, &node);
                        ok = tier1_pipeline_expect(test, error == TIER1_EMIT_OK, "real liveness, fragment allocation and frame emission succeed");
                        if (!ok)
                        {
                            fprintf(stderr, "  emission error=%u node=%u function=%u\n", error, node, f);
                        }
                    }
                    tier1_pipeline_expect(test, test->arenas[TIER1_PIPELINE_SCRATCH].length == 0, "fragment emission returns scratch");
                    if (ok)
                    {
                        tier1_pipeline_expect(test, tier1_allocation_verify(&emitted->prepared.allocation, &emitted->allocation), "independent allocation verifier accepts actual fragments");
                        const uint32_t call_slots = (uint32_t)__builtin_popcount(emitted->prepared.call_save_mask);
                        tier1_pipeline_expect(test, emitted->frame.count == emitted->allocation.spill_count + call_slots, "spill values and reserved caller-save values have complete frame rows");
                        tier1_pipeline_expect(test, emitted->reference_count == fixture.program.functions[f].call_count, "every original direct call survives optimization and reaches a symbolic native call");
                        if (emitted->reference_count != 0)
                        {
                            tier1_pipeline_expect(test, !emitted->frame.red_zone && emitted->call_stores == emitted->call_loads, "callers use a real frame and balance caller-save stores and reloads");
                        }
                        if (shape == 1 || (shape == 2 && !test->rematerialize))
                        {
                            tier1_pipeline_expect(test, emitted->spill_stores != 0 && emitted->spill_loads != 0 && emitted->frame.count != 0, "pressure reaches real spill stores, frame slots and reloads");
                        }
                        if (shape == 2 && test->rematerialize)
                        {
                            tier1_pipeline_expect(test, emitted->rematerializations != 0, "wide constant pressure reaches real rematerialization");
                        }
                        if (shape == 9 && f + 1 == fixture.program.function_count)
                        {
                            tier1_pipeline_expect(test, emitted->call_stores != 0 && emitted->call_loads != 0, "nonconstant values live across a direct call reach caller-save stores and reloads");
                        }
                        test->stores += emitted->spill_stores;
                        test->loads += emitted->spill_loads;
                        test->rematerializations += emitted->rematerializations;
                        test->call_stores += emitted->call_stores;
                        test->call_loads += emitted->call_loads;
                    }
                }
                if (ok)
                {
                    tier1_pipeline_execute(test, &fixture.program, bodies);
                }
            }
            tier1_pipeline_expect(test, tier1_pipeline_unchanged(&fixture, &original), "selection and native compilation preserve the frozen typed fixture");
        }
    }
}

static void tier1_pipeline_refusals(Tier1PipelineTest* test)
{
    const uint32_t shapes[] = {
        4,
        5,
        6,
        8,
    };
    for (uint32_t fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(shapes); fixture_index += 1)
    {
        const uint32_t shape = shapes[fixture_index];
        test->fixture = shape;
        Tier1PipelineFixture fixture;
        Tier1PipelineFixture original;
        tier1_pipeline_fixture(&fixture, shape);
        memcpy(&original, &fixture, sizeof(fixture));
        for (uint32_t path = 0; path < 2; path += 1)
        {
            test->vector = path != 0;
            test->rematerialize = false;
            tier1_pipeline_reset(test);
            Tier0Functions functions;
            IrFunction function;
            IrDominance dominance;
            const bool ready = tier1_pipeline_upstream(test, &fixture.program, fixture.program.function_count - 1, &functions, &function, &dominance);
            if (ready)
            {
                Arena* selected = &test->arenas[TIER1_PIPELINE_SELECTED];
                Arena* scratch = &test->arenas[TIER1_PIPELINE_SCRATCH];
                Tier1SelectPlan plan;
                memset(&plan, 0xa5, sizeof(plan));
                Tier1SelectPlan prior;
                memcpy(&prior, &plan, sizeof(plan));
                uint32_t node = BUSTER_IR_NONE;
                const Tier1SelectError selection = tier1_select_plan(&function, &dominance, selected, scratch, test->vector, &plan, &node);
                // Simple branches can be selected, but cannot yet reconcile allocation locations along edges.
                if (shape == 4 || shape == 5)
                {
                    const bool ok = tier1_pipeline_expect(test, selection == TIER1_SELECT_OK, "valid unsupported CFG reaches the machine consumer");
                    if (ok)
                    {
                        const Tier1EmitOptions options = {
                            .register_mask = tier1_liveness_register_mask(),
                            .vector = test->vector,
                        };
                        Tier1EmitResult emitted;
                        memset(&emitted, 0x5a, sizeof(emitted));
                        Tier1EmitResult before;
                        memcpy(&before, &emitted, sizeof(emitted));
                        Arena* output = &test->arenas[TIER1_PIPELINE_EMITTED];
                        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, output, scratch, &emitted, &node);
                        const bool refused = tier1_pipeline_expect(test, error == TIER1_EMIT_UNSUPPORTED, "branch or backedge receives explicit unsupported status");
                        tier1_pipeline_expect(test, output->length == 0 && scratch->length == 0 && memcmp(&emitted, &before, sizeof(emitted)) == 0, "unsupported machine consumer rolls back arenas and result");
                        test->refused += refused;
                    }
                }
                else
                {
                    const bool refused = tier1_pipeline_expect(test, selection == TIER1_SELECT_ERROR_UNSUPPORTED, "phi or floating point receives explicit unsupported status");
                    tier1_pipeline_expect(test, selected->length == 0 && scratch->length == 0 && memcmp(&plan, &prior, sizeof(plan)) == 0, "unsupported selection rolls back arenas and result");
                    test->refused += refused;
                }
            }
            tier1_pipeline_expect(test, tier1_pipeline_unchanged(&fixture, &original), "unsupported compilation preserves the original typed fixture");
        }
    }
}

static bool tier1_pipeline_oracle_source(const Tier1PipelineTest* test, const char* path)
{
    FILE* file = fopen(path, "w");
    bool ok = file != 0;
    if (ok)
    {
        // Separate source formulas intentionally know nothing about SSA IDs, selected patterns or allocations.
        // Signed narrow inputs are reconstructed with in-range arithmetic before casting, without host truncation.
        fputs("#include <stdint.h>\n#include <stdio.h>\n#include <inttypes.h>\n"
              "static uint64_t constant(unsigned i) { return UINT64_C(0x9e3779b97f4a7c15) + (uint64_t)i * UINT64_C(0xd1b54a32d192ed03); }\n"
              "static __attribute__((noinline)) uint64_t helper_constant(void) { return 7; }\n"
              "static __attribute__((noinline)) uint64_t helper_arguments(uint64_t x, uint64_t y, uint64_t z) { return (x + (y << 3)) ^ z; }\n"
              "static uint64_t original(unsigned shape, uint64_t a, uint64_t b, uint64_t c)\n{\n"
              "    uint64_t result;\n"
              "    if (shape == 0) { uint64_t local = ~a; result = (local + (b << 3)) ^ c; }\n"
              "    else if (shape == 1) { uint64_t local[12]; for (unsigned i = 0; i < 12; ++i) local[i] = (a ^ constant(i)) + (b << (i % 5 + 1)); result = c; for (unsigned i = 0; i < 12; ++i) result += local[i]; }\n"
              "    else if (shape == 2) { uint64_t local[16]; for (unsigned i = 0; i < 16; ++i) local[i] = constant(i); result = (a ^ b) + c; for (unsigned i = 0; i < 16; ++i) { if (i % 2 == 0) result += local[i]; else result ^= local[i]; } }\n"
              "    else if (shape == 3) { uint32_t local = (uint32_t)a + (uint32_t)b; result = (uint64_t)(uint32_t)((local >> 11) + (local ^ UINT32_C(0x92d68ca7))) ^ c; }\n"
              "    else if (shape == 4) { result = helper_constant(); }\n"
              "    else if (shape == 5) { uint64_t live = a ^ b; uint64_t called = helper_arguments(c, a, b); result = (called + live) ^ c; }\n"
              "    else if (shape == 6) { uint64_t low = a % UINT64_C(65536); uint64_t swapped = (low % 256) * 256 + low / 256; result = (swapped + b) ^ c; }\n"
              "    else { int8_t small = (int8_t)((int)(a & 255) - ((a & 128) != 0 ? 256 : 0)); int16_t via = small; int16_t medium = (int16_t)((int32_t)(b & 65535) - ((b & 32768) != 0 ? 65536 : 0)); int32_t wide = (int32_t)((int64_t)(c & UINT32_MAX) - ((c & UINT64_C(0x80000000)) != 0 ? INT64_C(0x100000000) : 0)); result = (((uint64_t)(int64_t)via + (uint64_t)(int64_t)medium) ^ (uint64_t)(int64_t)wide) + ((uint64_t)(int64_t)small << 17); }\n"
              "    return result;\n}\n"
              "static const uint64_t inputs[][3] = {\n", file);
        for (uint32_t input = 0; input < BUSTER_TIER1_PIPELINE_INPUTS; input += 1)
        {
            fprintf(file, "    { UINT64_C(0x%016" PRIx64 "), UINT64_C(0x%016" PRIx64 "), UINT64_C(0x%016" PRIx64 ") },\n", test->inputs[input][0], test->inputs[input][1], test->inputs[input][2]);
        }
        fprintf(file, "};\nint main(void)\n{\n    for (unsigned shape = 0; shape < %u; ++shape)\n        for (unsigned input = 0; input < %u; ++input)\n            printf(\"%%u %%u %%016\" PRIx64 \"\\n\", shape, input, original(shape, inputs[input][0], inputs[input][1], inputs[input][2]));\n    return 0;\n}\n", BUSTER_TIER1_PIPELINE_CASES, BUSTER_TIER1_PIPELINE_INPUTS);
        ok = !ferror(file);
        ok = fclose(file) == 0 && ok;
    }
    return ok;
}

static void tier1_pipeline_clang(Tier1PipelineTest* test, const char* clang)
{
    const char* temporary = getenv("TMPDIR");
    temporary = temporary != 0 && temporary[0] != 0 ? temporary : "/tmp";
    char directory[4096];
    char source[4096];
    char binary[4096];
    char output[4096];
    const int length = snprintf(directory, sizeof(directory), "%s/buster-tier1-pipeline-XXXXXX", temporary);
    bool ok = length > 0 && (uint64_t)length < sizeof(directory) - 32 && mkdtemp(directory) != 0;
    const bool created = ok;
    tier1_pipeline_expect(test, ok, "create a private Clang oracle directory");
    if (ok)
    {
        snprintf(source, sizeof(source), "%s/original.c", directory);
        snprintf(binary, sizeof(binary), "%s/oracle", directory);
        snprintf(output, sizeof(output), "%s/results.txt", directory);
        ok = tier1_pipeline_expect(test, tier1_pipeline_oracle_source(test, source), "write original C oracle formulas");
    }
    for (uint32_t optimization = 0; ok && optimization < 2; optimization += 1)
    {
        const char* level = optimization == 0 ? "-O0" : "-O2";
        char* const compile[] = {
            (char*)clang,
            "-std=c17",
            (char*)level,
            "-march=x86-64",
            "-Wall",
            "-Werror",
            source,
            "-o",
            binary,
            0,
        };
        const Tier1PipelineProcess compiled = tier1_pipeline_process(compile, 0, 30000);
        ok = tier1_pipeline_expect(test, tier1_pipeline_process_ok(&compiled), "Clang compiles the independent integer C formulas and reaps its descendants");
        if (ok)
        {
            char* const execute[] = {
                binary,
                0,
            };
            const Tier1PipelineProcess executed = tier1_pipeline_process(execute, output, 10000);
            ok = tier1_pipeline_expect(test, tier1_pipeline_process_ok(&executed), "Clang oracle exits normally and its process tree is reaped within budget");
        }
        FILE* file = ok ? fopen(output, "r") : 0;
        if (ok)
        {
            ok = tier1_pipeline_expect(test, file != 0, "read Clang oracle result rows");
        }
        for (uint32_t shape = 0; ok && shape < BUSTER_TIER1_PIPELINE_CASES; shape += 1)
        {
            test->fixture = shape;
            for (uint32_t input = 0; ok && input < BUSTER_TIER1_PIPELINE_INPUTS; input += 1)
            {
                unsigned parsed_shape = UINT32_MAX;
                unsigned parsed_input = UINT32_MAX;
                uint64_t value = 0;
                const int fields = fscanf(file, "%u %u %" SCNx64, &parsed_shape, &parsed_input, &value);
                ok = tier1_pipeline_expect(test, fields == 3 && parsed_shape == shape && parsed_input == input, "Clang returns each expected fixture/input row in order");
                if (ok)
                {
                    tier1_pipeline_expect(test, value == test->expected[shape][input], "Clang return bits equal the independent typed interpreter");
                    test->clang_results += 1;
                }
            }
        }
        if (file != 0)
        {
            char extra;
            tier1_pipeline_expect(test, fscanf(file, " %c", &extra) == EOF && !ferror(file), "Clang result file has no missing or extra trailing row");
            fclose(file);
        }
    }
    if (created)
    {
        if (ok && test->failures == 0)
        {
            unlink(source);
            unlink(binary);
            unlink(output);
            rmdir(directory);
        }
        else
        {
            fprintf(stderr, "tier1-pipeline: retained Clang oracle files in %s\n", directory);
        }
    }
}

static int run_tier1_pipeline_tests(int argc, char** argv)
{
    uint64_t seed = UINT64_C(0xb138134177);
    const char* clang = "clang";
    bool cross_check = false;
    bool valid = true;
    for (int argument = 0; valid && argument < argc; argument += 1)
    {
        if (strcmp(argv[argument], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else if (strcmp(argv[argument], "--clang") == 0 && argument + 1 < argc)
        {
            clang = argv[++argument];
            cross_check = true;
        }
        else if (strcmp(argv[argument], "--seed") == 0 && argument + 1 < argc)
        {
            const char* value = argv[++argument];
            char* end = 0;
            errno = 0;
            seed = strtoull(value, &end, 0);
            valid = value[0] != 0 && value[0] != '-' && end != value && *end == 0 && errno == 0;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-tier1-pipeline [--seed N] [--cross-check] [--clang PATH]\n");
    }
    else
    {
        Tier1PipelineTest test = {
            .seed = seed,
            .allowed = malloc(X86_FORM_COUNT),
        };
        bool ready = x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize() && tier0_initialize() && tier0_tables_open(&test.tables);
        for (uint32_t a = 0; a < BUSTER_TIER1_PIPELINE_ARENAS; a += 1)
        {
            test.arenas[a] = arena_reserve(8u << 20);
            ready = test.arenas[a].base != 0 && ready;
        }
        ready = tier1_pipeline_expect(&test, ready && test.allowed != 0, "initialize bounded pipeline storage and tables");
        if (ready)
        {
            const X86Target target = {
                .architecture = X86_ARCH_BASELINE,
            };
            x86_target_forms(&target, test.allowed);
            for (uint32_t batch = 0; batch < BUSTER_TIER1_PIPELINE_INPUTS / BUSTER_TIER0_TEST_INPUTS; batch += 1)
            {
                tier0_test_inputs(seed + batch, test.inputs + batch * BUSTER_TIER0_TEST_INPUTS);
            }
            const uint64_t boundaries[] = {
                0,
                1,
                UINT32_MAX,
                UINT64_C(0x100000000),
                UINT64_C(0x7fffffffffffffff),
                UINT64_C(0x8000000000000000),
                UINT64_MAX - 1,
                UINT64_MAX,
            };
            for (uint32_t input = 0; input < BUSTER_ARRAY_LENGTH(boundaries); input += 1)
            {
                test.inputs[input][0] = boundaries[input];
                test.inputs[input][1] = boundaries[BUSTER_ARRAY_LENGTH(boundaries) - 1 - input];
                test.inputs[input][2] = boundaries[(input + 3) % BUSTER_ARRAY_LENGTH(boundaries)];
            }
            const uint64_t narrow_boundaries[] = {
                UINT64_C(0x7f),
                UINT64_C(0x80),
                UINT64_C(0xff),
                UINT64_C(0x7fff),
                UINT64_C(0x8000),
                UINT64_C(0xffff),
                UINT64_C(0x7fffffff),
                UINT64_C(0x80000000),
            };
            _Static_assert(BUSTER_ARRAY_LENGTH(boundaries) + BUSTER_ARRAY_LENGTH(narrow_boundaries) <= BUSTER_TIER1_PIPELINE_INPUTS, "boundary inputs fit the fixture table");
            for (uint32_t input = 0; input < BUSTER_ARRAY_LENGTH(narrow_boundaries); input += 1)
            {
                const uint32_t row = BUSTER_ARRAY_LENGTH(boundaries) + input;
                test.inputs[row][0] = UINT64_C(0xa5a55a5a00000000) | narrow_boundaries[input];
                test.inputs[row][1] = UINT64_C(0x5a5aa5a500000000) | narrow_boundaries[BUSTER_ARRAY_LENGTH(narrow_boundaries) - 1 - input];
                test.inputs[row][2] = UINT64_C(0xfedcba9800000000) | narrow_boundaries[(input + 3) % BUSTER_ARRAY_LENGTH(narrow_boundaries)];
            }
            tier1_pipeline_mutation_regression(&test);
            tier1_pipeline_deadline_regression(&test);
            tier1_pipeline_supported(&test);
            tier1_pipeline_refusals(&test);
            if (cross_check && test.failures == 0)
            {
                tier1_pipeline_clang(&test, clang);
            }
            tier1_pipeline_expect(&test, test.native_results == BUSTER_TIER1_PIPELINE_CASES * 4 * BUSTER_TIER1_PIPELINE_INPUTS, "every supported path executes every input");
            tier1_pipeline_expect(&test, test.resolved_calls == BUSTER_TIER1_PIPELINE_CALL_CASES * 4, "every supported call path resolves its original local helper symbol");
            tier1_pipeline_expect(&test, test.refused == 8, "every remaining unsupported shape is refused on both paths");
            if (cross_check)
            {
                tier1_pipeline_expect(&test, test.clang_results == BUSTER_TIER1_PIPELINE_CASES * 2 * BUSTER_TIER1_PIPELINE_INPUTS, "both Clang optimization levels check every original fixture/input");
            }
        }
        printf("tier1 pipeline: %" PRIu64 " checks, %" PRIu64 " failures; native=%u clang=%u refused=%u calls=%u stores=%" PRIu64 " reloads=%" PRIu64 " call-stores=%" PRIu64 " call-reloads=%" PRIu64 " rematerializations=%" PRIu64 " seed=%" PRIu64 "\n", test.checks, test.failures, test.native_results, test.clang_results, test.refused, test.resolved_calls, test.stores, test.loads, test.call_stores, test.call_loads, test.rematerializations, seed);
        for (uint32_t a = 0; a < BUSTER_TIER1_PIPELINE_ARENAS; a += 1)
        {
            arena_release(&test.arenas[a]);
        }
        tier0_tables_close(&test.tables);
        free(test.allowed);
        result = test.failures != 0;
    }
    return result;
}
