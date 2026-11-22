// The machine with no node of the walk's (#2850, #2524; measure-node-machine --shadow --front-alone,
// BUSTER_NODE_READER_ALONE): no compile takes it.
//
// At reader time the machine's sweeps read the walk's nodes (c_check_node_machine_reader.c): T runs right after the walk
// acted at a chunk, on the graph the walk wrote. A compile that takes a function body from the machine has no such
// node, since what it saves is the walk. This file runs the same sweeps where that compile would: for each function
// body, before the walk acts at its first token, on a graph made of the front's records alone.
//
// - A body's tokens are one run of the postorder (a subtree's are), so its postorder positions are read here a chunk at
//   a time as the walk reads the unit's: the acting tokens compacted, their identifiers' and literals' entries made, and
//   a front of this file's own (the whole one, as the walk's hook runs it) making its records.
// - Each record is then written, once, to the machine's own graph (`view`): its operator, its token, its operands as that
//   graph's nodes, a leaf's value, a member, a cast's or a return's type, a call's arguments, its context. It is a
//   CheckGraph as the reader's is, made by the graph's own calls, and it holds the bodies' records one after another: a
//   condition's root is made a body after its operand (the tree puts a control's closing token after its body), and its
//   operand's records are still there. Nothing in it is the walk's: the reader's graph has no node of the body yet.
// - The reader-time machine (node_machine_reader_chunk) runs on the chunk's roots with that graph for its own and with
//   results of its own (`types`, by that graph's nodes): T, the census's tests, C and X, as it runs after the walk. The
//   tests read the reader's output through a copy whose graph is this one (`input`).
// - Each root's node is kept by its token (NodeFrontShadow.alone_roots), which is what the two graphs share. After the
//   pipeline the shadow compares each root's results with the ones the machine made of the walk's nodes
//   (c_check_node_census.c's node_shadow_alone): the same records, refusal, tests, tape and side.
//
// What it does not do yet: the reader's warning events the tests read are those raised so far, and the walk's own, which
// it raises as it acts at the body, are not among them (#2850's gate V2 lists them as refusals; the shadow counts the
// roots taken here at whose tokens the walk then raised one).
#ifndef BUSTER_C_CHECK_NODE_MACHINE_ALONE_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_ALONE_INCLUDED

struct NodeAlone
{
    // The front of its own, with its report (whose contexts' column is the graph's); and the walk's front's report, which
    // the shadow reads after the pipeline: this file's counts and its roots by token are there.
    NodeFront* front;
    NodeFrontShadow* report;
    NodeFrontShadow* shadow;
    // The graph of the front's records alone, and the reader's output with it for its graph.
    CheckGraph view;
    CheckInput input;
    // Its results, by the graph's nodes, and the tests' state that reads `input`.
    NodeFrontTypes* types;
    struct NodeCensusReader* tests;
    // The unit's function bodies in the postorder's order: each one's first position, the one past its last, and its
    // opening brace.
    uint32_t* body_firsts;
    uint32_t* body_ends;
    uint32_t* body_openings;
    uint32_t body_count;
    uint32_t next_body;
};

// The census's tests' state reading `input` for the reader's output (c_check_node_census.c).
static void node_census_reader_input(struct NodeCensusReader* tests, const CheckInput* input);

// A record's context as the walk's flush gives its node one (node_front_compare's): an initializer's or an operator's
// under a static initializer's token, and what the lists gave it; unknown where a left token took its value.
static inline uint8_t node_alone_context(const NodeFront* front, const CheckInputReader* reader, uint32_t record)
{
    const uint8_t form = front->forms[record];
    const uint32_t token = front->tokens[record];
    const bool statics = (form == CHECK_INPUT_FORM_OPERATOR || form == CHECK_INPUT_FORM_INITIALIZER) && reader->static_initializers[token];
    const uint8_t context = (uint8_t)((statics && form == CHECK_INPUT_FORM_OPERATOR ? BUSTER_CHECK_CONTEXT_STATIC : 0) |
                                      (statics && form == CHECK_INPUT_FORM_INITIALIZER ? BUSTER_CHECK_CONTEXT_VALUE : 0) | front->contexts[record]);
    return front->taken_by_left[record] != 0 ? BUSTER_NODE_FRONT_CONTEXT_UNKNOWN : context;
}

// The records the front made in the chunk it has just read, written to the graph in the order it made them, each given
// its node and the context it has as it is made: whether the graph had room. A record the front keeps for a later chunk
// keeps its node and its place alone (node_front_keep), so a context is written here once; a left token that takes a
// kept record's value later marks its node's unknown in the graph's column itself (node_front_context_unknown, whose
// report's contexts are that column).
static bool node_alone_write(struct NodeAlone* alone, const CheckInputReader* reader)
{
    NodeFront* front = alone->front;
    CheckGraph* view = &alone->view;
    bool room = true;
    for (uint32_t record = front->made; room && record < front->count; record += 1)
    {
        uint32_t operands[3];
        for (uint32_t k = 0; k < 3; k += 1)
        {
            const uint32_t operand = front->operands[k][record];
            operands[k] = operand != BUSTER_NODE_FRONT_UNKNOWN && operand < record ? front->nodes[operand] : BUSTER_CHECK_NONE;
        }
        const CheckOp op = (CheckOp)front->ops[record];
        const uint32_t argument_count = front->argument_counts[record];
        room = view->argument_count + (uint64_t)argument_count <= view->argument_capacity;
        const uint32_t node = room ? check_graph_add(view, op, front->tokens[record], operands[0], operands[1], operands[2]) : BUSTER_CHECK_NONE;
        room = node != BUSTER_CHECK_NONE;
        front->nodes[record] = node;
        if (room)
        {
            if (op == CHECK_OP_VALUE)
            {
                const uint16_t wide = front->wides[record];
                const CheckValue value = wide != 0 ? front->wide_values[wide - 1] :
                                                     (CheckValue){
                                                         .type = front->leaf_types[record],
                                                         .category = (CheckCategory)front->leaf_categories[record],
                                                         .flags = front->leaf_flags[record],
                                                         .constant_kind = (CheckConstantKind)front->leaf_constants[record],
                                                         .integer = (__int128)front->leaf_integers[record],
                                                         .object = front->leaf_objects[record],
                                                         .overload_head = front->leaf_heads[record],
                                                     };
                check_graph_set_leaf(view, node, &value);
            }
            view->types[node] = front->types[record];
            view->members[node] = front->members[record];
            view->contexts[node] = node_alone_context(front, reader, record);
            // What the walk's flush gives a node of its token alone (check_input_expression_flush): its expression's end,
            // its warnings' suppression, its floating-point environment.
            const uint32_t token = front->tokens[record];
            view->source_positions[node] = check_input_expression_end(reader, token);
            if (view->warning_suppressed != NULL)
            {
                view->warning_suppressed[node] = reader->warning_suppressed != NULL ? reader->warning_suppressed[token] : 0;
            }
            if (view->fp_environments != NULL)
            {
                const uint8_t mask = BUSTER_CHECK_FP_EVAL_MASK | BUSTER_CHECK_FP_CONTRACT_MASK | (reader->constant_contexts[token] == 0 ? BUSTER_CHECK_FP_ENV_ACCESS : 0);
                view->fp_environments[node] = reader->output->fp_environments[token] & mask;
            }
            if (argument_count != 0)
            {
                const uint32_t first = front->argument_firsts[record];
                view->argument_offsets[node] = view->argument_count;
                view->argument_counts[node] = argument_count;
                for (uint32_t i = 0; i < argument_count; i += 1)
                {
                    const uint32_t argument = front->arguments[first + i];
                    view->argument_nodes[view->argument_count + i] = argument < record ? front->nodes[argument] : BUSTER_CHECK_NONE;
                }
                view->argument_count += argument_count;
            }
            alone->report->nodes[node >> 6] |= UINT64_C(1) << (node & 63);
            alone->shadow->alone_records += 1;
        }
    }
    return room;
}

// The roots the front has lost so far, by its report (node_alone_body).
static inline uint64_t node_alone_lost(const NodeFrontShadow* report)
{
    return report->resets + report->left[CHECK_INPUT_CODE_TERMINATOR] + report->left[CHECK_INPUT_CODE_CONDITION] + report->left[CHECK_INPUT_CODE_RETURN] +
           report->left[CHECK_INPUT_CODE_INITIALIZE];
}

// One body, postorder positions `first` to `end` - 1, read whole before the walk acts at it. `typer` is the unit's
// machine at reader time, which runs here with the graph and the results of this file for its own.
static void node_alone_body(struct NodeAlone* alone, NodeMachineReader* typer, CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t opening)
{
    NodeFront* front = alone->front;
    // The roots the front loses of the body, counted from its report before and after: a root's token left (a statement's
    // terminator, a condition, a return, an initialization whose value it doesn't know: a token it left is below), and
    // the times it lost its waiting values. A token left below a declarator or a label reaches no root, and loses none.
    const uint64_t lost = node_alone_lost(alone->report);
    uint8_t verdict = BUSTER_NODE_ALONE_BODY_WHOLE;
    // No value waits at a body's opening brace: the front begins it with nothing held, its ordinals going on.
    front->count = 0;
    front->pending = 0;
    front->made = 0;
    front->stack_count = 0;
    front->group_count = 0;
    front->full = false;
    front->argument_count = 0;
    front->wide_count = 0;
    // What the reader's graph has of the unit and no node's: its tables and its defaults, as they stand.
    const CheckGraph* unit = &reader->output->graph;
    alone->view.marked_members = unit->marked_members;
    alone->view.marked_member_count = unit->marked_member_count;
    alone->view.availability_contexts = unit->availability_contexts;
    alone->view.ns_constant_string = unit->ns_constant_string;
    alone->view.cfstring_class = unit->cfstring_class;
    alone->view.const_values = unit->const_values;
    alone->view.const_warnings = unit->const_warnings;
    alone->view.const_warning_count = unit->const_warning_count;
    alone->view.fp_contract_default = unit->fp_contract_default;
    alone->view.system_tokens = unit->system_tokens;
    alone->view.system_token_count = unit->system_token_count;
    alone->view.tag_completions = unit->tag_completions;
    alone->view.transparent_unions = unit->transparent_unions;
    alone->view.tag_completion_count = unit->tag_completion_count;
    alone->view.extensions = unit->extensions;
    const CheckGraph graph = typer->graph;
    NodeFrontTypes* const types = typer->types;
    struct NodeCensusReader* const tests = typer->tests;
    struct NodeFactsReader* const facts = typer->facts;
    uint8_t* const library = typer->machine.reader_library;
    typer->types = alone->types;
    typer->tests = alone->tests;
    typer->facts = NULL;
    typer->machine.reader_library = alone->types->library;
    typer->alone = true;
    const uint32_t dropped = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_DROP << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    uint32_t tokens[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t classes[BUSTER_CHECK_EXPRESSION_CHUNK];
    CheckInputIdentifiers identifiers;
    CheckInputLiterals literals;
    bool room = true;
    for (uint32_t position = first; room && position < end; position += BUSTER_CHECK_EXPRESSION_CHUNK)
    {
        const uint32_t stop = end - position < BUSTER_CHECK_EXPRESSION_CHUNK ? end : position + BUSTER_CHECK_EXPRESSION_CHUNK;
        const uint64_t start = __rdtsc();
        const uint32_t acting = check_input_expression_compact(reader, position, stop, tokens, classes, dropped);
        check_input_expression_identifiers(reader, tokens, classes, acting, &identifiers);
        check_input_expression_literals(reader, tokens, classes, acting, &literals);
        const uint64_t prepared = __rdtsc();
        node_front_chunk(front, reader, tokens, classes, acting, &identifiers, &literals);
        const uint64_t fronted = __rdtsc();
        room = node_alone_write(alone, reader);
        const uint64_t written = __rdtsc();
        alone->shadow->alone_prepare_ticks += prepared - start;
        alone->shadow->alone_front_ticks += fronted - prepared;
        alone->shadow->alone_write_ticks += written - fronted;
        if (room)
        {
            // The machine on the chunk's roots: its graph is the records' as they stand, and the tests read the reader's
            // output as it stands with that graph in the reader's place.
            alone->input = *reader->output;
            alone->input.graph = alone->view;
            typer->graph = alone->view;
            // While the machine runs, the reader's own graph has no column of nodes: a read of the walk's node faults
            // (the unit's tables, which are no node's, stay).
            const CheckGraph walked = reader->output->graph;
            CheckGraph* const poisoned = &reader->output->graph;
            poisoned->ops = NULL;
            poisoned->tokens = NULL;
            poisoned->operand0 = NULL;
            poisoned->operand1 = NULL;
            poisoned->operand2 = NULL;
            poisoned->leaf_records = NULL;
            poisoned->leaf_payloads = NULL;
            poisoned->dense_leaves = NULL;
            poisoned->leaf_slots = NULL;
            poisoned->types = NULL;
            poisoned->members = NULL;
            poisoned->contexts = NULL;
            poisoned->fp_environments = NULL;
            poisoned->warning_suppressed = NULL;
            poisoned->source_positions = NULL;
            poisoned->argument_offsets = NULL;
            poisoned->argument_counts = NULL;
            poisoned->argument_nodes = NULL;
            poisoned->argument_tokens = NULL;
            const uint64_t machine = __rdtsc();
            node_machine_reader_chunk(typer, front, reader);
            alone->shadow->alone_machine_ticks += __rdtsc() - machine;
            reader->output->graph = walked;
            // Each root by its token: the node its results are kept by (an initialization's is its value's). And the body's
            // verdict: the first root in its order that the machine doesn't take whole (a declaration's discard is no
            // root of the machine's).
            for (uint32_t record = front->made; record < front->count; record += 1)
            {
                if (front->roots[record] != 0)
                {
                    const uint32_t operand = front->operands[0][record];
                    const uint32_t inner = operand < record ? front->nodes[operand] : BUSTER_CHECK_NONE;
                    const uint32_t root = front->ops[record] == CHECK_OP_INITIALIZE ? inner : front->nodes[record];
                    if (verdict == BUSTER_NODE_ALONE_BODY_WHOLE && !node_machine_reader_discard(reader, front, record, operand))
                    {
                        const NodeFrontTypes* results = alone->types;
                        const bool ran = root < results->node_capacity && results->counts[root] != 0;
                        const uint8_t flags = ran ? results->refusals[root] : 0;
                        verdict = !ran ? BUSTER_NODE_ALONE_BODY_UNRUN :
                                  (flags & BUSTER_NODE_FRONT_TYPED_REFUSAL) != NODE_MACHINE_TAKEN ? BUSTER_NODE_ALONE_BODY_TYPE :
                                  (flags & BUSTER_NODE_FRONT_TYPED_TESTED) == 0 || results->tests[root] != 0 ? BUSTER_NODE_ALONE_BODY_TESTS :
                                  (flags & BUSTER_NODE_FRONT_TYPED_SWEPT) == 0 || results->sweep_refusals[root] != NODE_MACHINE_TAKEN ? BUSTER_NODE_ALONE_BODY_SWEEP :
                                                                                                                                       BUSTER_NODE_ALONE_BODY_WHOLE;
                    }
                    if (root < alone->view.count && alone->view.tokens[root] < reader->parser->tokens.count)
                    {
                        // The roots of one token (a table's values, each discarded at its declaration's `;`, that the
                        // machine does not know for a declaration's discards) are a chain in the order they were made,
                        // which is the walk's nodes' order too.
                        uint32_t* link = &alone->shadow->alone_roots[alone->view.tokens[root]];
                        while (*link != 0 && *link - 1 != root)
                        {
                            link = &alone->shadow->alone_chains[*link - 1];
                        }
                        *link = root + 1;
                    }
                }
            }
        }
    }
    alone->shadow->alone_bodies += room;
    alone->shadow->alone_bodies_left += !room;
    alone->shadow->alone_positions += end - first;
    // The verdict, as it stands before the walk acts at the body's first token: a root the front lost comes before any
    // root's refusal, since its expression has no root at all.
    verdict = !room ? BUSTER_NODE_ALONE_BODY_ROOM : node_alone_lost(alone->report) != lost ? BUSTER_NODE_ALONE_BODY_LEFT : verdict;
    alone->shadow->alone_resets = alone->report->resets;
    alone->shadow->alone_body_openings[alone->shadow->alone_body_count] = opening;
    alone->shadow->alone_body_verdicts[alone->shadow->alone_body_count] = verdict;
    alone->shadow->alone_body_count += 1;
    typer->graph = graph;
    typer->types = types;
    typer->tests = tests;
    typer->facts = facts;
    typer->machine.reader_library = library;
    typer->alone = false;
}

static __attribute__((noinline)) void node_alone_bodies(struct NodeAlone* alone, NodeFront* front, CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const uint64_t start = __rdtsc();
    while (alone->next_body < alone->body_count && alone->body_firsts[alone->next_body] < end)
    {
        const uint32_t body = alone->next_body;
        alone->next_body += 1;
        if (alone->body_firsts[body] >= first)
        {
            node_alone_body(alone, front->typer, reader, alone->body_firsts[body], alone->body_ends[body], alone->body_openings[body]);
        }
        else
        {
            // One whose first position the walk has passed: a body within another's.
            alone->shadow->alone_bodies_left += 1;
        }
    }
    alone->shadow->alone_ticks += __rdtsc() - start;
}

static void node_alone_finish(struct NodeAlone* alone, const CheckInputReader* reader)
{
    const uint8_t* declined = reader->expression_declined;
    const NodeFrontTypes* results = alone->types;
    const CheckGraph* view = &alone->view;
    const uint32_t tokens = reader->parser->tokens.count;
    for (uint32_t root = 0; declined != NULL && root < view->count && root < results->node_capacity; root += 1)
    {
        const uint8_t flags = results->counts[root] != 0 ? results->refusals[root] : BUSTER_NODE_FRONT_TYPED_REFUSAL;
        const bool taken = (flags & BUSTER_NODE_FRONT_TYPED_REFUSAL) == NODE_MACHINE_TAKEN && (flags & BUSTER_NODE_FRONT_TYPED_TESTED) != 0 && results->tests[root] == 0 &&
                           (flags & BUSTER_NODE_FRONT_TYPED_SWEPT) != 0 && results->sweep_refusals[root] == NODE_MACHINE_TAKEN;
        if (taken)
        {
            // Its expression's tokens: from the lowest to the highest of its records', which a subtree's are all between.
            const uint32_t first = results->firsts[root];
            const uint32_t count = results->counts[root];
            uint32_t low = view->tokens[root];
            uint32_t high = view->tokens[root];
            for (uint32_t node = first; node + 1 < first + count && node < view->count; node += 1)
            {
                const uint32_t token = view->tokens[node];
                low = token < low ? token : low;
                high = token > high ? token : high;
            }
            bool left = false;
            for (uint32_t token = low; token <= high && token < tokens; token += 1)
            {
                left = left || declined[token] != 0;
            }
            alone->shadow->alone_taken_checked += 1;
            alone->shadow->alone_taken_declined += left;
        }
    }
}

// The results' columns for `capacity` nodes, as node_machine_reader_begin makes the walk's nodes' (`from`), whose objects'
// columns, which are the unit's, they share.
static NodeFrontTypes* node_alone_results(CheckInputReader* reader, const NodeFrontTypes* from, uint32_t capacity)
{
    Arena* arena = reader->arena;
    NodeFrontTypes* types = arena_push_array(arena, 1, sizeof(NodeFrontTypes), _Alignof(NodeFrontTypes));
    const uint64_t tape_capacity = 2 * (uint64_t)capacity + BUSTER_NODE_MACHINE_TAPE_MOST;
    bool room = types != NULL;
    if (room)
    {
        memset(types, 0, sizeof(*types));
        types->digests = arena_push_array(arena, capacity, sizeof(uint64_t), _Alignof(uint64_t));
        types->firsts = arena_push_array(arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
        types->counts = arena_push_array(arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
        types->refusals = arena_push_array(arena, capacity, sizeof(uint8_t), 1);
        types->library = arena_push_array(arena, capacity, sizeof(uint8_t), 1);
        types->sweep_refusals = arena_push_array(arena, capacity, sizeof(uint8_t), 1);
        types->targets = arena_push_array(arena, capacity, sizeof(TypeId), _Alignof(TypeId));
        types->objects = arena_push_array(arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
        types->tape_firsts = arena_push_array(arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
        types->tape_counts = arena_push_array(arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
        types->side_digests = arena_push_array(arena, capacity, sizeof(uint64_t), _Alignof(uint64_t));
        types->tape_kinds = arena_push_array(arena, tape_capacity, sizeof(uint8_t), 1);
        types->tape_classes = arena_push_array(arena, tape_capacity, sizeof(uint8_t), 1);
        types->tape_values = arena_push_array(arena, tape_capacity, sizeof(uint64_t), _Alignof(uint64_t));
        types->tests = arena_push_array(arena, capacity, sizeof(uint64_t), _Alignof(uint64_t));
        types->kinds = arena_push_array(arena, capacity, sizeof(uint8_t), 1);
        types->declared = arena_push_array(arena, capacity, sizeof(TypeId), _Alignof(TypeId));
        types->declarations = arena_push_array(arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
        room = types->digests != NULL && types->firsts != NULL && types->counts != NULL && types->refusals != NULL && types->library != NULL &&
               types->sweep_refusals != NULL && types->targets != NULL && types->objects != NULL && types->tape_firsts != NULL && types->tape_counts != NULL &&
               types->side_digests != NULL && types->tape_kinds != NULL && types->tape_classes != NULL && types->tape_values != NULL && types->tests != NULL &&
               types->kinds != NULL && types->declared != NULL && types->declarations != NULL;
    }
    if (room)
    {
        memset(types->counts, 0, (uint64_t)capacity * sizeof(uint32_t));
        memset(types->digests, 0, (uint64_t)capacity * sizeof(uint64_t));
        memset(types->refusals, 0, capacity);
        memset(types->library, 0, capacity);
        types->tape_capacity = tape_capacity;
        types->node_capacity = capacity;
        types->object_declarations = from->object_declarations;
        types->object_facts = from->object_facts;
        types->object_count = from->object_count;
        types->object_symbols = from->object_symbols;
        types->return_unknowns = from->return_unknowns;
    }
    return room ? types : NULL;
}

static struct NodeAlone* node_alone_begin(NodeFront* front, CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    NodeMachineReader* typer = front->typer;
    NodeFrontShadow* shadow = front->shadow;
    const uint32_t capacity = reader->output->graph.capacity;
    const uint32_t words = capacity / 64 + 1;
    struct NodeAlone* alone = arena_push_array(reader->scratch, 1, sizeof(struct NodeAlone), _Alignof(struct NodeAlone));
    NodeFront* own = arena_push_array(reader->scratch, 1, sizeof(NodeFront), 64);
    NodeFrontShadow* report = arena_push_array(reader->scratch, 1, sizeof(NodeFrontShadow), _Alignof(NodeFrontShadow));
    uint64_t* bits = arena_push_array(reader->scratch, words, sizeof(uint64_t), _Alignof(uint64_t));
    uint64_t* past = arena_push_array(reader->scratch, words, sizeof(uint64_t), _Alignof(uint64_t));
    uint32_t* roots = arena_push_array(reader->arena, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* chains = arena_push_array(reader->arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
    // A body an opening brace at most: the room for every one.
    uint32_t bodies = 0;
    for (uint32_t token = 0; token < count; token += 1)
    {
        bodies += parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY;
    }
    uint32_t* firsts = arena_push_array(reader->scratch, (uint64_t)bodies + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* ends = arena_push_array(reader->scratch, (uint64_t)bodies + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* openings = arena_push_array(reader->scratch, (uint64_t)bodies + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* verdict_openings = arena_push_array(reader->arena, (uint64_t)bodies + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint8_t* verdicts = arena_push_array(reader->arena, (uint64_t)bodies + 1, sizeof(uint8_t), 1);
    bool room = alone != NULL && own != NULL && report != NULL && bits != NULL && past != NULL && roots != NULL && chains != NULL && firsts != NULL && ends != NULL && openings != NULL && verdict_openings != NULL && verdicts != NULL &&
                typer != NULL && typer->types != NULL && typer->tests != NULL && typer->types->sweep_refusals != NULL && typer->facts == NULL &&
                reader->postorder != NULL && reader->postorder_positions != NULL;
    if (room)
    {
        memset(alone, 0, sizeof(*alone));
        room = check_graph_initialize(&alone->view, reader->scratch, capacity, reader->output->graph.argument_capacity);
    }
    if (room)
    {
        // The columns the reader's graph has only in some units: a node's warnings' suppression and its floating-point
        // environment.
        alone->view.warning_suppressed = reader->output->graph.warning_suppressed != NULL ? arena_push_array(reader->scratch, capacity, sizeof(uint8_t), 1) : NULL;
        alone->view.fp_environments = reader->output->graph.fp_environments != NULL ? arena_push_array(reader->scratch, capacity, sizeof(uint8_t), 1) : NULL;
        room = (alone->view.warning_suppressed != NULL) == (reader->output->graph.warning_suppressed != NULL) &&
               (alone->view.fp_environments != NULL) == (reader->output->graph.fp_environments != NULL);
    }
    NodeFrontTypes* types = room ? node_alone_results(reader, typer->types, capacity) : NULL;
    room = room && types != NULL;
    if (room)
    {
        memset(report, 0, sizeof(*report));
        memset(bits, 0, (uint64_t)words * sizeof(uint64_t));
        memset(past, 0, (uint64_t)words * sizeof(uint64_t));
        memset(roots, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(chains, 0, (uint64_t)capacity * sizeof(uint32_t));
        report->nodes = bits;
        report->past = past;
        // The front marks a node's context unknown where a left token takes its value: in the graph's own column.
        report->contexts = alone->view.contexts;
        report->node_capacity = capacity;
        own->shadow = report;
        own->count = 0;
        own->pending = 0;
        own->made = 0;
        own->stack_count = 0;
        own->group_count = 0;
        own->full = false;
        own->lean = false;
        own->ordinal = 0;
        own->argument_count = 0;
        own->wide_count = 0;
        own->typer = NULL;
        own->alone = NULL;
        own->window = false;
        own->window_facts = NULL;
        own->window_numbers = NULL;
        own->window_argument_count = 0;
        own->plain = NULL;
        own->chunks = 0;
        own->flat = NULL;
        alone->front = own;
        alone->report = report;
        alone->shadow = shadow;
        alone->types = types;
        // The tests' state of its own: it reads the reader's output with this file's graph for the reader's.
        alone->input = *reader->output;
        alone->input.graph = alone->view;
        alone->tests = node_census_reader_begin(reader, &typer->machine, types, capacity);
        room = alone->tests != NULL;
    }
    if (room)
    {
        node_census_reader_input(alone->tests, &alone->input);
        // The bodies in the order of their tokens, which is the postorder's for subtrees that share no token: a body's
        // closing brace is its opening one's parent and its subtree's last position.
        uint32_t last = 0;
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t closing = parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY ? parser->tree.parents[token] : count;
            if (closing < count && closing > token)
            {
                const uint32_t end = reader->postorder_positions[closing] + 1;
                const uint32_t size = closing - token + 1;
                bool run = end >= size && end - size >= last;
                for (uint32_t position = end - size; run && position < end; position += 1)
                {
                    run = reader->postorder[position] >= token && reader->postorder[position] <= closing;
                }
                if (run)
                {
                    firsts[alone->body_count] = end - size;
                    ends[alone->body_count] = end;
                    openings[alone->body_count] = token;
                    alone->body_count += 1;
                    last = end;
                }
                else
                {
                    shadow->alone_bodies_left += 1;
                }
            }
        }
        alone->body_firsts = firsts;
        alone->body_ends = ends;
        alone->body_openings = openings;
        shadow->alone_body_openings = verdict_openings;
        shadow->alone_body_verdicts = verdicts;
        shadow->alone_body_count = 0;
        // What the shadow reads after the pipeline is the walk's front's report.
        shadow->alone_types = types;
        shadow->alone_roots = roots;
        shadow->alone_chains = chains;
    }
    return room ? alone : NULL;
}

#endif
