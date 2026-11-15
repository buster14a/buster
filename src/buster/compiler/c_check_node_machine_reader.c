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
};
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
                     node_machine_record_integer(machine, size) <= BUSTER_CBUILTIN_LIBRARY_INLINE_BYTES;
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
// the objects with an asm label (check_program_labelled_objects' test); and the names that return twice from
// `identifiers` (check_lower_twice_names').
static bool node_machine_begin_reader(NodeMachine* machine, const CheckInput* input, const Parser* parser, TypeTable* types, const CheckGraph* graph,
                                      const InternTable* identifiers, uint32_t* declarations, uint8_t* facts, Arena* arena)
{
    *machine = (NodeMachine){ .input = input, .parser = parser };
    const uint32_t count = input->token_count;
    const uint32_t late = types->types.capacity - types->types.count < BUSTER_NODE_MACHINE_LATE_TYPES ? types->types.capacity - types->types.count : BUSTER_NODE_MACHINE_LATE_TYPES;
    const bool room = node_machine_setup(machine, types, graph, types->types.count + late, false, false, arena);
    machine->reader_declarations = declarations;
    machine->reader_facts = facts;
    if (room)
    {
        machine->reader_objects = count + 1;
        memset(machine->reader_declarations, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(machine->reader_facts, 0, (uint64_t)count + 1);
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t object = input->declarations[token] == token ? input->objects[token] : 0;
            if (object != 0 && object <= count)
            {
                const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
                if (check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC))
                {
                    machine->reader_declarations[object] = token;
                    const uint32_t bits = attributes != NULL && input->declaration_kinds[token] == C_BINDING_FUNCTION ? check_program_attribute_bits(attributes) : 0;
                    machine->reader_facts[object] |= (bits & BUSTER_CHECK_PROGRAM_ATTRIBUTE_RETURNS_TWICE) != 0 ? BUSTER_NODE_MACHINE_OBJECT_TWICE : 0;
                }
                if (input->asm_capacity != 0 && attributes != NULL && check_input_attribute_of(attributes, CHECK_ATTRIBUTE_ASM_LABEL) != NULL)
                {
                    machine->reader_facts[object] |= BUSTER_NODE_MACHINE_OBJECT_LABELLED;
                }
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
        digest = node_machine_mix(digest, (uint64_t)value.fault_node << 32 | value.object_bit_offset);
        digest = node_machine_mix(digest, (uint64_t)columns->words[r] << 32 | (uint64_t)columns->facts[r] << 8 | columns->value_classes[r]);
        digest = node_machine_mix(digest, (uint64_t)node_machine_record_converted(machine, r, 0) << 32 | node_machine_record_conversion_flags(machine, r, 0));
        digest = node_machine_mix(digest, (uint64_t)node_machine_record_converted(machine, r, 1) << 32 | node_machine_record_conversion_flags(machine, r, 1));
        digest = node_machine_mix(digest, node_machine_record_member_offset(machine, r));
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

// The machine at reader time for the unit `reader` reads, whose front's report is `shadow`: its state on the reader's
// scratch, its results in the output's arena (NodeFrontShadow.types); null without room.
static NodeMachineReader* node_machine_reader_begin(CheckInputReader* reader, NodeFrontShadow* shadow)
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
    bool room = typer != NULL && types != NULL && digests != NULL && firsts != NULL && counts != NULL && refusals != NULL && library != NULL && declarations != NULL &&
                facts != NULL;
    if (room)
    {
        typer->graph = reader->output->graph;
        typer->graph.contexts = shadow->contexts;
        typer->types = types;
        room = node_machine_begin_reader(&typer->machine, reader->output, reader->parser, reader->types, &typer->graph, reader->pp->table, declarations, facts,
                                         reader->scratch);
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
        types->node_capacity = capacity;
        types->setup_ticks = __rdtsc() - start;
        shadow->types = types;
    }
    return room ? typer : NULL;
}

// T on the roots the front made in the chunk it has just read and compared, each with the walk's nodes of its
// expression: the root's operand's subtree, one range of nodes ending at the operand's where the expression is whole (T
// refuses the shape otherwise), and the root. An initialization's expression is its value's alone, whose root the
// lowering pops with the object's type as its target: its INITIALIZE is no record of the machine's. Twice: a first run of
// T alone, timed, a chunk's expressions a run; and a second for the records' digests, kept by the root's node.
static void node_machine_reader_chunk(NodeMachineReader* typer, const NodeFront* front, const CheckInputReader* reader)
{
    NodeMachine* machine = &typer->machine;
    NodeFrontTypes* types = typer->types;
    // The graph as it stands after the chunk, and the types made since the last.
    typer->graph = reader->output->graph;
    typer->graph.contexts = front->shadow->contexts;
    node_machine_settle(machine);
    for (uint32_t pass = 0; pass < 2; pass += 1)
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
                const uint32_t inner = operand < record ? front->nodes[operand] : BUSTER_CHECK_NONE;
                const uint32_t size = operand < record ? front->sizes[operand] : 0;
                const uint32_t root = initialization ? inner : front->nodes[record];
                const uint32_t records = initialization ? size : size + 1;
                if (root < types->node_capacity && inner <= root && size != 0 && size <= inner + 1)
                {
                    const uint32_t first = inner + 1 - size;
                    const NodeMachineRefusal refusal = node_machine_type(machine, first, root, records);
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
}

// The unit's end: the types T's derivations made, counted.
static void node_machine_reader_finish(NodeMachineReader* typer)
{
    node_machine_settle(&typer->machine);
    typer->types->late_types = typer->machine.late_types;
}

#endif
