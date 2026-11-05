// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Retained weak/redefine_extname directives. The cold history owns their source coordinates; the completed binding
// stream supplies translation-unit declarations, independent of local shadows and of the bodies' reading schedule.

typedef enum CheckInputSymbolPragmaKind
{
    CHECK_SYMBOL_PRAGMA_WEAK,
    CHECK_SYMBOL_PRAGMA_REDEFINE,
    CHECK_SYMBOL_PRAGMA_ALIAS,
} CheckInputSymbolPragmaKind;

typedef enum CheckInputSymbolPragmaWarning
{
    CHECK_SYMBOL_WARNING_NONE,
    CHECK_SYMBOL_WARNING_IDENTIFIER,
    CHECK_SYMBOL_WARNING_EXTRA,
    CHECK_SYMBOL_WARNING_UNDECLARED,
    CHECK_SYMBOL_WARNING_TYPEDEF,
    CHECK_SYMBOL_WARNING_INTERNAL,
} CheckInputSymbolPragmaWarning;

typedef struct CheckInputSymbolPragma CheckInputSymbolPragma;
struct CheckInputSymbolPragma
{
    uint32_t pragma;
    uint32_t source;
    uint32_t end;
    uint32_t token;
    uint32_t name;
    uint32_t target;
    uint32_t diagnostic_source;
    uint32_t diagnostic_location;
    uint32_t declaration;
    uint8_t kind;
    uint8_t warning;
    bool supported;
};

struct CheckInputSymbolPragmas
{
    CheckInputSymbolPragma* records;
    uint32_t count;
};

static const CheckInputSymbolPragma* check_input_symbol_warning(const CheckInput* input, const CheckWarningEvent* event)
{
    const CheckInputSymbolPragmas* history = input->symbol_pragmas;
    const uint32_t index = event->payload.data;
    const CheckInputSymbolPragma* result = event->kind == CHECK_WARNING_EVENT_PRAGMA && (event->flags & BUSTER_CHECK_WARNING_SYMBOL_PRAGMA) != 0 &&
                                          index != 0 && history != NULL && index <= history->count ?
                                          history->records + index - 1 : NULL;
    return result;
}

static bool check_input_symbol_identifier(const CheckInputReader* reader, uint32_t source, uint32_t end)
{
    const PPTokens* output = &reader->pp->output;
    const bool result = source < end && output->kinds[source] == C_TOKEN_RAW_IDENTIFIER &&
                        !c_name_is_keyword(output->values[source], reader->pp->options.language);
    return result;
}

static bool check_input_symbol_record(CheckInputReader* reader, uint32_t pragma, uint32_t* previous_end,
                                       uint32_t* previous_token, uint64_t** macros, CheckInputSymbolPragma* record)
{
    uint32_t source = 0;
    uint32_t end = 0;
    uint32_t token = 0;
    const PPTokens* output = &reader->pp->output;
    const bool retained = check_input_retained_pragma(reader, pragma, previous_end, previous_token, &source, &end, &token);
    const bool weak = retained && pp_token_is_name(reader->pp, output, source + 2, end, "weak");
    const bool rename = retained && pp_token_is_name(reader->pp, output, source + 2, end, "redefine_extname");
    const bool matched = weak || rename;
    if (matched)
    {
        *record = (CheckInputSymbolPragma){ .pragma = pragma, .source = source, .end = end, .token = token,
                                           .declaration = BUSTER_CHECK_NONE, .kind = rename ? CHECK_SYMBOL_PRAGMA_REDEFINE : CHECK_SYMBOL_PRAGMA_WEAK,
                                           .supported = true };
        uint32_t at = source + 3;
        if (!check_input_symbol_identifier(reader, at, end))
        {
            record->warning = CHECK_SYMBOL_WARNING_IDENTIFIER;
        }
        else
        {
            record->name = output->values[at++];
            if (weak && at < end && output->kinds[at] == C_TOKEN_EQUAL)
            {
                record->kind = CHECK_SYMBOL_PRAGMA_ALIAS;
                at += 1;
            }
            if (rename || record->kind == CHECK_SYMBOL_PRAGMA_ALIAS)
            {
                if (!check_input_symbol_identifier(reader, at, end))
                {
                    record->warning = CHECK_SYMBOL_WARNING_IDENTIFIER;
                }
                else
                {
                    record->target = output->values[at++];
                }
            }
            if (record->warning == CHECK_SYMBOL_WARNING_NONE && at != end)
            {
                record->warning = CHECK_SYMBOL_WARNING_EXTRA;
            }
        }
        record->diagnostic_source = at < end ? at : end - 1;
        if (record->warning != CHECK_SYMBOL_WARNING_NONE && at == end)
        {
            const uint32_t last = end - 1;
            const bool named = check_input_pragma_name(reader, last) != 0;
            record->diagnostic_location = output->locations[last] + (named ? intern_length(reader->pp->table, output->values[last]) : 1);
        }
        // Raw -E callers cannot recover a macro's earlier value. The driver's semantic stream already expanded it.
        for (uint32_t s = source + 3; !reader->pp->options.semantic_pragmas && record->supported && s < end; s += 1)
        {
            const IdentId name = check_input_pragma_name(reader, s);
            record->supported = name == 0 || !check_input_pragma_macro(reader, macros, name);
        }
        // An alias can synthesize a typed declaration without a C declarator. It needs its own binding integration;
        // recognizing its spelling alone must not turn it into an ignored directive.
        record->supported &= record->warning != CHECK_SYMBOL_WARNING_NONE || record->kind != CHECK_SYMBOL_PRAGMA_ALIAS;
    }
    return matched;
}

static bool check_input_symbol_pragmas(CheckInputReader* reader, uint8_t* handled, bool complete_tree)
{
    const uint64_t retained = reader->arena->length;
    const uint64_t temporary = reader->scratch->length;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    uint64_t* macros = NULL;
    uint32_t count = 0;
    for (uint32_t p = 0; p < reader->parser->tokens.pragma_count; p += 1)
    {
        CheckInputSymbolPragma record;
        count += check_input_symbol_record(reader, p, &previous_end, &previous_token, &macros, &record);
    }
    CheckInputSymbolPragmas* history = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputSymbolPragmas, 1) : NULL;
    CheckInputSymbolPragma* records = history != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputSymbolPragma, count) : NULL;
    uint32_t* links = count != 0 && complete_tree ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)reader->parser->tokens.count + 1) : NULL;
    bool valid = !reader->output->resource && (count == 0 || (records != NULL && (!complete_tree || links != NULL)));
    if (valid && count != 0)
    {
        *history = (CheckInputSymbolPragmas){ .records = records };
        previous_end = 0;
        previous_token = 0;
        uint32_t cursor = 0;
        uint32_t group = BUSTER_CHECK_NONE;
        for (uint32_t p = 0; p < reader->parser->tokens.pragma_count; p += 1)
        {
            CheckInputSymbolPragma record;
            if (check_input_symbol_record(reader, p, &previous_end, &previous_token, &macros, &record))
            {
                while (complete_tree && cursor < record.token)
                {
                    const uint8_t role = reader->parser->tree.roles[cursor];
                    if (role < PARSE_ROLE_COUNT && parse_closer_roles[role] != PARSE_ROLE_NONE)
                    {
                        links[cursor] = group;
                        group = cursor;
                    }
                    else if (group < reader->parser->tokens.count && reader->parser->tree.parents[group] == cursor)
                    {
                        group = links[group];
                    }
                    cursor += 1;
                }
                if (complete_tree && record.warning == CHECK_SYMBOL_WARNING_NONE && record.token != 0)
                {
                    const uint8_t role = reader->parser->tree.roles[record.token - 1];
                    const uint8_t scope = group < reader->parser->tokens.count ? reader->parser->tree.roles[group] : PARSE_ROLE_NONE;
                    const bool boundary = role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_BLOCK ||
                                          role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK;
                    record.supported &= boundary && (group == BUSTER_CHECK_NONE || scope == PARSE_ROLE_OPEN_BODY || scope == PARSE_ROLE_OPEN_BLOCK);
                }
                records[history->count++] = record;
                handled[p] = record.supported ? 1 : 0;
            }
        }
        reader->output->symbol_pragmas = history;
    }
    if (!valid)
    {
        arena_pop_to(reader->arena, retained);
        reader->output->resource = true;
    }
    arena_pop_to(reader->scratch, temporary);
    return valid;
}

static bool check_input_symbol_warning_append(CheckInputReader* reader, uint32_t index, CheckWarningBuffer* buffer,
                                               uint32_t token, uint32_t completion_kind, uint32_t completion)
{
    const CheckInputSymbolPragma* record = reader->output->symbol_pragmas->records + index;
    const CheckWarningEvent event = {
        .id = record->warning == CHECK_SYMBOL_WARNING_UNDECLARED ? CHECK_WARNING_ID_WEAK_UNDECLARED :
              record->warning == CHECK_SYMBOL_WARNING_TYPEDEF ? CHECK_WARNING_ID_IGNORED_ATTRIBUTES :
              record->warning == CHECK_SYMBOL_WARNING_INTERNAL ? CHECK_WARNING_ID_REDEFINE_EXTNAME : CHECK_WARNING_ID_IGNORED_PRAGMA,
        .kind = CHECK_WARNING_EVENT_PRAGMA,
        .flags = BUSTER_CHECK_WARNING_SYMBOL_PRAGMA,
        .token = token,
        .source = record->diagnostic_source,
        .completion_kind = completion_kind,
        .completion = completion,
        .payload = { .name = record->name, .token = record->source, .data = index + 1 },
    };
    const bool valid = check_warning_append(buffer, &event, NULL, 0);
    reader->output->resource |= !valid;
    return valid;
}

// Append to the independently retained lexical facts after the unknown-pragma collector has published its view.
static bool check_input_symbol_syntax_warnings(CheckInputReader* reader)
{
    const CheckInputSymbolPragmas* history = reader->output->symbol_pragmas;
    const CheckWarningEvents before = reader->output->pragma_warnings;
    CheckWarningBuffer buffer = { .arena = reader->arena, .events = (CheckWarningEvent*)before.events, .count = before.count, .capacity = before.count };
    bool valid = true;
    for (uint32_t i = 0; valid && history != NULL && i < history->count; i += 1)
    {
        const CheckInputSymbolPragma* record = history->records + i;
        if (record->supported && (record->warning == CHECK_SYMBOL_WARNING_IDENTIFIER || record->warning == CHECK_SYMBOL_WARNING_EXTRA))
        {
            valid = check_input_symbol_warning_append(reader, i, &buffer, record->token, CHECK_WARNING_COMPLETE_AT_TOKEN, record->token);
        }
    }
    if (valid)
    {
        reader->output->pragma_warnings = check_warning_view(&buffer);
    }
    return valid;
}

static uint32_t check_input_symbol_lower(const uint64_t* keys, uint32_t count, uint32_t name)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = (uint32_t)(keys[middle] >> 32) < name;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low;
}

static bool check_input_symbol_attribute(CheckInputReader* reader, CheckInputSymbolPragma* record, uint32_t token)
{
    const CheckInputAttributes* previous = check_input_attributes_at(reader->output, token);
    CheckInputAttributes* attributes = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAttributes, 1);
    bool valid = attributes != NULL;
    if (valid)
    {
        *attributes = (CheckInputAttributes){ 0 };
        valid = previous == NULL || check_input_attribute_copy(reader, previous, attributes);
        const CheckInputAttribute attribute = {
            .first = token, .end = token + 1, .name_token = token,
            .kind = record->kind == CHECK_SYMBOL_PRAGMA_WEAK ? CHECK_ATTRIBUTE_WEAK : CHECK_ATTRIBUTE_ASM_LABEL,
            .text = record->kind == CHECK_SYMBOL_PRAGMA_REDEFINE ?
                (String8){ (char*)intern_bytes(reader->pp->table, record->target), intern_length(reader->pp->table, record->target) } : (String8){ 0 },
            .resolved = true,
        };
        valid = valid && check_input_attribute_append(reader, attributes, attribute);
        if (valid)
        {
            check_input_set_attributes_at(reader->output, token, attributes);
            record->declaration = token;
        }
    }
    reader->output->resource |= !valid;
    return valid;
}

// Only units with these directives pay this scan. Compress file-scope declarations and evaluated external uses from
// the binding columns, then stable-sort their names/objects. Each directive searches one declaration segment and one
// earliest-use entry, never the unit's whole token stream. The two sorts share their temporary buffer.
static void check_input_symbol_finish(CheckInputReader* reader, bool complete)
{
    CheckInput* input = reader->output;
    CheckInputSymbolPragmas* history = input->symbol_pragmas;
    const CBindings* bindings = &reader->bindings;
    const uint64_t temporary = reader->scratch->length;
    const bool present = history != NULL && history->count != 0 && input->deferred_tokens == NULL;
    bool renames = false;
    for (uint32_t i = 0; present && !renames && i < history->count; i += 1)
    {
        renames = history->records[i].supported && history->records[i].kind == CHECK_SYMBOL_PRAGMA_REDEFINE;
    }
    uint64_t* keys = present ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint64_t, (uint64_t)bindings->count * (renames ? 3 : 2) + 1) : NULL;
    uint64_t* uses = keys != NULL && renames ? keys + (uint64_t)bindings->count * 2 : NULL;
    bool valid = !present || keys != NULL;
    uint32_t count = 0;
    uint32_t use_count = 0;
    const bool scalar = BUSTER_SCALAR_PATH(reader->parser->options.scalar);
    for (uint32_t first = 0; valid && present && first < bindings->count; first += 64)
    {
        const uint64_t lanes = scalar ? check_input_scan_all(first, bindings->count) : check_input_scan_equal(bindings->operation, first, bindings->count, C_BINDING_DECLARE);
        for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t event = first + (uint32_t)__builtin_ctzll(selected);
            if (bindings->operation[event] == C_BINDING_DECLARE && bindings->depth[event] == 0 &&
                bindings->key[event] < bindings->name_capacity && bindings->declaration[event] < input->token_count)
            {
                keys[count++] = (uint64_t)bindings->key[event] << 32 | event;
            }
        }
        const uint64_t references = !renames ? 0 : scalar ? check_input_scan_all(first, bindings->count) :
                                    check_input_scan_equal(bindings->operation, first, bindings->count, C_BINDING_USE);
        for (uint64_t selected = references; selected != 0; selected &= selected - 1)
        {
            const uint32_t event = first + (uint32_t)__builtin_ctzll(selected);
            const uint32_t token = bindings->declaration[event];
            const uint32_t declaration = token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
            if (bindings->operation[event] == C_BINDING_USE && bindings->key[event] < bindings->name_capacity &&
                declaration < input->token_count && check_input_linkage(input->declaration_bits, declaration) == 2 &&
                !check_input_warning_unevaluated(reader, token))
            {
                uses[use_count++] = (uint64_t)input->objects[declaration] << 32 | bindings->point[event];
            }
        }
    }
    const uint64_t* references = valid && use_count > 1 ? c_bindings_sort(uses, keys + bindings->count, use_count, !scalar, UINT32_MAX) : uses;
    uint32_t unique = 0;
    for (uint32_t i = 0; valid && i < use_count; i += 1)
    {
        const uint64_t key = references[i];
        if (unique != 0 && (uint32_t)(uses[unique - 1] >> 32) == (uint32_t)(key >> 32))
        {
            const uint32_t first = (uint32_t)uses[unique - 1];
            uses[unique - 1] = (key & UINT64_C(0xFFFFFFFF00000000)) | (first < (uint32_t)key ? first : (uint32_t)key);
        }
        else
        {
            uses[unique++] = key;
        }
    }
    const uint64_t* ordered = valid && count > 1 ? c_bindings_sort(keys, keys + bindings->count, count, !scalar, UINT32_MAX) : keys;
    for (uint32_t i = 0; valid && present && i < history->count; i += 1)
    {
        CheckInputSymbolPragma* record = history->records + i;
        if (record->supported && record->warning == CHECK_SYMBOL_WARNING_NONE && record->token <= reader->expression_limit)
        {
            const uint32_t begin = check_input_symbol_lower(ordered, count, record->name);
            uint32_t end = begin;
            uint32_t previous = BUSTER_CHECK_NONE;
            uint32_t next = BUSTER_CHECK_NONE;
            uint32_t definition = BUSTER_CHECK_NONE;
            uint32_t label = BUSTER_CHECK_NONE;
            while (end < count && (uint32_t)(ordered[end] >> 32) == record->name)
            {
                const uint32_t event = (uint32_t)ordered[end++];
                const uint32_t token = bindings->declaration[event];
                previous = bindings->point[event] <= record->token ? token : previous;
                next = next == BUSTER_CHECK_NONE && bindings->point[event] > record->token ? token : next;
                const uint32_t point = bindings->point[event];
                const bool body = input->declaration_kinds[token] == C_BINDING_FUNCTION && point < input->token_count &&
                                  reader->parser->tree.roles[point] == PARSE_ROLE_OPEN_BODY;
                const uint32_t initialized = check_sparse_get(input->initializer_ends, token);
                const uint32_t completed = body ? reader->parser->tree.parents[point] + 1 : initialized < input->token_count ? initialized : point;
                definition = definition == BUSTER_CHECK_NONE && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED) ? completed : definition;
                const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
                for (uint32_t a = 0; attributes != NULL && a < attributes->count; a += 1)
                {
                    const CheckInputAttribute* attribute = attributes->records + a;
                    label = label == BUSTER_CHECK_NONE && !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL ? token : label;
                }
            }
            const uint32_t selected = previous != BUSTER_CHECK_NONE ? previous : next;
            const uint8_t kind = selected != BUSTER_CHECK_NONE ? input->declaration_kinds[selected] : C_BINDING_KINDS;
            const bool external = selected != BUSTER_CHECK_NONE && check_input_linkage(input->declaration_bits, selected) == 2;
            const bool entity = kind == C_BINDING_OBJECT || kind == C_BINDING_FUNCTION;
            if (record->kind == CHECK_SYMBOL_PRAGMA_WEAK)
            {
                if (external && entity && (definition == BUSTER_CHECK_NONE || definition > record->token))
                {
                    valid = check_input_symbol_attribute(reader, record, selected);
                }
                else if (previous == BUSTER_CHECK_NONE && kind == C_BINDING_TYPEDEF)
                {
                    record->warning = CHECK_SYMBOL_WARNING_TYPEDEF;
                    record->diagnostic_source = record->source + 3;
                    valid = check_input_symbol_warning_append(reader, i, &reader->warning_events, record->token, CHECK_WARNING_COMPLETE_AT_TOKEN, selected);
                }
                else if (previous == BUSTER_CHECK_NONE && complete)
                {
                    record->warning = CHECK_SYMBOL_WARNING_UNDECLARED;
                    record->diagnostic_source = record->source + 3;
                    valid = check_input_symbol_warning_append(reader, i, &reader->warning_events, record->token, CHECK_WARNING_COMPLETE_TU_END, BUSTER_CHECK_NONE);
                }
            }
            else if (record->kind == CHECK_SYMBOL_PRAGMA_REDEFINE && entity)
            {
                if (!external)
                {
                    record->warning = CHECK_SYMBOL_WARNING_INTERNAL;
                    record->diagnostic_source = reader->parser->tokens.sources[selected];
                    record->declaration = selected;
                    valid = check_input_symbol_warning_append(reader, i, &reader->warning_events, selected, CHECK_WARNING_COMPLETE_AT_TOKEN,
                                                               previous != BUSTER_CHECK_NONE ? record->token : selected);
                }
                else if (label == BUSTER_CHECK_NONE && (definition == BUSTER_CHECK_NONE || definition > record->token))
                {
                    // Clang's code generation can have cached a name at an earlier reference. Deciding whether a
                    // reference was emitted needs its control-flow/emission proof; keep that boundary explicit.
                    const uint32_t object = input->objects[selected];
                    const uint32_t first = check_input_symbol_lower(uses, unique, object);
                    const bool referenced = first < unique && (uint32_t)(uses[first] >> 32) == object && (uint32_t)uses[first] < record->token;
                    if (referenced)
                    {
                        const uint32_t before = input->report_count;
                        check_input_report(reader, record->token, CHECK_INPUT_PRAGMA_SEMANTICS);
                        if (input->report_count != before)
                        {
                            input->reports[before].pragma = record->pragma + 1;
                        }
                    }
                    else
                    {
                        valid = check_input_symbol_attribute(reader, record, selected);
                    }
                }
            }
        }
    }
    input->warning_events = check_warning_view(&reader->warning_events);
    input->resource |= !valid;
    arena_pop_to(reader->scratch, temporary);
}
