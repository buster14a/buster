// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-ordered packing state. Numeric tokens are literal operands, never declaration ICEs. A producer must
// expand pack operands in their original macro environment; unresolved action/alignment identifiers remain
// unhandled here. Named stack entries are interned identifiers. Layout samples pack_log2 at the opening brace.

#define BUSTER_CHECK_PACK_INPUT_IMPLEMENTATION 1

typedef enum CheckPackOperation
{
    CHECK_PACK_UNKNOWN,
    CHECK_PACK_RESET,
    CHECK_PACK_SET,
    CHECK_PACK_PUSH,
    CHECK_PACK_POP,
    CHECK_PACK_SHOW,
    // `#pragma options align=reset` or `#pragma align=reset` (#1849): a pop where the stack has an entry, else a reset where
    // the current value isn't the default, else clang's warning.
    CHECK_PACK_ALIGN_RESET,
} CheckPackOperation;

// A `#pragma options align` or `#pragma align` warning (CheckInputPackPragma.align_warning, #1849), clang's
// -Wignored-pragmas messages: the directive is ignored but for CHECK_PACK_ALIGN_RESET_EMPTY, an empty stack's reset.
typedef enum CheckPackAlignWarning
{
    CHECK_PACK_ALIGN_NONE,
    CHECK_PACK_ALIGN_EXPECTED_ALIGN,
    CHECK_PACK_ALIGN_EXPECTED_EQUAL,
    CHECK_PACK_ALIGN_EXPECTED_IDENTIFIER,
    CHECK_PACK_ALIGN_INVALID,
    CHECK_PACK_ALIGN_EXTRA,
    CHECK_PACK_ALIGN_RESET_EMPTY,
} CheckPackAlignWarning;

enum
{
    CHECK_PACK_WARNING_SYNTAX = 1u << 0,
    CHECK_PACK_WARNING_ALIGNMENT = 1u << 1,
    CHECK_PACK_WARNING_EMPTY = 1u << 2,
    CHECK_PACK_WARNING_NAMED_ALIGNMENT = 1u << 3,
    CHECK_PACK_WARNING_SHOW = 1u << 4,
    CHECK_PACK_WARNING_ALIGN = 1u << 5,
};

typedef struct CheckInputPackPragma CheckInputPackPragma;
struct CheckInputPackPragma
{
    uint32_t pragma;
    uint32_t source_begin;
    uint32_t source_end;
    uint32_t token;
    uint32_t diagnostic_token;
    uint32_t alignment_source;
    uint32_t name;
    uint32_t matching_push;
    uint32_t previous_same;
    uint32_t depth_before;
    uint32_t depth_after;
    uint8_t operation;
    uint8_t alignment_log2;
    uint8_t pack_before;
    uint8_t pack_after;
    uint8_t warnings;
    uint8_t handled;
    bool has_alignment;
    // `#pragma options align` and `#pragma align` (#1849): whether it is one, and `#pragma options`'s; its warning
    // (CheckPackAlignWarning) at diagnostic_source, or past its last token where diagnostic_location is nonzero; and
    // whether the current value before and after it is another than the default (clang's AlignPackInfo, whose mode or
    // pack attribute an align=reset with an empty stack resets).
    bool align;
    bool options;
    bool native_mode;
    uint8_t align_warning;
    bool custom_before;
    bool custom_after;
    uint32_t diagnostic_source;
    uint32_t diagnostic_location;
};

typedef struct CheckInputPackPragmas CheckInputPackPragmas;
struct CheckInputPackPragmas
{
    CheckInputPackPragma* records;
    uint32_t count;
    uint32_t maximum_depth;
    uint32_t unmatched_pushes;
    uint8_t final_log2;
    bool final_custom;
};

typedef struct CheckPackLabel CheckPackLabel;
struct CheckPackLabel
{
    uint32_t name;
    uint32_t top;
};

static CheckPackLabel* check_input_pack_label(CheckPackLabel* labels, uint64_t capacity, uint32_t name)
{
    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485)) & (capacity - 1);
    while (labels[slot].name != 0 && labels[slot].name != name)
    {
        slot = (slot + 1) & (capacity - 1);
    }
    if (labels[slot].name == 0)
    {
        labels[slot].name = name;
        labels[slot].top = BUSTER_CHECK_NONE;
    }
    return &labels[slot];
}

static bool check_input_pack_number_kind(uint8_t kind)
{
    return kind == C_TOKEN_NUMERIC_CONSTANT || kind == BUSTER_PP_KIND_INTEGER;
}

static void check_input_pack_number(CheckInputReader* reader, CheckInputPackPragma* record)
{
    const PPTokens* output = &reader->pp->output;
    const uint32_t source = record->alignment_source;
    const uint8_t kind = output->kinds[source];
    CheckLiteral literal = { 0 };
    if (kind == BUSTER_PP_KIND_INTEGER)
    {
        literal.integer = output->values[source];
        literal.valid = true;
    }
    else
    {
        const uint64_t mark = reader->scratch->length;
        const uint32_t location = output->locations[source];
        const uint32_t end = output->values[source];
        uint8_t* buffer = end >= location ? arena_push(reader->scratch, (uint64_t)end - location + 1, 1) : 0;
        reader->output->resource |= end >= location && buffer == 0;
        if (buffer != 0)
        {
            const uint8_t* bytes = 0;
            const uint32_t length = pp_spelling(reader->pp, kind, end, location, buffer, &bytes);
            check_literal_number(bytes, length, reader->scratch, &literal);
            reader->output->resource |= literal.resource;
        }
        reader->scratch->length = mark;
    }
    if (!literal.valid)
    {
        record->handled = 3;
    }
    else if (literal.real || literal.imaginary || literal.integer > 16 || literal.high != 0 || literal.bit_width > 128 ||
             (literal.integer != 0 && (literal.integer & (literal.integer - 1)) != 0))
    {
        record->warnings |= CHECK_PACK_WARNING_ALIGNMENT;
    }
    else if (literal.integer != 0)
    {
        uint8_t log2 = 1;
        for (uint64_t value = literal.integer; value > 1; value >>= 1)
        {
            log2 += 1;
        }
        record->alignment_log2 = log2;
    }
}

static CheckInputPackPragma check_input_pack_record(CheckInputReader* reader, uint32_t pragma, uint32_t source, uint32_t end, uint32_t token)
{
    const PPTokens* output = &reader->pp->output;
    CheckInputPackPragma record =
    {
        .pragma = pragma,
        .source_begin = source,
        .source_end = end + 1,
        .token = token,
        .diagnostic_token = BUSTER_CHECK_NONE,
        .alignment_source = BUSTER_CHECK_NONE,
        .matching_push = BUSTER_CHECK_NONE,
        .previous_same = BUSTER_CHECK_NONE,
        .handled = 1,
    };
    uint32_t cursor = source + 3;
    bool syntax = cursor < end && output->kinds[cursor] == C_TOKEN_L_PAREN;
    cursor += syntax;
    if (syntax && cursor < end && output->kinds[cursor] == C_TOKEN_R_PAREN)
    {
        record.operation = CHECK_PACK_RESET;
    }
    else if (syntax && cursor < end && check_input_pack_number_kind(output->kinds[cursor]))
    {
        record.operation = CHECK_PACK_SET;
        record.has_alignment = true;
        record.alignment_source = cursor++;
    }
    else if (syntax && cursor < end && output->kinds[cursor] == C_TOKEN_RAW_IDENTIFIER)
    {
        record.operation = pp_token_is_name(reader->pp, output, cursor, end, "push") ? CHECK_PACK_PUSH :
                           pp_token_is_name(reader->pp, output, cursor, end, "pop") ? CHECK_PACK_POP :
                           pp_token_is_name(reader->pp, output, cursor, end, "show") ? CHECK_PACK_SHOW : CHECK_PACK_UNKNOWN;
        record.handled = record.operation == CHECK_PACK_UNKNOWN ? 0 : 1;
        cursor += 1;
        if ((record.operation == CHECK_PACK_PUSH || record.operation == CHECK_PACK_POP) && cursor < end && output->kinds[cursor] == C_TOKEN_COMMA)
        {
            cursor += 1;
            if (cursor < end && output->kinds[cursor] == C_TOKEN_RAW_IDENTIFIER)
            {
                record.name = output->values[cursor++];
                if (cursor < end && output->kinds[cursor] == C_TOKEN_COMMA)
                {
                    cursor += 1;
                    record.has_alignment = true;
                }
            }
            else
            {
                record.has_alignment = true;
            }
            if (record.has_alignment)
            {
                if (cursor < end && check_input_pack_number_kind(output->kinds[cursor]))
                {
                    record.alignment_source = cursor++;
                }
                else
                {
                    record.handled = cursor < end && output->kinds[cursor] == C_TOKEN_RAW_IDENTIFIER ? 0 : record.handled;
                    syntax = false;
                }
            }
        }
    }
    else
    {
        syntax = false;
    }
    syntax = syntax && cursor < end && output->kinds[cursor] == C_TOKEN_R_PAREN && cursor + 1 == end;
    if (!syntax)
    {
        record.warnings |= CHECK_PACK_WARNING_SYNTAX;
    }
    else if (record.handled != 0 && record.has_alignment)
    {
        check_input_pack_number(reader, &record);
    }
    return record;
}

// Whether the pragma at `source` is `#pragma options` or `#pragma align` (#1849), the stack's other spellings: a
// '=' after `align` makes `#pragma align` one, as clang's handler is registered for both names.
static bool check_input_pack_align_name(CheckInputReader* reader, uint32_t source, uint32_t end)
{
    const PPTokens* output = &reader->pp->output;
    return pp_token_is_name(reader->pp, output, source + 2, end, "options") || pp_token_is_name(reader->pp, output, source + 2, end, "align");
}

// `#pragma options align=X` and `#pragma align=X` (#1849, clang's ParseAlignPragma and Sema::ActOnPragmaOptionsAlign):
// packed pushes the current value and packs to 1, as `#pragma pack(push, 1)` does; natural, native and power push it and
// set the default (on x86-64 they are the default's layout); reset pops (CHECK_PACK_ALIGN_RESET); mac68k is clang's error
// on this target, left unhandled for the checker's refusal. A malformed one is ignored with clang's warning, at the token
// it names or past the last one. A raw client's operand that a macro spells is left unhandled, as ms_struct's is.
static CheckInputPackPragma check_input_pack_align_record(CheckInputReader* reader, uint32_t pragma, uint32_t source, uint32_t end, uint32_t token, uint64_t** macros)
{
    const PPTokens* output = &reader->pp->output;
    const bool options = pp_token_is_name(reader->pp, output, source + 2, end, "options");
    CheckInputPackPragma record =
    {
        .pragma = pragma,
        .source_begin = source,
        .source_end = end + 1,
        .token = token,
        .diagnostic_token = BUSTER_CHECK_NONE,
        .alignment_source = BUSTER_CHECK_NONE,
        .matching_push = BUSTER_CHECK_NONE,
        .previous_same = BUSTER_CHECK_NONE,
        .handled = 1,
        .align = true,
        .options = options,
    };
    uint32_t at = source + 3;
    uint8_t warning = CHECK_PACK_ALIGN_NONE;
    if (options && !pp_token_is_name(reader->pp, output, at, end, "align"))
    {
        warning = CHECK_PACK_ALIGN_EXPECTED_ALIGN;
    }
    at += options && warning == CHECK_PACK_ALIGN_NONE ? 1 : 0;
    if (warning == CHECK_PACK_ALIGN_NONE && (at >= end || output->kinds[at] != C_TOKEN_EQUAL))
    {
        warning = CHECK_PACK_ALIGN_EXPECTED_EQUAL;
    }
    at += warning == CHECK_PACK_ALIGN_NONE ? 1 : 0;
    const IdentId option = warning == CHECK_PACK_ALIGN_NONE && at < end ? check_input_pragma_name(reader, at) : 0;
    if (warning == CHECK_PACK_ALIGN_NONE && option == 0)
    {
        warning = CHECK_PACK_ALIGN_EXPECTED_IDENTIFIER;
    }
    const bool packed = option != 0 && pp_token_is_name(reader->pp, output, at, end, "packed");
    const bool natural = option != 0 && (pp_token_is_name(reader->pp, output, at, end, "natural") || pp_token_is_name(reader->pp, output, at, end, "native") ||
                                         pp_token_is_name(reader->pp, output, at, end, "power"));
    const bool reset = option != 0 && pp_token_is_name(reader->pp, output, at, end, "reset");
    const bool mac68k = option != 0 && pp_token_is_name(reader->pp, output, at, end, "mac68k");
    if (warning == CHECK_PACK_ALIGN_NONE && !packed && !natural && !reset && !mac68k)
    {
        warning = CHECK_PACK_ALIGN_INVALID;
    }
    at += warning == CHECK_PACK_ALIGN_NONE ? 1 : 0;
    if (warning == CHECK_PACK_ALIGN_NONE && at < end)
    {
        warning = CHECK_PACK_ALIGN_EXTRA;
    }
    record.align_warning = warning;
    record.diagnostic_source = at < end ? at : end - 1;
    if (warning != CHECK_PACK_ALIGN_NONE)
    {
        // No annotation: the directive is ignored, wherever it is.
        record.warnings |= CHECK_PACK_WARNING_ALIGN | CHECK_PACK_WARNING_SYNTAX;
        if (at >= end)
        {
            const uint32_t last = end - 1;
            const bool named = check_input_pragma_name(reader, last) != 0;
            record.diagnostic_location = output->locations[last] + (named ? intern_length(reader->pp->table, output->values[last]) : 1);
        }
    }
    else
    {
        record.operation = reset ? CHECK_PACK_ALIGN_RESET : CHECK_PACK_PUSH;
        record.has_alignment = !reset;
        record.alignment_log2 = packed ? 1 : 0;
        record.native_mode = natural && !pp_token_is_name(reader->pp, output, at - 1, end, "natural");
        record.handled = mac68k ? 0 : 1;
    }
    for (uint32_t s = source + 3; !reader->pp->options.semantic_pragmas && record.handled != 0 && s < end; s += 1)
    {
        const IdentId name = check_input_pragma_name(reader, s);
        record.handled = name != 0 && check_input_pragma_macro(reader, macros, name) ? 0 : record.handled;
    }
    return record;
}

// Pack is a parser annotation in Clang: declaration-specifier and statement boundaries consume it, while an
// expression/declarator interior does not. Malformed directive syntax produces no annotation at all. Construct
// delimiter links and declarator ancestry once; a pragma never walks a potentially deep parent chain.
static bool check_input_pack_placements(CheckInputReader* reader, CheckInputPackPragmas* history)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t* links = arena_push_array(reader->scratch, (uint64_t)count + 1, sizeof(uint32_t), 4);
    uint8_t* declarations = links != 0 ? arena_push_array(reader->scratch, (uint64_t)count + 1, 1, 1) : 0;
    const bool valid = links != 0 && declarations != 0;
    reader->output->resource |= !valid;
    if (valid)
    {
        memset(links, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(declarations, 0, (uint64_t)count + 1);
        uint32_t group = BUSTER_CHECK_NONE;
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint8_t role = parser->tree.roles[token];
            const uint32_t parent = parser->tree.parents[token];
            if (role < PARSE_ROLE_COUNT && parse_closer_roles[role] != PARSE_ROLE_NONE && parent < count)
            {
                links[token] = group;
                links[parent] = token;
                group = token;
            }
            else if (group < count && parser->tree.parents[group] == token)
            {
                group = links[group];
            }
        }
        for (uint32_t order = count; order-- > 0;)
        {
            const uint32_t token = reader->postorder[order];
            const uint32_t parent = parser->tree.parents[token];
            const uint8_t role = parser->tree.roles[token];
            bool declaration = role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG || role == PARSE_ROLE_CLOSE_SPECIFIER ||
                               role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
            if (!declaration && parent < count && declarations[parent])
            {
                const uint8_t parent_role = parser->tree.roles[parent];
                declaration = parent_role == PARSE_ROLE_SPECIFIER || parent_role == PARSE_ROLE_SEPARATOR || parent_role == PARSE_ROLE_CLOSE_GROUP ||
                              (parent_role == PARSE_ROLE_PREFIX && parser->tokens.kinds[parent] == PARSE_KIND_STAR) ||
                              ((parent_role == PARSE_ROLE_ASSIGN || parent_role == PARSE_ROLE_BITFIELD) && token < parent) ||
                              ((parent_role == PARSE_ROLE_CLOSE_CALL || parent_role == PARSE_ROLE_CLOSE_FUNCTION ||
                                parent_role == PARSE_ROLE_CLOSE_SUBSCRIPT || parent_role == PARSE_ROLE_CLOSE_ATTRIBUTE) && token < links[parent]);
            }
            declarations[token] = declaration;
        }
        uint32_t cursor = 0;
        group = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; i < history->count; i += 1)
        {
            CheckInputPackPragma* record = history->records + i;
            const uint32_t token = record->token;
            while (cursor < token)
            {
                const uint8_t role = parser->tree.roles[cursor];
                if (role < PARSE_ROLE_COUNT && parse_closer_roles[role] != PARSE_ROLE_NONE)
                {
                    group = cursor;
                }
                else if (group < count && parser->tree.parents[group] == cursor)
                {
                    group = links[group];
                }
                cursor += 1;
            }
            if (record->handled == 1 && !(record->warnings & CHECK_PACK_WARNING_SYNTAX) && token != 0)
            {
                const uint32_t before = token - 1;
                const uint8_t role = parser->tree.roles[before];
                const uint8_t kind = parser->tokens.kinds[before];
                const uint32_t parent = parser->tree.parents[before];
                const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
                const uint8_t group_role = group < count ? parser->tree.roles[group] : PARSE_ROLE_NONE;
                const uint32_t close = group < count ? parser->tree.parents[group] : count;
                const bool parameter = group_role == PARSE_ROLE_OPEN_FUNCTION ||
                                       (group_role == PARSE_ROLE_OPEN_CALL && close < count && declarations[close]);
                const bool statement = group == BUSTER_CHECK_NONE || group_role == PARSE_ROLE_OPEN_BODY ||
                                       group_role == PARSE_ROLE_OPEN_BLOCK || group_role == PARSE_ROLE_OPEN_STRUCT;
                bool accepted = role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG || role == PARSE_ROLE_CLOSE_SPECIFIER ||
                                role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER || role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM ||
                                (parameter && (role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_SEPARATOR)) ||
                                (statement && (role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_STRUCT ||
                                               role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_TERMINATOR ||
                                               role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO ||
                                               role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE ||
                                               (role == PARSE_ROLE_CLOSE_CONDITION && parent_role != PARSE_ROLE_TERMINATOR)));
                const bool tag_name = kind == PARSE_KIND_IDENTIFIER && role == PARSE_ROLE_LEAF_RIGHT &&
                                      (parent_role == PARSE_ROLE_CLOSE_STRUCT || parent_role == PARSE_ROLE_CLOSE_ENUM);
                accepted |= tag_name;
                if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
                {
                    accepted = false;
                    record->diagnostic_token = before;
                }
                else if (tag_name && token < count && parser->tokens.kinds[token] == PARSE_KIND_L_BRACE)
                {
                    accepted = false;
                    record->diagnostic_token = token;
                }
                else if (token < count && parser->tokens.kinds[token] == PARSE_KIND_ELSE)
                {
                    accepted = false;
                    record->diagnostic_token = token;
                }
                else if (parent < count && parser->tokens.kinds[parent] == PARSE_KIND_DO)
                {
                    accepted = false;
                }
                else if (role == PARSE_ROLE_CLOSE_CONDITION && parent_role == PARSE_ROLE_TERMINATOR)
                {
                    accepted = false;
                    record->diagnostic_token = before;
                }
                else if (!accepted && statement)
                {
                    const bool complete = role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_STRING_NEXT || role == PARSE_ROLE_POSTFIX ||
                                          role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_CLOSE_ATTRIBUTE ||
                                          role == PARSE_ROLE_CLOSE_INITIALIZER || role == PARSE_ROLE_CLOSE_COMPOUND_BODY || role == PARSE_ROLE_CLOSE_ASM ||
                                          role == PARSE_ROLE_CLOSE_KEYWORD || role == PARSE_ROLE_CLOSE_SIZEOF ||
                                          ((role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_FUNCTION) && !declarations[before]);
                    if (complete)
                    {
                        record->diagnostic_token = before;
                    }
                }
                record->handled = accepted ? 1 : 3;
            }
        }
    }
    return valid;
}

// Two linear scans retain only pack records. A named push links to the previous push of that name in a bounded
// hash table; each popped stack entry is removed once, including a pop that unwinds several different names.
// Repeated unmatched named pops therefore do not repeatedly walk an arbitrarily deep stack.
static bool check_input_pack_pragmas(CheckInputReader* reader, uint8_t* handled)
{
    const uint64_t output_mark = reader->arena->length;
    const uint64_t scratch_mark = reader->scratch->length;
    const uint32_t pragmas = reader->parser->tokens.pragma_count;
    const uint32_t token_count = reader->parser->tokens.count;
    uint32_t capacity = 0;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    uint64_t* macros = NULL;
    for (uint32_t pragma = 0; pragma < pragmas; pragma += 1)
    {
        uint32_t source = 0;
        uint32_t end = 0;
        uint32_t token = 0;
        const bool range = check_input_retained_pragma(reader, pragma, &previous_end, &previous_token, &source, &end, &token);
        capacity += range && (pp_token_is_name(reader->pp, &reader->pp->output, source + 2, end, "pack") || check_input_pack_align_name(reader, source, end));
    }
    bool valid = true;
    CheckInputPackPragmas* history = 0;
    uint8_t* column = 0;
    if (capacity != 0)
    {
        uint64_t label_capacity = 1;
        while (label_capacity < (uint64_t)capacity * 2 + 1)
        {
            label_capacity *= 2;
        }
        history = arena_push(reader->arena, sizeof(*history), _Alignof(CheckInputPackPragmas));
        CheckInputPackPragma* records = history != 0 ? arena_push_array(reader->arena, capacity, sizeof(*records), _Alignof(CheckInputPackPragma)) : 0;
        column = records != 0 ? arena_push_array(reader->arena, (uint64_t)token_count + 1, sizeof(uint8_t), 1) : 0;
        uint32_t* stack = column != 0 ? arena_push_array(reader->scratch, capacity, sizeof(*stack), 4) : 0;
        CheckPackLabel* labels = stack != 0 ? arena_push_array(reader->scratch, label_capacity, sizeof(*labels), _Alignof(CheckPackLabel)) : 0;
        valid = history != 0 && records != 0 && column != 0 && stack != 0 && labels != 0;
        reader->output->resource |= !valid;
        if (valid)
        {
            *history = (CheckInputPackPragmas)
            {
                .records = records,
            };
            memset(labels, 0, label_capacity * sizeof(*labels));
            previous_end = 0;
            previous_token = 0;
            uint32_t written = 0;
            for (uint32_t pragma = 0; valid && pragma < pragmas; pragma += 1)
            {
                uint32_t source = 0;
                uint32_t end = 0;
                uint32_t token = 0;
                const bool range = check_input_retained_pragma(reader, pragma, &previous_end, &previous_token, &source, &end, &token);
                const bool align = range && check_input_pack_align_name(reader, source, end);
                if (range && (align || pp_token_is_name(reader->pp, &reader->pp->output, source + 2, end, "pack")))
                {
                    CheckInputPackPragma record = align ? check_input_pack_align_record(reader, pragma, source, end, token, &macros) :
                                                          check_input_pack_record(reader, pragma, source, end, token);
                    memset(column + written, history->final_log2, token - written);
                    written = token;
                    record.depth_before = history->unmatched_pushes;
                    record.pack_before = history->final_log2;
                    record.custom_before = history->final_custom;
                    if (record.handled == 1 && record.warnings == 0 && record.operation == CHECK_PACK_ALIGN_RESET)
                    {
                        // clang's reset: a pop of the stack's top, a reset of a value other than the default, or its warning.
                        record.operation = history->unmatched_pushes != 0 ? CHECK_PACK_POP : history->final_custom ? CHECK_PACK_RESET : CHECK_PACK_ALIGN_RESET;
                        record.align_warning = record.operation == CHECK_PACK_ALIGN_RESET ? CHECK_PACK_ALIGN_RESET_EMPTY : CHECK_PACK_ALIGN_NONE;
                        record.diagnostic_source = source + 2;
                        record.warnings |= record.operation == CHECK_PACK_ALIGN_RESET ? CHECK_PACK_WARNING_ALIGN : 0;
                    }
                    if (record.handled == 1 && record.warnings == 0)
                    {
                        if (record.operation == CHECK_PACK_PUSH)
                        {
                            stack[history->unmatched_pushes++] = history->count;
                            if (record.name != 0)
                            {
                                CheckPackLabel* label = check_input_pack_label(labels, label_capacity, record.name);
                                record.previous_same = label->top;
                                label->top = history->count;
                            }
                            history->maximum_depth = history->unmatched_pushes > history->maximum_depth ? history->unmatched_pushes : history->maximum_depth;
                        }
                        else if (record.operation == CHECK_PACK_POP)
                        {
                            record.warnings |= record.name != 0 && record.has_alignment ? CHECK_PACK_WARNING_NAMED_ALIGNMENT : 0;
                            if (history->unmatched_pushes == 0)
                            {
                                record.warnings |= CHECK_PACK_WARNING_EMPTY;
                            }
                            else
                            {
                                const uint32_t target = record.name != 0 ? check_input_pack_label(labels, label_capacity, record.name)->top :
                                                        stack[history->unmatched_pushes - 1];
                                if (target != BUSTER_CHECK_NONE)
                                {
                                    record.matching_push = target;
                                    const uint32_t depth = records[target].depth_before;
                                    history->final_log2 = records[target].pack_before;
                                    history->final_custom = records[target].custom_before;
                                    while (history->unmatched_pushes > depth)
                                    {
                                        const CheckInputPackPragma* popped = &records[stack[--history->unmatched_pushes]];
                                        if (popped->name != 0)
                                        {
                                            check_input_pack_label(labels, label_capacity, popped->name)->top = popped->previous_same;
                                        }
                                    }
                                }
                            }
                        }
                        else if (record.operation == CHECK_PACK_RESET)
                        {
                            history->final_log2 = 0;
                            history->final_custom = false;
                        }
                        else if (record.operation == CHECK_PACK_SHOW)
                        {
                            record.warnings |= CHECK_PACK_WARNING_SHOW;
                        }
                        if (record.has_alignment)
                        {
                            history->final_log2 = record.alignment_log2;
                            // A pack value set is clang's pack attribute; options align's mode is the default's alone
                            // for native and power (#1849).
                            history->final_custom = !record.native_mode;
                        }
                    }
                    record.depth_after = history->unmatched_pushes;
                    record.pack_after = history->final_log2;
                    record.custom_after = history->final_custom;
                    records[history->count++] = record;
                    valid = !reader->output->resource;
                }
            }
            if (valid)
            {
                memset(column + written, history->final_log2, (uint64_t)token_count + 1 - written);
            }
        }
    }
    if (valid && history != 0)
    {
        valid = check_input_pack_placements(reader, history);
    }
    if (valid && history != 0)
    {
        reader->output->pack_pragmas = history;
        reader->output->pack_log2 = column;
        for (uint32_t record = 0; record < history->count; record += 1)
        {
            const CheckInputPackPragma* item = &history->records[record];
            handled[item->pragma] = item->handled;
        }
    }
    else if (!valid)
    {
        reader->arena->length = output_mark;
    }
    reader->scratch->length = scratch_mark;
    return valid;
}

// A `#pragma options align` or `#pragma align` warning's record (#1849), for the event that names it.
static const CheckInputPackPragma* check_input_pack_warning(const CheckInput* input, const CheckWarningEvent* event)
{
    const CheckInputPackPragmas* history = input->pack_pragmas;
    const uint32_t index = event->payload.data;
    const CheckInputPackPragma* result = event->kind == CHECK_WARNING_EVENT_PRAGMA && (event->flags & BUSTER_CHECK_WARNING_PACK_PRAGMA) != 0 &&
                                        index != 0 && history != NULL && index <= history->count ? history->records + index - 1 : NULL;
    return result;
}

// clang's -Wignored-pragmas message of a `#pragma options align` or `#pragma align` record's warning (#1849).
static const char* check_input_pack_align_message(const CheckInputPackPragma* record)
{
    static const char* const options[] = {
        [CHECK_PACK_ALIGN_EXPECTED_ALIGN] = "expected 'align' following '#pragma options' - ignored",
        [CHECK_PACK_ALIGN_EXPECTED_EQUAL] = "expected '=' following '#pragma options align' - ignored",
        [CHECK_PACK_ALIGN_EXPECTED_IDENTIFIER] = "expected identifier in '#pragma options' - ignored",
        [CHECK_PACK_ALIGN_INVALID] = "invalid alignment option in '#pragma options align' - ignored",
        [CHECK_PACK_ALIGN_EXTRA] = "extra tokens at end of '#pragma options' - ignored",
        [CHECK_PACK_ALIGN_RESET_EMPTY] = "#pragma options align=reset failed: stack empty",
    };
    static const char* const align[] = {
        [CHECK_PACK_ALIGN_EXPECTED_ALIGN] = "expected 'align' following '#pragma options' - ignored",
        [CHECK_PACK_ALIGN_EXPECTED_EQUAL] = "expected '=' following '#pragma align' - ignored",
        [CHECK_PACK_ALIGN_EXPECTED_IDENTIFIER] = "expected identifier in '#pragma align' - ignored",
        [CHECK_PACK_ALIGN_INVALID] = "invalid alignment option in '#pragma align' - ignored",
        [CHECK_PACK_ALIGN_EXTRA] = "extra tokens at end of '#pragma align' - ignored",
        [CHECK_PACK_ALIGN_RESET_EMPTY] = "#pragma options align=reset failed: stack empty",
    };
    const uint8_t warning = record->align_warning;
    return warning != CHECK_PACK_ALIGN_NONE && warning <= CHECK_PACK_ALIGN_RESET_EMPTY ? (record->options ? options : align)[warning] : NULL;
}

// The warnings of `#pragma options align` and `#pragma align` (#1849), appended to the lexical pragma facts after the
// unknown-pragma collector has published its view, as ms_struct's are.
static bool check_input_pack_align_warnings(CheckInputReader* reader)
{
    const CheckInputPackPragmas* history = reader->output->pack_pragmas;
    const CheckWarningEvents before = reader->output->pragma_warnings;
    CheckWarningBuffer buffer = { .arena = reader->arena, .events = (CheckWarningEvent*)before.events, .count = before.count, .capacity = before.count };
    bool valid = true;
    for (uint32_t i = 0; valid && history != NULL && i < history->count; i += 1)
    {
        const CheckInputPackPragma* record = history->records + i;
        if (record->align && record->handled != 0 && record->align_warning != CHECK_PACK_ALIGN_NONE)
        {
            const CheckWarningEvent event = {
                .id = CHECK_WARNING_ID_IGNORED_PRAGMA,
                .kind = CHECK_WARNING_EVENT_PRAGMA,
                .flags = BUSTER_CHECK_WARNING_PACK_PRAGMA,
                .token = record->token,
                .source = record->diagnostic_source,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = record->token,
                .payload = { .token = record->source_begin, .data = i + 1 },
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
