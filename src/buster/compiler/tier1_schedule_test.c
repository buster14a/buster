// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original dependency, fusion, cost-coverage, permutation and guarded-slice tests for the experimental scheduler.
// Include after tier1_schedule.c. These are machine-record fixtures, not source/corpus compilation evidence.

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct Tier1ScheduleTest Tier1ScheduleTest;
struct Tier1ScheduleTest
{
    uint64_t checks;
    uint64_t failures;
    uint32_t seed;
};

static void tier1_schedule_test_expect(Tier1ScheduleTest* test, bool condition, const char* what)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 8)
        {
            fprintf(stderr, "tier1-schedule: %s (seed=%u)\n", what, test->seed);
        }
        test->failures += 1;
    }
}

static X86Instruction tier1_schedule_test_instruction(uint16_t form, uint8_t reg, uint8_t rm)
{
    const X86Instruction result = {
        .form = form,
        .reg = reg,
        .rm = rm,
        .mode = BUSTER_X86_REGISTER_DIRECT,
    };
    return result;
}

static uint32_t tier1_schedule_test_random(uint32_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void tier1_schedule_test_records(X86Instruction* records, uint8_t* pins, uint32_t count, uint32_t* state, bool movable)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t value = tier1_schedule_test_random(state);
        const uint8_t rm = (value >> 8) & 15;
        const uint8_t reg = (value >> 12) & 15;
        X86Instruction record = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, rm);
        const uint32_t kind = movable ? value % 3 : value % 14;
        switch (kind)
        {
            case 0:
                break;
            case 1:
                record = tier1_schedule_test_instruction(X86_FORM_IMUL_R64_RM64, reg, rm);
                break;
            case 2:
                record = tier1_schedule_test_instruction(X86_FORM_ADD_RM64_R64, reg, rm);
                break;
            case 3:
                record = tier1_schedule_test_instruction(X86_FORM_LEA_R64_M, reg, X86_REGISTER_R14);
                record.mode = BUSTER_X86_MEMORY_BASE_INDEX(0);
                record.index = X86_REGISTER_RDI;
                break;
            case 4:
                record = tier1_schedule_test_instruction(X86_FORM_ADD_RM64_IMM8, 0, rm);
                record.immediate = 3;
                break;
            case 5:
                record = tier1_schedule_test_instruction(X86_FORM_MOV_R64_IMM64, 0, rm);
                record.immediate = 4660;
                break;
            case 6:
                record = tier1_schedule_test_instruction(X86_FORM_MOV_R64_RM64, reg, rm);
                record.mode = BUSTER_X86_MEMORY_BASE;
                break;
            case 7:
                record = tier1_schedule_test_instruction(X86_FORM_CMP_RM64_R64, reg, rm);
                break;
            case 8:
                record = tier1_schedule_test_instruction(X86_FORM_JE_REL32, 0, 0);
                break;
            case 9:
                record = tier1_schedule_test_instruction(X86_FORM_DIV_RM64, 0, rm);
                break;
            case 10:
                record = tier1_schedule_test_instruction(X86_FORM_MOVSS_X_XM32, reg, rm);
                break;
            case 11:
                record.form = X86_FORM_COUNT;
                break;
            case 12:
                record = tier1_schedule_test_instruction(X86_FORM_XOR_RM64_R64, rm, rm);
                break;
            case 13:
                record.decorations = 1;
                break;
        }
        records[i] = record;
        pins[i] = !movable && (value >> 16) % 19 == 0;
    }
}

static void tier1_schedule_test_compare(Tier1ScheduleTest* test, Tier1ScheduleInput input, Tier1ScheduleGraph* scalar, Tier1ScheduleGraph* vector)
{
    uint16_t scalar_order[BUSTER_TIER1_SCHEDULE_MAX_RECORDS + 1];
    uint16_t vector_order[BUSTER_TIER1_SCHEDULE_MAX_RECORDS + 1];
    memset(scalar_order, 0x5a, sizeof(scalar_order));
    memset(vector_order, 0x5a, sizeof(vector_order));
    Tier1ScheduleStats scalar_stats;
    Tier1ScheduleStats vector_stats;
    const Tier1ScheduleStatus left = tier1_schedule_block(input, scalar, scalar_order, input.count, false, &scalar_stats);
    const Tier1ScheduleStatus right = tier1_schedule_block(input, vector, vector_order, input.count, true, &vector_stats);
    tier1_schedule_test_expect(test, left == TIER1_SCHEDULE_OK && right == TIER1_SCHEDULE_OK, "both paths accept bounded input");
    tier1_schedule_test_expect(test, memcmp(scalar_order, vector_order, sizeof(scalar_order)) == 0, "identical order and untouched output tail");
    tier1_schedule_test_expect(test, scalar_order[input.count] == 0x5a5a, "no store outside logical output");
    tier1_schedule_test_expect(test, memcmp(&scalar_stats, &vector_stats, sizeof(scalar_stats)) == 0, "identical coverage and scheduling statistics");
    tier1_schedule_test_expect(test, tier1_schedule_verify(input, scalar_order) && tier1_schedule_verify(input, vector_order), "independent dependency verifier accepts both orders");
    for (uint32_t i = 0; i < input.count; i += 1)
    {
        tier1_schedule_test_expect(test, scalar->reads[i] == vector->reads[i] && scalar->writes[i] == vector->writes[i], "identical resolved effects");
        tier1_schedule_test_expect(test, scalar->heights[i] == vector->heights[i] && scalar->levels[i] == vector->levels[i], "identical weighted heights and reverse levels");
        tier1_schedule_test_expect(test, scalar->latencies[i] == vector->latencies[i] && scalar->cost_ids[i] == vector->cost_ids[i], "identical operand-sensitive model consumption");
        tier1_schedule_test_expect(test, memcmp(scalar->predecessors[i], vector->predecessors[i], sizeof(scalar->predecessors[i])) == 0, "identical predecessor bitsets");
        tier1_schedule_test_expect(test, memcmp(scalar->successors[i], vector->successors[i], sizeof(scalar->successors[i])) == 0, "identical successor bitsets");
        tier1_schedule_test_expect(test, memcmp(scalar->data_successors[i], vector->data_successors[i], sizeof(scalar->data_successors[i])) == 0, "identical GPR RAW priority edges");
    }
    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
    {
        tier1_schedule_test_expect(test, scalar_stats.missing_columns[column] == x86_zen5_missing(input.instructions, input.count, 1u << column), "coverage counts the actual input records");
    }
}

static void tier1_schedule_test_examples(Tier1ScheduleTest* test, Tier1ScheduleGraph* scalar, Tier1ScheduleGraph* vector)
{
    X86Instruction records[] = {
        { .form = X86_FORM_NOT_RM64, .rm = X86_REGISTER_R8, .mode = BUSTER_X86_REGISTER_DIRECT },
        { .form = X86_FORM_NOT_RM64, .rm = X86_REGISTER_R9, .mode = BUSTER_X86_REGISTER_DIRECT },
        { .form = X86_FORM_IMUL_R64_RM64, .reg = X86_REGISTER_R10, .rm = X86_REGISTER_R11, .mode = BUSTER_X86_REGISTER_DIRECT },
        { .form = X86_FORM_NOT_RM64, .rm = X86_REGISTER_R10, .mode = BUSTER_X86_REGISTER_DIRECT },
    };
    Tier1ScheduleInput input = { .instructions = records, .count = 4 };
    uint16_t order[8];
    Tier1ScheduleStats stats;
    tier1_schedule_test_compare(test, input, scalar, vector);
    tier1_schedule_test_expect(test, tier1_schedule_block(input, vector, order, 8, true, &stats) == TIER1_SCHEDULE_OK, "measured dependency example schedules");
    const uint16_t expected[] = { 2, 0, 1, 3 };
    tier1_schedule_test_expect(test, memcmp(order, expected, sizeof(expected)) == 0 && stats.moved == 3, "measured IMUL chain starts first with stable ties");
    tier1_schedule_test_expect(test, vector->heights[2] == 4 * BUSTER_X86_COST_SCALE && vector->heights[0] == BUSTER_X86_COST_SCALE, "actual three-cycle IMUL weights its GPR dependency");
    tier1_schedule_test_expect(test, stats.unknown_cases == 0 && stats.missing_columns[X86_COST_PIPES] == 4, "known GPR costs never imply known pipes");
    uint16_t invalid[] = { 3, 0, 1, 2 };
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, invalid), "verifier rejects RAW reversal");
    invalid[0] = 0;
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, invalid), "verifier rejects a duplicate");
    invalid[0] = UINT16_MAX;
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, invalid), "verifier rejects an out-of-range position");

    records[0] = tier1_schedule_test_instruction(X86_FORM_IMUL_R64_RM64, X86_REGISTER_R10, X86_REGISTER_R11);
    records[1] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R11);
    input.count = 2;
    const uint16_t reversed[] = { 1, 0 };
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "verifier rejects WAR reversal");
    records[1] = tier1_schedule_test_instruction(X86_FORM_IMUL_R64_RM64, X86_REGISTER_R12, X86_REGISTER_R13);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "verifier rejects flags WAW reversal");
    tier1_schedule_test_compare(test, input, scalar, vector);
    tier1_schedule_test_expect(test, vector->heights[0] == 3 * BUSTER_X86_COST_SCALE && vector->heights[1] == 3 * BUSTER_X86_COST_SCALE, "flags-only ordering does not acquire a GPR latency");

    records[0] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R8);
    records[1] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R9);
    uint8_t pins[] = { 1, 0 };
    input.pinned = pins;
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "caller anchor cannot move");
    tier1_schedule_test_compare(test, input, scalar, vector);
    input.pinned = 0;

    records[0] = tier1_schedule_test_instruction(X86_FORM_CMP_RM64_R64, X86_REGISTER_R8, X86_REGISTER_R9);
    records[1] = tier1_schedule_test_instruction(X86_FORM_JE_REL32, 0, 0);
    records[2] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R10);
    input.count = 3;
    const uint16_t separated[] = { 0, 2, 1 };
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, separated), "compare and branch stay adjacent");
    tier1_schedule_test_compare(test, input, scalar, vector);

    records[0] = tier1_schedule_test_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_R8, X86_REGISTER_R9);
    records[0].mode = BUSTER_X86_MEMORY_BASE;
    records[1] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R10);
    input.count = 2;
    tier1_schedule_test_expect(test, x86_zen5_id(records) != 0 && !tier1_schedule_verify(input, reversed), "known L1 case remains a possible fault and memory barrier");
    tier1_schedule_test_compare(test, input, scalar, vector);
    records[0] = tier1_schedule_test_instruction(X86_FORM_ADD_RM64_IMM8, 0, X86_REGISTER_R8);
    records[0].immediate = 4;
    tier1_schedule_test_expect(test, x86_zen5_id(records) == 0 && !tier1_schedule_verify(input, reversed), "unmeasured immediate is unknown and stays pinned");
    tier1_schedule_test_compare(test, input, scalar, vector);
    records[0] = tier1_schedule_test_instruction(X86_FORM_DIV_RM64, 0, X86_REGISTER_R8);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "possible arithmetic fault stays pinned");
    tier1_schedule_test_compare(test, input, scalar, vector);
    records[0] = tier1_schedule_test_instruction(X86_FORM_MOV_RM8_R8, X86_REGISTER_R8, X86_REGISTER_R9);
    records[1] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R9);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "partial register write retains the whole-register dependency");
    tier1_schedule_test_compare(test, input, scalar, vector);

    Tier1ScheduleStats saved;
    memset(&saved, 0x5a, sizeof(saved));
    stats = saved;
    order[0] = 0x5a5a;
    input.count = BUSTER_TIER1_SCHEDULE_MAX_RECORDS + 1;
    input.instructions = (const X86Instruction*)1;
    tier1_schedule_test_expect(test, tier1_schedule_block(input, 0, order, input.count, true, &stats) == TIER1_SCHEDULE_CAPACITY, "oversized block declines before input access");
    tier1_schedule_test_expect(test, order[0] == 0x5a5a && memcmp(&stats, &saved, sizeof(saved)) == 0, "capacity rejection preserves output and statistics");
    input.count = 1;
    tier1_schedule_test_expect(test, tier1_schedule_block(input, 0, order, 0, true, &stats) == TIER1_SCHEDULE_CAPACITY, "short output capacity declines before input access");
    input.instructions = 0;
    tier1_schedule_test_expect(test, tier1_schedule_block(input, 0, order, 1, true, &stats) == TIER1_SCHEDULE_INVALID_ARGUMENT, "null nonempty input is invalid");
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, order), "null nonempty verification is invalid");
    input.count = 0;
    tier1_schedule_test_expect(test, tier1_schedule_block(input, 0, 0, 0, true, &stats) == TIER1_SCHEDULE_OK && tier1_schedule_verify(input, 0), "empty input permits null slices");
}

static void tier1_schedule_test_guards(Tier1ScheduleTest* test, Tier1ScheduleGraph* scalar, Tier1ScheduleGraph* vector)
{
    const long page = sysconf(_SC_PAGESIZE);
    bool allocated = page >= (long)(BUSTER_TIER1_SCHEDULE_MAX_RECORDS * sizeof(X86Instruction));
    uint8_t* records_mapping = MAP_FAILED;
    uint8_t* pins_mapping = MAP_FAILED;
    uint8_t* output_mapping = MAP_FAILED;
    if (allocated)
    {
        records_mapping = mmap(0, page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        pins_mapping = mmap(0, page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        output_mapping = mmap(0, page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        allocated = records_mapping != MAP_FAILED && pins_mapping != MAP_FAILED && output_mapping != MAP_FAILED;
    }
    if (allocated)
    {
        allocated = mprotect(records_mapping, page, PROT_READ | PROT_WRITE) == 0 && mprotect(pins_mapping, page, PROT_READ | PROT_WRITE) == 0 &&
                    mprotect(output_mapping, page, PROT_READ | PROT_WRITE) == 0;
    }
    tier1_schedule_test_expect(test, allocated, "guarded mappings allocated");
    if (allocated)
    {
        uint32_t state = test->seed;
        for (uint32_t count = 0; count <= BUSTER_TIER1_SCHEDULE_MAX_RECORDS; count += 1)
        {
            X86Instruction* records = (X86Instruction*)(records_mapping + page - count * sizeof(X86Instruction));
            uint8_t* pins = pins_mapping + page - count;
            uint16_t* output = (uint16_t*)(output_mapping + page - count * sizeof(uint16_t));
            tier1_schedule_test_records(records, pins, count, &state, count % 2 == 0);
            const Tier1ScheduleInput input = {
                .instructions = records,
                .pinned = pins,
                .count = count,
            };
            tier1_schedule_test_compare(test, input, scalar, vector);
            tier1_schedule_test_expect(test, tier1_schedule_block(input, vector, output, count, true, 0) == TIER1_SCHEDULE_OK && tier1_schedule_verify(input, output), "every guarded input/output length through 256");
        }
    }
    if (records_mapping != MAP_FAILED)
    {
        munmap(records_mapping, page * 2);
    }
    if (pins_mapping != MAP_FAILED)
    {
        munmap(pins_mapping, page * 2);
    }
    if (output_mapping != MAP_FAILED)
    {
        munmap(output_mapping, page * 2);
    }
}

static int run_tier1_schedule_tests(int argc, char** argv)
{
    Tier1ScheduleTest test = { .seed = 172 };
    bool arguments = true;
    for (int i = 0; arguments && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            const unsigned long value = strtoul(argv[i + 1], &end, 10);
            arguments = errno == 0 && end != argv[i + 1] && *end == 0 && value <= UINT32_MAX;
            test.seed = (uint32_t)value;
            i += 1;
        }
        else
        {
            arguments = false;
        }
    }
    const bool initialized = arguments && x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize();
    Tier1ScheduleGraph* scalar = initialized ? malloc(sizeof(*scalar)) : 0;
    Tier1ScheduleGraph* vector = initialized ? malloc(sizeof(*vector)) : 0;
    const bool ready = initialized && scalar && vector;
    if (ready)
    {
        tier1_schedule_test_examples(&test, scalar, vector);
        tier1_schedule_test_guards(&test, scalar, vector);
        X86Instruction records[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
        uint8_t pins[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
        uint32_t state = test.seed;
        for (uint32_t trial = 0; trial < 512; trial += 1)
        {
            const uint32_t count = tier1_schedule_test_random(&state) % (BUSTER_TIER1_SCHEDULE_MAX_RECORDS + 1);
            tier1_schedule_test_records(records, pins, count, &state, trial % 2 == 0);
            const Tier1ScheduleInput input = {
                .instructions = records,
                .pinned = trial % 3 == 0 ? 0 : pins,
                .count = count,
            };
            tier1_schedule_test_compare(&test, input, scalar, vector);
        }
    }
    else
    {
        fprintf(stderr, "test-tier1-schedule: invalid arguments, initialization or allocation failure\n");
    }
    free(scalar);
    free(vector);
    printf("Tier 1 scheduling: %" PRIu64 " checks, %" PRIu64 " failures, seed=%u; machine-record fixtures only\n", test.checks, test.failures, test.seed);
    return ready && test.failures == 0 ? 0 : 1;
}
