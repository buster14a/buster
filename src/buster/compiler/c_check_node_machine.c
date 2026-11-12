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
//   then its conversion's, in the evaluation order the tasks write them in (postorder: no template writes before or
//   between its operands). A local's entries hold its object, which the function's local numbers in step 2's splice; a
//   global's its symbol.
//
// The set is the census's (c_check_node_census.c) and, within it, the templates': a value read, an address, a folded
// constant, `&x`, `*p`, a member, a cast, a conversion, the unary and binary scalar operators, a pointer plus an
// integer, a subscript, the comma and an assignment to a local, a global or an address, each of a scalar class I8 to
// F64. Calls, `&&`, `||`, `?:`, the increments and the compound assignments are not yet the machine's: it refuses them.
//
// This file is the machine's state and its records' interface; main.c includes the three sweeps' files after it, in
// the checker unit, before baked.c:
//
// - NodeMachine: the unit's state (node_machine_begin) and the current expression's columns, which T writes, C reads and
//   writes its own of, and X reads, writing the tape.
// - The type words (node_machine_word, node_machine_class, node_machine_facts, node_machine_aggregate): one load a type.
// - The record accessors (node_machine_record_*): every read of a record by C, X and the shadow's comparison, and C's
//   writes of its own columns, go through them, so that a record's layout (T's columns, the design's: a value's type,
//   flags, object, standing and payload, and the rare value whole on the side) changes here and in T alone.
// - node_machine_local and node_machine_address_takes: what C and X ask of an object.
// - node_machine_run: the three sweeps, one after another.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_INCLUDED

// The most records an expression the machine takes has (docs/node-machine.md, "The records and the tapes").
#define BUSTER_NODE_MACHINE_MOST 4096u
// The most entries one record's row writes: its own and its conversion's (BUSTER_CHECK_LOWER_TEMPLATE_MOST).
#define BUSTER_NODE_MACHINE_ROW_MOST 8u
// A record's state in C: reached and evaluated by its consumer's template.
#define BUSTER_NODE_MACHINE_EVALUATED 0x01u

// A type's word's flags (NodeMachine.type_words): no aligned typedef's wrapper (type_strip_aligned is the type itself), and
// type_is_valid.
#define BUSTER_NODE_MACHINE_WORD_PLAIN 0x01000000u
#define BUSTER_NODE_MACHINE_WORD_VALID 0x02000000u

// A record's standing (NodeMachine.standings): its value's category (bits 0 and 1) and constant kind (bits 2 to 4), the
// field its payload holds (bits 5 and 6: its integer, its offset or its floating value, the others zero), and
// BUSTER_NODE_MACHINE_WIDE where the columns don't hold its value whole and `wides` does.
#define BUSTER_NODE_MACHINE_STANDING(category, constant_kind) ((uint8_t)((uint32_t)(category) | (uint32_t)(constant_kind) << 2))
#define BUSTER_NODE_MACHINE_FIELD 0x60u
#define BUSTER_NODE_MACHINE_OFFSET 0x20u
#define BUSTER_NODE_MACHINE_FLOATING 0x40u
#define BUSTER_NODE_MACHINE_WIDE 0x80u

// Why the machine refuses an expression: the first sweep's reason that stopped it.
typedef enum NodeMachineRefusal
{
    NODE_MACHINE_TAKEN,
    // Its nodes aren't one range of the graph with every operand inside it, or past BUSTER_NODE_MACHINE_MOST.
    NODE_MACHINE_REFUSED_SHAPE,
    // An operator without a code of the machine's (a call, a logical operator, a conditional, an increment, a compound
    // assignment, a builtin...).
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
    // T's columns (docs/node-machine.md, "The records and the tapes"), which C and X read through the record accessors
    // alone: each record's operator and its first two operands' records (UINT32_MAX for none), and its value, which
    // node_machine_record_value makes whole: its type, its flags, its object, its standing (BUSTER_NODE_MACHINE_STANDING and
    // the payload's field) and its payload: its integer's low 64 bits, its offset, or its floating value's significand,
    // with its sign and exponent in `exponents`. A value the columns can't hold whole (a fault, an overload set, a
    // bit-field's width, an integer past 64 bits, two of the payload's fields) is in `wides`, and its payload is its index
    // there. Then its type's word and facts and its class on the stack (an aggregate's is its address's), its operands'
    // conversions (their types and flags) and a member's offset in bits.
    uint8_t* codes;
    uint32_t* left;
    uint32_t* right;
    TypeId* value_types;
    uint32_t* value_flags;
    uint32_t* objects;
    uint64_t* payloads;
    uint8_t* standings;
    uint16_t* exponents;
    CheckValue* wides;
    uint32_t wide_count;
    uint32_t* words;
    uint8_t* facts;
    uint8_t* value_classes;
    TypeId* converted;
    uint32_t* conversion_flags;
    uint64_t* member_offsets;
    // C's: each record's state, its context (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS and _DISCARD), the type its consumer
    // converts it to, and its template (CheckLowerTemplate).
    uint8_t* states;
    uint8_t* contexts;
    TypeId* targets;
    uint8_t* templates;
    // X's tape: the expression's entries, a local's holding its object.
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* tape;
    uint32_t entry_count;
    // The record T, C or X stopped at, for a refusal's report.
    uint32_t stopped;
};

// The sweeps' files' functions that node_machine_begin and node_machine_run call: T's table and its types' completion
// positions (c_check_node_machine_type.c), and the three sweeps.
static bool node_machine_tables_initialize(void);
static uint32_t node_machine_complete_at(const TypeTable* types, const CheckGraph* graph, TypeId type);
static NodeMachineRefusal node_machine_type(NodeMachine* machine, uint32_t first, uint32_t root, uint32_t count);
static NodeMachineRefusal node_machine_contexts(NodeMachine* machine, uint8_t context, TypeId target, uint32_t object);
static NodeMachineRefusal node_machine_expand(NodeMachine* machine, uint32_t object);

// A column of `count` elements of `bytes` each from `arena`, starting `line` (below 64) cache lines past a 4 KB boundary.
// An expression's records are its columns' first elements: columns that all began on such a boundary, as arrays of 4,096
// elements do one after another, would put those in one set of the first-level cache, more lines than its ways, and
// each record's fields 4 KB apart, which the load-store unit takes for aliases of each other's stores. On the 9700X, on
// sqlite3.c, the three sweeps took 285 cycles a node so with T's columns and 154 staggered, and 169 so with a whole value
// a record and 153 staggered (docs/node-machine.md, "T's records as columns").
static void* node_machine_column(Arena* arena, uint64_t count, uint64_t bytes, uint32_t line)
{
    uint8_t* column = arena_push_array(arena, count * bytes + 4096, 1, 4096);
    return column != NULL ? column + (uint64_t)line * 64 : NULL;
}

// The machine's columns for `program`'s unit, from `arena`: whether there was room. Each record column starts two cache
// lines past the one before modulo 4 KB (node_machine_column).
static bool node_machine_begin(NodeMachine* machine, const CheckProgram* program, bool scalar, bool verify, Arena* arena)
{
    const uint32_t most = BUSTER_NODE_MACHINE_MOST;
    const uint32_t entries = BUSTER_NODE_MACHINE_MOST * BUSTER_NODE_MACHINE_ROW_MOST;
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
    machine->codes = node_machine_column(arena, most, sizeof(uint8_t), 0);
    machine->left = node_machine_column(arena, most, sizeof(uint32_t), 2);
    machine->right = node_machine_column(arena, most, sizeof(uint32_t), 4);
    machine->value_types = node_machine_column(arena, most, sizeof(TypeId), 6);
    machine->value_flags = node_machine_column(arena, most, sizeof(uint32_t), 8);
    machine->objects = node_machine_column(arena, most, sizeof(uint32_t), 10);
    machine->payloads = node_machine_column(arena, most, sizeof(uint64_t), 12);
    machine->standings = node_machine_column(arena, most, sizeof(uint8_t), 14);
    machine->exponents = node_machine_column(arena, most, sizeof(uint16_t), 16);
    machine->wides = node_machine_column(arena, most, sizeof(CheckValue), 18);
    machine->words = node_machine_column(arena, most, sizeof(uint32_t), 20);
    machine->facts = node_machine_column(arena, most, sizeof(uint8_t), 22);
    machine->value_classes = node_machine_column(arena, most, sizeof(uint8_t), 24);
    machine->converted = node_machine_column(arena, 2 * most, sizeof(TypeId), 26);
    machine->conversion_flags = node_machine_column(arena, 2 * most, sizeof(uint32_t), 28);
    machine->member_offsets = node_machine_column(arena, most, sizeof(uint64_t), 30);
    machine->states = node_machine_column(arena, most, sizeof(uint8_t), 32);
    machine->contexts = node_machine_column(arena, most, sizeof(uint8_t), 34);
    machine->targets = node_machine_column(arena, most, sizeof(TypeId), 36);
    machine->templates = node_machine_column(arena, most, sizeof(uint8_t), 38);
    machine->kinds = node_machine_column(arena, entries, sizeof(uint8_t), 40);
    machine->classes = node_machine_column(arena, entries, sizeof(uint8_t), 42);
    machine->tape = node_machine_column(arena, entries, sizeof(uint64_t), 44);
    const bool room = machine->type_words != NULL && machine->type_complete_at != NULL && classes != NULL && facts != NULL && machine->codes != NULL && machine->left != NULL && machine->right != NULL &&
                      machine->value_types != NULL && machine->value_flags != NULL && machine->objects != NULL && machine->payloads != NULL &&
                      machine->standings != NULL && machine->exponents != NULL && machine->wides != NULL && machine->words != NULL && machine->facts != NULL &&
                      machine->value_classes != NULL && machine->converted != NULL &&
                      machine->conversion_flags != NULL && machine->member_offsets != NULL && machine->states != NULL && machine->contexts != NULL &&
                      machine->targets != NULL && machine->templates != NULL && machine->kinds != NULL && machine->classes != NULL && machine->tape != NULL;
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

// Record `r`'s graph node.
static inline uint32_t node_machine_node(const NodeMachine* machine, uint32_t r)
{
    return r + 1 == machine->count ? machine->root : machine->first + r;
}

// The record accessors: what C and X read of record `r`, which T wrote, and C's own columns, read and written. Each is
// one load or store of the columns; a rare field's tests the standing first, and reads a wide value's. A record's
// operands are records below it, so that C, going down, and X, going up, read only records T has written.

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

// Its value's type, its category (an lvalue, an rvalue or a function designator) and its BUSTER_CHECK_* flags.
static inline TypeId node_machine_record_type(const NodeMachine* machine, uint32_t r)
{
    return machine->value_types[r];
}

static inline CheckCategory node_machine_record_category(const NodeMachine* machine, uint32_t r)
{
    return (CheckCategory)(machine->standings[r] & 0x03u);
}

static inline uint32_t node_machine_record_flags(const NodeMachine* machine, uint32_t r)
{
    return machine->value_flags[r];
}

// Its wide value (NodeMachine.wides), or null where its columns hold its value whole: what the fields below read where
// the columns don't have them.
static inline const CheckValue* node_machine_record_wide(const NodeMachine* machine, uint32_t r)
{
    return (machine->standings[r] & BUSTER_NODE_MACHINE_WIDE) != 0 ? machine->wides + machine->payloads[r] : NULL;
}

// Its whole value into `value`, every field as the action would give it: its wide value, or its columns' fields with
// every other field zero. The action's operands, --verify's twin and the shadow's comparison with the stream
// (c_check_node_census.c) read it, not C or X.
static inline __attribute__((always_inline)) void node_machine_record_value(const NodeMachine* machine, uint32_t r, CheckValue* value)
{
    const CheckValue* wide = node_machine_record_wide(machine, r);
    if (wide != NULL)
    {
        check_value_copy(value, wide);
    }
    else
    {
        const uint8_t field = machine->standings[r] & BUSTER_NODE_MACHINE_FIELD;
        const uint64_t payload = machine->payloads[r];
        const bool floating = field == BUSTER_NODE_MACHINE_FLOATING;
        *value = (CheckValue){
            .type = machine->value_types[r],
            .category = node_machine_record_category(machine, r),
            .flags = machine->value_flags[r],
            .constant_kind = (CheckConstantKind)((machine->standings[r] >> 2) & 0x07u),
            .integer = field == 0 ? payload : 0,
            .object = machine->objects[r],
            .offset = field == BUSTER_NODE_MACHINE_OFFSET ? (int64_t)payload : 0,
        };
        value->floating_storage[0] = floating ? payload : 0;
        value->floating_storage[1] = floating ? machine->exponents[r] : 0;
    }
}

// Its value's constant: its kind, an integer's low 64 bits, a floating constant's value, and an address constant's
// object (the object a value designates, for any other) and offset in bytes.
static inline CheckConstantKind node_machine_record_constant_kind(const NodeMachine* machine, uint32_t r)
{
    return (CheckConstantKind)((machine->standings[r] >> 2) & 0x07u);
}

static inline uint64_t node_machine_record_integer(const NodeMachine* machine, uint32_t r)
{
    const CheckValue* wide = node_machine_record_wide(machine, r);
    return wide != NULL ? (uint64_t)wide->integer : (machine->standings[r] & BUSTER_NODE_MACHINE_FIELD) == 0 ? machine->payloads[r] : 0;
}

static inline long double node_machine_record_floating(const NodeMachine* machine, uint32_t r)
{
    CheckValue value;
    node_machine_record_value(machine, r, &value);
    return value.floating;
}

static inline uint32_t node_machine_record_object(const NodeMachine* machine, uint32_t r)
{
    return machine->objects[r];
}

static inline int64_t node_machine_record_offset(const NodeMachine* machine, uint32_t r)
{
    const CheckValue* wide = node_machine_record_wide(machine, r);
    return wide != NULL ? wide->offset : (machine->standings[r] & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_OFFSET ? (int64_t)machine->payloads[r] : 0;
}

// Its value's fault, and whether it is an overload set's: a wide value's alone.
static inline CheckFault node_machine_record_fault(const NodeMachine* machine, uint32_t r)
{
    const CheckValue* wide = node_machine_record_wide(machine, r);
    return wide != NULL ? wide->fault : CHECK_FAULT_NONE;
}

static inline bool node_machine_record_overloaded(const NodeMachine* machine, uint32_t r)
{
    const CheckValue* wide = node_machine_record_wide(machine, r);
    return wide != NULL && wide->overload_head != 0;
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

// C's columns. Whether its consumer evaluates it (none is, until C reaches it), its context
// (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS and _DISCARD), the type its consumer converts it to and its template.
static inline bool node_machine_record_evaluated(const NodeMachine* machine, uint32_t r)
{
    return machine->states[r] == BUSTER_NODE_MACHINE_EVALUATED;
}

static inline uint8_t node_machine_record_context(const NodeMachine* machine, uint32_t r)
{
    return machine->contexts[r];
}

static inline TypeId node_machine_record_target(const NodeMachine* machine, uint32_t r)
{
    return machine->targets[r];
}

static inline CheckLowerTemplate node_machine_record_template(const NodeMachine* machine, uint32_t r)
{
    return (CheckLowerTemplate)machine->templates[r];
}

// C's writes: the first `count` records not evaluated, then a record evaluated, its context, its target and its template.
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
