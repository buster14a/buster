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
// - The type words (node_machine_word, node_machine_class, node_machine_facts): one load a type.
// - The record accessors (node_machine_record_*): every read of a record by C, X and the shadow's comparison, and C's
//   writes of its own columns, go through them, so that a record's layout (T's columns, the design's: a value's type,
//   flags, object, standing and payload, and the rare value whole on the side) changes here and in T alone.
// - node_machine_local and node_machine_address_takes: what C and X ask of an object; node_machine_callee_function,
//   node_machine_direct_callee, node_machine_call_target and node_machine_call_variadic: what T, C, X and the shadow ask
//   of a call.
// - node_machine_run: the three sweeps, one after another.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_INCLUDED

// The most records an expression the machine takes has (docs/node-machine.md, "The records and the tapes").
#define BUSTER_NODE_MACHINE_MOST 4096u
// The target features with which the lowering fuses a product and a sum (check_lower_contract_plan's): NodeMachine.fuses.
#define BUSTER_NODE_MACHINE_FUSED (BUSTER_X86_FEATURE(FMA) | BUSTER_X86_FEATURE(AVX512F))
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
// A value that an operator evaluated as branches tests (#2890): after its row, its test and a branch to its target, the
// label of the record its hook names (NodeMachine.hooks) or, for BUSTER_NODE_MACHINE_NO_OWNER, the control's own,
// taken where its truth is BUSTER_NODE_MACHINE_BRANCH_TRUE's (set: where it holds).
#define BUSTER_NODE_MACHINE_AFTER_BRANCH 0x10u
#define BUSTER_NODE_MACHINE_BRANCH_TRUE 0x20u
// An operator evaluated as branches (NODE_MACHINE_TEMPLATE_BRANCHES): a `!`, `&&` or `||` under a condition, or a
// statement's CONDITION over one. It makes no value; its target and its sense are kept as a tested value's are.
#define BUSTER_NODE_MACHINE_BRANCHED 0x40u
#define BUSTER_NODE_MACHINE_NO_OWNER UINT32_MAX
// A temporary's entry on the tape (an update's of an address, after its postfix): its number among the expression's
// temporaries, in the order X makes them, with this bit. The lowering's are locals it adds (check_lower_temporary).
#define BUSTER_NODE_MACHINE_TEMPORARY (UINT64_C(1) << 63)
// A branch's entry on the tape whose target is its control's (a statement's condition that is branches): a label
// outside the expression, which the lowering's control numbered before the expression's own and step 2's splice gives.
#define BUSTER_NODE_MACHINE_CONTROL_LABEL (UINT64_C(1) << 62)
// The root's context past the lowering's own bits (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS and _DISCARD), a statement's
// CONDITION's alone: its control branches where the condition holds (a do statement's, CHECK_CONTROL_BRANCH_TRUE), not
// where it doesn't. C reads it where the condition is branches, and keeps it out of the root's context.
#define BUSTER_NODE_MACHINE_ROOT_BRANCH_TRUE 0x80u

// A type's word's flags (NodeMachine.type_words): no aligned typedef's wrapper (type_strip_aligned is the type itself), and
// type_is_valid.
#define BUSTER_NODE_MACHINE_WORD_PLAIN 0x01000000u
#define BUSTER_NODE_MACHINE_WORD_VALID 0x02000000u
// And its six bits from BUSTER_NODE_MACHINE_WORD_TESTS_SHIFT: what the census's tests ask of a value's type at every
// record (c_check_node_census.c's node_census_type_bits_make, which follows this file in the unit), less the bits they
// ask of a conversion's type alone. In order: a type the machine has no kind for (a value in a segment's address space
// among them, #3205), a struct or union, an enumeration, a pointer, a pointer whose pointee may be void (both of a
// function's type and of nullptr_t too, which convert to a pointer and are none: #3201), an array. A record's word has
// them wherever T or the kernel wrote the record, so the tests on the window read a record's type's byte from the
// record (node_tests_window_bytes).
#define BUSTER_NODE_MACHINE_WORD_TESTS_SHIFT 26u
static uint8_t node_census_type_bits_make(const TypeTable* types, TypeId type);

// A record's standing (NodeMachine.standings): its value's category (bits 0 and 1) and constant kind (bits 2 to 4), the
// field its payload holds (bits 5 and 6: its integer, its offset or its floating value, the others zero), and
// BUSTER_NODE_MACHINE_WIDE where the columns don't hold its value whole and `wides` does.
#define BUSTER_NODE_MACHINE_STANDING(category, constant_kind) ((uint8_t)((uint32_t)(category) | (uint32_t)(constant_kind) << 2))
#define BUSTER_NODE_MACHINE_FIELD 0x60u
#define BUSTER_NODE_MACHINE_OFFSET 0x20u
#define BUSTER_NODE_MACHINE_FLOATING 0x40u
#define BUSTER_NODE_MACHINE_WIDE 0x80u

// Whether the columns hold a value whole, of its three storages (its integer `low`, its offset, a floating value): at
// most one is no zero, and an integer constant has none but its integer. The fold of a floating constant to an integer
// leaves the floating storage (`(int)0.5`, whose integer is zero: #3014), so such a constant's payload would be a
// value that is not its own, which a reader of the columns would take for it (the tests' sweep, as the constant's
// value; the kernel's operand, as a value with no storage): `wides` has it, and no reader of the columns sees it.
static inline bool node_machine_whole(CheckConstantKind kind, uint64_t low, uint64_t offset, bool floating)
{
    const uint32_t fields = (low != 0) + (offset != 0) + floating;
    return kind == CHECK_CONSTANT_INTEGER ? fields == (low != 0 ? 1u : 0u) : fields <= 1;
}

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
    // An operator evaluated as branches (check_lower_branch's, BUSTER_NODE_MACHINE_BRANCHED): no entry of its own; an
    // `&&` where true and an `||` where false have one label, past their second operand, which their row places.
    NODE_MACHINE_TEMPLATE_BRANCHES,
} NodeMachineTemplate;

// A call's and an update's templates (node_machine_record_template), the machine's own, past #1048's CheckLowerTemplate
// values.
#define BUSTER_NODE_MACHINE_TEMPLATE_CALL ((CheckLowerTemplate)NODE_MACHINE_TEMPLATE_CALL)
#define BUSTER_NODE_MACHINE_TEMPLATE_UPDATE ((CheckLowerTemplate)NODE_MACHINE_TEMPLATE_UPDATE)

// The argument memo's entries (NodeMachine.argument_memo), as a power of two: a unit's calls convert a few hundred pairs
// of types (docs/node-machine.md, "Calls").
#define BUSTER_NODE_MACHINE_MEMO_BITS 10u
#define BUSTER_NODE_MACHINE_MEMO_ENTRIES (1u << BUSTER_NODE_MACHINE_MEMO_BITS)
// An entry's standing, part of its key: filled, and its key's record an lvalue or an integer constant.
#define BUSTER_NODE_MACHINE_MEMO_FILLED 0x80u
#define BUSTER_NODE_MACHINE_MEMO_LVALUE 0x01u
#define BUSTER_NODE_MACHINE_MEMO_INTEGER 0x02u
// What an entry answers (node_machine_argument_memo): whether node_machine_argument took the key's arguments (else it
// refused them), and whether a hit answers for it (else the rule takes the key's arguments each time).
#define BUSTER_NODE_MACHINE_MEMO_TAKEN 0x01u
#define BUSTER_NODE_MACHINE_MEMO_ANSWERS 0x02u

// One entry of the argument memo (node_machine_argument_memo): its key, an argument's record's type, its standing and its
// parameter's type (0 for a variadic argument), and node_machine_argument's answer for the key, with the argument's
// conversion where it took it.
typedef struct NodeMachineMemo NodeMachineMemo;
struct NodeMachineMemo
{
    TypeId source;
    TypeId parameter;
    TypeId converted;
    uint16_t flags;
    uint8_t standing;
    uint8_t answer;
};

_Static_assert(sizeof(NodeMachineMemo) == 16, "an argument memo entry is sixteen bytes");

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

// T's and C's columns of an expression's records, one array a field, in one block: each record field is then one load
// from one base, where a column's own pointer in NodeMachine would be loaded again after every store, as the build's
// -fno-strict-aliasing lets a store change it (three instructions a field where one serves). Each array starts
// BUSTER_NODE_MACHINE_STAGGER bytes past the one before modulo 4 KB, every array's size being a multiple of 4 KB: an
// expression's records are its arrays' first elements, and arrays that all began on a 4 KB boundary would put those in
// one set of the first-level cache, more lines than its ways, and each record's fields 4 KB apart, which the load-store
// unit takes for aliases of each other's stores (docs/node-machine.md, "T's records as columns").
#define BUSTER_NODE_MACHINE_STAGGER 128u
typedef struct NodeMachineColumns NodeMachineColumns;
struct NodeMachineColumns
{
    uint8_t codes[BUSTER_NODE_MACHINE_MOST];
    uint8_t codes_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t left[BUSTER_NODE_MACHINE_MOST];
    uint8_t left_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t right[BUSTER_NODE_MACHINE_MOST];
    uint8_t right_stagger[BUSTER_NODE_MACHINE_STAGGER];
    TypeId value_types[BUSTER_NODE_MACHINE_MOST];
    uint8_t value_types_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t value_flags[BUSTER_NODE_MACHINE_MOST];
    uint8_t value_flags_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t objects[BUSTER_NODE_MACHINE_MOST];
    uint8_t objects_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint64_t payloads[BUSTER_NODE_MACHINE_MOST];
    uint8_t payloads_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint8_t standings[BUSTER_NODE_MACHINE_MOST];
    uint8_t standings_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint16_t exponents[BUSTER_NODE_MACHINE_MOST];
    uint8_t exponents_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t words[BUSTER_NODE_MACHINE_MOST];
    uint8_t words_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint8_t facts[BUSTER_NODE_MACHINE_MOST];
    uint8_t facts_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint8_t value_classes[BUSTER_NODE_MACHINE_MOST];
    uint8_t value_classes_stagger[BUSTER_NODE_MACHINE_STAGGER];
    TypeId converted[2 * BUSTER_NODE_MACHINE_MOST];
    uint8_t converted_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t conversion_flags[2 * BUSTER_NODE_MACHINE_MOST];
    uint8_t conversion_flags_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint64_t member_offsets[BUSTER_NODE_MACHINE_MOST];
    uint8_t member_offsets_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint8_t states[BUSTER_NODE_MACHINE_MOST];
    uint8_t states_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint8_t contexts[BUSTER_NODE_MACHINE_MOST];
    uint8_t contexts_stagger[BUSTER_NODE_MACHINE_STAGGER];
    TypeId targets[BUSTER_NODE_MACHINE_MOST];
    uint8_t targets_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint8_t templates[BUSTER_NODE_MACHINE_MOST];
    uint8_t templates_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t lowest[BUSTER_NODE_MACHINE_MOST];
    uint8_t lowest_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t argument_first[BUSTER_NODE_MACHINE_MOST];
    uint8_t argument_first_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t argument_counts[BUSTER_NODE_MACHINE_MOST];
    uint8_t argument_counts_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t hooks[BUSTER_NODE_MACHINE_MOST];
    uint8_t hooks_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t label_bases[BUSTER_NODE_MACHINE_MOST];
    uint8_t label_bases_stagger[BUSTER_NODE_MACHINE_STAGGER];
    uint32_t call_depths[BUSTER_NODE_MACHINE_MOST];
    uint8_t call_depths_stagger[BUSTER_NODE_MACHINE_STAGGER];
};

// An update's plan (`++x`, `--x`, `x++`, `x--` or `x op= y`), check_lower_update's choices read from the records: its
// destination's type (the lvalue's), its computation's (its operand 0's conversion, which check_lower_update converts
// the old value through) and operand 1's target (its conversion's type, or long for a pointer's); a direct update's
// local (its object) or global (its symbol), its read's and its store's kinds; and the constant its operation takes in
// place of operand 1, an increment's one or a power of two's count or mask (check_lower_power's MASK). The updates'
// functions are out of line: about one evaluated node in forty is an update, and the sweeps' loops stay as they are for
// the others.
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
    // Each type's pointer type, type_pointer's (its exact twin), by type index: the type unqualified's (`pointers`) and
    // const's (`const_pointers`, last of the machine's arrays, so that the others are where the function types' column left
    // them). 0 until an array of it decays, a designator of it converts or its address is taken (node_machine_pointer),
    // where the type table's interning took a hash lookup each time.
    TypeId* pointers;
    TypeId* const_pointers;
    // The argument memo: node_machine_argument's answers for the unit's recent argument keys, direct-mapped
    // (node_machine_argument_memo), where the rule's conversion read the type table's columns at each argument. `memo_fills`
    // counts the entries filled and `memo_rules` the arguments the rule took instead.
    NodeMachineMemo* argument_memo;
    uint64_t memo_fills;
    uint64_t memo_rules;
    // Whether the unit's graph has an overload's candidates, or will have: T refuses every call of such a unit, and a
    // discard is the action's there (node_machine_overloads).
    bool overloads;
    // Whether the unit has a `#pragma redefine_extname` the reader took: T refuses every plain library builtin's call
    // of such a unit, the one answer an object's asm label decides (node_machine_renames).
    bool renames;
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
    // The records' columns (NodeMachineColumns), which C and X read through the record accessors alone. T's (docs/node-
    // machine.md, "The records and the tapes"): each record's operator and its first two operands' records (UINT32_MAX for
    // none), and its value, which node_machine_record_value makes whole: its type, its flags, its object, its standing
    // (BUSTER_NODE_MACHINE_STANDING and the payload's field) and its payload: its integer's low 64 bits, its offset, or its
    // floating value's significand, with its sign and exponent in `exponents`. A value the columns can't hold whole (a
    // fault, an overload set, a bit-field's width, an integer past 64 bits, two of the payload's fields) is in `wides`,
    // and its payload is its index there. Then its type's word and facts and its class on the stack (an aggregate's is its
    // address's), its operands' conversions (their types and flags), a member's offset in bits, its subtree's least
    // record (itself for a leaf), for a call's row (node_machine_call_own), and a call's first argument in the
    // expression's list (`arguments`) and its count. C's: each record's state, its context
    // (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS and _DISCARD), the type its consumer converts it to, its template
    // (CheckLowerTemplate, or past it NodeMachineTemplate), its hook and label base (below) and the evaluated calls among
    // its ancestors.
    NodeMachineColumns* columns;
    CheckValue* wides;
    uint32_t wide_count;
    // A call's arguments: their records, by place in the expression's list, and each argument's conversion.
    // `argument_values` and `verified` are an action's arguments and --verify's conversions, for one call at a time.
    uint32_t* arguments;
    CheckConversion* argument_conversions;
    uint32_t argument_total;
    CheckValue* argument_values;
    CheckConversion* verified;
    // The unit's names of the functions that return twice and the jmp_buf's tag (check_lower_twice_names), for
    // node_machine_callee_refused.
    IdentId twice_names[BUSTER_CHECK_LOWER_TWICE_NAMES];
    IdentId twice_tag;
    // C's: where a record's state has BUSTER_NODE_MACHINE_AFTER_CONDITION or _AFTER_ARM, its hook is its consumer's
    // record, whose segment follows its row; where its template is NODE_MACHINE_TEMPLATE_BRANCH, its label base is the
    // first of its two labels, counted from the expression's first in the order the tasks number them
    // (node_machine_number_labels), of the expression's `label_count`. `stack` is the numbering's: the labeled records C
    // met, `labeled_count` of them, then their subtrees' first records. A logical operator's or a conditional's value
    // has two labels; an operator evaluated as branches that has one of its own (node_machine_record_past) has that one,
    // and it is its label base.
    uint32_t* stack;
    uint32_t label_count;
    uint32_t labeled_count;
    // For an update and its destination (BUSTER_NODE_MACHINE_SPLIT), its plan among the expression's `update_count`
    // `updates` (node_machine_record_update).
    uint32_t* consumers;
    NodeMachineUpdate* updates;
    uint32_t update_count;
    // X's tape, BUSTER_NODE_MACHINE_TAPE_MOST entries: the expression's entries, a local's holding its object, a branch's
    // its label, a call's its row relative to the expression's first and a temporary its number (`temporary_count` of
    // them); each label's entry, its place in the tape; the evaluated calls' records in the order X writes them
    // (postorder); and per row, its call's record, `call_count` rows.
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* tape;
    uint32_t* label_entries;
    uint32_t entry_count;
    uint32_t* calls_written;
    uint32_t* call_records;
    uint32_t call_count;
    uint32_t temporary_count;
    // node_machine_lower's (c_check_node_machine_lower.c), which writes the tape back from its columns' ends: its first
    // entry's place in them (X's tape starts at 0), each label's entry as its place from the tape's end, by the label's
    // number in the sweep's order (`label_ends`), the labeled records in that order (`label_met`) and each call's CALL's
    // place from the tape's end (`call_ends`).
    uint32_t entry_first;
    uint32_t* label_ends;
    uint32_t* label_met;
    uint32_t* call_ends;
    // The record T, C or X stopped at, for a refusal's report.
    uint32_t stopped;
    // What C and X ask of the unit's objects, `object_count` of them: each one's symbol (0 for a local: a parameter's, an
    // automatic object's) and its declaration's bits. After the pipeline the program's (CheckProgram.object_symbols, the
    // selection's numbering, and its input's declaration_bits). At reader time no symbol is numbered yet: an object with a
    // static declaration has itself there, which stands for its symbol (X writes it where it writes a symbol, and a
    // splice patches it), and the bits are the reader's input's.
    const SymbolId* object_symbols;
    const uint8_t* declaration_bits;
    uint32_t object_count;
    // At reader time (node_machine_begin_reader; `program` is null then, and T alone runs): what T asks of the program,
    // made from the reader's columns as the program makes its own from them. Each object's last static declaration's
    // token (CheckProgram.object_declarations', BUSTER_CHECK_NONE for an object without one), `reader_objects` of them,
    // and whether a declaration of it has returns_twice (BUSTER_NODE_MACHINE_OBJECT_TWICE, object_returns_twice's) or an
    // asm label (_LABELLED, object_labelled's); the unit's input and parser; and the room of the types' arrays, which
    // the types made after the machine began are taken into (node_machine_settle), `late_types` of them. The objects'
    // two columns are the caller's (node_machine_begin_reader).
    const CheckInput* input;
    const Parser* parser;
    // The unit's enumerations' definitions before the walk, as the tags' columns have them (the reader lists them in its
    // output at its end, CheckInput.enum_definitions): node_producer_begin's, for node_machine_root_loud. None otherwise.
    const CheckInputEnumDefinition* reader_enums;
    uint32_t reader_enum_count;
    // Whether the unit's target, or one of its functions' own, fuses a product and a sum (FMA or AVX-512F): the lowering
    // contracts a floating sum with its product operand there (check_lower_contract_plan), for which C has no template,
    // so C refuses a sum or a compound sum of a float's or a double's class there, whatever its operands are
    // (node_machine_classify, node_machine_update_plan), and the window's C lists every sum and every update
    // (node_contexts_window_loop). At reader time, where no attribute's target is read, it is the options' target's, or
    // true in a unit with a function of a target of its own, whatever that names (node_machine_begin_reader).
    bool fuses;
    uint32_t* reader_declarations;
    uint8_t* reader_facts;
    uint32_t reader_objects;
    // By call node, where the shadow keeps them (null otherwise): node_machine_reader_call_library's answers, 1 for no
    // and 2 for yes, which the shadow compares with the program's after the pipeline.
    uint8_t* reader_library;
    uint32_t type_capacity;
    uint64_t late_types;
    // The current expression's actions' warnings (CheckActionResult.warnings' 25 bits, each node's suppressed ones aside),
    // which the silence tests read at reader time (docs/node-machine.md, "The tests where they sit"): a row or a rule
    // types a node only where its action gives none, and --verify counts the nodes where one does (`twin_warnings`).
    uint32_t action_warnings;
    uint64_t twin_warnings;
    // A conditional's operand 2's conversion's type, by record: the columns hold two operands' conversions, and the tests
    // read a constant arm's. Written for a conditional alone (node_machine_record_converted_third).
    TypeId* converted_third;
};

// NodeMachine.reader_facts' bits. And whether a declaration of the unit's declares the object at all: one that none
// declares is the program's own (a multiversioned function's group's reference, a synthetic object at an attribute's
// name, #3203), which no leaf of the reader's names. And, while the facts are made, whether a declaration before the
// one at hand defines the object (node_machine_begin_reader's asm labels).
#define BUSTER_NODE_MACHINE_OBJECT_TWICE 0x01u
#define BUSTER_NODE_MACHINE_OBJECT_LABELLED 0x02u
#define BUSTER_NODE_MACHINE_OBJECT_DECLARED 0x04u
#define BUSTER_NODE_MACHINE_OBJECT_DEFINED 0x08u
// A declaration of the function has always_inline (CheckProgram.object_inline's BUSTER_CHECK_PROGRAM_INLINE_ALWAYS).
#define BUSTER_NODE_MACHINE_OBJECT_INLINED 0x10u

// Forgets the unit's call answers, the types' pointers and the argument memo, as a unit starts without them:
// node_machine_begin, and the shadow's timing before each pass (c_check_node_census.c's node_shadow_time), so that each
// pass pays their fills as one compile of the unit would.
static void node_machine_forget(NodeMachine* machine)
{
    memset(machine->pointers, 0, (uint64_t)machine->type_count * sizeof(TypeId));
    memset(machine->const_pointers, 0, (uint64_t)machine->type_count * sizeof(TypeId));
    memset(machine->argument_memo, 0, BUSTER_NODE_MACHINE_MEMO_ENTRIES * sizeof(NodeMachineMemo));
}

// The sweeps' files' functions that this file calls: T's table, its types' completion positions and its operands' records
// (c_check_node_machine_type.c), and the three sweeps.
bool check_node_window_prepare(void);
static uint32_t node_machine_complete_at(const TypeTable* types, const CheckGraph* graph, TypeId type);
static void node_machine_settle(NodeMachine* machine);
static bool node_machine_reader_call_library(const NodeMachine* machine, uint32_t r, uint32_t node);
static uint32_t node_machine_operand(const NodeMachine* machine, uint32_t r, uint32_t k);
static NodeMachineRefusal node_machine_type(NodeMachine* machine, uint32_t first, uint32_t root, uint32_t count);
static NodeMachineRefusal node_machine_contexts(NodeMachine* machine, uint8_t context, TypeId target, uint32_t object);
static NodeMachineRefusal node_machine_expand(NodeMachine* machine, uint32_t object);

// A column of `count` elements of `bytes` each from `arena`, starting `line` (below 64) cache lines past a 4 KB boundary,
// for the columns outside NodeMachineColumns: after its arrays' lines, as theirs are after each other's.
static void* node_machine_column(Arena* arena, uint64_t count, uint64_t bytes, uint32_t line)
{
    uint8_t* column = arena_push_array(arena, count * bytes + 4096, 1, 4096);
    return column != NULL ? column + (uint64_t)line * 64 : NULL;
}

// A type's word (NodeMachine.type_words) from its class and facts by the lowering's rules.
static uint32_t node_machine_make_word(const TypeTable* types, uint32_t index, uint8_t class, uint8_t facts)
{
    const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
    const bool valid = type_is_valid(types, type);
    const bool plain = types->types.kind[index] < BUSTER_TYPE_KIND_WRAPPERS;
    const uint8_t kind = valid ? check_action_kind(types, type) : BUSTER_TYPE_KIND_NONE;
    // The tests' byte's bits 1 to 3 (special, aggregate, enumeration) and 5 to 7 (pointer, void pointee, array), in a row.
    const uint32_t byte = node_census_type_bits_make(types, type);
    const uint32_t tests = ((byte >> 1) & 0x07u) | ((byte >> 2) & 0x38u);
    return (uint32_t)class | (uint32_t)facts << 8 | (uint32_t)kind << 16 | (plain ? BUSTER_NODE_MACHINE_WORD_PLAIN : 0) | (valid ? BUSTER_NODE_MACHINE_WORD_VALID : 0) |
           tests << BUSTER_NODE_MACHINE_WORD_TESTS_SHIFT;
}

// The machine's columns for a unit whose types are `types` and whose graph is `graph`, from `arena`, with room for
// `type_room` types' words (the unit's count, or more at reader time): whether there was room. The machine's `program`,
// or its reader's fields, are the caller's to set first.
static bool node_machine_setup(NodeMachine* machine, TypeTable* types, const CheckGraph* graph, uint32_t type_room, bool scalar, bool verify, Arena* arena)
{
    const uint32_t most = BUSTER_NODE_MACHINE_MOST;
    const uint32_t entries = BUSTER_NODE_MACHINE_TAPE_MOST;
    machine->types = types;
    machine->graph = graph;
    machine->type_count = types->types.count;
    machine->type_capacity = type_room;
    machine->scalar = scalar;
    machine->verify = verify;
    machine->type_words = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)type_room + 64);
    machine->type_complete_at = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)type_room + 64);
    machine->pointers = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, (uint64_t)type_room + 64);
    machine->argument_memo = BUSTER_ARENA_PUSH_ARRAY(arena, NodeMachineMemo, BUSTER_NODE_MACHINE_MEMO_ENTRIES);
    uint8_t* classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)machine->type_count + 64);
    uint8_t* facts = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)machine->type_count + 64);
    _Static_assert(sizeof(NodeMachineColumns) % 64 == 0 && sizeof(NodeMachineColumns) % 4096 == 25 * BUSTER_NODE_MACHINE_STAGGER % 4096,
                   "the record columns stagger by BUSTER_NODE_MACHINE_STAGGER each");
    machine->columns = arena_push_array(arena, 1, sizeof(NodeMachineColumns), 4096);
    // The others after the record columns' lines (25 arrays, two lines each).
    machine->wides = node_machine_column(arena, most, sizeof(CheckValue), 50);
    machine->kinds = node_machine_column(arena, entries, sizeof(uint8_t), 52);
    machine->classes = node_machine_column(arena, entries, sizeof(uint8_t), 54);
    machine->tape = node_machine_column(arena, entries, sizeof(uint64_t), 56);
    machine->label_entries = node_machine_column(arena, 2 * most, sizeof(uint32_t), 58);
    machine->calls_written = node_machine_column(arena, most, sizeof(uint32_t), 60);
    machine->call_records = node_machine_column(arena, most, sizeof(uint32_t), 62);
    machine->label_ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * most);
    machine->label_met = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->call_ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->arguments = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->argument_conversions = BUSTER_ARENA_PUSH_ARRAY(arena, CheckConversion, most);
    machine->argument_values = BUSTER_ARENA_PUSH_ARRAY(arena, CheckValue, BUSTER_TIER0_MAX_CALL_ARGUMENTS);
    machine->verified = BUSTER_ARENA_PUSH_ARRAY(arena, CheckConversion, BUSTER_TIER0_MAX_CALL_ARGUMENTS);
    machine->stack = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * most);
    machine->consumers = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, most);
    machine->updates = BUSTER_ARENA_PUSH_ARRAY(arena, NodeMachineUpdate, most / 2);
    machine->const_pointers = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, (uint64_t)type_room + 64);
    machine->converted_third = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, most);
    const bool room = machine->converted_third != NULL && machine->type_words != NULL && machine->type_complete_at != NULL && classes != NULL && facts != NULL && machine->columns != NULL &&
                      machine->wides != NULL && machine->kinds != NULL && machine->classes != NULL && machine->tape != NULL && machine->label_entries != NULL &&
                      machine->calls_written != NULL && machine->call_records != NULL && machine->arguments != NULL && machine->argument_conversions != NULL &&
                      machine->argument_values != NULL && machine->verified != NULL && machine->stack != NULL && machine->consumers != NULL &&
                      machine->updates != NULL && machine->pointers != NULL && machine->const_pointers != NULL &&
                      machine->argument_memo != NULL && machine->label_ends != NULL && machine->label_met != NULL && machine->call_ends != NULL;
    if (room)
    {
        // The pointers' columns start empty, past the unit's types too (node_machine_forget's are the unit's).
        memset(machine->pointers, 0, ((uint64_t)type_room + 64) * sizeof(TypeId));
        memset(machine->const_pointers, 0, ((uint64_t)type_room + 64) * sizeof(TypeId));
        memset(machine->argument_memo, 0, BUSTER_NODE_MACHINE_MEMO_ENTRIES * sizeof(NodeMachineMemo));
        check_lower_classify_types(types, machine->type_count, classes, facts);
        for (uint32_t index = 0; index < machine->type_count; index += 1)
        {
            const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
            const uint32_t word = node_machine_make_word(types, index, classes[index], facts[index]);
            machine->type_words[index] = word;
            machine->type_complete_at[index] = (word & BUSTER_NODE_MACHINE_WORD_VALID) != 0 ? node_machine_complete_at(types, graph, type) : UINT32_MAX;
        }
    }
    return room && check_node_window_prepare();
}

// Whether the unit whose input is `input` and whose graph is `graph` has an overload's candidates, or will have
// (#3203). A candidate is a declaration of an overloadable function, made where the reader reads it, before the bodies
// (c_check_overload_input.c); or a call of a function multiversioned by target strings, made after the unit's last body
// (check_input_multiversion_flush, c_check_target_versions.c). The machine after the pipeline read the graph's count
// whole; the machine at reader time begins before the bodies, has the first kind's and none of the second's, and took
// the calls of a unit that came to have one. What both have before the bodies is whether the unit declares a function
// multiversioned so at all (CheckInput.target_multiversion): a call's candidate is one's.
static bool node_machine_overloads(const CheckInput* input, const CheckGraph* graph)
{
    return graph->overload_candidate_count != 0 || input->target_multiversion;
}

// Whether the unit whose input is `input` has a `#pragma redefine_extname` the reader took (#3207). The pragma gives a
// declaration its asm label once the reader is done (check_input_symbol_finish: the declaration it selects, by the
// labels the asm pass kept and the uses the bodies made), so the machine at reader time cannot have the label of a call
// it reads before; the records are made before the bodies (check_input_symbol_pragmas), and whether one is taken is
// known at both times. An object's label decides one answer of T's, whether a plain library builtin's call is computed
// or called (node_machine_call_library): in such a unit T refuses every call the reader marked for one, and asks
// nothing of the labels.
static bool node_machine_renames(const CheckInput* input)
{
    const CheckInputSymbolPragmas* pragmas = input->symbol_pragmas;
    bool renames = false;
    for (uint32_t i = 0; pragmas != NULL && !renames && i < pragmas->count; i += 1)
    {
        renames = pragmas->records[i].supported && pragmas->records[i].kind == CHECK_SYMBOL_PRAGMA_REDEFINE;
    }
    return renames;
}

// The machine's columns for `program`'s unit, from `arena`: whether there was room.
static bool node_machine_begin(NodeMachine* machine, const CheckProgram* program, bool scalar, bool verify, Arena* arena)
{
    *machine = (NodeMachine){
        .program = program,
        .object_symbols = program->object_symbols,
        .declaration_bits = program->input->declaration_bits,
        .object_count = program->object_count,
    };
    uint64_t features = program->target.features;
    for (uint32_t t = 0; t < program->target_count; t += 1)
    {
        features |= program->targets[t].features;
    }
    machine->fuses = (features & BUSTER_NODE_MACHINE_FUSED) != 0;
    const bool room = node_machine_setup(machine, (TypeTable*)program->types, &program->input->graph, program->types->types.count, scalar, verify, arena);
    machine->overloads = node_machine_overloads(program->input, &program->input->graph);
    machine->renames = node_machine_renames(program->input);
    if (room)
    {
        (void)check_lower_twice_names(program, machine->twice_names, &machine->twice_tag);
    }
    return room;
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
    return (CheckOp)machine->columns->codes[r];
}

// Its operand 0's record and operand 1's: UINT32_MAX for none.
static inline uint32_t node_machine_record_left(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->left[r];
}

static inline uint32_t node_machine_record_right(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->right[r];
}

// Its operand 2's record (a conditional's second arm), from the graph: UINT32_MAX for none.
static inline uint32_t node_machine_record_third(const NodeMachine* machine, uint32_t r)
{
    return node_machine_operand(machine, r, 2);
}

// Its value's type, its category (an lvalue, an rvalue or a function designator) and its BUSTER_CHECK_* flags.
static inline TypeId node_machine_record_type(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->value_types[r];
}

static inline CheckCategory node_machine_record_category(const NodeMachine* machine, uint32_t r)
{
    return (CheckCategory)(machine->columns->standings[r] & 0x03u);
}

static inline uint32_t node_machine_record_flags(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->value_flags[r];
}

// Its wide value (NodeMachine.wides), or null where its columns hold its value whole: what the fields below read where
// the columns don't have them.
static inline const CheckValue* node_machine_record_wide(const NodeMachine* machine, uint32_t r)
{
    return (machine->columns->standings[r] & BUSTER_NODE_MACHINE_WIDE) != 0 ? machine->wides + machine->columns->payloads[r] : NULL;
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
        const uint8_t field = machine->columns->standings[r] & BUSTER_NODE_MACHINE_FIELD;
        const uint64_t payload = machine->columns->payloads[r];
        const bool floating = field == BUSTER_NODE_MACHINE_FLOATING;
        *value = (CheckValue){
            .type = machine->columns->value_types[r],
            .category = node_machine_record_category(machine, r),
            .flags = machine->columns->value_flags[r],
            .constant_kind = (CheckConstantKind)((machine->columns->standings[r] >> 2) & 0x07u),
            .integer = field == 0 ? payload : 0,
            .object = machine->columns->objects[r],
            .offset = field == BUSTER_NODE_MACHINE_OFFSET ? (int64_t)payload : 0,
        };
        value->floating_storage[0] = floating ? payload : 0;
        value->floating_storage[1] = floating ? machine->columns->exponents[r] : 0;
    }
}

// Its value's constant: its kind, an integer's low 64 bits, a floating constant's value, and an address constant's
// object (the object a value designates, for any other) and offset in bytes.
static inline CheckConstantKind node_machine_record_constant_kind(const NodeMachine* machine, uint32_t r)
{
    return (CheckConstantKind)((machine->columns->standings[r] >> 2) & 0x07u);
}

static inline uint64_t node_machine_record_integer(const NodeMachine* machine, uint32_t r)
{
    const CheckValue* wide = node_machine_record_wide(machine, r);
    return wide != NULL ? (uint64_t)wide->integer : (machine->columns->standings[r] & BUSTER_NODE_MACHINE_FIELD) == 0 ? machine->columns->payloads[r] : 0;
}

static inline long double node_machine_record_floating(const NodeMachine* machine, uint32_t r)
{
    CheckValue value;
    node_machine_record_value(machine, r, &value);
    return value.floating;
}

static inline uint32_t node_machine_record_object(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->objects[r];
}

static inline int64_t node_machine_record_offset(const NodeMachine* machine, uint32_t r)
{
    const CheckValue* wide = node_machine_record_wide(machine, r);
    return wide != NULL ? wide->offset : (machine->columns->standings[r] & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_OFFSET ? (int64_t)machine->columns->payloads[r] : 0;
}

// Whether its value's truth is known, check_action_truth's answer for its value made whole, and into `truth` that truth.
static inline __attribute__((always_inline)) bool node_machine_record_truth(const NodeMachine* machine, uint32_t r, bool* truth)
{
    CheckValue value;
    node_machine_record_value(machine, r, &value);
    return check_action_truth(machine->types, &value, truth);
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
    return machine->columns->facts[r];
}

static inline uint8_t node_machine_record_class(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->value_classes[r];
}

// Its operand `k`'s (0 or 1) conversion: the type the operator converts it to (0 for none) and the conversion's
// BUSTER_CHECK_CONVERT_* flags.
static inline TypeId node_machine_record_converted(const NodeMachine* machine, uint32_t r, uint32_t k)
{
    return machine->columns->converted[2 * r + k];
}

static inline uint32_t node_machine_record_conversion_flags(const NodeMachine* machine, uint32_t r, uint32_t k)
{
    return machine->columns->conversion_flags[2 * r + k];
}

// A conditional's operand 2's conversion's type (0 for any other record).
static inline TypeId node_machine_record_converted_third(const NodeMachine* machine, uint32_t r)
{
    return node_machine_record_op(machine, r) == CHECK_OP_CONDITIONAL ? machine->converted_third[r] : 0;
}

// A member's offset in its record, in bits.
static inline uint64_t node_machine_record_member_offset(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->member_offsets[r];
}

// The least record of its subtree: itself for a leaf, else its operands' and a call's arguments' least.
static inline uint32_t node_machine_record_lowest(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->lowest[r];
}

// A call's arguments: their count, argument `i`'s record, and its conversion (the type the call converts it to and the
// conversion's BUSTER_CHECK_CONVERT_* flags, as the stream's argument_conversions hold them).
static inline uint32_t node_machine_record_argument_count(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->argument_counts[r];
}

static inline uint32_t node_machine_record_argument(const NodeMachine* machine, uint32_t r, uint32_t i)
{
    return machine->arguments[machine->columns->argument_first[r] + i];
}

static inline TypeId node_machine_record_argument_converted(const NodeMachine* machine, uint32_t r, uint32_t i)
{
    return machine->argument_conversions[machine->columns->argument_first[r] + i].type;
}

static inline uint32_t node_machine_record_argument_conversion_flags(const NodeMachine* machine, uint32_t r, uint32_t i)
{
    return machine->argument_conversions[machine->columns->argument_first[r] + i].flags;
}

// C's columns. Whether its consumer evaluates it (none is, until C reaches it), and its whole state, which says too
// whether its consumer's segment follows its row (BUSTER_NODE_MACHINE_AFTER_CONDITION, _AFTER_ARM); its context
// (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS and _DISCARD), the type its consumer converts it to and its template; that
// consumer, where its state says; and a labeled record's first label.
static inline bool node_machine_record_evaluated(const NodeMachine* machine, uint32_t r)
{
    return (machine->columns->states[r] & BUSTER_NODE_MACHINE_EVALUATED) != 0;
}

static inline uint8_t node_machine_record_state(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->states[r];
}

static inline uint8_t node_machine_record_context(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->contexts[r];
}

static inline TypeId node_machine_record_target(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->targets[r];
}

// The evaluated calls among its ancestors, whose rows the lowering numbers before its own.
static inline uint32_t node_machine_record_call_depth(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->call_depths[r];
}

static inline CheckLowerTemplate node_machine_record_template(const NodeMachine* machine, uint32_t r)
{
    return (CheckLowerTemplate)machine->columns->templates[r];
}

static inline uint32_t node_machine_record_hook(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->hooks[r];
}

static inline uint32_t node_machine_record_label_base(const NodeMachine* machine, uint32_t r)
{
    return machine->columns->label_bases[r];
}

// Whether an operator evaluated as branches (BUSTER_NODE_MACHINE_BRANCHED) has a label of its own, past its second
// operand: an `&&` branched where true and an `||` where false (check_lower_branch's `past`), whose first operand
// decides the other way.
static inline bool node_machine_record_past(const NodeMachine* machine, uint32_t r)
{
    const CheckOp op = node_machine_record_op(machine, r);
    const bool sense = (machine->columns->states[r] & BUSTER_NODE_MACHINE_BRANCH_TRUE) != 0;
    return (op == CHECK_OP_LOGICAL_AND && sense) || (op == CHECK_OP_LOGICAL_OR && !sense);
}

// A labeled record's labels: one for an operator evaluated as branches, two for a value's.
static inline uint32_t node_machine_record_labels(const NodeMachine* machine, uint32_t r)
{
    return (machine->columns->states[r] & BUSTER_NODE_MACHINE_BRANCHED) != 0 ? 1 : 2;
}

// The tape's value of a branch to the label of record `owner` (a tested value's or a branched operator's hook): the
// control's own for BUSTER_NODE_MACHINE_NO_OWNER, else the record's first label, a value's alternate or a branched
// operator's own.
static inline uint64_t node_machine_branch_label(const NodeMachine* machine, uint32_t owner)
{
    return owner == BUSTER_NODE_MACHINE_NO_OWNER ? BUSTER_NODE_MACHINE_CONTROL_LABEL : node_machine_record_label_base(machine, owner);
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
    memset(machine->columns->states, 0, count);
}

static inline void node_machine_record_set_evaluated(NodeMachine* machine, uint32_t r)
{
    machine->columns->states[r] = BUSTER_NODE_MACHINE_EVALUATED;
}

static inline void node_machine_record_set_context(NodeMachine* machine, uint32_t r, uint8_t context)
{
    machine->columns->contexts[r] = context;
}

static inline void node_machine_record_set_target(NodeMachine* machine, uint32_t r, TypeId target)
{
    machine->columns->targets[r] = target;
}

static inline void node_machine_record_set_template(NodeMachine* machine, uint32_t r, CheckLowerTemplate kind)
{
    machine->columns->templates[r] = (uint8_t)kind;
}

static inline void node_machine_record_set_hook(NodeMachine* machine, uint32_t r, uint32_t consumer, uint8_t hook)
{
    machine->columns->states[r] = BUSTER_NODE_MACHINE_EVALUATED | hook;
    machine->columns->hooks[r] = consumer;
}

static inline void node_machine_record_set_label_base(NodeMachine* machine, uint32_t r, uint32_t base)
{
    machine->columns->label_bases[r] = base;
}

// An evaluated record `r` as a condition: a branch to `owner`'s label (BUSTER_NODE_MACHINE_NO_OWNER: the control's) where
// its truth is `sense`. With `branched` it is an operator evaluated as branches, and otherwise a value its consumer
// tests after its row.
static inline void node_machine_record_set_branch(NodeMachine* machine, uint32_t r, uint32_t owner, bool sense, bool branched)
{
    machine->columns->states[r] = (uint8_t)(BUSTER_NODE_MACHINE_EVALUATED | (branched ? BUSTER_NODE_MACHINE_BRANCHED : BUSTER_NODE_MACHINE_AFTER_BRANCH) |
                                            (sense ? BUSTER_NODE_MACHINE_BRANCH_TRUE : 0u));
    machine->columns->hooks[r] = owner;
}

static inline void node_machine_record_set_call_depth(NodeMachine* machine, uint32_t r, uint32_t depth)
{
    machine->columns->call_depths[r] = depth;
}

// Update record `r`'s plan, the expression's `k`th, and its destination `destination`'s: the update's first row follows
// the destination's entries.
static inline void node_machine_record_set_update(NodeMachine* machine, uint32_t r, uint32_t destination, uint32_t k)
{
    machine->columns->states[destination] |= BUSTER_NODE_MACHINE_SPLIT;
    machine->consumers[destination] = k;
    machine->consumers[r] = k;
}

// Whether `object` is a local of the function: an object without a symbol (a parameter's, an automatic object's), which
// the function's local numbers; one with a symbol is a global's (NodeMachine.object_symbols: at reader time, one with a
// static declaration).
static bool node_machine_local(const NodeMachine* machine, uint32_t object)
{
    return object != 0 && object < machine->object_count && machine->object_symbols[object] == 0;
}

// Whether an object's address is a template's (check_lower_template_address_takes): none's, a local's or a global's.
static bool node_machine_address_takes(const NodeMachine* machine, uint32_t object)
{
    return object == 0 || object < machine->object_count;
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

// Whether `object` has a symbol of its own (NodeMachine.object_symbols): the program's, which its selection numbers; at
// reader time, before any is numbered, whether it has a static declaration, which is what a referenced object's symbol
// comes from.
static inline bool node_machine_symbolled(const NodeMachine* machine, uint32_t object)
{
    return object < machine->object_count && machine->object_symbols[object] != 0;
}

// What T's callee refusal asks of a directly called object (node_machine_callee_refused): its declaration's name, and
// whether a declaration gives it returns_twice or always_inline; of the program's columns, or at reader time of the
// reader's.
static inline IdentId node_machine_object_name(const NodeMachine* machine, uint32_t object)
{
    const CheckProgram* program = machine->program;
    const uint32_t declaration = program != NULL ? (program->object_declarations != NULL ? program->object_declarations[object] : BUSTER_CHECK_NONE) :
                                                   machine->reader_declarations[object];
    const Parser* parser = program != NULL ? program->parser : machine->parser;
    const uint32_t token_count = program != NULL ? program->input->token_count : machine->input->token_count;
    return parser != NULL && declaration < token_count ? parser->tokens.names[declaration] : 0;
}

static inline bool node_machine_object_twice(const NodeMachine* machine, uint32_t object)
{
    const CheckProgram* program = machine->program;
    return program != NULL ? program->object_returns_twice != NULL && program->object_returns_twice[object] != 0 :
                             (machine->reader_facts[object] & BUSTER_NODE_MACHINE_OBJECT_TWICE) != 0;
}

// Whether a declaration gives it always_inline: the lowering expands a direct call of such a function's inline-only
// definition in its caller (#2983, check_program_select_wrappers), and the machine has a call's row alone.
static inline bool node_machine_object_inlined(const NodeMachine* machine, uint32_t object)
{
    const CheckProgram* program = machine->program;
    return program != NULL ? program->object_inline != NULL && (program->object_inline[object] & BUSTER_CHECK_PROGRAM_INLINE_ALWAYS) != 0 :
                             (machine->reader_facts[object] & BUSTER_NODE_MACHINE_OBJECT_INLINED) != 0;
}

// Whether call node `node`, record `r`, has a plain library builtin's semantics of its own (check_program_call_library):
// the program's answer, or at reader time the same from T's records (node_machine_reader_call_library).
static inline bool node_machine_call_library(const NodeMachine* machine, uint32_t r, uint32_t node)
{
    return machine->program != NULL ? check_program_call_library(machine->program, node) : node_machine_reader_call_library(machine, r, node);
}

// The object it designates where the lowering calls it directly (check_lower_call_body's `direct`): a function with a
// symbol of its own, its address a constant without an offset or an effect; 0 for a call through a pointer, whose callee
// the lowering evaluates.
static uint32_t node_machine_direct_callee(const NodeMachine* machine, uint32_t a)
{
    const uint32_t object = node_machine_record_object(machine, a);
    const bool direct = node_machine_record_category(machine, a) != CHECK_LVALUE && node_machine_record_constant_kind(machine, a) == CHECK_CONSTANT_ADDRESS &&
                        object != 0 && node_machine_record_offset(machine, a) == 0 && node_machine_symbolled(machine, object) &&
                        (node_machine_record_flags(machine, a) & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    return direct ? object : 0;
}

// What a call record `r`'s row of the function's calls table holds: the symbol it calls (Tier0Function.call_targets), its
// callee's where it is called directly and 0 through a pointer; and whether it is variadic (call_variadic), its callee's
// prototype's.
static SymbolId node_machine_call_target(const NodeMachine* machine, uint32_t r)
{
    const uint32_t object = node_machine_direct_callee(machine, node_machine_record_left(machine, r));
    return object != 0 ? machine->object_symbols[object] : 0;
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
