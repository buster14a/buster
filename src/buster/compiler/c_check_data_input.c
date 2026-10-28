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
    // The static declarations (the tokens that declare an object with static storage: a function, a file-scope object,
    // a static local, an extern declaration, a compound literal at file scope), as keys of the object (a token's id,
    // check_input's `objects`) in the high half and the token in the low, in object order and each object's in token
    // order; and the static objects, in object order, with each one's last declaration: check_data_latest's.
    uint64_t* declaration_keys;
    uint32_t declaration_count;
    uint32_t* declared_objects;
    uint32_t* declared_latest;
    uint32_t declared_count;
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

// Where `object` is among prepared's declared objects (check_data_prepare's, in object order), or declared_count where it
// isn't one (a string literal's, or a non-object). The search halves its range without a branch, each step a select of
// the half: its callers ask of objects in an order the branch predictor can't learn (the selection's definitions, in
// token order).
static uint32_t check_data_declared_index(const CheckDataPrepared* prepared, uint32_t object)
{
    const uint32_t* objects = prepared->declared_objects;
    uint32_t base = 0;
    uint32_t length = prepared->declared_count;
    while (length > 1)
    {
        const uint32_t half = length / 2;
        base = objects[base + half] < object ? base + half : base;
        length -= half;
    }
    // The first declared object not below `object`.
    const uint32_t lower = length != 0 ? base + (objects[base] < object) : 0;
    const uint32_t index = lower < prepared->declared_count && objects[lower] == object ? lower : prepared->declared_count;
    return index;
}

// The last static declaration of `object`, or BUSTER_CHECK_NONE where it has none.
static uint32_t check_data_latest(const CheckDataPrepared* prepared, uint32_t object)
{
    const uint32_t index = check_data_declared_index(prepared, object);
    const uint32_t token = index < prepared->declared_count ? prepared->declared_latest[index] : BUSTER_CHECK_NONE;
    return token;
}

// The static data: each static object's last declaration, and for each one the unit defines or tentatively defines, in
// object order, its definition, type, alignment and initializer's references; then the string literals. Few of a unit's
// tokens declare a static object (#592), so the vector path finds them 64 tokens at a time (the scalar path, `scalar`,
// takes every token), as keys of their object and token, which c_bindings_sort puts in object order, each object's
// declarations in token order: no token-sized column is filled or read past them.
static bool check_data_prepare(CheckDataPrepared* result, const CheckInput* input, const CheckStream* stream,
                               const CheckInitializers* initializers, TypeTable* types, bool scalar, Arena* output, Arena* scratch)
{
    *result = (CheckDataPrepared){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = input->token_count;
    bool valid = output != scratch && count < UINT32_MAX && stream->count == input->graph.count &&
                 stream->status == CHECK_STATUS_OK && stream->report_count == 0 && input->report_count == 0 && !input->resource &&
                 initializers->status == CHECK_INIT_OK && initializers->token_count == count && initializers->declaration_plans != 0;
    result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_INVALID;
    uint64_t* keys = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : 0;
    uint64_t* sorting = keys != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : 0;
    if (valid && sorting == 0)
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    uint32_t declared = 0;
    uint32_t varying = 0;
    for (uint32_t first = 0; valid && first < count; first += 64)
    {
        const uint64_t lanes = scalar ? check_input_scan_all(first, count) : check_input_scan_nonzero(input->static_objects, first, count);
        for (uint64_t selected = lanes; valid && selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
            if (input->declarations[token] == token && input->static_objects[token])
            {
                const uint32_t object = input->objects[token];
                valid = object != 0 && object <= count;
                if (valid)
                {
                    keys[declared++] = (uint64_t)object << 32 | token;
                    varying |= object;
                }
                else
                {
                    result->status = CHECK_DATA_INPUT_INVALID;
                    result->token = token;
                }
            }
        }
    }
    const uint64_t* sorted = valid ? c_bindings_sort(keys, sorting, declared, !scalar, varying) : 0;
    result->declaration_keys = valid && declared != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, declared) : 0;
    if (result->declaration_keys != 0)
    {
        memcpy(result->declaration_keys, sorted, (uint64_t)declared * sizeof(uint64_t));
        result->declaration_count = declared;
    }
    // Each static object's last declaration (the walk's last in token order), its definition (its first declaration that
    // defines it, or else its last tentative definition) and its largest alignment, in object order.
    result->declared_objects = result->declaration_keys != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, declared) : 0;
    result->declared_latest = result->declared_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, declared) : 0;
    uint32_t* definition = result->declared_latest != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, declared) : 0;
    uint8_t* alignments = definition != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, declared) : 0;
    if (valid && declared != 0 && alignments == 0)
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    uint32_t objects = 0;
    for (uint32_t i = 0; valid && i < declared; i += 1)
    {
        const uint32_t object = (uint32_t)(sorted[i] >> 32);
        const uint32_t token = (uint32_t)sorted[i];
        if (objects == 0 || result->declared_objects[objects - 1] != object)
        {
            result->declared_objects[objects] = object;
            definition[objects] = BUSTER_CHECK_NONE;
            alignments[objects] = 0;
            objects += 1;
        }
        const uint32_t at = objects - 1;
        result->declared_latest[at] = token;
        const CheckInputAttributes* attributes = input->attributes != 0 ? input->attributes[token] : 0;
        if (attributes != 0 && attributes->alignment_log2 > alignments[at])
        {
            alignments[at] = attributes->alignment_log2;
        }
        if (input->declaration_kinds[token] == C_BINDING_OBJECT && (input->defined[token] || input->tentative[token]))
        {
            const uint32_t previous = definition[at];
            if (previous == BUSTER_CHECK_NONE || !input->defined[previous])
            {
                definition[at] = token;
            }
        }
    }
    result->declared_count = objects;
    uint64_t object_capacity = input->string_count;
    uint64_t reference_capacity = 0;
    for (uint32_t at = 0; valid && at < objects; at += 1)
    {
        const uint32_t token = definition[at];
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
    for (uint32_t at = 0; valid && at < objects; at += 1)
    {
        const uint32_t selected = definition[at];
        if (selected != BUSTER_CHECK_NONE)
        {
            const uint32_t object = result->declared_objects[at];
            const uint32_t latest = result->declared_latest[at];
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
                const uint32_t written = result->count;
                result->objects[written] = (CheckDataObject){
                    .type = type,
                    .object = object,
                    .initializer = input->initializer_nodes[selected],
                    .token = selected,
                    .initializer_plan = plan,
                    .alignment_log2 = alignments[at] != 0 ? (uint8_t)(alignments[at] - 1) : 0,
                };
                result->reference_offsets[written] = result->reference_count;
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
                result->reference_counts[written] = result->reference_count - result->reference_offsets[written];
                result->count += 1;
            }
        }
    }
    for (uint32_t i = 0; valid && i < input->string_count; i += 1)
    {
        const CheckInitString* string = input->strings + i;
        valid = string->object != 0 && string->object <= count && check_data_latest(result, string->object) == BUSTER_CHECK_NONE;
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
        valid = check_data_prepare(&prepared, input, stream, initializers, types, parser->options.scalar, output, scratch);
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
            const uint32_t token = check_data_latest(&prepared, object);
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
    const CheckGraph* graph = stream->graph;
    for (uint32_t i = 0; valid && graph != 0 && i < graph->overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = graph->overload_candidates + i;
        if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0 && candidate->object != 0 && candidate->object <= count && needed[candidate->object])
        {
            // A selected overload object still needs its ABI symbol name before standalone publication.
            valid = false;
            result->status = CHECK_DATA_INPUT_UNSUPPORTED;
            result->token = candidate->declaration;
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
            const uint32_t token = check_data_latest(&prepared, object);
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
