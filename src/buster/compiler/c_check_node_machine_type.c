// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2524's node machine's T sweep (c_check_node_machine.c has the machine): node_machine_type types each record up the
// postorder and writes its columns, which C and X read through the record accessors. A record is typed by one of four
// paths, in this file's order:
//
// - a leaf, by the reader's leaf record (node_machine_leaf);
// - an operator of builtin arithmetic operands, by its row of the transition table (node_machine_row), which
//   node_machine_tables_initialize makes from the actions and baked.c bakes;
// - the lookups, by direct rules (node_machine_rule: node_machine_discard, _member, _truth, _logic, _store_rule,
//   _address_rule, _dereference, _pointer_rule and _call_rule), each its action's own steps for the common case behind a
//   guard;
// - everything else, by the action itself (node_machine_action), the rows' and the rules' exact twin, which --scalar
//   takes for every node and --verify compares with them (node_machine_same_as_action).
//
// A record's value is the design's columns (docs/node-machine.md, "The records and the tapes"; NodeMachine): the rows and
// the rules read their operands' columns and write their records' (node_machine_record, node_machine_record_from), and
// make a value whole (node_machine_record_value, node_machine_put_value) only where the action's own functions convert
// it: a node the action types, --verify's twin, a constant the rules don't fold, and a value the columns don't hold whole.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_TYPE_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_TYPE_INCLUDED

// The builtin types a row's operand may be: a code below this.
#define BUSTER_NODE_MACHINE_KINDS 24u

// The operators T's table has rows for, by their index in it; the others take the action.
typedef enum NodeMachineRowOperator
{
    NODE_MACHINE_ROW_NONE,
    NODE_MACHINE_ROW_NEGATIVE,
    NODE_MACHINE_ROW_BIT_NOT,
    NODE_MACHINE_ROW_ADD,
    NODE_MACHINE_ROW_SUBTRACT,
    NODE_MACHINE_ROW_MULTIPLY,
    NODE_MACHINE_ROW_DIVIDE,
    NODE_MACHINE_ROW_REMAINDER,
    NODE_MACHINE_ROW_SHIFT_LEFT,
    NODE_MACHINE_ROW_SHIFT_RIGHT,
    NODE_MACHINE_ROW_LESS,
    NODE_MACHINE_ROW_LESS_EQUAL,
    NODE_MACHINE_ROW_GREATER,
    NODE_MACHINE_ROW_GREATER_EQUAL,
    NODE_MACHINE_ROW_EQUAL,
    NODE_MACHINE_ROW_NOT_EQUAL,
    NODE_MACHINE_ROW_BIT_AND,
    NODE_MACHINE_ROW_BIT_OR,
    NODE_MACHINE_ROW_BIT_XOR,
    NODE_MACHINE_ROW_OPERATORS,
} NodeMachineRowOperator;

static const uint8_t node_machine_row_operators[CHECK_OP_COUNT] = {
    [CHECK_OP_NEGATIVE] = NODE_MACHINE_ROW_NEGATIVE,
    [CHECK_OP_BIT_NOT] = NODE_MACHINE_ROW_BIT_NOT,
    [CHECK_OP_ADD] = NODE_MACHINE_ROW_ADD,
    [CHECK_OP_SUBTRACT] = NODE_MACHINE_ROW_SUBTRACT,
    [CHECK_OP_MULTIPLY] = NODE_MACHINE_ROW_MULTIPLY,
    [CHECK_OP_DIVIDE] = NODE_MACHINE_ROW_DIVIDE,
    [CHECK_OP_REMAINDER] = NODE_MACHINE_ROW_REMAINDER,
    [CHECK_OP_SHIFT_LEFT] = NODE_MACHINE_ROW_SHIFT_LEFT,
    [CHECK_OP_SHIFT_RIGHT] = NODE_MACHINE_ROW_SHIFT_RIGHT,
    [CHECK_OP_LESS] = NODE_MACHINE_ROW_LESS,
    [CHECK_OP_LESS_EQUAL] = NODE_MACHINE_ROW_LESS_EQUAL,
    [CHECK_OP_GREATER] = NODE_MACHINE_ROW_GREATER,
    [CHECK_OP_GREATER_EQUAL] = NODE_MACHINE_ROW_GREATER_EQUAL,
    [CHECK_OP_EQUAL] = NODE_MACHINE_ROW_EQUAL,
    [CHECK_OP_NOT_EQUAL] = NODE_MACHINE_ROW_NOT_EQUAL,
    [CHECK_OP_BIT_AND] = NODE_MACHINE_ROW_BIT_AND,
    [CHECK_OP_BIT_OR] = NODE_MACHINE_ROW_BIT_OR,
    [CHECK_OP_BIT_XOR] = NODE_MACHINE_ROW_BIT_XOR,
};

static const CheckOp node_machine_row_ops[NODE_MACHINE_ROW_OPERATORS] = {
    [NODE_MACHINE_ROW_NEGATIVE] = CHECK_OP_NEGATIVE,
    [NODE_MACHINE_ROW_BIT_NOT] = CHECK_OP_BIT_NOT,
    [NODE_MACHINE_ROW_ADD] = CHECK_OP_ADD,
    [NODE_MACHINE_ROW_SUBTRACT] = CHECK_OP_SUBTRACT,
    [NODE_MACHINE_ROW_MULTIPLY] = CHECK_OP_MULTIPLY,
    [NODE_MACHINE_ROW_DIVIDE] = CHECK_OP_DIVIDE,
    [NODE_MACHINE_ROW_REMAINDER] = CHECK_OP_REMAINDER,
    [NODE_MACHINE_ROW_SHIFT_LEFT] = CHECK_OP_SHIFT_LEFT,
    [NODE_MACHINE_ROW_SHIFT_RIGHT] = CHECK_OP_SHIFT_RIGHT,
    [NODE_MACHINE_ROW_LESS] = CHECK_OP_LESS,
    [NODE_MACHINE_ROW_LESS_EQUAL] = CHECK_OP_LESS_EQUAL,
    [NODE_MACHINE_ROW_GREATER] = CHECK_OP_GREATER,
    [NODE_MACHINE_ROW_GREATER_EQUAL] = CHECK_OP_GREATER_EQUAL,
    [NODE_MACHINE_ROW_EQUAL] = CHECK_OP_EQUAL,
    [NODE_MACHINE_ROW_NOT_EQUAL] = CHECK_OP_NOT_EQUAL,
    [NODE_MACHINE_ROW_BIT_AND] = CHECK_OP_BIT_AND,
    [NODE_MACHINE_ROW_BIT_OR] = CHECK_OP_BIT_OR,
    [NODE_MACHINE_ROW_BIT_XOR] = CHECK_OP_BIT_XOR,
};

// One row of T's table: what the action gives an operator of two builtin arithmetic types' rvalues without flags or
// constants (one of them for a unary operator): the result's type (its code) and flags, and each operand's conversion's
// type (its code) and flags. `standing`: which operands may instead be an lvalue (whose conversion then takes
// BUSTER_CHECK_CONVERT_LVALUE besides) and which a constant, where the other is none, and give the same answer. An
// operand's flags in the operator's `allowed` mask either pass to the result (its `passed` ones) or change nothing.
// `result` 0: no row, the action types the node.
typedef struct NodeMachineRow NodeMachineRow;
struct NodeMachineRow
{
    uint8_t result;
    uint8_t converted[2];
    uint8_t standing;
    uint16_t conversion_flags[2];
    uint32_t flags;
};

_Static_assert(sizeof(NodeMachineRow) == 12, "a row is twelve bytes");

#define BUSTER_NODE_MACHINE_ROW_LVALUE(operand) (0x01u << (operand))
#define BUSTER_NODE_MACHINE_ROW_CONSTANT(operand) (0x04u << (operand))
// The flags a constant operand of a row may have, its literal's: an integer constant expression's, a floating literal's
// and a literal zero's.
#define BUSTER_NODE_MACHINE_ROW_CONSTANT_FLAGS (BUSTER_CHECK_ICE | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_LITERAL_ZERO)

// The operand flags node_machine_tables_initialize tries one at a time on every row of an operator: each one that passes
// to the result alone on every row, or changes nothing on every row, is in the operator's `allowed` mask.
static const uint32_t node_machine_row_candidates[] = {
    BUSTER_CHECK_SIDE_EFFECT,
    BUSTER_CHECK_NONNULL_ADDRESS,
    BUSTER_CHECK_STATIC_OBJECT,
    BUSTER_CHECK_CONST_READ_RESTRICTED,
    BUSTER_CHECK_MODIFICATION,
    BUSTER_CHECK_DISCARDED_UNEVALUABLE,
    BUSTER_CHECK_GNU_OBJECT_READ,
    BUSTER_CHECK_GNU_CONSTANT,
    BUSTER_CHECK_INACTIVE_UNION_READ,
    BUSTER_CHECK_INACTIVE_UNION_ADDRESS,
    BUSTER_CHECK_GNU_DIVISION,
    BUSTER_CHECK_GNU_FLOAT_RANGE,
};

// T's table, made by node_machine_tables_initialize from the actions and baked (baked.c): by row operator, then the
// first operand's builtin type's code, then the second's (0 for a unary operator); and by row operator, the flags its
// operands may have and those of them that pass to its result.
static BUSTER_BAKED_UNIT NodeMachineRow node_machine_rows[NODE_MACHINE_ROW_OPERATORS][BUSTER_NODE_MACHINE_KINDS][BUSTER_NODE_MACHINE_KINDS];
static BUSTER_BAKED_UNIT uint32_t node_machine_row_allowed[NODE_MACHINE_ROW_OPERATORS];
static BUSTER_BAKED_UNIT uint32_t node_machine_row_passed[NODE_MACHINE_ROW_OPERATORS];
static BUSTER_BAKED_UNIT uint32_t node_machine_row_count;
// Each builtin arithmetic type's width in bits where it is an integer type of at most 64 bits other than bool (0
// otherwise) and whether it is signed, by its code: what a store rule's constant conversion asks of
// check_action_integer_type.
static BUSTER_BAKED_UNIT uint8_t node_machine_integer_widths[BUSTER_NODE_MACHINE_KINDS];
static BUSTER_BAKED_UNIT bool node_machine_integer_signed[BUSTER_NODE_MACHINE_KINDS];
static BUSTER_BAKED_UNIT bool node_machine_tables_ready;

// The builtin types a row's operand may be: the arithmetic ones, from bool to unsigned __int128 and float and double.
static bool node_machine_row_kind(uint32_t code)
{
    return (code >= BUSTER_TYPE_CODE_BOOL && code <= BUSTER_TYPE_CODE_UINT128) || code == BUSTER_TYPE_CODE_FLOAT || code == BUSTER_TYPE_CODE_DOUBLE;
}

// An operand of builtin type code `code` for the rows' trials: an rvalue with `flags`; or (`standing` 1) an lvalue of a
// const object of the type, at a static object's address; or (2 and 3) the type's constant 1 or 0, as its literal is.
static CheckValue node_machine_row_operand(uint32_t code, uint32_t flags, uint32_t standing)
{
    CheckValue value;
    memset(&value, 0, sizeof(value));
    const bool floating = code == BUSTER_TYPE_CODE_FLOAT || code == BUSTER_TYPE_CODE_DOUBLE;
    value.type = BUSTER_TYPE_BUILTIN(code);
    value.flags = flags;
    if (standing == 1)
    {
        value.type |= BUSTER_TYPE_CONST;
        value.category = CHECK_LVALUE;
        value.flags |= BUSTER_CHECK_NONNULL_ADDRESS | BUSTER_CHECK_STATIC_OBJECT;
        value.constant_kind = CHECK_CONSTANT_ADDRESS;
        value.object = 1;
    }
    else if (standing >= 2)
    {
        const bool one = standing == 2;
        value.constant_kind = floating ? CHECK_CONSTANT_FLOATING : CHECK_CONSTANT_INTEGER;
        value.flags |= (floating ? BUSTER_CHECK_FLOAT_LITERAL : BUSTER_CHECK_ICE) | (one ? 0 : BUSTER_CHECK_LITERAL_ZERO);
        value.integer = one && !floating ? 1 : 0;
        value.floating = one && floating ? 1.0L : 0.0L;
    }
    return value;
}

// The action's answer for `op` of operands `a` and `b` (b's type 0 for a unary operator).
static void node_machine_row_action(TypeTable* types, CheckOp op, const CheckValue* a, const CheckValue* b, CheckActionResult* result)
{
    CheckAction action;
    memset(&action, 0, sizeof(action));
    action.op = op;
    action.node = 1;
    action.operands[0] = *a;
    action.operands[1] = *b;
    memset(result, 0, sizeof(*result));
    check_action_compute(types, &action, result, 0, NULL);
}

// Whether a trial's answer is the row's: the plain operands' answer `plain`, its result's flags with `passed` besides
// and operand `lvalue`'s conversion (UINT32_MAX for none) with BUSTER_CHECK_CONVERT_LVALUE; warnings aside where `warnings` is
// false (a constant operand's value can warn, which the stream's records don't hold).
static bool node_machine_row_same(const CheckActionResult* plain, const CheckActionResult* trial, uint32_t passed, uint32_t lvalue, bool warnings)
{
    bool same = plain->value.type == trial->value.type && plain->value.category == trial->value.category &&
                (plain->value.flags | passed) == trial->value.flags && plain->value.constant_kind == trial->value.constant_kind &&
                trial->value.object == 0 && trial->value.offset == 0 && trial->value.fault == CHECK_FAULT_NONE && plain->diagnostic == trial->diagnostic &&
                (!warnings || plain->warnings == trial->warnings) && plain->member_offset == trial->member_offset;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        same = same && plain->conversions[i].type == trial->conversions[i].type &&
               (plain->conversions[i].flags | (i == lvalue ? BUSTER_CHECK_CONVERT_LVALUE : 0)) == trial->conversions[i].flags;
    }
    same = same && plain->store_conversion.type == trial->store_conversion.type && plain->store_conversion.flags == trial->store_conversion.flags;
    return same;
}

// Makes node_machine_rows from the actions: for each row operator and each pair of builtin arithmetic types, the
// action's answer for rvalues of them, kept where it has no diagnostic and no warning and its types are builtin ones;
// then each operand tried as an lvalue and as a constant 1 and 0 (the other operand plain), and each candidate flag on
// each operand; and node_machine_integer_widths, from check_action_integer_type. Its type table, of the builtin types
// alone, lives on its stack: the bake checks that the initializers write nothing outside buster_baked.
static bool node_machine_tables_initialize(void)
{
    bool valid = true;
    if (!node_machine_tables_ready)
    {
        const TypeCapacity capacity = { .types = 16, .parameters = 16, .tags = 4, .members = 4, .lookups = 64 };
        uint8_t storage[1u << 16] __attribute__((aligned(64)));
        uint8_t index_storage[1u << 14] __attribute__((aligned(64)));
        Arena arena = { .base = storage, .capacity = sizeof(storage) };
        Arena index = { .base = index_storage, .capacity = sizeof(index_storage) };
        TypeTable table = { 0 };
        valid = type_capacity_bytes(capacity) <= sizeof(storage) && type_index_capacity_bytes(capacity) <= sizeof(index_storage) &&
                type_initialize(&table, &arena, &index, capacity);
        for (uint32_t code = 0; valid && code < BUSTER_NODE_MACHINE_KINDS; code += 1)
        {
            uint32_t width = 0;
            bool is_signed = false;
            const bool integer = node_machine_row_kind(code) && code != BUSTER_TYPE_CODE_BOOL && check_action_integer_type(&table, BUSTER_TYPE_BUILTIN(code), &width, &is_signed);
            node_machine_integer_widths[code] = (uint8_t)(integer && width <= 64 ? width : 0);
            node_machine_integer_signed[code] = integer && is_signed;
        }
        uint32_t count = 0;
        for (uint32_t o = 1; valid && o < NODE_MACHINE_ROW_OPERATORS; o += 1)
        {
            const CheckOp op = node_machine_row_ops[o];
            const bool unary = op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
            const uint32_t operands = unary ? 1 : 2;
            uint32_t passed = 0;
            uint32_t dropped = 0;
            uint32_t mixed = 0;
            for (uint32_t a = 0; a < BUSTER_NODE_MACHINE_KINDS; a += 1)
            {
                for (uint32_t b = 0; node_machine_row_kind(a) && b < (unary ? 1u : BUSTER_NODE_MACHINE_KINDS); b += 1)
                {
                    NodeMachineRow row = { 0 };
                    if (unary || node_machine_row_kind(b))
                    {
                        const uint32_t codes[2] = { a, unary ? 0 : b };
                        CheckValue plain_operands[2] = { node_machine_row_operand(a, 0, 0), unary ? (CheckValue){ 0 } : node_machine_row_operand(b, 0, 0) };
                        CheckActionResult plain;
                        node_machine_row_action(&table, op, plain_operands, plain_operands + 1, &plain);
                        const uint32_t result = type_index(plain.value.type);
                        const uint32_t converted_a = type_index(plain.conversions[0].type);
                        const uint32_t converted_b = type_index(plain.conversions[1].type);
                        const bool builtin = result < BUSTER_NODE_MACHINE_KINDS && converted_a < BUSTER_NODE_MACHINE_KINDS && converted_b < BUSTER_NODE_MACHINE_KINDS &&
                                             (plain.value.type & BUSTER_TYPE_QUALIFIERS) == 0 && plain.value.type != 0 &&
                                             (plain.conversions[0].type & BUSTER_TYPE_QUALIFIERS) == 0 && (plain.conversions[1].type & BUSTER_TYPE_QUALIFIERS) == 0;
                        const bool kept = builtin && plain.diagnostic == CHECK_DIAG_NONE && plain.warnings == 0 && plain.value.category == CHECK_RVALUE &&
                                          plain.value.constant_kind == CHECK_CONSTANT_NONE && plain.value.fault == CHECK_FAULT_NONE &&
                                          plain.conversions[2].type == 0 && plain.store_conversion.type == 0 && plain.member_offset == 0 &&
                                          plain.conversions[0].flags <= UINT16_MAX && plain.conversions[1].flags <= UINT16_MAX;
                        uint32_t standing = 0;
                        for (uint32_t k = 0; kept && k < operands; k += 1)
                        {
                            CheckValue trial_operands[2] = { plain_operands[0], plain_operands[1] };
                            CheckActionResult trial;
                            trial_operands[k] = node_machine_row_operand(codes[k], 0, 1);
                            node_machine_row_action(&table, op, trial_operands, trial_operands + 1, &trial);
                            standing |= node_machine_row_same(&plain, &trial, 0, k, true) ? BUSTER_NODE_MACHINE_ROW_LVALUE(k) : 0;
                            bool constant = true;
                            for (uint32_t value = 2; value <= 3; value += 1)
                            {
                                trial_operands[k] = node_machine_row_operand(codes[k], 0, value);
                                node_machine_row_action(&table, op, trial_operands, trial_operands + 1, &trial);
                                constant = constant && node_machine_row_same(&plain, &trial, 0, UINT32_MAX, false);
                            }
                            standing |= constant ? BUSTER_NODE_MACHINE_ROW_CONSTANT(k) : 0;
                            for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(node_machine_row_candidates); c += 1)
                            {
                                const uint32_t flag = node_machine_row_candidates[c];
                                trial_operands[k] = node_machine_row_operand(codes[k], flag, 0);
                                node_machine_row_action(&table, op, trial_operands, trial_operands + 1, &trial);
                                const bool passes = node_machine_row_same(&plain, &trial, flag, UINT32_MAX, true) && (plain.value.flags & flag) == 0;
                                const bool drops = node_machine_row_same(&plain, &trial, 0, UINT32_MAX, true);
                                passed |= passes ? flag : 0;
                                dropped |= drops && !passes ? flag : 0;
                                mixed |= !passes && !drops ? flag : 0;
                            }
                        }
                        if (kept)
                        {
                            row = (NodeMachineRow){
                                .result = (uint8_t)result,
                                .converted = { (uint8_t)converted_a, (uint8_t)converted_b },
                                .standing = (uint8_t)standing,
                                .conversion_flags = { (uint16_t)plain.conversions[0].flags, (uint16_t)plain.conversions[1].flags },
                                .flags = plain.value.flags,
                            };
                            count += 1;
                        }
                    }
                    node_machine_rows[o][a][b] = row;
                }
            }
            // A flag that passes on one row and changes nothing on another, or anything else on any, is the action's.
            const uint32_t allowed = (passed ^ dropped) & ~mixed & ~(passed & dropped);
            node_machine_row_allowed[o] = allowed;
            node_machine_row_passed[o] = passed & allowed;
        }
        node_machine_row_count = valid ? count : 0;
        node_machine_tables_ready = valid;
    }
    return valid;
}

// The first source position at which check_context_source_complete takes `type` as complete, with `graph`'s tag history:
// none (UINT32_MAX) for an incomplete type, every one (0) without a history or for a type whose base, past its arrays,
// names no tag, and else its tag's completion's (none past the history).
static uint32_t node_machine_complete_at(const TypeTable* types, const CheckGraph* graph, TypeId type)
{
    uint32_t position = type_is_complete(types, type) ? 0 : UINT32_MAX;
    if (position == 0 && graph->tag_completions != NULL)
    {
        TypeId base = type_strip_aligned(types, type);
        while (type_kind_is_array(type_kind(types, base)))
        {
            base = type_strip_aligned(types, type_element(types, base));
        }
        const uint8_t kind = type_kind(types, base);
        const uint32_t tag = types->types.index[type_index(base)];
        if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM)
        {
            position = tag < graph->tag_completion_count ? graph->tag_completions[tag] : UINT32_MAX;
        }
    }
    return position;
}

// Whether `type` is complete at node `node`'s source position (check_action_source_complete's answer for an action at the
// node), by the unit's table, and by the function for a type past it.
static bool node_machine_complete(const NodeMachine* machine, TypeId type, uint32_t node)
{
    const CheckGraph* graph = machine->graph;
    const uint32_t index = type_index(type);
    const uint32_t position = graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE;
    bool complete = false;
    if (index < machine->type_count)
    {
        // UINT32_MAX is never, even at a node without a position; a rule refused for it takes the action instead.
        const uint32_t at = machine->type_complete_at[index];
        complete = at != UINT32_MAX && at <= position;
    }
    else
    {
        CheckAction context = { .node = node };
        context.context = check_graph_context(graph, node, false);
        complete = check_action_source_complete(machine->types, type, &context);
    }
    return complete;
}

// Whether the operators of `op` are the machine's: the census's set within the templates' (the file's comment).
static bool node_machine_code(CheckOp op)
{
    bool code = false;
    switch (op)
    {
        case CHECK_OP_VALUE: case CHECK_OP_POSITIVE: case CHECK_OP_NEGATIVE: case CHECK_OP_NOT: case CHECK_OP_BIT_NOT: case CHECK_OP_ADD:
        case CHECK_OP_SUBTRACT: case CHECK_OP_MULTIPLY: case CHECK_OP_DIVIDE: case CHECK_OP_REMAINDER: case CHECK_OP_SHIFT_LEFT:
        case CHECK_OP_SHIFT_RIGHT: case CHECK_OP_LESS: case CHECK_OP_LESS_EQUAL: case CHECK_OP_GREATER: case CHECK_OP_GREATER_EQUAL:
        case CHECK_OP_EQUAL: case CHECK_OP_NOT_EQUAL: case CHECK_OP_BIT_AND: case CHECK_OP_BIT_OR: case CHECK_OP_BIT_XOR: case CHECK_OP_LOGICAL_AND:
        case CHECK_OP_LOGICAL_OR: case CHECK_OP_CONDITIONAL: case CHECK_OP_CAST:
        case CHECK_OP_ADDRESS: case CHECK_OP_DEREFERENCE: case CHECK_OP_ASSIGN: case CHECK_OP_COMMA: case CHECK_OP_SUBSCRIPT: case CHECK_OP_SIZEOF:
        case CHECK_OP_ALIGNOF: case CHECK_OP_MEMBER: case CHECK_OP_ARROW: case CHECK_OP_INITIALIZE: case CHECK_OP_CONVERT: case CHECK_OP_CONDITION:
        case CHECK_OP_SELECT: case CHECK_OP_CALL: case CHECK_OP_PRE_INCREMENT: case CHECK_OP_PRE_DECREMENT: case CHECK_OP_POST_INCREMENT:
        case CHECK_OP_POST_DECREMENT: case CHECK_OP_ADD_ASSIGN: case CHECK_OP_SUBTRACT_ASSIGN: case CHECK_OP_MULTIPLY_ASSIGN: case CHECK_OP_DIVIDE_ASSIGN:
        case CHECK_OP_REMAINDER_ASSIGN: case CHECK_OP_SHIFT_LEFT_ASSIGN: case CHECK_OP_SHIFT_RIGHT_ASSIGN: case CHECK_OP_BIT_AND_ASSIGN: case CHECK_OP_BIT_OR_ASSIGN:
        case CHECK_OP_BIT_XOR_ASSIGN:
            code = true;
            break;
        default:
            break;
    }
    return code;
}

// Record `r`'s value from its fields, which the columns hold whole: no rare field, no offset and no floating value (the
// caller knows), and its integer.
static inline __attribute__((always_inline)) void node_machine_record(NodeMachine* machine, uint32_t r, TypeId type, CheckCategory category, uint32_t flags,
                                                                      CheckConstantKind constant_kind, uint32_t object, uint64_t payload)
{
    machine->columns->value_types[r] = type;
    machine->columns->value_flags[r] = flags;
    machine->columns->objects[r] = object;
    machine->columns->payloads[r] = payload;
    machine->columns->standings[r] = BUSTER_NODE_MACHINE_STANDING(category, constant_kind);
}

// Record `r`'s value from `value`: into its columns where they hold it whole (node_machine_record_value gives it back
// field for field: at most one of its integer, offset and floating value is no zero), and else into `wides`.
static void node_machine_put_value(NodeMachine* machine, uint32_t r, const CheckValue* value)
{
    const CheckConstantKind kind = value->constant_kind;
    const uint64_t low = (uint64_t)value->integer;
    const uint64_t high = (uint64_t)(value->integer >> 64);
    const uint64_t offset = (uint64_t)value->offset;
    const uint64_t significand = value->floating_storage[0];
    const uint64_t exponent = value->floating_storage[1] & 0xffffu;
    const bool floating = significand != 0 || exponent != 0;
    const bool rare = high != 0 || value->imaginary_storage[0] != 0 || (value->imaginary_storage[1] & 0xffffu) != 0 || value->overload_head != 0 ||
                      value->bit_width != 0 || value->fault != CHECK_FAULT_NONE || value->fault_node != 0 || value->object_bit_offset != 0 ||
                      (uint32_t)value->category > 0x03u || (uint32_t)kind > 0x07u;
    const uint32_t fields = (low != 0) + (offset != 0) + floating;
    const bool whole = !rare && fields <= 1;
    machine->columns->value_types[r] = value->type;
    machine->columns->value_flags[r] = value->flags;
    machine->columns->objects[r] = value->object;
    machine->columns->exponents[r] = (uint16_t)exponent;
    uint8_t standing = BUSTER_NODE_MACHINE_STANDING(value->category & 0x03u, kind & 0x07u);
    if (whole)
    {
        standing |= offset != 0 ? BUSTER_NODE_MACHINE_OFFSET : floating ? BUSTER_NODE_MACHINE_FLOATING : 0;
        machine->columns->payloads[r] = offset != 0 ? offset : floating ? significand : low;
    }
    else
    {
        const uint32_t w = machine->wide_count;
        check_value_copy(machine->wides + w, value);
        machine->wide_count = w + 1;
        machine->columns->payloads[r] = w;
        standing |= BUSTER_NODE_MACHINE_WIDE;
    }
    machine->columns->standings[r] = standing;
}

// Record `r`'s value: record `a`'s with its type, category, flags and constant kind replaced, and its integer by `integer`
// where `replace`: into the columns, and else whole, where record `a`'s is wide or `integer` would be beside an offset or
// a floating value (whose payload, a significand, can be zero beside its sign and exponent).
static inline __attribute__((always_inline)) void node_machine_record_from(NodeMachine* machine, uint32_t r, uint32_t a, TypeId type, CheckCategory category,
                                                                           uint32_t flags, CheckConstantKind constant_kind, bool replace, uint64_t integer)
{
    const uint8_t field = machine->columns->standings[a] & BUSTER_NODE_MACHINE_FIELD;
    const uint64_t payload = machine->columns->payloads[a];
    if ((machine->columns->standings[a] & BUSTER_NODE_MACHINE_WIDE) == 0 && (!replace || field == 0))
    {
        machine->columns->value_types[r] = type;
        machine->columns->value_flags[r] = flags;
        machine->columns->objects[r] = machine->columns->objects[a];
        machine->columns->payloads[r] = replace ? integer : payload;
        machine->columns->exponents[r] = machine->columns->exponents[a];
        machine->columns->standings[r] = BUSTER_NODE_MACHINE_STANDING(category, constant_kind) | (replace ? 0 : field);
    }
    else
    {
        CheckValue value;
        node_machine_record_value(machine, a, &value);
        value.type = type;
        value.category = category;
        value.flags = flags;
        value.constant_kind = constant_kind;
        value.integer = integer;
        node_machine_put_value(machine, r, &value);
    }
}

// Record `r`'s operand `k` (0 to 2) as a record, or UINT32_MAX for none; BUSTER_NODE_MACHINE_MOST where it is outside the
// expression's range.
static uint32_t node_machine_operand(const NodeMachine* machine, uint32_t r, uint32_t k)
{
    const CheckGraph* graph = machine->graph;
    const uint32_t node = node_machine_node(machine, r);
    const uint32_t operand = k == 0 ? graph->operand0[node] : k == 1 ? graph->operand1[node] : graph->operand2[node];
    const uint32_t record = operand - machine->first;
    return operand == BUSTER_CHECK_NONE ? UINT32_MAX : operand >= machine->first && record < r ? record : BUSTER_NODE_MACHINE_MOST;
}

// Whether a value of standing `standing` and flags `flags`, operand `slot` of a row, takes row `row`'s answer, the other
// operand's standing being `other` (BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE) for none): an rvalue
// or (where the row says) an lvalue of the row's builtin type with no flag outside the operator's allowed ones; an
// lvalue's constant is its address, which its conversion drops; an rvalue's none, or (where the row says) a literal's
// where `other` has none. node_machine_row_plain checks the rest.
static bool node_machine_row_takes(const NodeMachineRow* row, uint32_t allowed, uint8_t standing, uint32_t flags, uint8_t other, uint32_t slot)
{
    const CheckCategory category = (CheckCategory)(standing & 0x03u);
    const CheckConstantKind kind = (CheckConstantKind)((standing >> 2) & 0x07u);
    const bool lvalue = category == CHECK_LVALUE;
    const bool constant = kind == CHECK_CONSTANT_INTEGER || kind == CHECK_CONSTANT_FLOATING;
    const bool other_constant = (other >> 2 & 0x07u) != CHECK_CONSTANT_NONE && (other & 0x03u) == CHECK_RVALUE;
    const uint32_t checked = flags & ~(constant && !lvalue ? (uint32_t)BUSTER_NODE_MACHINE_ROW_CONSTANT_FLAGS : 0u);
    const bool stands = lvalue ? (row->standing & BUSTER_NODE_MACHINE_ROW_LVALUE(slot)) != 0 && (kind == CHECK_CONSTANT_NONE || kind == CHECK_CONSTANT_ADDRESS) :
                        category == CHECK_RVALUE && (kind == CHECK_CONSTANT_NONE || (constant && !other_constant && (row->standing & BUSTER_NODE_MACHINE_ROW_CONSTANT(slot)) != 0));
    return stands && (checked & ~allowed) == 0;
}

// Whether record `k`'s value has no overload set, fault or bit-field width, which the rows don't take: a wide value's
// fields, and none of the columns'.
static inline bool node_machine_row_plain(const NodeMachine* machine, uint32_t k)
{
    const CheckValue* wide = node_machine_record_wide(machine, k);
    return wide == NULL || (wide->overload_head == 0 && wide->fault == CHECK_FAULT_NONE && wide->bit_width == 0);
}

// T's row for record `r`, `op` of its operands' records `a` and `b` (UINT32_MAX for a unary operator's), into its
// columns: whether a row typed it.
static bool node_machine_row(NodeMachine* machine, uint32_t r, CheckOp op, uint32_t a, uint32_t b)
{
    const uint8_t o = node_machine_row_operators[op];
    const bool binary = b != UINT32_MAX;
    const uint8_t standing_a = machine->columns->standings[a];
    const uint8_t standing_b = binary && b < BUSTER_NODE_MACHINE_MOST ? machine->columns->standings[b] : BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE);
    const uint32_t flags_a = machine->columns->value_flags[a];
    const uint32_t flags_b = binary && b < BUSTER_NODE_MACHINE_MOST ? machine->columns->value_flags[b] : 0;
    // An lvalue's const qualifier goes with its conversion; any other qualifier leaves the node to the action.
    const TypeId type_a = machine->columns->value_types[a] & ~((standing_a & 0x03u) == CHECK_LVALUE ? (TypeId)BUSTER_TYPE_CONST : 0);
    const TypeId type_b = binary && b < BUSTER_NODE_MACHINE_MOST ? machine->columns->value_types[b] & ~((standing_b & 0x03u) == CHECK_LVALUE ? (TypeId)BUSTER_TYPE_CONST : 0) : 0;
    const uint32_t code_a = type_index(type_a);
    const uint32_t code_b = type_index(type_b);
    const bool builtin = o != NODE_MACHINE_ROW_NONE && (!binary || b < BUSTER_NODE_MACHINE_MOST) && (type_a & BUSTER_TYPE_QUALIFIERS) == 0 &&
                         code_a < BUSTER_NODE_MACHINE_KINDS && (type_b & BUSTER_TYPE_QUALIFIERS) == 0 && code_b < BUSTER_NODE_MACHINE_KINDS;
    const NodeMachineRow* row = builtin ? &node_machine_rows[o][code_a][code_b] : NULL;
    const uint32_t allowed = builtin ? node_machine_row_allowed[o] : 0;
    const bool takes = row != NULL && row->result != 0 && node_machine_row_takes(row, allowed, standing_a, flags_a, standing_b, 0) &&
                       (!binary || node_machine_row_takes(row, allowed, standing_b, flags_b, standing_a, 1));
    // A wide operand's rare fields, read for one alone.
    const bool typed = takes && (((standing_a | standing_b) & BUSTER_NODE_MACHINE_WIDE) == 0 ||
                                 (node_machine_row_plain(machine, a) && (!binary || node_machine_row_plain(machine, b))));
    if (typed)
    {
        const uint32_t flags = check_value_source_flags(op, row->flags | ((flags_a | flags_b) & node_machine_row_passed[o]));
        node_machine_record(machine, r, BUSTER_TYPE_BUILTIN(row->result), CHECK_RVALUE, flags, CHECK_CONSTANT_NONE, 0, 0);
        machine->columns->converted[2 * r] = BUSTER_TYPE_BUILTIN(row->converted[0]);
        machine->columns->converted[2 * r + 1] = binary ? BUSTER_TYPE_BUILTIN(row->converted[1]) : 0;
        machine->columns->conversion_flags[2 * r] = row->conversion_flags[0] | ((standing_a & 0x03u) == CHECK_LVALUE ? BUSTER_CHECK_CONVERT_LVALUE : 0);
        machine->columns->conversion_flags[2 * r + 1] = binary ? row->conversion_flags[1] | ((standing_b & 0x03u) == CHECK_LVALUE ? BUSTER_CHECK_CONVERT_LVALUE : 0) : 0;
        machine->columns->member_offsets[r] = 0;
    }
    return typed;
}

// Record `r`'s operands' conversions and its member offset.
static inline void node_machine_conversions(NodeMachine* machine, uint32_t r, const CheckConversion* first, const CheckConversion* second, uint64_t member_offset)
{
    machine->columns->converted[2 * r] = first->type;
    machine->columns->converted[2 * r + 1] = second->type;
    machine->columns->conversion_flags[2 * r] = first->flags;
    machine->columns->conversion_flags[2 * r + 1] = second->flags;
    machine->columns->member_offsets[r] = member_offset;
}

// T's discard: a cast to void of an rvalue that check_graph_discard_taken takes (no overload set, no array, function,
// vector or complex value; the machine refuses constant and floating-point contexts before), by check_graph_discard's
// steps, which the graph run takes for it instead of the action: whether it typed record `r`, cast `node` of record `a`.
// Of a value the columns hold whole, and no _BitInt constant, check_action_convert_plain's conversion to void and
// check_action_cast_provenance's for a discarded void cast change its type, category, flags and constant kind alone, on
// the columns; another's they make whole.
static bool node_machine_discard(NodeMachine* machine, uint32_t r, uint32_t node, uint32_t a)
{
    const CheckGraph* graph = machine->graph;
    const uint8_t standing = machine->columns->standings[a];
    const CheckValue* wide = node_machine_record_wide(machine, a);
    const CheckConstantKind constant_kind = (CheckConstantKind)((standing >> 2) & 0x07u);
    const uint8_t kind = (uint8_t)(machine->columns->words[a] >> 16);
    const bool taken = (wide == NULL || wide->overload_head == 0) && graph->types != NULL && graph->types[node] == BUSTER_TYPE_VOID &&
                       graph->overload_candidate_count == 0 && (standing & 0x03u) == CHECK_RVALUE && !type_kind_is_array(kind) && kind != BUSTER_TYPE_KIND_FUNCTION &&
                       kind != BUSTER_TYPE_KIND_VECTOR && kind != BUSTER_TYPE_KIND_EXT_VECTOR && kind != BUSTER_TYPE_KIND_COMPLEX;
    const CheckConversion none = { 0 };
    if (taken && wide == NULL && (kind != BUSTER_TYPE_KIND_BIT_INT || constant_kind == CHECK_CONSTANT_NONE))
    {
        // A pointer converted to anything but its own pointer type restricts its reads (check_action_convert_plain).
        const uint32_t restricted = kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
        const uint32_t dropped = BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_ICE |
                                 BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_MODIFICATION |
                                 BUSTER_CHECK_LITERAL_ZERO;
        const uint32_t flags = (machine->columns->value_flags[a] | restricted) & ~dropped;
        node_machine_record_from(machine, r, a, BUSTER_TYPE_VOID, CHECK_RVALUE, flags, CHECK_CONSTANT_NONE, false, 0);
        const CheckConversion first = { .type = BUSTER_TYPE_VOID, .flags = BUSTER_CHECK_CONVERT_CAST };
        node_machine_conversions(machine, r, &first, &none, 0);
    }
    else if (taken)
    {
        CheckValue value;
        node_machine_record_value(machine, a, &value);
        CheckConversion conversion = { .type = value.type };
        check_action_conversion(machine->types, &value, BUSTER_TYPE_VOID, BUSTER_CHECK_CONVERT_CAST, &conversion, node);
        check_action_cast_provenance(&value, false, false, false, true, true);
        value.flags = check_value_source_flags(CHECK_OP_CAST, value.flags);
        node_machine_put_value(machine, r, &value);
        node_machine_conversions(machine, r, &conversion, &none, 0);
    }
    return taken;
}

// The kinds whose lvalue a direct rule converts: a scalar's that names no tag (whose completeness at a position the action
// would ask) and is neither atomic nor a vector.
static bool node_machine_plain_kind(uint8_t kind)
{
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_POINTER;
}

// The pointer type to type `type`, type_pointer's: the unit's column's (NodeMachine.pointers), one load once the type's
// first array, function or address has filled it. A type of qualifiers but none or const alone, or one past the column,
// asks type_pointer each time.
static TypeId node_machine_pointer(NodeMachine* machine, TypeId type)
{
    const uint32_t index = type_index(type);
    const uint32_t qualifiers = type_qualifiers(type);
    const bool column = index < machine->type_count && (qualifiers == 0 || qualifiers == BUSTER_TYPE_CONST);
    const uint32_t slot = 2 * index + qualifiers;
    TypeId pointer = column ? machine->pointers[slot] : 0;
    if (pointer == 0)
    {
        pointer = type_pointer(machine->types, type);
        if (column)
        {
            machine->pointers[slot] = pointer;
        }
    }
    return pointer;
}

// An operand's value as its operator reads it (node_machine_operand_value): record `record`'s value with its type,
// category, flags and constant kind replaced, the fields a conversion changes; on the stack, in registers once the rule
// that reads it is inlined. `integer` is the record's integer's low 64 bits.
typedef struct NodeMachineOperand NodeMachineOperand;
struct NodeMachineOperand
{
    TypeId type;
    uint32_t flags;
    CheckCategory category;
    CheckConstantKind constant_kind;
    uint32_t record;
    uint64_t integer;
};

// Operand `operand`'s value whole into `value`: its record's with the operand's type, category, flags and constant kind.
static void node_machine_operand_whole(const NodeMachine* machine, const NodeMachineOperand* operand, CheckValue* value)
{
    node_machine_record_value(machine, operand->record, value);
    value->type = operand->type;
    value->category = operand->category;
    value->flags = operand->flags;
    value->constant_kind = operand->constant_kind;
}

// Record `a`'s value as an operator's operand reads it (check_action_operand_value's lvalue, array and function
// conversions), into `value` and `conversion`: whether the common cases took it (an rvalue of no array or function type;
// an lvalue of a non-volatile object of node_machine_plain_kind; an array or a function decayed to a pointer that exists);
// the action does the rest. A wide record's fault, which check_action_inherit carries, is the rule's to check.
static inline __attribute__((always_inline)) bool node_machine_operand_value(NodeMachine* machine, uint32_t a, NodeMachineOperand* value, CheckConversion* conversion)
{
    TypeTable* types = machine->types;
    const uint8_t standing = machine->columns->standings[a];
    const TypeId type = machine->columns->value_types[a];
    const uint32_t word = machine->columns->words[a];
    const bool wrapped = (word & BUSTER_NODE_MACHINE_WORD_PLAIN) == 0;
    const TypeId plain = wrapped ? type_strip_aligned(types, type) : type;
    const uint8_t kind = wrapped ? type_kind(types, plain) : (uint8_t)(word >> 16);
    CheckCategory category = (CheckCategory)(standing & 0x03u);
    CheckConstantKind constant_kind = (CheckConstantKind)((standing >> 2) & 0x07u);
    uint32_t flags = machine->columns->value_flags[a];
    TypeId result = type;
    bool taken = true;
    *conversion = (CheckConversion){ .type = type };
    if (type_kind_is_array(kind))
    {
        const TypeId pointer = (flags & BUSTER_CHECK_REGISTER_NAME) == 0 ? node_machine_pointer(machine, type_element(types, plain)) : 0;
        taken = pointer != 0;
        result = pointer;
        category = CHECK_RVALUE;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME);
        constant_kind = (flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && constant_kind == CHECK_CONSTANT_ADDRESS ? CHECK_CONSTANT_NONE : constant_kind;
        *conversion = (CheckConversion){ .type = pointer, .flags = BUSTER_CHECK_CONVERT_ARRAY };
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        const TypeId pointer = node_machine_pointer(machine, plain);
        taken = pointer != 0;
        result = pointer;
        category = CHECK_RVALUE;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL);
        *conversion = (CheckConversion){ .type = pointer, .flags = BUSTER_CHECK_CONVERT_FUNCTION };
    }
    else if (category == CHECK_LVALUE)
    {
        taken = node_machine_plain_kind(kind) && (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) == 0 && (flags & BUSTER_CHECK_VOLATILE_PART) == 0;
        constant_kind = constant_kind == CHECK_CONSTANT_ADDRESS || (flags & BUSTER_CHECK_ICE) == 0 ? CHECK_CONSTANT_NONE : constant_kind;
        result = type_conversion_operand(types, type);
        category = CHECK_RVALUE;
        flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART);
        *conversion = (CheckConversion){ .type = result, .flags = BUSTER_CHECK_CONVERT_LVALUE };
    }
    *value = (NodeMachineOperand){
        .type = result,
        .flags = flags,
        .category = category,
        .constant_kind = constant_kind,
        .record = a,
        .integer = node_machine_record_integer(machine, a),
    };
    return taken;
}

// Builtin integer type code `code`'s signedness in this unit: node_machine_integer_signed's, but plain char's the unit's
// (TypeTable.char_unsigned, -funsigned-char), which the bake's table of the builtin types alone doesn't know.
static inline bool node_machine_integer_sign(const NodeMachine* machine, uint32_t code)
{
    return code == BUSTER_TYPE_CODE_CHAR ? !machine->types->char_unsigned : node_machine_integer_signed[code];
}

// An integer constant's bits, `bits`, of a type of `from` bits (signed where `is_signed`), converted to a type of `to` bits,
// as check_action_convert_plain converts them: both of at most 64 bits.
static inline uint64_t node_machine_convert_bits(uint64_t bits, uint32_t from, bool is_signed, uint32_t to)
{
    const uint64_t from_mask = from == 64 ? UINT64_MAX : (UINT64_C(1) << from) - 1;
    const uint64_t to_mask = to == 64 ? UINT64_MAX : (UINT64_C(1) << to) - 1;
    uint64_t value = bits & from_mask;
    value |= is_signed && ((value >> (from - 1)) & 1) != 0 && to > from ? ~from_mask : 0;
    return value & to_mask;
}

// The width of the type of operand `operand` where it is a builtin integer type of at most 64 bits but bool, unqualified
// (node_machine_integer_widths'), and else 0.
static inline uint32_t node_machine_operand_width(const NodeMachineOperand* operand)
{
    const uint32_t code = type_index(operand->type);
    return (operand->type & BUSTER_TYPE_QUALIFIERS) == 0 && code < BUSTER_NODE_MACHINE_KINDS ? node_machine_integer_widths[code] : 0;
}

// Whether operand `operand` is a null pointer constant (check_action_null), where the columns can tell: one with the null
// pointer's flag, or an integer constant zero of node_machine_operand_width's types in an integer constant expression's
// form (a GNU one's too, where the unit takes GNU constants). Another is no null pointer here, which leaves its node to
// the action where it would be one: a nullptr_t's, a zero of an enumeration or of a wider type.
static bool node_machine_null(const NodeMachine* machine, const NodeMachineOperand* operand)
{
    const uint32_t forms = BUSTER_CHECK_ICE | ((machine->graph->extensions & BUSTER_CHECK_EXTENSION_GNU_CONSTANT) != 0 ? BUSTER_CHECK_GNU_ICE_FORM : 0);
    const uint32_t width = node_machine_operand_width(operand);
    const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    const bool zero = operand->constant_kind == CHECK_CONSTANT_INTEGER && (operand->flags & forms) != 0 && width != 0 && (operand->integer & mask) == 0;
    return (operand->flags & BUSTER_CHECK_NULL_POINTER) != 0 || zero;
}

// node_machine_argument's test of a pointer conversion, which the store rule takes too.
static bool node_machine_pointer_assignable(const NodeMachine* machine, TypeId to, TypeId from);

// T's member access, `.` or `->` at record `r` (node `node`) of record `a`, by check_action_member's steps where the record is
// a complete struct or union at the node's position without an address space and the member is no bit-field, and its base
// is no constant or (for `.`) an address constant, a static object's: whether it typed the node.
static bool node_machine_member(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a)
{
    TypeTable* types = machine->types;
    const CheckGraph* graph = machine->graph;
    const bool arrow = op == CHECK_OP_ARROW;
    NodeMachineOperand base = {
        .type = machine->columns->value_types[a],
        .flags = machine->columns->value_flags[a],
        .category = node_machine_record_category(machine, a),
        .constant_kind = node_machine_record_constant_kind(machine, a),
    };
    CheckConversion conversion = { .type = base.type };
    // The base's fault, which check_action_inherit would carry, leaves the node to the action.
    bool taken = (arrow ? node_machine_operand_value(machine, a, &base, &conversion) : true) && node_machine_record_fault(machine, a) == CHECK_FAULT_NONE;
    TypeId record = type_strip_aligned(types, base.type);
    uint32_t space = arrow ? 0 : type_address_space(type_extended_qualifiers(types, base.type));
    if (taken && arrow)
    {
        taken = type_kind(types, record) == BUSTER_TYPE_KIND_POINTER;
        space = taken ? type_address_space(type_extended_qualifiers(types, type_element(types, record))) : 0;
        record = taken ? type_strip_aligned(types, type_element(types, record)) : record;
    }
    const uint8_t kind = type_kind(types, record);
    // A `.` of an address constant, a static object's member (its base held whole by the columns, so with no bit offset).
    const bool address = !arrow && base.constant_kind == CHECK_CONSTANT_ADDRESS && (machine->columns->standings[a] & BUSTER_NODE_MACHINE_WIDE) == 0;
    taken = taken && space == 0 && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && (base.constant_kind == CHECK_CONSTANT_NONE || address);
    taken = taken && node_machine_complete(machine, record, node);
    uint64_t offset = 0;
    uint32_t qualifiers = 0;
    const uint32_t member = taken ? type_lookup_member(types, record, graph->members[node], &offset, &qualifiers) : 0;
    taken = taken && member != 0 && member != BUSTER_TYPE_MEMBER_UNKNOWN && (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0;
    // check_action_member's address constant: the base's offset and the member's bytes (its bits, a whole number of bytes,
    // as no bit-field's are), where they don't overflow.
    int64_t addend = 0;
    taken = taken && (!address || (offset % 8 == 0 && offset / 8 <= INT64_MAX && !__builtin_add_overflow(node_machine_record_offset(machine, a), (int64_t)(offset / 8), &addend)));
    if (taken)
    {
        const TypeId type = type_qualify(types, types->members.type[member], qualifiers);
        uint32_t flags = (base.flags & BUSTER_CHECK_INHERITED) | (base.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS));
        flags |= type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
        flags |= arrow && (base.flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
        flags |= !arrow ? base.flags & BUSTER_CHECK_REGISTER : 0;
        const CheckCategory category = arrow || base.category == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
        node_machine_record(machine, r, type, category, check_value_source_flags(op, flags), address ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_NONE,
                            address ? machine->columns->objects[a] : 0, (uint64_t)addend);
        machine->columns->standings[r] |= addend != 0 ? BUSTER_NODE_MACHINE_OFFSET : 0;
        const CheckConversion none = { 0 };
        node_machine_conversions(machine, r, &conversion, &none, offset);
    }
    return taken;
}

// T's truth test, `!` or a condition at record `r` of record `a`, by check_action_logic's steps for an operand the common
// conversions take and the action takes as a scalar: whether it typed the node. An integer constant of a type of at most
// 64 bits is true by its low bits; another constant is made whole for check_action_truth.
static bool node_machine_truth(NodeMachine* machine, uint32_t r, CheckOp op, uint32_t a)
{
    NodeMachineOperand operand;
    CheckConversion conversion;
    // The operand's fault, which check_action_inherit would carry, leaves the node to the action.
    const bool taken = node_machine_operand_value(machine, a, &operand, &conversion) && check_action_is_scalar(machine->types, operand.type) &&
                       node_machine_record_fault(machine, a) == CHECK_FAULT_NONE;
    if (taken)
    {
        uint32_t width = 0;
        bool is_signed = false;
        bool truth = false;
        bool known = false;
        if (operand.constant_kind == CHECK_CONSTANT_INTEGER && check_action_integer_type(machine->types, operand.type, &width, &is_signed) && width <= 64)
        {
            const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
            truth = (operand.integer & mask) != 0;
            known = true;
        }
        else if (operand.constant_kind != CHECK_CONSTANT_NONE)
        {
            CheckValue value;
            node_machine_operand_whole(machine, &operand, &value);
            known = check_action_truth(machine->types, &value, &truth);
        }
        const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
        const uint32_t flags = (operand.flags & BUSTER_CHECK_INHERITED) |
                               ((operand.flags & BUSTER_CHECK_ICE) != 0 ? BUSTER_CHECK_ICE : (operand.flags & forms) != 0 ? BUSTER_CHECK_GNU_ICE_FORM : 0);
        const uint64_t integer = known ? (uint64_t)(op == CHECK_OP_NOT ? !truth : truth) : 0;
        node_machine_record(machine, r, BUSTER_TYPE_INT, CHECK_RVALUE, check_value_source_flags(op, flags), known ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE, 0,
                            integer);
        const CheckConversion none = { 0 };
        node_machine_conversions(machine, r, &conversion, &none, 0);
    }
    return taken;
}

// Whether operand `operand`'s truth is known, check_action_truth's answer, and into `truth` that truth: an integer
// constant's of a type of at most 64 bits by its low bits, another constant's made whole, no constant's unknown.
static bool node_machine_operand_truth(const NodeMachine* machine, const NodeMachineOperand* operand, bool* truth)
{
    uint32_t width = 0;
    bool is_signed = false;
    bool known = false;
    *truth = false;
    if (operand->constant_kind == CHECK_CONSTANT_INTEGER && check_action_integer_type(machine->types, operand->type, &width, &is_signed) && width <= 64)
    {
        const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        *truth = (operand->integer & mask) != 0;
        known = true;
    }
    else if (operand->constant_kind != CHECK_CONSTANT_NONE)
    {
        CheckValue value;
        node_machine_operand_whole(machine, operand, &value);
        known = check_action_truth(machine->types, &value, truth);
    }
    return known;
}

// T's logical operator, `&&` or `||` at record `r` of records `a` and `b`, by check_action_logic's steps for operands the
// common conversions take, both scalars without a fault (the machine refuses a node of a floating-point environment
// before, whose promotion the action would make): whether it typed the node. The action is its twin, as for the rules.
static bool node_machine_logic(NodeMachine* machine, uint32_t r, CheckOp op, uint32_t a, uint32_t b)
{
    TypeTable* types = machine->types;
    NodeMachineOperand left;
    NodeMachineOperand right;
    CheckConversion conversions[2];
    const bool taken = b < r && node_machine_operand_value(machine, a, &left, conversions) && node_machine_operand_value(machine, b, &right, conversions + 1) &&
                       check_action_is_scalar(types, left.type) && check_action_is_scalar(types, right.type) &&
                       node_machine_record_fault(machine, a) == CHECK_FAULT_NONE && node_machine_record_fault(machine, b) == CHECK_FAULT_NONE;
    if (taken)
    {
        const bool conjunction = op == CHECK_OP_LOGICAL_AND;
        bool left_truth = false;
        bool right_truth = false;
        const bool left_known = node_machine_operand_truth(machine, &left, &left_truth);
        const bool right_known = node_machine_operand_truth(machine, &right, &right_truth);
        // A condition that decides the value leaves the second operand unevaluated: its effects aren't the value's.
        const bool skips_right = left_known && (conjunction ? !left_truth : left_truth);
        const uint32_t inherited = (left.flags & BUSTER_CHECK_INHERITED) | (skips_right ? 0 : right.flags & BUSTER_CHECK_INHERITED);
        const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
        const bool right_ice = (right.flags & BUSTER_CHECK_ICE) != 0 || (skips_right && (right.flags & (BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM)) != 0);
        const bool ice = (left.flags & BUSTER_CHECK_ICE) != 0 && right_ice;
        const bool right_form = (right.flags & forms) != 0 || (skips_right && (right.flags & BUSTER_CHECK_ICE_UNEVALUATED) != 0);
        const uint32_t flags = inherited | (ice ? BUSTER_CHECK_ICE : (left.flags & forms) != 0 && right_form ? BUSTER_CHECK_GNU_ICE_FORM : 0);
        const bool constant = left_known && (skips_right || right_known);
        const bool value = conjunction ? left_truth && right_truth : left_truth || right_truth;
        node_machine_record(machine, r, BUSTER_TYPE_INT, CHECK_RVALUE, check_value_source_flags(op, flags), constant ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE, 0,
                            constant && value);
        node_machine_conversions(machine, r, conversions, conversions + 1, 0);
    }
    return taken;
}

// Whether the conversion of a value of type `source` to `target` is one a direct assignment takes as the action would: a
// builtin arithmetic type to another, or a pointer to the same pointer type.
static bool node_machine_assignable(const NodeMachine* machine, TypeId target, TypeId source)
{
    const TypeTable* types = machine->types;
    const bool arithmetic = type_index(target) < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(type_index(target)) && (target & BUSTER_TYPE_QUALIFIERS) == 0 &&
                            type_index(source) < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(type_index(source)) && (source & BUSTER_TYPE_QUALIFIERS) == 0;
    const bool pointer = target == source && type_kind(types, target) == BUSTER_TYPE_KIND_POINTER;
    return arithmetic || pointer;
}

// T's store, `=`, an initialization or a conversion at record `r` (node `node`), by check_action_assignment's steps where
// the destination (`=`'s operand 0) is a modifiable lvalue of node_machine_plain_kind (held whole by the columns, for its
// fault) and node_machine_assignable takes the conversion: whether it typed the node. check_action_convert_plain's
// conversion of the common sources changes their type and flags, and an integer constant's bits, alone, on the columns;
// it converts the rest whole.
static bool node_machine_store_rule(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a, uint32_t b)
{
    TypeTable* types = machine->types;
    const bool store = op == CHECK_OP_ASSIGN;
    const uint32_t source_record = store ? b : a;
    NodeMachineOperand source;
    CheckConversion conversion;
    bool taken = source_record < r && node_machine_operand_value(machine, source_record, &source, &conversion);
    TypeId target = 0;
    if (taken && store)
    {
        // The destination's fault, which check_action_inherit would carry, is a wide value's.
        const uint8_t kind = (uint8_t)(machine->columns->words[a] >> 16);
        const uint32_t destination_flags = machine->columns->value_flags[a];
        target = machine->columns->value_types[a];
        taken = (machine->columns->standings[a] & (0x03u | BUSTER_NODE_MACHINE_WIDE)) == CHECK_LVALUE &&
                (destination_flags & (BUSTER_CHECK_VECTOR_DUPLICATE | BUSTER_CHECK_BIT_FIELD)) == 0 && (type_qualifiers(target) & BUSTER_TYPE_CONST) == 0 &&
                node_machine_plain_kind(kind);
    }
    else if (taken)
    {
        target = machine->graph->types != NULL ? machine->graph->types[node] : 0;
        taken = target != 0 && type_is_valid(types, target);
    }
    if (taken)
    {
        const uint8_t target_kind = check_action_kind(types, target);
        taken = target_kind != BUSTER_TYPE_KIND_ATOMIC && check_action_kind(types, source.type) != BUSTER_TYPE_KIND_ATOMIC && target_kind != BUSTER_TYPE_KIND_VOID;
        target = taken ? type_conversion_operand(types, target) : target;
        // Besides node_machine_assignable's, a pointer takes a null pointer constant and a pointer node_machine_pointer_assignable
        // takes (check_action_assignable_mode's branches), converted whole below.
        taken = taken && (node_machine_assignable(machine, target, source.type) ||
                          (check_action_kind(types, target) == BUSTER_TYPE_KIND_POINTER &&
                           (node_machine_null(machine, &source) || node_machine_pointer_assignable(machine, target, source.type))));
    }
    const uint32_t source_code = type_index(source.type);
    const uint32_t target_code = type_index(target);
    const uint32_t source_width = taken && source_code < BUSTER_NODE_MACHINE_KINDS ? node_machine_integer_widths[source_code] : 0;
    const uint32_t target_width = taken && target_code < BUSTER_NODE_MACHINE_KINDS ? node_machine_integer_widths[target_code] : 0;
    const bool constant = source.constant_kind == CHECK_CONSTANT_INTEGER;
    const bool pointer = taken && check_action_kind(types, target) == BUSTER_TYPE_KIND_POINTER;
    // On the columns, a source the columns hold whole: no constant; an integer one of a builtin integer type of at most 64
    // bits converted to another but bool (node_machine_integer_widths: 0 for bool and the others); an address converted to
    // its own pointer type. Not a null pointer (check_action_null), which a pointer converted to the same pointer type is
    // by its flag alone, since no integer constant has a pointer's type, and which its conversion makes a null address
    // constant.
    const bool columns = (source.constant_kind == CHECK_CONSTANT_NONE || (constant && source_width != 0 && target_width != 0) ||
                          (source.constant_kind == CHECK_CONSTANT_ADDRESS && pointer)) &&
                         !(pointer && (source.flags & BUSTER_CHECK_NULL_POINTER) != 0) && !(pointer && target != source.type) &&
                         (machine->columns->standings[source.record] & BUSTER_NODE_MACHINE_WIDE) == 0;
    if (taken)
    {
        const uint32_t inherited = store ? machine->columns->value_flags[a] & BUSTER_CHECK_INHERITED : 0;
        const uint32_t stored = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED;
        if (columns)
        {
            const bool bool_target = type_unqualified(type_strip_aligned(types, target)) == BUSTER_TYPE_BOOL;
            const bool integer_target = target_code < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(target_code) && target_code != type_index(BUSTER_TYPE_FLOAT) &&
                                        target_code != type_index(BUSTER_TYPE_DOUBLE);
            uint32_t flags = source.flags & ~(BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER);
            flags &= integer_target ? ~0u : ~(uint32_t)(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
            flags = store ? (flags & ~stored) | BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION | inherited : flags;
            flags = check_value_source_flags(op, flags);
            conversion.flags |= BUSTER_CHECK_CONVERT_ASSIGNMENT;
            conversion.type = target;
            if (constant)
            {
                // An integer constant's bits, as check_action_convert_plain converts them.
                const uint64_t source_mask = source_width == 64 ? UINT64_MAX : (UINT64_C(1) << source_width) - 1;
                const uint64_t target_mask = target_width == 64 ? UINT64_MAX : (UINT64_C(1) << target_width) - 1;
                uint64_t bits = source.integer & source_mask;
                bits |= node_machine_integer_sign(machine, source_code) && (bits >> (source_width - 1)) != 0 && target_width > source_width ? ~source_mask : 0;
                node_machine_record_from(machine, r, source.record, target, CHECK_RVALUE, flags, CHECK_CONSTANT_INTEGER, true, bits & target_mask);
            }
            else
            {
                // A conversion to bool of no constant: its truth unknown, its integer 0.
                node_machine_record_from(machine, r, source.record, target, CHECK_RVALUE, flags, source.constant_kind, bool_target, 0);
            }
        }
        else
        {
            CheckValue value;
            node_machine_operand_whole(machine, &source, &value);
            const uint32_t extensions = check_graph_context(machine->graph, node, false).extensions;
            const uint32_t flags = BUSTER_CHECK_CONVERT_ASSIGNMENT | (pointer && check_action_null(types, &value, extensions) ? BUSTER_CHECK_CONVERT_NULL : 0);
            check_action_conversion(types, &value, target, flags, &conversion, node);
            value.category = CHECK_RVALUE;
            value.flags = store ? (value.flags & ~stored) | BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION | inherited : value.flags;
            value.flags = check_value_source_flags(op, value.flags);
            node_machine_put_value(machine, r, &value);
        }
        const CheckConversion first = { .type = store ? machine->columns->value_types[a] : 0 };
        node_machine_conversions(machine, r, store ? &first : &conversion, store ? &conversion : &first, 0);
    }
    return taken;
}

// T's address, `&x` at record `r` of record `a`, as the action takes it: whether it typed the node (an lvalue or a function
// designator without a vector element's, a bit-field's or a register's flag, whose pointer type exists).
static bool node_machine_address_rule(NodeMachine* machine, uint32_t r, uint32_t a)
{
    const uint8_t standing = machine->columns->standings[a];
    const CheckCategory category = (CheckCategory)(standing & 0x03u);
    const CheckConstantKind constant_kind = (CheckConstantKind)((standing >> 2) & 0x07u);
    uint32_t flags = machine->columns->value_flags[a];
    const TypeId pointer = (flags & (BUSTER_CHECK_VECTOR_ELEMENT | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER)) == 0 &&
                           (category == CHECK_LVALUE || category == CHECK_FUNCTION) ? node_machine_pointer(machine, machine->columns->value_types[a]) : 0;
    if (pointer != 0)
    {
        // check_action_address_provenance: an automatic object's address is no constant.
        const bool dropped = (flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && constant_kind == CHECK_CONSTANT_ADDRESS && machine->columns->objects[a] != 0;
        flags |= (flags & BUSTER_CHECK_INACTIVE_UNION_READ) != 0 ? BUSTER_CHECK_INACTIVE_UNION_ADDRESS : 0;
        flags &= ~(BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED |
                   BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_VOLATILE_PART);
        node_machine_record_from(machine, r, a, pointer, CHECK_RVALUE, check_value_source_flags(CHECK_OP_ADDRESS, flags), dropped ? CHECK_CONSTANT_NONE : constant_kind,
                                 false, 0);
        const CheckConversion first = { .type = machine->columns->value_types[a] };
        const CheckConversion none = { 0 };
        node_machine_conversions(machine, r, &first, &none, 0);
    }
    return pointer != 0;
}

// T's dereference, `*p` at record `r` of record `a`, as the action takes it: whether it typed the node (a pointer the
// common conversions take).
static bool node_machine_dereference(NodeMachine* machine, uint32_t r, uint32_t a)
{
    TypeTable* types = machine->types;
    NodeMachineOperand pointer;
    CheckConversion conversion;
    const bool taken = node_machine_operand_value(machine, a, &pointer, &conversion) && check_action_kind(types, pointer.type) == BUSTER_TYPE_KIND_POINTER;
    if (taken)
    {
        const TypeId type = type_element(types, type_strip_aligned(types, pointer.type));
        const CheckCategory category = check_action_kind(types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
        uint32_t flags = pointer.flags;
        flags |= (flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
        node_machine_record_from(machine, r, pointer.record, type, category, check_value_source_flags(CHECK_OP_DEREFERENCE, flags), pointer.constant_kind, false, 0);
        const CheckConversion none = { 0 };
        node_machine_conversions(machine, r, &conversion, &none, 0);
    }
    return taken;
}

// T's pointer arithmetic, a subscript or a pointer plus or minus an integer at record `r` (node `node`) of records `a` and
// `b`, by check_action_pointer_arithmetic's steps where the common conversions take both operands, one is a pointer to a
// complete object type and the other an integer (no bit-field's), the pointer is no constant or an address constant, and
// neither has a fault: whether it typed the node.
static bool node_machine_pointer_rule(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a, uint32_t b)
{
    TypeTable* types = machine->types;
    NodeMachineOperand left;
    NodeMachineOperand right;
    CheckConversion conversions[2];
    bool taken = b < r && node_machine_operand_value(machine, a, &left, conversions) && node_machine_operand_value(machine, b, &right, conversions + 1);
    const bool lp = taken && check_action_kind(types, left.type) == BUSTER_TYPE_KIND_POINTER;
    const bool rp = taken && check_action_kind(types, right.type) == BUSTER_TYPE_KIND_POINTER;
    const bool reverse = !lp && rp && op != CHECK_OP_SUBTRACT;
    const NodeMachineOperand* pointer = reverse ? &right : &left;
    const NodeMachineOperand* index = reverse ? &left : &right;
    const uint32_t slot = reverse ? 0 : 1;
    // A pointer that is an address constant (a static array's decay, say) held whole by the columns gives one with an index
    // that is an integer constant of node_machine_operand_width's types (check_action_address_index's), and none with an
    // index that has no constant.
    const bool address = pointer->constant_kind == CHECK_CONSTANT_ADDRESS && (machine->columns->standings[pointer->record] & BUSTER_NODE_MACHINE_WIDE) == 0 &&
                         (index->constant_kind == CHECK_CONSTANT_NONE ||
                          (index->constant_kind == CHECK_CONSTANT_INTEGER && node_machine_operand_width(index) != 0 &&
                           (machine->columns->standings[index->record] & BUSTER_NODE_MACHINE_WIDE) == 0));
    // Either operand's fault, which check_action_inherit would carry, and an index's bit-field width leave the node to the
    // action.
    taken = taken && lp != rp && (lp || reverse) && check_action_is_integer(types, index->type) && (index->flags & BUSTER_CHECK_BIT_FIELD) == 0 &&
            (pointer->constant_kind == CHECK_CONSTANT_NONE || address) && node_machine_record_fault(machine, a) == CHECK_FAULT_NONE &&
            node_machine_record_fault(machine, b) == CHECK_FAULT_NONE;
    if (taken)
    {
        const TypeId element = type_element(types, type_strip_aligned(types, pointer->type));
        const uint8_t kind = check_action_kind(types, element);
        taken = kind != BUSTER_TYPE_KIND_VOID && kind != BUSTER_TYPE_KIND_FUNCTION && node_machine_complete(machine, element, node);
    }
    if (taken)
    {
        const uint32_t raw = reverse ? b : a;
        // check_action_promote: the index's conversion, whose value nothing reads.
        const TypeId promoted = type_promote(types, index->type);
        conversions[slot].flags |= promoted != index->type ? BUSTER_CHECK_CONVERT_PROMOTION : 0;
        conversions[slot].type = promoted != index->type ? promoted : conversions[slot].type;
        const bool subscript = op == CHECK_OP_SUBSCRIPT;
        uint32_t flags = (left.flags | right.flags) & BUSTER_CHECK_INHERITED;
        flags |= pointer->flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS);
        flags |= subscript && (pointer->flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
        flags |= subscript && (machine->columns->value_flags[raw] & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) == BUSTER_CHECK_REGISTER &&
                 type_kind_is_array(check_action_kind(types, machine->columns->value_types[raw])) ? BUSTER_CHECK_REGISTER : 0;
        const TypeId type = subscript ? type_element(types, pointer->type) : pointer->type;
        // An address constant's element at a constant index: its object, its offset moved by the index's elements, the
        // low 64 bits (a wrap is no reason to drop the constant, as the action has it), where its element has no runtime
        // size.
        const bool constant = address && index->constant_kind == CHECK_CONSTANT_INTEGER && !check_action_runtime_size(types, type_element(types, pointer->type));
        uint64_t addend = 0;
        if (constant)
        {
            const uint32_t width = node_machine_operand_width(index);
            const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
            const uint64_t stored = index->integer & mask;
            const bool negative = node_machine_integer_sign(machine, type_index(index->type)) && width < 64 && ((stored >> (width - 1)) & 1) != 0;
            const uint64_t delta = (negative ? stored | ~mask : stored) * check_action_pointer_stride(types, pointer->type);
            const uint64_t base = (uint64_t)node_machine_record_offset(machine, pointer->record);
            addend = op == CHECK_OP_SUBTRACT ? base - delta : base + delta;
        }
        node_machine_record(machine, r, type, subscript ? CHECK_LVALUE : CHECK_RVALUE, check_value_source_flags(op, flags), constant ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_NONE,
                            constant ? machine->columns->objects[pointer->record] : 0, addend);
        machine->columns->standings[r] |= addend != 0 ? BUSTER_NODE_MACHINE_OFFSET : 0;
        node_machine_conversions(machine, r, conversions, conversions + 1, 0);
    }
    return taken;
}

// Call record `r`'s (node `node`'s) arguments into the expression's list (NodeMachine.arguments), each a record before it,
// and its subtree's least record past theirs too: whether they all are.
static bool node_machine_arguments(NodeMachine* machine, uint32_t r, uint32_t node)
{
    const CheckGraph* graph = machine->graph;
    const uint32_t count = graph->argument_counts[node];
    const uint32_t offset = graph->argument_offsets[node];
    const uint32_t first = machine->argument_total;
    uint32_t lowest = machine->columns->lowest[r];
    bool inside = count <= BUSTER_NODE_MACHINE_MOST - first;
    for (uint32_t i = 0; inside && i < count; i += 1)
    {
        const uint32_t argument = graph->argument_nodes[offset + i];
        const uint32_t record = argument - machine->first;
        inside = argument != BUSTER_CHECK_NONE && argument >= machine->first && record < r;
        machine->arguments[first + i] = record;
        lowest = inside && machine->columns->lowest[record] < lowest ? machine->columns->lowest[record] : lowest;
    }
    machine->columns->lowest[r] = lowest;
    machine->columns->argument_first[r] = first;
    machine->columns->argument_counts[r] = inside ? count : 0;
    machine->argument_total = inside ? first + count : first;
    return inside;
}

// Whether call record `r` (node `node`, callee record `a`) has a callee with semantics of its own, which the machine
// leaves to the pipeline (docs/node-machine.md, "T: bind and type"): a library builtin's call that the catalogue folds or
// computes (the reader's BUSTER_CHECK_LIBRARY_CALL), any call in a unit with overload sets, a callee without a prototype
// or of another convention than System V's (what check_lower_convention_call refuses, and the conventions that keep more
// registers), more arguments than Tier 0's calls take, or a callee that returns twice (check_lower_calls_twice's and
// check_lower_indirect_twice's, which the lowering marks its function for). The census refuses before it the callees
// whose attributes or names the warnings read (node_census_callee).
static bool node_machine_callee_refused(const NodeMachine* machine, uint32_t r, uint32_t node, uint32_t a)
{
    const CheckProgram* program = machine->program;
    const CheckGraph* graph = machine->graph;
    const TypeTable* types = machine->types;
    const uint32_t member = graph->members != NULL ? graph->members[node] : 0;
    const TypeId function = node_machine_callee_function(machine, a);
    bool refused = (member & BUSTER_CHECK_LIBRARY_CALL) != 0 || graph->overload_candidate_count != 0 || function == 0 ||
                   machine->columns->argument_counts[r] > BUSTER_TIER0_MAX_CALL_ARGUMENTS;
    refused = refused || (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0 ||
              type_function_abi_kind(types, function) != TYPE_FUNCTION_ABI_SYSV || type_function_has_parameter_abis(types, function);
    const uint32_t object = refused ? 0 : node_machine_direct_callee(machine, a);
    if (!refused && object != 0)
    {
        const uint32_t declaration = program->object_declarations != NULL ? program->object_declarations[object] : BUSTER_CHECK_NONE;
        const IdentId name = program->parser != NULL && declaration < program->input->token_count ? program->parser->tokens.names[declaration] : 0;
        refused = program->object_returns_twice != NULL && program->object_returns_twice[object] != 0;
        for (uint32_t k = 0; k < BUSTER_CHECK_LOWER_TWICE_NAMES; k += 1)
        {
            refused |= name != 0 && machine->twice_names[k] == name;
        }
    }
    else if (!refused)
    {
        // A pointer's call whose first parameter points to a jmp_buf's element (check_lower_indirect_twice).
        uint32_t parameters = 0;
        const TypeId* parameter_types = type_parameters(types, function, &parameters);
        const TypeId first = parameters != 0 ? type_strip_aligned(types, parameter_types[0]) : 0;
        const TypeId element = first != 0 && type_kind(types, first) == BUSTER_TYPE_KIND_POINTER ? type_unqualified(type_strip_aligned(types, type_element(types, first))) : 0;
        refused = element != 0 && type_kind(types, element) == BUSTER_TYPE_KIND_STRUCT && machine->twice_tag != 0 &&
                  types->tags.name[types->types.index[type_index(element)]] == machine->twice_tag;
    }
    return refused;
}

// Whether a pointer of type `from` converts to pointer type `to` as check_action_pointer_assignable converts it without
// a diagnostic or a warning, in its common cases: pointees of the same extended qualifiers, `to`'s with at least
// `from`'s qualifiers, and the same type but for them, or either void and neither a function.
static bool node_machine_pointer_assignable(const NodeMachine* machine, TypeId to, TypeId from)
{
    const TypeTable* types = machine->types;
    const TypeId target = type_unqualified(type_strip_aligned(types, to));
    const TypeId input = type_unqualified(type_strip_aligned(types, from));
    bool taken = type_kind(types, target) == BUSTER_TYPE_KIND_POINTER && type_kind(types, input) == BUSTER_TYPE_KIND_POINTER;
    if (taken)
    {
        const TypeId to_element = type_element(types, target);
        const TypeId from_element = type_element(types, input);
        const uint8_t to_kind = check_action_kind(types, to_element);
        const uint8_t from_kind = check_action_kind(types, from_element);
        const bool function = to_kind == BUSTER_TYPE_KIND_FUNCTION || from_kind == BUSTER_TYPE_KIND_FUNCTION;
        const bool either_void = to_kind == BUSTER_TYPE_KIND_VOID || from_kind == BUSTER_TYPE_KIND_VOID;
        taken = type_object_qualifiers(types, to_element) == type_object_qualifiers(types, from_element) &&
                (type_qualifiers(from_element) & ~type_qualifiers(to_element)) == 0 &&
                ((either_void && !function) || type_unqualified(to_element) == type_unqualified(from_element));
    }
    return taken;
}

// Whether check_action_assignable_mode takes a value of type `source` to type `destination` by its arithmetic branches,
// without a diagnostic or a warning: an arithmetic type (an enumeration's too) to another, or a scalar to bool, neither a
// vector.
static bool node_machine_arithmetic_assignable(const NodeMachine* machine, TypeId destination, TypeId source)
{
    const TypeTable* types = machine->types;
    const TypeId target = type_unqualified(type_strip_aligned(types, destination));
    const TypeId input = type_unqualified(type_strip_aligned(types, source));
    return !check_vector_type(types, target) && !check_vector_type(types, input) &&
           ((check_action_is_arithmetic(types, target) && check_action_is_arithmetic(types, input)) ||
            (target == BUSTER_TYPE_BOOL && check_action_is_scalar(types, input)));
}

// Record `a`'s value as an operand of node `node` reads it where it is an lvalue of an enumeration, which
// node_machine_operand_value leaves to the action: check_action_operand_value's lvalue conversion, where the enumeration
// is complete at the node and the object isn't volatile, on the value made whole. Whether it took it.
static bool node_machine_enumeration_value(NodeMachine* machine, uint32_t a, uint32_t node, CheckValue* value, CheckConversion* conversion)
{
    const TypeId type = machine->columns->value_types[a];
    const uint32_t flags = machine->columns->value_flags[a];
    const bool taken = node_machine_record_category(machine, a) == CHECK_LVALUE && (uint8_t)(machine->columns->words[a] >> 16) == BUSTER_TYPE_KIND_ENUM &&
                       (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) == 0 && (flags & BUSTER_CHECK_VOLATILE_PART) == 0 && node_machine_complete(machine, type, node);
    if (taken)
    {
        node_machine_record_value(machine, a, value);
        value->constant_kind = value->constant_kind == CHECK_CONSTANT_ADDRESS || (value->flags & BUSTER_CHECK_ICE) == 0 ? CHECK_CONSTANT_NONE : value->constant_kind;
        value->type = type_conversion_operand(machine->types, value->type);
        value->category = CHECK_RVALUE;
        value->flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART);
        *conversion = (CheckConversion){ .type = value->type, .flags = BUSTER_CHECK_CONVERT_LVALUE };
    }
    return taken;
}

// A call's argument, record `a` of call node `node`, as check_action_call converts it in the common cases, into `value`
// (its value as an operand reads it, made whole and converted by the action's own functions) and `conversion`: for a parameter of type `parameter`, as an assignment
// converts it (node_machine_arithmetic_assignable's, or a null pointer constant's or a pointer node_machine_pointer_assignable
// takes to a pointer); a variadic one (`parameter` 0) by the default argument promotions, of an arithmetic type or to the
// same pointer type. Whether they took it: the action takes the rest, as an atomic value's.
static bool node_machine_argument(NodeMachine* machine, uint32_t a, uint32_t node, TypeId parameter, CheckValue* value, CheckConversion* conversion)
{
    TypeTable* types = machine->types;
    NodeMachineOperand operand;
    bool taken = node_machine_operand_value(machine, a, &operand, conversion);
    if (taken)
    {
        node_machine_operand_whole(machine, &operand, value);
    }
    taken = (taken || node_machine_enumeration_value(machine, a, node, value, conversion)) && check_action_kind(types, value->type) != BUSTER_TYPE_KIND_ATOMIC;
    if (taken && parameter != 0)
    {
        taken = check_action_kind(types, parameter) != BUSTER_TYPE_KIND_ATOMIC;
        const TypeId destination = taken ? type_conversion_operand(types, parameter) : 0;
        if (taken && check_action_kind(types, destination) == BUSTER_TYPE_KIND_POINTER)
        {
            const uint32_t extensions = check_graph_context(machine->graph, node, false).extensions;
            taken = check_action_null(types, value, extensions) || node_machine_pointer_assignable(machine, destination, value->type);
        }
        else if (taken)
        {
            taken = node_machine_arithmetic_assignable(machine, destination, value->type);
        }
        if (taken)
        {
            check_action_conversion(types, value, destination, BUSTER_CHECK_CONVERT_ASSIGNMENT, conversion, node);
        }
    }
    else if (taken)
    {
        const TypeId original = type_unqualified(type_strip_aligned(types, value->type));
        check_action_promote(types, value, conversion, node);
        TypeId destination = type_default_argument_promote(types, value->type);
        destination = original == BUSTER_TYPE_FLOAT || original == BUSTER_TYPE_HALF ? BUSTER_TYPE_DOUBLE : destination;
        destination = type_conversion_operand(types, destination);
        const bool pointer = destination == value->type && check_action_kind(types, destination) == BUSTER_TYPE_KIND_POINTER;
        taken = pointer || node_machine_arithmetic_assignable(machine, destination, value->type);
        if (taken)
        {
            check_action_conversion(types, value, destination, destination != value->type ? BUSTER_CHECK_CONVERT_PROMOTION : 0, conversion, node);
        }
    }
    return taken;
}

// The flags of an argument's record that node_machine_argument reads besides its type (a null pointer's, a bit-field's
// promotion, a register's array, a volatile part's load): an argument with one takes the rule each time.
#define BUSTER_NODE_MACHINE_MEMO_FLAGS (BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART)

// Whether a conversion of a value from or to a type of kind `kind` changes nothing that check_action_inherit reads of it,
// where the value has no constant but an address (check_action_convert_plain's flags are none of the inherited ones, and it
// sets no fault there): an integer's, a floating type's, a pointer's and an enumeration's. A complex, a vector and a
// _BitInt convert otherwise.
static bool node_machine_memo_kind(uint8_t kind)
{
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_ENUM;
}

// Whether an integer constant of an integer type takes, for a parameter of type `parameter` (0: a variadic one), the
// answer any integer constant of its type takes, its conversion folding it without changing what check_action_inherit
// reads: an integer, an enumeration, a float, a double or a long double parameter, or the promotions. A pointer's isn't: a
// null pointer constant converts to it and another integer doesn't; nor a half-precision type's, whose fold can fault.
static bool node_machine_memo_integers(const NodeMachine* machine, TypeId parameter)
{
    const TypeId destination = parameter != 0 ? type_unqualified(type_strip_aligned(machine->types, parameter)) : BUSTER_TYPE_INT;
    const uint8_t kind = type_kind(machine->types, destination);
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM || destination == BUSTER_TYPE_FLOAT || destination == BUSTER_TYPE_DOUBLE ||
           destination == BUSTER_TYPE_LONG_DOUBLE;
}

// Call node `node`'s argument, record `a`, for a parameter of type `parameter` (0: a variadic one), by the unit's argument
// memo (NodeMachine.argument_memo): node_machine_argument's answer, its exact twin, into `conversion`, and the flags and
// fault of its converted value that check_action_inherit reads added to the call's value `value`. Whether it took it.
//
// The key is the record's type, its standing (an lvalue, an integer constant, or neither) and the parameter's type. Where
// the record isn't wide (no fault, overload set, bit-field's width or integer past 64 bits), its value has no constant but
// an address, none of BUSTER_NODE_MACHINE_MEMO_FLAGS and isn't an enumeration's lvalue (whose completeness at the node the
// rule asks), the rule reads of it only its type, whether it is an lvalue and the type table: check_action_null is its
// type's alone, and no conversion folds. Its answer is then the key's, and a hit answers where the conversion changes
// nothing that check_action_inherit reads (where the record's kind and the conversion's are node_machine_memo_kind's, an
// array's or a function's, which decay to a pointer), adding the record's inherited flags themselves. An integer constant
// (an rvalue of an integer type) is a key of its own, whose hit answers where node_machine_memo_integers says so. A miss
// runs the rule and keeps its answer; any other argument takes the rule.
static bool node_machine_argument_memo(NodeMachine* machine, uint32_t a, uint32_t node, TypeId parameter, CheckValue* value, CheckConversion* conversion)
{
    const NodeMachineColumns* columns = machine->columns;
    const uint8_t record = columns->standings[a];
    const TypeId type = columns->value_types[a];
    const uint32_t flags = columns->value_flags[a];
    const uint8_t kind = (uint8_t)(columns->words[a] >> 16);
    const CheckConstantKind constant_kind = (CheckConstantKind)((record >> 2) & 0x07u);
    const bool lvalue = (CheckCategory)(record & 0x03u) == CHECK_LVALUE;
    const bool integer = constant_kind == CHECK_CONSTANT_INTEGER;
    const bool constant = constant_kind == CHECK_CONSTANT_NONE || constant_kind == CHECK_CONSTANT_ADDRESS || (integer && !lvalue && kind == BUSTER_TYPE_KIND_INTEGER);
    const bool eligible = (record & BUSTER_NODE_MACHINE_WIDE) == 0 && constant && (flags & BUSTER_NODE_MACHINE_MEMO_FLAGS) == 0 &&
                          !(lvalue && kind == BUSTER_TYPE_KIND_ENUM);
    const uint8_t standing = BUSTER_NODE_MACHINE_MEMO_FILLED | (lvalue ? BUSTER_NODE_MACHINE_MEMO_LVALUE : 0) | (integer ? BUSTER_NODE_MACHINE_MEMO_INTEGER : 0);
    const uint32_t slot = ((type ^ parameter * 0x85ebca77u ^ standing) * 0x9e3779b1u) >> (32 - BUSTER_NODE_MACHINE_MEMO_BITS);
    NodeMachineMemo* entry = machine->argument_memo + slot;
    const bool matched = eligible && entry->source == type && entry->parameter == parameter && entry->standing == standing;
    bool taken = false;
    if (matched && (entry->answer & BUSTER_NODE_MACHINE_MEMO_ANSWERS) != 0)
    {
        taken = (entry->answer & BUSTER_NODE_MACHINE_MEMO_TAKEN) != 0;
        *conversion = (CheckConversion){ .type = entry->converted, .flags = entry->flags };
        // check_action_inherit of the record's value: its inherited flags, and no fault, which a record that isn't wide has.
        value->flags |= taken ? flags & BUSTER_CHECK_INHERITED : 0;
    }
    else
    {
        CheckValue argument;
        taken = node_machine_argument(machine, a, node, parameter, &argument, conversion);
        if (taken)
        {
            check_action_inherit(value, &argument);
        }
        const bool fill = eligible && !matched;
        if (fill)
        {
            const bool same = !taken || ((node_machine_memo_kind(kind) || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION) &&
                                         node_machine_memo_kind(check_action_kind(machine->types, conversion->type)));
            const bool answers = same && (!integer || node_machine_memo_integers(machine, parameter));
            *entry = (NodeMachineMemo){
                .source = type,
                .parameter = parameter,
                .converted = conversion->type,
                .flags = (uint16_t)conversion->flags,
                .standing = standing,
                .answer = (uint8_t)((taken ? BUSTER_NODE_MACHINE_MEMO_TAKEN : 0) | (answers ? BUSTER_NODE_MACHINE_MEMO_ANSWERS : 0)),
            };
        }
        machine->memo_fills += fill;
        machine->memo_rules += !fill;
    }
    return taken;
}

// T's call, record `r` (node `node`) of callee record `a`, by check_action_call's steps where its callee's prototype takes
// its arguments' count, its result is void or complete at the node's position and node_machine_argument takes each
// argument: whether it typed the node, its arguments' conversions in their column (NodeMachine.argument_conversions).
static bool node_machine_call_rule(NodeMachine* machine, uint32_t r, uint32_t node, uint32_t a)
{
    TypeTable* types = machine->types;
    const TypeId function = node_machine_callee_function(machine, a);
    NodeMachineOperand callee;
    CheckConversion callee_conversion;
    bool taken = function != 0 && node_machine_operand_value(machine, a, &callee, &callee_conversion) && node_machine_record_fault(machine, a) == CHECK_FAULT_NONE;
    uint32_t parameters = 0;
    const TypeId* parameter_types = taken ? type_parameters(types, function, &parameters) : NULL;
    const uint8_t flags = taken ? types->types.flags[type_index(function)] : 0;
    const uint32_t count = machine->columns->argument_counts[r];
    const TypeId result = taken ? type_return(types, function) : 0;
    taken = taken && (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 && count >= parameters && ((flags & BUSTER_TYPE_FLAG_VARIADIC) != 0 || count == parameters) &&
            (check_action_kind(types, result) == BUSTER_TYPE_KIND_VOID || node_machine_complete(machine, result, node));
    // check_action_call's result: its return type's rvalue with an effect, and its callee's and its arguments' flags (and
    // an argument's fault), whole on the stack and then into the record.
    CheckValue value = { .type = result, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_SIDE_EFFECT | (callee.flags & BUSTER_CHECK_INHERITED) };
    const uint32_t first = machine->columns->argument_first[r];
    for (uint32_t i = 0; taken && i < count; i += 1)
    {
        const TypeId parameter = i < parameters ? parameter_types[i] : 0;
        taken = node_machine_argument_memo(machine, machine->arguments[first + i], node, parameter, &value, machine->argument_conversions + first + i);
    }
    if (taken)
    {
        // The value's type and flags alone, into the columns, but where an argument's fault came with them.
        value.flags = check_value_source_flags(CHECK_OP_CALL, value.flags);
        if (value.fault == CHECK_FAULT_NONE)
        {
            node_machine_record(machine, r, value.type, CHECK_RVALUE, value.flags, CHECK_CONSTANT_NONE, 0, 0);
        }
        else
        {
            node_machine_put_value(machine, r, &value);
        }
        const CheckConversion none = { 0 };
        node_machine_conversions(machine, r, &callee_conversion, &none, 0);
    }
    return taken;
}

// An increment's or a decrement's right operand, check_action_update's: the int 1, an integer constant expression.
static const CheckValue node_machine_one = {
    .type = BUSTER_TYPE_INT,
    .flags = BUSTER_CHECK_ICE,
    .constant_kind = CHECK_CONSTANT_INTEGER,
    .integer = 1,
};

// The arithmetic operator an update computes with (check_action_update's), or CHECK_OP_COUNT for an operator that isn't
// an update.
static CheckOp node_machine_update_op(CheckOp op)
{
    CheckOp computed = CHECK_OP_COUNT;
    switch (op)
    {
        case CHECK_OP_PRE_INCREMENT: case CHECK_OP_POST_INCREMENT: case CHECK_OP_ADD_ASSIGN: computed = CHECK_OP_ADD; break;
        case CHECK_OP_PRE_DECREMENT: case CHECK_OP_POST_DECREMENT: case CHECK_OP_SUBTRACT_ASSIGN: computed = CHECK_OP_SUBTRACT; break;
        case CHECK_OP_MULTIPLY_ASSIGN: computed = CHECK_OP_MULTIPLY; break;
        case CHECK_OP_DIVIDE_ASSIGN: computed = CHECK_OP_DIVIDE; break;
        case CHECK_OP_REMAINDER_ASSIGN: computed = CHECK_OP_REMAINDER; break;
        case CHECK_OP_SHIFT_LEFT_ASSIGN: computed = CHECK_OP_SHIFT_LEFT; break;
        case CHECK_OP_SHIFT_RIGHT_ASSIGN: computed = CHECK_OP_SHIFT_RIGHT; break;
        case CHECK_OP_BIT_AND_ASSIGN: computed = CHECK_OP_BIT_AND; break;
        case CHECK_OP_BIT_OR_ASSIGN: computed = CHECK_OP_BIT_OR; break;
        case CHECK_OP_BIT_XOR_ASSIGN: computed = CHECK_OP_BIT_XOR; break;
        default: break;
    }
    return computed;
}

// The end of check_action_update's steps, at record `r` (node `node`, operator `op`) of destination record `a`, from its
// computation's answer `result` and conversions: the answer converted to the destination's type as it is stored, or for
// a postfix update the destination's old value (check_action_operand_value's of an lvalue the rules take) with the
// answer's effects; an rvalue with a side effect and a modification.
static __attribute__((noinline)) void node_machine_update_value(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a, CheckValue* result,
                                                                const CheckConversion* first, const CheckConversion* second)
{
    TypeTable* types = machine->types;
    CheckConversion store = { 0 };
    check_action_conversion(types, result, type_conversion_operand(types, machine->columns->value_types[a]), BUSTER_CHECK_CONVERT_ASSIGNMENT, &store, node);
    CheckValue updated;
    if (op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT)
    {
        node_machine_record_value(machine, a, &updated);
        updated.constant_kind = updated.constant_kind == CHECK_CONSTANT_ADDRESS || (updated.flags & BUSTER_CHECK_ICE) == 0 ? CHECK_CONSTANT_NONE : updated.constant_kind;
        updated.type = type_conversion_operand(types, updated.type);
        updated.flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART);
        check_action_inherit(&updated, result);
        updated.flags &= ~BUSTER_CHECK_BIT_FIELD;
        updated.bit_width = 0;
    }
    else
    {
        check_value_copy(&updated, result);
    }
    updated.category = CHECK_RVALUE;
    updated.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER |
                       BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME);
    updated.flags |= BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION;
    updated.constant_kind = updated.fault != CHECK_FAULT_NONE ? CHECK_CONSTANT_NONE : updated.constant_kind;
    updated.flags = check_value_source_flags(op, updated.flags);
    node_machine_put_value(machine, r, &updated);
    node_machine_conversions(machine, r, first, second, 0);
}

// T's update of an arithmetic destination, `++x`, `--x`, `x++`, `x--` or `x op= y` at record `r` (node `node`) of
// records `a` and `b`, by check_action_update's steps where the destination is a modifiable lvalue of a builtin
// arithmetic type without qualifiers, a bit-field's width, a vector's duplicate or a volatile part, and its computation's
// row of the transition table takes it and the value (an increment's int 1) as check_action_arithmetic would: whether
// it typed the node.
static __attribute__((noinline)) bool node_machine_update_rule(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a, uint32_t b)
{
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    const bool present = increment || b < r;
    // node_machine_one's standing and flags, or record `b`'s.
    const uint8_t standing_a = machine->columns->standings[a];
    const uint8_t standing_b = increment ? BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_INTEGER) : b < r ? machine->columns->standings[b] : 0;
    const uint32_t flags_a = machine->columns->value_flags[a];
    const uint32_t flags_b = increment ? BUSTER_CHECK_ICE : b < r ? machine->columns->value_flags[b] : 0;
    const TypeId type_a = machine->columns->value_types[a];
    const uint8_t o = node_machine_row_operators[node_machine_update_op(op)];
    // A value's const qualifier goes with its lvalue's conversion, as node_machine_row's; the destination's is a diagnostic.
    const TypeId value_type = increment ? BUSTER_TYPE_INT : b < r ? machine->columns->value_types[b] & ~((standing_b & 0x03u) == CHECK_LVALUE ? (TypeId)BUSTER_TYPE_CONST : 0) : 0;
    const uint32_t code_a = type_index(type_a);
    const uint32_t code_b = type_index(value_type);
    const bool builtin = present && o != NODE_MACHINE_ROW_NONE && (standing_a & 0x03u) == CHECK_LVALUE && (type_a & BUSTER_TYPE_QUALIFIERS) == 0 &&
                         code_a < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(code_a) && (value_type & BUSTER_TYPE_QUALIFIERS) == 0 &&
                         code_b < BUSTER_NODE_MACHINE_KINDS && (flags_a & (BUSTER_CHECK_VECTOR_DUPLICATE | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_VOLATILE_PART)) == 0;
    const NodeMachineRow* row = builtin ? &node_machine_rows[o][code_a][code_b] : NULL;
    const uint32_t allowed = builtin ? node_machine_row_allowed[o] : 0;
    // A wide operand's rare fields, read for one alone, as node_machine_row's.
    const bool taken = row != NULL && row->result != 0 && node_machine_row_takes(row, allowed, standing_a, flags_a, standing_b, 0) &&
                       node_machine_row_takes(row, allowed, standing_b, flags_b, standing_a, 1) && node_machine_row_plain(machine, a) &&
                       (increment || node_machine_row_plain(machine, b));
    if (taken)
    {
        CheckValue result;
        memset(&result, 0, sizeof(result));
        result.type = BUSTER_TYPE_BUILTIN(row->result);
        result.category = CHECK_RVALUE;
        result.flags = row->flags | ((flags_a | flags_b) & node_machine_row_passed[o]);
        const CheckConversion first = { .type = BUSTER_TYPE_BUILTIN(row->converted[0]), .flags = row->conversion_flags[0] | BUSTER_CHECK_CONVERT_LVALUE };
        const CheckConversion second = {
            .type = increment ? 0 : BUSTER_TYPE_BUILTIN(row->converted[1]),
            .flags = increment ? 0 : row->conversion_flags[1] | ((standing_b & 0x03u) == CHECK_LVALUE ? BUSTER_CHECK_CONVERT_LVALUE : 0),
        };
        node_machine_update_value(machine, r, node, op, a, &result, &first, &second);
    }
    return taken;
}

// T's update of a pointer, `++p`, `--p`, `p++`, `p--`, `p += n` or `p -= n` at record `r` (node `node`) of records `a`
// and `b`, by check_action_update's and check_action_pointer_arithmetic's steps where the destination is a modifiable
// lvalue of an unqualified pointer to a complete object type the common conversions take, and the value an integer:
// whether it typed the node.
static __attribute__((noinline)) bool node_machine_update_pointer_rule(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a, uint32_t b)
{
    TypeTable* types = machine->types;
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    const uint8_t standing_a = machine->columns->standings[a];
    const uint32_t flags_a = machine->columns->value_flags[a];
    NodeMachineOperand pointer_operand;
    NodeMachineOperand index_operand;
    CheckConversion conversions[2] = { { 0 }, { 0 } };
    bool taken = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_SUBTRACT_ASSIGN && (standing_a & 0x03u) == CHECK_LVALUE &&
                 (machine->columns->value_types[a] & BUSTER_TYPE_QUALIFIERS) == 0 &&
                 (flags_a & (BUSTER_CHECK_VECTOR_DUPLICATE | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_VOLATILE_PART)) == 0 &&
                 node_machine_operand_value(machine, a, &pointer_operand, conversions) && check_action_kind(types, pointer_operand.type) == BUSTER_TYPE_KIND_POINTER &&
                 pointer_operand.constant_kind == CHECK_CONSTANT_NONE && node_machine_record_fault(machine, a) == CHECK_FAULT_NONE;
    if (taken && increment)
    {
        conversions[1] = (CheckConversion){ .type = BUSTER_TYPE_INT };
    }
    else if (taken)
    {
        taken = b < r && node_machine_operand_value(machine, b, &index_operand, conversions + 1) && check_action_is_integer(types, index_operand.type);
    }
    if (taken)
    {
        const TypeId element = type_element(types, type_strip_aligned(types, pointer_operand.type));
        const uint8_t kind = check_action_kind(types, element);
        taken = kind != BUSTER_TYPE_KIND_VOID && kind != BUSTER_TYPE_KIND_FUNCTION && node_machine_complete(machine, element, node);
    }
    if (taken)
    {
        // The pointer and the index whole, for check_action_inherit and check_action_promote.
        CheckValue pointer;
        CheckValue index;
        node_machine_operand_whole(machine, &pointer_operand, &pointer);
        if (increment)
        {
            check_value_copy(&index, &node_machine_one);
        }
        else
        {
            node_machine_operand_whole(machine, &index_operand, &index);
        }
        CheckValue result;
        memset(&result, 0, sizeof(result));
        check_action_inherit(&result, &pointer);
        check_action_inherit(&result, &index);
        check_action_promote(types, &index, conversions + 1, node);
        result.type = pointer.type;
        result.category = CHECK_RVALUE;
        result.flags |= pointer.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS);
        const CheckConversion none = { 0 };
        node_machine_update_value(machine, r, node, op, a, &result, conversions, increment ? &none : conversions + 1);
    }
    return taken;
}

// T's integer constant fold: unary +, - or ~, or a binary arithmetic, shift, bitwise or comparison operator `op` at
// record `r` of records `a` and `b` (UINT32_MAX for a unary one), each an rvalue integer constant its columns hold whole,
// of an unqualified builtin integer type of at most 64 bits but bool, by check_action_arithmetic's steps: the operator's
// row's conversions and result type (unary + takes -'s, the same promotion), its flags from the operands' (their
// inherited ones and the integer constant expression's forms) and check_action_fold_integer's value, where the fold
// faults nothing (a signed overflow, a division by zero, a shift out of range or of a negative value, which the action
// takes): whether it typed the node.
static __attribute__((noinline)) bool node_machine_fold(NodeMachine* machine, uint32_t r, CheckOp op, uint32_t a, uint32_t b)
{
    const bool unary = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
    const uint32_t right = unary ? a : b;
    const uint8_t o = op == CHECK_OP_POSITIVE ? (uint8_t)NODE_MACHINE_ROW_NEGATIVE : node_machine_row_operators[op];
    const uint8_t constant = BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_INTEGER);
    const bool shaped = o != NODE_MACHINE_ROW_NONE && right < r && (unary ? b == UINT32_MAX : b != UINT32_MAX);
    const TypeId type_a = shaped ? machine->columns->value_types[a] : 0;
    const TypeId type_b = shaped ? machine->columns->value_types[right] : 0;
    const uint32_t code_a = type_index(type_a);
    const uint32_t code_b = type_index(type_b);
    const uint32_t flags_a = shaped ? machine->columns->value_flags[a] : 0;
    const uint32_t flags_b = shaped ? machine->columns->value_flags[right] : 0;
    const bool constants = shaped && machine->columns->standings[a] == constant && machine->columns->standings[right] == constant &&
                           ((type_a | type_b) & BUSTER_TYPE_QUALIFIERS) == 0 && code_a < BUSTER_NODE_MACHINE_KINDS && code_b < BUSTER_NODE_MACHINE_KINDS &&
                           node_machine_integer_widths[code_a] != 0 && node_machine_integer_widths[code_b] != 0 && ((flags_a | flags_b) & BUSTER_CHECK_BIT_FIELD) == 0;
    const NodeMachineRow* row = constants ? &node_machine_rows[o][code_a][unary ? 0 : code_b] : NULL;
    const uint32_t converted_a = row != NULL ? row->converted[0] : 0;
    const uint32_t converted_b = row != NULL ? (unary ? converted_a : row->converted[1]) : 0;
    const uint32_t width = node_machine_integer_widths[converted_a];
    const uint32_t right_width = node_machine_integer_widths[converted_b];
    bool taken = row != NULL && row->result != 0 && width != 0 && right_width != 0;
    if (taken)
    {
        const bool is_signed = node_machine_integer_sign(machine, converted_a);
        const bool right_signed = node_machine_integer_sign(machine, converted_b);
        const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        const uint64_t right_mask = right_width == 64 ? UINT64_MAX : (UINT64_C(1) << right_width) - 1;
        const uint64_t x = node_machine_convert_bits(machine->columns->payloads[a], node_machine_integer_widths[code_a], node_machine_integer_sign(machine, code_a), width);
        const uint64_t y = unary ? 0 : node_machine_convert_bits(machine->columns->payloads[b], node_machine_integer_widths[code_b], node_machine_integer_sign(machine, code_b),
                                                                 right_width);
        const uint64_t sign = UINT64_C(1) << (width - 1);
        const bool xn = is_signed && (x & sign) != 0;
        const bool yn = right_signed && ((y >> (right_width - 1)) & 1) != 0;
        const uint64_t xm = xn ? (0 - x) & mask : x;
        const uint64_t ym = yn ? (0 - y) & right_mask : y;
        uint64_t answer = 0;
        bool fault = false;
        switch (op)
        {
            case CHECK_OP_POSITIVE: answer = x; break;
            case CHECK_OP_NEGATIVE:
            {
                answer = (0 - x) & mask;
                fault = is_signed && x == sign;
            } break;
            case CHECK_OP_BIT_NOT: answer = ~x & mask; break;
            case CHECK_OP_ADD:
            {
                answer = (x + y) & mask;
                fault = is_signed && (~(x ^ y) & (x ^ answer) & sign) != 0;
            } break;
            case CHECK_OP_SUBTRACT:
            {
                answer = (x - y) & mask;
                fault = is_signed && ((x ^ y) & (x ^ answer) & sign) != 0;
            } break;
            case CHECK_OP_MULTIPLY:
            {
                answer = (x * y) & mask;
                const uint64_t limit = (sign - 1) + (xn != yn);
                fault = is_signed && ym != 0 && xm > limit / ym;
            } break;
            case CHECK_OP_DIVIDE:
            case CHECK_OP_REMAINDER:
            {
                fault = y == 0 || (is_signed && x == sign && y == mask);
                const uint64_t magnitude = fault ? 0 : op == CHECK_OP_DIVIDE ? xm / ym : xm % ym;
                const bool negative = op == CHECK_OP_DIVIDE ? xn != yn : xn;
                answer = (negative ? 0 - magnitude : magnitude) & mask;
            } break;
            case CHECK_OP_SHIFT_LEFT:
            case CHECK_OP_SHIFT_RIGHT:
            {
                fault = yn || y >= width;
                const uint32_t shift = fault ? 0 : (uint32_t)y;
                if (op == CHECK_OP_SHIFT_LEFT)
                {
                    answer = (x << shift) & mask;
                    // Clang's C constant evaluator admits a nonnegative value shifted into the sign bit (the action's).
                    fault = fault || (is_signed && (xn || x > mask >> shift));
                }
                else
                {
                    answer = (x >> shift) | (xn ? mask ^ (mask >> shift) : 0);
                }
            } break;
            case CHECK_OP_LESS: answer = xn != yn ? xn : x < y; break;
            case CHECK_OP_LESS_EQUAL: answer = xn != yn ? xn : x <= y; break;
            case CHECK_OP_GREATER: answer = xn != yn ? yn : x > y; break;
            case CHECK_OP_GREATER_EQUAL: answer = xn != yn ? yn : x >= y; break;
            case CHECK_OP_EQUAL: answer = x == y; break;
            case CHECK_OP_NOT_EQUAL: answer = x != y; break;
            case CHECK_OP_BIT_AND: answer = x & y; break;
            case CHECK_OP_BIT_OR: answer = x | y; break;
            case CHECK_OP_BIT_XOR: answer = x ^ y; break;
            default: fault = true; break;
        }
        taken = !fault;
        if (taken)
        {
            const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
            const bool ice = (flags_a & BUSTER_CHECK_ICE) != 0 && (flags_b & BUSTER_CHECK_ICE) != 0;
            const bool gnu_ice = !ice && (flags_a & forms) != 0 && (flags_b & forms) != 0;
            const uint32_t flags = ((flags_a | flags_b) & BUSTER_CHECK_INHERITED) | (ice ? BUSTER_CHECK_ICE : gnu_ice ? BUSTER_CHECK_GNU_ICE_FORM : 0);
            node_machine_record(machine, r, BUSTER_TYPE_BUILTIN(row->result), CHECK_RVALUE, check_value_source_flags(op, flags), CHECK_CONSTANT_INTEGER, 0, answer);
            machine->columns->converted[2 * r] = BUSTER_TYPE_BUILTIN(row->converted[0]);
            machine->columns->converted[2 * r + 1] = unary ? 0 : BUSTER_TYPE_BUILTIN(row->converted[1]);
            machine->columns->conversion_flags[2 * r] = row->conversion_flags[0];
            machine->columns->conversion_flags[2 * r + 1] = unary ? 0 : row->conversion_flags[1];
            machine->columns->member_offsets[r] = 0;
        }
    }
    return taken;
}

// Whether type code `code` is a builtin integer type's, bool's and the 128-bit ones' too.
static inline bool node_machine_integer_code(uint32_t code)
{
    return code >= BUSTER_TYPE_CODE_BOOL && code <= BUSTER_TYPE_CODE_UINT128;
}

// T's cast at record `r` (node `node`) of record `a` to void, a builtin integer type or a pointer, of a value of a builtin
// integer type or a pointer as the common conversions take it (an array or a function decayed, an lvalue of
// node_machine_plain_kind converted), held whole by the columns, by check_action_cast's steps: whether it typed the node.
// With no constant, check_action_convert_plain and check_action_cast_provenance change its type, category, flags and
// constant kind alone (and a conversion to bool its integer, to 0), on the columns; a constant is made whole for them.
static __attribute__((noinline)) bool node_machine_cast(NodeMachine* machine, uint32_t r, uint32_t node, uint32_t a)
{
    TypeTable* types = machine->types;
    const TypeId written = machine->graph->types != NULL ? machine->graph->types[node] : 0;
    NodeMachineOperand source;
    CheckConversion conversion;
    bool taken = written != 0 && type_is_valid(types, written) && (machine->columns->standings[a] & BUSTER_NODE_MACHINE_WIDE) == 0 &&
                 node_machine_operand_value(machine, a, &source, &conversion);
    const TypeId target = taken ? type_unqualified(type_strip_aligned(types, written)) : 0;
    const uint8_t target_kind = taken ? type_kind(types, target) : BUSTER_TYPE_KIND_NONE;
    const bool target_integer = node_machine_integer_code(type_index(target)) && target_kind == BUSTER_TYPE_KIND_INTEGER;
    const bool target_pointer = target_kind == BUSTER_TYPE_KIND_POINTER;
    const bool target_void = target_kind == BUSTER_TYPE_KIND_VOID;
    const bool source_integer = taken && (source.type & BUSTER_TYPE_QUALIFIERS) == 0 && node_machine_integer_code(type_index(source.type)) &&
                                type_kind(types, source.type) == BUSTER_TYPE_KIND_INTEGER;
    const bool source_pointer = taken && check_action_kind(types, source.type) == BUSTER_TYPE_KIND_POINTER;
    taken = taken && (target_integer || target_pointer || target_void) && (source_integer || source_pointer);
    if (taken)
    {
        const uint32_t extensions = machine->graph->extensions;
        const bool ice = target_integer && (source.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_FLOAT_LITERAL)) != 0;
        const bool gnu_ice = target_integer && (source.flags & BUSTER_CHECK_GNU_ICE_FORM) != 0;
        const bool void_pointer = target_pointer && check_action_kind(types, type_element(types, target)) == BUSTER_TYPE_KIND_VOID;
        conversion.flags |= BUSTER_CHECK_CONVERT_CAST;
        conversion.type = target;
        if (source.constant_kind == CHECK_CONSTANT_NONE)
        {
            const TypeId from = source_pointer ? type_unqualified(type_strip_aligned(types, type_element(types, source.type))) : 0;
            const TypeId to = target_pointer ? type_unqualified(type_strip_aligned(types, type_element(types, target))) : 0;
            const bool restricted = (source_pointer || target_pointer) && !(source_pointer && target_pointer && from == to);
            const bool null = void_pointer && (source.flags & BUSTER_CHECK_NULL_POINTER) != 0;
            uint32_t flags = source.flags | (restricted ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0);
            flags &= ~(BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM |
                       BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
            flags |= (flags & BUSTER_CHECK_GNU_FLOAT_RANGE) != 0 ? 0 : ice ? BUSTER_CHECK_ICE : gnu_ice ? BUSTER_CHECK_GNU_ICE_FORM : 0;
            flags |= null ? BUSTER_CHECK_NULL_POINTER : 0;
            flags &= target_void ? ~(uint32_t)BUSTER_CHECK_MODIFICATION : ~0u;
            const bool bool_target = target == BUSTER_TYPE_BOOL;
            node_machine_record_from(machine, r, source.record, target, CHECK_RVALUE, check_value_source_flags(CHECK_OP_CAST, flags), CHECK_CONSTANT_NONE, bool_target, 0);
        }
        else
        {
            CheckValue value;
            node_machine_operand_whole(machine, &source, &value);
            const bool null = void_pointer && check_action_null(types, &value, extensions);
            check_action_conversion(types, &value, target, 0, &conversion, node);
            check_action_cast_provenance(&value, ice, gnu_ice, null, target_void, true);
            value.flags = check_value_source_flags(CHECK_OP_CAST, value.flags);
            node_machine_put_value(machine, r, &value);
        }
        const CheckConversion none = { 0 };
        node_machine_conversions(machine, r, &conversion, &none, 0);
    }
    return taken;
}

// T's equality of a pointer and a null pointer constant, `==` or `!=` at record `r` of records `a` and `b`, by
// check_action_pointer_compare's steps: the pointer's type is the operands' common one (check_action_pointer_common's
// first branches), and the null pointer constant converts to it. Both operands as the common conversions take them, held
// whole by the columns, and no fold (the pointer no address constant without an object, which the action compares with
// the null pointer's): whether it typed the node.
static __attribute__((noinline)) bool node_machine_null_compare(NodeMachine* machine, uint32_t r, CheckOp op, uint32_t a, uint32_t b)
{
    TypeTable* types = machine->types;
    NodeMachineOperand left;
    NodeMachineOperand right;
    CheckConversion conversions[2];
    bool taken = (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL) && b < r && ((machine->columns->standings[a] | machine->columns->standings[b]) & BUSTER_NODE_MACHINE_WIDE) == 0 &&
                 node_machine_operand_value(machine, a, &left, conversions) && node_machine_operand_value(machine, b, &right, conversions + 1);
    const bool left_pointer = taken && check_action_kind(types, left.type) == BUSTER_TYPE_KIND_POINTER;
    const bool right_pointer = taken && check_action_kind(types, right.type) == BUSTER_TYPE_KIND_POINTER;
    const bool left_common = left_pointer && node_machine_null(machine, &right);
    const bool right_common = !left_common && right_pointer && taken && node_machine_null(machine, &left);
    const NodeMachineOperand* pointer = left_common ? &left : &right;
    taken = (left_common || right_common) && !(pointer->constant_kind == CHECK_CONSTANT_ADDRESS && machine->columns->objects[pointer->record] == 0);
    if (taken)
    {
        // The null pointer constant's conversion, where its type isn't the pointer's.
        const TypeId common = pointer->type;
        conversions[0].flags |= left.type == common ? 0 : BUSTER_CHECK_CONVERT_NULL;
        conversions[1].flags |= right.type == common ? 0 : BUSTER_CHECK_CONVERT_NULL;
        conversions[0].type = common;
        conversions[1].type = common;
        const uint32_t flags = (left.flags | right.flags) & BUSTER_CHECK_INHERITED;
        node_machine_record(machine, r, BUSTER_TYPE_INT, CHECK_RVALUE, check_value_source_flags(op, flags), CHECK_CONSTANT_NONE, 0, 0);
        node_machine_conversions(machine, r, conversions, conversions + 1, 0);
    }
    return taken;
}

// T's sizeof or _Alignof at record `r` (node `node`), of a type name or of record `a` (UINT32_MAX for a type name's), by
// check_action_query's steps for a type complete at the node that is no function, void or variably modified type, and
// an operand no bit-field: whether it typed the node.
static __attribute__((noinline)) bool node_machine_query(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a)
{
    TypeTable* types = machine->types;
    const TypeId named = machine->graph->types != NULL ? machine->graph->types[node] : 0;
    const bool operand = named == 0;
    bool taken = operand ? a < r && (machine->columns->standings[a] & BUSTER_NODE_MACHINE_WIDE) == 0 &&
                           !(node_machine_record_category(machine, a) == CHECK_LVALUE && (machine->columns->value_flags[a] & BUSTER_CHECK_BIT_FIELD) != 0) :
                           a == UINT32_MAX && type_is_valid(types, named);
    const TypeId queried = taken ? (operand ? machine->columns->value_types[a] : named) : 0;
    const TypeId plain = taken ? type_strip_aligned(types, queried) : 0;
    const TypeId target = taken && op == CHECK_OP_ALIGNOF && type_kind(types, plain) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ? type_element(types, plain) : queried;
    const uint8_t kind = taken ? check_action_kind(types, target) : BUSTER_TYPE_KIND_NONE;
    taken = taken && kind != BUSTER_TYPE_KIND_FUNCTION && kind != BUSTER_TYPE_KIND_VOID && node_machine_complete(machine, target, node) && !check_action_runtime_size(types, target);
    if (taken)
    {
        const uint64_t answer = op == CHECK_OP_SIZEOF ? type_size(types, target) : type_alignment(types, target);
        node_machine_record(machine, r, BUSTER_TYPE_ULONG, CHECK_RVALUE, BUSTER_CHECK_ICE, CHECK_CONSTANT_INTEGER, 0, answer);
        const CheckConversion first = { .type = operand ? machine->columns->value_types[a] : 0 };
        const CheckConversion none = { 0 };
        node_machine_conversions(machine, r, &first, &none, 0);
    }
    return taken;
}

// T's comma at record `r` of records `a` and `b`, by check_action_comma_provenance's steps outside a constant evaluation:
// the right operand's value as the common conversions take it, without its forms but an ICE_UNEVALUATED where both
// operands have one, and with the left's inherited flags; both held whole by the columns (no fault to carry): whether it
// typed the node.
static __attribute__((noinline)) bool node_machine_comma(NodeMachine* machine, uint32_t r, uint32_t a, uint32_t b)
{
    NodeMachineOperand left;
    NodeMachineOperand right;
    CheckConversion conversions[2];
    const bool taken = b < r && ((machine->columns->standings[a] | machine->columns->standings[b]) & BUSTER_NODE_MACHINE_WIDE) == 0 &&
                       node_machine_operand_value(machine, a, &left, conversions) && node_machine_operand_value(machine, b, &right, conversions + 1);
    if (taken)
    {
        const uint32_t allowed = BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM;
        uint32_t flags = right.flags & ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
        flags |= (left.flags & allowed) != 0 && (right.flags & allowed) != 0 ? BUSTER_CHECK_ICE_UNEVALUATED : 0;
        flags |= left.flags & BUSTER_CHECK_INHERITED;
        node_machine_record_from(machine, r, right.record, right.type, CHECK_RVALUE, check_value_source_flags(CHECK_OP_COMMA, flags), right.constant_kind, false, 0);
        node_machine_conversions(machine, r, conversions, conversions + 1, 0);
    }
    return taken;
}

// T's conditional at record `r` (node `node`) of records `a` (the condition), `b` and `c` (the arms), by
// check_action_conditional's steps where the condition is a scalar and the arms are of builtin arithmetic types that the
// add's row takes (whose conversions are the usual arithmetic ones the conditional makes) or either is void, each as the
// common conversions take it and held whole by the columns: whether it typed the node. The values are made whole for the
// action's own conversions and check_action_conditional_provenance.
static __attribute__((noinline)) bool node_machine_conditional(NodeMachine* machine, uint32_t r, uint32_t node, uint32_t a, uint32_t b, uint32_t c)
{
    TypeTable* types = machine->types;
    NodeMachineOperand condition;
    NodeMachineOperand left;
    NodeMachineOperand right;
    CheckConversion conversions[3];
    bool taken = b < r && c < r && ((machine->columns->standings[a] | machine->columns->standings[b] | machine->columns->standings[c]) & BUSTER_NODE_MACHINE_WIDE) == 0 &&
                 node_machine_operand_value(machine, a, &condition, conversions) && node_machine_operand_value(machine, b, &left, conversions + 1) &&
                 node_machine_operand_value(machine, c, &right, conversions + 2) && check_action_is_scalar(types, condition.type);
    const uint32_t code_left = type_index(left.type);
    const uint32_t code_right = type_index(right.type);
    const bool arithmetic = taken && ((left.type | right.type) & BUSTER_TYPE_QUALIFIERS) == 0 && code_left < BUSTER_NODE_MACHINE_KINDS &&
                            code_right < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(code_left) && node_machine_row_kind(code_right);
    const NodeMachineRow* row = arithmetic ? &node_machine_rows[NODE_MACHINE_ROW_ADD][code_left][code_right] : NULL;
    const bool void_arm = taken && !arithmetic &&
                          (check_action_kind(types, left.type) == BUSTER_TYPE_KIND_VOID || check_action_kind(types, right.type) == BUSTER_TYPE_KIND_VOID);
    taken = (row != NULL && row->result != 0) || void_arm;
    if (taken)
    {
        CheckValue values[3];
        node_machine_operand_whole(machine, &condition, values);
        node_machine_operand_whole(machine, &left, values + 1);
        node_machine_operand_whole(machine, &right, values + 2);
        TypeId common = BUSTER_TYPE_VOID;
        if (row != NULL)
        {
            check_action_promote(types, values + 1, conversions + 1, node);
            check_action_promote(types, values + 2, conversions + 2, node);
            common = BUSTER_TYPE_BUILTIN(row->converted[0]);
        }
        check_action_conversion(types, values + 1, common, values[1].type == common ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, conversions + 1, node);
        check_action_conversion(types, values + 2, common, values[2].type == common ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, conversions + 2, node);
        bool truth = false;
        const bool known = check_action_truth(types, values, &truth);
        CheckValue value;
        memset(&value, 0, sizeof(value));
        check_action_conditional_provenance(types, &value, common, values, values + 1, values + 2, known, truth);
        value.flags = check_value_source_flags(CHECK_OP_CONDITIONAL, value.flags);
        node_machine_put_value(machine, r, &value);
        node_machine_conversions(machine, r, conversions, conversions + 1, 0);
    }
    return taken;
}

// T's direct rules for the operators whose type a lookup gives, each by its action's own steps for the common case, the
// action taking the rest: whether one typed record `r` (node `node`, operator `op`, operands' records `a` and `b`).
static bool node_machine_rule(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a, uint32_t b)
{
    bool typed = false;
    if (op == CHECK_OP_CAST)
    {
        typed = node_machine_discard(machine, r, node, a) || node_machine_cast(machine, r, node, a);
    }
    else if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
    {
        typed = node_machine_query(machine, r, node, op, a);
    }
    else if (op == CHECK_OP_COMMA)
    {
        typed = node_machine_comma(machine, r, a, b);
    }
    else if (op == CHECK_OP_CONDITIONAL)
    {
        typed = node_machine_conditional(machine, r, node, a, b, node_machine_operand(machine, r, 2));
    }
    else if (op == CHECK_OP_POSITIVE || (node_machine_row_operators[op] != NODE_MACHINE_ROW_NONE && op != CHECK_OP_ADD && op != CHECK_OP_SUBTRACT))
    {
        // An operator of the rows that the row didn't type: its integer constants' fold, or a pointer's equality with a null
        // pointer constant.
        typed = node_machine_fold(machine, r, op, a, b) || node_machine_null_compare(machine, r, op, a, b);
    }
    else if (op == CHECK_OP_ARROW || op == CHECK_OP_MEMBER)
    {
        typed = node_machine_member(machine, r, node, op, a);
    }
    else if (op == CHECK_OP_CONDITION || op == CHECK_OP_NOT)
    {
        typed = node_machine_truth(machine, r, op, a);
    }
    else if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR)
    {
        typed = node_machine_logic(machine, r, op, a, b);
    }
    else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT)
    {
        typed = node_machine_store_rule(machine, r, node, op, a, b);
    }
    else if (op == CHECK_OP_ADDRESS)
    {
        typed = node_machine_address_rule(machine, r, a);
    }
    else if (op == CHECK_OP_DEREFERENCE)
    {
        typed = node_machine_dereference(machine, r, a);
    }
    else if (op == CHECK_OP_SUBSCRIPT || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
    {
        typed = node_machine_pointer_rule(machine, r, node, op, a, b) || (op != CHECK_OP_SUBSCRIPT && node_machine_fold(machine, r, op, a, b));
    }
    else if (op == CHECK_OP_CALL)
    {
        typed = node_machine_call_rule(machine, r, node, a);
    }
    else if (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)
    {
        typed = node_machine_update_rule(machine, r, node, op, a, b) || node_machine_update_pointer_rule(machine, r, node, op, a, b);
    }
    return typed;
}

// T's exact path for record `r` (node `node`, operator `op`): the action itself, as the graph run calls it, on the
// operands' records made whole, into `result`, and a call's on its arguments' records, their conversions into
// `argument_conversions` (as check_graph_action gives the action the stream's).
static void node_machine_action(const NodeMachine* machine, uint32_t r, CheckOp op, CheckConversion* argument_conversions, CheckActionResult* result)
{
    const CheckGraph* graph = machine->graph;
    const uint32_t node = node_machine_node(machine, r);
    CheckAction action;
    memset(&action, 0, sizeof(action));
    action.op = op;
    action.type = graph->types != NULL ? graph->types[node] : 0;
    action.member = graph->members != NULL ? graph->members[node] : 0;
    action.node = node;
    action.context = check_graph_context(graph, node, false);
    for (uint32_t k = 0; k < 3; k += 1)
    {
        const uint32_t operand = node_machine_operand(machine, r, k);
        if (operand < r)
        {
            node_machine_record_value(machine, operand, &action.operands[k]);
        }
    }
    if (op == CHECK_OP_CALL)
    {
        const uint32_t first = machine->columns->argument_first[r];
        action.argument_count = machine->columns->argument_counts[r];
        for (uint32_t i = 0; i < action.argument_count; i += 1)
        {
            node_machine_record_value(machine, machine->arguments[first + i], machine->argument_values + i);
        }
        action.arguments = machine->argument_values;
        action.argument_conversions = argument_conversions;
    }
    check_action_compute(machine->types, &action, result, 0, NULL);
}

// The action's answer stored into record `r`'s columns, as check_graph_store stores it.
static void node_machine_store(NodeMachine* machine, uint32_t r, CheckOp op, CheckActionResult* result)
{
    result->value.flags = check_value_source_flags(op, result->value.flags);
    node_machine_put_value(machine, r, &result->value);
    node_machine_conversions(machine, r, result->conversions, result->conversions + 1, result->member_offset);
}

// Whether two values are the same, field by field (their padding aside).
static bool node_machine_same_values(const CheckValue* left, const CheckValue* right)
{
    return left->type == right->type && left->category == right->category && left->flags == right->flags && left->constant_kind == right->constant_kind &&
           left->integer == right->integer && memcmp(left->floating_storage, right->floating_storage, 10) == 0 &&
           memcmp(left->imaginary_storage, right->imaginary_storage, 10) == 0 && left->object == right->object && left->overload_head == right->overload_head &&
           left->offset == right->offset && left->bit_width == right->bit_width && left->fault == right->fault && left->fault_node == right->fault_node &&
           left->object_bit_offset == right->object_bit_offset;
}

// Whether record `r`'s columns are what the action gives it: --verify's twin check of a node a row or a rule typed.
static bool node_machine_same_as_action(const NodeMachine* machine, uint32_t r, CheckOp op)
{
    CheckActionResult result;
    memset(&result, 0, sizeof(result));
    node_machine_action(machine, r, op, machine->verified, &result);
    CheckValue value;
    node_machine_record_value(machine, r, &value);
    bool same = result.diagnostic == CHECK_DIAG_NONE && node_machine_same_values(&result.value, &value) &&
                result.conversions[0].type == machine->columns->converted[2 * r] && result.conversions[1].type == machine->columns->converted[2 * r + 1] &&
                result.conversions[0].flags == machine->columns->conversion_flags[2 * r] && result.conversions[1].flags == machine->columns->conversion_flags[2 * r + 1] &&
                result.member_offset == machine->columns->member_offsets[r];
    // A call's arguments' conversions too.
    const uint32_t first = machine->columns->argument_first[r];
    for (uint32_t i = 0; op == CHECK_OP_CALL && i < machine->columns->argument_counts[r]; i += 1)
    {
        same = same && machine->verified[i].type == machine->argument_conversions[first + i].type && machine->verified[i].flags == machine->argument_conversions[first + i].flags;
    }
    return same;
}

// A leaf's value, check_graph_leaf_at's, into record `r`'s columns: from its record field by field where the columns hold
// it whole (no payload row and no overload set: its integer alone), and else made whole first. Its overload set.
static inline __attribute__((always_inline)) uint32_t node_machine_leaf(NodeMachine* machine, uint32_t r, uint32_t node)
{
    const CheckGraph* graph = machine->graph;
    const uint32_t slot = graph->leaf_slots[node];
    const CheckGraphLeaf* leaf = graph->dense_leaves == NULL ? graph->leaf_records + slot : NULL;
    uint32_t overload = 0;
    if (leaf != NULL && leaf->row == 0 && leaf->overload_head == 0 && leaf->category <= CHECK_FUNCTION && leaf->constant_kind <= CHECK_CONSTANT_LABEL_DIFFERENCE)
    {
        node_machine_record(machine, r, leaf->type, (CheckCategory)leaf->category, leaf->flags, (CheckConstantKind)leaf->constant_kind, leaf->object, leaf->integer);
    }
    else
    {
        const CheckValue value = check_graph_leaf_at(graph, slot);
        node_machine_put_value(machine, r, &value);
        overload = value.overload_head;
    }
    return overload;
}

// T: every record's value and its operands' conversions, up the postorder, for the expression of `count` records, graph
// nodes `first` to first + count - 2 and `root`. A refusal stops it, at machine->stopped.
static NodeMachineRefusal node_machine_type(NodeMachine* machine, uint32_t first, uint32_t root, uint32_t count)
{
    const CheckGraph* graph = machine->graph;
    NodeMachineRefusal refusal = count != 0 && count <= BUSTER_NODE_MACHINE_MOST && root >= first + count - 1 ? NODE_MACHINE_TAKEN : NODE_MACHINE_REFUSED_SHAPE;
    machine->first = first;
    machine->root = root;
    machine->count = refusal == NODE_MACHINE_TAKEN ? count : 0;
    machine->wide_count = 0;
    machine->argument_total = 0;
    uint32_t r = 0;
    while (refusal == NODE_MACHINE_TAKEN && r < machine->count)
    {
        const uint32_t node = node_machine_node(machine, r);
        const CheckOp op = graph->ops[node];
        const bool context = (graph->contexts != NULL && graph->contexts[node] != 0) || (graph->fp_environments != NULL && graph->fp_environments[node] != 0);
        uint32_t word = 0;
        machine->columns->codes[r] = (uint8_t)op;
        if (op == CHECK_OP_VALUE)
        {
            // A leaf, two in five records: its record is the reader's, with no operand to find and its type's word read once.
            const uint32_t overload = node_machine_leaf(machine, r, node);
            const TypeId type = machine->columns->value_types[r];
            word = node_machine_word(machine, type);
            const bool valid = (word & BUSTER_NODE_MACHINE_WORD_VALID) != 0 && (uint8_t)(word >> 16) != BUSTER_TYPE_KIND_VARIABLE_ARRAY;
            refusal = context ? NODE_MACHINE_REFUSED_CONTEXT : overload != 0 || !valid ? NODE_MACHINE_REFUSED_LEAF : NODE_MACHINE_TAKEN;
            machine->columns->left[r] = UINT32_MAX;
            machine->columns->right[r] = UINT32_MAX;
            machine->columns->lowest[r] = r;
            machine->columns->converted[2 * r] = type;
            machine->columns->converted[2 * r + 1] = 0;
            machine->columns->conversion_flags[2 * r] = 0;
            machine->columns->conversion_flags[2 * r + 1] = 0;
            machine->columns->member_offsets[r] = 0;
        }
        else
        {
            const uint32_t a = node_machine_operand(machine, r, 0);
            const uint32_t b = node_machine_operand(machine, r, 1);
            const uint32_t c = node_machine_operand(machine, r, 2);
            machine->columns->left[r] = a;
            machine->columns->right[r] = b;
            // Its subtree's least record (a call's arguments' too, node_machine_arguments).
            uint32_t lowest = a < r ? machine->columns->lowest[a] : r;
            lowest = b < r && machine->columns->lowest[b] < lowest ? machine->columns->lowest[b] : lowest;
            machine->columns->lowest[r] = c < r && machine->columns->lowest[c] < lowest ? machine->columns->lowest[c] : lowest;
            const bool call = op == CHECK_OP_CALL;
            if (!node_machine_code(op))
            {
                refusal = NODE_MACHINE_REFUSED_CODE;
            }
            else if (a == BUSTER_NODE_MACHINE_MOST || b == BUSTER_NODE_MACHINE_MOST || c == BUSTER_NODE_MACHINE_MOST ||
                     (call ? a == UINT32_MAX || !node_machine_arguments(machine, r, node) : graph->argument_counts[node] != 0))
            {
                refusal = NODE_MACHINE_REFUSED_SHAPE;
            }
            else if (context)
            {
                refusal = NODE_MACHINE_REFUSED_CONTEXT;
            }
            else if (call && node_machine_callee_refused(machine, r, node, a))
            {
                refusal = NODE_MACHINE_REFUSED_CALLEE;
            }
            else if (!machine->scalar && (a != UINT32_MAX || op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF) && (c == UINT32_MAX || op == CHECK_OP_CONDITIONAL) &&
                     ((node_machine_row_operators[op] != NODE_MACHINE_ROW_NONE && node_machine_row(machine, r, op, a, b)) || node_machine_rule(machine, r, node, op, a, b)))
            {
                machine->row_nodes += 1;
                machine->twin_differences += machine->verify && !node_machine_same_as_action(machine, r, op);
            }
            else
            {
                CheckActionResult result;
                memset(&result, 0, sizeof(result));
                node_machine_action(machine, r, op, machine->argument_conversions + (call ? machine->columns->argument_first[r] : 0), &result);
                refusal = result.diagnostic != CHECK_DIAG_NONE ? NODE_MACHINE_REFUSED_DIAGNOSTIC : NODE_MACHINE_TAKEN;
                node_machine_store(machine, r, op, &result);
                machine->action_nodes += 1;
                machine->action_ops[op] += 1;
            }
            word = node_machine_word(machine, machine->columns->value_types[r]);
        }
        const uint8_t facts = (uint8_t)(word >> 8);
        machine->columns->words[r] = word;
        machine->columns->facts[r] = facts;
        machine->columns->value_classes[r] = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : (uint8_t)word;
        machine->stopped = r;
        r += refusal == NODE_MACHINE_TAKEN;
    }
    return refusal;
}

#endif
