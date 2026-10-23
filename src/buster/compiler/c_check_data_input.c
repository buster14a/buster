// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source declaration columns -> checked static objects -> Link. The source reader owns scope and declaration
// semantics. This consumer selects one definition per stable object and uses the latest composite type, without
// reparsing declarators or doing name lookup. Unreferenced function bodies and automatic objects are not data.
// Include after c_check_data.c and c_check_input.c. Output owns the Link, symbols and bytes; scratch is transient.

typedef enum CheckDataInputStatus
{
    CHECK_DATA_INPUT_OK,
    CHECK_DATA_INPUT_UNSUPPORTED,
    CHECK_DATA_INPUT_RESOURCE,
    CHECK_DATA_INPUT_INVALID,
} CheckDataInputStatus;

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

static bool check_data_input(CheckDataInput* result, const CheckInput* input, const CheckStream* stream, const Parser* parser,
                             const InternTable* identifiers, const TypeTable* types, Arena* output, Arena* bytes, Arena* scratch)
{
    *result = (CheckDataInput){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t bytes_mark = bytes->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = input->token_count;
    const bool input_valid = output != scratch && bytes != scratch && output != bytes && count < UINT32_MAX && count == parser->tokens.count &&
                             stream->count == input->graph.count && stream->status == CHECK_STATUS_OK && stream->report_count == 0 &&
                             input->report_count == 0 && !input->resource;
    bool valid = input_valid;
    uint32_t* latest = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* definition = latest != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint8_t* needed = definition != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    valid = valid && needed != 0;
    result->status = valid ? CHECK_DATA_INPUT_OK : input_valid ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID;
    if (valid)
    {
        for (uint32_t object = 0; object <= count; object += 1)
        {
            latest[object] = BUSTER_CHECK_NONE;
            definition[object] = BUSTER_CHECK_NONE;
            needed[object] = 0;
        }
    }
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        if (input->declarations[token] == token && input->static_objects[token])
        {
            const uint32_t object = input->objects[token];
            valid = object != 0 && object <= count;
            if (valid)
            {
                latest[object] = token;
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
    uint32_t object_count = 0;
    uint32_t symbol_count = 0;
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        const uint32_t token = definition[object];
        if (token != BUSTER_CHECK_NONE)
        {
            needed[object] = 1;
            object_count += 1;
            const uint32_t initializer = input->initializer_nodes[token];
            if (initializer != BUSTER_CHECK_NONE)
            {
                valid = initializer < stream->count;
                if (valid && stream->values[initializer].constant_kind == CHECK_CONSTANT_ADDRESS)
                {
                    const uint32_t target = stream->values[initializer].object;
                    valid = target <= count && (target == 0 || latest[target] != BUSTER_CHECK_NONE);
                    if (valid && target != 0)
                    {
                        needed[target] = 1;
                    }
                }
                if (!valid)
                {
                    result->status = CHECK_DATA_INPUT_INVALID;
                    result->token = token;
                }
            }
        }
    }
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        if (needed[object])
        {
            const uint32_t token = latest[object];
            // Local undefined symbols cannot form a valid Link. Internal function bodies need the backend's
            // real atom before a static pointer to them can be published; never invent a body or global binding.
            valid = token != BUSTER_CHECK_NONE && (definition[object] != BUSTER_CHECK_NONE || input->linkage[token] == 2);
            symbol_count += valid;
            if (!valid)
            {
                result->status = CHECK_DATA_INPUT_UNSUPPORTED;
                result->token = token;
            }
        }
    }
    const LinkCapacity capacity = {
        .names = symbol_count,
        .atoms = object_count,
        .symbols = symbol_count,
        .fixups = object_count,
        .wide_addends = object_count,
    };
    result->object_count = count + 1;
    result->object_symbols = valid ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, (uint64_t)count + 1) : 0;
    CheckDataObject* objects = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDataObject, object_count) : 0;
    if (valid)
    {
        valid = result->object_symbols != 0 && (object_count == 0 || objects != 0) && link_initialize(&result->link, output, output, capacity);
        result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_RESOURCE;
    }
    if (valid)
    {
        memset(result->object_symbols, 0, ((uint64_t)count + 1) * sizeof(SymbolId));
    }
    uint32_t at = 0;
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        if (needed[object])
        {
            const uint32_t token = latest[object];
            const uint32_t name = parser->tokens.names[token];
            const NameId link_name = link_add_name(&result->link, intern_bytes(identifiers, name), intern_length(identifiers, name));
            const uint8_t kind = input->declaration_kinds[token] == C_BINDING_FUNCTION ? BUSTER_LINK_SYMBOL_FUNCTION : BUSTER_LINK_SYMBOL_OBJECT;
            const uint8_t binding = input->linkage[token] == 2 ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_LOCAL;
            result->object_symbols[object] = link_name != 0 ? link_add_symbol(&result->link, link_name, 0, 0, 0, kind | binding, BUSTER_LINK_SOURCE_NONE) : 0;
            valid = result->object_symbols[object] != 0;
            if (!valid)
            {
                result->status = CHECK_DATA_INPUT_RESOURCE;
                result->token = token;
            }
            if (definition[object] != BUSTER_CHECK_NONE)
            {
                const uint32_t selected = definition[object];
                objects[at++] = (CheckDataObject){
                    .type = input->declared_types[token],
                    .object = object,
                    .initializer = input->initializer_nodes[selected],
                    .token = selected,
                };
            }
        }
    }
    if (valid)
    {
        valid = check_data_emit(types, stream, objects, object_count, result->object_symbols, count + 1, &result->link, 0, bytes, scratch, &result->emission);
        if (!valid)
        {
            result->status = result->emission.status == CHECK_DATA_RESOURCE ? CHECK_DATA_INPUT_RESOURCE :
                             result->emission.status == CHECK_DATA_INVALID || result->emission.status == CHECK_DATA_SYMBOL ? CHECK_DATA_INPUT_INVALID : CHECK_DATA_INPUT_UNSUPPORTED;
            result->token = result->emission.failed_token;
        }
    }
    if (valid && link_check(&result->link) != LINK_ERROR_NONE)
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_INVALID;
    }
    arena_pop_to(scratch, scratch_mark);
    if (!valid)
    {
        const CheckDataInputStatus status = result->status;
        const uint32_t token = result->token;
        const CheckDataResult emission = result->emission;
        arena_pop_to(output, output_mark);
        arena_pop_to(bytes, bytes_mark);
        *result = (CheckDataInput){ .status = status, .token = token, .emission = emission };
    }
    return valid;
}
