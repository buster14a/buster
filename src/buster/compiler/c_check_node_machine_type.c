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
// each operand. Its type table, of the builtin types alone, lives on its stack: the bake checks that the initializers
// write nothing outside buster_baked.
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
        case CHECK_OP_SELECT: case CHECK_OP_CALL:
            code = true;
            break;
        default:
            break;
    }
    return code;
}

// A value of `type`, `category`, `flags`, `constant_kind`, an integer's low 64 bits and `object`, its other fields zero,
// written as check_value_copy writes one: its first 64 bytes by one store and its last 32 by another, so that a parent's
// copy of it, which loads it by the same halves, takes each from one store (#1050). Field by field, a record's value cost
// T a failed store forwarding at each parent's read of it on the 9700X. The scalar path is the assignment.
static inline __attribute__((always_inline)) void node_machine_put_value(CheckValue* destination, TypeId type, CheckCategory category, uint32_t flags,
                                                                         CheckConstantKind constant_kind, uint64_t integer, uint32_t object)
{
#if BUSTER_HOST_VECTOR
    const __m512i first = _mm512_setr_epi32((int)type, (int)category, (int)flags, (int)constant_kind, (int)(uint32_t)integer, (int)(uint32_t)(integer >> 32), 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0);
    const __m256i last = _mm256_setr_epi32((int)object, 0, 0, 0, 0, 0, 0, 0);
    _mm512_storeu_si512((uint8_t*)destination, first);
    _mm256_storeu_si256((__m256i*)((uint8_t*)destination + 64), last);
#else
    *destination = (CheckValue){
        .type = type,
        .category = category,
        .flags = flags,
        .constant_kind = constant_kind,
        .integer = integer,
        .object = object,
    };
#endif
}

// A leaf's value, check_graph_leaf_at's, into `destination` by node_machine_put_value where its record holds it whole.
static inline __attribute__((always_inline)) void node_machine_leaf(const CheckGraph* graph, uint32_t node, CheckValue* destination)
{
    const uint32_t slot = graph->leaf_slots[node];
    const CheckGraphLeaf* leaf = graph->dense_leaves == NULL ? graph->leaf_records + slot : NULL;
    if (leaf != NULL && leaf->row == 0 && leaf->overload_head == 0)
    {
        node_machine_put_value(destination, leaf->type, (CheckCategory)leaf->category, leaf->flags, (CheckConstantKind)leaf->constant_kind, leaf->integer, leaf->object);
    }
    else
    {
        const CheckValue value = check_graph_leaf_at(graph, slot);
        check_value_copy(destination, &value);
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

// Whether operand value `value` takes row `row`'s answer as its operand `k`, the other operand being `other` (null for
// none): an rvalue or (where the row says) an lvalue of a non-volatile object of the row's builtin type, without an
// overload set, a fault or a bit-field's width, and with no flag outside the operator's allowed ones; an lvalue's constant
// is its address, which its conversion drops; an rvalue's none, or (where the row says) a literal's where `other` has
// none.
static bool node_machine_row_takes(const NodeMachineRow* row, uint32_t allowed, const CheckValue* value, const CheckValue* other, uint32_t k)
{
    const bool lvalue = value->category == CHECK_LVALUE;
    const bool constant = value->constant_kind == CHECK_CONSTANT_INTEGER || value->constant_kind == CHECK_CONSTANT_FLOATING;
    const bool other_constant = other != NULL && other->constant_kind != CHECK_CONSTANT_NONE && other->category == CHECK_RVALUE;
    const uint32_t flags = value->flags & ~(constant && !lvalue ? (uint32_t)BUSTER_NODE_MACHINE_ROW_CONSTANT_FLAGS : 0u);
    const bool standing = lvalue ? (row->standing & BUSTER_NODE_MACHINE_ROW_LVALUE(k)) != 0 &&
                                   (value->constant_kind == CHECK_CONSTANT_NONE || value->constant_kind == CHECK_CONSTANT_ADDRESS) :
                          value->category == CHECK_RVALUE && (value->constant_kind == CHECK_CONSTANT_NONE ||
                                                              (constant && !other_constant && (row->standing & BUSTER_NODE_MACHINE_ROW_CONSTANT(k)) != 0));
    return standing && (flags & ~allowed) == 0 && value->overload_head == 0 && value->fault == CHECK_FAULT_NONE && value->bit_width == 0;
}

// T's row for record `r`, `op` of its operands' records `a` and `b` (UINT32_MAX for a unary operator's), into its
// columns: whether a row typed it.
static bool node_machine_row(NodeMachine* machine, uint32_t r, CheckOp op, uint32_t a, uint32_t b)
{
    const uint8_t o = node_machine_row_operators[op];
    const CheckValue* left = machine->values + a;
    const CheckValue* right = b != UINT32_MAX ? machine->values + b : NULL;
    // An lvalue's const qualifier goes with its conversion; any other qualifier leaves the node to the action.
    const TypeId type_a = left->type & ~(left->category == CHECK_LVALUE ? (TypeId)BUSTER_TYPE_CONST : 0);
    const TypeId type_b = right != NULL ? right->type & ~(right->category == CHECK_LVALUE ? (TypeId)BUSTER_TYPE_CONST : 0) : 0;
    const uint32_t code_a = type_index(type_a);
    const uint32_t code_b = type_index(type_b);
    const bool builtin = o != NODE_MACHINE_ROW_NONE && (b == UINT32_MAX || b < BUSTER_NODE_MACHINE_MOST) && (type_a & BUSTER_TYPE_QUALIFIERS) == 0 &&
                         code_a < BUSTER_NODE_MACHINE_KINDS && (type_b & BUSTER_TYPE_QUALIFIERS) == 0 && code_b < BUSTER_NODE_MACHINE_KINDS;
    const NodeMachineRow* row = builtin ? &node_machine_rows[o][code_a][code_b] : NULL;
    const uint32_t allowed = builtin ? node_machine_row_allowed[o] : 0;
    const bool typed = row != NULL && row->result != 0 && node_machine_row_takes(row, allowed, left, right, 0) &&
                       (right == NULL || node_machine_row_takes(row, allowed, right, left, 1));
    if (typed)
    {
        const uint32_t flags = check_value_source_flags(op, row->flags | ((left->flags | (right != NULL ? right->flags : 0)) & node_machine_row_passed[o]));
        node_machine_put_value(machine->values + r, BUSTER_TYPE_BUILTIN(row->result), CHECK_RVALUE, flags, CHECK_CONSTANT_NONE, 0, 0);
        machine->converted[2 * r] = BUSTER_TYPE_BUILTIN(row->converted[0]);
        machine->converted[2 * r + 1] = right != NULL ? BUSTER_TYPE_BUILTIN(row->converted[1]) : 0;
        machine->conversion_flags[2 * r] = row->conversion_flags[0] | (left->category == CHECK_LVALUE ? BUSTER_CHECK_CONVERT_LVALUE : 0);
        machine->conversion_flags[2 * r + 1] = right != NULL ? row->conversion_flags[1] | (right->category == CHECK_LVALUE ? BUSTER_CHECK_CONVERT_LVALUE : 0) : 0;
        machine->member_offsets[r] = 0;
    }
    return typed;
}

// T's discard: a cast to void of an rvalue that check_graph_discard_taken takes (no overload set, no array, function,
// vector or complex value; the machine refuses constant and floating-point contexts before), by check_graph_discard's
// steps, which the graph run takes for it instead of the action: whether it typed record `r`, cast `node` of record `a`.
static bool node_machine_discard(NodeMachine* machine, uint32_t r, uint32_t node, uint32_t a)
{
    const CheckGraph* graph = machine->graph;
    const CheckValue* operand = machine->values + a;
    bool taken = graph->types != NULL && graph->types[node] == BUSTER_TYPE_VOID && graph->overload_candidate_count == 0 && operand->category == CHECK_RVALUE &&
                 operand->overload_head == 0;
    if (taken)
    {
        const uint8_t kind = (uint8_t)(machine->words[a] >> 16);
        taken = !type_kind_is_array(kind) && kind != BUSTER_TYPE_KIND_FUNCTION && kind != BUSTER_TYPE_KIND_VECTOR && kind != BUSTER_TYPE_KIND_EXT_VECTOR &&
                kind != BUSTER_TYPE_KIND_COMPLEX;
    }
    if (taken)
    {
        CheckValue* value = machine->values + r;
        CheckConversion conversion = { .type = operand->type };
        check_value_copy(value, operand);
        check_action_conversion(machine->types, value, BUSTER_TYPE_VOID, BUSTER_CHECK_CONVERT_CAST, &conversion, node);
        check_action_cast_provenance(value, false, false, false, true, true);
        value->flags = check_value_source_flags(CHECK_OP_CAST, value->flags);
        machine->converted[2 * r] = conversion.type;
        machine->converted[2 * r + 1] = 0;
        machine->conversion_flags[2 * r] = conversion.flags;
        machine->conversion_flags[2 * r + 1] = 0;
        machine->member_offsets[r] = 0;
    }
    return taken;
}

// The kinds whose lvalue a direct rule converts: a scalar's that names no tag (whose completeness at a position the action
// would ask) and is neither atomic nor a vector.
static bool node_machine_plain_kind(uint8_t kind)
{
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_POINTER;
}

// The pointer type of function type `function`, type_pointer's: the unit's column's (NodeMachine.function_pointers), one
// load once the type's first designator has filled it. A type past the column, or a qualified one (which type_pointer
// takes unqualified), asks type_pointer each time.
static TypeId node_machine_function_pointer(NodeMachine* machine, TypeId function)
{
    const uint32_t index = type_index(function);
    const bool column = index < machine->type_count && function == (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
    TypeId pointer = column ? machine->function_pointers[index] : 0;
    if (pointer == 0)
    {
        pointer = type_pointer(machine->types, function);
        if (column)
        {
            machine->function_pointers[index] = pointer;
        }
    }
    return pointer;
}

// Record `a`'s value as an operator's operand reads it (check_action_operand_value's lvalue, array and function
// conversions), into `value` and `conversion`: whether the common cases took it (an rvalue of no array or function type;
// an lvalue of a non-volatile object of node_machine_plain_kind; an array or a function decayed to a pointer that
// exists), the action doing the rest.
static bool node_machine_operand_value(NodeMachine* machine, uint32_t a, CheckValue* value, CheckConversion* conversion)
{
    TypeTable* types = machine->types;
    const CheckValue* operand = machine->values + a;
    const uint32_t word = machine->words[a];
    const bool wrapped = (word & BUSTER_NODE_MACHINE_WORD_PLAIN) == 0;
    const TypeId plain = wrapped ? type_strip_aligned(types, operand->type) : operand->type;
    const uint8_t kind = wrapped ? type_kind(types, plain) : (uint8_t)(word >> 16);
    bool taken = true;
    check_value_copy(value, operand);
    *conversion = (CheckConversion){ .type = operand->type };
    if (type_kind_is_array(kind))
    {
        const TypeId pointer = (operand->flags & BUSTER_CHECK_REGISTER_NAME) == 0 ? type_pointer(types, type_element(types, plain)) : 0;
        taken = pointer != 0;
        value->type = pointer;
        value->category = CHECK_RVALUE;
        value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME);
        value->constant_kind = (value->flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && value->constant_kind == CHECK_CONSTANT_ADDRESS ? CHECK_CONSTANT_NONE : value->constant_kind;
        *conversion = (CheckConversion){ .type = pointer, .flags = BUSTER_CHECK_CONVERT_ARRAY };
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        const TypeId pointer = node_machine_function_pointer(machine, plain);
        taken = pointer != 0;
        value->type = pointer;
        value->category = CHECK_RVALUE;
        value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL);
        *conversion = (CheckConversion){ .type = pointer, .flags = BUSTER_CHECK_CONVERT_FUNCTION };
    }
    else if (operand->category == CHECK_LVALUE)
    {
        taken = node_machine_plain_kind(kind) && (type_qualifiers(operand->type) & BUSTER_TYPE_VOLATILE) == 0 && (operand->flags & BUSTER_CHECK_VOLATILE_PART) == 0;
        value->constant_kind = value->constant_kind == CHECK_CONSTANT_ADDRESS || (value->flags & BUSTER_CHECK_ICE) == 0 ? CHECK_CONSTANT_NONE : value->constant_kind;
        value->type = type_conversion_operand(types, value->type);
        value->category = CHECK_RVALUE;
        value->flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART);
        *conversion = (CheckConversion){ .type = value->type, .flags = BUSTER_CHECK_CONVERT_LVALUE };
    }
    return taken;
}

// Record `r`'s columns from its value and its operands' conversions.
static void node_machine_set(NodeMachine* machine, uint32_t r, CheckOp op, const CheckConversion* first, const CheckConversion* second, uint64_t member_offset)
{
    CheckValue* value = machine->values + r;
    value->flags = check_value_source_flags(op, value->flags);
    machine->converted[2 * r] = first->type;
    machine->converted[2 * r + 1] = second->type;
    machine->conversion_flags[2 * r] = first->flags;
    machine->conversion_flags[2 * r + 1] = second->flags;
    machine->member_offsets[r] = member_offset;
}

// T's member access, `.` or `->` at record `r` (node `node`) of record `a`, by check_action_member's steps where the record is
// a complete struct or union at the node's position without an address space and the member is no bit-field, and its base
// is no constant: whether it typed the node.
static bool node_machine_member(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a)
{
    TypeTable* types = machine->types;
    const CheckGraph* graph = machine->graph;
    const bool arrow = op == CHECK_OP_ARROW;
    CheckValue base;
    CheckConversion conversion = { .type = machine->values[a].type };
    bool taken = arrow ? node_machine_operand_value(machine, a, &base, &conversion) : (check_value_copy(&base, machine->values + a), true);
    TypeId record = type_strip_aligned(types, base.type);
    uint32_t space = arrow ? 0 : type_address_space(type_extended_qualifiers(types, base.type));
    if (taken && arrow)
    {
        taken = type_kind(types, record) == BUSTER_TYPE_KIND_POINTER;
        space = taken ? type_address_space(type_extended_qualifiers(types, type_element(types, record))) : 0;
        record = taken ? type_strip_aligned(types, type_element(types, record)) : record;
    }
    const uint8_t kind = type_kind(types, record);
    taken = taken && space == 0 && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && base.constant_kind == CHECK_CONSTANT_NONE;
    taken = taken && node_machine_complete(machine, record, node);
    uint64_t offset = 0;
    uint32_t qualifiers = 0;
    const uint32_t member = taken ? type_lookup_member(types, record, graph->members[node], &offset, &qualifiers) : 0;
    taken = taken && member != 0 && member != BUSTER_TYPE_MEMBER_UNKNOWN && (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0;
    // The base's fault, which check_action_inherit would carry, leaves the node to the action.
    taken = taken && base.fault == CHECK_FAULT_NONE;
    if (taken)
    {
        const TypeId type = type_qualify(types, types->members.type[member], qualifiers);
        CheckValue inherited = { 0 };
        check_action_inherit(&inherited, &base);
        uint32_t flags = inherited.flags | (base.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS));
        flags |= type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
        flags |= arrow && (base.flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
        flags |= !arrow ? base.flags & BUSTER_CHECK_REGISTER : 0;
        const CheckCategory category = arrow || base.category == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
        node_machine_put_value(machine->values + r, type, category, flags, CHECK_CONSTANT_NONE, 0, 0);
        const CheckConversion none = { 0 };
        node_machine_set(machine, r, op, &conversion, &none, offset);
    }
    return taken;
}

// T's truth test, `!` or a condition at record `r` of record `a`, by check_action_logic's steps for an operand the common
// conversions take and the action takes as a scalar: whether it typed the node.
static bool node_machine_truth(NodeMachine* machine, uint32_t r, CheckOp op, uint32_t a)
{
    CheckValue operand;
    CheckConversion conversion;
    // The operand's fault, which check_action_inherit would carry, leaves the node to the action.
    const bool taken = node_machine_operand_value(machine, a, &operand, &conversion) && check_action_is_scalar(machine->types, operand.type) &&
                       operand.fault == CHECK_FAULT_NONE;
    if (taken)
    {
        bool truth = false;
        const bool known = check_action_truth(machine->types, &operand, &truth);
        CheckValue inherited = { 0 };
        check_action_inherit(&inherited, &operand);
        const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
        const uint32_t flags = inherited.flags | ((operand.flags & BUSTER_CHECK_ICE) != 0 ? BUSTER_CHECK_ICE : (operand.flags & forms) != 0 ? BUSTER_CHECK_GNU_ICE_FORM : 0);
        const uint64_t integer = known ? (uint64_t)(op == CHECK_OP_NOT ? !truth : truth) : 0;
        node_machine_put_value(machine->values + r, BUSTER_TYPE_INT, CHECK_RVALUE, flags, known ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE, integer, 0);
        const CheckConversion none = { 0 };
        node_machine_set(machine, r, op, &conversion, &none, 0);
    }
    return taken;
}

// T's logical operator, `&&` or `||` at record `r` of records `a` and `b`, by check_action_logic's steps for operands the
// common conversions take, both scalars without a fault (the machine refuses a node of a floating-point environment
// before, whose promotion the action would make): whether it typed the node. The action is its twin, as for the rules.
static bool node_machine_logic(NodeMachine* machine, uint32_t r, CheckOp op, uint32_t a, uint32_t b)
{
    TypeTable* types = machine->types;
    CheckValue left;
    CheckValue right;
    CheckConversion conversions[2];
    const bool taken = b < r && node_machine_operand_value(machine, a, &left, conversions) && node_machine_operand_value(machine, b, &right, conversions + 1) &&
                       check_action_is_scalar(types, left.type) && check_action_is_scalar(types, right.type) && left.fault == CHECK_FAULT_NONE &&
                       right.fault == CHECK_FAULT_NONE;
    if (taken)
    {
        const bool conjunction = op == CHECK_OP_LOGICAL_AND;
        bool left_truth = false;
        bool right_truth = false;
        const bool left_known = check_action_truth(types, &left, &left_truth);
        const bool right_known = check_action_truth(types, &right, &right_truth);
        // A condition that decides the value leaves the second operand unevaluated: its effects aren't the value's.
        const bool skips_right = left_known && (conjunction ? !left_truth : left_truth);
        CheckValue inherited = { 0 };
        check_action_inherit(&inherited, &left);
        if (!skips_right)
        {
            check_action_inherit(&inherited, &right);
        }
        const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
        const bool right_ice = (right.flags & BUSTER_CHECK_ICE) != 0 || (skips_right && (right.flags & (BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM)) != 0);
        const bool ice = (left.flags & BUSTER_CHECK_ICE) != 0 && right_ice;
        const bool right_form = (right.flags & forms) != 0 || (skips_right && (right.flags & BUSTER_CHECK_ICE_UNEVALUATED) != 0);
        const uint32_t flags = inherited.flags | (ice ? BUSTER_CHECK_ICE : (left.flags & forms) != 0 && right_form ? BUSTER_CHECK_GNU_ICE_FORM : 0);
        const bool constant = left_known && (skips_right || right_known);
        const bool value = conjunction ? left_truth && right_truth : left_truth || right_truth;
        node_machine_put_value(machine->values + r, BUSTER_TYPE_INT, CHECK_RVALUE, flags, constant ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE, constant && value, 0);
        node_machine_set(machine, r, op, conversions, conversions + 1, 0);
    }
    return taken;
}

// Whether the conversion of `source` to `target` is one a direct assignment takes as the action would: a builtin
// arithmetic type to another, or a pointer to the same pointer type.
static bool node_machine_assignable(const NodeMachine* machine, TypeId target, const CheckValue* source)
{
    const TypeTable* types = machine->types;
    const bool arithmetic = type_index(target) < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(type_index(target)) && (target & BUSTER_TYPE_QUALIFIERS) == 0 &&
                            type_index(source->type) < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(type_index(source->type)) && (source->type & BUSTER_TYPE_QUALIFIERS) == 0;
    const bool pointer = target == source->type && type_kind(types, target) == BUSTER_TYPE_KIND_POINTER;
    return arithmetic || pointer;
}

// T's store, `=`, an initialization or a conversion at record `r` (node `node`), by check_action_assignment's steps where
// the destination (`=`'s operand 0) is a modifiable lvalue of node_machine_plain_kind and node_machine_assignable takes the
// conversion: whether it typed the node.
static bool node_machine_store_rule(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a, uint32_t b)
{
    TypeTable* types = machine->types;
    const bool store = op == CHECK_OP_ASSIGN;
    const uint32_t source_record = store ? b : a;
    const CheckValue* destination = store ? machine->values + a : NULL;
    CheckValue source;
    CheckConversion conversion;
    bool taken = source_record < r && node_machine_operand_value(machine, source_record, &source, &conversion);
    TypeId target = 0;
    if (taken && store)
    {
        const uint8_t kind = (uint8_t)(machine->words[a] >> 16);
        taken = destination->category == CHECK_LVALUE && (destination->flags & (BUSTER_CHECK_VECTOR_DUPLICATE | BUSTER_CHECK_BIT_FIELD)) == 0 &&
                (type_qualifiers(destination->type) & BUSTER_TYPE_CONST) == 0 && node_machine_plain_kind(kind);
        target = destination->type;
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
        taken = taken && node_machine_assignable(machine, target, &source);
    }
    if (taken)
    {
        const CheckGraph* graph = machine->graph;
        const uint32_t extensions = check_graph_context(graph, node, false).extensions;
        const uint32_t flags = BUSTER_CHECK_CONVERT_ASSIGNMENT |
                               (check_action_kind(types, target) == BUSTER_TYPE_KIND_POINTER && check_action_null(types, &source, extensions) ? BUSTER_CHECK_CONVERT_NULL : 0);
        check_action_conversion(types, &source, target, flags, &conversion, node);
        CheckValue* value = machine->values + r;
        check_value_copy(value, &source);
        value->category = CHECK_RVALUE;
        if (store)
        {
            value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
            value->flags |= BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION;
            check_action_inherit(value, destination);
            const CheckConversion first = { .type = destination->type };
            node_machine_set(machine, r, op, &first, &conversion, 0);
        }
        else
        {
            const CheckConversion none = { 0 };
            node_machine_set(machine, r, op, &conversion, &none, 0);
        }
    }
    return taken;
}

// T's address, `&x` at record `r` of record `a`, as the action takes it: whether it typed the node (an lvalue or a function
// designator without a vector element's, a bit-field's or a register's flag, whose pointer type exists).
static bool node_machine_address_rule(NodeMachine* machine, uint32_t r, uint32_t a)
{
    const CheckValue* operand = machine->values + a;
    const TypeId pointer = (operand->flags & (BUSTER_CHECK_VECTOR_ELEMENT | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER)) == 0 &&
                           (operand->category == CHECK_LVALUE || operand->category == CHECK_FUNCTION) ? type_pointer(machine->types, operand->type) : 0;
    if (pointer != 0)
    {
        CheckValue* value = machine->values + r;
        check_value_copy(value, operand);
        value->type = pointer;
        value->category = CHECK_RVALUE;
        check_action_address_provenance(value, false);
        const CheckConversion first = { .type = operand->type };
        const CheckConversion none = { 0 };
        node_machine_set(machine, r, CHECK_OP_ADDRESS, &first, &none, 0);
    }
    return pointer != 0;
}

// T's dereference, `*p` at record `r` of record `a`, as the action takes it: whether it typed the node (a pointer the
// common conversions take).
static bool node_machine_dereference(NodeMachine* machine, uint32_t r, uint32_t a)
{
    TypeTable* types = machine->types;
    CheckValue pointer;
    CheckConversion conversion;
    const bool taken = node_machine_operand_value(machine, a, &pointer, &conversion) && check_action_kind(types, pointer.type) == BUSTER_TYPE_KIND_POINTER;
    if (taken)
    {
        CheckValue* value = machine->values + r;
        check_value_copy(value, &pointer);
        value->type = type_element(types, type_strip_aligned(types, pointer.type));
        value->category = check_action_kind(types, value->type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
        check_action_dereference_provenance(value);
        const CheckConversion none = { 0 };
        node_machine_set(machine, r, CHECK_OP_DEREFERENCE, &conversion, &none, 0);
    }
    return taken;
}

// T's pointer arithmetic, a subscript or a pointer plus or minus an integer at record `r` (node `node`) of records `a` and
// `b`, by check_action_pointer_arithmetic's steps where the common conversions take both operands, one is a pointer to a
// complete object type and the other an integer, and the pointer is no constant: whether it typed the node.
static bool node_machine_pointer_rule(NodeMachine* machine, uint32_t r, uint32_t node, CheckOp op, uint32_t a, uint32_t b)
{
    TypeTable* types = machine->types;
    CheckValue left;
    CheckValue right;
    CheckConversion conversions[2];
    bool taken = b < r && node_machine_operand_value(machine, a, &left, conversions) && node_machine_operand_value(machine, b, &right, conversions + 1);
    const bool lp = taken && check_action_kind(types, left.type) == BUSTER_TYPE_KIND_POINTER;
    const bool rp = taken && check_action_kind(types, right.type) == BUSTER_TYPE_KIND_POINTER;
    const bool reverse = !lp && rp && op != CHECK_OP_SUBTRACT;
    const CheckValue* pointer = reverse ? &right : &left;
    CheckValue index = reverse ? left : right;
    const uint32_t slot = reverse ? 0 : 1;
    taken = taken && lp != rp && (lp || reverse) && check_action_is_integer(types, index.type) && pointer->constant_kind == CHECK_CONSTANT_NONE &&
            pointer->fault == CHECK_FAULT_NONE;
    if (taken)
    {
        const TypeId element = type_element(types, type_strip_aligned(types, pointer->type));
        const uint8_t kind = check_action_kind(types, element);
        taken = kind != BUSTER_TYPE_KIND_VOID && kind != BUSTER_TYPE_KIND_FUNCTION && node_machine_complete(machine, element, node);
    }
    if (taken)
    {
        const CheckValue* raw = machine->values + (reverse ? b : a);
        CheckValue* value = machine->values + r;
        memset(value, 0, sizeof(*value));
        check_action_inherit(value, &left);
        check_action_inherit(value, &right);
        check_action_promote(types, &index, conversions + slot, node);
        value->type = op == CHECK_OP_SUBSCRIPT ? type_element(types, pointer->type) : pointer->type;
        value->category = op == CHECK_OP_SUBSCRIPT ? CHECK_LVALUE : CHECK_RVALUE;
        value->flags |= pointer->flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS);
        value->flags |= op == CHECK_OP_SUBSCRIPT && (pointer->flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
        value->flags |= op == CHECK_OP_SUBSCRIPT && (raw->flags & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) == BUSTER_CHECK_REGISTER &&
                        type_kind_is_array(check_action_kind(types, raw->type)) ? BUSTER_CHECK_REGISTER : 0;
        node_machine_set(machine, r, op, conversions, conversions + 1, 0);
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
    uint32_t lowest = machine->lowest[r];
    bool inside = count <= BUSTER_NODE_MACHINE_MOST - first;
    for (uint32_t i = 0; inside && i < count; i += 1)
    {
        const uint32_t argument = graph->argument_nodes[offset + i];
        const uint32_t record = argument - machine->first;
        inside = argument != BUSTER_CHECK_NONE && argument >= machine->first && record < r;
        machine->arguments[first + i] = record;
        lowest = inside && machine->lowest[record] < lowest ? machine->lowest[record] : lowest;
    }
    machine->lowest[r] = lowest;
    machine->argument_first[r] = first;
    machine->argument_counts[r] = inside ? count : 0;
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
                   machine->argument_counts[r] > BUSTER_TIER0_MAX_CALL_ARGUMENTS;
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
// is complete at the node and the object isn't volatile. Whether it took it.
static bool node_machine_enumeration_value(NodeMachine* machine, uint32_t a, uint32_t node, CheckValue* value, CheckConversion* conversion)
{
    const CheckValue* operand = machine->values + a;
    const bool taken = operand->category == CHECK_LVALUE && (uint8_t)(machine->words[a] >> 16) == BUSTER_TYPE_KIND_ENUM &&
                       (type_qualifiers(operand->type) & BUSTER_TYPE_VOLATILE) == 0 && (operand->flags & BUSTER_CHECK_VOLATILE_PART) == 0 &&
                       node_machine_complete(machine, operand->type, node);
    if (taken)
    {
        check_value_copy(value, operand);
        value->constant_kind = value->constant_kind == CHECK_CONSTANT_ADDRESS || (value->flags & BUSTER_CHECK_ICE) == 0 ? CHECK_CONSTANT_NONE : value->constant_kind;
        value->type = type_conversion_operand(machine->types, value->type);
        value->category = CHECK_RVALUE;
        value->flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART);
        *conversion = (CheckConversion){ .type = value->type, .flags = BUSTER_CHECK_CONVERT_LVALUE };
    }
    return taken;
}

// A call's argument, record `a` of call node `node`, as check_action_call converts it in the common cases, into `value`
// (its value as an operand reads it, converted) and `conversion`: for a parameter of type `parameter`, as an assignment
// converts it (node_machine_arithmetic_assignable's, or a null pointer constant's or a pointer node_machine_pointer_assignable
// takes to a pointer); a variadic one (`parameter` 0) by the default argument promotions, of an arithmetic type or to the
// same pointer type. Whether they took it: the action takes the rest, as an atomic value's.
static bool node_machine_argument(NodeMachine* machine, uint32_t a, uint32_t node, TypeId parameter, CheckValue* value, CheckConversion* conversion)
{
    TypeTable* types = machine->types;
    bool taken = (node_machine_operand_value(machine, a, value, conversion) || node_machine_enumeration_value(machine, a, node, value, conversion)) &&
                 check_action_kind(types, value->type) != BUSTER_TYPE_KIND_ATOMIC;
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
// the record's value has no constant but an address, none of BUSTER_NODE_MACHINE_MEMO_FLAGS and isn't an enumeration's
// lvalue (whose completeness at the node the rule asks), the rule reads of it only its type, whether it is an lvalue and
// the type table: check_action_null is its type's alone, and no conversion folds. Its answer is then the key's, and a hit
// answers where the conversion changes nothing that check_action_inherit reads (where the record's kind and the
// conversion's are node_machine_memo_kind's, an array's or a function's, which decay to a pointer), inheriting the
// record's value itself. An integer constant (an rvalue of an integer type) is a key of its own, whose hit answers where
// node_machine_memo_integers says so. A miss runs the rule and keeps its answer; any other argument takes the rule.
static bool node_machine_argument_memo(NodeMachine* machine, uint32_t a, uint32_t node, TypeId parameter, CheckValue* value, CheckConversion* conversion)
{
    const CheckValue* operand = machine->values + a;
    const uint8_t kind = (uint8_t)(machine->words[a] >> 16);
    const bool lvalue = operand->category == CHECK_LVALUE;
    const bool integer = operand->constant_kind == CHECK_CONSTANT_INTEGER;
    const bool constant = operand->constant_kind == CHECK_CONSTANT_NONE || operand->constant_kind == CHECK_CONSTANT_ADDRESS || (integer && !lvalue && kind == BUSTER_TYPE_KIND_INTEGER);
    const bool eligible = constant && (operand->flags & BUSTER_NODE_MACHINE_MEMO_FLAGS) == 0 && !(lvalue && kind == BUSTER_TYPE_KIND_ENUM);
    const uint8_t standing = BUSTER_NODE_MACHINE_MEMO_FILLED | (lvalue ? BUSTER_NODE_MACHINE_MEMO_LVALUE : 0) | (integer ? BUSTER_NODE_MACHINE_MEMO_INTEGER : 0);
    const uint32_t slot = ((operand->type ^ parameter * 0x85ebca77u ^ standing) * 0x9e3779b1u) >> (32 - BUSTER_NODE_MACHINE_MEMO_BITS);
    NodeMachineMemo* entry = machine->argument_memo + slot;
    const bool matched = eligible && entry->source == operand->type && entry->parameter == parameter && entry->standing == standing;
    bool taken = false;
    if (matched && (entry->answer & BUSTER_NODE_MACHINE_MEMO_ANSWERS) != 0)
    {
        taken = (entry->answer & BUSTER_NODE_MACHINE_MEMO_TAKEN) != 0;
        *conversion = (CheckConversion){ .type = entry->converted, .flags = entry->flags };
        if (taken)
        {
            check_action_inherit(value, operand);
        }
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
                .source = operand->type,
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
    CheckValue callee;
    CheckConversion callee_conversion;
    bool taken = function != 0 && node_machine_operand_value(machine, a, &callee, &callee_conversion) && callee.fault == CHECK_FAULT_NONE;
    uint32_t parameters = 0;
    const TypeId* parameter_types = taken ? type_parameters(types, function, &parameters) : NULL;
    const uint8_t flags = taken ? types->types.flags[type_index(function)] : 0;
    const uint32_t count = machine->argument_counts[r];
    const TypeId result = taken ? type_return(types, function) : 0;
    taken = taken && (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 && count >= parameters && ((flags & BUSTER_TYPE_FLAG_VARIADIC) != 0 || count == parameters) &&
            (check_action_kind(types, result) == BUSTER_TYPE_KIND_VOID || node_machine_complete(machine, result, node));
    CheckValue* value = machine->values + r;
    if (taken)
    {
        // check_action_call's result: its return type's rvalue with an effect, and its callee's and its arguments' flags.
        node_machine_put_value(value, result, CHECK_RVALUE, BUSTER_CHECK_SIDE_EFFECT, CHECK_CONSTANT_NONE, 0, 0);
        check_action_inherit(value, &callee);
    }
    const uint32_t first = machine->argument_first[r];
    for (uint32_t i = 0; taken && i < count; i += 1)
    {
        const TypeId parameter = i < parameters ? parameter_types[i] : 0;
        taken = node_machine_argument_memo(machine, machine->arguments[first + i], node, parameter, value, machine->argument_conversions + first + i);
    }
    if (taken)
    {
        const CheckConversion none = { 0 };
        node_machine_set(machine, r, CHECK_OP_CALL, &callee_conversion, &none, 0);
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
        typed = node_machine_discard(machine, r, node, a);
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
        typed = node_machine_pointer_rule(machine, r, node, op, a, b);
    }
    else if (op == CHECK_OP_CALL)
    {
        typed = node_machine_call_rule(machine, r, node, a);
    }
    return typed;
}

// T's exact path for record `r` (node `node`, operator `op`): the action itself, as the graph run calls it, on the
// operands' records, into `result`, and a call's on its arguments' records, their conversions into
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
            check_value_copy(&action.operands[k], machine->values + operand);
        }
    }
    if (op == CHECK_OP_CALL)
    {
        const uint32_t first = machine->argument_first[r];
        action.argument_count = machine->argument_counts[r];
        for (uint32_t i = 0; i < action.argument_count; i += 1)
        {
            check_value_copy(machine->argument_values + i, machine->values + machine->arguments[first + i]);
        }
        action.arguments = machine->argument_values;
        action.argument_conversions = argument_conversions;
    }
    check_action_compute(machine->types, &action, result, 0, NULL);
}

// The action's answer stored into record `r`'s columns, as check_graph_store stores it.
static void node_machine_store(NodeMachine* machine, uint32_t r, CheckOp op, const CheckActionResult* result)
{
    CheckValue* value = machine->values + r;
    check_value_copy(value, &result->value);
    value->flags = check_value_source_flags(op, value->flags);
    machine->converted[2 * r] = result->conversions[0].type;
    machine->converted[2 * r + 1] = result->conversions[1].type;
    machine->conversion_flags[2 * r] = result->conversions[0].flags;
    machine->conversion_flags[2 * r + 1] = result->conversions[1].flags;
    machine->member_offsets[r] = result->member_offset;
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
    const CheckValue* value = machine->values + r;
    bool same = result.diagnostic == CHECK_DIAG_NONE && node_machine_same_values(&result.value, value) &&
                result.conversions[0].type == machine->converted[2 * r] && result.conversions[1].type == machine->converted[2 * r + 1] &&
                result.conversions[0].flags == machine->conversion_flags[2 * r] && result.conversions[1].flags == machine->conversion_flags[2 * r + 1] &&
                result.member_offset == machine->member_offsets[r];
    // A call's arguments' conversions too.
    const uint32_t first = machine->argument_first[r];
    for (uint32_t i = 0; op == CHECK_OP_CALL && i < machine->argument_counts[r]; i += 1)
    {
        same = same && machine->verified[i].type == machine->argument_conversions[first + i].type && machine->verified[i].flags == machine->argument_conversions[first + i].flags;
    }
    return same;
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
    machine->argument_total = 0;
    uint32_t r = 0;
    while (refusal == NODE_MACHINE_TAKEN && r < machine->count)
    {
        const uint32_t node = node_machine_node(machine, r);
        const CheckOp op = graph->ops[node];
        const bool context = (graph->contexts != NULL && graph->contexts[node] != 0) || (graph->fp_environments != NULL && graph->fp_environments[node] != 0);
        uint32_t word = 0;
        machine->codes[r] = (uint8_t)op;
        if (op == CHECK_OP_VALUE)
        {
            // A leaf, two in five records: its record is the reader's, with no operand to find and its type's word read once.
            CheckValue* value = machine->values + r;
            node_machine_leaf(graph, node, value);
            word = node_machine_word(machine, value->type);
            const bool valid = (word & BUSTER_NODE_MACHINE_WORD_VALID) != 0 && (uint8_t)(word >> 16) != BUSTER_TYPE_KIND_VARIABLE_ARRAY;
            refusal = context ? NODE_MACHINE_REFUSED_CONTEXT : value->overload_head != 0 || !valid ? NODE_MACHINE_REFUSED_LEAF : NODE_MACHINE_TAKEN;
            machine->left[r] = UINT32_MAX;
            machine->right[r] = UINT32_MAX;
            machine->lowest[r] = r;
            machine->converted[2 * r] = value->type;
            machine->converted[2 * r + 1] = 0;
            machine->conversion_flags[2 * r] = 0;
            machine->conversion_flags[2 * r + 1] = 0;
            machine->member_offsets[r] = 0;
        }
        else
        {
            const uint32_t a = node_machine_operand(machine, r, 0);
            const uint32_t b = node_machine_operand(machine, r, 1);
            const uint32_t c = node_machine_operand(machine, r, 2);
            machine->left[r] = a;
            machine->right[r] = b;
            // Its subtree's least record (a call's arguments' too, node_machine_arguments).
            uint32_t lowest = a < r ? machine->lowest[a] : r;
            lowest = b < r && machine->lowest[b] < lowest ? machine->lowest[b] : lowest;
            machine->lowest[r] = c < r && machine->lowest[c] < lowest ? machine->lowest[c] : lowest;
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
            else if (!machine->scalar && a != UINT32_MAX && c == UINT32_MAX &&
                     ((node_machine_row_operators[op] != NODE_MACHINE_ROW_NONE && node_machine_row(machine, r, op, a, b)) || node_machine_rule(machine, r, node, op, a, b)))
            {
                machine->row_nodes += 1;
                machine->twin_differences += machine->verify && !node_machine_same_as_action(machine, r, op);
            }
            else
            {
                CheckActionResult result;
                memset(&result, 0, sizeof(result));
                node_machine_action(machine, r, op, machine->argument_conversions + (call ? machine->argument_first[r] : 0), &result);
                refusal = result.diagnostic != CHECK_DIAG_NONE ? NODE_MACHINE_REFUSED_DIAGNOSTIC : NODE_MACHINE_TAKEN;
                node_machine_store(machine, r, op, &result);
                machine->action_nodes += 1;
                machine->action_ops[op] += 1;
            }
            word = node_machine_word(machine, machine->values[r].type);
        }
        const uint8_t facts = (uint8_t)(word >> 8);
        machine->words[r] = word;
        machine->facts[r] = facts;
        machine->value_classes[r] = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : (uint8_t)word;
        machine->stopped = r;
        r += refusal == NODE_MACHINE_TAKEN;
    }
    return refusal;
}

#endif
