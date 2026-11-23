// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2524's node machine where it sits (docs/node-machine.md, "T where it sits"): T run in the reader, on the full
// expressions its own front makes whole (c_check_node_machine_front.c), before the unit's program exists. Step 2 puts the
// machine there; every figure of step 1 has its sweeps run after the pipeline, on the finished graph and with the
// program's columns. This is the shadow of the first of them at reader time: no compile takes it
// (measure-node-machine --shadow --front-types, PPOptions.node_front's BUSTER_NODE_FRONT_TYPES).
//
// The coded walk runs after the reader has bound the whole unit's names and made its declarations' types, so the unit's
// types and its tags' history are whole by then. What isn't there is what the graph run and the program make:
//
// - The types made after the machine began: the walk's as it reads (a string literal's array's) and T's own derivations'
//   (a decayed array's pointer, an address's), which the graph run would have made before a machine begun after the
//   pipeline. Their words are taken into the table before a rule reads them (node_machine_settle).
// - The program's columns T's callee refusal reads: an object's last static declaration, whether one gives it
//   returns_twice or an asm label, and whether it has a symbol. The first three are made here from the reader's columns
//   as the program makes its own (node_machine_begin_reader); a symbol is the selection's, so a static declaration stands
//   for it (node_machine_symbolled).
// - The stream: a library call that the code computes inline asks whether its size is a small constant, of T's own record
//   of the size here (node_machine_reader_call_library).
// - A node's context: the walk marks a constant context's nodes once it has read the construct they are in, so T reads the
//   contexts the front gave its records (NodeFrontShadow.contexts), which are the walk's at the unit's end.
//
// T runs right after the walk acted at a chunk, on the chunk's roots, a chunk's expressions a run
// (node_machine_reader_chunk). Each expression's records are digested (node_machine_digest) and kept by its root's node;
// after the pipeline the shadow's own T runs on the same expression and the two digests are compared
// (c_check_node_census.c's node_shadow_typed).
//
// C and X run there too where the options ask (--front-sweeps, BUSTER_NODE_FRONT_SWEEPS; "C and X where they sit"), on
// each expression T took. They read T's records and two things besides:
//
// - An object's symbol: whether it has one (a global, or a local the function numbers) and, for a global's entries, the
//   symbol. It is the selection's numbering, which isn't there yet. An object with a static declaration has itself in
//   the symbols' place (NodeMachine.object_symbols), so X writes the object where it would write the symbol: the entries
//   of a global's kinds (BUSTER_TYPED_GLOBAL, _GLOBAL_ADDRESS, _STORE_GLOBAL, _SET_GLOBAL) and a direct call's row are
//   what a splice patches once the symbols are numbered.
// - What the root's consumer gives it, which after the pipeline is the lowering's pop's: no context, and a conversion's
//   target for an initialization alone, its object's type where the value's conversion to it isn't empty
//   (node_machine_reader_target).
//
// Each taken expression's tape is kept (NodeFrontTypes' store), with a digest of its labels and calls; after the pipeline
// the shadow's own C and X run on the same expression, and the kept tape, patched with the program's symbols, must be
// theirs entry for entry (c_check_node_census.c's node_shadow_swept).
//
// The census's set and its silence tests run there too where the options ask (--front-tests, PPOptions.node_reader's
// BUSTER_NODE_READER_TESTS; "The tests where they sit"): after T takes an expression and before C and X, the census's own
// test code on T's records (c_check_node_census.c's node_census_reader_expression, whose view reads a record where the
// census reads the stream's node). Their bits are the machine's verdict before the pipeline, kept by the root's node;
// C and X run on the expressions no test refuses. After the pipeline the census's verdict for the same root is compared
// test by test (node_shadow_tested).
#ifndef BUSTER_C_CHECK_NODE_MACHINE_READER_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_READER_INCLUDED

// The types' words' room at reader time past the unit's own types: the types T's derivations make (1,265 in sqlite3.c;
// a type past the room has no word, as after the pipeline, and shows in the shadow's comparison).
#define BUSTER_NODE_MACHINE_LATE_TYPES 16384u

// The machine at reader time: its state, the reader's graph as T reads it there (its columns, with the front's contexts in
// place of the graph's own) and its results.
struct NodeMachineReader
{
    NodeMachine machine;
    CheckGraph graph;
    NodeFrontTypes* types;
    // The unit's strings whose objects have their symbols' stand-ins (node_machine_reader_strings).
    uint32_t strings;
    // The census's tests at reader time (c_check_node_census.c's), or null.
    struct NodeCensusReader* tests;
    // Whether `graph` is the machine's own, made of the front's records alone (c_check_node_machine_alone.c): a chunk's
    // run then leaves it as it stands, where it otherwise takes the reader's graph as the walk has left it.
    bool alone;
    // With `alone`: the chunk has one run, which keeps what a compile would keep of a root (its records' place, its
    // verdicts, its tape and what is beside it), with no digest and no count (BUSTER_NODE_READER_PRODUCE_LEAN: the
    // producer as a compile would run it). Nothing of a root is then compared with the walk's.
    bool keeps;
    // #2850's fact words' kernel at reader time (c_check_node_machine_facts.c's), or null.
    struct NodeFactsReader* facts;
};

// The fact words' kernel at reader time (c_check_node_machine_facts.c, which follows this file in the unit): its state
// for the unit `reader` reads, whose machine there is `machine`, its counts kept in `types`; the machine's current
// expression, which T has taken (nodes `first` to its root `root`, `count` records; the front's root `record`, whose
// operand's record is `operand`; `taken`: the whole machine takes it there), appended to the chunk's windows; and, once T
// has run on each of a chunk's roots, the kernel on the chunk's windows, checked against T's records and timed.
struct NodeFactsReader;
static struct NodeFactsReader* node_facts_reader_begin(CheckInputReader* reader, NodeMachine* machine, NodeFrontTypes* types);
// What the reference's C did with the expression at hand, for C on the window (BUSTER_NODE_READER_WINDOW_CONTEXTS):
// whether it ran (0: it didn't; 1: it took the expression; 2: it refused it for a template), its ticks, and the
// root's target.
static void node_facts_reader_contexts(struct NodeFactsReader* state, uint8_t ran, uint64_t ticks, TypeId target, bool sense);
static void node_facts_reader_rows(struct NodeFactsReader* state, uint64_t ticks);
static void node_facts_reader_expression(struct NodeFactsReader* state, const NodeFront* front, uint32_t first, uint32_t root, uint32_t count, uint32_t record,
                                         uint32_t operand, bool taken);
static void node_facts_reader_chunk(struct NodeFactsReader* state, const NodeFront* front);
// The unit's tests' state given to the kernel's, which then runs the tests on each chunk's window too
// (BUSTER_NODE_READER_WINDOW_TESTS).
static void node_facts_reader_tests(struct NodeFactsReader* state, struct NodeCensusReader* tests);

// The census's tests at reader time (c_check_node_census.c, which follows this file in the unit): their state for the
// unit `reader` reads, whose machine there is `machine`, with columns for `node_capacity` nodes and the unit's policy
// kept in `types`; the reader's warning events since the last chunk taken in; and the tests of the
// machine's current expression, which T has just taken, whose root is node `root`, made by the front's root of operator
// `op` at token `token`, an initialization's of the object declared at `declaration` (BUSTER_CHECK_NONE otherwise) with
// type `declared`: their bits, and into `kind` the root's kind. They are the census's code's on T's records; or, where
// `given` is what the tests' loop on the window made of the expression (node_tests_window_chunk), the machine's own
// form's from that.
struct NodeCensusReader;
static struct NodeCensusReader* node_census_reader_begin(const CheckInputReader* reader, const NodeMachine* machine, NodeFrontTypes* types, uint32_t node_capacity);
static void node_census_reader_chunk(struct NodeCensusReader* tests, const CheckInputReader* reader);
struct NodeTestsLoop;
static uint64_t node_census_reader_expression(struct NodeCensusReader* tests, uint32_t root, CheckOp op, uint32_t token, uint32_t declaration, TypeId declared, uint8_t* kind,
                                              const struct NodeTestsLoop* given);
typedef struct NodeMachineReader NodeMachineReader;
_Static_assert(NODE_MACHINE_REFUSALS <= 16 && NODE_MACHINE_REFUSALS <= BUSTER_NODE_FRONT_TYPED_REFUSAL + 1, "a refusal is NodeFrontTypes.refusals' low four bits");

// The types made since the machine's table was last settled, taken into it: their words and their completion positions,
// as node_machine_setup makes the unit's; their pointers' columns are empty as they were made. At reader time alone: the
// walk makes types as it reads (a string literal's array's), and T's own derivations do (a decayed array's pointer, an
// address's), which the graph run would have made before a machine begun after the pipeline. T settles after each type
// it makes (node_machine_pointer), so a rule never reads a word that isn't there; the reader's hook settles the walk's
// before each chunk's expressions. A type past the arrays' room has no word, as any past the table after the pipeline.
static void node_machine_settle(NodeMachine* machine)
{
    const TypeTable* types = machine->types;
    const uint32_t count = types->types.count < machine->type_capacity ? types->types.count : machine->type_capacity;
    for (uint32_t index = machine->type_count; index < count; index += 1)
    {
        const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
        const uint32_t word = node_machine_make_word(types, index, check_lower_type_class(types, type), check_lower_type_facts(types, type));
        machine->type_words[index] = word;
        machine->type_complete_at[index] = (word & BUSTER_NODE_MACHINE_WORD_VALID) != 0 ? node_machine_complete_at(types, machine->graph, type) : UINT32_MAX;
    }
    if (count > machine->type_count)
    {
        machine->late_types += count - machine->type_count;
        machine->type_count = count;
    }
}

// check_program_call_library's answer at reader time, for call node `node`, record `r`, whose arguments T has typed: the
// reader marked it (BUSTER_CHECK_LIBRARY_CALL), and its row folds, or the code computes it (check_program_call_marked's
// steps: its prototype's arguments, a memory builtin's size a constant of at most BUSTER_CBUILTIN_LIBRARY_INLINE_BYTES,
// its callee without an asm label), with the size's and the callee's values read of T's records where the program reads
// the stream's.
static bool node_machine_reader_call_library(const NodeMachine* machine, uint32_t r, uint32_t node)
{
    const CheckGraph* graph = machine->graph;
    const uint32_t member = graph->members[node];
    const uint32_t index = member & ~BUSTER_CHECK_LIBRARY_CALL;
    const bool library = (member & BUSTER_CHECK_LIBRARY_CALL) != 0 && index < BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS;
    bool own = library && cbuiltin_library_plain_folds[index].fold != CBUILTIN_LIBRARY_FOLD_NONE;
    if (library && !own)
    {
        const CBuiltinLibraryComputed computed = cbuiltin_library_plain_folds[index].computed;
        const bool memory = computed == CBUILTIN_LIBRARY_COMPUTED_COPY || computed == CBUILTIN_LIBRARY_COMPUTED_MOVE || computed == CBUILTIN_LIBRARY_COMPUTED_SET;
        const bool unary = computed == CBUILTIN_LIBRARY_COMPUTED_ABSOLUTE || computed == CBUILTIN_LIBRARY_COMPUTED_FLOAT_ABSOLUTE;
        const uint32_t arity = memory ? 3 : unary ? 1 : 2;
        bool marked = computed != CBUILTIN_LIBRARY_COMPUTED_NONE && graph->argument_counts[node] == arity;
        if (marked && memory)
        {
            // check_program_constant_operand's test, of the size's record.
            const uint32_t size = node_machine_record_argument(machine, r, 2);
            const CheckOp op = size < r ? node_machine_record_op(machine, size) : CHECK_OP_BUILTIN_QUERY;
            marked = op != CHECK_OP_BUILTIN_QUERY && op != CHECK_OP_STATEMENT_VALUE && op != CHECK_OP_INITIALIZE && node_machine_record_category(machine, size) == CHECK_RVALUE &&
                     node_machine_record_constant_kind(machine, size) == CHECK_CONSTANT_INTEGER && node_machine_record_fault(machine, size) == CHECK_FAULT_NONE &&
                     (node_machine_record_flags(machine, size) & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 &&
                     node_machine_record_integer(machine, size) <= cbuiltin_library_inline_bytes(computed);
        }
        const uint32_t callee = node_machine_record_left(machine, r);
        const uint32_t object = marked && callee < r ? node_machine_record_object(machine, callee) : 0;
        const bool labelled = object != 0 && object < machine->reader_objects && (machine->reader_facts[object] & BUSTER_NODE_MACHINE_OBJECT_LABELLED) != 0;
        own = marked && !labelled;
    }
    if (machine->reader_library != NULL)
    {
        machine->reader_library[node] = own ? 2 : 1;
    }
    return own;
}

// The machine's columns at reader time, for the unit whose input is `input`, read so far as the coded walk finds it: its
// declarations bound, its types made, its graph `graph` (the reader's, or a view of it) being written. From `arena`;
// whether there was room. The program's columns T reads are made from the input's as the program makes them, into
// `declarations` and `facts` (the caller's, an entry an object: the unit's token count and one): each static object's
// last declaration (check_program_static_objects' visit, its returns_twice by check_program_static_object's test) and
// the objects with an asm label the asm pass will keep (check_program_labelled_objects' test of the labels
// check_input_asm_labels leaves); and the names that return twice from
// `identifiers` (check_lower_twice_names'). `symbols` (the caller's too) is what stands for the objects' symbols: each
// object with a static declaration has itself there, the others 0.
static bool node_machine_begin_reader(NodeMachine* machine, const CheckInput* input, const Parser* parser, TypeTable* types, const CheckGraph* graph,
                                      const InternTable* identifiers, uint32_t* declarations, uint8_t* facts, uint32_t* symbols, bool verify, Arena* arena)
{
    const uint32_t count = input->token_count;
    *machine = (NodeMachine){
        .input = input,
        .parser = parser,
        .object_symbols = symbols,
        .declaration_bits = input->declaration_bits,
        .object_count = count + 1,
    };
    const uint32_t late = types->types.capacity - types->types.count < BUSTER_NODE_MACHINE_LATE_TYPES ? types->types.capacity - types->types.count : BUSTER_NODE_MACHINE_LATE_TYPES;
    const bool room = node_machine_setup(machine, types, graph, types->types.count + late, false, verify, arena);
    machine->overloads = node_machine_overloads(input, graph);
    machine->renames = node_machine_renames(input);
    machine->reader_declarations = declarations;
    machine->reader_facts = facts;
    if (room)
    {
        machine->reader_objects = count + 1;
        memset(machine->reader_declarations, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(machine->reader_facts, 0, (uint64_t)count + 1);
        memset(symbols, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t object = input->declarations[token] == token ? input->objects[token] : 0;
            if (object != 0 && object <= count)
            {
                const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
                machine->reader_facts[object] |= BUSTER_NODE_MACHINE_OBJECT_DECLARED;
                if (check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC))
                {
                    machine->reader_declarations[object] = token;
                    symbols[object] = object;
                    const uint32_t bits = attributes != NULL && input->declaration_kinds[token] == C_BINDING_FUNCTION ? check_program_attribute_bits(attributes) : 0;
                    machine->reader_facts[object] |= (bits & BUSTER_CHECK_PROGRAM_ATTRIBUTE_RETURNS_TWICE) != 0 ? BUSTER_NODE_MACHINE_OBJECT_TWICE : 0;
                    machine->reader_facts[object] |= (bits & BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALWAYS_INLINE) != 0 ? BUSTER_NODE_MACHINE_OBJECT_INLINED : 0;
                    // A function with a target of its own (a target, its clones, a processor's version or its dispatch):
                    // no machine reads the attribute's string here, and the unit is taken for one whose target fuses a
                    // product and a sum (NodeMachine.fuses).
                    machine->fuses |= (bits & BUSTER_CHECK_PROGRAM_ATTRIBUTE_TARGET) != 0;
                }
                // The asm pass, which runs after the bodies, ignores a label on a declaration of an object that a
                // declaration before it defines, where none before it kept a label (check_input_asm_labels): the same of
                // the declarations in their order (#3207). A label kept before stays the object's whatever this one's
                // fate, and a register variable, whose label the pass never asks, has one declaration.
                const bool kept = (machine->reader_facts[object] & BUSTER_NODE_MACHINE_OBJECT_DEFINED) == 0;
                if (input->asm_capacity != 0 && attributes != NULL && check_input_attribute_of(attributes, CHECK_ATTRIBUTE_ASM_LABEL) != NULL && kept)
                {
                    machine->reader_facts[object] |= BUSTER_NODE_MACHINE_OBJECT_LABELLED;
                }
                machine->reader_facts[object] |= check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED) ? BUSTER_NODE_MACHINE_OBJECT_DEFINED : 0;
            }
        }
        for (uint32_t k = 0; k < BUSTER_CHECK_LOWER_TWICE_NAMES; k += 1)
        {
            const BuiltinIntrinsicName* entry = check_lower_twice_entries + k;
            machine->twice_names[k] = identifiers != NULL ? intern_find(identifiers, (const uint8_t*)entry->name, entry->length) : 0;
        }
        static const char jmp_buf_tag[] = "__jmp_buf_tag";
        machine->twice_tag = identifiers != NULL ? intern_find(identifiers, (const uint8_t*)jmp_buf_tag, sizeof(jmp_buf_tag) - 1) : 0;
    }
    return room;
}

static inline uint64_t node_machine_mix(uint64_t digest, uint64_t value)
{
    const uint64_t mixed = (digest ^ value) * UINT64_C(0x9e3779b97f4a7c15);
    return mixed ^ (mixed >> 29);
}

// A digest of what T made of the machine's current expression, whose refusal was `refusal`: the refusal, and of every
// record T typed whole (all of a taken expression's, those before the one a refusal stopped at) its operator and
// operands, its value whole, its type's word, its operands' conversions, its member's offset, its subtree's least
// record, and a call's arguments with their conversions: what C, X and the shadow read of T.
static uint64_t node_machine_digest(const NodeMachine* machine, NodeMachineRefusal refusal)
{
    const NodeMachineColumns* columns = machine->columns;
    const uint32_t typed = machine->count == 0 ? 0 : refusal == NODE_MACHINE_TAKEN ? machine->count : machine->stopped;
    uint64_t digest = node_machine_mix((uint64_t)refusal, typed);
    for (uint32_t r = 0; r < typed; r += 1)
    {
        CheckValue value;
        node_machine_record_value(machine, r, &value);
        const CheckOp op = node_machine_record_op(machine, r);
        digest = node_machine_mix(digest, (uint64_t)op);
        digest = node_machine_mix(digest, (uint64_t)node_machine_record_left(machine, r) << 32 | node_machine_record_right(machine, r));
        digest = node_machine_mix(digest, (uint64_t)value.type << 32 | value.flags);
        digest = node_machine_mix(digest, (uint64_t)value.category << 32 | (uint64_t)value.constant_kind);
        digest = node_machine_mix(digest, (uint64_t)value.integer);
        digest = node_machine_mix(digest, (uint64_t)(value.integer >> 64));
        digest = node_machine_mix(digest, value.floating_storage[0]);
        digest = node_machine_mix(digest, (value.floating_storage[1] & 0xffffu) | (value.imaginary_storage[1] & 0xffffu) << 16);
        digest = node_machine_mix(digest, value.imaginary_storage[0]);
        digest = node_machine_mix(digest, (uint64_t)value.object << 32 | value.overload_head);
        digest = node_machine_mix(digest, (uint64_t)value.offset);
        digest = node_machine_mix(digest, (uint64_t)value.bit_width << 32 | (uint64_t)value.fault);
        // A fault's node as its record of the expression's: the nodes are another graph's where the machine reads the
        // front's records alone (c_check_node_machine_alone.c), and the record is the same in both.
        const uint32_t fault_record = value.fault == CHECK_FAULT_NONE ? value.fault_node : value.fault_node == machine->root ? machine->count - 1 :
                                                                                           value.fault_node - machine->first;
        digest = node_machine_mix(digest, (uint64_t)fault_record << 32 | value.object_bit_offset);
        digest = node_machine_mix(digest, (uint64_t)columns->words[r] << 32 | (uint64_t)columns->facts[r] << 8 | columns->value_classes[r]);
        digest = node_machine_mix(digest, (uint64_t)node_machine_record_converted(machine, r, 0) << 32 | node_machine_record_conversion_flags(machine, r, 0));
        digest = node_machine_mix(digest, (uint64_t)node_machine_record_converted(machine, r, 1) << 32 | node_machine_record_conversion_flags(machine, r, 1));
        digest = node_machine_mix(digest, node_machine_record_member_offset(machine, r) ^ (uint64_t)node_machine_record_converted_third(machine, r) << 40);
        digest = node_machine_mix(digest, node_machine_record_lowest(machine, r));
        if (op == CHECK_OP_CALL)
        {
            const uint32_t count = node_machine_record_argument_count(machine, r);
            digest = node_machine_mix(digest, count);
            for (uint32_t i = 0; i < count; i += 1)
            {
                digest = node_machine_mix(digest, node_machine_record_argument(machine, r, i));
                digest = node_machine_mix(digest, (uint64_t)node_machine_record_argument_converted(machine, r, i) << 32 | node_machine_record_argument_conversion_flags(machine, r, i));
            }
        }
    }
    return digest;
}

// A digest of what C and X made of the machine's current expression beside its tape: its labels' entries, its calls'
// records in the order X wrote them and by row, and its temporaries. (A call's row's contents are T's records' and its
// callee's symbol: node_machine_call_target.)
static uint64_t node_machine_side_digest(const NodeMachine* machine)
{
    uint64_t digest = node_machine_mix((uint64_t)machine->label_count << 32 | machine->call_count, machine->temporary_count);
    for (uint32_t label = 0; label < machine->label_count; label += 1)
    {
        digest = node_machine_mix(digest, machine->label_entries[label]);
    }
    for (uint32_t call = 0; call < machine->call_count; call += 1)
    {
        digest = node_machine_mix(digest, (uint64_t)machine->calls_written[call] << 32 | machine->call_records[call]);
    }
    return digest;
}

// Whether the machine's current expression's root's value converts to `target` as a direct assignment's source does
// without a diagnostic: node_machine_store_rule's test of its conversion (a builtin arithmetic type to another, a pointer
// to the same pointer type, a null pointer constant or a pointer node_machine_pointer_assignable takes to a pointer). An
// initialization's value owes it to its declared type, and T, which types the value without its INITIALIZE record, does
// not ask. It is narrower than what the action takes quietly (an enumeration's value, among others, is left out).
static bool node_machine_root_converts(NodeMachine* machine, TypeId target)
{
    const TypeTable* types = machine->types;
    NodeMachineOperand source;
    CheckConversion conversion;
    bool taken = machine->count != 0 && type_is_valid(types, target) && node_machine_operand_value(machine, machine->count - 1, &source, &conversion);
    if (taken)
    {
        const uint8_t target_kind = check_action_kind(types, target);
        taken = target_kind != BUSTER_TYPE_KIND_ATOMIC && check_action_kind(types, source.type) != BUSTER_TYPE_KIND_ATOMIC && target_kind != BUSTER_TYPE_KIND_VOID;
        const TypeId converted = taken ? type_conversion_operand(types, target) : target;
        taken = taken && (node_machine_assignable(machine, converted, source.type) ||
                          (check_action_kind(types, converted) == BUSTER_TYPE_KIND_POINTER &&
                           (node_machine_null(machine, &source) || node_machine_pointer_assignable(machine, converted, source.type))));
    }
    return taken;
}

// The enumeration that record `r` of the machine's current expression is of, as the enumerations' warnings read an
// operand (check_common_enum_coerced, check_common_enum_cast): its value's type where that is one; an enumerator's
// leaf's own enumeration (its type is int); a comma's second operand's; a conditional's arms', where they agree or one
// has none. 0 for none, and UINT32_MAX for arms of two enumerations or a nesting past `depth`.
static TypeId node_machine_record_enumeration(const NodeMachine* machine, uint32_t r, uint32_t depth)
{
    const TypeTable* types = machine->types;
    const CheckInput* input = machine->input;
    const CheckGraph* graph = machine->graph;
    const CheckOp op = node_machine_record_op(machine, r);
    const TypeId own = type_unqualified(type_strip_aligned(types, node_machine_record_type(machine, r)));
    TypeId result = own != 0 && type_kind(types, own) == BUSTER_TYPE_KIND_ENUM ? own : 0;
    if (op == CHECK_OP_VALUE && result == 0)
    {
        const uint32_t node = r + 1 == machine->count ? machine->root : machine->first + r;
        const uint32_t token = node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
        const uint32_t declaration = token < input->token_count && input->declarations != NULL ? input->declarations[token] : BUSTER_CHECK_NONE;
        if (declaration < input->token_count && input->declaration_kinds[declaration] == C_BINDING_ENUMERATOR)
        {
            // The innermost definition that holds the declaration and has closed before the leaf: every definition is
            // asked (they are few), since one nested in an enumerator's value or in a fixed underlying type is not in its
            // braces' order.
            uint32_t selected = 0;
            for (uint32_t i = 0; i < machine->reader_enum_count; i += 1)
            {
                const CheckInputEnumDefinition* definition = machine->reader_enums + i;
                const uint32_t candidate = definition->open < declaration && declaration < definition->close && token > definition->close ? i + 1 : 0;
                selected = candidate != 0 && (selected == 0 || definition->open > machine->reader_enums[selected - 1].open) ? candidate : selected;
            }
            // (An enumerator whose definition is not found is of an enumeration all the same: of none that another is.)
            result = selected != 0 ? type_unqualified(type_strip_aligned(types, machine->reader_enums[selected - 1].type)) : UINT32_MAX;
        }
    }
    else if ((op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA) && result == 0)
    {
        const uint32_t second = node_machine_record_right(machine, r);
        const uint32_t third = op == CHECK_OP_CONDITIONAL ? node_machine_record_third(machine, r) : UINT32_MAX;
        const TypeId a = depth == 0 ? UINT32_MAX : second != UINT32_MAX ? node_machine_record_enumeration(machine, second, depth - 1) : 0;
        const TypeId b = depth == 0 ? UINT32_MAX : third != UINT32_MAX ? node_machine_record_enumeration(machine, third, depth - 1) : 0;
        result = a != 0 && b != 0 && a != b ? UINT32_MAX : a != 0 ? a : b;
    }
    return result;
}

// Whether the conversion of record `r`'s value to `target` may be warned of for a constant it changes, where the record
// is a conditional (or a comma, by its second operand) and its arms carry the conversion (clang's
// CheckConditionalOperator; check_common_conversion_arms): an integer constant arm, but to a floating target that holds
// it exactly whatever its type (under 2^24); a floating constant arm to what is no floating type at least as large; a
// constant the columns do not hold whole; a nesting past `depth`. (T takes a conditional's conversion to a floating
// target alone so far: an integer constant arm to an integer target is refused here whatever its value.)
static bool node_machine_arm_changes(const NodeMachine* machine, uint32_t r, TypeId target, uint32_t depth)
{
    const TypeTable* types = machine->types;
    const CheckOp op = node_machine_record_op(machine, r);
    bool changes = false;
    if (op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA)
    {
        const uint32_t second = node_machine_record_right(machine, r);
        const uint32_t third = op == CHECK_OP_CONDITIONAL ? node_machine_record_third(machine, r) : UINT32_MAX;
        changes = depth == 0 || (second != UINT32_MAX && node_machine_arm_changes(machine, second, target, depth - 1)) ||
                  (third != UINT32_MAX && node_machine_arm_changes(machine, third, target, depth - 1));
    }
    else
    {
        const CheckConstantKind kind = node_machine_record_constant_kind(machine, r);
        const TypeId source = node_machine_record_type(machine, r);
        const bool target_floating = check_action_kind(types, target) == BUSTER_TYPE_KIND_FLOATING;
        if (kind == CHECK_CONSTANT_INTEGER)
        {
            uint32_t width = 0;
            bool is_signed = false;
            const bool whole = node_machine_record_wide(machine, r) == NULL && (machine->columns->standings[r] & BUSTER_NODE_MACHINE_FIELD) == 0 &&
                               check_action_integer_type(types, source, &width, &is_signed) && width != 0 && width <= 64;
            const uint64_t raw = whole ? machine->columns->payloads[r] : 0;
            const uint32_t shift = whole ? 64 - width : 0;
            const int64_t value = is_signed ? (int64_t)(raw << shift) >> shift : (int64_t)((raw << shift) >> shift);
            const bool huge = whole && !is_signed && width == 64 && (raw >> 63) != 0;
            const bool exact = whole && target_floating && !huge && value > -(INT64_C(1) << 24) && value < (INT64_C(1) << 24);
            changes = !exact;
        }
        else if (kind == CHECK_CONSTANT_FLOATING)
        {
            changes = !(target_floating && check_action_kind(types, source) == BUSTER_TYPE_KIND_FLOATING && type_size(types, target) >= type_size(types, source));
        }
    }
    return changes;
}

// Whether the walk's actions or the passes after it may warn of the machine's current expression for what neither T's
// rules nor the census's tests at reader time stand for (BUSTER_NODE_FRONT_SIDE_LOUD), by T's records:
// - two enumerations meeting: an operator's two operands (an arithmetic, bitwise, shift or comparison operator's, their
//   assignments', a conditional's arms) of different enumerations, or one of an enumeration and one of a floating type
//   (check_common_enum_arithmetic); and a value of an enumeration converted to another (an operand's conversion, an
//   assignment's stored value, a call's argument, a return's: check_common_enum_cast). An enumerator is of its own
//   enumeration there, though its type is int, which is what the census's test of the operands' types does not see;
// - a conditional whose conversion to another type changes a constant arm (node_machine_arm_changes).
// `initialized` is the type of the object the root's value initializes (0 for a root that is no initialization's): that
// conversion is asked of the root's own record.
// The first is asked only in a unit that defines an enumeration (NodeMachine.reader_enums), and the second only of an
// expression with a conditional. It refuses more than is warned of: the ranges that keep clang from comparing two enumerations
// (check_common_enum_cast_reached), an unnamed enumeration and a compound assignment's silence are not read.
static bool node_machine_root_loud(const NodeMachine* machine, TypeId initialized)
{
    const TypeTable* types = machine->types;
    const CheckInput* input = machine->input;
    const uint32_t count = machine->count;
    const bool enums = input != NULL && machine->reader_enum_count != 0;
    bool conditional = false;
    for (uint32_t r = 0; r < count; r += 1)
    {
        conditional |= machine->columns->codes[r] == CHECK_OP_CONDITIONAL;
    }
    bool loud = false;
    for (uint32_t r = 0; (enums || conditional) && r < count && !loud; r += 1)
    {
        const CheckOp op = node_machine_record_op(machine, r);
        if (op == CHECK_OP_VALUE || op == CHECK_OP_CAST)
        {
            continue;
        }
        const uint32_t operands[3] = { node_machine_record_left(machine, r), node_machine_record_right(machine, r),
                                       op == CHECK_OP_CONDITIONAL ? node_machine_record_third(machine, r) : UINT32_MAX };
        const bool mixes = (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL) || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY ||
                           op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER || op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR ||
                           op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT || op == CHECK_OP_ADD_ASSIGN || op == CHECK_OP_SUBTRACT_ASSIGN ||
                           op == CHECK_OP_MULTIPLY_ASSIGN || op == CHECK_OP_DIVIDE_ASSIGN || op == CHECK_OP_REMAINDER_ASSIGN || op == CHECK_OP_BIT_AND_ASSIGN ||
                           op == CHECK_OP_BIT_OR_ASSIGN || op == CHECK_OP_BIT_XOR_ASSIGN || op == CHECK_OP_SHIFT_LEFT_ASSIGN || op == CHECK_OP_SHIFT_RIGHT_ASSIGN;
        if (enums && (mixes || op == CHECK_OP_CONDITIONAL))
        {
            const uint32_t left = op == CHECK_OP_CONDITIONAL ? operands[1] : operands[0];
            const uint32_t right = op == CHECK_OP_CONDITIONAL ? operands[2] : operands[1];
            const TypeId a = left != UINT32_MAX ? node_machine_record_enumeration(machine, left, 8) : 0;
            const TypeId b = right != UINT32_MAX ? node_machine_record_enumeration(machine, right, 8) : 0;
            const bool a_floating = left != UINT32_MAX && check_action_kind(types, node_machine_record_type(machine, left)) == BUSTER_TYPE_KIND_FLOATING;
            const bool b_floating = right != UINT32_MAX && check_action_kind(types, node_machine_record_type(machine, right)) == BUSTER_TYPE_KIND_FLOATING;
            loud = (a != 0 && b != 0 && a != b) || a == UINT32_MAX || b == UINT32_MAX || (a != 0 && b_floating) || (b != 0 && a_floating);
        }
        // Each operand's conversion: to the type the operator converts it to; a conversion's or an initialization's own
        // type; an assignment's first operand's type for its second.
        for (uint32_t k = 0; k < 3 && !loud; k += 1)
        {
            const uint32_t o = operands[k];
            TypeId target = o == UINT32_MAX ? 0 : k < 2 ? node_machine_record_converted(machine, r, k) : node_machine_record_converted_third(machine, r);
            target = target == 0 && o != UINT32_MAX && k == 0 && (op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE) ? node_machine_record_type(machine, r) : target;
            target = target == 0 && o != UINT32_MAX && k == 1 && op == CHECK_OP_ASSIGN && operands[0] != UINT32_MAX ? node_machine_record_type(machine, operands[0]) : target;
            const TypeId plain = target != 0 ? type_unqualified(type_strip_aligned(types, target)) : 0;
            if (plain != 0 && plain != BUSTER_TYPE_VOID)
            {
                const TypeId from = enums && type_kind(types, plain) == BUSTER_TYPE_KIND_ENUM ? node_machine_record_enumeration(machine, o, 8) : 0;
                const CheckOp inner = node_machine_record_op(machine, o);
                const bool arms = (inner == CHECK_OP_CONDITIONAL || inner == CHECK_OP_COMMA) &&
                                  type_unqualified(type_strip_aligned(types, node_machine_record_type(machine, o))) != plain;
                loud = (from != 0 && from != plain) || (arms && node_machine_arm_changes(machine, o, plain, 8));
            }
        }
        for (uint32_t i = 0; op == CHECK_OP_CALL && !loud && i < node_machine_record_argument_count(machine, r); i += 1)
        {
            const uint32_t o = node_machine_record_argument(machine, r, i);
            const TypeId target = node_machine_record_argument_converted(machine, r, i);
            const TypeId plain = target != 0 ? type_unqualified(type_strip_aligned(types, target)) : 0;
            if (plain != 0 && o < count)
            {
                const TypeId from = enums && type_kind(types, plain) == BUSTER_TYPE_KIND_ENUM ? node_machine_record_enumeration(machine, o, 8) : 0;
                const CheckOp inner = node_machine_record_op(machine, o);
                const bool arms = (inner == CHECK_OP_CONDITIONAL || inner == CHECK_OP_COMMA) &&
                                  type_unqualified(type_strip_aligned(types, node_machine_record_type(machine, o))) != plain;
                loud = (from != 0 && from != plain) || (arms && node_machine_arm_changes(machine, o, plain, 8));
            }
        }
    }
    // An initialization's value's conversion to its object's type, which no record holds (T types the value without its
    // INITIALIZE record): the root's record's, asked the same.
    if ((enums || conditional) && !loud && initialized != 0 && count != 0)
    {
        const uint32_t o = count - 1;
        const TypeId plain = type_unqualified(type_strip_aligned(types, initialized));
        const TypeId from = enums && type_kind(types, plain) == BUSTER_TYPE_KIND_ENUM ? node_machine_record_enumeration(machine, o, 8) : 0;
        const CheckOp inner = node_machine_record_op(machine, o);
        const bool arms = (inner == CHECK_OP_CONDITIONAL || inner == CHECK_OP_COMMA) && type_unqualified(type_strip_aligned(types, node_machine_record_type(machine, o))) != plain;
        loud = (from != 0 && from != plain) || (arms && node_machine_arm_changes(machine, o, plain, 8));
    }
    return loud;
}

// What a splice reads of the machine's current expression beside its tape (NodeFrontTypes.side_*), kept by its root's
// node `root`: its labels' places, its calls' rows, its temporaries and its root's facts. `initialized` is the type of the
// object the root's value initializes, without its qualifiers, or 0 for a root that is no initialization's. A root C and X refused, or one
// the stores have no room for, keeps none (its labels' first is UINT32_MAX).
static void node_machine_reader_side(NodeFrontTypes* types, NodeMachine* machine, uint32_t root, bool taken, TypeId initialized)
{
    uint64_t arguments = 0;
    for (uint32_t call = 0; taken && call < machine->call_count; call += 1)
    {
        arguments += node_machine_record_argument_count(machine, machine->call_records[call]);
    }
    const bool room = taken && machine->label_count <= types->side_capacity - types->side_label_count && machine->call_count <= types->side_capacity - types->side_call_count &&
                      arguments <= types->side_capacity - types->side_argument_count;
    types->side_label_firsts[root] = room ? (uint32_t)types->side_label_count : UINT32_MAX;
    types->side_label_counts[root] = room ? machine->label_count : 0;
    types->side_call_firsts[root] = room ? (uint32_t)types->side_call_count : 0;
    types->side_call_counts[root] = room ? machine->call_count : 0;
    types->side_temporaries[root] = room ? machine->temporary_count : 0;
    types->side_facts[root] = 0;
    types->side_unkept += taken && !room;
    if (room)
    {
        // The root's record is the expression's last: a statement's condition's is its truth, a constant where its
        // operand's value is one (an lvalue's own kind is its address's, and says nothing of its value).
        const uint32_t last = machine->count - 1;
        const bool constant = node_machine_record_constant_kind(machine, last) != CHECK_CONSTANT_NONE;
        types->side_facts[root] = (uint8_t)(((node_machine_record_state(machine, last) & BUSTER_NODE_MACHINE_BRANCHED) != 0 ? BUSTER_NODE_FRONT_SIDE_BRANCHED : 0u) |
                                            (constant ? BUSTER_NODE_FRONT_SIDE_CONSTANT : 0u));
        memcpy(types->side_labels + types->side_label_count, machine->label_entries, (uint64_t)machine->label_count * sizeof(uint32_t));
        types->side_label_count += machine->label_count;
        for (uint32_t call = 0; call < machine->call_count; call += 1)
        {
            const uint32_t r = machine->call_records[call];
            const uint32_t count = node_machine_record_argument_count(machine, r);
            const uint64_t row = types->side_call_count;
            types->side_call_targets[row] = node_machine_call_target(machine, r);
            types->side_call_argument_counts[row] = count;
            types->side_call_argument_firsts[row] = (uint32_t)types->side_argument_count;
            types->side_call_variadic[row] = node_machine_call_variadic(machine, r);
            for (uint32_t i = 0; i < count; i += 1)
            {
                types->side_argument_classes[types->side_argument_count + i] = node_machine_class(machine, node_machine_record_argument_converted(machine, r, i));
            }
            types->side_argument_count += count;
            types->side_call_count += 1;
        }
        // Last, since it reads the root's value as an operand: whether it converts to its consumer's target quietly.
        types->side_facts[root] |= initialized != 0 && node_machine_root_converts(machine, initialized) ? BUSTER_NODE_FRONT_SIDE_CONVERTS : 0u;
        types->side_facts[root] |= node_machine_root_loud(machine, initialized) ? BUSTER_NODE_FRONT_SIDE_LOUD : 0u;
    }
}

// The conversion's target the lowering's task gives the machine's current expression's root where its consumer converts
// it to `to` (check_lower_expression's, which a pop logs): `to` where the root's type isn't it and the conversion writes
// entries (check_lower_conversion_empty's test, of the types' words), 0 otherwise.
static TypeId node_machine_reader_target(const NodeMachine* machine, TypeId to)
{
    const TypeId from = node_machine_record_type(machine, machine->count - 1);
    TypeId target = 0;
    if (to != 0 && from != to)
    {
        const uint32_t from_word = node_machine_word(machine, from);
        const uint32_t to_word = node_machine_word(machine, to);
        const uint8_t from_facts = (uint8_t)(from_word >> 8);
        const uint8_t to_facts = (uint8_t)(to_word >> 8);
        const uint8_t source = (from_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : (uint8_t)from_word;
        const uint8_t class = (to_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : (uint8_t)to_word;
        const bool empty = check_lower_conversion_empty_by_class(source, from_facts, class, to_facts);
        target = empty ? 0 : to;
    }
    return target;
}

// The machine at reader time for the unit `reader` reads, whose front's report is `shadow`: its state on the reader's
// scratch, its results in the output's arena (NodeFrontShadow.types); null without room. With `sweeps`, C and X run
// after T, and their results' columns and the tapes' store are made too.
static NodeMachineReader* node_machine_reader_begin(CheckInputReader* reader, NodeFrontShadow* shadow, bool sweeps)
{
    const uint64_t start = __rdtsc();
    NodeMachineReader* typer = arena_push_array(reader->scratch, 1, sizeof(NodeMachineReader), 64);
    NodeFrontTypes* types = arena_push_array(reader->arena, 1, sizeof(NodeFrontTypes), _Alignof(NodeFrontTypes));
    const uint32_t capacity = shadow->node_capacity;
    uint64_t* digests = arena_push_array(reader->arena, capacity, sizeof(uint64_t), _Alignof(uint64_t));
    uint32_t* firsts = arena_push_array(reader->arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* counts = arena_push_array(reader->arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
    uint8_t* refusals = arena_push_array(reader->arena, capacity, sizeof(uint8_t), 1);
    uint8_t* library = arena_push_array(reader->arena, capacity, sizeof(uint8_t), 1);
    const uint32_t objects = reader->output->token_count + 1;
    uint32_t* declarations = arena_push_array(reader->arena, objects, sizeof(uint32_t), _Alignof(uint32_t));
    uint8_t* facts = arena_push_array(reader->arena, objects, sizeof(uint8_t), 1);
    uint32_t* symbols = arena_push_array(reader->arena, objects, sizeof(uint32_t), _Alignof(uint32_t));
    bool room = typer != NULL && types != NULL && digests != NULL && firsts != NULL && counts != NULL && refusals != NULL && library != NULL && declarations != NULL &&
                facts != NULL && symbols != NULL;
    // C's and X's results by root node, and the tapes' store: two entries a node, which the corpora's tapes are well
    // under (1.3 an evaluated node on sqlite3.c).
    const uint64_t tape_capacity = sweeps ? 2 * (uint64_t)capacity + BUSTER_NODE_MACHINE_TAPE_MOST : 0;
    uint8_t* sweep_refusals = sweeps ? arena_push_array(reader->arena, capacity, sizeof(uint8_t), 1) : NULL;
    TypeId* targets = sweeps ? arena_push_array(reader->arena, capacity, sizeof(TypeId), _Alignof(TypeId)) : NULL;
    uint32_t* root_objects = sweeps ? arena_push_array(reader->arena, capacity, sizeof(uint32_t), _Alignof(uint32_t)) : NULL;
    uint32_t* tape_firsts = sweeps ? arena_push_array(reader->arena, capacity, sizeof(uint32_t), _Alignof(uint32_t)) : NULL;
    uint32_t* tape_counts = sweeps ? arena_push_array(reader->arena, capacity, sizeof(uint32_t), _Alignof(uint32_t)) : NULL;
    uint64_t* side_digests = sweeps ? arena_push_array(reader->arena, capacity, sizeof(uint64_t), _Alignof(uint64_t)) : NULL;
    uint8_t* tape_kinds = sweeps ? arena_push_array(reader->arena, tape_capacity, sizeof(uint8_t), 1) : NULL;
    uint8_t* tape_classes = sweeps ? arena_push_array(reader->arena, tape_capacity, sizeof(uint8_t), 1) : NULL;
    uint64_t* tape_values = sweeps ? arena_push_array(reader->arena, tape_capacity, sizeof(uint64_t), _Alignof(uint64_t)) : NULL;
    room = room && (!sweeps || (sweep_refusals != NULL && targets != NULL && root_objects != NULL && tape_firsts != NULL && tape_counts != NULL && side_digests != NULL &&
                                tape_kinds != NULL && tape_classes != NULL && tape_values != NULL));
    // The census's tests' results by root node.
    const uint16_t options = reader->pp->options.node_reader;
    const bool tested = (options & BUSTER_NODE_READER_TESTS) != 0;
    uint64_t* tests = tested ? arena_push_array(reader->arena, capacity, sizeof(uint64_t), _Alignof(uint64_t)) : NULL;
    uint8_t* kinds = tested ? arena_push_array(reader->arena, capacity, sizeof(uint8_t), 1) : NULL;
    TypeId* declared = tested ? arena_push_array(reader->arena, capacity, sizeof(TypeId), _Alignof(TypeId)) : NULL;
    uint32_t* root_declarations = tested ? arena_push_array(reader->arena, capacity, sizeof(uint32_t), _Alignof(uint32_t)) : NULL;
    room = room && (!tested || (tests != NULL && kinds != NULL && declared != NULL && root_declarations != NULL));
    // A summary's side facts by root node, and their references' store: no expression has more references than
    // leaves, and each switch's additional selector type has its own promotion node, so a word a node is room for all.
    const bool summaries = (options & BUSTER_NODE_READER_WINDOW_ROWS) != 0;
    uint32_t* summary_firsts = summaries ? arena_push_array(reader->arena, capacity, sizeof(uint32_t), _Alignof(uint32_t)) : NULL;
    uint16_t* summary_counts = summaries ? arena_push_array(reader->arena, capacity, sizeof(uint16_t), _Alignof(uint16_t)) : NULL;
    uint8_t* summary_facts = summaries ? arena_push_array(reader->arena, capacity, sizeof(uint8_t), 1) : NULL;
    uint32_t* summary_words = summaries ? arena_push_array(reader->arena, (uint64_t)capacity + 1, sizeof(uint32_t), _Alignof(uint32_t)) : NULL;
    room = room && (!summaries || (summary_firsts != NULL && summary_counts != NULL && summary_facts != NULL && summary_words != NULL));
    if (room)
    {
        typer->graph = reader->output->graph;
        typer->graph.contexts = shadow->contexts;
        typer->types = types;
        typer->strings = 0;
        typer->tests = NULL;
        typer->facts = NULL;
        typer->alone = false;
        room = node_machine_begin_reader(&typer->machine, reader->output, reader->parser, reader->types, &typer->graph, reader->pp->table, declarations, facts,
                                         symbols, (options & BUSTER_NODE_READER_VERIFY) != 0, reader->scratch);
        typer->machine.fuses |= (reader->pp->options.target.features & BUSTER_NODE_MACHINE_FUSED) != 0;
        typer->machine.marks = (options & BUSTER_NODE_READER_WINDOW) == 0;
    }
    if (room)
    {
        memset(types, 0, sizeof(*types));
        memset(counts, 0, (uint64_t)capacity * sizeof(uint32_t));
        memset(refusals, 0, capacity);
        memset(library, 0, capacity);
        typer->machine.reader_library = library;
        types->digests = digests;
        types->firsts = firsts;
        types->counts = counts;
        types->refusals = refusals;
        types->library = library;
        types->object_declarations = declarations;
        types->object_facts = facts;
        types->object_count = objects;
        types->object_symbols = symbols;
        types->sweep_refusals = sweep_refusals;
        types->targets = targets;
        types->objects = root_objects;
        types->tape_firsts = tape_firsts;
        types->tape_counts = tape_counts;
        types->side_digests = side_digests;
        types->tape_kinds = tape_kinds;
        types->tape_classes = tape_classes;
        types->tape_values = tape_values;
        types->tape_capacity = tape_capacity;
        types->node_capacity = capacity;
        if (summaries)
        {
            memset(summary_firsts, 0xff, (uint64_t)capacity * sizeof(uint32_t));
            types->summary_firsts = summary_firsts;
            types->summary_counts = summary_counts;
            types->summary_facts = summary_facts;
            types->summary_words = summary_words;
            types->summary_capacity = capacity;
        }
        if (tested)
        {
            types->tests = tests;
            types->kinds = kinds;
            types->declared = declared;
            types->declarations = root_declarations;
            typer->tests = node_census_reader_begin(reader, &typer->machine, types, capacity);
            room = typer->tests != NULL;
        }
        if (room && (options & BUSTER_NODE_READER_FACTS) != 0 && (reader->pp->options.node_front & BUSTER_NODE_FRONT_LEAN) != 0)
        {
            // It reads the lean front's records. Without room for its windows it doesn't run, and the shadow reports that.
            typer->facts = node_facts_reader_begin(reader, &typer->machine, types);
            if (typer->facts != NULL && typer->tests != NULL && (options & BUSTER_NODE_READER_WINDOW_TESTS) != 0)
            {
                node_facts_reader_tests(typer->facts, typer->tests);
            }
        }
        types->setup_ticks = __rdtsc() - start;
        shadow->types = room ? types : NULL;
    }
    return room ? typer : NULL;
}

// The objects of the strings the walk has made since the last chunk (CheckInput.strings: a string literal's, a function
// name's), given what stands for their symbols: a string's object is static without a declaration, and the walk makes it
// where it first meets the string.
static void node_machine_reader_strings(NodeMachineReader* typer, const CheckInput* input)
{
    NodeFrontTypes* types = typer->types;
    for (uint32_t index = typer->strings; index < input->string_count; index += 1)
    {
        const uint32_t object = input->strings[index].object;
        if (object != 0 && object < types->object_count)
        {
            types->object_symbols[object] = object;
        }
    }
    typer->strings = input->string_count;
}

// What the lowering's task gives a root of the front's that T has just typed (record `record`, the machine's current
// expression): a conversion's target and an object for an initialization alone, into `target` and `object`: its
// INITIALIZE's type, the object's, without its qualifiers, where the value's conversion to it writes entries, and the
// object its declaration names. Its context, which it returns, is none but a do statement's condition's sense
// (BUSTER_NODE_MACHINE_ROOT_BRANCH_TRUE): its control branches back where the condition holds, where an if's, a while's
// and a for's branch away where it doesn't. By the parser's tree, as the program's controls find it
// (check_control_statement): a do statement's condition is a child of the statement's terminator, and any other
// condition is its statement's own token. A root that is no condition takes no sense, whatever its token's parent
// (node_machine_root reads it of a CONDITION alone).
static inline uint8_t node_machine_reader_root(const NodeMachineReader* typer, const NodeFront* front, const CheckInputReader* reader, uint32_t record, TypeId* target,
                                               uint32_t* object)
{
    const bool initialization = front->ops[record] == CHECK_OP_INITIALIZE;
    const uint32_t node = front->nodes[record];
    const uint32_t token = front->tokens[record];
    const Parser* parser = reader->parser;
    const uint32_t parent = token < parser->tokens.count ? parser->tree.parents[token] : BUSTER_PARSE_NONE;
    const bool repeats = parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_TERMINATOR;
    const uint32_t declaration = initialization && token < reader->parser->tokens.count ? reader->initializer_declarations[token] : BUSTER_CHECK_NONE;
    // The initializer's plan converts the value to the object's type without its qualifiers (check_action_convert's).
    *target = initialization && node < typer->graph.count ? node_machine_reader_target(&typer->machine, type_unqualified(typer->graph.types[node])) : 0;
    *object = declaration < reader->output->token_count ? reader->output->objects[declaration] : 0;
    return repeats ? BUSTER_NODE_MACHINE_ROOT_BRANCH_TRUE : 0;
}

// Whether the front's root `record`, whose operand's record is `operand`, is a declaration's discard: at a declaration's
// `;` the walk casts each of its initializers' values to void, an initialization's node or a braced initializer's value,
// as it does an expression statement's value. Nothing evaluates that cast (the program takes an initializer's nodes for
// data, and the initialization's value is a root of its own), so it is no root of the machine's. By the parser's tree:
// the operand's token is an initializer's `=` or below one, where an expression statement's is the terminator's child.
static inline bool node_machine_reader_discard(const CheckInputReader* reader, const NodeFront* front, uint32_t record, uint32_t operand)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t terminator = front->tokens[record];
    bool discard = false;
    uint32_t at = front->ops[record] == CHECK_OP_CAST && operand < record ? front->tokens[operand] : count;
    for (uint32_t steps = 0; !discard && at < count && at != terminator && steps < 64; steps += 1)
    {
        discard = reader->initializers[at] != 0;
        at = parser->tree.parents[at];
    }
    return discard;
}

// T on the roots the front made in the chunk it has just read and compared, each with the walk's nodes of its
// expression: the root's operand's subtree, one range of nodes ending at the operand's where the expression is whole (T
// refuses the shape otherwise), and the root. An initialization's expression is its value's alone, whose root the
// lowering pops with the object's type as its target: its INITIALIZE is no record of the machine's. Twice: a first run,
// timed, a chunk's expressions a run; and a second for the records' digests, kept by the root's node. With the sweeps
// (NodeFrontTypes.sweep_refusals), C and X follow T on each expression it takes, in both runs: the first times the three
// together, and the second keeps their refusal, the root's target and object, the tape and the side's digest. With the
// census's tests (NodeMachineReader.tests), they run between T and the other two, in both runs, and C and X run on the
// expressions none refuses: the first run times all of it, and the second keeps the tests' bits and the root's kind.
static void node_machine_reader_chunk(NodeMachineReader* typer, const NodeFront* front, const CheckInputReader* reader)
{
    NodeMachine* machine = &typer->machine;
    NodeFrontTypes* types = typer->types;
    const bool sweeps = types->sweep_refusals != NULL;
    const bool tested = typer->tests != NULL;
    const bool last = (reader->pp->options.node_reader & BUSTER_NODE_READER_LAST) != 0;
    // The graph as it stands after the chunk, and the types made since the last.
    if (!typer->alone)
    {
        typer->graph = reader->output->graph;
        typer->graph.const_values = &reader->output->const_values;
        typer->graph.contexts = front->shadow->contexts;
    }
    node_machine_settle(machine);
    node_machine_reader_strings(typer, reader->output);
    if (tested)
    {
        node_census_reader_chunk(typer->tests, reader);
    }
    const bool keeps = typer->keeps && typer->alone && types->side_label_firsts != NULL;
    for (uint32_t pass = 0; pass < (keeps ? 1u : 2u); pass += 1)
    {
        const uint64_t start = __rdtsc();
        for (uint32_t record = front->made; record < front->count; record += 1)
        {
            if (front->roots[record] != 0)
            {
                // Its operand's record: the whole front's, or the lean one's by its distance.
                const uint32_t distance = front->distances[0][record];
                const uint32_t operand = !front->lean ? front->operands[0][record] :
                                         distance != 0 && distance < BUSTER_NODE_FRONT_FAR ? node_front_record_at(front, front->ordinals[record] - distance) :
                                         BUSTER_NODE_FRONT_UNKNOWN;
                const bool initialization = front->ops[record] == CHECK_OP_INITIALIZE;
                const bool discard = node_machine_reader_discard(reader, front, record, operand);
                const uint32_t inner = operand < record ? front->nodes[operand] : BUSTER_CHECK_NONE;
                const uint32_t size = operand < record ? front->sizes[operand] : 0;
                const uint32_t root = initialization ? inner : front->nodes[record];
                const uint32_t records = initialization ? size : size + 1;
                if (discard)
                {
                    types->discards += pass != 0;
                }
                else if (root < types->node_capacity && inner <= root && size != 0 && size <= inner + 1)
                {
                    const uint32_t first = inner + 1 - size;
                    // T types an initializer's value without its INITIALIZE record. The token's mark supplies that
                    // record's VALUE context even when the outer node lies beyond the shadow's capacity.
                    const uint32_t initializer = front->tokens[record];
                    const bool required = initialization && initializer < reader->parser->tokens.count && BUSTER_CHECK_RARE(reader, static_initializers, initializer);
                    // (The first run by sweep where the unit asks: a clock's read before each and after the last.)
                    const bool split = pass == 0 && types->split;
                    const uint64_t typing = split ? __rdtsc() : 0;
                    const NodeMachineRefusal refusal = node_machine_type_in_context(machine, first, root, records, required);
                    if (split)
                    {
                        types->sweep_ticks[0] += __rdtsc() - typing;
                        types->sweep_reads += 2;
                    }
                    if (keeps)
                    {
                        types->firsts[root] = first;
                        types->counts[root] = records;
                        types->refusals[root] = (uint8_t)refusal;
                    }
                    if (pass != 0)
                    {
                        types->digests[root] = node_machine_digest(machine, refusal);
                        types->firsts[root] = first;
                        types->counts[root] = records;
                        types->refusals[root] = (uint8_t)refusal;
                        types->expressions += 1;
                        types->records += records;
                        types->typed += machine->count == 0 ? 0 : refusal == NODE_MACHINE_TAKEN ? machine->count : machine->stopped;
                        types->refused[refusal] += 1;
                    }
                    // The tests and then C and X; or (BUSTER_NODE_READER_LAST) C and X and then the tests on what they
                    // take: the two steps in the order the unit's options give.
                    uint64_t verdict = 0;
                    NodeMachineRefusal swept = NODE_MACHINE_TAKEN;
                    if (typer->facts != NULL)
                    {
                        node_facts_reader_contexts(typer->facts, BUSTER_NODE_CONTEXTS_RAN_NONE, 0, 0, false);
                    }
                    for (uint32_t step = 0; step < 2; step += 1)
                    {
                    if ((step == 0) != last && tested && refusal == NODE_MACHINE_TAKEN && (!last || swept == NODE_MACHINE_TAKEN || pass != 0))
                    {
                        // The census's tests on T's records: the machine's verdict before the pipeline.
                        const uint32_t token = front->tokens[record];
                        const uint32_t declaration = initialization && token < reader->parser->tokens.count ? reader->initializer_declarations[token] : BUSTER_CHECK_NONE;
                        const TypeId declared = declaration < reader->output->token_count ? reader->declared_types[declaration] : 0;
                        uint8_t kind = 0;
                        const uint64_t testing = split ? __rdtsc() : 0;
                        verdict = node_census_reader_expression(typer->tests, root, (CheckOp)front->ops[record], token, declaration, declared, &kind, NULL);
                        if (split)
                        {
                            types->sweep_ticks[1] += __rdtsc() - testing;
                            types->sweep_reads += 2;
                        }
                        if (keeps)
                        {
                            types->refusals[root] |= BUSTER_NODE_FRONT_TYPED_TESTED;
                            types->tests[root] = verdict;
                            types->kinds[root] = kind;
                            types->declared[root] = declared;
                            types->declarations[root] = declaration;
                        }
                        if (pass != 0)
                        {
                            types->refusals[root] |= BUSTER_NODE_FRONT_TYPED_TESTED;
                            types->tests[root] = verdict;
                            types->kinds[root] = kind;
                            types->declared[root] = declared;
                            types->declarations[root] = declaration;
                            types->tested += 1;
                            types->tested_refused += verdict != 0;
                            types->tested_first[verdict != 0 ? (uint32_t)__builtin_ctzll(verdict) : 63] += verdict != 0;
                            for (uint64_t bits = verdict; bits != 0; bits &= bits - 1)
                            {
                                types->tested_any[__builtin_ctzll(bits)] += 1;
                            }
                        }
                    }
                    // C and X on the expressions no test refuses (on every one T takes where the tests come last). The
                    // second run sweeps the others too: the shadow counts what the tests add to C's and X's own refusals.
                    if ((step == 0) == last && sweeps && refusal == NODE_MACHINE_TAKEN && (last || verdict == 0 || pass != 0))
                    {
                        TypeId target = 0;
                        uint32_t object = 0;
                        const uint8_t context = node_machine_reader_root(typer, front, reader, record, &target, &object);
                        const uint64_t contexts_start = __rdtsc();
                        swept = node_machine_contexts(machine, context, target, object);
                        if (pass != 0 && typer->facts != NULL)
                        {
                            // The reference of C on the window: this run's answers, which the kernel's hook below keeps.
                            const uint8_t ran = swept == NODE_MACHINE_TAKEN ? BUSTER_NODE_CONTEXTS_RAN_TAKEN : BUSTER_NODE_CONTEXTS_RAN_REFUSED;
                            node_facts_reader_contexts(typer->facts, ran, __rdtsc() - contexts_start, target, (context & BUSTER_NODE_MACHINE_ROOT_BRANCH_TRUE) != 0);
                        }
                        const uint64_t expand_start = __rdtsc();
                        swept = swept == NODE_MACHINE_TAKEN ? node_machine_expand(machine, object) : swept;
                        if (split)
                        {
                            types->sweep_ticks[2] += expand_start - contexts_start;
                            types->sweep_ticks[3] += __rdtsc() - expand_start;
                            types->sweep_reads += 1;
                        }
                        if (pass != 0 && typer->facts != NULL)
                        {
                            // The reference of X's rows on the window: this run's ticks; its tape is kept below.
                            node_facts_reader_rows(typer->facts, __rdtsc() - expand_start);
                        }
                        if (keeps)
                        {
                            const bool taken = swept == NODE_MACHINE_TAKEN;
                            const uint32_t entries = taken ? machine->entry_count : 0;
                            const bool kept = entries <= types->tape_capacity - types->tape_count;
                            types->refusals[root] |= BUSTER_NODE_FRONT_TYPED_SWEPT;
                            types->sweep_refusals[root] = (uint8_t)swept;
                            types->targets[root] = target;
                            types->objects[root] = object;
                            types->tape_firsts[root] = (uint32_t)types->tape_count;
                            types->tape_counts[root] = kept ? entries : UINT32_MAX;
                            if (kept && entries != 0)
                            {
                                memcpy(types->tape_kinds + types->tape_count, machine->kinds, entries);
                                memcpy(types->tape_classes + types->tape_count, machine->classes, entries);
                                memcpy(types->tape_values + types->tape_count, machine->tape, (uint64_t)entries * sizeof(uint64_t));
                                types->tape_count += entries;
                            }
                            node_machine_reader_side(types, machine, root, taken,
                                                     initialization && front->nodes[record] < typer->graph.count ? type_unqualified(typer->graph.types[front->nodes[record]]) : 0);
                        }
                        if (pass != 0)
                        {
                            const bool taken = swept == NODE_MACHINE_TAKEN;
                            const uint32_t entries = taken ? machine->entry_count : 0;
                            const bool kept = entries <= types->tape_capacity - types->tape_count;
                            types->refusals[root] |= BUSTER_NODE_FRONT_TYPED_SWEPT;
                            types->sweep_refusals[root] = (uint8_t)swept;
                            types->targets[root] = target;
                            types->objects[root] = object;
                            types->tape_firsts[root] = (uint32_t)types->tape_count;
                            types->tape_counts[root] = kept ? entries : UINT32_MAX;
                            types->side_digests[root] = taken ? node_machine_side_digest(machine) : 0;
                            if (kept && entries != 0)
                            {
                                memcpy(types->tape_kinds + types->tape_count, machine->kinds, entries);
                                memcpy(types->tape_classes + types->tape_count, machine->classes, entries);
                                memcpy(types->tape_values + types->tape_count, machine->tape, (uint64_t)entries * sizeof(uint64_t));
                                types->tape_count += entries;
                            }
                            if (types->side_label_firsts != NULL)
                            {
                                node_machine_reader_side(types, machine, root, taken,
                                                     initialization && front->nodes[record] < typer->graph.count ? type_unqualified(typer->graph.types[front->nodes[record]]) : 0);
                            }
                            types->swept += 1;
                            types->swept_refused[swept] += 1;
                            types->swept_entries += entries;
                            types->swept_unkept += taken && !kept;
                        }
                    }
                    }
                    if (pass != 0 && typer->facts != NULL && refusal == NODE_MACHINE_TAKEN)
                    {
                        // The fact words' kernel's expression, after the tests and C and X, which write none of T's
                        // records: with both, whether the whole machine takes it there.
                        node_facts_reader_expression(typer->facts, front, first, root, records, record, operand,
                                                     tested && sweeps && verdict == 0 && swept == NODE_MACHINE_TAKEN);
                    }
                }
                else if (pass != 0)
                {
                    types->skipped += 1;
                }
            }
        }
        if (pass == 0)
        {
            types->ticks += __rdtsc() - start;
        }
    }
    if (typer->facts != NULL)
    {
        node_facts_reader_chunk(typer->facts, front);
    }
}

// The unit's end: the types T's derivations made, counted.
static void node_machine_reader_finish(NodeMachineReader* typer)
{
    node_machine_settle(&typer->machine);
    typer->types->late_types = typer->machine.late_types;
    typer->types->twin_differences = typer->machine.twin_differences;
    typer->types->twin_warnings = typer->machine.twin_warnings;
}

#endif
