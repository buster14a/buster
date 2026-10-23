// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source definitions and retained initializer plans -> one shared Link symbol space. Preparation selects final
// definitions and retains per-initializer references before Link publication. The program consumer merges those
// references with its bodies, reserves code/data together, then publishes both into that Link. The standalone
// wrapper also supports data-only inspection. String candidates are emitted only when their symbols are used.
// Input, typed stream, initializer plans and type table must belong to the same successful source snapshot.

typedef enum CheckDataInputStatus
{
    CHECK_DATA_INPUT_OK,
    CHECK_DATA_INPUT_UNSUPPORTED,
    CHECK_DATA_INPUT_RESOURCE,
    CHECK_DATA_INPUT_INVALID,
} CheckDataInputStatus;

typedef struct CheckDataPrepared CheckDataPrepared;
struct CheckDataPrepared
{
    CheckDataObject* objects;
    uint32_t count;
    uint32_t* references;
    uint32_t reference_count;
    uint32_t* reference_offsets;
    uint32_t* reference_counts;
    uint32_t* latest_declarations;
    uint32_t object_count;
    LinkCapacity capacity;
    uint32_t token;
    CheckDataInputStatus status;
};

typedef struct CheckDataInput CheckDataInput;
struct CheckDataInput
{
    Link link;
    SymbolId* object_symbols;
    uint32_t object_count;
    uint32_t token;
    CheckDataInputStatus status;
    CheckDataResult emission;
};

static CheckDataInputStatus check_data_input_status(CheckDataStatus status)
{
    const CheckDataInputStatus result = status == CHECK_DATA_OK ? CHECK_DATA_INPUT_OK :
                                       status == CHECK_DATA_RESOURCE ? CHECK_DATA_INPUT_RESOURCE :
                                       status == CHECK_DATA_INVALID || status == CHECK_DATA_SYMBOL ? CHECK_DATA_INPUT_INVALID : CHECK_DATA_INPUT_UNSUPPORTED;
    return result;
}

static bool check_data_prepare(CheckDataPrepared* result, const CheckInput* input, const CheckStream* stream,
                               const CheckInitializers* initializers, TypeTable* types, Arena* output, Arena* scratch)
{
    *result = (CheckDataPrepared){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = input->token_count;
    bool valid = output != scratch && count < UINT32_MAX && stream->count == input->graph.count &&
                 stream->status == CHECK_STATUS_OK && stream->report_count == 0 && input->report_count == 0 && !input->resource &&
                 initializers->status == CHECK_INIT_OK && initializers->token_count == count && initializers->declaration_plans != 0;
    result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_INVALID;
    result->latest_declarations = valid ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* definition = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint8_t* alignments = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    if (valid && (result->latest_declarations == 0 || definition == 0 || alignments == 0))
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    for (uint32_t object = 0; valid && object <= count; object += 1)
    {
        result->latest_declarations[object] = BUSTER_CHECK_NONE;
        definition[object] = BUSTER_CHECK_NONE;
        alignments[object] = 0;
    }
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        if (input->declarations[token] == token && input->static_objects[token])
        {
            const uint32_t object = input->objects[token];
            valid = object != 0 && object <= count;
            if (valid)
            {
                result->latest_declarations[object] = token;
                const CheckInputAttributes* attributes = input->attributes != 0 ? input->attributes[token] : 0;
                if (attributes != 0 && attributes->alignment_log2 > alignments[object])
                {
                    alignments[object] = attributes->alignment_log2;
                }
                if (input->declaration_kinds[token] == C_BINDING_OBJECT && (input->defined[token] || input->tentative[token]))
                {
                    const uint32_t previous = definition[object];
                    if (previous == BUSTER_CHECK_NONE || !input->defined[previous])
                    {
                        definition[object] = token;
                    }
                }
            }
            else
            {
                result->status = CHECK_DATA_INPUT_INVALID;
                result->token = token;
            }
        }
    }
    uint64_t object_capacity = input->string_count;
    uint64_t reference_capacity = 0;
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        const uint32_t token = definition[object];
        if (token != BUSTER_CHECK_NONE)
        {
            object_capacity += 1;
            const uint32_t plan_index = initializers->declaration_plans[token];
            valid = plan_index <= initializers->count &&
                    (plan_index == 0 || (initializers->declaration_tokens != 0 && initializers->declaration_tokens[plan_index - 1] == token));
            reference_capacity += valid && plan_index != 0 ? initializers->plans[plan_index - 1].count : 0;
            if (!valid)
            {
                result->status = CHECK_DATA_INPUT_INVALID;
                result->token = token;
            }
        }
    }
    if (valid && (object_capacity > UINT32_MAX || reference_capacity > UINT32_MAX))
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    if (valid)
    {
        result->objects = object_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckDataObject, object_capacity) : 0;
        result->reference_offsets = object_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, object_capacity) : 0;
        result->reference_counts = object_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, object_capacity) : 0;
        result->references = reference_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, reference_capacity) : 0;
        valid = (object_capacity == 0 || (result->objects != 0 && result->reference_offsets != 0 && result->reference_counts != 0)) &&
                (reference_capacity == 0 || result->references != 0);
        result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_RESOURCE;
    }
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        const uint32_t selected = definition[object];
        if (selected != BUSTER_CHECK_NONE)
        {
            const uint32_t latest = result->latest_declarations[object];
            const uint32_t plan_index = initializers->declaration_plans[selected];
            const CheckInitializer* plan = plan_index != 0 ? initializers->plans + plan_index - 1 : 0;
            TypeId type = input->declared_types[latest];
            const TypeId stripped = type_strip_aligned(types, type);
            if (plan == 0 && input->tentative[selected] && type_kind(types, stripped) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
            {
                // Tentative arrays acquire one element at translation-unit end; earlier uses retain their
                // historical incomplete TypeIds and cannot gain a retroactive sizeof result.
                type = type_array(types, types->types.base[type_index(stripped)], 1);
                if (type != 0)
                {
                    type |= type_qualifiers(stripped);
                    if (type_kind(types, input->declared_types[latest]) == BUSTER_TYPE_KIND_ALIGNED)
                    {
                        type = type_aligned(types, type, type_alignment_log2(types, input->declared_types[latest]));
                    }
                }
                valid = type != 0;
                if (!valid)
                {
                    result->status = types->last_error == TYPE_ERROR_FULL ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID;
                    result->token = selected;
                }
            }
            if (valid)
            {
                const uint32_t at = result->count;
                result->objects[at] = (CheckDataObject){
                    .type = type,
                    .object = object,
                    .initializer = input->initializer_nodes[selected],
                    .token = selected,
                    .initializer_plan = plan,
                    .alignment_log2 = alignments[object] != 0 ? (uint8_t)(alignments[object] - 1) : 0,
                };
                result->reference_offsets[at] = result->reference_count;
                const uint64_t reference_mark = scratch->length;
                uint32_t* references = 0;
                uint32_t reference_count = 0;
                if (plan != 0)
                {
                    // Use the opposite arena roles for this temporary result. The helper restores its scratch
                    // suffix in output, and we copy the surviving references before releasing its result suffix.
                    const CheckInitStatus status = check_initializer_references(types, plan, scratch, output, &references, &reference_count);
                    valid = status == CHECK_INIT_OK;
                    if (!valid)
                    {
                        result->status = status == CHECK_INIT_RESOURCE ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID;
                        result->token = selected;
                    }
                }
                valid = valid && reference_count <= reference_capacity - result->reference_count;
                for (uint32_t reference = 0; valid && reference < reference_count; reference += 1)
                {
                    valid = references[reference] != 0 && references[reference] <= count;
                    if (valid)
                    {
                        result->references[result->reference_count++] = references[reference];
                    }
                }
                if (!valid && result->status == CHECK_DATA_INPUT_OK)
                {
                    result->status = CHECK_DATA_INPUT_INVALID;
                    result->token = selected;
                }
                arena_pop_to(scratch, reference_mark);
                result->reference_counts[at] = result->reference_count - result->reference_offsets[at];
                result->count += 1;
            }
        }
    }
    for (uint32_t i = 0; valid && i < input->string_count; i += 1)
    {
        const CheckInitString* string = input->strings + i;
        valid = string->object != 0 && string->object <= count && result->latest_declarations[string->object] == BUSTER_CHECK_NONE;
        if (valid)
        {
            const uint32_t at = result->count++;
            result->objects[at] = (CheckDataObject){
                .type = string->type,
                .object = string->object,
                .initializer = BUSTER_CHECK_NONE,
                .token = string->token,
                .literal_bytes = string->bytes,
                .literal_size = string->byte_count,
                .string_pool = true,
            };
            result->reference_offsets[at] = result->reference_count;
            result->reference_counts[at] = 0;
        }
        else
        {
            result->status = CHECK_DATA_INPUT_INVALID;
            result->token = string->token;
        }
    }
    if (valid)
    {
        result->object_count = count + 1;
        result->capacity = (LinkCapacity){
            .atoms = result->count,
            .fixups = result->reference_count,
            .wide_addends = result->reference_count,
        };
    }
    else
    {
        const CheckDataInputStatus status = result->status;
        const uint32_t token = result->token;
        arena_pop_to(output, output_mark);
        *result = (CheckDataPrepared){ .status = status, .token = token };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

static bool check_data_publish(const CheckDataPrepared* prepared, const TypeTable* types, const CheckStream* stream,
                               const SymbolId* object_symbols, uint32_t object_count, Link* link, uint32_t unit,
                               Arena* bytes, Arena* scratch, CheckDataResult* result)
{
    const uint64_t mark = scratch->length;
    bool valid = prepared->status == CHECK_DATA_INPUT_OK && object_count == prepared->object_count && object_symbols != 0;
    *result = (CheckDataResult){ .status = valid ? CHECK_DATA_OK : CHECK_DATA_INVALID, .failed_token = BUSTER_CHECK_NONE };
    CheckDataObject* objects = valid && prepared->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDataObject, prepared->count) : 0;
    if (valid && prepared->count != 0 && objects == 0)
    {
        valid = false;
        result->status = CHECK_DATA_RESOURCE;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; valid && i < prepared->count; i += 1)
    {
        const CheckDataObject* object = prepared->objects + i;
        valid = object->object < object_count;
        if (!valid)
        {
            result->status = CHECK_DATA_INVALID;
            result->failed_token = object->token;
        }
        else if (!object->string_pool || object_symbols[object->object] != 0)
        {
            objects[count++] = *object;
        }
    }
    if (valid)
    {
        valid = check_data_emit(types, stream, objects, count, object_symbols, object_count, link, unit, bytes, scratch, result);
    }
    arena_pop_to(scratch, mark);
    return valid;
}

static bool check_data_input_prepared(CheckDataInput* result, const CheckInput* input, const CheckStream* stream,
                                      const CheckInitializers* initializers, const Parser* parser, const InternTable* identifiers,
                                      TypeTable* types, Arena* output, Arena* bytes, Arena* scratch)
{
    *result = (CheckDataInput){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t bytes_mark = bytes->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = input->token_count;
    bool valid = output != scratch && bytes != scratch && output != bytes && count == parser->tokens.count;
    CheckDataPrepared prepared = { .status = CHECK_DATA_INPUT_INVALID, .token = BUSTER_CHECK_NONE };
    if (valid)
    {
        valid = check_data_prepare(&prepared, input, stream, initializers, types, output, scratch);
    }
    result->status = prepared.status;
    result->token = prepared.token;
    uint8_t* needed = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    uint32_t* definitions = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    if (valid && (needed == 0 || definitions == 0))
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    if (valid)
    {
        memset(needed, 0, (uint64_t)count + 1);
        memset(definitions, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < prepared.count; i += 1)
        {
            const CheckDataObject* object = prepared.objects + i;
            definitions[object->object] = i + 1;
            needed[object->object] = !object->string_pool;
        }
        for (uint32_t i = 0; i < prepared.reference_count; i += 1)
        {
            needed[prepared.references[i]] = 1;
        }
    }
    uint32_t symbol_count = 0;
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        if (needed[object])
        {
            const uint32_t token = prepared.latest_declarations[object];
            valid = definitions[object] != 0 || (token != BUSTER_CHECK_NONE && input->linkage[token] == 2);
            if (valid)
            {
                symbol_count += 1;
            }
            else
            {
                // Internal function addresses require the program consumer's real code atom.
                result->status = CHECK_DATA_INPUT_UNSUPPORTED;
                result->token = token;
            }
        }
    }
    if (valid)
    {
        LinkCapacity capacity = prepared.capacity;
        capacity.names = symbol_count;
        capacity.symbols = symbol_count;
        result->object_symbols = BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, (uint64_t)count + 1);
        valid = result->object_symbols != 0 && link_initialize(&result->link, output, output, capacity);
        result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_RESOURCE;
        if (valid)
        {
            memset(result->object_symbols, 0, ((uint64_t)count + 1) * sizeof(SymbolId));
            result->object_count = count + 1;
        }
    }
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        if (needed[object])
        {
            const uint32_t token = prepared.latest_declarations[object];
            const bool literal = token == BUSTER_CHECK_NONE;
            const bool compound = !literal && input->compound_objects != 0 && input->compound_objects[token];
            const uint32_t name = !literal ? parser->tokens.names[token] : 0;
            uint8_t generated_name[48];
            const int generated_length = compound ? snprintf((char*)generated_name, sizeof(generated_name), ".Lcompound.%u", object) : 0;
            const NameId link_name = compound ? link_add_name(&result->link, generated_name, (uint32_t)generated_length) :
                                     !literal ? link_add_name(&result->link, intern_bytes(identifiers, name), intern_length(identifiers, name)) : 0;
            const uint8_t kind = !literal && input->declaration_kinds[token] == C_BINDING_FUNCTION ? BUSTER_LINK_SYMBOL_FUNCTION :
                                 !literal && input->thread_local[token] ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
            const uint8_t binding = !literal && input->linkage[token] == 2 ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_LOCAL;
            result->object_symbols[object] = literal || link_name != 0 ? link_add_symbol(&result->link, link_name, 0, 0, 0, kind | binding, BUSTER_LINK_SOURCE_NONE) : 0;
            valid = result->object_symbols[object] != 0;
            if (!valid)
            {
                result->status = CHECK_DATA_INPUT_RESOURCE;
                result->token = token;
            }
        }
    }
    if (valid)
    {
        valid = check_data_publish(&prepared, types, stream, result->object_symbols, count + 1, &result->link, 0, bytes, scratch, &result->emission);
        result->status = check_data_input_status(result->emission.status);
        result->token = result->emission.failed_token;
    }
    if (valid && link_check(&result->link) != LINK_ERROR_NONE)
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_INVALID;
    }
    if (!valid)
    {
        const CheckDataInputStatus status = result->status;
        const uint32_t token = result->token;
        const CheckDataResult emission = result->emission;
        arena_pop_to(output, output_mark);
        arena_pop_to(bytes, bytes_mark);
        *result = (CheckDataInput){ .status = status, .token = token, .emission = emission };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

static bool check_data_input(CheckDataInput* result, const CheckInput* input, const CheckStream* stream, const Parser* parser,
                             const InternTable* identifiers, TypeTable* types, Arena* output, Arena* bytes, Arena* scratch)
{
    const uint64_t mark = output->length;
    CheckInitializers initializers;
    bool valid = check_initializers_run(&initializers, input, stream, parser, types, output, scratch);
    if (valid)
    {
        valid = check_data_input_prepared(result, input, stream, &initializers, parser, identifiers, types, output, bytes, scratch);
    }
    else
    {
        *result = (CheckDataInput){
            .status = initializers.status == CHECK_INIT_RESOURCE ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID,
            .token = initializers.token,
        };
    }
    if (!valid)
    {
        arena_pop_to(output, mark);
    }
    return valid;
}
