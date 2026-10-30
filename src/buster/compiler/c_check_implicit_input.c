// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Source-order implicit declarations. Library eligibility comes from the frozen catalogue's language and 'f'
// attributes, not from successful descriptor decoding. Explicit source bindings always win. Library calls use the
// ordinary declaration, ABI and external-symbol paths; no intrinsic call record or guessed runtime helper is made.

static const BuiltinIntrinsicInfo* check_input_implicit_info(const CheckInputReader* reader, uint32_t name)
{
    const uint32_t cached = reader->intrinsic_names != NULL ? reader->intrinsic_names[name] : 0;
    const BuiltinIntrinsicInfo* info = NULL;
    if (cached == UINT32_MAX)
    {
        const InternSpan span = reader->pp->table->spans[name];
        info = builtin_intrinsic_lookup((const char*)reader->pp->table->records + span.offset, span.length);
    }
    else if ((cached & UINT32_C(0x80000000)) != 0)
    {
        info = builtin_intrinsic_at(cached & UINT32_C(0x7fffffff));
    }
    else if (cached != 0 && cached <= reader->output->intrinsic_declaration_count)
    {
        info = reader->output->intrinsic_declarations[cached - 1].info;
    }
    return info;
}

static bool check_input_library_enabled(const BuiltinIntrinsicInfo* info, const PPOptions* options)
{
    const bool language = strcmp(info->language_token, "ALL_LANGUAGES") == 0 ||
                          (strcmp(info->language_token, "ALL_GNU_LANGUAGES") == 0 && options->language.gnu) ||
                          (strcmp(info->language_token, "C23_LANG") == 0 && options->language.standard >= 2023);
    bool enabled = language && strcmp(info->shard, "generic") == 0 && info->features_length == 0 &&
                   !options->no_builtin && !options->freestanding;
    for (uint32_t i = 0; enabled && i < options->no_builtin_function_count; i += 1)
    {
        const char* name = options->no_builtin_functions[i];
        enabled = strlen(name) != info->name_length || memcmp(name, info->name, info->name_length) != 0;
    }
    return enabled;
}

// The only newly scanned list is the explicitly supplied disabling names, on a missing library name alone. Ordinary
// operands reuse the intrinsic adapter's deduplicated name cache. Source-order binding itself cannot be reordered.
static void check_input_implicit_function(CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t name = parser->tokens.names[token];
    const bool missing = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name) == 0;
    const bool explicit_builtin = intern_length(reader->pp->table, name) >= 10 && memcmp(intern_bytes(reader->pp->table, name), "__builtin_", 10) == 0;
    if (missing && !explicit_builtin)
    {
        const BuiltinIntrinsicInfo* info = check_input_implicit_info(reader, name);
        const bool library_name = info != NULL && memchr(info->attributes, 'f', info->attributes_length) != NULL;
        const bool eligible = library_name && check_input_library_enabled(info, &reader->pp->options);
        const bool ordinary = (info == NULL || library_name) && parser->language.standard < 2023 && check_input_intrinsic_call_shape(parser, token);
        check_input_lane_refuse(reader, reader->lane && (eligible || ordinary), "an implicit function declaration needs source-order linkage");
        if (!reader->lane && (eligible || ordinary))
        {
            CheckInputIntrinsicDeclaration decoded = { 0 };
            if (eligible)
            {
                decoded = check_input_intrinsic_decode(reader, info);
                reader->output->resource |= decoded.types.status == BUILTIN_INTRINSIC_TYPES_CAPACITY;
            }
            const bool library = eligible && decoded.types.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && !decoded.types.signature_ignored;
            const bool header = eligible && ordinary && decoded.types.status == BUILTIN_INTRINSIC_TYPES_UNSUPPORTED &&
                                (decoded.types.unsupported & BUSTER_INTRINSIC_TYPE_UNSUPPORTED_CONTEXT) != 0 &&
                                decoded.types.error_offset < info->encoding_length &&
                                (info->encoding[decoded.types.error_offset] == 'P' || info->encoding[decoded.types.error_offset] == 'J');
            if (header && !reader->output->resource)
            {
                const CheckWarningEvent event =
                {
                    .id = CHECK_WARNING_ID_BUILTIN_HEADER,
                    .kind = CHECK_WARNING_EVENT_BUILTIN_HEADER,
                    .token = token,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = token + 1,
                    .payload = { .name = name, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                check_input_warning_event(reader, &event, NULL, 0);
            }
            const TypeId type = library ? decoded.types.function_type : ordinary ? check_input_type_result(reader, type_function(reader->types, BUSTER_TYPE_INT, NULL, 0, 0)) : 0;
            CheckInput* input = reader->output;
            const uint32_t linked = reader->linked_declarations[name];
            const bool diagnosed = library && linked != 0 && (input->function_flags[linked - 1] & CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY) != 0;
            if (!diagnosed && type != 0 && input->implicit_function_count == reader->implicit_function_capacity)
            {
                const uint32_t capacity = reader->implicit_function_capacity == 0 ? 8 :
                                          reader->implicit_function_capacity <= UINT32_MAX / 2 ? reader->implicit_function_capacity * 2 : 0;
                uint32_t* tokens = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity) : NULL;
                if (tokens == NULL)
                {
                    input->resource = true;
                }
                else
                {
                    if (input->implicit_function_count != 0)
                    {
                        memcpy(tokens, input->implicit_function_tokens, (uint64_t)input->implicit_function_count * sizeof(uint32_t));
                    }
                    input->implicit_function_tokens = tokens;
                    reader->implicit_function_capacity = capacity;
                }
            }
            CheckInputAttributes* attributes = NULL;
            if (library && !input->resource && memchr(info->attributes, 'r', info->attributes_length) != NULL)
            {
                attributes = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAttributes, 1);
                CheckInputAttribute* record = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAttribute, 1);
                input->resource |= attributes == NULL || record == NULL;
                if (!input->resource)
                {
                    *record = (CheckInputAttribute){ .first = token, .end = token + 1, .kind = CHECK_ATTRIBUTE_NORETURN, .schema = CHECK_ATTRIBUTE_NO_ARGUMENTS, .resolved = true };
                    *attributes = (CheckInputAttributes){ .records = record, .count = 1, .capacity = 1 };
                }
            }
            if (!input->resource && type != 0)
            {
                if (!diagnosed)
                {
                    input->implicit_function_tokens[input->implicit_function_count++] = token;
                }
                input->declarations[token] = token;
                reader->declared_types[token] = type;
                reader->declaration_kinds[token] = C_BINDING_FUNCTION;
                input->function_flags[token] = CHECK_INPUT_FUNCTION_IMPLICIT | (library ? CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY : 0);
                input->attributes[token] = attributes;
                reader->objects[token] = token + 1;
                reader->static_objects[token] = true;
                reader->linkage[token] = 2;
                input->declaration_bodies[token] = reader->function_body;
                check_input_bind(reader, token, token);
            }
        }
    }
}
