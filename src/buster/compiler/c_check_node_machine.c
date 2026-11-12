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
// - T, up the postorder (c_check_node_machine_type.c, node_machine_type): each node's value and its operands'
//   conversions. A leaf takes the reader's leaf; an operator the transition table's row for its operator and its
//   operands' builtin types (node_machine_rows, made from the actions themselves by node_machine_tables_initialize and
//   baked with the backend's tables), where its operands are plain values the row was made for; and otherwise the action
//   itself (check_action_compute) on the machine's own records, as the graph run calls it. The action is the row's exact
//   twin: --scalar types every node by it, and the shadow's --verify runs both on every row-typed node and compares
//   them. A node whose action makes a diagnostic, a leaf with an overload set and a node in a constant or floating-point
//   context refuse the expression.
// - C, down from the root (c_check_node_machine_contexts.c, node_machine_contexts): each node's template in its context
//   (#1048's, c_check_program_templates.c's rules, read from the records instead of the stream's columns), and each
//   evaluated operand's context and the type its consumer converts it to. A node the templates don't take refuses the
//   expression.
// - X, up again (c_check_node_machine_expand.c, node_machine_expand): each evaluated node's row, its own entries and
//   then its conversion's, in the evaluation order the tasks write them in: postorder, and a logical operator's and a
//   conditional's entries that come before or between their operands' as a segment after the operand's row
//   (NodeMachine.hooks), the rows' shape the design's risks name. A local's entries hold its object, which the
//   function's local numbers in step 2's splice; a global's its symbol; a branch's its label, numbered from the
//   expression's first (NodeMachine.label_bases), whose place in the tape is NodeMachine.label_entries: the splice numbers
//   them from the function's count and check_lower_resolve_labels gives them their targets.
//
// The set is the census's (c_check_node_census.c) and, within it, the templates' and check_lower_logical's: a value read,
// an address, a folded constant, `&x`, `*p`, a member, a cast, a conversion, the unary and binary scalar operators, a
// pointer plus an integer, a subscript, the comma, `&&`, `||`, `?:` and an assignment to a local, a global or an address,
// each of a scalar class I8 to F64 (a conditional's of void too); a plain call (below); and the lowering's updates
// (check_lower_update): `++x`, `--x`, `x++`, `x--` and the compound assignments, of a local, a global or an address, of
// the same classes, but a bit-field's.
//
// An update (docs/node-machine.md, "Increments and compound assignments") writes two rows: its first after its
// destination's entries (the old value read, by name or through the destination's kept address, and converted to its
// computation's type; BUSTER_NODE_MACHINE_SPLIT), and its second, its own, after its value's (the operation, the
// conversion back and the store). C keeps its plan (NodeMachineUpdate), which X reads at both. A postfix one through an
// address keeps the address and the old value in two temporaries, each its number in the expression on the tape
// (BUSTER_NODE_MACHINE_TEMPORARY), which step 2's splice numbers as check_lower_temporary does. T types it by
// check_action_update's steps (node_machine_update_rule, node_machine_update_pointer_rule), the action its twin.
//
// A call (docs/node-machine.md, "T: bind and type"): T types it by its callee's prototype, each argument converted as an
// assignment converts it and a variadic one by the default promotions (node_machine_call_rule, the action its twin), and
// refuses a callee with semantics of its own (node_machine_callee_refused). C takes the lowering's plain call
// (check_lower_call_body's: a result, parameters and arguments of plain scalars) and evaluates its callee where the call
// is indirect, then its arguments, each to its conversion's type (node_machine_classify_call). X writes a CALL whose
// value is the call's row of the function's calls table, numbered in the tasks' preorder, as the lowering allocates its
// rows, but relative to the expression's first: row 0 is the expression's outermost first call (step 2's splice adds the
// calls before it; node_machine_call_own). Its row's contents, the callee's symbol, the arguments' count and classes and
// whether it is variadic, are the call record's (node_machine_call_target, node_machine_call_variadic and the argument
// accessors).
//
// This file is the machine's state and its records' interface; main.c includes the three sweeps' files after it, in
// the checker unit, before baked.c:
//
// - NodeMachine: the unit's state (node_machine_begin) and the current expression's columns, which T writes, C reads and
//   writes its own of, and X reads, writing the tape.
// - The type words (node_machine_word, node_machine_class, node_machine_facts, node_machine_aggregate): one load a type.
// - The record accessors (node_machine_record_*): every read of a record by C, X and the shadow's comparison, and C's
//   writes of its own columns, go through them, so that a record's layout (today T's columns, a whole CheckValue a
//   record; the design's 28 bytes next) changes here and in T alone.
// - node_machine_local and node_machine_address_takes: what C and X ask of an object; node_machine_callee_function,
//   node_machine_direct_callee, node_machine_call_target and node_machine_call_variadic: what T, C, X and the shadow ask
//   of a call.
// - node_machine_run: the three sweeps, one after another.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_INCLUDED

// The most records an expression the machine takes has (docs/node-machine.md, "The records and the tapes").
#define BUSTER_NODE_MACHINE_MOST 4096u
// The most entries one record's row writes: its own and its conversion's (BUSTER_CHECK_LOWER_TEMPLATE_MOST).
#define BUSTER_NODE_MACHINE_ROW_MOST 8u
// The most entries a segment after a record's row writes (node_machine_hook): a test of its value and a branch.
#define BUSTER_NODE_MACHINE_HOOK_MOST 4u
// The most entries one row writes before X tests it against BUSTER_NODE_MACHINE_ROW_MOST and refuses it (12: a record's
// own 8 and its conversion's 4).
#define BUSTER_NODE_MACHINE_RECORD_MOST 32u
// The tape's entries: every row and segment an expression can keep, and the row that X refuses. A record ends its own
// row and at most one segment after it: a consumer's (BUSTER_NODE_MACHINE_HOOK_MOST) or an update's first row (an
// update and its destination are two records, so at most half the records have one).
#define BUSTER_NODE_MACHINE_TAPE_MOST                                                                                            \
    (BUSTER_NODE_MACHINE_MOST * (BUSTER_NODE_MACHINE_ROW_MOST + BUSTER_NODE_MACHINE_HOOK_MOST) + BUSTER_NODE_MACHINE_MOST / 2u * BUSTER_NODE_MACHINE_ROW_MOST + \
     BUSTER_NODE_MACHINE_RECORD_MOST)
// A record's state in C: reached and evaluated by its consumer's template.
#define BUSTER_NODE_MACHINE_EVALUATED 0x01u
// Its consumer's entries follow its row (NodeMachine.hooks): a logical operator's or a conditional's test of its
// condition, and the branch past its second operand or first arm.
#define BUSTER_NODE_MACHINE_AFTER_CONDITION 0x02u
// A conditional's jump from the end of its first arm to its end, and its second arm's label.
#define BUSTER_NODE_MACHINE_AFTER_ARM 0x04u
// An update's destination, after whose entries (none, for a local's or a global's by name) the update's first row goes
// (node_machine_record_update).
#define BUSTER_NODE_MACHINE_SPLIT 0x08u
// A temporary's entry on the tape (an update's of an address, after its postfix): its number among the expression's
// temporaries, in the order X makes them, with this bit. The lowering's are locals it adds (check_lower_temporary).
#define BUSTER_NODE_MACHINE_TEMPORARY (UINT64_C(1) << 63)

// A type's word's flags (NodeMachine.type_words): no aligned typedef's wrapper (type_strip_aligned is the type itself), and
// type_is_valid.
#define BUSTER_NODE_MACHINE_WORD_PLAIN 0x01000000u
#define BUSTER_NODE_MACHINE_WORD_VALID 0x02000000u

// The machine's templates past the node templates' (CheckLowerTemplate): check_lower_logical's, a logical operator's and a
// conditional's, and a call's. A conditional whose condition is known takes CHECK_LOWER_TEMPLATE_PASS: its arm's entries alone,
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
    // A plain call: its CALL (node_machine_call_own).
    NODE_MACHINE_TEMPLATE_CALL,
    // An update (check_lower_update's): its second row (node_machine_update_last); its plan is node_machine_record_update.
    NODE_MACHINE_TEMPLATE_UPDATE,
} NodeMachineTemplate;

// A call's and an update's templates (node_machine_record_template), the machine's own, past #1048's CheckLowerTemplate
// values.
#define BUSTER_NODE_MACHINE_TEMPLATE_CALL ((CheckLowerTemplate)NODE_MACHINE_TEMPLATE_CALL)
#define BUSTER_NODE_MACHINE_TEMPLATE_UPDATE ((CheckLowerTemplate)NODE_MACHINE_TEMPLATE_UPDATE)

// Why the machine refuses an expression: the first sweep's reason that stopped it.
typedef enum NodeMachineRefusal
{
    NODE_MACHINE_TAKEN,
    // Its nodes aren't one range of the graph with every operand inside it, or past BUSTER_NODE_MACHINE_MOST.
    NODE_MACHINE_REFUSED_SHAPE,
    // An operator without a code of the machine's (a builtin, a statement expression...).
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
    // A call whose callee has semantics of its own (node_machine_callee_refused).
    NODE_MACHINE_REFUSED_CALLEE,
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
    [NODE_MACHINE_REFUSED_CALLEE] = "callee",
};

// An update's plan (`++x`, `--x`, `x++`, `x--` or `x op= y`), check_lower_update's choices read from the records: its
// destination's type (the lvalue's), its computation's (its operand 0's conversion, which check_lower_update converts
// the old value through) and operand 1's target (its conversion's type, or long for a pointer's); a direct update's
// local (its object) or global (its symbol), its read's and its store's kinds; and the constant its operation takes in
// place of operand 1, an increment's one or an unsigned power of two's count or mask. The updates' functions are out of
// line: about one evaluated node in forty is an update, and the sweeps' loops stay as they are for the others.
typedef struct NodeMachineUpdate NodeMachineUpdate;
struct NodeMachineUpdate
{
    uint64_t destination;
    uint64_t constant;
    uint64_t stride;
    TypeId stored;
    TypeId calculated;
    TypeId right;
    uint8_t stored_class;
    uint8_t calculated_class;
    uint8_t computing;
    uint8_t kind;
    uint8_t get;
    uint8_t store;
    bool increment;
    bool postfix;
    bool subtract;
    bool pointer;
    bool power;
    bool direct;
};

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
    // flags) and a member's offset in bits. C and X read them through the record accessors alone.
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
    // Each record's least record of its subtree (itself for a leaf), for a call's row (node_machine_call_own).
    uint32_t* lowest;
    // A call's arguments: its first in the expression's list (`arguments`, their records) and its count; and each
    // argument's conversion, by its place in the list. `argument_values` and `verified` are an action's arguments and
    // --verify's conversions, for one call at a time.
    uint32_t* argument_first;
    uint32_t* argument_counts;
    uint32_t* arguments;
    CheckConversion* argument_conversions;
    uint32_t argument_total;
    CheckValue* argument_values;
    CheckConversion* verified;
    // The unit's names of the functions that return twice and the jmp_buf's tag (check_lower_twice_names), for
    // node_machine_callee_refused.
    IdentId twice_names[BUSTER_CHECK_LOWER_TWICE_NAMES];
    IdentId twice_tag;
    // C's: each record's state, its context (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS and _DISCARD), the type its consumer
    // converts it to, its template (CheckLowerTemplate, or past it NodeMachineTemplate) and the evaluated calls among its
    // ancestors; and the record C is classifying, its operands' consumer. Where its state has
    // BUSTER_NODE_MACHINE_AFTER_CONDITION or _AFTER_ARM, `hooks` is its consumer's record, whose segment follows its row;
    // where its template is NODE_MACHINE_TEMPLATE_BRANCH, `label_bases` is the first of its two labels, counted from the
    // expression's first in the order the tasks number them (node_machine_number_labels), of the expression's
    // `label_count`. `stack` is the numbering's: the labeled records C met, then their subtrees' first records.
    uint8_t* states;
    uint8_t* contexts;
    TypeId* targets;
    uint8_t* templates;
    uint32_t* hooks;
    uint32_t* label_bases;
    uint32_t* stack;
    uint32_t label_count;
    uint32_t* call_depths;
    uint32_t classified;
    // For an update and its destination (BUSTER_NODE_MACHINE_SPLIT), its plan among the expression's `update_count`
    // `updates` (node_machine_record_update).
    uint32_t* consumers;
    NodeMachineUpdate* updates;
    uint32_t update_count;
    // X's tape, BUSTER_NODE_MACHINE_TAPE_MOST entries: the expression's entries, a local's holding its object, a branch's
    // its label, a call's its row relative to the expression's first and a temporary its number (`temporary_count` of
    // them); each label's entry, its place in the tape; per record, the evaluated calls before it in postorder; and per
    // row, its call's record, `call_count` rows.
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* tape;
    uint32_t* label_entries;
    uint32_t entry_count;
    uint32_t* calls_at;
    uint32_t* call_records;
    uint32_t call_count;
    uint32_t temporary_count;
    // The record T, C or X stopped at, for a refusal's report.
    uint32_t stopped;
};

// The sweeps' files' functions that this file calls: T's table, its types' completion positions and its operands' records
// (c_check_node_machine_type.c), and the three sweeps.
static bool node_machine_tables_initialize(void);
static uint32_t node_machine_complete_at(const TypeTable* types, const CheckGraph* graph, TypeId type);
static uint32_t node_machine_operand(const NodeMachine* machine, uint32_t r, uint32_t k);
static NodeMachineRefusal node_machine_type(NodeMachine* machine, uint32_t first, uint32_t root, uint32_t count);
static NodeMachineRefusal node_machine_contexts(NodeMachine* machine, uint8_t context, TypeId target, uint32_t object);
static NodeMachineRefusal node_machine_expand(NodeMachine* machine, uint32_t object);

// The machine's columns for `program`'s unit, from `arena`: whether there was room.
static bool node_machine_begin(NodeMachine* machine, const CheckProgram* program, bool scalar, bool verify, Arena* arena)
{
    const uint32_t most = BUSTER_NODE_MACHINE_MOST;
    const uint32_t entries = BUSTER_NODE_MACHINE_TAPE_MOST;
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
    machine->lowest = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->argument_first = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->argument_counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->arguments = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->argument_conversions = BUSTER_ARENA_PUSH_ARRAY(arena, CheckConversion, most);
    machine->argument_values = BUSTER_ARENA_PUSH_ARRAY(arena, CheckValue, BUSTER_TIER0_MAX_CALL_ARGUMENTS);
    machine->verified = BUSTER_ARENA_PUSH_ARRAY(arena, CheckConversion, BUSTER_TIER0_MAX_CALL_ARGUMENTS);
    machine->states = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, most);
    machine->contexts = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, most);
    machine->targets = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, most);
    machine->templates = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, most);
    machine->hooks = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->label_bases = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->stack = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * most);
    machine->call_depths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->consumers = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->updates = BUSTER_ARENA_PUSH_ARRAY(arena, NodeMachineUpdate, most / 2);
    machine->kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, entries);
    machine->classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, entries);
    machine->tape = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, entries);
    machine->label_entries = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * most);
    machine->calls_at = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->call_records = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    const bool room = machine->type_words != NULL && machine->type_complete_at != NULL && classes != NULL && facts != NULL && machine->codes != NULL && machine->left != NULL && machine->right != NULL &&
                      machine->values != NULL && machine->words != NULL && machine->facts != NULL &&
                      machine->value_classes != NULL && machine->converted != NULL &&
                      machine->conversion_flags != NULL && machine->member_offsets != NULL && machine->states != NULL && machine->contexts != NULL &&
                      machine->targets != NULL && machine->templates != NULL && machine->hooks != NULL && machine->label_bases != NULL && machine->stack != NULL &&
                      machine->kinds != NULL && machine->classes != NULL && machine->tape != NULL && machine->label_entries != NULL &&
                      machine->lowest != NULL && machine->argument_first != NULL && machine->argument_counts != NULL && machine->arguments != NULL &&
                      machine->argument_conversions != NULL && machine->argument_values != NULL && machine->verified != NULL && machine->call_depths != NULL &&
                      machine->calls_at != NULL && machine->call_records != NULL && machine->consumers != NULL && machine->updates != NULL;
    if (room)
    {
        (void)check_lower_twice_names(program, machine->twice_names, &machine->twice_tag);
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

// Record `r`'s graph node.
static inline uint32_t node_machine_node(const NodeMachine* machine, uint32_t r)
{
    return r + 1 == machine->count ? machine->root : machine->first + r;
}

// The record accessors: what C and X read of record `r`, which T wrote, and C's own columns, read and written. Each is
// one load or store of today's columns; a record layout of its own (the design's 28 bytes) implements the same functions
// and leaves C and X as they are. A record's operands are records below it, so that C, going down, and X, going up, read
// only records T has written.

// Its operator.
static inline CheckOp node_machine_record_op(const NodeMachine* machine, uint32_t r)
{
    return (CheckOp)machine->codes[r];
}

// Its operand 0's record and operand 1's: UINT32_MAX for none.
static inline uint32_t node_machine_record_left(const NodeMachine* machine, uint32_t r)
{
    return machine->left[r];
}

static inline uint32_t node_machine_record_right(const NodeMachine* machine, uint32_t r)
{
    return machine->right[r];
}

// Its operand 2's record (a conditional's second arm), from the graph: UINT32_MAX for none.
static inline uint32_t node_machine_record_third(const NodeMachine* machine, uint32_t r)
{
    return node_machine_operand(machine, r, 2);
}

// Its value's type, its category (an lvalue, an rvalue or a function designator) and its BUSTER_CHECK_* flags.
static inline TypeId node_machine_record_type(const NodeMachine* machine, uint32_t r)
{
    return machine->values[r].type;
}

static inline CheckCategory node_machine_record_category(const NodeMachine* machine, uint32_t r)
{
    return machine->values[r].category;
}

static inline uint32_t node_machine_record_flags(const NodeMachine* machine, uint32_t r)
{
    return machine->values[r].flags;
}

// Its value's constant: its kind, an integer's low 64 bits, a floating constant's value, and an address constant's
// object (the object a value designates, for any other) and offset in bytes.
static inline CheckConstantKind node_machine_record_constant_kind(const NodeMachine* machine, uint32_t r)
{
    return machine->values[r].constant_kind;
}

static inline uint64_t node_machine_record_integer(const NodeMachine* machine, uint32_t r)
{
    return (uint64_t)machine->values[r].integer;
}

static inline long double node_machine_record_floating(const NodeMachine* machine, uint32_t r)
{
    return machine->values[r].floating;
}

static inline uint32_t node_machine_record_object(const NodeMachine* machine, uint32_t r)
{
    return machine->values[r].object;
}

static inline int64_t node_machine_record_offset(const NodeMachine* machine, uint32_t r)
{
    return machine->values[r].offset;
}

// Whether its value's truth is known, check_action_truth's answer for its value, and into `truth` that truth.
static inline bool node_machine_record_truth(const NodeMachine* machine, uint32_t r, bool* truth)
{
    return check_action_truth(machine->types, machine->values + r, truth);
}

// Its value's fault, and whether it is an overload set's.
static inline CheckFault node_machine_record_fault(const NodeMachine* machine, uint32_t r)
{
    return machine->values[r].fault;
}

static inline bool node_machine_record_overloaded(const NodeMachine* machine, uint32_t r)
{
    return machine->values[r].overload_head != 0;
}

// Its value's type's facts (BUSTER_CHECK_LOWER_TYPE_*) and its class on the stack (an aggregate's is its address's,
// BUSTER_TYPED_CLASS_U64).
static inline uint8_t node_machine_record_facts(const NodeMachine* machine, uint32_t r)
{
    return machine->facts[r];
}

static inline uint8_t node_machine_record_class(const NodeMachine* machine, uint32_t r)
{
    return machine->value_classes[r];
}

// Its operand `k`'s (0 or 1) conversion: the type the operator converts it to (0 for none) and the conversion's
// BUSTER_CHECK_CONVERT_* flags.
static inline TypeId node_machine_record_converted(const NodeMachine* machine, uint32_t r, uint32_t k)
{
    return machine->converted[2 * r + k];
}

static inline uint32_t node_machine_record_conversion_flags(const NodeMachine* machine, uint32_t r, uint32_t k)
{
    return machine->conversion_flags[2 * r + k];
}

// A member's offset in its record, in bits.
static inline uint64_t node_machine_record_member_offset(const NodeMachine* machine, uint32_t r)
{
    return machine->member_offsets[r];
}

// The least record of its subtree: itself for a leaf, else its operands' and a call's arguments' least.
static inline uint32_t node_machine_record_lowest(const NodeMachine* machine, uint32_t r)
{
    return machine->lowest[r];
}

// A call's arguments: their count, argument `i`'s record, and its conversion (the type the call converts it to and the
// conversion's BUSTER_CHECK_CONVERT_* flags, as the stream's argument_conversions hold them).
static inline uint32_t node_machine_record_argument_count(const NodeMachine* machine, uint32_t r)
{
    return machine->argument_counts[r];
}

static inline uint32_t node_machine_record_argument(const NodeMachine* machine, uint32_t r, uint32_t i)
{
    return machine->arguments[machine->argument_first[r] + i];
}

static inline TypeId node_machine_record_argument_converted(const NodeMachine* machine, uint32_t r, uint32_t i)
{
    return machine->argument_conversions[machine->argument_first[r] + i].type;
}

static inline uint32_t node_machine_record_argument_conversion_flags(const NodeMachine* machine, uint32_t r, uint32_t i)
{
    return machine->argument_conversions[machine->argument_first[r] + i].flags;
}

// Its whole value into `value`, every field as the action would give it: the shadow's comparison with the stream
// (c_check_node_census.c) reads it, not C or X.
static inline void node_machine_record_value(const NodeMachine* machine, uint32_t r, CheckValue* value)
{
    check_value_copy(value, machine->values + r);
}

// C's columns. Whether its consumer evaluates it (none is, until C reaches it), and its whole state, which says too
// whether its consumer's segment follows its row (BUSTER_NODE_MACHINE_AFTER_CONDITION, _AFTER_ARM); its context
// (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS and _DISCARD), the type its consumer converts it to and its template; that
// consumer, where its state says; and a labeled record's first label.
static inline bool node_machine_record_evaluated(const NodeMachine* machine, uint32_t r)
{
    return (machine->states[r] & BUSTER_NODE_MACHINE_EVALUATED) != 0;
}

static inline uint8_t node_machine_record_state(const NodeMachine* machine, uint32_t r)
{
    return machine->states[r];
}

static inline uint8_t node_machine_record_context(const NodeMachine* machine, uint32_t r)
{
    return machine->contexts[r];
}

static inline TypeId node_machine_record_target(const NodeMachine* machine, uint32_t r)
{
    return machine->targets[r];
}

// The evaluated calls among its ancestors, whose rows the lowering numbers before its own.
static inline uint32_t node_machine_record_call_depth(const NodeMachine* machine, uint32_t r)
{
    return machine->call_depths[r];
}

static inline CheckLowerTemplate node_machine_record_template(const NodeMachine* machine, uint32_t r)
{
    return (CheckLowerTemplate)machine->templates[r];
}

static inline uint32_t node_machine_record_hook(const NodeMachine* machine, uint32_t r)
{
    return machine->hooks[r];
}

static inline uint32_t node_machine_record_label_base(const NodeMachine* machine, uint32_t r)
{
    return machine->label_bases[r];
}

// An update's plan, at the update's record and at its destination's (BUSTER_NODE_MACHINE_SPLIT).
static inline const NodeMachineUpdate* node_machine_record_update(const NodeMachine* machine, uint32_t r)
{
    return machine->updates + machine->consumers[r];
}

// C's writes: the first `count` records not evaluated, then a record evaluated, its context, its target and its template;
// an evaluated record's consumer `consumer` whose segment `hook` follows its row; and a labeled record's first label.
static inline void node_machine_records_set_unevaluated(NodeMachine* machine, uint32_t count)
{
    memset(machine->states, 0, count);
}

static inline void node_machine_record_set_evaluated(NodeMachine* machine, uint32_t r)
{
    machine->states[r] = BUSTER_NODE_MACHINE_EVALUATED;
}

static inline void node_machine_record_set_context(NodeMachine* machine, uint32_t r, uint8_t context)
{
    machine->contexts[r] = context;
}

static inline void node_machine_record_set_target(NodeMachine* machine, uint32_t r, TypeId target)
{
    machine->targets[r] = target;
}

static inline void node_machine_record_set_template(NodeMachine* machine, uint32_t r, CheckLowerTemplate kind)
{
    machine->templates[r] = (uint8_t)kind;
}

static inline void node_machine_record_set_hook(NodeMachine* machine, uint32_t r, uint32_t consumer, uint8_t hook)
{
    machine->states[r] = BUSTER_NODE_MACHINE_EVALUATED | hook;
    machine->hooks[r] = consumer;
}

static inline void node_machine_record_set_label_base(NodeMachine* machine, uint32_t r, uint32_t base)
{
    machine->label_bases[r] = base;
}

static inline void node_machine_record_set_call_depth(NodeMachine* machine, uint32_t r, uint32_t depth)
{
    machine->call_depths[r] = depth;
}

// Update record `r`'s plan, the expression's `k`th, and its destination `destination`'s: the update's first row follows
// the destination's entries.
static inline void node_machine_record_set_update(NodeMachine* machine, uint32_t r, uint32_t destination, uint32_t k)
{
    machine->states[destination] |= BUSTER_NODE_MACHINE_SPLIT;
    machine->consumers[destination] = k;
    machine->consumers[r] = k;
}

// Whether `object` is a local of the function: an object without a symbol (a parameter's, an automatic object's), which
// the function's local numbers; one with a symbol is a global's.
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

// What T, C and X ask of a call's callee, record `a`. Its function type, by check_action_call's steps from its value's
// type (a function designator's, or a pointer's pointee), or 0 for none.
static TypeId node_machine_callee_function(const NodeMachine* machine, uint32_t a)
{
    const TypeTable* types = machine->types;
    TypeId function = type_strip_aligned(types, node_machine_record_type(machine, a));
    if (type_kind(types, function) == BUSTER_TYPE_KIND_POINTER)
    {
        function = type_strip_aligned(types, type_element(types, function));
    }
    return type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION ? function : 0;
}

// The object it designates where the lowering calls it directly (check_lower_call_body's `direct`): a function with a
// symbol of its own, its address a constant without an offset or an effect; 0 for a call through a pointer, whose callee
// the lowering evaluates.
static uint32_t node_machine_direct_callee(const NodeMachine* machine, uint32_t a)
{
    const CheckProgram* program = machine->program;
    const uint32_t object = node_machine_record_object(machine, a);
    const bool direct = node_machine_record_category(machine, a) != CHECK_LVALUE && node_machine_record_constant_kind(machine, a) == CHECK_CONSTANT_ADDRESS &&
                        object != 0 && node_machine_record_offset(machine, a) == 0 && object < program->object_count && program->object_symbols[object] != 0 &&
                        (node_machine_record_flags(machine, a) & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    return direct ? object : 0;
}

// What a call record `r`'s row of the function's calls table holds: the symbol it calls (Tier0Function.call_targets), its
// callee's where it is called directly and 0 through a pointer; and whether it is variadic (call_variadic), its callee's
// prototype's.
static SymbolId node_machine_call_target(const NodeMachine* machine, uint32_t r)
{
    const uint32_t object = node_machine_direct_callee(machine, node_machine_record_left(machine, r));
    return object != 0 ? machine->program->object_symbols[object] : 0;
}

static bool node_machine_call_variadic(const NodeMachine* machine, uint32_t r)
{
    const TypeId function = node_machine_callee_function(machine, node_machine_record_left(machine, r));
    return function != 0 && (machine->types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
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
