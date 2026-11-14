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

// A `#pragma pack` warning's message (#1849), an event's payload.name: clang's parser's (HandlePragmaPack) for a
// malformed directive, which is ignored, at the token it names or past the last one; Sema's (ActOnPragmaPack) at the
// directive's `pack`; and one at the unit's end for each push left open (DiagnoseUnterminatedPragmaAlignPack).
typedef enum CheckPackMessage
{
    CHECK_PACK_MESSAGE_ALIGN,
    CHECK_PACK_MESSAGE_EXPECTED_LPAREN,
    CHECK_PACK_MESSAGE_EXPECTED_RPAREN,
    CHECK_PACK_MESSAGE_INVALID_ACTION,
    CHECK_PACK_MESSAGE_MALFORMED,
    CHECK_PACK_MESSAGE_EXTRA,
    CHECK_PACK_MESSAGE_INVALID_ALIGNMENT,
    CHECK_PACK_MESSAGE_SHOW,
    CHECK_PACK_MESSAGE_NAMED_POP,
    CHECK_PACK_MESSAGE_EMPTY_POP,
    CHECK_PACK_MESSAGE_UNTERMINATED,
    CHECK_PACK_MESSAGE_INCLUDE,
} CheckPackMessage;

// An unterminated push's note (#1849): the event's payload.token is the source of the record that set the current value,
// or one of these.
#define BUSTER_CHECK_PACK_NO_NOTE UINT32_MAX
#define BUSTER_CHECK_PACK_UNLOCATED_NOTE (UINT32_MAX - 1u)

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
    bool error;
    uint8_t align_warning;
    bool custom_before;
    bool custom_after;
    uint32_t diagnostic_source;
    uint32_t diagnostic_location;
    // A `#pragma pack`'s parser warning (CheckPackMessage, #1849), at diagnostic_source or diagnostic_location as an
    // align one's; the record that set the current value when this one ran (clang's CurrentPragmaLocation, a push's
    // saved one), BUSTER_CHECK_NONE for none; and whether it is a push left open at the unit's end.
    uint8_t syntax_warning;
    bool unterminated;
    uint32_t current_before;
};

typedef struct CheckInputPackPragmas CheckInputPackPragmas;
struct CheckInputPackPragmas
{
    CheckInputPackPragma* records;
    uint32_t count;
    // Handled, state-free target errors; ordinary packing histories do not enter the driver's error merge.
    uint32_t error_count;
    uint32_t maximum_depth;
    uint32_t unmatched_pushes;
    uint8_t final_log2;
    bool final_custom;
    // The record that set the current value last (clang's CurrentPragmaLocation), BUSTER_CHECK_NONE for none (#1849).
    uint32_t current;
    // The inclusions that leave the value another than they found it (#1849, CheckPackInclude), in the order they end.
    struct CheckPackInclude* includes;
    uint32_t include_count;
};

// An inclusion that changes the pack value (#1849, clang's ChangedStateAtExit): where it was included (the file name's
// location), where its output ends, and the source of the directive that set the value at its end, or a
// BUSTER_CHECK_PACK_* note sentinel.
typedef struct CheckPackInclude CheckPackInclude;
struct CheckPackInclude
{
    uint32_t location;
    uint32_t exit;
    uint32_t note;
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

// The source location just past the token at `source` (#1849), where clang's parser places a warning about the end of
// the directive's line: a name's or a number's spelling, or a punctuator's one byte.
static uint32_t check_input_pack_after(CheckInputReader* reader, uint32_t source)
{
    const PPTokens* output = &reader->pp->output;
    const uint8_t kind = output->kinds[source];
    const bool named = check_input_pragma_name(reader, source) != 0;
    const uint32_t length = named ? intern_length(reader->pp->table, output->values[source]) :
                            kind == C_TOKEN_NUMERIC_CONSTANT && output->values[source] > output->locations[source] ? output->values[source] - output->locations[source] :
                                                                                                                    1;
    return output->locations[source] + length;
}

// `#pragma pack`'s record, as clang's parser reads it (HandlePragmaPack): `()` resets, `(N)` sets, `(show)`, and
// `(push|pop[, label][, N])`. A malformed one is ignored with the parser's warning (#1849, CheckPackMessage), at the
// token it names or past the line's last one.
static CheckInputPackPragma check_input_pack_record(CheckInputReader* reader, uint32_t pragma, uint32_t source, uint32_t end, uint32_t token, uint64_t** macros)
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
        .current_before = BUSTER_CHECK_NONE,
        .handled = 1,
        .operation = CHECK_PACK_RESET,
    };
    uint32_t cursor = source + 3;
    uint8_t warning = cursor < end && output->kinds[cursor] == C_TOKEN_L_PAREN ? CHECK_PACK_MESSAGE_ALIGN : CHECK_PACK_MESSAGE_EXPECTED_LPAREN;
    cursor += warning == CHECK_PACK_MESSAGE_ALIGN;
    if (warning == CHECK_PACK_MESSAGE_ALIGN && cursor < end && check_input_pack_number_kind(output->kinds[cursor]))
    {
        record.operation = CHECK_PACK_SET;
        record.has_alignment = true;
        record.alignment_source = cursor++;
    }
    else if (warning == CHECK_PACK_MESSAGE_ALIGN && cursor < end && output->kinds[cursor] == C_TOKEN_RAW_IDENTIFIER)
    {
        record.operation = pp_token_is_name(reader->pp, output, cursor, end, "push") ? CHECK_PACK_PUSH :
                           pp_token_is_name(reader->pp, output, cursor, end, "pop") ? CHECK_PACK_POP :
                           pp_token_is_name(reader->pp, output, cursor, end, "show") ? CHECK_PACK_SHOW : CHECK_PACK_UNKNOWN;
        warning = record.operation == CHECK_PACK_UNKNOWN ? CHECK_PACK_MESSAGE_INVALID_ACTION : warning;
        cursor += warning == CHECK_PACK_MESSAGE_ALIGN;
        if ((record.operation == CHECK_PACK_PUSH || record.operation == CHECK_PACK_POP) && cursor < end && output->kinds[cursor] == C_TOKEN_COMMA)
        {
            cursor += 1;
            if (cursor < end && check_input_pack_number_kind(output->kinds[cursor]))
            {
                record.has_alignment = true;
                record.alignment_source = cursor++;
            }
            else if (cursor < end && output->kinds[cursor] == C_TOKEN_RAW_IDENTIFIER)
            {
                record.name = output->values[cursor++];
                if (cursor < end && output->kinds[cursor] == C_TOKEN_COMMA)
                {
                    cursor += 1;
                    warning = cursor < end && check_input_pack_number_kind(output->kinds[cursor]) ? warning : CHECK_PACK_MESSAGE_MALFORMED;
                    record.has_alignment = warning == CHECK_PACK_MESSAGE_ALIGN;
                    record.alignment_source = warning == CHECK_PACK_MESSAGE_ALIGN ? cursor++ : BUSTER_CHECK_NONE;
                }
            }
            else
            {
                warning = CHECK_PACK_MESSAGE_MALFORMED;
            }
        }
    }
    if (warning == CHECK_PACK_MESSAGE_ALIGN && (cursor >= end || output->kinds[cursor] != C_TOKEN_R_PAREN))
    {
        warning = CHECK_PACK_MESSAGE_EXPECTED_RPAREN;
    }
    cursor += warning == CHECK_PACK_MESSAGE_ALIGN;
    if (warning == CHECK_PACK_MESSAGE_ALIGN && cursor < end)
    {
        warning = CHECK_PACK_MESSAGE_EXTRA;
    }
    if (warning != CHECK_PACK_MESSAGE_ALIGN)
    {
        // No annotation: the directive is ignored, wherever it is.
        record.warnings |= CHECK_PACK_WARNING_SYNTAX;
        record.syntax_warning = warning;
        record.diagnostic_source = cursor < end ? cursor : end - 1;
        record.diagnostic_location = cursor < end ? 0 : check_input_pack_after(reader, end - 1);
    }
    else if (record.has_alignment)
    {
        check_input_pack_number(reader, &record);
    }
    // A raw client's operand that a macro spells is left unhandled, as an align one's: clang's parser reads its expansion.
    for (uint32_t s = source + 3; !reader->pp->options.semantic_pragmas && record.handled != 0 && s < end; s += 1)
    {
        const IdentId name = check_input_pragma_name(reader, s);
        record.handled = name != 0 && check_input_pragma_macro(reader, macros, name) ? 0 : record.handled;
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
// on this target, retained without changing the stack. A malformed one is ignored with clang's warning, at the token
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
    else if (mac68k)
    {
        record.error = true;
        record.diagnostic_source = source + 2;
    }
    else
    {
        record.operation = reset ? CHECK_PACK_ALIGN_RESET : CHECK_PACK_PUSH;
        record.has_alignment = !reset;
        record.alignment_log2 = packed ? 1 : 0;
        record.native_mode = natural && !pp_token_is_name(reader->pp, output, at - 1, end, "natural");
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
                .current = BUSTER_CHECK_NONE,
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
                                                          check_input_pack_record(reader, pragma, source, end, token, &macros);
                    memset(column + written, history->final_log2, token - written);
                    written = token;
                    record.depth_before = history->unmatched_pushes;
                    record.pack_before = history->final_log2;
                    record.custom_before = history->final_custom;
                    record.current_before = history->current;
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
                                    history->current = records[target].current_before;
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
                            history->current = history->count;
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
                            history->current = history->count;
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
                // The pushes left open at the unit's end, which clang warns about there (#1849).
                for (uint32_t k = 0; k < history->unmatched_pushes; k += 1)
                {
                    records[stack[k]].unterminated = true;
                }
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
            history->error_count += item->handled == 1 && item->error;
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

// clang's message of a `#pragma pack`, `#pragma options align` or `#pragma align` record's warning (#1849): an align
// one's own (CHECK_PACK_MESSAGE_ALIGN), or the event's CheckPackMessage (payload.name), with its text in `scratch`.
static const char* check_input_pack_message(const CheckInputPackPragma* record, const CheckWarningEvent* event, Arena* scratch)
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
    static const char* const pack[] = {
        [CHECK_PACK_MESSAGE_EXPECTED_LPAREN] = "missing '(' after '#pragma pack' - ignoring",
        [CHECK_PACK_MESSAGE_EXPECTED_RPAREN] = "missing ')' after '#pragma pack' - ignoring",
        [CHECK_PACK_MESSAGE_INVALID_ACTION] = "unknown action for '#pragma pack' - ignored",
        [CHECK_PACK_MESSAGE_MALFORMED] = "expected integer or identifier in '#pragma pack' - ignored",
        [CHECK_PACK_MESSAGE_EXTRA] = "extra tokens at end of '#pragma pack' - ignored",
        [CHECK_PACK_MESSAGE_INVALID_ALIGNMENT] = "expected #pragma pack parameter to be '1', '2', '4', '8', or '16'",
        [CHECK_PACK_MESSAGE_NAMED_POP] = "specifying both a name and alignment to 'pop' is undefined",
        [CHECK_PACK_MESSAGE_EMPTY_POP] = "#pragma pack(pop, ...) failed: stack empty",
        [CHECK_PACK_MESSAGE_UNTERMINATED] = "unterminated '#pragma pack (push, ...)' at end of file",
        [CHECK_PACK_MESSAGE_INCLUDE] = "the current #pragma pack alignment value is modified in the included file",
    };
    const uint32_t kind = event->payload.name;
    const uint8_t warning = record->align_warning;
    // pack(show)'s value is the current one, 8 for the default (Sema::ActOnPragmaPack's).
    const uint32_t value = record->pack_before != 0 ? UINT32_C(1) << (record->pack_before - 1) : 8;
    char* shown = kind == CHECK_PACK_MESSAGE_SHOW ? BUSTER_ARENA_PUSH_ARRAY(scratch, char, 48) : NULL;
    if (shown != NULL)
    {
        snprintf(shown, 48, "value of #pragma pack(show) == %u", value);
    }
    return kind == CHECK_PACK_MESSAGE_SHOW ? shown :
           kind != CHECK_PACK_MESSAGE_ALIGN ? (kind <= CHECK_PACK_MESSAGE_INCLUDE ? pack[kind] : NULL) :
           warning != CHECK_PACK_ALIGN_NONE && warning <= CHECK_PACK_ALIGN_RESET_EMPTY ? (record->options ? options : align)[warning] :
                                                                                         NULL;
}

// Whether a pack event is Sema's, after the directive's annotation is parsed or at the unit's end (#1849), rather than the
// parser's at the directive.
static bool check_input_pack_semantic(const CheckInputPackPragma* record, const CheckWarningEvent* event)
{
    return (event->payload.name >= CHECK_PACK_MESSAGE_INVALID_ALIGNMENT && event->payload.name <= CHECK_PACK_MESSAGE_UNTERMINATED) ||
           (record->align && record->align_warning == CHECK_PACK_ALIGN_RESET_EMPTY);
}

// The output position just before the pack directive after `record` (#1849), where its Sema warnings come at the latest;
// UINT32_MAX for the last one, and for one right after a function's body, whose own warnings at its end the checker
// publishes at the next parser token, which clang reports before it handles the directive's annotation (a later pack
// directive's parser warnings then come before this one's Sema ones, where clang's come after).
static uint32_t check_input_pack_following(const Parser* parser, const CheckInputPackPragmas* history, const CheckInputPackPragma* record)
{
    const uint64_t index = (uint64_t)(record - history->records);
    const bool after_body = record->token != 0 && record->token - 1 < parser->tokens.count && parser->tree.roles[record->token - 1] == PARSE_ROLE_CLOSE_BODY;
    return !after_body && index + 1 < history->count && history->records[index + 1].source_begin != 0 ? history->records[index + 1].source_begin - 1 : UINT32_MAX;
}

// Appends a pack record's warning event of `message`, at `source` (#1849).
static bool check_input_pack_event(CheckWarningBuffer* buffer, const CheckInputPackPragma* record, uint32_t index, uint32_t id, uint32_t message, uint32_t source,
                                   bool end, uint32_t note)
{
    const CheckWarningEvent event = {
        .id = id,
        .kind = CHECK_WARNING_EVENT_PRAGMA,
        .flags = BUSTER_CHECK_WARNING_PACK_PRAGMA,
        .token = record->token,
        .source = source,
        .completion_kind = end ? CHECK_WARNING_COMPLETE_TU_END : CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = end ? BUSTER_CHECK_WARNING_NO_SOURCE : record->token,
        .suborder = end ? CHECK_WARNING_UNIT_END_PACK : 0,
        .payload = { .name = message, .token = end ? note : record->source_begin, .data = index + 1 },
    };
    return check_warning_append(buffer, &event, NULL, 0);
}

// The pack value after the records before output position `position` (#1849): the last such record's, and in `current`
// the record that set it (clang's CurrentPragmaLocation), BUSTER_CHECK_NONE for none.
static uint32_t check_input_pack_value_at(const CheckInputPackPragmas* history, uint32_t position, uint32_t* current)
{
    uint32_t low = 0;
    uint32_t high = history->count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = history->records[middle].source_begin < position ? middle + 1 : low;
        high = history->records[middle].source_begin < position ? high : middle;
    }
    const CheckInputPackPragma* last = low != 0 ? history->records + low - 1 : NULL;
    *current = last == NULL ? BUSTER_CHECK_NONE : low < history->count ? history->records[low].current_before : history->current;
    return last != NULL ? (uint32_t)last->pack_after << 1 | (last->custom_after ? 1u : 0u) : 0;
}

// The location of the file name of the directive at `location` in `range` (#1849), clang's include location: past its
// '#', its name and the blanks between them; `location` itself where the bytes end first.
static uint32_t check_input_pack_include_name(const PPRange* range, uint32_t location)
{
    uint32_t at = location >= range->base && location - range->base < range->length ? location - range->base : range->length;
    const uint32_t start = at;
    at += at < range->length && range->bytes[at] == '#' ? 1 : 0;
    while (at < range->length && (range->bytes[at] == ' ' || range->bytes[at] == '\t'))
    {
        at += 1;
    }
    while (at < range->length && (range->bytes[at] == '_' || (range->bytes[at] | 0x20) - 'a' < 26u))
    {
        at += 1;
    }
    while (at < range->length && (range->bytes[at] == ' ' || range->bytes[at] == '\t'))
    {
        at += 1;
    }
    return at < range->length && start < range->length ? range->base + at : location;
}

// The inclusions that leave the pack value another than they found it (#1849, clang's SemaPPCallbacks and
// DiagnoseNonDefaultPragmaAlignPack's ChangedStateAtExit), from the preprocessor's runs of output by range: each
// inclusion's entry is the first run in it, its exit the first run outside it (an ancestor's, or the output's end).
// Only a unit with pack directives and exact runs is walked; a range of no include location (the main file) is none's.
static bool check_input_pack_includes(CheckInputReader* reader, CheckInputPackPragmas* history)
{
    const Preprocessor* pp = reader->pp;
    const uint32_t runs = pp->system_runs_inexact ? 0 : pp->system_run_count;
    const uint64_t mark = reader->scratch->length;
    // The open inclusions: their ranges and entries, at most one per range.
    uint32_t* stack = runs != 0 ? arena_push_array(reader->scratch, (uint64_t)pp->range_count + 1, sizeof(uint32_t), 4) : 0;
    uint32_t* entries = runs != 0 ? arena_push_array(reader->scratch, (uint64_t)pp->range_count + 1, sizeof(uint32_t), 4) : 0;
    CheckPackInclude* found = runs != 0 ? arena_push_array(reader->arena, pp->range_count, sizeof(CheckPackInclude), _Alignof(CheckPackInclude)) : 0;
    bool valid = runs == 0 || (stack != 0 && entries != 0 && found != 0);
    reader->output->resource |= !valid;
    uint32_t depth = 0;
    uint32_t count = 0;
    for (uint32_t r = 0; valid && runs != 0 && r <= runs; r += 1)
    {
        const uint32_t range = r < runs ? pp->system_runs[r].range : BUSTER_PP_NONE;
        const uint32_t position = r < runs ? pp->system_runs[r].token : pp->output.count;
        // A run of scratch spellings (a chunk of no file) is no inclusion's.
        if (range < pp->range_count && pp->ranges[range].file == BUSTER_PP_NONE)
        {
            continue;
        }
        // Leave the open inclusions that aren't the run's range or one of its ancestors.
        bool ancestor = false;
        while (depth != 0 && !ancestor)
        {
            for (uint32_t at = range; !ancestor && at < pp->range_count; at = pp->ranges[at].parent)
            {
                ancestor = at == stack[depth - 1];
            }
            if (!ancestor)
            {
                depth -= 1;
                const uint32_t left = stack[depth];
                uint32_t entry_current = BUSTER_CHECK_NONE;
                uint32_t exit_current = BUSTER_CHECK_NONE;
                const uint32_t before = check_input_pack_value_at(history, entries[depth], &entry_current);
                const uint32_t after = check_input_pack_value_at(history, position, &exit_current);
                const uint32_t parent = pp->ranges[left].parent;
                if (before != after && parent < pp->range_count && pp->ranges[left].include_location != BUSTER_PP_NONE && count < pp->range_count)
                {
                    found[count++] = (CheckPackInclude){
                        .location = check_input_pack_include_name(&pp->ranges[parent], pp->ranges[left].include_location),
                        .exit = position,
                        .note = exit_current != BUSTER_CHECK_NONE ? history->records[exit_current].source_begin + 2 : BUSTER_CHECK_PACK_UNLOCATED_NOTE,
                    };
                }
            }
        }
        // Enter the run's range and the ancestors of it not open yet, outermost first.
        const uint32_t base = depth;
        for (uint32_t at = range; at < pp->range_count && (base == 0 || at != stack[base - 1]) && depth <= pp->range_count; at = pp->ranges[at].parent)
        {
            stack[depth] = at;
            entries[depth] = position;
            depth += 1;
        }
        for (uint32_t low = base, high = depth; low + 1 < high; low += 1, high -= 1)
        {
            const uint32_t swapped = stack[low];
            stack[low] = stack[high - 1];
            stack[high - 1] = swapped;
        }
    }
    history->includes = count != 0 ? found : NULL;
    history->include_count = count;
    reader->scratch->length = mark;
    return valid;
}

// The warnings of `#pragma pack`, `#pragma options align` and `#pragma align` (#1849), appended to the lexical pragma
// facts after the unknown-pragma collector has published its view, as ms_struct's are: each directive's parser or Sema
// warnings, then each push left open, the innermost first, at the unit's end, the innermost with clang's note where the
// current value is the default (BUSTER_CHECK_PACK_NOTE: the payload's token, past the record that set it, or none).
static bool check_input_pack_align_warnings(CheckInputReader* reader)
{
    const CheckInputPackPragmas* history = reader->output->pack_pragmas;
    const CheckWarningEvents before = reader->output->pragma_warnings;
    CheckWarningBuffer buffer = { .arena = reader->arena, .events = (CheckWarningEvent*)before.events, .count = before.count, .capacity = before.count };
    bool valid = true;
    for (uint32_t i = 0; valid && history != NULL && i < history->count; i += 1)
    {
        const CheckInputPackPragma* record = history->records + i;
        const uint32_t at = record->source_begin + 2;
        if (record->handled == 1 && record->align && record->align_warning != CHECK_PACK_ALIGN_NONE)
        {
            valid = check_input_pack_event(&buffer, record, i, CHECK_WARNING_ID_IGNORED_PRAGMA, CHECK_PACK_MESSAGE_ALIGN, record->diagnostic_source, false, 0);
        }
        else if (record->handled == 1 && !record->align && record->syntax_warning != CHECK_PACK_MESSAGE_ALIGN)
        {
            valid = check_input_pack_event(&buffer, record, i, CHECK_WARNING_ID_IGNORED_PRAGMA, record->syntax_warning, record->diagnostic_source, false, 0);
        }
        else if (record->handled == 1 && !record->align)
        {
            const uint8_t warnings = record->warnings;
            valid = (warnings & CHECK_PACK_WARNING_ALIGNMENT) == 0 ||
                    check_input_pack_event(&buffer, record, i, CHECK_WARNING_ID_IGNORED_PRAGMA, CHECK_PACK_MESSAGE_INVALID_ALIGNMENT, at, false, 0);
            valid = valid && ((warnings & CHECK_PACK_WARNING_SHOW) == 0 ||
                              check_input_pack_event(&buffer, record, i, CHECK_WARNING_ID_PRAGMA_PACK_NOTICE, CHECK_PACK_MESSAGE_SHOW, at, false, 0));
            valid = valid && ((warnings & CHECK_PACK_WARNING_NAMED_ALIGNMENT) == 0 ||
                              check_input_pack_event(&buffer, record, i, CHECK_WARNING_ID_PRAGMA_PACK_NOTICE, CHECK_PACK_MESSAGE_NAMED_POP, at, false, 0));
            valid = valid && ((warnings & CHECK_PACK_WARNING_EMPTY) == 0 ||
                              check_input_pack_event(&buffer, record, i, CHECK_WARNING_ID_IGNORED_PRAGMA, CHECK_PACK_MESSAGE_EMPTY_POP, at, false, 0));
        }
    }
    valid = valid && (history == NULL || check_input_pack_includes(reader, reader->output->pack_pragmas));
    for (uint32_t k = 0; valid && history != NULL && k < history->include_count; k += 1)
    {
        // The event's record is the unit's first, for the event's view; its payload's token is the inclusion's index.
        const CheckInputPackPragma* first = history->records;
        const CheckWarningEvent event = {
            .id = CHECK_WARNING_ID_PRAGMA_PACK,
            .kind = CHECK_WARNING_EVENT_PRAGMA,
            .flags = BUSTER_CHECK_WARNING_PACK_PRAGMA,
            .token = first->token,
            .source = first->source_begin,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = first->token,
            .payload = { .name = CHECK_PACK_MESSAGE_INCLUDE, .token = k, .data = 1 },
        };
        valid = check_warning_append(&buffer, &event, NULL, 0);
    }
    const bool reset = history != NULL && history->final_log2 == 0 && !history->final_custom;
    bool innermost = true;
    for (uint32_t i = history != NULL ? history->count : 0; valid && i != 0; i -= 1)
    {
        const CheckInputPackPragma* record = history->records + i - 1;
        if (record->unterminated)
        {
            const uint32_t note = !innermost || !reset ? BUSTER_CHECK_PACK_NO_NOTE :
                                  history->current != BUSTER_CHECK_NONE ? history->records[history->current].source_begin + 2 :
                                                                          BUSTER_CHECK_PACK_UNLOCATED_NOTE;
            valid = check_input_pack_event(&buffer, record, i - 1, CHECK_WARNING_ID_PRAGMA_PACK, CHECK_PACK_MESSAGE_UNTERMINATED, record->source_begin + 2, true, note);
            innermost = false;
        }
    }
    if (valid)
    {
        reader->output->pragma_warnings = check_warning_view(&buffer);
    }
    reader->output->resource |= !valid;
    return valid;
}
