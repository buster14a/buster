// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1048's statement sweep, included by c_check_program_lower.c after the tasks' loop: the vector path's lowering of a
// statement's expression by two passes over its nodes, in the graph's postorder, instead of the tasks' push, pop and
// dispatch per node, whose loop reaches each node through the task its parent pushed, a dependent load at a time.
//
// 1. From the root down (check_lower_sweep_down): each node, its context (a value or an address, discarded or not) and
//    its conversion's target given by its parent before its own turn, as check_lower_expression gives them, writes its
//    own entries and then its conversion's into its slot, and gives its evaluated operands theirs. The next node is the
//    one below, whatever the tree's shape, so a node's loads don't wait on its parent's work.
// 2. From the lowest node up (check_lower_sweep_up): the slots, in node order, which is the order the tasks write the
//    entries in for the shapes the sweep takes (each node's operands', in turn, then its own, then its conversion's).
//
// A node of another shape (an `&&`, `||` or `?:`, an update, a bit-field's access, an aggregate's, a memory class's or a
// _BitInt's value, a builtin, a compound literal, and any node whose rule could fail) is a task node: in the second pass
// its expression task, with its conversion's, is pushed and drained in its place, which lowers its subtree as before. A
// statement the sweep can't take (a shared operand, a node past BUSTER_CHECK_LOWER_SWEEP_NODES below its root) is lowered
// by the tasks whole, the first pass's slots discarded.
//
// The tasks number what they allocate in their preorder, as each node is expanded: a call's row (its call, its arguments
// and its signature for abi_assign), a temporary, a label. The sweep's nodes allocate only calls' rows, each placed at its
// preorder's place: after the rows of the statement's calls before its subtree (counted when the second pass reaches the
// subtree's first node) and of its ancestors (counted down the first pass), and a task node's from its own place so. The
// task nodes allocate everything else, one after another in node order, which for disjoint subtrees is the preorder. The
// token a failure would report is the tasks' last one: the statement's last node in their preorder.
//
// The tasks' loop remains the exact rules: the scalar path, -g's tokens, a tracked stack and a program with a _BitInt type
// or overloads take it alone, and test-lowering-sweep compares the two lowerings function by function.
#ifndef BUSTER_C_CHECK_PROGRAM_SWEEP_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_SWEEP_INCLUDED


// A node's state in its statement's sweep: evaluated, in an address context, discarded, lowered by the tasks, a call whose
// row the second pass places.
#define BUSTER_CHECK_LOWER_SWEEP_SET 0x01u
#define BUSTER_CHECK_LOWER_SWEEP_ADDRESS 0x02u
#define BUSTER_CHECK_LOWER_SWEEP_DISCARD 0x04u
#define BUSTER_CHECK_LOWER_SWEEP_TASK 0x08u
#define BUSTER_CHECK_LOWER_SWEEP_CALL 0x10u

// A node's slot: its own entries, then its conversion's (at most four each: an address constant's, a pointer difference's;
// a conversion to _Bool's), their kinds and classes a byte each in one word, which the first pass writes and the second
// reads whole, as a load that a narrower store before it wrote part of waits for the store to retire.
struct CheckLowerSweepSlot
{
    uint64_t values[BUSTER_CHECK_LOWER_SWEEP_MOST];
    uint64_t kinds;
    uint64_t classes;
    uint32_t count;
};

// The entries a node's turn has written to its slot: their values there, their kinds and classes as the slot's words take
// them, and their count.
typedef struct CheckLowerSweepEntries CheckLowerSweepEntries;
struct CheckLowerSweepEntries
{
    uint64_t* values;
    uint64_t kinds;
    uint64_t classes;
    uint32_t count;
};

// A call's row as the first pass found it, which the second pass places (check_lower_sweep_up): its node's distance below
// the root, its subtree's first node's, its arguments' first conversion, their count and the named ones, its symbol (0
// for an indirect call), its result type and whether it is variadic.
struct CheckLowerSweepCall
{
    uint32_t distance;
    uint32_t first;
    uint32_t arguments;
    uint32_t count;
    uint32_t named;
    SymbolId symbol;
    TypeId result;
    bool variadic;
};

// The columns the sweep reads and writes, from the writer and the program, made once a part
// (CheckLowerWriter.sweep_view): a node reaches each by one load rather than through the writer.
struct CheckLowerSweep
{
    const CheckLowerWriter* writer;
    const CheckProgram* program;
    const TypeTable* types;
    const CheckOp* ops;
    const uint32_t* operand0;
    const uint32_t* operand1;
    const uint32_t* argument_counts;
    const uint32_t* argument_offsets;
    const uint32_t* argument_nodes;
    const TypeId* graph_types;
    const TypeId* value_types;
    const uint32_t* value_flags;
    const uint32_t* value_objects;
    const uint8_t* value_categories;
    const uint8_t* value_constants;
    const uint32_t* payload_rows;
    const CheckValue* payloads;
    const CheckNodeConversions* conversions;
    const CheckConversion* argument_conversions;
    const CheckDiagnostic* diagnostics;
    const uint64_t* member_offsets;
    const uint32_t* type_templates;
    const uint32_t* local_map;
    const uint8_t* indirect;
    uint8_t* local_flags;
    const SymbolId* object_symbols;
    const uint8_t* declaration_bits;
    const uint32_t* initializer_declarations;
    uint64_t* bits;
    uint64_t* firsts;
    uint8_t* states;
    TypeId* targets;
    uint32_t* ancestors;
    CheckLowerSweepSlot* slots;
    CheckLowerSweepCall* calls;
    uint32_t payload_count;
    uint32_t stream_count;
    uint32_t object_count;
    uint32_t type_count;
};

// A statement's sweep: its root, the lowest node an operand reached, its calls' rows and its nodes so far, its last node
// in the tasks' preorder (whose token CheckLowerWriter.token is after them) and whether a task node lowers it, and
// whether the sweep still takes it.
typedef struct CheckLowerSweepRun CheckLowerSweepRun;
struct CheckLowerSweepRun
{
    uint32_t root;
    uint32_t low;
    uint32_t call_count;
    uint32_t nodes;
    uint32_t tasks;
    uint32_t last;
    bool last_task;
    bool valid;
};

// A type's class and facts (CheckLowerShared.type_templates' layout), UINT8_MAX for a type past the table.
static inline __attribute__((always_inline)) uint32_t check_lower_sweep_info(const CheckLowerSweep* sweep, TypeId type)
{
    const uint32_t index = type_index(type);
    return index < sweep->type_count ? sweep->type_templates[index] : UINT8_MAX;
}

// Whether a class is one the sweep computes in: I8 to F64.
static inline __attribute__((always_inline)) bool check_lower_sweep_class(uint8_t class)
{
    return class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64;
}

// Whether a type's class and facts are a plain scalar's (check_lower_plain_scalar's, without void).
static inline __attribute__((always_inline)) bool check_lower_sweep_plain(uint32_t info)
{
    const uint8_t facts = (uint8_t)(info >> 8);
    return check_lower_sweep_class((uint8_t)info) &&
           (facts & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_COMPLEX | BUSTER_CHECK_LOWER_TYPE_ATOMIC | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0;
}

static inline __attribute__((always_inline)) void check_lower_sweep_put(CheckLowerSweepEntries* entries, uint8_t kind, uint8_t class, uint64_t value)
{
    entries->values[entries->count] = value;
    entries->kinds |= (uint64_t)kind << (entries->count * 8);
    entries->classes |= (uint64_t)class << (entries->count * 8);
    entries->count += 1;
}

// Gives operand `child` of `node` its state and target, and the calls and arguments of its ancestors' rows: where it can't
// (no such node below `node`, one another node gave its own already, one past the scratch), the statement is the tasks'.
static inline __attribute__((always_inline)) void check_lower_sweep_mark(const CheckLowerSweep* sweep, CheckLowerSweepRun* run, uint32_t node, uint32_t child,
                                                                         uint8_t state, TypeId target, uint32_t ancestors)
{
    const uint32_t distance = run->root - child;
    const bool inside = child < node && distance < BUSTER_CHECK_LOWER_SWEEP_NODES;
    const uint32_t word = inside ? distance / 64 : 0;
    const uint64_t bit = UINT64_C(1) << (distance % 64);
    const bool valid = inside && (sweep->bits[word] & bit) == 0;
    if (valid)
    {
        sweep->bits[word] |= bit;
        sweep->states[distance] = state;
        sweep->targets[distance] = target;
        sweep->ancestors[distance] = ancestors;
        run->low = child < run->low ? child : run->low;
    }
    run->valid = run->valid && valid;
}

// check_lower_address's entries: whether the sweep takes them (an object it refuses, or a variable-length array's local, is
// the tasks'). A local's address is taken, as check_lower_address marks it.
static inline __attribute__((always_inline)) bool check_lower_sweep_address(const CheckLowerSweep* sweep, CheckLowerSweepEntries* entries, uint32_t object,
                                                                            int64_t offset)
{
    bool takes = true;
    if (object == 0)
    {
        check_lower_sweep_put(entries, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)offset);
    }
    else
    {
        const bool known = object < sweep->object_count;
        const uint32_t local = known ? sweep->local_map[object] : BUSTER_CHECK_NONE;
        const SymbolId symbol = known ? sweep->object_symbols[object] : 0;
        if (local != BUSTER_CHECK_NONE && sweep->indirect[local] == BUSTER_CHECK_LOWER_VLA_NONE)
        {
            sweep->local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            check_lower_sweep_put(entries, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        }
        else if (local == BUSTER_CHECK_NONE && symbol != 0)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, symbol);
        }
        else
        {
            takes = false;
        }
        if (takes && offset != 0)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, (uint64_t)offset);
            check_lower_sweep_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
    }
    return takes;
}

// The entries of the conversion check_lower_expression pushes for a node of `from` evaluated to `to`
// (check_lower_conversion_empty's test, then check_lower_scalar_conversion_body's entries): whether the sweep takes them (a
// complex value's, an aggregate's, a _BitInt's or a memory class's are the tasks').
static inline __attribute__((always_inline)) bool check_lower_sweep_conversion(const CheckLowerSweep* sweep, CheckLowerSweepEntries* entries, TypeId from, TypeId to)
{
    bool takes = true;
    if (to != 0 && from != to)
    {
        const uint32_t from_info = check_lower_sweep_info(sweep, from);
        const uint32_t to_info = check_lower_sweep_info(sweep, to);
        const uint8_t from_facts = (uint8_t)(from_info >> 8);
        const uint8_t to_facts = (uint8_t)(to_info >> 8);
        const uint8_t source = (from_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : (uint8_t)from_info;
        const uint8_t target = (to_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : (uint8_t)to_info;
        const bool empty = source == target && source < BUSTER_TYPED_CLASSES &&
                           ((from_facts ^ to_facts) & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0 &&
                           ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) == 0 && (to_facts & (BUSTER_CHECK_LOWER_TYPE_BOOL | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0;
        const bool mixed = ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 && target != BUSTER_TYPED_CLASS_VOID;
        const bool complex = ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) != 0 && target != BUSTER_TYPED_CLASS_VOID;
        if (empty)
        {
        }
        else if (complex || mixed || source >= BUSTER_TYPED_CLASS_F80 || target >= BUSTER_TYPED_CLASS_F80 || ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_BIT_INT) != 0)
        {
            takes = false;
        }
        else if (target == BUSTER_TYPED_CLASS_VOID)
        {
            if (source != BUSTER_TYPED_CLASS_VOID)
            {
                check_lower_sweep_put(entries, BUSTER_TYPED_DISCARD, source, 0);
            }
        }
        else if ((to_facts & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
        {
            const bool narrow = check_lower_narrow(source);
            if (narrow)
            {
                check_lower_sweep_put(entries, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
            }
            check_lower_sweep_put(entries, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : source, 0);
            check_lower_sweep_put(entries, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_sweep_put(entries, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
        }
        else if (source != target)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_CONVERT, target, 0);
        }
    }
    return takes;
}

// check_lower_truth's entries for an operand of `type`: whether the sweep takes them (of a scalar class's operand).
static inline __attribute__((always_inline)) bool check_lower_sweep_truth(const CheckLowerSweep* sweep, CheckLowerSweepEntries* entries, TypeId type, bool negate)
{
    const uint32_t info = check_lower_sweep_info(sweep, type);
    const uint8_t class = (uint8_t)info;
    const bool takes = check_lower_sweep_class(class) && ((info >> 8) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) == 0;
    if (takes)
    {
        const bool narrow = check_lower_narrow(class);
        if (narrow)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
        }
        check_lower_sweep_put(entries, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : class, 0);
        check_lower_sweep_put(entries, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
    }
    return takes;
}

// The first node of `node`'s subtree in the graph's postorder: its smallest operand's, and so down, as
// check_lower_template_range finds a body's.
static uint32_t check_lower_sweep_first(const CheckGraph* graph, uint32_t node)
{
    uint32_t first = node;
    bool deeper = true;
    while (deeper)
    {
        uint32_t smallest = first;
        const uint32_t operands[3] = { graph->operand0[first], graph->operand1[first], graph->operand2[first] };
        for (uint32_t k = 0; k < 3; k += 1)
        {
            smallest = operands[k] < smallest ? operands[k] : smallest;
        }
        if (graph->ops[first] != CHECK_OP_VARARGS && graph->argument_counts[first] != 0)
        {
            const uint32_t argument = graph->argument_nodes[graph->argument_offsets[first]];
            smallest = argument < smallest ? argument : smallest;
        }
        deeper = smallest < first;
        first = smallest;
    }
    return first;
}

// A call the sweep takes, check_lower_call_body's plain one: a source call of the System V convention whose parameters'
// and arguments' types are plain scalars, none a transparent union's, and whose result's is one or void. Whether it is
// one, with its row in `record`, and whether it is direct (its callee unevaluated).
static inline __attribute__((always_inline)) bool check_lower_sweep_call(const CheckLowerSweep* sweep, uint32_t root, uint32_t node, CheckLowerSweepCall* record,
                                                                         bool* direct)
{
    const TypeTable* types = sweep->types;
    const uint32_t callee = sweep->operand0[node];
    const uint32_t count = sweep->argument_counts[node];
    const uint32_t first = sweep->argument_offsets[node];
    const TypeId result = sweep->value_types[node];
    bool takes = callee < node && count <= BUSTER_TIER0_MAX_CALL_ARGUMENTS && check_lower_plain_scalar(check_lower_sweep_info(sweep, result), true);
    // The callee's value: a function's address constant (a direct call) or a pointer's value.
    const uint32_t row = takes ? sweep->payload_rows[callee] : 0;
    const CheckValue* payload = row != 0 && row <= sweep->payload_count ? sweep->payloads + row - 1 : NULL;
    const TypeId callee_type = takes ? sweep->value_types[callee] : 0;
    const uint32_t object = takes ? sweep->value_objects[callee] : 0;
    takes = takes && (row == 0 || payload != NULL);
    *direct = takes && sweep->value_categories[callee] != CHECK_LVALUE && sweep->value_constants[callee] == CHECK_CONSTANT_ADDRESS && object != 0 &&
              (payload == NULL || payload->offset == 0) && object < sweep->object_count && sweep->object_symbols[object] != 0 &&
              (sweep->value_flags[callee] & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    TypeId type = takes ? type_strip_aligned(types, callee_type) : 0;
    if (takes && type_kind(types, type) == BUSTER_TYPE_KIND_POINTER)
    {
        type = type_strip_aligned(types, type_element(types, type));
    }
    const uint8_t flags = takes ? types->types.flags[type_index(type)] : 0;
    const bool kr = takes && (flags & BUSTER_TYPE_FLAG_PROTOTYPE) == 0 && type_kind(types, sweep->graph_types[node]) == BUSTER_TYPE_KIND_FUNCTION;
    uint32_t parameters = 0;
    const TypeId* parameter_types = takes ? type_parameters(types, kr ? sweep->graph_types[node] : type, &parameters) : NULL;
    takes = takes && check_lower_convention(types, type) == NULL;
    for (uint32_t a = 0; takes && a < parameters; a += 1)
    {
        takes = check_lower_sweep_plain(check_lower_sweep_info(sweep, parameter_types[a]));
    }
    for (uint32_t a = 0; takes && a < count; a += 1)
    {
        const CheckConversion* conversion = sweep->argument_conversions + first + a;
        takes = check_lower_sweep_plain(check_lower_sweep_info(sweep, conversion->type)) && (conversion->flags & BUSTER_CHECK_CONVERT_TRANSPARENT_UNION) == 0;
    }
    if (takes)
    {
        *record = (CheckLowerSweepCall){
            .distance = root - node,
            .first = root - check_lower_sweep_first(&sweep->program->input->graph, node),
            .arguments = first,
            .count = count,
            .named = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ? parameters : count,
            .symbol = *direct ? sweep->object_symbols[object] : 0,
            .result = result,
            .variadic = (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0 || (flags & BUSTER_TYPE_FLAG_PROTOTYPE) == 0,
        };
    }
    return takes;
}

// The constant the tasks fold `node` to (check_lower_constant's entries, check_lower_expand's case), in a context of
// `address` (an address constant's entries are its object's address, its offset and an integer rvalue's conversion): whether
// the sweep takes them.
static inline __attribute__((always_inline)) bool check_lower_sweep_constant(const CheckLowerSweep* sweep, CheckLowerSweepEntries* entries, uint32_t node, uint32_t info, const CheckValue* payload)
{
    const uint8_t own = (uint8_t)info;
    const uint8_t constant_kind = sweep->value_constants[node];
    bool takes = true;
    if (constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        takes = check_lower_sweep_address(sweep, entries, sweep->value_objects[node], payload != NULL ? payload->offset : 0);
        if (sweep->value_categories[node] == CHECK_RVALUE && own >= BUSTER_TYPED_CLASS_I8 && own <= BUSTER_TYPED_CLASS_I64)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_CONVERT, own, 0);
        }
    }
    else if (((info >> 8) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 || own >= BUSTER_TYPED_CLASS_F80)
    {
        takes = false;
    }
    else if (own >= BUSTER_TYPED_CLASS_I8 && own <= BUSTER_TYPED_CLASS_U64)
    {
        uint64_t bits = payload != NULL ? (uint64_t)payload->integer : 0;
        const uint32_t width = 8u << ((own - 1) / 2);
        if (width < 64)
        {
            const uint64_t mask = (UINT64_C(1) << width) - 1;
            bits &= mask;
            bits |= (own & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
        }
        check_lower_sweep_put(entries, BUSTER_TYPED_CONST, own, bits);
    }
    else if ((own == BUSTER_TYPED_CLASS_F32 || own == BUSTER_TYPED_CLASS_F64) && constant_kind == CHECK_CONSTANT_FLOATING)
    {
        check_lower_sweep_put(entries, BUSTER_TYPED_CONST, own, check_lower_float_bits(NULL, own, payload != NULL ? payload->floating : 0.0L));
    }
    else
    {
        takes = false;
    }
    return takes;
}

// A node's operands as its case gives them: up to two, in their order, each with its state and its conversion's target;
// and check_lower_sweep_expand's entries.
typedef struct CheckLowerSweepUse CheckLowerSweepUse;
struct CheckLowerSweepUse
{
    CheckLowerSweepEntries entries;
    uint32_t first;
    uint32_t second;
    TypeId first_target;
    TypeId second_target;
    uint8_t first_state;
    uint8_t second_state;
    uint8_t count;
    bool takes;
};

// The cases of check_lower_expand that the sweep takes past a VALUE's, a wrapper's and a truth test's, for `node` of `op`
// whose parent gave it `state`, `type`'s `info`: its slot's own entries and its operands, or not taken. Out of line, as
// fewer nodes take it than the common cases, which keep the first pass's loop short.
static __attribute__((noinline)) CheckLowerSweepUse check_lower_sweep_expand(const CheckLowerSweep* sweep, uint32_t node, CheckOp op, uint8_t state, TypeId type,
                                                                             uint32_t info, const CheckValue* payload, uint64_t* values)
{
    const bool address = (state & BUSTER_CHECK_LOWER_SWEEP_ADDRESS) != 0;
    const bool discard = (state & BUSTER_CHECK_LOWER_SWEEP_DISCARD) != 0;
    const uint8_t facts = (uint8_t)(info >> 8);
    const bool aggregate = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
    const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = aggregate ? BUSTER_TYPED_CLASS_U64 : (uint8_t)info;
    const uint32_t flags = sweep->value_flags[node];
    const uint8_t category = sweep->value_categories[node];
    const uint8_t constant_kind = sweep->value_constants[node];
    const uint32_t a = sweep->operand0[node];
    const uint32_t b = sweep->operand1[node];
    const TypeId type_a = sweep->conversions[node].operands[0].type;
    const TypeId type_b = sweep->conversions[node].operands[1].type;
    const CheckFault fault = payload != NULL ? payload->fault : CHECK_FAULT_NONE;
    const bool constant = constant_kind != CHECK_CONSTANT_NONE && fault == CHECK_FAULT_NONE && (flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    CheckLowerSweepUse use = {
        .entries = { .values = values },
        .first = a,
        .second = b,
        .first_state = BUSTER_CHECK_LOWER_SWEEP_SET,
        .second_state = BUSTER_CHECK_LOWER_SWEEP_SET,
        .takes = true,
    };
    CheckLowerSweepEntries* entries = &use.entries;
    if (op > CHECK_OP_SELECT || (facts & BUSTER_CHECK_LOWER_TYPE_BIT_INT) != 0 || (!address && category == CHECK_LVALUE && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0))
    {
        // check_lower_expand_rare's (a builtin's, a statement expression's, va_arg's, a variable-length array's, a complex
        // part's, a bit cast's), a _BitInt's and an atomic lvalue's read: the tasks'.
        use.takes = false;
    }
    else if (constant && op != CHECK_OP_INITIALIZE && ((category == CHECK_RVALUE && !address) || ((address || address_value) && constant_kind == CHECK_CONSTANT_ADDRESS)))
    {
        // The node folded: none where it is discarded; its operands aren't evaluated.
        use.takes = discard || check_lower_sweep_constant(sweep, entries, node, info, payload);
    }
    else if (op == CHECK_OP_VALUE)
    {
        // An unfolded VALUE of a constant kind (a const object's lvalue): check_lower_sweep_node's case, but for its offset.
        const uint32_t object = sweep->value_objects[node];
        const bool known = object != 0 && object < sweep->object_count;
        const uint8_t bits = known ? sweep->declaration_bits[object - 1] : 0;
        use.count = 0;
        if ((bits & BUSTER_CHECK_BITS_COMPOUND) != 0 && (bits & BUSTER_CHECK_BITS_STATIC) == 0)
        {
            use.takes = false;
        }
        else if (type == BUSTER_TYPE_VOID)
        {
        }
        else if (address || address_value)
        {
            use.takes = check_lower_sweep_address(sweep, entries, object, payload != NULL ? payload->offset : 0);
        }
        else if (category == CHECK_LVALUE && known && check_lower_sweep_class(class))
        {
            const uint32_t local = sweep->local_map[object];
            const SymbolId symbol = sweep->object_symbols[object];
            use.takes = local != BUSTER_CHECK_NONE || symbol != 0;
            check_lower_sweep_put(entries, local != BUSTER_CHECK_NONE ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL, class, local != BUSTER_CHECK_NONE ? local : symbol);
        }
        else
        {
            use.takes = false;
        }
    }
    else if (op == CHECK_OP_SELECT)
    {
        use.first_state = BUSTER_CHECK_LOWER_SWEEP_SET | (address ? BUSTER_CHECK_LOWER_SWEEP_ADDRESS : 0);
        use.count = 1;
    }
    else if (op == CHECK_OP_ADDRESS)
    {
        use.first_state = BUSTER_CHECK_LOWER_SWEEP_SET | BUSTER_CHECK_LOWER_SWEEP_ADDRESS;
        use.count = 1;
    }
    else if (op == CHECK_OP_DEREFERENCE)
    {
        const bool loads = !address && !address_value;
        use.takes = !loads || check_lower_sweep_class(class);
        if (loads)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_LOAD, class, 0);
        }
        use.first_target = type_a;
        use.count = 1;
    }
    else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
    {
        const TypeId record = op == CHECK_OP_ARROW ? type_element(sweep->types, type_a) : sweep->value_types[a];
        const uint8_t record_kind = check_action_kind(sweep->types, record);
        const uint64_t offset = sweep->member_offsets[node];
        const bool loads = !address && !address_value;
        use.takes = sweep->diagnostics[node] == CHECK_DIAG_NONE && (record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) &&
                    (flags & BUSTER_CHECK_BIT_FIELD) == 0 && offset % 8 == 0 && (!loads || check_lower_sweep_class(class));
        if (offset / 8 != 0)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
            check_lower_sweep_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
        if (loads)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_LOAD, class, 0);
        }
        use.first_state = BUSTER_CHECK_LOWER_SWEEP_SET | (op == CHECK_OP_MEMBER ? BUSTER_CHECK_LOWER_SWEEP_ADDRESS : 0);
        use.first_target = type_a;
        use.count = 1;
    }
    else if (op == CHECK_OP_CALL)
    {
        // check_lower_sweep_node's own case.
        use.takes = false;
    }
    else if (!check_lower_sweep_class(class) || aggregate)
    {
        // A vector's, a memory class's, a half-precision value's or an aggregate's operation, or one of no class: the tasks',
        // but a cast's, a conversion's or a unary plus's to a class below VECTOR (a void cast among them).
        use.takes = (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE) && class < BUSTER_TYPED_CLASS_VECTOR;
        use.first_target = type;
        use.count = 1;
    }
    else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
    {
        use.first_target = type;
        use.count = 1;
    }
    else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
    {
        check_lower_sweep_put(entries, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
        use.first_target = type_a;
        use.count = 1;
    }
    else if (op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
    {
        // A truth test whose node has a constant kind but isn't folded (a fault's or an effect's).
        use.takes = check_lower_sweep_truth(sweep, entries, type_a, op == CHECK_OP_NOT);
        use.first_target = type_a;
        use.count = 1;
    }
    else if (op == CHECK_OP_COMMA)
    {
        use.first_target = BUSTER_TYPE_VOID;
        use.second_state = BUSTER_CHECK_LOWER_SWEEP_SET | (address ? BUSTER_CHECK_LOWER_SWEEP_ADDRESS : 0);
        use.second_target = discard ? BUSTER_TYPE_VOID : type;
        use.count = 2;
    }
    else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE)
    {
        // A scalar's store: to a local or a global it names, or through its destination's address.
        const bool destination_node = a < sweep->stream_count;
        const uint8_t destination_facts = destination_node ? (uint8_t)(check_lower_sweep_info(sweep, sweep->value_types[a]) >> 8) : 0;
        const bool bit_field = op == CHECK_OP_ASSIGN && destination_node && (sweep->value_flags[a] & BUSTER_CHECK_BIT_FIELD) != 0;
        const bool element = op == CHECK_OP_ASSIGN && destination_node && sweep->ops[a] == CHECK_OP_SUBSCRIPT && (sweep->value_flags[a] & BUSTER_CHECK_VECTOR_ELEMENT) != 0;
        // check_lower_initializer_of's declaration, where the initializers' map names the node.
        uint32_t declaration = BUSTER_CHECK_NONE;
        if (op == CHECK_OP_INITIALIZE)
        {
            const CheckInput* input = sweep->program->input;
            const uint32_t token = sweep->initializer_declarations[node];
            declaration = token < input->token_count && check_sparse_get(input->initializer_nodes, token) == node ? token : BUSTER_CHECK_NONE;
        }
        const uint32_t destination = declaration != BUSTER_CHECK_NONE ? sweep->program->input->objects[declaration] :
                                     destination_node && sweep->ops[a] == CHECK_OP_VALUE ? sweep->value_objects[a] : 0;
        const bool named = destination != 0 && destination < sweep->object_count;
        const uint32_t local = named ? sweep->local_map[destination] : BUSTER_CHECK_NONE;
        const SymbolId symbol = named ? sweep->object_symbols[destination] : 0;
        use.takes = (op != CHECK_OP_ASSIGN || (destination_facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0) && !bit_field && !element;
        if (use.takes && (local != BUSTER_CHECK_NONE || symbol != 0))
        {
            check_lower_sweep_put(entries, local != BUSTER_CHECK_NONE ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                                  local != BUSTER_CHECK_NONE ? local : symbol);
            use.first = op == CHECK_OP_INITIALIZE ? a : b;
            use.first_target = type;
            use.count = 1;
        }
        else if (use.takes && op == CHECK_OP_ASSIGN)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_STORE, class, 0);
            use.first_state = BUSTER_CHECK_LOWER_SWEEP_SET | BUSTER_CHECK_LOWER_SWEEP_ADDRESS;
            use.second_target = type;
            use.count = 2;
        }
        else
        {
            use.takes = false;
        }
    }
    else if (op >= CHECK_OP_PRE_INCREMENT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL)
    {
        // The updates and the logical operators: the tasks'.
        use.takes = false;
    }
    else if (op == CHECK_OP_SUBSCRIPT || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
    {
        const uint32_t info_a = check_lower_sweep_info(sweep, type_a);
        const uint32_t info_b = check_lower_sweep_info(sweep, type_b);
        const bool left_pointer = ((info_a >> 8) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
        const bool right_pointer = ((info_b >> 8) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
        if ((flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0 || right_pointer)
        {
            // A vector's element, a pointer's difference, and an integer plus a pointer, evaluated pointer first: the tasks'.
            use.takes = false;
        }
        else if (left_pointer)
        {
            // check_lower_scale's: the pointer, the index converted to long, its negation for a difference, the PTR_ADD by
            // the stride, and a subscript's load where it reads.
            const uint64_t stride = check_action_pointer_stride(sweep->types, type_a);
            const bool loads = op == CHECK_OP_SUBSCRIPT && !address && !address_value;
            use.takes = stride != 0 && (!loads || check_lower_sweep_class(class));
            if (op == CHECK_OP_SUBTRACT)
            {
                check_lower_sweep_put(entries, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
            }
            check_lower_sweep_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, stride);
            if (loads)
            {
                check_lower_sweep_put(entries, BUSTER_TYPED_LOAD, class, 0);
            }
            use.first_target = type_a;
            use.second_target = BUSTER_TYPE_LONG;
            use.count = 2;
        }
        else
        {
            use.takes = op != CHECK_OP_SUBSCRIPT && check_lower_sweep_plain(info_a) && check_lower_sweep_plain(info_b);
            check_lower_sweep_put(entries, check_lower_binary(op), class, 0);
            use.first_target = type_a;
            use.second_target = type_b;
            use.count = 2;
        }
    }
    else if (check_lower_binary(op) != BUSTER_TYPED_NONE)
    {
        // An operator of scalar operands: check_lower_operation's entry, an unsigned division or remainder by a constant
        // power of two its shift or mask by a CONST in place of its right operand, which isn't evaluated then.
        uint8_t kind = check_lower_binary(op);
        uint64_t right = 0;
        use.takes = check_lower_sweep_plain(check_lower_sweep_info(sweep, type_a)) && check_lower_sweep_plain(check_lower_sweep_info(sweep, type_b));
        const bool power = use.takes && check_lower_unsigned_power(sweep->writer, node, class, &kind, &right);
        if (power)
        {
            check_lower_sweep_put(entries, BUSTER_TYPED_CONST, class, right);
        }
        check_lower_sweep_put(entries, kind, class, 0);
        use.first_target = type_a;
        use.second_target = type_b;
        use.count = power ? 1 : 2;
    }
    else
    {
        use.takes = false;
    }
    return use;
}

// The first pass's turn of `node`, at `distance` below the root, whose parent gave it `state`: its slot's entries, its own
// then its conversion's, and its evaluated operands' contexts and targets, as the vector path's tasks' loop lowers it
// (check_lower_plain, then check_lower_expand); or the tasks' (BUSTER_CHECK_LOWER_SWEEP_TASK), its operands left to them.
// The common shapes are each op's own case here, with only the rules that can apply to it (a program the sweep takes has no
// _BitInt type and no overloads, so no node is either's); the rest are check_lower_sweep_expand's, in check_lower_expand's
// order.
static __attribute__((noinline)) void check_lower_sweep_node(const CheckLowerSweep* sweep, CheckLowerSweepRun* run, uint32_t node, uint32_t distance,
                                                                         uint8_t state)
{
    const CheckOp op = sweep->ops[node];
    const TypeId type = sweep->value_types[node];
    const TypeId target = sweep->targets[distance];
    const uint32_t info = check_lower_sweep_info(sweep, type);
    const uint8_t facts = (uint8_t)(info >> 8);
    const uint8_t constant_kind = sweep->value_constants[node];
    CheckLowerSweepSlot* slot = sweep->slots + distance;
    CheckLowerSweepEntries own = { .values = slot->values };
    CheckLowerSweepEntries* entries = &own;
    CheckLowerSweepUse use = { .first_state = BUSTER_CHECK_LOWER_SWEEP_SET, .second_state = BUSTER_CHECK_LOWER_SWEEP_SET };
    if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_COMMA || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN))
    {
        // check_lower_expression's discard, of the void context its parent gave it.
        const uint32_t a = sweep->operand0[node];
        const bool store = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE;
        const bool update = op == CHECK_OP_ASSIGN || op >= CHECK_OP_PRE_INCREMENT;
        const bool bit_field = update && a < sweep->stream_count && (sweep->value_flags[a] & BUSTER_CHECK_BIT_FIELD) != 0;
        const bool discard = (state & BUSTER_CHECK_LOWER_SWEEP_ADDRESS) == 0 && target == BUSTER_TYPE_VOID &&
                             ((store && (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0) || bit_field || op == CHECK_OP_COMMA);
        state |= discard ? BUSTER_CHECK_LOWER_SWEEP_DISCARD : 0;
    }
    const bool address = (state & BUSTER_CHECK_LOWER_SWEEP_ADDRESS) != 0;
    const bool discard = (state & BUSTER_CHECK_LOWER_SWEEP_DISCARD) != 0;
    const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : (uint8_t)info;
    const bool aggregate = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
    const bool scalar = check_lower_sweep_class(class) && !aggregate;
    bool calls = false;
    bool direct = false;
    bool general = false;
    // A node of a constant kind: folded where check_lower_expand folds it (no fault, effect or initialization, an rvalue
    // or an address constant's address); its payload row's, which it reads only then.
    bool folded = false;
    bool rowed = true;
    if (constant_kind != CHECK_CONSTANT_NONE)
    {
        const uint32_t row = sweep->payload_rows[node];
        const CheckValue* payload = row != 0 && row <= sweep->payload_count ? sweep->payloads + row - 1 : NULL;
        const uint8_t category = sweep->value_categories[node];
        rowed = row == 0 || payload != NULL;
        folded = rowed && (payload == NULL || payload->fault == CHECK_FAULT_NONE) && (sweep->value_flags[node] & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 &&
                 op != CHECK_OP_INITIALIZE && ((category == CHECK_RVALUE && !address) || ((address || address_value) && constant_kind == CHECK_CONSTANT_ADDRESS));
        if (folded && op <= CHECK_OP_SELECT && constant_kind == CHECK_CONSTANT_INTEGER && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64 && !aggregate)
        {
            // An integer constant of a class I8 to U64 (check_lower_sweep_constant's), none where it is discarded.
            uint64_t bits = payload != NULL ? (uint64_t)payload->integer : 0;
            const uint32_t width = 8u << ((class - 1) / 2);
            if (width < 64)
            {
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                bits &= mask;
                bits |= (class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
            }
            if (!discard)
            {
                check_lower_sweep_put(entries, BUSTER_TYPED_CONST, class, bits);
            }
            use.takes = true;
        }
        else
        {
            general = folded;
        }
    }
    if ((facts & BUSTER_CHECK_LOWER_TYPE_COMPLEX) != 0 || folded || !rowed)
    {
        // A complex value's (the tasks'), a folded node's (above) or one of a row past the payloads (the tasks').
    }
    else
    {
        const uint32_t a = sweep->operand0[node];
        const uint32_t b = sweep->operand1[node];
        switch (op)
        {
            case CHECK_OP_VALUE:
            {
                // A name's read or its address, but a compound literal's first read, which initializes it, and an atomic
                // lvalue's read: the tasks'. An offset is a payload row's, whose node is check_lower_sweep_expand's.
                const uint32_t object = sweep->value_objects[node];
                const bool known = object != 0 && object < sweep->object_count;
                const uint8_t bits = known ? sweep->declaration_bits[object - 1] : 0;
                const uint8_t category = sweep->value_categories[node];
                if (sweep->payload_rows[node] != 0)
                {
                    general = true;
                }
                else if (((bits & BUSTER_CHECK_BITS_COMPOUND) != 0 && (bits & BUSTER_CHECK_BITS_STATIC) == 0) ||
                         (!address && category == CHECK_LVALUE && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0))
                {
                }
                else if (type == BUSTER_TYPE_VOID)
                {
                    use.takes = true;
                }
                else if (address || address_value)
                {
                    use.takes = check_lower_sweep_address(sweep, entries, object, 0);
                }
                else if (category == CHECK_LVALUE && known && check_lower_sweep_class(class))
                {
                    const uint32_t local = sweep->local_map[object];
                    const SymbolId symbol = sweep->object_symbols[object];
                    use.takes = local != BUSTER_CHECK_NONE || symbol != 0;
                    check_lower_sweep_put(entries, local != BUSTER_CHECK_NONE ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL, class,
                                          local != BUSTER_CHECK_NONE ? local : symbol);
                }
                break;
            }
            case CHECK_OP_CAST:
            case CHECK_OP_CONVERT:
            case CHECK_OP_POSITIVE:
            case CHECK_OP_SELECT:
            {
                // check_lower_plain's wrapper; one of a constant kind is check_lower_expand's.
                general = constant_kind != CHECK_CONSTANT_NONE;
                use.takes = true;
                use.first = a;
                use.first_state = BUSTER_CHECK_LOWER_SWEEP_SET | (op == CHECK_OP_SELECT && address ? BUSTER_CHECK_LOWER_SWEEP_ADDRESS : 0);
                use.first_target = op == CHECK_OP_SELECT ? 0 : type;
                use.count = 1;
                break;
            }
            case CHECK_OP_NOT:
            case CHECK_OP_CONDITION:
            {
                // check_lower_plain's truth test, of an operand of a scalar class (a vector's is check_lower_expand's); one of a
                // constant kind is check_lower_expand's.
                const TypeId type_a = sweep->conversions[node].operands[0].type;
                general = constant_kind != CHECK_CONSTANT_NONE;
                use.takes = check_lower_sweep_truth(sweep, entries, type_a, op == CHECK_OP_NOT);
                use.first = a;
                use.first_target = type_a;
                use.count = 1;
                break;
            }
            case CHECK_OP_ADDRESS:
            {
                use.takes = true;
                use.first = a;
                use.first_state = BUSTER_CHECK_LOWER_SWEEP_SET | BUSTER_CHECK_LOWER_SWEEP_ADDRESS;
                use.count = 1;
                break;
            }
            case CHECK_OP_DEREFERENCE:
            {
                // Its operand's pointer, and its load where it reads; an atomic lvalue's read is the tasks'.
                const bool loads = !address && !address_value;
                use.takes = (!loads || check_lower_sweep_class(class)) &&
                            (address || sweep->value_categories[node] != CHECK_LVALUE || (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0);
                if (loads)
                {
                    check_lower_sweep_put(entries, BUSTER_TYPED_LOAD, class, 0);
                }
                use.first = a;
                use.first_target = sweep->conversions[node].operands[0].type;
                use.count = 1;
                break;
            }
            case CHECK_OP_MEMBER:
            case CHECK_OP_ARROW:
            {
                // A struct's or union's member at its offset, its load where it reads; a bit-field's, an atomic one's read and
                // a diagnosed one's are the tasks'.
                const TypeId type_a = sweep->conversions[node].operands[0].type;
                const TypeId record = op == CHECK_OP_ARROW ? type_element(sweep->types, type_a) : sweep->value_types[a];
                const uint8_t record_kind = check_action_kind(sweep->types, record);
                const uint64_t offset = sweep->member_offsets[node];
                const bool loads = !address && !address_value;
                use.takes = sweep->diagnostics[node] == CHECK_DIAG_NONE && (record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) &&
                            (sweep->value_flags[node] & BUSTER_CHECK_BIT_FIELD) == 0 && offset % 8 == 0 && (!loads || check_lower_sweep_class(class)) &&
                            (address || sweep->value_categories[node] != CHECK_LVALUE || (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0);
                if (offset / 8 != 0)
                {
                    check_lower_sweep_put(entries, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
                    check_lower_sweep_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
                }
                if (loads)
                {
                    check_lower_sweep_put(entries, BUSTER_TYPED_LOAD, class, 0);
                }
                use.first = a;
                use.first_state = BUSTER_CHECK_LOWER_SWEEP_SET | (op == CHECK_OP_MEMBER ? BUSTER_CHECK_LOWER_SWEEP_ADDRESS : 0);
                use.first_target = type_a;
                use.count = 1;
                break;
            }
            case CHECK_OP_CALL:
            {
                // Its CALL entry, whose call the second pass gives it; its callee first where it is evaluated, then its
                // arguments (check_lower_sweep_call's plain call).
                CheckLowerSweepCall call;
                use.takes = check_lower_sweep_call(sweep, run->root, node, &call, &direct);
                check_lower_sweep_put(entries, BUSTER_TYPED_CALL, (uint8_t)info, 0);
                calls = use.takes;
                if (calls)
                {
                    sweep->calls[run->call_count] = call;
                }
                break;
            }
            case CHECK_OP_NEGATIVE:
            case CHECK_OP_BIT_NOT:
            {
                use.takes = scalar;
                check_lower_sweep_put(entries, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
                use.first = a;
                use.first_target = sweep->conversions[node].operands[0].type;
                use.count = 1;
                break;
            }
            case CHECK_OP_COMMA:
            {
                use.takes = scalar;
                use.first = a;
                use.first_target = BUSTER_TYPE_VOID;
                use.second = b;
                use.second_state = BUSTER_CHECK_LOWER_SWEEP_SET | (address ? BUSTER_CHECK_LOWER_SWEEP_ADDRESS : 0);
                use.second_target = discard ? BUSTER_TYPE_VOID : type;
                use.count = 2;
                break;
            }
            case CHECK_OP_ASSIGN:
            case CHECK_OP_INITIALIZE:
            {
                // A scalar's store: to a local or a global it names, or through its destination's address.
                const bool destination_node = a < sweep->stream_count;
                const uint8_t destination_facts = destination_node ? (uint8_t)(check_lower_sweep_info(sweep, sweep->value_types[a]) >> 8) : 0;
                const bool bit_field = op == CHECK_OP_ASSIGN && destination_node && (sweep->value_flags[a] & BUSTER_CHECK_BIT_FIELD) != 0;
                const bool element = op == CHECK_OP_ASSIGN && destination_node && sweep->ops[a] == CHECK_OP_SUBSCRIPT && (sweep->value_flags[a] & BUSTER_CHECK_VECTOR_ELEMENT) != 0;
                // check_lower_initializer_of's declaration, where the initializers' map names the node.
                uint32_t declaration = BUSTER_CHECK_NONE;
                if (op == CHECK_OP_INITIALIZE)
                {
                    const CheckInput* input = sweep->program->input;
                    const uint32_t token = sweep->initializer_declarations[node];
                    declaration = token < input->token_count && check_sparse_get(input->initializer_nodes, token) == node ? token : BUSTER_CHECK_NONE;
                }
                const uint32_t destination = declaration != BUSTER_CHECK_NONE ? sweep->program->input->objects[declaration] :
                                             destination_node && sweep->ops[a] == CHECK_OP_VALUE ? sweep->value_objects[a] : 0;
                const bool named = destination != 0 && destination < sweep->object_count;
                const uint32_t local = named ? sweep->local_map[destination] : BUSTER_CHECK_NONE;
                const SymbolId symbol = named ? sweep->object_symbols[destination] : 0;
                use.takes = scalar && (op != CHECK_OP_ASSIGN || (destination_facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0) && !bit_field && !element;
                if (local != BUSTER_CHECK_NONE || symbol != 0)
                {
                    check_lower_sweep_put(entries, local != BUSTER_CHECK_NONE ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                                          local != BUSTER_CHECK_NONE ? local : symbol);
                    use.first = op == CHECK_OP_INITIALIZE ? a : b;
                    use.first_target = type;
                    use.count = 1;
                }
                else
                {
                    check_lower_sweep_put(entries, BUSTER_TYPED_STORE, class, 0);
                    use.takes = use.takes && op == CHECK_OP_ASSIGN;
                    use.first = a;
                    use.first_state = BUSTER_CHECK_LOWER_SWEEP_SET | BUSTER_CHECK_LOWER_SWEEP_ADDRESS;
                    use.second = b;
                    use.second_target = type;
                    use.count = 2;
                }
                break;
            }
            case CHECK_OP_SUBSCRIPT:
            case CHECK_OP_ADD:
            case CHECK_OP_SUBTRACT:
            {
                const TypeId type_a = sweep->conversions[node].operands[0].type;
                const TypeId type_b = sweep->conversions[node].operands[1].type;
                const uint32_t info_a = check_lower_sweep_info(sweep, type_a);
                const uint32_t info_b = check_lower_sweep_info(sweep, type_b);
                const bool left_pointer = ((info_a >> 8) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
                const bool right_pointer = ((info_b >> 8) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
                use.first = a;
                use.first_target = type_a;
                use.second = b;
                use.count = 2;
                if (!scalar || (sweep->value_flags[node] & BUSTER_CHECK_VECTOR_ELEMENT) != 0 || right_pointer ||
                    (op == CHECK_OP_SUBSCRIPT && !address && sweep->value_categories[node] == CHECK_LVALUE && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0))
                {
                    // An aggregate's or a vector's element, a pointer's difference, an integer plus a pointer, evaluated
                    // pointer first, and an atomic element's read: the tasks'.
                }
                else if (left_pointer)
                {
                    // check_lower_scale's: the pointer, the index converted to long, its negation for a difference, the PTR_ADD
                    // by the stride, and a subscript's load where it reads.
                    const uint64_t stride = check_action_pointer_stride(sweep->types, type_a);
                    const bool loads = op == CHECK_OP_SUBSCRIPT && !address && !address_value;
                    use.takes = stride != 0;
                    if (op == CHECK_OP_SUBTRACT)
                    {
                        check_lower_sweep_put(entries, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
                    }
                    check_lower_sweep_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, stride);
                    if (loads)
                    {
                        check_lower_sweep_put(entries, BUSTER_TYPED_LOAD, class, 0);
                    }
                    use.second_target = BUSTER_TYPE_LONG;
                }
                else
                {
                    use.takes = op != CHECK_OP_SUBSCRIPT && check_lower_sweep_plain(info_a) && check_lower_sweep_plain(info_b);
                    check_lower_sweep_put(entries, check_lower_binary(op), class, 0);
                    use.second_target = type_b;
                }
                break;
            }
            case CHECK_OP_MULTIPLY:
            case CHECK_OP_DIVIDE:
            case CHECK_OP_REMAINDER:
            case CHECK_OP_SHIFT_LEFT:
            case CHECK_OP_SHIFT_RIGHT:
            case CHECK_OP_LESS:
            case CHECK_OP_LESS_EQUAL:
            case CHECK_OP_GREATER:
            case CHECK_OP_GREATER_EQUAL:
            case CHECK_OP_EQUAL:
            case CHECK_OP_NOT_EQUAL:
            case CHECK_OP_BIT_AND:
            case CHECK_OP_BIT_OR:
            case CHECK_OP_BIT_XOR:
            {
                // An operator of scalar operands of a scalar class (check_lower_operation's entry); an unsigned division or
                // remainder by a constant power of two is check_lower_sweep_expand's.
                const TypeId type_a = sweep->conversions[node].operands[0].type;
                const TypeId type_b = sweep->conversions[node].operands[1].type;
                const uint8_t kind = check_lower_binary(op);
                general = (kind == BUSTER_TYPED_DIV || kind == BUSTER_TYPED_MOD) && (class == BUSTER_TYPED_CLASS_U32 || class == BUSTER_TYPED_CLASS_U64);
                use.takes = scalar && check_lower_sweep_plain(check_lower_sweep_info(sweep, type_a)) && check_lower_sweep_plain(check_lower_sweep_info(sweep, type_b));
                check_lower_sweep_put(entries, kind, class, 0);
                use.first = a;
                use.first_target = type_a;
                use.second = b;
                use.second_target = type_b;
                use.count = 2;
                break;
            }
            default:
            {
                // The updates, the logical operators, the builtins and the rest of check_lower_expand_rare's: the tasks'.
                break;
            }
        }
    }
    if (general)
    {
        const uint32_t row = sweep->payload_rows[node];
        const CheckValue* payload = row != 0 && row <= sweep->payload_count ? sweep->payloads + row - 1 : NULL;
        use = row == 0 || payload != NULL ? check_lower_sweep_expand(sweep, node, op, state, type, info, payload, slot->values) : (CheckLowerSweepUse){ 0 };
        own = use.entries;
        own.values = slot->values;
    }
    // Its conversion, after its own entries, unless it is an address or discarded.
    const bool takes = use.takes && (address || discard || target == 0 || target == type || check_lower_sweep_conversion(sweep, entries, type, target));
    if (takes)
    {
        const uint32_t ancestors = sweep->ancestors[distance];
        slot->kinds = own.kinds;
        slot->classes = own.classes;
        slot->count = own.count;
        run->nodes += 1;
        if (calls)
        {
            // Below the call, its row is an ancestor's: one call and its arguments. The second pass counts the calls before
            // its subtree's first node, which its callee may be, unevaluated, below every operand the statement evaluates.
            const CheckLowerSweepCall* call = sweep->calls + run->call_count++;
            const uint32_t below = ancestors + 1 + (call->count << 16);
            const uint32_t first = sweep->argument_offsets[node];
            sweep->states[distance] = state | BUSTER_CHECK_LOWER_SWEEP_CALL;
            const bool inside = call->first < BUSTER_CHECK_LOWER_SWEEP_NODES;
            run->valid = run->valid && inside;
            sweep->firsts[inside ? call->first / 64 : 0] |= inside ? UINT64_C(1) << (call->first % 64) : 0;
            run->low = run->root - call->first < run->low ? run->root - call->first : run->low;
            if (!direct)
            {
                check_lower_sweep_mark(sweep, run, node, sweep->operand0[node], BUSTER_CHECK_LOWER_SWEEP_SET, 0, below);
            }
            for (uint32_t k = 0; k < call->count; k += 1)
            {
                check_lower_sweep_mark(sweep, run, node, sweep->argument_nodes[first + k], BUSTER_CHECK_LOWER_SWEEP_SET, sweep->argument_conversions[first + k].type, below);
            }
            if (run->last == node)
            {
                run->last = call->count != 0 ? sweep->argument_nodes[first + call->count - 1] : !direct ? sweep->operand0[node] : node;
            }
        }
        else
        {
            sweep->states[distance] = state;
            if (use.count != 0)
            {
                check_lower_sweep_mark(sweep, run, node, use.first, use.first_state, use.first_target, ancestors);
            }
            if (use.count == 2)
            {
                check_lower_sweep_mark(sweep, run, node, use.second, use.second_state, use.second_target, ancestors);
            }
            // The tasks' preorder goes on to its last evaluated operand.
            if (run->last == node && use.count != 0)
            {
                run->last = use.count == 2 ? use.second : use.first;
            }
        }
    }
    else
    {
        sweep->states[distance] = state | BUSTER_CHECK_LOWER_SWEEP_TASK;
        run->tasks += 1;
        run->last_task = run->last_task || run->last == node;
    }
}

// The first pass, from the root down to the lowest node an operand reached: the evaluated nodes found by their bits, 64
// distances a word (the folded nodes' operands and the task nodes' subtrees between them have none), each one's operands'
// bits set past it before the next is found.
static inline __attribute__((always_inline)) void check_lower_sweep_down(const CheckLowerSweep* sweep, CheckLowerSweepRun* run)
{
    uint32_t distance = 0;
    while (run->valid && distance <= run->root - run->low)
    {
        const uint64_t set = sweep->bits[distance / 64] >> (distance % 64);
        if (set == 0)
        {
            distance = (distance | 63) + 1;
        }
        else
        {
            distance += (uint32_t)__builtin_ctzll(set);
            check_lower_sweep_node(sweep, run, run->root - distance, distance, sweep->states[distance]);
            distance += 1;
        }
    }
}

// The highest distance below `limit` whose bit `words` has, or UINT32_MAX where none has.
static inline __attribute__((always_inline)) uint32_t check_lower_sweep_highest(const uint64_t* words, uint32_t limit)
{
    uint32_t word = limit / 64;
    uint64_t set = limit % 64 != 0 ? words[word] & ((UINT64_C(1) << (limit % 64)) - 1) : 0;
    while (set == 0 && word > 0)
    {
        word -= 1;
        set = words[word];
    }
    return set != 0 ? word * 64 + 63 - (uint32_t)__builtin_clzll(set) : UINT32_MAX;
}

static bool check_lower_sweep_up_general(CheckLowerWriter* writer, const CheckLowerSweep* sweep, const CheckLowerSweepRun* run);

// The second pass, from the lowest node up: each node's slot, or its task with its conversion's drained; and each call's
// row at its preorder's place, after the rows of the statement's calls before its subtree's first node (`run_calls` and
// `run_arguments` at that position, the counts before it) and of its ancestors. Whether the tasks lowered it all.
static inline __attribute__((always_inline)) bool check_lower_sweep_up(CheckLowerWriter* writer, const CheckLowerSweep* sweep, const CheckLowerSweepRun* run)
{
    const uint32_t room = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES;
    const uint32_t span = run->root - run->low + 1;
    uint32_t count = writer->count;
    bool going = true;
    if (run->call_count == 0 && run->tasks == 0 && count <= room && room - count >= (run->nodes + 1) * BUSTER_CHECK_LOWER_SWEEP_MOST)
    {
        // Slots alone, with room for every one: copied in node order.
        uint8_t* kinds = writer->kinds;
        uint8_t* classes = writer->classes;
        uint64_t* values = writer->values;
        for (uint32_t word = (span - 1) / 64 + 1; word-- > 0;)
        {
            uint64_t set = sweep->bits[word];
            while (set != 0)
            {
                const uint32_t distance = word * 64 + 63 - (uint32_t)__builtin_clzll(set);
                const CheckLowerSweepSlot* slot = sweep->slots + distance;
                const uint32_t entries = slot->count;
                set &= ~(UINT64_C(1) << (distance % 64));
                memcpy(kinds + count, &slot->kinds, sizeof(uint64_t));
                memcpy(classes + count, &slot->classes, sizeof(uint64_t));
                values[count + 0] = slot->values[0];
                values[count + 1] = slot->values[1];
                values[count + 2] = slot->values[2];
                values[count + 3] = slot->values[3];
                for (uint32_t k = 4; k < entries; k += 1)
                {
                    values[count + k] = slot->values[k];
                }
                count += entries;
            }
        }
        writer->count = count;
    }
    else
    {
        going = check_lower_sweep_up_general(writer, sweep, run);
    }
    return going;
}

// check_lower_sweep_up's statement of calls or task nodes, or near the entries' room.
static __attribute__((noinline)) bool check_lower_sweep_up_general(CheckLowerWriter* writer, const CheckLowerSweep* sweep, const CheckLowerSweepRun* run)
{
    CheckLowerAbi* abi = writer->abi;
    const CheckStream* stream = writer->program->stream;
    uint8_t* kinds = writer->kinds;
    uint8_t* classes = writer->classes;
    uint64_t* values = writer->values;
    uint32_t* run_calls = writer->sweep_run_calls;
    uint32_t* run_arguments = writer->sweep_run_arguments;
    const uint32_t call_base = writer->call_count;
    const uint32_t argument_base = writer->argument_count;
    const uint32_t abi_base = abi->count;
    const uint32_t abi_argument_base = abi->argument_count;
    const uint32_t room = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES;
    const uint32_t span = run->root - run->low + 1;
    uint32_t calls = 0;
    uint32_t arguments = 0;
    uint32_t next_call = run->call_count;
    uint32_t count = writer->count;
    bool going = true;
    // The evaluated nodes by their bits, from the statement's last word down; at each, the counts before the calls'
    // subtrees' first nodes at or above it (`query`, the highest one left).
    uint32_t query = check_lower_sweep_highest(sweep->firsts, span);
    for (uint32_t word = (span - 1) / 64 + 1; going && word-- > 0;)
    {
        uint64_t set = sweep->bits[word];
        while (going && set != 0)
        {
            const uint32_t distance = word * 64 + 63 - (uint32_t)__builtin_clzll(set);
            const uint8_t state = sweep->states[distance];
            set &= ~(UINT64_C(1) << (distance % 64));
            while (query != UINT32_MAX && query >= distance)
            {
                run_calls[query] = calls;
                run_arguments[query] = arguments;
                query = check_lower_sweep_highest(sweep->firsts, query);
            }
            if ((state & BUSTER_CHECK_LOWER_SWEEP_TASK) != 0)
            {
                // check_lower_expression's tasks for the node, its rows from its place in the tasks' preorder.
                const uint32_t node = run->root - distance;
                const uint32_t ancestors = sweep->ancestors[distance];
                const uint32_t call_at = call_base + calls + (ancestors & 0xffffu);
                const uint32_t argument_at = argument_base + arguments + (ancestors >> 16);
                const bool address = (state & BUSTER_CHECK_LOWER_SWEEP_ADDRESS) != 0;
                const bool discard = (state & BUSTER_CHECK_LOWER_SWEEP_DISCARD) != 0;
                writer->count = count;
                writer->call_count = call_at;
                writer->argument_count = argument_at;
                abi->count = abi_base + (call_at - call_base);
                abi->argument_count = abi_argument_base + (argument_at - argument_base);
                if (!address && !discard)
                {
                    check_lower_convert(writer, stream->value_types[node], sweep->targets[distance]);
                }
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_EXPRESSION, .node = node, .address = address, .discard = discard });
                check_lower_drain(writer);
                calls += writer->call_count - call_at;
                arguments += writer->argument_count - argument_at;
                count = writer->count;
                going = writer->output->status == CHECK_PROGRAM_OK;
            }
            else
            {
                CheckLowerSweepSlot* slot = sweep->slots + distance;
                if ((state & BUSTER_CHECK_LOWER_SWEEP_CALL) != 0)
                {
                    // check_lower_call_body's row of a plain call, at its preorder's place.
                    const CheckLowerSweepCall* call = sweep->calls + --next_call;
                    const uint32_t ancestors = sweep->ancestors[distance];
                    const uint32_t at = call_base + run_calls[call->first] + (ancestors & 0xffffu);
                    const uint32_t argument_at = argument_base + run_arguments[call->first] + (ancestors >> 16);
                    const uint32_t signature = abi_base + (at - call_base);
                    const uint32_t abi_at = abi_argument_base + (argument_at - argument_base);
                    if (signature >= abi->capacity || call->count > abi->argument_capacity - abi_at)
                    {
                        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
                        going = false;
                    }
                    else
                    {
                        writer->call_targets[at] = call->symbol;
                        writer->call_first[at] = argument_at;
                        writer->call_counts[at] = call->count;
                        writer->call_variadic[at] = call->variadic;
                        writer->call_result_objects[at] = 0;
                        abi->first[signature] = abi_at;
                        abi->results[signature] = call->result;
                        abi->counts[signature] = call->count;
                        abi->named[signature] = call->named;
                        abi->variadic[signature] = call->variadic;
                        for (uint32_t k = 0; k < call->count; k += 1)
                        {
                            const TypeId argument = stream->argument_conversions[call->arguments + k].type;
                            abi->arguments[abi_at + k] = argument;
                            writer->argument_sizes[argument_at + k] = 0;
                            writer->argument_classes[argument_at + k] = (uint8_t)check_lower_sweep_info(sweep, argument);
                        }
                        slot->values[0] = at;
                        calls += 1;
                        arguments += call->count;
                    }
                }
                if (count > room || room - count < BUSTER_CHECK_LOWER_SWEEP_MOST)
                {
                    check_lower_resource(writer, count >= BUSTER_TIER0_MAX_ENTRIES ? "Tier 0 per-function entry limit" : "lowering entry storage");
                    going = false;
                }
                else if (going)
                {
                    // The slot's words whole, and its first four values, past the node's entries too, which the next ones
                    // overwrite: the columns have room past their capacity for them (check_lower_writer_begin). Each load
                    // reads what one store wrote whole.
                    const uint32_t entries = slot->count;
                    memcpy(kinds + count, &slot->kinds, sizeof(uint64_t));
                    memcpy(classes + count, &slot->classes, sizeof(uint64_t));
                    values[count + 0] = slot->values[0];
                    values[count + 1] = slot->values[1];
                    values[count + 2] = slot->values[2];
                    values[count + 3] = slot->values[3];
                    for (uint32_t k = 4; k < entries; k += 1)
                    {
                        values[count + k] = slot->values[k];
                    }
                    count += entries;
                }
            }
        }
    }
    writer->count = count;
    writer->call_count = call_base + calls;
    writer->argument_count = argument_base + arguments;
    abi->count = abi_base + calls;
    abi->argument_count = abi_argument_base + arguments;
    return going;
}

// The writer's columns for the sweep (CheckLowerWriter.sweep_view), which each statement's copies, from its program and its
// own scratch: made once a part, as nothing is written into the writer's room as it is made (check_lower_writer_begin).
static void check_lower_sweep_view(CheckLowerWriter* writer)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const CheckStream* stream = program->stream;
    *writer->sweep_view = (CheckLowerSweep){
        .writer = writer,
        .program = program,
        .types = program->types,
        .ops = graph->ops,
        .operand0 = graph->operand0,
        .operand1 = graph->operand1,
        .argument_counts = graph->argument_counts,
        .argument_offsets = graph->argument_offsets,
        .argument_nodes = graph->argument_nodes,
        .graph_types = graph->types,
        .value_types = stream->value_types,
        .value_flags = stream->value_flags,
        .value_objects = stream->value_objects,
        .value_categories = stream->value_categories,
        .value_constants = stream->value_constants,
        .payload_rows = stream->payload_rows,
        .payloads = stream->payloads,
        .conversions = stream->conversions,
        .argument_conversions = stream->argument_conversions,
        .diagnostics = stream->diagnostics,
        .member_offsets = stream->member_offsets,
        .type_templates = writer->type_templates,
        .local_map = writer->local_map,
        .indirect = writer->indirect,
        .local_flags = writer->local_flags,
        .object_symbols = program->object_symbols,
        .declaration_bits = input->declaration_bits,
        .initializer_declarations = writer->initializer_declarations,
        .bits = writer->sweep_bits,
        .firsts = writer->sweep_firsts,
        .states = writer->sweep_states,
        .targets = writer->sweep_targets,
        .ancestors = writer->sweep_ancestors,
        .slots = writer->sweep_slots,
        .calls = writer->sweep_calls,
        .payload_count = stream->payload_count,
        .stream_count = stream->count,
        .object_count = program->object_count,
        .type_count = writer->type_count,
    };
}

// Lowers expression `root`, converted to `target` (check_lower_expression's, as a value), by the sweep, with the task stack
// empty: whether it took the statement. One it doesn't take leaves the writer as it was (but its locals' addresses taken,
// which the tasks' lowering of it takes too), for the tasks.
static bool check_lower_sweep(CheckLowerWriter* writer, uint32_t root, TypeId target)
{
    const CheckLowerSweep* sweep = writer->sweep_view;
    CheckLowerSweepRun run = {
        .root = root,
        .low = root,
        .last = root,
        .valid = root < sweep->stream_count && writer->depth == 0 && writer->output->status == CHECK_PROGRAM_OK && writer->chain == BUSTER_CHECK_NONE &&
                 writer->tail == BUSTER_CHECK_NONE,
    };
    if (run.valid)
    {
        sweep->bits[0] = 1;
        sweep->states[0] = BUSTER_CHECK_LOWER_SWEEP_SET;
        sweep->targets[0] = target;
        sweep->ancestors[0] = 0;
        check_lower_sweep_down(sweep, &run);
    }
    if (run.valid)
    {
        writer->sweep_nodes += run.nodes;
        writer->expression_nodes += run.nodes;
        if (check_lower_sweep_up(writer, sweep, &run) && !run.last_task)
        {
            // The tasks' last expansion's token: the statement's last node in their preorder, where the sweep lowered it (a
            // task node there left the drain's).
            writer->token = run.last | BUSTER_CHECK_LOWER_TOKEN_NODE;
        }
    }
    // The bits it set, all within its span.
    const uint32_t span = root < sweep->stream_count ? root - run.low + 1 : 0;
    const uint32_t words = span != 0 ? ((span < BUSTER_CHECK_LOWER_SWEEP_NODES ? span : BUSTER_CHECK_LOWER_SWEEP_NODES) - 1) / 64 + 1 : 0;
    for (uint32_t word = 0; word < words; word += 1)
    {
        sweep->bits[word] = 0;
        sweep->firsts[word] = 0;
    }
    return run.valid;
}

// Lowers control `index` of the body being lowered where it is an expression statement, a branch, a return of a scalar or
// of nothing, or a scalar local's initializer of one value (check_lower_control's cases for them): the expression by the
// sweep, or by the tasks where the sweep doesn't take it, with the tasks drained. Whether the control was one of those.
static bool check_lower_sweep_control(CheckLowerWriter* writer, uint32_t index)
{
    const CheckProgram* program = writer->program;
    const CheckControl control = program->controls[writer->body->first + index];
    bool handled = false;
    if (control.kind == CHECK_CONTROL_EVALUATE || control.kind == CHECK_CONTROL_BRANCH_FALSE || control.kind == CHECK_CONTROL_BRANCH_TRUE)
    {
        const uint8_t branch = control.kind == CHECK_CONTROL_BRANCH_FALSE ? BUSTER_TYPED_BRANCH_FALSE : BUSTER_TYPED_BRANCH_TRUE;
        const TypeId target = control.kind == CHECK_CONTROL_EVALUATE ? BUSTER_TYPE_VOID : 0;
        check_lower_place(writer, index);
        writer->token = control.token;
        writer->statement = control.kind == CHECK_CONTROL_EVALUATE ? control.node : BUSTER_CHECK_NONE;
        if (control.node == BUSTER_CHECK_NONE || !check_lower_sweep(writer, control.node, target))
        {
            if (control.kind != CHECK_CONTROL_EVALUATE)
            {
                check_lower_instruction(writer, branch, BUSTER_TYPED_CLASS_I32, control.target);
            }
            if (control.node != BUSTER_CHECK_NONE)
            {
                check_lower_expression(writer, control.node, false, target);
            }
            check_lower_drain(writer);
        }
        else if (control.kind != CHECK_CONTROL_EVALUATE && writer->output->status == CHECK_PROGRAM_OK)
        {
            check_lower_emit(writer, branch, BUSTER_TYPED_CLASS_I32, control.target);
        }
        handled = true;
    }
    else if (control.kind == CHECK_CONTROL_RETURN && control.target != BUSTER_CHECK_CONTROL_KEPT && control.target != BUSTER_CHECK_CONTROL_TAIL &&
             !check_lower_aggregate(writer, writer->result) && writer->hidden == BUSTER_CHECK_NONE && writer->result_object == BUSTER_CHECK_NONE)
    {
        const uint8_t class = check_lower_class(writer, writer->result);
        check_lower_place(writer, index);
        writer->token = control.token;
        if (control.node == BUSTER_CHECK_NONE || !check_lower_sweep(writer, control.node, 0))
        {
            check_lower_instruction(writer, BUSTER_TYPED_RETURN, class, 0);
            if (control.node != BUSTER_CHECK_NONE)
            {
                check_lower_expression(writer, control.node, false, 0);
            }
            check_lower_drain(writer);
        }
        else if (writer->output->status == CHECK_PROGRAM_OK)
        {
            check_lower_emit(writer, BUSTER_TYPED_RETURN, class, 0);
        }
        handled = true;
    }
    else if (control.kind == CHECK_CONTROL_INITIALIZE)
    {
        // check_lower_initializer's scalar local of one value part: SET_LOCAL after its value.
        const CheckInitializers* initializers = program->initializers;
        const uint32_t declaration = control.token;
        const bool planned = initializers != 0 && control.target < initializers->count && declaration < program->input->token_count;
        const uint32_t local = planned ? check_lower_local_of(writer, program->input->objects[declaration]) : BUSTER_CHECK_NONE;
        const CheckInitializer* plan = planned ? &initializers->plans[control.target] : NULL;
        const CheckInitPart* write = plan != NULL && plan->count == 1 ? plan->parts : NULL;
        const uint8_t class = plan != NULL ? check_lower_class(writer, plan->type) : UINT8_MAX;
        if (local != BUSTER_CHECK_NONE && writer->local_sizes[local] == 0 && class != UINT8_MAX && write != NULL && write->kind == CHECK_INIT_VALUE &&
            write->bit_offset == 0 && write->bit_width == 0 && write->overload_candidate == 0)
        {
            check_lower_place(writer, index);
            writer->token = write->token;
            if (!check_lower_sweep(writer, write->node, write->conversion.type))
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                check_lower_expression(writer, write->node, false, write->conversion.type);
                check_lower_drain(writer);
            }
            else if (writer->output->status == CHECK_PROGRAM_OK)
            {
                check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, local);
            }
            handled = true;
        }
    }
    return handled;
}

#endif
