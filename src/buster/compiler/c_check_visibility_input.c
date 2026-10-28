// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Source-ordered GCC visibility state. Retained records own syntax diagnostics and the state after each directive;
// declarations sample that immutable history. Object visibility is retained only when a pragma/attribute needs it.

typedef struct CheckInputVisibilityPragma CheckInputVisibilityPragma;
struct CheckInputVisibilityPragma
{
    String8 name;
    const char* message;
    uint32_t pragma;
    uint32_t token;
    uint32_t source;
    uint32_t diagnostic_source;
    uint32_t diagnostic_location;
    uint8_t after;
    uint8_t warning;
    bool push;
    bool pop;
    bool error;
};

struct CheckInputVisibilityPragmas
{
    CheckInputVisibilityPragma* records;
    uint32_t count;
};

// ELF's INTERNAL spelling has the same lowering as HIDDEN on Clang's x86-64 target.
static uint8_t check_input_visibility_name(String8 text)
{
    const uint8_t value = check_input_attribute_equal(text, "default") ? 1 :
                          check_input_attribute_equal(text, "hidden") || check_input_attribute_equal(text, "internal") ? 3 :
                          check_input_attribute_equal(text, "protected") ? 4 : 0;
    return value;
}

static bool check_input_visibility_record(const CheckInputReader* reader, uint32_t pragma, uint32_t* previous_end,
                                           uint32_t* previous_token, CheckInputVisibilityPragma* record)
{
    uint32_t source = 0;
    uint32_t end = 0;
    uint32_t token = 0;
    const PPTokens* output = &reader->pp->output;
    const bool matched = check_input_retained_pragma(reader, pragma, previous_end, previous_token, &source, &end, &token) && end - source >= 4 &&
                         pp_token_is_name(reader->pp, output, source + 2, end, "GCC") &&
                         pp_token_is_name(reader->pp, output, source + 3, end, "visibility");
    if (matched)
    {
        *record = (CheckInputVisibilityPragma){ .pragma = pragma, .token = token, .source = source,
                                               .diagnostic_source = source + 4, .warning = 1 };
        const bool push = pp_token_is_name(reader->pp, output, source + 4, end, "push");
        const bool pop = pp_token_is_name(reader->pp, output, source + 4, end, "pop");
        if ((!push && !pop) || source + 4 == end)
        {
            record->message = "expected identifier in '#pragma visibility' - ignored [-Wignored-pragmas]";
        }
        else if (pop)
        {
            record->pop = source + 5 == end;
            record->warning = record->pop ? 0 : 1;
            record->diagnostic_source = source + 5;
            record->message = record->pop ? NULL : "extra tokens at end of '#pragma visibility' - ignored [-Wignored-pragmas]";
        }
        else if (source + 5 >= end || output->kinds[source + 5] != C_TOKEN_L_PAREN)
        {
            record->diagnostic_source = source + 5;
            record->message = "missing '(' after '#pragma visibility' - ignoring [-Wignored-pragmas]";
        }
        else if (source + 6 >= end || !(output->kinds[source + 6] == C_TOKEN_RAW_IDENTIFIER ||
                 (output->kinds[source + 6] >= BUSTER_PP_KIND_KEYWORD && output->kinds[source + 6] < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT)))
        {
            record->diagnostic_source = source + 6;
            record->message = "expected identifier in '#pragma visibility' - ignored [-Wignored-pragmas]";
        }
        else if (source + 7 >= end || output->kinds[source + 7] != C_TOKEN_R_PAREN)
        {
            record->diagnostic_source = source + 7;
            record->message = "missing ')' after '#pragma visibility' - ignoring [-Wignored-pragmas]";
        }
        else if (source + 8 != end)
        {
            record->diagnostic_source = source + 8;
            record->message = "extra tokens at end of '#pragma visibility' - ignored [-Wignored-pragmas]";
        }
        else
        {
            const uint32_t name = output->values[source + 6];
            record->name = (String8){ .pointer = (char*)intern_bytes(reader->pp->table, name), .length = intern_length(reader->pp->table, name) };
            record->after = check_input_visibility_name(record->name);
            record->push = record->after != 0;
            record->warning = record->after == 0 ? 2 : 0;
            record->diagnostic_source = source + 3;
        }
        if (record->message != NULL && record->diagnostic_source >= end)
        {
            const uint32_t last = end - 1;
            const uint8_t kind = output->kinds[last];
            const bool identifier = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT);
            const uint32_t length = identifier ? intern_length(reader->pp->table, output->values[last]) : 1;
            record->diagnostic_location = output->locations[last] + length;
        }
    }
    return matched;
}

// Clang delivers a well-formed visibility directive to the parser as an annotation: it can stand where a
// declaration or statement begins, but cannot interrupt a declarator/expression or appear among record members.
// Malformed directives have no annotation and keep their syntax warning even in those positions.
static void check_input_visibility_placement(CheckInputReader* reader, CheckInputVisibilityPragma* record,
                                              uint32_t* links, uint32_t* cursor, uint32_t* group, uint32_t* structs)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    while (links != NULL && *cursor < record->token)
    {
        const uint32_t token = *cursor;
        const uint8_t role = parser->tree.roles[token];
        if (role < PARSE_ROLE_COUNT && parse_closer_roles[role] != PARSE_ROLE_NONE)
        {
            links[token] = *group;
            *group = token;
            *structs += role == PARSE_ROLE_OPEN_STRUCT;
        }
        else if (*group < count && parser->tree.parents[*group] == token)
        {
            *structs -= parser->tree.roles[*group] == PARSE_ROLE_OPEN_STRUCT;
            *group = links[*group];
        }
        *cursor += 1;
    }
    if (record->warning != 1 && record->token != 0 && links == NULL)
    {
        // Recovery can erase the whole failing declaration's tree, and allocation failure can leave no tree.
        // Only these token kinds prove an interrupted expression without relying on that unavailable forest.
        const uint8_t before = parser->tokens.kinds[record->token - 1];
        if (before == PARSE_KIND_EQUAL || before == PARSE_KIND_RETURN)
        {
            record->message = "expected expression";
            record->diagnostic_source = record->source + 3;
            record->diagnostic_location = 0;
            record->warning = 0;
            record->error = true;
            record->push = false;
            record->pop = false;
            reader->output->visibility_error = true;
        }
    }
    else if (record->warning != 1 && record->token != 0)
    {
        const uint32_t before = record->token - 1;
        const uint8_t role = parser->tree.roles[before];
        const uint32_t parent = parser->tree.parents[before];
        const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
        const uint8_t group_role = *group < count ? parser->tree.roles[*group] : PARSE_ROLE_NONE;
        const bool statement = *group == BUSTER_CHECK_NONE || group_role == PARSE_ROLE_OPEN_BODY || group_role == PARSE_ROLE_OPEN_BLOCK;
        const bool boundary = role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_CLOSE_BODY ||
                              role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_FOR ||
                              role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE ||
                              (role == PARSE_ROLE_CLOSE_CONDITION && parent_role != PARSE_ROLE_TERMINATOR);
        if (*structs != 0 || !statement || !boundary)
        {
            record->message = *structs != 0 ? "this pragma cannot appear in struct declaration" :
                              group_role == PARSE_ROLE_OPEN_FUNCTION ? "expected parameter declarator" :
                              role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_CLOSE_SPECIFIER || *group == BUSTER_CHECK_NONE ?
                              "expected identifier or '('" : "expected expression";
            record->diagnostic_source = record->source + 3;
            record->diagnostic_location = 0;
            record->warning = 0;
            record->error = true;
            record->push = false;
            record->pop = false;
            reader->output->visibility_error = true;
        }
    }
}

static bool check_input_visibility_pragmas(CheckInputReader* reader, uint8_t* handled, bool complete_tree)
{
    const uint64_t mark = reader->arena->length;
    const uint64_t temporary = reader->scratch->length;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    uint32_t capacity = 0;
    for (uint32_t p = 0; p < reader->parser->tokens.pragma_count; p += 1)
    {
        CheckInputVisibilityPragma record;
        capacity += check_input_visibility_record(reader, p, &previous_end, &previous_token, &record);
    }
    bool valid = true;
    if (capacity != 0)
    {
        CheckInputVisibilityPragmas* history = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputVisibilityPragmas, 1);
        CheckInputVisibilityPragma* records = history != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputVisibilityPragma, capacity) : NULL;
        uint8_t* stack = records != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, capacity) : NULL;
        uint32_t* links = stack != NULL && complete_tree ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)reader->parser->tokens.count + 1) : NULL;
        valid = stack != NULL && (!complete_tree || links != NULL);
        if (valid)
        {
            *history = (CheckInputVisibilityPragmas){ .records = records };
            previous_end = 0;
            previous_token = 0;
            uint32_t depth = 0;
            uint32_t cursor = 0;
            uint32_t group = BUSTER_CHECK_NONE;
            uint32_t structs = 0;
            uint8_t state = 0;
            for (uint32_t p = 0; p < reader->parser->tokens.pragma_count; p += 1)
            {
                CheckInputVisibilityPragma record;
                if (check_input_visibility_record(reader, p, &previous_end, &previous_token, &record))
                {
                    check_input_visibility_placement(reader, &record, links, &cursor, &group, &structs);
                    if (record.push)
                    {
                        stack[depth++] = state;
                        state = record.after;
                    }
                    else if (record.pop && depth != 0)
                    {
                        state = stack[--depth];
                    }
                    else if (record.pop)
                    {
                        record.error = true;
                        reader->output->visibility_error = true;
                        record.message = "#pragma visibility pop with no matching #pragma visibility push";
                        record.diagnostic_source = record.source + 3;
                    }
                    record.after = state;
                    records[history->count++] = record;
                }
            }
            reader->output->visibility_pragmas = history;
            for (uint32_t r = 0; r < history->count; r += 1)
            {
                handled[records[r].pragma] = 1;
            }
        }
    }
    if (!valid)
    {
        arena_pop_to(reader->arena, mark);
        reader->output->resource = true;
    }
    arena_pop_to(reader->scratch, temporary);
    return valid;
}

static void check_input_visibility_declare(CheckInputReader* reader, uint32_t token, uint32_t point)
{
    const CheckInputVisibilityPragmas* history = reader->output->visibility_pragmas;
    uint32_t low = 0;
    uint32_t high = history != NULL ? history->count : 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (history->records[middle].token <= point)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    uint8_t visibility = low != 0 ? history->records[low - 1].after : 0;
    const CheckInputAttributes* attributes = reader->output->attributes != NULL ? reader->output->attributes[token] : NULL;
    bool explicit_visibility = false;
    for (uint32_t a = 0; attributes != NULL && a < attributes->count; a += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + a;
        if (attribute->kind == CHECK_ATTRIBUTE_VISIBILITY && !attribute->ignored)
        {
            visibility = check_input_visibility_name(attribute->text);
            explicit_visibility = true;
        }
    }
    const uint32_t object = reader->objects[token];
    if (reader->linkage[token] == 2 && object != 0 && object <= reader->parser->tokens.count && visibility != 0)
    {
        if (reader->output->object_visibility == NULL)
        {
            reader->output->object_visibility = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, (uint64_t)reader->parser->tokens.count + 1);
            reader->output->resource |= reader->output->object_visibility == NULL;
            if (!reader->output->resource)
            {
                memset(reader->output->object_visibility, 0, (uint64_t)reader->parser->tokens.count + 1);
            }
        }
        if (!reader->output->resource)
        {
            const uint8_t previous = reader->output->object_visibility[object];
            if (explicit_visibility && previous != 0 && previous != visibility)
            {
                check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
            }
            else if (previous == 0)
            {
                reader->output->object_visibility[object] = visibility;
            }
        }
    }
}
