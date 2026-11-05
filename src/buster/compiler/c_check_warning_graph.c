// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source conversion facts after checking, before initializer/liveness pruning. Select candidate opcodes in
// sixteen-node blocks; scalar is the independent traversal twin. Only established warning facts allocate storage.
// Stage a new immutable suffix so failure cannot publish an event pointer into a rolled-back arena.

static uint32_t check_warning_graph_first(const CheckGraph* graph, const Parser* parser, uint32_t node)
{
    uint32_t first = graph->tokens[node];
    const uint32_t end = graph->source_positions[node];
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        uint32_t token = graph->tokens[node];
        const uint8_t role = parser->tree.roles[token];
        if (role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_CLOSE_KEYWORD)
        {
            uint32_t open = token;
            while (open != 0 && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == token))
            {
                open -= 1;
            }
            if (parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == token)
            {
                token = role == PARSE_ROLE_CLOSE_KEYWORD && open != 0 ? open - 1 : open;
            }
        }
        first = token < first ? token : first;
        const uint32_t child = graph->operand0[node];
        if (child >= node || graph->tokens[child] >= first)
        {
            break;
        }
        node = child;
    }
    while (first != 0 && parser->tokens.kinds[first - 1] == PARSE_KIND_L_PAREN &&
           parser->tree.parents[first - 1] >= first && parser->tree.parents[first - 1] < end)
    {
        first -= 1;
    }
    return first;
}

// `lvalue` is the token naming the conversion's destination — an INITIALIZE's `=` (whose graph type carries
// the declared type, so `declared` passes it), an ASSIGN's left operand's root, or 0 where no declaration is
// reachable (#992).
// Always inlined (#880), as profile-guided builds do; hot caller: check_input_warning_graph.
static inline __attribute__((always_inline)) void check_warning_graph_null(CheckInputReader* reader, const CheckStream* stream, uint32_t source, TypeId destination,
                                     uint32_t lvalue, TypeId declared, uint32_t completion)
{
    const CheckGraph* graph = &reader->output->graph;
    const CheckValue value = source < graph->count ? check_stream_value(stream, source) : (CheckValue){ 0 };
    if (source < graph->count && check_warning_null_integer(reader->types, destination, &value))
    {
        uint32_t first = check_warning_graph_first(graph, reader->parser, source);
        uint32_t end = graph->source_positions[source];
        uint32_t root = graph->tokens[source];
        for (uint32_t steps = 0; root < reader->parser->tokens.count && steps < reader->parser->tokens.count; steps += 1)
        {
            const uint32_t parent = reader->parser->tree.parents[root];
            if (parent >= completion || reader->parser->tree.roles[parent] != PARSE_ROLE_CLOSE_GROUP)
            {
                break;
            }
            uint32_t open = root;
            while (open != 0 && !(reader->parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && reader->parser->tree.parents[open] == parent))
            {
                open -= 1;
            }
            first = open < first ? open : first;
            end = parent + 1 > end ? parent + 1 : end;
            root = parent;
        }
        reader->warning_bound_open = BUSTER_CHECK_NONE;
        for (uint32_t vla = 0; vla < reader->output->vla_count; vla += 1)
        {
            const CheckInputVla* bound = reader->output->vlas + vla;
            if (bound->bound_begin <= first && end <= bound->bound_end &&
                (reader->warning_bound_open == BUSTER_CHECK_NONE || reader->warning_bound_open < bound->bound_begin))
            {
                reader->warning_bound_open = bound->bound_begin;
            }
        }
        // The destination operand's own spelling (#992): the lvalue's declared type, qualifiers kept, and its
        // typedef specifier's declaration where its base was a typedef name.
        uint32_t spelling = 0;
        const TypeId printed = check_input_object_spelling(reader->types, reader->output, reader->parser, lvalue, declared, destination, &spelling);
        check_input_null_spelling(reader, destination, printed, spelling, &value, first, end, completion);
    }
}

// Whether node `operand`'s value can be a pointer once converted as an operand (check_action_operand): a pointer, an array
// or a function, which decay to one, an atomic type, whose value has its base type, or a type that isn't valid. An ordered
// comparison warns only where exactly one operand is a pointer (check_warning_ordered_event), so one of two other operands
// needs no action built, which read every operand's whole value (#1050).
static bool check_warning_graph_pointer_operand(const TypeTable* types, const CheckStream* stream, uint32_t operand)
{
    const TypeId type = check_stream_type(stream, operand);
    bool result = true;
    if (type_is_valid(types, type))
    {
        const uint8_t kind = check_action_kind(types, type);
        result = kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_ATOMIC || type_kind_is_array(kind);
    }
    return result;
}

// Whether node `source`'s value can be a null pointer constant that warns (check_warning_null_integer): an integer constant
// expression of value zero without a fault. Its constant kind and flags, two of its narrow columns, say whether it is one,
// and then its payload row, which a value of columns alone hasn't (its integer is zero, its fault none), its value and
// fault, all before its conversion's record or its whole value is read. On sqlite3.c's -c those two reads were most of
// this pass, as the records are no longer in the caches when it runs (#1050).
static bool check_warning_graph_null_candidate(const CheckGraph* graph, const CheckStream* stream, uint32_t source)
{
    bool result = source < graph->count && check_stream_constant_kind(stream, source) == CHECK_CONSTANT_INTEGER && (check_stream_flags(stream, source) & BUSTER_CHECK_ICE) != 0;
    if (result && (stream->dense_values != NULL || stream->payload_rows[source] != 0))
    {
        const CheckValue value = check_stream_value(stream, source);
        result = value.integer == 0 && value.fault == CHECK_FAULT_NONE;
    }
    return result;
}

// clang's diagnostics of a use of a member that a deprecated or unavailable attribute marks (#147, DiagnoseUseOfDecl's of
// a member expression): at the name of each `.` or `->` whose member is one, "'x' is deprecated"
// (-Wdeprecated-declarations), with the attribute's message after a colon and a note at the attribute, or the error
// "'x' is unavailable" (CHECK_INPUT_UNAVAILABLE_USE), with its note at the member's declaration. The member is the one
// the access's record finds by its name, through anonymous members (type_lookup_member's), as the checker found it. A
// deprecated context drops a deprecated use's and an unavailable one either's (CheckInput.availability_contexts). It runs
// only where some member is marked, after checking: the record's type is the operand's checked one.
static void check_input_member_uses(CheckInputReader* reader, const CheckStream* stream)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    const uint32_t count = parser->tokens.count;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t operand = graph->operand0[node];
        const uint32_t name = graph->tokens[node] + 1;
        if ((op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && stream->diagnostics[node] == CHECK_DIAG_NONE && operand < graph->count && name < count &&
            parser->tree.roles[name] == PARSE_ROLE_MEMBER)
        {
            TypeId record = type_strip_aligned(types, check_stream_type(stream, operand));
            if (op == CHECK_OP_ARROW)
            {
                record = type_kind(types, record) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, record)) : 0;
            }
            const uint8_t kind = record != 0 ? type_kind(types, record) : BUSTER_TYPE_KIND_NONE;
            uint64_t offset = 0;
            uint32_t qualifiers = 0;
            const uint32_t member = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ?
                                    type_lookup_member(types, record, graph->members[node], &offset, &qualifiers) : 0;
            const CheckInputMarkedMember* marked = member != 0 && member != BUSTER_TYPE_MEMBER_UNKNOWN ?
                                                   check_marked_member(input->marked_members, input->marked_member_count, member) : NULL;
            // Once: the uses' pass gave a use the evaluator read too (a variable length array's bound) its diagnostic.
            bool reported = false;
            const uint64_t key = (uint64_t)CHECK_WARNING_EVENT_DEPRECATED_USE << 32 | ((uint64_t)name + 1);
            for (uint32_t r = 0; marked != NULL && marked->unavailable && !reported && r < reader->output->report_count; r += 1)
            {
                reported = reader->output->reports[r].token == name && reader->output->reports[r].message == CHECK_INPUT_UNAVAILABLE_USE;
            }
            reported |= marked != NULL && !marked->unavailable && check_warning_set_contains(&reader->source_warning_keys, key);
            if (marked != NULL && !reported)
            {
                check_input_marked_member_use(reader, name, marked, 0);
            }
        }
    }
}

// clang's warning at a call that resolves to an overloadable function's overload a deprecated attribute marks (#147,
// DiagnoseUseOfDecl's of the selected overload): "'f' is deprecated" (-Wdeprecated-declarations) at the callee's name,
// with the attribute's message and the note at it, where the name's context doesn't drop it
// (CheckInput.availability_contexts). The selections are the stream's (CheckStream.overloads), and an address taken
// through the overload set isn't one, as clang doesn't warn of it.
static void check_input_overload_uses(CheckInputReader* reader, const CheckStream* stream)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    for (uint32_t i = 0; i < stream->overload_count && stream->overloads != NULL; i += 1)
    {
        const uint32_t node = stream->overloads[i].node;
        const CheckOverloadResult* selected = &stream->overloads[i].selected;
        const uint32_t callee = node < graph->count && graph->ops[node] == CHECK_OP_CALL ? graph->operand0[node] : BUSTER_CHECK_NONE;
        const uint32_t name = callee < graph->count ? graph->tokens[callee] : count;
        const CheckInputMarkedMember* marked = selected->status == CHECK_OVERLOAD_OK && stream->diagnostics[node] == CHECK_DIAG_NONE && name < count &&
                                               parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER ?
                                               check_marked_member(input->marked_overloads, input->marked_overload_count, selected->declaration) : NULL;
        const uint8_t context = marked != NULL && input->availability_contexts != NULL ? input->availability_contexts[name] : 0;
        if (marked != NULL && !check_marked_dropped(marked, context))
        {
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_DEPRECATED_DECLARATIONS,
                .kind = CHECK_WARNING_EVENT_DEPRECATED_USE,
                .token = name,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = name + 1,
                .payload = { .name = parser->tokens.names[name], .token = marked->message },
            };
            const CheckWarningNote note =
            {
                .kind = CHECK_WARNING_NOTE_DEPRECATED_HERE,
                .token = marked->attribute,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .name = parser->tokens.names[marked->name], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            check_input_warning_event(reader, &event, &note, 1);
        }
    }
}

#include "c_check_noderef_graph.c"

static bool check_input_warning_graph(CheckInput* input, const Parser* parser, TypeTable* types, const CheckStream* stream, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    CheckInput staged = *input;
    const CheckWarningEvents prefix = input->warning_events;
    CheckInputReader reader = {
        .output = &staged,
        .parser = parser,
        .types = types,
        .arena = output,
        .scratch = scratch,
        .ends = parser->ends,
        .warning_query_arena = output,
        .warning_bound_open = BUSTER_CHECK_NONE,
        .source_warning_keys = { .arena = output },
        .warning_events = {
            .arena = output,
            .events = (CheckWarningEvent*)prefix.events,
            .count = prefix.count,
            .capacity = prefix.count,
            .notes = (CheckWarningNote*)prefix.notes,
            .note_count = prefix.note_count,
            .note_capacity = prefix.note_count,
        },
    };
    bool valid = !input->resource && output != scratch && stream->count == input->graph.count && stream->status == CHECK_STATUS_OK;
    for (uint32_t i = 0; valid && i < prefix.count; i += 1)
    {
        const CheckWarningEvent* event = prefix.events + i;
        const uint64_t key = (uint64_t)event->kind << 32 | ((uint64_t)event->token + 1);
        valid = check_warning_set_add(&reader.source_warning_keys, key);
    }
    const CheckGraph* graph = &input->graph;
    for (uint32_t first = 0; valid && first < graph->count; first += 16)
    {
        const uint32_t live = graph->count - first < 16 ? graph->count - first : 16;
        uint32_t selected = (1u << live) - 1;
        if (!BUSTER_SCALAR_PATH(parser->options.scalar))
        {
            const __mmask16 lanes = (__mmask16)selected;
            const __m512i ops = _mm512_maskz_loadu_epi32(lanes, graph->ops + first);
            selected = _mm512_mask_cmpge_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_LESS)) &
                       _mm512_mask_cmple_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_GREATER_EQUAL));
            selected |= _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_ASSIGN)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_INITIALIZE)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_CONVERT)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_CALL));
        }
        for (; valid && selected != 0; selected &= selected - 1)
        {
            const uint32_t node = first + (uint32_t)__builtin_ctz(selected);
            const CheckOp op = graph->ops[node];
            if (stream->diagnostics[node] == CHECK_DIAG_NONE)
            {
                const uint32_t token = graph->tokens[node];
                const uint32_t completion = graph->source_positions[node];
                if (op >= CHECK_OP_LESS && op <= CHECK_OP_GREATER_EQUAL &&
                    (check_warning_graph_pointer_operand(types, stream, graph->operand0[node]) || check_warning_graph_pointer_operand(types, stream, graph->operand1[node])))
                {
                    CheckAction action;
                    check_graph_action(graph, stream, node, NULL, &action);
                    const CheckActionResult checked = { 0 };
                    CheckWarningEvent event;
                    if (check_warning_ordered_event(types, &action, &checked, token, completion, &event))
                    {
                        // Each operand's own spelling (#992): `__size_t` for a sizeof/alignof operand's result,
                        // a typedef specifier's declaration where a named object's base was a typedef name.
                        event.payload.name = check_input_operand_spelling(&reader, graph->operand0[node]);
                        event.payload.data = check_input_operand_spelling(&reader, graph->operand1[node]);
                        check_input_warning_once(&reader, &event);
                    }
                }
                else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT)
                {
                    const uint32_t slot = op == CHECK_OP_ASSIGN ? 1 : 0;
                    const uint32_t source = slot == 0 ? graph->operand0[node] : graph->operand1[node];
                    // The destination operand's naming token (#992): an INITIALIZE's `=` scans back to its
                    // specifiers and its node's type is the declared type; an ASSIGN's is its left operand's
                    // root; a CONVERT's is none (a `return` or cast).
                    const uint32_t lvalue_node = slot == 0 ? graph->operand1[node] : graph->operand0[node];
                    const uint32_t lvalue = op == CHECK_OP_INITIALIZE ? graph->tokens[node] :
                        op == CHECK_OP_ASSIGN && lvalue_node < graph->count ? graph->tokens[lvalue_node] : 0;
                    const TypeId declared = op == CHECK_OP_INITIALIZE ? graph->types[node] : 0;
                    if (check_warning_graph_null_candidate(graph, stream, source))
                    {
                        check_warning_graph_null(&reader, stream, source, stream->conversions[node].operands[slot].type, lvalue, declared, completion);
                    }
                }
                else if (op == CHECK_OP_CALL)
                {
                    const uint32_t begin = graph->argument_offsets[node];
                    for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                    {
                        const uint32_t argument = graph->argument_nodes[begin + i];
                        if (check_warning_graph_null_candidate(graph, stream, argument))
                        {
                            check_warning_graph_null(&reader, stream, argument, stream->argument_conversions[begin + i].type, 0, 0, token);
                        }
                    }
                }
            }
            valid = !staged.resource;
        }
    }
    if (valid && check_sparse_wide_present(input->noderef_types))
    {
        check_input_noderef_graph(&reader, stream);
        valid = !staged.resource;
    }
    if (valid && input->marked_member_count != 0)
    {
        check_input_member_uses(&reader, stream);
        valid = !staged.resource;
    }
    if (valid && input->marked_overload_count != 0)
    {
        check_input_overload_uses(&reader, stream);
        valid = !staged.resource;
    }
    if (valid)
    {
        input->warning_events = staged.warning_events;
        // The unavailable members' errors (check_input_member_uses), in the reports' capacity the reader gave them.
        input->report_count = staged.report_count;
    }
    else
    {
        arena_pop_to(output, mark);
        input->resource = true;
    }
    return valid;
}
