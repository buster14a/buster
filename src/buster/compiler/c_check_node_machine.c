// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2524's node machine (docs/node-machine.md): a full expression from its nodes to Tier 0's typed entries in one visit a
// node, by three sweeps over one small array of records. Step 1, the shadow: the sweeps run after the pipeline, on every
// root that measure-node-machine's census takes, compared with what the pipeline made and timed
// (c_check_node_census.c's --shadow). No compile takes their entries yet.
//
// The records are the expression's graph nodes, one range of them in postorder, its root last: the reader's walk built
// them, and in step 2 the machine reads the parser's tree in its place. The machine reads of them only what the reader
// answered at their tokens (a leaf's value, which is its declaration's or its literal's; a cast's, a sizeof's and a
// member's operand), never the graph run's output, the typed stream or the lowering's state:
//
// - T, up the postorder: each node's value and its operands' conversions. A leaf takes the reader's leaf; an operator
//   the transition table's row for its operator and its operands' builtin types (node_machine_rows, made from the
//   actions themselves by node_machine_tables_initialize and baked with the backend's tables), where its operands are
//   plain values the row was made for; and otherwise the action itself (check_action_compute) on the machine's own
//   records, as the graph run calls it. The action is the row's exact twin: --scalar types every node by it, and the
//   shadow's --verify runs both on every row-typed node and compares them. A node whose action makes a diagnostic, a
//   leaf with an overload set and a node in a constant or floating-point context refuse the expression.
// - C, down from the root: each node's template in its context (#1048's, c_check_program_templates.c's rules, read from
//   the records instead of the stream's columns), and each evaluated operand's context and the type its consumer
//   converts it to. A node the templates don't take refuses the expression.
// - X, up again: each evaluated node's row, its own entries and then its conversion's, in the evaluation order the tasks
//   write them in: postorder, and a logical operator's and a conditional's entries that come before or between their
//   operands' as a segment after the operand's row (NodeMachine.hooks), the rows' shape the design's risks name. A
//   local's entries hold its object, which the function's local numbers in step 2's splice; a global's its symbol; a
//   branch's its label, numbered from the expression's first (NodeMachine.label_bases), whose place in the tape is
//   NodeMachine.label_entries: the splice numbers them from the function's count and check_lower_resolve_labels gives
//   them their targets.
//
// The set is the census's (c_check_node_census.c) and, within it, the templates' and check_lower_logical's: a value read,
// an address, a folded constant, `&x`, `*p`, a member, a cast, a conversion, the unary and binary scalar operators, a
// pointer plus an integer, a subscript, the comma, `&&`, `||`, `?:` and an assignment to a local, a global or an address,
// each of a scalar class I8 to F64 (a conditional's of void too). Calls, the increments and the compound assignments are
// not yet the machine's: it refuses them.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_INCLUDED

// The most records an expression the machine takes has (docs/node-machine.md, "The records and the tapes").
#define BUSTER_NODE_MACHINE_MOST 4096u
// The most entries one record's row writes: its own and its conversion's (BUSTER_CHECK_LOWER_TEMPLATE_MOST).
#define BUSTER_NODE_MACHINE_ROW_MOST 8u
// The most entries a segment after a record's row writes (node_machine_hook): a test of its value and a branch.
#define BUSTER_NODE_MACHINE_HOOK_MOST 4u
// The builtin types a row's operand may be: a code below this.
#define BUSTER_NODE_MACHINE_KINDS 24u
// A record's state in C: reached and evaluated by its consumer's template.
#define BUSTER_NODE_MACHINE_EVALUATED 0x01u
// Its consumer's entries follow its row (NodeMachine.hooks): a logical operator's or a conditional's test of its
// condition, and the branch past its second operand or first arm.
#define BUSTER_NODE_MACHINE_AFTER_CONDITION 0x02u
// A conditional's jump from the end of its first arm to its end, and its second arm's label.
#define BUSTER_NODE_MACHINE_AFTER_ARM 0x04u

// A type's word's flags (NodeMachine.type_words): no aligned typedef's wrapper (type_strip_aligned is the type itself), and
// type_is_valid.
#define BUSTER_NODE_MACHINE_WORD_PLAIN 0x01000000u
#define BUSTER_NODE_MACHINE_WORD_VALID 0x02000000u

// The machine's templates past the node templates' (CheckLowerTemplate), check_lower_logical's: a logical operator's and
// a conditional's. A conditional whose condition is known takes CHECK_LOWER_TEMPLATE_PASS: its arm's entries alone,
// after its condition's where that has effects.
typedef enum NodeMachineTemplate
{
    // Its condition isn't known: two labels, its alternate and its end; its condition's test and branch to the alternate
    // after the condition's row (BUSTER_NODE_MACHINE_AFTER_CONDITION); a conditional's jump to its end after its first
    // arm's, with the alternate there (_AFTER_ARM); and its own row: a logical operator's second operand's test, its jump
    // to its end, the alternate and its value there, a conditional's nothing; then its end.
    NODE_MACHINE_TEMPLATE_BRANCH = CHECK_LOWER_TEMPLATE_STORE + 1,
    // A logical operator whose known condition decides it: its value, 0 for `&&` and 1 for `||`.
    NODE_MACHINE_TEMPLATE_DECIDED,
    // A logical operator whose known condition leaves its value to its second operand: that operand's test.
    NODE_MACHINE_TEMPLATE_SECOND,
} NodeMachineTemplate;

// Why the machine refuses an expression: the first sweep's reason that stopped it.
typedef enum NodeMachineRefusal
{
    NODE_MACHINE_TAKEN,
    // Its nodes aren't one range of the graph with every operand inside it, or past BUSTER_NODE_MACHINE_MOST.
    NODE_MACHINE_REFUSED_SHAPE,
    // An operator without a code of the machine's (a call, an increment, a compound assignment, a builtin...).
    NODE_MACHINE_REFUSED_CODE,
    // A leaf it doesn't take: an overload set's, an invalid type's, a variable-length array's.
    NODE_MACHINE_REFUSED_LEAF,
    // A node in a constant context or a floating-point environment.
    NODE_MACHINE_REFUSED_CONTEXT,
    // A node whose action makes a diagnostic.
    NODE_MACHINE_REFUSED_DIAGNOSTIC,
    // C: a node the templates don't take in its context.
    NODE_MACHINE_REFUSED_TEMPLATE,
    // X: a conversion the templates don't take, or a row past BUSTER_NODE_MACHINE_ROW_MOST entries.
    NODE_MACHINE_REFUSED_ROW,
    // A root whose consumer evaluates it in a context the machine doesn't give (initialized, a volatile copy, a _BitInt).
    NODE_MACHINE_REFUSED_ROOT,
    NODE_MACHINE_REFUSALS,
} NodeMachineRefusal;

static const char* const node_machine_refusal_names[NODE_MACHINE_REFUSALS] = {
    [NODE_MACHINE_TAKEN] = "taken",
    [NODE_MACHINE_REFUSED_SHAPE] = "shape",
    [NODE_MACHINE_REFUSED_CODE] = "code",
    [NODE_MACHINE_REFUSED_LEAF] = "leaf",
    [NODE_MACHINE_REFUSED_CONTEXT] = "context",
    [NODE_MACHINE_REFUSED_DIAGNOSTIC] = "diagnostic",
    [NODE_MACHINE_REFUSED_TEMPLATE] = "template",
    [NODE_MACHINE_REFUSED_ROW] = "row",
    [NODE_MACHINE_REFUSED_ROOT] = "root",
};

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

// The machine's state: the unit's, set once a file (node_machine_begin), and the current expression's records and tape.
typedef struct NodeMachine NodeMachine;
struct NodeMachine
{
    const CheckProgram* program;
    TypeTable* types;
    const CheckGraph* graph;
    // Every type's word, by type index (node_machine_word): its class and facts, its kind past an aligned typedef's
    // wrapper, and whether it is valid and has no such wrapper. One load answers what T, C and X ask of a type, where the
    // type table's columns take one each, each a miss of the first-level cache for a unit as large as sqlite3.c's.
    uint32_t* type_words;
    // Every type's first source position at which check_context_source_complete takes it as complete with the graph's tag
    // history (UINT32_MAX: none), by type index: one load where the function's walk reads four of the type table's
    // columns and the history.
    uint32_t* type_complete_at;
    uint32_t type_count;
    // --scalar: T types every node by the action, the rows' exact twin.
    bool scalar;
    // --verify: T types each row-typed node by the action too, and counts the nodes where the two differ.
    bool verify;
    uint64_t row_nodes;
    uint64_t action_nodes;
    uint64_t twin_differences;
    // The nodes T typed by the action, by operator.
    uint64_t action_ops[CHECK_OP_COUNT];
    // The current expression: its records are the graph's nodes first to first + count - 2, and its root the last, which is
    // the next node or a later one (a condition's wrapper, which the reader makes after the statement it controls).
    uint32_t first;
    uint32_t count;
    uint32_t root;
    // T's columns: each record's operator and its first two operands' records (UINT32_MAX for none), its value, its type's
    // facts and its class on the stack (an aggregate's is its address's), its operands' conversions (their types and
    // flags) and a member's offset in bits.
    uint8_t* codes;
    uint32_t* left;
    uint32_t* right;
    CheckValue* values;
    uint32_t* words;
    uint8_t* facts;
    uint8_t* value_classes;
    TypeId* converted;
    uint32_t* conversion_flags;
    uint64_t* member_offsets;
    // C's: each record's state, its context (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS and _DISCARD), the type its consumer
    // converts it to, and its template (CheckLowerTemplate, or past it NodeMachineTemplate). Where its state has
    // BUSTER_NODE_MACHINE_AFTER_CONDITION or _AFTER_ARM, `hooks` is its consumer's record, whose segment follows its row;
    // where its template is NODE_MACHINE_TEMPLATE_BRANCH, `label_bases` is the first of its two labels, counted from the expression's first in the
    // order the tasks number them (node_machine_number_labels), of the expression's `label_count`. `stack` is the
    // numbering's: the labeled records C met, then their subtrees' first records.
    uint8_t* states;
    uint8_t* contexts;
    TypeId* targets;
    uint8_t* templates;
    uint32_t* hooks;
    uint32_t* label_bases;
    uint32_t* stack;
    uint32_t label_count;
    // X's tape: the expression's entries, a local's holding its object and a branch's its label; and each label's entry,
    // its place in the tape.
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* tape;
    uint32_t* label_entries;
    uint32_t entry_count;
    // The record T, C or X stopped at, for a refusal's report.
    uint32_t stopped;
};

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

// The machine's columns for `program`'s unit, from `arena`: whether there was room.
static bool node_machine_begin(NodeMachine* machine, const CheckProgram* program, bool scalar, bool verify, Arena* arena)
{
    const uint32_t most = BUSTER_NODE_MACHINE_MOST;
    // Each record's row, and the segment one record's row may have after it.
    const uint32_t entries = BUSTER_NODE_MACHINE_MOST * (BUSTER_NODE_MACHINE_ROW_MOST + BUSTER_NODE_MACHINE_HOOK_MOST);
    *machine = (NodeMachine){
        .program = program,
        .types = (TypeTable*)program->types,
        .graph = &program->input->graph,
        .type_count = program->types->types.count,
        .scalar = scalar,
        .verify = verify,
    };
    machine->type_words = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)machine->type_count + 64);
    machine->type_complete_at = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)machine->type_count + 64);
    uint8_t* classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)machine->type_count + 64);
    uint8_t* facts = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)machine->type_count + 64);
    machine->codes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, most);
    machine->left = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->right = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->values = BUSTER_ARENA_PUSH_ARRAY(arena, CheckValue, most);
    machine->words = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->facts = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, most);
    machine->value_classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, most);
    machine->converted = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, 2 * most);
    machine->conversion_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * most);
    machine->member_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, most);
    machine->states = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, most);
    machine->contexts = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, most);
    machine->targets = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, most);
    machine->templates = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, most);
    machine->hooks = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->label_bases = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->stack = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * most);
    machine->kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, entries);
    machine->classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, entries);
    machine->tape = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, entries);
    machine->label_entries = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * most);
    const bool room = machine->type_words != NULL && machine->type_complete_at != NULL && classes != NULL && facts != NULL && machine->codes != NULL && machine->left != NULL && machine->right != NULL &&
                      machine->values != NULL && machine->words != NULL && machine->facts != NULL &&
                      machine->value_classes != NULL && machine->converted != NULL &&
                      machine->conversion_flags != NULL && machine->member_offsets != NULL && machine->states != NULL && machine->contexts != NULL &&
                      machine->targets != NULL && machine->templates != NULL && machine->hooks != NULL && machine->label_bases != NULL && machine->stack != NULL &&
                      machine->kinds != NULL && machine->classes != NULL && machine->tape != NULL && machine->label_entries != NULL;
    if (room)
    {
        check_lower_classify_types(program->types, machine->type_count, classes, facts);
        for (uint32_t index = 0; index < machine->type_count; index += 1)
        {
            const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
            const bool valid = type_is_valid(program->types, type);
            const bool plain = program->types->types.kind[index] < BUSTER_TYPE_KIND_WRAPPERS;
            const uint8_t kind = valid ? check_action_kind(program->types, type) : BUSTER_TYPE_KIND_NONE;
            machine->type_words[index] = (uint32_t)classes[index] | (uint32_t)facts[index] << 8 | (uint32_t)kind << 16 |
                                         (plain ? BUSTER_NODE_MACHINE_WORD_PLAIN : 0) | (valid ? BUSTER_NODE_MACHINE_WORD_VALID : 0);
            machine->type_complete_at[index] = valid ? node_machine_complete_at(program->types, &program->input->graph, type) : UINT32_MAX;
        }
    }
    return room && node_machine_tables_initialize();
}

// Type `type`'s word (NodeMachine.type_words), or a class of UINT8_MAX and no facts past the table.
static inline uint32_t node_machine_word(const NodeMachine* machine, TypeId type)
{
    const uint32_t index = type_index(type);
    return index < machine->type_count ? machine->type_words[index] : UINT8_MAX;
}

static uint8_t node_machine_class(const NodeMachine* machine, TypeId type)
{
    return (uint8_t)node_machine_word(machine, type);
}

static uint8_t node_machine_facts(const NodeMachine* machine, TypeId type)
{
    return (uint8_t)(node_machine_word(machine, type) >> 8);
}

static bool node_machine_aggregate(const NodeMachine* machine, TypeId type)
{
    return (node_machine_facts(machine, type) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
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
        case CHECK_OP_SELECT:
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

// Record `r`'s graph node.
static inline uint32_t node_machine_node(const NodeMachine* machine, uint32_t r)
{
    return r + 1 == machine->count ? machine->root : machine->first + r;
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
        const TypeId pointer = type_pointer(types, plain);
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
// Out of line, as the logical operators are few: inlined, it made node_machine_rule too large for clang to inline the
// store and pointer rules, which T reaches far more often.
static __attribute__((noinline)) bool node_machine_logic(NodeMachine* machine, uint32_t r, CheckOp op, uint32_t a, uint32_t b)
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
    return typed;
}

// T's exact path for record `r` (node `node`, operator `op`): the action itself, as the graph run calls it, on the
// operands' records, into `result`.
static void node_machine_action(const NodeMachine* machine, uint32_t r, CheckOp op, CheckActionResult* result)
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
    node_machine_action(machine, r, op, &result);
    const CheckValue* value = machine->values + r;
    return result.diagnostic == CHECK_DIAG_NONE && node_machine_same_values(&result.value, value) &&
           result.conversions[0].type == machine->converted[2 * r] && result.conversions[1].type == machine->converted[2 * r + 1] &&
           result.conversions[0].flags == machine->conversion_flags[2 * r] && result.conversions[1].flags == machine->conversion_flags[2 * r + 1] &&
           result.member_offset == machine->member_offsets[r];
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
            if (!node_machine_code(op))
            {
                refusal = NODE_MACHINE_REFUSED_CODE;
            }
            else if (a == BUSTER_NODE_MACHINE_MOST || b == BUSTER_NODE_MACHINE_MOST || c == BUSTER_NODE_MACHINE_MOST || graph->argument_counts[node] != 0)
            {
                refusal = NODE_MACHINE_REFUSED_SHAPE;
            }
            else if (context)
            {
                refusal = NODE_MACHINE_REFUSED_CONTEXT;
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
                node_machine_action(machine, r, op, &result);
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

// Whether record `r`'s object is a local of the function: an object without a symbol (a parameter's, an automatic
// object's), which the function's local numbers; one with a symbol is a global's.
static bool node_machine_local(const NodeMachine* machine, uint32_t object)
{
    const CheckProgram* program = machine->program;
    return object != 0 && object < program->object_count && program->object_symbols[object] == 0;
}

// Whether an object's address is a template's (check_lower_template_address_takes): none's, a local's or a global's.
static bool node_machine_address_takes(const NodeMachine* machine, uint32_t object)
{
    const CheckProgram* program = machine->program;
    return object == 0 || object < program->object_count;
}

// Whether a constant's entries are a template's (check_lower_template_constant_takes).
static bool node_machine_constant_takes(const NodeMachine* machine, const CheckValue* value)
{
    const uint8_t class = node_machine_class(machine, value->type);
    const uint8_t facts = node_machine_facts(machine, value->type);
    bool takes = false;
    if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        takes = node_machine_address_takes(machine, value->object);
    }
    else if ((facts & (BUSTER_CHECK_LOWER_TYPE_COMPLEX | BUSTER_CHECK_LOWER_TYPE_AGGREGATE)) == 0)
    {
        takes = (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64) ||
                (value->constant_kind == CHECK_CONSTANT_FLOATING && (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64));
    }
    return takes;
}

// The child's context as the tasks give it (check_lower_template_child_context): `address`, or discarded where the target
// is void and the child is a store of an aggregate, a bit-field's assignment or update, or a comma.
static uint8_t node_machine_child_context(const NodeMachine* machine, uint32_t child, bool address, TypeId target)
{
    uint8_t context = address ? BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS : 0;
    if (!address && target == BUSTER_TYPE_VOID)
    {
        const CheckOp op = (CheckOp)machine->codes[child];
        const bool store = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE;
        const uint32_t destination = machine->left[child];
        const bool bit_field = (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)) && destination < child &&
                               (machine->values[destination].flags & BUSTER_CHECK_BIT_FIELD) != 0;
        context = (store && (machine->facts[child] & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0) || bit_field || op == CHECK_OP_COMMA ? BUSTER_CHECK_LOWER_TEMPLATE_DISCARD : 0;
    }
    return context;
}

// Record `child` evaluated by its consumer with `address` and `target`. Always inlined: C calls it for every evaluated
// operand, and out of line it cost C 227 of its 3,024 samples on sqlite3.c on the 9700X (#2524's logical operators).
static inline __attribute__((always_inline)) void node_machine_evaluate(NodeMachine* machine, uint32_t child, bool address, TypeId target)
{
    machine->states[child] = BUSTER_NODE_MACHINE_EVALUATED;
    machine->contexts[child] = node_machine_child_context(machine, child, address, target);
    machine->targets[child] = target;
}

// Record `child` evaluated by its consumer `r` as a value converted to `target`, with `r`'s segment `hook`
// (BUSTER_NODE_MACHINE_AFTER_CONDITION or _AFTER_ARM) after its row.
static void node_machine_evaluate_hooked(NodeMachine* machine, uint32_t child, TypeId target, uint32_t r, uint8_t hook)
{
    node_machine_evaluate(machine, child, false, target);
    machine->states[child] = BUSTER_NODE_MACHINE_EVALUATED | hook;
    machine->hooks[child] = r;
}

// Whether a value of `type` takes check_lower_truth's test of it by a constant and NE (as the truth template does): a type of
// a class short of a vector's and not void, and no aggregate (a complex value's or a __float128's test is a task of its
// own) or vector (a vector condition's conditional is one too, check_lower_vector_logical).
static bool node_machine_testable(const NodeMachine* machine, TypeId type)
{
    const uint32_t word = node_machine_word(machine, type);
    const uint8_t class = (uint8_t)word;
    const uint8_t kind = (uint8_t)(word >> 16);
    return ((word >> 8) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) == 0 && class < BUSTER_TYPED_CLASS_VECTOR && class != BUSTER_TYPED_CLASS_VOID &&
           kind != BUSTER_TYPE_KIND_VECTOR && kind != BUSTER_TYPE_KIND_EXT_VECTOR;
}

// C's logical operator or conditional at record `r` (`op`), of value class `class` (check_lower_logical's rules, which
// check_lower_expand reaches for one that isn't folded, of a scalar class, or a conditional's of void, whose condition is
// no vector): its template, with its evaluated operands given their targets and its segments after them, or
// CHECK_LOWER_TEMPLATE_NONE. A condition the program knows (check_program_runtime_truth's, from its record) leaves its
// untaken operand unevaluated and numbers no label; a conditional of one operand twice (`a ?: b`), whose condition the
// tasks keep in a temporary, is the tasks'. Out of line, as these operators are few, so that C's classification of the
// others stays as small as it was.
static __attribute__((noinline)) CheckLowerTemplate node_machine_logical(NodeMachine* machine, uint32_t r, CheckOp op, uint8_t class)
{
    const uint32_t a = machine->left[r];
    const uint32_t b = machine->right[r];
    const bool conditional = op == CHECK_OP_CONDITIONAL;
    const uint32_t c = conditional ? node_machine_operand(machine, r, 2) : UINT32_MAX;
    const TypeId type = machine->values[r].type;
    const TypeId type_a = machine->converted[2 * r];
    const TypeId type_b = machine->converted[2 * r + 1];
    const bool scalar = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64 && (machine->facts[r] & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) == 0;
    const bool shape = a < r && b < r && (!conditional || (c < r && a != b));
    CheckLowerTemplate kind = CHECK_LOWER_TEMPLATE_NONE;
    if (shape && (scalar || (conditional && class == BUSTER_TYPED_CLASS_VOID)))
    {
        const CheckValue* condition = machine->values + a;
        bool truth = false;
        const bool known = condition->constant_kind != CHECK_CONSTANT_NONE && (condition->flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0 &&
                           condition->fault == CHECK_FAULT_NONE && check_action_truth(machine->types, condition, &truth);
        const bool effects = (condition->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) != 0;
        if (known)
        {
            // The condition, for its effects alone, then the arm or the second operand it leaves the value to, if any.
            const bool decided = !conditional && (op == CHECK_OP_LOGICAL_OR) == truth;
            const bool second = !conditional && !decided;
            kind = conditional ? CHECK_LOWER_TEMPLATE_PASS : decided ? (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_DECIDED :
                   node_machine_testable(machine, type_b) ? (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_SECOND : CHECK_LOWER_TEMPLATE_NONE;
            if (kind != CHECK_LOWER_TEMPLATE_NONE && effects)
            {
                node_machine_evaluate(machine, a, false, BUSTER_TYPE_VOID);
            }
            if (kind != CHECK_LOWER_TEMPLATE_NONE && (conditional || second))
            {
                node_machine_evaluate(machine, conditional ? (truth ? b : c) : b, false, conditional ? type : type_b);
            }
        }
        else if (node_machine_testable(machine, type_a) && (conditional || node_machine_testable(machine, type_b)))
        {
            kind = (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_BRANCH;
            machine->stack[machine->label_count / 2] = r;
            machine->label_count += 2;
            node_machine_evaluate_hooked(machine, a, type_a, r, BUSTER_NODE_MACHINE_AFTER_CONDITION);
            if (conditional)
            {
                node_machine_evaluate_hooked(machine, b, type, r, BUSTER_NODE_MACHINE_AFTER_ARM);
                node_machine_evaluate(machine, c, false, type);
            }
            else
            {
                node_machine_evaluate(machine, b, false, type_b);
            }
        }
    }
    return kind;
}

// Whether the unsigned division or remainder at record `r` is by a constant power of two, which the tasks write as a
// shift or a mask (check_lower_unsigned_power): the templates leave such a node to the tasks.
static bool node_machine_unsigned_power(const NodeMachine* machine, uint32_t r, uint8_t class, uint8_t binary)
{
    bool replace = false;
    const uint32_t b = machine->right[r];
    const TypeId converted = machine->converted[2 * r + 1];
    if ((binary == BUSTER_TYPED_DIV || binary == BUSTER_TYPED_MOD) && (class == BUSTER_TYPED_CLASS_U32 || class == BUSTER_TYPED_CLASS_U64) && b < r &&
        (node_machine_facts(machine, converted) & BUSTER_CHECK_LOWER_TYPE_BIT_INT) == 0)
    {
        const CheckValue* value = machine->values + b;
        const CheckOp op = (CheckOp)machine->codes[b];
        const uint8_t source_class = node_machine_class(machine, value->type);
        const bool plain_integer = source_class >= BUSTER_TYPED_CLASS_I8 && source_class <= BUSTER_TYPED_CLASS_U64 &&
                                   (node_machine_facts(machine, value->type) & BUSTER_CHECK_LOWER_TYPE_BIT_INT) == 0;
        uint64_t bits = (uint64_t)value->integer;
        if (plain_integer && source_class <= BUSTER_TYPED_CLASS_U32)
        {
            const uint32_t width = 8u << ((source_class - 1) / 2);
            const uint64_t mask = (UINT64_C(1) << width) - 1;
            bits &= mask;
            bits |= (source_class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
        }
        const uint64_t divisor = class == BUSTER_TYPED_CLASS_U32 ? (uint32_t)bits : bits;
        replace = plain_integer && value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE && value->category == CHECK_RVALUE &&
                  value->overload_head == 0 && (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 && op != CHECK_OP_STATEMENT_VALUE &&
                  op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY && divisor != 0 && (divisor & (divisor - 1)) == 0;
    }
    return replace;
}

// C's classification of record `r` in its context: its template, with its evaluated operands given their contexts and
// targets (check_lower_template_classify's rules, from the records), or CHECK_LOWER_TEMPLATE_NONE. `object` is the
// object a root INITIALIZE initializes.
static CheckLowerTemplate node_machine_classify(NodeMachine* machine, uint32_t r, uint32_t object)
{
    const CheckProgram* program = machine->program;
    const TypeTable* types = machine->types;
    const CheckOp op = (CheckOp)machine->codes[r];
    const CheckValue* value = machine->values + r;
    const uint8_t context = machine->contexts[r];
    const bool address = (context & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0;
    const bool discard = (context & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) != 0;
    const uint32_t a = machine->left[r];
    const uint32_t b = machine->right[r];
    const TypeId type_a = machine->converted[2 * r];
    const TypeId type_b = machine->converted[2 * r + 1];
    const uint8_t facts = machine->facts[r];
    const bool aggregate = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
    const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = machine->value_classes[r];
    const bool constant = value->constant_kind != CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_NONE &&
                          (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    const uint32_t known_object = value->object;
    const bool known = known_object != 0 && known_object < program->object_count;
    const bool arithmetic = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_SUBSCRIPT;
    const bool left_pointer = arithmetic && (node_machine_facts(machine, type_a) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
    const bool right_pointer = arithmetic && (node_machine_facts(machine, type_b) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
    const bool pointers = left_pointer || right_pointer;
    const bool scalar_class = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64;
    CheckLowerTemplate kind = CHECK_LOWER_TEMPLATE_NONE;
    if (value->overload_head != 0 || (facts & (BUSTER_CHECK_LOWER_TYPE_BIT_INT | BUSTER_CHECK_LOWER_TYPE_COMPLEX)) != 0 ||
        (op == CHECK_OP_CAST && (machine->conversion_flags[2 * r] & BUSTER_CHECK_CONVERT_TO_UNION) != 0))
    {
    }
    else if (op == CHECK_OP_VALUE && known && check_input_bit(program->input->declaration_bits, known_object - 1, BUSTER_CHECK_BITS_COMPOUND) &&
             !check_input_bit(program->input->declaration_bits, known_object - 1, BUSTER_CHECK_BITS_STATIC))
    {
    }
    else if (!address && value->category == CHECK_LVALUE && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0)
    {
    }
    else if (constant && op != CHECK_OP_INITIALIZE && ((value->category == CHECK_RVALUE && !address) || ((address || address_value) && value->constant_kind == CHECK_CONSTANT_ADDRESS)))
    {
        kind = discard || node_machine_constant_takes(machine, value) ? CHECK_LOWER_TEMPLATE_CONSTANT : CHECK_LOWER_TEMPLATE_NONE;
    }
    else if (op == CHECK_OP_VALUE)
    {
        if (value->type == BUSTER_TYPE_VOID)
        {
            kind = CHECK_LOWER_TEMPLATE_VOID;
        }
        else if (address || address_value)
        {
            kind = node_machine_address_takes(machine, known_object) ? CHECK_LOWER_TEMPLATE_ADDRESS : CHECK_LOWER_TEMPLATE_NONE;
        }
        else if (value->category == CHECK_LVALUE && known && scalar_class && !aggregate)
        {
            kind = node_machine_local(machine, known_object) ? CHECK_LOWER_TEMPLATE_LOCAL : CHECK_LOWER_TEMPLATE_GLOBAL;
        }
    }
    else if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL)
    {
        kind = address ? CHECK_LOWER_TEMPLATE_NONE : node_machine_logical(machine, r, op, class);
    }
    else if (op == CHECK_OP_SELECT)
    {
        kind = CHECK_LOWER_TEMPLATE_PASS;
        node_machine_evaluate(machine, a, address, 0);
    }
    else if (op == CHECK_OP_ADDRESS)
    {
        kind = CHECK_LOWER_TEMPLATE_PASS;
        node_machine_evaluate(machine, a, true, 0);
    }
    else if (op == CHECK_OP_DEREFERENCE)
    {
        kind = aggregate || address || address_value || scalar_class ? CHECK_LOWER_TEMPLATE_DEREFERENCE : CHECK_LOWER_TEMPLATE_NONE;
        node_machine_evaluate(machine, a, false, type_a);
    }
    else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
    {
        const TypeId record = op == CHECK_OP_ARROW ? type_element(types, type_a) : machine->values[a].type;
        const uint8_t record_kind = check_action_kind(types, record);
        const bool bit_field = (value->flags & BUSTER_CHECK_BIT_FIELD) != 0;
        const bool loads = !address && !address_value;
        if ((record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) && !bit_field && machine->member_offsets[r] % 8 == 0 && (!loads || scalar_class))
        {
            kind = CHECK_LOWER_TEMPLATE_MEMBER;
            node_machine_evaluate(machine, a, op == CHECK_OP_MEMBER, type_a);
        }
    }
    else if (arithmetic && (value->flags & BUSTER_CHECK_VECTOR_ELEMENT) == 0 && left_pointer && !right_pointer && check_action_pointer_stride(types, type_a) != 0 &&
             (op != CHECK_OP_SUBSCRIPT || address || address_value || scalar_class))
    {
        kind = CHECK_LOWER_TEMPLATE_POINTER;
        node_machine_evaluate(machine, a, false, type_a);
        node_machine_evaluate(machine, b, false, BUSTER_TYPE_LONG);
    }
    else if (op == CHECK_OP_SUBSCRIPT || aggregate || !scalar_class)
    {
        if ((op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE) && class != UINT8_MAX && class < BUSTER_TYPED_CLASS_VECTOR)
        {
            kind = CHECK_LOWER_TEMPLATE_PASS;
            node_machine_evaluate(machine, a, false, value->type);
        }
    }
    else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
    {
        kind = CHECK_LOWER_TEMPLATE_PASS;
        node_machine_evaluate(machine, a, false, value->type);
    }
    else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
    {
        kind = CHECK_LOWER_TEMPLATE_UNARY;
        node_machine_evaluate(machine, a, false, type_a);
    }
    else if (op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
    {
        const uint8_t operand_class = node_machine_class(machine, type_a);
        if (!node_machine_aggregate(machine, type_a) && operand_class < BUSTER_TYPED_CLASS_VECTOR && operand_class != BUSTER_TYPED_CLASS_VOID)
        {
            kind = CHECK_LOWER_TEMPLATE_TRUTH;
            node_machine_evaluate(machine, a, false, type_a);
        }
    }
    else if (op == CHECK_OP_COMMA)
    {
        kind = CHECK_LOWER_TEMPLATE_PASS;
        node_machine_evaluate(machine, a, false, BUSTER_TYPE_VOID);
        node_machine_evaluate(machine, b, address, discard ? BUSTER_TYPE_VOID : value->type);
    }
    else if ((op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT) && (value->flags & BUSTER_CHECK_VECTOR_ELEMENT) == 0 && !pointers)
    {
        const uint8_t class_a = node_machine_class(machine, type_a);
        const uint8_t class_b = node_machine_class(machine, type_b);
        if (class_a >= BUSTER_TYPED_CLASS_I8 && class_a <= BUSTER_TYPED_CLASS_F64 && class_b >= BUSTER_TYPED_CLASS_I8 && class_b <= BUSTER_TYPED_CLASS_F64)
        {
            kind = CHECK_LOWER_TEMPLATE_BINARY;
            node_machine_evaluate(machine, a, false, type_a);
            node_machine_evaluate(machine, b, false, type_b);
        }
    }
    else if (check_lower_binary(op) != BUSTER_TYPED_NONE && op < CHECK_OP_PRE_INCREMENT && !arithmetic)
    {
        const uint8_t class_a = node_machine_class(machine, type_a);
        const uint8_t class_b = node_machine_class(machine, type_b);
        const bool power = node_machine_unsigned_power(machine, r, class, check_lower_binary(op));
        if (class_a >= BUSTER_TYPED_CLASS_I8 && class_a <= BUSTER_TYPED_CLASS_F64 && class_b >= BUSTER_TYPED_CLASS_I8 && class_b <= BUSTER_TYPED_CLASS_F64 &&
            !node_machine_aggregate(machine, type_a) && !power)
        {
            kind = CHECK_LOWER_TEMPLATE_BINARY;
            node_machine_evaluate(machine, a, false, type_a);
            node_machine_evaluate(machine, b, false, type_b);
        }
    }
    else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE)
    {
        const uint32_t destination_facts = a < r ? node_machine_facts(machine, machine->values[a].type) : 0;
        const bool bit_field = op == CHECK_OP_ASSIGN && a < r && (machine->values[a].flags & BUSTER_CHECK_BIT_FIELD) != 0;
        if ((destination_facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0 && !bit_field)
        {
            const uint32_t target = op == CHECK_OP_INITIALIZE ? object :
                                    a < r && machine->codes[a] == CHECK_OP_VALUE ? machine->values[a].object : 0;
            const bool named = target != 0 && target < program->object_count;
            if (named)
            {
                kind = node_machine_local(machine, target) ? CHECK_LOWER_TEMPLATE_STORE_LOCAL : CHECK_LOWER_TEMPLATE_STORE_GLOBAL;
                node_machine_evaluate(machine, op == CHECK_OP_INITIALIZE ? a : b, false, value->type);
            }
            else if (op == CHECK_OP_ASSIGN)
            {
                kind = CHECK_LOWER_TEMPLATE_STORE;
                node_machine_evaluate(machine, a, true, 0);
                node_machine_evaluate(machine, b, false, value->type);
            }
        }
    }
    return kind;
}

// The first record of record `r`'s subtree. Its operands' subtrees come before it one after another (X's postorder), so it
// is its first operand's first record, the operand of the least record, down to a leaf.
static uint32_t node_machine_subtree_first(const NodeMachine* machine, uint32_t r)
{
    uint32_t first = r;
    bool deeper = true;
    while (deeper)
    {
        const bool leaf = machine->codes[first] == CHECK_OP_VALUE;
        const uint32_t a = leaf ? UINT32_MAX : machine->left[first];
        const uint32_t b = leaf ? UINT32_MAX : machine->right[first];
        const uint32_t c = machine->codes[first] == CHECK_OP_CONDITIONAL ? node_machine_operand(machine, first, 2) : UINT32_MAX;
        const uint32_t least = a < b ? (a < c ? a : c) : (b < c ? b : c);
        deeper = least < first;
        first = deeper ? least : first;
    }
    return first;
}

// The labels' numbers (NodeMachine.label_bases), each labeled record's first, in the order the tasks number them: the
// order they pop the records, a preorder from the root with each record's operands in the order of their entries, which
// is the records' (X's postorder). A labeled record's rank in it among the labeled records is the number of those that are
// its ancestors (whose subtree, from its first record to it, holds it) and of those before its subtree's first record (to
// its left). Only an expression with labels runs it, and it reads the labeled records alone (C listed them in `stack`,
// their subtrees' first records after them): a walk of every record cost C 125 cycles a new taken node on sqlite3.c on
// the 9700X, three times its average, by its branches. Scalar: an expression has a few, and their order is the tree's.
static __attribute__((noinline)) void node_machine_number_labels(NodeMachine* machine)
{
    const uint32_t labeled = machine->label_count / 2;
    uint32_t* records = machine->stack;
    uint32_t* firsts = machine->stack + labeled;
    for (uint32_t i = 0; i < labeled; i += 1)
    {
        firsts[i] = node_machine_subtree_first(machine, records[i]);
    }
    for (uint32_t i = 0; i < labeled; i += 1)
    {
        const uint32_t r = records[i];
        uint32_t rank = 0;
        for (uint32_t j = 0; j < labeled; j += 1)
        {
            const uint32_t other = records[j];
            rank += (other > r && firsts[j] <= r) || other < firsts[i];
        }
        machine->label_bases[r] = 2 * rank;
    }
}

// C: every evaluated record's template and its operands' contexts and targets, down from the root, which its consumer
// evaluates with `context` and converts to `target`; `object` is what a root INITIALIZE initializes. Then the labels'
// numbers, where a record numbers any.
static NodeMachineRefusal node_machine_contexts(NodeMachine* machine, uint8_t context, TypeId target, uint32_t object)
{
    NodeMachineRefusal refusal = NODE_MACHINE_TAKEN;
    const uint32_t count = machine->count;
    memset(machine->states, 0, count);
    machine->label_count = 0;
    const uint32_t root = count - 1;
    machine->states[root] = BUSTER_NODE_MACHINE_EVALUATED;
    machine->contexts[root] = context;
    machine->targets[root] = target;
    uint32_t r = count;
    while (refusal == NODE_MACHINE_TAKEN && r != 0)
    {
        r -= 1;
        if ((machine->states[r] & BUSTER_NODE_MACHINE_EVALUATED) != 0)
        {
            const CheckLowerTemplate kind = node_machine_classify(machine, r, r == root ? object : 0);
            machine->templates[r] = (uint8_t)kind;
            refusal = kind == CHECK_LOWER_TEMPLATE_NONE ? NODE_MACHINE_REFUSED_TEMPLATE : NODE_MACHINE_TAKEN;
            machine->stopped = r;
        }
    }
    if (refusal == NODE_MACHINE_TAKEN && machine->label_count != 0)
    {
        node_machine_number_labels(machine);
    }
    return refusal;
}

static inline __attribute__((always_inline)) void node_machine_put(NodeMachine* machine, uint8_t kind, uint8_t class, uint64_t value)
{
    const uint32_t at = machine->entry_count;
    machine->kinds[at] = kind;
    machine->classes[at] = class;
    machine->tape[at] = value;
    machine->entry_count = at + 1;
}

// A test of a value of class `class` (check_lower_truth's entries): a narrow class's value converted to I32, then compared
// with 0 by NE, or by EQ where `negate`.
static void node_machine_put_truth(NodeMachine* machine, uint8_t class, bool negate)
{
    const bool narrow = check_lower_narrow(class);
    if (narrow)
    {
        node_machine_put(machine, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
    }
    node_machine_put(machine, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : class, 0);
    node_machine_put(machine, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
}

// An object's address's entries (check_lower_template_address): a local's (its object, which the splice numbers) or a
// global's symbol, then its offset; a constant's alone.
static void node_machine_address(NodeMachine* machine, uint32_t object, int64_t offset)
{
    if (object == 0)
    {
        node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)offset);
    }
    else
    {
        if (node_machine_local(machine, object))
        {
            node_machine_put(machine, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
        else
        {
            node_machine_put(machine, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, machine->program->object_symbols[object]);
        }
        if (offset != 0)
        {
            node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, (uint64_t)offset);
            node_machine_put(machine, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
    }
}

// Record `r`'s own entries by its template (check_lower_template_own's), after its operands'. `object` as in C.
static void node_machine_own(NodeMachine* machine, uint32_t r, uint32_t object)
{
    const CheckProgram* program = machine->program;
    const TypeTable* types = machine->types;
    const CheckOp op = (CheckOp)machine->codes[r];
    const CheckValue* value = machine->values + r;
    const CheckLowerTemplate kind = (CheckLowerTemplate)machine->templates[r];
    const uint8_t context = machine->contexts[r];
    const bool address = (context & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0;
    const bool discard = (context & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) != 0;
    const bool address_value = (machine->facts[r] & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = machine->value_classes[r];
    if (kind == CHECK_LOWER_TEMPLATE_CONSTANT && !discard)
    {
        if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
        {
            node_machine_address(machine, value->object, value->offset);
            if (value->category == CHECK_RVALUE && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_I64)
            {
                node_machine_put(machine, BUSTER_TYPED_CONVERT, class, 0);
            }
        }
        else if (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64)
        {
            uint64_t bits = (uint64_t)value->integer;
            const uint32_t width = 8u << ((class - 1) / 2);
            if (width < 64)
            {
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                bits &= mask;
                bits |= (class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
            }
            node_machine_put(machine, BUSTER_TYPED_CONST, class, bits);
        }
        else
        {
            node_machine_put(machine, BUSTER_TYPED_CONST, class, check_lower_float_bits(NULL, class, value->floating));
        }
    }
    else if (kind == CHECK_LOWER_TEMPLATE_ADDRESS)
    {
        node_machine_address(machine, value->object, value->offset);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_LOCAL)
    {
        node_machine_put(machine, BUSTER_TYPED_LOCAL, class, value->object);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_GLOBAL)
    {
        node_machine_put(machine, BUSTER_TYPED_GLOBAL, class, program->object_symbols[value->object]);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_DEREFERENCE && !address && !address_value)
    {
        node_machine_put(machine, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
    }
    else if (kind == CHECK_LOWER_TEMPLATE_MEMBER)
    {
        const uint64_t offset = machine->member_offsets[r];
        if (offset / 8 != 0)
        {
            node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
            node_machine_put(machine, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
        if (!address && !address_value)
        {
            node_machine_put(machine, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
        }
    }
    else if (kind == CHECK_LOWER_TEMPLATE_UNARY)
    {
        node_machine_put(machine, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_TRUTH)
    {
        node_machine_put_truth(machine, node_machine_class(machine, machine->converted[2 * r]), op == CHECK_OP_NOT);
    }
    else if (kind == (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_BRANCH)
    {
        // After its second operand's or second arm's row: a logical operator's test of it, its jump to its end, its
        // alternate and its value there, 0 for `&&` and 1 for `||`; then its end, which a conditional's first arm jumps to.
        const uint32_t alternate = machine->label_bases[r];
        if (op != CHECK_OP_CONDITIONAL)
        {
            node_machine_put_truth(machine, node_machine_class(machine, machine->converted[2 * r + 1]), false);
            node_machine_put(machine, class == BUSTER_TYPED_CLASS_VOID ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE, class, alternate + 1);
            machine->label_entries[alternate] = machine->entry_count;
            node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, op == CHECK_OP_LOGICAL_OR);
        }
        machine->label_entries[alternate + 1] = machine->entry_count;
    }
    else if (kind == (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_DECIDED)
    {
        node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, op == CHECK_OP_LOGICAL_OR);
    }
    else if (kind == (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_SECOND)
    {
        node_machine_put_truth(machine, node_machine_class(machine, machine->converted[2 * r + 1]), false);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_BINARY)
    {
        node_machine_put(machine, check_lower_binary(op), class, 0);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_POINTER)
    {
        if (op == CHECK_OP_SUBTRACT)
        {
            node_machine_put(machine, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
        }
        node_machine_put(machine, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, check_action_pointer_stride(types, machine->converted[2 * r]));
        if (op == CHECK_OP_SUBSCRIPT && !address && !address_value)
        {
            node_machine_put(machine, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
        }
    }
    else if (kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL || kind == CHECK_LOWER_TEMPLATE_STORE_GLOBAL)
    {
        const uint32_t a = machine->left[r];
        const uint32_t target = op == CHECK_OP_INITIALIZE ? object : machine->values[a].object;
        node_machine_put(machine, kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                         kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL ? target : program->object_symbols[target]);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_STORE)
    {
        node_machine_put(machine, BUSTER_TYPED_STORE, class, check_lower_access_value(types, class, value->type));
    }
}

// Record `r`'s conversion's entries, from its type to its target in its context (check_lower_template_conversion's):
// whether the templates take it.
static bool node_machine_conversion(NodeMachine* machine, uint32_t r)
{
    const TypeId from = machine->values[r].type;
    const TypeId to = machine->targets[r];
    bool takes = true;
    if (machine->contexts[r] == 0 && to != 0 && from != to)
    {
        const uint8_t from_facts = machine->facts[r];
        const uint8_t to_facts = node_machine_facts(machine, to);
        const uint8_t source = machine->value_classes[r];
        const uint8_t target = (to_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : node_machine_class(machine, to);
        // check_lower_conversion_empty: the same class, neither a _BitInt nor complex, nor a conversion to bool.
        const bool empty = source == target && source < BUSTER_TYPED_CLASSES &&
                           ((from_facts ^ to_facts) & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0 &&
                           ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) == 0 &&
                           (to_facts & (BUSTER_CHECK_LOWER_TYPE_BOOL | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0;
        const bool mixed = ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 && target != BUSTER_TYPED_CLASS_VOID;
        const bool complex = ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) != 0 && target != BUSTER_TYPED_CLASS_VOID;
        if (empty)
        {
        }
        else if (complex || mixed || source >= BUSTER_TYPED_CLASS_F80 || target >= BUSTER_TYPED_CLASS_F80)
        {
            takes = false;
        }
        else if (target == BUSTER_TYPED_CLASS_VOID)
        {
            if (source != BUSTER_TYPED_CLASS_VOID)
            {
                node_machine_put(machine, BUSTER_TYPED_DISCARD, source, 0);
            }
        }
        else if ((to_facts & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
        {
            const bool narrow = check_lower_narrow(source);
            if (narrow)
            {
                node_machine_put(machine, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
            }
            node_machine_put(machine, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : source, 0);
            node_machine_put(machine, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
            node_machine_put(machine, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
        }
        else if (source != target)
        {
            node_machine_put(machine, BUSTER_TYPED_CONVERT, target, 0);
        }
    }
    return takes;
}

// The segment of record `r`'s consumer after `r`'s row (`state`, its state, says which): a logical operator's or a
// conditional's test of its condition and branch to its alternate, past its second operand or first arm (a `||`'s where
// the condition holds, the others' where it doesn't); or a conditional's jump from its first arm to its end, then its
// alternate, where its second arm starts.
static void node_machine_hook(NodeMachine* machine, uint32_t r, uint8_t state)
{
    const uint32_t consumer = machine->hooks[r];
    const uint32_t alternate = machine->label_bases[consumer];
    if ((state & BUSTER_NODE_MACHINE_AFTER_CONDITION) != 0)
    {
        node_machine_put_truth(machine, node_machine_class(machine, machine->converted[2 * consumer]), false);
        node_machine_put(machine, machine->codes[consumer] == CHECK_OP_LOGICAL_OR ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32,
                         alternate);
    }
    else
    {
        const uint8_t class = machine->value_classes[consumer];
        node_machine_put(machine, class == BUSTER_TYPED_CLASS_VOID ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE, class, alternate + 1);
        machine->label_entries[alternate] = machine->entry_count;
    }
}

// X: each evaluated record's row, its own entries and then its conversion's, up the postorder into the tape, and after it
// its consumer's segment where it has one (node_machine_hook). `object` as in C.
static NodeMachineRefusal node_machine_expand(NodeMachine* machine, uint32_t object)
{
    NodeMachineRefusal refusal = NODE_MACHINE_TAKEN;
    const uint32_t count = machine->count;
    machine->entry_count = 0;
    uint32_t r = 0;
    while (refusal == NODE_MACHINE_TAKEN && r < count)
    {
        const uint8_t state = machine->states[r];
        if ((state & BUSTER_NODE_MACHINE_EVALUATED) != 0)
        {
            const uint32_t before = machine->entry_count;
            node_machine_own(machine, r, r == count - 1 ? object : 0);
            const bool takes = node_machine_conversion(machine, r);
            refusal = !takes || machine->entry_count - before > BUSTER_NODE_MACHINE_ROW_MOST ? NODE_MACHINE_REFUSED_ROW : NODE_MACHINE_TAKEN;
            if (refusal == NODE_MACHINE_TAKEN && (state & (BUSTER_NODE_MACHINE_AFTER_CONDITION | BUSTER_NODE_MACHINE_AFTER_ARM)) != 0)
            {
                node_machine_hook(machine, r, state);
            }
            machine->stopped = r;
        }
        r += refusal == NODE_MACHINE_TAKEN;
    }
    return refusal;
}

// The three sweeps over the expression of `count` records (node_machine_type's), whose consumer evaluates it with
// `context` and converts it to `target` (`object`: what a root INITIALIZE initializes): its refusal, or NODE_MACHINE_TAKEN
// with its entries on the tape.
static NodeMachineRefusal node_machine_run(NodeMachine* machine, uint32_t first, uint32_t root, uint32_t count, uint8_t context, TypeId target, uint32_t object)
{
    NodeMachineRefusal refusal = node_machine_type(machine, first, root, count);
    refusal = refusal == NODE_MACHINE_TAKEN ? node_machine_contexts(machine, context, target, object) : refusal;
    refusal = refusal == NODE_MACHINE_TAKEN ? node_machine_expand(machine, object) : refusal;
    return refusal;
}

#endif
