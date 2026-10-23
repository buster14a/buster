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
    uint64_t emitted_records[3];
    uint64_t unknown_costs[3];
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
    return ready;
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
    Tier1SelectRecords selected = tier1_select_expand(holes);
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
                    Tier1SelectRecords selected = tier1_select_expand(&holes);
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
                Tier1SelectRecords selected = tier1_select_expand(&holes);
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
        const uint32_t width = tier1_select_width(function->type[i]);
        const uint64_t mask = function->type[i] == IR_I1 ? 1 : width == 4 ? UINT32_MAX : UINT64_MAX;
        switch (function->op[i])
        {
        case IR_PARAM:
            values[i] = arguments[left];
            break;
        case IR_CONST:
            values[i] = function->constants[left].low;
            break;
        case IR_COPY:
        case IR_TRUNC:
        case IR_ZEXT:
        case IR_INTTOPTR:
        case IR_PTRTOINT:
            values[i] = values[left];
            break;
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
            memcpy((void*)(uintptr_t)values[left], &values[right], tier1_select_width(function->type[right]));
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
            if (error == TIER1_SELECT_OK && tier1_select_test_load_code(test, code.records, code.count, code.count, true))
            {
                uint32_t unknown = 0;
                for (uint32_t r = 0; r < code.count; r += 1)
                {
                    tier1_select_test_expect(test, code.cost_ids[r] == x86_zen5_id(&code.records[r]), "actual operand case keeps its exact cost id");
                    unknown += code.cost_ids[r] == 0;
                }
                tier1_select_test_expect(test, unknown == code.unknown_costs, "unknown costs stay explicit");
                for (uint32_t sample = 0; sample < 32; sample += 1)
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
            if (manifest)
            {
                printf("# tier1-manifest-end,version=1,plan=%" PRIu64 ",expansion=%" PRIu64 ",wrapper=%" PRIu64 "\n", test.emitted_records[0], test.emitted_records[1], test.emitted_records[2]);
            }
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
