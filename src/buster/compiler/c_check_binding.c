// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Binding handoff (#336). Include after CheckInputReader and its resolver. Declaration/type elaboration needs
// immediate visibility in source order, so the checker's reader is the one producer of binding events: it records
// every declaration, use and scope restore in reader->bindings, which CheckEnvironment publishes (#113). A use
// event's `previous` is the head of its name when it was recorded: the activation event + 1 that the use sees,
// already in the resolvers' result form. The default path reads those retained answers directly, with no replay,
// sort or name lookup. --scalar resolves the same events with the independent stack replay (c_bindings_scalar),
// whose results must be the same. The batch sort and the parser's own grammar replay (#128's ParseBindings,
// mapped onto the reader's use events) remain selectable for measurement and as oracles.
//
// Every mode feeds one consumer, check_input_binding_answers, with an answer per event: each use's declaration
// identity, its type at that point and its reports. Raw attribute uses have reader events but no parser events;
// a use-specific type captured during ordered elaboration (before a later completion) is kept. Tags take their
// canonical declaration during elaboration; the parser modes also check them against the parser's tag column.
// The parser, tree and original scratch arena are immutable: a local Parser view borrows checker scratch only
// while the parser modes build their temporary columns. A failed resolution or an invalid answer publishes nothing
// and sets `resource`. No column has a padding contract.

typedef enum CheckInputBindingMode
{
    // The direct answers, or the stack replay for --scalar.
    CHECK_INPUT_BINDINGS_DEFAULT,
    CHECK_INPUT_BINDINGS_DIRECT,
    CHECK_INPUT_BINDINGS_STACKS,
    CHECK_INPUT_BINDINGS_BATCH,
    CHECK_INPUT_BINDINGS_PARSER_STACKS,
    CHECK_INPUT_BINDINGS_PARSER_BATCH,
    CHECK_INPUT_BINDING_MODES,
} CheckInputBindingMode;

static const char* const check_input_binding_mode_names[CHECK_INPUT_BINDING_MODES] =
{
    "default",
    "direct",
    "stacks",
    "batch",
    "parser-stacks",
    "parser-batch",
};

static TypeId check_input_binding_incomplete(const CheckInputReader* reader, uint32_t declaration, uint32_t token)
{
    return reader->incomplete_array_types != 0 && declaration < reader->parser->tokens.count &&
           token < reader->output->initializer_ends[declaration] ? reader->incomplete_array_types[declaration] : 0;
}

// Whether a declared type is const-qualified, through an array's element type as Clang's -Wunused-const-variable sees it.
static bool check_input_type_const(const TypeTable* types, TypeId type)
{
    bool result = false;
    TypeId current = type;
    for (uint32_t steps = 0; !result && current != 0 && steps < types->types.count; steps += 1)
    {
        const TypeId stripped = type_strip_aligned(types, current);
        result = (type_qualifiers(stripped) & BUSTER_TYPE_CONST) != 0;
        const uint8_t kind = type_kind(types, type_unqualified(stripped));
        if (!result && type_kind_is_array(kind))
        {
            current = type_element(types, stripped);
        }
        else
        {
            current = 0;
        }
    }
    return result;
}

// Whether a declaration is written in the main file, where Clang gives file-scope unused variables' warnings.
static bool check_input_in_main_file(const CheckInputReader* reader, uint32_t token)
{
    const uint32_t source = token < reader->parser->tokens.count ? reader->parser->tokens.sources[token] : BUSTER_CHECK_NONE;
    const uint32_t location = source < reader->pp->output.count && reader->pp->output.locations != NULL ? reader->pp->output.locations[source] : 0;
    const bool result = source < reader->pp->output.count && pp_location_in_main_file(reader->pp, location);
    return result;
}

// A declaration clang's unused warnings leave alone: one the source marks unused or used, a typedef or enumerator,
// a label or tag, or a name whose object is never emitted. Parameters are marked from their definitions' chains.
static bool check_input_unused_suppressed(const CheckInput* input, uint32_t token, uint32_t kind)
{
    const CheckInputAttributes* attributes = input->attributes != NULL && token < input->token_count ? input->attributes[token] : NULL;
    bool marked = false;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const uint32_t attribute = attributes->records[i].kind;
        marked = marked || attribute == CHECK_ATTRIBUTE_UNUSED || attribute == CHECK_ATTRIBUTE_USED;
    }
    const bool result = kind != C_BINDING_OBJECT && kind != C_BINDING_FUNCTION ? true : marked;
    return result;
}

// The definition parameters, whose uses alone decide -Wunused-parameter: a prototype's names never warn.
static bool check_input_parameter_marks(const CheckInput* input, const Parser* parser, uint8_t* marks)
{
    bool valid = parser != NULL && marks != NULL && input->body_declarations != NULL && input->parameters != NULL && input->parameter_next != NULL;
    for (uint32_t opening = 0; valid && opening < parser->tokens.count; opening += 1)
    {
        const uint32_t declaration = input->body_declarations[opening];
        valid = declaration == BUSTER_CHECK_NONE || declaration < parser->tokens.count;
        for (uint32_t parameter = declaration != BUSTER_CHECK_NONE ? input->parameters[opening] : BUSTER_CHECK_NONE, steps = 0;
             valid && parameter != BUSTER_CHECK_NONE && steps < parser->tokens.count; steps += 1)
        {
            valid = parameter < parser->tokens.count;
            marks[parameter] = 1;
            parameter = input->parameter_next[parameter];
        }
    }
    return valid;
}

// A declaration inherits an earlier declaration's unused/used mark, as clang's redeclarations do: an attribute on a
// prototype covers the definition that follows it.
static bool check_input_unused_marked_chain(const CheckInput* input, const CBindings* events, uint32_t event, uint32_t kind)
{
    bool marked = false;
    uint32_t at = event + 1;
    for (uint32_t steps = 0; !marked && at != 0 && steps < events->count; steps += 1)
    {
        const uint32_t previous = events->previous[at - 1];
        if (previous == 0 || events->operation[previous - 1] != C_BINDING_DECLARE || events->key[previous - 1] != events->key[event] ||
            events->depth[previous - 1] != events->depth[event])
        {
            break;
        }
        const uint32_t token = events->declaration[previous - 1];
        marked = token < input->token_count && check_input_unused_suppressed(input, token, kind);
        at = previous;
    }
    return marked;
}

// -Wunused-variable, -Wunused-function and -Wunused-parameter facts, from the resolved binding events: a declaration
// no use names. The declaration's own body decides the completion, as clang reports it after the function.
static void check_input_unused_events(CheckInputReader* reader, const uint8_t* used, const uint8_t* used_tokens, const uint8_t* parameters,
                                      uint8_t* reported)
{
    CheckInput* output = reader->output;
    const Parser* parser = reader->parser;
    const CBindings* events = &reader->bindings;
    const uint32_t count = parser->tokens.count;
    uint32_t* chain_first = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, events->count);
    uint32_t* chain_definition = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, events->count);
    bool valid = chain_first != NULL && chain_definition != NULL;
    if (valid)
    {
        memset(chain_definition, 0, (uint64_t)events->count * sizeof(uint32_t));
        for (uint32_t event = 0; event < events->count; event += 1)
        {
            if (events->operation[event] != C_BINDING_DECLARE)
            {
                continue;
            }
            const uint32_t previous = events->previous[event];
            const bool linked = previous != 0 && previous <= events->count && events->operation[previous - 1] == C_BINDING_DECLARE &&
                                events->key[previous - 1] == events->key[event] && events->depth[previous - 1] == events->depth[event];
            chain_first[event] = linked ? chain_first[previous - 1] : event;
        }
        for (uint32_t event = 0; valid && event < events->count; event += 1)
        {
            const uint32_t token = events->declaration[event];
            if (events->operation[event] == C_BINDING_DECLARE && token < count && output->defined != NULL && output->defined[token] != 0)
            {
                chain_definition[chain_first[event]] = token;
            }
        }
    }
    for (uint32_t event = 0; valid && event < events->count; event += 1)
    {
        const uint32_t token = events->declaration[event];
        const uint32_t kind = events->kind[event];
        const bool local = token < count && output->declaration_bodies[token] < count;
        const uint32_t parameter = token < count && parameters != NULL && parameters[token] != 0;
        const bool implicit = token < count && output->function_flags != NULL &&
                              (output->function_flags[token] & (CHECK_INPUT_FUNCTION_IMPLICIT | CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY)) != 0;
        const bool inline_function = kind == C_BINDING_FUNCTION && token < count && output->function_flags != NULL &&
                                     (output->function_flags[token] & CHECK_INPUT_FUNCTION_INLINE) != 0;
        const bool file_scope = !local && kind == C_BINDING_OBJECT;
        const bool main_file = !file_scope || check_input_in_main_file(reader, token);
        const bool const_variable = file_scope && check_input_type_const(reader->types, reader->declared_types[token]);
        const bool declaration_used = parameter || local ? used_tokens[token] != 0 : used[event] != 0;
        const bool selected = events->operation[event] == C_BINDING_DECLARE && events->key[event] / events->name_capacity == C_BINDING_ORDINARY &&
                              token < count && !implicit && !inline_function && !declaration_used && !check_input_unused_suppressed(output, token, kind) &&
                              !check_input_unused_marked_chain(output, events, event, kind) && main_file &&
                              (parameter || local || (token < count && output->linkage[token] == 1));
        const uint32_t first = events->operation[event] == C_BINDING_DECLARE ? chain_first[event] : event;
        const uint32_t chosen = chain_definition[first] != 0 ? chain_definition[first] : events->declaration[first];
        const uint32_t id = kind == C_BINDING_FUNCTION ? CHECK_WARNING_ID_UNUSED_FUNCTION :
                            parameter ? CHECK_WARNING_ID_UNUSED_PARAMETER :
                            const_variable ? CHECK_WARNING_ID_UNUSED_CONST_VARIABLE : CHECK_WARNING_ID_UNUSED_VARIABLE;
        const uint32_t event_kind = kind == C_BINDING_FUNCTION ? CHECK_WARNING_EVENT_UNUSED_FUNCTION :
                                    parameter ? CHECK_WARNING_EVENT_UNUSED_PARAMETER : CHECK_WARNING_EVENT_UNUSED_VARIABLE;
        if (selected && token == chosen && !reported[token])
        {
            const uint32_t body = output->declaration_bodies[token];
            const uint32_t closing = body < count ? parser->tree.parents[body] : BUSTER_CHECK_NONE;
            const CheckWarningEvent warning = {
                .id = id,
                .kind = event_kind,
                .token = token,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .flags = BUSTER_CHECK_WARNING_DEFAULT_IGNORED,
                .completion_kind = closing < count ? CHECK_WARNING_COMPLETE_AFTER_DELIMITER : CHECK_WARNING_COMPLETE_TU_END,
                .completion = closing < count ? closing : BUSTER_CHECK_NONE,
                .payload = { .name = parser->tokens.names[token] },
            };
            if (!check_input_warning_event(reader, &warning, NULL, 0))
            {
                output->resource = true;
            }
            else
            {
                reported[token] = 1;
            }
        }
    }
    output->resource |= !valid;
}

// `resolved` has an answer for each of reader->bindings' events (only its uses' are read). Every answer is
// checked before any is published: a use names a token, and sees no event at or after itself.
static bool check_input_binding_answers(CheckInputReader* reader, const uint32_t* resolved)
{
    CheckInput* output = reader->output;
    const Parser* parser = reader->parser;
    const CBindings* events = &reader->bindings;
    const uint32_t count = parser->tokens.count;
    bool valid = !output->resource;
    uint64_t identifiers = 0;
    uint64_t declarations = 0;
    for (uint32_t event = 0; valid && event < events->count; event += 1)
    {
        const uint8_t operation = events->operation[event];
        const uint32_t handle = operation == C_BINDING_USE ? resolved[event] : 0;
        valid = operation != C_BINDING_USE ||
                (events->declaration[event] < count && handle <= event &&
                 (handle == 0 || (events->operation[handle - 1] == C_BINDING_DECLARE && events->declaration[handle - 1] < count)));
        identifiers += operation != C_BINDING_RESTORE;
        declarations += operation == C_BINDING_DECLARE;
    }
    output->resource |= !valid;
    if (valid)
    {
        output->binding_statistics.identifiers = identifiers;
        output->binding_statistics.declarations = declarations;
    }
    for (uint32_t event = 0; valid && event < events->count; event += 1)
    {
        if (events->operation[event] == C_BINDING_USE)
        {
            const uint32_t token = events->declaration[event];
            const uint32_t handle = resolved[event];
            const uint32_t declaration = handle != 0 ? events->declaration[handle - 1] : BUSTER_CHECK_NONE;
            const uint32_t old = output->declarations[token];
            const TypeId captured = reader->declared_types[token];
            const TypeId incomplete = check_input_binding_incomplete(reader, declaration, token);
            output->declarations[token] = declaration;
            if (events->key[event] / events->name_capacity == C_BINDING_ORDINARY)
            {
                reader->declared_types[token] = incomplete != 0 ? incomplete : declaration < count ?
                    (old == declaration && captured != 0 ? captured : reader->declared_types[declaration]) : 0;
                const bool typedef_use = handle != 0 && events->kind[handle - 1] == C_BINDING_TYPEDEF;
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
    const bool unused_columns = output->body_declarations != NULL && output->parameters != NULL && output->parameter_next != NULL &&
                                output->declaration_bodies != NULL && output->declaration_kinds != NULL && output->static_objects != NULL &&
                                output->thread_local != NULL && output->linkage != NULL;
    if (valid && unused_columns && output->report_count == 0 && events->count != 0)
    {
        uint8_t* used = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, events->count);
        uint8_t* used_tokens = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
        uint8_t* parameters = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
        uint8_t* reported = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
        if (used != NULL && used_tokens != NULL && parameters != NULL && reported != NULL)
        {
            memset(used, 0, events->count);
            memset(used_tokens, 0, count);
            memset(parameters, 0, count);
            memset(reported, 0, count);
            for (uint32_t event = 0; event < events->count; event += 1)
            {
                if (events->operation[event] == C_BINDING_USE)
                {
                    const uint32_t handle = resolved[event];
                    if (handle != 0 && handle <= events->count && events->operation[handle - 1] == C_BINDING_DECLARE)
                    {
                        // A use binds the whole declaration chain: a prototype and its definition are one entity,
                        // and a use of either makes every declaration in the chain used.
                        uint32_t at = handle;
                        for (uint32_t steps = 0; at != 0 && steps < events->count; steps += 1)
                        {
                            const uint32_t declaration = at - 1;
                            if (events->operation[declaration] != C_BINDING_DECLARE || events->key[declaration] != events->key[handle - 1] ||
                                events->depth[declaration] != events->depth[handle - 1])
                            {
                                break;
                            }
                            used[declaration] = 1;
                            if (events->declaration[declaration] < count)
                            {
                                used_tokens[events->declaration[declaration]] = 1;
                            }
                            at = events->previous[declaration];
                        }
                    }
                }
            }
            // A use that bound an earlier declaration also keeps a later definition of the same entity used.
            for (uint32_t event = 0; event < events->count; event += 1)
            {
                const uint32_t previous = events->previous[event];
                if (events->operation[event] == C_BINDING_DECLARE && previous != 0 && previous <= events->count &&
                    events->operation[previous - 1] == C_BINDING_DECLARE && events->key[previous - 1] == events->key[event] &&
                    events->depth[previous - 1] == events->depth[event] && used[previous - 1] != 0)
                {
                    used[event] = 1;
                }
            }
            if (check_input_parameter_marks(output, parser, parameters))
            {
                check_input_unused_events(reader, used, used_tokens, parameters, reported);
            }
        }
    }
    return valid && output->report_count == 0;
}

// Resolve the reader's own events: DIRECT reads each use's retained answer, STACKS replays them independently and
// BATCH sorts them. The replay's and the sort's scratch is released before returning.
static bool check_input_binding_events(CheckInputReader* reader, CheckInputBindingMode mode)
{
    CheckInput* output = reader->output;
    const CBindings* bindings = &reader->bindings;
    const uint64_t mark = reader->scratch->length;
    output->binding_event_count = bindings->count;
    output->binding_statistics = (CBindingStatistics){ 0 };
    const bool direct = mode == CHECK_INPUT_BINDINGS_DIRECT;
    bool valid = bindings->valid && !output->resource && (mode == CHECK_INPUT_BINDINGS_DIRECT || mode == CHECK_INPUT_BINDINGS_STACKS || mode == CHECK_INPUT_BINDINGS_BATCH);
    valid = valid && (!direct || bindings->count == 0 || bindings->previous != 0);
    uint32_t* resolved = valid && !direct && bindings->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, bindings->count) : 0;
    valid = valid && (direct || bindings->count == 0 || resolved != 0);
    CBindingStatistics statistics = { 0 };
    if (valid && !direct && bindings->count != 0)
    {
        valid = mode == CHECK_INPUT_BINDINGS_STACKS ? c_bindings_scalar(bindings, resolved, reader->scratch) :
                                                      c_bindings_batch(bindings, resolved, reader->scratch, &statistics);
    }
    output->resource |= !valid;
    valid = valid && check_input_binding_answers(reader, direct ? bindings->previous : resolved);
    output->binding_statistics.walks = statistics.walks;
    output->binding_statistics.walked_uses = statistics.walked_uses;
    output->binding_statistics.fallback_uses = statistics.fallback_uses;
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

// The parser modes: the parser's own grammar replay (parse_bindings_build) and resolution, mapped onto the
// reader's use events. Kept for measurement and as an oracle; the reader's events already hold these answers.
static bool check_input_binding_parser(CheckInputReader* reader, bool scalar)
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

static bool check_input_bindings(CheckInputReader* reader, CheckInputBindingMode mode)
{
    const CheckInputBindingMode selected = mode != CHECK_INPUT_BINDINGS_DEFAULT ? mode :
                                           BUSTER_SCALAR_PATH(reader->parser->options.scalar) ? CHECK_INPUT_BINDINGS_STACKS : CHECK_INPUT_BINDINGS_DIRECT;
    const bool parser = selected == CHECK_INPUT_BINDINGS_PARSER_STACKS || selected == CHECK_INPUT_BINDINGS_PARSER_BATCH;
    return parser ? check_input_binding_parser(reader, selected == CHECK_INPUT_BINDINGS_PARSER_STACKS) : check_input_binding_events(reader, selected);
}
