// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Cold spelling of the common constraint failures (#1107). The driver and corpus use the same renderer;
// the typed graph and retained declaration/case facts decide the error, not a second semantic pass. A changed
// caret never changes the report's completion/publication key. Text belongs to the caller's scratch arena.

// A semantic report's notes, in the caller's array of this many (check_semantic_expression); the records a const
// member's search visits (check_semantic_const_fields).
#define BUSTER_CHECK_SEMANTIC_NOTES 32
#define BUSTER_CHECK_SEMANTIC_RECORDS 64

// The count of a report's notes: those before the first without a message.
static uint32_t check_semantic_note_count(const PPOutputDiagnosticNote* notes)
{
    uint32_t count = 0;
    while (count < BUSTER_CHECK_SEMANTIC_NOTES && notes[count].message != NULL)
    {
        count += 1;
    }
    return count;
}

// The first token of `node`'s subtree, whose tokens are contiguous before it: the tokens before it whose parents reach it
// (an error's path alone asks).
static uint32_t check_semantic_first_token(const Parser* parser, uint32_t node)
{
    const uint32_t count = parser->tokens.count;
    uint32_t first = node;
    bool inside = true;
    while (inside && first != 0)
    {
        uint32_t ancestor = first - 1;
        for (uint32_t steps = 0; ancestor < count && ancestor < node && steps < count; steps += 1)
        {
            ancestor = parser->tree.parents[ancestor];
        }
        inside = ancestor == node;
        first -= inside ? 1u : 0u;
    }
    return first;
}

// Whether parser token `token`'s parents reach `node` (a token is its own).
static bool check_semantic_inside(const Parser* parser, uint32_t token, uint32_t node)
{
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = token;
    for (uint32_t steps = 0; ancestor < count && ancestor != node && steps < count; steps += 1)
    {
        const uint32_t up = parser->tree.parents[ancestor];
        ancestor = up != ancestor ? up : count;
    }
    return ancestor == node;
}

// Adds the parser tokens of `node`'s subtree, which are contiguous around it, as a highlighted range of the next
// diagnostic (#2376): clang's source range of the expression the node is (getSourceRange), with `parentheses` the ones
// around it, which clang's ParenExpr keeps where a diagnostic doesn't look through them (IgnoreParens).
static PPDiagnosticRange check_semantic_token_range(const Parser* parser, uint32_t node, bool parentheses)
{
    const uint32_t count = parser->tokens.count;
    PPDiagnosticRange result = { 0 };
    if (node < count)
    {
        uint32_t first = node;
        uint32_t last = node;
        while (first != 0 && check_semantic_inside(parser, first - 1, node))
        {
            first -= 1;
        }
        while (last + 1 < count && check_semantic_inside(parser, last + 1, node))
        {
            last += 1;
        }
        // The parentheses around it, which the graph leaves out and clang's ParenExpr keeps: a group's opener and closer
        // just outside it, one level outside its shallowest token.
        bool grouped = parentheses;
        while (grouped && first != 0 && last + 1 < count)
        {
            uint32_t shallowest = UINT32_MAX;
            for (uint32_t at = first; at <= last; at += 1)
            {
                shallowest = parser->tree.depths[at] < shallowest ? parser->tree.depths[at] : shallowest;
            }
            grouped = parser->tree.roles[first - 1] == PARSE_ROLE_OPEN_GROUP && parser->tree.roles[last + 1] == PARSE_ROLE_CLOSE_GROUP &&
                      parser->tree.depths[first - 1] + 1 == shallowest && parser->tree.depths[last + 1] + 1 == shallowest;
            first -= grouped ? 1 : 0;
            last += grouped ? 1 : 0;
        }
        result = (PPDiagnosticRange)
        {
            .begin = parse_source_index(&parser->tokens, first),
            .end = parse_source_index(&parser->tokens, last),
            .pending = PP_RANGE_TOKENS,
        };
    }
    return result;
}

static void check_semantic_range(const Parser* parser, Preprocessor* pp, uint32_t node, bool parentheses)
{
    const PPDiagnosticRange range = check_semantic_token_range(parser, node, parentheses);
    if (range.pending == PP_RANGE_TOKENS)
    {
        pp_diagnostic_range_tokens(pp, range.begin, range.end);
    }
}

static const char* check_semantic_text(Preprocessor* pp, const char* text)
{
    // Do not quietly replace a failed contextual rendering with a successful generic diagnostic.
    pp->diagnostic_collection_failed |= text == NULL;
    return text != NULL ? text : "checker diagnostic capacity exhausted";
}

static String8 check_semantic_name(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    return token < parser->tokens.count ? check_driver_warning_name(pp, parser->tokens.names[token]) : (String8){ 0 };
}

// Only failures need the enclosing function's name. The reader's body-to-declaration column is authoritative;
// walking this cold column avoids retaining another name at every return or redoing lexical lookup.
static uint32_t check_semantic_function(const Parser* parser, const CheckInput* input, uint32_t token)
{
    uint32_t result = BUSTER_CHECK_NONE;
    const uint32_t count = input->token_count;
    for (uint32_t body = 0; body < token && body < count; body += 1)
    {
        const uint32_t declaration = check_sparse_get(input->body_declarations, body);
        if (declaration < count && token < parser->tree.parents[body])
        {
            result = declaration;
        }
    }
    return result;
}

// Whether the unnamed record at `keyword` is a record's anonymous member, a definition with no declarator: clang's
// "anonymous" one, any other unnamed one's "unnamed".
static bool check_semantic_anonymous_member(const Parser* parser, uint32_t keyword)
{
    const uint32_t body = check_input_skip_attributes(parser, keyword + 1, parser->tokens.count);
    const uint32_t close = body < parser->tokens.count && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE ? parser->tree.parents[body] : parser->tokens.count;
    const uint32_t after = close < parser->tokens.count && close > body ? check_input_skip_attributes(parser, close + 1, parser->tokens.count) : parser->tokens.count;
    return after < parser->tokens.count && parser->tokens.kinds[after] == PARSE_KIND_SEMI;
}

// The record at `token` as clang's C23 comparison names it (#1447): at a named record's name, `struct S`; at an unnamed
// record's keyword, its enclosing records' names outward in, each unnamed one `(unnamed struct)`, then its own place,
// `struct S::(unnamed struct)::(unnamed at file:line:col)`. The enclosing records are the record bodies (`{` of a struct's
// or a union's) open at the token, found by a backward scan that skips each closed bracket's pair.
static String8 check_semantic_tag_spelling(const Parser* parser, Preprocessor* pp, uint32_t token, Arena* scratch)
{
    // (An unnamed enumeration's keyword too, #2482: `enum (unnamed at f.c:1:2)`.)
    const bool unnamed = parser->tokens.kinds[token] == PARSE_KIND_STRUCT || parser->tokens.kinds[token] == PARSE_KIND_ENUM;
    uint32_t keyword = token;
    for (uint32_t steps = 0; !unnamed && keyword != 0 && steps < 64 && parser->tokens.kinds[keyword] != PARSE_KIND_STRUCT && parser->tokens.kinds[keyword] != PARSE_KIND_ENUM;
         steps += 1)
    {
        keyword -= 1;
    }
    const String8 word = check_semantic_name(parser, pp, keyword);
    const char* text = check_warning_text(scratch, "%.*s ", (int)word.length, word.pointer);
    // The enclosing records outward, their keywords, then written inward: each `{` of a record's body that no `}` after
    // it closes before the token.
    uint32_t enclosing[16];
    uint32_t depth = 0;
    uint32_t closed = 0;
    for (uint32_t t = token; unnamed && t != 0 && depth < BUSTER_ARRAY_LENGTH(enclosing);)
    {
        t -= 1;
        const uint8_t kind = parser->tokens.kinds[t];
        closed += kind == PARSE_KIND_R_BRACE ? 1u : 0u;
        if (kind == PARSE_KIND_L_BRACE && closed != 0)
        {
            closed -= 1;
        }
        else if (kind == PARSE_KIND_L_BRACE && parser->tree.roles[t] == PARSE_ROLE_OPEN_STRUCT)
        {
            uint32_t record = t;
            for (uint32_t steps = 0; record != 0 && steps < 256 && parser->tokens.kinds[record] != PARSE_KIND_STRUCT; steps += 1)
            {
                record -= 1;
                record = parser->tokens.kinds[record] == PARSE_KIND_R_PAREN && parser->tree.parents[record] < record ? parser->tree.parents[record] : record;
            }
            enclosing[depth++] = record;
        }
        else if (kind == PARSE_KIND_L_BRACE)
        {
            // A body or a block that isn't a record's: no record encloses past it.
            t = 0;
        }
    }
    for (uint32_t level = depth; text != NULL && level-- > 0;)
    {
        // A record's name is the identifier after its keyword, past its attributes, before its body.
        const uint32_t after = check_input_skip_attributes(parser, enclosing[level] + 1, parser->tokens.count);
        const bool named = after < parser->tokens.count && parser->tokens.kinds[after] == PARSE_KIND_IDENTIFIER;
        const String8 part = named ? check_semantic_name(parser, pp, after) : check_semantic_name(parser, pp, enclosing[level]);
        text = check_warning_text(scratch, named ? "%s%.*s::" : check_semantic_anonymous_member(parser, enclosing[level]) ? "%s(anonymous %.*s)::" :
                                                                                                                   "%s(unnamed %.*s)::",
                                  text, (int)part.length, part.pointer);
    }
    if (text != NULL && unnamed)
    {
        const bool history = pp_history_ensure(pp);
        const uint32_t source = parse_source_index(&parser->tokens, token);
        const uint32_t origin = history ? pp_history_at(pp, source) : 0;
        const uint32_t location = source < pp->output.count ? pp_expansion_location(pp, source, origin) : 0;
        const char* file = NULL;
        uint32_t file_length = 0;
        const uint32_t line = pp_presumed(pp, location, &file, &file_length);
        const uint32_t range = pp_find_range(pp, location);
        uint32_t column = 1;
        if (range != BUSTER_PP_NONE)
        {
            (void)pp_line_of(pp, range, location, &column);
        }
        text = check_warning_text(scratch, "%s(%s at %.*s:%u:%u)", text, check_semantic_anonymous_member(parser, token) ? "anonymous" : "unnamed",
                                  (int)file_length, file, line, column);
    }
    else if (text != NULL)
    {
        const String8 own = check_semantic_name(parser, pp, token);
        text = own.length != 0 ? check_warning_text(scratch, "%s%.*s", text, (int)own.length, own.pointer) : NULL;
    }
    return text != NULL ? (String8){ .pointer = (char*)text, .length = (uint64_t)strlen(text) } : (String8){ 0 };
}

// C23's incompatible redefinition's two notes (#1447, CHECK_INPUT_TAG_INCOMPATIBLE): at the first difference's member of
// the second definition then the earlier's, or at a member then "no corresponding field" at the other definition's name,
// as clang words them. How many there are.
static uint32_t check_semantic_tag_notes(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                         CheckWarningSpelling* spell, const CheckInputReport* report, PPOutputDiagnosticNote* notes, Arena* scratch)
{
    const uint32_t count = input->token_count;
    const uint32_t first = report->attribute;
    const uint32_t second = report->note_token;
    // An unnamed member's note is at its declaration's first token, and names nothing (BUSTER_CHECK_TAG_*_UNNAMED).
    const bool first_unnamed = (report->bit_widths & 0x8000u) != 0;
    const bool second_unnamed = (report->bit_widths & 0x80000000u) != 0;
    const String8 first_name = first < count && !first_unnamed ? check_semantic_name(parser, pp, first) : (String8){ "", 0 };
    const String8 second_name = second < count && !second_unnamed ? check_semantic_name(parser, pp, second) : (String8){ "", 0 };
    const char* texts[2] = { NULL, NULL };
    switch (report->detail)
    {
        case 0: // CHECK_TAG_MISMATCH_NAME
            texts[0] = check_warning_text(scratch, "field has name '%.*s' here", (int)first_name.length, first_name.pointer);
            texts[1] = check_warning_text(scratch, "field has name '%.*s' here", (int)second_name.length, second_name.pointer);
            break;
        case 1: // CHECK_TAG_MISMATCH_BIT_FIELD
        {
            const uint32_t widths[2] = { (report->bit_widths >> 16) & 0x7fffu, report->bit_widths & 0x7fffu };
            const String8 names[2] = { first_name, second_name };
            for (uint32_t n = 0; n < 2; n += 1)
            {
                texts[n] = widths[n] == 0 ? check_warning_text(scratch, "field '%.*s' is not a bit-field", (int)names[n].length, names[n].pointer) :
                                            check_warning_text(scratch, "bit-field '%.*s' has bit-width %u here", (int)names[n].length, names[n].pointer, widths[n] - 1);
            }
        } break;
        case 2: // CHECK_TAG_MISMATCH_TYPE
        {
            const String8 first_type = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type,
                                                                 first < count && !first_unnamed ? check_input_typedef_spelling(input, parser, first) : 0);
            const String8 second_type = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value,
                                                                  second < count && !second_unnamed ? check_input_typedef_spelling(input, parser, second) : 0);
            texts[0] = check_warning_text(scratch, "field '%.*s' has type %.*s here", (int)first_name.length, first_name.pointer, (int)first_type.length, first_type.pointer);
            texts[1] = check_warning_text(scratch, "field '%.*s' has type %.*s here", (int)second_name.length, second_name.pointer, (int)second_type.length, second_type.pointer);
        } break;
        case 5: // CHECK_TAG_MISMATCH_ATTRIBUTE
        case 6: // CHECK_TAG_MISMATCH_MEMBER_ATTRIBUTE: each definition's attribute where the lists differ, the earlier's first
            texts[0] = check_warning_text(scratch, "attribute '%.*s' here", (int)first_name.length, first_name.pointer);
            texts[1] = check_warning_text(scratch, "attribute '%.*s' here", (int)second_name.length, second_name.pointer);
            break;
        case 7: // BUSTER_CHECK_TAG_ENUM_VALUE: an enum's (#1447), the second definition's enumerator then the earlier's
        case 8: // BUSTER_CHECK_TAG_ENUM_EXTRA: one past the other's list, and "no corresponding enumerator here"
        {
            const uint64_t values[2] = { report->value, (uint64_t)report->type | (uint64_t)report->type_spelling << 32 };
            const String8 names[2] = { first_name, second_name };
            const uint32_t tokens[2] = { first, second };
            for (uint32_t n = 0; n < (report->detail == 7 ? 2u : 1u); n += 1)
            {
                // Printed as its type's signedness has it, as clang's APSInt is.
                const TypeId type = tokens[n] < count && input->declared_types != NULL ? input->declared_types[tokens[n]] : BUSTER_TYPE_INT;
                uint32_t width = 64;
                bool sign = true;
                if (!check_action_integer_type(types, type, &width, &sign) || width > 64)
                {
                    width = 64;
                }
                const uint64_t bits = width < 64 ? values[n] & ((UINT64_C(1) << width) - 1) : values[n];
                const int64_t signed_value = sign && width < 64 && (bits >> (width - 1)) != 0 ? (int64_t)(bits | ~((UINT64_C(1) << width) - 1)) : (int64_t)bits;
                texts[n] = sign ? check_warning_text(scratch, "enumerator '%.*s' with value %lld here", (int)names[n].length, names[n].pointer, (long long)signed_value) :
                                  check_warning_text(scratch, "enumerator '%.*s' with value %llu here", (int)names[n].length, names[n].pointer, (unsigned long long)bits);
            }
            texts[1] = report->detail == 7 ? texts[1] : "no corresponding enumerator here";
        } break;
        case 9: // CHECK_TAG_MISMATCH_KIND (#1826): the earlier unnamed record, its kind
            texts[0] = check_warning_text(scratch, "'%.*s' is a %s here", (int)first_name.length, first_name.pointer,
                                          report->value != 0 ? "union" : "struct");
            break;
        default: // CHECK_TAG_MISMATCH_EXTRA_NEW, CHECK_TAG_MISMATCH_EXTRA_OLD
        {
            const String8 first_type = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type,
                                                                 first < count && !first_unnamed ? check_input_typedef_spelling(input, parser, first) : 0);
            texts[0] = check_warning_text(scratch, "field '%.*s' has type %.*s here", (int)first_name.length, first_name.pointer, (int)first_type.length, first_type.pointer);
            texts[1] = "no corresponding field here";
        } break;
    }
    uint32_t result = 0;
    const uint32_t tokens[2] = { first, second };
    for (uint32_t n = 0; n < 2; n += 1)
    {
        if (tokens[n] < count && texts[n] != NULL)
        {
            notes[result] = (PPOutputDiagnosticNote){ .token = parse_source_index(&parser->tokens, tokens[n]), .message = texts[n] };
            result += 1;
        }
    }
    return result;
}

// The typedef that spells an enum's fixed underlying type after its name `token` (`enum E : my_int`), for clang's words
// of it, or 0 for a base of no typedef (or no base). Bounded by the base's tokens, to the body's `{` or the `;`.
static uint32_t check_semantic_enum_base_spelling(const Parser* parser, const CheckInput* input, uint32_t token)
{
    uint32_t result = 0;
    const bool based = token + 1 < parser->tokens.count && parser->tokens.kinds[token + 1] == PARSE_KIND_COLON;
    for (uint32_t t = token + 2; based && result == 0 && t < parser->tokens.count && t < input->token_count &&
                                 parser->tokens.kinds[t] != PARSE_KIND_L_BRACE && parser->tokens.kinds[t] != PARSE_KIND_SEMI; t += 1)
    {
        const uint32_t resolved = parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && input->declarations != NULL ? input->declarations[t] : 0;
        result = resolved != 0 && resolved < input->token_count && input->declaration_kinds[resolved] == C_BINDING_TYPEDEF ? resolved : 0;
    }
    return result;
}

// An __auto_type redeclaration's type as clang's TypePrinter prints it before deduction (#1844,
// CHECK_REDECLARATION_AUTO_TYPE), quoted: the placeholder with its qualifiers (`const __auto_type`, `_Atomic(__auto_type)`,
// `restrict __auto_type`), then each pointer level's `*` and its qualifiers (`__auto_type *const`, `const __auto_type **`).
static String8 check_semantic_auto_type_spelling(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                                CheckWarningSpelling* spell, TypeId type, uint32_t declaration, Arena* scratch)
{
    TypeId aliases[] =
    {
        BUSTER_CHECK_AUTO_TYPE,
        BUSTER_CHECK_AUTO_TYPE_ATOMIC,
        BUSTER_CHECK_AUTO_TYPE_RESTRICT,
        0,
    };
    String8 names[] =
    {
        {
            .pointer = "__auto_type",
            .length = 11,
        },
        {
            .pointer = "_Atomic(__auto_type)",
            .length = 20,
        },
        {
            .pointer = "restrict __auto_type",
            .length = 20,
        },
        { 0 },
    };
    String8 result = { 0 };
    if (check_driver_spelling_ready(parser, pp, input, types, spell, scratch))
    {
        // Reuse the type printer's declarator and extended-qualifier rules; aliases hide only the private markers.
        // In particular, a pointer's address space must stay on that pointer rather than its deduced pointee.
        CheckDriverSpaceScope saved = { 0 };
        check_driver_space_scope(parser, pp, input, types, spell, declaration, &saved, scratch);
        TypeNames spelling =
        {
            .alias_types = aliases,
            .alias_names = names,
            .alias_count = BUSTER_ARRAY_LENGTH(aliases) - 1,
            .space_macro_spaces = spell->space_macro_spaces,
            .space_macro_texts = spell->space_macro_texts,
            .space_macro_count = spell->space_macro_count,
        };
        TypeId level = type;
        for (uint32_t steps = 0; !check_input_auto_type(types, level) && type_kind(types, type_strip_aligned(types, level)) == BUSTER_TYPE_KIND_POINTER && steps < 64; steps += 1)
        {
            level = type_element(types, type_strip_aligned(types, level));
        }
        const TypeId bare = type_strip_aligned(types, level);
        const TypeId marker = bare & ~(TypeId)(BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE);
        const uint32_t space = type_address_space(type_extended_qualifiers(types, level));
        uint32_t macro = spell->space_macro_count;
        bool spelled = true;
        for (uint32_t i = 0; check_input_auto_type(types, level) && macro == spell->space_macro_count && i < spell->space_macro_count; i += 1)
        {
            macro = spell->space_macro_spaces[i] == space ? i : macro;
        }
        if (macro < spell->space_macro_count)
        {
            // A macro's specifier-level placeholder puts cv after itself; an _Atomic specifier rebuilds it without
            // the macro's sugar. A trailing attribute instead wraps the already-qualified placeholder. Keep these
            // rules local to the private marker aliases, without changing how real typedef aliases print.
            uint32_t begin = 0;
            uint32_t end = 0;
            if (declaration < parser->tokens.count)
            {
                check_driver_place_extent(parser, declaration, &begin, &end);
            }
            bool trailing = false;
            for (uint32_t t = declaration < end ? declaration + 1 : end; t < end; t += 1)
            {
                trailing |= check_driver_space_name(parser, pp, t);
            }
            TypeNames plain = spelling;
            plain.space_macro_count = 0;
            const bool atomic = marker == BUSTER_CHECK_AUTO_TYPE_ATOMIC;
            const String8 inner = type_print(types, scratch, atomic && !trailing ? level : trailing ? bare : marker, (String8){ 0 }, &plain, 0);
            const String8 word = spell->space_macro_texts[macro];
            const char* text = atomic && !trailing ? check_warning_text(scratch, "%.*s", (int)inner.length, inner.pointer) :
                                check_warning_text(scratch, "%.*s %.*s%s%s", (int)word.length, word.pointer, (int)inner.length, inner.pointer,
                                                   !trailing && (bare & BUSTER_TYPE_CONST) != 0 ? " const" : "",
                                                   !trailing && (bare & BUSTER_TYPE_VOLATILE) != 0 ? " volatile" : "");
            aliases[3] = type_strip_alignment(types, level);
            names[3] = text != NULL ? (String8){ .pointer = (char*)text, .length = strlen(text) } : (String8){ 0 };
            spelled = inner.length != 0 && text != NULL;
            spelling.alias_count += 1;
        }
        const String8 text = spelled ? type_print(types, scratch, type, (String8){ 0 }, &spelling, 0) : (String8){ 0 };
        const char* quoted = text.length != 0 ? check_warning_text(scratch, "'%.*s'", (int)text.length, text.pointer) : NULL;
        result = quoted != NULL ? (String8){ .pointer = (char*)quoted, .length = strlen(quoted) } : (String8){ 0 };
        check_driver_space_restore(spell, &saved);
    }
    return result;
}

static const char* check_semantic_input(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                       const CheckInputReport* report, CheckWarningSpelling* spell, Arena* scratch)
{
    const uint32_t token = report->token;
    const uint32_t count = input->token_count;
    const String8 name = check_semantic_name(parser, pp, token);
    const char* result = check_driver_input_report_message(input, report);
    if (report->message == CHECK_INPUT_BUILTIN_ARGUMENT_COUNT)
    {
        const uint32_t expected = report->detail & 0xffffu;
        const uint32_t have = report->detail >> 16;
        result = check_warning_text(scratch, "too %s arguments to function call, expected %u, have %u", have < expected ? "few" : "many", expected, have);
    }
    else if (report->message == CHECK_INPUT_BIT_FIELD)
    {
        const bool named = report->subject != 0 && report->subject - 1 < count;
        const String8 field = named ? check_semantic_name(parser, pp, report->subject - 1) : (String8){ 0 };
        const char* subject = named ? check_warning_text(scratch, "bit-field '%.*s'", (int)field.length, field.pointer) : "anonymous bit-field";
        if (subject == NULL)
        {
            result = NULL;
        }
        else if (report->detail == CHECK_INPUT_BIT_FIELD_TYPE)
        {
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
            result = type.length == 0 ? NULL : check_warning_text(scratch, "%s has non-integral type %.*s", subject, (int)type.length, type.pointer);
        }
        else if (report->detail == CHECK_INPUT_BIT_FIELD_NEGATIVE)
        {
            result = check_warning_text(scratch, "%s has negative width (%lld)", subject, (long long)(int64_t)report->value);
        }
        else if (report->detail == CHECK_INPUT_BIT_FIELD_TOO_WIDE)
        {
            result = check_warning_text(scratch, "%s is too wide (%llu bits)", subject, (unsigned long long)report->value);
        }
        else if (report->detail == CHECK_INPUT_BIT_FIELD_WIDE)
        {
            const uint64_t width = type_integer_width(types, report->type);
            result = check_warning_text(scratch, "width of %s (%llu bits) exceeds the width of its type (%llu %s)", subject,
                                        (unsigned long long)report->value, (unsigned long long)width, width == 1 ? "bit" : "bits");
        }
        else
        {
            result = check_warning_text(scratch, "named %s has zero width", subject);
        }
    }
    else if (report->message == CHECK_INPUT_FUNCTION_MEMBER)
    {
        result = check_warning_text(scratch, "field '%.*s' declared as a function", (int)name.length, name.pointer);
    }
    else if (report->message == CHECK_INPUT_RETURN)
    {
        const uint32_t function = check_semantic_function(parser, input, token);
        const String8 function_name = check_semantic_name(parser, pp, function);
        if (function_name.length != 0)
        {
            result = check_warning_text(scratch, "non-void function '%.*s' should return a value [-Wreturn-mismatch]",
                                        (int)function_name.length, function_name.pointer);
        }
    }
    else if (report->message == CHECK_INPUT_TAG_INCOMPATIBLE && token > 0 && token < parser->tokens.count)
    {
        // C23's incompatible redefinition of a record (#1447): its keyword, struct or union, and its name; or an unnamed
        // record's in a member's type, at its keyword, named as clang names it there (check_semantic_tag_spelling).
        const String8 spelled = check_semantic_tag_spelling(parser, pp, token, scratch);
        const char* what = report->detail == 5 ? "has an attribute which currently causes the types to be treated as though they are incompatible [-Wodr]" :
                           report->detail == 6 ? "has a member with an attribute which currently causes the types to be treated as though they are incompatible [-Wodr]" :
                                                 "has incompatible definitions";
        result = spelled.length == 0 ? NULL : check_warning_text(scratch, "type '%.*s' %s", (int)spelled.length, spelled.pointer, what);
    }
    else if (report->message == CHECK_INPUT_TYPE_SPECIFIER_MISSING && report->detail == 3 && name.length != 0)
    {
        result = check_warning_text(scratch, "unknown type name '%.*s'", (int)name.length, name.pointer);
    }
    else if (report->message == CHECK_INPUT_ENUMERATOR_REDEFINITION && name.length != 0)
    {
        result = check_warning_text(scratch, "redefinition of enumerator '%.*s'", (int)name.length, name.pointer);
    }
    else if (report->message == CHECK_INPUT_AMBIGUOUS && name.length != 0)
    {
        result = check_warning_text(scratch, "reference to '%.*s' is ambiguous", (int)name.length, name.pointer);
    }
    else if (report->message == CHECK_INPUT_ARRAY_LITERAL_MISMATCH)
    {
        // GNU's array initialized by a compound literal of another array type (#1447): both types as written.
        const String8 array = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
        const String8 literal = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, (uint32_t)(report->value >> 32));
        result = array.length == 0 || literal.length == 0 ? NULL : check_warning_text(scratch, "cannot initialize array of type %.*s with array of type %.*s",
                                                                                     (int)array.length, array.pointer, (int)literal.length, literal.pointer);
    }
    else if (report->message == CHECK_INPUT_RESTRICT_NOT_POINTER && report->type != 0)
    {
        const String8 type = check_semantic_auto_type_spelling(parser, pp, input, types, spell, report->type, BUSTER_CHECK_NONE, scratch);
        result = type.length == 0 ? NULL : check_warning_text(scratch, "restrict requires a pointer or reference (%.*s is invalid)", (int)type.length, type.pointer);
    }
    else if (report->message == CHECK_INPUT_GENERIC_NO_MATCH && report->type != 0)
    {
        const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
        result = type.length == 0 ? NULL : check_warning_text(scratch, "controlling expression type %.*s not compatible with any generic association type",
                                                              (int)type.length, type.pointer);
    }
    else if (report->message == CHECK_INPUT_GENERIC_COMPATIBLE && report->type != 0 && report->value != 0)
    {
        const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
        const String8 earlier = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, report->detail);
        result = type.length == 0 || earlier.length == 0 ? NULL :
                 check_warning_text(scratch, "type %.*s in generic association compatible with previously specified type %.*s", (int)type.length, type.pointer,
                                    (int)earlier.length, earlier.pointer);
    }
    else if (report->message == CHECK_INPUT_AUTO_TYPE_NULLABILITY && report->type != 0 && name.length != 0)
    {
        const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
        result = type.length == 0 ? NULL : check_warning_text(scratch, "nullability specifier '%.*s' cannot be applied to non-pointer type %.*s",
                                                              (int)name.length, name.pointer, (int)type.length, type.pointer);
    }
    else if (report->message == CHECK_INPUT_AUTO_ATOMIC_POINTER && report->type != 0 && name.length != 0)
    {
        const String8 type = check_semantic_auto_type_spelling(parser, pp, input, types, spell, report->type, token, scratch);
        const String8 initializer = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, (uint32_t)(report->value >> 32));
        result = type.length == 0 || initializer.length == 0 ? NULL : check_warning_text(scratch,
            "variable '%.*s' with type %.*s has incompatible initializer of type %.*s", (int)name.length, name.pointer,
            (int)type.length, type.pointer, (int)initializer.length, initializer.pointer);
    }
    else if ((report->message == CHECK_INPUT_AUTO_TYPE_MISMATCH || report->message == CHECK_INPUT_AUTO_DEDUCED_MISMATCH) && report->type != 0 &&
             report->subject != 0 && report->subject - 1 < count && report->detail < count)
    {
        // Two declarators' deductions that differ (#1844), each type as its initializer spells it, each declarator named.
        const String8 first = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
        const String8 second = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, (uint32_t)(report->value >> 32));
        const String8 first_name = check_semantic_name(parser, pp, report->subject - 1);
        const String8 second_name = check_semantic_name(parser, pp, report->detail);
        result = first.length == 0 || second.length == 0 || first_name.length == 0 || second_name.length == 0 ? NULL :
                 check_warning_text(scratch, "'%s' deduced as %.*s in declaration of '%.*s' and deduced as %.*s in declaration of '%.*s'",
                                    report->message == CHECK_INPUT_AUTO_TYPE_MISMATCH ? "__auto_type" : "auto", (int)first.length, first.pointer,
                                    (int)first_name.length, first_name.pointer, (int)second.length, second.pointer, (int)second_name.length,
                                    second_name.pointer);
    }
    else if (report->message == CHECK_INPUT_AUTO_TYPE_INITIALIZER && report->type != 0 && name.length != 0)
    {
        const String8 type = check_semantic_auto_type_spelling(parser, pp, input, types, spell, report->type, token, scratch);
        result = type.length == 0 ? NULL : check_warning_text(scratch, "declaration of variable '%.*s' with deduced type %.*s requires an initializer",
                                                              (int)name.length, name.pointer, (int)type.length, type.pointer);
    }
    else if ((report->message == CHECK_INPUT_REDECLARATION || report->message == CHECK_INPUT_CONFLICTING_TYPES) && token < count && name.length != 0)
    {
        const uint32_t previous = report->subject - 1;
        const uint8_t kind = input->declaration_kinds[token];
        if (report->detail == CHECK_REDECLARATION_OBJECT_TYPE || report->detail == CHECK_REDECLARATION_OBJECT_TYPE_EXTERN ||
            report->detail == CHECK_REDECLARATION_AUTO_TYPE || report->detail == CHECK_REDECLARATION_AUTO_TYPE_EXTERN)
        {
            const bool placeholder = report->detail == CHECK_REDECLARATION_AUTO_TYPE || report->detail == CHECK_REDECLARATION_AUTO_TYPE_EXTERN;
            const String8 current = placeholder ? check_semantic_auto_type_spelling(parser, pp, input, types, spell, report->type, token, scratch) :
                                                  check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
            // The prior type is the noted declaration's, spelled as that one spells it (review on #1453).
            const uint32_t noted = report->note_token < count ? report->note_token : previous;
            const uint32_t prior_spelling = noted < count ? check_input_typedef_spelling(input, parser, noted) : 0;
            const String8 prior = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, prior_spelling);
            result = current.length == 0 || prior.length == 0 ? NULL : check_warning_text(scratch,
                "%s of '%.*s' with a different type: %.*s vs %.*s",
                report->detail == CHECK_REDECLARATION_OBJECT_TYPE_EXTERN || report->detail == CHECK_REDECLARATION_AUTO_TYPE_EXTERN ? "redeclaration" :
                                                                                                                                 "redefinition",
                (int)name.length, name.pointer,
                (int)current.length, current.pointer, (int)prior.length, prior.pointer);
        }
        else if (report->detail == CHECK_REDECLARATION_TYPEDEF)
        {
            const String8 current = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
            const uint32_t noted = report->note_token < count ? report->note_token : previous;
            const uint32_t prior_spelling = noted < count ? check_input_typedef_spelling(input, parser, noted) : 0;
            const String8 prior = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, prior_spelling);
            result = current.length == 0 || prior.length == 0 ? NULL : check_warning_text(scratch, "typedef redefinition with different types (%.*s vs %.*s)",
                                                                                         (int)current.length, current.pointer, (int)prior.length, prior.pointer);
        }
        else if (report->detail == CHECK_REDECLARATION_ENUM_BASE)
        {
            // Both bases as written, a typedef's with its "aka": the new one's after this name's `:`, the earlier one's after
            // the noted name's.
            const uint32_t noted = report->note_token < count ? report->note_token : previous;
            const String8 current = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, check_semantic_enum_base_spelling(parser, input, token));
            const String8 prior = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, check_semantic_enum_base_spelling(parser, input, noted));
            result = current.length == 0 || prior.length == 0 ? NULL : check_warning_text(scratch, "enumeration redeclared with different underlying type %.*s (was %.*s)",
                                                                                         (int)current.length, current.pointer, (int)prior.length, prior.pointer);
        }
        else if (report->detail == CHECK_REDECLARATION_ENUM_FIXED || report->detail == CHECK_REDECLARATION_ENUM_NONFIXED)
        {
            result = report->detail == CHECK_REDECLARATION_ENUM_FIXED ? "enumeration previously declared with fixed underlying type" :
                                                                        "enumeration previously declared with nonfixed underlying type";
        }
        else if (report->detail == CHECK_REDECLARATION_KIND)
        {
            result = check_warning_text(scratch, "redefinition of '%.*s' as different kind of symbol", (int)name.length, name.pointer);
        }
        else if (report->detail == CHECK_REDECLARATION_THREAD_LOCAL || report->detail == CHECK_REDECLARATION_NON_THREAD_LOCAL)
        {
            const char* format = report->detail == CHECK_REDECLARATION_THREAD_LOCAL ?
                                 "thread-local declaration of '%.*s' follows non-thread-local declaration" :
                                 "non-thread-local declaration of '%.*s' follows thread-local declaration";
            result = check_warning_text(scratch, format, (int)name.length, name.pointer);
        }
        else if (report->detail == CHECK_REDECLARATION_EXTERN || report->detail == CHECK_REDECLARATION_NON_EXTERN)
        {
            const char* format = report->detail == CHECK_REDECLARATION_EXTERN ? "extern declaration of '%.*s' follows non-extern declaration" :
                                 "non-extern declaration of '%.*s' follows extern declaration";
            result = check_warning_text(scratch, format, (int)name.length, name.pointer);
        }
        else if (report->detail > CHECK_REDECLARATION_GENERIC && report->detail <= CHECK_REDECLARATION_NON_STATIC)
        {
            const char* format = report->detail == CHECK_REDECLARATION_FUNCTION_TYPE ? "conflicting types for '%.*s'" :
                                 report->detail == CHECK_REDECLARATION_PARAMETER ? "redefinition of parameter '%.*s'" :
                                 report->detail == CHECK_REDECLARATION_STATIC ? "static declaration of '%.*s' follows non-static declaration" :
                                 report->detail == CHECK_REDECLARATION_NON_STATIC ? "non-static declaration of '%.*s' follows static declaration" :
                                 "redefinition of '%.*s'";
            result = check_warning_text(scratch, format, (int)name.length, name.pointer);
        }
        else if (kind == C_BINDING_ENUMERATOR && previous < count && input->declaration_kinds[previous] == kind)
        {
            result = check_warning_text(scratch, "redefinition of enumerator '%.*s'", (int)name.length, name.pointer);
        }
        else if (previous < count && input->declaration_kinds[previous] == kind)
        {
            const uint32_t linkage = check_input_linkage(input->declaration_bits, token);
            const uint32_t prior_linkage = check_input_linkage(input->declaration_bits, previous);
            if (linkage != 0 && prior_linkage != 0 && linkage != prior_linkage)
            {
                result = check_warning_text(scratch, "%s declaration of '%.*s' follows %s declaration",
                                            linkage == 1 ? "static" : "non-static", (int)name.length, name.pointer,
                                            prior_linkage == 1 ? "static" : "non-static");
            }
            else if (report->message == CHECK_INPUT_CONFLICTING_TYPES)
            {
                result = check_warning_text(scratch, "conflicting types for '%.*s'", (int)name.length, name.pointer);
            }
        }
    }
    else if (token < count && (report->message == CHECK_INPUT_INCOMPLETE_OBJECT || report->message == CHECK_INPUT_INCOMPLETE_TENTATIVE ||
                              report->message == CHECK_INPUT_INCOMPLETE_MEMBER))
    {
        // The report's type where it has one: an invalid declaration's own is a stand-in (#1447, check_input_variable_recover).
        const TypeId incomplete = report->type != 0 ? report->type : input->declared_types[token];
        const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, incomplete, 0);
        result = type.length == 0 ? NULL : report->message == CHECK_INPUT_INCOMPLETE_MEMBER ?
                 check_warning_text(scratch, "field has incomplete type %.*s", (int)type.length, type.pointer) :
                 report->message == CHECK_INPUT_INCOMPLETE_TENTATIVE ?
                 check_warning_text(scratch, "tentative definition has type %.*s that is never completed", (int)type.length, type.pointer) :
                 check_warning_text(scratch, "variable has incomplete type %.*s", (int)type.length, type.pointer);
    }
    return check_semantic_text(pp, result);
}

// A preprocessed token's spelling, including generated and macro-expanded tokens. The spelling API's buffer
// bound is the same one used by the source adapter; compact #embed tokens have no single-token spelling.
static String8 check_semantic_token(const Parser* parser, Preprocessor* pp, uint32_t token, Arena* scratch)
{
    String8 result = { 0 };
    const uint32_t source = token < parser->tokens.count ? parse_source_index(&parser->tokens, token) : BUSTER_CHECK_NONE;
    // The output's columns, which the backend may have given back (pp_release_output).
    pp_output_ensure(pp);
    if (source < pp->output.count && pp->output.kinds[source] != BUSTER_PP_KIND_EMBED)
    {
        const uint8_t kind = pp->output.kinds[source];
        // Names carry intern handles rather than end locations, and pp_spelling borrows their interned bytes.
        const bool named = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT);
        const uint64_t capacity = named ? 0 : kind == BUSTER_PP_KIND_INTEGER ? 24 : (uint64_t)pp->output.values[source] - pp->output.locations[source] + 1;
        uint8_t* buffer = !named ? arena_push(scratch, capacity, 1) : NULL;
        const uint8_t* bytes = NULL;
        const uint32_t length = named || buffer != NULL ? pp_spelling(pp, kind, pp->output.values[source], pp->output.locations[source], buffer, &bytes) : 0;
        result = (String8){ .pointer = (char*)bytes, .length = length };
    }
    return result;
}

// A character constant's spelling in a failed assertion's requirement, as clang's CharacterLiteral::print gives it (#1999):
// its prefix, then its value quoted: a C escape for a quote, a backslash or a control character that has one; else, an
// ordinary constant's sign extension of a byte taken off, a printable byte as itself, `\xNN` below 256, `\uNNNN` up to
// 0xFFFF and `\UNNNNNNNN` past it, in lower case. The spelling as written where it doesn't decode.
static String8 check_semantic_character(Preprocessor* pp, String8 written, Arena* scratch)
{
    String8 result = written;
    const uint8_t* bytes = (const uint8_t*)written.pointer;
    const CheckLiteral literal = check_literal_character_by(bytes, (uint32_t)written.length, pp->options.language.standard >= 2023,
                                                            pp->options.predefines.char_unsigned, true);
    uint32_t prefix = 0;
    while (prefix < written.length && bytes[prefix] != '\'')
    {
        prefix += 1;
    }
    char* text = literal.valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, char, prefix + 16) : NULL;
    if (text != NULL)
    {
        uint32_t value = (uint32_t)literal.integer;
        const bool ordinary = prefix == 0;
        value = ordinary && (value & ~UINT32_C(0xFF)) == ~UINT32_C(0xFF) ? value & 0xFF : value;
        static const char escapes[][3] = { ['\a'] = "\\a", ['\b'] = "\\b", ['\t'] = "\\t", ['\n'] = "\\n", ['\v'] = "\\v", ['\f'] = "\\f", ['\r'] = "\\r" };
        const bool quoted = value == '\'' || value == '\\';
        const bool control = value < BUSTER_ARRAY_LENGTH(escapes) && escapes[value][0] != 0;
        memcpy(text, written.pointer, prefix);
        int length = 0;
        if (quoted || control)
        {
            length = snprintf(text + prefix, 16, "'%s'", quoted ? (value == '\'' ? "\\'" : "\\\\") : escapes[value]);
        }
        else if (value >= 0x20 && value < 0x7F)
        {
            length = snprintf(text + prefix, 16, "'%c'", (char)value);
        }
        else
        {
            length = snprintf(text + prefix, 16, value < 256 ? "'\\x%02x'" : value <= 0xFFFF ? "'\\u%04x'" : "'\\U%08x'", (unsigned)value);
        }
        result = length > 0 ? (String8){ .pointer = text, .length = prefix + (uint32_t)length } : written;
    }
    return result;
}

static bool check_semantic_binary(uint8_t role)
{
    return (role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_COMMA || role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_CLOSE_TERNARY;
}

// A run of builtin type keywords as clang's TypePrinter spells their type (#1447): `unsigned` is "unsigned int",
// `long unsigned int` "unsigned long", `signed` "int". NULL where the run isn't one it reorders (a typedef's, a
// complex's, a _BitInt's, or any keyword it doesn't know), which keeps the keywords as written.
static const char* check_semantic_builtin_spelling(const String8* parts, uint32_t count)
{
    uint32_t longs = 0;
    bool is_unsigned = false;
    bool is_signed = false;
    bool is_int = false;
    bool is_char = false;
    bool is_short = false;
    bool is_double = false;
    bool known = count != 0;
    for (uint32_t i = 0; known && i < count; i += 1)
    {
        const String8 word = parts[i];
        const bool unsigned_word = word.length == 8 && memcmp(word.pointer, "unsigned", 8) == 0;
        const bool signed_word = (word.length == 6 && memcmp(word.pointer, "signed", 6) == 0) ||
                                 (word.length == 8 && memcmp(word.pointer, "__signed", 8) == 0) ||
                                 (word.length == 10 && memcmp(word.pointer, "__signed__", 10) == 0);
        const bool int_word = word.length == 3 && memcmp(word.pointer, "int", 3) == 0;
        const bool char_word = word.length == 4 && memcmp(word.pointer, "char", 4) == 0;
        const bool short_word = word.length == 5 && memcmp(word.pointer, "short", 5) == 0;
        const bool long_word = word.length == 4 && memcmp(word.pointer, "long", 4) == 0;
        const bool double_word = word.length == 6 && memcmp(word.pointer, "double", 6) == 0;
        is_unsigned |= unsigned_word;
        is_signed |= signed_word;
        is_int |= int_word;
        is_char |= char_word;
        is_short |= short_word;
        is_double |= double_word;
        longs += long_word ? 1 : 0;
        known = unsigned_word || signed_word || int_word || char_word || short_word || long_word || double_word;
    }
    const bool integer = !is_double && !(is_unsigned && is_signed) && longs <= 2 && !(is_char && (is_short || is_int || longs != 0)) && !(is_short && longs != 0);
    const char* result = !known ? NULL :
                         is_double ? (longs == 1 && !is_unsigned && !is_signed && !is_int && !is_char && !is_short ? "long double" : longs == 0 && count == 1 ? "double" : NULL) :
                         !integer ? NULL :
                         is_char ? (is_unsigned ? "unsigned char" : is_signed ? "signed char" : "char") :
                         is_short ? (is_unsigned ? "unsigned short" : "short") :
                         longs == 2 ? (is_unsigned ? "unsigned long long" : "long long") :
                         longs == 1 ? (is_unsigned ? "unsigned long" : "long") :
                         is_unsigned ? "unsigned int" : "int";
    return result;
}

// c_check_common_warnings.c's test of a separator that is a comma operator, which that file defines later in the unit.
static bool check_common_comma_operator(const Parser* parser, uint32_t separator);

// Spell a failed assertion's requirement with expression spacing, not the source's arbitrary whitespace, and its character
// constants by their values (check_semantic_character). This is a bounded token pass, including macro expansions; it never
// walks the expression recursively. `spelled`, where not NULL, holds a token's replacement as clang's printer spells it
// (check_semantic_literal_spelling), one for each token from `first`.
static const char* check_semantic_requirement(const Parser* parser, Preprocessor* pp, uint32_t first, uint32_t end, const char* const* spelled,
                                              Arena* scratch)
{
    const uint32_t count = end - first;
    String8* parts = BUSTER_ARENA_PUSH_ARRAY(scratch, String8, count);
    bool valid = parts != NULL;
    uint64_t length = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const char* replacement = spelled != NULL ? spelled[i] : NULL;
        parts[i] = replacement != NULL ? (String8){ .pointer = (char*)replacement, .length = strlen(replacement) } : check_semantic_token(parser, pp, first + i, scratch);
        // A character constant by its value, as clang prints it (#1999).
        parts[i] = replacement == NULL && parts[i].length != 0 && parser->tokens.kinds[first + i] == PARSE_KIND_CHARACTER ?
                   check_semantic_character(pp, parts[i], scratch) : parts[i];
        valid = (parts[i].length != 0 || replacement != NULL) && parts[i].length < UINT64_MAX - length - 1;
        length += valid ? parts[i].length + 1 : 0;
    }
    // A record or enumeration a type name defines (`__builtin_offsetof(struct { int a, b; }, b)`) prints as clang's
    // TypePrinter names it, without its body (#2482): `struct T`, or an unnamed one's `struct (unnamed at f.c:1:2)`
    // (check_semantic_tag_spelling). Its tokens past the keyword print nothing.
    uint8_t* hidden = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    valid &= hidden != NULL;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        hidden[i] = parts[i].length == 0;
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const uint8_t kind = parser->tokens.kinds[first + i];
        const bool tag = kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM;
        const uint32_t after = tag ? check_input_skip_attributes(parser, first + i + 1, end) : end;
        const bool named = after < end && parser->tokens.kinds[after] == PARSE_KIND_IDENTIFIER;
        const uint32_t brace = named ? after + 1 : after;
        const uint8_t role = brace < end ? parser->tree.roles[brace] : PARSE_ROLE_NONE;
        const uint32_t close = role == PARSE_ROLE_OPEN_STRUCT || role == PARSE_ROLE_OPEN_ENUM ? parser->tree.parents[brace] : end;
        const String8 spelling = close < end ? check_semantic_tag_spelling(parser, pp, named ? after : first + i, scratch) : (String8){ 0 };
        if (spelling.length != 0)
        {
            parts[i] = spelling;
            length += spelling.length;
            for (uint32_t k = first + i + 1; k <= close; k += 1)
            {
                hidden[k - first] = 1;
            }
            i = close - first;
        }
    }
    // A cast's type name prints its builtin type as clang's TypePrinter does (#1447, `(unsigned)n`'s `(unsigned int)n`):
    // its run of type keywords becomes the first one's text, and the rest print nothing.
    uint8_t* dropped = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    valid &= dropped != NULL;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        dropped[i] = hidden[i];
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const bool cast = parser->tree.roles[first + i] == PARSE_ROLE_OPEN_CAST;
        uint32_t run = cast ? i + 1 : count;
        while (run < count && parser->tokens.kinds[first + run] == PARSE_KIND_QUALIFIER)
        {
            run += 1;
        }
        uint32_t stop = run;
        while (stop < count && parser->tokens.kinds[first + stop] == PARSE_KIND_TYPE)
        {
            stop += 1;
        }
        // C23's bool keyword is the type _Bool spells there (clang's printer's "bool" where the language has it).
        const bool boolean = cast && stop == run + 1 && parser->language.standard >= 2023 && parts[run].length == 5 && memcmp(parts[run].pointer, "_Bool", 5) == 0;
        const char* builtin = boolean ? "bool" : cast && stop > run ? check_semantic_builtin_spelling(parts + run, stop - run) : NULL;
        if (builtin != NULL)
        {
            parts[run] = (String8){ .pointer = (char*)builtin, .length = strlen(builtin) };
            length += parts[run].length;
            for (uint32_t k = run + 1; k < stop; k += 1)
            {
                dropped[k] = 1;
            }
        }
    }
    char* result = valid && length < UINT64_MAX ? BUSTER_ARENA_PUSH_ARRAY(scratch, char, length + 1) : NULL;
    uint64_t at = 0;
    uint32_t last = UINT32_MAX;
    for (uint32_t i = 0; result != NULL && i < count; i += 1)
    {
        if (dropped[i] != 0)
        {
            continue;
        }
        const uint32_t prior = last;
        last = i;
        const uint8_t role = parser->tree.roles[first + i];
        const uint8_t previous = prior != UINT32_MAX ? parser->tree.roles[first + prior] : PARSE_ROLE_NONE;
        const bool word = prior != UINT32_MAX && pp_word_character((uint8_t)parts[prior].pointer[parts[prior].length - 1], pp_dollar_punctuation(&pp->options)) &&
                                                 pp_word_character((uint8_t)parts[i].pointer[0], pp_dollar_punctuation(&pp->options));
        // A list's separator (a call's arguments, an initializer list's, a selection's) is followed by a space, as clang's
        // printer writes ", " between them (#2382); a comma operator a group holds is a binary operator, " , ".
        const bool comma = role == PARSE_ROLE_SEPARATOR && check_common_comma_operator(parser, first + i);
        const bool previous_comma = previous == PARSE_ROLE_SEPARATOR && check_common_comma_operator(parser, first + prior);
        const bool separated = previous == PARSE_ROLE_SEPARATOR;
        // A _Generic association's `:` is followed by a space, as clang's printer writes `int: 1` (#2482); a conditional's
        // is a binary operator's.
        const bool associated = prior != UINT32_MAX && parser->tokens.kinds[first + prior] == PARSE_KIND_COLON && !check_semantic_binary(previous);
        // A type's declarator starts after its specifiers, while consecutive pointer stars and a star's qualifiers
        // remain joined: `int **const`, `int *const *`, `int (*)(long)`. Expression prefixes have no specifier before them.
        const uint8_t prior_kind = prior != UINT32_MAX ? parser->tokens.kinds[first + prior] : PARSE_KIND_END;
        const uint8_t kind = parser->tokens.kinds[first + i];
        const bool specifier = previous == PARSE_ROLE_SPECIFIER || previous == PARSE_ROLE_TAG || previous == PARSE_ROLE_CLOSE_SPECIFIER ||
                               previous == PARSE_ROLE_CLOSE_STRUCT || previous == PARSE_ROLE_CLOSE_ENUM || previous == PARSE_ROLE_QUALIFIER_LEFT ||
                               prior_kind == PARSE_KIND_TYPE || prior_kind == PARSE_KIND_QUALIFIER;
        const bool declarator = (specifier && (kind == PARSE_KIND_STAR || kind == PARSE_KIND_L_PAREN)) ||
                                (prior_kind == PARSE_KIND_IDENTIFIER && kind == PARSE_KIND_STAR);
        const bool omitted = prior_kind == PARSE_KIND_QUESTION && kind == PARSE_KIND_COLON;
        if (prior != UINT32_MAX && !omitted && (check_semantic_binary(role) || check_semantic_binary(previous) || word || separated || comma || previous_comma || associated || declarator))
        {
            result[at++] = ' ';
        }
        memcpy(result + at, parts[i].pointer, parts[i].length);
        at += parts[i].length;
    }
    if (result != NULL)
    {
        result[at] = 0;
    }
    return result;
}

static void check_semantic_add_note(const Parser* parser, PPOutputDiagnosticNote* notes, uint32_t* count, uint32_t token, const char* message);

// c_check_warning_number.c's printers, which c_check_common_warnings.c includes later in the unit.
static void check_common_number(char* buffer, uint64_t capacity, __int128 value);
static void check_common_float_number(char* output, uint64_t capacity, long double value, uint32_t precision);
static void check_common_quad_number(char* output, uint64_t capacity, unsigned __int128 bits, uint32_t precision);

// A node under its implicit conversions, clang's IgnoreParenImpCasts: parentheses have no nodes, and CONVERT is only
// ever implicit.
static uint32_t check_semantic_bare(const CheckGraph* graph, uint32_t node)
{
    uint32_t result = node;
    for (uint32_t steps = 0; result < graph->count && graph->ops[result] == CHECK_OP_CONVERT && steps < graph->count; steps += 1)
    {
        result = graph->operand0[result];
    }
    return result;
}

// A node under its implicit conversions and its selections (_Generic's, __builtin_choose_expr's and __extension__'s
// SELECT), as clang's IgnoreParenImpCasts looks through them (IgnoreParensSingleStep, #2482): a selected literal is a
// literal, so `_Generic(x, int: 1) == 2` notes nothing.
static uint32_t check_semantic_unselected(const CheckGraph* graph, uint32_t node)
{
    uint32_t result = check_semantic_bare(graph, node);
    for (uint32_t steps = 0; result < graph->count && graph->ops[result] == CHECK_OP_SELECT && steps < graph->count; steps += 1)
    {
        result = check_semantic_bare(graph, graph->operand0[result]);
    }
    return result;
}

// Whether a bare node is an integer literal or C23's true or false (clang's IntegerLiteral and CXXBoolLiteralExpr): a
// number token's VALUE with an integer constant. A floating or a character literal is neither.
static bool check_semantic_integer_literal(const Parser* parser, const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    const uint32_t token = node < graph->count && graph->ops[node] == CHECK_OP_VALUE ? graph->tokens[node] : BUSTER_CHECK_NONE;
    return token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_NUMBER && check_stream_constant_kind(stream, node) == CHECK_CONSTANT_INTEGER;
}

// Whether a bare node is any literal clang's UsefulToPrintExpr finds obvious: an integer, floating or character one, or
// true or false.
static bool check_semantic_literal(const Parser* parser, const CheckGraph* graph, uint32_t node)
{
    const uint32_t token = node < graph->count && graph->ops[node] == CHECK_OP_VALUE ? graph->tokens[node] : BUSTER_CHECK_NONE;
    return token < parser->tokens.count && (parser->tokens.kinds[token] == PARSE_KIND_NUMBER || parser->tokens.kinds[token] == PARSE_KIND_CHARACTER);
}

// Whether a node's condition is known to fail, as EvaluateAsBooleanCondition finds it: its constant is zero.
static bool check_semantic_fails(const CheckStream* stream, uint32_t node)
{
    const CheckValue value = check_stream_value(stream, node);
    const bool result = value.constant_kind == CHECK_CONSTANT_INTEGER ? value.integer == 0 :
                        value.constant_kind == CHECK_CONSTANT_FLOATING ? value.floating == 0 :
                        value.constant_kind == CHECK_CONSTANT_COMPLEX && value.floating == 0 && value.imaginary == 0;
    return result;
}

// Whether parse token `token` lies in the subtree of `ancestor`: whether its parents reach it.
static bool check_semantic_under(const Parser* parser, uint32_t token, uint32_t ancestor)
{
    uint32_t at = token;
    for (uint32_t steps = 0; at != ancestor && at < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        at = parser->tree.parents[at];
    }
    return at == ancestor;
}

// The least and the greatest token of the nodes of `node`'s subtree but its implicit conversions, those in [first, end),
// and, in `literals` (one for each token from `first`), each VALUE node's at its token. `stack` holds the graph's count.
static void check_semantic_subtree(const CheckGraph* graph, uint32_t node, uint32_t first, uint32_t end, uint32_t* literals, uint32_t* stack,
                                   uint32_t* low, uint32_t* high)
{
    uint32_t top = 0;
    stack[top++] = node;
    while (top != 0)
    {
        const uint32_t at = stack[--top];
        const CheckOp op = graph->ops[at];
        const uint32_t token = graph->tokens[at];
        if (op != CHECK_OP_CONVERT && token >= first && token < end)
        {
            *low = token < *low ? token : *low;
            *high = *high == BUSTER_CHECK_NONE || token > *high ? token : *high;
            literals[token - first] = op == CHECK_OP_VALUE || op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF ? at : literals[token - first];
        }
        const uint32_t operands[3] = { graph->operand0[at], graph->operand1[at], graph->operand2[at] };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            if (operands[i] < at && top < graph->count)
            {
                stack[top++] = operands[i];
            }
        }
        const bool arguments = check_graph_has_arguments(op) && graph->argument_offsets != NULL;
        const uint32_t offset = arguments ? graph->argument_offsets[at] : 0;
        const uint32_t count = arguments ? graph->argument_counts[at] : 0;
        for (uint32_t i = 0; i < count && offset + i < graph->argument_count; i += 1)
        {
            const uint32_t argument = graph->argument_nodes[offset + i];
            if (argument < at && top < graph->count)
            {
                stack[top++] = argument;
            }
        }
    }
}

// A floating constant as APFloat::toString prints it at its default precision, its semantics' 2 + bits * 59 / 196 digits
// (clang's ConvertAPValueToString and StmtPrinter's floating literals). False for a type that isn't a real floating one.
static bool check_semantic_floating_text(char* output, uint64_t capacity, TypeId type, const CheckValue* value)
{
    const uint32_t bits = type == BUSTER_TYPE_BF16 ? 8 : type == BUSTER_TYPE_HALF || type == BUSTER_TYPE_FLOAT16 ? 11 : type == BUSTER_TYPE_FLOAT ? 24 :
                          type == BUSTER_TYPE_DOUBLE ? 53 : type == BUSTER_TYPE_LONG_DOUBLE ? 64 : type == BUSTER_TYPE_FLOAT128 ? 113 : 0;
    const uint32_t precision = 2 + bits * 59 / 196;
    if (type == BUSTER_TYPE_FLOAT128)
    {
        check_common_quad_number(output, capacity, value->integer, precision);
    }
    else if (bits != 0)
    {
        check_common_float_number(output, capacity, value->floating, precision);
    }
    return bits != 0 && value->constant_kind == CHECK_CONSTANT_FLOATING;
}

// A code unit as escapeCStyle<EscapeChar::Single> escapes it, or NULL.
static const char* check_semantic_escape(uint32_t unit)
{
    const char* result = unit == '\\' ? "\\\\" : unit == '\'' ? "\\'" : unit == '\a' ? "\\a" : unit == '\b' ? "\\b" : unit == '\f' ? "\\f" :
                         unit == '\n' ? "\\n" : unit == '\r' ? "\\r" : unit == '\t' ? "\\t" : unit == '\v' ? "\\v" : NULL;
    return result;
}

// A token's replacement as clang's StmtPrinter prints its node (#1447): an integer literal's value in decimal with its
// type's suffix, a floating one's APFloat digits with a `.` where they have none and its type's suffix, a character
// one's prefix and its value, escaped; a sizeof's or an alignof's keyword; an imaginary literal's imaginary part printed
// so, then `i` (#2219: 5iwb is 5wbi, 0x10i is 16i), as ImaginaryLiteral prints its subexpression. NULL keeps the token as
// written (true, false, a literal past 128 bits).
static const char* check_semantic_literal_spelling(const Parser* parser, Preprocessor* pp, const TypeTable* types, const CheckGraph* graph,
                                                   const CheckStream* stream, uint32_t node, Arena* scratch)
{
    const uint32_t token = graph->tokens[node];
    const uint8_t kind = parser->tokens.kinds[token];
    const CheckOp op = graph->ops[node];
    const bool query = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF;
    const CheckValue literal = check_stream_value(stream, node);
    const bool imaginary = kind == PARSE_KIND_NUMBER && literal.constant_kind == CHECK_CONSTANT_COMPLEX && type_is_valid(types, literal.type);
    const CheckValue value = imaginary ? check_complex_component(types, &literal, true) : literal;
    const char* unit = imaginary ? "i" : "";
    const TypeId type = value.type != 0 ? type_unqualified(type_strip_aligned(types, value.type)) : 0;
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = type != 0 && type != BUSTER_TYPE_BOOL && value.constant_kind == CHECK_CONSTANT_INTEGER &&
                         check_action_integer_type(types, type, &width, &is_signed) && width != 0;
    char text[128];
    const char* result = NULL;
    if (query)
    {
        // The keyword as UnaryExprOrTypeTraitExpr prints it: __alignof's (GNU's preferred alignment) or _Alignof's
        // whichever C spelling, then a space before an expression operand (`sizeof (x)`), none before a type name's `(`.
        const uint32_t name = parser->tokens.names[token];
        const char* keyword = op == CHECK_OP_SIZEOF ? "sizeof" : name == C_NAME_GNU_ALIGNOF || name == C_NAME_GNU_ALIGNOF2 ? "__alignof" : "_Alignof";
        result = graph->operand0[node] < graph->count ? check_warning_text(scratch, "%s ", keyword) : keyword;
    }
    else if (kind == PARSE_KIND_NUMBER && integer)
    {
        const bool bit_int = type_kind(types, type) == BUSTER_TYPE_KIND_BIT_INT;
        const char* suffix = bit_int ? (is_signed ? "wb" : "uwb") : type == BUSTER_TYPE_INT || type == BUSTER_TYPE_INT128 || type == BUSTER_TYPE_UINT128 ? "" :
                             type == BUSTER_TYPE_UINT ? "U" : type == BUSTER_TYPE_LONG ? "L" : type == BUSTER_TYPE_ULONG ? "UL" :
                             type == BUSTER_TYPE_LLONG ? "LL" : type == BUSTER_TYPE_ULLONG ? "ULL" : NULL;
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 bits = value.integer & mask;
        const bool negative = is_signed && width <= 128 && ((bits >> (width - 1)) & 1) != 0;
        if (suffix != NULL && width <= 128)
        {
            check_common_number(text, sizeof(text), negative ? (__int128)(bits | ~mask) : (__int128)bits);
            result = check_warning_text(scratch, "%s%s%s", text, suffix, unit);
        }
    }
    else if (kind == PARSE_KIND_NUMBER && check_semantic_floating_text(text, sizeof(text), type, &value))
    {
        const char* suffix = type == BUSTER_TYPE_FLOAT ? "F" : type == BUSTER_TYPE_LONG_DOUBLE ? "L" : type == BUSTER_TYPE_FLOAT128 ? "Q" :
                             type == BUSTER_TYPE_FLOAT16 ? "F16" : "";
        const bool digits = strspn(text, "-0123456789") == strlen(text);
        result = check_warning_text(scratch, "%s%s%s%s", text, digits ? "." : "", suffix, unit);
    }
    else if (kind == PARSE_KIND_CHARACTER && integer)
    {
        // The prefix as the token spells it: L, u8, u, U or none (CharacterLiteral::print).
        const String8 spelling = check_semantic_token(parser, pp, token, scratch);
        uint32_t prefix = 0;
        while (prefix < spelling.length && spelling.pointer[prefix] != '\'')
        {
            prefix += 1;
        }
        uint32_t unit = (uint32_t)value.integer;
        // An ordinary literal's value, sign-extended from a char, is its byte.
        unit = prefix == 0 && (unit & ~0xffu) == ~0xffu ? unit & 0xffu : unit;
        const char* escaped = check_semantic_escape(unit);
        if (prefix < spelling.length && escaped != NULL)
        {
            result = check_warning_text(scratch, "%.*s'%s'", (int)prefix, spelling.pointer, escaped);
        }
        else if (prefix < spelling.length)
        {
            const bool printable = unit >= 0x20 && unit < 0x7f;
            result = printable ? check_warning_text(scratch, "%.*s'%c'", (int)prefix, spelling.pointer, (char)unit) :
                     unit < 0x100 ? check_warning_text(scratch, "%.*s'\\x%02x'", (int)prefix, spelling.pointer, unit) :
                     unit <= 0xffff ? check_warning_text(scratch, "%.*s'\\u%04x'", (int)prefix, spelling.pointer, unit) :
                     check_warning_text(scratch, "%.*s'\\U%08x'", (int)prefix, spelling.pointer, unit);
        }
    }
    return result;
}

// Whether clang's UsefulToPrintExpr prints a static assertion's operand: not a literal, nor a unary operator's of one, nor
// a comparison, logical or comma operator; any other operator, a cast, a name or a sizeof is.
static bool check_semantic_useful(const Parser* parser, const CheckGraph* graph, uint32_t node)
{
    uint32_t at = check_semantic_bare(graph, node);
    bool result = true;
    bool unary = true;
    for (uint32_t steps = 0; unary && at < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[at];
        unary = op == CHECK_OP_NEGATIVE || op == CHECK_OP_POSITIVE || op == CHECK_OP_NOT || op == CHECK_OP_BIT_NOT || op == CHECK_OP_ADDRESS ||
                op == CHECK_OP_DEREFERENCE || op == CHECK_OP_REAL || op == CHECK_OP_IMAGINARY || op == CHECK_OP_PRE_INCREMENT ||
                op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT;
        const bool arithmetic = (op >= CHECK_OP_ADD && op <= CHECK_OP_SHIFT_RIGHT) || op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR;
        const bool other_binary = (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL) || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_COMMA;
        result = !unary && !check_semantic_literal(parser, graph, at) && (arithmetic || !other_binary);
        at = unary ? check_semantic_bare(graph, graph->operand0[at]) : at;
    }
    return result;
}

// A static assertion operand's value as clang's ConvertAPValueToString prints it, by the operand's own type: a bool's
// true or false; a plain char's character, its code unit and its value; an integer's value; a real floating one's
// APFloat digits. False for any other (no note).
// A failed requirement's operand's value for its note (#2214): its constant, or what a read of a const object loads from
// its completed initializer's receipt (`const int k = 0; ... k == 45`'s 0), as clang's evaluation folds it; else its
// value as the stream has it.
static CheckValue check_semantic_operand_value(const TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node, uint32_t at)
{
    CheckValue result = check_stream_value(stream, node);
    const uint32_t qualifiers = type_qualifiers(result.type);
    CheckValue loaded = { 0 };
    const bool load = result.constant_kind == CHECK_CONSTANT_ADDRESS && result.category == CHECK_LVALUE && result.fault == CHECK_FAULT_NONE &&
                      (qualifiers & BUSTER_TYPE_CONST) != 0 && (qualifiers & BUSTER_TYPE_VOLATILE) == 0 &&
                      (result.flags & (BUSTER_CHECK_VOLATILE_PART | BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED)) == 0 &&
                      graph->const_values != NULL && graph->source_positions != NULL &&
                      check_const_load(types, graph->const_values, result.object, result.offset, result.object_bit_offset, result.bit_width,
                                       result.type, graph->source_positions[at], &loaded) &&
                      loaded.constant_kind == CHECK_CONSTANT_INTEGER;
    result = load ? loaded : result;
    return result;
}

static bool check_semantic_value_text(char* output, uint64_t capacity, const TypeTable* types, const CheckValue* value)
{
    const TypeId type = value->type != 0 ? type_unqualified(type_strip_aligned(types, value->type)) : 0;
    uint32_t width = 0;
    bool is_signed = false;
    bool result = false;
    if (value->constant_kind == CHECK_CONSTANT_INTEGER && type == BUSTER_TYPE_BOOL)
    {
        snprintf(output, capacity, "%s", value->integer != 0 ? "true" : "false");
        result = true;
    }
    else if (value->constant_kind == CHECK_CONSTANT_INTEGER && type != 0 && check_action_integer_type(types, type, &width, &is_signed) && width != 0 &&
             width <= 128)
    {
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 bits = value->integer & mask;
        const bool negative = is_signed && ((bits >> (width - 1)) & 1) != 0;
        char number[48];
        check_common_number(number, sizeof(number), negative ? (__int128)(bits | ~mask) : (__int128)bits);
        if (type == BUSTER_TYPE_CHAR)
        {
            // The character as the diagnostic prints its byte: an escape, the character, a control's <U+00XX> or a byte
            // past ASCII's <0xXX> (no UTF-8 sequence of its own).
            const uint32_t unit = (uint32_t)(bits & 0xffu);
            const char* escaped = check_semantic_escape(unit);
            char character[16];
            if (escaped != NULL)
            {
                snprintf(character, sizeof(character), "%s", escaped);
            }
            else if (unit < 0x20 || unit == 0x7f)
            {
                snprintf(character, sizeof(character), "<U+%04X>", unit);
            }
            else if (unit >= 0x80)
            {
                snprintf(character, sizeof(character), "<0x%02X>", unit);
            }
            else
            {
                snprintf(character, sizeof(character), "%c", (char)unit);
            }
            snprintf(output, capacity, "'%s' (0x%02X, %s)", character, unit, number);
        }
        else
        {
            snprintf(output, capacity, "%s", number);
        }
        result = true;
    }
    else
    {
        result = check_semantic_floating_text(output, capacity, type, value);
    }
    return result;
}

// The keyword of the static assertion containing a report's token. Its written condition starts two tokens later,
// including grouping parentheses which the expression graph does not retain.
static uint32_t check_semantic_assertion_keyword(const Parser* parser, uint32_t token)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t at = 0; at < token && at + 1 < parser->tokens.count; at += 1)
    {
        if (parser->tokens.kinds[at] == PARSE_KIND_STATIC_ASSERT && parser->tree.parents[at + 1] > token)
        {
            result = at;
        }
    }
    return result;
}

// The cold type replay handles a specifier sequence and its direct pointer chain. Function/array declarators,
// typeof and inline tag definitions retain their source spelling: replaying them would require declaration state
// which the completed input deliberately does not keep. Attribute argument syntax is checked separately after binding.
static bool check_semantic_requirement_type_shape(const Parser* parser, const CheckInput* input, uint32_t first, uint32_t end)
{
    bool valid = first < end;
    for (uint32_t token = first; valid && token < end;)
    {
        const uint32_t after = check_input_skip_attributes(parser, token, end);
        const uint8_t kind = parser->tokens.kinds[token];
        const uint32_t declaration = kind == PARSE_KIND_IDENTIFIER ? input->declarations[token] : BUSTER_CHECK_NONE;
        valid = after > token || kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STAR ||
                kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
                (declaration < input->token_count && (input->declaration_kinds[declaration] == C_BINDING_TYPEDEF ||
                                                      input->declaration_kinds[declaration] == C_BINDING_TAG_KIND));
        token = after > token ? after : token + 1;
    }
    return valid;
}

// The constant reader retains only function ABI attributes after a pointer's star. Printing must also retain that
// pointer's own address space, before a _Generic control's value conversion drops it. Rebuild just the direct pointer
// chain in the private table, applying the already evaluated attributes with the ordinary type-attribute helper.
static TypeId check_semantic_requirement_pointer_types(CheckConstantInput* query, TypeId type)
{
    CheckInputReader* reader = query->reader;
    const Parser* parser = reader->parser;
    const uint32_t capacity = query->end - query->first;
    uint32_t* stars = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, capacity);
    TypeId* levels = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, TypeId, capacity);
    bool valid = stars != NULL && levels != NULL;
    reader->output->resource |= !valid;
    uint32_t count = 0;
    for (uint32_t token = query->first; valid && token < query->end;)
    {
        CheckConstantAttributes* attributes = query->attribute_starts[token - query->first];
        if (attributes != NULL)
        {
            token = attributes->end;
        }
        else
        {
            if (parser->tokens.kinds[token] == PARSE_KIND_STAR)
            {
                stars[count++] = token;
            }
            token += 1;
        }
    }
    TypeId base = type;
    for (uint32_t level = 0; valid && level < count; level += 1)
    {
        levels[level] = base;
        const TypeId plain = type_strip_aligned(reader->types, base);
        valid = type_kind(reader->types, plain) == BUSTER_TYPE_KIND_POINTER;
        base = valid ? type_element(reader->types, plain) : 0;
    }
    for (uint32_t star = 0; valid && star < count; star += 1)
    {
        const TypeId previous = levels[count - star - 1];
        base = type_pointer(reader->types, base);
        base = base != 0 ? type_qualify(reader->types, base, type_qualifiers(previous)) : 0;
        const uint32_t extended = type_object_qualifiers(reader->types, previous);
        base = base != 0 && extended != 0 ? type_extend(reader->types, base, extended) : base;
        const uint32_t end = star + 1 < count ? stars[star + 1] : query->end;
        for (uint32_t token = stars[star] + 1; base != 0 && token < end; token += 1)
        {
            CheckConstantAttributes* attributes = query->attribute_starts[token - query->first];
            for (uint32_t i = 0; attributes != NULL && base != 0 && i < attributes->parsed.count; i += 1)
            {
                CheckInputAttribute* attribute = attributes->parsed.records + i;
                if (attribute->kind == CHECK_ATTRIBUTE_ADDRESS_SPACE && !attribute->space_applied)
                {
                    base = check_input_address_space(reader, &attributes->parsed, attribute, base);
                }
            }
            token = attributes != NULL ? attributes->end - 1 : token;
        }
        valid = base != 0;
    }
    return valid ? base : 0;
}

// A failed requirement's attributed type names (#2409). Type-only operands, including a _Generic's control, need not
// have graph nodes. Read their already-bound syntax with the ordinary constant/type reader in private arenas: this
// folds an attribute's integer expression and drops ignored attributes by the same rules as the original reading.
// The table copy keeps every original TypeId, but neither a derived type nor a warning from this cold replay can
// change the checked input. A passing assertion never enters this path.
static bool check_semantic_requirement_types(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                             uint32_t first, uint32_t end, const char** spelled, Arena* scratch)
{
    bool needed = false;
    for (uint32_t token = first; !needed && token < end; token += 1)
    {
        needed = check_input_attribute_starts(parser, token, end);
    }
    const uint32_t count = parser->tokens.count;
    needed &= count <= input->token_count && input->declarations != NULL && input->declared_types != NULL && input->declaration_kinds != NULL;
    const uint64_t slots = (uint64_t)count + 1;
    const uint32_t extra = end - first < (UINT32_MAX - 16) / 8 ? (end - first) * 8 + 16 : 0;
    const bool fits = extra != 0 && types->types.count <= UINT32_MAX - extra && types->parameters.count <= UINT32_MAX - extra;
    const TypeCapacity capacity = {
        .types = fits ? types->types.count - BUSTER_TYPE_BUILTIN_COUNT + extra : 0,
        .parameters = fits ? types->parameters.count + extra : 0,
        .tags = types->tags.count - 1,
        .members = types->members.count - 1,
        .lookups = types->lookup.count - 1,
    };
    Arena storage = needed && fits ? arena_reserve(type_capacity_bytes(capacity) + slots * 256 + (uint64_t)pp->table->count * 4 + 65536) : (Arena){ 0 };
    Arena index = needed && fits ? arena_reserve(type_index_capacity_bytes(capacity) + 65536) : (Arena){ 0 };
    Arena temporary = needed && fits ? arena_reserve(((uint64_t)end - first + 1) * 4096 + 65536) : (Arena){ 0 };
    TypeTable copied = { 0 };
    CheckInput staged = {
        .token_count = count,
        .const_values = input->const_values,
        .enum_definitions = input->enum_definitions,
        .enum_definition_count = input->enum_definition_count,
        .graph = input->graph,
    };
    CheckInputReader reader = {
        .output = &staged,
        .parser = parser,
        .pp = pp,
        .types = &copied,
        .arena = &storage,
        .scratch = &temporary,
        .warning_events = { .arena = &storage },
        .literal_warnings = { .arena = &storage },
        .source_warning_keys = { .arena = &storage },
        .variable_sized_records = { .arena = &storage },
        .warning_query_arena = &storage,
        .warning_bound_open = BUSTER_CHECK_NONE,
        .function_body = BUSTER_CHECK_NONE,
        .function_name_body = BUSTER_CHECK_NONE,
        .first_child = parser->first_children,
        .next_child = parser->next_siblings,
        .ends = parser->ends,
        .postorder = parser->postorder,
        .postorder_positions = parser->positions,
        .subtree_sizes = parser->sizes,
    };
    bool valid = !needed || (fits && type_copy(&copied, types, &storage, &index, (TypeCapacity){ .types = extra, .parameters = extra }));
#define BUSTER_CHECK_REQUIREMENT_COPY(field, element) \
    staged.field = needed && valid ? BUSTER_ARENA_PUSH_ARRAY(&storage, element, slots) : NULL; \
    valid &= !needed || staged.field != NULL; \
    if (needed && valid) { memset(staged.field, 0, slots * sizeof(element)); if (input->field != NULL) { memcpy(staged.field, input->field, (uint64_t)count * sizeof(element)); } }
    BUSTER_CHECK_REQUIREMENT_COPY(declarations, uint32_t);
    BUSTER_CHECK_REQUIREMENT_COPY(declared_types, TypeId);
    BUSTER_CHECK_REQUIREMENT_COPY(declaration_kinds, uint8_t);
    BUSTER_CHECK_REQUIREMENT_COPY(declaration_bits, uint8_t);
    BUSTER_CHECK_REQUIREMENT_COPY(objects, uint32_t);
    BUSTER_CHECK_REQUIREMENT_COPY(token_nodes, uint32_t);
#undef BUSTER_CHECK_REQUIREMENT_COPY
    reader.declared_types = staged.declared_types;
    reader.declaration_kinds = staged.declaration_kinds;
    reader.declaration_bits = staged.declaration_bits;
    reader.objects = staged.objects;
#define BUSTER_CHECK_REQUIREMENT_COLUMN(field, element, fill) \
    reader.field = needed && valid ? BUSTER_ARENA_PUSH_ARRAY(&storage, element, slots) : NULL; \
    valid &= !needed || reader.field != NULL; \
    if (needed && valid) { memset(reader.field, fill, slots * sizeof(element)); }
    BUSTER_CHECK_REQUIREMENT_COLUMN(enum_values, unsigned __int128, 0);
    BUSTER_CHECK_REQUIREMENT_COLUMN(tag_types.dense, TypeId, 0);
    BUSTER_CHECK_REQUIREMENT_COLUMN(tag_ends.dense, uint32_t, 0);
    BUSTER_CHECK_REQUIREMENT_COLUMN(builtins, uint16_t, 0);
    BUSTER_CHECK_REQUIREMENT_COLUMN(selected_tokens, uint32_t, 0xff);
    BUSTER_CHECK_REQUIREMENT_COLUMN(query_types, TypeId, 0);
    BUSTER_CHECK_REQUIREMENT_COLUMN(sizeof_types.dense, TypeId, 0);
    BUSTER_CHECK_REQUIREMENT_COLUMN(constant_present, uint32_t, 0);
    BUSTER_CHECK_REQUIREMENT_COLUMN(ignored, uint8_t, 0);
    BUSTER_CHECK_REQUIREMENT_COLUMN(warning_suppressed, uint8_t, 0);
#undef BUSTER_CHECK_REQUIREMENT_COLUMN
    staged.constant_warnings = needed && valid ? BUSTER_ARENA_PUSH_ARRAY(&storage, uint8_t, slots) : NULL;
    reader.builtin_names = needed && valid ? BUSTER_ARENA_PUSH_ARRAY(&storage, uint16_t, pp->table->count) : NULL;
    reader.warning_index_base = 1;
    while ((uint64_t)reader.warning_index_base * BUSTER_CHECK_WARNING_BLOCK < count)
    {
        reader.warning_index_base *= 2;
    }
    reader.warning_index = needed && valid ? BUSTER_ARENA_PUSH_ARRAY(&storage, uint16_t, (uint64_t)reader.warning_index_base * 2) : NULL;
    valid &= !needed || (staged.constant_warnings != NULL && reader.builtin_names != NULL && reader.warning_index != NULL);
    if (needed && valid && (reader.first_child == NULL || reader.next_child == NULL || reader.ends == NULL))
    {
        uint32_t* traversal = BUSTER_ARENA_PUSH_ARRAY(&storage, uint32_t, slots * 5);
        valid = traversal != NULL;
        if (valid)
        {
            reader.postorder = traversal;
            reader.postorder_positions = traversal + slots;
            reader.ends = traversal + 2 * slots;
            reader.first_child = traversal + 3 * slots;
            reader.next_child = traversal + 4 * slots;
            parse_tree_columns(parser->tree.parents, count, traversal, traversal + slots, traversal + 2 * slots, traversal + 3 * slots, traversal + 4 * slots, NULL);
        }
    }
    if (needed && valid)
    {
        memset(staged.constant_warnings, 0, slots);
        memset(reader.builtin_names, 0xff, (uint64_t)pp->table->count * sizeof(uint16_t));
        for (uint32_t i = 0; i < input->enum_value_count; i += 1)
        {
            const CheckInputEnumValue* value = input->enum_values + i;
            if (value->token < count)
            {
                reader.enum_values[value->token] = value->value;
            }
        }
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint8_t kind = parser->tokens.kinds[token];
            if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
            {
                const uint32_t after = check_input_skip_attributes(parser, token + 1, count);
                const bool named = after < count && parser->tokens.kinds[after] == PARSE_KIND_IDENTIFIER;
                const uint32_t declaration = named ? after : token;
                const uint32_t body = check_input_tag_body(parser, token, count);
                const uint32_t close = body < count && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE ? parser->tree.parents[body] : count;
                reader.tag_types.dense[token] = staged.declared_types[declaration];
                reader.tag_ends.dense[token] = close < count ? close + 1 : named ? after + 1 : after;
            }
            const uint32_t open = token + 1;
            const uint32_t close = open < count && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN ? parser->tree.parents[open] : count;
            if (kind == PARSE_KIND_IDENTIFIER && close < count && staged.declared_types[token] == 0)
            {
                reader.builtins[close] = check_constant_builtin_name(&reader, parser->tokens.names[token]);
            }
        }
    }
    CheckWarningSpelling spelling = { 0 };
    for (uint32_t token = first; needed && valid && token < end; token += 1)
    {
        const uint32_t close = parser->tokens.kinds[token] == PARSE_KIND_L_PAREN ? parser->tree.parents[token] : end;
        const bool type_start = token + 1 < close && close < end && check_driver_place_starts(parser, input, token + 1);
        bool attributed = false;
        for (uint32_t t = type_start ? token + 1 : close; !attributed && t < close; t += 1)
        {
            attributed = check_input_attribute_starts(parser, t, close);
        }
        if (attributed && check_semantic_requirement_type_shape(parser, input, token + 1, close))
        {
            const uint64_t mark = temporary.length;
            CheckConstantInput query;
            bool read = check_constant_input_initialize(&query, &reader, token + 1, close, true, NULL);
            query.raw = true;
            query.scoped_names = true;
            read = read && check_constant_raw_bind(&query, true);
            // Only integer-expression operands with retained bindings enter the evaluator. Active strings, compound
            // values, calls and selections need scratch columns not retained by diagnostics. Ignored string attributes
            // remain syntax and can still disappear from the printed type. These other forms keep source spelling.
            for (uint32_t t = token + 1; read && t < close; t += 1)
            {
                const uint8_t kind = parser->tokens.kinds[t];
                const bool syntax = query.attribute_syntax[t - query.first] != 0;
                const bool active = !syntax && query.syntax[t - query.first] == 0;
                const uint32_t declaration = kind == PARSE_KIND_IDENTIFIER ? staged.declarations[t] : BUSTER_CHECK_NONE;
                const bool identifier = kind != PARSE_KIND_IDENTIFIER || syntax ||
                    (declaration < count && (staged.declaration_kinds[declaration] == C_BINDING_ENUMERATOR ||
                                             staged.declaration_kinds[declaration] == C_BINDING_TYPEDEF ||
                                             staged.declaration_kinds[declaration] == C_BINDING_TAG_KIND));
                read = identifier && (!active || (kind != PARSE_KIND_STRING && kind != PARSE_KIND_CHARACTER && kind != PARSE_KIND_L_BRACE &&
                                                 kind != PARSE_KIND_R_BRACE && kind != PARSE_KIND_TYPE_ARGUMENT && kind != PARSE_KIND_GENERIC));
            }
            read = read && check_constant_raw_prepare(&query);
            TypeId type = read ? check_constant_type(&query, token + 1, close) : 0;
            type = type != 0 ? check_semantic_requirement_pointer_types(&query, type) : 0;
            valid = !staged.resource;
            if (valid && type != 0)
            {
                valid = check_driver_spelling_ready(parser, pp, input, types, &spelling, scratch);
                const TypeNames names = {
                    .bytes = spelling.bytes,
                    .lengths = spelling.lengths,
                    .count = spelling.count,
                    .anonymous = spelling.anonymous,
                    .anonymous_count = spelling.anonymous_count,
                };
                const String8 text = valid ? type_print(&copied, scratch, type, (String8){ 0 }, &names,
                    parser->language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0) : (String8){ 0 };
                const char* replacement = text.length != 0 ? check_warning_text(scratch, "%.*s", (int)text.length, text.pointer) : NULL;
                valid = replacement != NULL;
                if (valid)
                {
                    spelled[token + 1 - first] = replacement;
                    for (uint32_t t = token + 2; t < close; t += 1)
                    {
                        spelled[t - first] = "";
                    }
                    token = close;
                }
            }
            arena_pop_to(&temporary, mark);
        }
    }
    arena_release(&temporary);
    arena_release(&index);
    arena_release(&storage);
    return valid;
}

// A failed static assertion's error, as clang's CheckStaticAssert gives it (#1447). Its requirement is the first term of
// the condition's conjunction that is no integer literal and is false (findFailedBooleanCondition), else the whole
// condition, printed as clang's printer prints it; an integer literal's is the plain "static assertion failed" at the
// condition's first token. A binary requirement but `||` notes its operands' values where either is worth printing
// (DiagnoseStaticAssertDetails), at its operator.
static const char* check_semantic_assert(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                        const TypeTable* types, const CheckReport* report, uint32_t* location, PPOutputDiagnosticNote* notes,
                                        uint32_t* note_count, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t keyword = check_semantic_assertion_keyword(parser, report->token);
    const uint32_t close = keyword < count && keyword + 1 < count ? parser->tree.parents[keyword + 1] : count;
    const char* result = "static assertion failed";
    if (keyword < count && close < count)
    {
        const uint32_t begin = keyword + 2;
        uint32_t first = begin;
        uint32_t end = check_constant_punctuation(parser, first, close, PARSE_KIND_COMMA);
        const uint32_t strings = end + 1;
        const CheckGraph* graph = &input->graph;
        const uint32_t root = report->node < graph->count && report->node < stream->count ? check_semantic_unselected(graph, report->node) : BUSTER_CHECK_NONE;
        uint32_t* stack = root != BUSTER_CHECK_NONE ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->count) : NULL;
        uint32_t* literals = stack != NULL && end > first ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, end - first) : NULL;
        const char** spelled = literals != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, const char*, end - first) : NULL;
        // The conjunction's terms, left to right, under the implicit conversions of each `&&`'s operands.
        uint32_t term = BUSTER_CHECK_NONE;
        uint32_t top = 0;
        if (spelled != NULL)
        {
            stack[top++] = root;
        }
        while (top != 0 && term == BUSTER_CHECK_NONE)
        {
            const uint32_t at = check_semantic_unselected(graph, stack[--top]);
            if (at < graph->count && graph->ops[at] == CHECK_OP_LOGICAL_AND && top + 2 <= graph->count)
            {
                stack[top++] = graph->operand1[at];
                stack[top++] = graph->operand0[at];
            }
            else if (at < stream->count && !check_semantic_integer_literal(parser, graph, stream, at) && check_semantic_fails(stream, at))
            {
                term = at;
            }
        }
        const uint32_t failed = term != BUSTER_CHECK_NONE ? term : root;
        for (uint32_t i = 0; spelled != NULL && i < end - first; i += 1)
        {
            literals[i] = BUSTER_CHECK_NONE;
            spelled[i] = NULL;
        }
        // The term's tokens: its parse subtree's, which leaves out the parentheses around it.
        uint32_t low = BUSTER_CHECK_NONE;
        uint32_t high = BUSTER_CHECK_NONE;
        if (spelled != NULL && failed < graph->count)
        {
            check_semantic_subtree(graph, failed, first, end, literals, stack, &low, &high);
        }
        if (term != BUSTER_CHECK_NONE && low < end && high < end)
        {
            uint32_t ancestor = low;
            for (uint32_t steps = 0; ancestor < count && !check_semantic_under(parser, high, ancestor) && steps < count; steps += 1)
            {
                ancestor = parser->tree.parents[ancestor];
            }
            uint32_t term_first = low;
            uint32_t term_last = high;
            while (ancestor >= first && ancestor < end && term_first > first && check_semantic_under(parser, term_first - 1, ancestor))
            {
                term_first -= 1;
            }
            while (ancestor >= first && ancestor < end && term_last + 1 < end && check_semantic_under(parser, term_last + 1, ancestor))
            {
                term_last += 1;
            }
            spelled += term_first - first;
            first = term_first;
            end = term_last + 1;
        }
        while (first + 1 < end && parser->tree.roles[first] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[first] == end - 1)
        {
            first += 1;
            spelled += spelled != NULL ? 1 : 0;
            end -= 1;
        }
        for (uint32_t t = first; spelled != NULL && t < end; t += 1)
        {
            const uint32_t node = literals[t - begin];
            spelled[t - first] = node < stream->count ? check_semantic_literal_spelling(parser, pp, types, graph, stream, node, scratch) : NULL;
        }
        // A compound literal's array of unknown size prints its completed type's bound, as clang's printer prints the
        // literal's type (#2482): `(int[]){1, 2}`'s `(int[2]){1, 2}`. Its type name's first `[]` takes the literal's length.
        for (uint32_t t = first; spelled != NULL && t < end; t += 1)
        {
            const uint32_t close = parser->tree.roles[t] == PARSE_ROLE_OPEN_COMPOUND ? parser->tree.parents[t] : BUSTER_CHECK_NONE;
            const bool braced = close + 1 < end && parser->tree.roles[close + 1] == PARSE_ROLE_OPEN_COMPOUND_BODY;
            const uint32_t body = braced ? parser->tree.parents[close + 1] : BUSTER_CHECK_NONE;
            const uint32_t node = body < end && body >= begin ? literals[body - begin] : BUSTER_CHECK_NONE;
            const TypeId type = node < stream->count ? type_strip_aligned(types, check_stream_type(stream, node)) : 0;
            uint32_t bracket = type != 0 && type_kind(types, type) == BUSTER_TYPE_KIND_ARRAY ? t + 1 : close;
            while (bracket < close && !(parser->tokens.kinds[bracket] == PARSE_KIND_L_SQUARE && parser->tokens.kinds[bracket + 1] == PARSE_KIND_R_SQUARE))
            {
                bracket += 1;
            }
            if (bracket < close)
            {
                spelled[bracket - first] = check_warning_text(scratch, "[%" PRIu64, types->types.extent[type_resolved_index(types, type)]);
            }
        }
        const bool literal = failed < stream->count ? check_semantic_integer_literal(parser, graph, stream, failed) :
                             first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_NUMBER;
        *location = literal ? begin : first;
        const bool type_names = literal || (spelled != NULL && check_semantic_requirement_types(parser, pp, input, types, first, end, spelled, scratch));
        const char* requirement = literal || !type_names ? NULL : check_semantic_requirement(parser, pp, first, end, spelled, scratch);
        // The note of the requirement's operands' values: a binary operator's but `||`'s.
        const CheckOp op = !literal && failed < graph->count ? graph->ops[failed] : CHECK_OP_COUNT;
        const bool binary = ((op >= CHECK_OP_ADD && op <= CHECK_OP_LOGICAL_AND) || op == CHECK_OP_COMMA) && graph->tokens[failed] < count;
        const uint32_t left = binary ? check_semantic_unselected(graph, graph->operand0[failed]) : BUSTER_CHECK_NONE;
        const uint32_t right = binary ? check_semantic_unselected(graph, graph->operand1[failed]) : BUSTER_CHECK_NONE;
        if (left < stream->count && right < stream->count && (check_semantic_useful(parser, graph, left) || check_semantic_useful(parser, graph, right)))
        {
            const CheckValue values[2] =
            {
                check_semantic_operand_value(types, graph, stream, left, failed),
                check_semantic_operand_value(types, graph, stream, right, failed),
            };
            char texts[2][192];
            const String8 operator = check_semantic_token(parser, pp, graph->tokens[failed], scratch);
            // (A comparison of a bool with C23's true or false prints nothing.)
            const bool boolean = (check_semantic_integer_literal(parser, graph, stream, left) && values[0].type == BUSTER_TYPE_BOOL &&
                                  type_unqualified(type_strip_aligned(types, values[1].type)) == BUSTER_TYPE_BOOL) ||
                                 (check_semantic_integer_literal(parser, graph, stream, right) && values[1].type == BUSTER_TYPE_BOOL &&
                                  type_unqualified(type_strip_aligned(types, values[0].type)) == BUSTER_TYPE_BOOL);
            if (!boolean && operator.length != 0 && check_semantic_value_text(texts[0], sizeof(texts[0]), types, &values[0]) &&
                check_semantic_value_text(texts[1], sizeof(texts[1]), types, &values[1]))
            {
                check_semantic_add_note(parser, notes, note_count, graph->tokens[failed],
                                        check_warning_text(scratch, "expression evaluates to '%s %.*s %s'", texts[0], (int)operator.length, operator.pointer, texts[1]));
            }
        }
        const bool message = strings < close;
        CheckStringPart* parts = message ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckStringPart, close - strings) : NULL;
        bool valid = literal || requirement != NULL;
        valid &= !message || parts != NULL;
        for (uint32_t token = strings; valid && token < close; token += 1)
        {
            const String8 part = check_semantic_token(parser, pp, token, scratch);
            valid = part.length != 0 && part.length <= UINT32_MAX && parser->tokens.kinds[token] == PARSE_KIND_STRING;
            parts[token - strings] = (CheckStringPart){ .bytes = (const uint8_t*)part.pointer, .length = (uint32_t)part.length };
        }
        // (An unevaluated string literal, whose encoding prefixes have no effect, #1447.)
        if (valid && message)
        {
            check_literal_unevaluated_parts(parts, close - strings);
        }
        CheckStringLiteral decoded = valid && message ? check_literal_strings(parts, close - strings, parser->language.standard >= 2023, scratch) : (CheckStringLiteral){ 0 };
        // A message of escapes clang takes that the fast decoding refuses (#1447: a delimited one, a named universal
        // character name, an unknown one), read as clang reads them.
        if (valid && message && !decoded.valid)
        {
            decoded = check_literal_strings_extended(parts, close - strings, parser->language.standard >= 2023, scratch);
        }
        valid &= !message || (decoded.valid && decoded.byte_count != 0 && decoded.byte_count - 1 <= INT_MAX);
        result = valid ? check_warning_text(scratch, "static assertion failed%s%s%s%s%.*s",
                                            literal ? "" : " due to requirement '", literal ? "" : requirement, literal ? "" : "'",
                                            message ? ": " : "", message ? (int)(decoded.byte_count - 1) : 0, message ? (const char*)decoded.bytes : "") : NULL;
    }
    (void)input;
    return check_semantic_text(pp, result);
}

// The declarator name token of member `member` (a TypeMembers index) in its own record's definition (#1447), the record
// whose members hold it (an anonymous member's record for a name found through it): as check_lower_dump_member finds one,
// an identifier at the body's own brace depth, of the member's name, that declares itself with the kind byte a member's
// declarator leaves 0 (TYPEDEF's). The body is the one CheckGraph.tag_completions ends. BUSTER_CHECK_NONE where the
// record has no body in the unit or none is found. A diagnostic's cold scan: the tags, then the body.
static uint32_t check_semantic_member_token(const Parser* parser, const CheckInput* input, const TypeTable* types, uint32_t member)
{
    const CheckGraph* graph = &input->graph;
    uint32_t tag = 0;
    for (uint32_t t = 1; tag == 0 && member != 0 && member < types->members.count && t < types->tags.count; t += 1)
    {
        tag = member >= types->tags.member_begin[t] && member - types->tags.member_begin[t] < types->tags.member_count[t] ? t : 0;
    }
    const uint32_t completion = tag != 0 && graph->tag_completions != NULL && tag < graph->tag_completion_count ? graph->tag_completions[tag] : BUSTER_CHECK_NONE;
    const uint32_t close = completion != BUSTER_CHECK_NONE && completion > 0 && completion <= parser->tokens.count && completion <= input->token_count &&
                           parser->tokens.kinds[completion - 1] == PARSE_KIND_R_BRACE ? completion - 1 : BUSTER_CHECK_NONE;
    // The body's `{`, back from its `}` over the braces of the records its members define.
    uint32_t open = close;
    uint32_t depth = 0;
    bool scanning = close != BUSTER_CHECK_NONE;
    while (scanning && open > 0)
    {
        const uint8_t kind = parser->tokens.kinds[open];
        depth = kind == PARSE_KIND_R_BRACE ? depth + 1 : kind == PARSE_KIND_L_BRACE ? depth - 1 : depth;
        scanning = depth != 0;
        open -= scanning ? 1 : 0;
    }
    const uint32_t name = tag != 0 ? types->members.name[member] : 0;
    const bool known = !scanning && close != BUSTER_CHECK_NONE && name != 0 && input->declarations != NULL && input->declaration_kinds != NULL;
    uint32_t result = BUSTER_CHECK_NONE;
    depth = 0;
    for (uint32_t t = known ? open + 1 : 0; known && result == BUSTER_CHECK_NONE && t < close; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        depth = kind == PARSE_KIND_L_BRACE ? depth + 1 : kind == PARSE_KIND_R_BRACE && depth != 0 ? depth - 1 : depth;
        result = depth == 0 && kind == PARSE_KIND_IDENTIFIER && parser->tokens.names[t] == name && input->declarations[t] == t &&
                 input->declaration_kinds[t] == C_BINDING_TYPEDEF ? t : BUSTER_CHECK_NONE;
    }
    return result;
}

// The member an initializer's subobject of type `target` at bit `offset` of an `object` is, where it is one (#1842's 3c:
// a counted_by family member's sugar), as check_semantic_subobject_spelling walks to it; 0 for an element or none.
static uint32_t check_semantic_subobject_member(const TypeTable* types, TypeId object, uint64_t offset, TypeId target)
{
    const TypeId wanted = target != 0 ? type_unqualified(type_strip_aligned(types, target)) : 0;
    TypeId level = object;
    uint32_t member = 0;
    bool reached = false;
    bool walking = wanted != 0 && type_is_valid(types, object);
    for (uint32_t depth = 0; walking && depth < 32; depth += 1)
    {
        const TypeId bare = type_unqualified(type_strip_aligned(types, level));
        const uint8_t kind = type_kind(types, bare);
        reached = bare == wanted && offset == 0;
        walking = !reached;
        if (walking && (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY))
        {
            const TypeId element = type_element(types, bare);
            const uint64_t bits = type_size(types, element) * 8;
            walking = bits != 0;
            offset = bits != 0 ? offset % bits : offset;
            level = element;
            member = 0;
        }
        else if (walking && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
        {
            const uint32_t tag = types->types.index[type_index(bare)];
            const uint32_t begin = tag < types->tags.count ? types->tags.member_begin[tag] : 0;
            const uint32_t count = tag < types->tags.count ? types->tags.member_count[tag] : 0;
            uint32_t found = 0;
            for (uint32_t m = begin; m < begin + count; m += 1)
            {
                const TypeId member_type = types->members.type[m];
                const uint64_t start = types->members.offset[m];
                const uint64_t width = (types->members.flags[m] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 ? types->members.bit_width[m] : type_size(types, member_type) * 8;
                const bool holds = start <= offset && offset - start < (width != 0 ? width : 1);
                const bool exact = holds && type_unqualified(type_strip_aligned(types, member_type)) == wanted && offset == start;
                found = found == 0 && holds && (kind == BUSTER_TYPE_KIND_STRUCT || exact) ? m : found;
                found = kind == BUSTER_TYPE_KIND_UNION && exact && found == 0 ? m : found;
            }
            walking = found != 0;
            offset -= found != 0 ? types->members.offset[found] : 0;
            level = found != 0 ? types->members.type[found] : level;
            member = found;
        }
        else
        {
            walking = false;
        }
    }
    return reached ? member : 0;
}

// The typedef that spells the subobject an initializer's value converts to (#1447): from the initialized object's type
// (`object`, declared at `declaration`, whose specifiers spell its own elements), down by `offset` bits through arrays'
// elements and records' members to a level of type `target`, each member's declarator spelling it in turn
// (check_semantic_member_token): `struct S s = { .lp = c }`'s member `LP lp` is 'LP'. A union's member is the first of
// the target's type. 0 where the walk doesn't reach the target. A diagnostic's cold walk.
static uint32_t check_semantic_subobject_spelling(const Parser* parser, const CheckInput* input, const TypeTable* types, TypeId object,
                                                  uint32_t declaration, uint64_t offset, TypeId target)
{
    const TypeId wanted = target != 0 ? type_unqualified(type_strip_aligned(types, target)) : 0;
    uint32_t spelling = declaration < input->token_count ? check_input_specifier_spelling(input, parser, declaration) : 0;
    TypeId level = object;
    bool reached = false;
    bool walking = wanted != 0 && type_is_valid(types, object);
    for (uint32_t depth = 0; walking && depth < 32; depth += 1)
    {
        const TypeId bare = type_unqualified(type_strip_aligned(types, level));
        const uint8_t kind = type_kind(types, bare);
        reached = bare == wanted && offset == 0;
        walking = !reached;
        if (walking && (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY))
        {
            const TypeId element = type_element(types, bare);
            const uint64_t bits = type_size(types, element) * 8;
            walking = bits != 0;
            offset = bits != 0 ? offset % bits : offset;
            level = element;
        }
        else if (walking && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
        {
            const uint32_t tag = types->types.index[type_index(bare)];
            const uint32_t begin = tag < types->tags.count ? types->tags.member_begin[tag] : 0;
            const uint32_t count = tag < types->tags.count ? types->tags.member_count[tag] : 0;
            uint32_t found = 0;
            for (uint32_t m = begin; m < begin + count; m += 1)
            {
                const TypeId member_type = types->members.type[m];
                const uint64_t start = types->members.offset[m];
                const uint64_t width = (types->members.flags[m] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 ? types->members.bit_width[m] : type_size(types, member_type) * 8;
                const bool holds = start <= offset && offset - start < (width != 0 ? width : 1);
                const bool exact = holds && type_unqualified(type_strip_aligned(types, member_type)) == wanted && offset == start;
                found = found == 0 && holds && (kind == BUSTER_TYPE_KIND_STRUCT || exact) ? m : found;
                found = kind == BUSTER_TYPE_KIND_UNION && exact && found == 0 ? m : found;
            }
            // (A union none of whose members is the target's type: its first that holds the offset.)
            for (uint32_t m = begin; found == 0 && kind == BUSTER_TYPE_KIND_UNION && m < begin + count; m += 1)
            {
                found = types->members.offset[m] <= offset ? m : 0;
            }
            const uint32_t declarator = found != 0 ? check_semantic_member_token(parser, input, types, found) : BUSTER_CHECK_NONE;
            spelling = declarator < input->token_count ? check_input_specifier_spelling(input, parser, declarator) : 0;
            walking = found != 0;
            offset -= found != 0 ? types->members.offset[found] : 0;
            level = found != 0 ? types->members.type[found] : level;
        }
        else
        {
            walking = false;
        }
    }
    return reached ? spelling : 0;
}

// The member a `.` or `->` node (`node`) selects (#1447), a TypeMembers index found through anonymous members as the
// action's lookup finds it, by the name after the operator and its record operand's type; 0 for none.
static uint32_t check_semantic_member_of(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const bool member = op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW;
    const uint32_t base = member ? graph->operand0[node] : BUSTER_CHECK_NONE;
    const TypeId base_type = base < stream->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, base))) : 0;
    const TypeId pointee = op == CHECK_OP_ARROW && base_type != 0 && type_kind(types, base_type) == BUSTER_TYPE_KIND_POINTER ?
                           type_unqualified(type_strip_aligned(types, type_element(types, base_type))) : 0;
    const TypeId record = op == CHECK_OP_ARROW ? pointee : base_type;
    const uint8_t kind = record != 0 ? type_kind(types, record) : BUSTER_TYPE_KIND_VOID;
    const uint32_t name_token = member ? graph->tokens[node] + 1 : BUSTER_CHECK_NONE;
    const uint32_t name = name_token < parser->tokens.count && parser->tokens.kinds[name_token] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[name_token] : 0;
    uint64_t offset = 0;
    return (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && name != 0 ? type_find_member(types, record, name, &offset) : 0;
}

// The typedef-specifier declaration an operand's type is spelled with (check_input_typedef_spelling), or 0: its object's
// declaration's, through parentheses, dereferences and subscripts to the named object or the member a `.` or `->`
// selects (#1447, its declarator in its record's body, check_semantic_member_token). Clang's type keeps that sugar:
// `(*p)++` of a `v4si *p` prints 'v4si', `x.f++` of a member `uu f` 'uu'; check_driver_warning_type_at finds the
// typedef of the declaration's chain that names the operand's level (`*p` of a `pv p`, `typedef v4si *pv`).
static uint32_t check_semantic_operand_spelling(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    uint32_t result = 0;
    if (node < graph->count && node < stream->count && (graph->ops[node] == CHECK_OP_MEMBER || graph->ops[node] == CHECK_OP_ARROW) &&
        (check_stream_value(stream, node).flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0)
    {
        const TypeId type = type_unqualified(type_strip_aligned(types, check_stream_type(stream, node)));
        const bool aliases = type != 0 && type_kind(types, type) == BUSTER_TYPE_KIND_EXT_VECTOR && input->declarations != NULL &&
                             input->declaration_kinds != NULL && input->declared_types != NULL && input->objects != NULL &&
                             check_sparse_wide_present(input->attributes);
        // Clang names a swizzle's vector result with the first matching extended-vector typedef declared so far.
        // This is diagnostic sugar only; the value keeps its interned vector type and component flags.
        const uint32_t before = graph->tokens[node] < input->token_count ? graph->tokens[node] : input->token_count;
        for (uint32_t t = 0; aliases && result == 0 && t < before; t += 1)
        {
            if (parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && input->declarations[t] == t && input->objects[t] != 0 &&
                input->declaration_kinds[t] == C_BINDING_TYPEDEF &&
                type_unqualified(type_strip_aligned(types, input->declared_types[t])) == type)
            {
                const CheckInputAttributes* attributes = check_input_attributes_at(input, t);
                const uint32_t attribute_count = attributes != NULL ? attributes->count : 0;
                for (uint32_t a = 0; result == 0 && a < attribute_count; a += 1)
                {
                    const CheckInputAttribute* attribute = attributes->records + a;
                    result = attribute->kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE && attribute->neon == 0 ? t : 0;
                }
            }
        }
    }
    uint32_t at = node;
    for (uint32_t steps = 0; result == 0 && at < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[at];
        const uint32_t token = graph->tokens[at];
        const bool member = op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW;
        const uint32_t selected = member ? check_semantic_member_of(parser, input, stream, types, at) : 0;
        const uint32_t declaration = member ? (selected != 0 ? check_semantic_member_token(parser, input, types, selected) : BUSTER_CHECK_NONE) :
                                     token < input->token_count && input->declarations != NULL && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ?
                                     input->declarations[token] : 0;
        result = declaration != 0 && declaration < input->token_count ? check_input_typedef_sugar(input, parser, declaration) : 0;
        const bool through = declaration == 0 && (op == CHECK_OP_VALUE || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT);
        at = result == 0 && through ? graph->operand0[at] : BUSTER_CHECK_NONE;
    }
    return result;
}

// Invalid operators keep an operand's written type, including a cast's type name and a call's return typedef. This
// spelling is needed only after an action has rejected the expression; it does not change the typed stream.
static uint32_t check_semantic_operator_spelling(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    uint32_t pending[64];
    uint32_t pending_count = 1;
    pending[0] = node;
    uint32_t steps = 0;
    uint32_t spelling = 0;
    uint32_t declared = 0;
    bool same = true;
    while (same && pending_count != 0 && steps < graph->count)
    {
        node = pending[--pending_count];
        uint32_t value_spelling = 0;
        while (same && value_spelling == 0 && node < graph->count && steps < graph->count)
        {
            steps += 1;
            value_spelling = check_input_operand_type_spelling(input, parser, node);
            const CheckOp op = graph->ops[node];
            if (value_spelling == 0 && op == CHECK_OP_CALL)
            {
                const uint32_t callee = check_driver_operand_declaration(input, stream, graph->operand0[node]);
                value_spelling = callee != 0 && callee < input->token_count ? check_input_specifier_spelling(input, parser, callee) : 0;
            }
            value_spelling = value_spelling != 0 ? value_spelling : check_semantic_operand_spelling(parser, input, stream, types, node);
            if (value_spelling == 0 && op == CHECK_OP_CONDITIONAL)
            {
                // A conditional keeps sugar only when both arms spell the same typedef. A bounded worklist handles
                // nested selections; exhaustion leaves the canonical type spelling, never a guessed alias.
                same = pending_count < BUSTER_ARRAY_LENGTH(pending);
                if (same)
                {
                    pending[pending_count++] = graph->operand2[node];
                }
                node = graph->operand1[node];
            }
            else
            {
                // Grouping, a comma's right operand and a generic's selected value keep their sugar.
                node = op == CHECK_OP_COMMA ? graph->operand1[node] : op == CHECK_OP_VALUE || op == CHECK_OP_SELECT ? graph->operand0[node] : BUSTER_CHECK_NONE;
            }
        }
        uint32_t written = 0;
        const uint32_t value_declared = check_input_sugar_declaration(input, parser, value_spelling, &written);
        same &= value_spelling != 0 && (declared == 0 || value_declared == declared);
        spelling = declared == 0 ? value_spelling : spelling;
        declared = value_declared;
    }
    return same && pending_count == 0 ? spelling : 0;
}

// Clang expands an identical typedef only once in one diagnostic. Distinct spellings of the same underlying type each
// keep their own expansion; this only shortens the second of two identical quoted type descriptions.
static String8 check_semantic_repeated_type(TypeId first_type, String8 first, TypeId second_type, String8 second)
{
    if (first_type == second_type && first.length != 0 && first.length == second.length && memcmp(first.pointer, second.pointer, first.length) == 0)
    {
        for (uint64_t i = 0; i + 6 < second.length; i += 1)
        {
            if (memcmp(second.pointer + i, " (aka ", 6) == 0)
            {
                second.length = i;
                break;
            }
        }
    }
    return second;
}

// A swizzle adopts the matching extended-vector typedef's name even when that declaration carries alignment.
// Supply its underlying type as the alias key without changing the result's type or generic typedef printing.
static String8 check_semantic_vector_type(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                          const TypeTable* types, CheckWarningSpelling* spell, Arena* scratch, TypeId type, uint32_t spelling, uint32_t node)
{
    const TypeId vector = type_unqualified(type_strip_aligned(types, type));
    const bool named = vector != 0 && type_kind(types, vector) == BUSTER_TYPE_KIND_EXT_VECTOR && node < stream->count &&
                       (check_stream_value(stream, node).flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0 && spelling != 0 &&
                       spelling < input->token_count && input->declared_types != NULL;
    String8 result = { 0 };
    if (named)
    {
        const TypeId alias = type_strip_alignment(types, input->declared_types[spelling]);
        const String8 name = check_semantic_name(parser, pp, spelling);
        const TypeNames names = { .alias_types = &alias, .alias_names = &name, .alias_count = 1 };
        const uint32_t options = pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0;
        const String8 sugared = type_print(types, scratch, type, (String8){ 0 }, &names, options);
        const String8 element = check_driver_warning_type(parser, pp, input, types, spell, scratch, check_vector_element(types, vector), 0);
        const uint64_t count = types->types.extent[type_index(vector)];
        const char* text = sugared.length != 0 && element.length != 0 ?
                           check_warning_text(scratch, "'%.*s' (vector of %llu %.*s %s)", (int)sugared.length, sugared.pointer,
                                              (unsigned long long)count, (int)element.length, element.pointer, count > 1 ? "values" : "value") : NULL;
        result = text != NULL ? (String8){ .pointer = (char*)text, .length = strlen(text) } : (String8){ 0 };
    }
    else
    {
        result = check_driver_warning_type(parser, pp, input, types, spell, scratch, type, spelling);
    }
    return result;
}

// c_check_common_warnings.c's, included after this file.
static uint32_t check_common_subtree_start(const Parser* parser, uint32_t root);

// Whether a type is const-qualified as clang's QualType::isConstQualified has it: an array's qualifiers are its
// innermost element's.
static bool check_semantic_const(const TypeTable* types, TypeId type)
{
    bool constant = type != 0 && (type_qualifiers(type) & BUSTER_TYPE_CONST) != 0;
    TypeId level = type;
    for (uint32_t depth = 0; !constant && level != 0 && depth < 64 && type_kind_is_array(check_action_kind(types, level)); depth += 1)
    {
        level = type_element(types, type_unqualified(type_strip_aligned(types, level)));
        constant = level != 0 && (type_qualifiers(level) & BUSTER_TYPE_CONST) != 0;
    }
    return constant;
}

// clang's IsTypeModifiable: a level reached through a dereference (an outer `->`) is its pointer's target.
static bool check_semantic_modifiable(const TypeTable* types, TypeId type, bool dereference)
{
    const bool pointer = dereference && type != 0 && check_action_kind(types, type) == BUSTER_TYPE_KIND_POINTER;
    const TypeId level = pointer ? type_element(types, type_unqualified(type_strip_aligned(types, type))) : type;
    return !check_semantic_const(types, level);
}

// Appends a note to a report's notes (BUSTER_CHECK_SEMANTIC_NOTES of them, the rest dropped), at `token`'s source.
static void check_semantic_add_note(const Parser* parser, PPOutputDiagnosticNote* notes, uint32_t* count, uint32_t token, const char* message)
{
    if (*count < BUSTER_CHECK_SEMANTIC_NOTES && token < parser->tokens.count && message != NULL)
    {
        notes[*count] = (PPOutputDiagnosticNote){ .token = parse_source_index(&parser->tokens, token), .message = message };
        *count += 1;
    }
}

// VarDecl/FieldDecl source ranges include the declaration specifiers and any initializer, but not their separator.
// A record's enclosing opening brace is a boundary here, while a record definition in the specifiers is retained.
static void check_semantic_declaration_note(const Parser* parser, const CheckInput* input, PPOutputDiagnosticNote* notes, uint32_t* count,
                                           uint32_t token, const char* message)
{
    const uint32_t prior = *count;
    check_semantic_add_note(parser, notes, count, token, message);
    if (*count != prior)
    {
        uint32_t first = check_input_declaration_begin(parser, token);
        for (uint32_t at = first; at < token; at += 1)
        {
            if (parser->tree.roles[at] == PARSE_ROLE_OPEN_STRUCT && parser->tree.parents[at] > token)
            {
                first = at + 1;
            }
        }
        const uint32_t initialized = token < input->token_count ? check_sparse_get(input->initializer_ends, token) : BUSTER_CHECK_NONE;
        const uint32_t end = initialized > token && initialized <= parser->tokens.count ? initialized : check_input_availability_declarator_end(parser, token);
        const uint32_t last = end > token && end <= parser->tokens.count ? end - 1 : token;
        notes[prior].ranges[0] = (PPDiagnosticRange)
        {
            .begin = parse_source_index(&parser->tokens, first),
            .end = parse_source_index(&parser->tokens, last),
            .pending = PP_RANGE_TOKENS,
        };
        notes[prior].range_count = 1;
    }
}

// The first token of a function declaration's return type, as clang's getReturnTypeSourceRange begins it: its
// specifiers' first type specifier, past storage classes, qualifiers and attributes (`const struct s *f(void)`'s
// `struct`). The declaration's own first token where none is found.
static uint32_t check_semantic_return_type_token(const Parser* parser, const CheckInput* input, uint32_t declaration)
{
    const uint32_t begin = check_input_declaration_begin(parser, declaration);
    uint32_t result = BUSTER_CHECK_NONE;
    uint32_t depth = 0;
    for (uint32_t t = begin; result == BUSTER_CHECK_NONE && t < declaration; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const uint32_t resolved = kind == PARSE_KIND_IDENTIFIER && t < input->token_count && input->declarations != NULL ? input->declarations[t] : 0;
        const bool name = resolved != 0 && resolved != t && resolved < input->token_count && input->declaration_kinds != NULL &&
                          input->declaration_kinds[resolved] == C_BINDING_TYPEDEF;
        const bool specifier = kind == PARSE_KIND_TYPE || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_TYPEOF ||
                               kind == PARSE_KIND_ATOMIC || name;
        result = depth == 0 && specifier ? t : BUSTER_CHECK_NONE;
        depth = kind == PARSE_KIND_L_PAREN ? depth + 1 : kind == PARSE_KIND_R_PAREN && depth != 0 ? depth - 1 : depth;
    }
    return result != BUSTER_CHECK_NONE ? result : begin;
}

// clang's DiagnoseConstAssignment (#1447): an assignment to a const lvalue names the first const level it finds
// through a chain of members and subscripts, then a variable or a direct call at its root, and every const level
// along it adds its note, outermost first: a member const itself (at its declarator, check_semantic_member_token),
// a variable declared const, a function returning a const value (at its return type's first specifier). A member
// reached through `->` is const by its pointer's target. "read-only variable is not assignable" where none is.
static const char* check_semantic_const_assignment(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                                   const TypeTable* types, CheckWarningSpelling* spell, uint32_t operand, PPOutputDiagnosticNote* notes,
                                                   uint32_t* note_count, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const char* result = NULL;
    uint32_t at = operand;
    bool dereference = false;
    bool next = false;
    bool walking = true;
    for (uint32_t steps = 0; walking && at < graph->count && steps < 256; steps += 1)
    {
        dereference = next;
        const CheckOp op = graph->ops[at];
        const uint32_t base = graph->operand0[at];
        const TypeId base_type = base < stream->count ? check_stream_type(stream, base) : 0;
        const uint8_t base_kind = base_type != 0 ? check_action_kind(types, base_type) : BUSTER_TYPE_KIND_NONE;
        const bool vector = base_kind == BUSTER_TYPE_KIND_VECTOR || base_kind == BUSTER_TYPE_KIND_EXT_VECTOR;
        if (op == CHECK_OP_MEMBER && vector)
        {
            // A vector's element (clang's ExtVectorElementExpr): its vector.
            at = base;
        }
        else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
        {
            next = op == CHECK_OP_ARROW;
            const uint32_t member = check_semantic_member_of(parser, input, stream, types, at);
            const TypeId field = member != 0 ? types->members.type[member] : 0;
            if (member != 0 && !check_semantic_modifiable(types, field, dereference))
            {
                const uint32_t declarator = check_semantic_member_token(parser, input, types, member);
                const String8 name = declarator < parser->tokens.count ? check_semantic_name(parser, pp, declarator) : (String8){ 0 };
                const uint32_t spelling = declarator < input->token_count ? check_input_typedef_spelling(input, parser, declarator) : 0;
                const String8 type = name.length != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, field, spelling) : (String8){ 0 };
                result = result != NULL || type.length == 0 ? result :
                         check_warning_text(scratch, "cannot assign to non-static data member '%.*s' with const-qualified type %.*s", (int)name.length,
                                            name.pointer, (int)type.length, type.pointer);
                check_semantic_declaration_note(parser, input, notes, note_count, declarator,
                                        name.length != 0 ? check_warning_text(scratch, "non-static data member '%.*s' declared const here", (int)name.length,
                                                                              name.pointer) : NULL);
            }
            walking = member != 0;
            at = walking ? graph->operand0[at] : at;
        }
        else if (op == CHECK_OP_SUBSCRIPT)
        {
            // Its base, the operand that is the array, the vector or the pointer (`i[a]` too).
            at = base_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(base_kind) || vector ? base : graph->operand1[at];
        }
        else
        {
            walking = false;
        }
    }
    const CheckOp root = at < graph->count ? graph->ops[at] : CHECK_OP_COUNT;
    const uint32_t token = at < graph->count ? graph->tokens[at] : BUSTER_CHECK_NONE;
    if (root == CHECK_OP_VALUE && token < input->token_count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && input->declarations != NULL)
    {
        const uint32_t declaration = input->declarations[token];
        const TypeId type = at < stream->count ? check_stream_type(stream, at) : 0;
        if (declaration < input->token_count && type != 0 && !check_semantic_modifiable(types, type, dereference))
        {
            // (A typo's variable is the one its correction names, as clang's recovery reads it.)
            const String8 name = check_driver_warning_name(pp, check_driver_corrected_name(parser, input, token, parser->tokens.names[token]));
            const String8 declared = name.length != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, type,
                                                                                   check_semantic_operand_spelling(parser, input, stream, types, at)) :
                                                        (String8){ 0 };
            result = result != NULL || declared.length == 0 ? result :
                     check_warning_text(scratch, "cannot assign to variable '%.*s' with const-qualified type %.*s", (int)name.length, name.pointer,
                                        (int)declared.length, declared.pointer);
            check_semantic_declaration_note(parser, input, notes, note_count, declaration,
                                    name.length != 0 ? check_warning_text(scratch, "variable '%.*s' declared const here", (int)name.length, name.pointer) :
                                                       NULL);
        }
    }
    else if (root == CHECK_OP_CALL)
    {
        // A direct call's (clang's getDirectCallee): its callee a function's name, through `*`, `&` and conversions
        // (`(*f)()`, `(&f)()`); a function pointer variable's call is no direct one.
        uint32_t callee = graph->operand0[at];
        for (uint32_t steps = 0; steps < 64 && callee < graph->count &&
                                 (graph->ops[callee] == CHECK_OP_DEREFERENCE || graph->ops[callee] == CHECK_OP_ADDRESS || graph->ops[callee] == CHECK_OP_CONVERT);
             steps += 1)
        {
            callee = graph->operand0[callee];
        }
        const uint32_t name_token = callee < graph->count && graph->ops[callee] == CHECK_OP_VALUE ? graph->tokens[callee] : BUSTER_CHECK_NONE;
        const uint32_t named = name_token < input->token_count && parser->tokens.kinds[name_token] == PARSE_KIND_IDENTIFIER && input->declarations != NULL ?
                               input->declarations[name_token] : BUSTER_CHECK_NONE;
        const uint32_t declaration = named < input->token_count && input->declaration_kinds != NULL && input->declaration_kinds[named] == C_BINDING_FUNCTION ?
                                     named : BUSTER_CHECK_NONE;
        const TypeId callee_type = callee < stream->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, callee))) : 0;
        const TypeId function = callee_type != 0 && type_kind(types, callee_type) == BUSTER_TYPE_KIND_POINTER ?
                                type_unqualified(type_strip_aligned(types, type_element(types, callee_type))) : callee_type;
        const bool direct = declaration < input->token_count && function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION;
        const TypeId returned = direct ? types->types.base[type_index(function)] : 0;
        if (direct && !check_semantic_modifiable(types, returned, dereference))
        {
            const String8 name = check_semantic_name(parser, pp, declaration);
            const String8 type = name.length != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, returned,
                                                                               check_input_typedef_spelling(input, parser, declaration)) : (String8){ 0 };
            result = result != NULL || name.length == 0 ? result :
                     check_warning_text(scratch, "cannot assign to return value because function '%.*s' returns a const value", (int)name.length, name.pointer);
            check_semantic_add_note(parser, notes, note_count, check_semantic_return_type_token(parser, input, declaration),
                                    type.length != 0 ? check_warning_text(scratch, "function '%.*s' which returns const-qualified type %.*s declared here",
                                                                          (int)name.length, name.pointer, (int)type.length, type.pointer) : NULL);
        }
    }
    return result != NULL ? result : "read-only variable is not assignable";
}

// clang's DiagnoseRecursiveConstFields (#1447): an assignment to a record with a const member somewhere in its
// hierarchy names the first one, breadth-first through its members' records, and notes each one, "nested" past the
// record's own members. The lvalue is named as a member (`.` or `->`), a variable, or "lvalue". NULL where none is
// const (the caller's then DiagnoseConstAssignment's). At most BUSTER_CHECK_SEMANTIC_RECORDS records are visited.
static const char* check_semantic_const_fields(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                               const TypeTable* types, uint32_t operand, TypeId record, PPOutputDiagnosticNote* notes,
                                               uint32_t* note_count, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const CheckOp op = operand < graph->count ? graph->ops[operand] : CHECK_OP_COUNT;
    const bool member = op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW;
    const uint32_t selected = member ? check_semantic_member_of(parser, input, stream, types, operand) : 0;
    const uint32_t declarator = selected != 0 ? check_semantic_member_token(parser, input, types, selected) : BUSTER_CHECK_NONE;
    const uint32_t token = operand < graph->count ? graph->tokens[operand] : BUSTER_CHECK_NONE;
    const bool variable = op == CHECK_OP_VALUE && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER;
    // A parenthesized lvalue is clang's "lvalue" (a ParenExpr): its `)` follows the name, a member's after its operator.
    const uint32_t last = member ? token + 1 : token;
    const bool parenthesized = last + 1 < parser->tokens.count && parser->tokens.kinds[last + 1] == PARSE_KIND_R_PAREN;
    const String8 name = parenthesized ? (String8){ 0 } : member ? check_semantic_name(parser, pp, declarator) :
                         variable ? check_semantic_name(parser, pp, token) : (String8){ 0 };
    const char* named = member && name.length != 0 ?
                        check_warning_text(scratch, "non-static data member '%.*s'", (int)name.length, name.pointer) :
                        variable && name.length != 0 ? check_warning_text(scratch, "variable '%.*s'", (int)name.length, name.pointer) : "lvalue";
    TypeId records[BUSTER_CHECK_SEMANTIC_RECORDS];
    uint32_t record_count = 1;
    records[0] = type_unqualified(type_strip_aligned(types, record));
    const char* result = NULL;
    for (uint32_t r = 0; named != NULL && r < record_count; r += 1)
    {
        const uint32_t tag = types->types.index[type_index(records[r])];
        const uint32_t begin = tag < types->tags.count ? types->tags.member_begin[tag] : 0;
        const uint32_t end = tag < types->tags.count ? begin + types->tags.member_count[tag] : 0;
        for (uint32_t m = begin; m < end; m += 1)
        {
            const TypeId field = types->members.type[m];
            if (check_semantic_const(types, field))
            {
                const uint32_t field_token = check_semantic_member_token(parser, input, types, m);
                const String8 field_name = field_token < parser->tokens.count ? check_semantic_name(parser, pp, field_token) : (String8){ 0 };
                result = result != NULL || field_name.length == 0 ? result :
                         check_warning_text(scratch, "cannot assign to %s with %sconst-qualified data member '%.*s'", named, r != 0 ? "nested " : "",
                                            (int)field_name.length, field_name.pointer);
                check_semantic_declaration_note(parser, input, notes, note_count, field_token,
                                        field_name.length != 0 ? check_warning_text(scratch, "%sdata member '%.*s' declared const here", r != 0 ? "nested " : "",
                                                                                    (int)field_name.length, field_name.pointer) : NULL);
            }
            const TypeId bare = field != 0 ? type_unqualified(type_strip_aligned(types, field)) : 0;
            const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_NONE;
            bool seen = !(kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) || record_count == BUSTER_CHECK_SEMANTIC_RECORDS;
            for (uint32_t i = 0; !seen && i < record_count; i += 1)
            {
                seen = records[i] == bare;
            }
            if (!seen)
            {
                records[record_count] = bare;
                record_count += 1;
            }
        }
    }
    return result;
}

// An operand's type as clang names it (#1447): an array's or a function's pointer (which the conversion made, so the
// table has it); an operator's operand's converted to a value (`value`), unqualified, without its extended qualifiers,
// where a conversion's source keeps its own.
static TypeId check_semantic_value_type(const TypeTable* types, TypeId type, bool value_only)
{
    TypeId value = type != 0 && value_only ? type_unqualified(type) : type;
    value = value != 0 && value_only && type_extended_qualifiers(types, value) != 0 ? type_unqualified(type_strip_aligned(types, value)) : value;
    const uint8_t kind = value != 0 ? type_kind(types, type_strip_aligned(types, value)) : BUSTER_TYPE_KIND_NONE;
    value = type_kind_is_array(kind) ? type_find_pointer(types, type_element(types, type_strip_aligned(types, value))) :
            kind == BUSTER_TYPE_KIND_FUNCTION ? type_find_pointer(types, value) : value;
    return value;
}

// The type an action actually read, before its rejection. Its conversion receipt distinguishes an atomic lvalue's
// loaded value type from the atomic type a cast rvalue retains, while preserving the ordinary decay and qualifier rules.
static TypeId check_semantic_operand_value_type(const TypeTable* types, const CheckStream* stream, uint32_t node, uint32_t operand, TypeId type)
{
    const TypeId converted = node < stream->count && stream->conversions != NULL ? stream->conversions[node].operands[operand].type : 0;
    return converted != 0 ? converted : check_semantic_value_type(types, type, true);
}

// clang's note at a called function's parameter (#1447, its "passing argument to parameter 'x' here"), where the callee is a
// function's name: at the `argument`-th parameter's name in the declaration the name finds, or just past an unnamed one,
// as clang places an abstract declarator's parameter.
static void check_semantic_parameter_note(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t callee, uint32_t argument,
                                          PPOutputDiagnosticNote* note, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t declaration = callee < input->token_count && parser->tokens.kinds[callee] == PARSE_KIND_IDENTIFIER ?
                                 check_driver_callee_declaration(input, callee) : BUSTER_CHECK_NONE;
    const bool function = declaration < input->token_count && input->declaration_kinds[declaration] == C_BINDING_FUNCTION && declaration + 1 < count &&
                          parser->tokens.kinds[declaration + 1] == PARSE_KIND_L_PAREN;
    const uint32_t close = function ? parser->tree.parents[declaration + 1] : 0;
    // The parameter's tokens: from after the `(` or the comma before it, to its comma or the `)`, groups skipped.
    uint32_t at = declaration + 2;
    uint32_t first = at;
    uint32_t end = at;
    bool found = false;
    for (uint32_t index = 0; function && !found && close < count && at <= close; index += 1)
    {
        first = at;
        while (at < close && parser->tokens.kinds[at] != PARSE_KIND_COMMA)
        {
            const uint8_t kind = parser->tokens.kinds[at];
            const bool open = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
            at = open && parser->tree.parents[at] > at && parser->tree.parents[at] < close ? parser->tree.parents[at] + 1 : at + 1;
        }
        end = at;
        found = index == argument && end > first;
        at += 1;
    }
    uint32_t name = BUSTER_CHECK_NONE;
    for (uint32_t t = first; found && name == BUSTER_CHECK_NONE && t < end; t += 1)
    {
        // An attribute's arguments are operands too (address_space's name is one): the parameter's is the first
        // operand identifier outside every attribute group.
        bool attribute = false;
        for (uint32_t at = t; !attribute && at < end;)
        {
            const uint32_t parent = parser->tree.parents[at];
            const uint8_t role = parent < parser->tokens.count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
            attribute = role == PARSE_ROLE_CLOSE_ATTRIBUTE || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ||
                        role == PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT || role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT ||
                        role == PARSE_ROLE_CLOSE_ATTRIBUTE_LIST;
            at = parent < parser->tokens.count && parent > at && parent < end ? parent : end;
        }
        name = !attribute && parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && parser->tree.roles[t] == PARSE_ROLE_OPERAND ? t : name;
    }
    if (found)
    {
        const String8 spelled = name != BUSTER_CHECK_NONE ? check_semantic_name(parser, pp, name) : (String8){ "", 0 };
        *note = (PPOutputDiagnosticNote){
            .token = parse_source_index(&parser->tokens, name != BUSTER_CHECK_NONE ? name : end),
            .message = spelled.length != 0 ? check_warning_text(scratch, "passing argument to parameter '%.*s' here", (int)spelled.length, spelled.pointer) :
                                             "passing argument to parameter here",
        };
    }
}

// The address spaces' errors (#1447), worded as clang's: a conversion's "changes address space of pointer" (an assignment's,
// an initialization's, a return's at its expression, an argument's) or of "nested pointer" one level down, and a
// subtraction's or a conditional's pointers to spaces that don't overlap
// (err_typecheck_op_on_nonoverlapping_address_space_pointers, its two spaces as clang prints them). An argument's has
// clang's note at the called function's parameter (check_semantic_parameter_note).
static const char* check_semantic_address_space(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                const CheckReport* report, CheckWarningSpelling* spell, uint32_t* location, PPOutputDiagnosticNote* note,
                                                Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t node = report->node;
    const uint32_t token = report->token;
    const CheckOp op = graph->ops[node];
    const char* result = NULL;
    uint32_t from_node = BUSTER_CHECK_NONE;
    TypeId from = 0;
    TypeId to = 0;
    const char* format = NULL;
    const char* nested = report->diagnostic == CHECK_DIAG_ADDRESS_SPACE_NESTED ? "nested " : "";
    if (report->diagnostic == CHECK_DIAG_ADDRESS_SPACE_OVERLAP)
    {
        const bool conditional = op == CHECK_OP_CONDITIONAL;
        const uint32_t first = conditional ? graph->operand1[node] : graph->operand0[node];
        const uint32_t second = conditional ? graph->operand2[node] : graph->operand1[node];
        const String8 left = first < stream->count ? check_driver_warning_type_declared(parser, pp, input, types, spell, scratch,
                                                                                check_semantic_value_type(types, check_stream_type(stream, first), true),
                                                                                check_semantic_operand_spelling(parser, input, stream, types, first),
                                                                                check_driver_operand_declaration(input, stream, first)) : (String8){ "", 0 };
        const String8 right = second < stream->count ? check_driver_warning_type_declared(parser, pp, input, types, spell, scratch,
                                                                                  check_semantic_value_type(types, check_stream_type(stream, second), true),
                                                                                  check_semantic_operand_spelling(parser, input, stream, types, second),
                                                                                  check_driver_operand_declaration(input, stream, second)) : (String8){ "", 0 };
        result = left.length != 0 && right.length != 0 ?
                 check_warning_text(scratch, "%s operands of type  (%.*s and %.*s) which are pointers to non-overlapping address spaces",
                                    conditional ? "conditional operator with the second and third" : "arithmetic operation with", (int)left.length, left.pointer,
                                    (int)right.length, right.pointer) : NULL;
        // A conditional's at its `?`, whose parent is the node's `:`.
        uint32_t question = token;
        while (conditional && question != 0 && question < parser->tokens.count &&
               !(parser->tree.roles[question] == PARSE_ROLE_OPEN_TERNARY && parser->tree.parents[question] == token))
        {
            question -= 1;
        }
        *location = conditional && question < parser->tokens.count && parser->tree.roles[question] == PARSE_ROLE_OPEN_TERNARY ? question : token;
    }
    else if (op == CHECK_OP_ASSIGN)
    {
        from_node = graph->operand1[node];
        to = graph->operand0[node] < stream->count ? check_stream_type(stream, graph->operand0[node]) : 0;
        format = "assigning %.*s to %.*s changes address space of %spointer";
    }
    else if (op == CHECK_OP_INITIALIZE)
    {
        from_node = graph->operand0[node];
        to = graph->types[node];
        format = "initializing %.*s with an expression of type %.*s changes address space of %spointer";
        for (uint32_t declaration = 0; declaration < input->token_count; declaration += 1)
        {
            if (check_sparse_get(input->initializer_nodes, declaration) == node)
            {
                *location = declaration;
                break;
            }
        }
    }
    else if (op == CHECK_OP_CONVERT && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_RETURN)
    {
        from_node = graph->operand0[node];
        to = graph->types[node];
        format = "returning %.*s from a function with result type %.*s changes address space of %spointer";
        *location = from_node < graph->count ? check_common_subtree_start(parser, graph->tokens[from_node]) : token;
    }
    else if (op == CHECK_OP_CALL && graph->argument_tokens != NULL)
    {
        const uint32_t offset = graph->argument_offsets[node];
        uint32_t argument = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; argument == BUSTER_CHECK_NONE && i < graph->argument_counts[node]; i += 1)
        {
            argument = graph->argument_tokens[offset + i] == token ? i : argument;
        }
        const uint32_t callee = graph->operand0[node];
        TypeId function = callee < stream->count ? type_strip_aligned(types, check_stream_type(stream, callee)) : 0;
        function = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, function)) : function;
        uint32_t parameter_count = 0;
        const TypeId* parameters = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION ? type_parameters(types, function, &parameter_count) : NULL;
        from_node = argument != BUSTER_CHECK_NONE ? graph->argument_nodes[offset + argument] : BUSTER_CHECK_NONE;
        to = argument < parameter_count ? parameters[argument] : 0;
        format = "passing %.*s to parameter of type %.*s changes address space of %spointer";
        check_semantic_parameter_note(parser, pp, input, callee < graph->count ? graph->tokens[callee] : BUSTER_CHECK_NONE, argument, note, scratch);
    }
    else if (op == CHECK_OP_BUILTIN_ATOMIC && graph->argument_tokens != NULL)
    {
        // A generic atomic builtin's value pointer in another address space than its object's (#2233): the pointer type its
        // check converted it to (CheckBuiltinAtomicEffect.space_target), at the argument.
        const uint32_t argument = stream->diagnostic_operands[node];
        for (uint32_t i = 0; i < stream->atomic_count; i += 1)
        {
            to = stream->atomics[i].node == node ? stream->atomics[i].space_target : to;
        }
        from_node = argument < graph->argument_counts[node] ? graph->argument_nodes[graph->argument_offsets[node] + argument] : BUSTER_CHECK_NONE;
        format = to != 0 ? "passing %.*s to parameter of type %.*s changes address space of %spointer" : NULL;
        *location = to != 0 ? graph->argument_tokens[graph->argument_offsets[node] + argument] : token;
    }
    if (format != NULL)
    {
        from = from_node < stream->count ? check_semantic_value_type(types, check_stream_type(stream, from_node), false) : 0;
        // Each with its declaration's own address space macros (#1447 item 2): the source's, and an assigned object's or an
        // initialized declarator's.
        const uint32_t from_declaration = check_driver_operand_declaration(input, stream, from_node);
        const uint32_t to_declaration = op == CHECK_OP_ASSIGN ? check_driver_operand_declaration(input, stream, graph->operand0[node]) :
                                        op == CHECK_OP_INITIALIZE ? *location : BUSTER_CHECK_NONE;
        const String8 source = from != 0 ? check_driver_warning_type_declared(parser, pp, input, types, spell, scratch, from,
                                                                             check_semantic_operand_spelling(parser, input, stream, types, from_node), from_declaration) :
                                           (String8){ "", 0 };
        const String8 target = to != 0 ? check_driver_warning_type_declared(parser, pp, input, types, spell, scratch, to, 0, to_declaration) : (String8){ "", 0 };
        const bool initializing = op == CHECK_OP_INITIALIZE;
        const String8 first = initializing ? target : source;
        const String8 second = initializing ? source : target;
        result = source.length != 0 && target.length != 0 ?
                 check_warning_text(scratch, format, (int)first.length, first.pointer, (int)second.length, second.pointer, nested) : NULL;
    }
    return result != NULL ? result : check_driver_report_message(report);
}

// The switch keyword whose condition `close` closes, or BUSTER_CHECK_NONE: the one before the condition's `(`.
static uint32_t check_semantic_switch(const Parser* parser, uint32_t close)
{
    uint32_t open = close;
    while (open != 0 && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == close))
    {
        open -= 1;
    }
    const bool found = open != 0 && parser->tokens.kinds[open - 1] == PARSE_KIND_SWITCH && parser->tree.parents[open - 1] == close;
    return found ? open - 1 : BUSTER_CHECK_NONE;
}

// A rejected scalar condition is diagnosed at its statement keyword. A for's condition is its second semicolon; a
// do's closing condition and its `do` head are children of the same final semicolon.
static uint32_t check_semantic_condition_keyword(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t close = token;
    if (close < count && parser->tree.roles[close] == PARSE_ROLE_TERMINATOR && parser->tree.parents[close] < count &&
        parser->tree.roles[parser->tree.parents[close]] == PARSE_ROLE_CLOSE_FOR)
    {
        close = parser->tree.parents[close];
    }
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t at = close; result == BUSTER_CHECK_NONE && at != 0 && at < count;)
    {
        at -= 1;
        const uint8_t kind = parser->tokens.kinds[at];
        const bool keyword = kind == PARSE_KIND_IF || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_FOR;
        result = keyword && parser->tree.parents[at] == close ? at : result;
    }
    const uint32_t parent = close < count ? parser->tree.parents[close] : count;
    if (result < count && parser->tokens.kinds[result] == PARSE_KIND_WHILE && parent < count &&
        parser->tree.roles[parent] == PARSE_ROLE_TERMINATOR)
    {
        for (uint32_t at = result; at != 0;)
        {
            at -= 1;
            if (parser->tree.roles[at] == PARSE_ROLE_DO && parser->tree.parents[at] == parent)
            {
                result = at;
                break;
            }
        }
    }
    return result != BUSTER_CHECK_NONE ? result : token;
}

static uint32_t check_semantic_conditional_question(const Parser* parser, uint32_t token)
{
    uint32_t question = token;
    while (question != 0 && question < parser->tokens.count &&
           !(parser->tree.roles[question] == PARSE_ROLE_OPEN_TERNARY && parser->tree.parents[question] == token))
    {
        question -= 1;
    }
    return question < parser->tokens.count && parser->tree.roles[question] == PARSE_ROLE_OPEN_TERNARY &&
           parser->tree.parents[question] == token ? question : token;
}

// `note`, where the message has one (its message NULL where not): a const variable's "declared const here" (#147).
// Clang's "forward declaration of 'struct S'" note of an incomplete tag (`bare`, unqualified), at the tag's declaring
// name: its first token that declares a tag of this type. None for a type of no such name.
static PPOutputDiagnosticNote check_semantic_forward_note(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                                          CheckWarningSpelling* spell, TypeId bare, Arena* scratch)
{
    PPOutputDiagnosticNote result = { 0 };
    const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_VOID;
    const bool tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
    const uint32_t name = tag ? types->tags.name[types->types.index[type_index(bare)]] : 0;
    uint32_t declared = BUSTER_CHECK_NONE;
    for (uint32_t t = 0; name != 0 && declared == BUSTER_CHECK_NONE && t < input->token_count; t += 1)
    {
        declared = parser->tokens.names[t] == name && input->declarations[t] == t && input->declaration_kinds[t] == C_BINDING_TAG_KIND &&
                   type_unqualified(input->declared_types[t]) == bare ? t : BUSTER_CHECK_NONE;
    }
    const String8 named = declared != BUSTER_CHECK_NONE ? check_driver_warning_type(parser, pp, input, types, spell, scratch, bare, 0) : (String8){ "", 0 };
    if (named.length != 0)
    {
        result = (PPOutputDiagnosticNote){
            .token = parse_source_index(&parser->tokens, declared),
            .message = check_warning_text(scratch, "forward declaration of %.*s", (int)named.length, named.pointer),
        };
    }
    return result;
}

// The display order of a call's candidates (#1447, clang's CompareOverloadCandidatesForDisplay): whether `left` comes
// before `right`. An ambiguous call's best and a call's non-viable candidates other than by their arguments' count by
// their declarations' order; those by count last, the ones whose parameter count is nearer the arguments' first, at the
// same distance the ones given too many first.
static bool check_semantic_overload_before(const Parser* parser, const CheckInput* input, const TypeTable* types, const CheckOverloadCandidate* candidates, const CheckOverloadExplained* left,
                                           const CheckOverloadExplained* right, uint32_t argument_count)
{
    const bool left_arity = left->failure == CHECK_OVERLOAD_FAILURE_FEW_ARGUMENTS || left->failure == CHECK_OVERLOAD_FAILURE_MANY_ARGUMENTS;
    const bool right_arity = right->failure == CHECK_OVERLOAD_FAILURE_FEW_ARGUMENTS || right->failure == CHECK_OVERLOAD_FAILURE_MANY_ARGUMENTS;
    uint32_t left_count = 0;
    uint32_t right_count = 0;
    type_parameters(types, type_strip_aligned(types, candidates[left->candidate].type), &left_count);
    type_parameters(types, type_strip_aligned(types, candidates[right->candidate].type), &right_count);
    const uint32_t left_distance = left_count > argument_count ? left_count - argument_count : argument_count - left_count;
    const uint32_t right_distance = right_count > argument_count ? right_count - argument_count : argument_count - right_count;
    const CheckOverloadCandidate* first = candidates + left->candidate;
    const CheckOverloadCandidate* second = candidates + right->candidate;
    const uint32_t first_pragma = check_input_symbol_overload_source(input, first);
    const uint32_t second_pragma = check_input_symbol_overload_source(input, second);
    const uint32_t first_source = first_pragma != BUSTER_CHECK_NONE ? first_pragma : parse_source_index(&parser->tokens, first->declaration);
    const uint32_t second_source = second_pragma != BUSTER_CHECK_NONE ? second_pragma : parse_source_index(&parser->tokens, second->declaration);
    const bool located = first_source < second_source;
    return left_arity != right_arity ? right_arity :
           !left_arity ? located :
           left_distance != right_distance ? left_distance < right_distance :
           left->failure != right->failure ? left->failure == CHECK_OVERLOAD_FAILURE_MANY_ARGUMENTS : located;
}

// clang's fix-it words of a candidate's argument `node` of type `source` that converts to no `target` (#2384,
// ConversionFixItGenerator in an overload's note): a pointer whose pointee is the target's type, "; dereference the
// argument with *" ("; remove &" of an `&x`); an lvalue of no pointer type where the target is a pointer to an object,
// "; take the address of the argument with &" ("; remove *" of a `*p`); else none.
static const char* check_semantic_overload_hint(const TypeTable* types, const CheckStream* stream, uint32_t node, TypeId source, TypeId target)
{
    const CheckGraph* graph = stream->graph;
    const TypeId from = type_strip_aligned(types, source);
    const TypeId to = type_strip_aligned(types, target);
    uint32_t written = node;
    for (uint32_t steps = 0; written < graph->count && steps < graph->count && graph->ops[written] == CHECK_OP_VALUE && graph->operand0[written] < graph->count;
         steps += 1)
    {
        written = graph->operand0[written];
    }
    const CheckOp op = written < graph->count ? graph->ops[written] : CHECK_OP_COUNT;
    const bool pointer = type_kind(types, from) == BUSTER_TYPE_KIND_POINTER;
    const char* result = "";
    if (pointer && type_unqualified(type_strip_aligned(types, type_element(types, from))) == type_unqualified(to))
    {
        result = op == CHECK_OP_ADDRESS ? "; remove &" : "; dereference the argument with *";
    }
    else if (!pointer && type_kind(types, to) == BUSTER_TYPE_KIND_POINTER && node < stream->count && check_stream_category(stream, node) == CHECK_LVALUE &&
             type_kind(types, type_strip_aligned(types, type_element(types, to))) != BUSTER_TYPE_KIND_FUNCTION)
    {
        result = op == CHECK_OP_DEREFERENCE ? "; remove *" : "; take the address of the argument with &";
    }
    return result;
}

// A call of an overload set that no candidate is viable for, or several are (#1447): clang's "no matching function for
// call to 'f'" with each candidate's note, or "call to 'f' is ambiguous" with its best's ("candidate function"), at the
// callee's name. The candidates' outcomes are the selection's again (check_overload_explain), a candidate that failed by
// an argument's conversion with clang's "no known conversion" note (#2384). NULL, and the checker's own error, where the
// notes are more than the report holds.
static uint32_t check_semantic_argument_begin(const Parser* parser, const CheckGraph* graph, uint32_t node, uint32_t argument);

static const char* check_semantic_overload_call(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                CheckWarningSpelling* spell, uint32_t node, uint32_t* location, PPOutputDiagnosticNote* note, uint32_t* note_count,
                                                Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    uint32_t low = 0;
    uint32_t high = graph->overload_count;
    uint32_t head = BUSTER_CHECK_NONE;
    while (head == BUSTER_CHECK_NONE && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const uint32_t at = graph->overload_calls[middle].node;
        head = at == node ? graph->overload_calls[middle].head : BUSTER_CHECK_NONE;
        high = node < at ? middle : high;
        low = node > at ? middle + 1 : low;
    }
    const uint32_t callee = graph->operand0[node];
    const uint32_t named = callee < graph->count && graph->ops[callee] == CHECK_OP_ADDRESS ? graph->operand0[callee] : callee;
    const uint32_t name = named < graph->count && graph->ops[named] == CHECK_OP_VALUE ? graph->tokens[named] : BUSTER_CHECK_NONE;
    const uint32_t argument_count = graph->argument_counts[node];
    const bool found = head < graph->overload_candidate_count && name < parser->tokens.count;
    CheckValue* arguments = found ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckValue, (uint64_t)argument_count + 1) : NULL;
    uint8_t* nonnull = arguments != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)argument_count + 1) : NULL;
    CheckOverloadExplained* explained = nonnull != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckOverloadExplained, (uint64_t)graph->overload_candidate_count + 1) : NULL;
    uint32_t explained_count = 0;
    if (explained != NULL)
    {
        for (uint32_t i = 0; i < argument_count; i += 1)
        {
            const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
            arguments[i] = check_stream_value(stream, argument);
            nonnull[i] = graph->ops[argument] == CHECK_OP_ADDRESS;
        }
        const CheckOverloadContext context = {
            .semantic = check_graph_context(graph, node, false),
            .conditions = &graph->conditions,
            .argument_nonnull = nonnull,
        };
        // The call was checked with these values, so the selection's conversions only find types the table has.
        explained_count = check_overload_explain((TypeTable*)types, graph->overload_candidates, graph->overload_candidate_count, head, arguments, argument_count,
                                                 &context, scratch, explained, true);
    }
    bool ambiguous = false;
    bool renderable = explained_count != 0;
    for (uint32_t i = 0; i < explained_count; i += 1)
    {
        ambiguous |= explained[i].best;
    }
    uint32_t shown = 0;
    for (uint32_t i = 0; renderable && i < explained_count; i += 1)
    {
        const uint8_t failure = explained[i].failure;
        renderable = ambiguous || failure == CHECK_OVERLOAD_FAILURE_FEW_ARGUMENTS || failure == CHECK_OVERLOAD_FAILURE_MANY_ARGUMENTS ||
                     failure == CHECK_OVERLOAD_FAILURE_DISABLED || (failure == CHECK_OVERLOAD_FAILURE_CONVERSION && explained[i].argument < argument_count);
        // (An ambiguous call's notes are its best's alone, which are listed first.)
        explained[shown] = explained[i];
        shown += !ambiguous || explained[i].best ? 1u : 0u;
    }
    renderable &= shown <= BUSTER_CHECK_SEMANTIC_NOTES;
    // In display order, by insertion: the lists are short.
    for (uint32_t i = 1; renderable && i < shown; i += 1)
    {
        const CheckOverloadExplained moved = explained[i];
        uint32_t j = i;
        while (j > 0 && check_semantic_overload_before(parser, input, types, graph->overload_candidates, &moved, explained + j - 1, argument_count))
        {
            explained[j] = explained[j - 1];
            j -= 1;
        }
        explained[j] = moved;
    }
    const char* result = NULL;
    for (uint32_t i = 0; renderable && i < shown; i += 1)
    {
        const CheckOverloadCandidate* candidate = graph->overload_candidates + explained[i].candidate;
        const uint32_t pragma_source = check_input_symbol_overload_source(input, candidate);
        const char* text = "candidate function";
        if (explained[i].failure == CHECK_OVERLOAD_FAILURE_DISABLED)
        {
            const String8 message = check_warning_strings_value(parser, pp, input->conditions[explained[i].condition].message, scratch);
            text = message.length != 0 ? check_warning_text(scratch, "candidate disabled: %.*s", (int)message.length, message.pointer) :
                                         "candidate disabled: <no message provided>";
        }
        else if (!ambiguous && explained[i].failure == CHECK_OVERLOAD_FAILURE_CONVERSION)
        {
            // clang's DiagnoseBadConversion (#2384): "no known conversion from 'A' to 'P' for Nth argument", the argument's type
            // after its lvalue conversion, the parameter's, the argument's ordinal, with clang's fix-it words where the
            // argument's address or its pointee would convert (check_semantic_overload_hint).
            const uint32_t index = explained[i].argument;
            const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + index];
            uint32_t parameter_count = 0;
            const TypeId* parameters = type_parameters(types, type_strip_aligned(types, candidate->type), &parameter_count);
            const TypeId parameter = parameters != NULL && index < parameter_count ? parameters[index] : 0;
            const TypeId actual = check_stream_type(stream, argument);
            // A rejected function designator keeps its function type in the candidate note (#2424).
            const TypeId value = type_kind(types, type_strip_aligned(types, actual)) == BUSTER_TYPE_KIND_FUNCTION ?
                                 actual : check_semantic_value_type(types, actual, false);
            const String8 from = value != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, value,
                                                                        check_semantic_operand_spelling(parser, input, stream, types, argument)) :
                                              (String8){ "", 0 };
            const String8 to = parameter != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, parameter, 0) : (String8){ "", 0 };
            const uint32_t ordinal = index + 1;
            const char* suffix = ordinal % 100 >= 11 && ordinal % 100 <= 13 ? "th" : ordinal % 10 == 1 ? "st" : ordinal % 10 == 2 ? "nd" : ordinal % 10 == 3 ? "rd" : "th";
            const bool pointer_lvalue = check_stream_category(stream, argument) == CHECK_LVALUE &&
                                        check_action_kind(types, value) == BUSTER_TYPE_KIND_POINTER && check_action_kind(types, parameter) == BUSTER_TYPE_KIND_POINTER &&
                                        check_action_kind(types, type_element(types, value)) == BUSTER_TYPE_KIND_FUNCTION &&
                                        check_action_kind(types, type_element(types, parameter)) == BUSTER_TYPE_KIND_FUNCTION;
            const uint32_t first = pointer_lvalue && graph->argument_tokens != NULL ? check_semantic_argument_begin(parser, graph, node, index) : BUSTER_CHECK_NONE;
            // clang's candidate fix-it names an address change for an incompatible function-pointer lvalue. Parentheses
            // distinguish its "take the address" wording from a bare dereference's "remove *" (#2424).
            const char* hint = pointer_lvalue ?
                               (first < parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_STAR && parser->tree.roles[first] == PARSE_ROLE_PREFIX ?
                                "; remove *" : "; take the address of the argument with &") :
                               parameter != 0 ? check_semantic_overload_hint(types, stream, argument, value, parameter) : "";
            const uint32_t referenced = argument < graph->count && graph->ops[argument] == CHECK_OP_ADDRESS ? graph->operand0[argument] : argument;
            const uint32_t reference = referenced < graph->count && graph->ops[referenced] == CHECK_OP_VALUE ? graph->tokens[referenced] : BUSTER_CHECK_NONE;
            const String8 overloaded = check_input_target_candidate(input, reference) != BUSTER_CHECK_NONE ?
                                       check_semantic_name(parser, pp, reference) : (String8){ 0 };
            text = overloaded.length != 0 && to.length != 0 ?
                   check_warning_text(scratch, "candidate function not viable: no overload of '%.*s' matching %.*s for %u%s argument", (int)overloaded.length,
                                      overloaded.pointer, (int)to.length, to.pointer, ordinal, suffix) :
                   from.length != 0 && to.length != 0 ?
                   check_warning_text(scratch, "candidate function not viable: no known conversion from %.*s to %.*s for %u%s argument%s", (int)from.length,
                                      from.pointer, (int)to.length, to.pointer, ordinal, suffix, hint) : NULL;
            renderable = text != NULL;
        }
        else if (!ambiguous)
        {
            // clang's DiagnoseArityMismatch: "requires single argument 'a'" for a named parameter, else the counts. Without
            // default arguments, fewer arguments than a variadic one's parameters need "at least" its count.
            uint32_t parameter_count = 0;
            const TypeId function = type_strip_aligned(types, candidate->type);
            type_parameters(types, function, &parameter_count);
            const bool variadic = (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
            const bool few = explained[i].failure == CHECK_OVERLOAD_FAILURE_FEW_ARGUMENTS;
            const uint32_t place = parameter_count == 1 ? check_driver_parameter_place(parser, input, candidate->declaration, 0) : BUSTER_CHECK_NONE;
            const bool named = pragma_source == BUSTER_CHECK_NONE && place < parser->tokens.count && parser->tokens.kinds[place] == PARSE_KIND_IDENTIFIER;
            const String8 parameter = named ? check_semantic_name(parser, pp, place) : (String8){ 0 };
            char provided[32];
            snprintf(provided, sizeof(provided), "%u", argument_count);
            text = named ? check_warning_text(scratch, "candidate function not viable: %s argument '%.*s', but %s arguments were provided",
                                              few && variadic ? "requires at least" : "requires single", (int)parameter.length, parameter.pointer,
                                              argument_count == 0 ? "no" : provided) :
                           check_warning_text(scratch, "candidate function not viable: requires%s %u argument%s, but %u %s provided", few && variadic ? " at least" : "",
                                              parameter_count, parameter_count == 1 ? "" : "s", argument_count, argument_count == 1 ? "was" : "were");
        }
        if (pragma_source != BUSTER_CHECK_NONE && *note_count < BUSTER_CHECK_SEMANTIC_NOTES)
        {
            note[(*note_count)++] = (PPOutputDiagnosticNote){ .token = pragma_source, .message = text };
        }
        else
        {
            check_semantic_add_note(parser, note, note_count, candidate->declaration, text);
        }
    }
    if (renderable)
    {
        const String8 called = check_semantic_name(parser, pp, name);
        uint32_t first = name;
        if ((graph->overload_candidates[head].flags & BUSTER_CHECK_OVERLOAD_TARGET) != 0)
        {
            check_target_versions_callee_end(parser, name, &first);
        }
        *location = first;
        result = ambiguous ? check_warning_text(scratch, "call to '%.*s' is ambiguous", (int)called.length, called.pointer) :
                             check_warning_text(scratch, "no matching function for call to '%.*s'", (int)called.length, called.pointer);
    }
    return result;
}

// Record `record`'s name as clang qualifies a member with it (#1842's 3d, 'B::p'): its tag's, or, for an anonymous record,
// the name of the typedef that names it (its name for linkage purposes); empty for none.
static String8 check_semantic_record_name(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types, TypeId record)
{
    const TypeId bare = record != 0 ? type_unqualified(type_strip_aligned(types, record)) : 0;
    const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_VOID;
    const uint32_t tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ? types->types.index[type_index(bare)] : types->tags.count;
    uint32_t name = tag < types->tags.count ? types->tags.name[tag] : 0;
    for (uint32_t t = 0; tag < types->tags.count && name == 0 && t < input->token_count; t += 1)
    {
        name = input->declarations[t] == t && input->declaration_kinds[t] == C_BINDING_TYPEDEF && type_unqualified(input->declared_types[t]) == bare ?
               parser->tokens.names[t] : 0;
    }
    return name != 0 ? check_driver_warning_name(pp, name) : (String8){ 0 };
}

// The notes of clang's errors for counted_by or counted_by_or_null (`kind`) pointer member `member` whose pointee
// `pointee` (spelled `incomplete`) is incomplete (#1842's 3d): "consider providing a complete definition for 'struct Inc'"
// at the pointee's forward declaration's keyword, and "consider using '__sized_by' instead of '__counted_by'" at the
// count's name in the member's attribute.
static void check_semantic_counted_notes(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types, uint32_t member, uint32_t kind,
                                         TypeId pointee, String8 incomplete, PPOutputDiagnosticNote* note, uint32_t* note_count, Arena* scratch)
{
    const char* name = kind == BUSTER_TYPE_COUNTED_BY ? "counted_by" : "counted_by_or_null";
    const TypeId bare = type_unqualified(type_strip_aligned(types, pointee));
    const uint8_t bare_kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_VOID;
    const uint32_t bare_tag = bare_kind == BUSTER_TYPE_KIND_STRUCT || bare_kind == BUSTER_TYPE_KIND_UNION ? types->types.index[type_index(bare)] : types->tags.count;
    const uint32_t bare_name = bare_tag < types->tags.count ? types->tags.name[bare_tag] : 0;
    uint32_t declared = BUSTER_CHECK_NONE;
    for (uint32_t t = 0; bare_name != 0 && declared == BUSTER_CHECK_NONE && t < input->token_count; t += 1)
    {
        declared = parser->tokens.names[t] == bare_name && input->declarations[t] == t && input->declaration_kinds[t] == C_BINDING_TAG_KIND &&
                   type_unqualified(input->declared_types[t]) == bare ? t : BUSTER_CHECK_NONE;
    }
    const uint32_t keyword = declared != BUSTER_CHECK_NONE && declared > 0 && parser->tokens.kinds[declared - 1] == PARSE_KIND_STRUCT ? declared - 1 : declared;
    if (keyword != BUSTER_CHECK_NONE && *note_count < BUSTER_CHECK_SEMANTIC_NOTES)
    {
        note[(*note_count)++] = (PPOutputDiagnosticNote){
            .token = parse_source_index(&parser->tokens, keyword),
            .message = check_warning_text(scratch, "consider providing a complete definition for %.*s", (int)incomplete.length, incomplete.pointer),
        };
    }
    const uint32_t declarator = check_semantic_member_token(parser, input, types, member);
    uint32_t begin = 0;
    uint32_t end = 0;
    if (declarator < parser->tokens.count)
    {
        check_driver_place_extent(parser, declarator, &begin, &end);
    }
    uint32_t count = BUSTER_CHECK_NONE;
    for (uint32_t t = begin; count == BUSTER_CHECK_NONE && t + 3 < end; t += 1)
    {
        String8 spelled = parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER ? check_driver_warning_name(pp, parser->tokens.names[t]) : (String8){ 0 };
        const bool underscored = spelled.length > 4 && memcmp(spelled.pointer, "__", 2) == 0 && memcmp(spelled.pointer + spelled.length - 2, "__", 2) == 0;
        spelled = underscored ? (String8){ .pointer = spelled.pointer + 2, .length = spelled.length - 4 } : spelled;
        const bool attribute = spelled.length == strlen(name) && memcmp(spelled.pointer, name, spelled.length) == 0 && parser->tokens.kinds[t + 1] == PARSE_KIND_L_PAREN;
        count = attribute ? t + 2 : BUSTER_CHECK_NONE;
    }
    if (count != BUSTER_CHECK_NONE && *note_count < BUSTER_CHECK_SEMANTIC_NOTES)
    {
        note[(*note_count)++] = (PPOutputDiagnosticNote){
            .token = parse_source_index(&parser->tokens, count),
            .message = check_warning_text(scratch, "consider using '__%s' instead of '__%s'", kind == BUSTER_TYPE_COUNTED_BY ? "sized_by" : "sized_by_or_null", name),
        };
    }
}

// clang's error for an initialization of counted_by or counted_by_or_null pointer member `member` of record `record`
// whose pointee is incomplete (#1842's 3d, CheckInitCountedError, BoundsSafetyCheckInitialization's): "cannot initialize
// 'B::p' with '__counted_by' attributed type 'struct Inc * __counted_by(n)' (aka 'struct Inc *') because the pointee type
// 'struct Inc' is incomplete" at a value it takes, "cannot implicitly initialize ..." where its list leaves it out, with
// check_semantic_counted_notes' notes. NULL where the member can't be spelled.
static const char* check_semantic_counted_initialization(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                                         CheckWarningSpelling* spell, uint32_t member, TypeId record, bool implicit, PPOutputDiagnosticNote* note,
                                                         uint32_t* note_count, Arena* scratch)
{
    const uint32_t kind = member != 0 && member < types->members.count ?
                          (types->members.flags[member] & BUSTER_TYPE_MEMBER_COUNT_MASK) >> BUSTER_TYPE_MEMBER_COUNT_SHIFT : BUSTER_TYPE_COUNT_NONE;
    const bool counted = kind == BUSTER_TYPE_COUNTED_BY || kind == BUSTER_TYPE_COUNTED_BY_OR_NULL;
    const TypeId pointer = counted ? type_strip_aligned(types, types->members.type[member]) : 0;
    const TypeId pointee = pointer != 0 && type_kind(types, pointer) == BUSTER_TYPE_KIND_POINTER ? type_element(types, pointer) : 0;
    const String8 written = pointee != 0 ? check_driver_counted_member_type(parser, pp, input, types, spell, scratch, member, 0, pointer) : (String8){ 0 };
    const String8 incomplete = written.length != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, pointee, 0) : (String8){ 0 };
    const String8 record_name = written.length != 0 ? check_semantic_record_name(parser, pp, input, types, record) : (String8){ 0 };
    const String8 member_name = counted ? check_driver_warning_name(pp, types->members.name[member]) : (String8){ 0 };
    const char* result = NULL;
    if (written.length != 0 && incomplete.length != 0 && record_name.length != 0 && member_name.length != 0)
    {
        result = check_warning_text(scratch, "cannot %sinitialize '%.*s::%.*s' with '__%s' attributed type %.*s because the pointee type %.*s is incomplete",
                                    implicit ? "implicitly " : "", (int)record_name.length, record_name.pointer, (int)member_name.length, member_name.pointer,
                                    kind == BUSTER_TYPE_COUNTED_BY ? "counted_by" : "counted_by_or_null", (int)written.length, written.pointer, (int)incomplete.length,
                                    incomplete.pointer);
        check_semantic_counted_notes(parser, pp, input, types, member, kind, pointee, incomplete, note, note_count, scratch);
    }
    return result;
}

// A use of a counted_by or counted_by_or_null pointer member whose pointee type is incomplete (#1842's 3d,
// CHECK_DIAG_COUNTED_INCOMPLETE), worded and placed as clang's: "cannot use 'b->p' with '__counted_by' attributed type
// 'struct Inc * __counted_by(n)' (aka 'struct Inc *') because the pointee type 'struct Inc' is incomplete" at the member's
// expression, or "cannot assign to 'B::p' ..." at an assignment's `=`; with "consider providing a complete definition for
// 'struct Inc'" at its forward declaration's keyword and "consider using '__sized_by' instead of '__counted_by'" at the
// count's name. NULL where the member isn't found, which keeps the generic message.
static const char* check_semantic_counted_incomplete(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                     const CheckReport* report, CheckWarningSpelling* spell, uint32_t* location, PPOutputDiagnosticNote* note,
                                                     uint32_t* note_count, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const bool assigned = graph->ops[report->node] == CHECK_OP_ASSIGN;
    const uint32_t node = assigned ? graph->operand0[report->node] : report->node;
    const uint32_t member = node < graph->count ? check_semantic_member_of(parser, input, stream, types, node) : 0;
    const uint32_t kind = member != 0 ? (types->members.flags[member] & BUSTER_TYPE_MEMBER_COUNT_MASK) >> BUSTER_TYPE_MEMBER_COUNT_SHIFT : BUSTER_TYPE_COUNT_NONE;
    const bool counted = kind == BUSTER_TYPE_COUNTED_BY || kind == BUSTER_TYPE_COUNTED_BY_OR_NULL;
    const TypeId pointer = counted ? type_strip_aligned(types, types->members.type[member]) : 0;
    const TypeId pointee = pointer != 0 && type_kind(types, pointer) == BUSTER_TYPE_KIND_POINTER ? type_element(types, pointer) : 0;
    const String8 written = pointee != 0 ? check_driver_counted_type(parser, pp, input, stream, types, spell, scratch, node, pointer) : (String8){ 0 };
    const String8 incomplete = written.length != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, pointee, 0) : (String8){ 0 };
    const char* name = kind == BUSTER_TYPE_COUNTED_BY ? "counted_by" : "counted_by_or_null";
    const char* result = NULL;
    // What it uses: the member's expression as written, or the record's member.
    const uint32_t first = written.length != 0 ? check_common_subtree_start(parser, graph->tokens[node]) : 0;
    const uint32_t last = graph->tokens[node] + 1;
    const char* used = written.length != 0 && !assigned ? check_semantic_requirement(parser, pp, first, last + 1, NULL, scratch) : NULL;
    const TypeId record = assigned && written.length != 0 ? type_unqualified(type_strip_aligned(types, graph->ops[node] == CHECK_OP_ARROW ?
                          type_element(types, type_strip_aligned(types, check_stream_type(stream, graph->operand0[node]))) : check_stream_type(stream, graph->operand0[node]))) : 0;
    const String8 record_name = record != 0 ? check_semantic_record_name(parser, pp, input, types, record) : (String8){ 0 };
    const String8 member_name = member != 0 ? check_driver_warning_name(pp, types->members.name[member]) : (String8){ 0 };
    if (used != NULL)
    {
        result = check_warning_text(scratch, "cannot use '%s' with '__%s' attributed type %.*s because the pointee type %.*s is incomplete", used, name, (int)written.length,
                                    written.pointer, (int)incomplete.length, incomplete.pointer);
        *location = first;
    }
    else if (assigned && record_name.length != 0 && incomplete.length != 0)
    {
        result = check_warning_text(scratch, "cannot assign to '%.*s::%.*s' with '__%s' attributed type %.*s because the pointee type %.*s is incomplete",
                                    (int)record_name.length, record_name.pointer, (int)member_name.length, member_name.pointer, name, (int)written.length, written.pointer,
                                    (int)incomplete.length, incomplete.pointer);
        *location = report->token;
    }
    if (result != NULL)
    {
        check_semantic_counted_notes(parser, pp, input, types, member, kind, pointee, incomplete, note, note_count, scratch);
    }
    return result;
}

// An alignment builtin's argument as clang's getExprLoc places its error (#2221): a binary operator's at the operator,
// any other's at its first token, a parenthesized one's `(` included.
static uint32_t check_semantic_alignment_place(const Parser* parser, const CheckGraph* graph, uint32_t node, uint32_t argument)
{
    uint32_t first = graph->argument_tokens[graph->argument_offsets[node] + argument];
    const uint32_t value = graph->argument_nodes[graph->argument_offsets[node] + argument];
    const uint32_t token = value < graph->count ? graph->tokens[value] : first;
    while (first > 0 && first < parser->tokens.count && parser->tokens.kinds[first - 1] == PARSE_KIND_L_PAREN &&
           parser->tree.roles[first - 1] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[first - 1] > token)
    {
        first -= 1;
    }
    const bool binary = token < parser->tokens.count && check_semantic_binary(parser->tree.roles[token]);
    const bool grouped = first < parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN && parser->tree.roles[first] == PARSE_ROLE_OPEN_GROUP &&
                         parser->tree.parents[first] > token;
    return binary && !grouped ? token : first;
}

// The decimal digits of 2^exponent, in `scratch` (or null): its limbs, divided by 10^19 until none is left, the
// remainders the digits' groups.
static const char* check_semantic_power_text(uint32_t exponent, Arena* scratch)
{
    const uint32_t limbs = exponent / 64 + 1;
    const uint64_t digits = (uint64_t)exponent * 30103u / 100000u + 2u;
    uint64_t* value = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, limbs);
    uint64_t* groups = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, digits / 19 + 2);
    char* text = BUSTER_ARENA_PUSH_ARRAY(scratch, char, digits + 20);
    if (value != NULL && groups != NULL && text != NULL)
    {
        for (uint32_t i = 0; i < limbs; i += 1)
        {
            value[i] = 0;
        }
        value[exponent / 64] = UINT64_C(1) << (exponent % 64);
        uint32_t top = limbs;
        uint32_t group_count = 0;
        while (top != 0)
        {
            unsigned __int128 remainder = 0;
            for (uint32_t i = top; i > 0; i -= 1)
            {
                const unsigned __int128 current = remainder << 64 | value[i - 1];
                value[i - 1] = (uint64_t)(current / UINT64_C(10000000000000000000));
                remainder = current % UINT64_C(10000000000000000000);
            }
            groups[group_count] = (uint64_t)remainder;
            group_count += 1;
            while (top != 0 && value[top - 1] == 0)
            {
                top -= 1;
            }
        }
        int at = snprintf(text, digits + 20, "%" PRIu64, groups[group_count - 1]);
        for (uint32_t g = group_count - 1; g > 0 && at > 0; g -= 1)
        {
            at += snprintf(text + at, digits + 20 - (uint64_t)at, "%019" PRIu64, groups[g - 1]);
        }
    }
    return value != NULL && groups != NULL ? text : NULL;
}

// An alignment builtin's errors (#2221) as clang's SemaBuiltinAlignment words them: a source of no integer or pointer
// type, "operand of type 'T' where arithmetic or pointer type is required" (a floating one's note "floating point types are
// not allowed here", a function pointer's "function pointers are not allowed here"); an alignment of no integer type,
// "used type 'T' where integer is required"; and its constant checks (check_builtin_misc_align's constraint): "requested
// alignment must be 1 or greater", "requested alignment must be N or smaller" (N 2 to the source's width less 1, a
// pointer's 64) and "requested alignment is not a power of 2", each at its argument. NULL for another report.
static const char* check_semantic_alignment(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                            const CheckReport* report, CheckWarningSpelling* spell, uint32_t* location, PPOutputDiagnosticNote* note,
                                            uint32_t* note_count, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t node = report->node;
    const char* result = NULL;
    CheckBuiltinMiscConstraint constraint = CHECK_BUILTIN_MISC_CONSTRAINT_NONE;
    for (uint32_t i = 0; i < stream->misc_count; i += 1)
    {
        constraint = stream->misc[i].node == node ? stream->misc[i].constraint : constraint;
    }
    const uint32_t source = graph->argument_nodes[graph->argument_offsets[node]];
    const uint32_t alignment = graph->argument_nodes[graph->argument_offsets[node] + 1];
    const TypeId source_type = source < stream->count ? check_stream_type(stream, source) : 0;
    const TypeId bare = type_unqualified(type_strip_aligned(types, source_type));
    const uint8_t kind = source_type != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_NONE;
    const uint32_t argument = report->diagnostic == CHECK_DIAG_SCALAR_REQUIRED ? 0 : 1;
    if (report->diagnostic == CHECK_DIAG_SCALAR_REQUIRED && source_type != 0)
    {
        const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, source_type,
                                                       check_semantic_operand_spelling(parser, input, stream, types, source));
        result = type.length != 0 ? check_warning_text(scratch, "operand of type %.*s where arithmetic or pointer type is required", (int)type.length, type.pointer) : NULL;
        const bool floating = kind == BUSTER_TYPE_KIND_FLOATING;
        const bool function = kind == BUSTER_TYPE_KIND_POINTER && type_kind(types, type_strip_aligned(types, type_element(types, bare))) == BUSTER_TYPE_KIND_FUNCTION;
        if (result != NULL && (floating || function))
        {
            check_semantic_add_note(parser, note, note_count, check_semantic_alignment_place(parser, graph, node, 0),
                                    floating ? "floating point types are not allowed here" : "function pointers are not allowed here");
        }
    }
    else if (report->diagnostic == CHECK_DIAG_INTEGER_REQUIRED && alignment < stream->count)
    {
        const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, check_stream_type(stream, alignment),
                                                       check_semantic_operand_spelling(parser, input, stream, types, alignment));
        result = type.length != 0 ? check_warning_text(scratch, "used type %.*s where integer is required", (int)type.length, type.pointer) : NULL;
    }
    else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && constraint == CHECK_BUILTIN_MISC_CONSTRAINT_MINIMUM)
    {
        result = "requested alignment must be 1 or greater";
    }
    else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && constraint == CHECK_BUILTIN_MISC_CONSTRAINT_ALIGNMENT)
    {
        result = "requested alignment is not a power of 2";
    }
    else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && constraint == CHECK_BUILTIN_MISC_CONSTRAINT_RANGE)
    {
        // The source's width: a pointer's 64, an integer's (a _BitInt's past 128 bits too).
        uint32_t width = 64;
        uint64_t bits = 64;
        bool is_signed = false;
        if (kind != BUSTER_TYPE_KIND_POINTER && !type_kind_is_array(kind) && check_action_integer_type(types, bare, &width, &is_signed))
        {
            bits = width;
        }
        else if (kind == BUSTER_TYPE_KIND_BIT_INT)
        {
            uint64_t rank = 0;
            type_integer_properties(types, bare, &rank, &bits, &is_signed);
        }
        const char* maximum = check_semantic_power_text(bits != 0 ? (uint32_t)bits - 1 : 0, scratch);
        result = maximum != NULL ? check_warning_text(scratch, "requested alignment must be %s or smaller", maximum) : NULL;
    }
    *location = result != NULL ? check_semantic_alignment_place(parser, graph, node, argument) : report->token;
    return result;
}

// The first token of call `node`'s argument `argument`, its parentheses included: clang's getBeginLoc of it.
static uint32_t check_semantic_argument_begin(const Parser* parser, const CheckGraph* graph, uint32_t node, uint32_t argument)
{
    const uint32_t value = graph->argument_nodes[graph->argument_offsets[node] + argument];
    uint32_t first = graph->argument_tokens[graph->argument_offsets[node] + argument];
    const uint32_t token = value < graph->count ? graph->tokens[value] : first;
    while (first > 0 && first < parser->tokens.count && parser->tokens.kinds[first - 1] == PARSE_KIND_L_PAREN &&
           parser->tree.roles[first - 1] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[first - 1] > token)
    {
        first -= 1;
    }
    return first;
}

// __builtin_clear_padding's argument refused (#2414, clang's BuiltinClearPadding), at its first token, its parentheses
// included: no pointer, "passing 'T' to parameter of incompatible type pointer: type mismatch at 1st parameter ('T' vs
// pointer)", the argument's type as written (an array isn't decayed); a function pointer, "argument to
// __builtin_clear_padding must be a pointer to a trivially-copyable type ('T' invalid)"; an incomplete pointee, "variable
// has incomplete type 'T'" with a tag's "forward declaration" note; a const one, "read-only variable is not assignable".
// NULL for another report.
static const char* check_semantic_clear_padding(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                const CheckReport* report, CheckWarningSpelling* spell, uint32_t* location, PPOutputDiagnosticNote* note,
                                                Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t node = report->node;
    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node]];
    const uint32_t first = check_semantic_argument_begin(parser, graph, node, 0);
    const TypeId written = argument < stream->count ? check_stream_type(stream, argument) : 0;
    const TypeId bare = written != 0 ? type_unqualified(type_strip_aligned(types, written)) : 0;
    const bool pointer = bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_POINTER;
    const TypeId pointee = pointer ? type_unqualified(type_strip_aligned(types, type_element(types, bare))) : 0;
    const char* result = NULL;
    if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && written != 0)
    {
        const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, written, check_semantic_operand_spelling(parser, input, stream, types, argument));
        result = type.length == 0 ? NULL :
                 pointer ? check_warning_text(scratch, "argument to __builtin_clear_padding must be a pointer to a trivially-copyable type (%.*s invalid)",
                                              (int)type.length, type.pointer) :
                           check_warning_text(scratch, "passing %.*s to parameter of incompatible type pointer: type mismatch at 1st parameter (%.*s vs pointer)",
                                              (int)type.length, type.pointer, (int)type.length, type.pointer);
    }
    else if (report->diagnostic == CHECK_DIAG_INCOMPLETE && pointee != 0)
    {
        const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, pointee, 0);
        result = type.length != 0 ? check_warning_text(scratch, "variable has incomplete type %.*s", (int)type.length, type.pointer) : NULL;
        *note = result != NULL ? check_semantic_forward_note(parser, pp, input, types, spell, pointee, scratch) : *note;
    }
    else if (report->diagnostic == CHECK_DIAG_INVALID_TYPE && pointer)
    {
        result = "read-only variable is not assignable";
    }
    *location = result != NULL ? first : report->token;
    return result;
}

// The beginning and retained root of a case-value operand whose ICE report or propagated constant fault is at `node`,
// including its written parentheses (#2438). The retained root distinguishes it from an unrelated constant context in
// the labeled statement or an inner declaration.
static uint32_t check_semantic_case_begin(const Parser* parser, const CheckInput* input, const CheckStream* stream, uint32_t node, uint32_t* root)
{
    const uint32_t count = parser->tokens.count;
    uint32_t result = BUSTER_CHECK_NONE;
    *root = BUSTER_CHECK_NONE;
    uint32_t ancestor = node < input->graph.count ? input->graph.tokens[node] : count;
    for (uint32_t steps = 0; ancestor < count && result == BUSTER_CHECK_NONE && steps < count; steps += 1)
    {
        if (parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_CASE)
        {
            const uint32_t lower = input->token_nodes[ancestor];
            const uint32_t upper = check_sparse_get(input->case_ranges, ancestor);
            const CheckValue low = lower < stream->count ? check_stream_value(stream, lower) : (CheckValue){ 0 };
            const CheckValue high = upper < stream->count ? check_stream_value(stream, upper) : (CheckValue){ 0 };
            const bool matches_lower = lower == node || (lower < stream->count && low.fault != CHECK_FAULT_NONE && low.fault_node == node);
            const bool matches_upper = upper == node || (upper < stream->count && high.fault != CHECK_FAULT_NONE && high.fault_node == node);
            if (matches_lower || matches_upper)
            {
                *root = matches_lower ? lower : upper;
                uint32_t keyword = ancestor;
                while (keyword != 0 && !(parser->tokens.kinds[keyword] == PARSE_KIND_CASE && parser->tree.parents[keyword] == ancestor))
                {
                    keyword -= 1;
                }
                result = parser->tokens.kinds[keyword] == PARSE_KIND_CASE && parser->tree.parents[keyword] == ancestor ? keyword + 1 : BUSTER_CHECK_NONE;
                if (!matches_lower)
                {
                    while (result < ancestor && !(parser->tree.roles[result] == PARSE_ROLE_RANGE && parser->tree.parents[result] == ancestor))
                    {
                        result += 1;
                    }
                    result = result < ancestor ? result + 1 : BUSTER_CHECK_NONE;
                }
            }
        }
        ancestor = parser->tree.parents[ancestor];
    }
    return result;
}

// Which of clang's CheckCStyleCast pointer errors a cast from `source` (before its lvalue conversion) to `target` is
// (#2914): 1 from a pointer (an array's or a function's decay too) to a floating or complex type, an _Atomic one's value
// type; 2 to a pointer from a floating or complex operand; 0 for any other.
static uint32_t check_semantic_cast_pointer_arithmetic(const TypeTable* types, TypeId target, TypeId source)
{
    TypeId value = type_strip_aligned(types, target);
    value = type_kind(types, value) == BUSTER_TYPE_KIND_ATOMIC ? types->types.base[type_index(value)] : value;
    const uint8_t target_kind = check_action_kind(types, value);
    const uint8_t source_kind = check_action_kind(types, source);
    const bool source_pointer = source_kind == BUSTER_TYPE_KIND_POINTER || source_kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(source_kind);
    const bool target_fractional = target_kind == BUSTER_TYPE_KIND_FLOATING || target_kind == BUSTER_TYPE_KIND_COMPLEX;
    const bool source_fractional = source_kind == BUSTER_TYPE_KIND_FLOATING || source_kind == BUSTER_TYPE_KIND_COMPLEX;
    return source_pointer && target_fractional ? 1u : target_kind == BUSTER_TYPE_KIND_POINTER && source_fractional ? 2u : 0u;
}

// Whether a cast from `source` (before its lvalue conversion) to `target` is one clang's CheckCStyleCast refuses for
// nullptr_t (#2914, err_bad_cstyle_cast_generic): to nullptr_t from anything but a nullptr_t or a null pointer constant
// (check_action_cast refused those alone), or from a nullptr_t to a type other than a pointer, bool, void or nullptr_t;
// an _Atomic target's value type decides.
static bool check_semantic_cast_nullptr(const TypeTable* types, TypeId target, TypeId source)
{
    TypeId value = type_strip_aligned(types, target);
    value = type_kind(types, value) == BUSTER_TYPE_KIND_ATOMIC ? types->types.base[type_index(value)] : value;
    const uint8_t target_kind = check_action_kind(types, value);
    const bool from_nullptr = check_action_kind(types, source) == BUSTER_TYPE_KIND_NULLPTR;
    const bool kept = target_kind == BUSTER_TYPE_KIND_POINTER || target_kind == BUSTER_TYPE_KIND_VOID || target_kind == BUSTER_TYPE_KIND_NULLPTR ||
                      type_unqualified(value) == BUSTER_TYPE_BOOL;
    return (target_kind == BUSTER_TYPE_KIND_NULLPTR && !from_nullptr) || (from_nullptr && !kept);
}

// A semantic report's clang text and location, with its notes in `note`, an array of BUSTER_CHECK_SEMANTIC_NOTES
// (check_semantic_note_count counts them): const assignment's are several (#1447).

static const char* check_semantic_expression(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            const TypeTable* types, const CheckReport* report, CheckWarningSpelling* spell, uint32_t* location, uint32_t* character_offset,
                                            PPOutputDiagnosticNote* note, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t node = report->node;
    const uint32_t count = graph->count;
    const uint32_t token = report->token;
    const CheckOp op = node < count ? graph->ops[node] : CHECK_OP_COUNT;
    const uint32_t operand = node < count ? graph->operand0[node] : BUSTER_CHECK_NONE;
    const TypeId operand_type = operand < stream->count ? check_stream_type(stream, operand) : 0;
    const TypeId operand_value_type = check_semantic_operand_value_type(types, stream, node, 0, operand_type);
    const char* result = check_driver_report_message(report);
    *location = token;
    *character_offset = 0;
    memset(note, 0, sizeof(*note) * BUSTER_CHECK_SEMANTIC_NOTES);
    uint32_t note_count = 0;
    // A call of an overload set none or several of whose candidates are viable (#1447), whose text is clang's where its
    // notes can be.
    const char* overloaded = report->message == CHECK_MESSAGE_ACTION && report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_CALL && graph->overload_count != 0 ?
                             check_semantic_overload_call(parser, pp, input, stream, types, spell, node, location, note, &note_count, scratch) : NULL;
    if (overloaded != NULL)
    {
        result = overloaded;
    }
    else if (report->message == CHECK_MESSAGE_ACTION && op == CHECK_OP_BUILTIN_MISC && graph->members[node] == CBUILTIN_CFSTRING_MAKE_CONSTANT_STRING &&
             graph->argument_counts[node] == 1 && report->diagnostic == CHECK_DIAG_INCOMPATIBLE)
    {
        // CFString's argument that converts but is no ordinary literal (#2189, clang's CheckObjCString), at its first token.
        CheckBuiltinMiscConstraint constraint = CHECK_BUILTIN_MISC_CONSTRAINT_NONE;
        for (uint32_t i = 0; i < stream->misc_count; i += 1)
        {
            constraint = stream->misc[i].node == node ? stream->misc[i].constraint : constraint;
        }
        if (constraint == CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT)
        {
            result = "CFString literal is not a string constant";
            *location = check_semantic_argument_begin(parser, graph, node, 0);
        }
    }
    else if (report->message == CHECK_MESSAGE_ACTION && op == CHECK_OP_BUILTIN_MISC && graph->members[node] == CBUILTIN_CLEAR_PADDING &&
             graph->argument_counts[node] == 1 &&
             (report->diagnostic == CHECK_DIAG_INCOMPATIBLE || report->diagnostic == CHECK_DIAG_INCOMPLETE || report->diagnostic == CHECK_DIAG_INVALID_TYPE))
    {
        const char* refused = check_semantic_clear_padding(parser, pp, input, stream, types, report, spell, location, note, scratch);
        result = refused != NULL ? refused : result;
    }
    else if (report->message == CHECK_MESSAGE_ACTION && op == CHECK_OP_BUILTIN_MISC && graph->argument_counts[node] == 2 &&
             (graph->members[node] == CBUILTIN_ALIGN_UP || graph->members[node] == CBUILTIN_ALIGN_DOWN || graph->members[node] == CBUILTIN_IS_ALIGNED) &&
             (report->diagnostic == CHECK_DIAG_SCALAR_REQUIRED || report->diagnostic == CHECK_DIAG_INTEGER_REQUIRED || report->diagnostic == CHECK_DIAG_INCOMPATIBLE))
    {
        const char* aligned = check_semantic_alignment(parser, pp, input, stream, types, report, spell, location, note, &note_count, scratch);
        result = aligned != NULL ? aligned : result;
    }
    else if (report->message == CHECK_MESSAGE_STATIC_ASSERT)
    {
        result = check_semantic_assert(parser, pp, input, stream, types, report, location, note, &note_count, scratch);
    }
    else if ((report->message == CHECK_MESSAGE_ICE || (report->message == CHECK_MESSAGE_CONSTANT && report->fault == CHECK_FAULT_DIVIDE_BY_ZERO)) && node < stream->count)
    {
        uint32_t root = BUSTER_CHECK_NONE;
        const uint32_t first = check_semantic_case_begin(parser, input, stream, node, &root);
        const CheckValue value = root < stream->count ? check_graph_context_value(types, graph, stream, root) : (CheckValue){ 0 };
        const CheckValue asserted = graph->contexts != NULL && (graph->contexts[node] & BUSTER_CHECK_CONTEXT_ASSERT) != 0 ?
                                    check_graph_context_value(types, graph, stream, node) : (CheckValue){ 0 };
        if (asserted.type != 0 && !check_graph_constant_type(types, BUSTER_CHECK_CONTEXT_ASSERT, asserted.type))
        {
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, asserted.type, node,
                                                           check_semantic_operator_spelling(parser, input, stream, types, node));
            result = type.length != 0 ? check_warning_text(scratch, "value of type %.*s is not contextually convertible to 'bool'", (int)type.length, type.pointer) : NULL;
            const uint32_t keyword = check_semantic_assertion_keyword(parser, graph->tokens[node]);
            *location = keyword < parser->tokens.count ? keyword + 2 : check_semantic_first_token(parser, graph->tokens[node]);
            const uint32_t close = keyword < parser->tokens.count && keyword + 1 < parser->tokens.count ? parser->tree.parents[keyword + 1] : parser->tokens.count;
            const uint32_t end = close < parser->tokens.count ? check_constant_punctuation(parser, keyword + 2, close, PARSE_KIND_COMMA) : 0;
            if (end > keyword + 2)
            {
                pp_diagnostic_range_tokens(pp, parse_source_index(&parser->tokens, keyword + 2), parse_source_index(&parser->tokens, end - 1));
            }
        }
        else if (first < parser->tokens.count)
        {
            if (!check_program_switch_type(types, value.type))
            {
                // Clang rejects the operand's type before attempting constant evaluation. A written cast retains its
                // typedef spelling, irrespective of the width of the switch's controlling integer type.
                const uint32_t spelling = graph->ops[root] == CHECK_OP_CAST ? check_input_operand_type_spelling(input, parser, root) :
                                          check_semantic_operand_spelling(parser, input, stream, types, root);
                const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, value.type, spelling);
                result = type.length != 0 ? check_warning_text(scratch, "integer constant expression must have integer type, not %.*s", (int)type.length, type.pointer) : NULL;
                *location = first;
                check_semantic_range(parser, pp, graph->tokens[root], true);
            }
            else if (report->message == CHECK_MESSAGE_CONSTANT)
            {
                result = "expression is not an integer constant expression";
                *location = first;
                check_semantic_add_note(parser, note, &note_count, token, "division by zero");
            }
        }
    }
    else if (report->message == CHECK_MESSAGE_STATIC_INITIALIZER && node < count && graph->tokens[node] == token)
    {
        // At clang's culprit (#1447): an initialization's value's, not the initializer's first token. (A report's token is
        // its node's; the reports' walk's model pairs others, which keep their token.)
        const bool initialize = op == CHECK_OP_INITIALIZE && operand < count;
        const uint32_t value = initialize ? operand : node;
        const TypeId target = initialize && node < stream->count ? check_stream_type(stream, node) : 0;
        *location = check_init_culprit_at(graph, stream, types, parser, value, token, target);
        check_semantic_range(parser, pp, graph->tokens[value], false);
    }
    else if (report->message == CHECK_MESSAGE_ACTION && node < count)
    {
        if (report->diagnostic == CHECK_DIAG_MEMBER && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && token + 1 < parser->tokens.count)
        {
            TypeId record = op == CHECK_OP_ARROW ? type_element(types, type_conversion_operand(types, operand_type)) : operand_type;
            record = type_strip_aligned(types, record);
            const TypeId atomic = type_kind(types, record) == BUSTER_TYPE_KIND_ATOMIC ? type_strip_aligned(types, type_element(types, record)) : 0;
            const uint8_t atomic_kind = atomic != 0 ? type_kind(types, atomic) : BUSTER_TYPE_KIND_NONE;
            record = atomic_kind == BUSTER_TYPE_KIND_STRUCT || atomic_kind == BUSTER_TYPE_KIND_UNION ? atomic : record;
            const String8 name = check_semantic_name(parser, pp, token + 1);
            // A base of no structure or union is clang's err_typecheck_member_reference_struct_union at the operator
            // (LookupMemberExpr, #2288), the operand's type as written for `.`; `.` of a pointer to one is its
            // err_typecheck_member_reference_suggestion there, naming the pointer. A record's missing member is at its name.
            const uint8_t kind = record != 0 ? type_kind(types, type_strip_aligned(types, record)) : 0;
            const bool aggregate = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
            const uint8_t pointee = op == CHECK_OP_MEMBER && kind == BUSTER_TYPE_KIND_POINTER ? type_kind(types, type_strip_aligned(types, type_element(types, record))) : 0;
            const bool suggestion = pointee == BUSTER_TYPE_KIND_STRUCT || pointee == BUSTER_TYPE_KIND_UNION;
            const uint32_t direct = op == CHECK_OP_MEMBER && !aggregate ? check_input_operand_type_spelling(input, parser, operand) : 0;
            const uint32_t spelling = direct != 0 ? direct : !aggregate ? check_semantic_operand_spelling(parser, input, stream, types, operand) : 0;
            // A `.` of a counted_by family member keeps its sugar (#1842's 3c).
            const String8 counted = op == CHECK_OP_MEMBER && !aggregate ? check_driver_counted_operand_type(parser, pp, input, stream, types, spell, scratch, record, operand) :
                                    (String8){ 0 };
            const String8 type = counted.length != 0 ? counted :
                                 check_semantic_vector_type(parser, pp, input, stream, types, spell, scratch, aggregate ? type_unqualified(record) : record, spelling, operand);
            *location = aggregate ? token + 1 : token;
            result = type.length == 0 ? NULL :
                     aggregate ? check_warning_text(scratch, "no member named '%.*s' in %.*s", (int)name.length, name.pointer, (int)type.length, type.pointer) :
                     suggestion ? check_warning_text(scratch, "member reference type %.*s is a pointer; did you mean to use '->'?", (int)type.length, type.pointer) :
                     check_warning_text(scratch, "member reference base type %.*s is not a structure or union", (int)type.length, type.pointer);
            // The base highlighted (#2376), clang's BaseExpr->getSourceRange(), and for a base of no record the member's
            // name too (its MemberLoc).
            if (result != NULL && kind != BUSTER_TYPE_KIND_EXT_VECTOR && operand < count)
            {
                check_semantic_range(parser, pp, graph->tokens[operand], true);
            }
            if (result != NULL && !aggregate && !suggestion && kind != BUSTER_TYPE_KIND_EXT_VECTOR && token + 1 < parser->tokens.count)
            {
                pp_diagnostic_range_tokens(pp, parse_source_index(&parser->tokens, token + 1), parse_source_index(&parser->tokens, token + 1));
            }
            if (result != NULL && suggestion)
            {
                const uint32_t source = parse_source_index(&parser->tokens, token);
                pp_diagnostic_fixit_tokens(pp, source, source, "->");
            }
            if (kind == BUSTER_TYPE_KIND_EXT_VECTOR)
            {
                // The typed action already rejected this member. Spell its first illegal component, or the valid
                // spelling's out-of-range access, only on this cold path. Clang locates a bad character relative to
                // the operator (even for ->); boolean vectors reject the whole member at its own token.
                const bool boolean = check_vector_element(types, record) == BUSTER_TYPE_BOOL;
                const bool numeric = name.length > 1 && (name.pointer[0] == 's' || name.pointer[0] == 'S');
                const bool color = name.length != 0 && (name.pointer[0] == 'r' || name.pointer[0] == 'g' || name.pointer[0] == 'b' || name.pointer[0] == 'a');
                const bool point = name.length != 0 && strchr("xyzwrgba", (unsigned char)name.pointer[0]) != NULL;
                const char* alphabet = numeric || !point ? "0123456789abcdefABCDEF" : color ? "rgba" : "xyzw";
                uint32_t bad = BUSTER_CHECK_NONE;
                for (uint32_t i = numeric ? 1u : 0u; !boolean && bad == BUSTER_CHECK_NONE && i < name.length; i += 1)
                {
                    bad = strchr(alphabet, (unsigned char)name.pointer[i]) == NULL ? i : BUSTER_CHECK_NONE;
                }
                if (boolean || bad != BUSTER_CHECK_NONE)
                {
                    result = check_warning_text(scratch, "illegal vector component name '%.*s'", boolean ? (int)name.length : 1,
                                                name.pointer + (boolean ? 0 : bad));
                    *location = boolean ? token + 1 : token;
                    *character_offset = boolean ? 0 : bad + 1;
                }
                else
                {
                    result = type.length != 0 ? check_warning_text(scratch, "vector component access exceeds type %.*s", (int)type.length, type.pointer) : NULL;
                }
            }
        }
        else if (report->diagnostic == CHECK_DIAG_POINTER_REQUIRED && op == CHECK_OP_ARROW && operand_type != 0 && token + 1 < parser->tokens.count)
        {
            // `->` of no pointer: clang's err_typecheck_member_reference_arrow, at the member's name (#2288).
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, operand_type, check_input_operand_type_spelling(input, parser, operand));
            *location = token + 1;
            result = type.length != 0 ? check_warning_text(scratch, "member reference type %.*s is not a pointer", (int)type.length, type.pointer) : NULL;
        }
        else if (report->diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED && op == CHECK_OP_POSITIVE && operand_type != 0 && graph->tokens[node] < parser->tokens.count &&
                 parser->tree.roles[graph->tokens[node]] == PARSE_ROLE_CLOSE_CONDITION && graph->tokens[node] != 0 &&
                 check_semantic_switch(parser, graph->tokens[node]) != BUSTER_CHECK_NONE)
        {
            // A switch's condition of no arithmetic type, which its promotion (a unary plus) refuses: clang's
            // err_typecheck_statement_requires_integer at the switch (#1447), as check_semantic_constraint's.
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, operand_value_type, operand,
                                                           check_semantic_operator_spelling(parser, input, stream, types, operand));
            *location = check_semantic_switch(parser, graph->tokens[node]);
            result = type.length != 0 ? check_warning_text(scratch, "statement requires expression of integer type (%.*s invalid)", (int)type.length, type.pointer) :
                                        NULL;
            check_semantic_range(parser, pp, graph->tokens[operand], true);
        }
        else if ((report->diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED || report->diagnostic == CHECK_DIAG_INTEGER_REQUIRED ||
                  report->diagnostic == CHECK_DIAG_SCALAR_REQUIRED) && op >= CHECK_OP_POSITIVE && op <= CHECK_OP_BIT_NOT && operand_type != 0)
        {
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, operand_value_type, operand,
                                                           check_semantic_operator_spelling(parser, input, stream, types, operand));
            result = type.length != 0 ? check_warning_text(scratch, "invalid argument type %.*s to unary expression", (int)type.length, type.pointer) : NULL;
            check_semantic_range(parser, pp, graph->tokens[operand], true);
        }
        else if (report->diagnostic == CHECK_DIAG_SCALAR_REQUIRED && op == CHECK_OP_CONDITION && operand_type != 0)
        {
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, operand_value_type, operand,
                                                           check_semantic_operator_spelling(parser, input, stream, types, operand));
            result = type.length != 0 ? check_warning_text(scratch, "statement requires expression of scalar type (%.*s invalid)", (int)type.length, type.pointer) : NULL;
            *location = check_semantic_condition_keyword(parser, graph->tokens[node]);
            check_semantic_range(parser, pp, graph->tokens[operand], true);
        }
        else if (report->diagnostic == CHECK_DIAG_SCALAR_REQUIRED && op == CHECK_OP_CONDITIONAL && operand_type != 0)
        {
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, operand_value_type, operand,
                                                           check_semantic_operator_spelling(parser, input, stream, types, operand));
            result = type.length != 0 ? check_warning_text(scratch, "used type %.*s where arithmetic or pointer type is required", (int)type.length, type.pointer) : NULL;
            *location = check_semantic_conditional_question(parser, graph->tokens[node]);
            check_semantic_range(parser, pp, graph->tokens[operand], true);
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_CONDITIONAL &&
                 graph->operand1[node] < stream->count && graph->operand2[node] < stream->count)
        {
            const uint32_t first = graph->operand1[node];
            const uint32_t second = graph->operand2[node];
            const String8 left = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch,
                                                           check_semantic_operand_value_type(types, stream, node, 1, check_stream_type(stream, first)), first,
                                                           check_semantic_operator_spelling(parser, input, stream, types, first));
            const String8 right = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch,
                                                            check_semantic_operand_value_type(types, stream, node, 2, check_stream_type(stream, second)), second,
                                                            check_semantic_operator_spelling(parser, input, stream, types, second));
            result = left.length != 0 && right.length != 0 ? check_warning_text(scratch, "incompatible operand types (%.*s and %.*s)",
                                                                               (int)left.length, left.pointer, (int)right.length, right.pointer) : NULL;
            *location = check_semantic_conditional_question(parser, graph->tokens[node]);
            check_semantic_range(parser, pp, graph->tokens[first], true);
            check_semantic_range(parser, pp, graph->tokens[second], true);
        }
        else if ((report->diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED || report->diagnostic == CHECK_DIAG_INTEGER_REQUIRED ||
                  report->diagnostic == CHECK_DIAG_SCALAR_REQUIRED ||
                  (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL)) &&
                 ((op >= CHECK_OP_ADD && op <= CHECK_OP_LOGICAL_OR) ||
                  (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN)) && operand_type != 0 && graph->operand1[node] < stream->count &&
                 !check_vector_type(types, type_strip_aligned(types, operand_type)) &&
                 !check_vector_type(types, type_strip_aligned(types, check_stream_type(stream, graph->operand1[node]))))
        {
            // clang's err_typecheck_invalid_operands (#1872, Sema::InvalidOperands): both operands' types as written, before
            // their conversions (an array's, a qualified lvalue's, a typedef's), at the operator, the report's token. A
            // vector's operands have clang's vector errors.
            const uint32_t second = graph->operand1[node];
            const String8 left = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, operand_type, operand,
                                                           check_semantic_operator_spelling(parser, input, stream, types, operand));
            const String8 right = check_semantic_repeated_type(operand_type, left, check_stream_type(stream, second),
                                                              check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, check_stream_type(stream, second),
                                                                                        second, check_semantic_operator_spelling(parser, input, stream, types, second)));
            result = left.length != 0 && right.length != 0 ? check_warning_text(scratch, "invalid operands to binary expression (%.*s and %.*s)", (int)left.length,
                                                                                left.pointer, (int)right.length, right.pointer) : NULL;
            check_semantic_range(parser, pp, graph->tokens[operand], true);
            check_semantic_range(parser, pp, graph->tokens[second], true);
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_CAST && operand_type != 0 && token + 1 < parser->tokens.count &&
                 parser->tree.roles[token] == PARSE_ROLE_CLOSE_CAST && node < stream->count && graph->types != NULL &&
                 check_action_is_scalar(types, type_conversion_operand(types, graph->types[node])) &&
                 (check_action_kind(types, operand_type) == BUSTER_TYPE_KIND_STRUCT || check_action_kind(types, operand_type) == BUSTER_TYPE_KIND_UNION ||
                  check_action_kind(types, operand_type) == BUSTER_TYPE_KIND_ATOMIC))
        {
            // A cast to an arithmetic or pointer type (an atomic one's value type too) from a record (#2375, clang's CheckCStyleCast,
            // err_typecheck_expect_scalar_operand), or from an atomic rvalue (#2230): the operand's type after its lvalue
            // conversion, its qualifiers dropped and its sugar kept where they are none (a cast's as written), at the
            // operand's first token, the getExprLoc of the conversion clang wraps it in (a member access's base too).
            const TypeId converted = type_unqualified(type_strip_aligned(types, operand_type));
            const bool cast = operand < count && graph->ops[operand] == CHECK_OP_CAST;
            const uint32_t spelling = converted != operand_type ? 0 :
                                      cast ? check_input_operand_type_spelling(input, parser, operand) : check_semantic_operand_spelling(parser, input, stream, types, operand);
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, converted, spelling);
            *location = token + 1;
            result = type.length == 0 ? NULL : check_warning_text(scratch, "operand of type %.*s where arithmetic or pointer type is required", (int)type.length, type.pointer);
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_CAST && operand_type != 0 && token < parser->tokens.count &&
                 parser->tree.roles[token] == PARSE_ROLE_CLOSE_CAST && node < stream->count && graph->types != NULL &&
                 (check_action_kind(types, graph->types[node]) == BUSTER_TYPE_KIND_UNION || check_action_kind(types, graph->types[node]) == BUSTER_TYPE_KIND_STRUCT))
        {
            // A cast to a record from no type compatible with it (#2301, clang's CheckCStyleCast), at the cast's `(`: to a
            // union from no member's type, err_typecheck_cast_to_union_no_type with the operand's type after its lvalue
            // conversion (an array's or a function's decay, its qualifiers dropped), its sugar kept; to a struct,
            // err_typecheck_cond_expect_scalar with the struct's type as written.
            const TypeId target = graph->types[node];
            const bool to_union = check_action_kind(types, target) == BUSTER_TYPE_KIND_UNION;
            const TypeId converted = stream->conversions[node].operands[0].type != 0 ? stream->conversions[node].operands[0].type : type_unqualified(operand_type);
            const uint32_t spelling = converted == type_unqualified(operand_type) ? check_semantic_operand_spelling(parser, input, stream, types, operand) : 0;
            const String8 type = to_union ? check_driver_warning_type(parser, pp, input, types, spell, scratch, converted, spelling) :
                                            check_driver_warning_type(parser, pp, input, types, spell, scratch, target, check_input_operand_type_spelling(input, parser, node));
            const uint32_t open = check_warning_graph_void_opener(parser, token);
            *location = open < parser->tokens.count ? open : token;
            result = type.length == 0 ? NULL :
                     to_union ? check_warning_text(scratch, "cast to union type from type %.*s not present in union", (int)type.length, type.pointer) :
                                check_warning_text(scratch, "used type %.*s where arithmetic or pointer type is required", (int)type.length, type.pointer);
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_CAST && operand_type != 0 && token + 1 < parser->tokens.count &&
                 parser->tree.roles[token] == PARSE_ROLE_CLOSE_CAST && node < stream->count && graph->types != NULL &&
                 check_semantic_cast_pointer_arithmetic(types, graph->types[node], operand_type) != 0)
        {
            // A cast between a pointer and an arithmetic type that isn't an integer's (#2914, clang's CheckCStyleCast), at
            // the operand's first token: to a floating or complex type from a pointer, err_cast_pointer_to_non_pointer_int
            // with the target as written; to a pointer from a floating or complex operand,
            // err_cast_pointer_from_non_pointer_int with the operand's type after its lvalue conversion, its sugar kept.
            const bool to_pointer = check_semantic_cast_pointer_arithmetic(types, graph->types[node], operand_type) == 2;
            const TypeId converted = type_unqualified(type_strip_aligned(types, operand_type));
            const bool cast = operand < count && graph->ops[operand] == CHECK_OP_CAST;
            const uint32_t spelling = converted != operand_type ? 0 :
                                      cast ? check_input_operand_type_spelling(input, parser, operand) : check_semantic_operand_spelling(parser, input, stream, types, operand);
            const TypeId target = type_strip_aligned(types, graph->types[node]);
            const bool atomic = type_kind(types, target) == BUSTER_TYPE_KIND_ATOMIC;
            const String8 type = to_pointer ? check_driver_warning_type(parser, pp, input, types, spell, scratch, converted, spelling) :
                                 atomic ? check_driver_warning_type(parser, pp, input, types, spell, scratch, types->types.base[type_index(target)], 0) :
                                          check_driver_warning_type(parser, pp, input, types, spell, scratch, graph->types[node],
                                                                    check_input_operand_type_spelling(input, parser, node));
            *location = token + 1;
            result = type.length == 0 ? NULL :
                     to_pointer ? check_warning_text(scratch, "operand of type %.*s cannot be cast to a pointer type", (int)type.length, type.pointer) :
                                  check_warning_text(scratch, "pointer cannot be cast to type %.*s", (int)type.length, type.pointer);
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_CAST && operand_type != 0 && token + 1 < parser->tokens.count &&
                 parser->tree.roles[token] == PARSE_ROLE_CLOSE_CAST && node < stream->count && graph->types != NULL &&
                 check_semantic_cast_nullptr(types, graph->types[node], operand_type))
        {
            // A cast to or from nullptr_t that C23 6.5.4p4 doesn't allow (#2914, clang's err_bad_cstyle_cast_generic), at
            // the operand's first token: "cannot cast an object of type 'S' to 'T'", S the operand's type after its lvalue
            // conversion, its sugar kept, and T the target as written (an _Atomic one's value type); the nullptr_t side
            // is 'nullptr_t', its sugar dropped, as clang prints it. (An array or a function as its decay made it.)
            const uint8_t operand_kind = check_action_kind(types, operand_type);
            const bool decays = type_kind_is_array(operand_kind) || operand_kind == BUSTER_TYPE_KIND_FUNCTION;
            const TypeId converted = decays && stream->conversions[node].operands[0].type != 0 ? stream->conversions[node].operands[0].type :
                                     type_unqualified(type_strip_aligned(types, operand_type));
            const bool cast = operand < count && graph->ops[operand] == CHECK_OP_CAST;
            const bool from_nullptr = check_action_kind(types, converted) == BUSTER_TYPE_KIND_NULLPTR;
            const uint32_t spelling = converted != operand_type || from_nullptr ? 0 :
                                      cast ? check_input_operand_type_spelling(input, parser, operand) : check_semantic_operand_spelling(parser, input, stream, types, operand);
            const TypeId target = type_strip_aligned(types, graph->types[node]);
            const bool atomic = type_kind(types, target) == BUSTER_TYPE_KIND_ATOMIC;
            const String8 from = check_driver_warning_type(parser, pp, input, types, spell, scratch, from_nullptr ? BUSTER_TYPE_NULLPTR : converted, spelling);
            const String8 to = !from_nullptr ? check_driver_warning_type(parser, pp, input, types, spell, scratch, BUSTER_TYPE_NULLPTR, 0) :
                               atomic ? check_driver_warning_type(parser, pp, input, types, spell, scratch, types->types.base[type_index(target)], 0) :
                                        check_driver_warning_type(parser, pp, input, types, spell, scratch, graph->types[node],
                                                                  check_input_operand_type_spelling(input, parser, node));
            *location = token + 1;
            result = from.length == 0 || to.length == 0 ? NULL :
                     check_warning_text(scratch, "cannot cast an object of type %.*s to %.*s", (int)from.length, from.pointer, (int)to.length, to.pointer);
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_SUBTRACT && operand_type != 0 && graph->operand1[node] < stream->count &&
                 check_action_kind(types, operand_type) == BUSTER_TYPE_KIND_POINTER &&
                 check_action_kind(types, check_stream_type(stream, graph->operand1[node])) == BUSTER_TYPE_KIND_POINTER)
        {
            // clang's err_typecheck_sub_ptr_compatible (#1872): two pointers to incompatible types, as written.
            const uint32_t second = graph->operand1[node];
            const String8 left = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, operand_type, operand,
                                                           check_semantic_operand_spelling(parser, input, stream, types, operand));
            const String8 right = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, check_stream_type(stream, second), second,
                                                            check_semantic_operand_spelling(parser, input, stream, types, second));
            result = left.length != 0 && right.length != 0 ? check_warning_text(scratch, "%.*s and %.*s are not pointers to compatible types", (int)left.length,
                                                                                left.pointer, (int)right.length, right.pointer) : NULL;
        }
        else if (report->diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED && op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT &&
                 operand_type != 0 && !check_action_steppable(types, operand_type))
        {
            // clang's err_typecheck_illegal_increment_decrement, the operand's type as written (its qualifiers kept),
            // an _Atomic one's value type.
            const TypeId stripped = type_strip_aligned(types, operand_type);
            const TypeId value = type_kind(types, stripped) == BUSTER_TYPE_KIND_ATOMIC ? types->types.base[type_index(stripped)] : operand_type;
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, value, operand,
                                                           check_semantic_operand_spelling(parser, input, stream, types, operand));
            const bool up = op == CHECK_OP_PRE_INCREMENT || op == CHECK_OP_POST_INCREMENT;
            result = type.length != 0 ? check_warning_text(scratch, "cannot %s value of type %.*s", up ? "increment" : "decrement", (int)type.length, type.pointer) : NULL;
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPLETE && ((op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT) || op == CHECK_OP_ADD ||
                 op == CHECK_OP_SUBTRACT || op == CHECK_OP_ADD_ASSIGN || op == CHECK_OP_SUBTRACT_ASSIGN || op == CHECK_OP_SUBSCRIPT))
        {
            // clang's arithmetic on a pointer to an incomplete type (#147, err_typecheck_arithmetic_incomplete_or_sizeless_type,
            // a subscript's err_subscript_incomplete_or_sizeless_type at its base): the pointee as written, with clang's
            // "forward declaration" note at the tag's declaring name.
            const uint32_t second = node < count ? graph->operand1[node] : BUSTER_CHECK_NONE;
            const TypeId second_type = second < stream->count ? check_stream_type(stream, second) : 0;
            const bool first_pointer = operand_type != 0 && check_action_kind(types, operand_type) == BUSTER_TYPE_KIND_POINTER;
            const uint32_t pointer = first_pointer ? operand : second;
            const TypeId pointer_type = first_pointer ? operand_type : second_type;
            // (A function's name, either operand, which the subscript decays: its function type is the pointee's.)
            const bool designator = op == CHECK_OP_SUBSCRIPT && operand_type != 0 && check_action_kind(types, operand_type) == BUSTER_TYPE_KIND_FUNCTION;
            const bool designator_second = op == CHECK_OP_SUBSCRIPT && !designator && second_type != 0 && check_action_kind(types, second_type) == BUSTER_TYPE_KIND_FUNCTION;
            const TypeId pointee = designator ? operand_type : designator_second ? second_type :
                                   pointer_type != 0 && check_action_kind(types, pointer_type) == BUSTER_TYPE_KIND_POINTER ?
                                   type_element(types, type_unqualified(type_strip_aligned(types, pointer_type))) : 0;
            const TypeId bare = pointee != 0 ? type_unqualified(type_strip_aligned(types, pointee)) : 0;
            const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_VOID;
            const bool tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
            // A subscript of a function's pointer (a function's name decayed): clang's err_subscript_function_type (#2406).
            const bool function = op == CHECK_OP_SUBSCRIPT && kind == BUSTER_TYPE_KIND_FUNCTION;
            const String8 type = tag ? check_driver_warning_type(parser, pp, input, types, spell, scratch, pointee, check_semantic_operand_spelling(parser, input, stream, types, pointer)) :
                                 function && !designator && !designator_second ?
                                 check_driver_warning_type(parser, pp, input, types, spell, scratch, pointee, check_semantic_operand_spelling(parser, input, stream, types, pointer)) :
                                 function ? check_driver_warning_type(parser, pp, input, types, spell, scratch, pointee, 0) : (String8){ "", 0 };
            if (type.length != 0)
            {
                result = check_warning_text(scratch, function ? "subscript of pointer to function type %.*s" :
                                                     op == CHECK_OP_SUBSCRIPT ? "subscript of pointer to incomplete type %.*s" :
                                                     "arithmetic on a pointer to an incomplete type %.*s", (int)type.length, type.pointer);
                // (A subscript's at its `[`, the opener of its node's `]`.)
                uint32_t open = token;
                while (op == CHECK_OP_SUBSCRIPT && open != 0 && open < parser->tokens.count &&
                       !(parser->tokens.kinds[open] == PARSE_KIND_L_SQUARE && parser->tree.parents[open] == token))
                {
                    open -= 1;
                }
                // (A function's pointer's at that operand, as clang's is at the base.)
                const uint32_t based = designator ? operand : designator_second ? second : pointer;
                *location = function && based < count && graph->tokens[based] < parser->tokens.count ? check_semantic_first_token(parser, graph->tokens[based]) : open;
                *note = function ? *note : check_semantic_forward_note(parser, pp, input, types, spell, bare, scratch);
            }
        }
        else if (report->diagnostic == CHECK_DIAG_POINTER_REQUIRED && op == CHECK_OP_DEREFERENCE && operand_type != 0)
        {
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, operand_value_type, operand,
                                                           check_semantic_operator_spelling(parser, input, stream, types, operand));
            result = type.length != 0 ? check_warning_text(scratch, "indirection requires pointer operand (%.*s invalid)", (int)type.length, type.pointer) : NULL;
            // The operand (#2376), clang's Op->getSourceRange().
            if (result != NULL && operand < count)
            {
                check_semantic_range(parser, pp, graph->tokens[operand], true);
            }
        }
        else if ((report->diagnostic == CHECK_DIAG_POINTER_REQUIRED || report->diagnostic == CHECK_DIAG_INTEGER_REQUIRED) &&
                 op == CHECK_OP_SUBSCRIPT && token < parser->tokens.count)
        {
            // Both refusals point to `[`: an invalid index where either operand supplies a pointer (or the base a
            // vector), otherwise no subscriptable value. The action can reject a reversed noninteger index before
            // checking its base, so `1[s]` still needs the latter message (#2441, #2406).
            const uint32_t second = graph->operand1[node];
            const TypeId second_type = second < stream->count ? check_semantic_operand_value_type(types, stream, node, 1, check_stream_type(stream, second)) : 0;
            const bool subscriptable = check_action_kind(types, operand_value_type) == BUSTER_TYPE_KIND_POINTER ||
                                       check_action_kind(types, second_type) == BUSTER_TYPE_KIND_POINTER || check_vector_type(types, operand_value_type);
            uint32_t open = token;
            while (open != 0 && !(parser->tokens.kinds[open] == PARSE_KIND_L_SQUARE && parser->tree.parents[open] == token))
            {
                open -= 1;
            }
            *location = open;
            result = subscriptable && report->diagnostic == CHECK_DIAG_INTEGER_REQUIRED ? "array subscript is not an integer" :
                                                                                         "subscripted value is not an array, pointer, or vector";
            if (operand < count && (!subscriptable || (check_action_kind(types, second_type) == BUSTER_TYPE_KIND_POINTER && !check_vector_type(types, operand_value_type))))
            {
                check_semantic_range(parser, pp, graph->tokens[operand], true);
            }
            if (second < count && (!subscriptable || check_action_kind(types, operand_value_type) == BUSTER_TYPE_KIND_POINTER || check_vector_type(types, operand_value_type)))
            {
                check_semantic_range(parser, pp, graph->tokens[second], true);
            }
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPLETE && (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF) && token < parser->tokens.count &&
                 graph->tokens[node] == token)
        {
            // clang's err_sizeof_alignof_incomplete_or_sizeless_type, named by the keyword as clang spells it (`alignof` for
            // _Alignof and alignof, `__alignof` for GNU's): a type name's at the keyword, its typedef as written; an
            // expression's at the expression, its type as the operand's. The tag's "forward declaration" note.
            const bool expression = operand < stream->count;
            const TypeId named = expression ? operand_type : graph->types != NULL ? graph->types[node] : 0;
            uint32_t spelling = expression ? check_semantic_operand_spelling(parser, input, stream, types, operand) : 0;
            const uint32_t close = !expression && token + 1 < parser->tokens.count && parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN ?
                                   parser->tree.parents[token + 1] : token;
            for (uint32_t type_name = token + 2; !expression && spelling == 0 && type_name < close && close < parser->tokens.count; type_name += 1)
            {
                const uint32_t resolved = parser->tokens.kinds[type_name] == PARSE_KIND_IDENTIFIER && input->declarations != NULL ? input->declarations[type_name] : 0;
                spelling = resolved != 0 && resolved < input->token_count && input->declaration_kinds[resolved] == C_BINDING_TYPEDEF ? resolved : 0;
            }
            const String8 type = named != 0 ? check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, named, expression ? operand : BUSTER_CHECK_NONE,
                                                                        spelling) : (String8){ "", 0 };
            const uint32_t keyword = parser->tokens.names[token];
            const char* word = op == CHECK_OP_SIZEOF ? "sizeof" : keyword == C_NAME_ALIGNOF_C11 || keyword == C_NAME_ALIGNOF ? "alignof" : "__alignof";
            if (type.length != 0)
            {
                result = check_warning_text(scratch, "invalid application of '%s' to an incomplete type %.*s", word, (int)type.length, type.pointer);
                *location = expression ? token + 1 : token;
                *note = check_semantic_forward_note(parser, pp, input, types, spell, type_unqualified(type_strip_aligned(types, named)), scratch);
            }
        }
        else if (report->diagnostic == CHECK_DIAG_CALL && op == CHECK_OP_CALL && operand_type != 0 && graph->tokens[node] == token)
        {
            // clang's err_typecheck_call_not_function at the call's `(`, the callee's type as written once loaded: without
            // its qualifiers and _Atomic, so that a typedef's name stays only where it is the loaded type (`const T`'s T,
            // not a `const int` typedef's; review on #1802). An array's or function's qualifiers are its elements', which
            // lvalue-to-rvalue never reaches (clang's DefaultLvalueConversion decays first), so they stay.
            const uint8_t callee_kind = check_action_kind(types, operand_type);
            const bool decays = type_kind_is_array(callee_kind) || callee_kind == BUSTER_TYPE_KIND_FUNCTION;
            const TypeId plain = decays ? operand_type : type_unqualified(operand_type);
            const TypeId loaded = !decays && type_kind(types, plain) == BUSTER_TYPE_KIND_ATOMIC ? type_unqualified(types->types.base[type_index(plain)]) : plain;
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, loaded, operand,
                                                           check_semantic_operand_spelling(parser, input, stream, types, operand));
            uint32_t open = token;
            while (open != 0 && open < parser->tokens.count && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == token))
            {
                open -= 1;
            }
            if (type.length != 0)
            {
                result = check_warning_text(scratch, "called object type %.*s is not a function or function pointer", (int)type.length, type.pointer);
                *location = open;
                check_semantic_range(parser, pp, graph->tokens[operand], true);
            }
        }
        else if ((report->diagnostic == CHECK_DIAG_LVALUE_REQUIRED || report->diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED) &&
                 (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)) && operand < stream->count &&
                 (check_stream_value(stream, operand).flags & BUSTER_CHECK_VECTOR_DUPLICATE) != 0)
        {
            result = "vector is not assignable (contains duplicate components)";
        }
        else if (report->diagnostic == CHECK_DIAG_LVALUE_REQUIRED && op == CHECK_OP_ADDRESS && operand < stream->count &&
                 (check_stream_value(stream, operand).flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0)
        {
            const CheckValue value = check_stream_value(stream, operand);
            if (value.category == CHECK_LVALUE && (value.flags & BUSTER_CHECK_VECTOR_DUPLICATE) == 0)
            {
                result = "address of vector element requested";
            }
            else
            {
                const String8 type = check_semantic_vector_type(parser, pp, input, stream, types, spell, scratch, operand_type,
                                                                 check_semantic_operand_spelling(parser, input, stream, types, operand), operand);
                result = type.length != 0 ? check_warning_text(scratch, "cannot take the address of an rvalue of type %.*s", (int)type.length, type.pointer) : NULL;
            }
        }
        else if (report->diagnostic == CHECK_DIAG_LVALUE_REQUIRED && op == CHECK_OP_ADDRESS && operand_type != 0)
        {
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, operand_type, operand,
                                                           check_semantic_operator_spelling(parser, input, stream, types, operand));
            result = type.length != 0 ? check_warning_text(scratch, "cannot take the address of an rvalue of type %.*s", (int)type.length, type.pointer) : NULL;
            check_semantic_range(parser, pp, graph->tokens[operand], false);
        }
        else if (report->diagnostic == CHECK_DIAG_LVALUE_REQUIRED &&
                 (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)))
        {
            // GCC's cast as an lvalue (#2264, clang's IsModifiable, CM_LValueCast): a cast, through parentheses, whose
            // operand is an lvalue through its implicit conversions, "assignment to cast is illegal, lvalue casts are not
            // supported" at the cast's `(`, for assignments, compound assignments, increments and decrements alike.
            uint32_t inner = operand < count && graph->ops[operand] == CHECK_OP_CAST ? graph->operand0[operand] : BUSTER_CHECK_NONE;
            for (uint32_t steps = 0; inner < count && graph->ops[inner] == CHECK_OP_CONVERT && steps < count; steps += 1)
            {
                inner = graph->operand0[inner];
            }
            const bool cast = inner < stream->count && check_stream_value(stream, inner).category == CHECK_LVALUE;
            const uint32_t close = cast ? graph->tokens[operand] : BUSTER_CHECK_NONE;
            uint32_t open = close;
            while (open != 0 && open < parser->tokens.count && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == close))
            {
                open -= 1;
            }
            const bool found = open < parser->tokens.count && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == close;
            result = found ? "assignment to cast is illegal, lvalue casts are not supported" : "expression is not assignable";
            *location = found ? open : token;
            // A cast's parser subtree ends at its type's `)`: its operand has a distinct graph node.
            if (found)
            {
                const PPDiagnosticRange cast_operand = check_semantic_token_range(parser, graph->tokens[inner], true);
                if (cast_operand.pending == PP_RANGE_TOKENS)
                {
                    pp_diagnostic_range_tokens(pp, parse_source_index(&parser->tokens, open), cast_operand.end);
                }
            }
            else if (operand < count)
            {
                check_semantic_range(parser, pp, graph->tokens[operand], true);
            }
            if (found)
            {
                const uint32_t source = parse_source_index(&parser->tokens, token);
                pp_diagnostic_range_tokens(pp, source, source);
            }
        }
        else if (report->diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED && operand_type != 0)
        {
            const String8 type = check_driver_operand_type(parser, pp, input, stream, types, spell, scratch, operand_type, operand, 0);
            const uint8_t kind = check_action_kind(types, operand_type);
            if (type_kind_is_array(kind))
            {
                result = type.length != 0 ? check_warning_text(scratch, "array type %.*s is not assignable", (int)type.length, type.pointer) : NULL;
            }
            else if (check_semantic_const(types, operand_type))
            {
                result = check_semantic_const_assignment(parser, pp, input, stream, types, spell, operand, note, &note_count, scratch);
            }
            else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
            {
                // A record with a const member: its first, else what the lvalue's chain finds.
                const char* fields = check_semantic_const_fields(parser, pp, input, stream, types, operand, operand_type, note, &note_count, scratch);
                result = fields != NULL ? fields : check_semantic_const_assignment(parser, pp, input, stream, types, spell, operand, note, &note_count, scratch);
            }
            // The expression assigned to (#2376), clang's E->getSourceRange() in DiagnoseConstAssignment and its kin.
            if (result != NULL && operand < count)
            {
                check_semantic_range(parser, pp, graph->tokens[operand], true);
            }
        }
        else if (report->diagnostic == CHECK_DIAG_COUNTED_INCOMPLETE)
        {
            const char* counted = check_semantic_counted_incomplete(parser, pp, input, stream, types, report, spell, location, note, &note_count, scratch);
            result = counted != NULL ? counted : result;
        }
        else if (report->diagnostic == CHECK_DIAG_ADDRESS_SPACE || report->diagnostic == CHECK_DIAG_ADDRESS_SPACE_OVERLAP ||
                 report->diagnostic == CHECK_DIAG_ADDRESS_SPACE_NESTED)
        {
            result = check_semantic_address_space(parser, pp, input, stream, types, report, spell, location, note, scratch);
        }
        else if ((report->diagnostic == CHECK_DIAG_INVALID_TYPE || report->diagnostic == CHECK_DIAG_INCOMPATIBLE) && op == CHECK_OP_BUILTIN_MISC &&
                 graph->members[node] == CBUILTIN_COMPLEX && graph->argument_counts[node] == 2 && graph->argument_tokens != NULL &&
                 graph->argument_offsets[node] + 2 <= graph->argument_count)
        {
            // __builtin_complex's arguments (#148), worded and placed as clang's BuiltinComplex reports them: an argument of no
            // real floating type, the first such, by its type as written (before its lvalue conversion: an array's, a
            // function's, a qualified one's, an _Atomic one's), at the argument; or two of different types once converted
            // (their qualifiers dropped, their typedefs kept), at the first (check_builtin_misc_complex decides which).
            const uint32_t offset = graph->argument_offsets[node];
            const uint32_t first = graph->argument_nodes[offset];
            const uint32_t second = graph->argument_nodes[offset + 1];
            const TypeId first_type = first < stream->count ? check_stream_type(stream, first) : 0;
            const TypeId second_type = second < stream->count ? check_stream_type(stream, second) : 0;
            // A cast's typedef is its type name's (`(D)1`'s D), a sizeof's or an alignof's clang's __size_t
            // (check_input_operand_type_spelling's sentinel), any other operand's its object's.
            const bool first_typed = first < graph->count && (graph->ops[first] == CHECK_OP_CAST || graph->ops[first] == CHECK_OP_SIZEOF ||
                                                              graph->ops[first] == CHECK_OP_ALIGNOF);
            const bool second_typed = second < graph->count && (graph->ops[second] == CHECK_OP_CAST || graph->ops[second] == CHECK_OP_SIZEOF ||
                                                                graph->ops[second] == CHECK_OP_ALIGNOF);
            const uint32_t first_spelling = first_typed ? check_input_operand_type_spelling(input, parser, first) :
                                                          check_semantic_operand_spelling(parser, input, stream, types, first);
            const uint32_t second_spelling = second_typed ? check_input_operand_type_spelling(input, parser, second) :
                                                            check_semantic_operand_spelling(parser, input, stream, types, second);
            if (report->diagnostic == CHECK_DIAG_INVALID_TYPE && first_type != 0 && second_type != 0)
            {
                const bool second_wrong = check_action_kind(types, first_type) == BUSTER_TYPE_KIND_FLOATING;
                const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, second_wrong ? second_type : first_type,
                                                               second_wrong ? second_spelling : first_spelling);
                result = type.length != 0 ? check_warning_text(scratch, "argument type %.*s is not a real floating point type", (int)type.length, type.pointer) : NULL;
                *location = graph->argument_tokens[offset + (second_wrong ? 1u : 0u)];
            }
            else if (first_type != 0 && second_type != 0)
            {
                const String8 real = check_driver_warning_type(parser, pp, input, types, spell, scratch, type_unqualified(first_type), first_spelling);
                const String8 imaginary = check_driver_warning_type(parser, pp, input, types, spell, scratch, type_unqualified(second_type), second_spelling);
                result = real.length != 0 && imaginary.length != 0 ? check_warning_text(scratch, "arguments are of different types (%.*s vs %.*s)", (int)real.length,
                                                                                         real.pointer, (int)imaginary.length, imaginary.pointer) : NULL;
                *location = graph->argument_tokens[offset];
            }
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_CONVERT && token < parser->tokens.count &&
                 parser->tokens.kinds[token] == PARSE_KIND_RETURN && check_action_kind(types, graph->types[node]) == BUSTER_TYPE_KIND_VOID)
        {
            const String8 name = check_semantic_name(parser, pp, check_semantic_function(parser, input, token));
            if (name.length != 0)
            {
                result = check_warning_text(scratch, "void function '%.*s' should not return a value [-Wreturn-mismatch]", (int)name.length, name.pointer);
            }
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_INITIALIZE && operand_type != 0)
        {
            const TypeId target = graph->types[node];
            const uint8_t target_kind = check_action_kind(types, target);
            const uint8_t source_kind = check_action_kind(types, operand_type);
            const bool source_pointer = source_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(source_kind) || source_kind == BUSTER_TYPE_KIND_FUNCTION;
            const bool target_pointer = target_kind == BUSTER_TYPE_KIND_POINTER;
            const bool function_pointer = target_pointer && check_action_kind(types, type_element(types, target)) == BUSTER_TYPE_KIND_FUNCTION;
            uint32_t width = 0;
            bool sign = false;
            const bool integer = check_action_integer_type(types, target, &width, &sign);
            if ((integer && source_pointer) || (target_pointer && source_pointer))
            {
                const String8 to = check_driver_warning_type(parser, pp, input, types, spell, scratch, target, 0);
                const String8 from = check_driver_warning_type(parser, pp, input, types, spell, scratch, operand_type, 0);
                for (uint32_t declaration = 0; declaration < input->token_count; declaration += 1)
                {
                    if (check_sparse_get(input->initializer_nodes, declaration) == node)
                    {
                        *location = declaration;
                        break;
                    }
                }
                result = to.length != 0 && from.length != 0 ? check_warning_text(scratch,
                    "%s initializing %.*s with an expression of type %.*s [%s]",
                    integer ? "incompatible pointer to integer conversion" : function_pointer ? "incompatible function pointer types" : "incompatible pointer types",
                    (int)to.length, to.pointer, (int)from.length, from.pointer,
                    integer ? "-Wint-conversion" : function_pointer ? "-Wincompatible-function-pointer-types" : "-Wincompatible-pointer-types") : NULL;
            }
        }
    }
    // A register variable's address (#2376): its operand without its parentheses, clang's op->getSourceRange() once
    // CheckAddressOfOperand has looked through them.
    if (report->message == CHECK_MESSAGE_ACTION && report->diagnostic == CHECK_DIAG_REGISTER_ADDRESS && op == CHECK_OP_ADDRESS && operand < count)
    {
        check_semantic_range(parser, pp, graph->tokens[operand], false);
    }
    return check_semantic_text(pp, result);
}

static const char* check_semantic_control(Preprocessor* pp, const TypeTable* types, CheckProgramStatus status, const CheckProgramFailure* failure, Arena* scratch)
{
    const char* result = status == CHECK_PROGRAM_RESOURCE ? "checker program capacity exhausted" :
                         status == CHECK_PROGRAM_UNSUPPORTED ? "unsupported C statement" : "invalid C statement or control flow";
    if (status == CHECK_PROGRAM_INVALID && failure->message == CHECK_PROGRAM_MESSAGE_DUPLICATE_DEFAULT)
    {
        result = "multiple default labels in one switch";
    }
    else if (status == CHECK_PROGRAM_INVALID && failure->message == CHECK_PROGRAM_MESSAGE_DUPLICATE_CASE)
    {
        uint32_t width = 0;
        bool sign = false;
        if (check_action_integer_type(types, failure->type, &width, &sign))
        {
            const unsigned __int128 mask = check_action_mask(width);
            const unsigned __int128 value = failure->value & mask;
            const bool negative = sign && (value >> (width - 1)) != 0;
            unsigned __int128 magnitude = negative ? (0 - value) & mask : value;
            // 128 bits need at most 39 decimal digits, plus sign and terminator.
            char text[41];
            uint32_t at = sizeof(text) - 1;
            text[at] = 0;
            do
            {
                text[--at] = '0' + magnitude % 10;
                magnitude /= 10;
            } while (magnitude != 0);
            if (negative)
            {
                text[--at] = '-';
            }
            result = check_warning_text(scratch, "duplicate case value '%s'", text + at);
        }
        else if (failure->wide != NULL && check_wide_type(types, failure->type, &width, &sign))
        {
            // A _BitInt's past 128 bits (#2220): its limbs' decimal digits.
            const CheckValue value = {
                .type = failure->type,
                .category = CHECK_RVALUE,
                .constant_kind = CHECK_CONSTANT_INTEGER,
                .integer = failure->value,
                .floating_storage = { (uint64_t)(uintptr_t)failure->wide, 0 },
            };
            const uint32_t capacity = width / 3 + 3;
            char* text = BUSTER_ARENA_PUSH_ARRAY(scratch, char, capacity + 1);
            const uint32_t length = text != NULL ? check_wide_decimal(types, &value, text, capacity) : 0;
            result = length != 0 ? check_warning_text(scratch, "duplicate case value '%.*s'", (int)length, text) : result;
        }
    }
    return check_semantic_text(pp, result);
}

// One of check_program_diagnose_all's constraint errors in clang's words: a switch condition's type spelled as clang
// spells it, with its typedef's name first (#1447).
static const char* check_semantic_constraint(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                             const CheckProgramConstraint* constraint, Arena* scratch)
{
    const char* result = NULL;
    if (constraint->failure.message == CHECK_PROGRAM_MESSAGE_CONDITION)
    {
        CheckWarningSpelling spell = { 0 };
        // The condition's own expression, through the implicit conversions its switch makes of it (its promotion, the
        // unary plus at its closing parenthesis).
        uint32_t node = constraint->node;
        for (uint32_t steps = 0; steps < 4 && node < input->graph.count &&
             (input->graph.ops[node] == CHECK_OP_CONVERT || input->graph.ops[node] == CHECK_OP_CONDITION ||
              (input->graph.ops[node] == CHECK_OP_POSITIVE && input->graph.tokens[node] < parser->tokens.count &&
               parser->tree.roles[input->graph.tokens[node]] == PARSE_ROLE_CLOSE_CONDITION)); steps += 1)
        {
            node = input->graph.operand0[node];
        }
        const uint32_t spelling = node < input->graph.count ? check_input_operand_type_spelling(input, parser, node) : 0;
        const String8 type = check_driver_warning_type(parser, pp, input, types, &spell, scratch, constraint->failure.type, spelling);
        result = type.length != 0 ? check_warning_text(scratch, "statement requires expression of integer type (%.*s invalid)", (int)type.length, type.pointer) :
                                    NULL;
        result = check_semantic_text(pp, result);
    }
    else
    {
        result = check_semantic_control(pp, types, CHECK_PROGRAM_INVALID, &constraint->failure, scratch);
    }
    return result;
}

// One of check_asms_diagnose_all's statement errors in clang's words (#1447), with an incomplete tag's note at its
// declaring name (`note`, whose message is null where there's none).
static const char* check_semantic_asm(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                      const CheckAsmFailure* failure, PPOutputDiagnosticNote* note, Arena* scratch)
{
    CheckWarningSpelling spell = { 0 };
    const CheckInputAsm* assembly = input->asm_statements + failure->statement;
    const CheckAsmOperand* operand = assembly->operands + failure->operand;
    const bool output = failure->operand < assembly->output_count;
    const int constraint_length = (int)operand->constraint.length;
    const char* constraint = (const char*)operand->constraint.bytes;
    const String8 type = failure->type != 0 ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, failure->type,
                                                                        check_input_operand_type_spelling(input, parser, operand->node)) : (String8){ 0 };
    const char* result = NULL;
    *note = (PPOutputDiagnosticNote){ 0 };
    switch (failure->error)
    {
        case CHECK_ASM_ERROR_GENERIC: result = "invalid assembly operand or constraint"; break;
        case CHECK_ASM_ERROR_OUTPUT_LVALUE: result = "invalid lvalue in asm output"; break;
        case CHECK_ASM_ERROR_CAST: result = "invalid use of a cast in an inline asm context requiring an lvalue [-Winvalid-gnu-asm-cast]"; break;
        case CHECK_ASM_ERROR_OUTPUT_SIZE: result = check_warning_text(scratch, "invalid output size for constraint '%.*s'", constraint_length, constraint); break;
        case CHECK_ASM_ERROR_INPUT_SIZE: result = check_warning_text(scratch, "invalid input size for constraint '%.*s'", constraint_length, constraint); break;
        case CHECK_ASM_ERROR_INPUT_LVALUE: result = check_warning_text(scratch, "invalid lvalue in asm input for constraint '%.*s'", constraint_length, constraint); break;
        case CHECK_ASM_ERROR_BIT_FIELD:
            result = check_warning_text(scratch, "reference to a bit-field in asm %s with a memory constraint '%.*s'", output ? "input" : "output", constraint_length,
                                        constraint);
            break;
        case CHECK_ASM_ERROR_GLOBAL_REGISTER:
            result = check_warning_text(scratch, "reference to a global register variable in asm %s with a memory constraint '%.*s'", output ? "input" : "output",
                                        constraint_length, constraint);
            break;
        case CHECK_ASM_ERROR_BIT_INT:
            result = type.length != 0 ? check_warning_text(scratch, "invalid type %.*s in asm %s", (int)type.length, type.pointer, output ? "input" : "output") : NULL;
            break;
        case CHECK_ASM_ERROR_VOID:
            result = type.length != 0 ? check_warning_text(scratch, "invalid type %.*s in asm input for constraint '%.*s'", (int)type.length, type.pointer,
                                                           constraint_length, constraint) : NULL;
            break;
        case CHECK_ASM_ERROR_INCOMPLETE:
        {
            result = type.length != 0 ? check_warning_text(scratch, "dereference of pointer to incomplete type %.*s", (int)type.length, type.pointer) : NULL;
            // The tag's declaring name: its first token that declares a tag of this type.
            TypeId bare = type_unqualified(type_strip_aligned(types, failure->type));
            const uint8_t kind = type_kind(types, bare);
            const bool tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
            const uint32_t name = tag ? types->tags.name[types->types.index[type_index(bare)]] : 0;
            uint32_t declared = BUSTER_CHECK_NONE;
            for (uint32_t t = 0; name != 0 && declared == BUSTER_CHECK_NONE && t < input->token_count; t += 1)
            {
                declared = parser->tokens.names[t] == name && input->declarations[t] == t && input->declaration_kinds[t] == C_BINDING_TAG_KIND &&
                           type_unqualified(input->declared_types[t]) == bare ? t : BUSTER_CHECK_NONE;
            }
            const String8 named = declared != BUSTER_CHECK_NONE ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, bare, 0) : (String8){ "", 0 };
            if (named.length != 0)
            {
                *note = (PPOutputDiagnosticNote){
                    .token = declared,
                    .message = check_warning_text(scratch, "forward declaration of %.*s", (int)named.length, named.pointer),
                };
            }
            break;
        }
        case CHECK_ASM_ERROR_TIED:
        {
            const uint32_t tied = operand->match < assembly->output_count ? assembly->operands[operand->match].node : BUSTER_CHECK_NONE;
            const String8 output_type = failure->output_type != 0 ?
                check_driver_warning_type(parser, pp, input, types, &spell, scratch, failure->output_type, check_input_operand_type_spelling(input, parser, tied)) :
                (String8){ 0 };
            result = type.length != 0 && output_type.length != 0 ?
                check_warning_text(scratch, "unsupported inline asm: input with type %.*s matching output with type %.*s", (int)type.length, type.pointer,
                                   (int)output_type.length, output_type.pointer) : NULL;
            break;
        }
    }
    return check_semantic_text(pp, result);
}
