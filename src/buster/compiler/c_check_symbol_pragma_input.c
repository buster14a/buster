// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Retained weak/redefine_extname directives. The cold history owns their source coordinates; the completed binding
// stream supplies translation-unit declarations, independent of local shadows and of the bodies' reading schedule.
// This extends #1135's literal handler with source-time expansion and warning policy. Completion belongs to the final
// unit, including selected lazy-system-header units; deferred lanes borrow the history without mutating it.

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
    // TARGET's declaration captured when clang creates its clone. It also locates a clone carried by a real ALIAS
    // declaration; code-generation diagnostics must not use that declaration's own attributes or source location.
    uint32_t alias_declaration;
    // `#pragma weak A = T` whose A the unit never declares (#1849): T's declaration, whose definition the emission gives
    // a weak symbol A of its own (check_program_define_pragma_aliases), as clang's clone of T's declaration is; else
    // BUSTER_CHECK_NONE.
    uint32_t alias_target;
    // A function clone's immutable overload chain. Uses before its creation retain their ordinary heads.
    uint32_t overload_head;
    uint32_t overload_declaration;
    uint8_t kind;
    uint8_t warning;
    bool supported;
    bool after_use;
};

struct CheckInputSymbolPragmas
{
    CheckInputSymbolPragma* records;
    uint32_t count;
};

static uint32_t check_input_symbol_alias_count(const CheckInputSymbolPragmas* history)
{
    uint32_t count = 0;
    for (uint32_t i = 0; history != NULL && i < history->count; i += 1)
    {
        count += history->records[i].supported && history->records[i].kind == CHECK_SYMBOL_PRAGMA_ALIAS &&
                 history->records[i].warning == CHECK_SYMBOL_WARNING_NONE;
    }
    return count;
}

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
                                           .declaration = BUSTER_CHECK_NONE, .alias_declaration = BUSTER_CHECK_NONE, .alias_target = BUSTER_CHECK_NONE,
                                           .overload_declaration = BUSTER_CHECK_NONE,
                                           .kind = rename ? CHECK_SYMBOL_PRAGMA_REDEFINE : CHECK_SYMBOL_PRAGMA_WEAK,
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
        // An alias (#1849) declares its name as a weak alias of its target, a declaration without a C declarator; the
        // shapes the unit's own declarations of the name can carry are resolved with the bindings
        // (check_input_symbol_alias), and the others refused there.
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
        .suborder = completion_kind == CHECK_WARNING_COMPLETE_TU_END ? CHECK_WARNING_UNIT_END_WEAK : 0,
        .payload = { .name = record->kind == CHECK_SYMBOL_PRAGMA_ALIAS && record->warning == CHECK_SYMBOL_WARNING_UNDECLARED ? record->target : record->name,
                     .token = record->source, .data = index + 1 },
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
        const String8 target = record->kind != CHECK_SYMBOL_PRAGMA_WEAK ?
            (String8){ (char*)intern_bytes(reader->pp->table, record->target), intern_length(reader->pp->table, record->target) } : (String8){ 0 };
        const CheckInputAttribute attribute = {
            .first = token, .end = token + 1, .name_token = token,
            .kind = record->kind == CHECK_SYMBOL_PRAGMA_REDEFINE ? CHECK_ATTRIBUTE_ASM_LABEL : CHECK_ATTRIBUTE_WEAK,
            .text = record->kind == CHECK_SYMBOL_PRAGMA_REDEFINE ? target : (String8){ 0 },
            .resolved = true,
        };
        valid = valid && check_input_attribute_append(reader, attributes, attribute);
        // `#pragma weak A = T` (#1849): A is a weak alias of T, as clang's cloned declaration has weak and alias attributes.
        const CheckInputAttribute alias = {
            .first = token, .end = token + 1, .name_token = token, .argument_count = 1, .kind = CHECK_ATTRIBUTE_ALIAS, .text = target, .resolved = true,
        };
        valid = valid && (record->kind != CHECK_SYMBOL_PRAGMA_ALIAS || check_input_attribute_append(reader, attributes, alias));
        if (valid)
        {
            check_input_set_attributes_at(reader->output, token, attributes);
            record->declaration = token;
        }
    }
    reader->output->resource |= !valid;
    return valid;
}

// The declarations of `name` in the sorted file-scope keys (`ordered`, `count` of them): the latest at or before `token`
// and the first after it, BUSTER_CHECK_NONE for none, and whether any of them defines its entity before and after it.
typedef struct CheckInputSymbolNamed CheckInputSymbolNamed;
struct CheckInputSymbolNamed
{
    uint32_t previous;
    uint32_t next;
    bool defined_before;
    bool defined_after;
    bool aliased;
    bool sectioned;
};

static CheckInputSymbolNamed check_input_symbol_named(const CheckInputReader* reader, const uint64_t* ordered, uint32_t count, uint32_t name, uint32_t token)
{
    const CBindings* bindings = &reader->bindings;
    const CheckInput* input = reader->output;
    CheckInputSymbolNamed result = { .previous = BUSTER_CHECK_NONE, .next = BUSTER_CHECK_NONE };
    for (uint32_t i = check_input_symbol_lower(ordered, count, name); i < count && (uint32_t)(ordered[i] >> 32) == name; i += 1)
    {
        const uint32_t event = (uint32_t)ordered[i];
        const uint32_t declaration = bindings->declaration[event];
        const bool before = bindings->point[event] <= token;
        result.previous = before ? declaration : result.previous;
        result.next = !before && result.next == BUSTER_CHECK_NONE ? declaration : result.next;
        const bool defines = check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_DEFINED) ||
                             check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_TENTATIVE);
        result.defined_before |= before && defines;
        result.defined_after |= !before && defines;
        const CheckInputAttributes* attributes = check_input_attributes_at(input, declaration);
        for (uint32_t a = 0; attributes != NULL && a < attributes->count; a += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + a;
            result.aliased |= !attribute->ignored && (attribute->kind == CHECK_ATTRIBUTE_ALIAS || attribute->kind == CHECK_ATTRIBUTE_IFUNC ||
                                                      attribute->kind == CHECK_ATTRIBUTE_WEAKREF);
            result.sectioned |= !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_SECTION;
        }
    }
    return result;
}

typedef struct CheckInputSymbolClone CheckInputSymbolClone;
struct CheckInputSymbolClone
{
    uint32_t target;
    uint32_t previous;
    uint32_t position;
};

// TARGET is looked up at the directive, or at its first later external declaration. The clone enters lookup at
// that point. These snapshots are cached only after the source walk reaches it; a later use need not scan bindings.
static CheckInputSymbolClone check_input_symbol_clone(const CheckInputReader* reader, const CheckInputSymbolPragma* record, uint32_t limit)
{
    const CheckInput* input = reader->output;
    const CBindings* bindings = &reader->bindings;
    CheckInputSymbolClone clone = { .target = record->alias_declaration, .previous = record->overload_declaration, .position = record->token };
    uint32_t before = BUSTER_CHECK_NONE;
    for (uint32_t e = 0; clone.target == BUSTER_CHECK_NONE && e < bindings->count; e += 1)
    {
        if (bindings->operation[e] == C_BINDING_DECLARE && bindings->depth[e] == 0 && bindings->key[e] == record->target &&
            bindings->point[e] <= limit)
        {
            const uint32_t declaration = bindings->declaration[e];
            if (bindings->point[e] <= record->token)
            {
                before = declaration;
            }
            else if (before == BUSTER_CHECK_NONE && check_input_linkage(input->declaration_bits, declaration) == 2)
            {
                clone.target = declaration;
            }
        }
    }
    clone.target = clone.target == BUSTER_CHECK_NONE ? before : clone.target;
    clone.position = clone.target != BUSTER_CHECK_NONE && clone.target > record->token ? clone.target : record->token;
    if (clone.previous == BUSTER_CHECK_NONE && clone.target < input->token_count)
    {
        for (uint32_t e = 0; e < bindings->count; e += 1)
        {
            if (bindings->operation[e] == C_BINDING_DECLARE && bindings->depth[e] == 0 && bindings->key[e] == record->name &&
                bindings->point[e] <= clone.position)
            {
                clone.previous = bindings->declaration[e];
            }
        }
    }
    return clone;
}

static bool check_input_symbol_function_clone(const CheckInput* input, CheckInputSymbolClone clone)
{
    const bool functions = clone.target < input->token_count && clone.previous < input->token_count &&
                           input->declaration_kinds[clone.target] == C_BINDING_FUNCTION && input->declaration_kinds[clone.previous] == C_BINDING_FUNCTION &&
                           check_input_attribute_of(check_input_attributes_at(input, clone.target), CHECK_ATTRIBUTE_ALIAS) == NULL;
    return functions;
}

// Deferred lanes share the immutable top level. Only an actual function lookup set requiring selection needs the
// overload reader's source-order fallback; weak object aliases keep their ordinary deferred reading schedule.
static bool check_input_symbol_overload_needed(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const CheckInputSymbolPragmas* history = input->symbol_pragmas;
    bool needed = false;
    for (uint32_t i = 0; !needed && history != NULL && i < history->count; i += 1)
    {
        const CheckInputSymbolPragma* record = history->records + i;
        const bool eligible = record->supported && record->warning == CHECK_SYMBOL_WARNING_NONE && record->kind == CHECK_SYMBOL_PRAGMA_ALIAS;
        const CheckInputSymbolClone clone = eligible ? check_input_symbol_clone(reader, record, input->token_count) :
                                            (CheckInputSymbolClone){ .target = BUSTER_CHECK_NONE, .previous = BUSTER_CHECK_NONE };
        const TypeAnswer compatible = check_input_symbol_function_clone(input, clone) ?
            type_compatible(reader->types, input->declared_types[clone.previous], input->declared_types[clone.target]) : TYPE_ANSWER_YES;
        input->resource |= compatible == TYPE_ANSWER_FULL;
        needed = compatible == TYPE_ANSWER_NO;
    }
    return needed;
}

// A lazy overload table may be created by a sizeof/typeof constant query, whose scratch allocation is temporary.
// Keep that index in the output arena. Earlier ordinary declarations need just their original candidate; the
// lexical index continues to contain real declarations, never the unchecked pragma clones.
static uint32_t check_input_symbol_overload_original(CheckInputReader* reader, uint32_t declaration)
{
    CheckInput* input = reader->output;
    if (input->overload_heads == NULL)
    {
        (void)check_input_overload_initialize(reader, true, reader->overload_capacity, reader->overload_call_capacity, reader->arena);
    }
    uint32_t head = !input->resource ? input->overload_heads[declaration] : 0;
    if (!input->resource && head == 0)
    {
        const uint32_t at = input->graph.overload_candidate_count;
        input->resource |= at >= reader->overload_capacity;
        if (!input->resource)
        {
            const CheckInputAttributes* attributes = check_input_attributes_at(input, declaration);
            const uint32_t object = input->objects[declaration];
            reader->overload_candidates[at] = (CheckOverloadCandidate){
                .type = input->declared_types[declaration],
                .declaration = declaration,
                .canonical = object != 0 ? object - 1 : declaration,
                .object = object,
                .previous = BUSTER_CHECK_NONE,
                .scope = 0,
                .position = declaration,
                .flags = check_input_overload_flags(attributes) |
                         (check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_DEFINED) ? BUSTER_CHECK_OVERLOAD_DEFINED : 0),
                .library_call = check_input_library_declaration(reader, declaration),
                .object_sizes = attributes != NULL ? attributes->object_sizes : NULL,
                .object_size_count = attributes != NULL ? attributes->object_size_count : 0,
            };
            reader->overload_index.lexical_next[at] = BUSTER_CHECK_NONE;
            reader->overload_index.linked_next[at] = BUSTER_CHECK_NONE;
            reader->overload_index.checked[at] = false;
            reader->overload_index.unmarked[at] = reader->overload_candidates[at].canonical;
            input->graph.overload_candidate_count += 1;
            head = at + 1;
            input->overload_heads[declaration] = head;
        }
    }
    return head;
}

// Clang inserts a function clone without checking the previous function declaration. Different signatures can
// consequently form an overload set without overloadable attributes. Capture its head while the source scope is
// live, so sizeof/typeof and initializer constants see the same selection as the final graph. Independent clones
// retain distinct identities and form one immutable chain; all name the ordinary ELF symbol A. A block declaration
// shadows the translation-unit lookup set even if it shares that symbol's linkage identity.
static void check_input_symbol_overload_use(CheckInputReader* reader, uint32_t token, uint32_t visible)
{
    CheckInput* input = reader->output;
    CheckInputSymbolPragmas* history = input->symbol_pragmas;
    const uint32_t name = reader->parser->tokens.names[token];
    const bool global = visible < input->token_count && input->declaration_kinds[visible] == C_BINDING_FUNCTION &&
                        check_sparse_get(input->declaration_bodies, visible) == BUSTER_CHECK_NONE;
    bool active = false;
    for (uint32_t i = 0; global && history != NULL && i < history->count && !input->resource; i += 1)
    {
        CheckInputSymbolPragma* record = history->records + i;
        if (record->supported && record->warning == CHECK_SYMBOL_WARNING_NONE && record->kind == CHECK_SYMBOL_PRAGMA_ALIAS &&
            record->name == name && record->token <= token)
        {
            const CheckInputSymbolClone clone = check_input_symbol_clone(reader, record, token);
            if (clone.position < token && check_input_symbol_function_clone(input, clone) && input->objects[visible] == input->objects[clone.previous])
            {
                record->alias_declaration = clone.target;
                record->overload_declaration = clone.previous;
                const TypeAnswer compatible = record->overload_head != 0 ? TYPE_ANSWER_NO :
                    type_compatible(reader->types, input->declared_types[clone.previous], input->declared_types[clone.target]);
                input->resource |= compatible == TYPE_ANSWER_FULL;
                active |= compatible == TYPE_ANSWER_NO;
            }
        }
    }
    bool remaining = active;
    while (remaining && !input->resource)
    {
        uint32_t selected = BUSTER_CHECK_NONE;
        uint32_t position = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; i < history->count; i += 1)
        {
            const CheckInputSymbolPragma* record = history->records + i;
            const uint32_t clone = record->alias_declaration > record->token ? record->alias_declaration : record->token;
            if (record->name == name && record->overload_head == 0 && record->overload_declaration < input->token_count &&
                input->objects[record->overload_declaration] == input->objects[visible] && clone < token && clone < position)
            {
                selected = i;
                position = clone;
            }
        }
        remaining = selected != BUSTER_CHECK_NONE;
        if (remaining)
        {
            CheckInputSymbolPragma* record = history->records + selected;
            const uint32_t target = record->alias_declaration;
            uint32_t head = check_input_symbol_overload_original(reader, record->overload_declaration);
            for (uint32_t i = 0; !input->resource && i < history->count; i += 1)
            {
                const CheckInputSymbolPragma* prior = history->records + i;
                if (prior->name == name && prior->overload_head != 0)
                {
                    const CheckOverloadCandidate* candidate = reader->overload_candidates + prior->overload_head - 1;
                    if (candidate->object == input->objects[visible] && candidate->position <= position)
                    {
                        head = prior->overload_head > head ? prior->overload_head : head;
                        // Identical pending WeakInfo entries are deduplicated before a later TARGET appears.
                        if (prior->target == record->target && target > prior->token && target > record->token)
                        {
                            record->overload_head = prior->overload_head;
                        }
                    }
                }
            }
            if (!input->resource && record->overload_head == 0)
            {
                const uint32_t at = input->graph.overload_candidate_count;
                input->resource |= head == 0 || at >= reader->overload_capacity || selected >= UINT32_MAX - input->token_count;
                if (!input->resource)
                {
                    reader->overload_candidates[at] = (CheckOverloadCandidate){
                        .type = input->declared_types[target],
                        .declaration = target,
                        .canonical = input->token_count + selected,
                        .object = input->objects[visible],
                        .previous = head - 1,
                        .scope = 0,
                        .position = position,
                        .flags = BUSTER_CHECK_OVERLOAD_WEAK_PRAGMA,
                    };
                    input->graph.overload_candidate_count += 1;
                    record->overload_head = at + 1;
                }
            }
        }
    }
    uint32_t head = 0;
    for (uint32_t i = 0; active && !input->resource && i < history->count; i += 1)
    {
        const CheckInputSymbolPragma* record = history->records + i;
        if (record->name == name && record->overload_head > head)
        {
            const CheckOverloadCandidate* candidate = reader->overload_candidates + record->overload_head - 1;
            head = candidate->object == input->objects[visible] && candidate->position < token ? record->overload_head : head;
        }
    }
    if (head != 0 && !input->resource)
    {
        input->overload_heads[token] = head;
    }
}

// A clone has no declarator token of its own. Its candidate uses TARGET's type, but diagnostics use the pragma's
// ALIAS spelling and its fake, unnamed parameters, never TARGET's parameter names or declaration location.
static uint32_t check_input_symbol_overload_source(const CheckInput* input, const CheckOverloadCandidate* candidate)
{
    const CheckInputSymbolPragmas* history = input->symbol_pragmas;
    uint32_t source = BUSTER_CHECK_NONE;
    const bool clone = (candidate->flags & BUSTER_CHECK_OVERLOAD_WEAK_PRAGMA) != 0;
    for (uint32_t i = 0; clone && history != NULL && source == BUSTER_CHECK_NONE && i < history->count; i += 1)
    {
        const CheckInputSymbolPragma* record = history->records + i;
        const uint32_t at = record->overload_head - 1;
        if (at < input->graph.overload_candidate_count && input->graph.overload_candidates + at == candidate)
        {
            source = record->source + 3;
        }
    }
    return source;
}

// A later declaration is checked against the pragma's clone, which the ordinary bindings do not contain. Replace
// only its ordinary declaration-conflict reports; unrelated attributes, its initializer and following declarations
// keep their own recovery. No producer retains indices into recovered_reports after the source walk has completed.
static void check_input_symbol_redeclaration(CheckInputReader* reader, const CheckInputSymbolPragma* record, uint32_t token,
                                             uint32_t previous, CheckRedeclarationKind detail, bool clone_note)
{
    CheckInput* input = reader->output;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < input->recovered_count; i += 1)
    {
        const CheckInputReport report = input->recovered_reports[i];
        const bool replaced = report.token == token && (report.message == CHECK_INPUT_REDECLARATION || report.message == CHECK_INPUT_CONFLICTING_TYPES);
        if (!replaced)
        {
            input->recovered_reports[kept++] = report;
        }
    }
    input->recovered_count = kept;
    const uint32_t before = input->report_count;
    check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
    if (input->report_count > before && !input->resource)
    {
        CheckInputReport* report = input->reports + before;
        report->subject = previous + 1;
        report->detail = detail;
        report->type = input->declared_types[token];
        report->type_spelling = check_input_typedef_spelling(input, reader->parser, token);
        report->value = input->declared_types[previous];
        report->note_token = previous;
        const bool definition = clone_note || detail == CHECK_REDECLARATION_KIND || detail == CHECK_REDECLARATION_DEFINITION ||
                                input->declaration_kinds[previous] == C_BINDING_OBJECT;
        report->note_kind = clone_note && input->declaration_kinds[previous] == C_BINDING_FUNCTION ?
                            (definition ? CHECK_INPUT_NOTE_PRAGMA_DEFINITION : CHECK_INPUT_NOTE_PRAGMA_DECLARATION) :
                            definition ? CHECK_INPUT_NOTE_PREVIOUS_DEFINITION : CHECK_INPUT_NOTE_PREVIOUS_DECLARATION;
        report->attribute = clone_note ? record->source + 3 : 0;
        (void)check_input_recover_reports(reader, before);
    }
}

// `#pragma weak A = T` (#1849): clang's Sema looks T up at the pragma (or at its first later external declaration) and
// declares A a weak alias of it, a clone of T's declaration, unless T already has an alias attribute; "weak identifier
// 'T' never declared" at A otherwise. The shapes whose A the unit declares, with external linkage, of T's kind and type,
// that nothing names after the pragma where A is declared before it, take the alias as attributes of A's declaration
// (the latest before the pragma, or else the first after it): weak and alias("T"), which the emission makes clang's weak
// alias of. One whose A the unit defined before it, or made an alias of its own, is clang's no-op. Where clang's clone
// is A's only declaration the emission makes, the record's alias_target is T's declaration. The others, at which clang's
// Sema errors, receive the supported ambiguity and object redeclaration diagnostics or are refused by name.
static bool check_input_symbol_alias(CheckInputReader* reader, CheckInputSymbolPragma* record, uint32_t index, const uint64_t* ordered, uint32_t count,
                                     bool complete)
{
    CheckInput* input = reader->output;
    const CBindings* bindings = &reader->bindings;
    const CheckInputSymbolNamed target = check_input_symbol_named(reader, ordered, count, record->target, record->token);
    const uint32_t later = target.next != BUSTER_CHECK_NONE && check_input_linkage(input->declaration_bits, target.next) == 2 ? target.next : BUSTER_CHECK_NONE;
    const uint32_t selected = target.previous != BUSTER_CHECK_NONE ? target.previous : later;
    const uint8_t kind = selected != BUSTER_CHECK_NONE ? input->declaration_kinds[selected] : C_BINDING_KINDS;
    bool valid = true;
    bool refused = false;
    if (selected == BUSTER_CHECK_NONE || (kind != C_BINDING_OBJECT && kind != C_BINDING_FUNCTION))
    {
        if (complete)
        {
            record->warning = CHECK_SYMBOL_WARNING_UNDECLARED;
            record->diagnostic_source = record->source + 3;
            valid = check_input_symbol_warning_append(reader, index, &reader->warning_events, record->token, CHECK_WARNING_COMPLETE_TU_END, BUSTER_CHECK_NONE);
        }
    }
    else if (check_input_attribute_of(check_input_attributes_at(input, selected), CHECK_ATTRIBUTE_ALIAS) == NULL)
    {
        record->alias_declaration = selected;
        const uint32_t clone = selected > record->token ? selected : record->token;
        const CheckInputSymbolNamed alias = check_input_symbol_named(reader, ordered, count, record->name, clone);
        const uint32_t declaration = alias.previous != BUSTER_CHECK_NONE ? alias.previous : alias.next;
        // Whether C names A after the pragma, and anywhere at all.
        bool named_after = false;
        bool named = false;
        for (uint32_t event = 0; declaration != BUSTER_CHECK_NONE && event < bindings->count; event += 1)
        {
            const bool use = bindings->operation[event] == C_BINDING_USE && bindings->key[event] == record->name;
            const uint32_t token = bindings->declaration[event];
            const uint32_t resolved = token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
            const bool same_object = use && resolved < input->token_count && input->objects[resolved] == input->objects[declaration] &&
                                     check_sparse_get(input->declaration_bodies, resolved) == BUSTER_CHECK_NONE;
            named |= same_object;
            named_after |= use && alias.previous != BUSTER_CHECK_NONE && resolved < input->token_count &&
                           input->objects[resolved] == input->objects[alias.previous] && bindings->point[event] > clone;
        }
        const TypeId declared = declaration != BUSTER_CHECK_NONE ? input->declared_types[declaration] : 0;
        const TypeAnswer same = declared != 0 ? type_compatible(reader->types, declared, input->declared_types[selected]) : TYPE_ANSWER_NO;
        reader->output->resource |= same == TYPE_ANSWER_FULL;
        // A defined before the pragma is clang's no-op, and so is one that is an alias already: clang keeps its own alias.
        const bool no_op = alias.previous != BUSTER_CHECK_NONE && (alias.defined_before || alias.aliased);
        const bool mismatched = declaration != BUSTER_CHECK_NONE &&
                                (alias.sectioned || input->declaration_kinds[declaration] != kind || same != TYPE_ANSWER_YES ||
                                 check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL) !=
                                     check_input_bit(input->declaration_bits, selected, BUSTER_CHECK_BITS_THREAD_LOCAL));
        // The clone appears when the target becomes visible. References to an earlier function declaration are then
        // ambiguous; a local shadow still resolves to its own declaration. A later variable definition conflicts with
        // the clone. Keep these diagnostics recoverable so subsequent declarations remain visible.
        bool diagnosed = false;
        const uint8_t alias_kind = alias.previous != BUSTER_CHECK_NONE ? input->declaration_kinds[alias.previous] : C_BINDING_KINDS;
        const bool functions = kind == C_BINDING_FUNCTION && alias_kind == C_BINDING_FUNCTION;
        // An object in the lookup set makes every reference ambiguous, even when its kind or type differs from the
        // clone's. Two functions with different parameter types can instead form a valid overload set in clang; those
        // still need overload selection and keep the existing refusal.
        const bool overloaded = record->overload_head != 0;
        const bool ambiguous = !overloaded && (!mismatched || (!functions && (alias_kind == C_BINDING_FUNCTION || alias_kind == C_BINDING_OBJECT)));
        if (ambiguous && alias.previous != BUSTER_CHECK_NONE && (kind == C_BINDING_FUNCTION || kind == C_BINDING_OBJECT))
        {
            for (uint32_t event = 0; event < bindings->count; event += 1)
            {
                const uint32_t token = bindings->declaration[event];
                const uint32_t resolved = token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
                if (bindings->operation[event] == C_BINDING_USE && bindings->key[event] == record->name && token < input->token_count &&
                    bindings->point[event] > clone && resolved < input->token_count && input->objects[resolved] == input->objects[alias.previous] &&
                    check_sparse_get(input->declaration_bodies, resolved) == BUSTER_CHECK_NONE)
                {
                    const uint32_t before = input->report_count;
                    check_input_report(reader, token, functions ? CHECK_INPUT_WEAK_ALIAS_AMBIGUOUS : CHECK_INPUT_WEAK_ALIAS_OBJECT_AMBIGUOUS);
                    if (input->report_count > before)
                    {
                        input->reports[before].note_token = functions ? alias.previous : selected;
                        input->reports[before].value = functions ? record->source + 3 : alias.previous;
                        // A function clone's location is the pragma's name, not the target function's declaration.
                        input->reports[before].detail = !functions && kind == C_BINDING_FUNCTION ? record->source + 4 : 0;
                        diagnosed = check_input_recover_reports(reader, before) || diagnosed;
                    }
                }
            }
        }
        uint32_t conflicts = 0;
        for (uint32_t token = clone; token < input->token_count; token += 1)
        {
            const uint8_t declared_kind = input->declaration_kinds[token];
            const bool declaring = reader->parser->tokens.names[token] == record->name && input->declarations[token] == token &&
                                   input->declared_types[token] != 0 &&
                                   (declared_kind == C_BINDING_OBJECT || declared_kind == C_BINDING_FUNCTION || declared_kind == C_BINDING_TYPEDEF) &&
                                   (declared_kind == C_BINDING_TYPEDEF || check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC)) &&
                                   (check_sparse_get(input->declaration_bodies, token) == BUSTER_CHECK_NONE ||
                                    check_input_linkage(input->declaration_bits, token) == 2);
            if (declaring)
            {
                const TypeAnswer compatible = declared_kind == kind ? type_compatible(reader->types, input->declared_types[token], input->declared_types[selected]) : TYPE_ANSWER_NO;
                input->resource |= compatible == TYPE_ANSWER_FULL;
                const uint32_t linkage = check_input_linkage(input->declaration_bits, token);
                // Function clones have no storage-class specifier. Variable clones keep TARGET's storage class,
                // but Clang's DeclClonePragmaWeak does not copy its thread-local storage-class specifier.
                const uint32_t clone_linkage = kind == C_BINDING_FUNCTION ? 2u : check_input_linkage(input->declaration_bits, selected);
                const bool prior_set = alias.previous != BUSTER_CHECK_NONE;
                const bool object_set = prior_set && (kind == C_BINDING_OBJECT || declared_kind == C_BINDING_OBJECT || alias_kind == C_BINDING_OBJECT);
                const bool definition = check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED);
                CheckRedeclarationKind detail = declared_kind != kind || object_set ? CHECK_REDECLARATION_KIND :
                    compatible == TYPE_ANSWER_NO ? (kind == C_BINDING_FUNCTION ? CHECK_REDECLARATION_FUNCTION_TYPE :
                                                   check_input_redeclaration_extern(reader->parser, token) ? CHECK_REDECLARATION_OBJECT_TYPE_EXTERN : CHECK_REDECLARATION_OBJECT_TYPE) :
                    linkage == 1 && clone_linkage == 2 ? CHECK_REDECLARATION_STATIC :
                    linkage == 2 && clone_linkage == 1 ? CHECK_REDECLARATION_NON_STATIC :
                    kind == C_BINDING_OBJECT && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_THREAD_LOCAL) ? CHECK_REDECLARATION_THREAD_LOCAL :
                    definition ? CHECK_REDECLARATION_DEFINITION : CHECK_REDECLARATION_GENERIC;
                // A later function declaration can merge one signature of an unchecked function lookup set. Its
                // ordinary redeclaration check already owns conflicts with that real declaration.
                if (prior_set && !object_set && declared_kind == C_BINDING_FUNCTION && kind == C_BINDING_FUNCTION && !definition)
                {
                    detail = CHECK_REDECLARATION_GENERIC;
                }
                if (detail != CHECK_REDECLARATION_GENERIC)
                {
                    const uint32_t previous = object_set && conflicts != 0 ? alias.previous : selected;
                    check_input_symbol_redeclaration(reader, record, token, previous, detail, previous == selected);
                    if (detail == CHECK_REDECLARATION_STATIC && kind == C_BINDING_OBJECT)
                    {
                        const uint32_t before = input->report_count;
                        check_input_report(reader, token, CHECK_INPUT_WEAK_LINKAGE);
                        if (input->report_count > before)
                        {
                            input->reports[before].pragma = record->pragma + 1;
                            input->reports[before].attribute = record->source + 3;
                            input->reports[before].repeated = true;
                            (void)check_input_recover_reports(reader, before);
                        }
                    }
                    diagnosed = true;
                    conflicts += 1;
                }
            }
        }
        // An A the unit never declares, or declares only before the pragma, of external linkage, never defines and never
        // names, of a T it defines (#1849): clang's clone of T's declaration, a weak alias at T's definition of T's kind,
        // type and size, whatever A's own declaration says (its kind, type, thread-local storage or section), which no C
        // reference names.
        const bool unnamed = declaration == BUSTER_CHECK_NONE ||
                             (alias.next == BUSTER_CHECK_NONE && !named && !alias.defined_before && !alias.aliased &&
                              (mismatched || check_input_linkage(input->declaration_bits, declaration) != 2));
        const bool undeclared = !no_op && unnamed;
        record->alias_target = undeclared ? selected : BUSTER_CHECK_NONE;
        // Refused by name, where clang's Sema errors: A declared before the pragma and named after it ("reference to 'A' is
        // ambiguous"), declared after it with another kind or type ("redefinition of 'A' as different kind of symbol",
        // "redeclaration of 'A' with a different type") or defined after it ("redefinition of 'A'"), and T not defined
        // ("alias must point to a defined variable or function"); and A of internal linkage.
        refused = !diagnosed && !no_op && !undeclared && !overloaded && (declaration == BUSTER_CHECK_NONE || named_after || alias.aliased ||
                                            (mismatched && alias.previous != BUSTER_CHECK_NONE) || check_input_linkage(input->declaration_bits, declaration) != 2);
        if (!no_op && !refused && !undeclared && !diagnosed)
        {
            valid = check_input_symbol_attribute(reader, record, declaration);
        }
    }
    if (refused)
    {
        const uint32_t before = input->report_count;
        check_input_report(reader, record->token, CHECK_INPUT_PRAGMA_SEMANTICS);
        if (input->report_count != before)
        {
            input->reports[before].pragma = record->pragma + 1;
        }
    }
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
    const bool present = history != NULL && history->count != 0;
    const uint8_t* const deferred = input->deferred_tokens;
    const uint32_t names = reader->pp->table->count;
    bool renames = false;
    for (uint32_t i = 0; present && !renames && i < history->count; i += 1)
    {
        renames = history->records[i].supported && history->records[i].kind == CHECK_SYMBOL_PRAGMA_REDEFINE;
    }
    uint64_t* keys = present ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint64_t, (uint64_t)bindings->count * (renames ? 3 : 2) + 1) : NULL;
    uint64_t* uses = keys != NULL && renames ? keys + (uint64_t)bindings->count * 2 : NULL;
    uint32_t* hidden = present && deferred != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, names) : NULL;
    bool valid = !present || (keys != NULL && (deferred == NULL || hidden != NULL));
    uint32_t count = 0;
    uint32_t use_count = 0;
    const bool scalar = BUSTER_SCALAR_PATH(reader->parser->options.scalar);
    if (valid && hidden != NULL)
    {
        memset(hidden, 0xff, (uint64_t)names * sizeof(uint32_t));
        const uint32_t tokens = input->token_count;
        for (uint32_t first = 0; first < tokens; first += 64)
        {
            const uint64_t lanes = scalar ? check_input_scan_all(first, tokens) : check_input_scan_nonzero(deferred, first, tokens);
            for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
                const IdentId name = reader->parser->tokens.names[token];
                if (deferred[token] != 0 && name != 0 && name < names && hidden[name] == BUSTER_CHECK_NONE)
                {
                    hidden[name] = token;
                }
            }
        }
    }
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
        const bool eligible = record->supported && record->warning == CHECK_SYMBOL_WARNING_NONE && record->token <= reader->expression_limit;
        const bool hidden_before = hidden != NULL && record->name < names && hidden[record->name] < record->token;
        if (eligible && hidden_before)
        {
            // A selected-out body has no binding proof. Retain the upstream refusal only for this name; an
            // unrelated skipped body must not suppress every directive in an otherwise complete selected unit.
            const uint32_t before = input->report_count;
            check_input_report(reader, record->token, CHECK_INPUT_PRAGMA_SEMANTICS);
            if (input->report_count != before)
            {
                input->reports[before].pragma = record->pragma + 1;
            }
        }
        if (eligible && !hidden_before)
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
            else if (record->kind == CHECK_SYMBOL_PRAGMA_ALIAS)
            {
                valid = check_input_symbol_alias(reader, record, i, ordered, count, complete);
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
                    // The declaration keeps its asm-label fact even when code generation already cached its old
                    // spelling. The emission selection resolves that distinction from live references and the
                    // source completion of eagerly emitted bodies, rather than treating every parsed use as emitted.
                    const uint32_t object = input->objects[selected];
                    const uint32_t first = check_input_symbol_lower(uses, unique, object);
                    record->after_use = first < unique && (uint32_t)(uses[first] >> 32) == object && (uint32_t)uses[first] < record->token;
                    valid = check_input_symbol_attribute(reader, record, selected);
                }
            }
        }
    }
    input->warning_events = check_warning_view(&reader->warning_events);
    input->resource |= !valid;
    arena_pop_to(reader->scratch, temporary);
}
