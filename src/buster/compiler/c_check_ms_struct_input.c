// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-time Microsoft record layout. Unlike floating settings, this pragma is translation-unit state even when
// it appears inside a function. A definition samples the optional column; forward declarations do not capture it.

typedef struct CheckInputMsPragma CheckInputMsPragma;
struct CheckInputMsPragma
{
    uint32_t pragma;
    uint32_t source;
    uint32_t token;
    uint32_t diagnostic_source;
    uint32_t diagnostic_location;
    uint8_t warning;
    uint8_t after;
    bool supported;
    bool error;
};

struct CheckInputMsPragmas
{
    CheckInputMsPragma* records;
    uint32_t count;
};

static const CheckInputMsPragma* check_input_ms_warning(const CheckInput* input, const CheckWarningEvent* event)
{
    const CheckInputMsPragmas* history = input->ms_pragmas;
    const uint32_t index = event->payload.data;
    const CheckInputMsPragma* result = event->kind == CHECK_WARNING_EVENT_PRAGMA && (event->flags & BUSTER_CHECK_WARNING_MS_PRAGMA) != 0 &&
                                      index != 0 && history != NULL && index <= history->count ? history->records + index - 1 : NULL;
    return result;
}

static const CheckInputMsPragma* check_input_ms_report(const CheckInput* input, const CheckInputReport* report)
{
    const CheckInputMsPragmas* history = input->ms_pragmas;
    const CheckInputMsPragma* result = NULL;
    for (uint32_t i = 0; result == NULL && report->pragma != 0 && history != NULL && i < history->count; i += 1)
    {
        const CheckInputMsPragma* record = history->records + i;
        result = record->pragma + 1 == report->pragma && record->error ? record : NULL;
    }
    return result;
}

static bool check_input_ms_record(CheckInputReader* reader, uint32_t pragma, uint32_t* previous_end,
                                   uint32_t* previous_token, uint64_t** macros, CheckInputMsPragma* record)
{
    uint32_t source = 0;
    uint32_t end = 0;
    uint32_t token = 0;
    const PPTokens* output = &reader->pp->output;
    const bool matched = check_input_retained_pragma(reader, pragma, previous_end, previous_token, &source, &end, &token) &&
                         pp_token_is_name(reader->pp, output, source + 2, end, "ms_struct");
    if (matched)
    {
        const bool on = pp_token_is_name(reader->pp, output, source + 3, end, "on");
        const bool off = pp_token_is_name(reader->pp, output, source + 3, end, "off") ||
                         pp_token_is_name(reader->pp, output, source + 3, end, "reset");
        const uint8_t warning = !on && !off ? 1 : end != source + 4 ? 2 : 0;
        const uint32_t at = source + (warning == 2 ? 4 : 3);
        *record = (CheckInputMsPragma){ .pragma = pragma, .source = source, .token = token,
                                       .diagnostic_source = at < end ? at : end - 1,
                                       .warning = warning, .after = on, .supported = true };
        if (warning != 0 && at == end)
        {
            const uint32_t last = end - 1;
            const bool named = check_input_pragma_name(reader, last) != 0;
            record->diagnostic_location = output->locations[last] + (named ? intern_length(reader->pp->table, output->values[last]) : 1);
        }
        for (uint32_t s = source + 3; !reader->pp->options.semantic_pragmas && record->supported && s < end; s += 1)
        {
            const IdentId name = check_input_pragma_name(reader, s);
            record->supported = name == 0 || !check_input_pragma_macro(reader, macros, name);
        }
    }
    return matched;
}

static bool check_input_ms_pragmas(CheckInputReader* reader, uint8_t* handled, bool complete_tree)
{
    const uint64_t retained = reader->arena->length;
    const uint64_t temporary = reader->scratch->length;
    const uint32_t tokens = reader->parser->tokens.count;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    uint64_t* macros = NULL;
    uint32_t count = 0;
    for (uint32_t p = 0; p < reader->parser->tokens.pragma_count; p += 1)
    {
        CheckInputMsPragma record;
        count += check_input_ms_record(reader, p, &previous_end, &previous_token, &macros, &record);
    }
    CheckInputMsPragmas* history = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputMsPragmas, 1) : NULL;
    CheckInputMsPragma* records = history != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputMsPragma, count) : NULL;
    uint8_t* column = records != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, (uint64_t)tokens + 1) : NULL;
    uint32_t* links = count != 0 && complete_tree ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)tokens + 1) : NULL;
    bool valid = !reader->output->resource && (count == 0 || (column != NULL && (!complete_tree || links != NULL)));
    if (valid && count != 0)
    {
        *history = (CheckInputMsPragmas){ .records = records };
        previous_end = 0;
        previous_token = 0;
        uint32_t cursor = 0;
        uint32_t group = BUSTER_CHECK_NONE;
        uint32_t structs = 0;
        uint32_t written = 0;
        uint8_t state = 0;
        for (uint32_t p = 0; p < reader->parser->tokens.pragma_count; p += 1)
        {
            CheckInputMsPragma record;
            if (check_input_ms_record(reader, p, &previous_end, &previous_token, &macros, &record))
            {
                while (complete_tree && cursor < record.token)
                {
                    const uint8_t role = reader->parser->tree.roles[cursor];
                    if (role < PARSE_ROLE_COUNT && parse_closer_roles[role] != PARSE_ROLE_NONE)
                    {
                        links[cursor] = group;
                        group = cursor;
                        structs += role == PARSE_ROLE_OPEN_STRUCT;
                    }
                    else if (group < tokens && reader->parser->tree.parents[group] == cursor)
                    {
                        structs -= reader->parser->tree.roles[group] == PARSE_ROLE_OPEN_STRUCT;
                        group = links[group];
                    }
                    cursor += 1;
                }
                if (complete_tree && record.supported && record.warning == 0 && record.token != 0)
                {
                    const uint8_t role = reader->parser->tree.roles[record.token - 1];
                    const uint8_t scope = group < tokens ? reader->parser->tree.roles[group] : PARSE_ROLE_NONE;
                    const bool boundary = role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_BLOCK ||
                                          role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK;
                    record.error = structs != 0;
                    record.supported &= record.error || (boundary && (group == BUSTER_CHECK_NONE || scope == PARSE_ROLE_OPEN_BODY || scope == PARSE_ROLE_OPEN_BLOCK));
                }
                memset(column + written, state, record.token - written);
                written = record.token;
                state = record.supported && !record.error && record.warning == 0 ? record.after : state;
                record.after = state;
                records[history->count++] = record;
                handled[p] = !record.supported ? 0 : record.error ? 3 : 1;
            }
        }
        memset(column + written, state, (uint64_t)tokens + 1 - written);
        reader->output->ms_pragmas = history;
        reader->output->ms_struct = column;
    }
    if (!valid)
    {
        arena_pop_to(reader->arena, retained);
        reader->output->resource = true;
    }
    arena_pop_to(reader->scratch, temporary);
    return valid;
}

static bool check_input_ms_warnings(CheckInputReader* reader)
{
    const CheckInputMsPragmas* history = reader->output->ms_pragmas;
    const CheckWarningEvents before = reader->output->pragma_warnings;
    CheckWarningBuffer buffer = { .arena = reader->arena, .events = (CheckWarningEvent*)before.events, .count = before.count, .capacity = before.count };
    bool valid = true;
    for (uint32_t i = 0; valid && history != NULL && i < history->count; i += 1)
    {
        const CheckInputMsPragma* record = history->records + i;
        if (record->supported && record->warning != 0)
        {
            const CheckWarningEvent event = {
                .id = CHECK_WARNING_ID_IGNORED_PRAGMA,
                .kind = CHECK_WARNING_EVENT_PRAGMA,
                .flags = BUSTER_CHECK_WARNING_MS_PRAGMA,
                .token = record->token,
                .source = record->diagnostic_source,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = record->token,
                .payload = { .token = record->source, .data = i + 1 },
            };
            valid = check_warning_append(&buffer, &event, NULL, 0);
        }
    }
    if (valid)
    {
        reader->output->pragma_warnings = check_warning_view(&buffer);
    }
    reader->output->resource |= !valid;
    return valid;
}
