// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Translation-unit undefined-internal facts. Source bindings, rather than emitted references, retain discarded
// expressions, unselected associations and unused bodies. Only actual language queries suppress a reference;
// checked array-bound witnesses can reopen evaluation inside a query. Builtin runtime masks do not apply here:
// Clang retains references even in constant_p, object_size and classify_type operands.
// Overload selection completes at its consuming edge, after nested argument selections. Its semantic order and
// the original identifier location are retained separately, so the first-use note need not be lexically first.
// Include after the source reader, attributes and checked stream definitions, before any program lowering.

typedef struct CheckWarningUndefinedFunction CheckWarningUndefinedFunction;
struct CheckWarningUndefinedFunction
{
    uint32_t object;
    uint32_t use;
    uint64_t order;
    bool supplied;
};

typedef struct CheckWarningUndefinedReference CheckWarningUndefinedReference;
struct CheckWarningUndefinedReference
{
    uint32_t token;
    uint32_t object;
    uint32_t bound;
    uint64_t order;
    bool selected;
    bool suppressed;
    bool conflict;
};

static bool check_warning_undefined_multiple(const CheckInput* input, uint32_t token)
{
    const CheckGraph* graph = &input->graph;
    uint32_t head = input->overload_heads != NULL ? input->overload_heads[token] : 0;
    const uint32_t canonical = head != 0 && head - 1 < graph->overload_candidate_count ? graph->overload_candidates[head - 1].canonical : BUSTER_CHECK_NONE;
    bool multiple = false;
    for (uint32_t steps = 0; head != 0 && head - 1 < graph->overload_candidate_count && steps < graph->overload_candidate_count; steps += 1)
    {
        const CheckOverloadCandidate* candidate = graph->overload_candidates + head - 1;
        multiple |= candidate->canonical != canonical;
        head = candidate->previous != BUSTER_CHECK_NONE ? candidate->previous + 1 : 0;
    }
    return multiple;
}

// Overload context belongs to the consuming edge. SELECT and ADDRESS preserve its original designator;
// changing a shared source leaf to the latest declaration would lose the selected canonical function.
static uint32_t check_warning_undefined_designator(const CheckGraph* graph, uint32_t node)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[node];
        if (op == CHECK_OP_VALUE)
        {
            result = graph->tokens[node];
            break;
        }
        if (op != CHECK_OP_SELECT && op != CHECK_OP_ADDRESS)
        {
            break;
        }
        node = graph->operand0[node];
    }
    return result;
}

static CheckWarningUndefinedReference check_warning_undefined_reference(const CheckWarningUndefinedReference* references,
                                                                        const uint64_t* keys, uint32_t count, uint32_t token)
{
    CheckWarningUndefinedReference result = { .token = token, .bound = BUSTER_CHECK_NONE, .order = UINT64_MAX };
    uint32_t first = 0;
    uint32_t end = count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const bool before = (keys[middle] >> 32) < token;
        first = before ? middle + 1 : first;
        end = before ? end : middle;
    }
    for (uint32_t i = first; i < count && (keys[i] >> 32) == token; i += 1)
    {
        const CheckWarningUndefinedReference* reference = references + (uint32_t)keys[i];
        result.conflict |= result.selected && reference->selected && result.object != reference->object;
        result.suppressed |= reference->suppressed;
        if (reference->selected || !result.selected)
        {
            result.order = result.selected && reference->selected && result.object == reference->object && result.order < reference->order ?
                           result.order : reference->order;
            result.object = reference->object;
            result.selected = reference->selected;
        }
        if (reference->bound != BUSTER_CHECK_NONE && (result.bound == BUSTER_CHECK_NONE || reference->bound > result.bound))
        {
            result.bound = reference->bound;
        }
    }
    return result;
}

static uint32_t check_warning_undefined_find(const CheckWarningUndefinedFunction* functions, uint32_t count, uint32_t object)
{
    uint32_t first = 0;
    uint32_t end = count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const bool before = functions[middle].object < object;
        first = before ? middle + 1 : first;
        end = before ? end : middle;
    }
    const uint32_t result = first < count && functions[first].object == object ? first : BUSTER_CHECK_NONE;
    return result;
}

static bool check_warning_undefined_supplied(const CheckInput* input, uint32_t declaration)
{
    bool supplied = input->defined[declaration] != 0;
    const CheckInputAttributes* attributes = input->attributes != NULL ? input->attributes[declaration] : NULL;
    for (uint32_t i = 0; !supplied && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        supplied = !attribute->ignored && (attribute->kind == CHECK_ATTRIBUTE_ALIAS || attribute->kind == CHECK_ATTRIBUTE_IFUNC ||
                                         attribute->kind == CHECK_ATTRIBUTE_WEAKREF);
    }
    return supplied;
}

static void check_warning_undefined_use(CheckInputReader* reader, CheckWarningUndefinedFunction* function, uint32_t use, uint32_t bound, uint64_t order)
{
    if (!function->supplied && order < function->order)
    {
        reader->warning_bound_open = bound;
        if (!check_input_warning_unevaluated(reader, use))
        {
            function->use = use;
            function->order = order;
        }
    }
}

static bool check_warning_undefined_internal(CheckWarningBuffer* output, const CheckInput* input, const Parser* parser,
                                             const TypeTable* types, const CheckStream* stream, Arena* scratch)
{
    bool valid = output != NULL && output->arena != NULL && input != NULL && parser != NULL && types != NULL && stream != NULL &&
                 scratch != NULL && output->arena != scratch && input->token_count == parser->tokens.count &&
                 stream->count == input->graph.count && stream->status == CHECK_STATUS_OK &&
                 (stream->overload_count == 0 || stream->overloads != NULL) &&
                 (stream->overload_address_count == 0 || stream->overload_addresses != NULL) &&
                 ((stream->overload_count == 0 && stream->overload_address_count == 0) || input->graph.source_positions != NULL);
    if (valid)
    {
        Arena* arena = output->arena;
        const uint64_t output_mark = arena->length;
        const uint64_t scratch_mark = scratch->length;
        const uint32_t count = parser->tokens.count;
        valid = count == 0 || (input->declarations != NULL && input->declaration_kinds != NULL && input->linkage != NULL &&
                              input->objects != NULL && input->defined != NULL && parser->tokens.kinds != NULL &&
                              parser->tokens.names != NULL && parser->tree.roles != NULL && parser->tree.parents != NULL);
        valid = valid && (input->vla_count == 0 || input->vlas != NULL) &&
                (input->warning_events.count == 0 || input->warning_events.events != NULL);
        uint32_t room = 0;
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            room += input->declarations[token] == token && input->declaration_kinds[token] == C_BINDING_FUNCTION &&
                    input->linkage[token] == 1 && input->objects[token] == token + 1 && !check_warning_undefined_supplied(input, token);
        }
        CheckWarningUndefinedFunction* functions = room != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckWarningUndefinedFunction, room) : NULL;
        uint64_t* keys = room != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, room) : NULL;
        uint64_t* sorting = keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, room) : NULL;
        valid = valid && (room == 0 || (functions != NULL && sorting != NULL));
        uint32_t written = 0;
        for (uint32_t token = 0; valid && room != 0 && token < count; token += 1)
        {
            if (input->declarations[token] == token && input->declaration_kinds[token] == C_BINDING_FUNCTION && input->linkage[token] == 1)
            {
                const uint32_t object = input->objects[token];
                if (object == token + 1 && !check_warning_undefined_supplied(input, token))
                {
                    valid = written < room;
                    if (valid)
                    {
                        functions[written++] = (CheckWarningUndefinedFunction){ .object = object, .use = BUSTER_CHECK_NONE, .order = UINT64_MAX };
                    }
                }
                const uint32_t index = check_warning_undefined_find(functions, written, object);
                if (index != BUSTER_CHECK_NONE)
                {
                    functions[index].supplied |= check_warning_undefined_supplied(input, token);
                }
            }
        }
        uint64_t reference_room = (uint64_t)stream->overload_count + stream->overload_address_count;
        for (uint32_t i = 0; valid && room != 0 && i < input->warning_events.count; i += 1)
        {
            const CheckWarningEvent* event = input->warning_events.events + i;
            reference_room += event->kind == CHECK_WARNING_EVENT_MESSAGE && event->id == CHECK_WARNING_ID_UNDEFINED_INTERNAL;
        }
        reference_room = room != 0 ? reference_room : 0;
        valid = valid && reference_room <= UINT32_MAX;
        CheckWarningUndefinedReference* references = valid && reference_room != 0 ?
            BUSTER_ARENA_PUSH_ARRAY(scratch, CheckWarningUndefinedReference, reference_room) : NULL;
        uint64_t* reference_keys = references != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, reference_room) : NULL;
        uint64_t* reference_sorting = reference_keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, reference_room) : NULL;
        valid = valid && (reference_room == 0 || reference_sorting != NULL);
        uint32_t reference_count = 0;
        const CheckGraph* graph = &input->graph;
        for (uint32_t i = 0; valid && room != 0 && i < stream->overload_count + stream->overload_address_count; i += 1)
        {
            uint32_t node = BUSTER_CHECK_NONE;
            uint32_t consumer = BUSTER_CHECK_NONE;
            const CheckOverloadResult* selected = NULL;
            if (i < stream->overload_count)
            {
                const CheckOverloadEffect* effect = stream->overloads + i;
                selected = &effect->selected;
                consumer = effect->node;
                node = effect->node < graph->count ? graph->operand0[effect->node] : BUSTER_CHECK_NONE;
            }
            else
            {
                const CheckOverloadAddressEffect* effect = stream->overload_addresses + i - stream->overload_count;
                selected = &effect->selected;
                consumer = effect->node;
                if (effect->node < graph->count)
                {
                    node = effect->operand == 0 ? graph->operand0[effect->node] : effect->operand == 1 ? graph->operand1[effect->node] :
                           effect->operand == 2 ? graph->operand2[effect->node] :
                           effect->operand - 3 < graph->argument_counts[effect->node] ?
                           graph->argument_nodes[graph->argument_offsets[effect->node] + effect->operand - 3] : BUSTER_CHECK_NONE;
                }
            }
            const uint32_t token = selected->status == CHECK_OVERLOAD_OK ? check_warning_undefined_designator(graph, node) : BUSTER_CHECK_NONE;
            if (token < count && selected->object != 0)
            {
                references[reference_count] = (CheckWarningUndefinedReference){ .token = token, .object = selected->object,
                    .bound = BUSTER_CHECK_NONE, .selected = true,
                    .order = check_warning_undefined_multiple(input, token) ? (uint64_t)graph->source_positions[consumer] << 32 | i :
                             (uint64_t)token << 32 | UINT32_MAX };
                reference_keys[reference_count] = (uint64_t)token << 32 | reference_count;
                reference_count += 1;
            }
        }
        for (uint32_t i = 0; valid && room != 0 && i < input->warning_events.count; i += 1)
        {
            const CheckWarningEvent* event = input->warning_events.events + i;
            if (event->kind == CHECK_WARNING_EVENT_MESSAGE && event->id == CHECK_WARNING_ID_UNDEFINED_INTERNAL &&
                event->token < count && event->payload.data != 0)
            {
                references[reference_count] = (CheckWarningUndefinedReference){ .token = event->token, .object = event->payload.data,
                    .bound = event->payload.token, .selected = event->payload.first_type != 0,
                    .order = event->payload.first_type != 0 && check_warning_undefined_multiple(input, event->token) ?
                             (uint64_t)event->completion << 32 | reference_count :
                             (uint64_t)event->token << 32 | UINT32_MAX,
                    .suppressed = (event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) != 0 };
                reference_keys[reference_count] = (uint64_t)event->token << 32 | reference_count;
                reference_count += 1;
            }
        }
        const uint64_t* ordered_references = valid && reference_count != 0 ?
            c_bindings_sort(reference_keys, reference_sorting, reference_count, !parser->options.scalar, UINT32_MAX) : NULL;
        for (uint32_t i = 0; valid && i < reference_count; i += 1)
        {
            const uint32_t token = (uint32_t)(ordered_references[i] >> 32);
            if (i == 0 || token != (ordered_references[i - 1] >> 32))
            {
                const CheckWarningUndefinedReference reference = check_warning_undefined_reference(references, ordered_references, reference_count, token);
                valid = !reference.conflict;
            }
        }
        CheckInput staged = *input;
        CheckInputReader reader = {
            .output = &staged,
            .parser = parser,
            .types = (TypeTable*)types,
            .arena = arena,
            .scratch = scratch,
            .ends = parser->ends,
            .warning_query_arena = arena,
            .warning_bound_open = BUSTER_CHECK_NONE,
        };
        for (uint32_t token = 0; valid && room != 0 && token < count; token += 1)
        {
            const uint32_t declaration = input->declarations[token];
            if (parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && parser->tree.roles[token] == PARSE_ROLE_OPERAND &&
                declaration < count && declaration != token && input->declaration_kinds[declaration] == C_BINDING_FUNCTION)
            {
                const CheckWarningUndefinedReference reference = check_warning_undefined_reference(references, ordered_references, reference_count, token);
                const uint32_t object = reference.selected ? reference.object : input->objects[declaration];
                const uint32_t index = check_warning_undefined_find(functions, written, object);
                const uint64_t order = reference.selected ? reference.order : (uint64_t)token << 32 | UINT32_MAX;
                if (index != BUSTER_CHECK_NONE && !reference.suppressed && !functions[index].supplied && order < functions[index].order)
                {
                    uint32_t bound = reference.bound;
                    for (uint32_t vla = 0; vla < input->vla_count; vla += 1)
                    {
                        const CheckInputVla* entry = input->vlas + vla;
                        if (entry->bound_begin <= token && token < entry->bound_end &&
                            (bound == BUSTER_CHECK_NONE || entry->scope_begin > bound))
                        {
                            bound = entry->scope_begin;
                        }
                    }
                    check_warning_undefined_use(&reader, functions + index, token, bound, order);
                    valid = !staged.resource;
                }
            }
        }
        // Constant and initializer contexts can retain an exact selected identity without a runtime graph node.
        // Merge each source occurrence once; selected typed witnesses dominate earlier raw bound metadata.
        for (uint32_t i = 0; valid && i < reference_count; i += 1)
        {
            const uint32_t token = (uint32_t)(ordered_references[i] >> 32);
            if (i == 0 || token != (ordered_references[i - 1] >> 32))
            {
                const CheckWarningUndefinedReference reference = check_warning_undefined_reference(references, ordered_references, reference_count, token);
                const uint32_t index = check_warning_undefined_find(functions, written, reference.object);
                if (index != BUSTER_CHECK_NONE && !reference.suppressed)
                {
                    check_warning_undefined_use(&reader, functions + index, token, reference.bound, reference.order);
                    valid = !staged.resource;
                }
            }
        }
        uint32_t used = 0;
        for (uint32_t i = 0; valid && i < written; i += 1)
        {
            if (!functions[i].supplied && functions[i].use != BUSTER_CHECK_NONE)
            {
                keys[used++] = (uint64_t)(uint32_t)functions[i].order << 32 | i;
            }
        }
        // No query point view escapes this analysis. Free its disjoint arena before appending the actual batch.
        arena_pop_to(arena, output_mark);
        CheckWarningBuffer appended = *output;
        appended.capacity = appended.count;
        appended.note_capacity = appended.note_count;
        // Stable low-half then high-half sorts preserve the selected-reference sequence at equal completion.
        uint64_t* sequences = valid && used != 0 ? c_bindings_sort(keys, sorting, used, !parser->options.scalar, UINT32_MAX) : NULL;
        for (uint32_t i = 0; valid && i < used; i += 1)
        {
            const uint32_t index = (uint32_t)sequences[i];
            sequences[i] = (functions[index].order & UINT64_C(0xffffffff00000000)) | index;
        }
        const uint64_t* ordered = valid && used != 0 ?
            c_bindings_sort(sequences, sequences == keys ? sorting : keys, used, !parser->options.scalar, UINT32_MAX) : NULL;
        for (uint32_t i = 0; valid && i < used; i += 1)
        {
            const CheckWarningUndefinedFunction* function = functions + (uint32_t)ordered[i];
            const uint32_t declaration = function->object - 1;
            const CheckWarningEvent event = {
                .id = CHECK_WARNING_ID_UNDEFINED_INTERNAL,
                .kind = CHECK_WARNING_EVENT_UNDEFINED_INTERNAL,
                .token = declaration,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_TU_END,
                .completion = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .name = parser->tokens.names[declaration], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            const CheckWarningNote note = {
                .kind = CHECK_WARNING_NOTE_USED_HERE,
                .token = function->use,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            valid = check_warning_append(&appended, &event, &note, 1);
        }
        if (valid && used != 0)
        {
            *output = appended;
        }
        else
        {
            arena_pop_to(arena, output_mark);
        }
        arena_pop_to(scratch, scratch_mark);
    }
    return valid;
}
