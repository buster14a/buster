// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's loop hints, `#pragma clang loop`, checked as clang 23 checks them, against its answers on probes of each rule.
// clang reads one in three steps, and so does this:
//   - PragmaLoopHintHandler (ParsePragma.cpp) lexes its options: each one of eleven names, with its argument in
//     parentheses, to the `)` that closes them. A name it doesn't know, a missing `(` and an unclosed argument are
//     errors, and a token after the options that isn't another one's name drops the pragma, with a warning: no hint;
//   - HandlePragmaLoopHint reads each argument: a state's one of its words, a count a positive integer constant that
//     fits an int (vectorize_width's also `fixed` or `scalable`, alone or after the count), with a warning for what
//     follows the word or the constant expression it reads, which it ignores. An empty argument is an error, and so is
//     one no expression starts with. A count here is an integer literal, alone or before what an expression can't go on
//     with; another constant expression (an enumerator, `4 + 4`) is left unsupported, not guessed;
//   - Sema (SemaStmtAttr.cpp's handleLoopHintAttr and CheckForIncompatibleAttributes) wants a for, while or do-while
//     loop right after the pragmas, in a function: another statement (a label's too) is an error at its first token,
//     and at file scope clang's error is at the pragma, which it can't parse as a declaration. No hint may come twice in
//     one category, across the pragmas before one loop too, nor a count beside a state that rules it out (unroll's
//     every state, the others' disable).
// Conflicts and placement are available only after the entire statement, including its typed checks. Their reader
// barrier is the statement's exclusive end; their diagnostic spelling is retained separately. No successful input
// keeps hint metadata: Tier 0 has no loop transformation for the hints to steer.
// clang's other loop hints follow the same rules, as hints of the same categories: `#pragma unroll` (a state), with a
// count after it alone or in parentheses, `#pragma nounroll` (disable), `#pragma unroll_and_jam` with a count or none,
// `#pragma nounroll_and_jam` and `#pragma GCC unroll` with a count. Their counts may be 0, which clang takes.

#define BUSTER_CHECK_LOOP_ENABLE 1u
#define BUSTER_CHECK_LOOP_DISABLE 2u
#define BUSTER_CHECK_LOOP_ASSUME_SAFETY 4u
#define BUSTER_CHECK_LOOP_FULL 8u
// A pragma-handler error can be emitted while lexing the lookahead after the previous statement, before its Sema.
#define BUSTER_CHECK_LOOP_HANDLER_ERROR 4u

struct CheckInputLoopDiagnostic
{
    uint32_t position;
    uint32_t statement;
    uint32_t source;
    uint32_t pragma;
    CheckInputMessage message;
    char text[192];
};

typedef struct CheckLoopHint CheckLoopHint;
struct CheckLoopHint
{
    uint32_t source;
    uint32_t value;
    uint8_t option;
    uint8_t state;
    uint8_t pragma_kind;
    bool scalable;
};

typedef enum CheckLoopPragma
{
    CHECK_LOOP_PRAGMA_CLANG,
    CHECK_LOOP_PRAGMA_UNROLL,
    CHECK_LOOP_PRAGMA_NOUNROLL,
    CHECK_LOOP_PRAGMA_JAM,
    CHECK_LOOP_PRAGMA_NOJAM,
} CheckLoopPragma;

static const char* const check_loop_pragma_names[] =
{
    "clang loop",
    "unroll",
    "nounroll",
    "unroll_and_jam",
    "nounroll_and_jam",
};

typedef enum CheckLoopCategory
{
    CHECK_LOOP_VECTORIZE,
    CHECK_LOOP_INTERLEAVE,
    CHECK_LOOP_UNROLL,
    CHECK_LOOP_PIPELINE,
    CHECK_LOOP_VECTORIZE_PREDICATE,
    CHECK_LOOP_DISTRIBUTE,
    CHECK_LOOP_UNROLL_AND_JAM,
    CHECK_LOOP_LICM,
    CHECK_LOOP_CATEGORIES,
} CheckLoopCategory;

typedef struct CheckLoopOption CheckLoopOption;
struct CheckLoopOption
{
    const char* name;
    CheckLoopCategory category;
    // The words a state takes (BUSTER_CHECK_LOOP_*), or 0 for a count.
    uint8_t states;
};

static const CheckLoopOption check_loop_options[] =
{
    { "vectorize", CHECK_LOOP_VECTORIZE, BUSTER_CHECK_LOOP_ENABLE | BUSTER_CHECK_LOOP_DISABLE | BUSTER_CHECK_LOOP_ASSUME_SAFETY },
    { "vectorize_width", CHECK_LOOP_VECTORIZE, 0 },
    { "interleave", CHECK_LOOP_INTERLEAVE, BUSTER_CHECK_LOOP_ENABLE | BUSTER_CHECK_LOOP_DISABLE | BUSTER_CHECK_LOOP_ASSUME_SAFETY },
    { "interleave_count", CHECK_LOOP_INTERLEAVE, 0 },
    { "unroll", CHECK_LOOP_UNROLL, BUSTER_CHECK_LOOP_ENABLE | BUSTER_CHECK_LOOP_DISABLE | BUSTER_CHECK_LOOP_FULL },
    { "unroll_count", CHECK_LOOP_UNROLL, 0 },
    { "pipeline", CHECK_LOOP_PIPELINE, BUSTER_CHECK_LOOP_DISABLE },
    { "pipeline_initiation_interval", CHECK_LOOP_PIPELINE, 0 },
    { "vectorize_predicate", CHECK_LOOP_VECTORIZE_PREDICATE, BUSTER_CHECK_LOOP_ENABLE | BUSTER_CHECK_LOOP_DISABLE | BUSTER_CHECK_LOOP_ASSUME_SAFETY },
    { "distribute", CHECK_LOOP_DISTRIBUTE, BUSTER_CHECK_LOOP_ENABLE | BUSTER_CHECK_LOOP_DISABLE },
    { "licm", CHECK_LOOP_LICM, BUSTER_CHECK_LOOP_DISABLE },
};

// The hints of the pragmas before one loop so far: each category's state word (0 for none) and whether it has a count.
typedef struct CheckLoopHints CheckLoopHints;
struct CheckLoopHints
{
    uint8_t states[CHECK_LOOP_CATEGORIES];
    bool counts[CHECK_LOOP_CATEGORIES];
    CheckLoopHint state_hints[CHECK_LOOP_CATEGORIES];
    CheckLoopHint count_hints[CHECK_LOOP_CATEGORIES];
    CheckInputLoopDiagnostic conflict;
};

// A count's literal at `source`: 1 for a valid one, 0 for one this can't read (not a literal), 3 for clang's error. A
// `#pragma clang loop` count must be positive (`zero` says whether 0 is one).
static uint8_t check_input_loop_count(CheckInputReader* reader, uint32_t source, bool zero, uint32_t* value)
{
    const PPTokens* output = &reader->pp->output;
    const uint8_t kind = output->kinds[source];
    uint8_t result = 0;
    CheckLiteral literal = { 0 };
    if (kind == BUSTER_PP_KIND_INTEGER)
    {
        literal.integer = output->values[source];
        literal.valid = true;
        result = 1;
    }
    else if (kind == C_TOKEN_NUMERIC_CONSTANT)
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
        result = 1;
    }
    if (result == 1 && (!literal.valid || literal.real || literal.imaginary || (literal.integer == 0 && !zero) || literal.integer > INT32_MAX))
    {
        result = 3;
    }
    *value = result == 1 ? (uint32_t)literal.integer : 0;
    return result;
}

// Whether the statement at parser token `token` is a for, while or do-while loop: a do-while's `while` isn't, whose
// condition's `)` hangs off the statement's `;`.
static bool check_input_loop_follows(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    const uint8_t kind = token < count ? parser->tokens.kinds[token] : 0;
    const uint32_t close = kind == PARSE_KIND_WHILE ? parser->tree.parents[token] : count;
    const uint32_t owner = close < count ? parser->tree.parents[close] : count;
    return kind == PARSE_KIND_FOR || kind == PARSE_KIND_DO ||
           (kind == PARSE_KIND_WHILE && close < count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_CONDITION &&
            (owner >= count || parser->tree.roles[owner] != PARSE_ROLE_TERMINATOR));
}

// Whether a keyword (its CName) can begin an expression, as a count's first token: sizeof, the alignofs, _Generic,
// _Countof, true, false, nullptr, the function names, __extension__, __real and __imag, and the builtins clang spells
// as keywords. A statement's, a declaration's or a type's keyword can't (`#pragma unroll return`, `unroll int`), which
// clang's ParseConstantExpression diagnoses as an expected expression.
static bool check_input_loop_keyword_starts(uint32_t name)
{
    bool result = false;
    switch (name)
    {
    case C_NAME_SIZEOF:
    case C_NAME_ALIGNOF_C11:
    case C_NAME_ALIGNOF:
    case C_NAME_GNU_ALIGNOF:
    case C_NAME_GNU_ALIGNOF2:
    case C_NAME_GENERIC:
    case C_NAME_COUNTOF:
    case C_NAME_TRUE:
    case C_NAME_FALSE:
    case C_NAME_NULLPTR:
    case C_NAME_GNU_NULLPTR:
    case C_NAME_FUNC:
    case C_NAME_FUNCTION:
    case C_NAME_PRETTY_FUNCTION:
    case C_NAME_EXTENSION:
    case C_NAME_GNU_REAL:
    case C_NAME_GNU_REAL2:
    case C_NAME_GNU_IMAG:
    case C_NAME_GNU_IMAG2:
    case C_NAME_BUILTIN_OFFSETOF:
    case C_NAME_BUILTIN_VA_ARG:
    case C_NAME_BUILTIN_TYPES_COMPATIBLE_P:
    case C_NAME_BUILTIN_CHOOSE_EXPR:
    case C_NAME_BUILTIN_CONVERTVECTOR:
    case C_NAME_BUILTIN_BIT_CAST:
    case C_NAME_BUILTIN_COLUMN:
    case C_NAME_BUILTIN_FILE:
    case C_NAME_BUILTIN_FILE_NAME:
    case C_NAME_BUILTIN_FUNCTION:
    case C_NAME_BUILTIN_LINE:
    case C_NAME_BUILTIN_VECTORELEMENTS:
        result = true;
        break;
    default:
        break;
    }
    return result;
}

// Whether a token of kind `kind` can begin an expression (an identifier, an expression's keyword, a constant, a
// literal, a parenthesis or a unary operator, && a label's address), and whether one can go on an expression after an
// operand (a binary or postfix operator, ?, a subscript, a call, a member).
static bool check_input_loop_starts(uint8_t kind)
{
    const bool keyword = kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT &&
                         check_input_loop_keyword_starts((uint32_t)kind - BUSTER_PP_KIND_KEYWORD + 1);
    const bool result = kind == C_TOKEN_RAW_IDENTIFIER || kind == BUSTER_PP_KIND_INTEGER || keyword ||
                        (kind >= C_TOKEN_NUMERIC_CONSTANT && kind <= C_TOKEN_UTF32_STRING_LITERAL) || kind == C_TOKEN_L_PAREN ||
                        kind == C_TOKEN_AMP || kind == C_TOKEN_AMPAMP || kind == C_TOKEN_STAR || kind == C_TOKEN_PLUS || kind == C_TOKEN_PLUSPLUS ||
                        kind == C_TOKEN_MINUS || kind == C_TOKEN_MINUSMINUS || kind == C_TOKEN_TILDE || kind == C_TOKEN_EXCLAIM;
    return result;
}

static bool check_input_loop_continues(uint8_t kind)
{
    const bool result = kind == C_TOKEN_L_SQUARE || kind == C_TOKEN_L_PAREN || kind == C_TOKEN_PERIOD || kind == C_TOKEN_ARROW ||
                        kind == C_TOKEN_AMP || kind == C_TOKEN_AMPAMP || kind == C_TOKEN_STAR || kind == C_TOKEN_PLUS || kind == C_TOKEN_PLUSPLUS ||
                        kind == C_TOKEN_MINUS || kind == C_TOKEN_MINUSMINUS || kind == C_TOKEN_EXCLAIMEQUAL || kind == C_TOKEN_SLASH ||
                        kind == C_TOKEN_PERCENT || kind == C_TOKEN_LESS || kind == C_TOKEN_LESSLESS || kind == C_TOKEN_LESSEQUAL ||
                        kind == C_TOKEN_GREATER || kind == C_TOKEN_GREATERGREATER || kind == C_TOKEN_GREATEREQUAL || kind == C_TOKEN_CARET ||
                        kind == C_TOKEN_PIPE || kind == C_TOKEN_PIPEPIPE || kind == C_TOKEN_QUESTION || kind == C_TOKEN_EQUALEQUAL;
    return result;
}

// The option the identifier at `token` names: its index in check_loop_options, or the table's length for none.
static uint32_t check_input_loop_option(CheckInputReader* reader, uint32_t token, uint32_t end)
{
    uint32_t option = 0;
    while (option < BUSTER_ARRAY_LENGTH(check_loop_options) && !pp_token_is_name(reader->pp, &reader->pp->output, token, end, check_loop_options[option].name))
    {
        option += 1;
    }
    return option;
}

// The `)` that closes an argument starting at `first`, as clang's ParseLoopHintValue finds it: the first that leaves no
// parenthesis open, or `end` (the pragma's PRAGMA_END) for none.
static uint32_t check_input_loop_close(const PPTokens* output, uint32_t first, uint32_t end)
{
    uint32_t close = end;
    uint32_t depth = 1;
    for (uint32_t t = first; close == end && t < end; t += 1)
    {
        depth += output->kinds[t] == C_TOKEN_L_PAREN;
        depth -= output->kinds[t] == C_TOKEN_R_PAREN;
        close = depth == 0 ? t : end;
    }
    return close;
}

// The state word the token at `token` is (BUSTER_CHECK_LOOP_*), or 0 for none.
static uint8_t check_input_loop_word(CheckInputReader* reader, uint32_t token, uint32_t end)
{
    const PPTokens* output = &reader->pp->output;
    return pp_token_is_name(reader->pp, output, token, end, "enable") ? BUSTER_CHECK_LOOP_ENABLE :
           pp_token_is_name(reader->pp, output, token, end, "disable") ? BUSTER_CHECK_LOOP_DISABLE :
           pp_token_is_name(reader->pp, output, token, end, "assume_safety") ? BUSTER_CHECK_LOOP_ASSUME_SAFETY :
           pp_token_is_name(reader->pp, output, token, end, "full") ? BUSTER_CHECK_LOOP_FULL : 0;
}

// Whether the token at `token` is vectorize_width's `fixed` or `scalable`.
static bool check_input_loop_scalable(CheckInputReader* reader, uint32_t token, uint32_t end)
{
    const PPTokens* output = &reader->pp->output;
    return pp_token_is_name(reader->pp, output, token, end, "fixed") || pp_token_is_name(reader->pp, output, token, end, "scalable");
}

// A count's argument, from `first` to `last` (not empty), as clang's ParseConstantExpression reads it, which leaves what
// follows the expression it reads to a warning: one that an integer literal starts, alone or before what an expression
// can't go on with (`4)`, `4 x`, `4, x`), is the literal's, 1 if it is valid (`zero` says whether 0 is) and 3 for clang's
// error otherwise; one that no expression starts with is clang's error (3, with its message in `*message`); and any
// other is a constant this doesn't evaluate (0: `4 + 4`, `-1`, an enumerator).
static uint8_t check_input_loop_value(CheckInputReader* reader, uint32_t first, uint32_t last, bool zero, uint8_t* message, uint32_t* value)
{
    const PPTokens* output = &reader->pp->output;
    const uint8_t kind = output->kinds[first];
    const bool literal = kind == C_TOKEN_NUMERIC_CONSTANT || kind == BUSTER_PP_KIND_INTEGER;
    uint8_t result = 0;
    if (literal && (first + 1 == last || !check_input_loop_continues(output->kinds[first + 1])))
    {
        result = check_input_loop_count(reader, first, zero, value);
    }
    else if (!check_input_loop_starts(kind))
    {
        result = 3;
        *message = CHECK_INPUT_LOOP_EXPRESSION;
    }
    return result;
}

// Whether the loop's hints of `category` have a count beside a state that rules it out: unroll's and unroll_and_jam's
// every state, and the others' disable.
static bool check_input_loop_incompatible(const CheckLoopHints* hints, CheckLoopCategory category)
{
    const uint8_t state = hints->states[category];
    return hints->counts[category] && state != 0 &&
           (category == CHECK_LOOP_UNROLL || category == CHECK_LOOP_UNROLL_AND_JAM || state == BUSTER_CHECK_LOOP_DISABLE);
}

// Only cold diagnostics need a spelling. The option/value records are bounded, original metadata, not source text
// kept for a successful loop. Clang prints evaluated counts and normalizes GCC unroll to #pragma unroll.
static void check_input_loop_describe(const CheckLoopHint* hint, char* text, uint32_t capacity)
{
    const char* word = hint->state == BUSTER_CHECK_LOOP_ENABLE ? "enable" : hint->state == BUSTER_CHECK_LOOP_DISABLE ? "disable" :
                       hint->state == BUSTER_CHECK_LOOP_FULL ? "full" : "assume_safety";
    if (hint->pragma_kind != CHECK_LOOP_PRAGMA_CLANG)
    {
        const char* name = check_loop_pragma_names[hint->pragma_kind];
        if (hint->state != 0)
        {
            snprintf(text, capacity, "#pragma %s", name);
        }
        else
        {
            snprintf(text, capacity, "#pragma %s(%u)", name, hint->value);
        }
    }
    else if (hint->state != 0)
    {
        snprintf(text, capacity, "%s(%s)", check_loop_options[hint->option].name, word);
    }
    else if (hint->option == 1 && hint->value == 0)
    {
        snprintf(text, capacity, "vectorize_width(%s)", hint->scalable ? "scalable" : "fixed");
    }
    else
    {
        snprintf(text, capacity, "%s(%u%s)", check_loop_options[hint->option].name, hint->value, hint->scalable ? ", scalable" : "");
    }
}

static void check_input_loop_conflict(CheckLoopHints* hints, const CheckInputLoopDiagnostic* at, const CheckLoopHint* first, const CheckLoopHint* second, bool duplicate)
{
    if (hints->conflict.pragma == 0 && first->source != 0 && second->source != 0)
    {
        char a[64];
        char b[64];
        check_input_loop_describe(first, a, sizeof(a));
        check_input_loop_describe(second, b, sizeof(b));
        hints->conflict = *at;
        snprintf(hints->conflict.text, sizeof(hints->conflict.text), "%s directives '%s' and '%s'", duplicate ? "duplicate" : "incompatible", a, b);
    }
}

static void check_input_loop_add(CheckLoopHints* hints, CheckLoopCategory category, CheckLoopHint hint, const CheckInputLoopDiagnostic* at)
{
    CheckLoopHint* previous = hint.state != 0 ? hints->state_hints + category : hints->count_hints + category;
    check_input_loop_conflict(hints, at, previous, &hint, true);
    *previous = hint;
    if (hint.state != 0)
    {
        hints->states[category] = hint.state;
    }
    else
    {
        hints->counts[category] = true;
    }
    if (check_input_loop_incompatible(hints, category))
    {
        check_input_loop_conflict(hints, at, hints->state_hints + category, hints->count_hints + category, false);
    }
}

// One `#pragma clang loop`'s options, from `cursor` to its end (`end`, its PRAGMA_END), into the loop's hints, in clang's
// steps (at the top of this file): 2 for valid hints, 1 for a pragma clang drops, 3 for an argument error, 4 for a
// pragma-handler error, and otherwise 0
// for what this leaves unsupported. The first error's message, where clang has one of its own for it (an unclosed or
// empty argument, one no expression starts with), goes in `*message`, which stays as it was otherwise.
static uint8_t check_input_loop_options(CheckInputReader* reader, uint32_t cursor, uint32_t end, CheckLoopHints* hints, uint8_t* message, const CheckInputLoopDiagnostic* at)
{
    const PPTokens* output = &reader->pp->output;
    // PragmaLoopHintHandler: the options' names and their arguments' parentheses.
    uint8_t result = cursor < end && output->kinds[cursor] == C_TOKEN_RAW_IDENTIFIER ? 2 : 3;
    uint32_t next = cursor;
    while (result == 2 && next < end && output->kinds[next] == C_TOKEN_RAW_IDENTIFIER)
    {
        const bool known = check_input_loop_option(reader, next, end) < BUSTER_ARRAY_LENGTH(check_loop_options);
        const bool open = known && next + 1 < end && output->kinds[next + 1] == C_TOKEN_L_PAREN;
        const uint32_t close = open ? check_input_loop_close(output, next + 2, end) : end;
        result = close < end ? 2 : 3;
        *message = open && close == end ? CHECK_INPUT_LOOP_PARENTHESIS : *message;
        next = close + 1;
    }
    result = result == 2 && next < end ? 1 : result;
    const bool handler_error = result == 3;
    // Parse every argument before publishing a Sema conflict: even a later argument error precedes the statement.
    next = cursor;
    while (result != 1 && result != 3 && next < end)
    {
        const uint32_t index = check_input_loop_option(reader, next, end);
        const CheckLoopOption* option = &check_loop_options[index];
        const CheckLoopCategory category = option->category;
        const uint32_t first = next + 2;
        const uint32_t close = check_input_loop_close(output, first, end);
        uint8_t hint = 2;
        CheckLoopHint record = { .source = at->source, .option = (uint8_t)index };
        if (first == close)
        {
            hint = 3;
            *message = CHECK_INPUT_LOOP_MISSING;
        }
        else if (option->states != 0)
        {
            const uint8_t word = check_input_loop_word(reader, first, end);
            hint = (word & option->states) != 0 ? 2 : 3;
            record.state = word;
        }
        else
        {
            // vectorize_width's `fixed` or `scalable`, alone (what follows it ignored) or after the count.
            const bool width = category == CHECK_LOOP_VECTORIZE;
            const bool alone = width && check_input_loop_scalable(reader, first, end);
            hint = alone ? 1 : check_input_loop_value(reader, first, close, false, message, &record.value);
            const bool comma = !alone && hint == 1 && width && first + 1 < close && output->kinds[first + 1] == C_TOKEN_COMMA;
            hint = comma && !(first + 2 < close && check_input_loop_scalable(reader, first + 2, end)) ? 3 : hint;
            record.scalable = (alone || comma) && pp_token_is_name(reader->pp, output, alone ? first : first + 2, end, "scalable");
            hint = hint == 1 ? 2 : hint;
        }
        if (hint == 2)
        {
            check_input_loop_add(hints, category, record, at);
        }
        result = hint == 3 || hint == 0 ? hint : result;
        next = close + 1;
    }
    return handler_error ? BUSTER_CHECK_LOOP_HANDLER_ERROR : result;
}

// One of the other loop hints, from `cursor` (after its name) to its end: a state of `category` (`state`, 0 for a count
// alone) and, where it is `counted` (unroll, unroll_and_jam and GCC unroll, which is unroll), an optional count, as
// clang's PragmaUnrollHintHandler and ParseLoopHintValue read it: in parentheses, to the one that closes them, or the
// rest of the line. clang drops a pragma with tokens it takes no count from (nounroll's, and those after a count's
// parentheses), with a warning: no hint. An unclosed or empty count is an error (`(4`, `()`), and the count is read as
// check_input_loop_value reads one, 0 among the valid ones (`#pragma unroll`'s 0 and 1 are clang's disable, a state,
// which conflicts with what a count does). Returns as check_input_loop_options does.
static uint8_t check_input_loop_unroll(CheckInputReader* reader, uint32_t cursor, uint32_t end, CheckLoopCategory category, uint8_t state, bool counted,
                                       CheckLoopHints* hints, uint8_t* message, const CheckInputLoopDiagnostic* at, CheckLoopPragma pragma_kind)
{
    const PPTokens* output = &reader->pp->output;
    const bool parenthesized = counted && cursor < end && output->kinds[cursor] == C_TOKEN_L_PAREN;
    const uint32_t close = parenthesized ? check_input_loop_close(output, cursor + 1, end) : end;
    const uint32_t first = parenthesized ? cursor + 1 : cursor;
    const bool dropped = cursor < end && (!counted || (parenthesized && close + 1 < end));
    uint8_t result = 1;
    CheckLoopHint record = { .source = at->source, .pragma_kind = (uint8_t)pragma_kind };
    if (!dropped && parenthesized && close == end)
    {
        result = BUSTER_CHECK_LOOP_HANDLER_ERROR;
        *message = CHECK_INPUT_LOOP_PARENTHESIS;
    }
    else if (!dropped && cursor < end && first == close)
    {
        result = 3;
        *message = CHECK_INPUT_LOOP_MISSING;
    }
    else if (!dropped && cursor < end)
    {
        result = check_input_loop_value(reader, first, close, true, message, &record.value);
        result = result == 1 ? 2 : result;
        // Clang represents #pragma unroll 0 and 1 as disable, in the state category, even though they had a count.
        record.state = pragma_kind == CHECK_LOOP_PRAGMA_UNROLL && record.value <= 1 ? BUSTER_CHECK_LOOP_DISABLE : 0;
    }
    else if (!dropped)
    {
        result = 2;
        record.state = state;
    }
    if (result == 2)
    {
        check_input_loop_add(hints, category, record, at);
    }
    return result;
}

// The reader already computed subtree ends in postorder. Find the annotated statement's root through parent links,
// never by rescanning its body. A do's root is its final semicolon, so its trailing condition is checked too.
static uint32_t check_input_loop_statement_end(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t root = token;
    while (root < count)
    {
        const uint8_t role = parser->tree.roles[root];
        if (role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR ||
            role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE)
        {
            break;
        }
        root = parser->tree.parents[root];
    }
    const bool closing = token < count && (parser->tree.roles[token] == PARSE_ROLE_CLOSE_BLOCK || parser->tree.roles[token] == PARSE_ROLE_CLOSE_BODY);
    return !closing && root < count ? reader->ends[root] + 1 : token;
}

// Earlier completion wins. Nested statements can end together; the inner one's attributes are processed first.
// A trailing hint before a closing brace consumes no statement and must not displace the preceding completion.
static void check_input_loop_first(CheckInputLoopDiagnostic* first, const CheckInputLoopDiagnostic* next)
{
    if (next->pragma != 0 && (first->pragma == 0 || next->position < first->position ||
        (next->position == first->position && next->statement > first->statement && next->statement < next->position)))
    {
        *first = *next;
    }
}

// Each loop hint's handled state (2 for valid hints, 1 for a pragma clang drops, 3 for an argument error, 4 for a
// pragma-handler error, 0 for
// unsupported), and an argument error's message in `messages`. The caller zeroes both arrays and `delayed`; the
// earliest completed statement's placement/conflict goes in `delayed`. A dropped pragma has no hints.
// In a function's body, clang checks each pragma's count expressions before it reads the next
// pragma, and drops a hint whose count it rejects (not an integer constant expression, `int N` or `4 / 0`, or a value
// out of range, an enumerator of 0); it places the hints, and checks them against each other, only after the statement
// they precede. So a count this doesn't evaluate (`unroll_count(N)`) leaves its pragma unsupported, whatever the later
// pragmas before the same loop or its place have: clang's first error is that count's when it is invalid, and theirs
// only when it is valid. At file scope clang reads no count, and the pragma is an error whatever its count. Whether
// there was memory enough.
static bool check_input_loop_pragmas(CheckInputReader* reader, uint8_t* handled, uint8_t* messages, CheckInputLoopDiagnostic* delayed)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    uint32_t run = BUSTER_CHECK_NONE;
    uint32_t scanned = 0;
    uint32_t bodies = 0;
    uint32_t statement_end = 0;
    CheckLoopHints hints = { 0 };
    for (uint32_t pragma = 0; pragma < parser->tokens.pragma_count && !reader->output->resource; pragma += 1)
    {
        uint32_t source = 0;
        uint32_t end = 0;
        uint32_t token = 0;
        const bool range = check_input_retained_pragma(reader, pragma, &previous_end, &previous_token, &source, &end, &token);
        const PPTokens* output = &reader->pp->output;
        const bool clang = range && pp_token_is_name(reader->pp, output, source + 2, end, "clang") && pp_token_is_name(reader->pp, output, source + 3, end, "loop");
        const bool gcc = range && pp_token_is_name(reader->pp, output, source + 2, end, "GCC") && pp_token_is_name(reader->pp, output, source + 3, end, "unroll");
        const bool unroll = range && pp_token_is_name(reader->pp, output, source + 2, end, "unroll");
        const bool nounroll = range && pp_token_is_name(reader->pp, output, source + 2, end, "nounroll");
        const bool jam = range && pp_token_is_name(reader->pp, output, source + 2, end, "unroll_and_jam");
        const bool nojam = range && pp_token_is_name(reader->pp, output, source + 2, end, "nounroll_and_jam");
        if (clang || gcc || unroll || nounroll || jam || nojam)
        {
            if (token != run)
            {
                hints = (CheckLoopHints){ 0 };
                run = token;
                statement_end = check_input_loop_statement_end(reader, token);
            }
            // Whether the pragma is in a function's body: the bodies opened and not closed before it.
            while (scanned < token && scanned < count)
            {
                bodies += parser->tree.roles[scanned] == PARSE_ROLE_OPEN_BODY;
                bodies -= parser->tree.roles[scanned] == PARSE_ROLE_CLOSE_BODY && bodies != 0;
                scanned += 1;
            }
            const CheckLoopCategory category = jam || nojam ? CHECK_LOOP_UNROLL_AND_JAM : CHECK_LOOP_UNROLL;
            const uint8_t word = nounroll || nojam ? BUSTER_CHECK_LOOP_DISABLE : BUSTER_CHECK_LOOP_ENABLE;
            const CheckLoopPragma pragma_kind = clang ? CHECK_LOOP_PRAGMA_CLANG : nounroll ? CHECK_LOOP_PRAGMA_NOUNROLL :
                                                jam ? CHECK_LOOP_PRAGMA_JAM : nojam ? CHECK_LOOP_PRAGMA_NOJAM : CHECK_LOOP_PRAGMA_UNROLL;
            CheckInputLoopDiagnostic at =
            {
                .position = statement_end,
                .statement = token,
                .source = source + (clang ? 3 : gcc ? 3 : 2),
                .pragma = pragma + 1,
                .message = CHECK_INPUT_LOOP_HINT,
            };
            uint8_t message = CHECK_INPUT_LOOP_HINT;
            uint8_t state = clang ? check_input_loop_options(reader, source + 4, end, &hints, &message, &at) :
                            check_input_loop_unroll(reader, source + (gcc ? 4 : 3), end, category, word, unroll || jam || gcc, &hints, &message, &at, pragma_kind);
            messages[pragma] = state >= 3 ? message : 0;
            if ((state == 2 || state == 0) && bodies == 0)
            {
                state = 3;
                messages[pragma] = CHECK_INPUT_LOOP_HINT;
            }
            const bool active = reader->output->deferred_tokens == 0 || reader->deferred != 0 || reader->output->deferred_tokens[token] == 0;
            if (state == 2 && bodies != 0 && active)
            {
                if (!check_input_loop_follows(parser, token))
                {
                    at.source = token < count ? parser->tokens.sources[token] : reader->pp->output.count;
                    at.message = CHECK_INPUT_LOOP_PLACEMENT;
                    snprintf(at.text, sizeof(at.text), "expected a for, while, or do-while loop to follow '#pragma %s'", check_loop_pragma_names[pragma_kind]);
                    check_input_loop_first(delayed, &at);
                }
                else
                {
                    check_input_loop_first(delayed, &hints.conflict);
                }
            }
            handled[pragma] = state;
        }
    }
    return !reader->output->resource;
}
