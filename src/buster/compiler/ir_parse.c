// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// One-pass, nonrecursive reader for ir_print's per-function text. Include after ir.c and String8. ir_parse and its
// scalar twin consume one function; consumed lets a caller read a sequence without retaining a module wrapper.
// Newline classification is 64-byte SIMD work with exact masked tails; ordered line grammar writes geometric
// scratch builders. Publication uses ir_create, transposes instruction rows into its exact columns, then verifies.
// Output and scratch arenas, and the source slice, must be disjoint. Failure preserves the output view and arena;
// every call restores scratch. There is no readable source padding. Constants/calls/addresses are normalized by
// occurrence because the printer does not expose unused entries or side-table sharing. Symbol IDs remain numeric.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "host_isa.h"

typedef enum IrParseError
{
    IR_PARSE_OK,
    IR_PARSE_END,
    IR_PARSE_SYNTAX,
    IR_PARSE_LIMIT,
    IR_PARSE_MEMORY,
    IR_PARSE_VERIFY,
} IrParseError;

typedef struct IrParseResult IrParseResult;
struct IrParseResult
{
    IrParseError error;
    uint64_t consumed;
    uint64_t offset;
    uint64_t line;
    uint64_t column;
    IrCheck check;
};

typedef struct IrParseArray IrParseArray;
struct IrParseArray
{
    void* data;
    uint32_t count;
    uint32_t capacity;
    uint32_t bytes;
};

typedef struct IrParseRow IrParseRow;
struct IrParseRow
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint8_t op;
    uint8_t type;
    uint8_t flags;
    uint8_t alignment_log2;
    uint64_t offset;
    uint64_t line;
};

typedef struct IrParseState IrParseState;
struct IrParseState
{
    String8 source;
    Arena* scratch;
    uint64_t cursor;
    uint64_t end;
    uint64_t line_start;
    uint64_t line;
    uint64_t scan_first;
    uint64_t scan_end;
    uint64_t scan_newlines;
    IrParseResult result;
    IrParseArray rows;
    IrParseArray blocks;
    IrParseArray extra;
    IrParseArray constants;
    IrParseArray vector_constants;
    IrParseArray slots;
    IrParseArray addresses;
    IrParseArray calls;
    IrParseArray call_types;
    IrParseArray parameters;
    IrParseArray labels;
    IrParseArray abi_calls;
    IrParseArray passed;
    // The machine rows (#141, IrFunction.rows), Tier0Operation each.
    IrParseArray machine_rows;
    uint32_t symbol_count;
    uint8_t result_type;
    // The header's own lines: a variadic definition's incoming frame, and the function's ABI records.
    bool variadic;
    AbiCall incoming;
    uint32_t parameter_passed;
    uint32_t result_size;
    AbiReturn result_abi;
};

static __attribute__((unused)) IrParseResult ir_parse(String8 source, Arena* output, Arena* scratch, IrFunction* result);
static __attribute__((unused)) IrParseResult ir_parse_scalar(String8 source, Arena* output, Arena* scratch, IrFunction* result);

static void ir_parse_fail(IrParseState* state, IrParseError error)
{
    if (state->result.error == IR_PARSE_OK)
    {
        state->result.error = error;
        state->result.offset = state->cursor;
        state->result.line = state->line;
        state->result.column = state->cursor - state->line_start + 1;
    }
}

static uint32_t ir_parse_append(IrParseState* state, IrParseArray* array, const void* element)
{
    const uint32_t index = array->count;
    if (state->result.error == IR_PARSE_OK && array->count == UINT32_MAX)
    {
        ir_parse_fail(state, IR_PARSE_LIMIT);
    }
    if (state->result.error == IR_PARSE_OK && array->count == array->capacity)
    {
        const uint64_t next = array->capacity == 0 ? 16 : (uint64_t)array->capacity * 2;
        const uint32_t capacity = next > UINT32_MAX ? UINT32_MAX : (uint32_t)next;
        void* data = arena_push_array(state->scratch, capacity, array->bytes, 64);
        if (data == 0)
        {
            ir_parse_fail(state, IR_PARSE_MEMORY);
        }
        else
        {
            if (array->count != 0)
            {
                memcpy(data, array->data, (uint64_t)array->count * array->bytes);
            }
            array->data = data;
            array->capacity = capacity;
        }
    }
    if (state->result.error == IR_PARSE_OK)
    {
        memcpy((uint8_t*)array->data + (uint64_t)array->count * array->bytes, element, array->bytes);
        array->count += 1;
    }
    return index;
}

static void ir_parse_space(IrParseState* state)
{
    while (state->cursor < state->end && (state->source.pointer[state->cursor] == ' ' || state->source.pointer[state->cursor] == '\t' || state->source.pointer[state->cursor] == '\r'))
    {
        state->cursor += 1;
    }
}

static bool ir_parse_end_line(IrParseState* state)
{
    ir_parse_space(state);
    return state->cursor == state->end || state->source.pointer[state->cursor] == ';';
}

static bool ir_parse_take(IrParseState* state, char byte)
{
    ir_parse_space(state);
    const bool taken = state->cursor < state->end && state->source.pointer[state->cursor] == byte;
    if (taken)
    {
        state->cursor += 1;
    }
    return taken;
}

static void ir_parse_expect(IrParseState* state, char byte)
{
    if (state->result.error == IR_PARSE_OK && !ir_parse_take(state, byte))
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
}

static bool ir_parse_word(IrParseState* state, const char* word)
{
    ir_parse_space(state);
    const uint64_t length = strlen(word);
    const bool matches = length <= state->end - state->cursor && memcmp(state->source.pointer + state->cursor, word, length) == 0;
    if (matches)
    {
        state->cursor += length;
    }
    return matches;
}

static void ir_parse_keyword(IrParseState* state, const char* word)
{
    if (state->result.error == IR_PARSE_OK && !ir_parse_word(state, word))
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
}

static uint64_t ir_parse_unsigned(IrParseState* state, uint64_t limit, bool hexadecimal)
{
    ir_parse_space(state);
    uint64_t value = 0;
    uint32_t digits = 0;
    const uint32_t radix = hexadecimal ? 16 : 10;
    if (hexadecimal)
    {
        ir_parse_keyword(state, "0x");
    }
    while (state->result.error == IR_PARSE_OK && state->cursor < state->end)
    {
        const uint8_t byte = (uint8_t)state->source.pointer[state->cursor];
        const uint32_t digit = byte >= '0' && byte <= '9' ? byte - '0' : hexadecimal && (byte | 32u) >= 'a' && (byte | 32u) <= 'f' ? (byte | 32u) - 'a' + 10 : radix;
        if (digit >= radix)
        {
            break;
        }
        if (digit > limit || value > (limit - digit) / radix)
        {
            ir_parse_fail(state, IR_PARSE_LIMIT);
        }
        else
        {
            value = value * radix + digit;
            state->cursor += 1;
            digits += 1;
        }
    }
    if (state->result.error == IR_PARSE_OK && digits == 0)
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    return value;
}

static uint32_t ir_parse_id(IrParseState* state, char prefix)
{
    ir_parse_expect(state, prefix);
    return (uint32_t)ir_parse_unsigned(state, UINT32_MAX, false);
}

static uint8_t ir_parse_named(IrParseState* state, const char* const* names, uint32_t count)
{
    ir_parse_space(state);
    const uint64_t first = state->cursor;
    while (state->cursor < state->end)
    {
        const uint8_t byte = (uint8_t)state->source.pointer[state->cursor];
        if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '_'))
        {
            break;
        }
        state->cursor += 1;
    }
    const uint64_t length = state->cursor - first;
    uint32_t found = count;
    for (uint32_t index = 0; index < count; index += 1)
    {
        if (strlen(names[index]) == length && memcmp(state->source.pointer + first, names[index], length) == 0)
        {
            found = index;
        }
    }
    if (found == count)
    {
        state->cursor = first;
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    return (uint8_t)found;
}

// An instruction's type (#141's element types): a type's name, or a vector operation's lanes' (ir_lane_type_names), its
// vector type with *lane its lanes (IrSimdLane, 0 for a type's own name).
static uint8_t ir_parse_lane_type(IrParseState* state, uint8_t* lane)
{
    ir_parse_space(state);
    const uint64_t first = state->cursor;
    while (state->cursor < state->end && ((state->source.pointer[state->cursor] >= 'a' && state->source.pointer[state->cursor] <= 'z') ||
                                          (state->source.pointer[state->cursor] >= '0' && state->source.pointer[state->cursor] <= '9')))
    {
        state->cursor += 1;
    }
    const uint64_t length = state->cursor - first;
    uint32_t type = IR_TYPE_COUNT;
    uint32_t lanes = 0;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t kind = 1; kind < IR_SIMD_LANE_COUNT; kind += 1)
        {
            if (strlen(ir_lane_type_names[width][kind]) == length && memcmp(state->source.pointer + first, ir_lane_type_names[width][kind], length) == 0)
            {
                type = IR_V4I32 + width;
                lanes = kind;
            }
        }
    }
    if (type == IR_TYPE_COUNT)
    {
        state->cursor = first;
        type = ir_parse_named(state, ir_type_names, IR_TYPE_COUNT);
    }
    *lane = (uint8_t)lanes;
    return (uint8_t)type;
}

static IrConstant ir_parse_constant(IrParseState* state)
{
    IrConstant value = { 0 };
    ir_parse_keyword(state, "0x");
    uint32_t digits = 0;
    while (state->result.error == IR_PARSE_OK && state->cursor < state->end)
    {
        const uint8_t byte = (uint8_t)state->source.pointer[state->cursor];
        const uint32_t digit = byte >= '0' && byte <= '9' ? byte - '0' : (byte | 32u) >= 'a' && (byte | 32u) <= 'f' ? (byte | 32u) - 'a' + 10 : 16;
        if (digit >= 16)
        {
            break;
        }
        if (digits == 32)
        {
            ir_parse_fail(state, IR_PARSE_LIMIT);
        }
        else
        {
            value.high = (value.high << 4) | (value.low >> 60);
            value.low = (value.low << 4) | digit;
            digits += 1;
            state->cursor += 1;
        }
    }
    if (digits == 0)
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    return value;
}

static IrSimdValue ir_parse_vector_constant(IrParseState* state, uint8_t type)
{
    IrSimdValue value = { 0 };
    ir_parse_keyword(state, "0x");
    uint32_t digits = 0;
    const uint32_t limit = ir_vector_type(type) ? ir_type_bits[type] / 4 : 128;
    while (state->result.error == IR_PARSE_OK && state->cursor < state->end)
    {
        const uint8_t byte = (uint8_t)state->source.pointer[state->cursor];
        const uint32_t digit = byte >= '0' && byte <= '9' ? byte - '0' : (byte | 32u) >= 'a' && (byte | 32u) <= 'f' ? (byte | 32u) - 'a' + 10 : 16;
        if (digit >= 16)
        {
            break;
        }
        if (digits == limit)
        {
            ir_parse_fail(state, IR_PARSE_LIMIT);
        }
        else
        {
            for (uint32_t word = 8; word-- > 0;)
            {
                value.words[word] = (value.words[word] << 4) | (word != 0 ? value.words[word - 1] >> 60 : digit);
            }
            digits += 1;
            state->cursor += 1;
        }
    }
    if (digits == 0)
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    return value;
}

static void ir_parse_symbol(IrParseState* state, uint32_t id)
{
    if (id == UINT32_MAX)
    {
        ir_parse_fail(state, IR_PARSE_LIMIT);
    }
    else if (id >= state->symbol_count)
    {
        state->symbol_count = id + 1;
    }
}

static void ir_parse_types(IrParseState* state, IrParseArray* types)
{
    ir_parse_expect(state, '(');
    if (state->result.error == IR_PARSE_OK && !ir_parse_take(state, ')'))
    {
        bool more = true;
        while (state->result.error == IR_PARSE_OK && more)
        {
            const uint8_t type = ir_parse_named(state, ir_type_names, IR_TYPE_COUNT);
            ir_parse_append(state, types, &type);
            more = ir_parse_take(state, ',');
        }
        ir_parse_expect(state, ')');
    }
}

static void ir_parse_header(IrParseState* state)
{
    ir_parse_keyword(state, "function");
    ir_parse_types(state, &state->parameters);
    ir_parse_keyword(state, "->");
    state->result_type = ir_parse_named(state, ir_type_names, IR_TYPE_COUNT);
    ir_parse_expect(state, '{');
}

static void ir_parse_slot(IrParseState* state)
{
    ir_parse_keyword(state, "slot");
    const uint32_t id = ir_parse_id(state, '$');
    if (state->result.error == IR_PARSE_OK && id != state->slots.count)
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    IrSlot slot = { 0 };
    ir_parse_keyword(state, "size=");
    slot.size = ir_parse_unsigned(state, UINT64_MAX, false);
    ir_parse_keyword(state, "align=");
    slot.alignment = (uint32_t)ir_parse_unsigned(state, UINT32_MAX, false);
    ir_parse_keyword(state, "type=");
    slot.type = ir_parse_named(state, ir_type_names, IR_TYPE_COUNT);
    ir_parse_keyword(state, "flags=");
    slot.flags = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, true);
    ir_parse_append(state, &state->slots, &slot);
}

// The ABI records' forms, as ir_print_location, ir_print_return and ir_print_abi_call write them.
static AbiLocation ir_parse_location(IrParseState* state)
{
    AbiLocation location = { 0 };
    ir_parse_keyword(state, "loc(");
    location.kind = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    ir_parse_expect(state, ',');
    location.registers[0] = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    ir_parse_expect(state, ',');
    location.registers[1] = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    ir_parse_expect(state, ',');
    location.stack_offset = (uint32_t)ir_parse_unsigned(state, UINT32_MAX, false);
    ir_parse_expect(state, ',');
    location.flags = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    ir_parse_expect(state, ')');
    return location;
}

static AbiReturn ir_parse_return(IrParseState* state)
{
    AbiReturn result = { 0 };
    ir_parse_keyword(state, "ret(");
    result.kind = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    ir_parse_expect(state, ',');
    result.flags = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    ir_parse_expect(state, ',');
    result.part_bytes = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    for (uint32_t r = 0; r < BUSTER_ABI_RESULT_PARTS; r += 1)
    {
        ir_parse_expect(state, ',');
        result.registers[r] = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    }
    ir_parse_expect(state, ')');
    return result;
}

static AbiCall ir_parse_abi_call(IrParseState* state)
{
    AbiCall call = { 0 };
    ir_parse_keyword(state, "call(");
    call.stack_bytes = (uint32_t)ir_parse_unsigned(state, UINT32_MAX, false);
    ir_parse_expect(state, ',');
    call.stack_alignment_log2 = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    ir_parse_expect(state, ',');
    call.integer_registers = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    ir_parse_expect(state, ',');
    call.vector_registers = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    ir_parse_expect(state, ',');
    call.result = ir_parse_return(state);
    ir_parse_expect(state, ')');
    return call;
}

// `label #k b<block>` or `label #k none`, in their order.
static void ir_parse_label(IrParseState* state)
{
    ir_parse_keyword(state, "label");
    const uint32_t id = ir_parse_id(state, '#');
    if (state->result.error == IR_PARSE_OK && id != state->labels.count)
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    const uint32_t block = ir_parse_word(state, "none") ? BUSTER_IR_NONE : ir_parse_id(state, 'b');
    ir_parse_append(state, &state->labels, &block);
}

// `passed #k size=<bytes> loc(...)`, in their order.
static void ir_parse_passed(IrParseState* state)
{
    ir_parse_keyword(state, "passed");
    const uint32_t id = ir_parse_id(state, '#');
    if (state->result.error == IR_PARSE_OK && id != state->passed.count)
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    IrPassed passed = { 0 };
    ir_parse_keyword(state, "size=");
    passed.size = (uint32_t)ir_parse_unsigned(state, UINT32_MAX, false);
    passed.location = ir_parse_location(state);
    ir_parse_append(state, &state->passed, &passed);
}

// `abi #k call(...) passed=#<first> result=<bytes>`, in their order.
static void ir_parse_abi(IrParseState* state)
{
    ir_parse_keyword(state, "abi");
    const uint32_t id = ir_parse_id(state, '#');
    if (state->result.error == IR_PARSE_OK && id != state->abi_calls.count)
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    IrAbiCall abi = { 0 };
    abi.call = ir_parse_abi_call(state);
    ir_parse_keyword(state, "passed=");
    abi.passed = ir_parse_id(state, '#');
    ir_parse_keyword(state, "result=");
    abi.result_size = (uint32_t)ir_parse_unsigned(state, UINT32_MAX, false);
    ir_parse_append(state, &state->abi_calls, &abi);
}

// A machine row's class (#141): its name (ir_row_class_names), a vector's and a mask's bytes after it, into `class` and
// `bytes`.
static void ir_parse_row_class(IrParseState* state, uint8_t* class, uint8_t* bytes)
{
    ir_parse_space(state);
    const uint64_t first = state->cursor;
    while (state->cursor < state->end && ((state->source.pointer[state->cursor] >= 'a' && state->source.pointer[state->cursor] <= 'z') || (state->source.pointer[state->cursor] >= '0' && state->source.pointer[state->cursor] <= '9')))
    {
        state->cursor += 1;
    }
    const uint64_t length = state->cursor - first;
    const char* token = state->source.pointer + first;
    uint32_t found = BUSTER_TYPED_CLASS_LIMIT;
    uint64_t name_length = 0;
    for (uint32_t k = 0; k < BUSTER_TYPED_CLASS_LIMIT; k += 1)
    {
        const uint64_t n = strlen(ir_row_class_names[k]);
        const bool sized = k == BUSTER_TYPED_CLASS_VECTOR || k == BUSTER_TYPED_CLASS_MASK;
        if ((sized ? n < length : n == length) && memcmp(token, ir_row_class_names[k], n) == 0)
        {
            found = k;
            name_length = n;
        }
    }
    uint64_t value = 0;
    for (uint64_t k = name_length; found != BUSTER_TYPED_CLASS_LIMIT && k < length; k += 1)
    {
        const uint8_t digit = (uint8_t)(token[k] - '0');
        found = digit <= 9 && value <= 25 ? found : BUSTER_TYPED_CLASS_LIMIT;
        value = value * 10 + digit;
    }
    if (found == BUSTER_TYPED_CLASS_LIMIT || value > UINT8_MAX)
    {
        state->cursor = first;
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    *class = (uint8_t)(found < BUSTER_TYPED_CLASS_LIMIT ? found : 0);
    *bytes = (uint8_t)(found < BUSTER_TYPED_CLASS_LIMIT ? value : 0);
}

// A machine row's record's signed field, in decimal.
static int32_t ir_parse_signed(IrParseState* state)
{
    const bool negative = ir_parse_take(state, '-');
    const uint64_t magnitude = ir_parse_unsigned(state, negative ? (uint64_t)INT32_MAX + 1 : INT32_MAX, false);
    return negative ? (int32_t)(UINT32_C(0) - (uint32_t)magnitude) : (int32_t)magnitude;
}

// `row #k (<operand classes>) -> <result class> [<form> reg vvvv rm index mode decorations immediate displacement]...`,
// in their order (#141, ir_print's): a form by its name (ir_form_names), the bytes in hexadecimal and the immediate and
// the displacement in decimal.
static void ir_parse_machine_row(IrParseState* state)
{
    const uint32_t id = ir_parse_id(state, '#');
    if (state->result.error == IR_PARSE_OK && id != state->machine_rows.count)
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    ir_parse_expect(state, '(');
    if (state->result.error == IR_PARSE_OK && !ir_parse_take(state, ')'))
    {
        bool more = true;
        while (state->result.error == IR_PARSE_OK && more)
        {
            if (row.operand_count == BUSTER_TIER0_OPERATION_OPERANDS)
            {
                ir_parse_fail(state, IR_PARSE_LIMIT);
            }
            else
            {
                ir_parse_row_class(state, &row.operand_classes[row.operand_count], &row.operand_bytes[row.operand_count]);
                row.operand_count += 1;
            }
            more = ir_parse_take(state, ',');
        }
        ir_parse_expect(state, ')');
    }
    ir_parse_keyword(state, "->");
    ir_parse_row_class(state, &row.result_class, &row.result_bytes);
    while (state->result.error == IR_PARSE_OK && ir_parse_take(state, '['))
    {
        ir_parse_space(state);
        const uint64_t first = state->cursor;
        while (state->cursor < state->end && ((state->source.pointer[state->cursor] >= 'A' && state->source.pointer[state->cursor] <= 'Z') || (state->source.pointer[state->cursor] >= '0' && state->source.pointer[state->cursor] <= '9') ||
                                              state->source.pointer[state->cursor] == '_'))
        {
            state->cursor += 1;
        }
        const uint64_t length = state->cursor - first;
        uint32_t form = X86_FORM_COUNT;
        for (uint32_t k = 0; form == X86_FORM_COUNT && k < X86_FORM_COUNT; k += 1)
        {
            form = strlen(ir_form_names[k]) == length && memcmp(state->source.pointer + first, ir_form_names[k], length) == 0 ? k : form;
        }
        X86Instruction record = { .form = (uint16_t)form };
        record.reg = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, true);
        record.vvvv = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, true);
        record.rm = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, true);
        record.index = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, true);
        record.mode = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, true);
        record.decorations = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, true);
        record.immediate = ir_parse_signed(state);
        record.displacement = ir_parse_signed(state);
        ir_parse_expect(state, ']');
        if (state->result.error == IR_PARSE_OK && (form == X86_FORM_COUNT || row.record_count == BUSTER_TIER0_OPERATION_RECORDS))
        {
            ir_parse_fail(state, form == X86_FORM_COUNT ? IR_PARSE_SYNTAX : IR_PARSE_LIMIT);
        }
        else if (state->result.error == IR_PARSE_OK)
        {
            row.records[row.record_count] = record;
            row.record_count += 1;
        }
    }
    ir_parse_append(state, &state->machine_rows, &row);
}

// `variadic call(...)`, `result size=<bytes> ret(...) passed=#<first>`, each once, and the machine rows (#141).
static void ir_parse_frame(IrParseState* state)
{
    if (ir_parse_word(state, "row"))
    {
        ir_parse_machine_row(state);
    }
    else if (ir_parse_word(state, "variadic"))
    {
        if (state->result.error == IR_PARSE_OK && state->variadic)
        {
            ir_parse_fail(state, IR_PARSE_SYNTAX);
        }
        state->variadic = true;
        state->incoming = ir_parse_abi_call(state);
    }
    else
    {
        ir_parse_keyword(state, "result");
        if (state->result.error == IR_PARSE_OK && state->parameter_passed != 0)
        {
            ir_parse_fail(state, IR_PARSE_SYNTAX);
        }
        ir_parse_keyword(state, "size=");
        state->result_size = (uint32_t)ir_parse_unsigned(state, UINT32_MAX, false);
        state->result_abi = ir_parse_return(state);
        ir_parse_keyword(state, "passed=");
        const uint32_t first = ir_parse_id(state, '#');
        if (state->result.error == IR_PARSE_OK && first == UINT32_MAX)
        {
            ir_parse_fail(state, IR_PARSE_LIMIT);
        }
        state->parameter_passed = first + 1;
    }
}

static void ir_parse_address(IrParseState* state, IrParseRow* row)
{
    row->a = ir_parse_id(state, '%');
    IrAddress address = {
        .scale = 1,
    };
    if (ir_parse_take(state, '+'))
    {
        row->b = ir_parse_id(state, '%');
        ir_parse_expect(state, '*');
        address.scale = (uint8_t)ir_parse_unsigned(state, UINT8_MAX, false);
    }
    ir_parse_keyword(state, "offset=");
    const bool negative = ir_parse_take(state, '-');
    const uint64_t magnitude = ir_parse_unsigned(state, negative ? (uint64_t)INT64_MAX + 1 : INT64_MAX, false);
    address.offset = negative ? (int64_t)(UINT64_C(0) - magnitude) : (int64_t)magnitude;
    row->c = ir_parse_append(state, &state->addresses, &address);
}

static void ir_parse_call(IrParseState* state, IrParseRow* row)
{
    // A row call (#141): `row #k(...)`, of no symbol.
    const bool machine = row->op == IR_CALL && ir_parse_word(state, "row");
    const uint32_t machine_row = machine ? ir_parse_id(state, '#') : 0;
    if (state->result.error == IR_PARSE_OK && machine_row == UINT32_MAX)
    {
        ir_parse_fail(state, IR_PARSE_LIMIT);
    }
    row->a = machine ? 0 : ir_parse_id(state, row->op == IR_CALL ? '@' : '%');
    if (row->op == IR_CALL && !machine)
    {
        ir_parse_symbol(state, row->a);
    }
    IrCall call = {
        .argument_first = state->extra.count,
        .type_first = state->call_types.count,
        .result_type = row->type,
        .row = machine ? machine_row + 1 : 0,
    };
    ir_parse_expect(state, '(');
    if (state->result.error == IR_PARSE_OK && !ir_parse_take(state, ')'))
    {
        bool more = true;
        while (state->result.error == IR_PARSE_OK && more)
        {
            const uint32_t value = ir_parse_id(state, '%');
            ir_parse_append(state, &state->extra, &value);
            more = ir_parse_take(state, ',');
        }
        ir_parse_expect(state, ')');
    }
    call.argument_count = state->extra.count - call.argument_first;
    ir_parse_keyword(state, "fixed=");
    ir_parse_types(state, &state->call_types);
    call.parameter_count = state->call_types.count - call.type_first;
    ir_parse_keyword(state, "variadic=");
    call.variadic = ir_parse_unsigned(state, 1, false) != 0;
    // A call that passes or returns an aggregate: its ABI records, and the destination of an aggregate result.
    if (ir_parse_word(state, "abi="))
    {
        const uint32_t abi = ir_parse_id(state, '#');
        if (state->result.error == IR_PARSE_OK && abi == UINT32_MAX)
        {
            ir_parse_fail(state, IR_PARSE_LIMIT);
        }
        call.abi = abi + 1;
    }
    if (ir_parse_word(state, "into="))
    {
        row->c = ir_parse_id(state, '%');
    }
    row->b = ir_parse_append(state, &state->calls, &call);
}

static void ir_parse_phi(IrParseState* state, IrParseRow* row)
{
    row->a = state->extra.count;
    row->b = 0;
    bool more = !ir_parse_end_line(state);
    while (state->result.error == IR_PARSE_OK && more)
    {
        ir_parse_expect(state, '[');
        const uint32_t value = ir_parse_id(state, '%');
        ir_parse_expect(state, ',');
        const uint32_t block = ir_parse_id(state, 'b');
        ir_parse_expect(state, ']');
        ir_parse_append(state, &state->extra, &block);
        ir_parse_append(state, &state->extra, &value);
        row->b += 1;
        more = ir_parse_take(state, ',');
    }
}

static void ir_parse_switch(IrParseState* state, IrParseRow* row)
{
    row->a = ir_parse_id(state, '%');
    row->b = state->extra.count;
    row->c = 0;
    ir_parse_expect(state, ',');
    ir_parse_keyword(state, "default");
    const uint32_t fallback = ir_parse_id(state, 'b');
    ir_parse_append(state, &state->extra, &fallback);
    while (state->result.error == IR_PARSE_OK && ir_parse_take(state, ','))
    {
        ir_parse_expect(state, '[');
        const IrConstant value = ir_parse_constant(state);
        const uint32_t constant = ir_parse_append(state, &state->constants, &value);
        ir_parse_expect(state, ',');
        const uint32_t block = ir_parse_id(state, 'b');
        ir_parse_expect(state, ']');
        ir_parse_append(state, &state->extra, &constant);
        ir_parse_append(state, &state->extra, &block);
        row->c += 1;
    }
}

static void ir_parse_instruction(IrParseState* state)
{
    IrParseRow row = {
        .a = BUSTER_IR_NONE,
        .b = BUSTER_IR_NONE,
        .c = BUSTER_IR_NONE,
        .offset = state->cursor,
        .line = state->line,
    };
    const uint32_t id = ir_parse_id(state, '%');
    if (state->result.error == IR_PARSE_OK && (id != state->rows.count || state->blocks.count == 0))
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    ir_parse_expect(state, '=');
    row.op = ir_parse_named(state, ir_op_names, IR_OP_COUNT);
    ir_parse_expect(state, '.');
    row.type = ir_parse_lane_type(state, &row.flags);
    // Lanes name a vector operation's elements alone, never a transport's (a load's flags are its access's).
    if (state->result.error == IR_PARSE_OK && row.flags != 0 && (row.op >= IR_OP_COUNT || ir_lane_operation(row.op) == IR_SIMD_OPERATION_COUNT || row.op == IR_VCMP))
    {
        ir_parse_fail(state, IR_PARSE_SYNTAX);
    }
    if (state->result.error == IR_PARSE_OK)
    {
        switch (row.op)
        {
            case IR_CONST:
            {
                const IrConstant constant = ir_parse_constant(state);
                row.a = ir_parse_append(state, &state->constants, &constant);
            } break;
            case IR_VCONST:
            {
                const IrSimdValue constant = ir_parse_vector_constant(state, row.type);
                row.a = ir_parse_append(state, &state->vector_constants, &constant);
            } break;
            case IR_PARAM: row.a = (uint32_t)ir_parse_unsigned(state, UINT32_MAX, false); break;
            case IR_STACK_ADDR: row.a = ir_parse_id(state, '$'); break;
            case IR_LIFETIME:
            {
                static const char* const edges[2] = {
                    "start",
                    "end",
                };
                row.a = ir_parse_id(state, '$');
                row.b = ir_parse_named(state, edges, 2);
            } break;
            case IR_FENCE:
            {
                // Its order, and `signal` for a signal fence (#1059).
                row.a = ir_parse_named(state, ir_fence_orders, BUSTER_IR_FENCE_SEQ_CST + 1);
                if (state->result.error == IR_PARSE_OK && !ir_parse_end_line(state))
                {
                    ir_parse_keyword(state, "signal");
                    row.a |= BUSTER_IR_FENCE_SIGNAL;
                }
            } break;
            case IR_SYMBOL_ADDR:
            {
                row.a = ir_parse_id(state, '@');
                ir_parse_symbol(state, row.a);
            } break;
            case IR_ADDRESS: ir_parse_address(state, &row); break;
            case IR_CALL:
            case IR_CALL_INDIRECT: ir_parse_call(state, &row); break;
            case IR_PHI: ir_parse_phi(state, &row); break;
            case IR_BR: row.a = ir_parse_id(state, 'b'); break;
            case IR_CBR:
            {
                row.a = ir_parse_id(state, '%');
                ir_parse_expect(state, ',');
                row.b = ir_parse_id(state, 'b');
                ir_parse_expect(state, ',');
                row.c = ir_parse_id(state, 'b');
            } break;
            case IR_SWITCH: ir_parse_switch(state, &row); break;
            case IR_BR_INDIRECT:
            {
                // Its labels, a jump table's (#785), when it lists them: [#k, ...].
                row.a = ir_parse_id(state, '%');
                if (!ir_parse_end_line(state))
                {
                    ir_parse_expect(state, '[');
                    row.b = state->extra.count;
                    row.c = 0;
                    bool more = true;
                    while (state->result.error == IR_PARSE_OK && more)
                    {
                        const uint32_t label = ir_parse_id(state, '#');
                        ir_parse_append(state, &state->extra, &label);
                        row.c += 1;
                        more = ir_parse_take(state, ',');
                    }
                    ir_parse_expect(state, ']');
                }
            } break;
            case IR_RET:
            {
                if (!ir_parse_end_line(state))
                {
                    row.a = ir_parse_id(state, '%');
                }
            } break;
            default:
            {
                const uint32_t mask = ir_operand_table[row.op];
                uint32_t* operands[3] = {
                    &row.a,
                    &row.b,
                    &row.c,
                };
                bool first = true;
                for (uint32_t p = 0; p < 3; p += 1)
                {
                    if ((mask & (1u << p)) != 0)
                    {
                        if (!first)
                        {
                            ir_parse_expect(state, ',');
                        }
                        *operands[p] = ir_parse_id(state, '%');
                        first = false;
                    }
                }
                // A compare-exchange's desired value (#1059), its optional third operand.
                if (row.op == IR_ATOMIC_RMW && ir_parse_take(state, ','))
                {
                    row.c = ir_parse_id(state, '%');
                }
                if (row.op == IR_ICMP || row.op == IR_FCMP || row.op == IR_VCMP)
                {
                    static const char* const names[IR_PREDICATE_COUNT] = {
                        "eq",
                        "ne",
                        "lt",
                        "le",
                        "gt",
                        "ge",
                        "ult",
                        "ule",
                        "ugt",
                        "uge",
                    };
                    row.c = ir_parse_named(state, names, IR_PREDICATE_COUNT);
                    // A vector comparison's lanes (#141's element types), whose type is its mask's.
                    if (row.op == IR_VCMP && ir_parse_word(state, "lanes="))
                    {
                        row.flags = ir_parse_named(state, ir_lane_names, IR_SIMD_LANE_COUNT);
                    }
                }
                // An intrinsic's operation (#141), after its operands as a comparison's predicate is.
                if (row.op == IR_INTRINSIC)
                {
                    uint32_t third = BUSTER_IR_NONE;
                    if (ir_parse_take(state, ','))
                    {
                        row.b = ir_parse_id(state, '%');
                        // A ternary kind's third operand (#1840), which goes to the extra array.
                        if (ir_parse_take(state, ','))
                        {
                            third = ir_parse_id(state, '%');
                        }
                    }
                    row.c = ir_parse_named(state, ir_intrinsic_names, BUSTER_IR_INTRINSIC_COUNT);
                    if (state->result.error == IR_PARSE_OK && ir_intrinsic_ternary(row.c) != (third != BUSTER_IR_NONE))
                    {
                        ir_parse_fail(state, IR_PARSE_SYNTAX);
                    }
                    if (state->result.error == IR_PARSE_OK && third != BUSTER_IR_NONE)
                    {
                        if (state->extra.count > BUSTER_IR_INTRINSIC_EXTRA_LIMIT)
                        {
                            ir_parse_fail(state, IR_PARSE_SYNTAX);
                        }
                        else
                        {
                            row.c = ir_intrinsic_ternary_c(row.c, state->extra.count);
                            ir_parse_append(state, &state->extra, &third);
                        }
                    }
                    // A width change's part (#1840), its flags.
                    if (row.c == BUSTER_IR_INTRINSIC_EXTRACT || row.c == BUSTER_IR_INTRINSIC_INSERT || row.c == BUSTER_IR_INTRINSIC_PERMUTE ||
                        row.c == BUSTER_IR_INTRINSIC_ALIGN || row.c == BUSTER_IR_INTRINSIC_SHUFFLE_LANES || row.c == BUSTER_IR_INTRINSIC_EXTEND ||
                        row.c == BUSTER_IR_INTRINSIC_TRUNCATE || row.c == BUSTER_IR_INTRINSIC_SHIFT ||
                        row.c == BUSTER_IR_INTRINSIC_LANE_OP || ir_intrinsic_ternary(row.c) || row.c == BUSTER_IR_INTRINSIC_AES || row.c == BUSTER_IR_INTRINSIC_LANE_EXTRACT ||
                        row.c == BUSTER_IR_INTRINSIC_LANE_INSERT || row.c == BUSTER_IR_INTRINSIC_MOVEMASK || row.c == BUSTER_IR_INTRINSIC_LANE_UNARY)
                    {
                        row.flags = (uint8_t)ir_parse_unsigned(state, 255, false);
                    }
                }
                // A frame address's save (#1847).
                if (row.op == IR_STACK_SAVE && ir_parse_word(state, "frame"))
                {
                    row.flags = BUSTER_IR_STACK_SAVE_FRAME;
                }
                if (row.op == IR_LOAD || row.op == IR_STORE || row.op == IR_ATOMIC_RMW || row.op == IR_MASKED_LOAD || row.op == IR_MASKED_STORE)
                {
                    ir_parse_keyword(state, "align=");
                    const uint32_t alignment = (uint32_t)ir_parse_unsigned(state, UINT32_C(1) << 31, false);
                    if (state->result.error == IR_PARSE_OK && (alignment == 0 || (alignment & (alignment - 1)) != 0))
                    {
                        ir_parse_fail(state, IR_PARSE_SYNTAX);
                    }
                    if (alignment != 0)
                    {
                        row.alignment_log2 = (uint8_t)__builtin_ctz(alignment);
                    }
                }
                if (row.op == IR_MASKED_LOAD || row.op == IR_MASKED_STORE)
                {
                    // A masked access's lanes (#1840), after its alignment, as its flags.
                    ir_parse_keyword(state, "lanes=");
                    row.flags = (uint8_t)ir_parse_named(state, ir_lane_names, IR_SIMD_LANE_COUNT);
                }
                if (row.op == IR_ATOMIC_RMW)
                {
                    // An atomic read/modify/write's operation and its order (#1059), after its alignment, as its flags.
                    static const char* const operations[3] = {
                        "xchg",
                        "add",
                        "cas",
                    };
                    const uint32_t operation = ir_parse_named(state, operations, 3);
                    const uint32_t order = ir_parse_named(state, ir_fence_orders, BUSTER_IR_FENCE_SEQ_CST + 1);
                    row.flags = (uint8_t)(BUSTER_IR_ATOMIC | (order <= BUSTER_IR_FENCE_SEQ_CST ? order << BUSTER_IR_ORDER_SHIFT : 0) |
                                          (operation < 3 ? operation << 5 : 0));
                }
                if (row.op == IR_LOAD || row.op == IR_STORE)
                {
                    if (ir_parse_word(state, "volatile"))
                    {
                        row.flags = BUSTER_IR_VOLATILE;
                    }
                    // An atomic access's order (#1059), as a fence's is named.
                    if (ir_parse_word(state, "atomic"))
                    {
                        const uint32_t order = ir_parse_named(state, ir_fence_orders, BUSTER_IR_FENCE_SEQ_CST + 1);
                        row.flags = (uint8_t)(row.flags | BUSTER_IR_ATOMIC | (order <= BUSTER_IR_FENCE_SEQ_CST ? order << BUSTER_IR_ORDER_SHIFT : 0));
                    }
                    // A segment override (#1447), as the printer names it.
                    if (ir_parse_word(state, "fs"))
                    {
                        row.flags = (uint8_t)(row.flags | BUSTER_IR_SEGMENT_FS << BUSTER_IR_SEGMENT_SHIFT);
                    }
                    else if (ir_parse_word(state, "gs"))
                    {
                        row.flags = (uint8_t)(row.flags | BUSTER_IR_SEGMENT_GS << BUSTER_IR_SEGMENT_SHIFT);
                    }
                    else if (ir_parse_word(state, "ss"))
                    {
                        row.flags = (uint8_t)(row.flags | BUSTER_IR_SEGMENT_SS << BUSTER_IR_SEGMENT_SHIFT);
                    }
                }
            } break;
        }
    }
    ir_parse_append(state, &state->rows, &row);
}

static uint64_t ir_parse_line_end(IrParseState* state, uint64_t first, bool vector)
{
    (void)vector;

    const String8 source = state->source;
    uint64_t end = first;
    if (BUSTER_VECTOR_PATH(vector))
    {
        bool found = false;
        while (!found)
        {
            if (state->scan_newlines != 0)
            {
                end = state->scan_first + (uint32_t)__builtin_ctzll(state->scan_newlines);
                state->scan_newlines &= state->scan_newlines - 1;
                found = true;
            }
            else if (state->scan_end == source.length)
            {
                end = source.length;
                found = true;
            }
            else
            {
                state->scan_first = state->scan_end;
                const uint32_t count = source.length - state->scan_first < 64 ? (uint32_t)(source.length - state->scan_first) : 64;
                const __mmask64 active = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
                const __m512i bytes = _mm512_maskz_loadu_epi8(active, source.pointer + state->scan_first);
                state->scan_newlines = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\n')) & active;
                state->scan_end += count;
            }
        }
    }
    else
    {
        while (end < source.length && source.pointer[end] != '\n')
        {
            end += 1;
        }
    }
    return end;
}

static void ir_parse_copy(IrParseArray array, void* output)
{
    if (array.count != 0)
    {
        memcpy(output, array.data, (uint64_t)array.count * array.bytes);
    }
}

static void ir_parse_publish(IrParseState* state, Arena* output, bool vector, IrFunction* result)
{
    (void)vector;

    const IrSizes sizes = {
        .instruction_count = state->rows.count,
        .block_count = state->blocks.count,
        .extra_count = state->extra.count,
        .constant_count = state->constants.count,
        .vector_constant_count = state->vector_constants.count,
        .slot_count = state->slots.count,
        .address_count = state->addresses.count,
        .call_count = state->calls.count,
        .call_type_count = state->call_types.count,
        .parameter_count = state->parameters.count,
        .label_count = state->labels.count,
        .abi_call_count = state->abi_calls.count,
        .passed_count = state->passed.count,
        .row_count = state->machine_rows.count,
    };
    IrFunction function = { 0 };
    const IrError created = ir_create(output, &sizes, &function);
    if (created != IR_OK)
    {
        ir_parse_fail(state, created == IR_ERROR_LIMIT ? IR_PARSE_LIMIT : IR_PARSE_MEMORY);
    }
    if (state->result.error == IR_PARSE_OK)
    {
        function.result_type = state->result_type;
        function.symbol_count = state->symbol_count;
        ir_parse_copy(state->blocks, function.block_offsets);
        function.block_offsets[function.block_count] = function.instruction_count;
        ir_parse_copy(state->extra, function.extra);
        ir_parse_copy(state->constants, function.constants);
        ir_parse_copy(state->vector_constants, function.vector_constants);
        ir_parse_copy(state->slots, function.slots);
        ir_parse_copy(state->addresses, function.addresses);
        ir_parse_copy(state->calls, function.calls);
        ir_parse_copy(state->call_types, function.call_types);
        ir_parse_copy(state->parameters, function.parameter_types);
        ir_parse_copy(state->labels, function.labels);
        ir_parse_copy(state->abi_calls, function.abi_calls);
        ir_parse_copy(state->passed, function.passed);
        ir_parse_copy(state->machine_rows, function.rows);
        function.variadic = state->variadic;
        function.incoming = state->incoming;
        function.parameter_passed = state->parameter_passed;
        function.result_size = state->result_size;
        function.result_abi = state->result_abi;
        const IrParseRow* rows = state->rows.data;
        for (uint32_t id = 0; id < function.instruction_count; id += 1)
        {
            function.op[id] = rows[id].op;
            function.type[id] = rows[id].type;
            function.flags[id] = rows[id].flags;
            function.alignment_log2[id] = rows[id].alignment_log2;
            function.a[id] = rows[id].a;
            function.b[id] = rows[id].b;
            function.c[id] = rows[id].c;
        }
        state->result.check = ir_verify_complete(&function, state->scratch, BUSTER_VECTOR_PATH(vector));
        if (state->result.check.error == IR_OK)
        {
            *result = function;
        }
        else
        {
            const IrError error = state->result.check.error;
            ir_parse_fail(state, error == IR_ERROR_MEMORY ? IR_PARSE_MEMORY : error == IR_ERROR_LIMIT ? IR_PARSE_LIMIT : IR_PARSE_VERIFY);
            if (state->result.check.instruction < function.instruction_count)
            {
                const IrParseRow row = rows[state->result.check.instruction];
                state->result.offset = row.offset;
                state->result.line = row.line;
                state->result.column = 1;
                uint64_t first = row.offset;
                while (first != 0 && state->source.pointer[first - 1] != '\n')
                {
                    state->result.column += 1;
                    first -= 1;
                }
            }
        }
    }
}

static IrParseResult ir_parse_path(String8 source, Arena* output, Arena* scratch, IrFunction* result, bool vector)
{
    (void)vector;

    IrParseState state = {
        .source = source,
        .scratch = scratch,
        .line = 1,
        .rows.bytes = sizeof(IrParseRow),
        .blocks.bytes = sizeof(uint32_t),
        .extra.bytes = sizeof(uint32_t),
        .constants.bytes = sizeof(IrConstant),
        .vector_constants.bytes = sizeof(IrSimdValue),
        .slots.bytes = sizeof(IrSlot),
        .addresses.bytes = sizeof(IrAddress),
        .calls.bytes = sizeof(IrCall),
        .call_types.bytes = sizeof(uint8_t),
        .parameters.bytes = sizeof(uint8_t),
        .labels.bytes = sizeof(uint32_t),
        .abi_calls.bytes = sizeof(IrAbiCall),
        .passed.bytes = sizeof(IrPassed),
        .machine_rows.bytes = sizeof(Tier0Operation),
        .symbol_count = 1,
        .result.check.instruction = BUSTER_IR_NONE,
    };
    if (output == 0 || scratch == 0 || result == 0 || output == scratch || (source.length != 0 && source.pointer == 0))
    {
        ir_parse_fail(&state, IR_PARSE_SYNTAX);
    }
    const uint64_t output_mark = output != 0 ? output->length : 0;
    const uint64_t scratch_mark = scratch != 0 ? scratch->length : 0;
    bool header = false;
    bool finished = false;
    uint64_t next = 0;
    while (state.result.error == IR_PARSE_OK && !finished && next < source.length)
    {
        state.cursor = next;
        state.line_start = next;
        state.end = ir_parse_line_end(&state, next, BUSTER_VECTOR_PATH(vector));
        next = state.end + (state.end < source.length);
        if (!ir_parse_end_line(&state))
        {
            if (!header)
            {
                ir_parse_header(&state);
                header = true;
            }
            else if (ir_parse_take(&state, '}'))
            {
                finished = true;
            }
            else if (state.source.pointer[state.cursor] == 's' && state.blocks.count == 0)
            {
                ir_parse_slot(&state);
            }
            else if (state.source.pointer[state.cursor] == 'l' && state.blocks.count == 0)
            {
                ir_parse_label(&state);
            }
            else if (state.source.pointer[state.cursor] == 'p' && state.blocks.count == 0)
            {
                ir_parse_passed(&state);
            }
            else if (state.source.pointer[state.cursor] == 'a' && state.blocks.count == 0)
            {
                ir_parse_abi(&state);
            }
            else if ((state.source.pointer[state.cursor] == 'v' || state.source.pointer[state.cursor] == 'r') && state.blocks.count == 0)
            {
                ir_parse_frame(&state);
            }
            else if (state.source.pointer[state.cursor] == 'b')
            {
                const uint32_t block = ir_parse_id(&state, 'b');
                ir_parse_expect(&state, ':');
                if (state.result.error == IR_PARSE_OK && block != state.blocks.count)
                {
                    ir_parse_fail(&state, IR_PARSE_SYNTAX);
                }
                ir_parse_append(&state, &state.blocks, &state.rows.count);
            }
            else
            {
                ir_parse_instruction(&state);
            }
            if (state.result.error == IR_PARSE_OK && !ir_parse_end_line(&state))
            {
                ir_parse_fail(&state, IR_PARSE_SYNTAX);
            }
        }
        if (!finished && state.end < source.length)
        {
            state.line += 1;
            state.line_start = next;
        }
    }
    if (state.result.error == IR_PARSE_OK)
    {
        state.result.consumed = next;
        if (!header)
        {
            state.result.error = IR_PARSE_END;
        }
        else if (!finished)
        {
            state.cursor = source.length;
            ir_parse_fail(&state, IR_PARSE_SYNTAX);
        }
        else
        {
            ir_parse_publish(&state, output, BUSTER_VECTOR_PATH(vector), result);
        }
    }
    if (state.result.error != IR_PARSE_OK && output != 0 && output->length >= output_mark)
    {
        arena_pop_to(output, output_mark);
    }
    if (scratch != 0 && scratch->length >= scratch_mark)
    {
        arena_pop_to(scratch, scratch_mark);
    }
    return state.result;
}

static IrParseResult ir_parse_scalar(String8 source, Arena* output, Arena* scratch, IrFunction* result);

static IrParseResult ir_parse(String8 source, Arena* output, Arena* scratch, IrFunction* result)
{
#if !BUSTER_HOST_VECTOR
    return ir_parse_scalar(source, output, scratch, result);
#else
    return ir_parse_path(source, output, scratch, result, true);
#endif
}

static IrParseResult ir_parse_scalar(String8 source, Arena* output, Arena* scratch, IrFunction* result)
{
    return ir_parse_path(source, output, scratch, result, false);
}
