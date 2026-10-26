// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's loop hints, `#pragma clang loop`, checked as clang 23 checks them (ParsePragma.cpp's HandlePragmaLoopHint, and
// SemaStmtAttr.cpp's handleLoopHintAttr and CheckForIncompatibleAttributes), against its answers on probes of each rule:
//   - each option one of ten, with its argument in parentheses: a state's one of its words, a count a positive integer
//     constant that fits an int (vectorize_width's also `fixed` or `scalable`, alone or after the count). A count here is
//     an integer literal; another constant expression (an enumerator, `4 + 4`) is left unsupported, not guessed;
//   - no hint twice in one category, across the pragmas before one loop too: two states or two counts are duplicates;
//   - no count beside a state that rules it out: unroll's every state, and the others' disable;
//   - a for, while or do-while loop right after the pragmas, in a function: another statement (a label's too) is an
//     error at its first token. At file scope clang's error is at the pragma, which it can't parse as a declaration.
// Tokens after the last option, which clang warns about and ignores, are ignored. Each error is reported on the
// pragma's line (a conflict on the later pragma's), but a misplaced pragma's at the statement after it. Tier 0 has no
// loop transformation for the hints to steer, so nothing keeps them.
// clang's other loop hints follow the same rules, as hints of the same categories: `#pragma unroll` (a state), with a
// count after it alone or in parentheses, `#pragma nounroll` (disable), `#pragma unroll_and_jam` with a count or none,
// `#pragma nounroll_and_jam` and `#pragma GCC unroll` with a count. Their counts may be 0, which clang takes.

#define BUSTER_CHECK_LOOP_ENABLE 1u
#define BUSTER_CHECK_LOOP_DISABLE 2u
#define BUSTER_CHECK_LOOP_ASSUME_SAFETY 4u
#define BUSTER_CHECK_LOOP_FULL 8u

typedef enum CheckLoopCategory
{
    CHECK_LOOP_VECTORIZE,
    CHECK_LOOP_INTERLEAVE,
    CHECK_LOOP_UNROLL,
    CHECK_LOOP_PIPELINE,
    CHECK_LOOP_VECTORIZE_PREDICATE,
    CHECK_LOOP_DISTRIBUTE,
    CHECK_LOOP_UNROLL_AND_JAM,
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
    { "vectorize_predicate", CHECK_LOOP_VECTORIZE_PREDICATE, BUSTER_CHECK_LOOP_ENABLE | BUSTER_CHECK_LOOP_DISABLE },
    { "distribute", CHECK_LOOP_DISTRIBUTE, BUSTER_CHECK_LOOP_ENABLE | BUSTER_CHECK_LOOP_DISABLE },
};

// The hints of the pragmas before one loop so far: each category's state word (0 for none) and whether it has a count.
typedef struct CheckLoopHints CheckLoopHints;
struct CheckLoopHints
{
    uint8_t states[CHECK_LOOP_CATEGORIES];
    bool counts[CHECK_LOOP_CATEGORIES];
};

// A count's literal at `source`: 1 for a valid one, 0 for one this can't read (not a literal), 3 for clang's error. A
// `#pragma clang loop` count must be positive (`zero` says whether 0 is one).
static uint8_t check_input_loop_count(CheckInputReader* reader, uint32_t source, bool zero)
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
            literal = check_literal_number(bytes, length, reader->scratch);
            reader->output->resource |= literal.resource;
        }
        reader->scratch->length = mark;
        result = 1;
    }
    if (result == 1 && (!literal.valid || literal.real || literal.imaginary || (literal.integer == 0 && !zero) || literal.integer > INT32_MAX))
    {
        result = 3;
    }
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

// One pragma's options, from `cursor` to its end (`end`, its PRAGMA_END), into the loop's hints: 2 if they're valid, 3
// for clang's error, 0 for what this leaves unsupported.
static uint8_t check_input_loop_options(CheckInputReader* reader, uint32_t cursor, uint32_t end, CheckLoopHints* hints)
{
    const PPTokens* output = &reader->pp->output;
    uint8_t result = cursor < end && output->kinds[cursor] == C_TOKEN_RAW_IDENTIFIER ? 2 : 3;
    while (result == 2 && cursor < end && output->kinds[cursor] == C_TOKEN_RAW_IDENTIFIER)
    {
        uint32_t option = 0;
        while (option < BUSTER_ARRAY_LENGTH(check_loop_options) && !pp_token_is_name(reader->pp, output, cursor, end, check_loop_options[option].name))
        {
            option += 1;
        }
        const bool known = option < BUSTER_ARRAY_LENGTH(check_loop_options);
        const uint32_t argument = cursor + 2;
        result = known && argument < end && output->kinds[cursor + 1] == C_TOKEN_L_PAREN ? 2 : 3;
        uint32_t close = argument;
        if (result == 2 && check_loop_options[option].states != 0)
        {
            uint8_t word = 0;
            word = pp_token_is_name(reader->pp, output, argument, end, "enable") ? BUSTER_CHECK_LOOP_ENABLE :
                   pp_token_is_name(reader->pp, output, argument, end, "disable") ? BUSTER_CHECK_LOOP_DISABLE :
                   pp_token_is_name(reader->pp, output, argument, end, "assume_safety") ? BUSTER_CHECK_LOOP_ASSUME_SAFETY :
                   pp_token_is_name(reader->pp, output, argument, end, "full") ? BUSTER_CHECK_LOOP_FULL : 0;
            const CheckLoopCategory category = check_loop_options[option].category;
            result = (word & check_loop_options[option].states) != 0 && hints->states[category] == 0 ? 2 : 3;
            hints->states[category] = word;
            close = argument + 1;
        }
        else if (result == 2)
        {
            // vectorize_width's `fixed` or `scalable`, alone or after its count.
            const bool width = check_loop_options[option].category == CHECK_LOOP_VECTORIZE;
            const bool scalable = width && (pp_token_is_name(reader->pp, output, argument, end, "fixed") || pp_token_is_name(reader->pp, output, argument, end, "scalable"));
            result = scalable ? 2 : argument < end ? check_input_loop_count(reader, argument, false) : 3;
            result = result == 1 ? 2 : result;
            close = argument + 1;
            if (result == 2 && !scalable && width && close + 1 < end && output->kinds[close] == C_TOKEN_COMMA)
            {
                result = pp_token_is_name(reader->pp, output, close + 1, end, "fixed") || pp_token_is_name(reader->pp, output, close + 1, end, "scalable") ? 2 : 3;
                close += 2;
            }
            else if (result == 2 && !scalable && close < end && output->kinds[close] != C_TOKEN_R_PAREN)
            {
                // A literal that an expression goes on from (`4 + 4`): a constant this doesn't evaluate.
                result = 0;
            }
            const CheckLoopCategory category = check_loop_options[option].category;
            result = result == 2 && hints->counts[category] ? 3 : result;
            hints->counts[category] = true;
        }
        if (result == 2)
        {
            result = close < end && output->kinds[close] == C_TOKEN_R_PAREN ? 2 : 3;
            cursor = close + 1;
            const CheckLoopCategory category = check_loop_options[option].category;
            const uint8_t state = hints->states[category];
            const bool incompatible = hints->counts[category] && state != 0 &&
                                      (category == CHECK_LOOP_UNROLL || category == CHECK_LOOP_UNROLL_AND_JAM || state == BUSTER_CHECK_LOOP_DISABLE);
            result = incompatible ? 3 : result;
        }
    }
    return result;
}

// One of the other loop hints, from `cursor` (after its name) to its end: a state of `category` (`state`, 0 for a count
// alone) and an optional count, alone or in parentheses (`counted`: whether it may have one; GCC unroll's must). Returns
// as check_input_loop_options does.
static uint8_t check_input_loop_unroll(CheckInputReader* reader, uint32_t cursor, uint32_t end, CheckLoopCategory category, uint8_t state, bool counted,
                                       bool required, CheckLoopHints* hints)
{
    const PPTokens* output = &reader->pp->output;
    const bool parenthesized = counted && cursor + 2 < end && output->kinds[cursor] == C_TOKEN_L_PAREN;
    const uint32_t literal = parenthesized ? cursor + 1 : cursor;
    const bool present = counted && literal < end && (output->kinds[literal] == C_TOKEN_NUMERIC_CONSTANT || output->kinds[literal] == BUSTER_PP_KIND_INTEGER);
    uint8_t result = present ? check_input_loop_count(reader, literal, true) : (counted && literal < end) || required ? 0 : 1;
    result = result == 1 && present && parenthesized && output->kinds[literal + 1] != C_TOKEN_R_PAREN ? 0 : result;
    result = result == 1 ? 2 : result;
    if (result == 2 && present)
    {
        result = hints->counts[category] ? 3 : 2;
        hints->counts[category] = true;
    }
    else if (result == 2)
    {
        result = hints->states[category] != 0 ? 3 : 2;
        hints->states[category] = state;
    }
    const bool incompatible = hints->counts[category] && hints->states[category] != 0 &&
                              (category == CHECK_LOOP_UNROLL || category == CHECK_LOOP_UNROLL_AND_JAM || hints->states[category] == BUSTER_CHECK_LOOP_DISABLE);
    return result == 2 && incompatible ? 3 : result;
}

// Each `#pragma clang loop`'s handled state (2 for valid hints, 3 for clang's error, 0 for unsupported), and for an error
// its message and the token it is reported at (BUSTER_CHECK_NONE for the pragma's own place), in `messages` and
// `tokens`, which the caller zeroes and fills with BUSTER_CHECK_NONE. Whether there was memory enough.
static bool check_input_loop_pragmas(CheckInputReader* reader, uint8_t* handled, uint8_t* messages, uint32_t* tokens)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    uint32_t run = BUSTER_CHECK_NONE;
    uint32_t scanned = 0;
    uint32_t bodies = 0;
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
            uint8_t state = clang ? check_input_loop_options(reader, source + 4, end, &hints) :
                            check_input_loop_unroll(reader, source + (gcc ? 4 : 3), end, category, word, unroll || jam || gcc, gcc, &hints);
            messages[pragma] = state == 3 ? CHECK_INPUT_LOOP_HINT : 0;
            if (state == 2 && (bodies == 0 || !check_input_loop_follows(parser, token)))
            {
                state = 3;
                messages[pragma] = bodies == 0 ? CHECK_INPUT_LOOP_HINT : CHECK_INPUT_LOOP_PLACEMENT;
                tokens[pragma] = bodies == 0 ? BUSTER_CHECK_NONE : token;
            }
            handled[pragma] = state;
        }
    }
    return !reader->output->resource;
}
