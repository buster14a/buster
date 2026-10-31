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

static void check_warning_graph_null(CheckInputReader* reader, const CheckStream* stream, uint32_t source, TypeId destination, uint32_t completion)
{
    const CheckGraph* graph = &reader->output->graph;
    if (source < graph->count && check_warning_null_integer(reader->types, destination, stream->values + source))
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
                (reader->warning_bound_open == BUSTER_CHECK_NONE || reader->warning_bound_open < bound->scope_begin))
            {
                reader->warning_bound_open = bound->scope_begin;
            }
        }
        check_input_null_warning(reader, destination, stream->values + source, first, end, completion);
    }
}

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
                if (op >= CHECK_OP_LESS && op <= CHECK_OP_GREATER_EQUAL)
                {
                    const CheckAction action = { .op = op, .operands = { stream->values[graph->operand0[node]], stream->values[graph->operand1[node]] } };
                    const CheckActionResult checked = { 0 };
                    CheckWarningEvent event;
                    if (check_warning_ordered_event(types, &action, &checked, token, completion, &event))
                    {
                        check_input_warning_once(&reader, &event);
                    }
                }
                else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT)
                {
                    const uint32_t slot = op == CHECK_OP_ASSIGN ? 1 : 0;
                    const uint32_t source = slot == 0 ? graph->operand0[node] : graph->operand1[node];
                    check_warning_graph_null(&reader, stream, source, stream->conversions[node].operands[slot].type, completion);
                }
                else if (op == CHECK_OP_CALL)
                {
                    const uint32_t begin = graph->argument_offsets[node];
                    for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                    {
                        check_warning_graph_null(&reader, stream, graph->argument_nodes[begin + i], stream->argument_conversions[begin + i].type, token);
                    }
                }
            }
            valid = !staged.resource;
        }
    }
    if (valid)
    {
        input->warning_events = staged.warning_events;
    }
    else
    {
        arena_pop_to(output, mark);
        input->resource = true;
    }
    return valid;
}
