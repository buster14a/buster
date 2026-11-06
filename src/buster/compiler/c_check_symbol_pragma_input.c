// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Literal file-scope weak/redefine_extname directives (#1135). Preparation retains immutable source coordinates;
// completion joins them to canonical objects after binding/deferred-body replay. Only the existing weak/asm-label
// metadata reaches emission. Unsupported expansion, alias, placement and late-use cases stay named hard errors.

typedef struct CheckInputSymbolPragma CheckInputSymbolPragma;
struct CheckInputSymbolPragma
{
    uint32_t pragma;
    uint32_t token;
    IdentId name;
    IdentId replacement;
};

struct CheckInputSymbolPragmas
{
    CheckInputSymbolPragma* records;
    uint32_t count;
};

static bool check_input_symbol_pragma_record(const CheckInputReader* reader, uint32_t pragma, uint32_t* previous_end, uint32_t* previous_token, CheckInputSymbolPragma* record)
{
    uint32_t source = 0;
    uint32_t end = 0;
    uint32_t token = 0;
    const bool retained = check_input_retained_pragma(reader, pragma, previous_end, previous_token, &source, &end, &token);
    const PPTokens* output = &reader->pp->output;
    const bool weak = retained && end - source == 4 && pp_token_is_name(reader->pp, output, source + 2, end, "weak");
    const bool rename = retained && end - source == 5 && pp_token_is_name(reader->pp, output, source + 2, end, "redefine_extname");
    const IdentId name = weak || rename ? check_input_pragma_name(reader, source + 3) : 0;
    const IdentId replacement = rename ? check_input_pragma_name(reader, source + 4) : 0;
    const bool valid = name != 0 && (weak || replacement != 0);
    if (valid)
    {
        *record = (CheckInputSymbolPragma){ .pragma = pragma, .token = token, .name = name, .replacement = replacement };
    }
    return valid;
}

static bool check_input_symbol_pragmas(CheckInputReader* reader, uint8_t* handled)
{
    const Parser* parser = reader->parser;
    const uint32_t pragma_count = parser->tokens.pragma_count;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    uint32_t capacity = 0;
    for (uint32_t pragma = 0; pragma < pragma_count; pragma += 1)
    {
        CheckInputSymbolPragma record = { 0 };
        capacity += check_input_symbol_pragma_record(reader, pragma, &previous_end, &previous_token, &record);
    }
    const uint64_t mark = reader->arena->length;
    const uint64_t scratch_mark = reader->scratch->length;
    bool valid = true;
    if (capacity != 0)
    {
        CheckInputSymbolPragmas* history = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputSymbolPragmas, 1);
        CheckInputSymbolPragma* records = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputSymbolPragma, capacity);
        const uint32_t count = parser->tokens.count;
        uint32_t* links = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
        valid = history != NULL && records != NULL && links != NULL;
        uint64_t* macro_names = NULL;
        uint32_t used = 0;
        uint32_t cursor = 0;
        uint32_t group = BUSTER_CHECK_NONE;
        previous_end = 0;
        previous_token = 0;
        for (uint32_t pragma = 0; valid && pragma < pragma_count; pragma += 1)
        {
            CheckInputSymbolPragma record = { 0 };
            if (check_input_symbol_pragma_record(reader, pragma, &previous_end, &previous_token, &record))
            {
                while (cursor < record.token)
                {
                    const uint8_t role = parser->tree.roles[cursor];
                    if (role < PARSE_ROLE_COUNT && parse_closer_roles[role] != PARSE_ROLE_NONE)
                    {
                        links[cursor] = group;
                        group = cursor;
                    }
                    else if (group < count && parser->tree.parents[group] == cursor)
                    {
                        group = links[group];
                    }
                    cursor += 1;
                }
                const uint8_t before = record.token != 0 ? parser->tree.roles[record.token - 1] : PARSE_ROLE_NONE;
                const bool boundary = group == BUSTER_CHECK_NONE && (record.token == 0 || before == PARSE_ROLE_TERMINATOR || before == PARSE_ROLE_CLOSE_BODY);
                // Clang expands these operands. The retained stream does not: never attach an unexpanded spelling
                // to the wrong declaration. The common macro-history helper also conservatively refuses #undef.
                const bool macro = check_input_pragma_macro(reader, &macro_names, record.name) ||
                                   (record.replacement != 0 && check_input_pragma_macro(reader, &macro_names, record.replacement));
                valid = !reader->output->resource;
                if (valid && boundary && !macro)
                {
                    records[used++] = record;
                }
            }
        }
        if (valid)
        {
            *history = (CheckInputSymbolPragmas){ .records = records, .count = used };
            reader->output->symbol_pragmas = history;
            for (uint32_t i = 0; i < used; i += 1)
            {
                handled[records[i].pragma] = 2;
            }
        }
    }
    if (!valid)
    {
        arena_pop_to(reader->arena, mark);
    }
    arena_pop_to(reader->scratch, scratch_mark);
    return valid;
}

typedef struct CheckInputSymbolPragmaObject CheckInputSymbolPragmaObject;
struct CheckInputSymbolPragmaObject
{
    uint32_t declaration;
    uint32_t definition;
    uint32_t use;
    const CheckInputAttribute* label;
    IdentId replacement;
};

static void check_input_symbol_pragma_refuse(CheckInputReader* reader, const CheckInputSymbolPragma* record)
{
    const uint32_t before = reader->output->report_count;
    check_input_report(reader, record->token, CHECK_INPUT_PRAGMA_SEMANTICS);
    if (reader->output->report_count != before)
    {
        reader->output->reports[before].pragma = record->pragma + 1;
    }
}

static void check_input_symbol_pragma_attribute(CheckInputReader* reader, uint32_t token, IdentId replacement)
{
    const CheckInputAttributeKind kind = replacement != 0 ? CHECK_ATTRIBUTE_ASM_LABEL : CHECK_ATTRIBUTE_WEAK;
    const CheckInputAttributes* previous = check_input_attributes_at(reader->output, token);
    if (check_input_attribute_of(previous, kind) == NULL)
    {
        CheckInputAttributes* attributes = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAttributes, 1);
        bool valid = attributes != NULL;
        if (valid)
        {
            *attributes = (CheckInputAttributes){ 0 };
            valid = previous == NULL || check_input_attribute_copy(reader, previous, attributes);
        }
        const String8 text = replacement != 0 ? (String8){ .pointer = (char*)intern_bytes(reader->pp->table, replacement), .length = intern_length(reader->pp->table, replacement) } : (String8){ 0 };
        const CheckInputAttribute attribute =
        {
            .first = token,
            .end = token + 1,
            .name_token = token,
            .kind = kind,
            .schema = replacement != 0 ? CHECK_ATTRIBUTE_STRING : CHECK_ATTRIBUTE_NO_ARGUMENTS,
            .argument_count = replacement != 0,
            .text = text,
            .resolved = true,
        };
        valid = valid && check_input_attribute_append(reader, attributes, attribute);
        if (valid)
        {
            check_input_set_attributes_at(reader->output, token, attributes);
        }
        reader->output->resource |= !valid;
    }
}

// One join, not one scan per directive: the linked-name table identifies each canonical object, a scratch map
// coalesces repeated directives, and one token pass checks its uses and existing asm labels. No work or storage
// is added to translation units without these pragmas. Completion never mutates a deferred lane's borrowed state.
static void check_input_symbol_pragmas_complete(CheckInputReader* reader)
{
    const CheckInputSymbolPragmas* history = reader->output->symbol_pragmas;
    const uint32_t count = history != NULL ? history->count : 0;
    const uint64_t mark = reader->scratch->length;
    if (count != 0)
    {
        const uint32_t tokens = reader->parser->tokens.count;
        const uint32_t names = reader->pp->table->count;
        uint32_t* map = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)tokens + 1);
        CheckInputSymbolPragmaObject* objects = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckInputSymbolPragmaObject, count);
        uint32_t* deferred_first = reader->output->deferred_tokens != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, names) : NULL;
        reader->output->resource |= map == NULL || objects == NULL || (reader->output->deferred_tokens != NULL && deferred_first == NULL);
        uint32_t used = 0;
        if (!reader->output->resource)
        {
            memset(map, 0, ((uint64_t)tokens + 1) * sizeof(uint32_t));
            if (deferred_first != NULL)
            {
                memset(deferred_first, 0xff, (uint64_t)names * sizeof(uint32_t));
            }
        }
        for (uint32_t i = 0; !reader->output->resource && i < count; i += 1)
        {
            const CheckInputSymbolPragma* record = history->records + i;
            const uint32_t linked = reader->linked_declarations[record->name];
            if (linked != 0 && check_input_linkage(reader->declaration_bits, linked - 1) == 2)
            {
                const uint32_t object = reader->objects[linked - 1];
                if (object != 0 && object <= tokens && map[object] == 0)
                {
                    objects[used] = (CheckInputSymbolPragmaObject){ .declaration = linked - 1, .definition = reader->definition_origins[linked - 1], .use = tokens };
                    map[object] = ++used;
                }
            }
        }
        for (uint32_t token = 0; !reader->output->resource && token < tokens; token += 1)
        {
            if (deferred_first != NULL && reader->output->deferred_tokens[token] != 0)
            {
                const IdentId name = reader->parser->tokens.names[token];
                if (name != 0 && name < names && deferred_first[name] == BUSTER_CHECK_NONE)
                {
                    deferred_first[name] = token;
                }
                continue;
            }
            const uint32_t declaration = reader->output->declarations[token];
            const uint32_t object = declaration < tokens ? reader->objects[declaration] : 0;
            const uint32_t index = object <= tokens ? map[object] : 0;
            if (index != 0)
            {
                CheckInputSymbolPragmaObject* entry = objects + index - 1;
                if (reader->output->declarations[token] == token)
                {
                    const CheckInputAttribute* label = check_input_attribute_of(check_input_attributes_at(reader->output, token), CHECK_ATTRIBUTE_ASM_LABEL);
                    entry->label = entry->label != NULL ? entry->label : label;
                }
                else
                {
                    entry->use = token < entry->use ? token : entry->use;
                }
            }
        }
        for (uint32_t i = 0; !reader->output->resource && i < count; i += 1)
        {
            const CheckInputSymbolPragma* record = history->records + i;
            const uint32_t linked = reader->linked_declarations[record->name];
            const uint32_t object = linked != 0 ? reader->objects[linked - 1] : 0;
            const uint32_t index = object <= tokens ? map[object] : 0;
            const bool hidden_before = deferred_first != NULL && record->name < names && deferred_first[record->name] < record->token;
            if (hidden_before)
            {
                // A skipped body may contain an earlier use or declaration. Refuse only the affected name;
                // unrelated spellings cannot change this pragma's ordering or binding.
                check_input_symbol_pragma_refuse(reader, record);
            }
            else if (index == 0)
            {
                // An unused external-name mapping is a no-op, unlike an unmatched weak directive (Clang warns).
                if (record->replacement == 0 || linked != 0)
                {
                    check_input_symbol_pragma_refuse(reader, record);
                }
            }
            else
            {
                CheckInputSymbolPragmaObject* entry = objects + index - 1;
                const bool late = (entry->definition != 0 && entry->definition - 1 < record->token) || entry->use < record->token;
                const String8 text = record->replacement != 0 ? (String8){ .pointer = (char*)intern_bytes(reader->pp->table, record->replacement), .length = intern_length(reader->pp->table, record->replacement) } : (String8){ 0 };
                const bool conflict = record->replacement != 0 && entry->label != NULL &&
                                      (entry->label->text.length != text.length || memcmp(entry->label->text.pointer, text.pointer, text.length) != 0);
                if (late || conflict)
                {
                    // Clang's emission timing affects late changes. Until that timing and its warnings are
                    // represented, a hard refusal is safer than retroactively changing a symbol already used.
                    check_input_symbol_pragma_refuse(reader, record);
                }
                else if (record->replacement == 0 || entry->replacement == 0)
                {
                    check_input_symbol_pragma_attribute(reader, entry->declaration, record->replacement);
                    entry->replacement = record->replacement != 0 ? record->replacement : entry->replacement;
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
}
