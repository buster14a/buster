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
                                           reader->parser->options.scalar ? CHECK_INPUT_BINDINGS_STACKS : CHECK_INPUT_BINDINGS_DIRECT;
    const bool parser = selected == CHECK_INPUT_BINDINGS_PARSER_STACKS || selected == CHECK_INPUT_BINDINGS_PARSER_BATCH;
    return parser ? check_input_binding_parser(reader, selected == CHECK_INPUT_BINDINGS_PARSER_STACKS) : check_input_binding_events(reader, selected);
}
