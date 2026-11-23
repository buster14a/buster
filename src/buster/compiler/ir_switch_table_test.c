// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #3298's switches of constants (ir_switch_table.c), included after ir_optimize_test.c's independent executor. Each
// fixture is written in the IR's text form as the C lowering's jump-table dispatch makes it (a bounds check, the
// entry's 32-bit load, the table's address plus it, a `br_indirect` of the table's labels) with arms that return a
// constant, or give a join's phi one, or are the join itself; it is parsed, converted, verified as complete SSA, and
// executed on the converted table's bytes at every selector value around the table and at the type's extremes, against
// the value the fixture's own description gives (the entry's arm's constant, or the default's), then again after the
// optimizer, which must leave no indirect jump. The table's values are checked against the description too. The
// refused fixtures (an arm with a store, a wide i64 constant, a phi an arm gives a variable, a retyped load read twice,
// an arm that branches elsewhere) must leave the function as it was, borrowed.

#include "ir_switch_table.c"

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

#define BUSTER_IR_SWITCH_TABLE_TEST_SYMBOL 7u
#define BUSTER_IR_SWITCH_TABLE_TEST_FIRST 100u
#define BUSTER_IR_SWITCH_TABLE_TEST_TEXT 16384u

typedef enum IrSwitchTableTestMode
{
    // Every arm returns its constant.
    IR_SWITCH_TABLE_TEST_RETURN,
    // Every arm gives J's phi its constant and branches to J, whose other phi takes the selector from all of them.
    IR_SWITCH_TABLE_TEST_BRANCH,
    // As BRANCH, but position 0 (the default and the holes) is J itself: the dispatch's own constant, and the bounds
    // check's `outside` one.
    IR_SWITCH_TABLE_TEST_JOIN,
} IrSwitchTableTestMode;

typedef enum IrSwitchTableTestRefusal
{
    IR_SWITCH_TABLE_TEST_CONVERTS,
    // Arm 1 stores before it returns.
    IR_SWITCH_TABLE_TEST_EFFECT,
    // Arm 1's phi input is the selector, the others' constants.
    IR_SWITCH_TABLE_TEST_VARIABLE,
    // The load is also read by a dead addition (an i8 or i16 value retypes it).
    IR_SWITCH_TABLE_TEST_READ_TWICE,
    // Arm 1 branches to a block that returns.
    IR_SWITCH_TABLE_TEST_ELSEWHERE,
    // An i64 constant past 32 signed bits (the fixture's values).
    IR_SWITCH_TABLE_TEST_WIDE,
} IrSwitchTableTestRefusal;

typedef struct IrSwitchTableTestCase IrSwitchTableTestCase;
struct IrSwitchTableTestCase
{
    const char* name;
    const uint32_t* entries;
    const int64_t* values;
    int64_t lower;
    int64_t outside;
    uint32_t count;
    uint32_t listed;
    uint8_t selector;
    uint8_t type;
    uint8_t mode;
    uint8_t refusal;
};

static const uint32_t ir_switch_table_test_dense_entries[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const int64_t ir_switch_table_test_dense_values[] = { -1, 3, 7, 1, 9, 2, 8, 4, 6, 5, 0, 11, 13, 17, 19, 23, 29 };
// Holes go to position 0, and two cases share position 2.
static const uint32_t ir_switch_table_test_sparse_entries[] = { 1, 0, 0, 2, 0, 3, 2, 0, 4, 5 };
static const int64_t ir_switch_table_test_bytes[] = { -9, -128, 127, -1, 0, 33 };
static const int64_t ir_switch_table_test_shorts[] = { 9, -32768, 32767, -2, 30000, 7 };
static const int64_t ir_switch_table_test_longs[] = { -77, INT32_MIN, INT32_MAX, -1, 0, 1 };
static const int64_t ir_switch_table_test_wide[] = { 1, 2, INT64_C(1) << 40, 4, 5, 6 };
static const int64_t ir_switch_table_test_ints[] = { 12, 100, -100, 5, 77, -2147483647 - 1 };

static const IrSwitchTableTestCase ir_switch_table_test_cases[] = {
    { "dense returns", ir_switch_table_test_dense_entries, ir_switch_table_test_dense_values, 0, 0, 16, 17, IR_I32, IR_I32, IR_SWITCH_TABLE_TEST_RETURN, IR_SWITCH_TABLE_TEST_CONVERTS },
    { "sparse byte returns below zero", ir_switch_table_test_sparse_entries, ir_switch_table_test_bytes, -7, 0, 10, 6, IR_I32, IR_I8, IR_SWITCH_TABLE_TEST_RETURN, IR_SWITCH_TABLE_TEST_CONVERTS },
    { "i16 returns of an i64 selector", ir_switch_table_test_sparse_entries, ir_switch_table_test_shorts, 100, 0, 10, 6, IR_I64, IR_I16, IR_SWITCH_TABLE_TEST_RETURN, IR_SWITCH_TABLE_TEST_CONVERTS },
    { "i64 returns at 32 bits' limits", ir_switch_table_test_sparse_entries, ir_switch_table_test_longs, -3, 0, 10, 6, IR_I64, IR_I64, IR_SWITCH_TABLE_TEST_RETURN, IR_SWITCH_TABLE_TEST_CONVERTS },
    { "i32 phi", ir_switch_table_test_sparse_entries, ir_switch_table_test_ints, 2, 0, 10, 6, IR_I32, IR_I32, IR_SWITCH_TABLE_TEST_BRANCH, IR_SWITCH_TABLE_TEST_CONVERTS },
    { "i8 phi of an i64 selector", ir_switch_table_test_sparse_entries, ir_switch_table_test_bytes, -1, 0, 10, 6, IR_I64, IR_I8, IR_SWITCH_TABLE_TEST_BRANCH, IR_SWITCH_TABLE_TEST_CONVERTS },
    { "i64 phi of an i32 selector", ir_switch_table_test_dense_entries, ir_switch_table_test_dense_values, 5, 0, 16, 17, IR_I32, IR_I64, IR_SWITCH_TABLE_TEST_BRANCH, IR_SWITCH_TABLE_TEST_CONVERTS },
    { "the join among the arms", ir_switch_table_test_sparse_entries, ir_switch_table_test_ints, 0, 1000, 10, 6, IR_I32, IR_I32, IR_SWITCH_TABLE_TEST_JOIN, IR_SWITCH_TABLE_TEST_CONVERTS },
    { "an i16 join among the arms", ir_switch_table_test_sparse_entries, ir_switch_table_test_shorts, -4, -5, 10, 6, IR_I64, IR_I16, IR_SWITCH_TABLE_TEST_JOIN, IR_SWITCH_TABLE_TEST_CONVERTS },
    { "an arm with a store", ir_switch_table_test_sparse_entries, ir_switch_table_test_ints, 0, 0, 10, 6, IR_I32, IR_I32, IR_SWITCH_TABLE_TEST_RETURN, IR_SWITCH_TABLE_TEST_EFFECT },
    { "an i64 past 32 bits", ir_switch_table_test_sparse_entries, ir_switch_table_test_wide, 0, 0, 10, 6, IR_I64, IR_I64, IR_SWITCH_TABLE_TEST_RETURN, IR_SWITCH_TABLE_TEST_WIDE },
    { "an arm's variable", ir_switch_table_test_sparse_entries, ir_switch_table_test_ints, 0, 0, 10, 6, IR_I32, IR_I32, IR_SWITCH_TABLE_TEST_BRANCH, IR_SWITCH_TABLE_TEST_VARIABLE },
    { "a byte load read twice", ir_switch_table_test_sparse_entries, ir_switch_table_test_bytes, 0, 0, 10, 6, IR_I32, IR_I8, IR_SWITCH_TABLE_TEST_RETURN, IR_SWITCH_TABLE_TEST_READ_TWICE },
    { "an arm branching elsewhere", ir_switch_table_test_sparse_entries, ir_switch_table_test_ints, 0, 0, 10, 6, IR_I32, IR_I32, IR_SWITCH_TABLE_TEST_RETURN, IR_SWITCH_TABLE_TEST_ELSEWHERE },
};

static const char* const ir_switch_table_test_types[IR_TYPE_COUNT] = {
    [IR_I8] = "i8",
    [IR_I16] = "i16",
    [IR_I32] = "i32",
    [IR_I64] = "i64",
};

static uint64_t ir_switch_table_test_mask(uint32_t type)
{
    return UINT64_MAX >> (64 - ir_type_bits[type]);
}

static int64_t ir_switch_table_test_signed(uint64_t value, uint32_t type)
{
    const uint32_t shift = 64 - ir_type_bits[type];
    return (int64_t)(value << shift) >> shift;
}

// A constant row's text: its bits at the type's width, the unused ones zero.
static int ir_switch_table_test_constant(char* text, uint64_t room, uint32_t row, uint32_t type, int64_t value)
{
    return snprintf(text, room, "  %%%u = const.%s 0x0000000000000000%016" PRIx64 "\n", row, ir_switch_table_test_types[type], (uint64_t)value & ir_switch_table_test_mask(type));
}

// The fixture's text, into `text`; false where it doesn't fit.
static bool ir_switch_table_test_text(const IrSwitchTableTestCase* c, char* text, uint64_t room)
{
    const char* s = ir_switch_table_test_types[c->selector];
    const char* t = ir_switch_table_test_types[c->type];
    const bool joined = c->mode == IR_SWITCH_TABLE_TEST_JOIN;
    const bool branches = c->mode != IR_SWITCH_TABLE_TEST_RETURN;
    const bool elsewhere = c->refusal == IR_SWITCH_TABLE_TEST_ELSEWHERE;
    // Blocks: b0 the bounds check, b1 the dispatch, then one per arm position (J's position 0 has none), J, and the
    // elsewhere fixture's returning block.
    const uint32_t arm_first = 2;
    const uint32_t arms = joined ? c->listed - 1 : c->listed;
    const uint32_t join = arm_first + arms;
    const uint32_t other = join + (branches ? 1 : 0);
    uint64_t at = 0;
    uint32_t row = 0;
    uint32_t arm_rows[64] = { 0 };
    uint32_t arm_blocks[64] = { 0 };
    at += (uint64_t)snprintf(text + at, room - at, "function (%s) -> %s {\n", s, t);
    for (uint32_t p = 0; p < c->listed; p += 1)
    {
        arm_blocks[p] = joined && p == 0 ? join : arm_first + (joined ? p - 1 : p);
        at += at < room ? (uint64_t)snprintf(text + at, room - at, "  label #%u b%u\n", p, arm_blocks[p]) : 0;
    }
    at += at < room ? (uint64_t)snprintf(text + at, room - at, "b0:\n  %%0 = param.%s 0\n  %%1 = symbol_addr.ptr @%u\n  %%2 = ptrtoint.i64 %%1\n", s, BUSTER_IR_SWITCH_TABLE_TEST_SYMBOL) : 0;
    at += at < room ? (uint64_t)ir_switch_table_test_constant(text + at, room - at, 3, c->selector, c->lower) : 0;
    at += at < room ? (uint64_t)snprintf(text + at, room - at, "  %%4 = sub.%s %%0, %%3\n", s) : 0;
    at += at < room ? (uint64_t)ir_switch_table_test_constant(text + at, room - at, 5, c->selector, (int64_t)c->count - 1) : 0;
    at += at < room ? (uint64_t)snprintf(text + at, room - at, "  %%6 = icmp.i1 %%4, %%5 ugt\n") : 0;
    row = 7;
    // The bounds check's own constant where J is among the arms.
    const uint32_t outside = row;
    if (joined)
    {
        at += at < room ? (uint64_t)ir_switch_table_test_constant(text + at, room - at, row, c->type, c->outside) : 0;
        row += 1;
    }
    at += at < room ? (uint64_t)snprintf(text + at, room - at, "  %%%u = cbr.void %%6, b%u, b1\nb1:\n", row, arm_blocks[0]) : 0;
    row += 1;
    const uint32_t index = row;
    at += at < room ? (uint64_t)snprintf(text + at, room - at, c->selector == IR_I32 ? "  %%%u = zext.i64 %%4\n" : "  %%%u = copy.i64 %%4\n", row) : 0;
    at += at < room ? (uint64_t)snprintf(text + at, room - at, "  %%%u = inttoptr.ptr %%2\n  %%%u = address.ptr %%%u + %%%u * 4 offset=0\n  %%%u = load.i32 %%%u align=1\n", row + 1, row + 2,
                                         row + 1, index, row + 3, row + 2)
                      : 0;
    const uint32_t load = row + 3;
    row += 4;
    if (c->refusal == IR_SWITCH_TABLE_TEST_READ_TWICE)
    {
        at += at < room ? (uint64_t)snprintf(text + at, room - at, "  %%%u = add.i32 %%%u, %%%u\n", row, load, load) : 0;
        row += 1;
    }
    const uint32_t dispatched = row;
    if (joined)
    {
        at += at < room ? (uint64_t)ir_switch_table_test_constant(text + at, room - at, row, c->type, c->values[0]) : 0;
        row += 1;
    }
    at += at < room ? (uint64_t)snprintf(text + at, room - at, "  %%%u = sext.i64 %%%u\n  %%%u = inttoptr.ptr %%2\n  %%%u = address.ptr %%%u + %%%u * 1 offset=0\n  %%%u = br_indirect.void %%%u [", row,
                                         load, row + 1, row + 2, row + 1, row, row + 3, row + 2)
                      : 0;
    row += 4;
    for (uint32_t p = 0; p < c->listed; p += 1)
    {
        at += at < room ? (uint64_t)snprintf(text + at, room - at, p == 0 ? "#%u" : ", #%u", p) : 0;
    }
    at += at < room ? (uint64_t)snprintf(text + at, room - at, "]\n") : 0;
    for (uint32_t p = joined ? 1 : 0; p < c->listed; p += 1)
    {
        at += at < room ? (uint64_t)snprintf(text + at, room - at, "b%u:\n", arm_blocks[p]) : 0;
        const bool variable = c->refusal == IR_SWITCH_TABLE_TEST_VARIABLE && p == 1;
        arm_rows[p] = variable ? 0 : row;
        if (!variable)
        {
            at += at < room ? (uint64_t)ir_switch_table_test_constant(text + at, room - at, row, c->type, c->values[p]) : 0;
            row += 1;
        }
        if (c->refusal == IR_SWITCH_TABLE_TEST_EFFECT && p == 1)
        {
            at += at < room ? (uint64_t)snprintf(text + at, room - at, "  %%%u = symbol_addr.ptr @9\n  %%%u = store.void %%%u, %%%u align=4\n", row, row + 1, row, arm_rows[p]) : 0;
            row += 2;
        }
        const bool away = elsewhere && p == 1;
        at += at < room ? (uint64_t)(branches || away ? snprintf(text + at, room - at, "  %%%u = br.void b%u\n", row, away ? other : join) :
                                                         snprintf(text + at, room - at, "  %%%u = ret.void %%%u\n", row, arm_rows[p]))
                        : 0;
        row += 1;
    }
    if (branches)
    {
        at += at < room ? (uint64_t)snprintf(text + at, room - at, "b%u:\n  %%%u = phi.%s ", join, row, t) : 0;
        const uint32_t phi = row;
        if (joined)
        {
            at += at < room ? (uint64_t)snprintf(text + at, room - at, "[%%%u, b0], [%%%u, b1]", outside, dispatched) : 0;
        }
        for (uint32_t p = joined ? 1 : 0; p < c->listed; p += 1)
        {
            at += at < room ? (uint64_t)snprintf(text + at, room - at, "%s[%%%u, b%u]", joined || p != 0 ? ", " : "", arm_rows[p], arm_blocks[p]) : 0;
        }
        at += at < room ? (uint64_t)snprintf(text + at, room - at, "\n  %%%u = phi.%s ", phi + 1, s) : 0;
        if (joined)
        {
            at += at < room ? (uint64_t)snprintf(text + at, room - at, "[%%0, b0], [%%0, b1]") : 0;
        }
        for (uint32_t p = joined ? 1 : 0; p < c->listed; p += 1)
        {
            at += at < room ? (uint64_t)snprintf(text + at, room - at, "%s[%%0, b%u]", joined || p != 0 ? ", " : "", arm_blocks[p]) : 0;
        }
        at += at < room ? (uint64_t)snprintf(text + at, room - at, "\n") : 0;
        row = phi + 2;
        // The selector at the value's type, added to it.
        const uint32_t sbits = ir_type_bits[c->selector];
        const uint32_t tbits = ir_type_bits[c->type];
        uint32_t converted = phi + 1;
        if (sbits != tbits)
        {
            at += at < room ? (uint64_t)snprintf(text + at, room - at, "  %%%u = %s.%s %%%u\n", row, sbits > tbits ? "trunc" : "sext", t, phi + 1) : 0;
            converted = row;
            row += 1;
        }
        at += at < room ? (uint64_t)snprintf(text + at, room - at, "  %%%u = add.%s %%%u, %%%u\n  %%%u = ret.void %%%u\n", row, t, phi, converted, row + 1, row) : 0;
        row += 2;
    }
    if (elsewhere)
    {
        at += at < room ? (uint64_t)snprintf(text + at, room - at, "b%u:\n  %%%u = ret.void %%%u\n", other, row, arm_rows[1]) : 0;
    }
    at += at < room ? (uint64_t)snprintf(text + at, room - at, "}\n") : 0;
    return at < room;
}

// The value the fixture's description gives at selector `x`: the entry's position's constant, or the default's (the
// bounds check's own where J is among the arms), plus the selector at the value's type for a phi.
static uint64_t ir_switch_table_test_expected(const IrSwitchTableTestCase* c, uint64_t x)
{
    const uint64_t smask = ir_switch_table_test_mask(c->selector);
    const uint64_t index = ((x & smask) - ((uint64_t)c->lower & smask)) & smask;
    const bool inside = index < c->count;
    const int64_t chosen = inside ? c->values[c->entries[index]] : c->mode == IR_SWITCH_TABLE_TEST_JOIN ? c->outside : c->values[0];
    const uint64_t extended = ir_type_bits[c->type] > ir_type_bits[c->selector] ? (uint64_t)ir_switch_table_test_signed(x & smask, c->selector) : x;
    const uint64_t added = c->mode == IR_SWITCH_TABLE_TEST_RETURN ? 0 : extended;
    return ((uint64_t)chosen + added) & ir_switch_table_test_mask(c->type);
}

static uint32_t ir_switch_table_test_count(const IrFunction* f, uint32_t op)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        count += f->op[i] == op;
    }
    return count;
}

static void ir_switch_table_test_case(IrOptimizeTest* test, const IrSwitchTableTestCase* c, Arena* input, Arena* output, Arena* optimized, Arena* scratch)
{
    const uint64_t im = input->length;
    const uint64_t om = output->length;
    const uint64_t pm = optimized->length;
    const uint64_t sm = scratch->length;
    char message[192];
    char text[BUSTER_IR_SWITCH_TABLE_TEST_TEXT];
    const bool written = ir_switch_table_test_text(c, text, sizeof(text));
    IrFunction f = { 0 };
    const IrParseResult parsed = written ? ir_parse((String8){ .pointer = text, .length = strlen(text) }, input, scratch, &f) : (IrParseResult){ .error = IR_PARSE_SYNTAX };
    const bool valid = parsed.error == IR_PARSE_OK && ir_verify_complete(&f, scratch).error == IR_OK;
    snprintf(message, sizeof(message), "switch table %s: the fixture parses as complete SSA", c->name);
    ir_optimize_test_expect(test, valid, message);
    if (!valid)
    {
        fprintf(stderr, "  switch table %s: parse error %u at %" PRIu64 ":%" PRIu64 " (verifier %u at %u):\n%s", c->name, (uint32_t)parsed.error, parsed.line, parsed.column,
                (uint32_t)parsed.check.error, parsed.check.instruction, text);
    }
    // The lowering's table: entry i's label's place, past a first label of the tables'.
    uint32_t entries[64] = { 0 };
    for (uint32_t i = 0; i < c->count; i += 1)
    {
        entries[i] = BUSTER_IR_SWITCH_TABLE_TEST_FIRST + c->entries[i];
    }
    const IrSwitchTable table = {
        .entries = entries,
        .count = c->count,
        .first = BUSTER_IR_SWITCH_TABLE_TEST_FIRST,
        .symbol = BUSTER_IR_SWITCH_TABLE_TEST_SYMBOL,
    };
    IrSwitchTableResult result = { 0 };
    const IrCheck check = valid ? ir_switch_tables(&f, &table, 1, output, scratch, &result) : ir_check(IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    const bool converts = c->refusal == IR_SWITCH_TABLE_TEST_CONVERTS;
    snprintf(message, sizeof(message), "switch table %s: %s, scratch restored", c->name, converts ? "converted" : "refused");
    ir_optimize_test_expect(test, check.error == IR_OK && result.converted == (converts ? 1u : 0u) && scratch->length == sm, message);
    if (check.error == IR_OK && !converts)
    {
        snprintf(message, sizeof(message), "switch table %s: a refusal borrows the unchanged input and allocates nothing", c->name);
        ir_optimize_test_expect(test, result.function.op == f.op && result.function.extra == f.extra && result.values == NULL && output->length == om, message);
    }
    if (check.error == IR_OK && converts && result.converted == 1)
    {
        bool values = result.values != NULL && result.values[0] != NULL;
        uint8_t bytes[256] = { 0 };
        for (uint32_t i = 0; values && i < c->count; i += 1)
        {
            const int64_t value = ir_switch_table_test_signed((uint64_t)c->values[c->entries[i]] & ir_switch_table_test_mask(c->type), c->type);
            values = result.values[0][i] == (int32_t)value;
            memcpy(bytes + 4 * i, &result.values[0][i], 4);
        }
        snprintf(message, sizeof(message), "switch table %s: each entry is its arm's constant at 32 bits", c->name);
        ir_optimize_test_expect(test, values, message);
        const IrCheck complete = ir_verify_complete(&result.function, scratch);
        snprintf(message, sizeof(message), "switch table %s: the result is complete SSA without the indirect jump", c->name);
        ir_optimize_test_expect(test, complete.error == IR_OK && ir_switch_table_test_count(&result.function, IR_BR_INDIRECT) == 0, message);
        if (complete.error != IR_OK)
        {
            fprintf(stderr, "  switch table %s: error=%u instruction=%u detail=%u\n", c->name, complete.error, complete.instruction, complete.detail);
        }
        IrOptimizeResult o = { 0 };
        IrOptimizeOptions options = ir_optimize_options();
        options.passes |= BUSTER_IR_OPTIMIZE_IDENTITY;
        const IrCheck oc = complete.error == IR_OK ? ir_optimize(&result.function, optimized, scratch, options, &o) : complete;
        snprintf(message, sizeof(message), "switch table %s: the optimizer takes the result and removes the arms", c->name);
        ir_optimize_test_expect(test, oc.error == IR_OK && ir_switch_table_test_count(&o.function, IR_BR_INDIRECT) == 0 && o.function.block_count < result.function.block_count, message);
        const IrOptimizeTestTable memory = {
            .bytes = bytes,
            .size = (uint64_t)c->count * 4,
            .symbol = BUSTER_IR_SWITCH_TABLE_TEST_SYMBOL,
        };
        const uint64_t smask = ir_switch_table_test_mask(c->selector);
        bool executed = complete.error == IR_OK && oc.error == IR_OK;
        for (uint32_t trial = 0; executed && trial < c->count + 16; trial += 1)
        {
            // Every selector from four below the lowest case to past the table, then the type's extremes.
            const uint64_t extremes[6] = { 0, smask, smask >> 1, (smask >> 1) + 1, (uint64_t)c->lower - 1, (uint64_t)c->lower + c->count };
            const uint64_t x = (trial < c->count + 10 ? (uint64_t)c->lower - 4 + trial : extremes[trial - c->count - 10]) & smask;
            const uint64_t expected = ir_switch_table_test_expected(c, x);
            const IrOptimizeExecution converted = ir_optimize_test_execute_table(&result.function, scratch, x, 0, &memory);
            const IrOptimizeExecution final = ir_optimize_test_execute_table(&o.function, scratch, x, 0, &memory);
            executed = converted.status == 1 && converted.value == expected && final.status == 1 && final.value == expected;
            if (!executed)
            {
                fprintf(stderr, "  switch table %s: x=%#" PRIx64 " expected %#" PRIx64 ", converted %u:%#" PRIx64 ", optimized %u:%#" PRIx64 "\n", c->name, x, expected, converted.status,
                        converted.value, final.status, final.value);
            }
        }
        snprintf(message, sizeof(message), "switch table %s: the converted and optimized functions give each selector's arm's value", c->name);
        ir_optimize_test_expect(test, executed, message);
    }
    arena_pop_to(optimized, pm);
    arena_pop_to(output, om);
    arena_pop_to(input, im);
}

// Two dispatches of one table: neither converts, as the table can hold one kind of entries.
static void ir_switch_table_test_twice(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch)
{
    static const char text[] = "function (i32) -> i32 {\n"
                               "  label #0 b3\n"
                               "  label #1 b4\n"
                               "b0:\n"
                               "  %0 = param.i32 0\n"
                               "  %1 = symbol_addr.ptr @7\n"
                               "  %2 = zext.i64 %0\n"
                               "  %3 = const.i32 0x00000000000000000000000000000000\n"
                               "  %4 = icmp.i1 %0, %3 eq\n"
                               "  %5 = cbr.void %4, b1, b2\n"
                               "b1:\n"
                               "  %6 = address.ptr %1 + %2 * 4 offset=0\n"
                               "  %7 = load.i32 %6 align=1\n"
                               "  %8 = sext.i64 %7\n"
                               "  %9 = address.ptr %1 + %8 * 1 offset=0\n"
                               "  %10 = br_indirect.void %9 [#0, #1]\n"
                               "b2:\n"
                               "  %11 = address.ptr %1 + %2 * 4 offset=0\n"
                               "  %12 = load.i32 %11 align=1\n"
                               "  %13 = sext.i64 %12\n"
                               "  %14 = address.ptr %1 + %13 * 1 offset=0\n"
                               "  %15 = br_indirect.void %14 [#0, #1]\n"
                               "b3:\n"
                               "  %16 = const.i32 0x00000000000000000000000000000005\n"
                               "  %17 = ret.void %16\n"
                               "b4:\n"
                               "  %18 = const.i32 0x00000000000000000000000000000006\n"
                               "  %19 = ret.void %18\n"
                               "}\n";
    const uint64_t im = input->length;
    const uint64_t om = output->length;
    IrFunction f = { 0 };
    const IrParseResult parsed = ir_parse((String8){ .pointer = (char*)text, .length = sizeof(text) - 1 }, input, scratch, &f);
    static const uint32_t entries[2] = { 0, 1 };
    const IrSwitchTable table = {
        .entries = entries,
        .count = 2,
        .first = 0,
        .symbol = 7,
    };
    IrSwitchTableResult result = { 0 };
    const IrCheck check = parsed.error == IR_PARSE_OK ? ir_switch_tables(&f, &table, 1, output, scratch, &result) : ir_check(IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    ir_optimize_test_expect(test, parsed.error == IR_PARSE_OK && check.error == IR_OK && result.converted == 0 && result.function.op == f.op && output->length == om,
                            "switch table: a table two dispatches load stays a jump table");
    arena_pop_to(output, om);
    arena_pop_to(input, im);
}

static void ir_switch_table_tests(IrOptimizeTest* test)
{
    Arena input = arena_reserve(4ull << 20);
    Arena output = arena_reserve(4ull << 20);
    Arena optimized = arena_reserve(4ull << 20);
    Arena scratch = arena_reserve(16ull << 20);
    const bool ready = input.base != 0 && output.base != 0 && optimized.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "switch table fixture arenas");
    for (uint32_t k = 0; ready && k < BUSTER_ARRAY_LENGTH(ir_switch_table_test_cases); k += 1)
    {
        ir_switch_table_test_case(test, &ir_switch_table_test_cases[k], &input, &output, &optimized, &scratch);
    }
    if (ready)
    {
        ir_switch_table_test_twice(test, &input, &output, &scratch);
    }
    arena_release(&input);
    arena_release(&output);
    arena_release(&optimized);
    arena_release(&scratch);
}

#endif
