// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Selector tests: the table cover against scalar maximal munch, exact effective-use handoff, target/form validation,
// and native execution against independent C arithmetic. Emitted functions execute in a child, so a bad instruction
// or address is a test failure. No arbitrary external IR or machine code is executed.

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct Tier1SelectTest Tier1SelectTest;
struct Tier1SelectTest
{
    uint64_t checks;
    uint64_t failures;
    uint8_t* executable;
    uint8_t* allowed;
    const uint8_t* native_allowed;
    uint64_t native_skipped;
    uint64_t emitted_records[3];
    uint64_t unknown_costs[3];
    // #221: the records whose forms x86_semantics.c doesn't cover, which the effects' consumers (the scheduler, #172)
    // need of each record Tier 1 emits; and the first such form.
    uint64_t uncovered[3];
    uint16_t uncovered_form;
    uint64_t missing_columns[3][X86_COST_COLUMN_COUNT];
    bool manifest;
};

static void tier1_select_test_expect(Tier1SelectTest* test, bool passed, const char* label)
{
    test->checks += 1;
    if (!passed)
    {
        if (test->failures < 20)
        {
            fprintf(stderr, "test-tier1-select: %s\n", label);
        }
        test->failures += 1;
    }
}

static uint64_t tier1_select_test_random(uint64_t* state)
{
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static uint64_t tier1_select_test_binary(X86Operation operation, uint32_t width, uint64_t a, uint64_t b)
{
    const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
    uint64_t result = 0;
    a &= mask;
    b &= mask;
    switch (operation)
    {
    case X86_OPERATION_ADD:
        result = a + b;
        break;
    case X86_OPERATION_SUB:
        result = a - b;
        break;
    case X86_OPERATION_IMUL:
        result = a * b;
        break;
    case X86_OPERATION_AND:
        result = a & b;
        break;
    case X86_OPERATION_OR:
        result = a | b;
        break;
    case X86_OPERATION_XOR:
        result = a ^ b;
        break;
    case X86_OPERATION_SHL:
        result = a << (b & (width * 8 - 1));
        break;
    case X86_OPERATION_SHR:
        result = a >> (b & (width * 8 - 1));
        break;
    case X86_OPERATION_SAR:
    {
        const uint32_t shift = (uint32_t)b & (width * 8 - 1);
        const uint64_t sign = a >> (width * 8 - 1);
        result = (a >> shift) | (shift != 0 && sign != 0 ? mask << (width * 8 - shift) : 0);
        break;
    }
    default:
        break;
    }
    return result & mask;
}

static bool tier1_select_test_compare(uint32_t condition, uint32_t width, uint64_t a, uint64_t b)
{
    const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
    const uint64_t sign = UINT64_C(1) << (width * 8 - 1);
    a &= mask;
    b &= mask;
    bool result = false;
    switch (condition)
    {
    case 0:
        result = a == b;
        break;
    case 1:
        result = a != b;
        break;
    case 2:
        result = (a ^ sign) < (b ^ sign);
        break;
    case 3:
        result = (a ^ sign) <= (b ^ sign);
        break;
    case 4:
        result = (a ^ sign) > (b ^ sign);
        break;
    case 5:
        result = (a ^ sign) >= (b ^ sign);
        break;
    case 6:
        result = a < b;
        break;
    case 7:
        result = a <= b;
        break;
    case 8:
        result = a > b;
        break;
    case 9:
        result = a >= b;
        break;
    }
    return result;
}

static const char* const tier1_select_test_origins[] = {"plan", "expansion", "wrapper"};
static const char* const tier1_select_test_cost_columns[] = {"gpr_latency", "throughput", "retired_ops", "flags_latency", "load_extra", "executed_uops", "pipes"};

static bool tier1_select_test_load_code(Tier1SelectTest* test, const X86Instruction* records, uint32_t count, uint32_t selected_count, bool plan)
{
    const uint32_t valid = x86_target_validate(test->allowed, records, count);
    tier1_select_test_expect(test, valid == count, "record valid and eligible for x86-64 baseline");
    for (uint32_t i = 0; i < valid; i += 1)
    {
        const uint32_t origin = i >= selected_count ? 2 : plan ? 0 : 1;
        test->emitted_records[origin] += 1;
        const bool covered = x86_effects(records[i].form)->operation != X86_OPERATION_NONE;
        test->uncovered_form = !covered && test->uncovered[0] + test->uncovered[1] + test->uncovered[2] == 0 ? records[i].form : test->uncovered_form;
        test->uncovered[origin] += !covered;
        const uint8_t id = x86_zen5_id(&records[i]);
        test->unknown_costs[origin] += id == 0;
        uint16_t costs[X86_COST_COLUMN_COUNT];
        for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
        {
            x86_zen5_lookup_scalar(&id, 1, (X86CostColumn)column, &costs[column]);
            test->missing_columns[origin][column] += costs[column] == BUSTER_X86_COST_UNKNOWN;
        }
        if (test->manifest)
        {
            const X86Instruction record = records[i];
            printf("%s,%" PRIu64 ",%u,%s,%u,%u,%u,%u,%u,%u,%d,%d,%u", tier1_select_test_origins[origin], test->emitted_records[origin], record.form, x86_form_description(record.form)->mnemonic, record.mode, record.reg, record.rm, record.index, record.vvvv, record.decorations, record.immediate, record.displacement, id);
            for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
            {
                printf(",%u", costs[column]);
            }
            printf("\n");
        }
    }
    bool ready = valid == count && mprotect(test->executable, 4096, PROT_READ | PROT_WRITE) == 0;
    if (ready)
    {
        ready = x86_encode(records, count, test->executable, 4096, 0, 0) > 0;
        ready = ready && mprotect(test->executable, 4096, PROT_READ | PROT_EXEC) == 0;
    }
    tier1_select_test_expect(test, ready, "encode and protect native function");
    if (ready && test->native_allowed != 0 && x86_target_validate(test->native_allowed, records, count) != count)
    {
        test->native_skipped += 1;
        ready = false;
    }
    return ready;
}

// A node's records with its immediate's form chosen, as the emitters' batch chooses it (a batch of one), by both paths,
// which must choose alike.
static Tier1SelectRecords tier1_select_test_expand(Tier1SelectTest* test, const Tier1SelectHoles* holes)
{
    Tier1SelectRecords chosen[2];
    for (uint32_t path = 0; path < 2; path += 1)
    {
        chosen[path] = tier1_select_expand(holes);
        Tier1SelectRequests requests = { .vector = path == 0 };
        tier1_select_requests_add(&requests, &chosen[path], 0, chosen[path].records);
        tier1_select_requests_flush(&requests, chosen[path].records);
    }
    tier1_select_test_expect(test, memcmp(chosen[0].records, chosen[1].records, chosen[0].count * sizeof(X86Instruction)) == 0, "both paths choose an immediate's form alike");
    return chosen[0];
}

// Generated functions have no compiler-emitted UBSan function metadata before their entry point.
static __attribute__((no_sanitize("function"))) uint64_t tier1_select_test_call(Tier1SelectTest* test, uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t (*function)(uint64_t, uint64_t, uint64_t);
    memcpy(&function, &test->executable, sizeof(function));
    return function(a, b, c);
}

static void tier1_select_test_holes(Tier1SelectTest* test, const Tier1SelectHoles* holes, uint64_t* random)
{
    Tier1SelectRecords selected = tier1_select_test_expand(test, holes);
    X86Instruction records[16];
    uint32_t count = selected.count;
    memcpy(records, selected.records, count * sizeof(*records));
    records[count++] = tier1_select_move(8, X86_REGISTER_RAX, holes->destination);
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    if (tier1_select_test_load_code(test, records, count, selected.count, false))
    {
        for (uint32_t sample = 0; sample < 32; sample += 1)
        {
            const uint64_t a = sample < 4 ? (UINT64_MAX >> sample) : tier1_select_test_random(random);
            const uint64_t b = sample < 4 ? (sample == 0 ? UINT64_MAX : 0) : tier1_select_test_random(random);
            const uint64_t c = tier1_select_test_random(random);
            const uint64_t mask = holes->width == 4 ? UINT32_MAX : UINT64_MAX;
            uint64_t expected = 0;
            switch (holes->rule)
            {
            case TIER1_RULE_CONSTANT:
                expected = holes->immediate & mask;
                break;
            case TIER1_RULE_REGISTER:
                expected = tier1_select_test_binary(holes->operation, holes->width, a, b);
                break;
            case TIER1_RULE_IMMEDIATE:
                expected = tier1_select_test_binary(holes->operation, holes->width, a, holes->immediate);
                break;
            case TIER1_RULE_MULTIPLY_SHIFT:
                expected = (a * (UINT64_C(1) << holes->scale)) & mask;
                break;
            case TIER1_RULE_LEA_ADD:
            case TIER1_RULE_LEA_SHIFT:
            case TIER1_RULE_MULTIPLY_LEA:
                expected = (a + (holes->rule == TIER1_RULE_MULTIPLY_LEA ? a : b) * (UINT64_C(1) << holes->scale)) & mask;
                break;
            case TIER1_RULE_COMPARE:
            case TIER1_RULE_COMPARE_IMMEDIATE:
                expected = tier1_select_test_compare(holes->condition, holes->width, a, holes->compare_immediate ? holes->immediate : b);
                break;
            case TIER1_RULE_SELECT:
                expected = ((uint32_t)a != 0 ? b : c) & mask;
                break;
            default:
                tier1_select_test_expect(test, false, "test oracle covers rule");
                break;
            }
            const uint64_t actual = tier1_select_test_call(test, a, b, c);
            if (actual != expected && test->failures < 20)
            {
                fprintf(stderr, "rule=%u op=%u width=%u dst=%u cond=%u imm=%016" PRIx64 " a=%016" PRIx64 " b=%016" PRIx64 " actual=%016" PRIx64 " expected=%016" PRIx64 "\n", holes->rule, holes->operation, holes->width, holes->destination, holes->condition, holes->immediate, a, b, actual, expected);
            }
            tier1_select_test_expect(test, actual == expected, "generated result agrees with C arithmetic");
        }
    }
}

static void tier1_select_test_patterns(Tier1SelectTest* test)
{
    static const uint64_t immediates[] =
    {
        0,
        1,
        127,
        128,
        255,
        256,
        UINT64_MAX - 127,
        UINT64_MAX - 128,
        UINT64_MAX,
        INT32_MAX,
        (uint64_t)(int64_t)INT32_MIN,
        UINT32_MAX,
        UINT64_C(1) << 32,
        UINT64_C(1) << 63,
    };
    static const X86Operation operations[] =
    {
        X86_OPERATION_ADD,
        X86_OPERATION_SUB,
        X86_OPERATION_IMUL,
        X86_OPERATION_AND,
        X86_OPERATION_OR,
        X86_OPERATION_XOR,
    };
    static const uint8_t destinations[] =
    {
        X86_REGISTER_RAX,
        X86_REGISTER_RDI,
        X86_REGISTER_RSI,
        X86_REGISTER_RDX,
        X86_REGISTER_R8,
    };
    uint64_t random = UINT64_C(0xb48e7d2f9439a019);
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(destinations); d += 1)
        {
            Tier1SelectHoles holes =
            {
                .width = (uint8_t)width,
                .destination = destinations[d],
                .left = X86_REGISTER_RDI,
                .right = X86_REGISTER_RSI,
                .third = X86_REGISTER_RDX,
            };
            for (uint32_t op = 0; op < BUSTER_ARRAY_LENGTH(operations); op += 1)
            {
                holes.rule = TIER1_RULE_REGISTER;
                holes.operation = operations[op];
                tier1_select_test_holes(test, &holes, &random);
            }
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(immediates); i += 1)
            {
                holes.immediate = width == 4 ? (uint32_t)immediates[i] : immediates[i];
                holes.rule = TIER1_RULE_CONSTANT;
                tier1_select_test_holes(test, &holes, &random);
                if (tier1_select_immediate_fits(width, holes.immediate))
                {
                    for (uint32_t op = 0; op < BUSTER_ARRAY_LENGTH(operations); op += 1)
                    {
                        holes.rule = TIER1_RULE_IMMEDIATE;
                        holes.operation = operations[op];
                        tier1_select_test_holes(test, &holes, &random);
                    }
                    for (uint32_t condition = 0; condition < 10; condition += 1)
                    {
                        holes.rule = TIER1_RULE_COMPARE_IMMEDIATE;
                        holes.condition = (uint8_t)condition;
                        holes.compare_immediate = true;
                        tier1_select_test_holes(test, &holes, &random);
                    }
                }
            }
            holes.compare_immediate = false;
            for (uint32_t condition = 0; condition < 10; condition += 1)
            {
                holes.rule = TIER1_RULE_COMPARE;
                holes.condition = (uint8_t)condition;
                tier1_select_test_holes(test, &holes, &random);
            }
            holes.rule = TIER1_RULE_SELECT;
            tier1_select_test_holes(test, &holes, &random);
            for (uint32_t scale = 0; scale < 4; scale += 1)
            {
                holes.scale = (uint8_t)scale;
                holes.memory = (X86Instruction)
                {
                    .rm = X86_REGISTER_RDI,
                    .index = X86_REGISTER_RSI,
                    .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale),
                };
                holes.rule = TIER1_RULE_LEA_SHIFT;
                tier1_select_test_holes(test, &holes, &random);
                holes.rule = TIER1_RULE_MULTIPLY_LEA;
                holes.memory.index = X86_REGISTER_RDI;
                tier1_select_test_holes(test, &holes, &random);
            }
            for (uint32_t shift = 0; shift < width * 8; shift += 1)
            {
                holes.rule = TIER1_RULE_MULTIPLY_SHIFT;
                holes.scale = (uint8_t)shift;
                tier1_select_test_holes(test, &holes, &random);
                holes.rule = TIER1_RULE_IMMEDIATE;
                holes.immediate = shift;
                for (uint32_t operation = X86_OPERATION_SHL; operation <= X86_OPERATION_SAR; operation += 1)
                {
                    holes.operation = (X86Operation)operation;
                    tier1_select_test_holes(test, &holes, &random);
                }
            }
        }
    }
}

static void tier1_select_test_memory(Tier1SelectTest* test)
{
    uint8_t bytes[512];
    uint8_t expected[512];
    uint64_t random = UINT64_C(0x4e97a1cbb0f7d329);
    const uint8_t destinations[] =
    {
        X86_REGISTER_RAX,
        X86_REGISTER_RDI,
        X86_REGISTER_RSI,
        X86_REGISTER_RDX,
    };
    for (uint32_t i = 0; i < sizeof(bytes); i += 1)
    {
        bytes[i] = (uint8_t)tier1_select_test_random(&random);
    }
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(destinations); d += 1)
        {
            for (uint32_t scale = 0; scale < 4; scale += 1)
            {
                for (uint32_t mode = 0; mode < 3; mode += 1)
                {
                    Tier1SelectHoles holes =
                    {
                        .rule = mode == 0 ? TIER1_RULE_LOAD_ADDRESS : mode == 1 ? TIER1_RULE_LOAD_OPERAND : TIER1_RULE_STORE_ADDRESS,
                        .operation = X86_OPERATION_SUB,
                        .memory =
                        {
                            .displacement = -17,
                            .rm = X86_REGISTER_RDI,
                            .index = X86_REGISTER_RSI,
                            .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale),
                        },
                        .width = (uint8_t)width,
                        .destination = destinations[d],
                        .left = X86_REGISTER_RDX,
                        .right = X86_REGISTER_RDX,
                    };
                    Tier1SelectRecords selected = tier1_select_test_expand(test, &holes);
                    X86Instruction records[16];
                    uint32_t count = selected.count;
                    memcpy(records, selected.records, count * sizeof(*records));
                    if (mode != 2)
                    {
                        records[count++] = tier1_select_move(8, X86_REGISTER_RAX, holes.destination);
                    }
                    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
                    if (tier1_select_test_load_code(test, records, count, selected.count, false))
                    {
                        for (uint32_t index = 0; index < 16; index += 1)
                        {
                            const uint64_t value = tier1_select_test_random(&random);
                            const uint32_t offset = 128 + (index << scale) - 17;
                            uint64_t loaded = 0;
                            memcpy(&loaded, bytes + offset, width);
                            memcpy(expected, bytes, sizeof(bytes));
                            if (mode == 2)
                            {
                                memcpy(expected + offset, &value, width);
                            }
                            const uint64_t actual = tier1_select_test_call(test, (uintptr_t)(bytes + 128), index, value);
                            const uint64_t wanted = mode == 0 ? loaded : tier1_select_test_binary(X86_OPERATION_SUB, width, value, loaded);
                            tier1_select_test_expect(test, mode == 2 || actual == wanted, "folded memory operand including register/address overlap");
                            tier1_select_test_expect(test, memcmp(expected, bytes, sizeof(bytes)) == 0, "exact memory access width and store position");
                        }
                    }
                }
            }
        }
    }
}

static void tier1_select_test_branches(Tier1SelectTest* test)
{
    uint64_t random = UINT64_C(0xb477306e65f901e7);
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        for (uint32_t condition = 0; condition < 10; condition += 1)
        {
            for (uint32_t immediate = 0; immediate < 2; immediate += 1)
            {
                const Tier1SelectHoles holes =
                {
                    .rule = TIER1_RULE_BRANCH_COMPARE,
                    .width = (uint8_t)width,
                    .left = X86_REGISTER_RDI,
                    .right = X86_REGISTER_RSI,
                    .condition = (uint8_t)condition,
                    .compare_immediate = immediate != 0,
                };
                Tier1SelectRecords selected = tier1_select_test_expand(test, &holes);
                X86Instruction records[7];
                memcpy(records, selected.records, 3 * sizeof(*records));
                records[3] = (X86Instruction){ .immediate = 11, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 };
                records[4] = (X86Instruction){ .form = X86_FORM_RET };
                records[5] = (X86Instruction){ .immediate = 22, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 };
                records[6] = (X86Instruction){ .form = X86_FORM_RET };
                const uint32_t branches[] = {1, 2};
                const uint32_t targets[] = {3, 5};
                uint32_t offsets[7];
                const BranchSizingCheck check = branch_sizing_validate(records, 7, branches, targets, 2);
                tier1_select_test_expect(test, check.error == BRANCH_SIZING_OK, "fused compare/branch sizing input");
                const BranchSizing sizing = size_branches(records, 7, branches, targets, 2, offsets);
                tier1_select_test_expect(test, sizing.error == BRANCH_SIZING_OK, "fused compare/branch sized");
                if (sizing.error == BRANCH_SIZING_OK && tier1_select_test_load_code(test, records, 7, selected.count, false))
                {
                    for (uint32_t sample = 0; sample < 64; sample += 1)
                    {
                        const uint64_t a = sample < 2 ? sample : tier1_select_test_random(&random);
                        const uint64_t b = sample < 2 ? 0 : tier1_select_test_random(&random);
                        const uint64_t expected = tier1_select_test_compare(condition, width, a, immediate != 0 ? 0 : b) ? 11 : 22;
                        tier1_select_test_expect(test, tier1_select_test_call(test, a, b, 0) == expected, "fused compare and branch executes with exact flags");
                    }
                }
            }
        }
    }
}

static void tier1_select_test_node(IrFunction* function, uint32_t i, uint8_t op, uint8_t type, uint32_t a, uint32_t b, uint32_t c)
{
    function->op[i] = op;
    function->type[i] = type;
    function->a[i] = a;
    function->b[i] = b;
    function->c[i] = c;
}

// Independent, deliberately direct evaluator for the generated single-block integer fixtures. It never consults a
// selection rule, pattern or physical register. Types, operand IDs and memory accesses come only from the original IR.
static uint64_t tier1_select_test_interpret(const IrFunction* function, uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t values[512] = {0};
    const uint64_t arguments[3] = {a, b, c};
    uint64_t result = 0;
    assert(function->instruction_count <= BUSTER_ARRAY_LENGTH(values));
    for (uint32_t i = 0; i < function->instruction_count; i += 1)
    {
        const uint32_t left = function->a[i];
        const uint32_t right = function->b[i];
        const uint32_t third = function->c[i];
        const uint32_t bits = ir_type_bits[function->type[i]];
        const uint32_t width = (bits + 7) / 8;
        const uint64_t mask = bits < 64 ? (UINT64_C(1) << bits) - 1 : UINT64_MAX;
        switch (function->op[i])
        {
        case IR_PARAM:
            values[i] = arguments[left];
            break;
        case IR_CONST:
            values[i] = function->constants[left].low;
            break;
        case IR_COPY:
        case IR_BITCAST:
        case IR_TRUNC:
        case IR_ZEXT:
        case IR_INTTOPTR:
        case IR_PTRTOINT:
            values[i] = values[left];
            break;
        case IR_SEXT:
        {
            const uint32_t source_bits = ir_type_bits[function->type[left]];
            const uint64_t sign = UINT64_C(1) << (source_bits - 1);
            values[i] = (values[left] ^ sign) - sign;
            break;
        }
        case IR_ADD:
            values[i] = values[left] + values[right];
            break;
        case IR_SUB:
            values[i] = values[left] - values[right];
            break;
        case IR_MUL:
            values[i] = values[left] * values[right];
            break;
        case IR_AND:
            values[i] = values[left] & values[right];
            break;
        case IR_OR:
            values[i] = values[left] | values[right];
            break;
        case IR_XOR:
            values[i] = values[left] ^ values[right];
            break;
        case IR_SHL:
            values[i] = values[left] << values[right];
            break;
        case IR_NEG:
            values[i] = 0 - values[left];
            break;
        case IR_NOT:
            values[i] = ~values[left];
            break;
        case IR_ICMP:
            values[i] = tier1_select_test_compare(third, tier1_select_width(function->type[left]), values[left], values[right]);
            break;
        case IR_SELECT:
            values[i] = values[left] != 0 ? values[right] : values[third];
            break;
        case IR_ADDRESS:
            values[i] = values[left] + (right == BUSTER_IR_NONE ? 0 : values[right] * function->addresses[third].scale) + (uint64_t)function->addresses[third].offset;
            break;
        case IR_LOAD:
            memcpy(&values[i], (const void*)(uintptr_t)values[left], width);
            break;
        case IR_STORE:
            memcpy((void*)(uintptr_t)values[left], &values[right], (ir_type_bits[function->type[right]] + 7) / 8);
            break;
        case IR_RET:
            result = left == BUSTER_IR_NONE ? 0 : values[left];
            break;
        default:
            assert(false);
            break;
        }
        values[i] &= mask;
    }
    return result;
}

static void tier1_select_test_plan_execute(Tier1SelectTest* test, const IrFunction* function, const uint8_t* registers, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    Tier1SelectPlan vector;
    Tier1SelectPlan scalar;
    uint32_t node;
    const Tier1SelectError ve = tier1_select_plan(function, 0, output, scratch, true, &vector, &node);
    const Tier1SelectError se = tier1_select_plan(function, 0, output, scratch, false, &scalar, &node);
    tier1_select_test_expect(test, ve == TIER1_SELECT_OK && se == TIER1_SELECT_OK, "real IR produces both selection covers");
    if (ve == TIER1_SELECT_OK && se == TIER1_SELECT_OK)
    {
        tier1_select_test_expect(test, vector.estimated_cost <= scalar.estimated_cost, "bottom-up cover never costs more than maximal munch");
        const Tier1SelectPlan* plans[] = {&vector, &scalar};
        uint64_t random = UINT64_C(0x7a87b0c525ee96fb);
        for (uint32_t path = 0; path < 2; path += 1)
        {
            Tier1SelectCode code;
            const Tier1SelectError error = tier1_select_emit(plans[path], registers, test->allowed, output, &code, &node);
            tier1_select_test_expect(test, error == TIER1_SELECT_OK, "real plan emits allocated records");
            if (error == TIER1_SELECT_OK)
            {
                uint32_t unknown = 0;
                for (uint32_t r = 0; r < code.count; r += 1)
                {
                    tier1_select_test_expect(test, code.cost_ids[r] == x86_zen5_id(&code.records[r]), "actual operand case keeps its exact cost id");
                    unknown += code.cost_ids[r] == 0;
                }
                tier1_select_test_expect(test, unknown == code.unknown_costs, "unknown costs stay explicit");
                const bool executable = tier1_select_test_load_code(test, code.records, code.count, code.count, true);
                for (uint32_t sample = 0; executable && sample < 32; sample += 1)
                {
                    const uint64_t a = tier1_select_test_random(&random);
                    const uint64_t b = tier1_select_test_random(&random);
                    const uint64_t c = tier1_select_test_random(&random);
                    const uint64_t wanted = tier1_select_test_interpret(function, a, b, c);
                    const uint64_t actual = tier1_select_test_call(test, a, b, c);
                    tier1_select_test_expect(test, actual == wanted, "both selected functions execute like original IR");
                }
            }
        }
    }
    arena_pop_to(output, mark);
}

static void tier1_select_test_plans(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    const uint32_t lengths[] = {0, 1, 15, 16, 17, 31, 32, 33, 63};
    uint64_t random = UINT64_C(0xf0071bd0c8e47935);
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        for (uint32_t length = 0; length < BUSTER_ARRAY_LENGTH(lengths); length += 1)
        {
            for (uint32_t shape = 0; shape < 2; shape += 1)
            {
                const uint32_t operations = lengths[length];
                const uint32_t n = operations * 2 + 2;
                const uint8_t type = width == 4 ? IR_I32 : IR_I64;
                const IrSizes sizes =
                {
                    .instruction_count = n,
                    .block_count = 1,
                    .constant_count = operations,
                    .parameter_count = 1,
                };
                IrFunction function;
                const IrError created = ir_create(&input, &sizes, &function);
                tier1_select_test_expect(test, created == IR_OK, "create generated IR fixture");
                if (created == IR_OK)
                {
                    function.block_offsets[1] = n;
                    function.result_type = type;
                    function.parameter_types[0] = type;
                    uint8_t registers[512];
                    memset(registers, X86_REGISTER_RAX, sizeof(registers));
                    registers[0] = X86_REGISTER_RDI;
                    tier1_select_test_node(&function, 0, IR_PARAM, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    uint32_t previous = 0;
                    for (uint32_t op = 0; op < operations; op += 1)
                    {
                        const uint32_t constant = shape == 0 ? op * 2 + 1 : op + 1;
                        const uint32_t arithmetic = shape == 0 ? op * 2 + 2 : operations + op + 1;
                        const uint8_t opcode = shape == 1 ? IR_MUL : (const uint8_t[]){IR_ADD, IR_SUB, IR_MUL, IR_AND, IR_OR, IR_XOR, IR_SHL}[op % 7];
                        uint64_t value = shape == 1 ? UINT64_C(1) << (op % (width * 8)) : tier1_select_test_random(&random);
                        value = opcode == IR_SHL ? value % (width * 8) : value;
                        value = width == 4 ? (uint32_t)value : value;
                        function.constants[op].low = value;
                        tier1_select_test_node(&function, constant, IR_CONST, type, op, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        tier1_select_test_node(&function, arithmetic, opcode, type, shape == 0 ? previous : 0, constant, BUSTER_IR_NONE);
                        registers[constant] = X86_REGISTER_RCX;
                        previous = arithmetic;
                    }
                    tier1_select_test_node(&function, n - 1, IR_RET, IR_VOID, previous, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_plan_execute(test, &function, registers, &output, &scratch);
                }
                arena_pop_to(&input, 0);
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_memory_plans(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    for (uint32_t variant = 0; variant < 4; variant += 1)
    {
        const bool extra = variant == 1 || variant == 3;
        const uint32_t subtract = extra ? 6 : 5;
        const uint32_t count = subtract + 2;
        const IrSizes sizes =
        {
            .instruction_count = count,
            .block_count = 1,
            .address_count = 1,
            .parameter_count = 3,
        };
        IrFunction function;
        if (ir_create(&input, &sizes, &function) == IR_OK)
        {
            function.result_type = IR_I64;
            function.block_offsets[1] = count;
            function.parameter_types[0] = IR_PTR;
            function.parameter_types[1] = IR_I64;
            function.parameter_types[2] = IR_I64;
            function.addresses[0].scale = 1;
            tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, IR_PARAM, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 3, IR_ADDRESS, IR_PTR, 0, 1, 0);
            tier1_select_test_node(&function, 4, IR_LOAD, IR_I64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function.flags[4] = variant == 2 ? BUSTER_IR_VOLATILE : 0;
            if (variant == 1)
            {
                tier1_select_test_node(&function, 5, IR_STORE, IR_VOID, 0, 2, BUSTER_IR_NONE);
            }
            if (variant == 3)
            {
                tier1_select_test_node(&function, 5, IR_COPY, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            tier1_select_test_node(&function, subtract, IR_SUB, IR_I64, 2, 4, BUSTER_IR_NONE);
            tier1_select_test_node(&function, count - 1, IR_RET, IR_VOID, subtract, BUSTER_IR_NONE, BUSTER_IR_NONE);
            uint8_t registers[8] =
            {
                X86_REGISTER_RDI,
                X86_REGISTER_RSI,
                X86_REGISTER_RDX,
                X86_REGISTER_R8,
                X86_REGISTER_RCX,
                X86_REGISTER_R9,
                X86_REGISTER_RAX,
                X86_REGISTER_RAX,
            };
            registers[subtract] = X86_REGISTER_RAX;
            for (uint32_t path = 0; path < 2; path += 1)
            {
                Tier1SelectPlan plan;
                uint32_t error_node;
                const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &error_node);
                tier1_select_test_expect(test, error == TIER1_SELECT_OK, "memory fixture selects");
                if (error == TIER1_SELECT_OK)
                {
                    tier1_select_test_expect(test, (plan.covered[4] != 0) == (variant == 0), "load fusion requires adjacent single use and nonvolatile access");
                    if (variant == 0)
                    {
                        bool uses[5] = {0};
                        for (uint32_t u = plan.effective_use_offsets[subtract]; u < plan.effective_use_offsets[subtract + 1]; u += 1)
                        {
                            uses[plan.effective_use_values[u]] = true;
                        }
                        tier1_select_test_expect(test, uses[0] && uses[1] && uses[2] && !uses[3] && !uses[4], "folded base and index live through consuming arithmetic");
                    }
                    Tier1SelectCode code;
                    const Tier1SelectError emitted = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &error_node);
                    tier1_select_test_expect(test, emitted == TIER1_SELECT_OK, "memory fixture emits");
                    if (emitted == TIER1_SELECT_OK && tier1_select_test_load_code(test, code.records, code.count, code.count, true))
                    {
                        uint64_t actual_memory[4] = {73, 19, 51, 99};
                        uint64_t expected_memory[4];
                        memcpy(expected_memory, actual_memory, sizeof(actual_memory));
                        const uint64_t expected = tier1_select_test_interpret(&function, (uintptr_t)expected_memory, 0, 112);
                        const uint64_t actual = tier1_select_test_call(test, (uintptr_t)actual_memory, 0, 112);
                        tier1_select_test_expect(test, expected == actual && memcmp(actual_memory, expected_memory, sizeof(actual_memory)) == 0, "load/store ordering agrees with original IR");
                    }
                }
                arena_pop_to(&output, 0);
            }
        }
        else
        {
            tier1_select_test_expect(test, false, "create memory fixture");
        }
        arena_pop_to(&input, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_errors(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    IrFunction function;
    const IrSizes sizes =
    {
        .instruction_count = 4,
        .block_count = 1,
        .constant_count = 1,
        .parameter_count = 1,
    };
    if (ir_create(&input, &sizes, &function) == IR_OK)
    {
        function.result_type = IR_I64;
        function.block_offsets[1] = 4;
        function.parameter_types[0] = IR_I64;
        function.constants[0].low = 8;
        tier1_select_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 2, IR_MUL, IR_I64, 0, 1, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t path = 0; path < 2; path += 1)
        {
            Tier1SelectPlan plan;
            memset(&plan, 0xa5, sizeof(plan));
            const Tier1SelectPlan before = plan;
            uint32_t node;
            function.op[2] = IR_UDIV;
            Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
            tier1_select_test_expect(test, error == TIER1_SELECT_ERROR_UNSUPPORTED && node == 2, "unsupported division stays explicit");
            tier1_select_test_expect(test, output.length == 0 && scratch.length == 0 && memcmp(&plan, &before, sizeof(plan)) == 0, "failed planning is transactional");
            function.op[2] = IR_MUL;
            function.a[2] = 2;
            error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
            tier1_select_test_expect(test, error == TIER1_SELECT_ERROR_INVALID && node == 2, "semantic invalidity is not an unsupported-pattern fallback");
            function.a[2] = 0;
            Arena tiny = {0};
            error = tier1_select_plan(&function, 0, &tiny, &scratch, path == 0, &plan, &node);
            tier1_select_test_expect(test, error == TIER1_SELECT_ERROR_MEMORY && tiny.length == 0 && scratch.length == 0, "plan capacity exhaustion restores both arenas");
            error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
            tier1_select_test_expect(test, error == TIER1_SELECT_OK, "valid fixture selects after failures");
            if (error == TIER1_SELECT_OK)
            {
                const uint64_t mark = output.length;
                uint8_t registers[4] = {X86_REGISTER_RDI, X86_REGISTER_RCX, UINT8_MAX, 0};
                Tier1SelectCode code;
                memset(&code, 0xa5, sizeof(code));
                const Tier1SelectCode previous = code;
                error = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &node);
                tier1_select_test_expect(test, error == TIER1_SELECT_ERROR_ALLOCATION && output.length == mark && memcmp(&code, &previous, sizeof(code)) == 0, "unlowered spill rejected transactionally");
                registers[2] = X86_REGISTER_R11;
                error = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &node);
                tier1_select_test_expect(test, error == TIER1_SELECT_ERROR_ALLOCATION, "reserved scratch register rejected");
                registers[2] = X86_REGISTER_RAX;
                const uint8_t allowed_return = test->allowed[X86_FORM_RET];
                test->allowed[X86_FORM_RET] = 0;
                error = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &node);
                test->allowed[X86_FORM_RET] = allowed_return;
                tier1_select_test_expect(test, error == TIER1_SELECT_ERROR_TARGET && output.length == mark, "target form eligibility fails closed");
                registers[0] = X86_REGISTER_RSI;
                error = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &node);
                tier1_select_test_expect(test, error == TIER1_SELECT_ERROR_ALLOCATION && node == 0, "incoming argument register cannot be guessed");
            }
            arena_pop_to(&output, 0);
        }
    }
    else
    {
        tier1_select_test_expect(test, false, "create failure fixture");
    }
    arena_pop_to(&input, 0);
    if (ir_create(&input, &(IrSizes){0}, &function) == IR_OK)
    {
        Tier1SelectPlan plan;
        Tier1SelectCode code;
        uint32_t node;
        const Tier1SelectError planned = tier1_select_plan(&function, 0, &output, &scratch, true, &plan, &node);
        const Tier1SelectError emitted = planned == TIER1_SELECT_OK ? tier1_select_emit(&plan, 0, test->allowed, &output, &code, &node) : planned;
        tier1_select_test_expect(test, emitted == TIER1_SELECT_OK && code.count == 0 && code.branch_count == 0 && plan.effective_use_count == 0, "empty function accepts null value columns");
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_guarded(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    const uint32_t counts[] = {1, 15, 16, 17, 31, 32, 33};
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(counts); t += 1)
    {
        const uint32_t n = counts[t];
        IrFunction function;
        const IrSizes sizes =
        {
            .instruction_count = n,
            .block_count = 1,
            .constant_count = n - 1,
        };
        if (ir_create(&input, &sizes, &function) == IR_OK)
        {
            function.block_offsets[1] = n;
            function.result_type = n == 1 ? IR_VOID : IR_I64;
            for (uint32_t i = 0; i + 1 < n; i += 1)
            {
                function.constants[i].low = i * 17;
                tier1_select_test_node(&function, i, IR_CONST, IR_I64, i, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            tier1_select_test_node(&function, n - 1, IR_RET, IR_VOID, n == 1 ? BUSTER_IR_NONE : n - 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            uint8_t* mapping = mmap(0, 14 * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mapping != MAP_FAILED)
            {
                void* source[7] = {function.op, function.type, function.flags, function.alignment_log2, function.a, function.b, function.c};
                void* columns[7];
                bool protected = true;
                for (uint32_t column = 0; column < 7; column += 1)
                {
                    const uint32_t bytes = n * (column < 4 ? 1 : 4);
                    columns[column] = mapping + (column * 2 + 1) * 4096 - bytes;
                    memcpy(columns[column], source[column], bytes);
                    protected &= mprotect(mapping + (column * 2 + 1) * 4096, 4096, PROT_NONE) == 0;
                }
                function.op = columns[0];
                function.type = columns[1];
                function.flags = columns[2];
                function.alignment_log2 = columns[3];
                function.a = columns[4];
                function.b = columns[5];
                function.c = columns[6];
                tier1_select_test_expect(test, protected, "guard pages protected");
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    Tier1SelectPlan plan;
                    uint32_t node;
                    const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
                    tier1_select_test_expect(test, error == TIER1_SELECT_OK, "exact unpadded IR columns at guard pages");
                    arena_pop_to(&output, 0);
                }
                munmap(mapping, 14 * 4096);
            }
            else
            {
                tier1_select_test_expect(test, false, "guard mapping");
            }
        }
        arena_pop_to(&input, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_calls(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    const uint8_t result_types[] =
    {
        IR_VOID,
        IR_I32,
        IR_I64,
        IR_PTR,
    };
    const uint8_t parameter_types[] =
    {
        IR_I32,
        IR_I64,
        IR_PTR,
        IR_I32,
        IR_I64,
        IR_PTR,
    };
    const uint32_t ordered[] =
    {
        5,
        0,
        1,
        1,
        4,
        2,
        0,
    };
    for (uint32_t type = 0; type < BUSTER_ARRAY_LENGTH(result_types); type += 1)
    {
        for (uint32_t arguments = 0; arguments <= BUSTER_TIER1_SELECT_CALL_ARGUMENTS; arguments += 1)
        {
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                const uint32_t calls = mode == 0 ? 1 : 17;
                const uint32_t count = 6 + calls + 1;
                const IrSizes sizes =
                {
                    .instruction_count = count,
                    .block_count = 1,
                    .extra_count = 7,
                    .call_count = 1,
                    .call_type_count = 7,
                    .parameter_count = 6,
                };
                IrFunction function;
                const bool created = ir_create(&input, &sizes, &function) == IR_OK;
                tier1_select_test_expect(test, created, "create direct-call selection fixture");
                if (created)
                {
                    function.result_type = result_types[type];
                    function.symbol_count = 2;
                    function.block_offsets[1] = count;
                    memcpy(function.parameter_types, parameter_types, sizeof(parameter_types));
                    for (uint32_t p = 0; p < 6; p += 1)
                    {
                        tier1_select_test_node(&function, p, IR_PARAM, parameter_types[p], p, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    }
                    for (uint32_t p = 0; p < 7; p += 1)
                    {
                        function.extra[p] = ordered[p];
                        function.call_types[p] = parameter_types[ordered[p]];
                    }
                    function.calls[0] = (IrCall)
                    {
                        .argument_count = arguments,
                        .parameter_count = arguments,
                        .result_type = result_types[type],
                    };
                    for (uint32_t c = 0; c < calls; c += 1)
                    {
                        tier1_select_test_node(&function, 6 + c, IR_CALL, result_types[type], 1, 0, BUSTER_IR_NONE);
                    }
                    tier1_select_test_node(&function, count - 1, IR_RET, IR_VOID, result_types[type] == IR_VOID ? BUSTER_IR_NONE : count - 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    Tier1SelectPlan plans[2];
                    bool planned = true;
                    for (uint32_t path = 0; path < 2; path += 1)
                    {
                        uint32_t failed = 0;
                        const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path != 0, &plans[path], &failed);
                        tier1_select_test_expect(test, error == TIER1_SELECT_OK && failed == BUSTER_IR_NONE && scratch.length == 0, "bounded direct call selects on both paths");
                        planned &= error == TIER1_SELECT_OK;
                    }
                    if (planned)
                    {
                        const uint32_t expected_uses = calls * arguments + (result_types[type] != IR_VOID);
                        tier1_select_test_expect(test, plans[0].effective_use_count == expected_uses && plans[1].effective_use_count == expected_uses, "call CSR capacity covers more than three uses per instruction");
                        tier1_select_test_expect(test, memcmp(plans[0].effective_use_offsets, plans[1].effective_use_offsets, (uint64_t)(count + 1) * 4) == 0 && (expected_uses == 0 || memcmp(plans[0].effective_use_values, plans[1].effective_use_values, (uint64_t)expected_uses * 4) == 0), "scalar/vector call use CSR agrees exactly");
                        for (uint32_t path = 0; path < 2; path += 1)
                        {
                            const Tier1SelectPlan* plan = &plans[path];
                            for (uint32_t c = 0; c < calls; c += 1)
                            {
                                const uint32_t node = 6 + c;
                                const Tier1SelectPattern* pattern = &plan->patterns[node];
                                tier1_select_test_expect(test, pattern->rule == TIER1_RULE_CALL && pattern->immediate == 1 && pattern->width == tier1_select_width(result_types[type]) && pattern->fold_count == 0 && plan->covered[node] == 0, "CALL pattern publishes symbol and result width without folding");
                                for (uint32_t p = 0; p < 3; p += 1)
                                {
                                    tier1_select_test_expect(test, pattern->values[p] == BUSTER_IR_NONE, "CALL ordinary value slots remain absent");
                                }
                                const uint32_t first = plan->effective_use_offsets[node];
                                tier1_select_test_expect(test, plan->effective_use_offsets[node + 1] - first == arguments, "one effective use per ordered call argument");
                                for (uint32_t p = 0; p < arguments; p += 1)
                                {
                                    tier1_select_test_expect(test, plan->effective_use_values[first + p] == ordered[p], "call argument order and duplicate uses are retained");
                                }
                            }
                            uint8_t registers[24];
                            memset(registers, UINT8_MAX, sizeof(registers));
                            Tier1SelectCode code;
                            memset(&code, 0xa5, sizeof(code));
                            const Tier1SelectCode before = code;
                            const uint64_t mark = output.length;
                            uint32_t failed = 0;
                            const Tier1SelectError error = tier1_select_emit(plan, registers, test->allowed, &output, &code, &failed);
                            tier1_select_test_expect(test, error == TIER1_SELECT_ERROR_UNSUPPORTED && failed == 6 && output.length == mark && memcmp(&code, &before, sizeof(code)) == 0, "legacy emitter refuses call plan transactionally before allocation checks");
                        }
                    }
                    if (calls == 1 && arguments == 6 && result_types[type] == IR_I64)
                    {
                        for (uint32_t refusal = 0; refusal < 7; refusal += 1)
                        {
                            const IrCall saved = function.calls[0];
                            if (refusal == 0)
                            {
                                function.calls[0].variadic = true;
                            }
                            else if (refusal == 1)
                            {
                                function.calls[0].argument_count = 7;
                                function.calls[0].parameter_count = 7;
                            }
                            else if (refusal == 2)
                            {
                                function.op[6] = IR_CALL_INDIRECT;
                                function.a[6] = 2;
                            }
                            else if (refusal == 3)
                            {
                                function.parameter_types[5] = IR_I1;
                                function.type[5] = IR_I1;
                                function.call_types[0] = IR_I1;
                            }
                            else if (refusal == 4)
                            {
                                function.result_type = IR_F64;
                                function.type[6] = IR_F64;
                                function.calls[0].result_type = IR_F64;
                            }
                            else if (refusal == 5)
                            {
                                function.calls[0].result_type = IR_I32;
                            }
                            else
                            {
                                function.calls[0].argument_first = function.extra_count;
                            }
                            for (uint32_t path = 0; path < 2; path += 1)
                            {
                                Tier1SelectPlan rejected;
                                memset(&rejected, 0xa5, sizeof(rejected));
                                const Tier1SelectPlan before = rejected;
                                const uint64_t mark = output.length;
                                uint32_t failed = 0;
                                const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path != 0, &rejected, &failed);
                                const Tier1SelectError expected = refusal < 5 ? TIER1_SELECT_ERROR_UNSUPPORTED : TIER1_SELECT_ERROR_INVALID;
                                tier1_select_test_expect(test, error == expected && (refusal >= 5 || failed == 6), "unsupported call ABI and invalid IR remain distinct");
                                tier1_select_test_expect(test, output.length == mark && scratch.length == 0 && memcmp(&rejected, &before, sizeof(rejected)) == 0, "call refusal preserves result and arena lengths");
                            }
                            function.calls[0] = saved;
                            function.op[6] = IR_CALL;
                            function.a[6] = 1;
                            function.parameter_types[5] = parameter_types[5];
                            function.type[5] = parameter_types[5];
                            function.call_types[0] = parameter_types[5];
                            function.result_type = IR_I64;
                            function.type[6] = IR_I64;
                        }
                    }
                }
                arena_pop_to(&input, 0);
                arena_pop_to(&output, 0);
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_call_barriers(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    for (uint32_t shape = 0; shape < 6; shape += 1)
    {
        const IrSizes sizes =
        {
            .instruction_count = shape == 3 ? 5 : 6,
            .block_count = 1,
            .constant_count = 1,
            .address_count = 1,
            .call_count = 1,
            .parameter_count = shape == 1 || shape == 2 ? 2 : 1,
        };
        IrFunction function;
        const bool created = ir_create(&input, &sizes, &function) == IR_OK;
        tier1_select_test_expect(test, created, "create call barrier fixture");
        if (created)
        {
            function.result_type = IR_I64;
            function.symbol_count = 2;
            function.block_offsets[1] = function.instruction_count;
            function.parameter_types[0] = shape == 1 || shape == 2 ? IR_PTR : IR_I64;
            function.constants[0].low = 3;
            function.addresses[0].scale = 1;
            tier1_select_test_node(&function, 0, IR_PARAM, function.parameter_types[0], 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function.calls[0].result_type = IR_VOID;
            if (shape == 0 || shape == 4)
            {
                const uint32_t constant = shape == 0 ? 1 : 2;
                const uint32_t shift = shape == 0 ? 2 : 3;
                const uint32_t call = shape == 0 ? 3 : 1;
                tier1_select_test_node(&function, constant, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, shift, IR_SHL, IR_I64, 0, constant, BUSTER_IR_NONE);
                tier1_select_test_node(&function, call, IR_CALL, IR_VOID, 1, 0, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, IR_ADD, IR_I64, 0, shift, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else if (shape == 1 || shape == 2)
            {
                function.parameter_types[1] = IR_I64;
                tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, shape == 1 ? IR_LOAD : IR_ADDRESS, shape == 1 ? IR_I64 : IR_PTR, 0, shape == 1 ? BUSTER_IR_NONE : 1, shape == 1 ? BUSTER_IR_NONE : 0);
                tier1_select_test_node(&function, 3, IR_CALL, IR_VOID, 1, 0, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, shape == 1 ? IR_ADD : IR_LOAD, IR_I64, shape == 1 ? 1 : 2, shape == 1 ? 2 : BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else if (shape == 3)
            {
                tier1_select_test_node(&function, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, IR_CALL, IR_VOID, 1, 0, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 3, IR_ADD, IR_I64, 0, 1, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else
            {
                tier1_select_test_node(&function, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, IR_CALL, IR_VOID, 1, 0, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 3, IR_SHL, IR_I64, 0, 1, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, IR_COPY, IR_I64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            for (uint32_t path = 0; path < 2; path += 1)
            {
                Tier1SelectPlan plan;
                uint32_t failed = 0;
                const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path != 0, &plan, &failed);
                tier1_select_test_expect(test, error == TIER1_SELECT_OK, "call-separated patterns remain selectable");
                if (error == TIER1_SELECT_OK)
                {
                    const uint32_t before = shape == 3 || shape == 5 ? 1 : shape == 4 ? 3 : 2;
                    const uint32_t consumer = shape == 3 || shape == 5 ? 3 : 4;
                    tier1_select_test_expect(test, (plan.covered[before] != 0) == (shape != 1), "calls block memory folding while preserving pure and constant folds");
                    if (shape == 1)
                    {
                        bool used = false;
                        for (uint32_t use = plan.effective_use_offsets[consumer]; use < plan.effective_use_offsets[consumer + 1]; use += 1)
                        {
                            used |= plan.effective_use_values[use] == before;
                        }
                        tier1_select_test_expect(test, used, "pre-call load remains a physical use after call");
                    }
                    else if (shape == 5)
                    {
                        tier1_select_test_expect(test, plan.patterns[3].rule == TIER1_RULE_IMMEDIATE && plan.patterns[3].immediate == 3, "constant before call still selects an immediate shift after call");
                    }
                }
                arena_pop_to(&output, 0);
            }
        }
        arena_pop_to(&input, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_conversions(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    const uint8_t types[] =
    {
        IR_I1,
        IR_I8,
        IR_I16,
        IR_I32,
        IR_I64,
    };
    const uint8_t operations[] =
    {
        IR_TRUNC,
        IR_ZEXT,
        IR_SEXT,
    };
    const uint8_t destinations[] =
    {
        X86_REGISTER_RAX,
        X86_REGISTER_RDI,
        X86_REGISTER_RSI,
        X86_REGISTER_R8,
        X86_REGISTER_R10,
    };
    const uint64_t samples[] =
    {
        0,
        1,
        0x7f,
        0x80,
        0xff,
        0x100,
        0x7fff,
        0x8000,
        0xffff,
        0x10000,
        INT32_MAX,
        UINT64_C(0x80000000),
        UINT32_MAX,
        UINT64_C(0x100000000),
        UINT64_MAX,
    };
    uint64_t random = UINT64_C(0xba5e48d301c7602f);
    for (uint32_t operation = 0; operation < BUSTER_ARRAY_LENGTH(operations); operation += 1)
    {
        for (uint32_t from = 0; from < BUSTER_ARRAY_LENGTH(types); from += 1)
        {
            for (uint32_t to = 1; to < BUSTER_ARRAY_LENGTH(types); to += 1)
            {
                const uint32_t op = operations[operation];
                const bool supported = op == IR_TRUNC ? from > to : from < to && (op == IR_ZEXT || from != 0);
                if (supported)
                {
                    const uint32_t converted = from == 0 ? 3 : from == 4 ? 1 : 2;
                    const uint32_t returned = converted + (to != 4);
                    const uint32_t count = returned + 2;
                    const IrSizes sizes =
                    {
                        .instruction_count = count,
                        .block_count = 1,
                        .constant_count = from == 0,
                        .parameter_count = 1,
                    };
                    IrFunction function;
                    if (ir_create(&input, &sizes, &function) == IR_OK)
                    {
                        function.result_type = IR_I64;
                        function.block_offsets[1] = count;
                        function.parameter_types[0] = IR_I64;
                        tier1_select_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        if (from == 0)
                        {
                            tier1_select_test_node(&function, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                            tier1_select_test_node(&function, 2, IR_ICMP, IR_I1, 0, 1, IR_NE);
                        }
                        else if (from != 4)
                        {
                            tier1_select_test_node(&function, 1, IR_TRUNC, types[from], 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        }
                        tier1_select_test_node(&function, converted, (uint8_t)op, types[to], converted - 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        if (to != 4)
                        {
                            tier1_select_test_node(&function, returned, IR_ZEXT, IR_I64, converted, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        }
                        tier1_select_test_node(&function, count - 1, IR_RET, IR_VOID, returned, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        Tier1SelectPlan plans[2];
                        for (uint32_t path = 0; path < 2; path += 1)
                        {
                            uint32_t failed;
                            const Tier1SelectError planned = tier1_select_plan(&function, 0, &output, &scratch, path != 0, &plans[path], &failed);
                            tier1_select_test_expect(test, planned == TIER1_SELECT_OK && scratch.length == 0, "integer conversion selects with restored scratch");
                            if (planned == TIER1_SELECT_OK)
                            {
                                const Tier1SelectPlan* plan = &plans[path];
                                const uint32_t first = plan->effective_use_offsets[converted];
                                tier1_select_test_expect(test, plan->effective_use_offsets[converted + 1] == first + 1 && plan->effective_use_values[first] == converted - 1 && !plan->covered[converted], "conversion preserves its exact selected source use");
                                tier1_select_test_expect(test, plan->patterns[converted].width == ir_type_bits[types[to]] / 8 && plan->patterns[converted].source_width == (from == 0 ? 4 : ir_type_bits[types[from]] / 8), "conversion keeps logical widths separately from register storage");
                                for (uint32_t destination = 0; destination < BUSTER_ARRAY_LENGTH(destinations); destination += 1)
                                {
                                    uint8_t registers[8];
                                    memset(registers, destinations[destination], sizeof(registers));
                                    registers[0] = X86_REGISTER_RDI;
                                    if (from == 0)
                                    {
                                        registers[1] = X86_REGISTER_R9;
                                    }
                                    const uint64_t mark = output.length;
                                    Tier1SelectCode code;
                                    const Tier1SelectError emitted = tier1_select_emit(plan, registers, test->allowed, &output, &code, &failed);
                                    tier1_select_test_expect(test, emitted == TIER1_SELECT_OK, "conversion emits for aliasing and low-byte register assignments");
                                    if (emitted == TIER1_SELECT_OK && tier1_select_test_load_code(test, code.records, code.count, code.count, true))
                                    {
                                        for (uint32_t sample = 0; sample < 47; sample += 1)
                                        {
                                            const uint64_t value = sample < BUSTER_ARRAY_LENGTH(samples) ? samples[sample] : tier1_select_test_random(&random);
                                            const uint64_t expected = tier1_select_test_interpret(&function, value, 0, 0);
                                            const uint64_t actual = tier1_select_test_call(test, value, 0, 0);
                                            tier1_select_test_expect(test, actual == expected, "conversion and canonical high bits match independent IR arithmetic");
                                        }
                                    }
                                    arena_pop_to(&output, mark);
                                }
                            }
                        }
                    }
                    else
                    {
                        tier1_select_test_expect(test, false, "create integer conversion fixture");
                    }
                    arena_pop_to(&input, 0);
                    arena_pop_to(&output, 0);
                }
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_narrow_memory(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* guarded = mmap(0, (uint64_t)page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = guarded != MAP_FAILED && mprotect(guarded, (uint64_t)page, PROT_READ | PROT_WRITE) == 0;
    tier1_select_test_expect(test, mapped, "allocate guard page for exact narrow accesses");
    for (uint32_t bytes = 1; mapped && bytes <= 2; bytes += 1)
    {
        for (uint32_t variant = 0; variant < 4; variant += 1)
        {
            const bool sign = (variant & 1) != 0;
            const uint8_t type = bytes == 1 ? IR_I8 : IR_I16;
            const IrSizes sizes =
            {
                .instruction_count = 9,
                .block_count = 1,
                .address_count = 1,
                .parameter_count = 2,
            };
            IrFunction function;
            if (ir_create(&input, &sizes, &function) == IR_OK)
            {
                function.result_type = IR_I64;
                function.block_offsets[1] = 9;
                function.parameter_types[0] = IR_PTR;
                function.parameter_types[1] = IR_I64;
                function.addresses[0].scale = 1;
                function.addresses[0].offset = 1;
                tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, IR_ADDRESS, IR_PTR, 0, BUSTER_IR_NONE, 0);
                tier1_select_test_node(&function, 3, IR_TRUNC, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, IR_STORE, IR_VOID, 2, 3, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 5, IR_ADDRESS, IR_PTR, 0, BUSTER_IR_NONE, 0);
                tier1_select_test_node(&function, 6, IR_LOAD, type, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 7, sign ? IR_SEXT : IR_ZEXT, IR_I64, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 8, IR_RET, IR_VOID, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.flags[4] = variant >= 2 ? BUSTER_IR_VOLATILE : 0;
                function.flags[6] = function.flags[4];
                const uint8_t registers[9] =
                {
                    X86_REGISTER_RDI,
                    X86_REGISTER_RSI,
                    X86_REGISTER_R8,
                    X86_REGISTER_RSI,
                    0,
                    X86_REGISTER_R8,
                    X86_REGISTER_RAX,
                    X86_REGISTER_RAX,
                    0,
                };
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    Tier1SelectPlan plan;
                    uint32_t failed;
                    const Tier1SelectError planned = tier1_select_plan(&function, 0, &output, &scratch, path != 0, &plan, &failed);
                    tier1_select_test_expect(test, planned == TIER1_SELECT_OK, "narrow volatile and ordinary memory selects");
                    if (planned == TIER1_SELECT_OK)
                    {
                        tier1_select_test_expect(test, plan.patterns[4].width == bytes && plan.patterns[6].width == bytes && plan.covered[2] && plan.covered[5] && !plan.covered[4] && !plan.covered[6], "folded address retains exact narrow access width and memory operation");
                        Tier1SelectCode code;
                        const Tier1SelectError emitted = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &failed);
                        tier1_select_test_expect(test, emitted == TIER1_SELECT_OK, "narrow memory emits baseline forms");
                        if (emitted == TIER1_SELECT_OK && tier1_select_test_load_code(test, code.records, code.count, code.count, true))
                        {
                            for (uint32_t sample = 0; sample < 32; sample += 1)
                            {
                                const uint64_t value = sample == 0 ? UINT64_MAX : sample == 1 ? UINT64_C(1) << (bytes * 8 - 1) : sample * UINT64_C(0x81db07e35);
                                uint8_t expected[16];
                                uint8_t actual[16];
                                memset(expected, 0x5a, sizeof(expected));
                                memcpy(actual, expected, sizeof(actual));
                                const uint64_t wanted = tier1_select_test_interpret(&function, (uintptr_t)(expected + 5), value, 0);
                                const uint64_t result = tier1_select_test_call(test, (uintptr_t)(actual + 5), value, 0);
                                tier1_select_test_expect(test, wanted == result && memcmp(expected, actual, sizeof(actual)) == 0, "narrow store changes only the declared bytes and signed load result agrees");
                                memset(guarded + page - 16, 0x5a, 16);
                                const uint64_t boundary = tier1_select_test_call(test, (uintptr_t)(guarded + page - bytes - 1), value, 0);
                                tier1_select_test_expect(test, boundary == wanted && guarded[page - bytes - 1] == 0x5a, "narrow load/store at guard boundary accesses no wider bytes");
                            }
                        }
                    }
                    arena_pop_to(&output, 0);
                }
            }
            else
            {
                tier1_select_test_expect(test, false, "create narrow memory fixture");
            }
            arena_pop_to(&input, 0);
        }
    }
    if (guarded != MAP_FAILED)
    {
        munmap(guarded, (uint64_t)page * 2);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_narrow_values(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    for (uint32_t bytes = 1; bytes <= 2; bytes += 1)
    {
        for (uint32_t variant = 0; variant < 4; variant += 1)
        {
            const uint8_t type = bytes == 1 ? IR_I8 : IR_I16;
            const IrSizes sizes =
            {
                .instruction_count = 8,
                .block_count = 1,
                .constant_count = 2,
                .parameter_count = 1,
            };
            IrFunction function;
            if (ir_create(&input, &sizes, &function) == IR_OK)
            {
                function.result_type = IR_I64;
                function.block_offsets[1] = 8;
                function.parameter_types[0] = IR_I64;
                function.constants[0].low = (UINT64_C(1) << (bytes * 8)) - 1;
                function.constants[1].low = variant & 1;
                tier1_select_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 1, IR_TRUNC, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, IR_CONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 3, IR_CONST, IR_I1, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, IR_SELECT, type, 3, 1, 2);
                tier1_select_test_node(&function, 5, variant >= 2 ? IR_BITCAST : IR_COPY, type, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 6, IR_ZEXT, IR_I64, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
                const uint8_t registers[8] =
                {
                    X86_REGISTER_RDI,
                    X86_REGISTER_RSI,
                    X86_REGISTER_RDX,
                    X86_REGISTER_RCX,
                    variant >= 2 ? X86_REGISTER_RSI : X86_REGISTER_RDX,
                    X86_REGISTER_R8,
                    X86_REGISTER_RAX,
                    0,
                };
                tier1_select_test_plan_execute(test, &function, registers, &output, &scratch);
            }
            else
            {
                tier1_select_test_expect(test, false, "create canonical narrow value fixture");
            }
            arena_pop_to(&input, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_narrow_refusals(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    for (uint32_t bytes = 1; bytes <= 2; bytes += 1)
    {
        for (uint32_t variant = 0; variant < 12; variant += 1)
        {
            const uint8_t type = bytes == 1 ? IR_I8 : IR_I16;
            const bool parameter = variant == 4 || variant == 9 || variant == 10;
            const IrSizes sizes =
            {
                .instruction_count = 4,
                .block_count = 1,
                .extra_count = variant == 6,
                .constant_count = 2,
                .call_count = variant == 6,
                .call_type_count = variant == 6,
                .parameter_count = parameter,
            };
            IrFunction function;
            if (ir_create(&input, &sizes, &function) == IR_OK)
            {
                function.block_offsets[1] = 4;
                function.constants[0].low = 1;
                function.constants[1].low = 1;
                tier1_select_test_node(&function, 0, IR_CONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 1, IR_CONST, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, IR_COPY, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                switch (variant)
                {
                case 0:
                    tier1_select_test_node(&function, 2, IR_ADD, type, 0, 1, BUSTER_IR_NONE);
                    break;
                case 1:
                    tier1_select_test_node(&function, 2, IR_NOT, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    break;
                case 2:
                    tier1_select_test_node(&function, 2, IR_SHL, type, 0, 1, BUSTER_IR_NONE);
                    break;
                case 3:
                    tier1_select_test_node(&function, 2, IR_ICMP, IR_I1, 0, 1, IR_EQ);
                    break;
                case 4:
                    function.parameter_types[0] = type;
                    tier1_select_test_node(&function, 0, IR_PARAM, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    break;
                case 5:
                    function.result_type = type;
                    function.a[3] = 2;
                    break;
                case 6:
                    function.symbol_count = 2;
                    function.extra[0] = 0;
                    function.call_types[0] = type;
                    function.calls[0] = (IrCall)
                    {
                        .argument_count = 1,
                        .parameter_count = 1,
                    };
                    tier1_select_test_node(&function, 2, IR_CALL, IR_VOID, 1, 0, BUSTER_IR_NONE);
                    break;
                case 7:
                    function.type[0] = IR_I1;
                    tier1_select_test_node(&function, 2, IR_SEXT, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    break;
                case 8:
                    tier1_select_test_node(&function, 2, IR_TRUNC, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    break;
                case 9:
                case 10:
                    function.parameter_types[0] = IR_PTR;
                    tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    function.type[1] = IR_I1;
                    tier1_select_test_node(&function, 2, variant == 9 ? IR_LOAD : IR_STORE, variant == 9 ? IR_I1 : IR_VOID, 0, variant == 9 ? BUSTER_IR_NONE : 1, BUSTER_IR_NONE);
                    break;
                case 11:
                    tier1_select_test_node(&function, 2, IR_BITCAST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    break;
                }
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    Tier1SelectPlan plan;
                    memset(&plan, 0xa5, sizeof(plan));
                    const Tier1SelectPlan before = plan;
                    const uint64_t mark = output.length;
                    uint32_t failed;
                    const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path != 0, &plan, &failed);
                    tier1_select_test_expect(test, error == (variant == 11 ? TIER1_SELECT_ERROR_INVALID : TIER1_SELECT_ERROR_UNSUPPORTED), "narrow storage does not admit unsupported arithmetic, ABI, I1 memory or invalid widening bitcast");
                    tier1_select_test_expect(test, output.length == mark && scratch.length == 0 && memcmp(&plan, &before, sizeof(plan)) == 0, "narrow refusal preserves result and arenas");
                }
            }
            else
            {
                tier1_select_test_expect(test, false, "create narrow refusal fixture");
            }
            arena_pop_to(&input, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_unreachable(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    IrFunction function;
    const IrSizes sizes =
    {
        .instruction_count = 1,
        .block_count = 1,
    };
    if (ir_create(&input, &sizes, &function) == IR_OK)
    {
        function.block_offsets[1] = 1;
        tier1_select_test_node(&function, 0, IR_UNREACHABLE, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t path = 0; path < 2; path += 1)
        {
            Tier1SelectPlan plan;
            uint32_t failed;
            const Tier1SelectError planned = tier1_select_plan(&function, 0, &output, &scratch, path != 0, &plan, &failed);
            tier1_select_test_expect(test, planned == TIER1_SELECT_OK && scratch.length == 0, "unreachable terminal selects on both paths");
            if (planned == TIER1_SELECT_OK)
            {
                tier1_select_test_expect(test, plan.patterns[0].rule == TIER1_RULE_UNREACHABLE && plan.patterns[0].width == 0 && plan.effective_use_count == 0 && !plan.covered[0], "unreachable has no value, operands or effective uses");
                const uint8_t registers[1] = {UINT8_MAX};
                Tier1SelectCode code;
                const Tier1SelectError emitted = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &failed);
                tier1_select_test_expect(test, emitted == TIER1_SELECT_OK && code.count == 1 && code.records[0].form == X86_FORM_UD2 && code.branch_count == 0, "legacy terminal expands to one baseline UD2 without a register");
                if (emitted == TIER1_SELECT_OK)
                {
                    uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
                    const int64_t encoded = x86_encode(code.records, code.count, bytes, sizeof(bytes), 0, 0);
                    tier1_select_test_expect(test, encoded == 2 && bytes[0] == 0x0f && bytes[1] == 0x0b, "unreachable encodes exact UD2 bytes without executing the trap");
                    tier1_select_test_load_code(test, code.records, code.count, code.count, true);
                }
                const uint8_t allowed = test->allowed[X86_FORM_UD2];
                test->allowed[X86_FORM_UD2] = 0;
                memset(&code, 0xa5, sizeof(code));
                const Tier1SelectCode before = code;
                const uint64_t mark = output.length;
                const Tier1SelectError denied = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &failed);
                tier1_select_test_expect(test, denied == TIER1_SELECT_ERROR_TARGET && failed == 0 && output.length == mark && memcmp(&code, &before, sizeof(code)) == 0, "target-denied UD2 preserves result and output arena");
                test->allowed[X86_FORM_UD2] = allowed;
            }
            arena_pop_to(&output, 0);
        }
    }
    else
    {
        tier1_select_test_expect(test, false, "create unreachable fixture");
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static int run_tier1_select_tests(int argc, char** argv)
{
    int result = 1;
    const bool manifest = argc == 1 && strcmp(argv[0], "--manifest") == 0;
    if (argc != 0 && !manifest)
    {
        fprintf(stderr, "usage: test-tier1-select [--manifest]\n");
    }
    else if (!x86_encoder_initialize() || !x86_semantics_initialize() || !x86_zen5_initialize())
    {
        fprintf(stderr, "test-tier1-select: initialization failed\n");
    }
    else
    {
        fflush(0);
        const pid_t child = fork();
        if (child == 0)
        {
            Tier1SelectTest test = {0};
            test.manifest = manifest;
            test.executable = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            test.allowed = malloc(X86_FORM_COUNT);
            const X86Target target = { .architecture = X86_ARCH_BASELINE };
            if (test.executable == MAP_FAILED || test.allowed == 0)
            {
                _exit(1);
            }
            x86_target_forms(&target, test.allowed);
            if (manifest)
            {
                printf("origin,record,form,mnemonic,mode,reg,rm,index,vvvv,decorations,immediate,displacement,cost_id,gpr_latency,throughput,retired_ops,flags_latency,load_extra,executed_uops,pipes\n");
            }
            tier1_select_test_patterns(&test);
            tier1_select_test_memory(&test);
            tier1_select_test_branches(&test);
            tier1_select_test_plans(&test);
            tier1_select_test_memory_plans(&test);
            tier1_select_test_errors(&test);
            tier1_select_test_guarded(&test);
            tier1_select_test_calls(&test);
            tier1_select_test_call_barriers(&test);
            tier1_select_test_conversions(&test);
            tier1_select_test_narrow_memory(&test);
            tier1_select_test_narrow_values(&test);
            tier1_select_test_narrow_refusals(&test);
            tier1_select_test_unreachable(&test);
            if (manifest)
            {
                printf("# tier1-manifest-end,version=1,plan=%" PRIu64 ",expansion=%" PRIu64 ",wrapper=%" PRIu64 "\n", test.emitted_records[0], test.emitted_records[1], test.emitted_records[2]);
            }
            // #221: every record the selections emitted has a form x86_semantics.c covers, as test-tier0 checks Tier 0's.
            const uint64_t uncovered = test.uncovered[0] + test.uncovered[1] + test.uncovered[2];
            char coverage[192];
            snprintf(coverage, sizeof(coverage), "every emitted record has a covered form: %" PRIu64 " uncovered, the first %s", uncovered,
                     uncovered != 0 ? x86_form_description(test.uncovered_form)->mnemonic : "none");
            tier1_select_test_expect(&test, uncovered == 0, coverage);
            FILE* report = manifest ? stderr : stdout;
            fprintf(report, "tier1 selection: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
            for (uint32_t origin = 0; origin < 3; origin += 1)
            {
                fprintf(report, "tier1 %s operand cases: %" PRIu64 " records, %" PRIu64 " unknown Zen 5 IDs; missing", tier1_select_test_origins[origin], test.emitted_records[origin], test.unknown_costs[origin]);
                for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
                {
                    fprintf(report, " %s=%" PRIu64, tier1_select_test_cost_columns[column], test.missing_columns[origin][column]);
                }
                fprintf(report, "\n");
            }
            munmap(test.executable, 4096);
            free(test.allowed);
            fflush(0);
            _exit(test.failures != 0);
        }
        int status = 0;
        if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        {
            result = WEXITSTATUS(status);
        }
        else
        {
            fprintf(stderr, "test-tier1-select: native child failed (status %d)\n", status);
        }
    }
    return result;
}
