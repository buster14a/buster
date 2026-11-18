// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original dependency, fusion, cost-coverage, permutation, guarded-slice and generated-execution tests for the experimental scheduler.
// Include after tier1_schedule.c. These are machine-record fixtures, not source/corpus compilation evidence.

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "buster/compiler/tier1_schedule_fixture.c"

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

static bool tier1_schedule_test_bit(const uint64_t* row, uint32_t index)
{
    return ((row[index / 64] >> (index & 63)) & 1) != 0;
}

// Schedules `input` into a poisoned graph and checks the graph and the order against their definitions, from the
// graph's own effect columns (x86_instructions_effects resolves them; test-x86-semantics checks that):
// - record j precedes a later record i exactly where they conflict: one's writes meet the other's reads, or a write
//   meets the other's live writes, or either is a barrier; successors are the predecessors transposed; a data successor
//   is an edge whose earlier record writes a register or flag the later one reads;
// - each height is the record's latency plus its data successors' greatest height, or zero at a barrier; each level is
//   one more than its successors' greatest level, or 1 without one; each latency is the model's for the record's cost
//   id where the model has one;
// - the statistics count the edges, the levels and the moved records;
// - the order takes, at each step, the earliest of the ready records with the greatest height, and leaves the slot past
//   the block untouched; tier1_schedule_verify accepts it.
static void tier1_schedule_test_check(Tier1ScheduleTest* test, Tier1ScheduleInput input, Tier1ScheduleGraph* graph)
{
    uint16_t order[BUSTER_TIER1_SCHEDULE_MAX_RECORDS + 1];
    memset(order, 0x5a, sizeof(order));
    memset(graph, 0xa5, sizeof(*graph));
    Tier1ScheduleStats stats;
    memset(&stats, 0xa5, sizeof(stats));
    const Tier1ScheduleStatus status = tier1_schedule_block(input, graph, order, input.count, &stats);
    tier1_schedule_test_expect(test, status == TIER1_SCHEDULE_OK, "bounded input schedules");
    if (status == TIER1_SCHEDULE_OK)
    {
        const uint32_t count = input.count;
        uint32_t edges = 0;
        uint32_t levels = 0;
        uint32_t moved = 0;
        bool graph_valid = true;
        bool heights_valid = true;
        bool latencies_valid = true;
        for (uint32_t i = 0; i < count; i += 1)
        {
            for (uint32_t j = 0; j < count; j += 1)
            {
                const bool conflict = j < i && ((graph->writes[j] & graph->reads[i]) != 0 || (graph->reads[j] & graph->writes[i]) != 0 ||
                                                (graph->writes[j] & graph->live_writes[i]) != 0 || (graph->live_writes[j] & graph->writes[i]) != 0 ||
                                                graph->barriers[j] != 0 || graph->barriers[i] != 0);
                const bool data = conflict && (graph->writes[j] & graph->reads[i] & BUSTER_TIER1_SCHEDULE_DATA) != 0;
                graph_valid = graph_valid && tier1_schedule_test_bit(graph->predecessors[i], j) == conflict && tier1_schedule_test_bit(graph->successors[j], i) == conflict &&
                              tier1_schedule_test_bit(graph->data_successors[j], i) == data;
                edges += conflict;
            }
            const uint16_t model = x86_zen5_columns[X86_COST_LATENCY][graph->cost_ids[i]];
            latencies_valid = latencies_valid && graph->cost_ids[i] == x86_zen5_id(&input.instructions[i]) && (model == BUSTER_X86_COST_UNKNOWN || graph->latencies[i] == model);
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            uint32_t height = 0;
            uint32_t level = 0;
            for (uint32_t next = i + 1; next < count; next += 1)
            {
                height = tier1_schedule_test_bit(graph->data_successors[i], next) && graph->heights[next] > height ? graph->heights[next] : height;
                level = tier1_schedule_test_bit(graph->successors[i], next) && graph->levels[next] > level ? graph->levels[next] : level;
            }
            heights_valid = heights_valid && graph->heights[i] == (graph->barriers[i] != 0 ? 0 : graph->latencies[i] + height) && graph->levels[i] == level + 1;
            levels = graph->levels[i] > levels ? graph->levels[i] : levels;
        }
        uint64_t pending[BUSTER_TIER1_SCHEDULE_WORDS] = { 0 };
        for (uint32_t i = 0; i < count; i += 1)
        {
            pending[i / 64] |= UINT64_C(1) << (i & 63);
        }
        bool order_valid = true;
        for (uint32_t position = 0; order_valid && position < count; position += 1)
        {
            uint32_t best = count;
            for (uint32_t i = 0; i < count; i += 1)
            {
                uint64_t waiting = 0;
                for (uint32_t word = 0; word < BUSTER_TIER1_SCHEDULE_WORDS; word += 1)
                {
                    waiting |= graph->predecessors[i][word] & pending[word];
                }
                const bool ready = tier1_schedule_test_bit(pending, i) && waiting == 0;
                best = ready && (best == count || graph->heights[i] > graph->heights[best]) ? i : best;
            }
            order_valid = best < count && order[position] == best;
            pending[best / 64] &= order_valid ? ~(UINT64_C(1) << (best & 63)) : UINT64_MAX;
            moved += order[position] != position;
        }
        tier1_schedule_test_expect(test, graph_valid, "each edge is exactly a conflict, successors transpose predecessors, data edges are register or flag RAW");
        tier1_schedule_test_expect(test, heights_valid, "each height is its latency plus its data successors' greatest, zero at a barrier; each level one past its successors'");
        tier1_schedule_test_expect(test, latencies_valid, "each record's cost id and measured latency are the model's");
        tier1_schedule_test_expect(test, stats.records == count && stats.edges == edges && stats.levels == levels && stats.moved == moved, "statistics count the edges, levels and moved records");
        tier1_schedule_test_expect(test, order_valid && order[count] == 0x5a5a, "the order takes the earliest ready record of greatest height and stores nothing past the block");
        tier1_schedule_test_expect(test, tier1_schedule_verify(input, order), "the independent dependency verifier accepts the order");
        // The columns whose value each record's case lacks: unknown, an absent path's too (x86_zen5_missing counts the ones
        // a row doesn't account for, which an absent path is).
        for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
        {
            uint32_t unknown = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                unknown += x86_zen5_columns[column][x86_zen5_id(&input.instructions[i])] == BUSTER_X86_COST_UNKNOWN;
            }
            tier1_schedule_test_expect(test, stats.missing_columns[column] == unknown, "coverage counts the actual input records");
        }
    }
}

static void tier1_schedule_test_examples(Tier1ScheduleTest* test, Tier1ScheduleGraph* graph)
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
    tier1_schedule_test_check(test, input, graph);
    tier1_schedule_test_expect(test, tier1_schedule_block(input, graph, order, 8, &stats) == TIER1_SCHEDULE_OK, "measured dependency example schedules");
    const uint16_t expected[] = { 2, 0, 1, 3 };
    tier1_schedule_test_expect(test, memcmp(order, expected, sizeof(expected)) == 0 && stats.moved == 3, "measured IMUL chain starts first with stable ties");
    tier1_schedule_test_expect(test, graph->heights[2] == 4 * BUSTER_X86_COST_SCALE && graph->heights[0] == BUSTER_X86_COST_SCALE, "actual three-cycle IMUL weights its GPR dependency");
    tier1_schedule_test_expect(test, stats.unknown_cases == 0 && stats.missing_columns[X86_COST_PIPES] == 0, "the measured cases' pipes are known (#181)");
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
    tier1_schedule_test_check(test, input, graph);
    tier1_schedule_test_expect(test, graph->heights[0] == 3 * BUSTER_X86_COST_SCALE && graph->heights[1] == 3 * BUSTER_X86_COST_SCALE, "flags-only ordering does not acquire a GPR latency");

    records[0] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R8);
    records[1] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R9);
    uint8_t pins[] = { 1, 0 };
    input.pinned = pins;
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "caller anchor cannot move");
    tier1_schedule_test_check(test, input, graph);
    input.pinned = 0;

    records[0] = tier1_schedule_test_instruction(X86_FORM_CMP_RM64_R64, X86_REGISTER_R8, X86_REGISTER_R9);
    records[1] = tier1_schedule_test_instruction(X86_FORM_JE_REL32, 0, 0);
    records[2] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R10);
    input.count = 3;
    const uint16_t separated[] = { 0, 2, 1 };
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, separated), "compare and branch stay adjacent");
    tier1_schedule_test_check(test, input, graph);

    // A load moves among the records that don't access memory; any two accesses keep their order (no alias analysis,
    // and a volatile access stays where it was among the others).
    records[0] = tier1_schedule_test_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_R8, X86_REGISTER_R9);
    records[0].mode = BUSTER_X86_MEMORY_BASE;
    records[1] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R10);
    input.count = 2;
    tier1_schedule_test_expect(test, x86_zen5_id(records) != 0 && tier1_schedule_verify(input, reversed), "a known L1 load moves past a register operation");
    tier1_schedule_test_check(test, input, graph);
    tier1_schedule_test_expect(test, tier1_schedule_block(input, graph, order, 8, &stats) == TIER1_SCHEDULE_OK && stats.pinned == 0 &&
                                         graph->latencies[0] == 4 * BUSTER_X86_COST_SCALE, "a load is no barrier, its measured latency its weight");
    records[1] = tier1_schedule_test_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_R10, X86_REGISTER_R11);
    records[1].mode = BUSTER_X86_MEMORY_BASE;
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "two loads keep their order");
    tier1_schedule_test_check(test, input, graph);
    records[1] = tier1_schedule_test_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_R10, X86_REGISTER_R11);
    records[1].mode = BUSTER_X86_MEMORY_BASE;
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "a load and a store keep their order");
    tier1_schedule_test_check(test, input, graph);
    records[0] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R8);
    tier1_schedule_test_expect(test, tier1_schedule_verify(input, reversed), "a store moves past a register operation");
    tier1_schedule_test_check(test, input, graph);
    records[1] = tier1_schedule_test_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_R10, X86_REGISTER_R8);
    records[1].mode = BUSTER_X86_MEMORY_BASE;
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "a store stays after its value's definition");
    tier1_schedule_test_check(test, input, graph);
    // A rotate: a form the backends don't emit, which the model has no row of (#181).
    records[0] = tier1_schedule_test_instruction(X86_FORM_ROL_RM64_IMM8, 0, X86_REGISTER_R8);
    records[0].immediate = 4;
    records[1] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R10);
    tier1_schedule_test_expect(test, x86_zen5_id(records) == 0 && tier1_schedule_verify(input, reversed), "an unmeasured form moves");
    tier1_schedule_test_check(test, input, graph);
    tier1_schedule_test_expect(test, tier1_schedule_block(input, graph, order, 8, &stats) == TIER1_SCHEDULE_OK && stats.unknown_cases == 1 &&
                                         graph->latencies[0] == BUSTER_X86_COST_SCALE, "an unmeasured case is counted unknown and weighs its class");
    records[0] = tier1_schedule_test_instruction(X86_FORM_DIV_RM64, 0, X86_REGISTER_R8);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "possible arithmetic fault stays pinned");
    tier1_schedule_test_check(test, input, graph);
    // #1059's audit: a fence, a locked read/modify/write and a reader of the floating environment (MXCSR, which a
    // FENV_ACCESS region's rounding mode and exception flags live in) stay pinned, so that no access, and no floating
    // operation, moves across an ordering point or a fesetround call's effect.
    records[0] = tier1_schedule_test_instruction(X86_FORM_MFENCE, 0, 0);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "a fence stays pinned");
    tier1_schedule_test_check(test, input, graph);
    records[0] = tier1_schedule_test_instruction(X86_FORM_FLD_ST, 0, 0);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "a retained x87 result keeps its original spill's waiting boundary");
    tier1_schedule_test_check(test, input, graph);
    records[0] = tier1_schedule_test_instruction(X86_FORM_FSTP_ST, 0, 0);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "a retained x87 reload keeps its waiting boundary");
    tier1_schedule_test_check(test, input, graph);
    records[0] = tier1_schedule_test_instruction(X86_FORM_LOCK_CMPXCHG_M64_R64, X86_REGISTER_R9, X86_REGISTER_R8);
    records[0].mode = BUSTER_X86_MEMORY_BASE;
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "a locked compare-exchange stays pinned");
    tier1_schedule_test_check(test, input, graph);
    // A floating operation reads MXCSR's controls and may set its sticky status flags: floating operations move among
    // the others, and stmxcsr, which reads the status flags, stays pinned, as do calls (fesetround's, fetestexcept's).
    records[0] = tier1_schedule_test_instruction(X86_FORM_ADDSD_X_XM64, 0, 1);
    tier1_schedule_test_expect(test, tier1_schedule_verify(input, reversed), "a floating addition moves past a register operation");
    tier1_schedule_test_check(test, input, graph);
    records[1] = tier1_schedule_test_instruction(X86_FORM_MULSD_X_XM64, 2, 3);
    tier1_schedule_test_expect(test, tier1_schedule_verify(input, reversed), "two independent floating operations change places");
    tier1_schedule_test_check(test, input, graph);
    records[1] = tier1_schedule_test_instruction(X86_FORM_STMXCSR_M32, 0, X86_REGISTER_R8);
    records[1].mode = BUSTER_X86_MEMORY_BASE;
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "stmxcsr stays after a floating operation");
    tier1_schedule_test_check(test, input, graph);
    records[1] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R9);
    records[0] = tier1_schedule_test_instruction(X86_FORM_MOV_RM8_R8, X86_REGISTER_R8, X86_REGISTER_R9);
    records[1] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R9);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reversed), "partial register write retains the whole-register dependency");
    tier1_schedule_test_check(test, input, graph);

    // Flags: a writer whose flags are overwritten before any read is dead, and two dead writers may change places; a
    // live writer (read, or the block's last) keeps its order with every other writer and its readers.
    records[0] = tier1_schedule_test_instruction(X86_FORM_ADD_RM64_R64, X86_REGISTER_R9, X86_REGISTER_R8);
    records[1] = tier1_schedule_test_instruction(X86_FORM_ADD_RM64_R64, X86_REGISTER_R11, X86_REGISTER_R10);
    records[2] = tier1_schedule_test_instruction(X86_FORM_ADD_RM64_R64, X86_REGISTER_R13, X86_REGISTER_R12);
    input.count = 3;
    const uint16_t dead_swapped[] = { 1, 0, 2 };
    const uint16_t live_swapped[] = { 0, 2, 1 };
    tier1_schedule_test_expect(test, tier1_schedule_verify(input, dead_swapped), "two dead flag writers change places");
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, live_swapped), "the block's last flag writer stays last");
    tier1_schedule_test_check(test, input, graph);
    tier1_schedule_test_expect(test, tier1_schedule_block(input, graph, order, 8, &stats) == TIER1_SCHEDULE_OK && stats.dead_flags == 2 && stats.edges == 2,
                               "dead flag writers depend only on the live one");
    records[0] = tier1_schedule_test_instruction(X86_FORM_CMP_RM64_R64, X86_REGISTER_R9, X86_REGISTER_R8);
    records[1] = tier1_schedule_test_instruction(X86_FORM_SETE_RM8, 0, X86_REGISTER_R10);
    records[2] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R11);
    const uint16_t reader_first[] = { 2, 0, 1 };
    const uint16_t reader_split[] = { 0, 2, 1 };
    tier1_schedule_test_expect(test, tier1_schedule_verify(input, reader_first) && tier1_schedule_verify(input, reader_split), "a flags reader moves with its writer");
    tier1_schedule_test_check(test, input, graph);
    records[2] = tier1_schedule_test_instruction(X86_FORM_ADD_RM64_R64, X86_REGISTER_R13, X86_REGISTER_R12);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reader_split), "no flag writer between a reader and its writer");
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reader_first), "no reordering of the block's last flag writer");
    tier1_schedule_test_check(test, input, graph);
    records[1] = tier1_schedule_test_instruction(X86_FORM_CMOVE_R64_RM64, X86_REGISTER_R10, X86_REGISTER_R11);
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, reader_split), "no flag writer between a cmov and its writer");
    tier1_schedule_test_check(test, input, graph);
    records[1] = tier1_schedule_test_instruction(X86_FORM_CALL_REL32, 0, 0);
    records[2] = tier1_schedule_test_instruction(X86_FORM_NOT_RM64, 0, X86_REGISTER_R11);
    const uint16_t across[] = { 0, 2, 1 };
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, across), "a call is a barrier");
    tier1_schedule_test_check(test, input, graph);

    Tier1ScheduleStats saved;
    memset(&saved, 0x5a, sizeof(saved));
    stats = saved;
    order[0] = 0x5a5a;
    input.count = BUSTER_TIER1_SCHEDULE_MAX_RECORDS + 1;
    input.instructions = (const X86Instruction*)1;
    tier1_schedule_test_expect(test, tier1_schedule_block(input, 0, order, input.count, &stats) == TIER1_SCHEDULE_CAPACITY, "oversized block declines before input access");
    tier1_schedule_test_expect(test, order[0] == 0x5a5a && memcmp(&stats, &saved, sizeof(saved)) == 0, "capacity rejection preserves output and statistics");
    input.count = 1;
    tier1_schedule_test_expect(test, tier1_schedule_block(input, 0, order, 0, &stats) == TIER1_SCHEDULE_CAPACITY, "short output capacity declines before input access");
    input.instructions = 0;
    tier1_schedule_test_expect(test, tier1_schedule_block(input, 0, order, 1, &stats) == TIER1_SCHEDULE_INVALID_ARGUMENT, "null nonempty input is invalid");
    tier1_schedule_test_expect(test, !tier1_schedule_verify(input, order), "null nonempty verification is invalid");
    input.count = 0;
    tier1_schedule_test_expect(test, tier1_schedule_block(input, 0, 0, 0, &stats) == TIER1_SCHEDULE_OK && tier1_schedule_verify(input, 0), "empty input permits null slices");
}

static void tier1_schedule_test_guards(Tier1ScheduleTest* test, Tier1ScheduleGraph* graph)
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
            tier1_schedule_test_check(test, input, graph);
            tier1_schedule_test_expect(test, tier1_schedule_block(input, graph, output, count, 0) == TIER1_SCHEDULE_OK && tier1_schedule_verify(input, output), "every guarded input/output length through 256");
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

static void tier1_schedule_test_execution(Tier1ScheduleTest* test, Tier1ScheduleGraph* graph)
{
    Tier1ScheduleFixture fixture = { 0 };
    uint16_t order[BUSTER_TIER1_SCHEDULE_FIXTURE_RECORDS];
    const bool ready = tier1_schedule_fixture_open(&fixture, graph, order);
    tier1_schedule_test_expect(test, ready && tier1_schedule_fixture_check(&fixture), "the schedule and both encoders execute 64 independent integer inputs");
    tier1_schedule_fixture_close(&fixture);
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
    Tier1ScheduleGraph* graph = initialized ? malloc(sizeof(*graph)) : 0;
    const bool ready = initialized && graph;
    if (ready)
    {
        tier1_schedule_test_examples(&test, graph);
        tier1_schedule_test_guards(&test, graph);
        tier1_schedule_test_execution(&test, graph);
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
            tier1_schedule_test_check(&test, input, graph);
        }
    }
    else
    {
        fprintf(stderr, "test-tier1-schedule: invalid arguments, initialization or allocation failure\n");
    }
    free(graph);
    printf("Tier 1 scheduling: %" PRIu64 " checks, %" PRIu64 " failures, seed=%u; machine-record fixtures only\n", test.checks, test.failures, test.seed);
    return ready && test.failures == 0 ? 0 : 1;
}
