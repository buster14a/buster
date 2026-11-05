// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU noderef's source diagnostics after checking, before liveness pruning. The ordinary graph and canonical types
// remain unchanged. Units without the spelling take no pass or allocation. An annotated unit composes immutable
// shadows in the graph's existing dependency order, then diagnoses only its marked dereferences. Address-taking
// cancels accesses along an lvalue's address computation, stopping where a pointer object must itself be read.

static bool check_noderef_graph_pointer(const TypeTable* types, TypeId type)
{
    const uint8_t kind = type_kind(types, type_strip_aligned(types, type_conversion_operand(types, type)));
    return kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind);
}

static bool check_noderef_graph_marked(CheckInputReader* reader, TypeId type, const CheckNoderefType* shadow)
{
    const CheckNoderefType* value = check_input_noderef_value(reader, type, shadow);
    const CheckNoderefType* pointed = check_input_noderef_base(value);
    return pointed != NULL && pointed->marked;
}

static void check_noderef_graph_conversion(CheckInputReader* reader, const CheckStream* stream, const CheckNoderefType* const* shadows,
                                         uint32_t source, TypeId destination, const CheckNoderefType* target, uint32_t first, uint32_t completion)
{
    const CheckGraph* graph = &reader->output->graph;
    // Clang checks the written pointer types before atomic lvalue conversion or array decay.
    if (source < graph->count && type_kind(reader->types, type_strip_aligned(reader->types, destination)) == BUSTER_TYPE_KIND_POINTER &&
        type_kind(reader->types, type_strip_aligned(reader->types, check_stream_type(stream, source))) == BUSTER_TYPE_KIND_POINTER &&
        check_noderef_graph_marked(reader, check_stream_type(stream, source), shadows[source]) &&
        !check_noderef_graph_marked(reader, destination, target))
    {
        const Parser* parser = reader->parser;
        // Parentheses do not create graph nodes. Recover the outermost enclosing group once, then find its opening
        // token in a single backwards walk, so a parenthesized initializer and the graph use the same warning key.
        uint32_t group = BUSTER_CHECK_NONE;
        for (uint32_t root = graph->tokens[source], steps = 0; root < parser->tokens.count && steps < parser->tokens.count; steps += 1)
        {
            const uint32_t parent = parser->tree.parents[root];
            if (parent >= parser->tokens.count || parent >= completion || parser->tree.roles[parent] != PARSE_ROLE_CLOSE_GROUP || parent == root)
            {
                break;
            }
            group = parent;
            root = parent;
        }
        if (group != BUSTER_CHECK_NONE)
        {
            uint32_t open = first;
            while (open != 0 && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == group))
            {
                open -= 1;
            }
            if (parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == group)
            {
                first = open;
            }
        }
        const uint32_t token = check_warning_expression_location(parser, reader->types, stream, source, first, completion);
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_NODEREF,
            .kind = CHECK_WARNING_EVENT_NODEREF_CONVERSION,
            .token = token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = completion,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_once(reader, &event);
    }
}

// Clang defers a function's dereference diagnostics until its closing brace, after its ordinary conversion warnings.
// The parser's enclosing nodes are a bounded chain; no source-depth recursion or traversal of unrelated siblings.
static uint32_t check_noderef_graph_completion(const Parser* parser, uint32_t token, uint32_t fallback)
{
    const uint32_t count = parser->tokens.count;
    uint32_t result = fallback;
    for (uint32_t steps = 0; token < count && steps < count; steps += 1)
    {
        if (parser->tree.roles[token] == PARSE_ROLE_CLOSE_BODY)
        {
            result = token + 1;
            break;
        }
        const uint32_t parent = parser->tree.parents[token];
        if (parent == token)
        {
            break;
        }
        token = parent;
    }
    return result;
}

static void check_noderef_graph_cancel(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, uint32_t node, uint8_t* accesses)
{
    while (node < graph->count)
    {
        const CheckOp op = graph->ops[node];
        accesses[node] = 0;
        uint32_t child = graph->operand0[node];
        if (op == CHECK_OP_SUBSCRIPT)
        {
            child = child < node && check_noderef_graph_pointer(types, check_stream_type(stream, child)) ? child : graph->operand1[node];
            // An array contributes its address without loading a pointer. A pointer-valued member's access is real.
            if (child >= node || !type_kind_is_array(type_kind(types, type_strip_aligned(types, check_stream_type(stream, child)))))
            {
                break;
            }
        }
        else if (op != CHECK_OP_MEMBER && op != CHECK_OP_SELECT)
        {
            break;
        }
        if (child >= node)
        {
            break;
        }
        node = child;
    }
}

// A K&R parameter's binding names its identifier-list token, while clang's note names its typed declaration. The
// pass sorts these keys once, so any number of warnings uses bounded binary lookups without rescanning headers.
static uint32_t check_noderef_graph_declaration(const uint64_t* keys, uint32_t count, uint32_t declaration)
{
    uint32_t first = 0;
    uint32_t end = count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        if ((keys[middle] >> 32) < declaration)
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return first < count && (keys[first] >> 32) == declaration ? (uint32_t)keys[first] : declaration;
}

static void check_input_noderef_graph(CheckInputReader* reader, const CheckStream* stream)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = reader->parser;
    const uint32_t count = graph->count;
    const uint32_t token_count = parser->tokens.count;
    const uint64_t mark = reader->scratch->length;
    const uint32_t declaration_count = input->noderef_declaration_count;
    uint64_t* declaration_keys = declaration_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint64_t, (uint64_t)declaration_count * 2) : NULL;
    reader->output->resource |= declaration_count != 0 && declaration_keys == NULL;
    if (declaration_keys != NULL)
    {
        uint32_t index = 0;
        for (const CheckNoderefDeclaration* record = input->noderef_declarations; record != NULL; record = record->next)
        {
            declaration_keys[index++] = (uint64_t)record->declaration << 32 | record->token;
        }
        declaration_keys = c_bindings_sort(declaration_keys, declaration_keys + declaration_count, declaration_count, !parser->options.scalar, UINT32_MAX);
    }
    const uint64_t words = ((uint64_t)count + 63) / 64;
    uint64_t* marked_values = words != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint64_t, words) : NULL;
    const CheckNoderefType** shadows = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, const CheckNoderefType*, count);
    uint8_t* accesses = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
    reader->output->resource |= count != 0 && (shadows == NULL || accesses == NULL || marked_values == NULL);
    if (marked_values != NULL)
    {
        memset(marked_values, 0, words * sizeof(uint64_t));
    }
    reader->output->noderef_values = marked_values;
    Arena* output = reader->arena;
    // These expression-only shadows live for this pass. No sparse column may point into its rolled-back scratch.
    reader->arena = reader->scratch;
    for (uint32_t node = 0; !reader->output->resource && node < count; node += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t token = graph->tokens[node];
        const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        TypeId types[3] = { 0, 0, 0 };
        const CheckNoderefType* values[3] = { NULL, NULL, NULL };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            types[i] = operands[i] < node ? check_stream_type(stream, operands[i]) : 0;
            values[i] = operands[i] < node ? shadows[operands[i]] : NULL;
        }
        const CheckNoderefType* destination = check_input_noderef_at(input, token);
        shadows[node] = NULL;
        accesses[node] = 0;
        if (stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            continue;
        }
        if (op == CHECK_OP_VALUE)
        {
            const uint32_t declaration = token < token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
            shadows[node] = declaration < token_count ? check_input_noderef_at(input, declaration) : destination;
        }
        else
        {
            shadows[node] = check_input_noderef_action(reader, op, types, values, destination, graph->members[node]);
        }
        if (check_noderef_graph_marked(reader, check_stream_type(stream, node), shadows[node]))
        {
            marked_values[node / 64] |= UINT64_C(1) << (node % 64);
        }
        if (op == CHECK_OP_DEREFERENCE || op == CHECK_OP_ARROW || op == CHECK_OP_SUBSCRIPT)
        {
            const uint32_t operand = op == CHECK_OP_SUBSCRIPT && !check_noderef_graph_pointer(reader->types, types[0]) ? 1 : 0;
            const uint8_t result = type_kind(reader->types, type_strip_aligned(reader->types, check_stream_type(stream, node)));
            // Clang suppresses unary dereference of an array result; its subscript path still diagnoses an
            // annotation on that array type (for example, int (__attribute__((noderef)) *p)[3]; p[0]).
            accesses[node] = (op != CHECK_OP_DEREFERENCE || !type_kind_is_array(result)) &&
                             check_noderef_graph_marked(reader, types[operand], values[operand]);
        }
        else if (op == CHECK_OP_ADDRESS && operands[0] < node)
        {
            check_noderef_graph_cancel(graph, stream, reader->types, operands[0], accesses);
        }
        if (op == CHECK_OP_ASSIGN)
        {
            check_noderef_graph_conversion(reader, stream, shadows, operands[1], types[0], values[0], token + 1, graph->source_positions[node]);
        }
        else if (op == CHECK_OP_INITIALIZE || (op == CHECK_OP_CONVERT && token < token_count && parser->tokens.kinds[token] == PARSE_KIND_RETURN))
        {
            check_noderef_graph_conversion(reader, stream, shadows, operands[0], graph->types[node], destination, token + 1, graph->source_positions[node]);
        }
        else if (op == CHECK_OP_CALL)
        {
            const CheckNoderefType* function = check_input_noderef_function(reader, types[0], values[0]);
            const CheckNoderefParameter* parameter = function != NULL ? function->parameters : NULL;
            TypeId callee = type_strip_aligned(reader->types, type_conversion_operand(reader->types, types[0]));
            if (type_kind(reader->types, callee) == BUSTER_TYPE_KIND_POINTER)
            {
                callee = type_strip_aligned(reader->types, type_element(reader->types, callee));
            }
            uint32_t parameter_count = 0;
            const TypeId* parameter_types = type_kind(reader->types, callee) == BUSTER_TYPE_KIND_FUNCTION &&
                                            (reader->types->types.flags[type_index(callee)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ?
                                            type_parameters(reader->types, callee, &parameter_count) : NULL;
            const uint32_t begin = graph->argument_offsets[node];
            const uint32_t arguments = graph->argument_counts[node];
            for (uint32_t i = 0; i < arguments && i < parameter_count; i += 1)
            {
                const bool annotated = parameter != NULL && parameter->index == i;
                check_noderef_graph_conversion(reader, stream, shadows, graph->argument_nodes[begin + i], parameter_types[i],
                                               annotated ? parameter->type : NULL, graph->argument_tokens[begin + i], token);
                parameter = annotated ? parameter->next : parameter;
            }
        }
    }
    for (uint32_t node = 0; !reader->output->resource && node < count; node += 1)
    {
        if (accesses[node] == 0)
        {
            continue;
        }
        const CheckOp op = graph->ops[node];
        const uint32_t a = graph->operand0[node];
        const uint32_t operand = op == CHECK_OP_SUBSCRIPT && a < node && !check_noderef_graph_pointer(reader->types, check_stream_type(stream, a)) ? graph->operand1[node] : a;
        // Clang names a declaration only when its written type is a pointer or array. An atomic operand's value
        // carries the annotation after conversion, but its declaration still has an atomic type.
        const uint8_t kind = operand < node ? type_kind(reader->types, type_strip_aligned(reader->types, check_stream_type(stream, operand))) : BUSTER_TYPE_KIND_NONE;
        const uint32_t use = (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind)) && operand < node &&
                             graph->ops[operand] == CHECK_OP_VALUE ? graph->tokens[operand] : BUSTER_CHECK_NONE;
        const uint32_t declaration = use < token_count && parser->tokens.kinds[use] == PARSE_KIND_IDENTIFIER ? input->declarations[use] : BUSTER_CHECK_NONE;
        const uint32_t token = op == CHECK_OP_ARROW ? graph->tokens[node] + 1 : op == CHECK_OP_SUBSCRIPT ?
                               check_warning_graph_first(graph, parser, node) : graph->tokens[node];
        const bool named = declaration < token_count;
        const uint32_t name = named ? parser->tokens.names[declaration] : 0;
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_NODEREF,
            .kind = CHECK_WARNING_EVENT_NODEREF_DEREFERENCE,
            .token = token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .flags = check_input_warning_unevaluated(reader, token) ? BUSTER_CHECK_WARNING_SUPPRESSED : 0,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = check_noderef_graph_completion(parser, token, graph->source_positions[node]),
            .payload = { .name = name, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        const CheckWarningNote note =
        {
            .kind = CHECK_WARNING_NOTE_NODEREF_DECLARATION,
            .token = check_noderef_graph_declaration(declaration_keys, declaration_count, declaration),
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .payload = { .name = name, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_event(reader, &event, named ? &note : NULL, named ? 1 : 0);
    }
    reader->arena = output;
    arena_pop_to(reader->scratch, mark);
}
