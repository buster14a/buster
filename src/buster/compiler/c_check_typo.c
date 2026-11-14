// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typo correction (#1447): clang's "did you mean" for an undeclared name. A value's (Sema::DiagnoseEmptyLookup's
// CorrectTypo, with the parser's CastExpressionIdValidator) suggests and recovers through the candidate, as if it were
// written; an undeclared function's call (ImplicitlyDefineFunction's, a DeclFilterCCC<FunctionDecl>) only notes it.
//
// Clang's rules, as probed against clang 23: the candidates are the names visible at the use (its lookup of each
// spelling of the identifier table), the innermost declaration of each; a name more than a third of the typo's length
// longer or shorter is skipped (TypoCorrectionConsumer::addName), and so is one further than (length + 2) / 3; of the
// rest, the one at the least Levenshtein distance (StringRef::edit_distance, replacements allowed) is taken if no other
// name the callback accepts is as near (CorrectTypo's second best) and the typo's length is at least three times its
// distance. At most 50 searches are made in a unit (-fspell-checking-limit), counted in source order.
//
// Clang also offers keywords, and the first name in parentheses or a cast's operand may be a typedef's name
// (check_typo_place). Within an expression, a keyword or a type name taken is suggested and the name stays undeclared,
// as clang's expression is then rejected. At a statement's start clang parses the statement again with a keyword or a
// type it takes (`whil;` gives "expected '(' after 'while'"), which the bindings pass cannot follow: the name's report
// says so (CHECK_TYPO_STATEMENT), and the driver reads the unit again with the correction in its tokens
// (check_driver_typo_edits, docs/c-checker.md).
//
// Only a unit with an undeclared name pays: the search replays the binding stream's scope stack once (CheckTypoScope),
// with no per-typo walk of the stream. The vector path prefilters the visible names by length in AVX-512 and finds each
// distance with Myers's bit-parallel algorithm, the candidate's equality masks from byte compares; the scalar path, its
// twin and oracle, filters one name at a time and fills the dynamic-programming table.

#define BUSTER_CHECK_TYPO_LIMIT 50
// An undeclared function's call that made no search (CheckInput.implicit_function_corrections): one clang warns of, or
// a library function's, or a C89 unit's.
#define BUSTER_CHECK_TYPO_UNSEARCHED (UINT32_MAX - 1)
// In an undeclared name's report's detail beside its keyword's index + 1 (check_typo_suggestion): the name starts a
// statement, whose suggestion clang's parser takes and reads the statement again with (check_driver_typo_edits).
#define CHECK_TYPO_STATEMENT 0x100u
// Beside it instead: the name is a parameter's unknown type name (check_typo_parameter), which the parser read as `int`
// and clang's corrects to a type, its error "unknown type name" (check_driver_typo_errors).
#define CHECK_TYPO_PARAMETER 0x200u
// A suggestion without a source declaration (#2250): subject is a predeclared typedef's index + 1, or a builtin's
// first use token + 1. The driver reads the corrected name again; neither manufactures a binding event.
#define CHECK_TYPO_PREDEFINED 0x400u
#define CHECK_TYPO_BUILTIN 0x800u

typedef enum CheckTypoMode
{
    // A value: an object, a function or an enumerator (CastExpressionIdValidator); before `=`, `->` or `.`, not a function.
    CHECK_TYPO_VALUE,
    // An undeclared function's call: a function alone (DeclFilterCCC<FunctionDecl>).
    CHECK_TYPO_FUNCTION,
    // A parameter's unknown type name (Sema::DiagnoseUnknownTypeName's): a typedef's name, or a type specifier's or a
    // storage class's keyword; suggested, as a statement's start's, for the driver to read again with.
    CHECK_TYPO_TYPE,
} CheckTypoMode;

typedef enum CheckTypoSource
{
    CHECK_TYPO_BINDING,
    CHECK_TYPO_PREDECLARED,
    CHECK_TYPO_BUILTIN_USE,
} CheckTypoSource;

// The declarations visible at `cursor`, the binding stream's declare events replayed in order as c_bindings_replay does:
// the scope stack, innermost last, with each one's key and its name's length beside it. The permanent entries, from
// `capacity` on, are implicit library declarations, predeclared typedefs and previously used builtin functions: clang
// declares the builtin at file scope (Sema::LazilyCreateBuiltin), so it stays a candidate after its block.
typedef struct CheckTypoScope CheckTypoScope;
struct CheckTypoScope
{
    // A binding event, a predeclared typedef's index, or a builtin's first use token, according to sources.
    uint32_t* events;
    uint32_t* keys;
    uint32_t* lengths;
    uint8_t* sources;
    uint32_t count;
    uint32_t cursor;
    uint32_t token_cursor;
    // A tentative parser probe's invalid statement tail, before its original semicolon (check_typo_probe_end).
    uint32_t token_skip;
    uint32_t capacity;
    uint32_t permanent_count;
    uint32_t permanent_capacity;
};

typedef struct CheckTypoCalls CheckTypoCalls;
struct CheckTypoCalls
{
    uint32_t cursor;
    uint32_t count;
};

struct CheckTypo
{
    // The walk's scope (an undeclared function's call, read in source order) and the binding answers' (a value's).
    CheckTypoScope walk;
    CheckTypoScope answers;
    // A search's prefiltered stack positions, and the kept ones' distances.
    uint32_t* candidates;
    uint32_t* distances;
    uint32_t candidate_capacity;
    uint32_t distance_capacity;
    // The dynamic-programming rows of the scalar path and of names longer than a word.
    uint32_t* row;
    uint32_t row_capacity;
    // The values' searches, made in source order after the walk's calls' (CheckInput's implicit list), and the calls'
    // searches before the latest value's: the limit counts both in source order.
    uint32_t value_tokens[BUSTER_CHECK_TYPO_LIMIT];
    // The uses corrected, each its use event and the handle (declare event + 1) it took, which the unused declarations'
    // analysis counts as a use as clang marks the declaration referenced.
    uint32_t corrected_events[BUSTER_CHECK_TYPO_LIMIT];
    uint32_t corrected_handles[BUSTER_CHECK_TYPO_LIMIT];
    // Their tokens, which CheckInput.corrected_tokens lists for the driver.
    uint32_t corrected_tokens[BUSTER_CHECK_TYPO_LIMIT];
    uint32_t corrected_count;
    // The parameters' unknown type names' suggestions (check_typo_parameter), which CheckInput.parameter_typo_tokens lists.
    uint32_t parameter_tokens[BUSTER_CHECK_TYPO_LIMIT];
    uint32_t parameter_details[BUSTER_CHECK_TYPO_LIMIT];
    uint32_t parameter_types[BUSTER_CHECK_TYPO_LIMIT];
    uint32_t value_count;
    uint32_t function_cursor;
    uint32_t function_searches;
    uint32_t walk_searches;
    // The latest search's suggestion that takes no binding: a keyword's index + 1 or 0, and a type declaration token,
    // predefined index or builtin first use, according to suggested_flags; BUSTER_CHECK_NONE for none.
    uint32_t suggested_token;
    uint32_t suggested_keyword;
    uint32_t suggested_type;
    uint32_t suggested_flags;
    // Whether that name starts a statement, which clang's parser reads again with the suggestion (CHECK_TYPO_STATEMENT).
    bool suggested_statement;
    // The walk's own values' searches (check_typo_walk_value), in its order, and how many of them the answers' have passed.
    uint32_t walk_value_tokens[BUSTER_CHECK_TYPO_LIMIT];
    uint32_t walk_value_count;
    uint32_t walk_value_cursor;
    // The calls of a prototyped function with too few arguments or too many before the latest value's, each a search of
    // clang's own (TryTypoCorrectionForCall's, which suggests nothing in C): counted once a search needs them.
    CheckTypoCalls value_calls;
};

// clang's keywords that a typo correction may offer in C: a statement's (where its function is), an expression's, and
// a declaration's specifiers', the language's own among them (AddKeywordsToConsumer).
static bool check_typo_keyword_offered(const char* keyword, uint32_t standard, bool gnu)
{
    const bool c99 = strcmp(keyword, "restrict") == 0 || strcmp(keyword, "_Bool") == 0;
    const bool c11 = strcmp(keyword, "_Alignof") == 0;
    const bool c23 = strcmp(keyword, "true") == 0 || strcmp(keyword, "false") == 0 || strcmp(keyword, "bool") == 0;
    const bool extension = strcmp(keyword, "typeof") == 0;
    const bool before_c23 = strcmp(keyword, "_Bool") == 0;
    return (!c99 || standard >= 1999) && (!c11 || standard >= 2011) && (!c23 || standard >= 2023) && (!before_c23 || standard < 2023) &&
           (!extension || gnu || standard >= 2023);
}

static const char* const check_typo_keywords[] =
{
    "do", "else", "for", "goto", "if", "return", "switch", "while", "break", "continue", "case", "default",
    "sizeof", "_Alignof", "true", "false",
    "char", "const", "double", "enum", "float", "int", "long", "short", "signed", "struct", "union", "unsigned", "void",
    "volatile", "_Complex", "_Imaginary", "extern", "inline", "static", "typedef", "restrict", "_Bool", "bool", "typeof",
};

// Which of clang's keyword lists each keyword is in (AddKeywordsToConsumer): a statement's, offered within a function;
// `break` where a loop or a switch encloses the name (Scope::getBreakParent), `continue` where a loop does, `case` and
// `default` where a switch does (the function's SwitchStack); an expression's (WantExpressionKeywords); a type
// specifier's or a storage class's (WantTypeSpecifiers).
enum
{
    CHECK_TYPO_KEYWORD_STATEMENT = 1u << 0,
    CHECK_TYPO_KEYWORD_BREAK = 1u << 1,
    CHECK_TYPO_KEYWORD_CONTINUE = 1u << 2,
    CHECK_TYPO_KEYWORD_SWITCH = 1u << 3,
    CHECK_TYPO_KEYWORD_EXPRESSION = 1u << 4,
    CHECK_TYPO_KEYWORD_TYPE = 1u << 5,
    CHECK_TYPO_KEYWORD_REMAINING = CHECK_TYPO_KEYWORD_STATEMENT | CHECK_TYPO_KEYWORD_BREAK | CHECK_TYPO_KEYWORD_CONTINUE | CHECK_TYPO_KEYWORD_SWITCH,
};

static const uint8_t check_typo_keyword_kinds[] =
{
    CHECK_TYPO_KEYWORD_STATEMENT, CHECK_TYPO_KEYWORD_STATEMENT, CHECK_TYPO_KEYWORD_STATEMENT, CHECK_TYPO_KEYWORD_STATEMENT,
    CHECK_TYPO_KEYWORD_STATEMENT, CHECK_TYPO_KEYWORD_STATEMENT, CHECK_TYPO_KEYWORD_STATEMENT, CHECK_TYPO_KEYWORD_STATEMENT,
    CHECK_TYPO_KEYWORD_BREAK, CHECK_TYPO_KEYWORD_CONTINUE, CHECK_TYPO_KEYWORD_SWITCH, CHECK_TYPO_KEYWORD_SWITCH,
    CHECK_TYPO_KEYWORD_EXPRESSION, CHECK_TYPO_KEYWORD_EXPRESSION, CHECK_TYPO_KEYWORD_EXPRESSION, CHECK_TYPO_KEYWORD_EXPRESSION,
    CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE,
    CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE,
    CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE,
    CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE,
    CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE, CHECK_TYPO_KEYWORD_TYPE,
};

_Static_assert(BUSTER_ARRAY_LENGTH(check_typo_keyword_kinds) == BUSTER_ARRAY_LENGTH(check_typo_keywords), "each typo keyword has its lists");

static bool check_typo_reserve(Arena* arena, uint32_t** column, uint32_t* capacity, uint32_t needed, uint32_t used)
{
    bool valid = needed <= *capacity;
    if (!valid)
    {
        const uint32_t doubled = *capacity > UINT32_MAX / 2 ? UINT32_MAX : *capacity * 2;
        const uint32_t grown = doubled > needed ? doubled : needed < 16 ? 16 : needed;
        uint32_t* records = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, grown);
        valid = records != NULL;
        if (valid && used != 0)
        {
            memcpy(records, *column, (uint64_t)used * sizeof(uint32_t));
        }
        *column = valid ? records : *column;
        *capacity = valid ? grown : *capacity;
    }
    return valid;
}

// Whether a spelling names one of the checker-supported builtin functions in the unit's language, without constructing
// its type or a declaration. Parser-only forms such as __builtin_va_arg never reach this lookup as operand identifiers.
static bool check_typo_builtin_name(CheckInputReader* reader, uint32_t name)
{
    const InternSpan span = reader->pp->table->spans[name];
    const uint8_t* spelling = reader->pp->table->records + span.offset;
    bool result = false;
    if (span.length > 1 && spelling[0] == '_')
    {
        uint16_t builtin = reader->builtin_names != NULL ? check_constant_builtin_name(reader, name) : (uint16_t)cbuiltin_resolve((const char*)spelling, span.length);
        builtin = builtin == CBUILTIN_C23_VA_START && reader->pp->options.language.standard < 2023 ? (uint16_t)CBUILTIN_NONE : builtin;
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup((const char*)spelling, span.length);
        const bool library = info != NULL && memchr(info->attributes, 'f', info->attributes_length) != NULL;
        const bool language = info == NULL || check_input_builtin_language(info, &reader->pp->options);
        const bool catalog = builtin != CBUILTIN_NONE && !cbuiltin_stdc_library(builtin);
        result = !library && language && (catalog || cbuiltin_library_source(spelling, span.length) ||
                                         (info != NULL && cbuiltin_source_intrinsic(spelling, span.length)));
    }
    return result;
}

static bool check_typo_permanent(CheckInputReader* reader, CheckTypoScope* scope, uint32_t name, uint32_t event, CheckTypoSource source)
{
    const bool valid = scope->permanent_count < scope->permanent_capacity;
    if (valid)
    {
        const uint32_t at = scope->capacity + scope->permanent_count;
        scope->events[at] = event;
        scope->keys[at] = name;
        scope->lengths[at] = intern_length(reader->pp->table, name);
        scope->sources[at] = (uint8_t)source;
        scope->permanent_count += 1;
    }
    return valid;
}

// Keep the first use, including a name used as a value (whose builtin-not-called error still creates clang's builtin).
static bool check_typo_builtin_add(CheckInputReader* reader, CheckTypoScope* scope, uint32_t name, uint32_t token)
{
    uint32_t previous = BUSTER_CHECK_NONE;
    const uint32_t end = scope->capacity + scope->permanent_count;
    for (uint32_t at = scope->capacity; previous == BUSTER_CHECK_NONE && at < end; at += 1)
    {
        previous = scope->sources[at] == CHECK_TYPO_BUILTIN_USE && scope->keys[at] == name ? at : BUSTER_CHECK_NONE;
    }
    const bool valid = previous != BUSTER_CHECK_NONE || check_typo_permanent(reader, scope, name, token, CHECK_TYPO_BUILTIN_USE);
    if (previous != BUSTER_CHECK_NONE && token < scope->events[previous])
    {
        scope->events[previous] = token;
    }
    return valid;
}

// The opener of `close` with the requested role, from retained child links or a bounded parent-column walk in a test
// reader without them. Attribute token groups can share closer roles, so their opener's role distinguishes them.
static uint32_t check_typo_group_open(const CheckInputReader* reader, uint32_t close, uint8_t role)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t result = BUSTER_CHECK_NONE;
    uint32_t children = 0;
    for (uint32_t child = reader->first_child != NULL ? reader->first_child[close] : BUSTER_CHECK_NONE;
         result == BUSTER_CHECK_NONE && child < count && children < count; children += 1)
    {
        result = parser->tree.roles[child] == role ? child : BUSTER_CHECK_NONE;
        child = reader->next_child != NULL ? reader->next_child[child] : BUSTER_CHECK_NONE;
    }
    for (uint32_t before = close; (reader->first_child == NULL || reader->next_child == NULL) && result == BUSTER_CHECK_NONE && before != 0;)
    {
        before -= 1;
        result = parser->tree.roles[before] == role && parser->tree.parents[before] == close ? before : BUSTER_CHECK_NONE;
    }
    return result;
}

// A declarator's identifier has OPERAND role too, before the reader schedules its declaration. Follow only its
// declarator edges to a specifier: an initializer's value, a bound's contents, a call or another expression ends the
// path. This matters while a bound is being read (`int __builtin_abs[__builtin_absx(1)]`), before any binding exists.
static bool check_typo_declarator_name(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    bool declaration = false;
    uint32_t steps = 0;
    for (uint32_t at = parser->tree.parents[token]; !declaration && at < count && steps < count;)
    {
        const uint8_t role = parser->tree.roles[at];
        const uint8_t kind = parser->tokens.kinds[at];
        declaration = role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG || (role == PARSE_ROLE_CLOSE_SPECIFIER && at < token) ||
                      role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM;
        bool next = (role == PARSE_ROLE_PREFIX && kind == PARSE_KIND_STAR) || role == PARSE_ROLE_CLOSE_GROUP ||
                    role == PARSE_ROLE_CLOSE_FUNCTION || role == PARSE_ROLE_SEPARATOR ||
                    ((role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_BITFIELD) && token < at);
        if (role == PARSE_ROLE_CLOSE_SUBSCRIPT)
        {
            // The base precedes the matching '['; a name in its bound follows it.
            const uint32_t open = check_typo_group_open(reader, at, PARSE_ROLE_OPEN_SUBSCRIPT);
            next = open < count && token < open;
        }
        else if (role == PARSE_ROLE_CLOSE_ATTRIBUTE || role == PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT || role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT)
        {
            const uint8_t opening = role == PARSE_ROLE_CLOSE_ATTRIBUTE ? PARSE_ROLE_OPEN_ATTRIBUTE :
                                    role == PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT ? PARSE_ROLE_OPEN_ATTRIBUTE_LEFT : PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT;
            const uint32_t open = check_typo_group_open(reader, at, opening);
            next = open < count && token < open;
        }
        at = next && !declaration ? parser->tree.parents[at] : BUSTER_CHECK_NONE;
        steps += 1;
    }
    return declaration;
}

// Whether a token in an attribute list is part of an expression argument. GNU unknown attributes still parse those
// expressions, but their first lone identifier and the generated identifier/string argument slots are syntax. C23
// unknown attributes skip opaque balanced tokens, so none of their identifier operands creates a builtin.
static bool check_typo_attribute_argument(CheckInputReader* reader, uint32_t token, uint32_t open, uint32_t close)
{
    const Parser* parser = reader->parser;
    const bool standard = parser->tokens.kinds[open] == PARSE_KIND_L_SQUARE;
    bool expression = false;
    for (uint32_t item = open + 1; !expression && item < close && item <= token;)
    {
        const uint32_t end = check_input_attribute_item_end(parser, item, close);
        if (token < end)
        {
            const bool scoped = standard && item + 3 < end && parser->tokens.kinds[item + 1] == PARSE_KIND_COLON &&
                                parser->tokens.kinds[item + 2] == PARSE_KIND_COLON;
            const uint32_t name_token = item + (scoped ? 3u : 0u);
            const uint32_t name = parser->tokens.names[name_token];
            const String8 scope = scoped ? check_input_attribute_name(reader, parser->tokens.names[item]) : (String8){ "", 0 };
            const String8 spelled = check_input_attribute_name(reader, name);
            const bool known = check_input_attribute_clang_knows(scope, spelled, scoped, standard);
            const uint32_t arguments = name_token + 1;
            const bool argued = arguments < end && parser->tokens.kinds[arguments] == PARSE_KIND_L_PAREN;
            const uint32_t arguments_end = argued ? parser->tree.parents[arguments] : BUSTER_CHECK_NONE;
            const CheckInputArgumentsRow* row = argued ? check_input_attribute_gnu_arguments(reader, name) : NULL;
            const uint32_t flags = row != NULL ? row->flags : 0;
            const uint32_t strings = row != NULL ? row->strings : 0;
            const uint32_t identifiers = BUSTER_CHECK_ARGUMENTS_VARIADIC_IDENTIFIER | BUSTER_CHECK_ARGUMENTS_STRICT_IDENTIFIER;
            const bool custom = check_input_attribute_equal(spelled, "availability") || check_input_attribute_equal(spelled, "external_source_symbol") ||
                                check_input_attribute_equal(spelled, "objc_bridge_related") || check_input_attribute_equal(spelled, "swift_newtype") ||
                                check_input_attribute_equal(spelled, "swift_wrapper");
            const bool type_tag = check_input_attribute_equal(spelled, "type_tag_for_datatype");
            uint32_t index = 0;
            for (uint32_t first = arguments + 1; argued && (!standard || known) && !custom && arguments_end < end &&
                 first < arguments_end && first <= token; index += 1)
            {
                uint32_t stop = first;
                while (stop < arguments_end && parser->tokens.kinds[stop] != PARSE_KIND_COMMA)
                {
                    stop = check_input_attribute_after_group(parser, stop, arguments_end);
                }
                const bool word = parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
                const bool identifier_slot = (index == 0 && (flags & BUSTER_CHECK_ARGUMENTS_IDENTIFIER) != 0) || (flags & identifiers) != 0;
                const bool identifier = identifier_slot && (word || (flags & BUSTER_CHECK_ARGUMENTS_STRICT_IDENTIFIER) != 0);
                const bool lone = !known && !standard && index == 0 && word && stop == first + 1;
                const bool string = index < 32 && ((strings >> index) & 1u) != 0 && (flags & (identifiers | BUSTER_CHECK_ARGUMENTS_TYPE)) == 0;
                const bool type_name = ((flags & BUSTER_CHECK_ARGUMENTS_TYPE) != 0 || (type_tag && index == 1)) && token == first;
                expression = token < stop && !identifier && !lone && !string && !type_name && (!type_tag || index == 1);
                first = stop + 1;
            }
        }
        item = end + 1;
    }
    return expression;
}

static bool check_typo_attribute_operand(CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    bool expression = true;
    uint32_t steps = 0;
    for (uint32_t at = parser->tree.parents[token]; expression && at < count && steps < count; at = parser->tree.parents[at])
    {
        if (parser->tree.roles[at] == PARSE_ROLE_CLOSE_ATTRIBUTE_LIST)
        {
            const uint32_t open = check_typo_group_open(reader, at, PARSE_ROLE_OPEN_ATTRIBUTE_LIST);
            expression = open == BUSTER_CHECK_NONE || check_typo_attribute_argument(reader, token, open, at);
        }
        steps += 1;
    }
    return expression;
}

// An identifier operand's first builtin use, with the source scope replayed up to it. A local object or an internal
// function of the spelling hides the builtin; an external function declaration keeps it (check_input_builtin_kept).
static bool check_typo_builtin_operand(CheckInputReader* reader, CheckTypoScope* scope, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t name = parser->tokens.names[token];
    const uint32_t parent = parser->tree.parents[token];
    const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
    const uint8_t parent_kind = parent < count ? parser->tokens.kinds[parent] : PARSE_KIND_END;
    const bool label = parent_role == PARSE_ROLE_LABEL || parent_kind == PARSE_KIND_GOTO ||
                       (parent_role == PARSE_ROLE_PREFIX && parent_kind == PARSE_KIND_AMPAMP) ||
                       (token + 1 < count && parser->tree.roles[token + 1] == PARSE_ROLE_LABEL);
    const bool declaration = (reader->output->declarations != NULL && reader->output->declarations[token] == token) || label;
    const bool named = !declaration && check_typo_builtin_name(reader, name) && !check_typo_declarator_name(reader, token) &&
                       check_typo_attribute_operand(reader, token);
    uint32_t bound = BUSTER_CHECK_NONE;
    for (uint32_t at = scope->count; named && bound == BUSTER_CHECK_NONE && at != 0; at -= 1)
    {
        bound = scope->keys[at - 1] == name ? scope->events[at - 1] : BUSTER_CHECK_NONE;
    }
    const CBindings* bindings = &reader->bindings;
    const bool external = bound != BUSTER_CHECK_NONE && bindings->kind[bound] == C_BINDING_FUNCTION && reader->declaration_bits != NULL &&
                          check_input_linkage(reader->declaration_bits, bindings->declaration[bound]) == 2;
    const bool builtin = named && (bound == BUSTER_CHECK_NONE || external);
    return !builtin || check_typo_builtin_add(reader, scope, name, token);
}

static bool check_typo_statement_start(const Parser* parser, uint32_t token);

// A recovery view tentatively inserts a semicolon after an unknown statement-start name (`nosuch x = builtin()`),
// before its driver knows whether that name corrects to a keyword/type or leaves an invalid expression. If it is still
// unbound, the parser discards its tail: those temporarily retained tokens must not create builtin candidates. A known
// value, including one the binding answers already corrected, keeps the tail after its missing-semicolon diagnostic.
static uint32_t check_typo_probe_end(CheckInputReader* reader, const CheckTypoScope* scope, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool synthetic = token != 0 && token + 1 < count && parser->tokens.names[token] == 0 && parser->tokens.sources != NULL &&
                           parser->tokens.sources[token] == parser->tokens.sources[token + 1];
    uint32_t name_token = synthetic && parser->tokens.kinds[token - 1] == PARSE_KIND_IDENTIFIER ? token - 1 : BUSTER_CHECK_NONE;
    if (synthetic && parser->tokens.kinds[token - 1] == PARSE_KIND_R_PAREN)
    {
        const uint32_t open = check_typo_group_open(reader, token - 1, PARSE_ROLE_OPEN_CALL);
        name_token = open != 0 && open < count && parser->tokens.kinds[open - 1] == PARSE_KIND_IDENTIFIER ? open - 1 : BUSTER_CHECK_NONE;
    }
    const bool statement = name_token < count && parser->tree.roles[name_token] == PARSE_ROLE_OPERAND && check_typo_statement_start(parser, name_token);
    const uint32_t name = statement ? parser->tokens.names[name_token] : 0;
    bool declared = statement && reader->output->declarations != NULL && reader->output->declarations[name_token] < count;
    for (uint32_t at = scope->count; statement && !declared && at != 0; at -= 1)
    {
        declared = scope->keys[at - 1] == name;
    }
    const bool unknown = statement && !declared && !check_typo_builtin_name(reader, name);
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t at = token + 1; unknown && result == BUSTER_CHECK_NONE && at < count;)
    {
        const uint8_t kind = parser->tokens.kinds[at];
        const bool stop = kind == PARSE_KIND_SEMI || kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE;
        const bool group = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        const uint32_t close = group ? parser->tree.parents[at] : BUSTER_CHECK_NONE;
        result = stop && kind == PARSE_KIND_SEMI ? at : BUSTER_CHECK_NONE;
        at = stop ? count : close > at && close < count ? close + 1 : at + 1;
    }
    return result;
}

static bool check_typo_source_token(CheckInputReader* reader, CheckTypoScope* scope, uint32_t token)
{
    bool valid = true;
    if (token >= scope->token_skip)
    {
        if (reader->parser->tokens.kinds[token] == PARSE_KIND_SEMI)
        {
            const uint32_t end = check_typo_probe_end(reader, scope, token);
            scope->token_skip = end != BUSTER_CHECK_NONE ? end : scope->token_skip;
        }
        else
        {
            valid = check_typo_builtin_operand(reader, scope, token);
        }
    }
    return valid;
}

// Scan only the effective parse's preceding identifier operands. A failed semantic expression still creates its
// builtins, while a tail omitted by parser recovery does not. Source order also makes an outer call's callee visible
// to a typo in its arguments, before the call itself has been checked.
static bool check_typo_builtins_before(CheckInputReader* reader, CheckTypoScope* scope, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint8_t* kinds = parser->tokens.kinds;
    const uint8_t* roles = parser->tree.roles;
    uint32_t token = scope->token_cursor;
    bool valid = true;
    if (BUSTER_VECTOR_PATH(!parser->options.scalar))
    {
        const __m512i identifier = _mm512_set1_epi8(PARSE_KIND_IDENTIFIER);
        const __m512i operand = _mm512_set1_epi8(PARSE_ROLE_OPERAND);
        const __m512i semicolon = _mm512_set1_epi8(PARSE_KIND_SEMI);
        while (valid && token < end)
        {
            const uint32_t left = end - token;
            const uint32_t lanes = left < 64 ? left : 64;
            const __mmask64 live = lanes == 64 ? ~(__mmask64)0 : (((__mmask64)1 << lanes) - 1);
            const __m512i token_kinds = _mm512_maskz_loadu_epi8(live, kinds + token);
            const __m512i token_roles = _mm512_maskz_loadu_epi8(live, roles + token);
            const __mmask64 identifiers = _mm512_mask_cmpeq_epi8_mask(live, token_kinds, identifier);
            uint64_t candidates = (uint64_t)_mm512_mask_cmpeq_epi8_mask(identifiers, token_roles, operand) |
                                  (uint64_t)_mm512_mask_cmpeq_epi8_mask(live, token_kinds, semicolon);
            while (valid && candidates != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctzll(candidates);
                candidates &= candidates - 1;
                valid = check_typo_source_token(reader, scope, token + lane);
            }
            token += lanes;
        }
    }
    else
    {
        for (; valid && token < end; token += 1)
        {
            if (kinds[token] == PARSE_KIND_SEMI || (kinds[token] == PARSE_KIND_IDENTIFIER && roles[token] == PARSE_ROLE_OPERAND))
            {
                valid = check_typo_source_token(reader, scope, token);
            }
        }
    }
    scope->token_cursor = valid && end > scope->token_cursor ? end : scope->token_cursor;
    return valid;
}

// The scope at event `limit` (exclusive), and builtin uses before `token`: the binding stream and the effective parse
// replayed together, stopping at every declaration or restoration so each builtin is tested in its own source scope.
static bool check_typo_scope_advance(CheckInputReader* reader, CheckTypoScope* scope, uint32_t limit, uint32_t token)
{
    const CBindings* bindings = &reader->bindings;
    bool valid = scope->sources != NULL;
    if (scope->events == NULL)
    {
        // A stack event and at most one permanent entry per declaration, plus each operand's builtin and the predefs.
        scope->capacity = bindings->capacity;
        const uint64_t permanent = (uint64_t)bindings->capacity + reader->parser->tokens.count + BUSTER_PARSE_PREDECLARED_TYPEDEFS;
        const uint64_t room = (uint64_t)scope->capacity + permanent;
        scope->events = room <= UINT32_MAX - 32 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, room) : NULL;
        scope->keys = scope->events != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, room) : NULL;
        scope->lengths = scope->keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, room) : NULL;
        scope->sources = scope->lengths != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, room) : NULL;
        scope->permanent_capacity = scope->sources != NULL ? (uint32_t)permanent : 0;
        valid = scope->sources != NULL;
        for (uint32_t p = 0; valid && p < BUSTER_PARSE_PREDECLARED_TYPEDEFS; p += 1)
        {
            const uint32_t name = reader->predefined_names[p];
            valid = name == 0 || check_typo_permanent(reader, scope, name, p, CHECK_TYPO_PREDECLARED);
        }
    }
    const uint32_t end = limit < bindings->count ? limit : bindings->count;
    for (uint32_t e = scope->cursor; valid && e < end; e += 1)
    {
        const uint8_t operation = bindings->operation[e];
        if (operation == C_BINDING_RESTORE || operation == C_BINDING_DECLARE)
        {
            const uint32_t point = bindings->point[e] < token ? bindings->point[e] : token;
            valid = check_typo_builtins_before(reader, scope, point);
        }
        if (valid && operation == C_BINDING_RESTORE)
        {
            const uint32_t mark = bindings->declaration[e];
            scope->count = mark < scope->count ? mark : scope->count;
        }
        else if (valid && operation == C_BINDING_DECLARE)
        {
            const uint32_t key = bindings->key[e];
            const uint32_t name = key % bindings->name_capacity;
            const uint32_t declared = bindings->declaration[e];
            const bool library = declared < reader->parser->tokens.count && reader->output->function_flags != NULL &&
                                 (reader->output->function_flags[declared] & CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY) != 0;
            const bool external = key < bindings->name_capacity && bindings->kind[e] == C_BINDING_FUNCTION && reader->declaration_bits != NULL &&
                                  declared < reader->parser->tokens.count && check_input_linkage(reader->declaration_bits, declared) == 2;
            valid = scope->count < scope->capacity;
            if (valid)
            {
                scope->events[scope->count] = e;
                scope->keys[scope->count] = key;
                scope->lengths[scope->count] = intern_length(reader->pp->table, name);
                scope->sources[scope->count] = CHECK_TYPO_BINDING;
                scope->count += 1;
                valid = !library || check_typo_permanent(reader, scope, name, e, CHECK_TYPO_BINDING);
                valid = valid && (!external || !check_typo_builtin_name(reader, name) || check_typo_builtin_add(reader, scope, name, declared));
            }
        }
    }
    valid = valid && check_typo_builtins_before(reader, scope, token);
    scope->cursor = valid && end > scope->cursor ? end : scope->cursor;
    return valid;
}

// Levenshtein's distance, the dynamic-programming table one row at a time (the scalar path, and the vector path's for
// a name longer than a word): `row` has room for `b_length` + 1.
static uint32_t check_typo_distance_table(const uint8_t* a, uint32_t a_length, const uint8_t* b, uint32_t b_length, uint32_t* row)
{
    for (uint32_t j = 0; j <= b_length; j += 1)
    {
        row[j] = j;
    }
    for (uint32_t i = 1; i <= a_length; i += 1)
    {
        uint32_t diagonal = row[0];
        row[0] = i;
        for (uint32_t j = 1; j <= b_length; j += 1)
        {
            const uint32_t above = row[j];
            const uint32_t replaced = diagonal + (a[i - 1] != b[j - 1] ? 1u : 0u);
            const uint32_t inserted = row[j - 1] + 1;
            const uint32_t deleted = above + 1;
            const uint32_t least = replaced < inserted ? replaced : inserted;
            row[j] = least < deleted ? least : deleted;
            diagonal = above;
        }
    }
    return row[b_length];
}

// Levenshtein's distance by Myers's bit-parallel algorithm (Hyyrö's formulation of it for the global distance): the
// pattern, at most 64 bytes, one bit per byte, its equality masks for each of the text's bytes from one byte compare.
static uint32_t check_typo_distance_bits(const uint8_t* pattern, uint32_t pattern_length, const uint8_t* text, uint32_t text_length)
{
    const __mmask64 used = pattern_length == 64 ? ~(__mmask64)0 : (((__mmask64)1 << pattern_length) - 1);
    const __m512i bytes = _mm512_maskz_loadu_epi8(used, pattern);
    const uint64_t last = (uint64_t)1 << (pattern_length - 1);
    uint64_t positive = (uint64_t)used;
    uint64_t negative = 0;
    uint32_t score = pattern_length;
    for (uint32_t j = 0; j < text_length; j += 1)
    {
        const uint64_t equal = (uint64_t)_mm512_mask_cmpeq_epi8_mask(used, bytes, _mm512_set1_epi8((char)text[j]));
        const uint64_t vertical = equal | negative;
        const uint64_t horizontal = (((equal & positive) + positive) ^ positive) | equal;
        uint64_t up = negative | ~(horizontal | positive);
        uint64_t down = positive & horizontal;
        score += (up & last) != 0 ? 1u : 0u;
        score -= (down & last) != 0 ? 1u : 0u;
        up = (up << 1) | 1;
        down = down << 1;
        positive = (down | ~(vertical | up)) & (uint64_t)used;
        negative = up & vertical;
    }
    return score;
}

static uint32_t check_typo_distance(CheckTypo* typo, bool vector, const uint8_t* a, uint32_t a_length, const uint8_t* b, uint32_t b_length)
{
    const bool bits = vector && a_length != 0 && a_length <= 64;
    return bits ? check_typo_distance_bits(a, a_length, b, b_length) : b_length == 0 ? a_length :
           check_typo_distance_table(a, a_length, b, b_length, typo->row);
}

// The scope's names from `begin` to `end` near enough to the typo's `length` to be taken (3 * |difference| <= length),
// in stack order, their positions written from `candidates` on.
static uint32_t check_typo_prefilter(const CheckTypoScope* scope, uint32_t begin, uint32_t end, uint32_t name_capacity, uint32_t length, bool vector,
                                     uint32_t* candidates)
{
    uint32_t count = 0;
    uint32_t i = begin;
    if (vector)
    {
        const __m512i wanted = _mm512_set1_epi32((int)length);
        const __m512i limit = _mm512_set1_epi32((int)length);
        const __m512i three = _mm512_set1_epi32(3);
        const __m512i names = _mm512_set1_epi32((int)name_capacity);
        const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        for (; i < end; i += 16)
        {
            const uint32_t left = end - i;
            const __mmask16 live = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
            const __m512i keys = _mm512_maskz_loadu_epi32(live, scope->keys + i);
            const __m512i lengths = _mm512_maskz_loadu_epi32(live, scope->lengths + i);
            const __m512i difference = _mm512_abs_epi32(_mm512_sub_epi32(lengths, wanted));
            const __mmask16 ordinary = _mm512_mask_cmplt_epu32_mask(live, keys, names);
            const __mmask16 near = _mm512_mask_cmple_epu32_mask(ordinary, _mm512_mullo_epi32(difference, three), limit);
            _mm512_mask_compressstoreu_epi32(candidates + count, near, _mm512_add_epi32(lanes, _mm512_set1_epi32((int)i)));
            count += (uint32_t)__builtin_popcount((uint32_t)near);
        }
    }
    else
    {
        for (; i < end; i += 1)
        {
            const uint32_t difference = scope->lengths[i] > length ? scope->lengths[i] - length : length - scope->lengths[i];
            if (scope->keys[i] < name_capacity && 3 * difference <= length)
            {
                candidates[count++] = i;
            }
        }
    }
    return count;
}

// What a value's context takes (clang's callbacks): every value; or, before `=`, `->` or `.` within an expression, no
// function (CastExpressionIdValidator); or, before a statement's `=`, an object alone (StatementFilterCCC's VarDecl).
typedef enum CheckTypoTakes
{
    CHECK_TYPO_TAKES_VALUES,
    CHECK_TYPO_TAKES_NO_FUNCTION,
    CHECK_TYPO_TAKES_OBJECT,
} CheckTypoTakes;

// Whether a name's declaration kind is one `mode` takes, in its context.
static bool check_typo_accepted(uint8_t kind, CheckTypoMode mode, CheckTypoTakes takes)
{
    const bool value = kind == C_BINDING_OBJECT || (kind == C_BINDING_ENUMERATOR && takes != CHECK_TYPO_TAKES_OBJECT) ||
                       (kind == C_BINDING_FUNCTION && takes == CHECK_TYPO_TAKES_VALUES);
    return mode == CHECK_TYPO_FUNCTION ? kind == C_BINDING_FUNCTION : value;
}

// Whether the name at `token` starts a statement, where clang's parser classifies it (ClassifyName, StatementFilterCCC):
// after a `;`, a `{` (not an initializer's) or a `}`, a condition's or a for header's `)`, `else`, `do`, or a label's or a
// case's `:`.
static bool check_typo_statement_start(const Parser* parser, uint32_t token)
{
    const uint8_t kind = token != 0 ? parser->tokens.kinds[token - 1] : PARSE_KIND_END;
    const uint8_t role = token != 0 ? parser->tree.roles[token - 1] : PARSE_ROLE_COUNT;
    const bool block = kind == PARSE_KIND_L_BRACE && role != PARSE_ROLE_OPEN_INITIALIZER && role != PARSE_ROLE_OPEN_COMPOUND_BODY;
    return kind == PARSE_KIND_SEMI || block || kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_ELSE || kind == PARSE_KIND_DO ||
           role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR || (kind == PARSE_KIND_COLON && role != PARSE_ROLE_CLOSE_TERNARY);
}

// What a value's name at `token` may be corrected to, by its place (clang's callback there and the keywords it wants).
typedef struct CheckTypoPlace CheckTypoPlace;
struct CheckTypoPlace
{
    CheckTypoTakes takes;
    // CHECK_TYPO_KEYWORD_* of the keywords offered.
    uint32_t keywords;
    // Whether objects, functions and enumerators are taken (not after a cast's `)`), and typedef names: a cast's operand,
    // and the first name in parentheses, which clang's parser reads as a type cast's or an expression's (MaybeTypeCast).
    bool values;
    bool types;
    // A group used as a cast's operand: its unknown name is parsed as a value, so a predefined typedef is no candidate.
    bool cast_group;
    // A statement's start (StatementFilterCCC), where clang parses the statement again with a keyword or a type it
    // takes: the name stays undeclared here, and the driver reads the statement again with it (check_driver_typo_edits).
    bool statement;
};

// The loops and switches around `token`, and whether it is in a function's body (CHECK_TYPO_KEYWORD_*): the parse
// tree's ancestors, the statements a head owns after it, as check_input_statements finds a `break`'s.
static uint32_t check_typo_scopes(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t result = 0;
    uint32_t steps = 0;
    // A do statement's condition, read after its body's scope ends: the `do` that owns the condition the name is in.
    uint32_t condition_of = BUSTER_CHECK_NONE;
    for (uint32_t ancestor = parser->tree.parents[token]; ancestor < count && steps < count; ancestor = parser->tree.parents[ancestor])
    {
        const uint8_t role = parser->tree.roles[ancestor];
        const uint32_t keyword = reader->first_child != NULL ? reader->first_child[ancestor] : BUSTER_CHECK_NONE;
        const uint8_t head = keyword < count ? parser->tokens.kinds[keyword] : PARSE_KIND_END;
        const bool owned = token > ancestor || (role == PARSE_ROLE_DO && ancestor != condition_of);
        condition_of = role == PARSE_ROLE_CLOSE_CONDITION && token < ancestor && head == PARSE_KIND_WHILE ? parser->tree.parents[ancestor] : condition_of;
        const bool loop = owned && (role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_DO || (role == PARSE_ROLE_CLOSE_CONDITION && head == PARSE_KIND_WHILE));
        const bool selection = owned && role == PARSE_ROLE_CLOSE_CONDITION && head == PARSE_KIND_SWITCH;
        result |= (loop ? CHECK_TYPO_KEYWORD_BREAK | CHECK_TYPO_KEYWORD_CONTINUE : 0u) | (selection ? CHECK_TYPO_KEYWORD_BREAK | CHECK_TYPO_KEYWORD_SWITCH : 0u) |
                  (role == PARSE_ROLE_CLOSE_BODY ? CHECK_TYPO_KEYWORD_STATEMENT : 0u);
        steps += 1;
    }
    return result;
}

static CheckTypoPlace check_typo_place(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    // The token after the name as clang's parser sees it: past a `;` a recovery view put in before it (check_prefix_resume,
    // which gives it its next token's source), as after `retrn x` (ParseDiagnostic.probe).
    const uint32_t count = parser->tokens.count;
    const bool synthetic = token + 2 < count && parser->tokens.kinds[token + 1] == PARSE_KIND_SEMI && parser->tokens.names[token + 1] == 0 &&
                           parser->tokens.sources[token + 1] == parser->tokens.sources[token + 2];
    const uint8_t next = token + 1 < count ? parser->tokens.kinds[token + 1 + (synthetic ? 1u : 0u)] : PARSE_KIND_END;
    const uint8_t before = token != 0 ? parser->tree.roles[token - 1] : PARSE_ROLE_COUNT;
    const bool statement = check_typo_statement_start(parser, token);
    const bool member = next == PARSE_KIND_ARROW || next == PARSE_KIND_PERIOD;
    const bool cast = before == PARSE_ROLE_CLOSE_CAST;
    const bool parenthesized = before == PARSE_ROLE_OPEN_GROUP || before == PARSE_ROLE_OPEN_SIZEOF;
    uint32_t group = token;
    while (group != 0 && parser->tree.roles[group - 1] == PARSE_ROLE_OPEN_GROUP)
    {
        group -= 1;
    }
    const bool cast_group = group < token && group != 0 && parser->tree.roles[group - 1] == PARSE_ROLE_CLOSE_CAST;
    // A statement's start: StatementFilterCCC's keywords by the next token. Within an expression, CastExpressionIdValidator's,
    // with a statement's keywords only where no `)` follows (the parser's WantRemainingKeywords).
    // (A statement's start also takes a type before a `*` or a `&`, as clang reads `myint * p` and `myint & 1` as
    // declarations.)
    const bool type_keywords = statement ? next == PARSE_KIND_L_PAREN || next == PARSE_KIND_L_SQUARE || next == PARSE_KIND_IDENTIFIER ||
                                           next == PARSE_KIND_STAR || next == PARSE_KIND_AMP :
                                           cast || parenthesized;
    const bool expression_keywords = statement ? next == PARSE_KIND_L_PAREN || next == PARSE_KIND_IDENTIFIER || member : true;
    const bool remaining_keywords = statement ? next == PARSE_KIND_L_PAREN || next == PARSE_KIND_SEMI || next == PARSE_KIND_IDENTIFIER || next == PARSE_KIND_L_BRACE :
                                    next != PARSE_KIND_R_PAREN;
    const uint32_t wanted = (type_keywords ? CHECK_TYPO_KEYWORD_TYPE : 0u) | (expression_keywords ? CHECK_TYPO_KEYWORD_EXPRESSION : 0u) |
                            (remaining_keywords ? CHECK_TYPO_KEYWORD_REMAINING : 0u);
    const uint32_t scoped = remaining_keywords ? check_typo_scopes(reader, token) | ~(uint32_t)CHECK_TYPO_KEYWORD_REMAINING : ~(uint32_t)CHECK_TYPO_KEYWORD_REMAINING;
    return (CheckTypoPlace){
        .takes = statement && next == PARSE_KIND_EQUAL ? CHECK_TYPO_TAKES_OBJECT :
                 !statement && (next == PARSE_KIND_EQUAL || member) ? CHECK_TYPO_TAKES_NO_FUNCTION : CHECK_TYPO_TAKES_VALUES,
        .keywords = wanted & scoped,
        .values = !cast,
        .types = type_keywords,
        .cast_group = cast_group,
        .statement = statement,
    };
}

// The declaration a typo at `token`, spelled `name`, corrects to, its declare event's, from the declarations visible at
// `limit` (an event index); or BUSTER_CHECK_NONE. A value's search also leaves what it would suggest without taking it in CheckTypo's
// suggested_* (a keyword, or a type name where the place takes one).
static uint32_t check_typo_search(CheckInputReader* reader, CheckTypoScope* scope, uint32_t token, uint32_t name, uint32_t limit, CheckTypoMode mode)
{
    const Parser* parser = reader->parser;
    const CBindings* bindings = &reader->bindings;
    CheckTypo* typo = reader->typo;
    const uint8_t* spelling = (const uint8_t*)intern_bytes(reader->pp->table, name);
    const uint32_t length = intern_length(reader->pp->table, name);
    const uint32_t bound = (length + 2) / 3;
    const bool vector = BUSTER_VECTOR_PATH(!parser->options.scalar);
    uint32_t result = BUSTER_CHECK_NONE;
    typo->suggested_token = token;
    typo->suggested_keyword = 0;
    typo->suggested_type = BUSTER_CHECK_NONE;
    typo->suggested_flags = 0;
    typo->suggested_statement = false;
    // The prefilter's compressed stores write whole vectors: room for one past the stack's.
    bool valid = length >= 3 && check_typo_scope_advance(reader, scope, limit, token) &&
                 check_typo_reserve(reader->arena, &typo->candidates, &typo->candidate_capacity, scope->permanent_count + scope->count + 32, 0) &&
                 check_typo_reserve(reader->arena, &typo->distances, &typo->distance_capacity, scope->permanent_count + scope->count + 32, 0) &&
                 check_typo_reserve(reader->arena, &typo->row, &typo->row_capacity, length + 1, 0);
    reader->output->resource |= length >= 3 && !valid;
    if (valid)
    {
        const uint32_t name_capacity = bindings->name_capacity;
        // The predeclared types and used builtins first, as file scope's, so a stack declaration shadows them even when
        // the place does not take that declaration's kind (a local object hides a predefined typedef in type context).
        const uint32_t permanent = check_typo_prefilter(scope, scope->capacity, scope->capacity + scope->permanent_count, name_capacity, length, vector,
                                                       typo->candidates);
        const uint32_t filtered = permanent + check_typo_prefilter(scope, 0, scope->count, name_capacity, length, vector, typo->candidates + permanent);
        // The names within the bound, their distances beside them.
        uint32_t kept = 0;
        for (uint32_t c = 0; c < filtered; c += 1)
        {
            const uint32_t at = typo->candidates[c];
            const uint32_t other = scope->keys[at] % name_capacity;
            const uint32_t distance = check_typo_distance(typo, vector, (const uint8_t*)intern_bytes(reader->pp->table, other), scope->lengths[at], spelling, length);
            if (distance <= bound)
            {
                typo->candidates[kept] = at;
                typo->distances[kept] = distance;
                kept += 1;
            }
        }
        // Each name's innermost declaration (the latest kept of its key) that the place takes, and the keywords it offers
        // (check_typo_place); the nearest is taken where it is the only one that near, a statement start's typedef or
        // keyword counting as near without being taken.
        const CheckTypoPlace place = mode == CHECK_TYPO_VALUE ? check_typo_place(reader, token) :
                                     mode == CHECK_TYPO_TYPE ? (CheckTypoPlace){ .keywords = CHECK_TYPO_KEYWORD_TYPE, .types = true, .statement = true } :
                                                               (CheckTypoPlace){ .values = true };
        uint32_t best = UINT32_MAX;
        uint32_t best_at = BUSTER_CHECK_NONE;
        uint32_t best_keyword = 0;
        bool best_taken = false;
        uint32_t ties = 0;
        for (uint32_t k = 0; k < kept; k += 1)
        {
            const uint32_t at = typo->candidates[k];
            bool shadowed = false;
            for (uint32_t later = k + 1; later < kept; later += 1)
            {
                shadowed |= scope->keys[typo->candidates[later]] == scope->keys[at];
            }
            const uint8_t source = scope->sources[at];
            const uint32_t event = scope->events[at];
            const uint8_t kind = source == CHECK_TYPO_PREDECLARED ? C_BINDING_TYPEDEF :
                                 source == CHECK_TYPO_BUILTIN_USE ? C_BINDING_FUNCTION : bindings->kind[event];
            const uint32_t distance = typo->distances[k];
            const bool type = mode != CHECK_TYPO_FUNCTION && kind == C_BINDING_TYPEDEF;
            const bool prior = source != CHECK_TYPO_BUILTIN_USE || event < token;
            const bool taken = prior && ((type && place.types && (source != CHECK_TYPO_PREDECLARED || !place.cast_group)) ||
                                      (!type && place.values && check_typo_accepted(kind, mode, place.takes)));
            const bool near = !shadowed && distance != 0 && taken;
            ties = near && distance == best ? ties + 1 : near && distance < best ? 1 : ties;
            best_at = near && distance < best ? at : best_at;
            best_taken = near && distance < best ? taken : best_taken;
            best = near && distance < best ? distance : best;
        }
        for (uint32_t w = 0; mode != CHECK_TYPO_FUNCTION && place.keywords != 0 && w < BUSTER_ARRAY_LENGTH(check_typo_keywords); w += 1)
        {
            const char* keyword = check_typo_keywords[w];
            const uint32_t keyword_length = (uint32_t)strlen(keyword);
            const uint32_t difference = keyword_length > length ? keyword_length - length : length - keyword_length;
            if (3 * difference <= length && (place.keywords & check_typo_keyword_kinds[w]) != 0 &&
                check_typo_keyword_offered(keyword, parser->language.standard, parser->language.gnu))
            {
                const uint32_t distance = check_typo_distance(typo, vector, (const uint8_t*)keyword, keyword_length, spelling, length);
                const bool near = distance <= bound;
                ties = near && distance == best ? ties + 1 : near && distance < best ? 1 : ties;
                best_keyword = near && distance < best ? w + 1 : near && distance == best ? best_keyword : best_keyword;
                best_at = near && distance < best ? BUSTER_CHECK_NONE : best_at;
                best_taken = near && distance < best ? true : best_taken;
                best = near && distance < best ? distance : best;
            }
        }
        const bool unique = best != UINT32_MAX && ties == 1 && length / best >= 3 && best_taken;
        const bool keyword = unique && best_at == BUSTER_CHECK_NONE;
        const uint8_t source = unique && !keyword ? scope->sources[best_at] : CHECK_TYPO_BINDING;
        const uint32_t event = unique && !keyword && source == CHECK_TYPO_BINDING ? scope->events[best_at] : BUSTER_CHECK_NONE;
        const uint32_t declaration = event != BUSTER_CHECK_NONE ? bindings->declaration[event] : BUSTER_CHECK_NONE;
        const bool external = event != BUSTER_CHECK_NONE && bindings->kind[event] == C_BINDING_FUNCTION && declaration < parser->tokens.count &&
                              reader->declaration_bits != NULL && check_input_linkage(reader->declaration_bits, declaration) == 2;
        const bool predefined = unique && !keyword && source == CHECK_TYPO_PREDECLARED;
        const bool builtin = unique && !keyword && (source == CHECK_TYPO_BUILTIN_USE ||
                             (mode != CHECK_TYPO_FUNCTION && external && check_typo_builtin_name(reader, scope->keys[best_at])));
        const bool type = unique && !keyword && (predefined || (source == CHECK_TYPO_BINDING && bindings->kind[scope->events[best_at]] == C_BINDING_TYPEDEF));
        result = unique && !keyword && !type && !builtin ? scope->events[best_at] : BUSTER_CHECK_NONE;
        typo->suggested_keyword = keyword ? best_keyword : 0;
        typo->suggested_type = predefined || (source == CHECK_TYPO_BUILTIN_USE && mode != CHECK_TYPO_FUNCTION) ? scope->events[best_at] :
                               type || (builtin && mode != CHECK_TYPO_FUNCTION) ? declaration : BUSTER_CHECK_NONE;
        typo->suggested_flags = predefined ? CHECK_TYPO_PREDEFINED : builtin && mode != CHECK_TYPO_FUNCTION ? CHECK_TYPO_BUILTIN : 0;
        typo->suggested_statement = place.statement;
    }
    return result;
}

static bool check_typo_ready(CheckInputReader* reader)
{
    // A reader with the whole unit's state: its preprocessor's names and its arena (a test's reader may hold the
    // bindings alone).
    const bool whole = reader->pp != NULL && reader->arena != NULL && reader->parser->tree.roles != NULL && reader->parser->tree.parents != NULL;
    if (whole && reader->typo == NULL && !reader->output->resource)
    {
        reader->typo = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckTypo, 1);
        reader->output->resource |= reader->typo == NULL;
        if (reader->typo != NULL)
        {
            *reader->typo = (CheckTypo){ 0 };
            reader->typo->suggested_token = BUSTER_CHECK_NONE;
            reader->typo->suggested_type = BUSTER_CHECK_NONE;
        }
    }
    return whole && reader->typo != NULL;
}

// Name corrections already consumed by an earlier recovery read (#2250): their source indices survive token removal
// and insertion. The sorted sparse list needs no per-token state, and each corrected name spends its original search
// even after rereading it as a valid builtin, keyword or type. A correction at this source is not an earlier search.
static uint32_t check_typo_prior_searches(const Parser* parser, uint32_t token)
{
    const uint32_t source = parser->tokens.sources != NULL && token < parser->tokens.count ? parser->tokens.sources[token] : 0;
    uint32_t begin = 0;
    uint32_t end = parser->typo_sources != NULL ? parser->typo_count : 0;
    while (begin < end)
    {
        const uint32_t middle = begin + (end - begin) / 2;
        if (parser->typo_sources[middle] < source)
        {
            begin = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return begin < BUSTER_CHECK_TYPO_LIMIT ? begin : BUSTER_CHECK_TYPO_LIMIT;
}

// The calls before `token`, from the counter's cursor on (each counter's tokens only grow), whose callee is a name of a
// prototyped function given too few arguments or, not variadic, too many: clang's ConvertArgumentsForCall searches for
// another function there. The callees' names are those the answers bound (the uses before the answers' latest).
static uint32_t check_typo_calls_before(const CheckInputReader* reader, CheckTypoCalls* calls, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t end = token < count ? token : count;
    for (uint32_t t = calls->cursor; t < end; t += 1)
    {
        const bool callee = parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && parser->tree.roles[t] == PARSE_ROLE_OPERAND && t + 1 < count &&
                            parser->tree.roles[t + 1] == PARSE_ROLE_OPEN_CALL;
        const uint32_t declaration = callee ? reader->output->declarations[t] : BUSTER_CHECK_NONE;
        // An ordinary implicit declaration is clang's `int()`, with no prototype to count against; a library's has one.
        const uint32_t flags_of = declaration < count && reader->output->function_flags != NULL ? reader->output->function_flags[declaration] : 0;
        const bool unprototyped = (flags_of & CHECK_INPUT_FUNCTION_IMPLICIT) != 0 && (flags_of & CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY) == 0;
        // A function's own declarator, `f(void)`, names itself; so does a library function's first call, a call.
        const bool declarator = declaration == t && (flags_of & CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY) == 0;
        const TypeId function = declaration < count && !unprototyped && !declarator ? type_strip_aligned(reader->types, reader->declared_types[declaration]) : 0;
        if (function != 0 && type_kind(reader->types, function) == BUSTER_TYPE_KIND_FUNCTION)
        {
            const uint8_t flags = reader->types->types.flags[type_index(function)];
            uint32_t parameters = 0;
            type_parameters(reader->types, function, &parameters);
            const uint32_t close = parser->tree.parents[t + 1];
            uint32_t arguments = close > t + 2 && close < count ? 1u : 0u;
            for (uint32_t a = t + 2; a < close && close < count; a += 1)
            {
                const uint8_t kind = parser->tokens.kinds[a];
                const bool nested = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
                arguments += kind == PARSE_KIND_COMMA ? 1u : 0u;
                a = nested && parser->tree.parents[a] > a && parser->tree.parents[a] < close ? parser->tree.parents[a] : a;
            }
            const bool prototype = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
            const bool variadic = (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0;
            calls->count += prototype && (arguments < parameters || (!variadic && arguments > parameters)) ? 1u : 0u;
        }
    }
    calls->cursor = end > calls->cursor ? end : calls->cursor;
    return calls->count;
}

// A value's undeclared name at `token`, its use event `event` (the binding answers', in source order): the declare event
// of the name it corrects to, or BUSTER_CHECK_NONE. A name just after a cast's `)` takes no value, as clang's validator
// there takes a type or a keyword alone (check_typo_place).
static uint32_t check_typo_value(CheckInputReader* reader, uint32_t token, uint32_t event)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const bool roles = parser->tree.roles != NULL && parser->tokens.kinds != NULL;
    const bool operand = roles && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                         parser->tree.roles[token] == PARSE_ROLE_OPERAND;
    uint32_t result = BUSTER_CHECK_NONE;
    if (operand && !reader->typo_disabled && check_typo_ready(reader))
    {
        CheckTypo* typo = reader->typo;
        // The calls' searches before this name.
        while (input->implicit_function_corrections != NULL && typo->function_cursor < input->implicit_function_count &&
               input->implicit_function_tokens[typo->function_cursor] < token)
        {
            typo->function_searches += input->implicit_function_corrections[typo->function_cursor] != BUSTER_CHECK_TYPO_UNSEARCHED ? 1u : 0u;
            typo->function_cursor += 1;
        }
        // And the walk's values' before it (check_typo_walk_value).
        while (typo->walk_value_cursor < typo->walk_value_count && typo->walk_value_tokens[typo->walk_value_cursor] < token)
        {
            typo->walk_value_cursor += 1;
        }
        const uint32_t searches = typo->value_count + typo->function_searches + typo->walk_value_cursor + check_typo_prior_searches(parser, token);
        const bool room = searches < BUSTER_CHECK_TYPO_LIMIT && searches + check_typo_calls_before(reader, &typo->value_calls, token) < BUSTER_CHECK_TYPO_LIMIT;
        if (room)
        {
            typo->value_tokens[typo->value_count++] = token;
        }
        result = room ? check_typo_search(reader, &typo->answers, token, parser->tokens.names[token], event, CHECK_TYPO_VALUE) : BUSTER_CHECK_NONE;
    }
    return result;
}

// A search's suggestion without taking a binding (#1447, #2250), encoded directly for CheckInputReport.detail/subject:
// a keyword index and flags, with a typedef declaration, predefined index or builtin first use plus one (0 for none).
// The name stays undeclared until the driver rereads the correction, where the recovery form calls for it.
static void check_typo_suggestion(const CheckInputReader* reader, uint32_t token, uint32_t* detail, uint32_t* subject)
{
    const bool searched = reader->typo != NULL && reader->typo->suggested_token == token;
    const bool suggested = searched && (reader->typo->suggested_keyword != 0 || reader->typo->suggested_type != BUSTER_CHECK_NONE);
    *detail = (searched ? reader->typo->suggested_keyword | reader->typo->suggested_flags : 0) |
              (suggested && reader->typo->suggested_statement ? CHECK_TYPO_STATEMENT : 0u);
    *subject = searched && reader->typo->suggested_type != BUSTER_CHECK_NONE ? reader->typo->suggested_type + 1 : 0;
}

// An undeclared function's call at `token`, read by the walk before its implicit declaration is bound: the declaration
// token of the function it may have meant, or BUSTER_CHECK_NONE. Past the limit's count of the calls' own searches
// alone none is made; the values' are counted in once they are known (check_typo_finish).
static uint32_t check_typo_function(CheckInputReader* reader, uint32_t token)
{
    uint32_t result = BUSTER_CHECK_NONE;
    if (!reader->typo_disabled && check_typo_ready(reader) &&
        reader->typo->walk_searches + check_typo_prior_searches(reader->parser, token) < BUSTER_CHECK_TYPO_LIMIT)
    {
        reader->typo->walk_searches += 1;
        const uint32_t event = check_typo_search(reader, &reader->typo->walk, token, reader->parser->tokens.names[token], reader->bindings.count, CHECK_TYPO_FUNCTION);
        const uint32_t declaration = event != BUSTER_CHECK_NONE ? reader->bindings.declaration[event] : BUSTER_CHECK_NONE;
        // An implicitly declared function, a library's or another call's, is no suggestion of clang's here, though it is
        // as near as any other (ImplicitlyDefineFunction: "not being a particularly good idea").
        const bool implicit = declaration < reader->parser->tokens.count && reader->output->function_flags != NULL &&
                              (reader->output->function_flags[declaration] & CHECK_INPUT_FUNCTION_IMPLICIT) != 0;
        result = implicit ? BUSTER_CHECK_NONE : declaration;
    }
    return result;
}

// A value's undeclared name at `token` that the walk reads (#1447, check_constant_name): a declarator's constant's, whose
// value or type the declaration needs before the walk goes on, so that it can't wait for the binding answers. The
// declarations visible are the stream's so far, as for an undeclared function's call. The declare event of the name it
// corrects to, or BUSTER_CHECK_NONE. It counts toward the limit with the walk's calls, in the walk's order; a value's
// search that the answers make later in a body before it isn't counted there.
static uint32_t check_typo_walk_value(CheckInputReader* reader, uint32_t token)
{
    uint32_t result = BUSTER_CHECK_NONE;
    if (!reader->typo_disabled && check_typo_ready(reader) &&
        reader->typo->walk_searches + check_typo_prior_searches(reader->parser, token) < BUSTER_CHECK_TYPO_LIMIT)
    {
        CheckTypo* typo = reader->typo;
        typo->walk_searches += 1;
        typo->walk_value_tokens[typo->walk_value_count++] = token;
        result = check_typo_search(reader, &typo->walk, token, reader->parser->tokens.names[token], reader->bindings.count, CHECK_TYPO_VALUE);
    }
    return result;
}

// A parameter's type at `token` that a recovery view reads as `int` (check_prefix_make), whose spelling `name` named no
// type (#1447): clang's parser corrects it to a typedef's name or a type's keyword visible there (ParseImplicitInt's
// DiagnoseUnknownTypeName) and reads the parameter with it. The walk's scope is the declarations so far, as for a
// declarator's constant, and the search counts toward the limit with the walk's values. A suggestion is listed in
// CheckInput.parameter_typo_tokens, in the search's own room.
static void check_typo_parameter(CheckInputReader* reader, uint32_t token, uint32_t name)
{
    if (!reader->typo_disabled && check_typo_ready(reader) &&
        reader->typo->walk_searches + check_typo_prior_searches(reader->parser, token) < BUSTER_CHECK_TYPO_LIMIT &&
        reader->typo->walk_value_count < BUSTER_CHECK_TYPO_LIMIT)
    {
        CheckTypo* typo = reader->typo;
        typo->walk_searches += 1;
        typo->walk_value_tokens[typo->walk_value_count++] = token;
        check_typo_search(reader, &typo->walk, token, name, reader->bindings.count, CHECK_TYPO_TYPE);
        const bool suggested = typo->suggested_token == token && (typo->suggested_keyword != 0 || typo->suggested_type != BUSTER_CHECK_NONE);
        CheckInput* input = reader->output;
        if (suggested && input->parameter_typo_count < BUSTER_CHECK_TYPO_LIMIT)
        {
            const uint32_t at = input->parameter_typo_count;
            input->parameter_typo_tokens = typo->parameter_tokens;
            input->parameter_typo_details = typo->parameter_details;
            input->parameter_typo_types = typo->parameter_types;
            typo->parameter_tokens[at] = token;
            check_typo_suggestion(reader, token, &typo->parameter_details[at], &typo->parameter_types[at]);
            typo->parameter_details[at] = (typo->parameter_details[at] & ~CHECK_TYPO_STATEMENT) | CHECK_TYPO_PARAMETER;
            input->parameter_typo_count = at + 1;
        }
    }
}

// The calls' suggestions past the limit, once the values' searches are known, dropped: a call's search is clang's
// 51st or later where the values', the calls' and the wrong counts' before it number 50, which the merge of the
// source-ordered lists counts.
static void check_typo_finish(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const uint32_t* value_tokens = reader->typo != NULL ? reader->typo->value_tokens : NULL;
    const uint32_t value_count = reader->typo != NULL ? reader->typo->value_count : 0;
    uint32_t searches = 0;
    uint32_t v = 0;
    CheckTypoCalls calls = { 0 };
    for (uint32_t i = 0; reader->typo != NULL && input->implicit_function_corrections != NULL && i < input->implicit_function_count; i += 1)
    {
        const uint32_t token = input->implicit_function_tokens[i];
        while (v < value_count && value_tokens[v] < token)
        {
            searches += 1;
            v += 1;
        }
        const uint32_t correction = input->implicit_function_corrections[i];
        const bool searched = correction != BUSTER_CHECK_TYPO_UNSEARCHED;
        const uint32_t prior = check_typo_prior_searches(reader->parser, token);
        const bool over = searched && (searches + prior >= BUSTER_CHECK_TYPO_LIMIT ||
                                      searches + prior + check_typo_calls_before(reader, &calls, token) >= BUSTER_CHECK_TYPO_LIMIT);
        input->implicit_function_corrections[i] = over ? BUSTER_CHECK_NONE : correction;
        searches += searched ? 1u : 0u;
    }
}

// A use's handle as its answer took it: the batch's, or the declaration its typo corrected to.
static uint32_t check_typo_handle(const CheckInputReader* reader, const uint32_t* resolved, uint32_t event)
{
    uint32_t result = resolved[event];
    for (uint32_t i = 0; result == 0 && reader->typo != NULL && i < reader->typo->corrected_count; i += 1)
    {
        result = reader->typo->corrected_events[i] == event ? reader->typo->corrected_handles[i] : 0;
    }
    return result;
}
