// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Parser declaration-column consumption and the retained semantic event oracle. Include after CheckInputReader
// and its resolver. Declaration/type elaboration still needs immediate visibility in source order and publishes
// its semantic events to CheckEnvironment. Ordinary expression identities come from ParseBindings, independently
// resolved by the selected scalar/batch kernel. Its source-less builtin typedefs never index a source column.
// Raw attributes have no parser binding events; those uses retain the identity and type captured during ordered
// elaboration. Tags map lexical parser declarations through the checker's canonical tag declaration. A use-specific
// type already captured before a later completion remains unchanged. The parser, tree and original scratch arena
// are immutable: a local Parser view borrows checker scratch only while constructing the temporary columns.
// Failed allocation/resolution publishes no parser results. No column has a padding contract. The original event
// consumer remains an independent semantic-history oracle for focused synthetic and real-source tests.

static TypeId check_input_binding_incomplete(const CheckInputReader* reader, uint32_t declaration, uint32_t token)
{
    return reader->incomplete_array_types != 0 && declaration < reader->parser->tokens.count &&
           token < reader->output->initializer_ends[declaration] ? reader->incomplete_array_types[declaration] : 0;
}

static bool check_input_binding_events(CheckInputReader* reader, bool scalar)
{
    CheckInput* output = reader->output;
    const CBindings* bindings = &reader->bindings;
    const uint64_t mark = reader->scratch->length;
    output->binding_event_count = bindings->count;
    output->binding_statistics = (CBindingStatistics){ 0 };
    bool valid = bindings->valid && !output->resource;
    uint32_t* resolved = valid && bindings->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, bindings->count) : 0;
    valid = valid && (bindings->count == 0 || resolved != 0);
    if (valid && bindings->count != 0)
    {
        valid = scalar ? c_bindings_scalar(bindings, resolved, reader->scratch) :
                         c_bindings_batch(bindings, resolved, reader->scratch, &output->binding_statistics);
    }
    if (!valid)
    {
        output->resource = true;
    }
    for (uint32_t event = 0; valid && event < bindings->count; event += 1)
    {
        const uint8_t operation = bindings->operation[event];
        if (scalar)
        {
            output->binding_statistics.identifiers += operation != C_BINDING_RESTORE;
            output->binding_statistics.declarations += operation == C_BINDING_DECLARE;
        }
        if (operation == C_BINDING_USE)
        {
            const uint32_t token = bindings->declaration[event];
            const uint32_t handle = resolved[event];
            const uint32_t declaration = handle != 0 ? bindings->declaration[handle - 1] : BUSTER_CHECK_NONE;
            const TypeId incomplete = check_input_binding_incomplete(reader, declaration, token);
            output->declarations[token] = declaration;
            if (bindings->key[event] / bindings->name_capacity == C_BINDING_ORDINARY)
            {
                reader->declared_types[token] = incomplete != 0 ? incomplete : handle != 0 ? reader->declared_types[declaration] : 0;
                if (handle != 0 && (reader->declared_types[token] == 0 ||
                    (bindings->kind[handle - 1] == C_BINDING_TYPEDEF && reader->parser->tree.roles[token] == PARSE_ROLE_OPERAND && !reader->ignored[token])))
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
            }
            if (handle == 0)
            {
                check_input_report(reader, token, CHECK_INPUT_UNDECLARED);
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid && !output->resource && output->report_count == 0;
}

static bool check_input_binding_columns(CheckInputReader* reader, const ParseBindings* parsed)
{
    CheckInput* output = reader->output;
    const Parser* parser = reader->parser;
    const CBindings* events = &reader->bindings;
    const uint32_t count = parser->tokens.count;
    bool valid = parsed->valid && parsed->count == count && events->valid && !output->resource &&
                 parsed->token_events != 0 && parsed->declarations != 0 && parsed->namespaces != 0 && parsed->kinds != 0;
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        const uint32_t event = parsed->token_events[token];
        const uint32_t declaration = parsed->declarations[token];
        valid = event <= parsed->events.count && (declaration < count || declaration == BUSTER_PARSE_NONE);
    }
    for (uint32_t event = 0; valid && event < events->count; event += 1)
    {
        valid = events->operation[event] != C_BINDING_USE || events->declaration[event] < count;
    }
    output->resource |= !valid;
    for (uint32_t event = 0; valid && event < events->count; event += 1)
    {
        const uint8_t operation = events->operation[event];
        output->binding_statistics.identifiers += operation != C_BINDING_RESTORE;
        output->binding_statistics.declarations += operation == C_BINDING_DECLARE;
        if (operation == C_BINDING_USE)
        {
            const uint32_t token = events->declaration[event];
            const uint32_t old = output->declarations[token];
            const bool ordinary = events->key[event] / events->name_capacity == C_BINDING_ORDINARY;
            const bool recorded = parsed->token_events[token] != 0 && parsed->namespaces[token] == C_BINDING_ORDINARY;
            const uint32_t declaration = ordinary && recorded ? parsed->declarations[token] : old;
            const TypeId captured = reader->declared_types[token];
            const TypeId incomplete = check_input_binding_incomplete(reader, declaration, token);
            output->declarations[token] = declaration;
            if (ordinary)
            {
                reader->declared_types[token] = incomplete != 0 ? incomplete : declaration < count ?
                    (old == declaration && captured != 0 ? captured : reader->declared_types[declaration]) : 0;
                const bool typedef_use = recorded ? parsed->kinds[token] == C_BINDING_TYPEDEF :
                                                    declaration < count && reader->declaration_kinds[declaration] == C_BINDING_TYPEDEF;
                if (declaration < count && (reader->declared_types[token] == 0 ||
                    (captured != 0 && old != declaration) ||
                    (typedef_use && parser->tree.roles[token] == PARSE_ROLE_OPERAND && !reader->ignored[token])))
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
            }
            if (declaration == BUSTER_CHECK_NONE)
            {
                check_input_report(reader, token, CHECK_INPUT_UNDECLARED);
            }
        }
    }
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        const uint32_t declaration = parsed->declarations[token];
        if (parsed->token_events[token] != 0 && parsed->namespaces[token] == C_BINDING_TAG &&
            declaration < count && output->declarations[token] != BUSTER_CHECK_NONE)
        {
            const uint32_t canonical = output->declarations[declaration];
            if (canonical < count && reader->declared_types[canonical] != 0)
            {
                output->declarations[token] = canonical;
            }
            else
            {
                check_input_report(reader, token, CHECK_INPUT_DECLARATION);
            }
        }
    }
    return valid && !output->resource && output->report_count == 0;
}

static bool check_input_bindings(CheckInputReader* reader, bool scalar)
{
    CheckInput* output = reader->output;
    const uint64_t mark = reader->arena->length;
    Parser parser = *reader->parser;
    parser.scratch = *reader->scratch;
    ParseBindings parsed = { 0 };
    output->binding_event_count = reader->bindings.count;
    output->binding_statistics = (CBindingStatistics){ 0 };
    bool valid = reader->bindings.valid && !output->resource && reader->arena != reader->scratch;
    if (valid && parser.meanings.meanings == 0)
    {
        // Validated diagnostic-prefix views deliberately borrow no owning parser arenas. Reconstitute only
        // their initial identifier meanings in temporary storage; ParseBindings replays the retained grammar.
        parser.meanings.capacity = reader->pp->table->count;
        parser.meanings.meanings = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, parser.meanings.capacity);
        valid = parser.meanings.meanings != 0;
        if (valid)
        {
            memset(parser.meanings.meanings, PARSE_MEANING_ORDINARY, parser.meanings.capacity);
            for (uint32_t name = 0; name < BUSTER_PARSE_PREDECLARED_TYPEDEFS; name += 1)
            {
                valid &= parser.predeclared[name] < parser.meanings.capacity;
                if (valid)
                {
                    parser.meanings.meanings[parser.predeclared[name]] = PARSE_MEANING_TYPEDEF;
                }
            }
        }
    }
    valid = valid && parse_bindings_build(&parsed, &parser, reader->arena) && parse_bindings_resolve(&parsed, scalar, reader->scratch);
    output->resource |= !valid;
    if (valid)
    {
        output->parser_binding_event_count = parsed.events.count;
        output->parser_binding_statistics = parsed.statistics;
        valid = check_input_binding_columns(reader, &parsed);
    }
    arena_pop_to(reader->arena, mark);
    return valid && !output->resource && output->report_count == 0;
}
