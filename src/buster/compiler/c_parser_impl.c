// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C parser's scalar and vector implementations, tables and command. c_parser.c supplies the shared types and
// readers, then includes this file in the frontend unit and standalone parser consumers. Keep the private tables
// and functions in their original order so that the home retains its initialization and emission order.

static const char* const parse_kind_names[PARSE_KIND_COUNT] =
{
#define BUSTER_PARSE_KIND_NAME(name) #name,
    BUSTER_PARSE_KINDS(BUSTER_PARSE_KIND_NAME)
#undef BUSTER_PARSE_KIND_NAME
};

static const ParseKeywordKind parse_keyword_kinds[] =
{
#define BUSTER_PARSE_KEYWORD_KIND(name, kind) { C_NAME_##name, PARSE_KIND_##kind },
    BUSTER_PARSE_KEYWORD_KINDS(BUSTER_PARSE_KEYWORD_KIND)
#undef BUSTER_PARSE_KEYWORD_KIND
};

_Static_assert(sizeof(parse_keyword_kinds) / sizeof(parse_keyword_kinds[0]) == C_NAME_KEYWORD_COUNT, "every keyword needs a parser kind");

// The parser kind of each kind of the preprocessor's output (a C_TOKEN_*, a keyword's BUSTER_PP_KIND_KEYWORD + id - 1,
// or BUSTER_PP_KIND_PRAGMA_END), for parse_tokens_from_output's lookup: UNKNOWN for what C doesn't use after
// preprocessing (`#`, `##`, and C++'s `.*`, `->*` and `<=>`). parse_kind_table_initialize fills it.
static BUSTER_BAKED_FRONTEND uint8_t parse_kind_table[256] __attribute__((aligned(64)));

static void parse_kind_table_initialize(void)
{
    static const uint8_t lexer_kinds[C_TOKEN_KIND_COUNT] =
    {
        [C_TOKEN_UNKNOWN] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_RAW_IDENTIFIER] = PARSE_KIND_IDENTIFIER,
        [C_TOKEN_NUMERIC_CONSTANT] = PARSE_KIND_NUMBER,
        [C_TOKEN_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_WIDE_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_UTF8_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_UTF16_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_UTF32_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_WIDE_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_UTF8_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_UTF16_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_UTF32_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_L_SQUARE] = PARSE_KIND_L_SQUARE,
        [C_TOKEN_R_SQUARE] = PARSE_KIND_R_SQUARE,
        [C_TOKEN_L_PAREN] = PARSE_KIND_L_PAREN,
        [C_TOKEN_R_PAREN] = PARSE_KIND_R_PAREN,
        [C_TOKEN_L_BRACE] = PARSE_KIND_L_BRACE,
        [C_TOKEN_R_BRACE] = PARSE_KIND_R_BRACE,
        [C_TOKEN_PERIOD] = PARSE_KIND_PERIOD,
        [C_TOKEN_ELLIPSIS] = PARSE_KIND_ELLIPSIS,
        [C_TOKEN_AMP] = PARSE_KIND_AMP,
        [C_TOKEN_AMPAMP] = PARSE_KIND_AMPAMP,
        [C_TOKEN_AMPEQUAL] = PARSE_KIND_AMPEQUAL,
        [C_TOKEN_STAR] = PARSE_KIND_STAR,
        [C_TOKEN_STAREQUAL] = PARSE_KIND_STAREQUAL,
        [C_TOKEN_PLUS] = PARSE_KIND_PLUS,
        [C_TOKEN_PLUSPLUS] = PARSE_KIND_PLUSPLUS,
        [C_TOKEN_PLUSEQUAL] = PARSE_KIND_PLUSEQUAL,
        [C_TOKEN_MINUS] = PARSE_KIND_MINUS,
        [C_TOKEN_ARROW] = PARSE_KIND_ARROW,
        [C_TOKEN_MINUSMINUS] = PARSE_KIND_MINUSMINUS,
        [C_TOKEN_MINUSEQUAL] = PARSE_KIND_MINUSEQUAL,
        [C_TOKEN_TILDE] = PARSE_KIND_TILDE,
        [C_TOKEN_EXCLAIM] = PARSE_KIND_EXCLAIM,
        [C_TOKEN_EXCLAIMEQUAL] = PARSE_KIND_EXCLAIMEQUAL,
        [C_TOKEN_SLASH] = PARSE_KIND_SLASH,
        [C_TOKEN_SLASHEQUAL] = PARSE_KIND_SLASHEQUAL,
        [C_TOKEN_PERCENT] = PARSE_KIND_PERCENT,
        [C_TOKEN_PERCENTEQUAL] = PARSE_KIND_PERCENTEQUAL,
        [C_TOKEN_LESS] = PARSE_KIND_LESS,
        [C_TOKEN_LESSLESS] = PARSE_KIND_LESSLESS,
        [C_TOKEN_LESSEQUAL] = PARSE_KIND_LESSEQUAL,
        [C_TOKEN_LESSLESSEQUAL] = PARSE_KIND_LESSLESSEQUAL,
        [C_TOKEN_GREATER] = PARSE_KIND_GREATER,
        [C_TOKEN_GREATERGREATER] = PARSE_KIND_GREATERGREATER,
        [C_TOKEN_GREATEREQUAL] = PARSE_KIND_GREATEREQUAL,
        [C_TOKEN_GREATERGREATEREQUAL] = PARSE_KIND_GREATERGREATEREQUAL,
        [C_TOKEN_CARET] = PARSE_KIND_CARET,
        [C_TOKEN_CARETEQUAL] = PARSE_KIND_CARETEQUAL,
        [C_TOKEN_PIPE] = PARSE_KIND_PIPE,
        [C_TOKEN_PIPEPIPE] = PARSE_KIND_PIPEPIPE,
        [C_TOKEN_PIPEEQUAL] = PARSE_KIND_PIPEEQUAL,
        [C_TOKEN_QUESTION] = PARSE_KIND_QUESTION,
        [C_TOKEN_COLON] = PARSE_KIND_COLON,
        // Split into two colons by parse_tokens_from_output.
        [C_TOKEN_COLONCOLON] = PARSE_KIND_COLON,
        [C_TOKEN_SEMI] = PARSE_KIND_SEMI,
        [C_TOKEN_EQUAL] = PARSE_KIND_EQUAL,
        [C_TOKEN_EQUALEQUAL] = PARSE_KIND_EQUALEQUAL,
        [C_TOKEN_COMMA] = PARSE_KIND_COMMA,
        [C_TOKEN_HASH] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_HASHHASH] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_PERIODSTAR] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_ARROWSTAR] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_SPACESHIP] = PARSE_KIND_UNKNOWN,
    };
    memset(parse_kind_table, PARSE_KIND_UNKNOWN, sizeof(parse_kind_table));
    memcpy(parse_kind_table, lexer_kinds, sizeof(lexer_kinds));
    // An embedded resource remains one operand. The initializer cursor consumes its byte span without expanding it.
    parse_kind_table[BUSTER_PP_KIND_EMBED] = PARSE_KIND_NUMBER;
    for (uint32_t i = 0; i < sizeof(parse_keyword_kinds) / sizeof(parse_keyword_kinds[0]); i += 1)
    {
        parse_kind_table[BUSTER_PP_KIND_KEYWORD + parse_keyword_kinds[i].name - 1] = parse_keyword_kinds[i].kind;
    }
}

// Marks the tokens a `#pragma pack` (or its _Pragma) comes just before with BUSTER_PARSE_AFTER_PRAGMA_PACK, which the
// conversion leaves clear: the pragmas are few, and a declaration's specifiers are where the scalar parser reads it.
static void parse_mark_pragma_packs(const Preprocessor* pp, ParseTokens* tokens)
{
    for (uint32_t k = 0; k < tokens->pragma_count; k += 1)
    {
        const uint32_t hash = tokens->pragma_sources[k];
        const uint32_t position = tokens->pragma_positions[k];
        uint32_t end = hash;
        if (parse_pragma_at(&pp->output, hash, &end) && pp_token_is_name(pp, &pp->output, hash + 2, end, "pack") && position < tokens->count)
        {
            tokens->flags[position] |= BUSTER_PARSE_AFTER_PRAGMA_PACK;
        }
    }
}

// One token of the output at `i` into the parser's tokens at `*written`, or a whole pragma from its '#'. Returns the
// index of the output token after it.
static uint32_t parse_convert_token(const PPTokens* output, uint32_t i, ParseTokens* tokens, uint32_t* written)
{
    const uint8_t kind = output->kinds[i];
    uint32_t end = i;
    uint32_t result = i + 1;
    if (kind == C_TOKEN_HASH && (output->flags[i] & BUSTER_PP_START_OF_LINE) != 0 && parse_pragma_at(output, i, &end))
    {
        tokens->pragma_sources[tokens->pragma_count] = i;
        tokens->pragma_positions[tokens->pragma_count] = *written;
        tokens->pragma_count += 1;
        result = end + 1;
    }
    else
    {
        const uint32_t w = *written;
        tokens->kinds[w] = parse_kind_table[kind];
        tokens->names[w] = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END) ? output->values[i] : 0;
        tokens->sources[w] = i;
        tokens->flags[w] = output->flags[i];
        // C's `::` (C23's attribute scopes, and two colons of an asm statement) parses as two colons.
        const uint32_t halves = kind == C_TOKEN_COLONCOLON ? 2 : 1;
        tokens->kinds[w + 1] = halves == 2 ? PARSE_KIND_COLON : tokens->kinds[w + 1];
        tokens->names[w + 1] = halves == 2 ? 0 : tokens->names[w + 1];
        tokens->sources[w + 1] = halves == 2 ? i : tokens->sources[w + 1];
        tokens->flags[w + 1] = halves == 2 ? BUSTER_PARSE_COLONCOLON_SECOND : tokens->flags[w + 1];
        *written = w + halves;
    }
    return result;
}

// Fills the padding around the tokens.
static void parse_tokens_pad(ParseTokens* tokens)
{
    for (uint32_t i = 0; i < BUSTER_PARSE_TOKEN_PADDING; i += 1)
    {
        tokens->kinds[tokens->count + i] = PARSE_KIND_END;
        tokens->names[tokens->count + i] = 0;
        tokens->sources[tokens->count + i] = 0;
        tokens->flags[tokens->count + i] = 0;
        tokens->flags[-1 - (int64_t)i] = 0;
        tokens->kinds[-1 - (int64_t)i] = PARSE_KIND_END;
        tokens->names[-1 - (int64_t)i] = 0;
        tokens->sources[-1 - (int64_t)i] = 0;
    }
}

// The scalar twin of parse_tokens_from_output: token by token.
static void parse_tokens_from_output_scalar(const PPTokens* output, ParseTokens* tokens)
{
    uint32_t written = 0;
    tokens->pragma_count = 0;
    uint32_t i = 0;
    while (i < output->count)
    {
        i = parse_convert_token(output, i, tokens, &written);
    }
    tokens->count = written;
    parse_tokens_pad(tokens);
}

// The output's tokens as the parser's: kinds through parse_kind_table, names for identifiers and each token's index in
// the output, 64 tokens at a time. A run of tokens without a '#', a PRAGMA_END or a `::` is stored as it is, with one
// table lookup (two vpermi2b for the table's halves, picked by the kind's bit 7) and masked stores; each of those
// three goes through parse_convert_token. `tokens` has room for twice the output's tokens (every one a `::`) and its
// pragmas, with the padding on both sides.
static void parse_tokens_from_output(const PPTokens* output, ParseTokens* tokens)
{
#if !BUSTER_HOST_VECTOR
    parse_tokens_from_output_scalar(output, tokens);
#else
    const __m512i table0 = _mm512_load_si512(parse_kind_table);
    const __m512i table1 = _mm512_load_si512(parse_kind_table + 64);
    const __m512i table2 = _mm512_load_si512(parse_kind_table + 128);
    const __m512i table3 = _mm512_load_si512(parse_kind_table + 192);
    const __m512i lanes = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    uint32_t written = 0;
    tokens->pragma_count = 0;
    uint32_t i = 0;
    while (i < output->count)
    {
        const uint32_t left = output->count - i;
        const __mmask64 live = left >= 64 ? ~(__mmask64)0 : _bzhi_u64(~0ull, left);
        const __m512i kinds = _mm512_maskz_loadu_epi8(live, output->kinds + i);
        const uint64_t special = _mm512_mask_cmpeq_epi8_mask(live, kinds, _mm512_set1_epi8(C_TOKEN_HASH)) |
                                 _mm512_mask_cmpeq_epi8_mask(live, kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_PRAGMA_END)) |
                                 _mm512_mask_cmpeq_epi8_mask(live, kinds, _mm512_set1_epi8(C_TOKEN_COLONCOLON));
        // The run before the first special token, or the whole block.
        const uint32_t run = special != 0 ? (uint32_t)__builtin_ctzll(special) : (left >= 64 ? 64 : left);
        const __mmask64 stored = _bzhi_u64(~0ull, run);
        const __m512i low = _mm512_permutex2var_epi8(table0, kinds, table1);
        const __m512i high = _mm512_permutex2var_epi8(table2, kinds, table3);
        const __m512i parse = _mm512_mask_blend_epi8(_mm512_movepi8_mask(kinds), low, high);
        _mm512_mask_storeu_epi8(tokens->kinds + written, stored, parse);
        _mm512_mask_storeu_epi8(tokens->flags + written, stored, _mm512_maskz_loadu_epi8(live, output->flags + i));
        // Identifiers' and keywords' values are their IdentIds.
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(stored, kinds, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER)) |
                                     (_mm512_mask_cmpge_epu8_mask(stored, kinds, _mm512_set1_epi8(BUSTER_PP_KIND_KEYWORD)) &
                                      _mm512_mask_cmplt_epu8_mask(stored, kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_PRAGMA_END)));
        for (uint32_t group = 0; group < 4; group += 1)
        {
            const __mmask16 in_run = (__mmask16)(stored >> (16 * group));
            const __m512i names = _mm512_maskz_loadu_epi32((__mmask16)(identifiers >> (16 * group)), output->values + i + 16 * group);
            _mm512_mask_storeu_epi32(tokens->names + written + 16 * group, in_run, names);
            _mm512_mask_storeu_epi32(tokens->sources + written + 16 * group, in_run, _mm512_add_epi32(lanes, _mm512_set1_epi32((int)(i + 16 * group))));
        }
        written += run;
        i += run;
        if (special != 0)
        {
            i = parse_convert_token(output, i, tokens, &written);
        }
    }
    tokens->count = written;
    parse_tokens_pad(tokens);
#endif
}

static const char* const parse_role_names[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_NAME(name, level, compare, rule) #name,
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_NAME)
#undef BUSTER_PARSE_ROLE_NAME
};

static const uint8_t parse_role_levels[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_LEVEL(name, level, compare, rule) PARSE_LEVEL_##level,
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_LEVEL)
#undef BUSTER_PARSE_ROLE_LEVEL
};

static const uint8_t parse_role_left_thresholds[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_LEFT(name, level, compare, rule) BUSTER_PARSE_LEFT_THRESHOLD_##compare(PARSE_LEVEL_##level),
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_LEFT)
#undef BUSTER_PARSE_ROLE_LEFT
};

static const uint8_t parse_role_right_thresholds[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_RIGHT(name, level, compare, rule) BUSTER_PARSE_RIGHT_THRESHOLD_##compare(PARSE_LEVEL_##level),
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_RIGHT)
#undef BUSTER_PARSE_ROLE_RIGHT
};

// Which way equal keys go, by level: true for left. Only levels whose operators can be both L and R of a token tie.
static const bool parse_level_ties_left[BUSTER_PARSE_LEVELS] =
{
    [PARSE_LEVEL_ASM_COLON] = true,
    [PARSE_LEVEL_SEPARATOR] = true,
    [PARSE_LEVEL_ASSOCIATION] = true,
    [PARSE_LEVEL_COMMA] = true,
    [PARSE_LEVEL_BITFIELD] = true,
    [PARSE_LEVEL_RANGE] = true,
    [PARSE_LEVEL_LOGICAL_OR] = true,
    [PARSE_LEVEL_LOGICAL_AND] = true,
    [PARSE_LEVEL_BITWISE_OR] = true,
    [PARSE_LEVEL_BITWISE_XOR] = true,
    [PARSE_LEVEL_BITWISE_AND] = true,
    [PARSE_LEVEL_EQUALITY] = true,
    [PARSE_LEVEL_RELATIONAL] = true,
    [PARSE_LEVEL_SHIFT] = true,
    [PARSE_LEVEL_ADDITIVE] = true,
    [PARSE_LEVEL_MULTIPLICATIVE] = true,
    [PARSE_LEVEL_POSTFIX] = true,
};

// Whether an L candidate of this level loses to any R: openers, terminators and heads, whose tokens to their right
// never belong to them through the levels (the expression in a group belongs to its closer, a statement's tokens to
// its terminator, and `i++` in `for (...; ...; i++)` to the `)`).
BUSTER_INLINE bool parse_level_is_weak(uint8_t level)
{
    return level == PARSE_LEVEL_OPENER || level == PARSE_LEVEL_TERMINATOR || level == PARSE_LEVEL_HEAD;
}

// The parent from L and R and their keys (depth * BUSTER_PARSE_LEVELS + level) under PARSE_RULE_SELECT: equal keys are on
// the same level, which says which way they go.
BUSTER_INLINE uint32_t parse_select_keys(uint32_t left, uint32_t left_key, uint32_t right, uint32_t right_key)
{
    uint32_t result;
    const uint8_t left_level = (uint8_t)(left_key % BUSTER_PARSE_LEVELS);
    if (left == BUSTER_PARSE_NONE || (right != BUSTER_PARSE_NONE && parse_level_is_weak(left_level)))
    {
        result = right;
    }
    else if (right == BUSTER_PARSE_NONE)
    {
        result = left;
    }
    else
    {
        const bool take_left = left_key > right_key || (left_key == right_key && parse_level_ties_left[left_level]);
        result = take_left ? left : right;
    }
    return result;
}

// The same from depths and levels.
BUSTER_INLINE uint32_t parse_select(uint32_t left, uint32_t left_depth, uint8_t left_level, uint32_t right, uint32_t right_depth, uint8_t right_level)
{
    uint32_t result;
    if (left == BUSTER_PARSE_NONE || (right != BUSTER_PARSE_NONE && parse_level_is_weak(left_level)))
    {
        result = right;
    }
    else if (right == BUSTER_PARSE_NONE)
    {
        result = left;
    }
    else
    {
        const uint64_t left_key = (uint64_t)left_depth * BUSTER_PARSE_LEVELS + left_level;
        const uint64_t right_key = (uint64_t)right_depth * BUSTER_PARSE_LEVELS + right_level;
        const bool take_left = left_key > right_key || (left_key == right_key && parse_level_ties_left[left_level]);
        result = take_left ? left : right;
    }
    return result;
}


// The messages of a missing `;`, which clang gives at the token instead when it is a `:` or a `,` (its "common typo").
BUSTER_INLINE bool parse_message_expects_semi(uint8_t message)
{
    return message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_EXPRESSION || message == PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TOP_LEVEL_DECLARATOR || message == PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION_LIST ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_RETURN || message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_BREAK ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_CONTINUE || message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_GOTO ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_DO_WHILE || message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_ASM ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TOP_LEVEL_ASM || message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_STATIC_ASSERT ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_IN_FOR;
}

// The first error of the current top-level declaration. An expected closer's at a token has clang's note "to match this
// '('" at its opener (#147, BalancedDelimiterTracker's): the innermost open frame's of the closer's kind.
BUSTER_INLINE void parse_scalar_error_argument(ParseScalar* parser, uint32_t token, uint8_t message, uint8_t place, uint32_t argument)
{
    if (parser->error_token == BUSTER_PARSE_NONE)
    {
        const uint8_t kind = parser->kinds[token];
        place = parse_message_expects_semi(message) && (kind == PARSE_KIND_COLON || kind == PARSE_KIND_COMMA) ? BUSTER_PARSE_AT_TOKEN : place;
        parser->error_token = token;
        parser->error_message = message;
        parser->error_place = place;
        parser->error_argument = argument;
        const uint8_t opener_kind = place != BUSTER_PARSE_AT_TOKEN ? PARSE_KIND_END : message == PARSE_MESSAGE_EXPECTED_R_PAREN ? PARSE_KIND_L_PAREN :
                                    message == PARSE_MESSAGE_EXPECTED_R_SQUARE ? PARSE_KIND_L_SQUARE : message == PARSE_MESSAGE_EXPECTED_R_BRACE ? PARSE_KIND_L_BRACE :
                                                                                                                                             PARSE_KIND_END;
        uint32_t note = BUSTER_PARSE_NONE;
        for (uint32_t f = parser->frame_count; opener_kind != PARSE_KIND_END && note == BUSTER_PARSE_NONE && f > 1; f -= 1)
        {
            const uint32_t opener = parser->frames[f - 1].opener;
            note = opener < parser->count && parser->kinds[opener] == opener_kind ? opener : BUSTER_PARSE_NONE;
        }
        parser->error_note = note;
    }
}

BUSTER_INLINE void parse_scalar_error(ParseScalar* parser, uint32_t token, uint8_t message, uint8_t place)
{
    parse_scalar_error_argument(parser, token, message, place, BUSTER_PARSE_NONE);
}

// Whether a function's body is due at token `i`, as clang asks for one: after the first declarator of a declaration at
// file scope when it declares a function, and its attributes, unless an asm label came after it (clang reads what
// follows one as the declaration's rest).
static bool parse_scalar_body_expected(const ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    bool result = frame->role == PARSE_ROLE_NONE && frame->mode == PARSE_MODE_AFTER_DECLARATOR && frame->derivation == PARSE_DERIVATION_FUNCTION &&
                  (frame->flags & BUSTER_PARSE_FRAME_LATER_DECLARATOR) == 0;
    // The declarator's attributes and asm label, back from the token before: each closer's group, then its keyword.
    int64_t before = (int64_t)i - 1;
    while (result && before > 0 && parser->roles[before] == PARSE_ROLE_CLOSE_ATTRIBUTE && parser->kinds[before] == PARSE_KIND_R_PAREN)
    {
        uint32_t open = 0;
        do
        {
            open += parser->kinds[before] == PARSE_KIND_R_PAREN;
            open -= parser->kinds[before] == PARSE_KIND_L_PAREN;
            before -= 1;
        } while (before > 0 && open != 0);
        result = parser->kinds[before] != PARSE_KIND_ASM;
        before -= 1;
    }
    return result;
}

// How many arguments of a keyword's group clang expects a `,` after (its ExpectAndConsume, after the token before):
// _Static_assert's and _Generic's first, and those of the builtins it reads apart (#1127).
static uint16_t parse_scalar_keyword_commas(const ParseScalar* parser, const ParseFrame* frame)
{
    const IdentId name = frame->keyword == PARSE_KIND_TYPE_ARGUMENT ? parser->names[(int64_t)frame->opener - 1] : 0;
    const bool one = frame->keyword == PARSE_KIND_STATIC_ASSERT || frame->keyword == PARSE_KIND_GENERIC || name == C_NAME_BUILTIN_OFFSETOF ||
                     name == C_NAME_BUILTIN_VA_ARG || name == C_NAME_BUILTIN_BIT_CAST || name == C_NAME_BUILTIN_CONVERTVECTOR;
    return name == C_NAME_BUILTIN_CHOOSE_EXPR ? 2 : one ? 1 : 0;
}

// The missing closer of a group `opener_kind` opens, at token `i`: `expected ')'` at the token, but where clang closes the
// group with its balanced delimiters (`balanced`: every group but a few builtins'), a `;` just before the closer is
// unexpected there instead (#1127).
static void parse_scalar_close_expected(ParseScalar* parser, uint32_t i, uint8_t opener_kind, bool balanced)
{
    const uint8_t closer = opener_kind == PARSE_KIND_L_SQUARE ? PARSE_KIND_R_SQUARE : opener_kind == PARSE_KIND_L_BRACE ? PARSE_KIND_R_BRACE : PARSE_KIND_R_PAREN;
    const uint8_t message = closer == PARSE_KIND_R_SQUARE ? PARSE_MESSAGE_EXPECTED_R_SQUARE : closer == PARSE_KIND_R_BRACE ? PARSE_MESSAGE_EXPECTED_R_BRACE : PARSE_MESSAGE_EXPECTED_R_PAREN;
    if (balanced && parser->kinds[i] == PARSE_KIND_SEMI && parser->kinds[i + 1] == closer)
    {
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_UNEXPECTED_SEMI_BEFORE, BUSTER_PARSE_AT_TOKEN, i + 1);
    }
    else
    {
        // The builtins clang reads with an argument list of its own expect their `)` without a balanced tracker: no note.
        const bool first = parser->error_token == BUSTER_PARSE_NONE;
        parse_scalar_error(parser, i, message, BUSTER_PARSE_AT_TOKEN);
        parser->error_note = first && !balanced ? BUSTER_PARSE_NONE : parser->error_note;
    }
}

// A diagnostic that doesn't stop the parse: a warning, or an error after which clang parses on as if it hadn't been one
// (a missing type specifier, which C99 and later make an error, and an unknown type name, which it takes for a type).
// `argument` is the token the message names, or BUSTER_PARSE_NONE.
static void parse_scalar_note(ParseScalar* parser, uint32_t token, uint8_t message, uint32_t argument, bool error)
{
    if (parser->diagnostic_count < parser->diagnostic_capacity)
    {
        parser->diagnostics[parser->diagnostic_count] = (ParseDiagnostic){
            .token = token,
            .argument = argument,
            .message = message,
            .place = BUSTER_PARSE_AT_TOKEN,
            .error = error,
            .note = BUSTER_PARSE_NONE,
        };
        parser->diagnostic_count += 1;
    }
    parser->errors += error;
}

BUSTER_INLINE bool parse_scalar_is_typedef_name(const ParseScalar* parser, uint32_t token)
{
    return parser->kinds[token] == PARSE_KIND_IDENTIFIER && parser->meanings[parser->names[token]] == PARSE_MEANING_TYPEDEF;
}

// Whether the token can start a type name: a specifier or qualifier keyword, an attribute or a typedef name.
BUSTER_INLINE bool parse_scalar_starts_type(const ParseScalar* parser, uint32_t token)
{
    const uint8_t kind = parser->kinds[token];
    return kind == PARSE_KIND_TYPE || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_ATOMIC ||
           kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATTRIBUTE || parse_scalar_is_typedef_name(parser, token);
}

// Whether the token can start a declaration: a type name, or a storage class, typedef, _Alignas, _Static_assert,
// __label__ or __extension__ before one. In a block, a typedef name followed by `:` is a label instead.
BUSTER_INLINE bool parse_scalar_starts_declaration(const ParseScalar* parser, uint32_t token, bool block)
{
    uint32_t i = token;
    while (parser->kinds[i] == PARSE_KIND_EXTENSION)
    {
        i += 1;
    }
    const uint8_t kind = parser->kinds[i];
    const bool label = block && parser->kinds[i] == PARSE_KIND_IDENTIFIER && parser->kinds[i + 1] == PARSE_KIND_COLON;
    return !label && (parse_scalar_starts_type(parser, i) || kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_TYPEDEF || kind == PARSE_KIND_ALIGNAS ||
                      kind == PARSE_KIND_STATIC_ASSERT || kind == PARSE_KIND_CALLING_CONVENTION);
}

// Whether token `i` is what clang's isDeclarationSpecifier takes for one, in C: a type name's start, a storage class, a
// function specifier, typedef, an alignment specifier or a calling convention; not _Static_assert, __extension__ or a
// C23 attribute list. Clang's ParseDeclGroup reads one where a declaration's `;` is missing as the next declaration's
// (#147).
BUSTER_INLINE bool parse_scalar_declaration_specifier(const ParseScalar* parser, uint32_t i)
{
    const uint8_t kind = parser->kinds[i];
    return parse_scalar_starts_type(parser, i) || kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_TYPEDEF || kind == PARSE_KIND_ALIGNAS ||
           kind == PARSE_KIND_CALLING_CONVENTION;
}

// Whether clang's recovery reads past a declaration's missing `;` at token `i` (ParseDiagnostic.resumed): a declaration
// specifier there, which a `#pragma pack` before it doesn't come between, as its annotation token would (#147). The
// checker's view leaves the other pragmas clang's parser takes as tokens (check_prefix_resume).
BUSTER_INLINE bool parse_scalar_resumes(const ParseScalar* parser, uint32_t i)
{
    return (parser->flags[i] & BUSTER_PARSE_AFTER_PRAGMA_PACK) == 0 && parse_scalar_declaration_specifier(parser, i);
}

// Whether `i` is a keyword after a parameter's declarator that has no name, which clang takes for the name it would be,
// as it parses the declarator.
BUSTER_INLINE bool parse_scalar_keyword_parameter(const ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    const uint8_t kind = parser->kinds[i];
    return frame->role == PARSE_ROLE_OPEN_FUNCTION && frame->mode == PARSE_MODE_AFTER_DECLARATOR && frame->derivation == PARSE_DERIVATION_NONE &&
           kind >= PARSE_KIND_TYPE && kind < PARSE_KIND_COUNT;
}

// Whether clang asks for a function's body at token `i`, which can't continue the declarator: where one is due, the end
// of the file too, unless a declaration specifier there starts the next declaration after a missing `;`
// (ParseDeclGroup).
static bool parse_scalar_fn_body_due(const ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    const uint8_t kind = parser->kinds[i];
    return parse_scalar_body_expected(parser, i, frame) && (kind == PARSE_KIND_EXTENSION || !parse_scalar_starts_declaration(parser, i, false));
}

// Whether `[` at `token` starts a C23 attribute: `[[`, which C has nowhere else (a subscript or a designator can't
// start with `[`).
BUSTER_INLINE bool parse_scalar_standard_attribute(const ParseScalar* parser, uint32_t token)
{
    return parser->kinds[token] == PARSE_KIND_L_SQUARE && parser->kinds[token + 1] == PARSE_KIND_L_SQUARE;
}

// The token after a block item's attribute specifiers from `token`, C23's ([[ and its brackets, balanced) and GNU's
// (__attribute__ and its parentheses, balanced), or the count: what decides whether C23 attributes there start a statement
// (parse_scalar_keyword_statement). A lookahead over the attributes alone, which a block item has few of.
static uint32_t parse_scalar_after_attributes(const ParseScalar* parser, uint32_t token)
{
    uint32_t at = token;
    bool more = true;
    while (more && at + 1 < parser->count)
    {
        const bool standard = parse_scalar_standard_attribute(parser, at);
        more = standard || parser->kinds[at] == PARSE_KIND_ATTRIBUTE;
        if (more)
        {
            const uint8_t open = standard ? PARSE_KIND_L_SQUARE : PARSE_KIND_L_PAREN;
            const uint8_t close = standard ? PARSE_KIND_R_SQUARE : PARSE_KIND_R_PAREN;
            uint32_t depth = 0;
            uint32_t k = standard ? at : at + 1;
            bool inside = true;
            while (inside && k < parser->count)
            {
                depth += parser->kinds[k] == open ? 1 : 0;
                depth -= parser->kinds[k] == close && depth != 0 ? 1 : 0;
                k += 1;
                inside = depth != 0;
            }
            at = k;
        }
    }
    return at;
}

// Whether `token` is a statement's keyword (or a compound statement's brace), after which C23 attributes at a block item
// are the statement's, as clang reads them, rather than a declaration's. A null statement's `;` isn't one: C23 attributes
// before it read as a declaration's, which the checker takes as the null statement's (fallthrough's).
static bool parse_scalar_keyword_statement(const ParseScalar* parser, uint32_t token)
{
    const uint8_t kind = token < parser->count ? parser->kinds[token] : PARSE_KIND_SEMI;
    return kind == PARSE_KIND_RETURN || kind == PARSE_KIND_IF || kind == PARSE_KIND_SWITCH || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_DO ||
           kind == PARSE_KIND_FOR || kind == PARSE_KIND_GOTO || kind == PARSE_KIND_CONTINUE || kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CASE ||
           kind == PARSE_KIND_DEFAULT || kind == PARSE_KIND_L_BRACE;
}

// Whether `token`, after a block item's C23 attributes, starts the statement they are of rather than a declaration, as
// clang reads them: a statement's keyword, or what starts no declaration (clang's isDeclarationStatement), but the null
// statement's `;` (above): an expression statement's first token, a label's name, asm, or a block's `}`, where clang
// expects the statement (parse_scalar_block_attribute_end).
static bool parse_scalar_attributed_statement(const ParseScalar* parser, uint32_t token)
{
    const uint8_t kind = token < parser->count ? parser->kinds[token] : PARSE_KIND_SEMI;
    return parse_scalar_keyword_statement(parser, token) || (kind != PARSE_KIND_SEMI && !parse_scalar_starts_declaration(parser, token, true));
}

BUSTER_INLINE void parse_scalar_bind_name(ParseScalar* parser, uint32_t name, uint8_t meaning)
{
    parser->log_names[parser->log_count] = name;
    parser->log_meanings[parser->log_count] = parser->meanings[name];
    parser->log_count += 1;
    parser->meanings[name] = meaning;
}

BUSTER_INLINE void parse_scalar_bind(ParseScalar* parser, uint32_t token, uint8_t meaning, CBindingKind kind)
{
    parse_binding_trace_declare(parser, token, C_BINDING_ORDINARY, kind, false);
    parse_scalar_bind_name(parser, parser->names[token], meaning == PARSE_MEANING_ORDINARY ? PARSE_MEANING_DECLARED : meaning);
}

BUSTER_INLINE void parse_scalar_restore(ParseScalar* parser, uint32_t mark)
{
    while (parser->tag_count > 0 && parser->tag_positions[parser->tag_count - 1] >= mark)
    {
        parser->tag_count -= 1;
        parser->tags[parser->tag_names[parser->tag_count]] = parser->tag_hidden[parser->tag_count];
    }
    ParseBindingTrace* trace = parser->binding_trace;
    if (trace != 0 && trace->bindings->valid && trace->bindings->active_count > mark + trace->builtin_count)
    {
        c_bindings_restore(trace->bindings, mark + trace->builtin_count, trace->point);
    }
    while (parser->log_count > mark)
    {
        parser->log_count -= 1;
        parser->meanings[parser->log_names[parser->log_count]] = parser->log_meanings[parser->log_count];
    }
}

// A definition's parameter list ends: what it bound since `scope` (its parameters, and the tags and enumerators they
// declare) is kept, with the meaning each name has now (a K&R list's names as ordinary identifiers, which they are in
// the body), then unbound, so that what follows in the declarator sees what they hid. A body binds them again
// (parse_scalar_open_body). A tag the list declares keeps its kind at its entry, as its scope is the body's too (C17
// 6.2.1p4: only a prototype's ends with the list), so that a name after a block item's GNU attributes finds it
// (parse_scalar_attribute_name), as clang's does.
static void parse_scalar_keep_parameters(ParseScalar* parser, uint32_t scope)
{
    parser->kept_count = 0;
    for (uint32_t k = scope; k < parser->log_count; k += 1)
    {
        const uint32_t name = parser->log_names[k];
        const uint8_t meaning = parser->meanings[name];
        parser->kept_names[parser->kept_count] = name;
        parser->kept_meanings[parser->kept_count] = meaning == PARSE_MEANING_KR_LISTED ? PARSE_MEANING_DECLARED : meaning;
        parser->kept_tags[parser->kept_count] = 0;
        if (parser->binding_trace != 0 && parser->binding_trace->bindings->valid)
        {
            ParseBindingTrace* trace = parser->binding_trace;
            trace->kept_events[parser->kept_count] = trace->bindings->active[k + trace->builtin_count];
        }
        parser->kept_count += 1;
    }
    // The list's tags, each undone as parse_scalar_restore would, the latest first, its kind kept at its entry.
    while (parser->tag_count > 0 && parser->tag_positions[parser->tag_count - 1] >= scope)
    {
        parser->tag_count -= 1;
        const uint32_t name = parser->tag_names[parser->tag_count];
        const uint32_t k = parser->tag_positions[parser->tag_count] - scope;
        parser->kept_tags[k] = parser->tags[name];
        parser->kept_tag_names[k] = name;
        parser->tags[name] = parser->tag_hidden[parser->tag_count];
    }
    parse_scalar_restore(parser, scope);
}

// The frame a declarator's name belongs to, from frame `f` (in the declarator): the innermost one below its groups.
BUSTER_INLINE ParseFrame* parse_scalar_declaration_frame(ParseScalar* parser, uint32_t f)
{
    return &parser->frames[parser->frames[f].declaration];
}

BUSTER_INLINE bool parse_frame_is_block(const ParseFrame* frame)
{
    return frame->role == PARSE_ROLE_OPEN_BLOCK || frame->role == PARSE_ROLE_OPEN_BODY;
}

// Whether `frame`, the innermost, is a GNU attribute list, `__attribute__((` ... `))`, or the arguments of an attribute
// in one: clang expects their `)` after the token before (ExpectAndConsume), where it expects a group's elsewhere at the
// token (#1127). C23's `[[` list opens with `[`.
BUSTER_INLINE bool parse_scalar_gnu_attribute_group(const ParseScalar* parser, const ParseFrame* frame)
{
    const ParseFrame* parent = frame != parser->frames ? frame - 1 : frame;
    const bool list = frame->role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && parser->kinds[frame->opener] == PARSE_KIND_L_PAREN;
    const bool arguments = frame->role == PARSE_ROLE_OPEN_CALL && parent->role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && parser->kinds[parent->opener] == PARSE_KIND_L_PAREN;
    return list || arguments;
}

// The missing type of the declarator in declaration frame `frame`, if it has one, which clang's Sema reports as it takes
// the declarator, so before any error at what follows it (#1127).
static void parse_scalar_typeless_note(ParseScalar* parser, ParseFrame* frame)
{
    if (frame->typeless_name != 0)
    {
        const uint8_t message = parser->language.standard >= 2023 ? PARSE_MESSAGE_TYPE_SPECIFIER_REQUIRED : PARSE_MESSAGE_TYPE_SPECIFIER_MISSING;
        const uint32_t before = parser->diagnostic_count;
        parse_scalar_note(parser, frame->typeless_name - 1, message, BUSTER_PARSE_NONE, true);
        if (parser->diagnostic_count > before && frame->role == PARSE_ROLE_OPEN_FUNCTION)
        {
            parser->diagnostics[before].parameter_type = true;
        }
        frame->typeless_name = 0;
    }
}

// The declarator in declaration frame `frame` is complete: its name is bound, as its scope begins there (C 6.2.1p7).
// Without `definition` (no body follows), the parameters a declarator in a file or a block kept are dropped. A K&R
// definition's parameter declaration must declare one of its parameters, once, as clang's ParseKNRParamDeclarations
// checks it once the declarator ends.
static void parse_scalar_complete_declarator(ParseScalar* parser, ParseFrame* frame, bool definition)
{
    const bool kr = (frame->flags & BUSTER_PARSE_FRAME_KR_DECLARATIONS) != 0;
    if (frame->typeless_name != 0 && definition && (frame->flags & BUSTER_PARSE_FRAME_KR) != 0)
    {
        parser->typeless_function = frame->typeless_name;
        frame->typeless_name = 0;
    }
    parse_scalar_typeless_note(parser, frame);
    if (frame->name != BUSTER_PARSE_NONE && kr)
    {
        const uint8_t meaning = parser->meanings[parser->names[frame->name]];
        if (meaning != PARSE_MEANING_KR_PARAMETER)
        {
            const uint8_t message = meaning == PARSE_MEANING_KR_DECLARED ? PARSE_MESSAGE_PARAMETER_REDEFINITION : PARSE_MESSAGE_PARAMETER_MISSING;
            parse_scalar_error_argument(parser, frame->name, message, BUSTER_PARSE_AT_TOKEN, frame->name);
        }
        parse_scalar_bind(parser, frame->name, PARSE_MEANING_KR_DECLARED, C_BINDING_OBJECT);
    }
    else if (frame->name != BUSTER_PARSE_NONE)
    {
        const CBindingKind kind = frame->name_meaning == PARSE_MEANING_TYPEDEF ? C_BINDING_TYPEDEF :
                                  frame->role == PARSE_ROLE_OPEN_ENUM ? C_BINDING_ENUMERATOR :
                                  frame->derivation == PARSE_DERIVATION_FUNCTION && frame->role != PARSE_ROLE_OPEN_FUNCTION ? C_BINDING_FUNCTION : C_BINDING_OBJECT;
        parse_scalar_bind(parser, frame->name, frame->name_meaning, kind);
    }
    if (!definition && !kr && (frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame)))
    {
        parser->kept_count = 0;
    }
    if (definition && parser->binding_trace != NULL)
    {
        parser->binding_trace->pending_definition = frame->name;
    }
    frame->name = BUSTER_PARSE_NONE;
    frame->name_frame = BUSTER_PARSE_NONE;
    frame->derivation = PARSE_DERIVATION_NONE;
}

// The innermost frame with a scope: where a declaration's names are bound.
BUSTER_INLINE bool parse_scalar_binds(const ParseScalar* parser)
{
    return (parser->frames[parser->frame_count - 1].flags & BUSTER_PARSE_FRAME_NO_BIND) == 0;
}

// Pushes a frame for the group `opener` opens; the current frame continues in `continuation` once it closes. Returns
// 0 (and an error) if the frames are full, which only nesting deeper than the tokens' count can make.
BUSTER_INLINE ParseFrame* parse_scalar_push(ParseScalar* parser, uint32_t opener, uint8_t role, uint8_t mode, uint8_t continuation, bool scope)
{
    ParseFrame* parent = &parser->frames[parser->frame_count - 1];
    ParseFrame* frame = &parser->frames[parser->frame_count];
    const bool ternary = role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_OPEN_CASE;
    const bool commas = role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_STRUCT || role == PARSE_ROLE_OPEN_FOR;
    if (parser->binding_trace != 0)
    {
        parser->binding_trace->frame_scopes[parser->frame_count] = scope ? opener + 1 : parse_binding_trace_scope(parser);
    }
    parser->frame_count += 1;
    *frame = (ParseFrame){
        .opener = opener,
        .scope = scope ? parser->log_count : BUSTER_PARSE_NONE,
        .flags = parent->flags & BUSTER_PARSE_FRAME_INHERITED,
        .role = role,
        .mode = mode,
        .closer_role = parse_closer_roles[role],
        .keyword = parser->kinds[(int64_t)opener - 1],
        .continuation = continuation,
        .attribute_closer_role = PARSE_ROLE_NONE,
        .attribute_continuation = PARSE_MODE_ITEM,
        .tag_kind = 0,
        .statement = PARSE_ITEM_EXPRESSION,
        .argument = 0,
        .tag_token = BUSTER_PARSE_NONE,
        .name = BUSTER_PARSE_NONE,
        .name_frame = BUSTER_PARSE_NONE,
        .declaration = role == PARSE_ROLE_OPEN_GROUP ? parent->declaration : parser->frame_count - 1,
        .name_meaning = PARSE_MEANING_ORDINARY,
        .derivation = PARSE_DERIVATION_NONE,
        .comma_role = ternary ? parent->comma_role : commas ? PARSE_ROLE_COMMA : PARSE_ROLE_SEPARATOR,
    };
    return frame;
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's statement stack

BUSTER_INLINE ParseStatement* parse_statement_top(ParseScalar* parser)
{
    return parser->statement_count != 0 ? &parser->statements[parser->statement_count - 1] : 0;
}

BUSTER_INLINE void parse_statement_pop(ParseScalar* parser)
{
    ParseStatement* top = parse_statement_top(parser);
    // A selection or an iteration statement's scope, which its condition's or its clauses' declarations are bound in,
    // ends with the statement.
    if (top->scope != BUSTER_PARSE_NONE)
    {
        parse_scalar_restore(parser, top->scope);
    }
    parser->statement_count -= 1;
}

// A statement just completed: so did every statement whose head owned it, up to an if, which an else may still
// continue, or a do, which its while continues.
BUSTER_INLINE void parse_statement_complete(ParseScalar* parser)
{
    bool completing = true;
    while (completing)
    {
        ParseStatement* top = parse_statement_top(parser);
        completing = top != 0 && top->state == PARSE_STATEMENT_STARTED;
        if (completing && top->kind == PARSE_KIND_IF)
        {
            top->state = PARSE_STATEMENT_IF_DONE;
            completing = false;
        }
        else if (completing && top->kind == PARSE_KIND_DO)
        {
            // The body is a block, which ends before the while.
            parse_scalar_restore(parser, top->scope);
            top->state = PARSE_STATEMENT_DO_DONE;
            completing = false;
        }
        else if (completing)
        {
            parse_statement_pop(parser);
        }
    }
}

// Anything but an else ends the if-statements that were waiting for one.
BUSTER_INLINE void parse_statement_flush(ParseScalar* parser)
{
    ParseStatement* top = parse_statement_top(parser);
    while (top != 0 && top->state == PARSE_STATEMENT_IF_DONE)
    {
        parse_statement_pop(parser);
        parse_statement_complete(parser);
        top = parse_statement_top(parser);
    }
}

// The head that owns the statement starting now, if one is waiting for it; it isn't waiting any more.
BUSTER_INLINE uint32_t parse_statement_owner(ParseScalar* parser)
{
    uint32_t result = BUSTER_PARSE_NONE;
    ParseStatement* top = parse_statement_top(parser);
    if (top != 0 && top->state == PARSE_STATEMENT_WAITING)
    {
        result = top->token;
        top->state = PARSE_STATEMENT_STARTED;
    }
    return result;
}

BUSTER_INLINE void parse_statement_open_block(ParseScalar* parser, bool statement)
{
    uint32_t owner = BUSTER_PARSE_NONE;
    if (statement)
    {
        parse_statement_flush(parser);
        owner = parse_statement_owner(parser);
    }
    if (parser->binding_trace != 0)
    {
        parser->binding_trace->statement_scopes[parser->statement_count] = parse_binding_trace_scope(parser);
    }
    parser->statements[parser->statement_count] = (ParseStatement){
        .token = BUSTER_PARSE_NONE,
        .owner = owner,
        .extra = BUSTER_PARSE_NONE,
        .scope = BUSTER_PARSE_NONE,
        .state = PARSE_STATEMENT_BLOCK,
        .kind = statement,
    };
    parser->statement_count += 1;
}

BUSTER_INLINE void parse_statement_close_block(ParseScalar* parser, uint32_t closer)
{
    parse_statement_flush(parser);
    // C23 lets a label end a block (clang takes it in every mode); anything else left waiting is an error the modes
    // have reported.
    ParseStatement* top = parse_statement_top(parser);
    while (top != 0 && top->state != PARSE_STATEMENT_BLOCK)
    {
        parse_statement_pop(parser);
        top = parse_statement_top(parser);
    }
    if (top != 0)
    {
        const bool statement = top->kind != 0;
        if (top->owner != BUSTER_PARSE_NONE)
        {
            parser->parents[closer] = top->owner;
        }
        parser->statement_count -= 1;
        if (statement)
        {
            parse_statement_complete(parser);
        }
    }
}

BUSTER_INLINE void parse_statement_terminator(ParseScalar* parser, uint32_t terminator)
{
    parse_statement_flush(parser);
    ParseStatement* top = parse_statement_top(parser);
    if (top != 0 && top->state == PARSE_STATEMENT_DO_WHILE)
    {
        parser->parents[top->token] = terminator;
        parser->parents[top->extra] = terminator;
        if (top->owner != BUSTER_PARSE_NONE)
        {
            parser->parents[terminator] = top->owner;
        }
        parse_statement_pop(parser);
    }
    else
    {
        const uint32_t owner = parse_statement_owner(parser);
        if (owner != BUSTER_PARSE_NONE)
        {
            parser->parents[terminator] = owner;
        }
    }
    parse_statement_complete(parser);
}

// A head: the `)` of if, while, for or switch, a label's or case's `:`, or `do`. `kind` is its keyword, and `scope` the
// binding log's length where the statement's scope began (BUSTER_PARSE_NONE for a label). A while after a do's body is
// its do's instead, in its do's scope.
BUSTER_INLINE void parse_statement_head(ParseScalar* parser, uint32_t head, uint8_t kind, uint32_t scope)
{
    parse_statement_flush(parser);
    ParseStatement* top = parse_statement_top(parser);
    if (kind == PARSE_KIND_WHILE && top != 0 && top->state == PARSE_STATEMENT_DO_DONE)
    {
        top->state = PARSE_STATEMENT_DO_WHILE;
        top->extra = head;
    }
    else
    {
        const uint32_t owner = parse_statement_owner(parser);
        if (owner != BUSTER_PARSE_NONE && kind != PARSE_KIND_DO)
        {
            parser->parents[head] = owner;
        }
        if (parser->binding_trace != 0)
        {
            ParseBindingTrace* trace = parser->binding_trace;
            trace->statement_scopes[parser->statement_count] = scope != BUSTER_PARSE_NONE ? trace->matches[head] + 1 : parse_binding_trace_scope(parser);
        }
        parser->statements[parser->statement_count] = (ParseStatement){
            .token = head,
            .owner = kind == PARSE_KIND_DO ? owner : BUSTER_PARSE_NONE,
            .extra = kind == PARSE_KIND_IF ? parser->log_count : BUSTER_PARSE_NONE,
            .scope = scope,
            .state = PARSE_STATEMENT_WAITING,
            .kind = kind,
        };
        parser->statement_count += 1;
    }
}

BUSTER_INLINE void parse_statement_else(ParseScalar* parser, uint32_t token)
{
    ParseStatement* top = parse_statement_top(parser);
    if (top != 0 && top->state == PARSE_STATEMENT_IF_DONE)
    {
        // The then-statement is a block, which ends here; the else-statement is in the if's scope.
        parse_scalar_restore(parser, top->extra);
        parser->parents[token] = top->token;
        if (parser->binding_trace != 0)
        {
            parser->binding_trace->statement_scopes[parser->statement_count - 1] = token + 1;
        }
        *top = (ParseStatement){
            .token = token,
            .owner = BUSTER_PARSE_NONE,
            .extra = BUSTER_PARSE_NONE,
            .scope = top->scope,
            .state = PARSE_STATEMENT_WAITING,
            .kind = PARSE_KIND_ELSE,
        };
    }
    else
    {
        // clang reads an else without an if as the start of an expression.
        parse_scalar_error(parser, token, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
    }
}

static BUSTER_BAKED_FRONTEND uint32_t parse_role_info[PARSE_ROLE_COUNT];

static void parse_role_info_initialize(void)
{
    for (uint32_t role = 0; role < PARSE_ROLE_COUNT; role += 1)
    {
        parse_role_info[role] = BUSTER_PARSE_ROLE_INFO(role);
    }
}

// Adds token `token` with its role and depth to the tree. Keys count depths from 1 here, so that a sentinel at the
// bottom of each stack (key 0 and threshold 0, under every key and threshold) ends every search without a check of the
// count: an L of BUSTER_PARSE_NONE, and an R search that nothing ends.
BUSTER_INLINE void parse_scalar_emit(ParseScalar* parser, uint32_t token, uint8_t role, uint32_t depth)
{
    uint32_t* const parents = parser->parents;
    ParseCandidate* const candidates = parser->candidates;
    ParseWaiting* const waiting = parser->waiting;
    uint32_t candidate_count = parser->candidate_count;
    uint32_t waiting_count = parser->waiting_count;
    const uint32_t info = parse_role_info[role];
    const uint32_t base = (depth + 1) * BUSTER_PARSE_LEVELS;
    const uint32_t key = base + (info & 0xff);
    const uint8_t rule = (uint8_t)(info >> 24);
    parser->roles[token] = role;
    parser->depths[token] = depth;
    parents[token] = BUSTER_PARSE_NONE;
    // The tokens this one ends the right search of. The statement stack has already set the parents of what a head
    // owns.
    while (waiting[waiting_count - 1].threshold > key)
    {
        waiting_count -= 1;
        const ParseWaiting entry = waiting[waiting_count];
        const uint32_t parent = parse_select_keys(entry.left, entry.left_key, token, key);
        if (parents[entry.position] == BUSTER_PARSE_NONE)
        {
            parents[entry.position] = parent;
        }
    }
    // L, then this token replaces every candidate it hides.
    const uint32_t left_threshold = base + ((info >> 8) & 0xff);
    while (candidates[candidate_count - 1].key >= left_threshold)
    {
        candidate_count -= 1;
    }
    const ParseCandidate left = candidates[candidate_count - 1];
    while (candidates[candidate_count - 1].key >= key)
    {
        candidate_count -= 1;
    }
    candidates[candidate_count] = (ParseCandidate){ .position = token, .key = key };
    candidate_count += 1;
    if (rule == PARSE_RULE_LEFT)
    {
        parents[token] = left.position;
    }
    else if (rule != PARSE_RULE_OPENER)
    {
        const bool right_only = rule == PARSE_RULE_RIGHT;
        waiting[waiting_count] = (ParseWaiting){
            .position = token,
            .threshold = base + ((info >> 16) & 0xff),
            .left = right_only ? BUSTER_PARSE_NONE : left.position,
            .left_key = left.key,
        };
        waiting_count += 1;
    }
    parser->candidate_count = candidate_count;
    parser->waiting_count = waiting_count;
}

// Resolves what is still waiting at the end, which has no R.
static void parse_scalar_finish(ParseScalar* parser)
{
    while (parser->waiting_count > 1)
    {
        parser->waiting_count -= 1;
        const ParseWaiting entry = parser->waiting[parser->waiting_count];
        const uint32_t parent = parse_select_keys(entry.left, entry.left_key, BUSTER_PARSE_NONE, 0);
        if (parser->parents[entry.position] == BUSTER_PARSE_NONE)
        {
            parser->parents[entry.position] = parent;
        }
    }
}

// Whether a comma separates declarators or is the comma operator (COMMA), or separates arguments, parameters,
// initializers or enumerators (SEPARATOR, below the specifiers), by the innermost group of ( [ { around it: the ?: and
// case groups don't count, so the kernels decide it from the bracket depth of ( [ { alone. Each frame has it from its
// push (parse_scalar_push), so that commas in nested ?: groups don't walk them.
BUSTER_INLINE uint8_t parse_frame_comma_role(const ParseScalar* parser)
{
    return parser->frames[parser->frame_count - 1].comma_role;
}

BUSTER_INLINE bool parse_kind_is_closer(uint8_t kind)
{
    return kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
}

// Whether token `i` can't start an expression, as clang's isNotExpressionStart says after a comma operator: braces,
// some statement keywords, and declaration specifiers.
static bool parse_scalar_not_expression_start(const ParseScalar* parser, uint32_t i)
{
    const uint8_t kind = parser->kinds[i];
    const bool keyword = kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_FOR || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_IF ||
                         kind == PARSE_KIND_ELSE || kind == PARSE_KIND_GOTO;
    const bool specifier = kind == PARSE_KIND_TYPE || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STORAGE ||
                           kind == PARSE_KIND_TYPEDEF || kind == PARSE_KIND_ATOMIC || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ALIGNAS ||
                           parse_scalar_is_typedef_name(parser, i);
    return keyword || specifier;
}

// Whether token `i` may start a declarator, as clang's MightBeDeclarator asks after a declaration's comma.
static bool parse_scalar_might_be_declarator(const ParseScalar* parser, uint32_t i, bool member)
{
    const uint8_t kind = parser->kinds[i];
    return kind == PARSE_KIND_IDENTIFIER || kind == PARSE_KIND_STAR || kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_CALLING_CONVENTION ||
           kind == PARSE_KIND_QUALIFIER || (member && kind == PARSE_KIND_COLON) || parse_scalar_standard_attribute(parser, i);
}

// The message for `[[` where clang takes no attribute (its DiagnoseProhibitedCXX11Attribute): an attribute list's if it
// looks like one, `[[`, a balanced run, then `]]`, and else that `[[` starts only an attribute.
static uint8_t parse_scalar_prohibited_attribute(const ParseScalar* parser, uint32_t i)
{
    uint32_t j = i + 2;
    uint32_t depth = 0;
    bool open = true;
    while (open && parser->kinds[j] != PARSE_KIND_END)
    {
        const uint8_t kind = parser->kinds[j];
        open = depth != 0 || !parse_kind_is_closer(kind);
        depth += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        depth -= depth != 0 && parse_kind_is_closer(kind);
        j += open;
    }
    const bool attribute = parser->kinds[j] == PARSE_KIND_R_SQUARE && parser->kinds[j + 1] == PARSE_KIND_R_SQUARE;
    return attribute ? PARSE_MESSAGE_ATTRIBUTE_LIST_NOT_ALLOWED : PARSE_MESSAGE_NOT_ATTRIBUTE;
}

// The token after the balanced group that opens at `open`, or at the `;` or the end that leaves it open (`*balanced`
// false then).
static uint32_t parse_scalar_skip_group(const ParseScalar* parser, uint32_t open, bool* balanced)
{
    uint32_t j = open;
    uint32_t depth = 0;
    do
    {
        const uint8_t kind = parser->kinds[j];
        depth += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        depth -= depth != 0 && parse_kind_is_closer(kind);
        j += 1;
    } while (depth != 0 && parser->kinds[j] != PARSE_KIND_END && parser->kinds[j] != PARSE_KIND_SEMI);
    *balanced &= depth == 0;
    return j;
}

// Whether the expression statement before token `i` is a name alone or a name's call, `retrn` or `whil (a)`, at the
// statement's start (ParseDiagnostic.probe): after a `;`, a block's `{` or `}`, `else`, `do`, a label's or a case's `:`, or a
// condition's or a for header's `)` (check_typo_statement_start). The `)` of the call is matched backwards, balanced.
static bool parse_scalar_name_statement(const ParseScalar* parser, uint32_t i)
{
    uint32_t name = i != 0 && parser->kinds[i - 1] == PARSE_KIND_IDENTIFIER ? i - 1 : BUSTER_PARSE_NONE;
    if (name == BUSTER_PARSE_NONE && i != 0 && parser->kinds[i - 1] == PARSE_KIND_R_PAREN)
    {
        uint32_t depth = 0;
        uint32_t k = i - 1;
        bool open = true;
        while (open)
        {
            const uint8_t kind = parser->kinds[k];
            depth += kind == PARSE_KIND_R_PAREN ? 1u : 0u;
            depth -= kind == PARSE_KIND_L_PAREN ? 1u : 0u;
            open = depth != 0 && k != 0 && kind != PARSE_KIND_SEMI && kind != PARSE_KIND_L_BRACE && kind != PARSE_KIND_R_BRACE;
            k -= open ? 1u : 0u;
        }
        name = depth == 0 && k != 0 && parser->kinds[k] == PARSE_KIND_L_PAREN && parser->kinds[k - 1] == PARSE_KIND_IDENTIFIER ? k - 1 : BUSTER_PARSE_NONE;
    }
    const uint8_t kind = name != BUSTER_PARSE_NONE && name != 0 ? parser->kinds[name - 1] : PARSE_KIND_END;
    const uint8_t role = name != BUSTER_PARSE_NONE && name != 0 ? parser->roles[name - 1] : PARSE_ROLE_COUNT;
    const bool block = kind == PARSE_KIND_L_BRACE && role != PARSE_ROLE_OPEN_INITIALIZER && role != PARSE_ROLE_OPEN_COMPOUND_BODY;
    return kind == PARSE_KIND_SEMI || block || kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_ELSE || kind == PARSE_KIND_DO ||
           role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR || (kind == PARSE_KIND_COLON && role != PARSE_ROLE_CLOSE_TERNARY);
}

// After the first error at `token`, where clang's balanced tracker skips to the group's `closer` (its SkipUntil, which
// stops at a `;`) and goes on as if the tokens from `token` to it weren't there: the diagnostic-only view drops them
// (ParseDiagnostic.resume_delete). Without the closer before a `;`, nothing is resumed.
static void parse_scalar_resume_at_closer(ParseScalar* parser, uint32_t token, uint8_t closer)
{
    uint32_t tail = token;
    bool balanced = true;
    while (balanced && tail < parser->count && parser->kinds[tail] != closer && parser->kinds[tail] != PARSE_KIND_SEMI)
    {
        const uint8_t next = parser->kinds[tail];
        if (next == PARSE_KIND_L_PAREN || next == PARSE_KIND_L_SQUARE || next == PARSE_KIND_L_BRACE)
        {
            tail = parse_scalar_skip_group(parser, tail, &balanced);
        }
        else
        {
            balanced = !parse_kind_is_closer(next);
            tail += 1;
        }
    }
    if (balanced && tail < parser->count && parser->kinds[tail] == closer)
    {
        parser->error_resumed = true;
        parser->error_resume_delete = true;
        parser->error_resume_end = tail;
    }
}

// Brackets at `i` where a declarator's name is due (`int [3] x;`), as clang reads them (ParseMisplacedBracketDeclarator):
// the brackets, then the declarator they would follow. Where that one has a name, the brackets are in the wrong place,
// which clang says at its end; else clang expects the name where it is due after them, but at the brackets before a
// `;`, as without them.
static void parse_scalar_misplaced_brackets(ParseScalar* parser, uint32_t i)
{
    uint32_t j = i;
    bool balanced = true;
    while (balanced && parser->kinds[j] == PARSE_KIND_L_SQUARE && !parse_scalar_standard_attribute(parser, j))
    {
        j = parse_scalar_skip_group(parser, j, &balanced);
    }
    // The declarator after them, if it is a plain one: pointers, a name or a group, and suffixes.
    uint32_t k = j;
    bool pointer = false;
    while (parser->kinds[k] == PARSE_KIND_STAR || parser->kinds[k] == PARSE_KIND_QUALIFIER)
    {
        pointer |= parser->kinds[k] == PARSE_KIND_STAR;
        k += 1;
    }
    const bool named = parser->kinds[k] == PARSE_KIND_IDENTIFIER || parser->kinds[k] == PARSE_KIND_L_PAREN;
    k = parser->kinds[k] == PARSE_KIND_IDENTIFIER ? k + 1 : parser->kinds[k] == PARSE_KIND_L_PAREN ? parse_scalar_skip_group(parser, k, &balanced) : k;
    while (balanced && named && (parser->kinds[k] == PARSE_KIND_L_PAREN || (parser->kinds[k] == PARSE_KIND_L_SQUARE && !parse_scalar_standard_attribute(parser, k))))
    {
        k = parse_scalar_skip_group(parser, k, &balanced);
    }
    const uint8_t next = parser->kinds[j];
    const bool plain = parser->kinds[k] == PARSE_KIND_SEMI || parser->kinds[k] == PARSE_KIND_COMMA || parser->kinds[k] == PARSE_KIND_EQUAL ||
                       parser->kinds[k] == PARSE_KIND_R_PAREN || parser->kinds[k] == PARSE_KIND_L_BRACE;
    const bool declarator = next == PARSE_KIND_IDENTIFIER || next == PARSE_KIND_L_PAREN || next == PARSE_KIND_STAR || next == PARSE_KIND_L_SQUARE ||
                            next == PARSE_KIND_ATTRIBUTE || next == PARSE_KIND_QUALIFIER || next == PARSE_KIND_CALLING_CONVENTION || next == PARSE_KIND_ATOMIC;
    if (balanced && named && plain)
    {
        parse_scalar_error(parser, k, PARSE_MESSAGE_BRACKETS_AFTER_NAME, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (balanced && ((pointer && !named) || (next != PARSE_KIND_SEMI && !declarator)))
    {
        parse_scalar_error(parser, k, PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
}

// The error for token `i`, which can't continue what the frame holds: clang's "expected" of the item or the group, after
// the token before for a missing `;` (and the other punctuators clang expects at the end of something), at the token
// for a group's closer.
static void parse_scalar_unexpected(ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t opener_kind = frame->opener != BUSTER_PARSE_NONE ? parser->kinds[frame->opener] : PARSE_KIND_END;
    const bool statements = frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame);
    // A `)` or `]` before the `;` that ends a declaration or an expression statement is extraneous (clang's
    // ExpectAndConsumeSemi), but not where a return statement's `;` is due, which clang expects as any other token.
    const bool extraneous = statements && !(parse_frame_is_block(frame) && frame->statement == PARSE_ITEM_RETURN && (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) == 0);
    if (parse_scalar_fn_body_due(parser, i, frame))
    {
        // After a function's declarator, what can't continue the declaration is taken for a missing body, but a
        // declaration specifier for the next declaration after a missing `;`; a `)` before a `;` among them, as clang asks
        // for the body before it looks for the `;` (#1127).
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_FUNCTION_BODY, BUSTER_PARSE_AT_TOKEN);
    }
    else if (extraneous && (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE) && parser->kinds[i + 1] == PARSE_KIND_SEMI)
    {
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXTRANEOUS_BEFORE_SEMI, BUSTER_PARSE_AT_TOKEN, i);
    }
    else if (frame->mode == PARSE_MODE_OPERAND && (frame->role == PARSE_ROLE_OPEN_CALL || frame->role == PARSE_ROLE_OPEN_TERNARY))
    {
        // A call's first argument, or GNU's `?:` operand, may be absent where the group ends, but anything else clang
        // reads as the expression (#1127).
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
    }
    else if (frame->role == PARSE_ROLE_NONE)
    {
        parser->error_resumed = parser->error_token == BUSTER_PARSE_NONE && (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) &&
                                parse_scalar_resumes(parser, i);
        parse_scalar_error(parser, i, (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) ? PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TOP_LEVEL_DECLARATOR : PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN,
                           (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) ? BUSTER_PARSE_AFTER_PREVIOUS : BUSTER_PARSE_AT_TOKEN);
    }
    else if (parse_frame_is_block(frame))
    {
        const uint8_t message = (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) ? PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION :
                                frame->statement == PARSE_ITEM_RETURN ? PARSE_MESSAGE_EXPECTED_SEMI_AFTER_RETURN : PARSE_MESSAGE_EXPECTED_SEMI_AFTER_EXPRESSION;
        const bool probe = parser->error_token == BUSTER_PARSE_NONE && message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_EXPRESSION && parse_scalar_name_statement(parser, i);
        parser->error_resumed = probe || (parser->error_token == BUSTER_PARSE_NONE && (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) &&
                                          parse_scalar_resumes(parser, i));
        parser->error_probe = probe;
        parse_scalar_error(parser, i, message, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_STRUCT)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION_LIST, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_FOR)
    {
        // A third `;` where the last clause's expression would start: clang expects the expression. clang expects each
        // clause's `;` at the token it finds instead, not after the one before as a statement's (#1127).
        const uint8_t message = frame->argument < 2 ? PARSE_MESSAGE_EXPECTED_SEMI_IN_FOR :
                                (frame->flags & BUSTER_PARSE_FRAME_EMPTY_OK) && frame->mode == PARSE_MODE_OPERAND ? PARSE_MESSAGE_EXPECTED_EXPRESSION : PARSE_MESSAGE_EXPECTED_R_PAREN;
        if (message == PARSE_MESSAGE_EXPECTED_R_PAREN)
        {
            parse_scalar_close_expected(parser, i, PARSE_KIND_L_PAREN, true);
        }
        else
        {
            parse_scalar_error(parser, i, message, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (frame->role == PARSE_ROLE_OPEN_CASE)
    {
        const bool is_default = parser->kinds[frame->opener] == PARSE_KIND_DEFAULT;
        // A `;` where the `:` is due is the label's empty statement to clang's recovery, which reads on past it.
        if (parser->error_token == BUSTER_PARSE_NONE)
        {
            parser->error_resume_colon = kind == PARSE_KIND_SEMI;
        }
        parse_scalar_error(parser, i, is_default ? PARSE_MESSAGE_EXPECTED_COLON_AFTER_DEFAULT : PARSE_MESSAGE_EXPECTED_COLON_AFTER_CASE, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_TERNARY)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AT_TOKEN);
    }
    else if (frame->role == PARSE_ROLE_OPEN_ENUM && kind == PARSE_KIND_IDENTIFIER)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_MISSING_COMMA_BETWEEN_ENUMERATORS, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_ENUM)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_BRACE_OR_COMMA, BUSTER_PARSE_AT_TOKEN);
    }
    else if (frame->role == PARSE_ROLE_OPEN_KEYWORD && frame->argument < parse_scalar_keyword_commas(parser, frame))
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COMMA, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_KEYWORD && frame->keyword == PARSE_KIND_GENERIC && frame->argument != 0 && (frame->flags & BUSTER_PARSE_FRAME_ABSTRACT))
    {
        // A _Generic association's type, which its `:` follows.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (parse_scalar_gnu_attribute_group(parser, frame) && (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) != 0)
    {
        // A type argument's group (parse_scalar_type_argument_attribute), whose `)` clang's ParseAttributeWithTypeArg
        // expects at the token, as a group's.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else if (parse_scalar_gnu_attribute_group(parser, frame))
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (parse_scalar_keyword_parameter(parser, i, frame))
    {
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_KEYWORD_AS_PARAMETER, BUSTER_PARSE_AT_TOKEN, i);
    }
    else
    {
        // The builtins clang reads with an argument list of its own check their `)` without its balanced delimiters, all
        // but __builtin_offsetof, __builtin_types_compatible_p and __builtin_bit_cast.
        const IdentId keyword = frame->role == PARSE_ROLE_OPEN_KEYWORD && frame->keyword == PARSE_KIND_TYPE_ARGUMENT ? parser->names[(int64_t)frame->opener - 1] : 0;
        const bool balanced = keyword == 0 || keyword == C_NAME_BUILTIN_OFFSETOF || keyword == C_NAME_BUILTIN_TYPES_COMPATIBLE_P || keyword == C_NAME_BUILTIN_BIT_CAST;
        parse_scalar_close_expected(parser, i, opener_kind, balanced);
    }
}

// Closes the current frame with token `i` (a `)`, `]`, `}`, or the `:` of ? or case).
static void parse_scalar_close(ParseScalar* parser, uint32_t i, ParseStep* step)
{
    ParseFrame* frame = &parser->frames[parser->frame_count - 1];
    const uint8_t kind = parser->kinds[i];
    const bool colon = kind == PARSE_KIND_COLON;
    const uint8_t opener_kind = frame->opener != BUSTER_PARSE_NONE ? parser->kinds[frame->opener] : PARSE_KIND_END;
    const bool matches = colon ? (frame->role == PARSE_ROLE_OPEN_TERNARY || frame->role == PARSE_ROLE_OPEN_CASE) :
                         (kind == PARSE_KIND_R_PAREN && opener_kind == PARSE_KIND_L_PAREN) || (kind == PARSE_KIND_R_SQUARE && opener_kind == PARSE_KIND_L_SQUARE) ||
                         (kind == PARSE_KIND_R_BRACE && opener_kind == PARSE_KIND_L_BRACE);
    if (!matches && parser->frame_count == 1)
    {
        parse_scalar_error(parser, i, kind == PARSE_KIND_R_BRACE ? PARSE_MESSAGE_EXTRANEOUS_CLOSING_BRACE : PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else if (!matches)
    {
        parse_scalar_unexpected(parser, i, frame);
    }
    else if (frame->role == PARSE_ROLE_OPEN_FOR && frame->argument != 2)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_IN_FOR, BUSTER_PARSE_AT_TOKEN);
    }
    else
    {
        uint8_t role = frame->closer_role;
        uint8_t continuation = frame->continuation;
        // The last parameter's declarator ends with the list.
        parse_scalar_complete_declarator(parser, frame, false);
        const bool sizeof_type = frame->role == PARSE_ROLE_OPEN_SIZEOF && (frame->flags & BUSTER_PARSE_FRAME_ABSTRACT) != 0;
        if (frame->role == PARSE_ROLE_OPEN_CAST || sizeof_type)
        {
            if (parser->kinds[i + 1] == PARSE_KIND_L_BRACE)
            {
                // A compound literal: `(T){...}`, also as sizeof's operand, `sizeof (T){...}`.
                parser->roles[frame->opener] = PARSE_ROLE_OPEN_COMPOUND;
                role = PARSE_ROLE_CLOSE_COMPOUND;
                continuation = PARSE_MODE_COMPOUND_BODY;
            }
            else if (frame->role == PARSE_ROLE_OPEN_CAST)
            {
                continuation = PARSE_MODE_OPERAND;
            }
        }
        if (frame->role == PARSE_ROLE_OPEN_GROUP)
        {
            // A declarator's group closing after the name, without a suffix applied to it inside: with a `*`, the name
            // is a pointer; without, the group is only parentheses, and what follows it decides.
            ParseFrame* declaration = parse_scalar_declaration_frame(parser, parser->frame_count - 2);
            if (declaration->derivation == PARSE_DERIVATION_PENDING && declaration->name_frame == parser->frame_count - 1)
            {
                declaration->derivation = (frame->flags & BUSTER_PARSE_FRAME_POINTER) ? PARSE_DERIVATION_OTHER : declaration->derivation;
                declaration->name_frame = parser->frame_count - 2;
            }
        }
        if (frame->flags & BUSTER_PARSE_FRAME_DEFINITION_PARAMETERS)
        {
            // The declared function's parameters, which a body binds again if one follows the declarator.
            parse_scalar_keep_parameters(parser, frame->scope);
        }
        else if (frame->role == PARSE_ROLE_OPEN_FOR || frame->role == PARSE_ROLE_OPEN_CONDITION)
        {
            // A condition's or the clauses' declarations are in the statement's scope, which its head carries on.
            step->scope = frame->scope;
        }
        else if (frame->scope != BUSTER_PARSE_NONE)
        {
            parse_scalar_restore(parser, frame->scope);
        }
        if (frame->role == PARSE_ROLE_OPEN_CONDITION || frame->role == PARSE_ROLE_OPEN_FOR || frame->role == PARSE_ROLE_OPEN_CASE)
        {
            step->event = PARSE_EVENT_HEAD;
            step->keyword = frame->role == PARSE_ROLE_OPEN_CASE ? PARSE_KIND_CASE : frame->keyword;
        }
        else if (frame->role == PARSE_ROLE_OPEN_BLOCK || frame->role == PARSE_ROLE_OPEN_BODY)
        {
            step->event = PARSE_EVENT_CLOSE_BLOCK;
        }
        // A K&R identifier list that is the declared name's own parameters, which a definition's parameter declarations
        // may follow: the declaration's frame says so, past the declarator's groups (`int (f(x)) int x; {...}`).
        const bool kr = (frame->flags & BUSTER_PARSE_FRAME_DEFINITION_PARAMETERS) != 0 && (frame->flags & BUSTER_PARSE_FRAME_KR) != 0;
        const bool type_operand = sizeof_type && role != PARSE_ROLE_CLOSE_COMPOUND;
        parser->parents[frame->opener] = i;
        parser->frame_count -= 1;
        ParseFrame* parent = &parser->frames[parser->frame_count - 1];
        parent->mode = continuation;
        // The while of a do-statement: its `;` must follow.
        if (frame->role == PARSE_ROLE_OPEN_CONDITION && frame->keyword == PARSE_KIND_WHILE && parse_frame_is_block(parent))
        {
            const ParseStatement* top = parse_statement_top(parser);
            parent->mode = top != 0 && top->state == PARSE_STATEMENT_DO_DONE ? PARSE_MODE_END_STATEMENT : continuation;
        }
        parser->frames[parent->declaration].flags |= kr ? BUSTER_PARSE_FRAME_KR : 0;
        parent->flags = type_operand ? parent->flags | BUSTER_PARSE_FRAME_NO_POSTFIX : parent->flags;
        step->role = role;
        step->depth = parser->frame_count - 1;
    }
}

// A closer after an expression or a declarator: it ends the group the item is in, but a statement, a declaration or a
// member needs its `;` first. A struct's last member may go without one (clang warns, and so does GCC).
static void parse_scalar_close_item(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    if (frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame) || (frame->role == PARSE_ROLE_OPEN_STRUCT && parser->kinds[i] != PARSE_KIND_R_BRACE))
    {
        parse_scalar_unexpected(parser, i, frame);
    }
    else
    {
        parse_scalar_close(parser, i, step);
    }
}

// Opens a group with token `i`: the opener's depth is its parent's.
BUSTER_INLINE ParseFrame* parse_scalar_open(ParseScalar* parser, uint32_t i, uint8_t role, uint8_t mode, uint8_t continuation, bool scope, ParseStep* step)
{
    step->role = role;
    step->depth = parser->frame_count - 1;
    return parse_scalar_push(parser, i, role, mode, continuation, scope);
}

// The end of a declaration's `;`, or of an expression statement's, in any frame that holds them.
static void parse_scalar_terminator(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    step->role = PARSE_ROLE_TERMINATOR;
    if (frame->role == PARSE_ROLE_OPEN_FOR && frame->argument < 2)
    {
        frame->argument += 1;
        frame->statement = PARSE_ITEM_EXPRESSION;
        frame->mode = PARSE_MODE_OPERAND;
        frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_ITEM_STATE) | BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame) || frame->role == PARSE_ROLE_OPEN_STRUCT)
    {
        // A K&R definition's parameter declarations go on until its body.
        const bool kr = (frame->flags & BUSTER_PARSE_FRAME_KR_DECLARATIONS) != 0;
        frame->mode = kr ? PARSE_MODE_KR_PARAMETERS : PARSE_MODE_ITEM;
        step->event = parse_frame_is_block(frame) ? PARSE_EVENT_TERMINATOR : PARSE_EVENT_NONE;
        frame->flags &= ~BUSTER_PARSE_FRAME_ITEM_STATE;
        frame->statement = PARSE_ITEM_EXPRESSION;
    }
    else
    {
        parse_scalar_unexpected(parser, i, frame);
    }
}

// The specifiers of a declaration start at `i`: with names required (declarations), optional (parameters) or absent
// (type names).
BUSTER_INLINE void parse_scalar_begin_declaration(ParseFrame* frame, uint32_t names, ParseStep* step)
{
    frame->mode = PARSE_MODE_SPECIFIERS;
    frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_ITEM_STATE) | BUSTER_PARSE_FRAME_DECLARATION | names;
    step->again = true;
}

// The start of an expression at `i` in the frame: `empty` if it may be absent.
BUSTER_INLINE void parse_scalar_begin_expression(ParseFrame* frame, bool empty, bool conditional, ParseStep* step)
{
    frame->mode = PARSE_MODE_OPERAND;
    frame->flags = (frame->flags & ~(BUSTER_PARSE_FRAME_EMPTY_OK | BUSTER_PARSE_FRAME_CONDITIONAL)) | (empty ? BUSTER_PARSE_FRAME_EMPTY_OK : 0) |
                   (conditional ? BUSTER_PARSE_FRAME_CONDITIONAL : 0);
    step->again = true;
}

// A C23 attribute's first `[`, in the place `role` says (OPEN_ATTRIBUTE_SPECIFIER among specifiers, OPEN_ATTRIBUTE after
// a declarator, ...), after which the frame continues in `continuation`.
BUSTER_INLINE void parse_scalar_standard_attribute_open(ParseScalar* parser, uint32_t i, uint8_t role, uint8_t continuation, ParseStep* step)
{
    ParseFrame* group = parse_scalar_open(parser, i, role, PARSE_MODE_STANDARD_ATTRIBUTE_LIST, continuation, false, step);
    group->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE;
}

// What argument `argument` of the keyword group whose keyword is `keyword` (its IdentId) may be, and how many arguments
// the group takes at most and at least.
static uint8_t parse_keyword_argument(IdentId keyword, uint8_t kind, uint32_t argument, uint32_t* most, uint32_t* least)
{
    uint8_t result = BUSTER_PARSE_ARGUMENT_EXPRESSION;
    *most = 1;
    *least = 1;
    if (kind == PARSE_KIND_STATIC_ASSERT)
    {
        *most = 2;
    }
    else if (kind == PARSE_KIND_GENERIC)
    {
        *most = UINT32_MAX;
        *least = 2;
        result = argument == 0 ? BUSTER_PARSE_ARGUMENT_EXPRESSION : BUSTER_PARSE_ARGUMENT_TYPE;
    }
    else
    {
        switch (keyword)
        {
            case C_NAME_BUILTIN_OFFSETOF:
                *most = 2;
                *least = 2;
                result = argument == 0 ? BUSTER_PARSE_ARGUMENT_TYPE : BUSTER_PARSE_ARGUMENT_EXPRESSION;
                break;
            case C_NAME_BUILTIN_VA_ARG:
            case C_NAME_BUILTIN_CONVERTVECTOR:
                *most = 2;
                *least = 2;
                result = argument == 0 ? BUSTER_PARSE_ARGUMENT_EXPRESSION : BUSTER_PARSE_ARGUMENT_TYPE;
                break;
            case C_NAME_BUILTIN_BIT_CAST:
                *most = 2;
                *least = 2;
                result = argument == 0 ? BUSTER_PARSE_ARGUMENT_TYPE : BUSTER_PARSE_ARGUMENT_EXPRESSION;
                break;
            case C_NAME_BUILTIN_TYPES_COMPATIBLE_P:
                *most = 2;
                *least = 2;
                result = BUSTER_PARSE_ARGUMENT_TYPE;
                break;
            case C_NAME_BUILTIN_CHOOSE_EXPR:
                *most = 3;
                *least = 3;
                break;
            case C_NAME_BUILTIN_COLUMN:
            case C_NAME_BUILTIN_FILE:
            case C_NAME_BUILTIN_FILE_NAME:
            case C_NAME_BUILTIN_FUNCTION:
            case C_NAME_BUILTIN_LINE:
                *most = 0;
                *least = 0;
                break;
            case C_NAME_BUILTIN_VECTORELEMENTS:
                // clang reads its parentheses as sizeof's: a type, or an expression, a comma expression too
                // (`__builtin_vectorelements(a, b)`, #1126's review), whose operands after the first are expressions.
                *most = UINT32_MAX;
                result = argument == 0 ? BUSTER_PARSE_ARGUMENT_EITHER : BUSTER_PARSE_ARGUMENT_EXPRESSION;
                break;
            case C_NAME_BUILTIN_AVAILABLE:
                // Its specs, which parse_scalar_item hands to their own modes: one at least.
                *most = UINT32_MAX;
                break;
            case C_NAME_BUILTIN_OMP_REQUIRED_SIMD_ALIGN:
            case C_NAME_BUILTIN_PTRAUTH_TYPE_DISCRIMINATOR:
            case C_NAME_IS_BITWISE_CLONEABLE:
            case C_NAME_IS_DESTRUCTIBLE:
            case C_NAME_IS_NOTHROW_DESTRUCTIBLE:
                result = BUSTER_PARSE_ARGUMENT_TYPE;
                break;
            default:
                *most = UINT32_MAX;
                *least = 0;
                result = BUSTER_PARSE_ARGUMENT_EITHER;
                break;
        }
    }
    return result;
}

// Whether the `__extension__` at `i` is one of those before a static assertion, which clang's ParseExternalDeclaration
// and its statement reader pass over (#1447): `__extension__`s alone from it to a `_Static_assert`.
static bool parse_scalar_extension_assertion(const ParseScalar* parser, uint32_t i)
{
    uint32_t at = i;
    while (parser->kinds[at] == PARSE_KIND_EXTENSION)
    {
        at += 1;
    }
    return parser->kinds[at] == PARSE_KIND_STATIC_ASSERT;
}

static void parse_scalar_item(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t group = frame->role;
    const bool block = parse_frame_is_block(frame);
    if (group == PARSE_ROLE_OPEN_ENUM)
    {
        if (kind == PARSE_KIND_IDENTIFIER)
        {
            // The constant's scope begins after its enumerator, value included (C 6.2.1p7): it is bound at the
            // enumerator's `,` or `}` (parse_scalar_complete_declarator).
            step->role = PARSE_ROLE_OPERAND;
            frame->name = i;
            frame->name_meaning = PARSE_MEANING_ORDINARY;
            if (parser->binding_trace != 0)
            {
                parser->binding_trace->namespaces[i] = C_BINDING_ORDINARY;
            }
            frame->mode = PARSE_MODE_ENUMERATOR;
            frame->argument += 1;
        }
        else if (kind == PARSE_KIND_R_BRACE && frame->argument == 0)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EMPTY_ENUM, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_R_BRACE)
        {
            parse_scalar_close(parser, i, step);
        }
        else
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (group == PARSE_ROLE_OPEN_INITIALIZER || group == PARSE_ROLE_OPEN_COMPOUND_BODY)
    {
        if (kind == PARSE_KIND_L_BRACE)
        {
            parse_scalar_open(parser, i, PARSE_ROLE_OPEN_INITIALIZER, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, false, step);
        }
        else if (kind == PARSE_KIND_PERIOD)
        {
            step->role = PARSE_ROLE_MEMBER_OPERATOR;
            frame->mode = PARSE_MODE_MEMBER;
            frame->statement = PARSE_ITEM_DESIGNATOR;
        }
        else if (kind == PARSE_KIND_L_SQUARE)
        {
            parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_MODE_OPERAND, PARSE_MODE_OPERATOR, false, step);
            frame->statement = PARSE_ITEM_ARRAY_DESIGNATOR;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && parser->kinds[i + 1] == PARSE_KIND_COLON)
        {
            // GNU's old-style field designator names a member, even if an outer scope declares a typedef of that name.
            step->role = PARSE_ROLE_MEMBER;
            frame->mode = PARSE_MODE_OPERATOR;
            frame->statement = PARSE_ITEM_OLD_STYLE_DESIGNATOR;
        }
        else if (kind == PARSE_KIND_R_BRACE)
        {
            parse_scalar_close(parser, i, step);
        }
        else
        {
            frame->statement = PARSE_ITEM_EXPRESSION;
            parse_scalar_begin_expression(frame, false, false, step);
        }
    }
    else if (group == PARSE_ROLE_OPEN_KEYWORD)
    {
        uint32_t most;
        uint32_t least;
        const uint8_t argument = parse_keyword_argument(parser->names[(int64_t)frame->opener - 1], frame->keyword, frame->argument, &most, &least);
        const bool type = parse_scalar_starts_type(parser, i);
        if (frame->keyword == PARSE_KIND_TYPE_ARGUMENT && parser->names[(int64_t)frame->opener - 1] == C_NAME_BUILTIN_AVAILABLE)
        {
            // __builtin_available's specs, which are no expressions (#148).
            frame->mode = PARSE_MODE_AVAILABILITY_SPEC;
            step->again = true;
        }
        else if (kind == PARSE_KIND_R_PAREN && frame->argument == 0 && least == 0)
        {
            parse_scalar_close(parser, i, step);
        }
        else if (frame->argument >= most)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
        }
        else if (frame->keyword == PARSE_KIND_GENERIC && frame->argument != 0 && kind == PARSE_KIND_DEFAULT)
        {
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_ASSOCIATION;
        }
        else if (frame->keyword == PARSE_KIND_STATIC_ASSERT && frame->argument == 1)
        {
            // The message: string literals only.
            if (kind == PARSE_KIND_STRING)
            {
                step->role = PARSE_ROLE_OPERAND;
                frame->mode = PARSE_MODE_MESSAGE;
            }
            else
            {
                parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_STRING, BUSTER_PARSE_AT_TOKEN);
            }
        }
        else if (frame->argument == 1 && parser->names[(int64_t)frame->opener - 1] == C_NAME_BUILTIN_OFFSETOF)
        {
            // The member designator, which clang reads as its own grammar: names and subscripts, no expression.
            frame->mode = PARSE_MODE_OFFSETOF_MEMBER;
            step->again = true;
        }
        else if (argument != BUSTER_PARSE_ARGUMENT_EXPRESSION && type)
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_NO_BIND, step);
        }
        else if (argument == BUSTER_PARSE_ARGUMENT_TYPE)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_TYPE, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            parse_scalar_begin_expression(frame, false, frame->keyword == PARSE_KIND_STATIC_ASSERT, step);
        }
    }
    else if (group == PARSE_ROLE_OPEN_SIZEOF || group == PARSE_ROLE_OPEN_SPECIFIER)
    {
        // `_Atomic(` takes a type name, `_BitInt(` and `_ExtInt(` a constant, `typeof(`, `_Alignas(` and `sizeof(` either.
        const IdentId keyword = parser->names[(int64_t)frame->opener - 1];
        const bool bit_int = keyword == C_NAME_BITINT || keyword == C_NAME_EXTINT;
        const bool type = parse_scalar_starts_type(parser, i) && !bit_int;
        if (type)
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_NO_BIND, step);
        }
        else if (frame->keyword == PARSE_KIND_ATOMIC)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_TYPE, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            parse_scalar_begin_expression(frame, false, frame->keyword == PARSE_KIND_ALIGNAS || bit_int, step);
        }
    }
    else if (group == PARSE_ROLE_OPEN_FUNCTION)
    {
        if (kind == PARSE_KIND_ELLIPSIS && frame->argument != 0)
        {
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_CLOSE;
        }
        else if (kind == PARSE_KIND_ELLIPSIS)
        {
            // C23's `f(...)`, which clang takes in every mode with a warning before C23.
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_CLOSE;
        }
        else if (kind == PARSE_KIND_R_PAREN && frame->argument == 0)
        {
            parse_scalar_close(parser, i, step);
        }
        else if ((kind != PARSE_KIND_IDENTIFIER || parse_scalar_is_typedef_name(parser, i)) && (frame->flags & BUSTER_PARSE_FRAME_KR))
        {
            // A K&R identifier list holds identifiers only.
            parse_scalar_error_argument(parser, i, kind == PARSE_KIND_IDENTIFIER ? PARSE_MESSAGE_UNEXPECTED_TYPE_NAME : PARSE_MESSAGE_EXPECTED_IDENTIFIER,
                                        BUSTER_PARSE_AT_TOKEN, i);
        }
        else if (parse_scalar_starts_declaration(parser, i, false) || parse_scalar_standard_attribute(parser, i))
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_OPTIONAL, step);
        }
        else if (kind == PARSE_KIND_IDENTIFIER && ((frame->flags & BUSTER_PARSE_FRAME_KR) ||
                                                   (frame->argument == 0 && parser->language.standard < 2023 &&
                                                    (parser->kinds[i + 1] == PARSE_KIND_COMMA || parser->kinds[i + 1] == PARSE_KIND_R_PAREN))))
        {
            // A K&R identifier list, which C23 removed (#154 takes them in C17 and before): clang reads one only if its
            // first identifier is followed by `,` or `)` (isFunctionDeclaratorIdentifierList). Its names are bound in its
            // scope, so that one may come once.
            if (parser->meanings[parser->names[i]] == PARSE_MEANING_KR_LISTED)
            {
                parse_scalar_error_argument(parser, i, PARSE_MESSAGE_PARAMETER_REDEFINITION, BUSTER_PARSE_AT_TOKEN, i);
            }
            parse_scalar_bind(parser, i, PARSE_MEANING_KR_LISTED, C_BINDING_OBJECT);
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_IDENTIFIER_LIST;
            frame->flags |= BUSTER_PARSE_FRAME_KR;
        }
        else if (kind == PARSE_KIND_IDENTIFIER)
        {
            // A parameter whose type is missing or unknown (parse_scalar_specifiers).
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_OPTIONAL, step);
        }
        else if (kind == PARSE_KIND_STAR || kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE)
        {
            // A parameter's declarator without specifiers: an implicit int, which C99 and later make an error at its
            // name, or where the name would be (#1127), as at file scope.
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_OPTIONAL | BUSTER_PARSE_FRAME_SPECIFIED, step);
        }
        else
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_PARAMETER, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (group == PARSE_ROLE_OPEN_FOR)
    {
        if (kind == PARSE_KIND_SEMI)
        {
            parse_scalar_terminator(parser, i, frame, step);
        }
        else if (parse_scalar_starts_declaration(parser, i, false) || parse_scalar_standard_attribute(parser, i))
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED | BUSTER_PARSE_FRAME_FOR_DECLARATION, step);
        }
        else
        {
            parse_scalar_begin_expression(frame, false, false, step);
        }
    }
    else if (group == PARSE_ROLE_OPEN_STRUCT)
    {
        if (kind == PARSE_KIND_SEMI)
        {
            parse_scalar_terminator(parser, i, frame, step);
        }
        else if (kind == PARSE_KIND_R_BRACE)
        {
            parse_scalar_close(parser, i, step);
        }
        else if (kind == PARSE_KIND_STATIC_ASSERT)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_KEYWORD_PAREN;
        }
        else if (parse_scalar_starts_declaration(parser, i, false) || parse_scalar_standard_attribute(parser, i) || kind == PARSE_KIND_IDENTIFIER)
        {
            // An identifier that isn't a type name is an error the specifiers report (parse_scalar_specifiers).
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED, step);
        }
        else
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_TYPE_NAME_REQUIRES_SPECIFIER, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (group == PARSE_ROLE_NONE)
    {
        // File scope.
        if (kind == PARSE_KIND_SEMI)
        {
            parse_scalar_terminator(parser, i, frame, step);
        }
        else if (parse_kind_is_closer(kind))
        {
            parse_scalar_close(parser, i, step);
        }
        else if (kind == PARSE_KIND_ASM)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_ASM_PAREN;
            frame->statement = PARSE_ITEM_TOP_LEVEL_ASM;
            frame->asm_qualifiers = 0;
        }
        else if (kind == PARSE_KIND_STATIC_ASSERT)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_KEYWORD_PAREN;
        }
        else if (kind == PARSE_KIND_EXTENSION && parse_scalar_extension_assertion(parser, i))
        {
            // An `__extension__` before a static assertion (#1447), which clang takes: a marker of its statement.
            step->role = PARSE_ROLE_SPECIFIER;
        }
        else if (parse_scalar_starts_declaration(parser, i, false) || parse_scalar_standard_attribute(parser, i))
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED, step);
        }
        else if (kind == PARSE_KIND_IDENTIFIER || kind == PARSE_KIND_EXTENSION)
        {
            // A declaration without specifiers (or with `__extension__` alone): an implicit int, or an unknown type
            // name (parse_scalar_specifiers).
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED, step);
        }
        else if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_STAR)
        {
            // A declarator without specifiers: an implicit int, which C99 and later make an error at its name.
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED | BUSTER_PARSE_FRAME_SPECIFIED, step);
        }
        else
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (block)
    {
        // A statement or a declaration. What the statement stack waits for decides what may come: after a head, a
        // statement (no declaration); after a do's body, its while.
        if (kind != PARSE_KIND_ELSE)
        {
            parse_statement_flush(parser);
        }
        const ParseStatement* top = parse_statement_top(parser);
        const bool waiting = top != 0 && top->state == PARSE_STATEMENT_WAITING;
        if (parser->binding_trace != 0 && waiting && top->scope != BUSTER_PARSE_NONE)
        {
            // A selection/iteration's substatement has its own block scope even without braces.
            parser->binding_trace->statement_scopes[parser->statement_count - 1] = i + 1;
        }
        // A label may end a block or come before a declaration (C23's, which clang takes in every mode).
        const bool labeled = waiting && (top->kind == PARSE_KIND_COLON || top->kind == PARSE_KIND_CASE);
        const bool started = (frame->flags & BUSTER_PARSE_FRAME_STARTED) != 0;
        frame->flags |= kind != PARSE_KIND_LABEL ? BUSTER_PARSE_FRAME_STARTED : 0;
        if (top != 0 && top->state == PARSE_STATEMENT_DO_DONE && kind != PARSE_KIND_WHILE)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_WHILE, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_SEMI)
        {
            parse_scalar_terminator(parser, i, frame, step);
        }
        else if (kind == PARSE_KIND_R_BRACE && waiting && !labeled)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_STATEMENT, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_R_BRACE)
        {
            parse_scalar_close(parser, i, step);
        }
        else if (kind == PARSE_KIND_R_PAREN && parser->roles[(int64_t)i - 1] == PARSE_ROLE_CLOSE_CONDITION)
        {
            // A `)` after an if's, a while's or a switch's condition, where its statement would start.
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXTRANEOUS_AFTER_CONDITION, BUSTER_PARSE_AT_TOKEN);
        }
        else if (parse_kind_is_closer(kind))
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_L_BRACE)
        {
            ParseFrame* inner = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_BLOCK, PARSE_MODE_ITEM, PARSE_MODE_ITEM, true, step);
            inner->flags |= BUSTER_PARSE_FRAME_IN_FUNCTION;
            step->event = PARSE_EVENT_OPEN_STATEMENT_BLOCK;
        }
        else if (kind == PARSE_KIND_IF || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_SWITCH)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_CONDITION_PAREN;
        }
        else if (kind == PARSE_KIND_FOR)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_FOR_PAREN;
        }
        else if (kind == PARSE_KIND_DO)
        {
            step->role = PARSE_ROLE_DO;
            step->event = PARSE_EVENT_HEAD;
            step->keyword = PARSE_KIND_DO;
            step->scope = parser->log_count;
        }
        else if (kind == PARSE_KIND_ELSE)
        {
            step->role = PARSE_ROLE_ELSE;
            step->event = PARSE_EVENT_ELSE;
        }
        else if (kind == PARSE_KIND_CASE)
        {
            ParseFrame* label = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CASE, PARSE_MODE_OPERAND, PARSE_MODE_ITEM, false, step);
            label->flags |= BUSTER_PARSE_FRAME_CONDITIONAL;
        }
        else if (kind == PARSE_KIND_DEFAULT)
        {
            parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CASE, PARSE_MODE_DEFAULT, PARSE_MODE_ITEM, false, step);
        }
        else if (kind == PARSE_KIND_RETURN)
        {
            step->role = PARSE_ROLE_RETURN;
            frame->mode = PARSE_MODE_OPERAND;
            frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_ITEM_STATE) | BUSTER_PARSE_FRAME_EMPTY_OK;
            frame->statement = PARSE_ITEM_RETURN;
        }
        else if (kind == PARSE_KIND_GOTO)
        {
            step->role = PARSE_ROLE_RETURN;
            frame->mode = PARSE_MODE_GOTO;
            frame->statement = PARSE_ITEM_GOTO;
        }
        else if (kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE)
        {
            step->role = PARSE_ROLE_RETURN;
            frame->mode = PARSE_MODE_JUMP;
            frame->statement = kind == PARSE_KIND_BREAK ? PARSE_ITEM_BREAK : PARSE_ITEM_CONTINUE;
        }
        else if (kind == PARSE_KIND_ASM)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_ASM_PAREN;
            frame->statement = PARSE_ITEM_ASM;
            frame->asm_qualifiers = 0;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && parser->kinds[i + 1] == PARSE_KIND_COLON)
        {
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_LABEL;
        }
        else if (kind == PARSE_KIND_LABEL && (waiting || started))
        {
            // GNU's local labels are declared at the start of a block only.
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_LABEL)
        {
            step->role = PARSE_ROLE_SPECIFIER;
            frame->mode = PARSE_MODE_LOCAL_LABEL_NAME;
        }
        else if (kind == PARSE_KIND_ATTRIBUTE)
        {
            // At a block-item boundary the following token decides whether attributes introduce a declaration or a
            // statement. After a label they may precede the statement without a separating ';', and after a control
            // head they must not make a declaration into a statement. Keep the head waiting until that token arrives.
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
            frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
            frame->attribute_continuation = PARSE_MODE_BLOCK_ATTRIBUTE;
            frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_BLOCK_ATTRIBUTES) | BUSTER_PARSE_FRAME_GNU_ATTRIBUTE;
        }
        else if (parse_scalar_standard_attribute(parser, i) && parse_scalar_attributed_statement(parser, parse_scalar_after_attributes(parser, i)))
        {
            // C23 attributes before a statement's keyword, which clang reads as that statement's (#147's
            // [[clang::musttail]] return), or before what starts no declaration (an expression statement's `[[x]] y += 1;`,
            // #147's unknown attributes): read as GNU's are at a block item, the token after them deciding.
            parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER, PARSE_MODE_BLOCK_ATTRIBUTE, step);
            frame->flags &= ~BUSTER_PARSE_FRAME_BLOCK_ATTRIBUTES;
        }
        else if ((parse_scalar_starts_declaration(parser, i, true) || kind == PARSE_KIND_STATIC_ASSERT) && waiting && !labeled)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_STATIC_ASSERT)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_KEYWORD_PAREN;
        }
        else if (kind == PARSE_KIND_EXTENSION && !waiting && parse_scalar_extension_assertion(parser, i))
        {
            // An `__extension__` before a static assertion (#1447), which clang takes: a marker of its statement.
            step->role = PARSE_ROLE_SPECIFIER;
        }
        else if (parse_scalar_starts_declaration(parser, i, true) || parse_scalar_standard_attribute(parser, i))
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED, step);
        }
        else
        {
            frame->statement = PARSE_ITEM_EXPRESSION;
            parse_scalar_begin_expression(frame, false, false, step);
        }
    }
    else
    {
        parse_scalar_begin_expression(frame, false, false, step);
    }
}

// The first of the GNU attributes right before `i`, back over their groups' parentheses.
static uint32_t parse_scalar_first_gnu_attribute(const ParseScalar* parser, uint32_t i)
{
    uint32_t result = i;
    int64_t before = (int64_t)i - 1;
    bool going = true;
    while (going && before > 0 && parser->kinds[before] == PARSE_KIND_R_PAREN)
    {
        uint32_t open = 0;
        do
        {
            open += parser->kinds[before] == PARSE_KIND_R_PAREN;
            open -= parser->kinds[before] == PARSE_KIND_L_PAREN;
            before -= 1;
        } while (before > 0 && open != 0);
        going = parser->kinds[before] == PARSE_KIND_ATTRIBUTE;
        result = going ? (uint32_t)before : result;
        before -= 1;
    }
    return result;
}

// Whether identifier `i`, after the GNU attributes that start a block item's declaration, is clang's "expected identifier
// or '('" (#147): clang's statement reader asks Sema what the name is first (TryAnnotateName), and a name of no type
// (an object, a function, an enumerator, a parameter, or before C23 one it declares implicitly as a function before a
// `(`) is an annotation token no declarator starts with. A name of no ordinary binding and no `(` after it that a tag in scope has
// is clang's "must use 'struct' tag to refer to type 'x'" (its isTagTypeWithMissingTag), the tag's keyword named. Any
// other name of no binding of the parse is an implicit int's declarator, unless a file-scope declaration before the
// tokens names it (ParseScalarResult.undecided). Returns the error's message, or PARSE_MESSAGE_NONE. Clang's typo
// correction and builtins' names without a call aren't modelled.
static uint8_t parse_scalar_attribute_name(ParseScalar* parser, uint32_t i)
{
    const uint32_t name = parser->names[i];
    const uint8_t meaning = parser->meanings[name];
    const uint8_t tag = parser->tags[name];
    const bool bound = meaning != PARSE_MEANING_ORDINARY && meaning != PARSE_MEANING_TYPEDEF;
    // C23 has no implicit function declarations: a `(` decides nothing there.
    const bool implicit = parser->kinds[i + 1] == PARSE_KIND_L_PAREN && parser->language.standard < 2023;
    const uint8_t result = bound || implicit ? PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN :
                           tag == BUSTER_PARSE_TAG_STRUCT ? PARSE_MESSAGE_MUST_USE_STRUCT_TAG :
                           tag == BUSTER_PARSE_TAG_UNION ? PARSE_MESSAGE_MUST_USE_UNION_TAG :
                           tag == BUSTER_PARSE_TAG_ENUM ? PARSE_MESSAGE_MUST_USE_ENUM_TAG : PARSE_MESSAGE_NONE;
    parser->undecided |= result == PARSE_MESSAGE_NONE;
    return result;
}

// The token after a block item's attributes (parse_scalar_block_attribute), which isn't another's: whether they start a
// declaration or the statement they are of. `head` after a control head or a label, `attributes` GNU ones that are each a
// statement's, `gnu` any GNU one. An asm statement's GNU attributes (not a label's) are clang's error at the first
// (ParseStatementOrDeclarationAfterAttributes's ProhibitAttributes, #147).
static void parse_scalar_block_attribute_end(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step, bool head, bool attributes, bool gnu)
{
    const uint8_t kind = parser->kinds[i];
    const ParseStatement* top = parse_statement_top(parser);
    const bool label = top != 0 && top->state == PARSE_STATEMENT_WAITING && top->kind == PARSE_KIND_COLON;
    const bool statement = kind == PARSE_KIND_IF || kind == PARSE_KIND_SWITCH || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_DO ||
                           kind == PARSE_KIND_FOR || kind == PARSE_KIND_GOTO || kind == PARSE_KIND_CONTINUE || kind == PARSE_KIND_BREAK ||
                           kind == PARSE_KIND_RETURN || kind == PARSE_KIND_CASE || kind == PARSE_KIND_DEFAULT || kind == PARSE_KIND_L_BRACE ||
                           kind == PARSE_KIND_SEMI || kind == PARSE_KIND_ASM ||
                           (kind == PARSE_KIND_IDENTIFIER && parser->kinds[i + 1] == PARSE_KIND_COLON);
    const bool expression = !gnu && kind != PARSE_KIND_R_BRACE && !parse_scalar_starts_declaration(parser, i, true);
    // A name after GNU attributes that start a declaration, which clang looks up first (parse_scalar_attribute_name).
    const uint8_t named = !head && !attributes && !statement && gnu && kind == PARSE_KIND_IDENTIFIER && !parse_scalar_is_typedef_name(parser, i) ?
                              parse_scalar_attribute_name(parser, i) : PARSE_MESSAGE_NONE;
    frame->flags &= ~BUSTER_PARSE_FRAME_BLOCK_ATTRIBUTES;
    if (kind == PARSE_KIND_ASM && gnu && !label)
    {
        parse_scalar_error(parser, parse_scalar_first_gnu_attribute(parser, i), PARSE_MESSAGE_ATTRIBUTE_LIST_NOT_ALLOWED, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_R_BRACE && ((!head && attributes) || !gnu))
    {
        // A statement's attributes before a block's end, which clang's statement reader expects a statement after (C23's
        // after a label's name too).
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_STATEMENT, BUSTER_PARSE_AT_TOKEN);
    }
    else if (named != PARSE_MESSAGE_NONE)
    {
        parse_scalar_error_argument(parser, i, named, BUSTER_PARSE_AT_TOKEN, named == PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN ? BUSTER_PARSE_NONE : i);
    }
    else if (!head && !attributes && !statement && !expression)
    {
        parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED, step);
        // An attribute has already started the declaration: an extension marker is not a specifier here.
        frame->mode = kind == PARSE_KIND_EXTENSION ? PARSE_MODE_DECLARATOR : frame->mode;
    }
    else if (kind == PARSE_KIND_EXTENSION)
    {
        frame->statement = PARSE_ITEM_EXPRESSION;
        parse_scalar_begin_expression(frame, false, false, step);
    }
    else
    {
        frame->mode = PARSE_MODE_ITEM;
        step->again = true;
    }
}

// Clang's statement reader distinguishes `__attribute__((noinline)) f();` from a declaration beginning with other
// attributes. Statement keywords and labels decide directly; after a control head only a statement is permitted.
// A named label consumes its own attributes before its statement. None of these places uses a compound body's
// special handling of `__extension__` before a declaration: there it is an expression prefix.
static void parse_scalar_block_attribute(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    if (kind == PARSE_KIND_ATTRIBUTE)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
        frame->attribute_continuation = PARSE_MODE_BLOCK_ATTRIBUTE;
        frame->flags |= BUSTER_PARSE_FRAME_GNU_ATTRIBUTE;
    }
    else
    {
        const ParseStatement* top = parse_statement_top(parser);
        const bool waiting = top != 0 && top->state == PARSE_STATEMENT_WAITING;
        const bool head = waiting && top->kind != PARSE_KIND_CASE;
        const bool attributes = (frame->flags & BUSTER_PARSE_FRAME_STATEMENT_ATTRIBUTE) != 0 &&
                                (frame->flags & BUSTER_PARSE_FRAME_DECLARATION_ATTRIBUTE) == 0;
        // GNU's attributes decide by their names (clang: a declaration's unless each is a statement's); C23's alone by the
        // token after them, which starts an expression statement where it starts no declaration.
        const bool gnu = (frame->flags & BUSTER_PARSE_FRAME_GNU_ATTRIBUTE) != 0;
        const bool label = waiting && top->kind == PARSE_KIND_COLON;
        if (parse_scalar_standard_attribute(parser, i) && gnu && !label)
        {
            // C23 attributes after GNU ones, which clang reads no further: an expression's `[[` after a control head or
            // where the GNU ones are a statement's, and a declaration's misplaced list where they are a declaration's.
            // After a label's name GNU's are the label's, and C23's then the statement's.
            parse_scalar_error(parser, i, waiting || attributes ? PARSE_MESSAGE_EXPECTED_EXPRESSION : PARSE_MESSAGE_ATTRIBUTE_LIST_NOT_ALLOWED,
                               BUSTER_PARSE_AT_TOKEN);
        }
        else if (parse_scalar_standard_attribute(parser, i) && parse_scalar_attributed_statement(parser, parse_scalar_after_attributes(parser, i)))
        {
            parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER, PARSE_MODE_BLOCK_ATTRIBUTE, step);
        }
        else
        {
            parse_scalar_block_attribute_end(parser, i, frame, step, head, attributes, gnu);
        }
    }
}

// The place of a declaration's missing type, `token`, where a declarator without a name would have had it, if the
// declaration has no type and no place for its error yet (parse_scalar_complete_declarator reports it there).
BUSTER_INLINE void parse_scalar_typeless_place(ParseScalar* parser, uint32_t token)
{
    ParseFrame* declaration = parse_scalar_declaration_frame(parser, parser->frame_count - 1);
    if (declaration->flags & BUSTER_PARSE_FRAME_TYPELESS)
    {
        declaration->typeless_name = token + 1;
        declaration->flags &= ~BUSTER_PARSE_FRAME_TYPELESS;
    }
}

// A declaration's specifiers have ended at `i`: a declarator, or the declaration's end, comes. A declaration without a
// type specifier has an implicit int, which C99 and later make an error (but C23's `auto`, which infers the type).
BUSTER_INLINE void parse_scalar_end_specifiers(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint32_t flags = frame->flags;
    const bool typeless = (flags & BUSTER_PARSE_FRAME_TYPE_SEEN) == 0 && (flags & BUSTER_PARSE_FRAME_SPECIFIED) != 0;
    const bool inferred = (flags & BUSTER_PARSE_FRAME_AUTO) != 0 && parser->language.standard >= 2023;
    frame->flags |= typeless && !inferred && parser->language.standard >= 1999 ? BUSTER_PARSE_FRAME_TYPELESS : 0;
    frame->mode = PARSE_MODE_DECLARATOR;
    step->again = true;
}

// Whether token `i` may follow a declarator's name right after the specifiers, as clang's
// isValidAfterIdentifierInDeclarator has it: what makes an identifier that isn't a type name the declarator's name.
BUSTER_INLINE bool parse_scalar_valid_after_name(const ParseScalar* parser, uint32_t i)
{
    const uint8_t kind = parser->kinds[i];
    return kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_SEMI || kind == PARSE_KIND_COMMA ||
           kind == PARSE_KIND_EQUAL || kind == PARSE_KIND_ASM || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_COLON;
}

// Whether token `i` may follow a struct's, union's or enum's body: what may follow a type specifier, as clang's
// isValidAfterTypeSpecifier has it. A qualifier or a storage class followed by a type specifier is taken for the next
// declaration's, after a missing `;`.
static bool parse_scalar_valid_after_tag(const ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t next = parser->kinds[i + 1];
    const bool next_type = next == PARSE_KIND_TYPE || next == PARSE_KIND_STRUCT || next == PARSE_KIND_ENUM || next == PARSE_KIND_TYPEOF || parse_scalar_is_typedef_name(parser, i + 1);
    bool result = false;
    switch (kind)
    {
        case PARSE_KIND_SEMI:
        case PARSE_KIND_STAR:
        case PARSE_KIND_AMPAMP:
        case PARSE_KIND_IDENTIFIER:
        case PARSE_KIND_R_PAREN:
        case PARSE_KIND_ELLIPSIS:
        case PARSE_KIND_COMMA:
        case PARSE_KIND_L_SQUARE:
        case PARSE_KIND_L_PAREN:
        case PARSE_KIND_ATTRIBUTE:
        // A struct's last member may go without its `;` in C: what follows decides (a member's missing name).
        case PARSE_KIND_R_BRACE: result = true; break;
        // A bit-field's width, or a _Generic association's `:` (clang's ColonIsSacred there).
        case PARSE_KIND_COLON: result = frame->role == PARSE_ROLE_OPEN_STRUCT || (frame->role == PARSE_ROLE_OPEN_KEYWORD && frame->keyword == PARSE_KIND_GENERIC); break;
        case PARSE_KIND_QUALIFIER:
        case PARSE_KIND_STORAGE:
        case PARSE_KIND_TYPEDEF:
        case PARSE_KIND_ATOMIC: result = !next_type; break;
        default: break;
    }
    return result;
}

// Whether token `i`, of kind PARSE_KIND_STORAGE or PARSE_KIND_TYPEDEF, is a storage class (not a function specifier or
// `__module_private__`).
BUSTER_INLINE bool parse_scalar_storage_class(const ParseScalar* parser, uint32_t i)
{
    const IdentId name = parser->names[i];
    return name != C_NAME_INLINE && name != C_NAME_GNU_INLINE && name != C_NAME_GNU_INLINE2 && name != C_NAME_NORETURN && name != C_NAME_MODULE_PRIVATE;
}

// Whether the token at `i` comes just after a file-scope declaration's leading run of attribute lists and `__extension__`s
// (BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES), which nothing else has followed.
static bool parse_scalar_leading_attributes(const ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    return (frame->flags & BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES) != 0 && i > 0 &&
           (parser->kinds[i - 1] == PARSE_KIND_EXTENSION || parser->roles[i - 1] == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER);
}

// The first token of the first attribute list of the leading run (parse_scalar_leading_attributes) before `i`, past its
// `__extension__`s: a GNU list's keyword or a C23 one's first `[`; `i` where the run has none.
static uint32_t parse_scalar_leading_list(const ParseScalar* parser, uint32_t i)
{
    uint32_t result = i;
    uint32_t at = i;
    while (at > 0 && parser->kinds[at - 1] == PARSE_KIND_EXTENSION)
    {
        at -= 1;
    }
    // Back over the lists, each from its closer to its opener: a GNU one's `))` to its keyword, a C23 one's `]]` to its `[[`.
    while (at > 0 && parser->roles[at - 1] == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER)
    {
        const uint8_t closer = parser->kinds[at - 1];
        const uint8_t opener = closer == PARSE_KIND_R_PAREN ? PARSE_KIND_L_PAREN : PARSE_KIND_L_SQUARE;
        uint32_t depth = 0;
        uint32_t token = at;
        do
        {
            token -= 1;
            depth += parser->kinds[token] == closer;
            depth -= parser->kinds[token] == opener;
        } while (token > 0 && depth != 0);
        at = closer == PARSE_KIND_R_PAREN && token > 0 && parser->kinds[token - 1] == PARSE_KIND_ATTRIBUTE ? token - 1 : token;
        result = at;
    }
    return result;
}

static void parse_scalar_specifiers(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t previous_role = i > 0 ? parser->roles[i - 1] : PARSE_ROLE_NONE;
    const bool new_line = (parser->flags[i] & BUSTER_PP_START_OF_LINE) != 0;
    bool after_tag = false;
    // The tag's closer (or the tag), before the attributes that may follow it.
    int64_t before_tag = (int64_t)i - 1;
    const bool tag_before = previous_role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER || previous_role == PARSE_ROLE_CLOSE_STRUCT ||
                            previous_role == PARSE_ROLE_CLOSE_ENUM || (previous_role == PARSE_ROLE_TAG && new_line);
    // A `#pragma pack` after the tag is one more thing clang lets follow it, after which the specifiers go on.
    const bool valid = !tag_before || (parser->flags[i] & BUSTER_PARSE_AFTER_PRAGMA_PACK) != 0 || parse_scalar_valid_after_tag(parser, i, frame);
    // After a tag's definition in a declaration (not a member's or a parameter's), a name that a name, `*`, `&` or `&&`
    // follows can't be the declarator: clang takes it for the next declaration's type, after a missing `;` (its
    // DiagnoseMissingSemiAfterTagDefinition, #1127).
    const uint8_t next = parser->kinds[i + 1];
    const bool not_declarator = tag_before && kind == PARSE_KIND_IDENTIFIER &&
                                (frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame) || frame->role == PARSE_ROLE_OPEN_FOR) &&
                                (next == PARSE_KIND_IDENTIFIER || next == PARSE_KIND_STAR || next == PARSE_KIND_AMP || next == PARSE_KIND_AMPAMP);
    if (tag_before && (!valid || not_declarator))
    {
        // The role before, past the attributes after a struct's body (`struct S { ... } __attribute__((packed))`), which
        // are the struct specifier's own. Only a token after a tag or an attribute that can't follow a tag looks, so
        // that the token after a run of attributes walks it, and none of the run's own.
        int64_t before = (int64_t)i - 1;
        while (before > 0 && parser->roles[before] == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER)
        {
            uint32_t open = 0;
            do
            {
                open += parser->kinds[before] == PARSE_KIND_R_PAREN;
                open -= parser->kinds[before] == PARSE_KIND_L_PAREN;
                before -= 1;
            } while (before > 0 && open != 0);
            // Now on the attribute's keyword: the token before it.
            before -= 1;
        }
        const uint8_t tag_role = before >= 0 ? parser->roles[before] : PARSE_ROLE_NONE;
        before_tag = before;
        after_tag = tag_role == PARSE_ROLE_CLOSE_STRUCT || tag_role == PARSE_ROLE_CLOSE_ENUM || (previous_role == PARSE_ROLE_TAG && new_line && !valid);
    }
    if (after_tag)
    {
        // A tag without a body, then what can't follow a type on the next line: clang takes it for a missing `;` too.
        // A struct's definition must be followed by what may follow a type: clang reads another type as a missing `;`,
        // after the attributes that follow its body, and a name that can't be the declarator, just after its body.
        const uint32_t opener = parser->frames[parser->frame_count - 1].tag_token;
        parse_scalar_error_argument(parser, valid ? (uint32_t)(before_tag + 1) : i, PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TAG, BUSTER_PARSE_AFTER_PREVIOUS, opener);
    }
    else if (kind == PARSE_KIND_TYPE)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_SPECIFIED;
    }
    else if (kind == PARSE_KIND_EXTENSION && ((frame->flags & (BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_TYPE_SEEN)) == 0 ||
                                              parse_scalar_leading_attributes(parser, i, frame)))
    {
        // `__extension__` starts a declaration, before its specifiers, or after a file-scope one's leading attributes.
        step->role = PARSE_ROLE_SPECIFIER;
    }
    else if (kind == PARSE_KIND_STATIC_ASSERT && frame->role == PARSE_ROLE_NONE &&
             ((frame->flags & (BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_TYPE_SEEN)) == 0 || parse_scalar_leading_attributes(parser, i, frame)) &&
             parse_scalar_leading_list(parser, i) != i)
    {
        // A static assertion after attribute lists at file scope: clang's error at the first of them.
        parse_scalar_error(parser, parse_scalar_leading_list(parser, i), PARSE_MESSAGE_ATTRIBUTE_LIST_NOT_ALLOWED, BUSTER_PARSE_AT_TOKEN);
    }
    else if ((kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_TYPEDEF) && (frame->flags & BUSTER_PARSE_FRAME_KR_DECLARATIONS) &&
             parse_scalar_storage_class(parser, i) && parser->names[i] != C_NAME_REGISTER)
    {
        // A K&R definition's parameter takes `register` only.
        parse_scalar_error(parser, i, PARSE_MESSAGE_PARAMETER_STORAGE_CLASS, BUSTER_PARSE_AT_TOKEN);
    }
    else if ((kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_TYPEDEF) && frame->role == PARSE_ROLE_OPEN_STRUCT)
    {
        // A member has no storage class, and no function specifier.
        const IdentId name = parser->names[i];
        const bool function = name == C_NAME_INLINE || name == C_NAME_GNU_INLINE || name == C_NAME_GNU_INLINE2 || name == C_NAME_NORETURN;
        parse_scalar_error(parser, i, function ? PARSE_MESSAGE_MEMBER_FUNCTION_SPECIFIER : PARSE_MESSAGE_MEMBER_STORAGE_CLASS, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_CALLING_CONVENTION)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_SPECIFIED | (parser->names[i] == C_NAME_AUTO ? BUSTER_PARSE_FRAME_AUTO : 0);
    }
    else if (kind == PARSE_KIND_TYPEDEF)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_SPECIFIED;
    }
    else if (kind == PARSE_KIND_ATOMIC && parser->kinds[i + 1] != PARSE_KIND_L_PAREN)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_SPECIFIED;
    }
    else if (kind == PARSE_KIND_ATOMIC || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ALIGNAS)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->flags |= (kind != PARSE_KIND_ALIGNAS ? BUSTER_PARSE_FRAME_TYPE_SEEN : 0) | BUSTER_PARSE_FRAME_SPECIFIED;
        frame->mode = PARSE_MODE_SPECIFIER_PAREN;
    }
    else if (kind == PARSE_KIND_ATTRIBUTE)
    {
        // GNU's attributes are among clang's declaration specifiers: specifiers of them alone have no type, an implicit
        // int, a member's too (#147).
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
        frame->attribute_continuation = PARSE_MODE_SPECIFIERS;
        // (A file-scope declaration's leading run, its first specifiers' or one's just after another list of it.)
        const bool leading = frame->role == PARSE_ROLE_NONE && (i == 0 || parser->kinds[i - 1] != PARSE_KIND_EXTENSION) &&
                             ((frame->flags & (BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_TYPE_SEEN)) == 0 ||
                              ((frame->flags & BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES) != 0 && previous_role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER));
        frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES) | BUSTER_PARSE_FRAME_SPECIFIED | (leading ? BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES : 0);
    }
    else if (parse_scalar_standard_attribute(parser, i) && frame->role == PARSE_ROLE_NONE && i > 0 && parser->kinds[i - 1] == PARSE_KIND_EXTENSION)
    {
        // C23's attributes just after a file-scope `__extension__`, which clang's declaration specifiers don't take.
        parse_scalar_error(parser, i, PARSE_MESSAGE_ATTRIBUTE_LIST_NOT_ALLOWED, BUSTER_PARSE_AT_TOKEN);
    }
    else if (parse_scalar_standard_attribute(parser, i))
    {
        // (A file-scope declaration's leading run, its first specifiers' or one's just after another list of it.)
        const bool leading = frame->role == PARSE_ROLE_NONE &&
                             ((frame->flags & (BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_TYPE_SEEN)) == 0 ||
                              ((frame->flags & BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES) != 0 && previous_role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER));
        frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES) | (leading ? BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES : 0);
        parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER, PARSE_MODE_SPECIFIERS, step);
    }
    else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->flags |= BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_SPECIFIED;
        frame->mode = PARSE_MODE_TAG;
        frame->tag_kind = kind;
        frame->tag_token = i;
    }
    else if (kind == PARSE_KIND_IDENTIFIER && parse_scalar_is_typedef_name(parser, i) && (frame->flags & BUSTER_PARSE_FRAME_TYPE_SEEN) == 0)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_SPECIFIED;
    }
    else if (kind == PARSE_KIND_IDENTIFIER && (frame->flags & (BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_ABSTRACT)) == 0)
    {
        // An identifier no typedef declares where the type would be, which clang's ParseImplicitInt decides by what
        // follows it: the declarator's name, with an implicit int, if what follows may follow a name; else an unknown
        // type name, an error after which clang takes it for the type. A parameter in C23, which has no implicit int, is
        // always the latter.
        const bool parameter = frame->role == PARSE_ROLE_OPEN_FUNCTION;
        // C23's auto already supplies the inferred type: its name may be followed by GNU attributes, including
        // the attributes before a direct initializer (#1252), rather than being mistaken for an unknown type.
        const bool inferred_attribute = parser->language.standard >= 2023 && (frame->flags & BUSTER_PARSE_FRAME_AUTO) != 0 &&
                                        parser->kinds[i + 1] == PARSE_KIND_ATTRIBUTE;
        if ((parameter && parser->language.standard >= 2023) || (!inferred_attribute && !parse_scalar_valid_after_name(parser, i + 1)))
        {
            const uint32_t before = parser->diagnostic_count;
            parse_scalar_note(parser, i, PARSE_MESSAGE_UNKNOWN_TYPE_NAME, i, true);
            if (parameter && parser->diagnostic_count > before)
            {
                parser->diagnostics[before].parameter_type = true;
            }
            step->role = PARSE_ROLE_SPECIFIER;
            frame->flags |= BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_SPECIFIED;
        }
        else if (frame->role == PARSE_ROLE_OPEN_STRUCT && (frame->flags & BUSTER_PARSE_FRAME_SPECIFIED) == 0)
        {
            // A member without a specifier or a qualifier.
            parse_scalar_error(parser, i, PARSE_MESSAGE_TYPE_NAME_REQUIRES_SPECIFIER, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            // An implicit int, which the declarator reports as it does after specifiers without a type.
            frame->flags |= BUSTER_PARSE_FRAME_SPECIFIED;
            parse_scalar_end_specifiers(parser, i, frame, step);
        }
    }
    else if (kind == PARSE_KIND_SEMI && (frame->flags & BUSTER_PARSE_FRAME_KR_DECLARATIONS))
    {
        // A K&R definition's parameter declaration declares a parameter (C 6.9.1p6).
        parse_scalar_error(parser, parser->kr_declaration, PARSE_MESSAGE_DECLARATION_WITHOUT_PARAMETER, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_SEMI && (frame->flags & BUSTER_PARSE_FRAME_NAME_REQUIRED))
    {
        // A declaration without declarators: `struct S;`, `struct S { ... };`, `int;`.
        parse_scalar_terminator(parser, i, frame, step);
    }
    else if (kind == PARSE_KIND_COLON && frame->role == PARSE_ROLE_OPEN_STRUCT)
    {
        // An unnamed bit-field, whose missing type is clang's at its `:`.
        parse_scalar_end_specifiers(parser, i, frame, step);
        parse_scalar_typeless_place(parser, i);
        frame->mode = PARSE_MODE_AFTER_DECLARATOR;
    }
    else if ((kind == PARSE_KIND_STAR || kind == PARSE_KIND_L_PAREN) && (frame->flags & (BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_TYPE_SEEN)) == 0 &&
             frame->role == PARSE_ROLE_OPEN_STRUCT)
    {
        // A member's declarator after C23 attributes alone, which clang's specifiers don't hold (#147), as after nothing.
        parse_scalar_error(parser, i, PARSE_MESSAGE_TYPE_NAME_REQUIRES_SPECIFIER, BUSTER_PARSE_AT_TOKEN);
    }
    else
    {
        // Another declarator after C23 attributes alone (`[[x]] *p;` at file scope) has an implicit int, as one after
        // nothing has (parse_scalar_item).
        frame->flags |= kind == PARSE_KIND_STAR || kind == PARSE_KIND_L_PAREN ? BUSTER_PARSE_FRAME_SPECIFIED : 0;
        parse_scalar_end_specifiers(parser, i, frame, step);
    }
}

// A tag's name at `token`, whose keyword frame `frame` read (#147, ParseScalar.tags): declared in the current scope where
// no tag of the name is in scope, or by its definition or a declaration of it alone (`struct x;`), and a use otherwise, as
// the binding trace takes it (parse_binding_trace_tag), whose undo-log entry of name zero for the declaration then keeps
// its scope; without the trace, an entry of its own. A tag in a definition's parameter list is in scope in its body too
// (parse_scalar_keep_parameters keeps it, parse_scalar_open_body binds it again); a K&R definition's declarations' tags
// end with them, as clang's do.
static void parse_scalar_bind_tag(ParseScalar* parser, uint32_t token, const ParseFrame* frame, bool definition, bool standalone)
{
    const uint32_t name = parser->names[token];
    const uint32_t before = parser->log_count;
    const bool declares = parser->tags[name] == 0 || definition || standalone;
    if (parser->binding_trace != 0)
    {
        parse_binding_trace_tag(parser, token, definition, standalone);
    }
    else if (declares)
    {
        parser->log_names[parser->log_count] = 0;
        parser->log_meanings[parser->log_count] = parser->meanings[0];
        parser->log_count += 1;
    }
    if (parser->log_count > before)
    {
        const uint32_t keyword = frame->tag_token;
        const uint8_t kind = frame->tag_kind == PARSE_KIND_ENUM ? BUSTER_PARSE_TAG_ENUM :
                             keyword < parser->count && parser->names[keyword] == C_NAME_UNION ? BUSTER_PARSE_TAG_UNION : BUSTER_PARSE_TAG_STRUCT;
        parser->tag_positions[parser->tag_count] = before;
        parser->tag_names[parser->tag_count] = name;
        parser->tag_hidden[parser->tag_count] = parser->tags[name];
        parser->tag_count += 1;
        parser->tags[name] = kind;
    }
}

static void parse_scalar_tag(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    // An enum's underlying type (C23's `enum E : int`), which a struct's bit-field width (`enum E : 3`) isn't.
    const bool base = kind == PARSE_KIND_COLON && frame->tag_kind == PARSE_KIND_ENUM && parse_scalar_starts_type(parser, i + 1);
    if (kind == PARSE_KIND_ATTRIBUTE)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT;
        frame->attribute_continuation = frame->mode;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
    }
    else if (parse_scalar_standard_attribute(parser, i))
    {
        parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT, frame->mode, step);
    }
    else if (kind == PARSE_KIND_IDENTIFIER && frame->mode == PARSE_MODE_TAG)
    {
        const uint8_t next = parser->kinds[i + 1];
        // C23 lists between a struct's or a union's name and its body (#2268), which clang's parser refuses and reads on
        // past as the tag's own (CheckMisplacedCXX11Attribute): the checker reports them.
        uint32_t listed = i + 1;
        while (frame->tag_kind == PARSE_KIND_STRUCT && listed + 1 < parser->count && parse_scalar_standard_attribute(parser, listed))
        {
            uint32_t depth = 0;
            bool inside = true;
            while (inside && listed < parser->count)
            {
                depth += parser->kinds[listed] == PARSE_KIND_L_SQUARE ? 1u : 0u;
                depth -= parser->kinds[listed] == PARSE_KIND_R_SQUARE && depth != 0 ? 1u : 0u;
                listed += 1;
                inside = depth != 0;
            }
        }
        const bool body = next == PARSE_KIND_L_BRACE || (next == PARSE_KIND_COLON && frame->tag_kind == PARSE_KIND_ENUM && parse_scalar_starts_type(parser, i + 2)) ||
                          (listed > i + 1 && listed < parser->count && parser->kinds[listed] == PARSE_KIND_L_BRACE);
        parse_scalar_bind_tag(parser, i, frame, body, next == PARSE_KIND_SEMI);
        step->role = body ? PARSE_ROLE_LEAF_RIGHT : PARSE_ROLE_TAG;
        frame->mode = body ? PARSE_MODE_TAG_BODY : PARSE_MODE_SPECIFIERS;
    }
    else if (kind == PARSE_KIND_L_BRACE)
    {
        const bool is_enum = frame->tag_kind == PARSE_KIND_ENUM;
        ParseFrame* body = parse_scalar_open(parser, i, is_enum ? PARSE_ROLE_OPEN_ENUM : PARSE_ROLE_OPEN_STRUCT, PARSE_MODE_ITEM, PARSE_MODE_SPECIFIERS, false, step);
        body->flags |= is_enum ? 0 : BUSTER_PARSE_FRAME_NO_BIND;
        body->tag_token = frame->tag_token;
    }
    else if (base)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->mode = PARSE_MODE_ENUM_BASE;
    }
    else if (frame->mode == PARSE_MODE_TAG && frame->tag_kind == PARSE_KIND_ENUM)
    {
        // An enum without a tag or a body: clang asks for either at the token.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_BRACE, BUSTER_PARSE_AT_TOKEN);
    }
    else if (frame->mode == PARSE_MODE_TAG && (parser->flags[i] & BUSTER_PP_START_OF_LINE) &&
             !((frame->role == PARSE_ROLE_OPEN_KEYWORD || frame->role == PARSE_ROLE_OPEN_SPECIFIER) &&
               (frame->keyword == PARSE_KIND_GENERIC || frame->keyword == PARSE_KIND_TYPE_ARGUMENT || frame->keyword == PARSE_KIND_ALIGNAS)) &&
             !parse_scalar_valid_after_tag(parser, i, frame))
    {
        // `struct` or `union` without a tag or a body, then a line that can't go on with it: clang takes it for a missing
        // `;` before it asks for the tag, but in the type names of _Generic, _Alignas and the builtins that take one.
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TAG, BUSTER_PARSE_AFTER_PREVIOUS, frame->tag_token);
    }
    else if (frame->mode == PARSE_MODE_TAG)
    {
        parse_scalar_error_argument(parser, frame->tag_token, PARSE_MESSAGE_ANONYMOUS_TAG_DECLARATION, BUSTER_PARSE_AT_TOKEN, frame->tag_token);
    }
    else
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_BRACE, BUSTER_PARSE_AT_TOKEN);
    }
}

// After an enum's `:`: its underlying type's specifiers and qualifiers, then its body or the declaration's end.
static void parse_scalar_enum_base(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    if (kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER || parse_scalar_is_typedef_name(parser, i) || (kind == PARSE_KIND_ATOMIC && parser->kinds[i + 1] != PARSE_KIND_L_PAREN))
    {
        step->role = PARSE_ROLE_SPECIFIER;
    }
    else if (kind == PARSE_KIND_ATTRIBUTE)
    {
        // GNU attributes among the base's specifiers, before, between or after them, which clang takes as the base type's:
        // a specifier's group, then the base again.
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
        frame->attribute_continuation = PARSE_MODE_ENUM_BASE;
    }
    else if (kind == PARSE_KIND_L_BRACE)
    {
        ParseFrame* body = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_ENUM, PARSE_MODE_ITEM, PARSE_MODE_SPECIFIERS, false, step);
        body->tag_token = frame->tag_token;
    }
    else
    {
        frame->mode = PARSE_MODE_SPECIFIERS;
        step->again = true;
    }
}

static void parse_scalar_declarator(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const bool after_star = (frame->flags & BUSTER_PARSE_FRAME_AFTER_STAR) != 0;
    const bool abstract = (frame->flags & (BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_NAME_OPTIONAL)) != 0;
    if (kind == PARSE_KIND_STAR)
    {
        step->role = PARSE_ROLE_PREFIX;
        frame->flags |= BUSTER_PARSE_FRAME_AFTER_STAR | BUSTER_PARSE_FRAME_POINTER;
    }
    else if ((kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_CALLING_CONVENTION || (kind == PARSE_KIND_ATOMIC && parser->kinds[i + 1] != PARSE_KIND_L_PAREN)) && after_star)
    {
        step->role = PARSE_ROLE_QUALIFIER_LEFT;
    }
    else if (kind == PARSE_KIND_CALLING_CONVENTION && parser->kinds[(int64_t)i - 1] != PARSE_KIND_COMMA)
    {
        // `void (__cdecl *f)(void)`: a calling convention before the declarator in its group. A later declarator doesn't
        // start with one (`int a, __cdecl b;`), which clang's declarator parser rejects as it would any other token (#147).
        step->role = PARSE_ROLE_SPECIFIER;
    }
    else if (kind == PARSE_KIND_ATTRIBUTE)
    {
        // After `*`, the attribute is the pointer's, as a qualifier is: its keyword and its closer both belong to the `*`
        // (a keyword that looked right would skip its group's closer, an operand, and its subtree wouldn't be a run).
        step->role = after_star ? PARSE_ROLE_QUALIFIER_LEFT : PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = after_star ? PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT : PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
        frame->attribute_continuation = PARSE_MODE_DECLARATOR;
    }
    else if (parse_scalar_standard_attribute(parser, i))
    {
        parse_scalar_standard_attribute_open(parser, i, after_star ? PARSE_ROLE_OPEN_ATTRIBUTE_LEFT : PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER, PARSE_MODE_DECLARATOR, step);
    }
    else if (kind == PARSE_KIND_L_PAREN)
    {
        // Where the declarator may be abstract, `(` after the specifiers or a `*` is a function's parameters if they
        // start (a type, `)` or `...`), else a group; where it needs a name, it is a group, whose name comes inside. GNU
        // attributes right after the `(` are read first, as clang's ParseParenDeclarator reads them, and the token after
        // them decides (`int (__attribute__((ms_abi)) *p)(int)` is a group, #147).
        const uint32_t first = parser->kinds[i + 1] == PARSE_KIND_ATTRIBUTE ? parse_scalar_after_attributes(parser, i + 1) : i + 1;
        const uint8_t next = first < parser->count ? parser->kinds[first] : PARSE_KIND_END;
        const bool function = abstract && (parse_scalar_starts_type(parser, first) || next == PARSE_KIND_R_PAREN || next == PARSE_KIND_ELLIPSIS ||
                                           parse_scalar_standard_attribute(parser, first));
        frame->flags &= ~BUSTER_PARSE_FRAME_AFTER_STAR;
        if (function)
        {
            // An abstract declarator's missing type is clang's where its name would be: inside the parentheses.
            parse_scalar_typeless_place(parser, i + 1);
            parse_scalar_open(parser, i, PARSE_ROLE_OPEN_FUNCTION, PARSE_MODE_ITEM, PARSE_MODE_AFTER_DECLARATOR, true, step);
        }
        else
        {
            const uint32_t inherited = frame->flags & BUSTER_PARSE_FRAME_DECLARATOR_STATE;
            ParseFrame* group = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_GROUP, PARSE_MODE_DECLARATOR, PARSE_MODE_AFTER_DECLARATOR, false, step);
            group->flags |= inherited;
        }
    }
    else if (kind == PARSE_KIND_IDENTIFIER && (frame->flags & BUSTER_PARSE_FRAME_ABSTRACT))
    {
        // A type name has no declarator name: clang expects what ends the type name instead.
        uint32_t f = parser->frame_count - 1;
        while (f > 0 && parser->frames[f].role == PARSE_ROLE_OPEN_GROUP && (parser->frames[f].flags & BUSTER_PARSE_FRAME_ABSTRACT))
        {
            f -= 1;
        }
        parse_scalar_unexpected(parser, i, &parser->frames[f]);
        // Clang consumes a stray declarator name in an attribute's sizeof/alignof type and still handles the outer
        // attribute. Retain the original error and opener note, and let the diagnostic-only view drop this one name.
        // No synthesized operand or type is introduced: the complete type and its original ')' remain in the view.
        bool attribute = false;
        for (uint32_t outer = f; !attribute && outer > 0; outer -= 1)
        {
            const ParseFrame* parent = parser->frames + outer - 1;
            attribute = parent->role == PARSE_ROLE_OPEN_CALL && parse_scalar_gnu_attribute_group(parser, parent);
        }
        if (attribute && parser->frames[f].role == PARSE_ROLE_OPEN_SIZEOF && parser->kinds[i + 1] == PARSE_KIND_R_PAREN &&
            parser->error_token == i && parser->error_message == PARSE_MESSAGE_EXPECTED_R_PAREN)
        {
            parser->error_resumed = true;
            parser->error_resume_delete = true;
            parser->error_resume_end = i + 1;
        }
    }
    else if (kind == PARSE_KIND_IDENTIFIER)
    {
        step->role = PARSE_ROLE_OPERAND;
        // The name is bound once its declarator is complete (parse_scalar_complete_declarator), and what the declarator
        // derives it as is decided as it goes on, from this frame out.
        ParseFrame* declaration = parse_scalar_declaration_frame(parser, parser->frame_count - 1);
        if (declaration->flags & BUSTER_PARSE_FRAME_TYPELESS)
        {
            // Syntax within a function declarator can fail before its missing type is diagnosed. Keep the original
            // name location until the complete declarator, including names nested inside groups.
            declaration->typeless_name = i + 1;
            declaration->flags &= ~BUSTER_PARSE_FRAME_TYPELESS;
        }
        declaration->name = parse_scalar_binds(parser) ? i : BUSTER_PARSE_NONE;
        if (parser->binding_trace != 0)
        {
            // Both ordinary and member declarator names must be excluded from expression-use recording.
            parser->binding_trace->namespaces[i] = C_BINDING_NAMESPACES;
        }
        declaration->name_meaning = (frame->flags & BUSTER_PARSE_FRAME_TYPEDEF) ? PARSE_MEANING_TYPEDEF : PARSE_MEANING_ORDINARY;
        declaration->derivation = PARSE_DERIVATION_PENDING;
        declaration->name_frame = parser->frame_count - 1;
        frame->flags &= ~BUSTER_PARSE_FRAME_AFTER_STAR;
        frame->mode = PARSE_MODE_AFTER_DECLARATOR;
    }
    else if (kind == PARSE_KIND_L_SQUARE && (frame->flags & BUSTER_PARSE_FRAME_NAME_REQUIRED) && frame->role != PARSE_ROLE_OPEN_STRUCT)
    {
        parse_scalar_misplaced_brackets(parser, i);
    }
    else if ((frame->flags & BUSTER_PARSE_FRAME_NAME_REQUIRED) && !(kind == PARSE_KIND_COLON && frame->role == PARSE_ROLE_OPEN_STRUCT))
    {
        parse_scalar_error(parser, i, frame->role == PARSE_ROLE_OPEN_STRUCT ? PARSE_MESSAGE_EXPECTED_MEMBER_NAME : PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else
    {
        // No name: an abstract declarator, whose missing type clang reports where the name would be.
        parse_scalar_typeless_place(parser, i);
        frame->flags &= ~BUSTER_PARSE_FRAME_AFTER_STAR;
        frame->mode = PARSE_MODE_AFTER_DECLARATOR;
        step->again = true;
    }
}

// A function's body opens at `i`, its scope beginning at `scope` (where a K&R definition's parameter declarations'
// began, which it ends): the parameters its declarator kept are bound again in it, as the body is their scope.
static void parse_scalar_open_body(ParseScalar* parser, uint32_t i, uint32_t scope, ParseStep* step)
{
    if (parser->binding_trace != NULL)
    {
        ParseBindingTrace* trace = parser->binding_trace;
        const bool valid = trace->body_count < trace->body_capacity && trace->pending_definition < parser->count;
        trace->bindings->valid &= valid;
        if (valid)
        {
            trace->body_declarations[trace->body_count++] = trace->pending_definition;
        }
        trace->pending_definition = BUSTER_PARSE_NONE;
    }
    if (parser->typeless_function != 0)
    {
        const uint8_t message = parser->language.standard >= 2023 ? PARSE_MESSAGE_TYPE_SPECIFIER_REQUIRED : PARSE_MESSAGE_TYPE_SPECIFIER_MISSING;
        parse_scalar_note(parser, parser->typeless_function - 1, message, BUSTER_PARSE_NONE, true);
        parser->typeless_function = 0;
    }
    if (parser->binding_trace != 0 && parser->body_scope != BUSTER_PARSE_NONE)
    {
        // K&R's typed parameter declarations replace the identifier-list declarations. Save the current heads
        // once, before retiring the declaration-list scope; a per-declaration parameter search would be quadratic.
        ParseBindingTrace* trace = parser->binding_trace;
        for (uint32_t k = 0; k < parser->kept_count && trace->bindings->valid; k += 1)
        {
            const uint32_t name = parser->kept_names[k];
            if (name != 0)
            {
                const uint32_t event = c_bindings_lookup(trace->bindings, C_BINDING_ORDINARY, name);
                trace->bindings->valid = event != 0;
                trace->kept_events[k] = event - 1;
            }
        }
    }
    parse_scalar_restore(parser, scope);
    ParseFrame* body = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_BODY, PARSE_MODE_ITEM, PARSE_MODE_ITEM, false, step);
    body->scope = scope;
    body->flags |= BUSTER_PARSE_FRAME_IN_FUNCTION;
    if (parser->binding_trace != 0)
    {
        parser->binding_trace->frame_scopes[parser->frame_count - 1] = i + 1;
    }
    for (uint32_t k = 0; k < parser->kept_count; k += 1)
    {
        parse_binding_trace_rebind(parser, k);
        const uint32_t entry = parser->log_count;
        parse_scalar_bind_name(parser, parser->kept_names[k], parser->kept_meanings[k]);
        // A tag the parameter list declared (parse_scalar_keep_parameters), in scope again with its entry.
        const uint8_t tag = parser->kept_tags[k];
        if (tag != 0)
        {
            const uint32_t name = parser->kept_tag_names[k];
            parser->tag_positions[parser->tag_count] = entry;
            parser->tag_names[parser->tag_count] = name;
            parser->tag_hidden[parser->tag_count] = parser->tags[name];
            parser->tag_count += 1;
            parser->tags[name] = tag;
        }
    }
    parse_binding_trace_labels(parser, i);
    parser->kept_count = 0;
    step->event = PARSE_EVENT_OPEN_BLOCK;
}

static void parse_scalar_after_declarator(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t group = frame->role;
    const bool abstract = (frame->flags & BUSTER_PARSE_FRAME_ABSTRACT) != 0;
    const bool definition_scope = group == PARSE_ROLE_NONE || parse_frame_is_block(frame);
    const bool kr = (frame->flags & BUSTER_PARSE_FRAME_KR_DECLARATIONS) != 0;
    // The frame the declarator's name belongs to, and whether this suffix is the first applied to the name, which
    // decides what the name is.
    ParseFrame* declaration = parse_scalar_declaration_frame(parser, parser->frame_count - 1);
    const bool first = declaration->derivation == PARSE_DERIVATION_PENDING && declaration->name_frame == parser->frame_count - 1;
    // After a GNU attribute (its `)`) the declarator has ended; after a C23 list (its `]`) it goes on, which C23's
    // direct-declarator allows after the name and each suffix (#147).
    const bool after_gnu = parser->roles[(int64_t)i - 1] == PARSE_ROLE_CLOSE_ATTRIBUTE && parser->kinds[(int64_t)i - 1] == PARSE_KIND_R_PAREN;
    if ((kind == PARSE_KIND_L_PAREN || (kind == PARSE_KIND_L_SQUARE && !parse_scalar_standard_attribute(parser, i))) && after_gnu &&
        parse_scalar_body_expected(parser, i, frame))
    {
        // The declarator ends at its attributes: clang asks for the body there (#1127).
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_FUNCTION_BODY, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_L_SQUARE && after_gnu)
    {
        // Nor does an array's suffix come after them, nor a C23 list: what may end the declaration is due there (#1127).
        parse_scalar_unexpected(parser, i, frame);
    }
    else if (kind == PARSE_KIND_L_PAREN && after_gnu && (group == PARSE_ROLE_NONE || parse_frame_is_block(frame) || group == PARSE_ROLE_OPEN_FOR) && !kr)
    {
        // After its attributes or its asm label, a declaration's `(` is no function's parameters: clang reads it as
        // C++'s direct initializer, `( expression-list )`, in C too (its InitKind::CXXDirect), with the name in scope.
        // The checker initializes from the first expression and still checks the remaining arguments (#1252).
        parse_scalar_complete_declarator(parser, declaration, false);
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_GROUP, PARSE_MODE_OPERAND, PARSE_MODE_OPERATOR, false, step);
        frame->statement = PARSE_ITEM_DIRECT_INITIALIZER;
    }
    else if (kind == PARSE_KIND_L_PAREN && after_gnu)
    {
        // Elsewhere (a member's, a parameter's), what ends the declarator is due there.
        parse_scalar_unexpected(parser, i, frame);
    }
    else if ((kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_ASM) && group == PARSE_ROLE_OPEN_GROUP)
    {
        // A declarator's parentheses end at its name and suffixes: clang takes no attributes after them in there.
        parse_scalar_close_expected(parser, i, PARSE_KIND_L_PAREN, true);
    }
    else if (kind == PARSE_KIND_L_PAREN)
    {
        // The name's own parameters, in a file or a block, are a definition's if a body follows the declarator (which a
        // K&R definition's parameter declarations never have).
        const bool definition = first && (declaration->role == PARSE_ROLE_NONE || parse_frame_is_block(declaration)) &&
                                (declaration->flags & BUSTER_PARSE_FRAME_KR_DECLARATIONS) == 0;
        declaration->derivation = first ? PARSE_DERIVATION_FUNCTION : declaration->derivation;
        // (A K&R flag stays: it is the declared name's own list's, which suffixes of its return type don't change.)
        ParseFrame* parameters = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_FUNCTION, PARSE_MODE_ITEM, PARSE_MODE_AFTER_DECLARATOR, true, step);
        parameters->flags |= definition ? BUSTER_PARSE_FRAME_DEFINITION_PARAMETERS : 0;
    }
    else if (kind == PARSE_KIND_L_SQUARE && !parse_scalar_standard_attribute(parser, i))
    {
        declaration->derivation = first ? PARSE_DERIVATION_OTHER : declaration->derivation;
        ParseFrame* array = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_MODE_OPERAND, PARSE_MODE_AFTER_DECLARATOR, false, step);
        array->flags |= BUSTER_PARSE_FRAME_ARRAY_DECLARATOR | BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_ATTRIBUTE && abstract && (group == PARSE_ROLE_OPEN_CAST || group == PARSE_ROLE_OPEN_SIZEOF || group == PARSE_ROLE_OPEN_COMPOUND ||
                                                          group == PARSE_ROLE_OPEN_KEYWORD || group == PARSE_ROLE_OPEN_SPECIFIER))
    {
        // A type name's declarator takes no attribute after it: its group's `)` is due there, as clang's parser has it
        // (#147). A parameter's does.
        parse_scalar_close_expected(parser, i, PARSE_KIND_L_PAREN, true);
    }
    else if (kind == PARSE_KIND_ATTRIBUTE || (kind == PARSE_KIND_ASM && definition_scope && !kr))
    {
        // An attribute after the declarator, or an asm label, which renames the declared symbol.
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE;
        frame->attribute_continuation = PARSE_MODE_AFTER_DECLARATOR;
    }
    else if (parse_scalar_standard_attribute(parser, i) && (declaration->flags & BUSTER_PARSE_FRAME_KR))
    {
        // After a K&R identifier list, clang takes no C23 attribute list.
        parse_scalar_error(parser, i, PARSE_MESSAGE_ATTRIBUTE_LIST_NOT_ALLOWED, BUSTER_PARSE_AT_TOKEN);
    }
    else if (parse_scalar_standard_attribute(parser, i) && after_gnu)
    {
        // Nor a C23 list after a GNU attribute, which clang's declarator has ended at.
        parse_scalar_unexpected(parser, i, frame);
    }
    else if (parse_scalar_standard_attribute(parser, i))
    {
        parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE, PARSE_MODE_AFTER_DECLARATOR, step);
    }
    else if (kr && kind != PARSE_KIND_COMMA && kind != PARSE_KIND_SEMI)
    {
        // A K&R definition's parameter declaration: its declarator is checked, then only `,` or `;` may come (no
        // initializer, asm label or body), as clang reads it.
        parse_scalar_complete_declarator(parser, declaration, false);
        const bool first_error = parser->error_token == BUSTER_PARSE_NONE;
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION, BUSTER_PARSE_AFTER_PREVIOUS);
        // Clang's K&R recovery keeps this parameter, then skips to its semicolon or the function body. Record only
        // a proven balanced tail; the diagnostic adapter reparses a separate view before any semantic walk uses it.
        uint32_t tail = i;
        bool balanced = true;
        while (first_error && balanced && tail < parser->count && parser->kinds[tail] != PARSE_KIND_SEMI && parser->kinds[tail] != PARSE_KIND_L_BRACE)
        {
            const uint8_t next = parser->kinds[tail];
            if (next == PARSE_KIND_L_PAREN || next == PARSE_KIND_L_SQUARE)
            {
                tail = parse_scalar_skip_group(parser, tail, &balanced);
            }
            else
            {
                balanced = !parse_kind_is_closer(next);
                tail += 1;
            }
        }
        if (first_error && balanced && tail < parser->count)
        {
            parser->error_resumed = true;
            parser->error_resume_end = tail + (parser->kinds[tail] == PARSE_KIND_SEMI);
        }
    }
    else if (kind == PARSE_KIND_EQUAL && group == PARSE_ROLE_OPEN_STRUCT)
    {
        // A member has no initializer in C. Diagnose the missing declaration-list terminator here rather than
        // consuming an initializer and reporting a later delimiter; vector validation already defers this case.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION_LIST, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (kind == PARSE_KIND_EQUAL && group == PARSE_ROLE_OPEN_GROUP)
    {
        // A declarator's group holds no initializer: clang closes the group at the `=` (#1127).
        parse_scalar_close_expected(parser, i, PARSE_KIND_L_PAREN, true);
    }
    else if (kind == PARSE_KIND_EQUAL && !abstract)
    {
        // The name is in scope in its initializer.
        parse_scalar_complete_declarator(parser, declaration, false);
        step->role = PARSE_ROLE_ASSIGN;
        frame->mode = PARSE_MODE_INITIALIZER;
    }
    else if (kind == PARSE_KIND_COMMA && parse_frame_comma_role(parser) == PARSE_ROLE_COMMA && (parser->flags[i + 1] & BUSTER_PP_START_OF_LINE) &&
             !parse_scalar_might_be_declarator(parser, i + 1, group == PARSE_ROLE_OPEN_STRUCT) && !kr)
    {
        // A comma, then a line break and what can't start a declarator: clang takes the comma for a `;`.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_COMMA && !(abstract && group != PARSE_ROLE_OPEN_KEYWORD) && group != PARSE_ROLE_OPEN_GROUP)
    {
        // (A declarator's group holds one declarator: a comma there is a missing `)`.)
        parse_scalar_complete_declarator(parser, declaration, false);
        step->role = parse_frame_comma_role(parser);
        frame->mode = step->role == PARSE_ROLE_COMMA ? PARSE_MODE_DECLARATOR : PARSE_MODE_ITEM;
        frame->flags &= ~(BUSTER_PARSE_FRAME_POINTER | BUSTER_PARSE_FRAME_AFTER_STAR | BUSTER_PARSE_FRAME_KR);
        frame->flags |= step->role == PARSE_ROLE_COMMA ? BUSTER_PARSE_FRAME_LATER_DECLARATOR : 0;
        if (step->role == PARSE_ROLE_SEPARATOR)
        {
            frame->flags &= ~(BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_AUTO);
            frame->argument += 1;
        }
        if (group == PARSE_ROLE_OPEN_KEYWORD && frame->keyword == PARSE_KIND_GENERIC)
        {
            // A _Generic association's type is followed by its `:`.
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AFTER_PREVIOUS);
        }
    }
    else if (kind == PARSE_KIND_SEMI)
    {
        // (In the declarator's parentheses, their missing `)` comes first, #1127.)
        if (group != PARSE_ROLE_OPEN_GROUP)
        {
            parse_scalar_complete_declarator(parser, declaration, false);
        }
        parse_scalar_terminator(parser, i, frame, step);
    }
    else if (kind == PARSE_KIND_COLON && group == PARSE_ROLE_OPEN_STRUCT)
    {
        // The member's declarator ends at its width (a missing type is reported at its name or at the `:`).
        parse_scalar_complete_declarator(parser, declaration, false);
        step->role = PARSE_ROLE_BITFIELD;
        frame->mode = PARSE_MODE_OPERAND;
        frame->flags |= BUSTER_PARSE_FRAME_CONDITIONAL;
    }
    else if (kind == PARSE_KIND_COLON && group == PARSE_ROLE_OPEN_KEYWORD && frame->keyword == PARSE_KIND_GENERIC)
    {
        step->role = PARSE_ROLE_ASSOCIATION;
        frame->mode = PARSE_MODE_OPERAND;
        frame->flags &= ~(BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_NO_BIND | BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_DECLARATION);
        frame->flags |= parser->frames[parser->frame_count - 2].flags & BUSTER_PARSE_FRAME_NO_BIND;
    }
    else if (kind == PARSE_KIND_L_BRACE && definition_scope && (frame->derivation == PARSE_DERIVATION_FUNCTION || (frame->flags & BUSTER_PARSE_FRAME_KR)) &&
             (frame->flags & BUSTER_PARSE_FRAME_LATER_DECLARATOR) == 0)
    {
        if (group != PARSE_ROLE_NONE)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_FUNCTION_DEFINITION_NOT_ALLOWED, BUSTER_PARSE_AT_TOKEN);
        }
        else if (frame->flags & BUSTER_PARSE_FRAME_TYPEDEF)
        {
            // Clang points at the storage-class specifier, not the body. Search only this declaration's file-scope
            // specifiers: the keyword may follow the type, or one inside an attribute may precede the real specifier.
            uint32_t at = parser->item_begin;
            while (at < i && (parser->kinds[at] != PARSE_KIND_TYPEDEF || parser->depths[at] != 0))
            {
                at += 1;
            }
            parse_scalar_error(parser, at < i ? at : i, PARSE_MESSAGE_FUNCTION_DEFINITION_TYPEDEF, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            parse_scalar_complete_declarator(parser, frame, true);
            parse_scalar_open_body(parser, i, parser->log_count, step);
            frame->flags &= ~BUSTER_PARSE_FRAME_ITEM_STATE;
        }
    }
    else if ((frame->flags & BUSTER_PARSE_FRAME_KR) && group == PARSE_ROLE_NONE && parse_scalar_starts_declaration(parser, i, false))
    {
        // A K&R definition's parameter declarations, before its body, in a scope of their own, in which its
        // identifier list's names are parameters to declare; the body binds them again (parse_scalar_open_body), as
        // ordinary identifiers, and nothing else the declarations declare.
        parse_scalar_complete_declarator(parser, frame, true);
        parser->body_scope = parser->log_count;
        for (uint32_t k = 0; k < parser->kept_count; k += 1)
        {
            parse_binding_trace_rebind(parser, k);
            parse_scalar_bind_name(parser, parser->kept_names[k], PARSE_MEANING_KR_PARAMETER);
        }
        frame->mode = PARSE_MODE_KR_PARAMETERS;
        step->again = true;
    }
    else
    {
        // What can't continue a declarator ends it, a closer too, and clang's Sema takes it (saying a missing type)
        // before the parser looks for the `,`, `;` or `)` due, but after it asks for a function's body, at the end of
        // the file too (ParseDeclGroup), and after a keyword it takes for a parameter's name (#1127). In the
        // declarator's own parentheses it hasn't ended: their `)` is due, and clang's parser says so first.
        if (group != PARSE_ROLE_OPEN_GROUP && !parse_scalar_fn_body_due(parser, i, frame) && !parse_scalar_keyword_parameter(parser, i, frame))
        {
            parse_scalar_typeless_note(parser, declaration);
        }
        if (parse_kind_is_closer(kind))
        {
            parse_scalar_close_item(parser, i, frame, step);
        }
        else
        {
            parse_scalar_unexpected(parser, i, frame);
        }
    }
}

// Between a K&R definition's `)` and its body: its parameters' declarations.
static void parse_scalar_kr_parameters(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    if (kind == PARSE_KIND_L_BRACE)
    {
        parse_scalar_open_body(parser, i, parser->body_scope, step);
        parser->body_scope = BUSTER_PARSE_NONE;
        frame->flags &= ~(BUSTER_PARSE_FRAME_ITEM_STATE | BUSTER_PARSE_FRAME_KR_DECLARATIONS);
    }
    else if (kind != PARSE_KIND_EXTENSION && parse_scalar_starts_declaration(parser, i, false))
    {
        // clang takes no `__extension__` here: what follows is a missing body.
        parser->kr_declaration = i;
        parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED | BUSTER_PARSE_FRAME_KR_DECLARATIONS, step);
    }
    else
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_FUNCTION_BODY, BUSTER_PARSE_AT_TOKEN);
    }
}

// How many colons an asm statement may have: 3 (outputs, inputs, clobbers), or 4 with `goto` among its qualifiers
// (labels); a top-level asm block, only its template.
static uint32_t parse_scalar_asm_sections(const ParseScalar* parser, const ParseFrame* frame)
{
    uint32_t result = parser->frames[parser->frame_count - 2].statement == PARSE_ITEM_TOP_LEVEL_ASM ? 0 : 3;
    for (int64_t t = (int64_t)frame->opener - 1; result != 0 && t >= 0 && parser->kinds[t] != PARSE_KIND_ASM; t -= 1)
    {
        result = parser->kinds[t] == PARSE_KIND_GOTO ? 4 : result;
    }
    return result;
}

// The GNU spellings Clang 23 recognizes as statement attributes. Only this syntactic distinction is made here;
// applicability, arguments and effects belong to semantic checking. No lookup is paid outside a block prefix.
static bool parse_scalar_statement_attribute(const ParseScalar* parser, IdentId name)
{
    static const char* const names[] =
    {
        "always_inline",
        "atomic",
        "assume",
        "code_align",
        "fallthrough",
        "musttail",
        "noinline",
        "nomerge",
        "no_outline",
        "opencl_unroll_hint",
        "suppress",
    };
    bool result = false;
    if (parser->table != 0 && name != 0 && name < parser->table->count)
    {
        const uint8_t* bytes = intern_bytes(parser->table, name);
        uint32_t length = intern_length(parser->table, name);
        const bool alternate = length >= 4 && bytes[0] == '_' && bytes[1] == '_' && bytes[length - 2] == '_' && bytes[length - 1] == '_';
        bytes += alternate ? 2 : 0;
        length -= alternate ? 4 : 0;
        for (uint32_t n = 0; !result && n < sizeof(names) / sizeof(names[0]); n += 1)
        {
            result = length == strlen(names[n]) && memcmp(bytes, names[n], length) == 0;
        }
    }
    return result;
}

// Whether an identifier's spelling, a keyword's surrounding underscores stripped where `strip` says, is `expected`.
static bool parse_scalar_attribute_spelled(const ParseScalar* parser, IdentId name, bool strip, const char* expected)
{
    bool result = false;
    if (parser->table != 0 && name != 0 && name < parser->table->count)
    {
        const uint8_t* bytes = intern_bytes(parser->table, name);
        uint32_t length = intern_length(parser->table, name);
        const bool alternate = strip && length >= 4 && bytes[0] == '_' && bytes[1] == '_' && bytes[length - 2] == '_' && bytes[length - 1] == '_';
        bytes += alternate ? 2 : 0;
        length -= alternate ? 4 : 0;
        result = length == strlen(expected) && memcmp(bytes, expected, length) == 0;
    }
    return result;
}

// The C23 spellings of clang 23's statement attributes (#147, its Attr.td's StmtAttr and DeclOrStmtAttr): what a block
// item's attributes must all be, GNU ones among them, for the item to be the statement after them (clang's
// ParseStatementOrDeclaration). A scope `0` for none; clang's scopes normalized (__gnu__ as gnu, _Clang as clang) and their
// names' and an unscoped one's keyword underscores stripped, as clang's normalizeAttrName does.
static bool parse_scalar_standard_statement_attribute(const ParseScalar* parser, IdentId scope, IdentId name)
{
    static const char* const clang_names[] = {
        "always_inline", "assume", "atomic", "code_align", "likely", "musttail", "no_outline", "noinline", "nomerge", "suppress", "unlikely",
    };
    static const char* const gnu_names[] = { "always_inline", "fallthrough", "noinline" };
    static const char* const msvc_names[] = { "forceinline", "forceinline_calls", "noinline" };
    const bool gnu = scope != 0 && (parse_scalar_attribute_spelled(parser, scope, false, "gnu") || parse_scalar_attribute_spelled(parser, scope, false, "__gnu__"));
    const bool clang = scope != 0 && (parse_scalar_attribute_spelled(parser, scope, false, "clang") || parse_scalar_attribute_spelled(parser, scope, false, "_Clang"));
    const bool msvc = scope != 0 && parse_scalar_attribute_spelled(parser, scope, false, "msvc");
    bool result = scope == 0 && parse_scalar_attribute_spelled(parser, name, true, "fallthrough");
    for (uint32_t n = 0; clang && !result && n < sizeof(clang_names) / sizeof(clang_names[0]); n += 1)
    {
        result = parse_scalar_attribute_spelled(parser, name, true, clang_names[n]);
    }
    for (uint32_t n = 0; gnu && !result && n < sizeof(gnu_names) / sizeof(gnu_names[0]); n += 1)
    {
        result = parse_scalar_attribute_spelled(parser, name, true, gnu_names[n]);
    }
    for (uint32_t n = 0; msvc && !result && n < sizeof(msvc_names) / sizeof(msvc_names[0]); n += 1)
    {
        result = parse_scalar_attribute_spelled(parser, name, false, msvc_names[n]);
    }
    return result;
}

// In an expression, where an operand comes next.
// clang's attributes whose argument is a type name (#147): its attributeIsTypeArgAttr, the attributes whose first argument
// Attr.td makes a TypeArgument, by their normalized name in any syntax, unknown ones included, which its
// ParseAttributeWithTypeArg reads with ParseTypeName, an expression and all.
static bool parse_scalar_type_argument_attribute(const ParseScalar* parser, IdentId name)
{
    static const char* const names[] = {
        "Owner", "Pointer", "contained_type", "iboutletcollection", "preferred_name", "preferred_type", "sycl_kernel_entry_point", "vec_type_hint",
    };
    bool result = false;
    for (uint32_t n = 0; !result && n < sizeof(names) / sizeof(names[0]); n += 1)
    {
        result = parse_scalar_attribute_spelled(parser, name, true, names[n]);
    }
    return result;
}

// Whether the typedef name at `token`, in the arguments of a counted_by family attribute (frame `frame`, a GNU attribute's
// call), names a member of a record being defined, which clang's lookup in the attribute's argument finds before the
// typedef (#1842): a declarator's name of its name in an enclosing struct or union body before it. A name is an identifier
// after a specifier, a typedef or tag name, `*`, `,`, a body's `}` or a declarator's grouping `(`, and before `;`, `,`, `:`, `[`,
// a grouping `)` or an attribute. Parentheses group a declarator after a specifier, a typedef or tag name, `*`, `,`, `}` or
// another grouping `(` (`int (n);`, `int ((n));`); any others (a function declarator's parameters, an attribute's
// arguments, sizeof's operand) hold no member's name. The bodies of named records nested in it hold their own records'
// members; each body's own scan finds the members of a named record still open.
// Which counted_by family attribute's argument group `frame` is (#1842): 1 to 4 for counted_by, sized_by,
// counted_by_or_null and sized_by_or_null (the order of their PARSE_MESSAGE_*_INTEGER), 0 for none.
static uint32_t parse_scalar_counted_attribute(const ParseScalar* parser, const ParseFrame* frame)
{
    static const char* const names[] = { "counted_by", "sized_by", "counted_by_or_null", "sized_by_or_null" };
    const bool counted = frame->role == PARSE_ROLE_OPEN_CALL && frame->opener > 0 && parse_scalar_gnu_attribute_group(parser, frame);
    uint32_t spelled = 0;
    for (uint32_t n = 0; counted && spelled == 0 && n < sizeof(names) / sizeof(names[0]); n += 1)
    {
        spelled = parse_scalar_attribute_spelled(parser, parser->names[frame->opener - 1], true, names[n]) ? n + 1 : 0;
    }
    return spelled;
}

static bool parse_scalar_counted_member(const ParseScalar* parser, const ParseFrame* frame, uint32_t token)
{
    const bool spelled = parse_scalar_counted_attribute(parser, frame) != 0;
    bool found = false;
    for (uint32_t f = 0; spelled && !found && f < parser->frame_count; f += 1)
    {
        const ParseFrame* body = parser->frames + f;
        // The depth of the parentheses open since the body's `{`, the depth of the first that groups no declarator (0
        // where none is open), and the depth of a named record's body the scan skips.
        uint32_t parentheses = 0;
        uint32_t other = 0;
        uint32_t named = 0;
        for (uint32_t t = body->role == PARSE_ROLE_OPEN_STRUCT ? body->opener + 1 : token; !found && t < token; t += 1)
        {
            const uint8_t kind = parser->kinds[t];
            const uint8_t before = parser->kinds[t - 1];
            const uint8_t after = parser->kinds[t + 1];
            const bool place = before == PARSE_KIND_TYPE || before == PARSE_KIND_QUALIFIER || before == PARSE_KIND_STAR || before == PARSE_KIND_COMMA ||
                               before == PARSE_KIND_R_BRACE ||
                               (before == PARSE_KIND_IDENTIFIER && (parse_scalar_is_typedef_name(parser, t - 1) || parser->kinds[t - 2] == PARSE_KIND_STRUCT ||
                                                                    parser->kinds[t - 2] == PARSE_KIND_ENUM));
            const bool opens = kind == PARSE_KIND_L_BRACE && (named != 0 || before == PARSE_KIND_IDENTIFIER);
            named = named + (opens ? 1u : 0u) - (named != 0 && kind == PARSE_KIND_R_BRACE ? 1u : 0u);
            const bool open = named == 0 && kind == PARSE_KIND_L_PAREN;
            const bool close = named == 0 && kind == PARSE_KIND_R_PAREN && parentheses != 0;
            // A `(` that groups: after a declarator's place or a grouping `(`.
            const bool grouping = open && other == 0 && (place || before == PARSE_KIND_L_PAREN);
            parentheses += open ? 1u : 0u;
            other = open && !grouping && other == 0 ? parentheses : other;
            const bool closes_other = close && other == parentheses;
            other = closes_other ? 0 : other;
            parentheses -= close ? 1u : 0u;
            const bool declarator = place || (before == PARSE_KIND_L_PAREN && other == 0);
            const bool ends = after == PARSE_KIND_SEMI || after == PARSE_KIND_COMMA || after == PARSE_KIND_COLON || after == PARSE_KIND_L_SQUARE ||
                              after == PARSE_KIND_ATTRIBUTE || (after == PARSE_KIND_R_PAREN && parentheses != 0);
            found = named == 0 && other == 0 && kind == PARSE_KIND_IDENTIFIER && parser->names[t] == parser->names[token] && declarator && ends;
        }
    }
    return found;
}

// clang's attributes whose arguments its parser reads its own way, as no expressions (#1447): availability's
// (ParseAvailabilityAttribute), external_source_symbol's (ParseExternalSourceSymbolAttribute) and type_tag_for_datatype's
// (ParseTypeTagForDatatypeAttribute: a kind, a type name and flags), objc_bridge_related's (ParseObjCBridgeRelatedAttribute:
// a class and selectors) and swift_newtype's and swift_wrapper's (ParseSwiftNewTypeAttribute: `struct` or `enum`), by their
// GNU names, a keyword's underscores too. Their arguments are a balanced run of tokens,
// which the checker reads as clang's parser does.
static bool parse_scalar_custom_attribute(const ParseScalar* parser, IdentId name)
{
    return parse_scalar_attribute_spelled(parser, name, true, "availability") || parse_scalar_attribute_spelled(parser, name, true, "external_source_symbol") ||
           parse_scalar_attribute_spelled(parser, name, true, "type_tag_for_datatype") ||
           parse_scalar_attribute_spelled(parser, name, true, "objc_bridge_related") || parse_scalar_attribute_spelled(parser, name, true, "swift_newtype") ||
           parse_scalar_attribute_spelled(parser, name, true, "swift_wrapper");
}

// An expression parse failure inside a GNU attribute can follow an earlier malformed string slot. Clang's
// ParseAttributeArgsCommon diagnoses that slot before parsing expressions (section(1,), for example). Only this
// already-failed path consults the shared generated argument/fact rows; ordinary attribute parsing pays no lookup.
static bool parse_scalar_attribute_string_error(ParseScalar* parser, uint32_t token, const ParseFrame* frame)
{
    typedef struct ParseAttributeStrings ParseAttributeStrings;
    struct ParseAttributeStrings
    {
        const char* name;
        uint32_t flags;
        uint32_t strings;
    };
    static const ParseAttributeStrings rows[] = {
#define BUSTER_CHECK_ARGUMENTS(name, flags, strings) { name, flags, strings },
#include "c_check_attribute_arguments.inc"
#undef BUSTER_CHECK_ARGUMENTS
    };
    static const char* const common[] = {
#define BUSTER_CHECK_SUBJECT_GNU(spelling, subjects, error, message)
#define BUSTER_CHECK_SUBJECT_C23(spelling, subjects, error, message)
#define BUSTER_CHECK_FACT_GNU(spelling, flags, minimum, maximum) ((flags) & 0x20u) == 0 ? spelling : NULL,
#define BUSTER_CHECK_FACT_C23(spelling, flags, minimum, maximum)
#define BUSTER_CHECK_EXCLUSION(first, second)
#include "c_check_attribute_subjects.inc"
#undef BUSTER_CHECK_EXCLUSION
#undef BUSTER_CHECK_FACT_C23
#undef BUSTER_CHECK_FACT_GNU
#undef BUSTER_CHECK_SUBJECT_C23
#undef BUSTER_CHECK_SUBJECT_GNU
    };
    const ParseFrame* arguments = frame;
    while (arguments > parser->frames && !(arguments->role == PARSE_ROLE_OPEN_CALL && parse_scalar_gnu_attribute_group(parser, arguments)))
    {
        arguments -= 1;
    }
    const uint32_t name_token = arguments > parser->frames ? arguments->opener - 1 : BUSTER_PARSE_NONE;
    const IdentId name = name_token != BUSTER_PARSE_NONE ? parser->names[name_token] : 0;
    const ParseAttributeStrings* row = NULL;
    for (uint32_t r = 0; name != 0 && row == NULL && r < sizeof(rows) / sizeof(rows[0]); r += 1)
    {
        // Variadic/strict identifier lists and type arguments use their own grammar, not string slots.
        if (rows[r].strings != 0 && (rows[r].flags & 0xeu) == 0 && parse_scalar_attribute_spelled(parser, name, true, rows[r].name))
        {
            row = rows + r;
        }
    }
    bool known = false;
    for (uint32_t r = 0; row != NULL && !known && r < sizeof(common) / sizeof(common[0]); r += 1)
    {
        known = common[r] != NULL && parse_scalar_attribute_spelled(parser, name, true, common[r]);
    }
    uint32_t first = known ? arguments->opener + 1 : token + 1;
    bool reported = false;
    for (uint32_t index = 0; !reported && first <= token; index += 1)
    {
        uint32_t end = first;
        uint32_t depth = 0;
        while (end < token && !(depth == 0 && (parser->kinds[end] == PARSE_KIND_COMMA || parser->kinds[end] == PARSE_KIND_R_PAREN)))
        {
            const uint8_t kind = parser->kinds[end];
            const bool open = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
            const bool close = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
            depth = open ? depth + 1 : close && depth != 0 ? depth - 1 : depth;
            end += 1;
        }
        const bool identifier = index == 0 && (row->flags & 0x1u) != 0 && first + 1 == end && parser->kinds[first] == PARSE_KIND_IDENTIFIER;
        const bool string = index < 32 && ((row->strings >> index) & 1u) != 0;
        if (string && !identifier && (first == end || parser->kinds[first] != PARSE_KIND_STRING))
        {
            parse_scalar_error_argument(parser, first, PARSE_MESSAGE_EXPECTED_ATTRIBUTE_STRING, BUSTER_PARSE_AT_TOKEN, name_token);
            reported = true;
        }
        first = end + 1;
    }
    return reported;
}

static void parse_scalar_operand(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint32_t flags = frame->flags;
    const bool attribute = (flags & BUSTER_PARSE_FRAME_ATTRIBUTE) != 0;
    const bool asm_operands = (flags & BUSTER_PARSE_FRAME_ASM) != 0;
    // An omitted array bound ends only at `]`, not at another closer or a statement terminator (#1253).
    const bool empty = (flags & BUSTER_PARSE_FRAME_EMPTY_OK) != 0 &&
                       ((flags & BUSTER_PARSE_FRAME_ARRAY_DECLARATOR) == 0 || kind == PARSE_KIND_R_SQUARE);
    const uint8_t previous = parser->kinds[(int64_t)i - 1];
    // A GNU attribute's type argument (parse_scalar_type_argument_attribute), where a type name starts it.
    const bool type_argument = frame->role == PARSE_ROLE_OPEN_CALL && i == frame->opener + 1 && parse_scalar_gnu_attribute_group(parser, frame) &&
                               parse_scalar_starts_type(parser, i) && parse_scalar_type_argument_attribute(parser, parser->names[(int64_t)frame->opener - 1]);
    if (type_argument)
    {
        parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_NO_BIND, step);
    }
    else if (asm_operands && i == frame->opener + 1 && kind != PARSE_KIND_STRING)
    {
        // An asm statement's template comes first.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_ASM_STRING, BUSTER_PARSE_AT_TOKEN);
    }
    else if (previous == PARSE_KIND_AMPAMP && parser->roles[(int64_t)i - 1] == PARSE_ROLE_PREFIX && kind != PARSE_KIND_IDENTIFIER)
    {
        // GNU's `&&label` takes a label's name.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
    }
    else if (asm_operands && frame->argument == 4 && kind == PARSE_KIND_IDENTIFIER)
    {
        // asm goto's labels, whatever the ordinary names mean.
        step->role = PARSE_ROLE_OPERAND;
        frame->mode = PARSE_MODE_OPERATOR;
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (asm_operands && frame->argument == 4)
    {
        // The labels' section has at least one.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
    }
    else if (asm_operands && frame->argument == 3 && previous != PARSE_KIND_COMMA && kind != PARSE_KIND_STRING && kind != PARSE_KIND_COLON &&
             kind != PARSE_KIND_R_PAREN)
    {
        // A clobbers' section that doesn't start with a string is empty, and the asm statement ends.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else if (asm_operands && frame->argument != 0 &&
             !(kind == PARSE_KIND_STRING || (kind == PARSE_KIND_L_SQUARE && frame->argument <= 2) ||
               ((kind == PARSE_KIND_COLON || kind == PARSE_KIND_R_PAREN) && previous != PARSE_KIND_COMMA)))
    {
        // An operand starts with its symbolic name or its constraint, and a clobber is a string, after a comma too.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_ASM_STRING, BUSTER_PARSE_AT_TOKEN);
    }
    else if (asm_operands && kind == PARSE_KIND_R_PAREN && frame->argument != 0 && parse_scalar_asm_sections(parser, frame) == 4)
    {
        // asm goto's colons come to its labels'.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AT_TOKEN);
    }
    else if (attribute && frame->role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && kind != PARSE_KIND_IDENTIFIER && kind < PARSE_KIND_TYPE && kind != PARSE_KIND_COMMA &&
             kind != PARSE_KIND_R_PAREN)
    {
        // An attribute list's items are names (or empty); clang expects the list's `)` after the token before. Its
        // ParseGNUAttributes then skips to that `)` and goes on with the list's attributes before the token, and with the
        // declaration (#1447): `__attribute__((unused, __clang__::x))` as `__attribute__((unused, ))`.
        const bool first_error = parser->error_token == BUSTER_PARSE_NONE;
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AFTER_PREVIOUS);
        if (first_error && parser->kinds[frame->opener] == PARSE_KIND_L_PAREN)
        {
            parse_scalar_resume_at_closer(parser, i, PARSE_KIND_R_PAREN);
        }
    }
    else if (attribute && frame->role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && kind == PARSE_KIND_COMMA)
    {
        // An empty item.
        step->role = PARSE_ROLE_SEPARATOR;
    }
    else if (kind == PARSE_KIND_IDENTIFIER || kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER || kind == PARSE_KIND_STRING || (attribute && kind >= PARSE_KIND_TYPE))
    {
        if (attribute && frame->role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && parser->frame_count >= 3 &&
            parser->frames[parser->frame_count - 2].continuation == PARSE_MODE_BLOCK_ATTRIBUTE)
        {
            const bool statement = parse_scalar_statement_attribute(parser, parser->names[i]);
            parser->frames[parser->frame_count - 3].flags |= statement ? BUSTER_PARSE_FRAME_STATEMENT_ATTRIBUTE : BUSTER_PARSE_FRAME_DECLARATION_ATTRIBUTE;
        }
        // GNU's `&&label` names a label, whatever the ordinary names mean.
        // A counted_by family's count names a member before a typedef of its name (parse_scalar_counted_member).
        const bool ordinary = !attribute && !(previous == PARSE_KIND_AMPAMP && parser->roles[(int64_t)i - 1] == PARSE_ROLE_PREFIX) &&
                              !(parse_scalar_is_typedef_name(parser, i) && parse_scalar_counted_member(parser, frame, i));
        if (ordinary && parse_scalar_is_typedef_name(parser, i) && previous == PARSE_KIND_SIZEOF)
        {
            parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXPECTED_PARENTHESES_AROUND_TYPE_NAME, BUSTER_PARSE_AFTER_PREVIOUS, i - 1);
        }
        else if (ordinary && parse_scalar_is_typedef_name(parser, i))
        {
            // In a counted_by family's argument, at any depth, clang's invalid expression has no integer type: its second
            // error, at the argument's first token (#1842). Past the argument's top-level `,` clang reads nothing, its
            // "expected ')'" there the checker's (CHECK_INPUT_COUNTED_PAREN): the name is an operand here.
            uint32_t counted = 0;
            uint32_t group = 0;
            for (uint32_t f = parser->frame_count; counted == 0 && f > 0; f -= 1)
            {
                counted = parse_scalar_counted_attribute(parser, parser->frames + f - 1);
                group = counted != 0 ? parser->frames[f - 1].opener : group;
            }
            uint32_t depth = 0;
            bool listed = false;
            for (uint32_t t = group + 1; counted != 0 && !listed && t < i; t += 1)
            {
                const uint8_t at = parser->kinds[t];
                depth += at == PARSE_KIND_L_PAREN || at == PARSE_KIND_L_SQUARE || at == PARSE_KIND_L_BRACE;
                depth -= depth != 0 && (at == PARSE_KIND_R_PAREN || at == PARSE_KIND_R_SQUARE || at == PARSE_KIND_R_BRACE);
                listed = depth == 0 && at == PARSE_KIND_COMMA;
            }
            if (listed)
            {
                step->role = PARSE_ROLE_OPERAND;
                frame->mode = PARSE_MODE_OPERATOR;
                frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
            }
            else
            {
                const bool first = parser->error_token == BUSTER_PARSE_NONE;
                parse_scalar_error_argument(parser, i, PARSE_MESSAGE_UNEXPECTED_TYPE_NAME_EXPRESSION, BUSTER_PARSE_AT_TOKEN, i);
                static const uint8_t follows[] = { PARSE_MESSAGE_COUNTED_BY_INTEGER, PARSE_MESSAGE_SIZED_BY_INTEGER, PARSE_MESSAGE_COUNTED_BY_OR_NULL_INTEGER,
                                                   PARSE_MESSAGE_SIZED_BY_OR_NULL_INTEGER };
                parser->error_follow_token = first && counted != 0 ? group + 1 : parser->error_follow_token;
                parser->error_follow_message = first && counted != 0 ? follows[counted - 1] : parser->error_follow_message;
            }
        }
        else
        {
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_OPERATOR;
            frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
            // GNU's `&&label`, the address of a label.
            frame->flags |= previous == PARSE_KIND_AMPAMP && parser->roles[(int64_t)i - 1] == PARSE_ROLE_PREFIX ? BUSTER_PARSE_FRAME_NO_POSTFIX : 0;
        }
    }
    else if (kind == PARSE_KIND_L_PAREN && !attribute && parse_scalar_starts_type(parser, i + 1))
    {
        ParseFrame* cast = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CAST, PARSE_MODE_SPECIFIERS, PARSE_MODE_OPERAND, false, step);
        cast->flags |= BUSTER_PARSE_FRAME_NO_BIND | BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_DECLARATION;
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_L_PAREN && parser->kinds[i + 1] == PARSE_KIND_L_BRACE && (flags & BUSTER_PARSE_FRAME_IN_FUNCTION) == 0)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_STATEMENT_EXPRESSION_AT_FILE_SCOPE, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_L_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_GROUP, PARSE_MODE_OPERAND, PARSE_MODE_OPERATOR, false, step);
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_L_BRACE && frame->role == PARSE_ROLE_OPEN_GROUP && previous == PARSE_KIND_L_PAREN && i == frame->opener + 1)
    {
        // A statement expression.
        ParseFrame* block = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_BLOCK, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, true, step);
        block->flags |= BUSTER_PARSE_FRAME_IN_FUNCTION;
        step->event = PARSE_EVENT_OPEN_BLOCK;
    }
    else if (kind == PARSE_KIND_MINUS || kind == PARSE_KIND_PLUS || kind == PARSE_KIND_EXCLAIM || kind == PARSE_KIND_TILDE || kind == PARSE_KIND_STAR ||
             kind == PARSE_KIND_AMP || kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS || kind == PARSE_KIND_AMPAMP ||
             kind == PARSE_KIND_EXTENSION || kind == PARSE_KIND_REAL_IMAG)
    {
        step->role = PARSE_ROLE_PREFIX;
        // `[*]`, an array parameter's unspecified size: the `]` may come.
        const bool unspecified = kind == PARSE_KIND_STAR && (flags & BUSTER_PARSE_FRAME_ARRAY_DECLARATOR) && parser->kinds[i + 1] == PARSE_KIND_R_SQUARE;
        frame->flags = unspecified ? frame->flags | BUSTER_PARSE_FRAME_EMPTY_OK : frame->flags & ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_SIZEOF)
    {
        step->role = PARSE_ROLE_PREFIX;
        // (`sizeof ({ ... })`'s `(` is its statement expression's own: clang's parser reads a parenthesized expression.)
        frame->mode = parser->kinds[i + 1] == PARSE_KIND_L_PAREN && parser->kinds[i + 2] != PARSE_KIND_L_BRACE ? PARSE_MODE_SIZEOF_PAREN : PARSE_MODE_OPERAND;
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_TYPE_ARGUMENT || kind == PARSE_KIND_GENERIC)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_KEYWORD_PAREN;
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_L_SQUARE && asm_operands)
    {
        // An asm operand's symbolic name, then its constraint.
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_MODE_ASM_NAME, PARSE_MODE_ASM_CONSTRAINT, false, step);
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if ((kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_QUALIFIER) && (flags & BUSTER_PARSE_FRAME_ARRAY_DECLARATOR))
    {
        // `[static 10]` and `[const]` in an array parameter.
        step->role = PARSE_ROLE_SPECIFIER;
    }
    else if (parse_kind_is_closer(kind) && empty && frame->role != PARSE_ROLE_NONE && !parse_frame_is_block(frame) && frame->role != PARSE_ROLE_OPEN_STRUCT)
    {
        parse_scalar_close(parser, i, step);
    }
    else if (kind == PARSE_KIND_SEMI && empty && frame->role != PARSE_ROLE_OPEN_CALL && frame->role != PARSE_ROLE_OPEN_TERNARY)
    {
        parse_scalar_terminator(parser, i, frame, step);
    }
    else if (kind == PARSE_KIND_COLON && empty && frame->role == PARSE_ROLE_OPEN_TERNARY)
    {
        // GNU's `a ?: b`.
        parse_scalar_close(parser, i, step);
    }
    else if (kind == PARSE_KIND_COLON && asm_operands && frame->argument >= parse_scalar_asm_sections(parser, frame))
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_COLON && asm_operands)
    {
        step->role = PARSE_ROLE_ASM_COLON;
        frame->argument += 1;
    }
    else if (previous == PARSE_KIND_SIZEOF && parse_scalar_starts_type(parser, i))
    {
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXPECTED_PARENTHESES_AROUND_TYPE_NAME, BUSTER_PARSE_AFTER_PREVIOUS, i - 1);
    }
    else
    {
        // clang puts a prefix `++` or `--` back when no operand follows it, and so expects the expression at the first of
        // them.
        uint32_t at = i;
        while (at > 0 && (parser->kinds[at - 1] == PARSE_KIND_PLUSPLUS || parser->kinds[at - 1] == PARSE_KIND_MINUSMINUS) && parser->roles[at - 1] == PARSE_ROLE_PREFIX)
        {
            at -= 1;
        }
        if (!parse_scalar_attribute_string_error(parser, at, frame))
        {
            parse_scalar_error(parser, at, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
        }
    }
}

BUSTER_INLINE uint8_t parse_binary_role(uint8_t kind)
{
    uint8_t result = PARSE_ROLE_NONE;
    switch (kind)
    {
        case PARSE_KIND_STAR:
        case PARSE_KIND_SLASH:
        case PARSE_KIND_PERCENT: result = PARSE_ROLE_MULTIPLICATIVE; break;
        case PARSE_KIND_PLUS:
        case PARSE_KIND_MINUS: result = PARSE_ROLE_ADDITIVE; break;
        case PARSE_KIND_LESSLESS:
        case PARSE_KIND_GREATERGREATER: result = PARSE_ROLE_SHIFT; break;
        case PARSE_KIND_LESS:
        case PARSE_KIND_GREATER:
        case PARSE_KIND_LESSEQUAL:
        case PARSE_KIND_GREATEREQUAL: result = PARSE_ROLE_RELATIONAL; break;
        case PARSE_KIND_EQUALEQUAL:
        case PARSE_KIND_EXCLAIMEQUAL: result = PARSE_ROLE_EQUALITY; break;
        case PARSE_KIND_AMP: result = PARSE_ROLE_BITWISE_AND; break;
        case PARSE_KIND_CARET: result = PARSE_ROLE_BITWISE_XOR; break;
        case PARSE_KIND_PIPE: result = PARSE_ROLE_BITWISE_OR; break;
        case PARSE_KIND_AMPAMP: result = PARSE_ROLE_LOGICAL_AND; break;
        case PARSE_KIND_PIPEPIPE: result = PARSE_ROLE_LOGICAL_OR; break;
        case PARSE_KIND_EQUAL:
        case PARSE_KIND_STAREQUAL:
        case PARSE_KIND_SLASHEQUAL:
        case PARSE_KIND_PERCENTEQUAL:
        case PARSE_KIND_PLUSEQUAL:
        case PARSE_KIND_MINUSEQUAL:
        case PARSE_KIND_LESSLESSEQUAL:
        case PARSE_KIND_GREATERGREATEREQUAL:
        case PARSE_KIND_AMPEQUAL:
        case PARSE_KIND_CARETEQUAL:
        case PARSE_KIND_PIPEEQUAL: result = PARSE_ROLE_ASSIGN; break;
        default: break;
    }
    return result;
}

// The first token of an initializer expression after GNU's single array designator without an '='.
BUSTER_INLINE bool parse_kind_starts_initializer_value(uint8_t kind)
{
    return kind == PARSE_KIND_IDENTIFIER || kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER || kind == PARSE_KIND_STRING ||
           kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_MINUS || kind == PARSE_KIND_PLUS ||
           kind == PARSE_KIND_EXCLAIM || kind == PARSE_KIND_TILDE || kind == PARSE_KIND_STAR || kind == PARSE_KIND_AMP ||
           kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS || kind == PARSE_KIND_AMPAMP || kind == PARSE_KIND_EXTENSION ||
           kind == PARSE_KIND_REAL_IMAG || kind == PARSE_KIND_SIZEOF || kind == PARSE_KIND_TYPE_ARGUMENT || kind == PARSE_KIND_GENERIC;
}

// In an expression, after an operand.
// What the rare flags of an operator's frame refuse, which parse_scalar_operator tests for with one test of both: a
// postfix operator after `sizeof` or `_Alignof` of a type name or GNU's `&&label` (NO_POSTFIX), and after a bit-field
// width's attributes anything but another or what ends the member's declarator (AFTER_WIDTH), which ends the member's
// declaration where clang expects a `;`. Reports it, and returns whether it did.
static bool parse_scalar_operator_refused(ParseScalar* parser, uint32_t i, ParseFrame* frame, uint32_t flags)
{
    const uint8_t kind = parser->kinds[i];
    const bool postfix = (flags & BUSTER_PARSE_FRAME_NO_POSTFIX) != 0 && (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_PERIOD ||
                                                                         kind == PARSE_KIND_ARROW || kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS);
    const bool after_width = !postfix && (flags & BUSTER_PARSE_FRAME_AFTER_WIDTH) != 0 &&
                             !(kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_COMMA || kind == PARSE_KIND_SEMI || parse_kind_is_closer(kind));
    if (postfix)
    {
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_POSTFIX_AFTER_UNARY, BUSTER_PARSE_AT_TOKEN, i);
    }
    else if (after_width)
    {
        parse_scalar_unexpected(parser, i, frame);
    }
    return postfix || after_width;
}

static void parse_scalar_operator(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t binary = parse_binary_role(kind);
    const uint8_t group = frame->role;
    const uint32_t flags = frame->flags;
    const bool conditional = (flags & BUSTER_PARSE_FRAME_CONDITIONAL) != 0;
    const bool initializer_list = group == PARSE_ROLE_OPEN_INITIALIZER || group == PARSE_ROLE_OPEN_COMPOUND_BODY;
    const bool array_designator = frame->statement == PARSE_ITEM_ARRAY_DESIGNATOR && initializer_list;
    const bool designator = initializer_list && (frame->statement == PARSE_ITEM_DESIGNATOR || array_designator);
    // A designator's subscript in an initializer list, where GNU's ranges `[1 ... 3]` may be.
    const bool designator_subscript = group == PARSE_ROLE_OPEN_SUBSCRIPT && parser->frame_count >= 2 &&
                                      (parser->frames[parser->frame_count - 2].statement == PARSE_ITEM_DESIGNATOR ||
                                       parser->frames[parser->frame_count - 2].statement == PARSE_ITEM_ARRAY_DESIGNATOR);
    frame->flags &= ~BUSTER_PARSE_FRAME_NO_POSTFIX;
    if ((flags & (BUSTER_PARSE_FRAME_NO_POSTFIX | BUSTER_PARSE_FRAME_AFTER_WIDTH)) != 0 && parse_scalar_operator_refused(parser, i, frame, flags))
    {
        // Refused by the frame's rare flags, and reported: parse_scalar_operator_refused's.
    }
    else if (frame->statement == PARSE_ITEM_DIRECT_INITIALIZER && kind != PARSE_KIND_COMMA && kind != PARSE_KIND_SEMI && !parse_kind_is_closer(kind))
    {
        // A direct initializer's `)` ends the declarator's initializer.
        parse_scalar_unexpected(parser, i, frame);
    }
    else if (initializer_list && parser->roles[(int64_t)i - 1] == PARSE_ROLE_CLOSE_INITIALIZER && kind != PARSE_KIND_COMMA && kind != PARSE_KIND_R_BRACE)
    {
        // A braced initializer in a list takes no operator after it: clang expects the list's `,` or its `}` (#1127).
        parse_scalar_close_expected(parser, i, PARSE_KIND_L_BRACE, true);
    }
    else if ((flags & BUSTER_PARSE_FRAME_ASM) && (frame->argument == 1 || frame->argument == 2) && parser->kinds[(int64_t)i - 1] == PARSE_KIND_STRING &&
             kind != PARSE_KIND_L_PAREN && kind != PARSE_KIND_STRING)
    {
        // An output's or input's constraint, which its operand in parentheses follows.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER_ASM_OPERAND, BUSTER_PARSE_AT_TOKEN);
    }
    else if ((flags & BUSTER_PARSE_FRAME_ASM) &&
             ((kind == PARSE_KIND_COLON && frame->argument >= parse_scalar_asm_sections(parser, frame)) ||
              !(kind == PARSE_KIND_COLON || kind == PARSE_KIND_R_PAREN || (kind == PARSE_KIND_COMMA && frame->argument != 0) ||
                (kind == PARSE_KIND_STRING && parser->kinds[(int64_t)i - 1] == PARSE_KIND_STRING) ||
                (kind == PARSE_KIND_L_PAREN && parser->kinds[(int64_t)i - 1] == PARSE_KIND_STRING && (frame->argument == 1 || frame->argument == 2)))))
    {
        // Too many sections, or what no section has after its strings, its operands' parentheses and its labels: an
        // operator, a clobber's (or a label's) operand, a second template.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else if ((flags & BUSTER_PARSE_FRAME_ASM) && kind == PARSE_KIND_R_PAREN && frame->argument != 0 && frame->argument < 4 &&
             parse_scalar_asm_sections(parser, frame) == 4)
    {
        // asm goto's colons come to its labels'.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AT_TOKEN);
    }
    else if (group == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && kind != PARSE_KIND_L_PAREN && kind != PARSE_KIND_COMMA && !parse_kind_is_closer(kind))
    {
        // An attribute's name is followed by its arguments, the next attribute or the list's end.
        parse_scalar_unexpected(parser, i, frame);
    }
    else if (initializer_list && frame->statement == PARSE_ITEM_OLD_STYLE_DESIGNATOR && kind == PARSE_KIND_COLON)
    {
        // GNU's `member: value`: treat ':' as the designator's assignment.
        step->role = PARSE_ROLE_ASSIGN;
        frame->mode = PARSE_MODE_INITIALIZER;
        frame->statement = PARSE_ITEM_EXPRESSION;
    }
    else if (array_designator && parse_kind_starts_initializer_value(kind))
    {
        // GNU omits '=' after one `[index]`. Chained `[index].member` and `[index][index]` still require it. The
        // value's tree is then its own and the item's shape an `=`'s (#1127): the designator's `]`, the last token
        // emitted, searches its R past the value, as the left operand of an assignment would, where a prefix operator or
        // a cast would end a postfix `]`'s search and take it; and it leaves the candidates, where it would be the
        // value's first operand's L. Its threshold is still above every one waiting below it, all of the list's or
        // outside it. The vector path flags what follows the `]`, so every such declaration is the scalar parser's.
        const uint32_t waiting = parser->waiting_count - 1;
        const uint32_t candidate = parser->candidate_count - 1;
        if (parser->waiting[waiting].position == i - 1 && parser->candidates[candidate].position == i - 1)
        {
            parser->waiting[waiting].threshold += (uint32_t)PARSE_LEVEL_ASSIGN - parse_role_right_thresholds[PARSE_ROLE_CLOSE_SUBSCRIPT];
            parser->candidate_count = candidate;
        }
        frame->mode = PARSE_MODE_INITIALIZER;
        frame->statement = PARSE_ITEM_EXPRESSION;
        step->again = true;
    }
    else if (designator && kind != PARSE_KIND_EQUAL && kind != PARSE_KIND_PERIOD && kind != PARSE_KIND_L_SQUARE)
    {
        // After one `[index]`, clang reads the value GNU lets follow it, so what can't begin one is a missing
        // expression (#1127).
        parse_scalar_error(parser, i, array_designator ? PARSE_MESSAGE_EXPECTED_EXPRESSION : PARSE_MESSAGE_EXPECTED_DESIGNATOR, BUSTER_PARSE_AT_TOKEN);
    }
    else if (binary == PARSE_ROLE_ASSIGN && conditional)
    {
        parse_scalar_unexpected(parser, i, frame);
    }
    else if (binary != PARSE_ROLE_NONE)
    {
        step->role = binary;
        // A designator's `=` in an initializer list may be followed by a braced initializer.
        const bool initializer = binary == PARSE_ROLE_ASSIGN && initializer_list;
        frame->mode = initializer ? PARSE_MODE_INITIALIZER : PARSE_MODE_OPERAND;
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
        frame->statement = designator ? PARSE_ITEM_EXPRESSION : frame->statement;
    }
    else if (kind == PARSE_KIND_QUESTION)
    {
        ParseFrame* ternary = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_TERNARY, PARSE_MODE_OPERAND, PARSE_MODE_OPERAND, false, step);
        // GNU's `a ?: b`.
        ternary->flags |= BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_COLON && (group == PARSE_ROLE_OPEN_TERNARY || group == PARSE_ROLE_OPEN_CASE))
    {
        parse_scalar_close(parser, i, step);
    }
    else if (kind == PARSE_KIND_COLON && (flags & BUSTER_PARSE_FRAME_ASM))
    {
        step->role = PARSE_ROLE_ASM_COLON;
        frame->mode = PARSE_MODE_OPERAND;
        frame->flags |= BUSTER_PARSE_FRAME_EMPTY_OK;
        frame->argument += 1;
    }
    else if (kind == PARSE_KIND_L_PAREN && group == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && parse_scalar_custom_attribute(parser, parser->names[(int64_t)i - 1]))
    {
        // availability's and external_source_symbol's arguments, which clang's parser reads its own way (#1447): a balanced
        // run of tokens, as a C23 attribute's arguments are, which the checker reads as clang's parser does
        // (c_check_attribute_checks_input.c).
        ParseFrame* call = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CALL, PARSE_MODE_ATTRIBUTE_TOKENS, PARSE_MODE_OPERATOR, false, step);
        call->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE;
        call->closer_role = PARSE_ROLE_CLOSE_CALL;
    }
    else if (kind == PARSE_KIND_L_PAREN)
    {
        // An asm operand's expression, after its constraint, may not be empty, as a call's arguments may.
        const bool operand = (flags & BUSTER_PARSE_FRAME_ASM) && parser->kinds[(int64_t)i - 1] == PARSE_KIND_STRING;
        ParseFrame* call = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CALL, PARSE_MODE_OPERAND, PARSE_MODE_OPERATOR, false, step);
        call->flags |= operand ? 0 : BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_L_SQUARE && parse_scalar_standard_attribute(parser, i))
    {
        // `[[` after an operand is an attribute list, which clang takes nowhere in an expression.
        parse_scalar_error(parser, i, parse_scalar_prohibited_attribute(parser, i), BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_ATTRIBUTE && conditional && group == PARSE_ROLE_OPEN_STRUCT)
    {
        // GNU attributes after a bit-field's width end it, as clang takes them: the member's, which hang on what ends the
        // member's declarator (its `,` or `;`), as a tag's attributes hang on what follows them, outside the width's
        // expression, whose tree stays whole.
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
        frame->flags |= BUSTER_PARSE_FRAME_AFTER_WIDTH;
        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT;
        frame->attribute_continuation = PARSE_MODE_OPERATOR;
    }
    else if (kind == PARSE_KIND_L_SQUARE)
    {
        frame->statement = array_designator ? PARSE_ITEM_DESIGNATOR : frame->statement;
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_MODE_OPERAND, PARSE_MODE_OPERATOR, false, step);
    }
    else if (kind == PARSE_KIND_PERIOD || (kind == PARSE_KIND_ARROW && !designator))
    {
        frame->statement = designator ? PARSE_ITEM_DESIGNATOR : frame->statement;
        step->role = PARSE_ROLE_MEMBER_OPERATOR;
        frame->mode = PARSE_MODE_MEMBER;
    }
    else if (kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS)
    {
        step->role = PARSE_ROLE_POSTFIX;
    }
    else if (kind == PARSE_KIND_STRING && parser->kinds[(int64_t)i - 1] == PARSE_KIND_STRING)
    {
        step->role = PARSE_ROLE_STRING_NEXT;
    }
    else if (kind == PARSE_KIND_ELLIPSIS && (group == PARSE_ROLE_OPEN_CASE || designator_subscript))
    {
        step->role = PARSE_ROLE_RANGE;
        frame->mode = PARSE_MODE_OPERAND;
    }
    else if (kind == PARSE_KIND_COMMA)
    {
        step->role = parse_frame_comma_role(parser);
        const bool items = group == PARSE_ROLE_OPEN_ENUM || initializer_list || group == PARSE_ROLE_OPEN_FUNCTION || group == PARSE_ROLE_OPEN_KEYWORD;
        // Where the comma is an operator (not between arguments, items or declarators), clang ends the expression
        // before a comma that what follows can't continue: a brace, some statement keywords, a declaration specifier.
        const bool operator_comma = (flags & BUSTER_PARSE_FRAME_DECLARATION) == 0 && !items && group != PARSE_ROLE_OPEN_CALL && group != PARSE_ROLE_OPEN_ASM &&
                                    group != PARSE_ROLE_OPEN_ATTRIBUTE_LIST && group != PARSE_ROLE_OPEN_ATTRIBUTE && group != PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER &&
                                    group != PARSE_ROLE_OPEN_ATTRIBUTE_LEFT && group != PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT;
        // A declaration's comma, then a line break and what can't start a declarator: clang takes the comma for a `;`.
        const bool declaration_comma = (flags & BUSTER_PARSE_FRAME_DECLARATION) && step->role == PARSE_ROLE_COMMA && (parser->flags[i + 1] & BUSTER_PP_START_OF_LINE) &&
                                       !parse_scalar_might_be_declarator(parser, i + 1, group == PARSE_ROLE_OPEN_STRUCT);
        if (operator_comma && parse_scalar_not_expression_start(parser, i + 1))
        {
            parse_scalar_unexpected(parser, i, frame);
        }
        else if (declaration_comma)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION, BUSTER_PARSE_AT_TOKEN);
        }
        else if (group == PARSE_ROLE_OPEN_CASE || (conditional && group == PARSE_ROLE_OPEN_SPECIFIER) || (flags & BUSTER_PARSE_FRAME_ARRAY_DECLARATOR) ||
                 designator_subscript)
        {
            // No comma at the top of a case label, of `_Alignas(` and `_BitInt(`, or of an array declarator's size or an
            // array designator's index (an assignment-expression each).
            const bool first_error = parser->error_token == BUSTER_PARSE_NONE;
            parse_scalar_unexpected(parser, i, frame);
            if (first_error && ((flags & BUSTER_PARSE_FRAME_ARRAY_DECLARATOR) || designator_subscript) && parser->error_message == PARSE_MESSAGE_EXPECTED_R_SQUARE)
            {
                // ParseBracketDeclarator and ParseInitializerWithPotentialDesignator go on with what came before the comma
                // (#1867).
                parse_scalar_resume_at_closer(parser, i, PARSE_KIND_R_SQUARE);
            }
        }
        else if (flags & BUSTER_PARSE_FRAME_DECLARATION)
        {
            // The end of an initializer: the next declarator, or the next parameter.
            frame->statement = frame->statement == PARSE_ITEM_DIRECT_INITIALIZER ? PARSE_ITEM_EXPRESSION : frame->statement;
            frame->mode = step->role == PARSE_ROLE_COMMA ? PARSE_MODE_DECLARATOR : PARSE_MODE_ITEM;
            frame->flags &= ~(BUSTER_PARSE_FRAME_POINTER | BUSTER_PARSE_FRAME_AFTER_STAR | BUSTER_PARSE_FRAME_CONDITIONAL | BUSTER_PARSE_FRAME_KR | BUSTER_PARSE_FRAME_AFTER_WIDTH);
            frame->flags |= step->role == PARSE_ROLE_COMMA ? BUSTER_PARSE_FRAME_LATER_DECLARATOR : 0;
            if (step->role == PARSE_ROLE_SEPARATOR)
            {
                frame->flags &= ~(BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_AUTO);
                frame->argument += 1;
            }
        }
        else
        {
            frame->mode = items ? PARSE_MODE_ITEM : PARSE_MODE_OPERAND;
            if (group == PARSE_ROLE_OPEN_ENUM)
            {
                // The end of an enumerator's value, and of the enumerator.
                parse_scalar_complete_declarator(parser, frame, false);
            }
            // A keyword group counts its arguments (a for-statement's frame counts its clauses, which commas don't end).
            frame->argument += group == PARSE_ROLE_OPEN_KEYWORD;
            if (items)
            {
                frame->flags &= ~(BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_NO_BIND | BUSTER_PARSE_FRAME_CONDITIONAL |
                                  BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_SPECIFIED);
                frame->flags |= parser->frames[parser->frame_count - 2].flags & BUSTER_PARSE_FRAME_NO_BIND;
                frame->statement = PARSE_ITEM_EXPRESSION;
            }
            // A GNU attribute list's items may be empty, the last one too (`__attribute__((unused,))`).
            frame->flags = group == PARSE_ROLE_OPEN_ATTRIBUTE_LIST ? frame->flags | BUSTER_PARSE_FRAME_EMPTY_OK : frame->flags & ~BUSTER_PARSE_FRAME_EMPTY_OK;
        }
    }
    else if (kind == PARSE_KIND_SEMI)
    {
        parse_scalar_terminator(parser, i, frame, step);
    }
    else if (parse_kind_is_closer(kind))
    {
        parse_scalar_close_item(parser, i, frame, step);
    }
    else
    {
        parse_scalar_unexpected(parser, i, frame);
    }
}

// The token after a keyword that a group follows.
static void parse_scalar_paren(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t mode = frame->mode;
    const uint8_t kind = parser->kinds[i];
    const IdentId name = parser->names[i];
    const uint8_t qualifier = name == C_NAME_VOLATILE || name == C_NAME_GNU_VOLATILE || name == C_NAME_GNU_VOLATILE2 ? 1 :
                              name == C_NAME_INLINE || name == C_NAME_GNU_INLINE || name == C_NAME_GNU_INLINE2 ? 2 : kind == PARSE_KIND_GOTO ? 4 : 0;
    const bool asm_label = mode == PARSE_MODE_ATTRIBUTE_PAREN && parser->kinds[(int64_t)i - 1] == PARSE_KIND_ASM;
    if (kind != PARSE_KIND_L_PAREN && (mode == PARSE_MODE_ASM_PAREN || asm_label))
    {
        const bool outside = asm_label || frame->statement == PARSE_ITEM_TOP_LEVEL_ASM;
        const bool duplicate = (frame->asm_qualifiers & qualifier) != 0;
        if (qualifier != 0 && (outside || duplicate))
        {
            const uint8_t offset = qualifier == 1 ? 0 : qualifier == 2 ? 1 : 2;
            const ParseMessage message = (outside ? PARSE_MESSAGE_MEANINGLESS_ASM_VOLATILE : PARSE_MESSAGE_DUPLICATE_ASM_VOLATILE) + offset;
            parse_scalar_error(parser, i, message, BUSTER_PARSE_AT_TOKEN);
        }
        else if (qualifier != 0)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->asm_qualifiers |= qualifier;
        }
        else
        {
            parse_scalar_error(parser, i, outside ? PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER_ASM : PARSE_MESSAGE_EXPECTED_ASM_QUALIFIER, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (kind != PARSE_KIND_L_PAREN && mode == PARSE_MODE_ATTRIBUTE_PAREN && parser->kinds[(int64_t)i - 1] == PARSE_KIND_ATTRIBUTE)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER_ATTRIBUTE, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (kind != PARSE_KIND_L_PAREN && mode == PARSE_MODE_CONDITION_PAREN && parser->kinds[(int64_t)i - 1] == PARSE_KIND_WHILE &&
             parse_statement_top(parser) != 0 && parse_statement_top(parser)->state == PARSE_STATEMENT_DO_DONE)
    {
        // A do-statement's while, which clang names with its do.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER_DO_WHILE, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind != PARSE_KIND_L_PAREN)
    {
        // The keyword clang names: the one before, or for asm the one before its qualifiers. __builtin_available's
        // balanced delimiters name none, just after the keyword (#148, ExpectAndConsume).
        uint32_t keyword = i - 1;
        while (mode == PARSE_MODE_ASM_PAREN && keyword > 0 && parser->kinds[keyword] != PARSE_KIND_ASM)
        {
            keyword -= 1;
        }
        if (parser->names[keyword] == C_NAME_BUILTIN_AVAILABLE && parser->kinds[keyword] == PARSE_KIND_TYPE_ARGUMENT)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN, BUSTER_PARSE_AFTER_PREVIOUS);
        }
        else
        {
            parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER, BUSTER_PARSE_AT_TOKEN, keyword);
        }
    }
    else if (mode == PARSE_MODE_CONDITION_PAREN)
    {
        // The statement's scope begins with its condition.
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CONDITION, PARSE_MODE_OPERAND, PARSE_MODE_ITEM, true, step);
    }
    else if (mode == PARSE_MODE_FOR_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_FOR, PARSE_MODE_ITEM, PARSE_MODE_ITEM, true, step);
    }
    else if (mode == PARSE_MODE_KEYWORD_PAREN)
    {
        // _Static_assert where a declaration is ends with its `;`.
        const bool declaration = parser->kinds[(int64_t)i - 1] == PARSE_KIND_STATIC_ASSERT && (frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame) ||
                                                                                     frame->role == PARSE_ROLE_OPEN_STRUCT);
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_KEYWORD, PARSE_MODE_ITEM, declaration ? PARSE_MODE_END_STATEMENT : PARSE_MODE_OPERATOR, false, step);
        frame->statement = declaration ? PARSE_ITEM_STATIC_ASSERT : frame->statement;
    }
    else if (mode == PARSE_MODE_SIZEOF_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SIZEOF, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, false, step);
    }
    else if (mode == PARSE_MODE_SPECIFIER_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SPECIFIER, PARSE_MODE_ITEM, PARSE_MODE_SPECIFIERS, false, step);
    }
    else if (mode == PARSE_MODE_ASM_PAREN)
    {
        const bool statement = frame->statement == PARSE_ITEM_ASM || frame->statement == PARSE_ITEM_TOP_LEVEL_ASM;
        ParseFrame* group = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_ASM, PARSE_MODE_OPERAND, statement ? PARSE_MODE_END_STATEMENT : PARSE_MODE_OPERATOR, false, step);
        group->flags |= BUSTER_PARSE_FRAME_ASM;
    }
    else
    {
        // An attribute: `__attribute__((list))`, or `__declspec(list)` and an asm label's `("name")`.
        const uint8_t closer_role = frame->attribute_closer_role;
        const uint8_t continuation = frame->attribute_continuation;
        // `__attribute__` takes a list in two parentheses (ATTRIBUTE_LIST rejects one); an asm label takes its string in one.
        const bool list = parser->kinds[(int64_t)i - 1] == PARSE_KIND_ATTRIBUTE;
        const uint8_t opener_role = closer_role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ? PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER :
                                    closer_role == PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT ? PARSE_ROLE_OPEN_ATTRIBUTE_LEFT :
                                    closer_role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT ? PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT : PARSE_ROLE_OPEN_ATTRIBUTE;
        const uint8_t contents = list ? PARSE_MODE_ATTRIBUTE_LIST : parser->kinds[(int64_t)i - 1] == PARSE_KIND_ASM ? PARSE_MODE_ASM_LABEL : PARSE_MODE_OPERAND;
        ParseFrame* group = parse_scalar_open(parser, i, opener_role, contents, continuation, false, step);
        group->flags |= list ? 0 : BUSTER_PARSE_FRAME_ATTRIBUTE;
    }
}

// An attribute list's tokens, as a balanced run: C23's `[[gnu::unused, deprecated("why")]]`.
static void parse_scalar_attribute_tokens(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
    {
        const uint8_t role = kind == PARSE_KIND_L_PAREN ? PARSE_ROLE_OPEN_GROUP : kind == PARSE_KIND_L_SQUARE ? PARSE_ROLE_OPEN_SUBSCRIPT : PARSE_ROLE_OPEN_INITIALIZER;
        ParseFrame* group = parse_scalar_open(parser, i, role, PARSE_MODE_ATTRIBUTE_TOKENS, PARSE_MODE_ATTRIBUTE_TOKENS, false, step);
        group->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE;
        // Each group's closer is an operand, so that its contents and the tokens around it stay a flat run.
        group->closer_role = kind == PARSE_KIND_L_PAREN ? PARSE_ROLE_CLOSE_GROUP : kind == PARSE_KIND_L_SQUARE ? PARSE_ROLE_CLOSE_ATTRIBUTE_LIST : PARSE_ROLE_CLOSE_INITIALIZER;
    }
    else if (parse_kind_is_closer(kind))
    {
        parse_scalar_close(parser, i, step);
    }
    else if (kind == PARSE_KIND_END)
    {
        parse_scalar_unexpected(parser, i, frame);
    }
    else
    {
        step->role = kind == PARSE_KIND_COMMA ? PARSE_ROLE_SEPARATOR : PARSE_ROLE_OPERAND;
    }
}

// In `[[`'s list, as clang reads it: items of a name (an identifier or a keyword), perhaps in a scope (`gnu::unused`),
// and perhaps an argument clause in parentheses, a balanced run of tokens (parse_scalar_attribute_tokens), separated by
// commas, any of them empty. The names and the scope's colons are leaves, as in the run.
static void parse_scalar_standard_attribute_item(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t mode = frame->mode;
    const bool name = kind == PARSE_KIND_IDENTIFIER || (kind >= PARSE_KIND_TYPE && kind < PARSE_KIND_COUNT);
    // Only two halves of the same preprocessing token form `::`; macro expansion may juxtapose separate colons.
    const bool scope = kind == PARSE_KIND_COLON && parser->kinds[i + 1] == PARSE_KIND_COLON &&
                       (parser->flags[i + 1] & BUSTER_PARSE_COLONCOLON_SECOND) != 0;
    const bool named = mode == PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_NAME || mode == PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_SCOPED_NAME;
    const bool ended = mode == PARSE_MODE_STANDARD_ATTRIBUTE_NAME || named || mode == PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_ARGUMENTS;
    if (mode == PARSE_MODE_STANDARD_ATTRIBUTE_SCOPE)
    {
        step->role = PARSE_ROLE_OPERAND;
        frame->mode = PARSE_MODE_STANDARD_ATTRIBUTE_SCOPED_NAME;
    }
    else if ((mode == PARSE_MODE_STANDARD_ATTRIBUTE_NAME || mode == PARSE_MODE_STANDARD_ATTRIBUTE_SCOPED_NAME) && name)
    {
        step->role = PARSE_ROLE_OPERAND;
        frame->mode = mode == PARSE_MODE_STANDARD_ATTRIBUTE_NAME ? PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_NAME : PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_SCOPED_NAME;
        // At a block item, whether the attribute is a statement's, as GNU's names say (parse_scalar_operand): a scoped
        // one's at its name, its scope three tokens before; an unscoped one's where no `::` follows.
        const bool scoped = mode == PARSE_MODE_STANDARD_ATTRIBUTE_SCOPED_NAME;
        const bool scoping = !scoped && parser->kinds[i + 1] == PARSE_KIND_COLON && parser->kinds[i + 2] == PARSE_KIND_COLON &&
                             (parser->flags[i + 2] & BUSTER_PARSE_COLONCOLON_SECOND) != 0;
        if (!scoping && parser->frame_count >= 3 && parser->frames[parser->frame_count - 2].continuation == PARSE_MODE_BLOCK_ATTRIBUTE)
        {
            const bool statement = parse_scalar_standard_statement_attribute(parser, scoped ? parser->names[i - 3] : 0, parser->names[i]);
            parser->frames[parser->frame_count - 3].flags |= statement ? BUSTER_PARSE_FRAME_STATEMENT_ATTRIBUTE : BUSTER_PARSE_FRAME_DECLARATION_ATTRIBUTE;
        }
    }
    else if (mode == PARSE_MODE_STANDARD_ATTRIBUTE_SCOPED_NAME)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
    }
    else if (mode == PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_NAME && scope)
    {
        step->role = PARSE_ROLE_OPERAND;
        frame->mode = PARSE_MODE_STANDARD_ATTRIBUTE_SCOPE;
    }
    else if (named && kind == PARSE_KIND_L_PAREN)
    {
        ParseFrame* group = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_GROUP, PARSE_MODE_ATTRIBUTE_TOKENS, PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_ARGUMENTS, false, step);
        group->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE;
        group->closer_role = PARSE_ROLE_CLOSE_GROUP;
    }
    else if (ended && kind == PARSE_KIND_COMMA)
    {
        step->role = PARSE_ROLE_SEPARATOR;
        frame->mode = PARSE_MODE_STANDARD_ATTRIBUTE_NAME;
    }
    else if (ended && kind == PARSE_KIND_R_SQUARE)
    {
        parse_scalar_close(parser, i, step);
    }
    else
    {
        // clang expects the `]` or the `,` after the token before.
        parse_scalar_error(parser, i, mode == PARSE_MODE_STANDARD_ATTRIBUTE_NAME ? PARSE_MESSAGE_EXPECTED_R_SQUARE : PARSE_MESSAGE_EXPECTED_COMMA, BUSTER_PARSE_AFTER_PREVIOUS);
    }
}

// What a frame whose statement must end with `;` reports when something else comes, after the token before it.
static void parse_scalar_missing_semi(ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    uint8_t message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_EXPRESSION;
    switch (frame->statement)
    {
        case PARSE_ITEM_GOTO: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_GOTO; break;
        case PARSE_ITEM_BREAK: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_BREAK; break;
        case PARSE_ITEM_CONTINUE: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_CONTINUE; break;
        case PARSE_ITEM_ASM: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_ASM; break;
        case PARSE_ITEM_TOP_LEVEL_ASM: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TOP_LEVEL_ASM; break;
        case PARSE_ITEM_STATIC_ASSERT: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_STATIC_ASSERT; break;
        case PARSE_ITEM_DO_WHILE: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_DO_WHILE; break;
        default: break;
    }
    parse_scalar_error(parser, i, message, BUSTER_PARSE_AFTER_PREVIOUS);
}

static void parse_scalar_token(ParseScalar* parser, uint32_t i)
{
    const uint8_t kind = parser->kinds[i];
    uint8_t binding_mode = PARSE_MODE_ITEM;
    uint32_t binding_flags = 0;
    uint32_t binding_argument = 0;
    if (parser->binding_trace != 0)
    {
        parser->binding_trace->point = i;
    }
    ParseStep step = {
        .depth = parser->frame_count - 1,
        .scope = BUSTER_PARSE_NONE,
        .role = PARSE_ROLE_ERROR,
        .event = PARSE_EVENT_NONE,
        .keyword = 0,
        .again = true,
    };
    while (step.again && parser->error_token == BUSTER_PARSE_NONE)
    {
        step.again = false;
        step.depth = parser->frame_count - 1;
        ParseFrame* frame = &parser->frames[parser->frame_count - 1];
        binding_mode = frame->mode;
        binding_flags = frame->flags;
        binding_argument = frame->argument;
        if (kind == PARSE_KIND_END && frame->mode == PARSE_MODE_ITEM && frame->opener != BUSTER_PARSE_NONE && parser->kinds[frame->opener] == PARSE_KIND_L_BRACE)
        {
            // What the statement stack waits for comes first, at the end of the file too (#1127): a do-statement whose
            // body ended asks for its while, and a head or a label for its statement, which clang reads as an
            // expression.
            const bool block = parse_frame_is_block(frame);
            if (block)
            {
                parse_statement_flush(parser);
            }
            const ParseStatement* top = block ? parse_statement_top(parser) : 0;
            const uint8_t message = top == 0 ? PARSE_MESSAGE_EXPECTED_R_BRACE : top->state == PARSE_STATEMENT_DO_DONE ? PARSE_MESSAGE_EXPECTED_WHILE :
                                    top->state == PARSE_STATEMENT_WAITING ? PARSE_MESSAGE_EXPECTED_EXPRESSION : PARSE_MESSAGE_EXPECTED_R_BRACE;
            parse_scalar_error(parser, i, message, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            switch (frame->mode)
            {
                case PARSE_MODE_ITEM: parse_scalar_item(parser, i, frame, &step); break;
                case PARSE_MODE_BLOCK_ATTRIBUTE: parse_scalar_block_attribute(parser, i, frame, &step); break;
                case PARSE_MODE_SPECIFIERS: parse_scalar_specifiers(parser, i, frame, &step); break;
                case PARSE_MODE_TAG:
                case PARSE_MODE_TAG_BODY: parse_scalar_tag(parser, i, frame, &step); break;
                case PARSE_MODE_ENUM_BASE: parse_scalar_enum_base(parser, i, frame, &step); break;
                case PARSE_MODE_DECLARATOR: parse_scalar_declarator(parser, i, frame, &step); break;
                case PARSE_MODE_AFTER_DECLARATOR: parse_scalar_after_declarator(parser, i, frame, &step); break;
                case PARSE_MODE_KR_PARAMETERS: parse_scalar_kr_parameters(parser, i, frame, &step); break;
                case PARSE_MODE_INITIALIZER:
                    if (kind == PARSE_KIND_L_BRACE)
                    {
                        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_INITIALIZER, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, false, &step);
                    }
                    else
                    {
                        parse_scalar_begin_expression(frame, false, false, &step);
                    }
                    break;
                case PARSE_MODE_OPERAND: parse_scalar_operand(parser, i, frame, &step); break;
                case PARSE_MODE_OPERATOR: parse_scalar_operator(parser, i, frame, &step); break;
                case PARSE_MODE_MEMBER:
                    if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        step.role = PARSE_ROLE_MEMBER;
                        frame->mode = PARSE_MODE_OPERATOR;
                    }
                    else
                    {
                        // A designator's `.` names a member, as clang's message says.
                        const bool designator = frame->statement == PARSE_ITEM_DESIGNATOR &&
                                                (frame->role == PARSE_ROLE_OPEN_INITIALIZER || frame->role == PARSE_ROLE_OPEN_COMPOUND_BODY);
                        parse_scalar_error(parser, i, designator ? PARSE_MESSAGE_EXPECTED_FIELD_DESIGNATOR : PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_LABEL:
                    step.role = PARSE_ROLE_LABEL;
                    step.event = PARSE_EVENT_HEAD;
                    step.keyword = PARSE_KIND_COLON;
                    frame->mode = PARSE_MODE_ITEM;
                    break;
                case PARSE_MODE_DEFAULT:
                    if (kind == PARSE_KIND_COLON)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        // A `;` where the `:` is due is the label's empty statement to clang's recovery, which reads on past it.
                        if (parser->error_token == BUSTER_PARSE_NONE)
                        {
                            parser->error_resume_colon = kind == PARSE_KIND_SEMI;
                        }
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON_AFTER_DEFAULT, BUSTER_PARSE_AFTER_PREVIOUS);
                    }
                    break;
                case PARSE_MODE_ASSOCIATION:
                    if (kind == PARSE_KIND_COLON)
                    {
                        step.role = PARSE_ROLE_ASSOCIATION;
                        frame->mode = PARSE_MODE_OPERAND;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_ENUMERATOR:
                    if (kind == PARSE_KIND_EQUAL)
                    {
                        step.role = PARSE_ROLE_ASSIGN;
                        parse_scalar_begin_expression(frame, false, true, &step);
                        step.again = false;
                    }
                    else if (kind == PARSE_KIND_COMMA)
                    {
                        parse_scalar_complete_declarator(parser, frame, false);
                        step.role = PARSE_ROLE_SEPARATOR;
                        frame->mode = PARSE_MODE_ITEM;
                    }
                    else if (kind == PARSE_KIND_R_BRACE)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else if (kind == PARSE_KIND_ATTRIBUTE)
                    {
                        step.role = PARSE_ROLE_LEAF_RIGHT;
                        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
                        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE;
                        frame->attribute_continuation = PARSE_MODE_ENUMERATOR;
                    }
                    else if (parse_scalar_standard_attribute(parser, i))
                    {
                        parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE, PARSE_MODE_ENUMERATOR, &step);
                    }
                    else if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_MISSING_COMMA_BETWEEN_ENUMERATORS, BUSTER_PARSE_AFTER_PREVIOUS);
                    }
                    else
                    {
                        // Before an `=`, clang asks for the value too; after one, `}` or `,` (parse_scalar_unexpected).
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_END_OF_ENUMERATOR, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_END_STATEMENT:
                    if (kind == PARSE_KIND_SEMI)
                    {
                        parse_scalar_terminator(parser, i, frame, &step);
                    }
                    else
                    {
                        // The `)` of a do-statement's while has its own message.
                        const ParseStatement* top = parse_statement_top(parser);
                        if (top != 0 && top->state == PARSE_STATEMENT_DO_WHILE && parse_frame_is_block(frame) && frame->statement == PARSE_ITEM_EXPRESSION)
                        {
                            frame->statement = PARSE_ITEM_DO_WHILE;
                        }
                        parse_scalar_missing_semi(parser, i, frame);
                    }
                    break;
                case PARSE_MODE_GOTO:
                    if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        step.role = PARSE_ROLE_OPERAND;
                        frame->mode = PARSE_MODE_END_STATEMENT;
                    }
                    else if (kind == PARSE_KIND_STAR)
                    {
                        // GNU's computed goto, goto *expression: the `*` is the goto's delimiter, not a prefix, and takes the
                        // whole expression after it as `return` does (a comma's and an assignment's too), so that it is the
                        // goto's child and the expression its own.
                        step.role = PARSE_ROLE_RETURN;
                        frame->mode = PARSE_MODE_OPERAND;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_LOCAL_LABEL_NAME:
                    if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        step.role = PARSE_ROLE_OPERAND;
                        frame->mode = PARSE_MODE_LOCAL_LABEL_END;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_LOCAL_LABEL_END:
                    if (kind == PARSE_KIND_COMMA)
                    {
                        step.role = frame->comma_role;
                        frame->mode = PARSE_MODE_LOCAL_LABEL_NAME;
                    }
                    else if (kind == PARSE_KIND_SEMI)
                    {
                        parse_scalar_terminator(parser, i, frame, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION, BUSTER_PARSE_AFTER_PREVIOUS);
                    }
                    break;
                case PARSE_MODE_JUMP:
                    if (kind == PARSE_KIND_SEMI)
                    {
                        parse_scalar_terminator(parser, i, frame, &step);
                    }
                    else if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        // C2y's `break label;`, which clang rejects in C23 and before.
                        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_NAMED_JUMP, BUSTER_PARSE_AT_TOKEN, i - 1);
                    }
                    else
                    {
                        parse_scalar_missing_semi(parser, i, frame);
                    }
                    break;
                case PARSE_MODE_ATTRIBUTE_LIST:
                    if (kind == PARSE_KIND_L_PAREN)
                    {
                        ParseFrame* list = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_LIST, PARSE_MODE_OPERAND, PARSE_MODE_ATTRIBUTE_END, false, &step);
                        list->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE | BUSTER_PARSE_FRAME_EMPTY_OK;
                    }
                    else
                    {
                        // clang expects the list's `(` after the first, as it expects the first after `__attribute__`.
                        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER, BUSTER_PARSE_AFTER_PREVIOUS, i - 1);
                    }
                    break;
                case PARSE_MODE_ATTRIBUTE_END:
                    if (kind == PARSE_KIND_R_PAREN)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AFTER_PREVIOUS);
                    }
                    break;
                case PARSE_MODE_STANDARD_ATTRIBUTE_LIST:
                {
                    ParseFrame* list = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_LIST, PARSE_MODE_STANDARD_ATTRIBUTE_NAME, PARSE_MODE_STANDARD_ATTRIBUTE_END, false,
                                                         &step);
                    list->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE;
                    break;
                }
                case PARSE_MODE_STANDARD_ATTRIBUTE_NAME:
                case PARSE_MODE_STANDARD_ATTRIBUTE_SCOPE:
                case PARSE_MODE_STANDARD_ATTRIBUTE_SCOPED_NAME:
                case PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_NAME:
                case PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_SCOPED_NAME:
                case PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_ARGUMENTS: parse_scalar_standard_attribute_item(parser, i, frame, &step); break;
                case PARSE_MODE_STANDARD_ATTRIBUTE_END:
                    if (kind == PARSE_KIND_R_SQUARE)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_SQUARE, BUSTER_PARSE_AFTER_PREVIOUS);
                    }
                    break;
                case PARSE_MODE_ATTRIBUTE_TOKENS: parse_scalar_attribute_tokens(parser, i, frame, &step); break;
                case PARSE_MODE_ASM_CONSTRAINT:
                    if (kind == PARSE_KIND_STRING)
                    {
                        step.role = PARSE_ROLE_OPERAND;
                        frame->mode = PARSE_MODE_OPERATOR;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_ASM_STRING, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_ASM_NAME:
                    // An asm operand's symbolic name, whatever the ordinary names mean, then its `]`.
                    if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        step.role = PARSE_ROLE_OPERAND;
                        frame->mode = PARSE_MODE_CLOSE;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_ASM_LABEL:
                    // An asm label's string literals.
                    if (kind == PARSE_KIND_STRING)
                    {
                        step.role = PARSE_ROLE_OPERAND;
                        frame->mode = PARSE_MODE_MESSAGE;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_ASM_STRING, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_COMPOUND_BODY:
                    if (kind == PARSE_KIND_L_BRACE)
                    {
                        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_COMPOUND_BODY, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, false, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_BRACE, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_CLOSE:
                    if (parse_kind_is_closer(kind))
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_unexpected(parser, i, frame);
                    }
                    break;
                case PARSE_MODE_OFFSETOF_MEMBER:
                    if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        step.role = parser->kinds[(int64_t)i - 1] == PARSE_KIND_PERIOD ? PARSE_ROLE_MEMBER : PARSE_ROLE_OPERAND;
                        frame->mode = PARSE_MODE_OFFSETOF_DESIGNATOR;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_AVAILABILITY_SPEC:
                    // A spec: `*`, or a platform's name, which names nothing of the unit's.
                    if (kind == PARSE_KIND_STAR || kind == PARSE_KIND_IDENTIFIER)
                    {
                        step.role = PARSE_ROLE_OPERAND;
                        frame->mode = kind == PARSE_KIND_STAR ? PARSE_MODE_AVAILABILITY_NEXT : PARSE_MODE_AVAILABILITY_VERSION;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_PLATFORM_NAME, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_AVAILABILITY_VERSION:
                    // The platform's version: a number, whose spelling the checker reads (check_input_availability_prepare).
                    if (kind == PARSE_KIND_NUMBER)
                    {
                        step.role = PARSE_ROLE_OPERAND;
                        frame->mode = PARSE_MODE_AVAILABILITY_NEXT;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_VERSION, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_AVAILABILITY_NEXT:
                    if (kind == PARSE_KIND_COMMA)
                    {
                        step.role = PARSE_ROLE_SEPARATOR;
                        frame->argument += 1;
                        frame->mode = PARSE_MODE_AVAILABILITY_SPEC;
                    }
                    else if (kind == PARSE_KIND_R_PAREN)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        // clang closes the group with its balanced delimiters.
                        parse_scalar_close_expected(parser, i, PARSE_KIND_L_PAREN, true);
                    }
                    break;
                case PARSE_MODE_OFFSETOF_DESIGNATOR:
                    if (kind == PARSE_KIND_PERIOD)
                    {
                        step.role = PARSE_ROLE_MEMBER_OPERATOR;
                        frame->mode = PARSE_MODE_OFFSETOF_MEMBER;
                    }
                    else if (kind == PARSE_KIND_L_SQUARE && parse_scalar_standard_attribute(parser, i))
                    {
                        parse_scalar_error(parser, i, parse_scalar_prohibited_attribute(parser, i), BUSTER_PARSE_AT_TOKEN);
                    }
                    else if (kind == PARSE_KIND_L_SQUARE)
                    {
                        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_MODE_OPERAND, PARSE_MODE_OFFSETOF_DESIGNATOR, false, &step);
                    }
                    else if (kind == PARSE_KIND_R_PAREN)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_MESSAGE:
                    if (kind == PARSE_KIND_STRING)
                    {
                        step.role = PARSE_ROLE_STRING_NEXT;
                    }
                    else if (kind == PARSE_KIND_R_PAREN)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_close_expected(parser, i, PARSE_KIND_L_PAREN, true);
                    }
                    break;
                case PARSE_MODE_IDENTIFIER_LIST:
                    if (kind == PARSE_KIND_COMMA)
                    {
                        step.role = PARSE_ROLE_SEPARATOR;
                        frame->mode = PARSE_MODE_ITEM;
                        frame->argument += 1;
                    }
                    else if (kind == PARSE_KIND_R_PAREN)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                default: parse_scalar_paren(parser, i, frame, &step); break;
            }
        }
    }
    if (parser->error_token == BUSTER_PARSE_NONE && i < parser->count)
    {
        parse_binding_trace_identifier(parser, i, binding_mode, binding_flags, binding_argument, step.role);
        parse_scalar_emit(parser, i, step.role, step.depth);
        switch (step.event)
        {
            case PARSE_EVENT_OPEN_BLOCK: parse_statement_open_block(parser, false); break;
            case PARSE_EVENT_OPEN_STATEMENT_BLOCK: parse_statement_open_block(parser, true); break;
            case PARSE_EVENT_CLOSE_BLOCK: parse_statement_close_block(parser, i); break;
            case PARSE_EVENT_TERMINATOR: parse_statement_terminator(parser, i); break;
            case PARSE_EVENT_HEAD:
                // Only heads in blocks: a case's `:` closes its group into the block it is in.
                if (parse_frame_is_block(&parser->frames[parser->frame_count - 1]))
                {
                    parse_statement_head(parser, i, step.keyword, step.scope);
                }
                break;
            case PARSE_EVENT_ELSE: parse_statement_else(parser, i); break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's entry point and recovery

// The last token of the top-level declaration an error stopped at `token`: its `;` at file scope, or for an error in a
// function body the `}` that closes the body, with the brackets the frames hold open counted, and a closer that closes
// nothing open skipped (or the last token, if the input ends first). `stack` has room for a bracket per token and frame.
static uint32_t parse_scalar_skip(const ParseScalar* parser, uint32_t token, uint8_t* stack)
{
    uint32_t depth = 0;
    for (uint32_t f = 1; f < parser->frame_count; f += 1)
    {
        const uint8_t opener = parser->kinds[parser->frames[f].opener];
        if (opener == PARSE_KIND_L_PAREN || opener == PARSE_KIND_L_SQUARE || opener == PARSE_KIND_L_BRACE)
        {
            stack[depth] = opener;
            depth += 1;
        }
    }
    const bool body = parser->frame_count > 1 && parser->frames[1].role == PARSE_ROLE_OPEN_BODY;
    uint32_t result = parser->count != 0 ? parser->count - 1 : 0;
    bool done = false;
    for (uint32_t i = token; !done && i < parser->count; i += 1)
    {
        const uint8_t kind = parser->kinds[i];
        const uint8_t opener = kind == PARSE_KIND_R_PAREN ? PARSE_KIND_L_PAREN : kind == PARSE_KIND_R_SQUARE ? PARSE_KIND_L_SQUARE : PARSE_KIND_L_BRACE;
        if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
        {
            stack[depth] = kind;
            depth += 1;
        }
        else if (parse_kind_is_closer(kind) && depth != 0 && stack[depth - 1] == opener)
        {
            depth -= 1;
            done = depth == 0 && body && kind == PARSE_KIND_R_BRACE;
        }
        else if (kind == PARSE_KIND_R_BRACE && depth == 0)
        {
            // A `}` with nothing open: an extraneous one, which the declaration takes.
            done = true;
        }
        else
        {
            done = kind == PARSE_KIND_SEMI && depth == 0;
        }
        result = done ? i : result;
    }
    return result;
}

// When a file-scope C23 attribute name is missing, clang's failed first `]` expectation skips through one `]` and
// leaves the second one for the declaration parser. Report that bracket too, without publishing any of the damaged
// declaration's tree or bindings. Balanced argument tokens in the skipped list cannot supply its closing bracket.
static void parse_scalar_attribute_tail(ParseScalar* parser, uint32_t token, uint32_t end)
{
    const bool prefix = parser->frame_count == 3 && parser->frames[1].role == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER &&
                        (parser->frames[1].continuation == PARSE_MODE_SPECIFIERS || parser->frames[1].continuation == PARSE_MODE_DECLARATOR);
    const bool missing = prefix && parser->frames[2].role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST &&
                         parser->frames[2].mode == PARSE_MODE_STANDARD_ATTRIBUTE_NAME && parser->error_message == PARSE_MESSAGE_EXPECTED_R_SQUARE;
    uint32_t depth = 0;
    bool done = !missing;
    for (uint32_t i = token; !done && i < end && i < parser->count; i += 1)
    {
        const uint8_t kind = parser->kinds[i];
        if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
        {
            parser->brackets[depth] = kind;
            depth += 1;
        }
        else if (parse_kind_is_closer(kind))
        {
            const uint8_t opener = kind == PARSE_KIND_R_PAREN ? PARSE_KIND_L_PAREN : kind == PARSE_KIND_R_SQUARE ? PARSE_KIND_L_SQUARE : PARSE_KIND_L_BRACE;
            if (depth != 0 && parser->brackets[depth - 1] == opener)
            {
                depth -= 1;
            }
            else
            {
                if (depth == 0 && kind == PARSE_KIND_R_SQUARE && parser->kinds[i + 1] == PARSE_KIND_R_SQUARE)
                {
                    parse_scalar_note(parser, i + 1, PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN, BUSTER_PARSE_NONE, true);
                }
                done = true;
            }
        }
        else
        {
            done = depth == 0 && kind == PARSE_KIND_SEMI;
        }
    }
}

// Back to file scope, between top-level declarations.
static void parse_scalar_reset(ParseScalar* parser)
{
    if (parser->binding_trace != 0)
    {
        parser->binding_trace->frame_scopes[0] = 0;
    }
    parser->frame_count = 1;
    parser->frames[0] = (ParseFrame){
        .opener = BUSTER_PARSE_NONE,
        .scope = 0,
        .flags = 0,
        .role = PARSE_ROLE_NONE,
        .mode = PARSE_MODE_ITEM,
        .closer_role = PARSE_ROLE_NONE,
        .keyword = 0,
        .continuation = PARSE_MODE_ITEM,
        .attribute_closer_role = PARSE_ROLE_NONE,
        .attribute_continuation = PARSE_MODE_ITEM,
        .tag_kind = 0,
        .statement = PARSE_ITEM_EXPRESSION,
        .argument = 0,
        .tag_token = BUSTER_PARSE_NONE,
        .name = BUSTER_PARSE_NONE,
        .name_frame = BUSTER_PARSE_NONE,
        .declaration = 0,
        .name_meaning = PARSE_MEANING_ORDINARY,
        .derivation = PARSE_DERIVATION_NONE,
        .comma_role = PARSE_ROLE_COMMA,
    };
    parser->statement_count = 0;
    // Only top-level declarations' last tokens wait across them, and with no R their parent is the root anyway.
    parser->candidate_count = 1;
    parser->waiting_count = 1;
    parser->body_scope = BUSTER_PARSE_NONE;
    parser->kr_declaration = BUSTER_PARSE_NONE;
    parser->typeless_function = 0;
    parser->kept_count = 0;
    parser->error_token = BUSTER_PARSE_NONE;
    parser->error_follow_token = BUSTER_PARSE_NONE;
    parser->error_resumed = false;
    parser->error_resume_delete = false;
    parser->error_resume_end = 0;
    parser->error_resume_colon = false;
    parser->error_probe = false;
}

// The first error of the current top-level declaration, stopped at token `stopped`: its diagnostic, then the rest of
// the declaration skipped. Its tokens become ERROR leaves of the root, the parse goes on at file scope, and the names
// it bound are unbound. Returns the declaration's last token. A file-scope declaration's missing `;` before a declaration
// specifier ends the declaration before it instead, where clang's ParseDeclGroup reads the specifier as the next
// declaration's (#147): the parse goes on there, so that the next declaration's errors are clang's too.
static uint32_t parse_scalar_recover(ParseScalar* parser, uint32_t stopped)
{
    const bool resumed = parser->error_resumed && parser->error_resume_end == 0 && parser->frame_count == 1 &&
                         stopped == parser->error_token && stopped > parser->item_begin;
    if (parser->diagnostic_count < parser->diagnostic_capacity)
    {
        parser->diagnostics[parser->diagnostic_count] = (ParseDiagnostic){
            .token = parser->error_token,
            .argument = parser->error_argument,
            .message = parser->error_message,
            .place = parser->error_place,
            .error = true,
            .note = parser->error_note,
            .resumed = parser->error_resumed,
            .resume_delete = parser->error_resume_delete,
            .resume_end = parser->error_resume_end,
            .resume_colon = parser->error_resume_colon,
            .probe = parser->error_probe,
        };
        parser->diagnostic_count += 1;
    }
    parser->errors += 1;
    if (parser->error_follow_token != BUSTER_PARSE_NONE && parser->diagnostic_count < parser->diagnostic_capacity)
    {
        parser->diagnostics[parser->diagnostic_count] = (ParseDiagnostic){
            .token = parser->error_follow_token,
            .publication = parser->error_token,
            .argument = BUSTER_PARSE_NONE,
            .message = parser->error_follow_message,
            .place = BUSTER_PARSE_AT_TOKEN,
            .error = true,
            .note = BUSTER_PARSE_NONE,
        };
        parser->diagnostic_count += 1;
        parser->errors += 1;
    }
    const uint32_t end = resumed ? stopped - 1 : parse_scalar_skip(parser, stopped, parser->brackets);
    parse_scalar_attribute_tail(parser, stopped, end);
    for (uint32_t t = parser->item_begin; t <= end && t < parser->count; t += 1)
    {
        parser->roles[t] = PARSE_ROLE_ERROR;
        parser->depths[t] = 0;
        parser->parents[t] = BUSTER_PARSE_ROOT;
    }
    parse_scalar_restore(parser, parser->item_log);
    parse_scalar_reset(parser);
    return end;
}

// Parses tokens [0, count) into `tree`, whose arrays have `count` entries, with the scratch arena's memory for its
// stacks. `kinds` and `names` have the padding of ParseTokens around them. The diagnostics go to `diagnostics`, which has
// room for `capacity`, and their number to `*diagnostic_count`.
BUSTER_PARSER_ENTRY ParseScalarResult parse_scalar_traced(const uint8_t* kinds, const uint32_t* names, const uint8_t* flags, uint32_t count, CLanguage language, ParseMeanings* meanings,
                                             ParseTree* tree, Arena* scratch, ParseDiagnostic* diagnostics, uint32_t capacity, uint32_t* diagnostic_count, ParseBindingTrace* trace)
{
    ParseScalarResult result = { .errors = 0, .out_of_memory = false, .retyped = false, .undecided = false };
    const uint64_t mark = scratch->length;
    const uint64_t slots = (uint64_t)count + 2;
    ParseScalar parser = {
        .kinds = kinds,
        .names = names,
        .flags = flags,
        .count = count,
        .language = language,
        .roles = tree->roles,
        .depths = tree->depths,
        .parents = tree->parents,
        .frames = BUSTER_ARENA_PUSH_ARRAY(scratch, ParseFrame, slots),
        .frame_count = 1,
        .candidates = BUSTER_ARENA_PUSH_ARRAY(scratch, ParseCandidate, slots + 1),
        .candidate_count = 1,
        .waiting = BUSTER_ARENA_PUSH_ARRAY(scratch, ParseWaiting, slots + 1),
        .waiting_count = 1,
        .meanings = meanings->meanings,
        .table = meanings->table,
        .log_names = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .log_meanings = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, slots),
        .log_count = 0,
        .kept_names = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .kept_meanings = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, slots),
        .kept_tags = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, slots),
        .kept_tag_names = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .kept_count = 0,
        .body_scope = BUSTER_PARSE_NONE,
        .kr_declaration = BUSTER_PARSE_NONE,
        .statements = BUSTER_ARENA_PUSH_ARRAY(scratch, ParseStatement, slots),
        .statement_count = 0,
        .brackets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, 2 * slots),
        .item_begin = 0,
        .item_log = 0,
        .error_token = BUSTER_PARSE_NONE,
        .error_argument = BUSTER_PARSE_NONE,
        .error_follow_token = BUSTER_PARSE_NONE,
        .error_message = PARSE_MESSAGE_NONE,
        .error_place = BUSTER_PARSE_AT_TOKEN,
        .diagnostics = diagnostics,
        .diagnostic_count = 0,
        .diagnostic_capacity = capacity,
        .errors = 0,
        .undecided = false,
        .tags = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)meanings->capacity + 1),
        .tag_positions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .tag_names = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .tag_hidden = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, slots),
        .tag_count = 0,
        .binding_trace = trace,
    };
    if (parser.frames == 0 || parser.candidates == 0 || parser.waiting == 0 || parser.log_names == 0 || parser.log_meanings == 0 || parser.kept_names == 0 ||
        parser.kept_meanings == 0 || parser.kept_tags == 0 || parser.kept_tag_names == 0 || parser.statements == 0 || parser.brackets == 0 || parser.tags == 0 || parser.tag_positions == 0 || parser.tag_names == 0 ||
        parser.tag_hidden == 0)
    {
        result.out_of_memory = true;
    }
    else
    {
        memset(parser.tags, 0, (uint64_t)meanings->capacity + 1);
        // The stacks' sentinels.
        parser.candidates[0] = (ParseCandidate){ .position = BUSTER_PARSE_NONE, .key = 0 };
        parser.waiting[0] = (ParseWaiting){ .position = BUSTER_PARSE_NONE, .threshold = 0, .left = BUSTER_PARSE_NONE, .left_key = 0 };
        parse_scalar_reset(&parser);
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (parser.frame_count == 1 && parser.frames[0].mode == PARSE_MODE_ITEM)
            {
                parser.item_begin = i;
                parser.item_log = parser.log_count;
            }
            parse_scalar_token(&parser, i);
            if (parser.error_token != BUSTER_PARSE_NONE)
            {
                i = parse_scalar_recover(&parser, i);
            }
        }
        // The end of the input, as a token of its own: whatever is still open is an error there.
        if (parser.frame_count != 1 || parser.frames[0].mode != PARSE_MODE_ITEM)
        {
            parse_scalar_token(&parser, count);
            if (parser.error_token == BUSTER_PARSE_NONE)
            {
                parse_scalar_error(&parser, count, PARSE_MESSAGE_UNEXPECTED_TOKEN, BUSTER_PARSE_AT_TOKEN);
            }
            parse_scalar_recover(&parser, count);
        }
        parse_scalar_finish(&parser);
        // What is still in the log is file scope's: every inner scope has been restored.
        for (uint32_t k = 0; k < parser.log_count; k += 1)
        {
            result.retyped |= parser.log_meanings[k] == PARSE_MEANING_TYPEDEF && parser.meanings[parser.log_names[k]] != PARSE_MEANING_TYPEDEF;
        }
        if (trace != 0)
        {
            trace->point = count;
        }
        parse_scalar_restore(&parser, 0);
        result.errors = parser.errors;
        result.undecided = parser.undecided;
        *diagnostic_count = parser.diagnostic_count;
    }
    arena_pop_to(scratch, mark);
    return result;
}

BUSTER_PARSER_ENTRY ParseScalarResult parse_scalar(const uint8_t* kinds, const uint32_t* names, const uint8_t* flags, uint32_t count, CLanguage language, ParseMeanings* meanings,
                                      ParseTree* tree, Arena* scratch, ParseDiagnostic* diagnostics, uint32_t capacity, uint32_t* diagnostic_count)
{
    return parse_scalar_traced(kinds, names, flags, count, language, meanings, tree, scratch, diagnostics, capacity, diagnostic_count, 0);
}

static const uint8_t parse_role_preds[PARSE_ROLE_COUNT] =
{
    [PARSE_ROLE_NONE] = PARSE_PRED_ITEM,
    [PARSE_ROLE_ERROR] = PARSE_PRED_ITEM,
    [PARSE_ROLE_OPERAND] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_MEMBER] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_STRING_NEXT] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_QUALIFIER_LEFT] = PARSE_PRED_STAR,
    [PARSE_ROLE_LEAF_RIGHT] = PARSE_PRED_LEAF,
    // SPECIFIER: PARSE_PRED_TYPE for a type keyword or a typedef name.
    [PARSE_ROLE_SPECIFIER] = PARSE_PRED_SPECIFIER,
    [PARSE_ROLE_TAG] = PARSE_PRED_TYPE,
    // PREFIX: PARSE_PRED_STAR for `*`.
    [PARSE_ROLE_PREFIX] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_POSTFIX] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_MEMBER_OPERATOR] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_MULTIPLICATIVE] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_ADDITIVE] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_SHIFT] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_RELATIONAL] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_EQUALITY] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_BITWISE_AND] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_BITWISE_XOR] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_BITWISE_OR] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_LOGICAL_AND] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_LOGICAL_OR] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_ASSIGN] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_RANGE] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_BITFIELD] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_COMMA] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_RETURN] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_ASSOCIATION] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_SEPARATOR] = PARSE_PRED_PARAMETER,
    [PARSE_ROLE_ASM_COLON] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_TERMINATOR] = PARSE_PRED_ITEM,
    [PARSE_ROLE_ELSE] = PARSE_PRED_HEAD,
    [PARSE_ROLE_DO] = PARSE_PRED_HEAD,
    [PARSE_ROLE_LABEL] = PARSE_PRED_ITEM,
    [PARSE_ROLE_OPEN_GROUP] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_CALL] = PARSE_PRED_PARAMETER,
    [PARSE_ROLE_OPEN_FUNCTION] = PARSE_PRED_PARAMETER,
    [PARSE_ROLE_OPEN_CAST] = PARSE_PRED_PARAMETER,
    [PARSE_ROLE_OPEN_COMPOUND] = PARSE_PRED_PARAMETER,
    [PARSE_ROLE_OPEN_SIZEOF] = PARSE_PRED_PARAMETER,
    [PARSE_ROLE_OPEN_SPECIFIER] = PARSE_PRED_PARAMETER,
    [PARSE_ROLE_OPEN_ATTRIBUTE] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_ATTRIBUTE_LEFT] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_ATTRIBUTE_LIST] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_ASM] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_KEYWORD] = PARSE_PRED_PARAMETER,
    [PARSE_ROLE_OPEN_CONDITION] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_FOR] = PARSE_PRED_PARAMETER,
    [PARSE_ROLE_OPEN_SUBSCRIPT] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_BLOCK] = PARSE_PRED_ITEM,
    [PARSE_ROLE_OPEN_BODY] = PARSE_PRED_ITEM,
    [PARSE_ROLE_OPEN_STRUCT] = PARSE_PRED_ITEM,
    [PARSE_ROLE_OPEN_ENUM] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_INITIALIZER] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_COMPOUND_BODY] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_TERNARY] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_OPEN_CASE] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_CLOSE_GROUP] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_CALL] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_FUNCTION] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_CAST] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_CLOSE_COMPOUND] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_SIZEOF] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_SPECIFIER] = PARSE_PRED_TYPE,
    [PARSE_ROLE_CLOSE_ATTRIBUTE] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER] = PARSE_PRED_SPECIFIER,
    [PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT] = PARSE_PRED_STAR,
    [PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT] = PARSE_PRED_LEAF,
    [PARSE_ROLE_CLOSE_ATTRIBUTE_LIST] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_ASM] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_KEYWORD] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_CONDITION] = PARSE_PRED_HEAD,
    [PARSE_ROLE_CLOSE_FOR] = PARSE_PRED_HEAD,
    [PARSE_ROLE_CLOSE_SUBSCRIPT] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_BLOCK] = PARSE_PRED_ITEM,
    [PARSE_ROLE_CLOSE_BODY] = PARSE_PRED_ITEM,
    [PARSE_ROLE_CLOSE_STRUCT] = PARSE_PRED_TYPE,
    [PARSE_ROLE_CLOSE_ENUM] = PARSE_PRED_TYPE,
    [PARSE_ROLE_CLOSE_INITIALIZER] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_COMPOUND_BODY] = PARSE_PRED_OPERAND,
    [PARSE_ROLE_CLOSE_TERNARY] = PARSE_PRED_EXPRESSION,
    [PARSE_ROLE_CLOSE_CASE] = PARSE_PRED_ITEM,
};

// The kinds of the next token after which an opener whose contents start a type name is a cast (an operand follows),
// as opposed to an abstract function declarator.
static bool parse_vkind_starts_operand(uint8_t vkind)
{
    return vkind == PARSE_KIND_IDENTIFIER || vkind == PARSE_VKIND_TYPE_NAME || vkind == PARSE_KIND_NUMBER || vkind == PARSE_KIND_CHARACTER ||
           vkind == PARSE_KIND_STRING || vkind == PARSE_KIND_L_PAREN || vkind == PARSE_KIND_MINUS || vkind == PARSE_KIND_PLUS || vkind == PARSE_KIND_EXCLAIM ||
           vkind == PARSE_KIND_TILDE || vkind == PARSE_KIND_STAR || vkind == PARSE_KIND_AMP || vkind == PARSE_KIND_PLUSPLUS || vkind == PARSE_KIND_MINUSMINUS ||
           vkind == PARSE_KIND_AMPAMP || vkind == PARSE_KIND_SIZEOF || vkind == PARSE_KIND_REAL_IMAG || vkind == PARSE_KIND_EXTENSION ||
           vkind == PARSE_KIND_TYPE_ARGUMENT || vkind == PARSE_KIND_GENERIC;
}

static bool parse_vkind_starts_type(uint8_t vkind)
{
    return vkind == PARSE_KIND_TYPE || vkind == PARSE_KIND_STRUCT || vkind == PARSE_KIND_ENUM || vkind == PARSE_KIND_QUALIFIER || vkind == PARSE_KIND_ATOMIC ||
           vkind == PARSE_KIND_TYPEOF || vkind == PARSE_KIND_ATTRIBUTE || vkind == PARSE_VKIND_TYPE_NAME;
}

static bool parse_vkind_starts_declaration(uint8_t vkind)
{
    return parse_vkind_starts_type(vkind) || vkind == PARSE_KIND_STORAGE || vkind == PARSE_KIND_TYPEDEF || vkind == PARSE_KIND_ALIGNAS ||
           vkind == PARSE_KIND_STATIC_ASSERT || vkind == PARSE_KIND_LABEL || vkind == PARSE_KIND_CALLING_CONVENTION || vkind == PARSE_KIND_EXTENSION;
}

// ---------------------------------------------------------------------------------------------------------------------
// K1, brackets: the scalar twin

// The bracket deltas: of ( [ { by kind, for the first pass, and of every group by role, for the second.
static BUSTER_BAKED_FRONTEND int8_t parse_kind_deltas[128];

static BUSTER_BAKED_FRONTEND int8_t parse_role_deltas[128];

static void parse_kernels_init_tables(void)
{
    parse_kind_deltas[PARSE_KIND_L_PAREN] = 1;
    parse_kind_deltas[PARSE_KIND_L_SQUARE] = 1;
    parse_kind_deltas[PARSE_KIND_L_BRACE] = 1;
    parse_kind_deltas[PARSE_KIND_R_PAREN] = -1;
    parse_kind_deltas[PARSE_KIND_R_SQUARE] = -1;
    parse_kind_deltas[PARSE_KIND_R_BRACE] = -1;
    for (uint32_t role = 0; role < PARSE_ROLE_COUNT; role += 1)
    {
        parse_role_deltas[role] = parse_role_rules[role] == PARSE_RULE_OPENER ? 1 : 0;
        for (uint32_t opener = 0; opener < PARSE_ROLE_COUNT; opener += 1)
        {
            if (parse_closer_roles[opener] == role && role != PARSE_ROLE_NONE)
            {
                parse_role_deltas[role] = -1;
            }
        }
    }
}

// The depth before each token and its enclosing opener, from codes (kinds or roles) and their deltas. Returns false if
// a closer has no opener or a group is still open at the end.
static bool parse_brackets_scalar(const uint8_t* codes, const int8_t* deltas, uint32_t count, uint32_t* depths, uint32_t* enclosing, uint32_t* stack)
{
    bool balanced = true;
    uint32_t depth = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const int8_t delta = deltas[codes[i]];
        depths[i] = depth;
        enclosing[i] = depth != 0 ? stack[depth - 1] : BUSTER_PARSE_NONE;
        if (delta > 0)
        {
            stack[depth] = i;
            depth += 1;
        }
        else if (delta < 0 && depth == 0)
        {
            balanced = false;
        }
        else if (delta < 0)
        {
            depth -= 1;
        }
    }
    return balanced && depth == 0;
}

// K1's view of the block of tokens [block, block + live) from their depths and enclosing openers (`codes` and `deltas`
// as for parse_brackets_scalar): the twin's, which the check compares with the vector K1's.
static void parse_bracket_block(const uint8_t* codes, const int8_t* deltas, const uint32_t* depths, const uint32_t* enclosing, uint32_t block, uint32_t live,
                                ParseBracketBlock* out)
{
    uint64_t open = 0;
    int64_t lowest = INT64_MAX;
    for (uint32_t lane = 64; lane-- > 0;)
    {
        const uint32_t i = block + lane;
        const bool alive = lane < live;
        const uint32_t opener = alive ? enclosing[i] : BUSTER_PARSE_NONE;
        const int64_t depth = alive ? depths[i] : 0;
        const int8_t delta = alive ? deltas[codes[i]] : 0;
        out->opener_lanes[lane] = opener != BUSTER_PARSE_NONE && opener >= block && opener - block < 64 ? (uint8_t)(opener - block) : 0xFF;
        out->relative_depths[lane] = (int8_t)(depth - (alive ? (int64_t)depths[block] : depth));
        // Open at the end if no token after it leaves a lower depth than its group's.
        open |= (uint64_t)(alive && delta > 0 && depth < lowest) << lane;
        lowest = alive && depth + delta < lowest ? depth + delta : lowest;
    }
    out->open = open;
}

// Every block's, as parse_brackets_avx512 writes them, from the twin's depths and enclosing openers.
static __attribute__((unused)) void parse_bracket_blocks(const uint8_t* codes, const int8_t* deltas, const uint32_t* depths, const uint32_t* enclosing, uint32_t count, ParseBracketBlock* blocks)
{
    for (uint32_t block = 0; block < count; block += 64)
    {
        parse_bracket_block(codes, deltas, depths, enclosing, block, count - block < 64 ? count - block : 64, &blocks[block / 64]);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// K3, type names

// The token after the group `opener` opens (or after `opener` if it isn't an opener): the first token after it whose
// depth is the opener's again. The typedef pass is the only user, over typedef declarations alone.
static uint32_t parse_after_group(const ParseKernels* kernels, const uint8_t* kinds, uint32_t opener)
{
    const bool group = kinds[opener] == PARSE_KIND_L_PAREN || kinds[opener] == PARSE_KIND_L_SQUARE || kinds[opener] == PARSE_KIND_L_BRACE;
    uint32_t result = opener + 1;
    while (group && kernels->depths[result] > kernels->depths[opener])
    {
        result += 1;
    }
    return result;
}

// The typedef pass records the sparse declarations in source order. A declaration activates after its complete
// declarator, not at its name: a bound in `typedef int T[sizeof(T)]` still sees the outer T. A block declaration's
// interval ends at its enclosing brace. Other scopes, or specifiers this pass cannot read, leave the unit to the
// scalar parser. The bracket matching uses K4's not-yet-live waiting array and never walks nested scopes per token.
static void parse_typedefs(ParseKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count)
{
    kernels->typedef_count = 0;
    // Keywords and matching closers share the existing scan of 64 kinds; resolving the sparse scopes afterwards
    // avoids another full-token traversal or repeated searches for the same enclosing block's end.
    uint32_t next = 0;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __mmask64 live = count - block < 64 ? (((__mmask64)1 << (count - block)) - 1) : ~(__mmask64)0;
        const __m512i kind = _mm512_loadu_si512(kinds + block);
        uint64_t closers = live & (_mm512_cmpeq_epi8_mask(kind, _mm512_set1_epi8(PARSE_KIND_R_BRACE)) |
                                  _mm512_cmpeq_epi8_mask(kind, _mm512_set1_epi8(PARSE_KIND_R_PAREN)) |
                                  _mm512_cmpeq_epi8_mask(kind, _mm512_set1_epi8(PARSE_KIND_R_SQUARE)));
        while (closers != 0)
        {
            const uint32_t closer = block + (uint32_t)__builtin_ctzll(closers);
            closers &= closers - 1;
            const uint32_t opener = kernels->enclosing[closer];
            if (opener != BUSTER_PARSE_NONE)
            {
                kernels->waiting[opener] = closer;
            }
        }
        // A declaration the pass reads may run past later keywords' blocks.
        uint64_t keywords = _mm512_mask_cmpeq_epi8_mask(live, kind, _mm512_set1_epi8(PARSE_KIND_TYPEDEF));
        while (keywords != 0 && !kernels->unsupported)
        {
            const uint32_t t = block + (uint32_t)__builtin_ctzll(keywords);
            keywords &= keywords - 1;
            if (t >= next)
            {
                const uint32_t depth = kernels->depths[t];
                const uint8_t scope = depth == 0 ? BUSTER_PARSE_TYPEDEF_FILE : BUSTER_PARSE_TYPEDEF_SCOPED;
                const uint32_t opener = kernels->enclosing[t];
                const bool block_scope = opener != BUSTER_PARSE_NONE && kinds[opener] == PARSE_KIND_L_BRACE;
                kernels->unsupported |= depth != 0 && !block_scope;
                // What may come before `typedef` in its specifiers: no type specifier.
                uint32_t back = t;
                while (back > 0 && (kinds[back - 1] == PARSE_KIND_EXTENSION || kinds[back - 1] == PARSE_KIND_STORAGE || kinds[back - 1] == PARSE_KIND_QUALIFIER))
                {
                    back -= 1;
                }
                const uint8_t before = back > 0 ? kinds[back - 1] : PARSE_KIND_SEMI;
                if (before != PARSE_KIND_SEMI && before != PARSE_KIND_L_BRACE && before != PARSE_KIND_R_BRACE && before != PARSE_KIND_COLON)
                {
                    kernels->unsupported = true;
                }
                // The specifiers.
                uint32_t i = t + 1;
                bool type_seen = false;
                bool specifiers = true;
                while (specifiers && i < count)
                {
                    const uint8_t kind = kinds[i];
                    if (kind == PARSE_KIND_TYPE)
                    {
                        type_seen = true;
                        i += 1;
                    }
                    else if (kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_EXTENSION || kind == PARSE_KIND_CALLING_CONVENTION ||
                             kind == PARSE_KIND_TYPEDEF || (kind == PARSE_KIND_ATOMIC && kinds[i + 1] != PARSE_KIND_L_PAREN))
                    {
                        i += 1;
                    }
                    else if (kind == PARSE_KIND_ATOMIC || kind == PARSE_KIND_TYPEOF)
                    {
                        type_seen = true;
                        i = parse_after_group(kernels, kinds, i + 1);
                    }
                    else if (kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ATTRIBUTE)
                    {
                        i = parse_after_group(kernels, kinds, i + 1);
                    }
                    else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
                    {
                        type_seen = true;
                        i += 1;
                        while (kinds[i] == PARSE_KIND_ATTRIBUTE)
                        {
                            i = parse_after_group(kernels, kinds, i + 1);
                        }
                        i += kinds[i] == PARSE_KIND_IDENTIFIER;
                        // An enum's underlying type (C23's `enum E : T`), which a typedef's `:` after the tag always is: the
                        // type's specifiers and GNU attributes among them, as parse_scalar_enum_base takes them, then its
                        // body.
                        if (kind == PARSE_KIND_ENUM && kinds[i] == PARSE_KIND_COLON)
                        {
                            i += 1;
                            while (kinds[i] == PARSE_KIND_TYPE || kinds[i] == PARSE_KIND_QUALIFIER || kinds[i] == PARSE_KIND_ATTRIBUTE ||
                                   (kinds[i] == PARSE_KIND_IDENTIFIER && kernels->typedef_names[names[i]] != 0) ||
                                   (kinds[i] == PARSE_KIND_ATOMIC && kinds[i + 1] != PARSE_KIND_L_PAREN))
                            {
                                i = kinds[i] == PARSE_KIND_ATTRIBUTE ? parse_after_group(kernels, kinds, i + 1) : i + 1;
                            }
                        }
                        i = kinds[i] == PARSE_KIND_L_BRACE ? parse_after_group(kernels, kinds, i) : i;
                    }
                    else if (kind == PARSE_KIND_IDENTIFIER && !type_seen && kernels->typedef_names[names[i]] != 0)
                    {
                        type_seen = true;
                        i += 1;
                    }
                    else
                    {
                        specifiers = false;
                    }
                }
                // The declarators, to the `;` at the declaration's depth.
                bool declarators = true;
                while (declarators && i < count)
                {
                    uint32_t name = BUSTER_PARSE_NONE;
                    bool searching = true;
                    while (searching && i < count)
                    {
                        const uint8_t kind = kinds[i];
                        if (kind == PARSE_KIND_STAR || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_CALLING_CONVENTION || kind == PARSE_KIND_L_PAREN)
                        {
                            i += 1;
                        }
                        else if (kind == PARSE_KIND_ATTRIBUTE)
                        {
                            i = parse_after_group(kernels, kinds, i + 1);
                        }
                        else
                        {
                            searching = false;
                            name = kind == PARSE_KIND_IDENTIFIER ? i : BUSTER_PARSE_NONE;
                        }
                    }
                    if (name != BUSTER_PARSE_NONE)
                    {
                        i = name + 1;
                    }
                    while (i < count && !(kernels->depths[i] == depth && (kinds[i] == PARSE_KIND_COMMA || kinds[i] == PARSE_KIND_SEMI)))
                    {
                        i = parse_after_group(kernels, kinds, i);
                    }
                    if (name != BUSTER_PARSE_NONE)
                    {
                        const uint32_t id = names[name];
                        const uint32_t record = kernels->typedef_count++;
                        kernels->typedef_first[id] = kernels->typedef_names[id] == 0 ? name : kernels->typedef_first[id];
                        kernels->typedef_names[id] |= scope;
                        kernels->typedef_log[record] = id;
                        kernels->typedef_tokens[record] = name;
                        kernels->typedef_points[record] = i;
                        kernels->typedef_ends[record] = block_scope ? opener : BUSTER_PARSE_NONE;
                        kernels->typedef_heads[id] = 0;
                        kernels->typedef_scopes[record] = scope;
                    }
                    declarators = i < count && kinds[i] == PARSE_KIND_COMMA;
                    i += 1;
                }
                next = i;
            }
        }
    }
    for (uint32_t record = 0; record < kernels->typedef_count; record += 1)
    {
        const uint32_t id = kernels->typedef_log[record];
        const uint32_t opener = kernels->typedef_ends[record];
        kernels->typedef_ends[record] = opener != BUSTER_PARSE_NONE ? kernels->waiting[opener] : count;
        uint32_t previous = kernels->typedef_heads[id];
        while (previous != 0 && kernels->typedef_ends[previous - 1] <= kernels->typedef_points[record])
        {
            previous = kernels->typedef_previous[previous - 1];
        }
        kernels->typedef_previous[record] = previous;
        kernels->typedef_heads[id] = record + 1;
    }
}

// Clears the file's typedef names, for the next file.
static void parse_typedefs_clear(ParseKernels* kernels)
{
    for (uint32_t i = 0; i < kernels->typedef_count; i += 1)
    {
        kernels->typedef_names[kernels->typedef_log[i]] = 0;
        kernels->typedef_heads[kernels->typedef_log[i]] = 0;
    }
}

// A classification replay starts with only the predefined typedefs. Clearing just the names in this unit keeps the
// heads reusable across translation units whose process-wide identifier table may be much larger than the source.
static void parse_classify_begin(ParseKernels* kernels)
{
    for (uint32_t i = 0; i < kernels->typedef_count; i += 1)
    {
        kernels->typedef_heads[kernels->typedef_log[i]] = 0;
    }
}

// Activate declarations through this identifier, then remove expired heads of its name. Each declaration is pushed
// once per replay and each expired head popped once; nested aliases and repeated sibling scopes cannot cause a walk
// per use. The previous links are immutable so the vector classifier and its scalar twin can replay independently.
BUSTER_INLINE bool parse_classify_typedef(ParseKernels* kernels, uint32_t token, uint32_t name, uint32_t* visible)
{
    while (*visible < kernels->typedef_count && kernels->typedef_points[*visible] <= token)
    {
        kernels->typedef_heads[kernels->typedef_log[*visible]] = *visible + 1;
        *visible += 1;
    }
    uint32_t head = kernels->typedef_heads[name];
    while (head != 0 && kernels->typedef_ends[head - 1] <= token)
    {
        head = kernels->typedef_previous[head - 1];
    }
    kernels->typedef_heads[name] = head;
    return head != 0 || (kernels->typedef_names[name] != 0 && kernels->typedef_first[name] == 0);
}

// The declarator names of the typedef declarations are identifiers being declared, not uses of type names.
static void parse_classify_declarators(ParseKernels* kernels)
{
    for (uint32_t i = 0; i < kernels->typedef_count; i += 1)
    {
        const uint32_t token = kernels->typedef_tokens[i];
        kernels->vkinds[token] = PARSE_KIND_IDENTIFIER;
        kernels->flags[token] = 0;
    }
}

// K3's classification, the scalar twin. Exact source intervals distinguish type names before, inside and after each
// block. K2 flags an ordinary declarator that shadows one; the scalar fallback then resolves that declaration's body.
static void parse_classify_scalar(ParseKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count)
{
    parse_classify_begin(kernels);
    uint32_t visible = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const bool type = kinds[i] == PARSE_KIND_IDENTIFIER && parse_classify_typedef(kernels, i, names[i], &visible);
        kernels->vkinds[i] = type ? PARSE_VKIND_TYPE_NAME : kinds[i];
        kernels->flags[i] = 0;
    }
    parse_classify_declarators(kernels);
}

// ---------------------------------------------------------------------------------------------------------------------
// K2, roles: the scalar twin

static uint8_t parse_pred_of(uint8_t role, uint8_t vkind)
{
    uint8_t result = parse_role_preds[role];
    if (role == PARSE_ROLE_SPECIFIER && (vkind == PARSE_KIND_TYPE || vkind == PARSE_VKIND_TYPE_NAME))
    {
        result = PARSE_PRED_TYPE;
    }
    else if (role == PARSE_ROLE_PREFIX && vkind == PARSE_KIND_STAR)
    {
        result = PARSE_PRED_STAR;
    }
    return result;
}

// Whether a role is a type specifier, for the typed state of a declaration's specifiers.
static bool parse_role_is_type_specifier(uint8_t role, uint8_t vkind)
{
    return (role == PARSE_ROLE_SPECIFIER && (vkind == PARSE_KIND_TYPE || vkind == PARSE_VKIND_TYPE_NAME)) || role == PARSE_ROLE_TAG ||
           role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_SPECIFIER;
}

// Whether a role carries a declaration's typed state through: specifiers that aren't type specifiers, the keywords
// before struct, typeof and attribute groups.
static bool parse_role_propagates_typed(uint8_t role)
{
    return role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_LEAF_RIGHT;
}

// The closer's role for the opener `opener` (whose role is final), and for a cast candidate the opener's final role.
static uint8_t parse_closer_role(ParseKernels* kernels, uint32_t closer)
{
    const uint32_t opener = kernels->enclosing[closer];
    uint8_t result = PARSE_ROLE_ERROR;
    if (opener != BUSTER_PARSE_NONE)
    {
        const uint8_t opener_role = kernels->roles[opener];
        result = parse_closer_roles[opener_role];
        if (opener_role == PARSE_ROLE_OPEN_CAST)
        {
            const uint8_t after = kernels->vkinds[closer + 1];
            const uint8_t role = after == PARSE_KIND_L_BRACE ? PARSE_ROLE_OPEN_COMPOUND : parse_vkind_starts_operand(after) ? PARSE_ROLE_OPEN_CAST : PARSE_ROLE_OPEN_FUNCTION;
            kernels->roles[opener] = role;
            result = parse_closer_roles[role];
        }
        else if (result == PARSE_ROLE_NONE)
        {
            result = PARSE_ROLE_ERROR;
        }
    }
    return result;
}

// The keyword of a tagged type whose body starts after `token`: back over the tag and the attribute groups between them
// (`struct __attribute__((packed)) S {`), through each group's closer's opener. PARSE_KIND_END if there is none.
static uint8_t parse_tag_keyword(const ParseKernels* kernels, uint32_t token)
{
    uint32_t t = token;
    bool searching = true;
    uint8_t result = PARSE_KIND_END;
    while (searching)
    {
        const uint8_t kind = kernels->vkinds[t];
        const uint32_t opener = kind == PARSE_KIND_R_PAREN ? kernels->enclosing[t] : BUSTER_PARSE_NONE;
        if (opener != BUSTER_PARSE_NONE && opener >= 2 && kernels->vkinds[opener - 1] == PARSE_KIND_ATTRIBUTE)
        {
            t = opener - 2;
        }
        else if ((kind == PARSE_KIND_IDENTIFIER || kind == PARSE_VKIND_TYPE_NAME) && t > 0)
        {
            t -= 1;
        }
        else
        {
            searching = false;
            result = kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ? kind : PARSE_KIND_END;
        }
    }
    return result;
}

// A unary operator's role where no operand ends before it: a prefix's, but after goto `return`'s, the role of a computed
// goto's `*`, which takes the whole expression after it (the scalar parser's PARSE_MODE_GOTO; any other operator there is
// an error, which the checks flag).
static uint8_t parse_unary_role(uint8_t previous)
{
    return previous == PARSE_KIND_GOTO ? PARSE_ROLE_RETURN : PARSE_ROLE_PREFIX;
}

// The role of token `i` from the rules, given the roles before it. Sets *flag if the rules can't be sure.
static uint8_t parse_role_rule(ParseKernels* kernels, uint32_t i, bool typed, bool* flag)
{
    const uint8_t* vk = kernels->vkinds;
    const uint8_t kind = vk[i];
    // The padding's PARSE_KIND_END before token 0 (with a signed index: `i - 1` would wrap past it to 2^32 - 1).
    const uint8_t previous = vk[(int64_t)i - 1];
    const uint8_t before = vk[(int64_t)i - 2];
    const uint8_t next = vk[i + 1];
    const uint8_t previous_role = i > 0 ? kernels->roles[i - 1] : PARSE_ROLE_NONE;
    const uint8_t pred = i > 0 ? parse_pred_of(previous_role, previous) : PARSE_PRED_ITEM;
    const uint8_t before_pred = i > 1 ? parse_pred_of(kernels->roles[i - 2], before) : PARSE_PRED_ITEM;
    const uint32_t opener = kernels->enclosing[i];
    const uint8_t group = opener != BUSTER_PARSE_NONE ? kernels->roles[opener] : PARSE_ROLE_NONE;
    const bool operand = pred == PARSE_PRED_OPERAND;
    const bool after_asm = previous == PARSE_KIND_ASM || (previous_role == PARSE_ROLE_LEAF_RIGHT && (previous == PARSE_KIND_QUALIFIER || previous == PARSE_KIND_STORAGE ||
                                                                                                    previous == PARSE_KIND_GOTO));
    uint8_t result = PARSE_ROLE_ERROR;
    if (group == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && (kind >= PARSE_KIND_TYPE || kind == PARSE_KIND_IDENTIFIER))
    {
        // An attribute's name, whatever its spelling.
        result = PARSE_ROLE_OPERAND;
    }
    else
    {
        switch (kind)
        {
            case PARSE_KIND_NUMBER:
            case PARSE_KIND_CHARACTER: result = PARSE_ROLE_OPERAND; break;
            case PARSE_KIND_STRING: result = previous == PARSE_KIND_STRING ? PARSE_ROLE_STRING_NEXT : PARSE_ROLE_OPERAND; break;
            case PARSE_KIND_IDENTIFIER:
                if (previous == PARSE_KIND_PERIOD || previous == PARSE_KIND_ARROW)
                {
                    result = PARSE_ROLE_MEMBER;
                }
                else if (previous == PARSE_KIND_STRUCT || previous == PARSE_KIND_ENUM || previous_role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT)
                {
                    result = next == PARSE_KIND_L_BRACE ? PARSE_ROLE_LEAF_RIGHT : PARSE_ROLE_TAG;
                }
                else
                {
                    result = PARSE_ROLE_OPERAND;
                }
                break;
            case PARSE_VKIND_TYPE_NAME:
                if (previous == PARSE_KIND_PERIOD || previous == PARSE_KIND_ARROW)
                {
                    result = PARSE_ROLE_MEMBER;
                }
                else if (previous == PARSE_KIND_STRUCT || previous == PARSE_KIND_ENUM || previous_role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT)
                {
                    result = next == PARSE_KIND_L_BRACE ? PARSE_ROLE_LEAF_RIGHT : PARSE_ROLE_TAG;
                }
                else if (previous == PARSE_KIND_GOTO)
                {
                    result = PARSE_ROLE_OPERAND;
                }
                else if (next == PARSE_KIND_COLON && (pred == PARSE_PRED_ITEM || pred == PARSE_PRED_HEAD) && (group == PARSE_ROLE_OPEN_BLOCK || group == PARSE_ROLE_OPEN_BODY))
                {
                    // A label spelled like a type name.
                    result = PARSE_ROLE_OPERAND;
                }
                else if (pred == PARSE_PRED_ITEM || pred == PARSE_PRED_PARAMETER || (pred == PARSE_PRED_SPECIFIER && !typed))
                {
                    result = PARSE_ROLE_SPECIFIER;
                }
                else
                {
                    // A declarator spelled like a type name, or one in an expression: it hides the typedef.
                    result = PARSE_ROLE_OPERAND;
                    *flag = true;
                }
                break;
            case PARSE_KIND_TYPE:
            case PARSE_KIND_TYPEDEF:
            case PARSE_KIND_LABEL: result = PARSE_ROLE_SPECIFIER; break;
            case PARSE_KIND_QUALIFIER:
            case PARSE_KIND_CALLING_CONVENTION:
                result = after_asm ? PARSE_ROLE_LEAF_RIGHT : pred == PARSE_PRED_STAR ? PARSE_ROLE_QUALIFIER_LEFT : PARSE_ROLE_SPECIFIER;
                break;
            case PARSE_KIND_STORAGE: result = after_asm ? PARSE_ROLE_LEAF_RIGHT : PARSE_ROLE_SPECIFIER; break;
            case PARSE_KIND_ATOMIC:
                result = next == PARSE_KIND_L_PAREN ? PARSE_ROLE_LEAF_RIGHT : pred == PARSE_PRED_STAR ? PARSE_ROLE_QUALIFIER_LEFT : PARSE_ROLE_SPECIFIER;
                break;
            case PARSE_KIND_EXTENSION: result = parse_vkind_starts_declaration(next) ? PARSE_ROLE_SPECIFIER : PARSE_ROLE_PREFIX; break;
            case PARSE_KIND_GENERIC:
                result = PARSE_ROLE_LEAF_RIGHT;
                *flag = true;
                break;
            case PARSE_KIND_ATTRIBUTE: result = pred == PARSE_PRED_STAR ? PARSE_ROLE_QUALIFIER_LEFT : PARSE_ROLE_LEAF_RIGHT; break;
            case PARSE_KIND_STRUCT:
            case PARSE_KIND_ENUM:
            case PARSE_KIND_TYPEOF:
            case PARSE_KIND_ALIGNAS:
            case PARSE_KIND_ASM:
            case PARSE_KIND_TYPE_ARGUMENT:
            case PARSE_KIND_STATIC_ASSERT:
            case PARSE_KIND_IF:
            case PARSE_KIND_WHILE:
            case PARSE_KIND_SWITCH:
            case PARSE_KIND_FOR: result = PARSE_ROLE_LEAF_RIGHT; break;
            case PARSE_KIND_SIZEOF:
            case PARSE_KIND_REAL_IMAG: result = PARSE_ROLE_PREFIX; break;
            case PARSE_KIND_ELSE: result = PARSE_ROLE_ELSE; break;
            case PARSE_KIND_DO: result = PARSE_ROLE_DO; break;
            case PARSE_KIND_CASE:
            case PARSE_KIND_DEFAULT: result = PARSE_ROLE_OPEN_CASE; break;
            case PARSE_KIND_GOTO: result = after_asm ? PARSE_ROLE_LEAF_RIGHT : PARSE_ROLE_RETURN; break;
            case PARSE_KIND_RETURN:
            case PARSE_KIND_BREAK:
            case PARSE_KIND_CONTINUE: result = PARSE_ROLE_RETURN; break;
            case PARSE_KIND_SEMI: result = PARSE_ROLE_TERMINATOR; break;
            case PARSE_KIND_COMMA:
                result = group == PARSE_ROLE_NONE || group == PARSE_ROLE_OPEN_BLOCK || group == PARSE_ROLE_OPEN_BODY || group == PARSE_ROLE_OPEN_STRUCT ||
                         group == PARSE_ROLE_OPEN_FOR ? PARSE_ROLE_COMMA : PARSE_ROLE_SEPARATOR;
                break;
            case PARSE_KIND_QUESTION: result = PARSE_ROLE_OPEN_TERNARY; break;
            case PARSE_KIND_COLON:
                if (group == PARSE_ROLE_OPEN_ASM)
                {
                    result = PARSE_ROLE_ASM_COLON;
                }
                else if (group == PARSE_ROLE_OPEN_STRUCT)
                {
                    result = PARSE_ROLE_BITFIELD;
                }
                else
                {
                    // The closer of ? or case, or a label: parse_secondary decides.
                    result = PARSE_ROLE_CLOSE_TERNARY;
                }
                break;
            case PARSE_KIND_ELLIPSIS:
                // In a parameter list (a cast candidate may turn out to be one), else a range.
                result = group == PARSE_ROLE_OPEN_CALL || group == PARSE_ROLE_OPEN_FUNCTION || group == PARSE_ROLE_OPEN_CAST ? PARSE_ROLE_OPERAND : PARSE_ROLE_RANGE;
                break;
            case PARSE_KIND_STAR: result = operand ? PARSE_ROLE_MULTIPLICATIVE : parse_unary_role(previous); break;
            case PARSE_KIND_AMP: result = operand ? PARSE_ROLE_BITWISE_AND : parse_unary_role(previous); break;
            case PARSE_KIND_PLUS:
            case PARSE_KIND_MINUS: result = operand ? PARSE_ROLE_ADDITIVE : parse_unary_role(previous); break;
            case PARSE_KIND_AMPAMP: result = operand ? PARSE_ROLE_LOGICAL_AND : parse_unary_role(previous); break;
            case PARSE_KIND_PLUSPLUS:
            case PARSE_KIND_MINUSMINUS: result = operand ? PARSE_ROLE_POSTFIX : parse_unary_role(previous); break;
            case PARSE_KIND_EXCLAIM:
            case PARSE_KIND_TILDE: result = PARSE_ROLE_PREFIX; break;
            case PARSE_KIND_PERIOD:
            case PARSE_KIND_ARROW: result = PARSE_ROLE_MEMBER_OPERATOR; break;
            case PARSE_KIND_L_SQUARE: result = PARSE_ROLE_OPEN_SUBSCRIPT; break;
            case PARSE_KIND_L_PAREN:
                if (previous == PARSE_KIND_IF || previous == PARSE_KIND_WHILE || previous == PARSE_KIND_SWITCH)
                {
                    result = PARSE_ROLE_OPEN_CONDITION;
                }
                else if (previous == PARSE_KIND_FOR)
                {
                    result = PARSE_ROLE_OPEN_FOR;
                }
                else if (previous == PARSE_KIND_SIZEOF && next != PARSE_KIND_L_BRACE)
                {
                    result = PARSE_ROLE_OPEN_SIZEOF;
                }
                else if (previous == PARSE_KIND_TYPEOF || previous == PARSE_KIND_ALIGNAS || previous == PARSE_KIND_ATOMIC)
                {
                    result = PARSE_ROLE_OPEN_SPECIFIER;
                }
                else if (previous == PARSE_KIND_ATTRIBUTE)
                {
                    // Where the attribute is, by the token before its keyword.
                    result = before_pred == PARSE_PRED_OPERAND ? PARSE_ROLE_OPEN_ATTRIBUTE : before_pred == PARSE_PRED_STAR ? PARSE_ROLE_OPEN_ATTRIBUTE_LEFT :
                             before_pred == PARSE_PRED_LEAF ? PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT : PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER;
                }
                else if (previous == PARSE_KIND_L_PAREN && before == PARSE_KIND_ATTRIBUTE)
                {
                    result = PARSE_ROLE_OPEN_ATTRIBUTE_LIST;
                }
                else if (previous == PARSE_KIND_ASM)
                {
                    // An asm statement at the start of a statement or a file-scope item, else an asm label.
                    result = before_pred == PARSE_PRED_ITEM || before_pred == PARSE_PRED_HEAD ? PARSE_ROLE_OPEN_ASM : PARSE_ROLE_OPEN_ATTRIBUTE;
                }
                else if (after_asm)
                {
                    result = PARSE_ROLE_OPEN_ASM;
                }
                else if (previous == PARSE_KIND_TYPE_ARGUMENT || previous == PARSE_KIND_GENERIC || previous == PARSE_KIND_STATIC_ASSERT)
                {
                    result = PARSE_ROLE_OPEN_KEYWORD;
                }
                else if (operand)
                {
                    result = PARSE_ROLE_OPEN_CALL;
                }
                else if (parse_vkind_starts_type(next))
                {
                    // A cast, a compound literal's type or an abstract function declarator, which its closer decides.
                    result = PARSE_ROLE_OPEN_CAST;
                }
                else if ((pred == PARSE_PRED_TYPE || pred == PARSE_PRED_SPECIFIER || pred == PARSE_PRED_STAR) && (next == PARSE_KIND_R_PAREN || next == PARSE_KIND_ELLIPSIS))
                {
                    result = PARSE_ROLE_OPEN_FUNCTION;
                }
                else
                {
                    result = PARSE_ROLE_OPEN_GROUP;
                }
                break;
            case PARSE_KIND_L_BRACE:
                if (previous == PARSE_KIND_EQUAL)
                {
                    result = PARSE_ROLE_OPEN_INITIALIZER;
                }
                else if (previous == PARSE_KIND_STRUCT || ((previous == PARSE_KIND_IDENTIFIER || previous == PARSE_VKIND_TYPE_NAME) && before == PARSE_KIND_STRUCT))
                {
                    result = PARSE_ROLE_OPEN_STRUCT;
                }
                else if (previous == PARSE_KIND_ENUM || ((previous == PARSE_KIND_IDENTIFIER || previous == PARSE_VKIND_TYPE_NAME) && before == PARSE_KIND_ENUM))
                {
                    result = PARSE_ROLE_OPEN_ENUM;
                }
                else if (previous_role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT || (previous_role == PARSE_ROLE_LEAF_RIGHT && (previous == PARSE_KIND_IDENTIFIER ||
                                                                                                            previous == PARSE_VKIND_TYPE_NAME)))
                {
                    // A tagged type's body after attributes: `struct __attribute__((packed)) S {`.
                    const uint8_t keyword = parse_tag_keyword(kernels, i - 1);
                    result = keyword == PARSE_KIND_ENUM ? PARSE_ROLE_OPEN_ENUM : PARSE_ROLE_OPEN_STRUCT;
                    *flag = keyword == PARSE_KIND_END;
                }
                else if (previous_role == PARSE_ROLE_CLOSE_CONDITION || previous_role == PARSE_ROLE_CLOSE_FOR || previous == PARSE_KIND_L_PAREN)
                {
                    result = PARSE_ROLE_OPEN_BLOCK;
                }
                else if (previous_role == PARSE_ROLE_CLOSE_CALL || previous_role == PARSE_ROLE_CLOSE_FUNCTION)
                {
                    result = PARSE_ROLE_OPEN_BODY;
                }
                else if (previous_role == PARSE_ROLE_CLOSE_COMPOUND)
                {
                    result = PARSE_ROLE_OPEN_COMPOUND_BODY;
                }
                else if ((previous == PARSE_KIND_L_BRACE || previous == PARSE_KIND_COMMA) && (group == PARSE_ROLE_OPEN_INITIALIZER || group == PARSE_ROLE_OPEN_COMPOUND_BODY))
                {
                    result = PARSE_ROLE_OPEN_INITIALIZER;
                }
                else if (pred == PARSE_PRED_ITEM || pred == PARSE_PRED_HEAD)
                {
                    result = PARSE_ROLE_OPEN_BLOCK;
                }
                else
                {
                    result = PARSE_ROLE_OPEN_BLOCK;
                    *flag = true;
                }
                break;
            case PARSE_KIND_R_PAREN:
            case PARSE_KIND_R_SQUARE:
            case PARSE_KIND_R_BRACE: result = parse_closer_role(kernels, i); break;
            default:
                result = parse_binary_role(kind);
                if (result == PARSE_ROLE_NONE)
                {
                    result = PARSE_ROLE_ERROR;
                    *flag = true;
                }
                break;
        }
    }
    if (result == PARSE_ROLE_ERROR)
    {
        *flag = true;
    }
    return result;
}

// K2, the scalar twin: every token's role in source order, so that each rule sees the final roles before it. The typed
// state of a declaration's specifiers (whether a type specifier came before) runs along with it: a type specifier sets
// it, the other specifiers and the keywords before struct and attribute groups carry it, an attribute's closer takes
// it from before the attribute, and anything else clears it.
static void parse_roles_scalar(ParseKernels* kernels, uint32_t count, uint8_t* typed)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const bool typed_before = i > 0 && typed[i - 1];
        bool flag = false;
        const uint8_t role = parse_role_rule(kernels, i, typed_before, &flag);
        kernels->roles[i] = role;
        kernels->flags[i] |= flag;
        const uint32_t opener = kernels->enclosing[i];
        if (role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER && opener != BUSTER_PARSE_NONE && opener >= 2)
        {
            typed[i] = typed[opener - 2];
        }
        else
        {
            typed[i] = parse_role_is_type_specifier(role, kernels->vkinds[i]) || (parse_role_propagates_typed(role) && typed_before);
        }
    }
}

// K1's second part, the groups of ? : and case ... : (the secondary brackets), over their tokens only: each `:` that
// isn't an asm operand's or a bit-field's closes the innermost ? or case still open in the same group of ( [ {, or is a
// label if none is (a `:` it has resolved before is resolved again the same way). Then the depth of the secondary
// brackets, added to the depth of ( [ {, gives every token's node depth for K4. `secondary` has a slot per token for
// the matching stack.
static void parse_secondary_scalar(ParseKernels* kernels, uint32_t count, uint32_t* secondary)
{
    uint32_t open = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t role = kernels->roles[i];
        if (role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_OPEN_CASE)
        {
            secondary[open] = i;
            open += 1;
        }
        else if (role == PARSE_ROLE_CLOSE_TERNARY || role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_LABEL)
        {
            const uint32_t depth = kernels->depths[i];
            // Openers left open in a group that has ended: invalid C, for the scalar parser to report.
            while (open != 0 && kernels->depths[secondary[open - 1]] > depth)
            {
                kernels->flags[secondary[open - 1]] = 1;
                kernels->unbalanced = true;
                open -= 1;
            }
            if (open != 0 && kernels->depths[secondary[open - 1]] == depth)
            {
                const uint32_t opener = secondary[open - 1];
                open -= 1;
                kernels->roles[i] = kernels->roles[opener] == PARSE_ROLE_OPEN_CASE ? PARSE_ROLE_CLOSE_CASE : PARSE_ROLE_CLOSE_TERNARY;
            }
            else
            {
                const uint8_t previous = kernels->vkinds[(int64_t)i - 1];
                kernels->roles[i] = PARSE_ROLE_LABEL;
                kernels->flags[i] |= previous != PARSE_KIND_IDENTIFIER && previous != PARSE_VKIND_TYPE_NAME;
            }
        }
    }
    while (open != 0)
    {
        kernels->flags[secondary[open - 1]] = 1;
        kernels->unbalanced = true;
        open -= 1;
    }
    // The node depths: the depth of ( [ { and of ? : and case : groups before each token, less one for a closer. Every
    // closer here has its opener (or the file is unbalanced, and the scalar parser takes it), so the sum never goes
    // below 0.
    uint32_t inner = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t role = kernels->roles[i];
        const uint32_t before = inner;
        inner += role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_OPEN_CASE;
        inner -= role == PARSE_ROLE_CLOSE_TERNARY || role == PARSE_ROLE_CLOSE_CASE;
        kernels->node_depths[i] = kernels->depths[i] + before - (parse_role_deltas[role] < 0);
    }
}

// The roles that depend on whether a `:` before them ends a case label or a label (a statement starts) or a ternary's
// middle (an operand follows), which the first K2 pass guessed as the latter: a `{` after a case label is a block; a
// typedef name there stays flagged, since what follows it was decided from the guess.
static void parse_after_colons(ParseKernels* kernels, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const uint8_t previous = kernels->roles[i - 1];
        if ((previous == PARSE_ROLE_CLOSE_CASE || previous == PARSE_ROLE_LABEL) && kernels->vkinds[i] == PARSE_KIND_L_BRACE)
        {
            kernels->roles[i] = PARSE_ROLE_OPEN_BLOCK;
            kernels->flags[i] = 0;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// K4, parents: the scalar twin

// A token's node depth: a closer's is outside its group.
static uint32_t parse_node_depth(const ParseKernels* kernels, uint32_t token)
{
    return kernels->node_depths[token];
}

BUSTER_INLINE void parse_parents_resolve(ParseKernels* kernels, uint32_t token, uint32_t right, uint32_t right_key)
{
    const uint8_t role = kernels->roles[token];
    uint32_t parent;
    if (parse_role_rules[role] == PARSE_RULE_RIGHT || parse_role_rules[role] == PARSE_RULE_OPENER)
    {
        parent = right;
    }
    else
    {
        parent = parse_select_keys(kernels->lefts[token], kernels->left_key_values[token], right, right_key);
    }
    kernels->parents[token] = parent;
}

// K4, the scalar twin: L and R of every token with the two stacks of parse_scalar_emit, then each role's rule; an
// opener's parent is its closer.
static void parse_parents_scalar(ParseKernels* kernels, uint32_t count)
{
    uint32_t left_count = 0;
    uint32_t right_count = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t role = kernels->roles[i];
        const uint32_t base = parse_node_depth(kernels, i) * BUSTER_PARSE_LEVELS;
        const uint32_t key = base + parse_role_levels[role];
        const uint8_t rule = parse_role_rules[role];
        while (right_count != 0 && kernels->waiting_thresholds[right_count - 1] > key)
        {
            right_count -= 1;
            parse_parents_resolve(kernels, kernels->waiting[right_count], i, key);
        }
        const uint32_t left_threshold = base + parse_role_left_thresholds[role];
        while (left_count != 0 && kernels->stack_keys[left_count - 1] >= left_threshold)
        {
            left_count -= 1;
        }
        const uint32_t left = left_count != 0 ? kernels->stack[left_count - 1] : BUSTER_PARSE_NONE;
        const uint32_t left_key = left_count != 0 ? kernels->stack_keys[left_count - 1] : 0;
        while (left_count != 0 && kernels->stack_keys[left_count - 1] >= key)
        {
            left_count -= 1;
        }
        kernels->stack[left_count] = i;
        kernels->stack_keys[left_count] = key;
        left_count += 1;
        if (rule == PARSE_RULE_LEFT)
        {
            kernels->parents[i] = left;
        }
        else
        {
            kernels->lefts[i] = left;
            kernels->left_key_values[i] = left_key;
            kernels->waiting[right_count] = i;
            kernels->waiting_thresholds[right_count] = base + parse_role_right_thresholds[role];
            right_count += 1;
        }
    }
    while (right_count != 0)
    {
        right_count -= 1;
        parse_parents_resolve(kernels, kernels->waiting[right_count], BUSTER_PARSE_NONE, 0);
    }
}

static ParseStatement* parse_k5_top(ParseStatementStack* stack)
{
    return stack->count != 0 ? &stack->items[stack->count - 1] : 0;
}

static void parse_k5_complete(ParseStatementStack* stack)
{
    bool completing = true;
    while (completing)
    {
        ParseStatement* top = parse_k5_top(stack);
        completing = top != 0 && top->state == PARSE_STATEMENT_STARTED;
        if (completing && top->kind == PARSE_KIND_IF)
        {
            top->state = PARSE_STATEMENT_IF_DONE;
            completing = false;
        }
        else if (completing && top->kind == PARSE_KIND_DO)
        {
            top->state = PARSE_STATEMENT_DO_DONE;
            completing = false;
        }
        else if (completing)
        {
            stack->count -= 1;
        }
    }
}

static void parse_k5_flush(ParseStatementStack* stack)
{
    ParseStatement* top = parse_k5_top(stack);
    while (top != 0 && top->state == PARSE_STATEMENT_IF_DONE)
    {
        stack->count -= 1;
        parse_k5_complete(stack);
        top = parse_k5_top(stack);
    }
}

static uint32_t parse_k5_owner(ParseStatementStack* stack)
{
    uint32_t result = BUSTER_PARSE_NONE;
    ParseStatement* top = parse_k5_top(stack);
    if (top != 0 && top->state == PARSE_STATEMENT_WAITING)
    {
        result = top->token;
        top->state = PARSE_STATEMENT_STARTED;
    }
    return result;
}

// One token through the statement stack, if it is a statement-level token of a block: the stack sets the parents of the
// statements heads own, of `else` and of a do-statement's parts. Returns false if it is an else without an if, or any
// other than its while (then its `;`) after a do's statement.
static bool parse_statement_event(ParseKernels* kernels, ParseStatementStack* stack, uint32_t i)
{
    bool result = true;
    const uint8_t role = kernels->roles[i];
    const uint32_t opener = kernels->enclosing[i];
    const uint8_t group = opener != BUSTER_PARSE_NONE ? kernels->roles[opener] : PARSE_ROLE_NONE;
    // A closer's statement context is its opener's; the enclosing groups are those of ( [ { only, since no statement is
    // inside a ? : or case group.
    const uint32_t outer = opener != BUSTER_PARSE_NONE ? kernels->enclosing[opener] : BUSTER_PARSE_NONE;
    const uint8_t outer_group = outer != BUSTER_PARSE_NONE ? kernels->roles[outer] : PARSE_ROLE_NONE;
    const bool in_block = group == PARSE_ROLE_OPEN_BLOCK || group == PARSE_ROLE_OPEN_BODY;
    const bool closer_in_block = outer_group == PARSE_ROLE_OPEN_BLOCK || outer_group == PARSE_ROLE_OPEN_BODY;
    // After a do's statement comes its while (`while (...)`), and right after that its `;`: any other event is an error.
    const bool event = role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY ||
                       (role == PARSE_ROLE_TERMINATOR && in_block) || ((role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR) && closer_in_block) ||
                       ((role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_DO) && in_block);
    if (event)
    {
        parse_k5_flush(stack);
        const ParseStatement* waiting = parse_k5_top(stack);
        const bool its_while = role == PARSE_ROLE_CLOSE_CONDITION && closer_in_block && kernels->vkinds[opener - 1] == PARSE_KIND_WHILE;
        const bool its_end = role == PARSE_ROLE_TERMINATOR && in_block && waiting != 0 && i == waiting->extra + 1;
        result = waiting == 0 || ((waiting->state != PARSE_STATEMENT_DO_DONE || its_while) && (waiting->state != PARSE_STATEMENT_DO_WHILE || its_end));
    }
    if (role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY)
    {
        const bool statement = role == PARSE_ROLE_OPEN_BLOCK && in_block;
        uint32_t owner = BUSTER_PARSE_NONE;
        if (statement)
        {
            parse_k5_flush(stack);
            owner = parse_k5_owner(stack);
        }
        stack->items[stack->count] = (ParseStatement){ .token = BUSTER_PARSE_NONE, .owner = owner, .extra = BUSTER_PARSE_NONE, .state = PARSE_STATEMENT_BLOCK, .kind = statement };
        stack->count += 1;
    }
    else if (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY)
    {
        parse_k5_flush(stack);
        ParseStatement* top = parse_k5_top(stack);
        while (top != 0 && top->state != PARSE_STATEMENT_BLOCK)
        {
            stack->count -= 1;
            top = parse_k5_top(stack);
        }
        if (top != 0)
        {
            const bool statement = top->kind != 0;
            if (top->owner != BUSTER_PARSE_NONE)
            {
                kernels->parents[i] = top->owner;
            }
            stack->count -= 1;
            if (statement)
            {
                parse_k5_complete(stack);
            }
        }
    }
    else if (role == PARSE_ROLE_TERMINATOR && in_block)
    {
        parse_k5_flush(stack);
        ParseStatement* top = parse_k5_top(stack);
        if (top != 0 && top->state == PARSE_STATEMENT_DO_WHILE)
        {
            kernels->parents[top->token] = i;
            kernels->parents[top->extra] = i;
            if (top->owner != BUSTER_PARSE_NONE)
            {
                kernels->parents[i] = top->owner;
            }
            stack->count -= 1;
        }
        else
        {
            const uint32_t owner = parse_k5_owner(stack);
            if (owner != BUSTER_PARSE_NONE)
            {
                kernels->parents[i] = owner;
            }
        }
        parse_k5_complete(stack);
    }
    else if (((role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR) && closer_in_block) ||
             ((role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_DO) && in_block))
    {
        const uint8_t keyword = role == PARSE_ROLE_CLOSE_CONDITION ? kernels->vkinds[opener - 1] : role == PARSE_ROLE_CLOSE_FOR ? PARSE_KIND_FOR :
                                role == PARSE_ROLE_DO ? PARSE_KIND_DO : PARSE_KIND_CASE;
        parse_k5_flush(stack);
        ParseStatement* top = parse_k5_top(stack);
        if (keyword == PARSE_KIND_WHILE && top != 0 && top->state == PARSE_STATEMENT_DO_DONE)
        {
            top->state = PARSE_STATEMENT_DO_WHILE;
            top->extra = i;
        }
        else
        {
            const uint32_t owner = parse_k5_owner(stack);
            if (owner != BUSTER_PARSE_NONE && keyword != PARSE_KIND_DO)
            {
                kernels->parents[i] = owner;
            }
            stack->items[stack->count] = (ParseStatement){
                .token = i,
                .owner = keyword == PARSE_KIND_DO ? owner : BUSTER_PARSE_NONE,
                .extra = BUSTER_PARSE_NONE,
                .state = PARSE_STATEMENT_WAITING,
                .kind = keyword,
            };
            stack->count += 1;
        }
    }
    else if (role == PARSE_ROLE_ELSE)
    {
        ParseStatement* top = parse_k5_top(stack);
        if (top != 0 && top->state == PARSE_STATEMENT_IF_DONE)
        {
            kernels->parents[i] = top->token;
            *top = (ParseStatement){ .token = i, .owner = BUSTER_PARSE_NONE, .extra = BUSTER_PARSE_NONE, .state = PARSE_STATEMENT_WAITING, .kind = PARSE_KIND_ELSE };
        }
        else
        {
            result = false;
        }
    }
    return result;
}

// K5, the twin: every token through parse_statement_event. Returns false if an else has no if, or a do's statement isn't
// followed by its while and its `;`.
static bool parse_statements(ParseKernels* kernels, uint32_t count, ParseStatement* items)
{
    ParseStatementStack stack = { .items = items, .count = 0, .parents = kernels->parents };
    bool result = true;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result &= parse_statement_event(kernels, &stack, i);
    }
    return result;
}

// The statement contexts of tokens [block, block + live).
static ParseStatementContext parse_statement_context_block(const ParseKernels* kernels, uint32_t block, uint32_t live)
{
    ParseStatementContext result = { .blocks = 0, .ifs = 0, .whiles = 0 };
    for (uint32_t lane = 0; lane < live; lane += 1)
    {
        const uint32_t i = block + lane;
        const uint32_t opener = kernels->enclosing[i];
        const bool closer = parse_kind_deltas[kernels->vkinds[i]] < 0;
        const uint32_t group = closer && opener != BUSTER_PARSE_NONE ? kernels->enclosing[opener] : opener;
        const uint8_t role = group != BUSTER_PARSE_NONE ? kernels->roles[group] : PARSE_ROLE_NONE;
        const uint8_t keyword = closer && opener != BUSTER_PARSE_NONE ? kernels->vkinds[(int64_t)opener - 1] : PARSE_KIND_END;
        result.blocks |= (uint64_t)(role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY) << lane;
        result.ifs |= (uint64_t)(keyword == PARSE_KIND_IF) << lane;
        result.whiles |= (uint64_t)(keyword == PARSE_KIND_WHILE) << lane;
    }
    return result;
}

// Every block's statement contexts, one per 64 tokens.
static __attribute__((unused)) void parse_statement_contexts(const ParseKernels* kernels, uint32_t count, ParseStatementContext* contexts)
{
    for (uint32_t block = 0; block < count; block += 64)
    {
        contexts[block / 64] = parse_statement_context_block(kernels, block, count - block < 64 ? count - block : 64);
    }
}

static BUSTER_BAKED_FRONTEND ParseCheckTables parse_check_tables;

BUSTER_INLINE bool parse_class_set_has(const ParseClassSet* set, uint8_t c)
{
    return ((set->words[c >> 6] >> (c & 63)) & 1) != 0;
}

static void parse_class_set_add(ParseClassSet* set, const uint8_t* classes, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        set->words[classes[i] >> 6] |= (uint64_t)1 << (classes[i] & 63);
    }
}

static void parse_class_set_union(ParseClassSet* set, const ParseClassSet* other)
{
    set->words[0] |= other->words[0];
    set->words[1] |= other->words[1];
}

static void parse_class_set_remove(ParseClassSet* set, const uint8_t* classes, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        set->words[classes[i] >> 6] &= ~((uint64_t)1 << (classes[i] & 63));
    }
}

#define BUSTER_PARSE_SET(name, ...) \
    static const uint8_t name##_classes[] = { __VA_ARGS__ }; \
    ParseClassSet name = { { 0, 0 } }; \
    parse_class_set_add(&name, name##_classes, sizeof(name##_classes))

#define BUSTER_PARSE_ADD(set, ...) \
    do \
    { \
        static const uint8_t added[] = { __VA_ARGS__ }; \
        parse_class_set_add(&(set), added, sizeof(added)); \
    } while (0)

#define BUSTER_PARSE_REMOVE(set, ...) \
    do \
    { \
        static const uint8_t removed[] = { __VA_ARGS__ }; \
        parse_class_set_remove(&(set), removed, sizeof(removed)); \
    } while (0)

static void parse_check_tables_init(void)
{
    ParseCheckTables* t = &parse_check_tables;
    memset(t, 0, sizeof(*t));
    // What may start an operand, a type name, a declaration, a statement and a declarator.
    BUSTER_PARSE_SET(operand_starts, PARSE_ROLE_OPERAND, PARSE_CLASS_CONSTANT, PARSE_CLASS_STRING, PARSE_ROLE_PREFIX, PARSE_CLASS_STAR, PARSE_CLASS_SIZEOF,
                     PARSE_CLASS_LABEL_ADDRESS, PARSE_ROLE_OPEN_GROUP, PARSE_ROLE_OPEN_CAST, PARSE_ROLE_OPEN_COMPOUND, PARSE_CLASS_OPERAND_KEYWORD,
                     PARSE_CLASS_OFFSETOF_KEYWORD, PARSE_CLASS_VA_ARG_KEYWORD, PARSE_CLASS_BIT_CAST_KEYWORD);
    BUSTER_PARSE_SET(type_starts, PARSE_CLASS_TYPE, PARSE_ROLE_SPECIFIER, PARSE_CLASS_TAG_KEYWORD, PARSE_CLASS_SPECIFIER_KEYWORD, PARSE_CLASS_ALIGNAS_KEYWORD,
                     PARSE_CLASS_ATTRIBUTE_KEYWORD, PARSE_CLASS_ATOMIC_KEYWORD, PARSE_CLASS_BITINT_KEYWORD);
    ParseClassSet declaration_starts = type_starts;
    BUSTER_PARSE_ADD(declaration_starts, PARSE_CLASS_STORAGE, PARSE_CLASS_ASSERT_KEYWORD, PARSE_CLASS_EXTENSION);
    ParseClassSet statement_starts = operand_starts;
    BUSTER_PARSE_ADD(statement_starts, PARSE_CLASS_STATEMENT_KEYWORD, PARSE_ROLE_DO, PARSE_ROLE_OPEN_CASE, PARSE_CLASS_DEFAULT, PARSE_ROLE_RETURN, PARSE_CLASS_JUMP,
                     PARSE_CLASS_GOTO, PARSE_ROLE_TERMINATOR, PARSE_ROLE_OPEN_BLOCK, PARSE_CLASS_ASM_KEYWORD, PARSE_ROLE_ELSE);
    ParseClassSet item_starts = declaration_starts;
    parse_class_set_union(&item_starts, &statement_starts);
    BUSTER_PARSE_SET(declarator_starts, PARSE_ROLE_OPERAND, PARSE_CLASS_STAR, PARSE_ROLE_OPEN_GROUP, PARSE_CLASS_ATTRIBUTE_KEYWORD);
    BUSTER_PARSE_SET(abstract_starts, PARSE_CLASS_STAR, PARSE_ROLE_OPEN_GROUP, PARSE_ROLE_OPEN_FUNCTION, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_CLASS_ATTRIBUTE_KEYWORD);
    // What may follow a complete operand, and a declarator's name or suffix (and an abstract declarator's end).
    BUSTER_PARSE_SET(after_operand, PARSE_ROLE_POSTFIX, PARSE_ROLE_MEMBER_OPERATOR, PARSE_CLASS_ARROW, PARSE_ROLE_MULTIPLICATIVE, PARSE_ROLE_ADDITIVE, PARSE_ROLE_SHIFT,
                     PARSE_ROLE_RELATIONAL, PARSE_ROLE_EQUALITY, PARSE_ROLE_BITWISE_AND, PARSE_ROLE_BITWISE_XOR, PARSE_ROLE_BITWISE_OR, PARSE_ROLE_LOGICAL_AND,
                     PARSE_ROLE_LOGICAL_OR, PARSE_ROLE_ASSIGN, PARSE_ROLE_RANGE, PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR, PARSE_ROLE_TERMINATOR, PARSE_ROLE_ASM_COLON,
                     PARSE_ROLE_OPEN_CALL, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_ROLE_OPEN_TERNARY, PARSE_ROLE_CLOSE_GROUP, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION,
                     PARSE_ROLE_CLOSE_SIZEOF, PARSE_ROLE_CLOSE_SPECIFIER, PARSE_ROLE_CLOSE_ASM, PARSE_ROLE_CLOSE_KEYWORD, PARSE_ROLE_CLOSE_CONDITION,
                     PARSE_ROLE_CLOSE_FOR, PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_ROLE_CLOSE_ENUM, PARSE_ROLE_CLOSE_INITIALIZER, PARSE_ROLE_CLOSE_COMPOUND_BODY,
                     PARSE_ROLE_CLOSE_TERNARY, PARSE_ROLE_CLOSE_CASE);
    BUSTER_PARSE_SET(after_declarator, PARSE_ROLE_OPEN_CALL, PARSE_ROLE_OPEN_FUNCTION, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_CLASS_ATTRIBUTE_KEYWORD, PARSE_CLASS_ASM_KEYWORD,
                     PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR, PARSE_ROLE_TERMINATOR, PARSE_ROLE_BITFIELD, PARSE_ROLE_CLOSE_GROUP, PARSE_ROLE_CLOSE_CALL,
                     PARSE_ROLE_CLOSE_FUNCTION, PARSE_ROLE_CLOSE_CAST, PARSE_ROLE_CLOSE_COMPOUND, PARSE_ROLE_CLOSE_SIZEOF, PARSE_ROLE_CLOSE_SPECIFIER,
                     PARSE_ROLE_CLOSE_KEYWORD, PARSE_ROLE_CLOSE_SUBSCRIPT);
    // What may follow a type specifier (or another specifier after one): more specifiers, a declarator or an abstract one,
    // the end of a type name, of a declaration that declares only a tag, and an anonymous bit-field's width.
    ParseClassSet after_type = declarator_starts;
    parse_class_set_union(&after_type, &abstract_starts);
    parse_class_set_union(&after_type, &after_declarator);
    BUSTER_PARSE_REMOVE(after_type, PARSE_ROLE_OPEN_CALL, PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_CLASS_ASM_KEYWORD, PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA);
    BUSTER_PARSE_ADD(after_type, PARSE_CLASS_TYPE, PARSE_CLASS_TYPED, PARSE_CLASS_TYPED_STORAGE, PARSE_CLASS_ALIGNAS_KEYWORD, PARSE_CLASS_ATOMIC_KEYWORD,
                     PARSE_CLASS_BITINT_KEYWORD);
    // What may follow a specifier before any type specifier: more specifiers.
    ParseClassSet after_untyped = type_starts;
    BUSTER_PARSE_ADD(after_untyped, PARSE_CLASS_STORAGE);
    // What may follow `*` or a qualifier of it: the rest of a declarator or an abstract one, or an operand.
    ParseClassSet after_star = abstract_starts;
    parse_class_set_union(&after_star, &declarator_starts);
    BUSTER_PARSE_ADD(after_star, PARSE_ROLE_QUALIFIER_LEFT, PARSE_ROLE_CLOSE_GROUP, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION, PARSE_ROLE_CLOSE_CAST,
                     PARSE_ROLE_CLOSE_SIZEOF, PARSE_ROLE_CLOSE_SPECIFIER, PARSE_ROLE_CLOSE_KEYWORD, PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_ROLE_SEPARATOR);
    ParseClassSet* f = t->follows;
    f[PARSE_ROLE_NONE] = item_starts;
    f[PARSE_ROLE_OPERAND] = after_operand;
    parse_class_set_union(&f[PARSE_ROLE_OPERAND], &after_declarator);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPERAND], PARSE_ROLE_LABEL, PARSE_ROLE_CLOSE_ATTRIBUTE_LIST);
    f[PARSE_ROLE_MEMBER] = after_operand;
    f[PARSE_ROLE_STRING_NEXT] = after_operand;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_STRING_NEXT], PARSE_ROLE_STRING_NEXT, PARSE_ROLE_CLOSE_ATTRIBUTE);
    f[PARSE_ROLE_QUALIFIER_LEFT] = after_star;
    f[PARSE_ROLE_SPECIFIER] = after_untyped;
    // `__extension__` before a declaration, before its specifiers.
    f[PARSE_CLASS_EXTENSION] = declaration_starts;
    BUSTER_PARSE_REMOVE(f[PARSE_CLASS_EXTENSION], PARSE_CLASS_ASSERT_KEYWORD);
    f[PARSE_CLASS_STORAGE] = after_untyped;
    f[PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER] = after_untyped;
    // GNU's attribute statement: `__attribute__((fallthrough));`.
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER], PARSE_ROLE_TERMINATOR);
    f[PARSE_CLASS_TYPE] = after_type;
    f[PARSE_CLASS_TYPED] = after_type;
    f[PARSE_CLASS_TYPED_STORAGE] = after_type;
    f[PARSE_CLASS_TYPED_ATTRIBUTE] = after_type;
    f[PARSE_ROLE_TAG] = after_type;
    f[PARSE_ROLE_CLOSE_STRUCT] = after_type;
    f[PARSE_ROLE_CLOSE_ENUM] = after_type;
    f[PARSE_ROLE_CLOSE_SPECIFIER] = after_type;
    f[PARSE_ROLE_PREFIX] = operand_starts;
    f[PARSE_ROLE_POSTFIX] = after_operand;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_MEMBER_OPERATOR], PARSE_ROLE_MEMBER);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_ARROW], PARSE_ROLE_MEMBER);
    f[PARSE_ROLE_MULTIPLICATIVE] = operand_starts;
    f[PARSE_ROLE_ADDITIVE] = operand_starts;
    f[PARSE_ROLE_SHIFT] = operand_starts;
    f[PARSE_ROLE_RELATIONAL] = operand_starts;
    f[PARSE_ROLE_EQUALITY] = operand_starts;
    f[PARSE_ROLE_BITWISE_AND] = operand_starts;
    f[PARSE_ROLE_BITWISE_XOR] = operand_starts;
    f[PARSE_ROLE_BITWISE_OR] = operand_starts;
    f[PARSE_ROLE_LOGICAL_AND] = operand_starts;
    f[PARSE_ROLE_LOGICAL_OR] = operand_starts;
    f[PARSE_ROLE_ASSIGN] = operand_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_ASSIGN], PARSE_ROLE_OPEN_INITIALIZER);
    f[PARSE_ROLE_RANGE] = operand_starts;
    f[PARSE_ROLE_BITFIELD] = operand_starts;
    f[PARSE_ROLE_COMMA] = operand_starts;
    parse_class_set_union(&f[PARSE_ROLE_COMMA], &declarator_starts);
    f[PARSE_ROLE_RETURN] = operand_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_RETURN], PARSE_ROLE_TERMINATOR);
    f[PARSE_ROLE_SEPARATOR] = operand_starts;
    parse_class_set_union(&f[PARSE_ROLE_SEPARATOR], &declaration_starts);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_SEPARATOR], PARSE_CLASS_ELLIPSIS, PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_ROLE_OPEN_INITIALIZER,
                     PARSE_ROLE_CLOSE_INITIALIZER, PARSE_ROLE_CLOSE_COMPOUND_BODY, PARSE_ROLE_CLOSE_ENUM, PARSE_CLASS_WORD);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_ASM_COLON], PARSE_CLASS_STRING, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_ROLE_ASM_COLON, PARSE_ROLE_CLOSE_ASM, PARSE_ROLE_OPERAND);
    f[PARSE_ROLE_TERMINATOR] = item_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_TERMINATOR], PARSE_ROLE_CLOSE_BLOCK, PARSE_ROLE_CLOSE_BODY, PARSE_ROLE_CLOSE_STRUCT, PARSE_ROLE_CLOSE_FOR);
    f[PARSE_ROLE_ELSE] = statement_starts;
    BUSTER_PARSE_REMOVE(f[PARSE_ROLE_ELSE], PARSE_ROLE_ELSE);
    f[PARSE_ROLE_DO] = f[PARSE_ROLE_ELSE];
    f[PARSE_ROLE_LABEL] = item_starts;
    BUSTER_PARSE_REMOVE(f[PARSE_ROLE_LABEL], PARSE_ROLE_ELSE);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_LABEL], PARSE_ROLE_CLOSE_BLOCK, PARSE_ROLE_CLOSE_BODY);
    f[PARSE_ROLE_CLOSE_CASE] = f[PARSE_ROLE_LABEL];
    // The openers: what their groups may start with (or end, empty).
    f[PARSE_ROLE_OPEN_GROUP] = operand_starts;
    parse_class_set_union(&f[PARSE_ROLE_OPEN_GROUP], &declarator_starts);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_GROUP], PARSE_ROLE_OPEN_BLOCK);
    f[PARSE_ROLE_OPEN_CALL] = operand_starts;
    parse_class_set_union(&f[PARSE_ROLE_OPEN_CALL], &declaration_starts);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_CALL], PARSE_ROLE_CLOSE_CALL, PARSE_CLASS_ELLIPSIS);
    f[PARSE_ROLE_OPEN_FUNCTION] = declaration_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_FUNCTION], PARSE_ROLE_CLOSE_FUNCTION, PARSE_CLASS_ELLIPSIS);
    f[PARSE_ROLE_OPEN_CAST] = type_starts;
    f[PARSE_ROLE_OPEN_COMPOUND] = type_starts;
    f[PARSE_ROLE_OPEN_SIZEOF] = operand_starts;
    parse_class_set_union(&f[PARSE_ROLE_OPEN_SIZEOF], &type_starts);
    f[PARSE_ROLE_OPEN_SPECIFIER] = f[PARSE_ROLE_OPEN_SIZEOF];
    f[PARSE_ROLE_OPEN_KEYWORD] = f[PARSE_ROLE_OPEN_SIZEOF];
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_ATTRIBUTE], PARSE_ROLE_OPEN_ATTRIBUTE_LIST, PARSE_CLASS_STRING);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER], PARSE_ROLE_OPEN_ATTRIBUTE_LIST);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_ATTRIBUTE_LEFT], PARSE_ROLE_OPEN_ATTRIBUTE_LIST);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT], PARSE_ROLE_OPEN_ATTRIBUTE_LIST);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_ATTRIBUTE_LIST], PARSE_ROLE_OPERAND, PARSE_CLASS_WORD, PARSE_ROLE_CLOSE_ATTRIBUTE_LIST);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_ASM], PARSE_CLASS_STRING);
    f[PARSE_ROLE_OPEN_CONDITION] = operand_starts;
    f[PARSE_ROLE_OPEN_FOR] = operand_starts;
    parse_class_set_union(&f[PARSE_ROLE_OPEN_FOR], &declaration_starts);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_FOR], PARSE_ROLE_TERMINATOR);
    f[PARSE_ROLE_OPEN_SUBSCRIPT] = operand_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_SUBSCRIPT], PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_ROLE_SPECIFIER, PARSE_CLASS_STORAGE);
    f[PARSE_ROLE_OPEN_BLOCK] = item_starts;
    BUSTER_PARSE_REMOVE(f[PARSE_ROLE_OPEN_BLOCK], PARSE_ROLE_ELSE);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_BLOCK], PARSE_ROLE_CLOSE_BLOCK);
    f[PARSE_ROLE_OPEN_BODY] = item_starts;
    BUSTER_PARSE_REMOVE(f[PARSE_ROLE_OPEN_BODY], PARSE_ROLE_ELSE);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_BODY], PARSE_ROLE_CLOSE_BODY);
    f[PARSE_ROLE_OPEN_STRUCT] = declaration_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_STRUCT], PARSE_ROLE_CLOSE_STRUCT, PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_ENUM], PARSE_ROLE_OPERAND);
    f[PARSE_ROLE_OPEN_INITIALIZER] = operand_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_INITIALIZER], PARSE_ROLE_OPEN_INITIALIZER, PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_ROLE_CLOSE_INITIALIZER);
    f[PARSE_ROLE_OPEN_COMPOUND_BODY] = operand_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_COMPOUND_BODY], PARSE_ROLE_OPEN_INITIALIZER, PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_OPEN_SUBSCRIPT,
                     PARSE_ROLE_CLOSE_COMPOUND_BODY);
    f[PARSE_ROLE_OPEN_TERNARY] = operand_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_OPEN_TERNARY], PARSE_ROLE_CLOSE_TERNARY);
    f[PARSE_ROLE_OPEN_CASE] = operand_starts;
    // The closers.
    f[PARSE_ROLE_CLOSE_GROUP] = after_operand;
    parse_class_set_union(&f[PARSE_ROLE_CLOSE_GROUP], &after_declarator);
    f[PARSE_ROLE_CLOSE_CALL] = f[PARSE_ROLE_CLOSE_GROUP];
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_CALL], PARSE_ROLE_OPEN_BODY, PARSE_ROLE_CLOSE_ATTRIBUTE_LIST);
    f[PARSE_ROLE_CLOSE_FUNCTION] = f[PARSE_ROLE_CLOSE_CALL];
    f[PARSE_ROLE_CLOSE_CAST] = operand_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_COMPOUND], PARSE_ROLE_OPEN_COMPOUND_BODY);
    f[PARSE_ROLE_CLOSE_SIZEOF] = after_operand;
    BUSTER_PARSE_REMOVE(f[PARSE_ROLE_CLOSE_SIZEOF], PARSE_ROLE_POSTFIX, PARSE_ROLE_MEMBER_OPERATOR, PARSE_CLASS_ARROW, PARSE_ROLE_OPEN_CALL, PARSE_ROLE_OPEN_SUBSCRIPT);
    // (Not a group's `)`: attributes after a declarator's name in its parentheses are clang's error, #1127.)
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_ATTRIBUTE], PARSE_CLASS_ATTRIBUTE_KEYWORD, PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR, PARSE_ROLE_TERMINATOR,
                     PARSE_ROLE_BITFIELD, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION, PARSE_ROLE_OPEN_BODY);
    f[PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT] = after_star;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT], PARSE_CLASS_BODY_NAME, PARSE_ROLE_TAG, PARSE_ROLE_OPEN_STRUCT, PARSE_ROLE_OPEN_ENUM, PARSE_CLASS_ATTRIBUTE_KEYWORD);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_ATTRIBUTE_LIST], PARSE_ROLE_CLOSE_ATTRIBUTE, PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER, PARSE_CLASS_TYPED_ATTRIBUTE,
                     PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT, PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_ASM], PARSE_ROLE_TERMINATOR);
    f[PARSE_ROLE_CLOSE_KEYWORD] = after_operand;
    f[PARSE_ROLE_CLOSE_CONDITION] = statement_starts;
    BUSTER_PARSE_REMOVE(f[PARSE_ROLE_CLOSE_CONDITION], PARSE_ROLE_ELSE);
    f[PARSE_ROLE_CLOSE_FOR] = f[PARSE_ROLE_CLOSE_CONDITION];
    // (No string after `]`: an asm operand's `[name] "constraint"` is an asm statement's, which the checks leave to the
    // scalar parser, as they do GNU's `[1] "abc"` designator; after a subscript, a string is an error, #1127.)
    f[PARSE_ROLE_CLOSE_SUBSCRIPT] = after_operand;
    parse_class_set_union(&f[PARSE_ROLE_CLOSE_SUBSCRIPT], &after_declarator);
    f[PARSE_ROLE_CLOSE_BLOCK] = item_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_BLOCK], PARSE_ROLE_CLOSE_BLOCK, PARSE_ROLE_CLOSE_BODY, PARSE_ROLE_CLOSE_GROUP);
    f[PARSE_ROLE_CLOSE_BODY] = declaration_starts;
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_BODY], PARSE_ROLE_TERMINATOR, PARSE_CLASS_ASM_KEYWORD);
    BUSTER_PARSE_ADD(f[PARSE_ROLE_CLOSE_INITIALIZER], PARSE_ROLE_SEPARATOR, PARSE_ROLE_COMMA, PARSE_ROLE_TERMINATOR, PARSE_ROLE_CLOSE_INITIALIZER,
                     PARSE_ROLE_CLOSE_COMPOUND_BODY);
    f[PARSE_ROLE_CLOSE_COMPOUND_BODY] = after_operand;
    f[PARSE_ROLE_CLOSE_TERNARY] = operand_starts;
    // _Alignas's closer is a specifier but not a type specifier, as an attribute's is.
    f[PARSE_CLASS_ALIGNAS_CLOSE] = after_untyped;
    f[PARSE_CLASS_TYPED_ALIGNAS_CLOSE] = after_type;
    // The refined classes.
    f[PARSE_CLASS_CONSTANT] = after_operand;
    f[PARSE_CLASS_STRING] = f[PARSE_ROLE_STRING_NEXT];
    BUSTER_PARSE_ADD(f[PARSE_CLASS_ELLIPSIS], PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_WORD], PARSE_ROLE_CLOSE_ATTRIBUTE_LIST, PARSE_ROLE_SEPARATOR, PARSE_ROLE_OPEN_CALL);
    f[PARSE_CLASS_STAR] = after_star;
    parse_class_set_union(&f[PARSE_CLASS_STAR], &operand_starts);
    f[PARSE_CLASS_SIZEOF] = operand_starts;
    BUSTER_PARSE_ADD(f[PARSE_CLASS_SIZEOF], PARSE_ROLE_OPEN_SIZEOF);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_LABEL_ADDRESS], PARSE_ROLE_OPERAND);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_TAG_KEYWORD], PARSE_ROLE_TAG, PARSE_CLASS_BODY_NAME, PARSE_ROLE_OPEN_STRUCT, PARSE_ROLE_OPEN_ENUM, PARSE_CLASS_ATTRIBUTE_KEYWORD);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_ATTRIBUTE_KEYWORD], PARSE_ROLE_OPEN_ATTRIBUTE, PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER, PARSE_ROLE_OPEN_ATTRIBUTE_LEFT,
                     PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_ASM_KEYWORD], PARSE_ROLE_OPEN_ASM, PARSE_ROLE_OPEN_ATTRIBUTE, PARSE_CLASS_ASM_QUALIFIER);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_ASM_QUALIFIER], PARSE_CLASS_ASM_QUALIFIER, PARSE_ROLE_OPEN_ASM);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_SPECIFIER_KEYWORD], PARSE_ROLE_OPEN_SPECIFIER);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_ALIGNAS_KEYWORD], PARSE_ROLE_OPEN_SPECIFIER);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_OPERAND_KEYWORD], PARSE_ROLE_OPEN_KEYWORD);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_OFFSETOF_KEYWORD], PARSE_ROLE_OPEN_KEYWORD);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_VA_ARG_KEYWORD], PARSE_ROLE_OPEN_KEYWORD);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_BIT_CAST_KEYWORD], PARSE_ROLE_OPEN_KEYWORD);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_ATOMIC_KEYWORD], PARSE_ROLE_OPEN_SPECIFIER);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_BITINT_KEYWORD], PARSE_ROLE_OPEN_SPECIFIER);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_ASSERT_KEYWORD], PARSE_ROLE_OPEN_KEYWORD);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_STATEMENT_KEYWORD], PARSE_ROLE_OPEN_CONDITION, PARSE_ROLE_OPEN_FOR);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_BODY_NAME], PARSE_ROLE_OPEN_STRUCT, PARSE_ROLE_OPEN_ENUM);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_JUMP], PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_GOTO], PARSE_ROLE_OPERAND, PARSE_CLASS_STAR);
    BUSTER_PARSE_ADD(f[PARSE_CLASS_DEFAULT], PARSE_ROLE_CLOSE_CASE);
    // Where CLOSE_SPECIFIER may come, _Alignas's closers may.
    for (uint32_t c = 0; c < PARSE_CLASS_COUNT; c += 1)
    {
        if (parse_class_set_has(&f[c], PARSE_ROLE_CLOSE_SPECIFIER))
        {
            BUSTER_PARSE_ADD(f[c], PARSE_CLASS_ALIGNAS_CLOSE, PARSE_CLASS_TYPED_ALIGNAS_CLOSE);
        }
    }
    // The modes: what an expression, specifiers, a declarator and an abstract one hold.
    ParseClassSet expression = operand_starts;
    BUSTER_PARSE_ADD(expression, PARSE_ROLE_MEMBER, PARSE_ROLE_STRING_NEXT, PARSE_ROLE_POSTFIX, PARSE_ROLE_MEMBER_OPERATOR, PARSE_CLASS_ARROW, PARSE_ROLE_MULTIPLICATIVE,
                     PARSE_ROLE_ADDITIVE, PARSE_ROLE_SHIFT, PARSE_ROLE_RELATIONAL, PARSE_ROLE_EQUALITY, PARSE_ROLE_BITWISE_AND, PARSE_ROLE_BITWISE_XOR,
                     PARSE_ROLE_BITWISE_OR, PARSE_ROLE_LOGICAL_AND, PARSE_ROLE_LOGICAL_OR, PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR,
                     PARSE_ROLE_OPEN_CALL, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_ROLE_OPEN_TERNARY,
                     PARSE_ROLE_CLOSE_TERNARY, PARSE_ROLE_CLOSE_GROUP, PARSE_ROLE_CLOSE_CAST, PARSE_ROLE_CLOSE_COMPOUND, PARSE_ROLE_OPEN_COMPOUND_BODY,
                     PARSE_ROLE_CLOSE_COMPOUND_BODY, PARSE_ROLE_OPEN_SIZEOF, PARSE_ROLE_CLOSE_SIZEOF, PARSE_ROLE_OPEN_KEYWORD, PARSE_ROLE_CLOSE_KEYWORD);
    // The other builtins that take a type are left to the scalar parser.
    BUSTER_PARSE_REMOVE(expression, PARSE_CLASS_OPERAND_KEYWORD);
    BUSTER_PARSE_SET(specifiers, PARSE_CLASS_TYPE, PARSE_ROLE_SPECIFIER, PARSE_CLASS_TYPED, PARSE_CLASS_STORAGE, PARSE_CLASS_TYPED_STORAGE, PARSE_CLASS_EXTENSION, PARSE_ROLE_TAG,
                     PARSE_CLASS_TAG_KEYWORD, PARSE_CLASS_BODY_NAME, PARSE_ROLE_OPEN_STRUCT, PARSE_ROLE_CLOSE_STRUCT, PARSE_ROLE_OPEN_ENUM, PARSE_ROLE_CLOSE_ENUM,
                     PARSE_CLASS_SPECIFIER_KEYWORD, PARSE_CLASS_ATOMIC_KEYWORD, PARSE_CLASS_BITINT_KEYWORD, PARSE_CLASS_ALIGNAS_KEYWORD, PARSE_ROLE_OPEN_SPECIFIER,
                     PARSE_ROLE_CLOSE_SPECIFIER, PARSE_CLASS_ALIGNAS_CLOSE, PARSE_CLASS_TYPED_ALIGNAS_CLOSE, PARSE_CLASS_ATTRIBUTE_KEYWORD,
                     PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER, PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER, PARSE_CLASS_TYPED_ATTRIBUTE, PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT,
                     PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT);
    BUSTER_PARSE_SET(abstract, PARSE_CLASS_STAR, PARSE_ROLE_QUALIFIER_LEFT, PARSE_CLASS_ATTRIBUTE_KEYWORD, PARSE_ROLE_OPEN_ATTRIBUTE_LEFT,
                     PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT, PARSE_ROLE_OPEN_ATTRIBUTE, PARSE_ROLE_CLOSE_ATTRIBUTE, PARSE_ROLE_OPEN_GROUP, PARSE_ROLE_CLOSE_GROUP,
                     PARSE_ROLE_OPEN_CALL, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_OPEN_FUNCTION, PARSE_ROLE_CLOSE_FUNCTION, PARSE_ROLE_OPEN_SUBSCRIPT,
                     PARSE_ROLE_CLOSE_SUBSCRIPT);
    ParseClassSet declarator = abstract;
    BUSTER_PARSE_ADD(declarator, PARSE_ROLE_OPERAND, PARSE_CLASS_ASM_KEYWORD);
    ParseClassSet* a = t->allows;
    // File scope: declarations (and `;`).
    a[PARSE_CHECK_FILE_START] = declaration_starts;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_FILE_START], PARSE_CLASS_ALIGNAS_KEYWORD);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_FILE_START], PARSE_ROLE_TERMINATOR);
    ParseClassSet declaration = specifiers;
    parse_class_set_union(&declaration, &declarator);
    BUSTER_PARSE_ADD(declaration, PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA, PARSE_ROLE_TERMINATOR);
    a[PARSE_CHECK_FILE_DECLARATION] = declaration;
    BUSTER_PARSE_ADD(a[PARSE_CHECK_FILE_DECLARATION], PARSE_ROLE_OPEN_BODY, PARSE_ROLE_CLOSE_BODY);
    ParseClassSet initializer = expression;
    BUSTER_PARSE_ADD(initializer, PARSE_ROLE_OPEN_INITIALIZER, PARSE_ROLE_CLOSE_INITIALIZER, PARSE_ROLE_TERMINATOR);
    a[PARSE_CHECK_FILE_INITIALIZER] = initializer;
    // Blocks: declarations and statements.
    a[PARSE_CHECK_BLOCK_START] = item_starts;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_BLOCK_START], PARSE_CLASS_ALIGNAS_KEYWORD, PARSE_CLASS_ASM_KEYWORD, PARSE_CLASS_OPERAND_KEYWORD);
    a[PARSE_CHECK_BLOCK_DECLARATION] = declaration;
    a[PARSE_CHECK_BLOCK_INITIALIZER] = initializer;
    a[PARSE_CHECK_BLOCK_STATEMENT] = expression;
    BUSTER_PARSE_ADD(a[PARSE_CHECK_BLOCK_STATEMENT], PARSE_ROLE_TERMINATOR, PARSE_ROLE_LABEL, PARSE_ROLE_OPEN_CONDITION, PARSE_ROLE_CLOSE_CONDITION, PARSE_ROLE_OPEN_FOR,
                     PARSE_ROLE_CLOSE_FOR, PARSE_ROLE_CLOSE_CASE, PARSE_ROLE_CLOSE_BLOCK);
    // Struct bodies: member declarations, with widths and without initializers or storage classes.
    a[PARSE_CHECK_STRUCT_START] = type_starts;
    BUSTER_PARSE_ADD(a[PARSE_CHECK_STRUCT_START], PARSE_ROLE_TERMINATOR, PARSE_CLASS_ASSERT_KEYWORD, PARSE_CLASS_EXTENSION);
    a[PARSE_CHECK_STRUCT_DECLARATION] = declaration;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_STRUCT_DECLARATION], PARSE_ROLE_ASSIGN, PARSE_CLASS_ASM_KEYWORD, PARSE_CLASS_STORAGE, PARSE_CLASS_TYPED_STORAGE);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_STRUCT_DECLARATION], PARSE_ROLE_BITFIELD);
    a[PARSE_CHECK_STRUCT_WIDTH] = expression;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_STRUCT_WIDTH], PARSE_ROLE_ASSIGN);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_STRUCT_WIDTH], PARSE_ROLE_TERMINATOR, PARSE_CLASS_ATTRIBUTE_KEYWORD, PARSE_ROLE_OPEN_ATTRIBUTE, PARSE_ROLE_CLOSE_ATTRIBUTE);
    // Parameters: declarations without storage classes or initializers, and `...`.
    a[PARSE_CHECK_PARAMETERS_START] = type_starts;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_PARAMETERS_START], PARSE_CLASS_ALIGNAS_KEYWORD);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_PARAMETERS_START], PARSE_CLASS_ELLIPSIS);
    a[PARSE_CHECK_PARAMETERS_DECLARATION] = specifiers;
    parse_class_set_union(&a[PARSE_CHECK_PARAMETERS_DECLARATION], &declarator);
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_PARAMETERS_DECLARATION], PARSE_CLASS_ASM_KEYWORD, PARSE_CLASS_STORAGE, PARSE_CLASS_TYPED_STORAGE);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_PARAMETERS_DECLARATION], PARSE_ROLE_SEPARATOR);
    // A for's clauses: the first a declaration or an expression, the others expressions.
    a[PARSE_CHECK_FOR_START] = operand_starts;
    parse_class_set_union(&a[PARSE_CHECK_FOR_START], &type_starts);
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_FOR_START], PARSE_CLASS_ALIGNAS_KEYWORD, PARSE_CLASS_OPERAND_KEYWORD);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_FOR_START], PARSE_ROLE_TERMINATOR);
    a[PARSE_CHECK_FOR_DECLARATION] = declaration;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_FOR_DECLARATION], PARSE_CLASS_ASM_KEYWORD, PARSE_CLASS_STORAGE, PARSE_CLASS_TYPED_STORAGE);
    a[PARSE_CHECK_FOR_INITIALIZER] = initializer;
    a[PARSE_CHECK_FOR_EXPRESSION] = expression;
    BUSTER_PARSE_ADD(a[PARSE_CHECK_FOR_EXPRESSION], PARSE_ROLE_TERMINATOR);
    a[PARSE_CHECK_FOR_CLAUSE_START] = operand_starts;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_FOR_CLAUSE_START], PARSE_CLASS_OPERAND_KEYWORD);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_FOR_CLAUSE_START], PARSE_ROLE_TERMINATOR);
    // Type names, and sizeof's type name or expression.
    a[PARSE_CHECK_TYPE_START] = type_starts;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_TYPE_START], PARSE_CLASS_ALIGNAS_KEYWORD);
    a[PARSE_CHECK_TYPE_DECLARATION] = specifiers;
    parse_class_set_union(&a[PARSE_CHECK_TYPE_DECLARATION], &abstract);
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_TYPE_DECLARATION], PARSE_CLASS_STORAGE, PARSE_CLASS_TYPED_STORAGE);
    // __builtin_offsetof's type name, which a `,` ends.
    a[PARSE_CHECK_TYPE_ARGUMENT] = a[PARSE_CHECK_TYPE_DECLARATION];
    BUSTER_PARSE_ADD(a[PARSE_CHECK_TYPE_ARGUMENT], PARSE_ROLE_SEPARATOR);
    a[PARSE_CHECK_SIZEOF_START] = a[PARSE_CHECK_TYPE_START];
    parse_class_set_union(&a[PARSE_CHECK_SIZEOF_START], &operand_starts);
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_SIZEOF_START], PARSE_CLASS_OPERAND_KEYWORD);
    a[PARSE_CHECK_SIZEOF_TYPE] = a[PARSE_CHECK_TYPE_DECLARATION];
    a[PARSE_CHECK_SIZEOF_EXPRESSION] = expression;
    // Enumerators: a name, then perhaps `=` and a constant expression.
    BUSTER_PARSE_ADD(a[PARSE_CHECK_ENUM_START], PARSE_ROLE_OPERAND);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_ENUM_NAME], PARSE_ROLE_ASSIGN, PARSE_ROLE_SEPARATOR, PARSE_CLASS_ATTRIBUTE_KEYWORD, PARSE_ROLE_OPEN_ATTRIBUTE, PARSE_ROLE_CLOSE_ATTRIBUTE);
    a[PARSE_CHECK_ENUM_VALUE] = expression;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_ENUM_VALUE], PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA);
    // A call's arguments.
    a[PARSE_CHECK_ARGUMENTS_START] = operand_starts;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_ARGUMENTS_START], PARSE_CLASS_OPERAND_KEYWORD);
    a[PARSE_CHECK_ARGUMENTS] = expression;
    // Declarators in parentheses.
    a[PARSE_CHECK_DECLARATOR] = declarator;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_DECLARATOR], PARSE_CLASS_ASM_KEYWORD);
    a[PARSE_CHECK_ABSTRACT] = abstract;
    a[PARSE_CHECK_OPTIONAL] = a[PARSE_CHECK_DECLARATOR];
    // Expressions: in parentheses, conditions and a ternary's middle; a case's constant expression (with GNU's ranges); a
    // subscript or an array's size (and an initializer's designator, with its range).
    // A statement expression's block in parentheses, which the checks see first and last in its group (in a function).
    a[PARSE_CHECK_EXPRESSION] = expression;
    BUSTER_PARSE_ADD(a[PARSE_CHECK_EXPRESSION], PARSE_ROLE_OPEN_BLOCK, PARSE_ROLE_CLOSE_BLOCK);
    a[PARSE_CHECK_CONDITIONAL] = expression;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_CONDITIONAL], PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR);
    a[PARSE_CHECK_ASSERT_CONDITION] = a[PARSE_CHECK_CONDITIONAL];
    BUSTER_PARSE_ADD(a[PARSE_CHECK_ASSERT_CONDITION], PARSE_ROLE_SEPARATOR);
    a[PARSE_CHECK_CASE] = a[PARSE_CHECK_CONDITIONAL];
    BUSTER_PARSE_ADD(a[PARSE_CHECK_CASE], PARSE_ROLE_RANGE);
    a[PARSE_CHECK_SUBSCRIPT] = expression;
    BUSTER_PARSE_ADD(a[PARSE_CHECK_SUBSCRIPT], PARSE_ROLE_RANGE);
    // An array declarator's size and an array designator's index are assignment-expressions: no comma at their top (#1867).
    a[PARSE_CHECK_ARRAY] = a[PARSE_CHECK_SUBSCRIPT];
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_ARRAY], PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR);
    // Initializer lists: designators, then an initializer; or an initializer.
    a[PARSE_CHECK_INITIALIZER_START] = operand_starts;
    BUSTER_PARSE_REMOVE(a[PARSE_CHECK_INITIALIZER_START], PARSE_CLASS_OPERAND_KEYWORD);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_INITIALIZER_START], PARSE_ROLE_OPEN_INITIALIZER, PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_OPEN_SUBSCRIPT);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_INITIALIZER_DESIGNATOR], PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_MEMBER, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_ROLE_CLOSE_SUBSCRIPT,
                     PARSE_ROLE_ASSIGN);
    a[PARSE_CHECK_INITIALIZER] = expression;
    BUSTER_PARSE_ADD(a[PARSE_CHECK_INITIALIZER], PARSE_ROLE_OPEN_INITIALIZER, PARSE_ROLE_CLOSE_INITIALIZER);
    // Attributes: `__attribute__((...))`'s outer group holds the list, and the list names with arguments; an asm label
    // holds strings.
    BUSTER_PARSE_ADD(a[PARSE_CHECK_ATTRIBUTE], PARSE_ROLE_OPEN_ATTRIBUTE_LIST, PARSE_ROLE_CLOSE_ATTRIBUTE_LIST);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_ATTRIBUTE_LIST_START], PARSE_ROLE_OPERAND, PARSE_CLASS_WORD);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_ATTRIBUTE_LIST], PARSE_ROLE_OPEN_CALL, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_SEPARATOR);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_ASM_LABEL], PARSE_CLASS_STRING, PARSE_ROLE_STRING_NEXT);
    // _Static_assert's item, and its message.
    BUSTER_PARSE_ADD(a[PARSE_CHECK_ASSERTION], PARSE_ROLE_OPEN_KEYWORD, PARSE_ROLE_CLOSE_KEYWORD, PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_MESSAGE_START], PARSE_CLASS_STRING);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_MESSAGE], PARSE_ROLE_STRING_NEXT);
    // __builtin_offsetof's member designator.
    BUSTER_PARSE_ADD(a[PARSE_CHECK_MEMBER_START], PARSE_ROLE_OPERAND);
    BUSTER_PARSE_ADD(a[PARSE_CHECK_MEMBER], PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_MEMBER, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_ROLE_CLOSE_SUBSCRIPT);
    // What may end a group's content in each mode, before its closer (or the end of the file).
    BUSTER_PARSE_SET(operand_ends, PARSE_ROLE_OPERAND, PARSE_CLASS_CONSTANT, PARSE_CLASS_STRING, PARSE_ROLE_STRING_NEXT, PARSE_ROLE_MEMBER, PARSE_ROLE_POSTFIX,
                     PARSE_ROLE_CLOSE_GROUP, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_ROLE_CLOSE_SIZEOF, PARSE_ROLE_CLOSE_KEYWORD,
                     PARSE_ROLE_CLOSE_COMPOUND_BODY);
    BUSTER_PARSE_SET(item_ends, PARSE_ROLE_TERMINATOR, PARSE_ROLE_CLOSE_BLOCK, PARSE_ROLE_LABEL, PARSE_ROLE_CLOSE_CASE);
    BUSTER_PARSE_SET(type_ends, PARSE_CLASS_TYPE, PARSE_CLASS_TYPED, PARSE_ROLE_TAG, PARSE_ROLE_CLOSE_STRUCT, PARSE_ROLE_CLOSE_ENUM, PARSE_ROLE_CLOSE_SPECIFIER,
                     PARSE_CLASS_TYPED_ATTRIBUTE, PARSE_CLASS_STAR, PARSE_ROLE_QUALIFIER_LEFT, PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT, PARSE_ROLE_CLOSE_GROUP,
                     PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION, PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_ROLE_CLOSE_ATTRIBUTE);
    ParseClassSet* e = t->ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_FILE_START], PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_FILE_DECLARATION], PARSE_ROLE_TERMINATOR, PARSE_ROLE_CLOSE_BODY);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_FILE_INITIALIZER], PARSE_ROLE_TERMINATOR);
    e[PARSE_CHECK_BLOCK_START] = item_ends;
    e[PARSE_CHECK_BLOCK_DECLARATION] = item_ends;
    e[PARSE_CHECK_BLOCK_INITIALIZER] = item_ends;
    e[PARSE_CHECK_BLOCK_STATEMENT] = item_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_STRUCT_START], PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_STRUCT_DECLARATION], PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_STRUCT_WIDTH], PARSE_ROLE_TERMINATOR);
    e[PARSE_CHECK_PARAMETERS_START] = type_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_PARAMETERS_START], PARSE_CLASS_ELLIPSIS);
    e[PARSE_CHECK_PARAMETERS_DECLARATION] = type_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_PARAMETERS_DECLARATION], PARSE_ROLE_OPERAND, PARSE_CLASS_TYPED_STORAGE);
    e[PARSE_CHECK_FOR_EXPRESSION] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_FOR_EXPRESSION], PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_FOR_START], PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_FOR_DECLARATION], PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_FOR_INITIALIZER], PARSE_ROLE_TERMINATOR);
    e[PARSE_CHECK_FOR_CLAUSE_START] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_FOR_CLAUSE_START], PARSE_ROLE_TERMINATOR);
    e[PARSE_CHECK_TYPE_START] = type_ends;
    e[PARSE_CHECK_TYPE_DECLARATION] = type_ends;
    e[PARSE_CHECK_TYPE_ARGUMENT] = type_ends;
    // An item's start that ends its group is all of it: a lone type, or an operand.
    e[PARSE_CHECK_SIZEOF_START] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_SIZEOF_START], PARSE_CLASS_TYPE);
    e[PARSE_CHECK_SIZEOF_TYPE] = type_ends;
    e[PARSE_CHECK_SIZEOF_EXPRESSION] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_ENUM_START], PARSE_ROLE_OPERAND);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_ENUM_NAME], PARSE_ROLE_SEPARATOR, PARSE_ROLE_CLOSE_ATTRIBUTE);
    e[PARSE_CHECK_ENUM_VALUE] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_ENUM_VALUE], PARSE_ROLE_SEPARATOR);
    e[PARSE_CHECK_ARGUMENTS_START] = operand_ends;
    e[PARSE_CHECK_ARGUMENTS] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_DECLARATOR], PARSE_ROLE_OPERAND, PARSE_ROLE_CLOSE_GROUP, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION, PARSE_ROLE_CLOSE_SUBSCRIPT,
                     PARSE_ROLE_CLOSE_ATTRIBUTE);
    e[PARSE_CHECK_ABSTRACT] = type_ends;
    e[PARSE_CHECK_OPTIONAL] = type_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_OPTIONAL], PARSE_ROLE_OPERAND);
    e[PARSE_CHECK_EXPRESSION] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_EXPRESSION], PARSE_ROLE_CLOSE_BLOCK);
    e[PARSE_CHECK_CASE] = operand_ends;
    e[PARSE_CHECK_SUBSCRIPT] = operand_ends;
    e[PARSE_CHECK_ARRAY] = operand_ends;
    e[PARSE_CHECK_INITIALIZER_START] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_INITIALIZER_START], PARSE_ROLE_CLOSE_INITIALIZER);
    e[PARSE_CHECK_INITIALIZER] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_INITIALIZER], PARSE_ROLE_CLOSE_INITIALIZER, PARSE_ROLE_SEPARATOR);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_ATTRIBUTE], PARSE_ROLE_CLOSE_ATTRIBUTE_LIST);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_ATTRIBUTE_LIST_START], PARSE_ROLE_OPERAND, PARSE_CLASS_WORD);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_ATTRIBUTE_LIST], PARSE_ROLE_CLOSE_CALL);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_ASM_LABEL], PARSE_CLASS_STRING, PARSE_ROLE_STRING_NEXT);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_MEMBER_START], PARSE_ROLE_OPERAND);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_MEMBER], PARSE_ROLE_MEMBER, PARSE_ROLE_CLOSE_SUBSCRIPT);
    e[PARSE_CHECK_CONDITIONAL] = operand_ends;
    e[PARSE_CHECK_ASSERT_CONDITION] = operand_ends;
    BUSTER_PARSE_ADD(e[PARSE_CHECK_ASSERTION], PARSE_ROLE_TERMINATOR);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_MESSAGE_START], PARSE_CLASS_STRING);
    BUSTER_PARSE_ADD(e[PARSE_CHECK_MESSAGE], PARSE_ROLE_STRING_NEXT);
    // Where a declarator needs a name: what comes before it, and what shows it was skipped; a declaration of nothing but
    // specifiers (a tag's) ends right after them, and so does an anonymous bit-field's type.
    BUSTER_PARSE_ADD(t->before_name, PARSE_CLASS_TYPE, PARSE_CLASS_TYPED, PARSE_CLASS_TYPED_STORAGE, PARSE_ROLE_TAG, PARSE_ROLE_CLOSE_STRUCT, PARSE_ROLE_CLOSE_ENUM,
                     PARSE_ROLE_CLOSE_SPECIFIER, PARSE_CLASS_TYPED_ATTRIBUTE, PARSE_CLASS_TYPED_ALIGNAS_CLOSE, PARSE_CLASS_STAR, PARSE_ROLE_QUALIFIER_LEFT,
                     PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT);
    BUSTER_PARSE_ADD(t->after_name, PARSE_ROLE_OPEN_FUNCTION, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_ROLE_CLOSE_GROUP, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION,
                     PARSE_ROLE_SEPARATOR, PARSE_ROLE_COMMA, PARSE_ROLE_TERMINATOR, PARSE_ROLE_ASSIGN, PARSE_ROLE_BITFIELD);
    BUSTER_PARSE_ADD(t->specifier_ends, PARSE_CLASS_TYPE, PARSE_CLASS_TYPED, PARSE_CLASS_TYPED_STORAGE, PARSE_ROLE_TAG, PARSE_ROLE_CLOSE_STRUCT, PARSE_ROLE_CLOSE_ENUM,
                     PARSE_ROLE_CLOSE_SPECIFIER, PARSE_CLASS_TYPED_ATTRIBUTE, PARSE_CLASS_TYPED_ALIGNAS_CLOSE);
    t->operand_starts = operand_starts;
    t->named_modes = (1ull << PARSE_CHECK_FILE_DECLARATION) | (1ull << PARSE_CHECK_BLOCK_DECLARATION) | (1ull << PARSE_CHECK_STRUCT_DECLARATION) |
                     (1ull << PARSE_CHECK_FOR_DECLARATION) | (1ull << PARSE_CHECK_DECLARATOR);
    t->declaration_modes = t->named_modes | (1ull << PARSE_CHECK_PARAMETERS_DECLARATION) | (1ull << PARSE_CHECK_TYPE_DECLARATION) | (1ull << PARSE_CHECK_TYPE_ARGUMENT) |
                           (1ull << PARSE_CHECK_SIZEOF_TYPE) | (1ull << PARSE_CHECK_ABSTRACT) | (1ull << PARSE_CHECK_OPTIONAL);
}

#undef BUSTER_PARSE_SET

#undef BUSTER_PARSE_ADD

#undef BUSTER_PARSE_REMOVE

// A token's class, from its role, its kind, its name (a keyword's IdentId) and the typed state after it.
static uint8_t parse_check_class(uint8_t role, uint8_t vkind, uint32_t name, bool typed)
{
    uint8_t result = role;
    if (role == PARSE_ROLE_OPERAND)
    {
        result = vkind == PARSE_KIND_IDENTIFIER || vkind == PARSE_VKIND_TYPE_NAME ? PARSE_ROLE_OPERAND :
                 vkind == PARSE_KIND_NUMBER || vkind == PARSE_KIND_CHARACTER ? PARSE_CLASS_CONSTANT :
                 vkind == PARSE_KIND_STRING ? PARSE_CLASS_STRING : vkind == PARSE_KIND_ELLIPSIS ? PARSE_CLASS_ELLIPSIS : PARSE_CLASS_WORD;
    }
    else if (role == PARSE_ROLE_SPECIFIER)
    {
        const bool storage = vkind == PARSE_KIND_STORAGE || vkind == PARSE_KIND_TYPEDEF;
        result = vkind == PARSE_KIND_LABEL ? PARSE_CLASS_LOCAL_LABEL :
                 vkind == PARSE_KIND_TYPE || vkind == PARSE_VKIND_TYPE_NAME ? PARSE_CLASS_TYPE : vkind == PARSE_KIND_EXTENSION ? PARSE_CLASS_EXTENSION :
                 storage ? (typed ? PARSE_CLASS_TYPED_STORAGE : PARSE_CLASS_STORAGE) : typed ? PARSE_CLASS_TYPED : PARSE_ROLE_SPECIFIER;
    }
    else if (role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER)
    {
        result = typed ? PARSE_CLASS_TYPED_ATTRIBUTE : PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
    }
    else if (role == PARSE_ROLE_PREFIX)
    {
        result = vkind == PARSE_KIND_STAR ? PARSE_CLASS_STAR : vkind == PARSE_KIND_SIZEOF ? PARSE_CLASS_SIZEOF : vkind == PARSE_KIND_AMPAMP ? PARSE_CLASS_LABEL_ADDRESS :
                 PARSE_ROLE_PREFIX;
    }
    else if (role == PARSE_ROLE_LEAF_RIGHT || role == PARSE_ROLE_QUALIFIER_LEFT)
    {
        switch (vkind)
        {
            case PARSE_KIND_STRUCT:
            case PARSE_KIND_ENUM: result = PARSE_CLASS_TAG_KEYWORD; break;
            case PARSE_KIND_ATTRIBUTE: result = PARSE_CLASS_ATTRIBUTE_KEYWORD; break;
            case PARSE_KIND_ASM: result = PARSE_CLASS_ASM_KEYWORD; break;
            case PARSE_KIND_TYPEOF:
                result = name == C_NAME_BITINT || name == C_NAME_EXTINT ? PARSE_CLASS_BITINT_KEYWORD : PARSE_CLASS_SPECIFIER_KEYWORD;
                break;
            case PARSE_KIND_ATOMIC: result = role == PARSE_ROLE_LEAF_RIGHT ? PARSE_CLASS_ATOMIC_KEYWORD : role; break;
            case PARSE_KIND_ALIGNAS: result = PARSE_CLASS_ALIGNAS_KEYWORD; break;
            case PARSE_KIND_TYPE_ARGUMENT:
                result = name == C_NAME_BUILTIN_OFFSETOF ? PARSE_CLASS_OFFSETOF_KEYWORD :
                         name == C_NAME_BUILTIN_VA_ARG || name == C_NAME_BUILTIN_CONVERTVECTOR ? PARSE_CLASS_VA_ARG_KEYWORD :
                         name == C_NAME_BUILTIN_BIT_CAST ? PARSE_CLASS_BIT_CAST_KEYWORD : PARSE_CLASS_OPERAND_KEYWORD;
                break;
            case PARSE_KIND_GENERIC: result = PARSE_CLASS_OPERAND_KEYWORD; break;
            case PARSE_KIND_STATIC_ASSERT: result = PARSE_CLASS_ASSERT_KEYWORD; break;
            case PARSE_KIND_IF:
            case PARSE_KIND_WHILE:
            case PARSE_KIND_SWITCH:
            case PARSE_KIND_FOR: result = PARSE_CLASS_STATEMENT_KEYWORD; break;
            case PARSE_KIND_IDENTIFIER:
            case PARSE_VKIND_TYPE_NAME: result = role == PARSE_ROLE_LEAF_RIGHT ? PARSE_CLASS_BODY_NAME : role; break;
            case PARSE_KIND_QUALIFIER:
            case PARSE_KIND_STORAGE:
            case PARSE_KIND_GOTO:
            case PARSE_KIND_CALLING_CONVENTION: result = role == PARSE_ROLE_LEAF_RIGHT ? PARSE_CLASS_ASM_QUALIFIER : role; break;
            default: break;
        }
    }
    else if (role == PARSE_ROLE_RETURN)
    {
        // A computed goto's `*` is checked as a prefix `*` is: an operand must follow it. Another unary operator after goto,
        // which K2 gives the same role (parse_unary_role), keeps `return`'s class, which nothing lets follow goto: an error.
        result = vkind == PARSE_KIND_GOTO                                    ? PARSE_CLASS_GOTO
                 : vkind == PARSE_KIND_BREAK || vkind == PARSE_KIND_CONTINUE ? PARSE_CLASS_JUMP
                 : vkind == PARSE_KIND_STAR                                  ? PARSE_CLASS_STAR
                                                                             : PARSE_ROLE_RETURN;
    }
    else if (role == PARSE_ROLE_OPEN_CASE)
    {
        result = vkind == PARSE_KIND_DEFAULT ? PARSE_CLASS_DEFAULT : PARSE_ROLE_OPEN_CASE;
    }
    else if (role == PARSE_ROLE_MEMBER_OPERATOR)
    {
        // `->` is an expression's only: designators (an initializer's and __builtin_offsetof's) take `.`.
        result = vkind == PARSE_KIND_ARROW ? PARSE_CLASS_ARROW : PARSE_ROLE_MEMBER_OPERATOR;
    }
    return result;
}

// The type of the group an opener of class `opener` opens, in mode `mode`, after a token of class `before`.
static uint8_t parse_check_group(uint8_t opener, uint8_t mode, uint8_t before)
{
    const ParseCheckTables* t = &parse_check_tables;
    const bool declaration = ((t->declaration_modes >> mode) & 1) != 0;
    const bool named = ((t->named_modes >> mode) & 1) != 0;
    const bool abstract = mode == PARSE_CHECK_TYPE_DECLARATION || mode == PARSE_CHECK_TYPE_ARGUMENT || mode == PARSE_CHECK_SIZEOF_TYPE || mode == PARSE_CHECK_ABSTRACT;
    // An initializer's designators: `[` starts one at an initializer's start, and goes on after one.
    const bool designator = mode == PARSE_CHECK_INITIALIZER_START || mode == PARSE_CHECK_INITIALIZER_DESIGNATOR;
    uint8_t result = PARSE_GROUP_OTHER;
    switch (opener)
    {
        case PARSE_ROLE_OPEN_BLOCK:
        case PARSE_ROLE_OPEN_BODY: result = PARSE_GROUP_BLOCK; break;
        case PARSE_ROLE_OPEN_STRUCT: result = PARSE_GROUP_STRUCT; break;
        case PARSE_ROLE_OPEN_ENUM: result = PARSE_GROUP_ENUM; break;
        case PARSE_ROLE_OPEN_FUNCTION: result = PARSE_GROUP_PARAMETERS; break;
        case PARSE_ROLE_OPEN_CALL: result = declaration ? PARSE_GROUP_PARAMETERS : PARSE_GROUP_ARGUMENTS; break;
        case PARSE_ROLE_OPEN_GROUP:
            result = named ? PARSE_GROUP_DECLARATOR : abstract ? PARSE_GROUP_ABSTRACT : declaration ? PARSE_GROUP_OPTIONAL : PARSE_GROUP_EXPRESSION;
            break;
        case PARSE_ROLE_OPEN_CAST:
        case PARSE_ROLE_OPEN_COMPOUND: result = PARSE_GROUP_TYPE; break;
        case PARSE_ROLE_OPEN_SIZEOF: result = PARSE_GROUP_SIZEOF; break;
        case PARSE_ROLE_OPEN_SPECIFIER:
            result = before == PARSE_CLASS_SPECIFIER_KEYWORD ? PARSE_GROUP_SIZEOF : before == PARSE_CLASS_ATOMIC_KEYWORD ? PARSE_GROUP_TYPE :
                     before == PARSE_CLASS_ALIGNAS_KEYWORD ? PARSE_GROUP_ALIGNAS : before == PARSE_CLASS_BITINT_KEYWORD ? PARSE_GROUP_BITINT : PARSE_GROUP_OTHER;
            break;
        case PARSE_ROLE_OPEN_ATTRIBUTE: result = before == PARSE_CLASS_ASM_KEYWORD ? PARSE_GROUP_ASM_LABEL : PARSE_GROUP_ATTRIBUTE; break;
        case PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER:
        case PARSE_ROLE_OPEN_ATTRIBUTE_LEFT:
        case PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT: result = PARSE_GROUP_ATTRIBUTE; break;
        case PARSE_ROLE_OPEN_ATTRIBUTE_LIST: result = PARSE_GROUP_ATTRIBUTE_LIST; break;
        case PARSE_ROLE_OPEN_KEYWORD:
            result = before == PARSE_CLASS_OFFSETOF_KEYWORD ? PARSE_GROUP_OFFSETOF : before == PARSE_CLASS_VA_ARG_KEYWORD ? PARSE_GROUP_VA_ARG :
                     before == PARSE_CLASS_BIT_CAST_KEYWORD ? PARSE_GROUP_BIT_CAST : before == PARSE_CLASS_ASSERT_KEYWORD ? PARSE_GROUP_ASSERT : PARSE_GROUP_OTHER;
            break;
        case PARSE_ROLE_OPEN_CONDITION:
        case PARSE_ROLE_OPEN_TERNARY: result = PARSE_GROUP_EXPRESSION; break;
        case PARSE_ROLE_OPEN_FOR: result = PARSE_GROUP_FOR; break;
        case PARSE_ROLE_OPEN_SUBSCRIPT:
            result = declaration ? PARSE_GROUP_ARRAY : designator ? PARSE_GROUP_DESIGNATOR : PARSE_GROUP_SUBSCRIPT;
            break;
        case PARSE_ROLE_OPEN_INITIALIZER:
        case PARSE_ROLE_OPEN_COMPOUND_BODY: result = PARSE_GROUP_INITIALIZER; break;
        case PARSE_ROLE_OPEN_CASE: result = PARSE_GROUP_CASE; break;
        default: break;
    }
    return result;
}

// What an item that starts with a token of class `start` is, in a group of type `group` (BUSTER_PARSE_ITEM_*): a
// declaration if specifiers start it (in an initializer list, a designator; an enumerator always), a _Static_assert, or
// another.
static uint8_t parse_check_item(uint8_t group, uint8_t start)
{
    const bool specifiers = start == PARSE_CLASS_TYPE || start == PARSE_ROLE_SPECIFIER || start == PARSE_CLASS_TYPED || start == PARSE_CLASS_STORAGE ||
                            start == PARSE_CLASS_TYPED_STORAGE || start == PARSE_CLASS_TAG_KEYWORD || start == PARSE_CLASS_SPECIFIER_KEYWORD ||
                            start == PARSE_CLASS_ALIGNAS_KEYWORD || start == PARSE_CLASS_ATTRIBUTE_KEYWORD || start == PARSE_CLASS_ATOMIC_KEYWORD ||
                            start == PARSE_CLASS_BITINT_KEYWORD || start == PARSE_CLASS_EXTENSION;
    uint8_t result = specifiers ? BUSTER_PARSE_ITEM_DECLARATION : start == PARSE_CLASS_ASSERT_KEYWORD ? BUSTER_PARSE_ITEM_ASSERTION : BUSTER_PARSE_ITEM_OTHER;
    if (group == PARSE_GROUP_ENUM)
    {
        result = BUSTER_PARSE_ITEM_DECLARATION;
    }
    else if (group == PARSE_GROUP_INITIALIZER)
    {
        result = start == PARSE_ROLE_MEMBER_OPERATOR || start == PARSE_ROLE_OPEN_SUBSCRIPT ? BUSTER_PARSE_ITEM_DECLARATION : BUSTER_PARSE_ITEM_OTHER;
    }
    return result;
}

// The mode of a token in a group of type `group`: of its item's start, or by what its item is (`kind`) and in a
// declaration by its part, and by its item's place in the group (`item`: 1 for the first, 2 for the second, 3 for any
// later one).
static uint8_t parse_check_mode(uint8_t group, bool start, uint8_t kind, uint8_t part, uint8_t item)
{
    const bool first = item == 1;
    const bool declares = kind == BUSTER_PARSE_ITEM_DECLARATION;
    const bool assertion = kind == BUSTER_PARSE_ITEM_ASSERTION && !start;
    uint8_t result = PARSE_CHECK_NONE;
    const bool initializer = part == PARSE_PART_INITIALIZER;
    const bool width = part == PARSE_PART_WIDTH;
    switch (group)
    {
        case PARSE_GROUP_FILE:
            result = start ? PARSE_CHECK_FILE_START : assertion ? PARSE_CHECK_ASSERTION : !declares || width ? PARSE_CHECK_NONE :
                     initializer ? PARSE_CHECK_FILE_INITIALIZER : PARSE_CHECK_FILE_DECLARATION;
            break;
        case PARSE_GROUP_BLOCK:
            result = start ? PARSE_CHECK_BLOCK_START : assertion ? PARSE_CHECK_ASSERTION : !declares ? PARSE_CHECK_BLOCK_STATEMENT : width ? PARSE_CHECK_NONE :
                     initializer ? PARSE_CHECK_BLOCK_INITIALIZER : PARSE_CHECK_BLOCK_DECLARATION;
            break;
        case PARSE_GROUP_STRUCT:
            result = start ? PARSE_CHECK_STRUCT_START : assertion ? PARSE_CHECK_ASSERTION : !declares || initializer ? PARSE_CHECK_NONE :
                     width ? PARSE_CHECK_STRUCT_WIDTH : PARSE_CHECK_STRUCT_DECLARATION;
            break;
        case PARSE_GROUP_PARAMETERS:
            result = start ? PARSE_CHECK_PARAMETERS_START : declares && part == PARSE_PART_DECLARATION ? PARSE_CHECK_PARAMETERS_DECLARATION : PARSE_CHECK_NONE;
            break;
        case PARSE_GROUP_FOR:
            result = !first ? (start ? PARSE_CHECK_FOR_CLAUSE_START : PARSE_CHECK_FOR_EXPRESSION) : start ? PARSE_CHECK_FOR_START :
                     !declares ? PARSE_CHECK_FOR_EXPRESSION : width ? PARSE_CHECK_NONE : initializer ? PARSE_CHECK_FOR_INITIALIZER : PARSE_CHECK_FOR_DECLARATION;
            break;
        case PARSE_GROUP_TYPE:
            result = !first ? PARSE_CHECK_NONE : start ? PARSE_CHECK_TYPE_START : declares && part == PARSE_PART_DECLARATION ? PARSE_CHECK_TYPE_DECLARATION :
                                                                                                                                PARSE_CHECK_NONE;
            break;
        case PARSE_GROUP_SIZEOF:
            // A type name, or an expression, whose commas (separators here) start the next operand.
            result = !first ? (start ? PARSE_CHECK_ARGUMENTS_START : PARSE_CHECK_SIZEOF_EXPRESSION) : start ? PARSE_CHECK_SIZEOF_START :
                     !declares ? PARSE_CHECK_SIZEOF_EXPRESSION : part == PARSE_PART_DECLARATION ? PARSE_CHECK_SIZEOF_TYPE : PARSE_CHECK_NONE;
            break;
        case PARSE_GROUP_ENUM:
            result = start ? PARSE_CHECK_ENUM_START : initializer ? PARSE_CHECK_ENUM_VALUE : width ? PARSE_CHECK_NONE : PARSE_CHECK_ENUM_NAME;
            break;
        case PARSE_GROUP_ARGUMENTS: result = start ? PARSE_CHECK_ARGUMENTS_START : PARSE_CHECK_ARGUMENTS; break;
        case PARSE_GROUP_DECLARATOR: result = PARSE_CHECK_DECLARATOR; break;
        case PARSE_GROUP_ABSTRACT: result = PARSE_CHECK_ABSTRACT; break;
        case PARSE_GROUP_OPTIONAL: result = PARSE_CHECK_OPTIONAL; break;
        case PARSE_GROUP_EXPRESSION: result = PARSE_CHECK_EXPRESSION; break;
        case PARSE_GROUP_SUBSCRIPT: result = PARSE_CHECK_SUBSCRIPT; break;
        case PARSE_GROUP_ARRAY:
        case PARSE_GROUP_DESIGNATOR: result = PARSE_CHECK_ARRAY; break;
        case PARSE_GROUP_CASE: result = PARSE_CHECK_CASE; break;
        case PARSE_GROUP_INITIALIZER:
            result = start ? PARSE_CHECK_INITIALIZER_START : declares && part == PARSE_PART_DECLARATION ? PARSE_CHECK_INITIALIZER_DESIGNATOR :
                     declares && width ? PARSE_CHECK_NONE : PARSE_CHECK_INITIALIZER;
            break;
        case PARSE_GROUP_ATTRIBUTE: result = PARSE_CHECK_ATTRIBUTE; break;
        case PARSE_GROUP_ATTRIBUTE_LIST: result = start ? PARSE_CHECK_ATTRIBUTE_LIST_START : PARSE_CHECK_ATTRIBUTE_LIST; break;
        case PARSE_GROUP_ASM_LABEL: result = PARSE_CHECK_ASM_LABEL; break;
        case PARSE_GROUP_OFFSETOF:
            result = item == 1 ? (start ? PARSE_CHECK_TYPE_START : declares && part == PARSE_PART_DECLARATION ? PARSE_CHECK_TYPE_ARGUMENT : PARSE_CHECK_NONE) :
                     item == 2 ? (start ? PARSE_CHECK_MEMBER_START : PARSE_CHECK_MEMBER) : PARSE_CHECK_NONE;
            break;
        case PARSE_GROUP_VA_ARG:
            result = item == 1 ? (start ? PARSE_CHECK_ARGUMENTS_START : PARSE_CHECK_ARGUMENTS) :
                     item == 2 ? (start ? PARSE_CHECK_TYPE_START : declares && part == PARSE_PART_DECLARATION ? PARSE_CHECK_TYPE_DECLARATION : PARSE_CHECK_NONE) :
                     PARSE_CHECK_NONE;
            break;
        case PARSE_GROUP_BIT_CAST:
            result = item == 1 ? (start ? PARSE_CHECK_TYPE_START : declares && part == PARSE_PART_DECLARATION ? PARSE_CHECK_TYPE_ARGUMENT : PARSE_CHECK_NONE) :
                     item == 2 ? (start ? PARSE_CHECK_ARGUMENTS_START : PARSE_CHECK_ARGUMENTS) : PARSE_CHECK_NONE;
            break;
        case PARSE_GROUP_ASSERT:
            result = item == 1 ? (start ? PARSE_CHECK_ARGUMENTS_START : PARSE_CHECK_ASSERT_CONDITION) :
                     item == 2 ? (start ? PARSE_CHECK_MESSAGE_START : PARSE_CHECK_MESSAGE) : PARSE_CHECK_NONE;
            break;
        case PARSE_GROUP_ALIGNAS:
            result = !first ? PARSE_CHECK_NONE : start ? PARSE_CHECK_SIZEOF_START : !declares ? PARSE_CHECK_CONDITIONAL :
                     part == PARSE_PART_DECLARATION ? PARSE_CHECK_SIZEOF_TYPE : PARSE_CHECK_NONE;
            break;
        case PARSE_GROUP_BITINT: result = !first ? PARSE_CHECK_NONE : start ? PARSE_CHECK_ARGUMENTS_START : PARSE_CHECK_CONDITIONAL; break;
        default: break;
    }
    return result;
}

// The part of a declaration after a token of class `c` at its depth.
static uint8_t parse_check_part_after(uint8_t c, uint8_t part)
{
    return c == PARSE_ROLE_ASSIGN ? PARSE_PART_INITIALIZER : c == PARSE_ROLE_COMMA ? PARSE_PART_DECLARATION : c == PARSE_ROLE_BITFIELD ? PARSE_PART_WIDTH : part;
}

// Whether a token that isn't a closer, whose previous token has role `previous_role` and class `previous`, starts an
// item: it is first in its group, or follows a token after which an item starts (`;`, `{`, `}`, a head, a label) or a
// separator.
static bool parse_check_starts(bool first_in_group, uint8_t previous_role, uint8_t previous)
{
    const uint8_t pred = parse_role_preds[previous_role];
    return first_in_group || pred == PARSE_PRED_ITEM || pred == PARSE_PRED_HEAD || previous == PARSE_ROLE_SEPARATOR;
}

// Whether a token of kind `kind` (before one of virtual kind `next`) may follow a tag's body, as the scalar parser's
// parse_scalar_valid_after_tag has it: what may follow a type specifier, or a qualifier or storage class not before a
// type specifier (which clang takes for the next declaration's, after a missing `;`).
static bool parse_check_valid_after_tag(uint8_t kind, uint8_t next, bool in_struct)
{
    const bool next_type = next == PARSE_KIND_TYPE || next == PARSE_KIND_STRUCT || next == PARSE_KIND_ENUM || next == PARSE_KIND_TYPEOF || next == PARSE_VKIND_TYPE_NAME;
    bool result = false;
    switch (kind)
    {
        case PARSE_KIND_SEMI:
        case PARSE_KIND_STAR:
        case PARSE_KIND_AMPAMP:
        case PARSE_KIND_IDENTIFIER:
        case PARSE_VKIND_TYPE_NAME:
        case PARSE_KIND_R_PAREN:
        case PARSE_KIND_ELLIPSIS:
        case PARSE_KIND_COMMA:
        case PARSE_KIND_L_SQUARE:
        case PARSE_KIND_L_PAREN:
        case PARSE_KIND_ATTRIBUTE: result = true; break;
        case PARSE_KIND_COLON: result = in_struct; break;
        case PARSE_KIND_QUALIFIER:
        case PARSE_KIND_STORAGE:
        case PARSE_KIND_TYPEDEF:
        case PARSE_KIND_ATOMIC: result = !next_type; break;
        default: break;
    }
    return result;
}

// The kind of each closer role's token: a closer's opener's role gave it its role, so a closer of another kind is
// another bracket's.
static uint8_t parse_check_closer_kind(uint8_t role)
{
    uint8_t result = PARSE_KIND_R_PAREN;
    switch (role)
    {
        case PARSE_ROLE_CLOSE_SUBSCRIPT: result = PARSE_KIND_R_SQUARE; break;
        case PARSE_ROLE_CLOSE_BLOCK:
        case PARSE_ROLE_CLOSE_BODY:
        case PARSE_ROLE_CLOSE_STRUCT:
        case PARSE_ROLE_CLOSE_ENUM:
        case PARSE_ROLE_CLOSE_INITIALIZER:
        case PARSE_ROLE_CLOSE_COMPOUND_BODY: result = PARSE_KIND_R_BRACE; break;
        case PARSE_ROLE_CLOSE_TERNARY:
        case PARSE_ROLE_CLOSE_CASE: result = PARSE_KIND_COLON; break;
        default: break;
    }
    return result;
}

static const char* const parse_class_names[PARSE_CLASS_COUNT] =
{
#define BUSTER_PARSE_ROLE_CLASS_NAME(name, level, compare, rule) #name,
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_CLASS_NAME)
#undef BUSTER_PARSE_ROLE_CLASS_NAME
#define BUSTER_PARSE_CLASS_NAME(name) #name,
    BUSTER_PARSE_CLASSES(BUSTER_PARSE_CLASS_NAME)
#undef BUSTER_PARSE_CLASS_NAME
};

static const char* const parse_check_mode_names[PARSE_CHECK_MODE_COUNT] =
{
#define BUSTER_PARSE_MODE_NAME(name) #name,
    BUSTER_PARSE_MODES(BUSTER_PARSE_MODE_NAME)
#undef BUSTER_PARSE_MODE_NAME
};

// The kernels' K6 frames.
static ParseCheckFrames parse_check_frames(const ParseKernels* kernels)
{
    return (ParseCheckFrames){
        .group = kernels->check_group,
        .kind = kernels->check_kind,
        .part = kernels->check_part,
        .items = kernels->check_items,
        .typed = kernels->check_typed,
        .after_body = kernels->check_after_body,
    };
}

// The frames' first entry, file scope's, and the carry before the first token.
static void parse_check_begin(const ParseCheckFrames* frames, ParseCheckCarry* carry)
{
    frames->group[0] = PARSE_GROUP_FILE;
    frames->kind[0] = BUSTER_PARSE_ITEM_OTHER;
    frames->part[0] = PARSE_PART_START;
    frames->items[0] = 0;
    frames->typed[0] = 0;
    frames->after_body[0] = 0;
    *carry = (ParseCheckCarry){
        .before_previous = PARSE_ROLE_NONE,
        .previous = PARSE_ROLE_NONE,
        .previous_role = PARSE_ROLE_NONE,
        .previous_mode = PARSE_CHECK_FILE_START,
        .previous_items = 0,
        .previous_start = 0,
        .previous_group = PARSE_GROUP_FILE,
        .previous_depth = 0,
    };
}

// A token's role once K6 has its group (#1968): a call's parentheses that hold a declarator's parameters are a
// function's, as the scalar parser marks them. K2's rules can't tell them apart (both follow an operand); K6's group
// can (PARSE_GROUP_PARAMETERS, from the declaration's mode). Its vector twin is parse_function_roles_block_avx512.
static uint8_t parse_function_role(uint8_t role, uint8_t group)
{
    const bool parameters = group == PARSE_GROUP_PARAMETERS;
    return parameters && role == PARSE_ROLE_OPEN_CALL ? PARSE_ROLE_OPEN_FUNCTION : parameters && role == PARSE_ROLE_CLOSE_CALL ? PARSE_ROLE_CLOSE_FUNCTION : role;
}

// K6, the twin, on tokens [begin, end): every token's mode from the frames (`frames`, which have an entry per node
// depth) and the carry, then the checks, which flag the tokens that fail them. `names` are the tokens' IdentIds,
// `line_flags` their output flags (BUSTER_PP_START_OF_LINE) and `typed` K2's typed state per token. `classes` gets
// each token's class, which the rule after a tag's body reads back. For the tests, `modes` and `reasons` (each 0, or a
// byte per token) get each token's mode and failed checks (BUSTER_PARSE_CHECK_*; an END failure is the token's before
// it, which ends its group). parse_check_end checks the end of the file.
static void parse_check_range(ParseKernels* kernels, const uint32_t* names, const uint8_t* line_flags, const uint8_t* typed, uint32_t begin, uint32_t end,
                              const ParseCheckFrames* frames, ParseCheckCarry* carry, uint8_t* classes, uint8_t* modes, uint8_t* reasons)
{
    const ParseCheckTables* t = &parse_check_tables;
    for (uint32_t i = begin; i < end; i += 1)
    {
        const uint8_t role = kernels->roles[i];
        const uint32_t depth = kernels->node_depths[i];
        const uint8_t group = frames->group[depth];
        // A closer is its group's, at the depth of its opener: it starts nothing, and closes the group at depth + 1.
        const bool closer = parse_role_deltas[role] < 0;
        const bool empty = closer && depth == carry->previous_depth;
        const uint8_t inner_group = frames->group[depth + 1];
        const uint8_t inner_items = empty ? 0 : frames->items[depth + 1];
        // _Alignas's closer is a specifier as an attribute's is, whose specifiers had a type if they had before it.
        const bool alignas = role == PARSE_ROLE_CLOSE_SPECIFIER && inner_group == PARSE_GROUP_ALIGNAS;
        const uint8_t c = alignas ? (frames->typed[depth + 1] ? PARSE_CLASS_TYPED_ALIGNAS_CLOSE : PARSE_CLASS_ALIGNAS_CLOSE) :
                                    parse_check_class(role, kernels->vkinds[i], names[i], typed[i] != 0);
        const uint8_t previous = carry->previous;
        const bool start = !closer && parse_check_starts(depth > carry->previous_depth || i == 0, carry->previous_role, previous);
        if (start)
        {
            frames->kind[depth] = parse_check_item(group, c);
            frames->part[depth] = PARSE_PART_DECLARATION;
            frames->items[depth] += frames->items[depth] < 3;
        }
        const uint8_t kind = frames->kind[depth];
        const uint8_t items = frames->items[depth];
        const uint8_t mode = parse_check_mode(group, start, kind, frames->part[depth], items);
        // An attribute specifier after a tag's body is the tag's: the body's rule looks past it.
        const bool transparent = c == PARSE_CLASS_ATTRIBUTE_KEYWORD || c == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER || c == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ||
                                 c == PARSE_CLASS_TYPED_ATTRIBUTE;
        // The token before this one ends its group if this one is at a lower depth (a closer), and it is the group's
        // declarator that may need a name.
        const bool previous_last = depth < carry->previous_depth;
        const bool named = ((t->named_modes >> (previous_last ? carry->previous_mode : mode)) & 1) != 0;
        const bool skipped = named && parse_class_set_has(&t->before_name, previous) && parse_class_set_has(&t->after_name, c) &&
                             !(parse_class_set_has(&t->specifier_ends, previous) && (c == PARSE_ROLE_TERMINATOR || c == PARSE_ROLE_BITFIELD));
        const bool after_body = frames->after_body[depth] != 0;
        // A for's group holds three clauses: two `;`, the last one the second item's; a builtin's group two arguments
        // (_Static_assert's one or two).
        const bool clauses = (group == PARSE_GROUP_FOR && role == PARSE_ROLE_TERMINATOR && items == 3) ||
                             (role == PARSE_ROLE_CLOSE_FOR && !(previous == PARSE_ROLE_TERMINATOR ? inner_items == 2 : inner_items == 3)) ||
                             (role == PARSE_ROLE_CLOSE_KEYWORD && inner_items != 2 && !(inner_group == PARSE_GROUP_ASSERT && inner_items == 1));
        const uint8_t failed = (uint8_t)((!parse_class_set_has(&t->follows[previous], c) ? BUSTER_PARSE_CHECK_PAIR : 0) |
                                         (!parse_class_set_has(&t->allows[mode], c) ? BUSTER_PARSE_CHECK_MODE : 0) |
                                         (previous_last && !parse_class_set_has(&t->ends[carry->previous_mode], previous) ? BUSTER_PARSE_CHECK_END : 0) |
                                         (skipped ? BUSTER_PARSE_CHECK_NAME : 0) |
                                         // A label's name starts its statement.
                                         (role == PARSE_ROLE_LABEL && !(carry->previous_start && previous == PARSE_ROLE_OPERAND) ? BUSTER_PARSE_CHECK_LABEL : 0) |
                                         (clauses ? BUSTER_PARSE_CHECK_CLAUSES : 0) |
                                         // A statement expression: its block first and last in parentheses, in a function.
                                         (mode == PARSE_CHECK_EXPRESSION && ((c == PARSE_ROLE_OPEN_BLOCK && (previous != PARSE_ROLE_OPEN_GROUP ||
                                                                                                            frames->group[1] != PARSE_GROUP_BLOCK)) ||
                                                                             (c == PARSE_ROLE_CLOSE_BLOCK && kernels->roles[i + 1] != PARSE_ROLE_CLOSE_GROUP)) ?
                                              BUSTER_PARSE_CHECK_CLAUSES : 0) |
                                         (role == PARSE_ROLE_ERROR || (closer && kernels->vkinds[i] != parse_check_closer_kind(role)) ? BUSTER_PARSE_CHECK_ERROR : 0) |
                                         // Only an array declarator's brackets may be empty.
                                         (empty && (inner_group == PARSE_GROUP_SUBSCRIPT || inner_group == PARSE_GROUP_DESIGNATOR) ? BUSTER_PARSE_CHECK_CLAUSES : 0) |
                                         // After a tag's body (past its attributes), or a tag and a line break, what may
                                         // follow a type; and nothing postfix after `&&label`.
                                         (((after_body && !transparent) || (previous == PARSE_ROLE_TAG && (line_flags[i] & BUSTER_PP_START_OF_LINE) != 0)) &&
                                                  !parse_check_valid_after_tag(kernels->vkinds[i], kernels->vkinds[i + 1], group == PARSE_GROUP_STRUCT) ?
                                              BUSTER_PARSE_CHECK_TAG : 0) |
                                         (carry->before_previous == PARSE_CLASS_LABEL_ADDRESS && (c == PARSE_ROLE_OPEN_CALL || c == PARSE_ROLE_OPEN_SUBSCRIPT ||
                                                                                                  c == PARSE_ROLE_POSTFIX || c == PARSE_ROLE_MEMBER_OPERATOR ||
                                                                                                  c == PARSE_CLASS_ARROW) ?
                                              BUSTER_PARSE_CHECK_TAG : 0) |
                                         // A `*` outside declarators is an operator: an operand follows it.
                                         (previous == PARSE_CLASS_STAR && ((t->declaration_modes >> carry->previous_mode) & 1) == 0 &&
                                                  !parse_class_set_has(&t->operand_starts, c) ? BUSTER_PARSE_CHECK_PAIR : 0));
        kernels->flags[i] |= failed != 0;
        classes[i] = c;
        if (modes != 0)
        {
            modes[i] = mode;
            reasons[i] = failed;
        }
        if (kind == BUSTER_PARSE_ITEM_DECLARATION)
        {
            frames->part[depth] = parse_check_part_after(c, frames->part[depth]);
        }
        frames->after_body[depth] = c == PARSE_ROLE_CLOSE_STRUCT || c == PARSE_ROLE_CLOSE_ENUM || (after_body && transparent);
        const bool opener = parse_role_rules[role] == PARSE_RULE_OPENER;
        const uint8_t opened = opener ? parse_check_group(c, mode, previous) : PARSE_GROUP_OTHER;
        kernels->check_groups[i] = closer ? inner_group : opened;
        if (opener)
        {
            frames->group[depth + 1] = opened;
            frames->kind[depth + 1] = BUSTER_PARSE_ITEM_OTHER;
            frames->part[depth + 1] = PARSE_PART_START;
            frames->items[depth + 1] = 0;
            frames->typed[depth + 1] = i >= 2 && typed[i - 2] != 0;
            frames->after_body[depth + 1] = 0;
        }
        *carry = (ParseCheckCarry){
            .before_previous = previous,
            .previous = c,
            .previous_role = role,
            .previous_mode = mode,
            .previous_items = items,
            .previous_start = start,
            .previous_group = group,
            .previous_depth = depth,
        };
    }
}

// The end of the file: its last token ends file scope's last item. Flags it if it can't.
static void parse_check_end(ParseKernels* kernels, uint32_t count, const ParseCheckCarry* carry, uint8_t* reasons)
{
    if (count != 0 && !parse_class_set_has(&parse_check_tables.ends[carry->previous_mode], carry->previous))
    {
        kernels->flags[count - 1] = 1;
        if (reasons != 0)
        {
            reasons[count - 1] |= BUSTER_PARSE_CHECK_END;
        }
    }
}

// A function's body follows a declaration's only declarator, and only one that its first derivation makes a
// function's (parse_scalar_complete_declarator). The tables can't tell `int (*p)(int) {` from `int (*f(void))[3] {`,
// nor see a comma before the declarator, so a body whose declaration has a declarator group or a comma at the body's
// depth is flagged, for the scalar parser to decide. Bodies are few: each one's declaration is read back from it, to
// the end of the item before it.
static void parse_check_body(ParseKernels* kernels, uint32_t body, uint8_t* reasons)
{
    const uint32_t depth = kernels->node_depths[body];
    bool done = false;
    bool fails = false;
    for (uint32_t j = body; j-- > 0 && !done;)
    {
        const uint32_t at = kernels->node_depths[j];
        const uint8_t role = kernels->roles[j];
        done = at < depth || (at == depth && (role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_BODY));
        fails = !done && at == depth && (role == PARSE_ROLE_OPEN_GROUP || role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_COMMA ||
                                         (role == PARSE_ROLE_SPECIFIER && kernels->vkinds[j] == PARSE_KIND_TYPEDEF));
        done |= fails;
    }
    kernels->flags[body] |= fails;
    if (reasons != 0)
    {
        reasons[body] |= fails ? BUSTER_PARSE_CHECK_CLAUSES : 0;
    }
}

// K6, the twin, on the whole file.
static void parse_check_scalar(ParseKernels* kernels, const uint32_t* names, const uint8_t* line_flags, uint32_t count, const uint8_t* typed,
                               const ParseCheckFrames* frames, uint8_t* classes, uint8_t* modes, uint8_t* reasons)
{
    ParseCheckCarry carry;
    parse_check_begin(frames, &carry);
    parse_check_range(kernels, names, line_flags, typed, 0, count, frames, &carry, classes, modes, reasons);
    parse_check_end(kernels, count, &carry, reasons);
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (kernels->roles[i] == PARSE_ROLE_OPEN_BODY)
        {
            parse_check_body(kernels, i, reasons);
        }
    }
}

// The scalar parser on tokens [begin, end] in place, with the file-scope typedef names declared before begin visible.
static void parse_fallback_item(ParseKernels* kernels, const ParseInput* input, uint32_t begin, uint32_t end, uint32_t* visible, ParseFallback* result)
{
    ParseMeanings* meanings = input->meanings;
    while (*visible < kernels->typedef_count && kernels->typedef_tokens[*visible] < begin)
    {
        const uint32_t id = kernels->typedef_log[*visible];
        meanings->meanings[id] = kernels->typedef_scopes[*visible] == BUSTER_PARSE_TYPEDEF_FILE ? PARSE_MEANING_TYPEDEF : meanings->meanings[id];
        *visible += 1;
    }
    ParseTree tree = { .roles = kernels->roles + begin, .depths = kernels->node_depths + begin, .parents = kernels->parents + begin };
    uint32_t diagnostic_count = 0;
    const ParseScalarResult parse = parse_scalar(input->kinds + begin, input->names + begin, input->flags + begin, end + 1 - begin, input->language, meanings, &tree,
                                                 input->scratch, input->diagnostics, input->diagnostic_capacity, &diagnostic_count);
    // A declaration that made a file-scope typedef name an ordinary one changes how the rest of the unit parses, which
    // the kernels' type names don't know, and a name only the earlier file-scope declarations can decide
    // (parse_scalar_attribute_name), which the scalar parser doesn't have here: the whole unit goes to the scalar parser.
    result->errors |= parse.errors != 0 || parse.out_of_memory || parse.retyped || (parse.undecided && begin != 0);
    for (uint32_t j = begin; j <= end; j += 1)
    {
        kernels->parents[j] = kernels->parents[j] == BUSTER_PARSE_ROOT ? BUSTER_PARSE_ROOT : kernels->parents[j] + begin;
    }
    result->items += 1;
    result->tokens += end + 1 - begin;
}

// Parses every top-level declaration that has a flagged token with the scalar parser, in place: roles, depths and
// parents of its tokens. The scalar parser sees the file-scope typedef names declared before each one. With
// `everything`, the whole unit is one range (unbalanced brackets, a typedef the pass can't read, or what K5 can't
// settle). The declarations end where a terminator or a function body's closer is at node depth 0, found 64 tokens at a
// time.
static ParseFallback parse_fallback(ParseKernels* kernels, const ParseInput* input, bool everything)
{
    ParseFallback result = { .items = 0, .tokens = 0, .errors = false };
    const uint32_t count = input->count;
    uint32_t visible = 0;
    if (everything && count != 0)
    {
        parse_fallback_item(kernels, input, 0, count - 1, &visible, &result);
    }
    else
    {
        uint32_t begin = 0;
        bool flagged = false;
        for (uint32_t block = 0; block < count; block += 64)
        {
            const __mmask64 live = count - block < 64 ? (((__mmask64)1 << (count - block)) - 1) : ~(__mmask64)0;
            const __m512i flag_bytes = _mm512_maskz_loadu_epi8(live, kernels->flags + block);
            uint64_t flags = _mm512_test_epi8_mask(flag_bytes, flag_bytes);
            const __m512i role = _mm512_loadu_si512(kernels->roles + block);
            __mmask64 top = 0;
            for (uint32_t q = 0; q < 4; q += 1)
            {
                top |= (__mmask64)_mm512_cmpeq_epi32_mask(_mm512_loadu_si512(kernels->node_depths + block + 16 * q), _mm512_setzero_si512()) << (16 * q);
            }
            uint64_t ends = top & live & (_mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(PARSE_ROLE_TERMINATOR)) | _mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(PARSE_ROLE_CLOSE_BODY)));
            if (!flagged && flags == 0)
            {
                begin = ends != 0 ? block + 64 - (uint32_t)__builtin_clzll(ends) : begin;
            }
            else
            {
                while (ends != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctzll(ends);
                    ends &= ends - 1;
                    const uint64_t through = lane == 63 ? ~(uint64_t)0 : (((uint64_t)1 << (lane + 1)) - 1);
                    flagged |= (flags & through) != 0;
                    flags &= ~through;
                    if (flagged)
                    {
                        parse_fallback_item(kernels, input, begin, block + lane, &visible, &result);
                    }
                    begin = block + lane + 1;
                    flagged = false;
                }
                flagged |= flags != 0;
            }
        }
        // A flagged declaration the input ends in.
        if (flagged && begin < count)
        {
            parse_fallback_item(kernels, input, begin, count - 1, &visible, &result);
        }
    }
    for (uint32_t i = 0; i < visible; i += 1)
    {
        input->meanings->meanings[kernels->typedef_log[i]] = PARSE_MEANING_ORDINARY;
    }
    return result;
}

static void parse_shifts_init(void);

static void parse_parent_tables_init(void);

static void parse_role_tables_init(void);

static void parse_statement_tables_init(void);

// Reserves the kernels' arrays for up to `capacity` tokens, with padding around each per-token array, and up to
// `name_capacity` names, in `arena`, but for the tree's roles, node depths and parents with `tree`: its columns (each of
// capacity + 2 * BUSTER_PARSE_TOKEN_PADDING entries, the first token's at the padding's end), which outlive the others,
// a parse's temporaries.
static bool parse_kernels_reserve_tree(ParseKernels* kernels, Arena* arena, uint64_t capacity, uint64_t name_capacity, const ParseTree* tree)
{
    const uint64_t slots = capacity + 2 * BUSTER_PARSE_TOKEN_PADDING;
    uint8_t* vkinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots);
    uint8_t* roles = tree == 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots) : tree->roles != 0 ? tree->roles - BUSTER_PARSE_TOKEN_PADDING : 0;
    uint8_t* flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots);
    uint32_t* node_depths = tree == 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : tree->depths != 0 ? tree->depths - BUSTER_PARSE_TOKEN_PADDING : 0;
    uint32_t* parents = tree == 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : tree->parents != 0 ? tree->parents - BUSTER_PARSE_TOKEN_PADDING : 0;
    *kernels = (ParseKernels){
        .vkinds = vkinds != 0 ? vkinds + BUSTER_PARSE_TOKEN_PADDING : 0,
        .depths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .node_depths = node_depths != 0 ? node_depths + BUSTER_PARSE_TOKEN_PADDING : 0,
        .enclosing = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .bracket_blocks = BUSTER_ARENA_PUSH_ARRAY(arena, ParseBracketBlock, slots / 64 + 1),
        .role_statics = BUSTER_ARENA_PUSH_ARRAY(arena, ParseRoleStatics, slots / 64 + 1),
        .roles = roles != 0 ? roles + BUSTER_PARSE_TOKEN_PADDING : 0,
        .parents = parents != 0 ? parents + BUSTER_PARSE_TOKEN_PADDING : 0,
        .flags = flags != 0 ? flags + BUSTER_PARSE_TOKEN_PADDING : 0,
        .stack = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .stack_keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .waiting = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .waiting_thresholds = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .lefts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .left_key_values = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_names = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, name_capacity),
        .typedef_first = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, name_capacity),
        .typedef_heads = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, name_capacity),
        .typedef_log = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_tokens = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_points = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_previous = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_scopes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
        .check_group = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
        .check_kind = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
        .check_part = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
        .check_items = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
        .check_typed = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
        .check_after_body = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
        .check_classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
        .check_groups = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
    };
    const bool result = kernels->vkinds != 0 && kernels->depths != 0 && kernels->node_depths != 0 && kernels->enclosing != 0 && kernels->bracket_blocks != 0 &&
                        kernels->role_statics != 0 && kernels->roles != 0 &&
                        kernels->parents != 0 && kernels->flags != 0 && kernels->stack != 0 && kernels->stack_keys != 0 && kernels->waiting != 0 &&
                        kernels->waiting_thresholds != 0 && kernels->lefts != 0 && kernels->left_key_values != 0 && kernels->typedef_names != 0 &&
                        kernels->typedef_first != 0 && kernels->typedef_heads != 0 && kernels->typedef_log != 0 &&
                        kernels->typedef_tokens != 0 && kernels->typedef_points != 0 && kernels->typedef_ends != 0 && kernels->typedef_previous != 0 &&
                        kernels->typedef_scopes != 0 && kernels->check_group != 0 && kernels->check_kind != 0 &&
                        kernels->check_part != 0 && kernels->check_items != 0 && kernels->check_typed != 0 && kernels->check_after_body != 0 &&
                        kernels->check_classes != 0 && kernels->check_groups != 0;
    if (result)
    {
        memset(kernels->typedef_names, 0, name_capacity);
        memset(kernels->typedef_first, 0, name_capacity * sizeof(uint32_t));
        memset(kernels->typedef_heads, 0, name_capacity * sizeof(uint32_t));
        memset(vkinds, PARSE_KIND_END, slots);
        memset(roles, PARSE_ROLE_NONE, slots);
        memset(flags, 0, slots);
        memset(node_depths, 0, slots * sizeof(uint32_t));
    }
    return result;
}

// parse_kernels_reserve_tree with the tree's arrays in `arena` too.
static bool parse_kernels_reserve(ParseKernels* kernels, Arena* arena, uint64_t capacity, uint64_t name_capacity)
{
    return parse_kernels_reserve_tree(kernels, arena, capacity, name_capacity, 0);
}

// Marks clang's predeclared typedef names (parse_predeclared_typedefs, by their IdentIds in `predeclared`) as file-scope
// type names for K3, or clears them after the parse.
static void parse_kernels_predeclared(ParseKernels* kernels, const IdentId* predeclared, bool mark)
{
    for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        kernels->typedef_names[predeclared[i]] = mark ? BUSTER_PARSE_TYPEDEF_FILE : 0;
    }
}

// Every kernel's scalar twin in turn, then the fallback, into kernels->parents. `typed` and `statements` are scratch
// arrays of a byte and a ParseStatement per token.
static ParseKernelResult parse_twins(ParseKernels* kernels, const ParseInput* input, const IdentId* predeclared, uint8_t* typed, ParseStatement* statements)
{
    ParseKernelResult result = { .flagged_tokens = 0, .everything = false };
    const uint8_t* kinds = input->kinds;
    const uint32_t* names = input->names;
    const uint32_t count = input->count;
    kernels->unbalanced = false;
    kernels->unsupported = false;
    parse_kernels_predeclared(kernels, predeclared, true);
    // K1, on ( [ {.
    kernels->unbalanced = !parse_brackets_scalar(kinds, parse_kind_deltas, count, kernels->depths, kernels->enclosing, kernels->stack);
    // K3.
    if (!kernels->unbalanced)
    {
        parse_typedefs(kernels, kinds, names, count);
    }
    parse_classify_scalar(kernels, kinds, names, count);
    // K2.
    parse_roles_scalar(kernels, count, typed);
    // K1's second part, and the roles after the colons it resolved.
    parse_secondary_scalar(kernels, count, kernels->stack);
    parse_after_colons(kernels, count);
    // K4, K5 and K6, which a unit the kernels can't take skips, as parse_vector does.
    parse_parents_scalar(kernels, count);
    const bool statements_ok = parse_statements(kernels, count, statements);
    result.everything = kernels->unbalanced || kernels->unsupported || !statements_ok;
    if (!result.everything)
    {
        const ParseCheckFrames frames = parse_check_frames(kernels);
        parse_check_scalar(kernels, names, input->flags, count, typed, &frames, kernels->check_classes, 0, 0);
        for (uint32_t i = 0; i < count; i += 1)
        {
            result.flagged_tokens += kernels->flags[i] != 0;
            kernels->roles[i] = parse_function_role(kernels->roles[i], kernels->check_groups[i]);
        }
    }
    result.fallback = parse_fallback(kernels, input, result.everything);
    // The names this unit declared as typedefs, for the next one.
    parse_typedefs_clear(kernels);
    parse_kernels_predeclared(kernels, predeclared, false);
    return result;
}

static BUSTER_BAKED_FRONTEND ParseShifts parse_shifts;

static void parse_shifts_init(void)
{
    for (uint32_t k = 0; k < 6; k += 1)
    {
        uint8_t up[64];
        uint8_t down[64];
        const uint32_t s = 1u << k;
        for (uint32_t i = 0; i < 64; i += 1)
        {
            up[i] = (uint8_t)(i >= s ? i - s : 64);
            down[i] = (uint8_t)(i + s < 64 ? i + s : 64);
        }
        parse_shifts.up8[k] = _mm512_loadu_si512(up);
        parse_shifts.down8[k] = _mm512_loadu_si512(down);
    }
    for (uint32_t k = 0; k < 5; k += 1)
    {
        uint16_t up[32];
        uint16_t down[32];
        const uint32_t s = 1u << k;
        for (uint32_t i = 0; i < 32; i += 1)
        {
            up[i] = (uint16_t)(i >= s ? i - s : 32);
            down[i] = (uint16_t)(i + s < 32 ? i + s : 32);
        }
        parse_shifts.up16[k] = _mm512_loadu_si512(up);
        parse_shifts.down16[k] = _mm512_loadu_si512(down);
    }
    uint8_t lanes8[64];
    uint16_t lanes16[32];
    for (uint32_t i = 0; i < 64; i += 1)
    {
        lanes8[i] = (uint8_t)i;
    }
    for (uint32_t i = 0; i < 32; i += 1)
    {
        lanes16[i] = (uint16_t)i;
    }
    parse_shifts.lanes8 = _mm512_loadu_si512(lanes8);
    parse_shifts.lanes16 = _mm512_loadu_si512(lanes16);
    uint8_t up2[64];
    for (uint32_t i = 0; i < 64; i += 1)
    {
        up2[i] = (uint8_t)(i >= 2 ? i - 2 : 64 + i);
    }
    parse_shifts.up2_with_fill = _mm512_loadu_si512(up2);
    uint8_t reverse8[64];
    uint8_t last8[64];
    for (uint32_t i = 0; i < 64; i += 1)
    {
        reverse8[i] = (uint8_t)(63 - i);
        last8[i] = 63;
    }
    parse_shifts.reverse8 = _mm512_loadu_si512(reverse8);
    parse_shifts.last8 = _mm512_loadu_si512(last8);
    for (uint32_t h = 0; h < 2; h += 1)
    {
        uint8_t words[64];
        uint8_t unpacked[64];
        for (uint32_t w = 0; w < 32; w += 1)
        {
            const uint32_t l = w / 8;
            const uint32_t k = w % 8;
            words[2 * w] = (uint8_t)(32 * h + w);
            words[2 * w + 1] = 0;
            unpacked[2 * w] = (uint8_t)(k < 4 ? 32 * h + 4 * l + k : 32 * h + 16 + 4 * l + k - 4);
            unpacked[2 * w + 1] = 0;
        }
        parse_shifts.words[h] = _mm512_loadu_si512(words);
        parse_shifts.unpacked[h] = _mm512_loadu_si512(unpacked);
    }
    for (uint32_t q = 0; q < 4; q += 1)
    {
        uint8_t dwords[64];
        for (uint32_t d = 0; d < 16; d += 1)
        {
            dwords[4 * d] = (uint8_t)(16 * q + d);
            dwords[4 * d + 1] = 0;
            dwords[4 * d + 2] = 0;
            dwords[4 * d + 3] = 0;
        }
        parse_shifts.dwords[q] = _mm512_loadu_si512(dwords);
    }
}

BUSTER_INLINE __m512i parse_up8(__m512i x, uint32_t k, __m512i fill)
{
    return _mm512_permutex2var_epi8(x, parse_shifts.up8[k], fill);
}

BUSTER_INLINE __m512i parse_down8(__m512i x, uint32_t k, __m512i fill)
{
    return _mm512_permutex2var_epi8(x, parse_shifts.down8[k], fill);
}

// The nearest previous lane whose key is below the lane's threshold, in byte lanes (keys and thresholds unsigned): the
// lane index, or a negative byte (bit 7 set) if no earlier lane in the vector qualifies. The windows of the doubling
// table are built with shifts and vpminub, and searched with in-register permutes: 32, 16, 8, 4, 2 and 1 lanes back.
BUSTER_INLINE __m512i parse_previous_smaller8(__m512i keys, __m512i thresholds)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    const __m512i one = _mm512_set1_epi8(1);
    __m512i windows[6];
    windows[0] = keys;
    for (uint32_t k = 1; k < 6; k += 1)
    {
        windows[k] = _mm512_min_epu8(windows[k - 1], parse_up8(windows[k - 1], k - 1, fill));
    }
    __m512i p = _mm512_sub_epi8(parse_shifts.lanes8, one);
    for (int32_t k = 5; k >= 0; k -= 1)
    {
        const __m512i v = _mm512_permutexvar_epi8(p, windows[k]);
        // 0 where every key in the window is at or above the threshold, so the search moves past it.
        const __m512i below = _mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one);
        const __m512i step = _mm512_set1_epi8((char)(1 << k));
        p = _mm512_sub_epi8(p, _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------
// K1 in AVX-512

// The inclusive prefix sum of signed bytes, in six shifted adds.
BUSTER_INLINE __m512i parse_prefix_sum8(__m512i x)
{
    const __m512i zero = _mm512_setzero_si512();
    for (uint32_t k = 0; k < 6; k += 1)
    {
        x = _mm512_add_epi8(x, parse_up8(x, k, zero));
    }
    return x;
}


// One block of K1's twin, for a block whose enclosing openers reach further back than the vector path's 16 depths. Out
// of line, so that this rare path leaves the block loop's code alone. Returns false if a closer has no opener.
__attribute__((noinline)) static bool parse_brackets_block_scalar(const uint8_t* codes, const int8_t* deltas, uint32_t begin, uint32_t end, uint32_t* depths, uint32_t* enclosing,
                                        uint32_t* open_stack, int64_t* depth)
{
    bool balanced = true;
    for (uint32_t i = begin; i < end; i += 1)
    {
        const int8_t delta = deltas[codes[i]];
        depths[i] = (uint32_t)*depth;
        enclosing[i] = *depth > 0 ? open_stack[*depth - 1] : BUSTER_PARSE_NONE;
        if (delta > 0)
        {
            open_stack[*depth] = i;
        }
        *depth += delta;
        balanced &= *depth >= 0;
        *depth = *depth < 0 ? 0 : *depth;
    }
    return balanced;
}

// K1 in AVX-512, 64 tokens a block in byte lanes. The depth is a prefix sum of the deltas; a token's enclosing opener is
// the nearest previous token whose depth before it is lower, found in the block with parse_previous_smaller8 and before
// it in open_stack (the open opener at each depth), of which the 16 entries below the block's starting depth are one
// vpermd away. `open_stack` needs an entry per depth the input reaches, and 16 of BUSTER_PARSE_NONE below depth 0. Each
// block's view goes to `blocks` (ParseBracketBlock), for K2. Returns false if a closer has no opener or a group is still
// open at the end.
static bool parse_brackets_avx512(const uint8_t* codes, const int8_t* deltas, uint32_t count, uint32_t* depths, uint32_t* enclosing, uint32_t* open_stack,
                                  ParseBracketBlock* blocks)
{
    const __m512i delta_low = _mm512_loadu_si512(deltas);
    const __m512i delta_high = _mm512_loadu_si512(deltas + 64);
    const __m512i bias = _mm512_set1_epi8(64);
    const __m512i fill = _mm512_set1_epi8(0x7F);
    const __m512i none = _mm512_set1_epi32((int)BUSTER_PARSE_NONE);
    int64_t base = 0;
    bool balanced = true;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i code = _mm512_loadu_si512(codes + block);
        const __m512i delta = _mm512_permutex2var_epi8(delta_low, code, delta_high);
        const __m512i after = parse_prefix_sum8(delta);
        const __m512i before = _mm512_sub_epi8(after, delta);
        const __m512i keys = _mm512_add_epi8(before, bias);
        const __m512i p = parse_previous_smaller8(keys, keys);
        const __mmask64 found = _mm512_cmplt_epu8_mask(p, _mm512_set1_epi8(64));
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        // The block's view for K2, which needs nothing from before the block: a lane whose opener isn't in the block has
        // it before the block, and the openers still open at its end are those that no token from them on in the block
        // leaves at a lower depth than theirs.
        __m512i suffix = after;
        for (uint32_t k = 0; k < 6; k += 1)
        {
            suffix = _mm512_min_epi8(suffix, parse_down8(suffix, k, fill));
        }
        const __mmask64 openers = _mm512_cmpgt_epi8_mask(delta, _mm512_setzero_si512()) & live_mask;
        uint64_t open = openers & _mm512_cmpeq_epi8_mask(suffix, after);
        ParseBracketBlock* view = &blocks[block / 64];
        _mm512_store_si512(view->opener_lanes, _mm512_mask_mov_epi8(_mm512_set1_epi8((char)0xFF), found & live_mask, p));
        _mm512_store_si512(view->relative_depths, _mm512_maskz_mov_epi8(live_mask, before));
        view->open = open;
        // Tokens whose opener is before the block are at or below its starting depth; at most 15 depths below it the
        // vector path handles them.
        const __mmask64 deep = _mm512_mask_cmplt_epi8_mask(~found & live_mask, before, _mm512_set1_epi8(-15));
        if (deep != 0)
        {
            balanced &= parse_brackets_block_scalar(codes, deltas, block, block + live, depths, enclosing, open_stack, &base);
        }
        else
        {
            // The open openers at the 16 depths below the block's start (open_stack has BUSTER_PARSE_DEPTH_PADDING entries
            // of no opener below depth 0).
            const __m512i below_vector = _mm512_loadu_si512(open_stack + base - 16);
            const __m512i base_vector = _mm512_set1_epi32((int)base);
            const __m512i block_vector = _mm512_set1_epi32((int)block);
            for (uint32_t q = 0; q < 4; q += 1)
            {
                const __m128i p_quarter = q == 0 ? _mm512_extracti32x4_epi32(p, 0) : q == 1 ? _mm512_extracti32x4_epi32(p, 1) :
                                          q == 2 ? _mm512_extracti32x4_epi32(p, 2) : _mm512_extracti32x4_epi32(p, 3);
                const __m128i before_quarter = q == 0 ? _mm512_extracti32x4_epi32(before, 0) : q == 1 ? _mm512_extracti32x4_epi32(before, 1) :
                                               q == 2 ? _mm512_extracti32x4_epi32(before, 2) : _mm512_extracti32x4_epi32(before, 3);
                const __m512i relative = _mm512_cvtepi8_epi32(before_quarter);
                const __m512i inside = _mm512_add_epi32(block_vector, _mm512_cvtepu8_epi32(p_quarter));
                const __m512i outside = _mm512_permutexvar_epi32(_mm512_add_epi32(relative, _mm512_set1_epi32(15)), below_vector);
                const __mmask16 found_quarter = (__mmask16)(found >> (16 * q));
                _mm512_storeu_si512(enclosing + block + 16 * q, _mm512_mask_blend_epi32(found_quarter, outside, inside));
                _mm512_storeu_si512(depths + block + 16 * q, _mm512_add_epi32(base_vector, relative));
            }
            (void)none;
            // The openers still open at the block's end, onto open_stack.
            if (open != 0)
            {
                int8_t before_bytes[64];
                _mm512_storeu_si512(before_bytes, before);
                while (open != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctzll(open);
                    open &= open - 1;
                    open_stack[base + before_bytes[lane]] = block + lane;
                }
            }
            int8_t after_bytes[64];
            _mm512_storeu_si512(after_bytes, after);
            const __mmask64 negative = _mm512_cmplt_epi8_mask(_mm512_add_epi8(after, _mm512_set1_epi8((char)(base < 64 ? base : 64))), _mm512_setzero_si512()) & live_mask;
            balanced &= base >= 64 || negative == 0;
            base += after_bytes[live - 1];
            base = base < 0 ? 0 : base;
        }
    }
    return balanced && base == 0;
}

static BUSTER_BAKED_FRONTEND ParseParentTables parse_parent_tables;

static void parse_parent_tables_init(void)
{
    for (uint32_t role = 0; role < PARSE_ROLE_COUNT; role += 1)
    {
        parse_parent_tables.levels[role] = parse_role_levels[role];
        parse_parent_tables.closers[role] = parse_role_deltas[role] < 0;
        parse_parent_tables.left_thresholds[role] = parse_role_left_thresholds[role];
        parse_parent_tables.right_thresholds[role] = parse_role_right_thresholds[role];
        parse_parent_tables.rules[role] = parse_role_rules[role];
    }
    for (uint32_t level = 0; level < 32; level += 1)
    {
        parse_parent_tables.weak[level] = parse_level_is_weak((uint8_t)level);
        parse_parent_tables.ties[level] = parse_level_ties_left[level];
        parse_parent_tables.weak_ties[level] = (uint16_t)(parse_level_is_weak((uint8_t)level) | (parse_level_ties_left[level] << 1));
    }
}

BUSTER_INLINE __m512i parse_lookup8(const uint8_t* table, __m512i index)
{
    return _mm512_permutex2var_epi8(_mm512_loadu_si512(table), index, _mm512_loadu_si512(table + 64));
}

static BUSTER_BAKED_FRONTEND ParseRoleTables parse_role_tables;

static void parse_role_tables_init(void)
{
    ParseRoleTables* t = &parse_role_tables;
    memset(t, 0, sizeof(*t));
    for (uint32_t k = 0; k < 128; k += 1)
    {
        t->fixed[k] = PARSE_ROLE_ERROR;
        t->fixed_flags[k] = 1;
        t->classes[k] = PARSE_RULE_CLASS_FIXED;
        const uint8_t binary = k < BUSTER_PARSE_VKINDS ? parse_binary_role((uint8_t)k) : PARSE_ROLE_NONE;
        if (binary != PARSE_ROLE_NONE)
        {
            t->fixed[k] = binary;
            t->fixed_flags[k] = 0;
        }
        const uint8_t vk = (uint8_t)k;
        t->vkind_bits[k] = (uint8_t)((k < BUSTER_PARSE_VKINDS && parse_vkind_starts_type(vk) ? BUSTER_PARSE_VKIND_STARTS_TYPE : 0) |
                                     (k < BUSTER_PARSE_VKINDS && parse_vkind_starts_declaration(vk) ? BUSTER_PARSE_VKIND_STARTS_DECLARATION : 0) |
                                     (k == PARSE_KIND_TYPE || k == PARSE_VKIND_TYPE_NAME ? BUSTER_PARSE_VKIND_TYPE_WORD : 0) |
                                     (k < BUSTER_PARSE_VKINDS && parse_vkind_starts_operand(vk) ? BUSTER_PARSE_VKIND_STARTS_OPERAND : 0) |
                                     (k == PARSE_KIND_IDENTIFIER || k == PARSE_VKIND_TYPE_NAME ? BUSTER_PARSE_VKIND_NAME : 0) |
                                     ((k >= PARSE_KIND_TYPE && k < BUSTER_PARSE_VKINDS) || k == PARSE_KIND_IDENTIFIER ? BUSTER_PARSE_VKIND_KEYWORD : 0) |
                                     (k == PARSE_KIND_QUALIFIER || k == PARSE_KIND_STORAGE || k == PARSE_KIND_GOTO ? BUSTER_PARSE_VKIND_ASM_QUALIFIER : 0));
        t->cast_closers[k] = k == PARSE_KIND_L_BRACE ? PARSE_ROLE_CLOSE_COMPOUND : k < BUSTER_PARSE_VKINDS && parse_vkind_starts_operand(vk) ? PARSE_ROLE_CLOSE_CAST :
                                                                                                                                   PARSE_ROLE_CLOSE_FUNCTION;
    }
    static const uint8_t fixed_kinds[][2] = {
        { PARSE_KIND_NUMBER, PARSE_ROLE_OPERAND },
        { PARSE_KIND_CHARACTER, PARSE_ROLE_OPERAND },
        { PARSE_KIND_TYPE, PARSE_ROLE_SPECIFIER },
        { PARSE_KIND_TYPEDEF, PARSE_ROLE_SPECIFIER },
        { PARSE_KIND_LABEL, PARSE_ROLE_SPECIFIER },
        { PARSE_KIND_STRUCT, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_ENUM, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_TYPEOF, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_ALIGNAS, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_ATTRIBUTE, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_ASM, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_TYPE_ARGUMENT, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_STATIC_ASSERT, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_IF, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_WHILE, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_SWITCH, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_FOR, PARSE_ROLE_LEAF_RIGHT },
        { PARSE_KIND_SIZEOF, PARSE_ROLE_PREFIX },
        { PARSE_KIND_REAL_IMAG, PARSE_ROLE_PREFIX },
        { PARSE_KIND_EXCLAIM, PARSE_ROLE_PREFIX },
        { PARSE_KIND_TILDE, PARSE_ROLE_PREFIX },
        { PARSE_KIND_ELSE, PARSE_ROLE_ELSE },
        { PARSE_KIND_DO, PARSE_ROLE_DO },
        { PARSE_KIND_CASE, PARSE_ROLE_OPEN_CASE },
        { PARSE_KIND_DEFAULT, PARSE_ROLE_OPEN_CASE },
        { PARSE_KIND_RETURN, PARSE_ROLE_RETURN },
        { PARSE_KIND_BREAK, PARSE_ROLE_RETURN },
        { PARSE_KIND_CONTINUE, PARSE_ROLE_RETURN },
        { PARSE_KIND_SEMI, PARSE_ROLE_TERMINATOR },
        { PARSE_KIND_QUESTION, PARSE_ROLE_OPEN_TERNARY },
        { PARSE_KIND_PERIOD, PARSE_ROLE_MEMBER_OPERATOR },
        { PARSE_KIND_ARROW, PARSE_ROLE_MEMBER_OPERATOR },
        { PARSE_KIND_L_SQUARE, PARSE_ROLE_OPEN_SUBSCRIPT },
    };
    for (uint32_t i = 0; i < sizeof(fixed_kinds) / sizeof(fixed_kinds[0]); i += 1)
    {
        t->fixed[fixed_kinds[i][0]] = fixed_kinds[i][1];
        t->fixed_flags[fixed_kinds[i][0]] = 0;
    }
    // _Generic is the scalar parser's.
    t->fixed[PARSE_KIND_GENERIC] = PARSE_ROLE_LEAF_RIGHT;
    t->fixed_flags[PARSE_KIND_GENERIC] = 1;
    for (uint32_t k = 0; k < 128; k += 1)
    {
        t->guesses[k] = t->fixed[k];
    }
    static const uint8_t guesses[][2] = {
        { PARSE_KIND_IDENTIFIER, PARSE_ROLE_OPERAND },
        { PARSE_KIND_STRING, PARSE_ROLE_OPERAND },
        { PARSE_KIND_R_SQUARE, PARSE_ROLE_CLOSE_SUBSCRIPT },
        { PARSE_KIND_L_PAREN, PARSE_ROLE_OPEN_CALL },
        { PARSE_KIND_R_PAREN, PARSE_ROLE_CLOSE_CALL },
        { PARSE_KIND_L_BRACE, PARSE_ROLE_OPEN_BLOCK },
        { PARSE_KIND_R_BRACE, PARSE_ROLE_CLOSE_BLOCK },
        { PARSE_KIND_ELLIPSIS, PARSE_ROLE_OPERAND },
        { PARSE_KIND_AMP, PARSE_ROLE_BITWISE_AND },
        { PARSE_KIND_AMPAMP, PARSE_ROLE_LOGICAL_AND },
        { PARSE_KIND_STAR, PARSE_ROLE_PREFIX },
        { PARSE_KIND_PLUS, PARSE_ROLE_ADDITIVE },
        { PARSE_KIND_PLUSPLUS, PARSE_ROLE_POSTFIX },
        { PARSE_KIND_MINUS, PARSE_ROLE_ADDITIVE },
        { PARSE_KIND_MINUSMINUS, PARSE_ROLE_POSTFIX },
        { PARSE_KIND_COLON, PARSE_ROLE_CLOSE_TERNARY },
        { PARSE_KIND_COMMA, PARSE_ROLE_SEPARATOR },
        { PARSE_KIND_QUALIFIER, PARSE_ROLE_SPECIFIER },
        { PARSE_KIND_CALLING_CONVENTION, PARSE_ROLE_SPECIFIER },
        { PARSE_KIND_STORAGE, PARSE_ROLE_SPECIFIER },
        { PARSE_KIND_GOTO, PARSE_ROLE_RETURN },
        { PARSE_KIND_ATOMIC, PARSE_ROLE_SPECIFIER },
        { PARSE_KIND_EXTENSION, PARSE_ROLE_SPECIFIER },
        { PARSE_VKIND_TYPE_NAME, PARSE_ROLE_SPECIFIER },
    };
    for (uint32_t i = 0; i < sizeof(guesses) / sizeof(guesses[0]); i += 1)
    {
        t->guesses[guesses[i][0]] = guesses[i][1];
    }
    static const uint8_t class_kinds[][2] = {
        { PARSE_KIND_STRING, PARSE_RULE_CLASS_STRING },
        { PARSE_KIND_IDENTIFIER, PARSE_RULE_CLASS_IDENTIFIER },
        { PARSE_VKIND_TYPE_NAME, PARSE_RULE_CLASS_TYPE_NAME },
        { PARSE_KIND_QUALIFIER, PARSE_RULE_CLASS_QUALIFIER },
        { PARSE_KIND_CALLING_CONVENTION, PARSE_RULE_CLASS_QUALIFIER },
        { PARSE_KIND_STORAGE, PARSE_RULE_CLASS_STORAGE },
        { PARSE_KIND_GOTO, PARSE_RULE_CLASS_GOTO },
        { PARSE_KIND_ATOMIC, PARSE_RULE_CLASS_ATOMIC },
        { PARSE_KIND_ATTRIBUTE, PARSE_RULE_CLASS_ATTRIBUTE },
        { PARSE_KIND_EXTENSION, PARSE_RULE_CLASS_EXTENSION },
        { PARSE_KIND_COMMA, PARSE_RULE_CLASS_COMMA },
        { PARSE_KIND_COLON, PARSE_RULE_CLASS_COLON },
        { PARSE_KIND_ELLIPSIS, PARSE_RULE_CLASS_ELLIPSIS },
        { PARSE_KIND_STAR, PARSE_RULE_CLASS_UNARY },
        { PARSE_KIND_AMP, PARSE_RULE_CLASS_UNARY },
        { PARSE_KIND_PLUS, PARSE_RULE_CLASS_UNARY },
        { PARSE_KIND_MINUS, PARSE_RULE_CLASS_UNARY },
        { PARSE_KIND_AMPAMP, PARSE_RULE_CLASS_UNARY },
        { PARSE_KIND_PLUSPLUS, PARSE_RULE_CLASS_UNARY },
        { PARSE_KIND_MINUSMINUS, PARSE_RULE_CLASS_UNARY },
        { PARSE_KIND_L_PAREN, PARSE_RULE_CLASS_PAREN },
        { PARSE_KIND_L_BRACE, PARSE_RULE_CLASS_BRACE },
        { PARSE_KIND_R_PAREN, PARSE_RULE_CLASS_CLOSER },
        { PARSE_KIND_R_SQUARE, PARSE_RULE_CLASS_CLOSER },
        { PARSE_KIND_R_BRACE, PARSE_RULE_CLASS_CLOSER },
    };
    for (uint32_t i = 0; i < sizeof(class_kinds) / sizeof(class_kinds[0]); i += 1)
    {
        t->classes[class_kinds[i][0]] = class_kinds[i][1];
        t->fixed_flags[class_kinds[i][0]] = 0;
    }
    t->binary[PARSE_KIND_STAR] = PARSE_ROLE_MULTIPLICATIVE;
    t->binary[PARSE_KIND_AMP] = PARSE_ROLE_BITWISE_AND;
    t->binary[PARSE_KIND_PLUS] = PARSE_ROLE_ADDITIVE;
    t->binary[PARSE_KIND_MINUS] = PARSE_ROLE_ADDITIVE;
    t->binary[PARSE_KIND_AMPAMP] = PARSE_ROLE_LOGICAL_AND;
    t->binary[PARSE_KIND_PLUSPLUS] = PARSE_ROLE_POSTFIX;
    t->binary[PARSE_KIND_MINUSMINUS] = PARSE_ROLE_POSTFIX;
    t->paren_previous[PARSE_KIND_IF] = PARSE_ROLE_OPEN_CONDITION;
    t->paren_previous[PARSE_KIND_WHILE] = PARSE_ROLE_OPEN_CONDITION;
    t->paren_previous[PARSE_KIND_SWITCH] = PARSE_ROLE_OPEN_CONDITION;
    t->paren_previous[PARSE_KIND_FOR] = PARSE_ROLE_OPEN_FOR;
    t->paren_previous[PARSE_KIND_SIZEOF] = PARSE_ROLE_OPEN_SIZEOF;
    t->paren_previous[PARSE_KIND_TYPEOF] = PARSE_ROLE_OPEN_SPECIFIER;
    t->paren_previous[PARSE_KIND_ALIGNAS] = PARSE_ROLE_OPEN_SPECIFIER;
    t->paren_previous[PARSE_KIND_ATOMIC] = PARSE_ROLE_OPEN_SPECIFIER;
    t->paren_previous[PARSE_KIND_ATTRIBUTE] = BUSTER_PARSE_PAREN_ATTRIBUTE;
    t->paren_previous[PARSE_KIND_ASM] = BUSTER_PARSE_PAREN_ASM;
    t->paren_previous[PARSE_KIND_TYPE_ARGUMENT] = PARSE_ROLE_OPEN_KEYWORD;
    t->paren_previous[PARSE_KIND_GENERIC] = PARSE_ROLE_OPEN_KEYWORD;
    t->paren_previous[PARSE_KIND_STATIC_ASSERT] = PARSE_ROLE_OPEN_KEYWORD;
    for (uint32_t r = 0; r < 128; r += 1)
    {
        const uint8_t role = r < PARSE_ROLE_COUNT ? (uint8_t)r : PARSE_ROLE_NONE;
        const bool comma = role == PARSE_ROLE_NONE || role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_STRUCT ||
                           role == PARSE_ROLE_OPEN_FOR;
        t->closers[r] = parse_closer_roles[role] != PARSE_ROLE_NONE ? parse_closer_roles[role] : PARSE_ROLE_ERROR;
        t->group_rules[r] = (uint8_t)((comma ? BUSTER_PARSE_GROUP_RULE_COMMA : 0) | (role == PARSE_ROLE_OPEN_ASM ? BUSTER_PARSE_GROUP_RULE_ASM_COLON : 0) |
                                      (role == PARSE_ROLE_OPEN_STRUCT ? BUSTER_PARSE_GROUP_RULE_BITFIELD : 0) |
                                      (role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_CAST ? BUSTER_PARSE_GROUP_RULE_PARAMETERS : 0) |
                                      (role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST ? BUSTER_PARSE_GROUP_RULE_NAMES : 0) |
                                      (role == PARSE_ROLE_OPEN_CAST ? BUSTER_PARSE_GROUP_RULE_CAST_CLOSER : 0) |
                                      (role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY ? BUSTER_PARSE_GROUP_RULE_BLOCK : 0));
    }
    for (uint32_t r = 0; r < 128; r += 1)
    {
        const uint8_t role = r < PARSE_ROLE_COUNT ? (uint8_t)r : PARSE_ROLE_NONE;
        const uint8_t named = role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT ? BUSTER_PARSE_FEATURE_ATTRIBUTE_RIGHT :
                              role == PARSE_ROLE_LEAF_RIGHT ? BUSTER_PARSE_FEATURE_LEAF_RIGHT :
                              role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR ? BUSTER_PARSE_FEATURE_HEAD_CLOSER :
                              role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_FUNCTION ? BUSTER_PARSE_FEATURE_CALL_CLOSER :
                              role == PARSE_ROLE_CLOSE_COMPOUND ? BUSTER_PARSE_FEATURE_COMPOUND_CLOSER :
                              role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY ? BUSTER_PARSE_FEATURE_BLOCK :
                              role == PARSE_ROLE_OPEN_INITIALIZER || role == PARSE_ROLE_OPEN_COMPOUND_BODY ? BUSTER_PARSE_FEATURE_INITIALIZER : 0;
        t->features[r] = (uint8_t)(parse_role_preds[role] | named);
    }
    // The features of the roles the rules name are theirs alone.
    assert(t->features[PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT] == BUSTER_PARSE_FEATURES_ATTRIBUTE_RIGHT && t->features[PARSE_ROLE_LEAF_RIGHT] == BUSTER_PARSE_FEATURES_LEAF_RIGHT);
    assert(t->features[PARSE_ROLE_CLOSE_CONDITION] == BUSTER_PARSE_FEATURES_HEAD_CLOSER && t->features[PARSE_ROLE_CLOSE_FOR] == BUSTER_PARSE_FEATURES_HEAD_CLOSER);
    assert(t->features[PARSE_ROLE_CLOSE_CALL] == BUSTER_PARSE_FEATURES_CALL_CLOSER && t->features[PARSE_ROLE_CLOSE_FUNCTION] == BUSTER_PARSE_FEATURES_CALL_CLOSER);
    assert(t->features[PARSE_ROLE_CLOSE_COMPOUND] == BUSTER_PARSE_FEATURES_COMPOUND_CLOSER);
    assert(t->features[PARSE_ROLE_OPEN_BLOCK] == BUSTER_PARSE_FEATURES_BLOCK && t->features[PARSE_ROLE_OPEN_BODY] == BUSTER_PARSE_FEATURES_BLOCK);
    assert(t->features[PARSE_ROLE_OPEN_INITIALIZER] == BUSTER_PARSE_FEATURES_INITIALIZER && t->features[PARSE_ROLE_OPEN_COMPOUND_BODY] == BUSTER_PARSE_FEATURES_INITIALIZER);
    _Static_assert((PARSE_ROLE_SPECIFIER & 15) != (PARSE_ROLE_PREFIX & 15), "the fixed features are by a role's low nibble");
    t->fixed_features[PARSE_ROLE_SPECIFIER & 15] = PARSE_PRED_TYPE;
    t->fixed_features[PARSE_ROLE_PREFIX & 15] = PARSE_PRED_STAR;
    for (uint32_t k = 0; k < 128; k += 1)
    {
        const uint8_t c = t->classes[k];
        const bool word = (t->vkind_bits[k] & BUSTER_PARSE_VKIND_TYPE_WORD) != 0;
        t->fix_roles[k] = word ? PARSE_ROLE_SPECIFIER : k == PARSE_KIND_STAR ? PARSE_ROLE_PREFIX : 0xFF;
        t->guess_features[k] = t->guesses[k] == t->fix_roles[k] ? t->fixed_features[t->guesses[k] & 15] : t->features[t->guesses[k]];
        const bool reads_previous = c == PARSE_RULE_CLASS_TYPE_NAME || c == PARSE_RULE_CLASS_QUALIFIER || c == PARSE_RULE_CLASS_STORAGE || c == PARSE_RULE_CLASS_GOTO ||
                                    c == PARSE_RULE_CLASS_ATOMIC || c == PARSE_RULE_CLASS_ATTRIBUTE || c == PARSE_RULE_CLASS_UNARY || c == PARSE_RULE_CLASS_PAREN ||
                                    c == PARSE_RULE_CLASS_BRACE;
        t->self_bits[k] = (uint8_t)((reads_previous ? BUSTER_PARSE_SELF_READS_PREVIOUS : 0) | (c == PARSE_RULE_CLASS_IDENTIFIER ? BUSTER_PARSE_SELF_IDENTIFIER : 0) |
                                    (c == PARSE_RULE_CLASS_PAREN ? BUSTER_PARSE_SELF_PAREN : 0) | (c == PARSE_RULE_CLASS_TYPE_NAME ? BUSTER_PARSE_SELF_TYPE_NAME : 0) |
                                    (c == PARSE_RULE_CLASS_BRACE ? BUSTER_PARSE_SELF_BRACE : 0) | (c == PARSE_RULE_CLASS_CLOSER ? BUSTER_PARSE_SELF_CLOSER : 0) |
                                    ((t->vkind_bits[k] & BUSTER_PARSE_VKIND_KEYWORD) != 0 ? BUSTER_PARSE_SELF_KEYWORD : 0) |
                                    (t->fixed_flags[k] != 0 ? BUSTER_PARSE_SELF_FLAGGED : 0));
        t->codes[k] = c == PARSE_RULE_CLASS_COMMA ? BUSTER_PARSE_CODE_COMMA : c == PARSE_RULE_CLASS_COLON ? BUSTER_PARSE_CODE_COLON :
                      c == PARSE_RULE_CLASS_ELLIPSIS ? BUSTER_PARSE_CODE_ELLIPSIS : 0;
        t->brace_previous[k] = (uint8_t)(((t->vkind_bits[k] & BUSTER_PARSE_VKIND_NAME) != 0 ? BUSTER_PARSE_BRACE_AFTER_NAME : 0) |
                                         (k == PARSE_KIND_L_BRACE || k == PARSE_KIND_COMMA ? BUSTER_PARSE_BRACE_NESTED : 0));
    }
    for (uint32_t f = 0; f < 128; f += 1)
    {
        const uint32_t pred = f & 15;
        t->dynamic[f] = (uint8_t)((pred == PARSE_PRED_STAR ? BUSTER_PARSE_RULE_STAR : 0) | (f == BUSTER_PARSE_FEATURES_LEAF_RIGHT ? BUSTER_PARSE_RULE_LEAF_RIGHT : 0) |
                                  (pred == PARSE_PRED_OPERAND ? BUSTER_PARSE_RULE_OPERAND : 0) |
                                  (f == BUSTER_PARSE_FEATURES_ATTRIBUTE_RIGHT ? BUSTER_PARSE_RULE_ATTRIBUTE_RIGHT : 0) |
                                  (pred == PARSE_PRED_ITEM || pred == PARSE_PRED_HEAD ? BUSTER_PARSE_RULE_ITEM_OR_HEAD : 0) |
                                  (pred == PARSE_PRED_ITEM || pred == PARSE_PRED_PARAMETER ? BUSTER_PARSE_RULE_ITEM_OR_PARAMETER : 0) |
                                  (pred == PARSE_PRED_SPECIFIER ? BUSTER_PARSE_RULE_SPECIFIER : 0) |
                                  (pred == PARSE_PRED_TYPE || pred == PARSE_PRED_SPECIFIER || pred == PARSE_PRED_STAR ? BUSTER_PARSE_RULE_FUNCTION : 0));
        t->brace_codes[f] = f == BUSTER_PARSE_FEATURES_ATTRIBUTE_RIGHT ? BUSTER_PARSE_BRACE_CODE_ATTRIBUTE_RIGHT :
                            f == BUSTER_PARSE_FEATURES_LEAF_RIGHT ? BUSTER_PARSE_BRACE_CODE_LEAF_RIGHT :
                            f == BUSTER_PARSE_FEATURES_HEAD_CLOSER ? BUSTER_PARSE_BRACE_CODE_HEAD :
                            f == BUSTER_PARSE_FEATURES_CALL_CLOSER ? BUSTER_PARSE_BRACE_CODE_CALL :
                            f == BUSTER_PARSE_FEATURES_COMPOUND_CLOSER ? BUSTER_PARSE_BRACE_CODE_COMPOUND :
                            pred == PARSE_PRED_ITEM || pred == PARSE_PRED_HEAD ? BUSTER_PARSE_BRACE_CODE_ITEM : BUSTER_PARSE_BRACE_CODE_OTHER;
    }
    // A `{` by the token before, from the highest priority down: a tagged type's after its attributes or its name; a
    // statement's after a head's `)`; a function's; a compound literal's body; a nested initializer's after `{` or `,`
    // in one; else a block's, flagged if no statement may start there.
    for (uint32_t i = 0; i < 128; i += 1)
    {
        const uint32_t code = i & 7;
        const bool tag = code == BUSTER_PARSE_BRACE_CODE_ATTRIBUTE_RIGHT || (code == BUSTER_PARSE_BRACE_CODE_LEAF_RIGHT && (i & BUSTER_PARSE_BRACE_AFTER_NAME) != 0);
        const bool nested = (i & BUSTER_PARSE_BRACE_NESTED) != 0;
        const uint8_t role = tag ? ((i & BUSTER_PARSE_BRACE_TAG_ENUM) != 0 ? PARSE_ROLE_OPEN_ENUM : PARSE_ROLE_OPEN_STRUCT) :
                             code == BUSTER_PARSE_BRACE_CODE_HEAD ? PARSE_ROLE_OPEN_BLOCK : code == BUSTER_PARSE_BRACE_CODE_CALL ? PARSE_ROLE_OPEN_BODY :
                             code == BUSTER_PARSE_BRACE_CODE_COMPOUND ? PARSE_ROLE_OPEN_COMPOUND_BODY : nested ? PARSE_ROLE_OPEN_INITIALIZER : PARSE_ROLE_OPEN_BLOCK;
        const bool placed = code == BUSTER_PARSE_BRACE_CODE_HEAD || code == BUSTER_PARSE_BRACE_CODE_CALL || code == BUSTER_PARSE_BRACE_CODE_COMPOUND || nested ||
                            code == BUSTER_PARSE_BRACE_CODE_ITEM;
        const bool flag = tag ? (i & BUSTER_PARSE_BRACE_TAG_NONE) != 0 : !placed;
        t->brace_rules[i] = (uint8_t)(role | (flag ? 0x80 : 0));
    }
    t->nested_groups[BUSTER_PARSE_FEATURES_INITIALIZER & 63] = BUSTER_PARSE_BRACE_NESTED;
    for (uint32_t i = 0; i < 64; i += 1)
    {
        const uint32_t nibble = i & 15;
        const uint32_t code = (i & 0x30) | 0x80;
        t->separators[i] = code == BUSTER_PARSE_CODE_COMMA ? ((nibble & BUSTER_PARSE_GROUP_RULE_COMMA) != 0 ? PARSE_ROLE_COMMA : PARSE_ROLE_SEPARATOR) :
                           code == BUSTER_PARSE_CODE_COLON ? ((nibble & BUSTER_PARSE_GROUP_RULE_ASM_COLON) != 0 ? PARSE_ROLE_ASM_COLON :
                                                              (nibble & BUSTER_PARSE_GROUP_RULE_BITFIELD) != 0 ? PARSE_ROLE_BITFIELD : PARSE_ROLE_CLOSE_TERNARY) :
                           code == BUSTER_PARSE_CODE_ELLIPSIS ? ((nibble & BUSTER_PARSE_GROUP_RULE_PARAMETERS) != 0 ? PARSE_ROLE_OPERAND : PARSE_ROLE_RANGE) : PARSE_ROLE_ERROR;
        const uint32_t paren = (i & 0x30) | 0x40;
        t->paren_by_before[i] = paren == BUSTER_PARSE_CODE_ATTRIBUTE ? (nibble == PARSE_PRED_OPERAND ? PARSE_ROLE_OPEN_ATTRIBUTE : nibble == PARSE_PRED_STAR ? PARSE_ROLE_OPEN_ATTRIBUTE_LEFT :
                                                                        nibble == PARSE_PRED_LEAF ? PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT : PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER) :
                                paren == BUSTER_PARSE_CODE_ASM ? (nibble == PARSE_PRED_ITEM || nibble == PARSE_PRED_HEAD ? PARSE_ROLE_OPEN_ASM : PARSE_ROLE_OPEN_ATTRIBUTE) : PARSE_ROLE_ERROR;
    }
}

static ParseGroupWindows parse_group_windows_load(const ParseRoleCarry* carry, uint32_t depth)
{
    const ParseGroupWindows result = {
        .roles = _mm512_loadu_si512(carry->group_roles + (int64_t)(int32_t)depth - 64),
        .keywords = _mm512_loadu_si512(carry->group_keywords + (int64_t)(int32_t)depth - 64),
    };
    return result;
}

BUSTER_INLINE __m512i parse_table8(const uint8_t* table, __m512i index)
{
    return _mm512_permutex2var_epi8(_mm512_loadu_si512(table), index, _mm512_loadu_si512(table + 64));
}

BUSTER_INLINE __mmask64 parse_equal8(__m512i x, uint8_t value)
{
    return _mm512_cmpeq_epi8_mask(x, _mm512_set1_epi8((char)value));
}

// What a round reads of each lane's role: its features, with the pred class fixed by the lane's kind, as parse_pred_of
// fixes it: a type keyword's or typedef name's SPECIFIER is a TYPE, `*`'s PREFIX a STAR (`fix_role` has the role each
// lane's kind fixes). Two role vectors with the same features (and typed states) make a round compute the same roles.
BUSTER_INLINE __m512i parse_features8(__m512i role, __m512i fix_role)
{
    const ParseRoleTables* t = &parse_role_tables;
    const __m512i fixed = _mm512_shuffle_epi8(_mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i*)t->fixed_features)), fix_role);
    return _mm512_mask_mov_epi8(parse_table8(t->features, role), _mm512_cmpeq_epi8_mask(role, fix_role), fixed);
}

// The features of one token's role and kind, as parse_features8 gives them.
static uint8_t parse_role_features(uint8_t role, uint8_t vkind)
{
    const ParseRoleTables* t = &parse_role_tables;
    return role == t->fix_roles[vkind] ? t->fixed_features[role & 15] : t->features[role];
}

// The lanes whose role sets the typed state (a type specifier: features of pred TYPE and no named role), and those
// that carry it (another specifier or LEAF_RIGHT; with an attribute's specifier closer, which the caller leaves out).
BUSTER_INLINE uint64_t parse_typed_generate8(__m512i features)
{
    return parse_equal8(features, PARSE_PRED_TYPE);
}

BUSTER_INLINE uint64_t parse_typed_propagate8(__m512i features)
{
    return parse_equal8(features, PARSE_PRED_SPECIFIER) | parse_equal8(features, BUSTER_PARSE_FEATURES_LEAF_RIGHT);
}

// The typed state of 64 lanes from their generate, propagate and carry-in bits: t[i] = g[i] | (p[i] & t[i - 1]), as an
// add's carries.
BUSTER_INLINE uint64_t parse_typed_carries(uint64_t generate, uint64_t propagate, uint64_t carry_in, uint64_t* carry_out)
{
    const uint64_t x = generate | propagate;
    uint64_t sum;
    uint64_t sum_carry = __builtin_add_overflow(x, generate, &sum);
    uint64_t total;
    sum_carry |= __builtin_add_overflow(sum, carry_in, &total);
    // The carry into each bit, then the carry out of it.
    const uint64_t into = total ^ x ^ generate;
    *carry_out = sum_carry;
    return (into >> 1) | (sum_carry << 63);
}

// The statics of the blocks from `start` to `end`, in a pass of their own before their rounds: nothing in them depends
// on a block before, so the blocks' work overlaps, and the rounds' loop keeps what it computes in registers. With the
// statics in the same loop, about a dozen masks a block went to the stack and back, and K2 took 0.55 cycles a token more
// on sqlite3.c and 0.7 more on the libc corpus.
static void parse_role_statics_avx512(ParseKernels* kernels, uint32_t start, uint32_t end)
{
    const ParseRoleTables* t = &parse_role_tables;
    const uint8_t* vkinds = kernels->vkinds;
    for (uint32_t block = start; block < end; block += 64)
    {
        const uint32_t live = end - block < 64 ? end - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const __m512i vk = _mm512_loadu_si512(vkinds + block);
        const __m512i vk_previous = _mm512_loadu_si512(vkinds + block - 1);
        const __m512i vk_before = _mm512_loadu_si512(vkinds + block - 2);
        const __m512i vk_next = _mm512_loadu_si512(vkinds + block + 1);
        const __m512i enclosing_lane = _mm512_load_si512(kernels->bracket_blocks[block / 64].opener_lanes);
        const __mmask64 inside = _mm512_cmplt_epu8_mask(enclosing_lane, _mm512_set1_epi8(64));
        const __m512i previous_bits = parse_table8(t->vkind_bits, vk_previous);
        // The tagged-type keyword before a `{` after a tag or an attribute, which doesn't depend on roles. The rules read it
        // only after a tag's name or an attribute's `)` (after `__attribute__`), so a `{` after a `)` whose opener, in the
        // block, follows anything else (a function's or a statement's body) gets none.
        const __m512i previous_opener_lane = _mm512_permutex2var_epi8(enclosing_lane, parse_shifts.up8[0], _mm512_setzero_si512());
        const __mmask64 previous_inside = (inside << 1) & ~(__mmask64)1;
        const __mmask64 attribute_opener = parse_equal8(_mm512_permutexvar_epi8(previous_opener_lane, vk_previous), PARSE_KIND_ATTRIBUTE) | ~previous_inside;
        uint64_t tag_lanes = parse_equal8(vk, PARSE_KIND_L_BRACE) & ((parse_equal8(vk_previous, PARSE_KIND_R_PAREN) & attribute_opener) |
                                                                       _mm512_test_epi8_mask(previous_bits, _mm512_set1_epi8(BUSTER_PARSE_VKIND_NAME))) & live_mask;
        uint64_t tag_enum = 0;
        uint64_t tag_none = 0;
        while (tag_lanes != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(tag_lanes);
            tag_lanes &= tag_lanes - 1;
            const uint8_t keyword = parse_tag_keyword(kernels, block + lane - 1);
            tag_enum |= (uint64_t)(keyword == PARSE_KIND_ENUM) << lane;
            tag_none |= (uint64_t)(keyword == PARSE_KIND_END) << lane;
        }
        // Each class's lanes are apart, so each lane takes one pattern of bits.
        const __m512i cls = parse_table8(t->classes, vk);
        const __mmask64 class_identifier = parse_equal8(cls, PARSE_RULE_CLASS_IDENTIFIER);
        const __mmask64 class_type_name = parse_equal8(cls, PARSE_RULE_CLASS_TYPE_NAME);
        const __mmask64 class_qualifier = parse_equal8(cls, PARSE_RULE_CLASS_QUALIFIER);
        const __mmask64 class_atomic = parse_equal8(cls, PARSE_RULE_CLASS_ATOMIC);
        const __mmask64 class_unary = parse_equal8(cls, PARSE_RULE_CLASS_UNARY);
        const __mmask64 class_paren = parse_equal8(cls, PARSE_RULE_CLASS_PAREN);
        const __mmask64 class_brace = parse_equal8(cls, PARSE_RULE_CLASS_BRACE);
        __m512i base = parse_table8(t->guesses, vk);
        base = _mm512_mask_mov_epi8(base, parse_equal8(cls, PARSE_RULE_CLASS_STRING) & parse_equal8(vk_previous, PARSE_KIND_STRING), _mm512_set1_epi8(PARSE_ROLE_STRING_NEXT));
        // Names: a member after `.` and `->`; a tag, or the name of a body, after struct and enum, and after an attribute's
        // right closer (a rule); and for a typedef name, a label's after goto, else a label's, a specifier, or a declarator
        // or operand (rules: by the token before, its group and the typed state).
        const __mmask64 names = class_identifier | class_type_name;
        const __mmask64 member = parse_equal8(vk_previous, PARSE_KIND_PERIOD) | parse_equal8(vk_previous, PARSE_KIND_ARROW);
        const __mmask64 tag_keyword = parse_equal8(vk_previous, PARSE_KIND_STRUCT) | parse_equal8(vk_previous, PARSE_KIND_ENUM);
        const __m512i tag_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(PARSE_ROLE_TAG), parse_equal8(vk_next, PARSE_KIND_L_BRACE), _mm512_set1_epi8(PARSE_ROLE_LEAF_RIGHT));
        const __mmask64 after_goto = parse_equal8(vk_previous, PARSE_KIND_GOTO);
        const __mmask64 type_goto = class_type_name & after_goto;
        base = _mm512_mask_mov_epi8(base, names & tag_keyword, tag_role);
        base = _mm512_mask_mov_epi8(base, names & member, _mm512_set1_epi8(PARSE_ROLE_MEMBER));
        base = _mm512_mask_mov_epi8(base, type_goto, _mm512_set1_epi8(PARSE_ROLE_OPERAND));
        const __mmask64 names_ruled = names & ~member & ~tag_keyword & ~type_goto;
        __m512i alternative = _mm512_maskz_mov_epi8(names_ruled, tag_role);
        __m512i rules = _mm512_maskz_mov_epi8(names_ruled, _mm512_set1_epi8(BUSTER_PARSE_RULE_ATTRIBUTE_RIGHT));
        rules = _mm512_mask_mov_epi8(rules, names_ruled & class_type_name,
                                     _mm512_set1_epi8(BUSTER_PARSE_RULE_ATTRIBUTE_RIGHT | BUSTER_PARSE_RULE_ITEM_OR_PARAMETER | BUSTER_PARSE_RULE_SPECIFIER));
        rules = _mm512_mask_mov_epi8(rules, names_ruled & class_type_name & parse_equal8(vk_next, PARSE_KIND_COLON),
                                     _mm512_set1_epi8(BUSTER_PARSE_RULE_ATTRIBUTE_RIGHT | BUSTER_PARSE_RULE_ITEM_OR_PARAMETER | BUSTER_PARSE_RULE_SPECIFIER |
                                                      BUSTER_PARSE_RULE_ITEM_OR_HEAD));
        // Qualifiers, storage classes and goto after asm are the asm's, and so after one of them that is (a rule); a
        // qualifier, an _Atomic not before `(` and an attribute's keyword belong to a `*` before them (a rule).
        const __mmask64 previous_asm = parse_equal8(vk_previous, PARSE_KIND_ASM);
        const __mmask64 asm_qualifier = _mm512_test_epi8_mask(previous_bits, _mm512_set1_epi8(BUSTER_PARSE_VKIND_ASM_QUALIFIER));
        const __mmask64 asm_words = class_qualifier | parse_equal8(cls, PARSE_RULE_CLASS_STORAGE) | parse_equal8(cls, PARSE_RULE_CLASS_GOTO);
        const __mmask64 next_paren = parse_equal8(vk_next, PARSE_KIND_L_PAREN);
        base = _mm512_mask_mov_epi8(base, (asm_words & previous_asm) | (class_atomic & next_paren), _mm512_set1_epi8(PARSE_ROLE_LEAF_RIGHT));
        const __mmask64 star_words = (class_qualifier & ~previous_asm) | (class_atomic & ~next_paren) | parse_equal8(cls, PARSE_RULE_CLASS_ATTRIBUTE);
        alternative = _mm512_mask_mov_epi8(alternative, star_words, _mm512_set1_epi8(PARSE_ROLE_QUALIFIER_LEFT));
        rules = _mm512_mask_mov_epi8(rules, star_words, _mm512_set1_epi8(BUSTER_PARSE_RULE_STAR));
        rules = _mm512_mask_mov_epi8(rules, asm_words & asm_qualifier, _mm512_set1_epi8(BUSTER_PARSE_RULE_LEAF_RIGHT));
        rules = _mm512_mask_mov_epi8(rules, class_qualifier & asm_qualifier, _mm512_set1_epi8(BUSTER_PARSE_RULE_STAR | BUSTER_PARSE_RULE_LEAF_RIGHT));
        // __extension__ before a declaration is a specifier. * & + - && ++ -- after an operand are binary (a rule).
        const __m512i next_bits = parse_table8(t->vkind_bits, vk_next);
        base = _mm512_mask_mov_epi8(base, parse_equal8(cls, PARSE_RULE_CLASS_EXTENSION) & ~_mm512_test_epi8_mask(next_bits, _mm512_set1_epi8(BUSTER_PARSE_VKIND_STARTS_DECLARATION)),
                                    _mm512_set1_epi8(PARSE_ROLE_PREFIX));
        // After goto, the unary operators' role is `return`'s: a computed goto's `*` takes the whole expression after it.
        const __m512i unary_role = _mm512_mask_blend_epi8(after_goto, _mm512_set1_epi8(PARSE_ROLE_PREFIX), _mm512_set1_epi8(PARSE_ROLE_RETURN));
        base = _mm512_mask_mov_epi8(base, class_unary, unary_role);
        alternative = _mm512_mask_mov_epi8(alternative, class_unary, parse_table8(t->binary, vk));
        rules = _mm512_mask_mov_epi8(rules, class_unary, _mm512_set1_epi8(BUSTER_PARSE_RULE_OPERAND));
        // `(` by the keyword before it, or an attribute's list; else a cast's before a type, and by the token before (rules):
        // after an asm's qualifier, an asm's; after an operand, a call's; after a type, a specifier or `*` and before `)` or
        // `...`, a function's; else a group's. An attribute's `(` and an asm's go by the token before their keyword, which
        // their code in `facts` indexes ParseRoleTables.paren_by_before with.
        const __m512i paren_previous = parse_table8(t->paren_previous, vk_previous);
        // (`sizeof ({ ... })`'s `(` is a group's, its statement expression's.)
        const __mmask64 by_previous = class_paren & _mm512_test_epi8_mask(paren_previous, paren_previous) &
                                      ~(parse_equal8(vk_previous, PARSE_KIND_SIZEOF) & parse_equal8(vk_next, PARSE_KIND_L_BRACE));
        const __mmask64 attribute_list = class_paren & parse_equal8(vk_previous, PARSE_KIND_L_PAREN) & parse_equal8(vk_before, PARSE_KIND_ATTRIBUTE);
        const __mmask64 next_starts_type = _mm512_test_epi8_mask(next_bits, _mm512_set1_epi8(BUSTER_PARSE_VKIND_STARTS_TYPE));
        const __mmask64 next_function = parse_equal8(vk_next, PARSE_KIND_R_PAREN) | parse_equal8(vk_next, PARSE_KIND_ELLIPSIS);
        const __mmask64 paren_ruled = class_paren & ~by_previous & ~attribute_list;
        base = _mm512_mask_mov_epi8(base, paren_ruled, _mm512_mask_mov_epi8(_mm512_set1_epi8(PARSE_ROLE_OPEN_GROUP), next_starts_type, _mm512_set1_epi8(PARSE_ROLE_OPEN_CAST)));
        base = _mm512_mask_mov_epi8(base, attribute_list, _mm512_set1_epi8(PARSE_ROLE_OPEN_ATTRIBUTE_LIST));
        base = _mm512_mask_mov_epi8(base, by_previous, _mm512_mask_mov_epi8(paren_previous, _mm512_cmpge_epu8_mask(paren_previous, _mm512_set1_epi8((char)BUSTER_PARSE_PAREN_ASM)),
                                                                            _mm512_set1_epi8(PARSE_ROLE_OPEN_ATTRIBUTE)));
        alternative = _mm512_mask_mov_epi8(alternative, paren_ruled, _mm512_set1_epi8(PARSE_ROLE_OPEN_CALL));
        rules = _mm512_mask_mov_epi8(rules, paren_ruled, _mm512_set1_epi8(BUSTER_PARSE_RULE_OPERAND));
        rules = _mm512_mask_mov_epi8(rules, paren_ruled & asm_qualifier, _mm512_set1_epi8(BUSTER_PARSE_RULE_OPERAND | BUSTER_PARSE_RULE_LEAF_RIGHT));
        rules = _mm512_mask_add_epi8(rules, paren_ruled & next_function & ~next_starts_type, rules, _mm512_set1_epi8((char)BUSTER_PARSE_RULE_FUNCTION));
        // A comma's, a colon's and an ellipsis's codes, by kind.
        __m512i facts = parse_table8(t->codes, vk);
        facts = _mm512_mask_mov_epi8(facts, by_previous & parse_equal8(paren_previous, BUSTER_PARSE_PAREN_ATTRIBUTE), _mm512_set1_epi8(BUSTER_PARSE_CODE_ATTRIBUTE));
        facts = _mm512_mask_mov_epi8(facts, by_previous & parse_equal8(paren_previous, BUSTER_PARSE_PAREN_ASM), _mm512_set1_epi8(BUSTER_PARSE_CODE_ASM));
        // `{` after `=` (an initializer's), after struct or enum or their tag (their body's), after `(` (a statement
        // expression's block); else by the token before (parse_roles_block_avx512).
        const __mmask64 previous_name = _mm512_test_epi8_mask(previous_bits, _mm512_set1_epi8(BUSTER_PARSE_VKIND_NAME));
        const __mmask64 brace_initializer = parse_equal8(vk_previous, PARSE_KIND_EQUAL);
        const __mmask64 brace_struct = parse_equal8(vk_previous, PARSE_KIND_STRUCT) | (previous_name & parse_equal8(vk_before, PARSE_KIND_STRUCT));
        const __mmask64 brace_enum = parse_equal8(vk_previous, PARSE_KIND_ENUM) | (previous_name & parse_equal8(vk_before, PARSE_KIND_ENUM));
        base = _mm512_mask_mov_epi8(base, class_brace & brace_enum, _mm512_set1_epi8(PARSE_ROLE_OPEN_ENUM));
        base = _mm512_mask_mov_epi8(base, class_brace & brace_struct, _mm512_set1_epi8(PARSE_ROLE_OPEN_STRUCT));
        base = _mm512_mask_mov_epi8(base, class_brace & brace_initializer, _mm512_set1_epi8(PARSE_ROLE_OPEN_INITIALIZER));
        const __mmask64 brace_ruled = class_brace & ~brace_initializer & ~brace_struct & ~brace_enum & ~parse_equal8(vk_previous, PARSE_KIND_L_PAREN);
        facts = _mm512_mask_mov_epi8(facts, brace_ruled, _mm512_or_si512(parse_table8(t->brace_previous, vk_previous), _mm512_set1_epi8(BUSTER_PARSE_BRACE_RULED)));
        facts = _mm512_mask_add_epi8(facts, brace_ruled & tag_enum, facts, _mm512_set1_epi8(BUSTER_PARSE_BRACE_TAG_ENUM));
        facts = _mm512_mask_add_epi8(facts, brace_ruled & tag_none, facts, _mm512_set1_epi8(BUSTER_PARSE_BRACE_TAG_NONE));
        ParseRoleStatics* statics = &kernels->role_statics[block / 64];
        _mm512_store_si512(statics->base, base);
        _mm512_store_si512(statics->alternative, alternative);
        _mm512_store_si512(statics->rules, rules);
        _mm512_store_si512(statics->facts, facts);
    }
}

// The rules a round applies, given what each lane's kind and its neighbors' kinds decide once for the block: `base` is a
// lane's role where no rule holds, `alternative` the role a rule on a `*`, an operand or an attribute's right closer
// before it gives it, and `after_leaf` the one a LEAF_RIGHT before it gives it; `holds` has the BUSTER_PARSE_RULE_* bits
// of the rules that hold, from the lowest priority up: a function's `(`, then the alternative, then after a LEAF_RIGHT.
BUSTER_INLINE __m512i parse_rules_apply8(__m512i base, __m512i holds, __m512i alternative, __m512i after_leaf)
{
    __m512i role = _mm512_mask_mov_epi8(base, _mm512_test_epi8_mask(holds, _mm512_set1_epi8((char)BUSTER_PARSE_RULE_FUNCTION)), _mm512_set1_epi8(PARSE_ROLE_OPEN_FUNCTION));
    role = _mm512_mask_mov_epi8(role, _mm512_test_epi8_mask(holds, _mm512_set1_epi8(BUSTER_PARSE_RULE_STAR | BUSTER_PARSE_RULE_OPERAND | BUSTER_PARSE_RULE_ATTRIBUTE_RIGHT)),
                                alternative);
    role = _mm512_mask_mov_epi8(role, _mm512_test_epi8_mask(holds, _mm512_set1_epi8(BUSTER_PARSE_RULE_LEAF_RIGHT)), after_leaf);
    return role;
}

// One block of K2 in AVX-512 (see above), in at most `round_limit` rounds. Returns the rounds it took, or 0 if it didn't
// converge; either way the roles of its last round are in kernels->roles, and its typed states in *typed_out. When it
// converges, *context_out gets the block's statement contexts (parse_statement_contexts), from the roles it settles on,
// its flags join K3's, and it puts the block's openers still open at its end onto the group stacks, and the features
// of its last two tokens into `carry`. The block loop runs one round (parse_roles_rounds_avx512), with no loop around it,
// and the few blocks that need more run the rest out of line (parse_roles_block_more_avx512), from those roles and
// typed states (`typed_start`; else the typed states come from the roles the rounds start from).
//
// What a round reads of the tokens before a lane and of its group is their features (parse_features8), and what each
// lane's kind and its neighbors' kinds decide is worked out once, before the rounds, into a few vectors: the role each
// lane has where no rule that reads a role holds, the roles such rules give it, and which rules apply to it, as bits
// that one table lookup on the features before it tests (ParseRoleTables.dynamic). So a round keeps few masks and
// vectors live, and the block loop stores almost none of them to the stack to load it back.
BUSTER_INLINE uint32_t parse_roles_block_avx512(ParseKernels* kernels, uint32_t block, uint32_t live, ParseRoleCarry* carry, ParseGroupWindows* windows, const uint8_t* starts,
                                                const uint64_t* typed_start, uint32_t round_limit, uint64_t* typed_out, ParseStatementContext* context_out)
{
    const ParseRoleTables* t = &parse_role_tables;
    const uint8_t* vkinds = kernels->vkinds;
    const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
    const __m512i vk = _mm512_loadu_si512(vkinds + block);
    const __m512i vk_previous = _mm512_loadu_si512(vkinds + block - 1);
    const __m512i vk_next = _mm512_loadu_si512(vkinds + block + 1);
    const uint32_t depth_base = kernels->depths[block];
    // The enclosing opener: a lane of the block, or an open group before it, whose role comes from carry->group_roles;
    // the window index of the group stacks for each token's depth before it, (depth - 1) - (base - 64); and the block's
    // openers still open at its end, the only ones a later block reads from the group stacks. K1 found them all.
    const ParseBracketBlock* view = &kernels->bracket_blocks[block / 64];
    const __m512i enclosing_lane = _mm512_load_si512(view->opener_lanes);
    const __mmask64 inside = _mm512_cmplt_epu8_mask(enclosing_lane, _mm512_set1_epi8(64));
    const __m512i window_index = _mm512_add_epi8(_mm512_load_si512(view->relative_depths), _mm512_set1_epi8(63));
    const uint64_t open = view->open & live_mask;
    // The 64 depths below the block's start, from the windows of the group stacks (which have BUSTER_PARSE_DEPTH_PADDING
    // entries below depth 0); and the 64 below those, from the stacks, which the block before last wrote at the latest.
    // Lanes with no enclosing opener at all are at depth 0: their entry is below depth 0, PARSE_ROLE_NONE.
    const __m512i outside_group = _mm512_permutexvar_epi8(window_index, windows->roles);
    const __m512i below_roles = _mm512_loadu_si512(carry->group_roles + (int64_t)(int32_t)depth_base - 128);
    const __m512i below_keywords = _mm512_loadu_si512(carry->group_keywords + (int64_t)(int32_t)depth_base - 128);
    const __m512i outside_features = parse_table8(t->features, outside_group);
    // What each lane's kind and its neighbors' kinds decide, from the pass before the rounds (parse_role_statics_avx512).
    const ParseRoleStatics* statics = &kernels->role_statics[block / 64];
    const __m512i base = _mm512_load_si512(statics->base);
    const __m512i alternative = _mm512_load_si512(statics->alternative);
    const __m512i rules = _mm512_load_si512(statics->rules);
    const __m512i facts = _mm512_load_si512(statics->facts);
    // What the rounds read of each lane's own kind, the role its kind fixes the features of, and a cast's closer's role
    // by the token after it.
    const __m512i self = parse_table8(t->self_bits, vk);
    const __m512i fix_role = parse_table8(t->fix_roles, vk);
    const __m512i cast_closer = parse_table8(t->cast_closers, vk_next);
    const __m512i after_leaf = _mm512_mask_mov_epi8(_mm512_set1_epi8(PARSE_ROLE_LEAF_RIGHT), _mm512_test_epi8_mask(self, _mm512_set1_epi8(BUSTER_PARSE_SELF_PAREN)),
                                                    _mm512_set1_epi8(PARSE_ROLE_OPEN_ASM));
    // The features of the two tokens before the block, for the shifts: lane 0 of the first fill, lanes 0 and 1 of the
    // second.
    const __m512i fill_previous = _mm512_castsi128_si512(_mm_cvtsi32_si128(carry->previous_feature));
    const __m512i fill_before = _mm512_castsi128_si512(_mm_cvtsi32_si128(carry->before_feature | (carry->previous_feature << 8)));
    // Rounds, from the rules applied to the guesses before each lane (or, for the check of the rounds' end, from the roles
    // in `starts`), with the closers by their openers' guesses; and the typed state they give (without the attributes'
    // copies). The guesses change how many rounds a block takes, never their result.
    __m512i roles;
    if (starts != 0)
    {
        roles = _mm512_loadu_si512(starts + block);
    }
    else
    {
        const __m512i guessed_before = _mm512_mask_mov_epi8(parse_table8(t->guess_features, vk_previous), 1, fill_previous);
        roles = parse_rules_apply8(base, _mm512_and_si512(rules, parse_table8(t->dynamic, guessed_before)), alternative, after_leaf);
        const __m512i group_guess = _mm512_mask_mov_epi8(outside_group, inside, _mm512_permutexvar_epi8(enclosing_lane, roles));
        roles = _mm512_mask_mov_epi8(roles, _mm512_test_epi8_mask(self, _mm512_set1_epi8(BUSTER_PARSE_SELF_CLOSER)),
                                     _mm512_mask_mov_epi8(parse_table8(t->closers, group_guess), parse_equal8(group_guess, PARSE_ROLE_OPEN_CAST), cast_closer));
    }
    __m512i features = parse_features8(roles, fix_role);
    uint64_t typed = 0;
    if (typed_start != 0)
    {
        typed = *typed_start;
    }
    else
    {
        uint64_t unused_carry;
        typed = parse_typed_carries(parse_typed_generate8(features), parse_typed_propagate8(features) & ~parse_equal8(roles, PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER),
                                    carry->typed >> 63, &unused_carry);
    }
    __m512i final_group = outside_group;
    __mmask64 flags = 0;
    uint32_t rounds = 0;
    bool converged = false;
    while (!converged && rounds < round_limit)
    {
        rounds += 1;
        // The rules that hold, by the features of the token before each lane.
        const __m512i previous_features = _mm512_permutex2var_epi8(features, parse_shifts.up8[0], fill_previous);
        const __m512i holds = _mm512_and_si512(rules, parse_table8(t->dynamic, previous_features));
        __m512i role = parse_rules_apply8(base, holds, alternative, after_leaf);
        // A typedef name a rule settles: a label's in a block (after an item or a head, before `:`); a specifier where
        // one may start; else a declarator or an operand, which hides the typedef, flagged.
        const __m512i group_features = _mm512_mask_mov_epi8(outside_features, inside, _mm512_permutexvar_epi8(enclosing_lane, features));
        const uint64_t typed_before = (typed << 1) | (carry->typed >> 63);
        const __mmask64 type_names = _mm512_test_epi8_mask(rules, _mm512_set1_epi8(BUSTER_PARSE_RULE_ITEM_OR_PARAMETER)) &
                                     ~_mm512_test_epi8_mask(holds, _mm512_set1_epi8(BUSTER_PARSE_RULE_ATTRIBUTE_RIGHT));
        const __mmask64 label = _mm512_test_epi8_mask(holds, _mm512_set1_epi8(BUSTER_PARSE_RULE_ITEM_OR_HEAD)) & parse_equal8(group_features, BUSTER_PARSE_FEATURES_BLOCK);
        const __mmask64 position = _mm512_test_epi8_mask(holds, _mm512_set1_epi8(BUSTER_PARSE_RULE_ITEM_OR_PARAMETER)) |
                                   (_mm512_test_epi8_mask(holds, _mm512_set1_epi8(BUSTER_PARSE_RULE_SPECIFIER)) & ~typed_before);
        role = _mm512_mask_mov_epi8(role, type_names & (label | ~position), _mm512_set1_epi8(PARSE_ROLE_OPERAND));
        __mmask64 flag = type_names & ~label & ~position;
        // An attribute's `(` and an asm's, by the pred class of the token before their keyword.
        const __m512i before_features = _mm512_permutex2var_epi8(features, parse_shifts.up2_with_fill, fill_before);
        const __m512i by_before = _mm512_permutexvar_epi8(_mm512_ternarylogic_epi64(before_features, _mm512_set1_epi8(0x0F), facts, 0xEA), _mm512_loadu_si512(t->paren_by_before));
        role = _mm512_mask_mov_epi8(role, _mm512_test_epi8_mask(facts, _mm512_set1_epi8(BUSTER_PARSE_CODE_PAREN)), by_before);
        // `{` by the token before (its code from ParseRoleTables.brace_codes) and its facts, which keep NESTED only in an
        // initializer: its role and flag (bit 7) from ParseRoleTables.brace_rules.
        const __m512i brace_facts = _mm512_ternarylogic_epi64(facts, _mm512_permutexvar_epi8(group_features, _mm512_loadu_si512(t->nested_groups)),
                                                              _mm512_set1_epi8(BUSTER_PARSE_BRACE_AFTER_NAME | BUSTER_PARSE_BRACE_TAG_ENUM | BUSTER_PARSE_BRACE_TAG_NONE), 0xE0);
        const __m512i brace = parse_table8(t->brace_rules, _mm512_or_si512(brace_facts, parse_table8(t->brace_codes, previous_features)));
        const __mmask64 braces = _mm512_test_epi8_mask(facts, _mm512_set1_epi8(BUSTER_PARSE_BRACE_RULED));
        role = _mm512_mask_mov_epi8(role, braces, _mm512_and_si512(brace, _mm512_set1_epi8(0x7F)));
        flag |= braces & _mm512_movepi8_mask(brace);
        // What depends on the group, from this round's openers (( and { by their rules, [ fixed): a closer and the commas,
        // colons and ellipses in a group then settle in the same round as their opener, and so do an attribute's names.
        const __m512i group_now = _mm512_mask_mov_epi8(outside_group, inside, _mm512_permutexvar_epi8(enclosing_lane, role));
        final_group = group_now;
        const __m512i group_rules = parse_table8(t->group_rules, group_now);
        const __m512i separator = _mm512_permutexvar_epi8(_mm512_ternarylogic_epi64(group_rules, _mm512_set1_epi8(0x0F), facts, 0xEA), _mm512_loadu_si512(t->separators));
        role = _mm512_mask_mov_epi8(role, _mm512_test_epi8_mask(facts, _mm512_set1_epi8((char)BUSTER_PARSE_CODE_SEPARATOR)), separator);
        const __m512i closer_role = _mm512_mask_mov_epi8(parse_table8(t->closers, group_now),
                                                         _mm512_test_epi8_mask(group_rules, _mm512_set1_epi8(BUSTER_PARSE_GROUP_RULE_CAST_CLOSER)), cast_closer);
        role = _mm512_mask_mov_epi8(role, _mm512_test_epi8_mask(self, _mm512_set1_epi8(BUSTER_PARSE_SELF_CLOSER)), closer_role);
        const __mmask64 attribute_name = _mm512_test_epi8_mask(group_rules, _mm512_set1_epi8(BUSTER_PARSE_GROUP_RULE_NAMES)) &
                                         _mm512_test_epi8_mask(self, _mm512_set1_epi8(BUSTER_PARSE_SELF_KEYWORD));
        role = _mm512_mask_mov_epi8(role, attribute_name, _mm512_set1_epi8(PARSE_ROLE_OPERAND));
        flag = ((flag | _mm512_test_epi8_mask(self, _mm512_set1_epi8((char)BUSTER_PARSE_SELF_FLAGGED))) & ~attribute_name) | parse_equal8(role, PARSE_ROLE_ERROR);
        // The typed state: a type specifier sets it, the other specifiers and LEAF_RIGHT carry it, and an attribute's
        // closer takes the state from before the attribute's keyword, two tokens before its opener.
        const __m512i new_features = parse_features8(role, fix_role);
        const uint64_t copies = parse_equal8(role, PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER);
        uint64_t copied = 0;
        uint64_t copy_lanes = copies & live_mask;
        while (copy_lanes != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(copy_lanes);
            copy_lanes &= copy_lanes - 1;
            const uint32_t opener = kernels->enclosing[block + lane];
            bool value = false;
            if (opener != BUSTER_PARSE_NONE && opener >= 2 && opener - 2 >= block)
            {
                value = (typed >> (opener - 2 - block)) & 1;
            }
            else if (opener != BUSTER_PARSE_NONE && opener >= 2 && opener - 2 + 64 >= block)
            {
                value = (carry->typed >> (opener - 2 + 64 - block)) & 1;
            }
            else if (opener != BUSTER_PARSE_NONE && opener >= 2)
            {
                value = carry->group_typed[(int64_t)(int32_t)depth_base - 64 + view->relative_depths[lane] + 63] != 0;
            }
            copied |= (uint64_t)value << lane;
        }
        uint64_t carry_out;
        const uint64_t new_typed = parse_typed_carries(parse_typed_generate8(new_features) | copied, parse_typed_propagate8(new_features) & ~copies, carry->typed >> 63, &carry_out);
        // The round's roles are final when no lane reads what changed in them, so that the next round would compute them
        // again. Of the roles before it a round reads, per class: the features of the token before (all of them, or for
        // an identifier whether it is CLOSE_ATTRIBUTE_RIGHT), the pred of the token two back (for `(`), the features of
        // the lane's opener (for typedef names and `{`), and the typed state before (for typedef names); an attribute's
        // closer reads typed states before its opener. The commas, colons, ellipses, closers and attribute names of a
        // group read its opener as this round sets it, which changes only if the opener is a lane that reads a change.
        // So each lane gets, as its BUSTER_PARSE_SELF_* bits, what changed of what a lane of each class reads (`read`),
        // and is affected if those meet its own.
        const __mmask64 changed = _mm512_cmpneq_epi8_mask(new_features, features);
        const __m512i attribute_right = _mm512_set1_epi8(BUSTER_PARSE_FEATURES_ATTRIBUTE_RIGHT);
        const __mmask64 right_changed = _kxor_mask64(_mm512_cmpeq_epi8_mask(new_features, attribute_right), _mm512_cmpeq_epi8_mask(features, attribute_right));
        const uint64_t typed_changed = (new_typed ^ typed) & live_mask;
        const __mmask64 opener_changed = _mm512_mask_test_epi8_mask(inside, _mm512_permutexvar_epi8(enclosing_lane, _mm512_movm_epi8(changed)), _mm512_set1_epi8(-1));
        const __m512i read = _mm512_or_si512(
            _mm512_ternarylogic_epi64(_mm512_maskz_mov_epi8(_kshiftli_mask64(changed, 1), _mm512_set1_epi8(BUSTER_PARSE_SELF_READS_PREVIOUS)),
                                      _mm512_maskz_mov_epi8(_kshiftli_mask64(right_changed, 1), _mm512_set1_epi8(BUSTER_PARSE_SELF_IDENTIFIER)),
                                      _mm512_maskz_mov_epi8(_kshiftli_mask64(changed, 2), _mm512_set1_epi8(BUSTER_PARSE_SELF_PAREN)), 0xFE),
            _mm512_or_si512(_mm512_maskz_mov_epi8(opener_changed, _mm512_set1_epi8(BUSTER_PARSE_SELF_TYPE_NAME | BUSTER_PARSE_SELF_BRACE)),
                            _mm512_maskz_mov_epi8(_cvtu64_mask64(typed_changed << 1), _mm512_set1_epi8(BUSTER_PARSE_SELF_TYPE_NAME))));
        const __mmask64 affected = _kor_mask64(_mm512_test_epi8_mask(read, self), typed_changed != 0 ? copies : 0);
        converged = _ktestz_mask64_u8(affected, live_mask) != 0;
        roles = role;
        features = new_features;
        typed = new_typed;
        flags = flag;
    }
    // The statement contexts, from the final roles (the last round's openers' roles, its group roles): a cast candidate's
    // opener that its closer turns into another role later is no block either way. A closer's is its opener's group: the
    // opener's lane's, or the group two depths below the closer's depth on the group stack.
    const __mmask64 closers = _mm512_test_epi8_mask(self, _mm512_set1_epi8(BUSTER_PARSE_SELF_CLOSER)) & live_mask;
    const __m512i outside_outer = _mm512_permutex2var_epi8(below_roles, _mm512_add_epi8(window_index, _mm512_set1_epi8(63)), windows->roles);
    const __m512i outer_group = _mm512_mask_mov_epi8(outside_outer, inside, _mm512_permutexvar_epi8(enclosing_lane, final_group));
    const __m512i context_group = _mm512_mask_mov_epi8(final_group, closers, outer_group);
    // A closer's opener's keyword: the kind before the opener's lane, or the group stack's.
    const __m512i outside_keyword = _mm512_permutexvar_epi8(window_index, windows->keywords);
    const __m512i opener_keyword = _mm512_mask_mov_epi8(outside_keyword, inside, _mm512_permutexvar_epi8(enclosing_lane, vk_previous));
    *context_out = (ParseStatementContext){
        .blocks = _mm512_test_epi8_mask(parse_table8(t->group_rules, context_group), _mm512_set1_epi8(BUSTER_PARSE_GROUP_RULE_BLOCK)) & live_mask,
        .ifs = parse_equal8(opener_keyword, PARSE_KIND_IF) & closers,
        .whiles = parse_equal8(opener_keyword, PARSE_KIND_WHILE) & closers,
    };
    _mm512_storeu_si512(kernels->roles + block, roles);
    // The flags from K3 stay, and a block that didn't converge adds none: the rounds after, or the twin, set its own.
    const __m512i old_flags = _mm512_loadu_si512(kernels->flags + block);
    _mm512_storeu_si512(kernels->flags + block, _mm512_mask_mov_epi8(old_flags, converged ? flags & live_mask : 0, _mm512_set1_epi8(1)));
    *typed_out = typed;
    if (converged)
    {
        // The openers still open at the block's end, for the later blocks that read the group stacks, from the registers
        // (a byte loaded right after the 64-byte stores above would wait for them): they sit at consecutive depths from the
        // first one's, in lane order, so each stack takes them compressed, in one store from that depth. What the store
        // writes past them is above every group open after the block, which no block reads before its opener writes it.
        // The stores stay in the stacks: K2 runs on balanced files only, whose depths are at most half their tokens, and
        // the stacks have an entry per token and 2 * BUSTER_PARSE_TOKEN_PADDING more.
        // The next block's windows, for its starting depth: lane i holds depth depth_next - 64 + i, which an opener still open
        // at this block's end holds from the first one's depth up, and below that this block's windows, and below those the
        // stacks' 64 depths below them.
        const uint32_t depth_next = kernels->depths[block + 64];
        const uint32_t first_open = open != 0 ? depth_base + (uint32_t)(int32_t)view->relative_depths[__builtin_ctzll(open)] : depth_next;
        const __m512i open_roles = _mm512_maskz_compress_epi8(open, roles);
        const __m512i open_keywords = _mm512_maskz_compress_epi8(open, vk_previous);
        const __m512i from_windows = _mm512_add_epi8(parse_shifts.lanes8, _mm512_set1_epi8((char)(depth_next - depth_base + 64)));
        const __m512i from_open = _mm512_add_epi8(parse_shifts.lanes8, _mm512_set1_epi8((char)(depth_next - 64 - first_open)));
        const __mmask64 opened = _mm512_cmpge_epu8_mask(parse_shifts.lanes8, _mm512_set1_epi8((char)(first_open + 64 - depth_next)));
        windows->roles = _mm512_mask_permutexvar_epi8(_mm512_permutex2var_epi8(below_roles, from_windows, windows->roles), opened, from_open, open_roles);
        windows->keywords = _mm512_mask_permutexvar_epi8(_mm512_permutex2var_epi8(below_keywords, from_windows, windows->keywords), opened, from_open, open_keywords);
        if (open != 0)
        {
            const uint64_t typed_two_back = (typed << 2) | (carry->typed >> 62);
            _mm512_storeu_si512(carry->group_roles + (int32_t)first_open, open_roles);
            _mm512_storeu_si512(carry->group_typed + (int32_t)first_open, _mm512_maskz_compress_epi8(open, _mm512_maskz_mov_epi8(typed_two_back, _mm512_set1_epi8(1))));
            _mm512_storeu_si512(carry->group_keywords + (int32_t)first_open, open_keywords);
        }
        const __m512i last_two = _mm512_permutexvar_epi8(_mm512_set1_epi16((short)((live - 1) | (live >= 2 ? live - 2 : live - 1) << 8)), features);
        const uint32_t bytes = (uint32_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(last_two));
        carry->previous_feature = (uint8_t)bytes;
        carry->before_feature = (uint8_t)(bytes >> 8);
    }
    return converged ? rounds : 0;
}

// The statement contexts of a block the twin's loop took, from its final roles, as a converged block works them out
// (parse_roles_block_avx512): parse_statement_context_block's, in AVX-512. A token's group is its opener's lane's role,
// or the group stacks' at the depth below its own, and a closer's context is its opener's group.
static ParseStatementContext parse_statement_context_avx512(const ParseKernels* kernels, const ParseRoleCarry* carry, uint32_t block, uint32_t live)
{
    const ParseRoleTables* t = &parse_role_tables;
    const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
    const ParseBracketBlock* view = &kernels->bracket_blocks[block / 64];
    const __m512i enclosing_lane = _mm512_load_si512(view->opener_lanes);
    const __mmask64 inside = _mm512_cmplt_epu8_mask(enclosing_lane, _mm512_set1_epi8(64));
    const __m512i window_index = _mm512_add_epi8(_mm512_load_si512(view->relative_depths), _mm512_set1_epi8(63));
    const int64_t depth_base = (int32_t)kernels->depths[block];
    const __m512i roles = _mm512_loadu_si512(kernels->roles + block);
    const __m512i vk = _mm512_loadu_si512(kernels->vkinds + block);
    const __m512i vk_previous = _mm512_loadu_si512(kernels->vkinds + block - 1);
    const __mmask64 closers = _mm512_cmplt_epi8_mask(parse_table8((const uint8_t*)parse_kind_deltas, vk), _mm512_setzero_si512()) & live_mask;
    const __m512i outside_group = _mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_roles + depth_base - 64));
    const __m512i group = _mm512_mask_mov_epi8(outside_group, inside, _mm512_permutexvar_epi8(enclosing_lane, roles));
    const __m512i outside_outer = _mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_roles + depth_base - 65));
    const __m512i outer_group = _mm512_mask_mov_epi8(outside_outer, inside, _mm512_permutexvar_epi8(enclosing_lane, group));
    const __m512i context_group = _mm512_mask_mov_epi8(group, closers, outer_group);
    const __m512i outside_keyword = _mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_keywords + depth_base - 64));
    const __m512i opener_keyword = _mm512_mask_mov_epi8(outside_keyword, inside, _mm512_permutexvar_epi8(enclosing_lane, vk_previous));
    const ParseStatementContext result = {
        .blocks = _mm512_test_epi8_mask(parse_table8(t->group_rules, context_group), _mm512_set1_epi8(BUSTER_PARSE_GROUP_RULE_BLOCK)) & live_mask,
        .ifs = parse_equal8(opener_keyword, PARSE_KIND_IF) & closers,
        .whiles = parse_equal8(opener_keyword, PARSE_KIND_WHILE) & closers,
    };
    return result;
}


// A block K2's rounds didn't settle, through the twin's loop, from its start: its roles, flags and typed states, its
// statement contexts, and its openers still open at its end (K1's view has them) onto the group stacks, at their
// depths, for the later blocks that read them. Returns its typed states as a mask. Out of line, so that this rare path
// leaves the rounds' code alone.
__attribute__((noinline)) static uint64_t parse_roles_twin_block(ParseKernels* kernels, uint32_t block, uint32_t live, uint8_t* typed, ParseRoleCarry* carry,
                                                                  ParseStatementContext* context)
{
    for (uint32_t i = block; i < block + live; i += 1)
    {
        const bool typed_before = i > 0 && typed[i - 1];
        bool flag = false;
        const uint8_t role = parse_role_rule(kernels, i, typed_before, &flag);
        kernels->roles[i] = role;
        kernels->flags[i] |= flag;
        const uint32_t opener = kernels->enclosing[i];
        if (role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER && opener != BUSTER_PARSE_NONE && opener >= 2)
        {
            typed[i] = typed[opener - 2];
        }
        else
        {
            typed[i] = parse_role_is_type_specifier(role, kernels->vkinds[i]) || (parse_role_propagates_typed(role) && typed_before);
        }
    }
    const uint64_t result = _mm512_test_epi8_mask(_mm512_loadu_si512(typed + block), _mm512_set1_epi8(1));
    *context = parse_statement_context_avx512(kernels, carry, block, live);
    uint64_t openers = kernels->bracket_blocks[block / 64].open;
    while (openers != 0)
    {
        const uint32_t i = block + (uint32_t)__builtin_ctzll(openers);
        openers &= openers - 1;
        carry->group_roles[(int32_t)kernels->depths[i]] = kernels->roles[i];
        carry->group_typed[(int32_t)kernels->depths[i]] = i >= 2 ? typed[i - 2] : 0;
        carry->group_keywords[(int32_t)kernels->depths[i]] = kernels->vkinds[(int64_t)i - 1];
    }
    carry->previous_feature = parse_role_features(kernels->roles[block + live - 1], kernels->vkinds[block + live - 1]);
    carry->before_feature = live >= 2 ? parse_role_features(kernels->roles[block + live - 2], kernels->vkinds[block + live - 2]) : carry->previous_feature;
    return result;
}

// What a block the rounds settled leaves besides its roles: its typed states as bytes, the role of each cast
// candidate's opener that its closer decided, its statement contexts, and the typed states the next block carries.
BUSTER_INLINE void parse_roles_settled(ParseKernels* kernels, uint8_t* typed, ParseStatementContext* contexts, ParseRoleCarry* carry, uint32_t block, uint32_t live,
                                       uint64_t typed_mask, ParseStatementContext context)
{
    _mm512_storeu_si512(typed + block, _mm512_maskz_mov_epi8(typed_mask, _mm512_set1_epi8(1)));
    // A cast candidate's opener takes the role its closer decided, as parse_closer_role does; one that stays a cast keeps
    // OPEN_CAST.
    const __m512i role = _mm512_loadu_si512(kernels->roles + block);
    uint64_t casts = (parse_equal8(role, PARSE_ROLE_CLOSE_COMPOUND) | parse_equal8(role, PARSE_ROLE_CLOSE_FUNCTION)) & (live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1));
    while (casts != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(casts);
        casts &= casts - 1;
        const uint32_t opener = kernels->enclosing[block + lane];
        if (opener != BUSTER_PARSE_NONE && kernels->roles[opener] == PARSE_ROLE_OPEN_CAST)
        {
            const uint8_t closer_role = kernels->roles[block + lane];
            kernels->roles[opener] = closer_role == PARSE_ROLE_CLOSE_COMPOUND ? PARSE_ROLE_OPEN_COMPOUND : closer_role == PARSE_ROLE_CLOSE_CAST ? PARSE_ROLE_OPEN_CAST :
                                                                                                                                            PARSE_ROLE_OPEN_FUNCTION;
        }
    }
    contexts[block / 64] = context;
    carry->typed = live == 64 ? typed_mask : typed_mask << (64 - live);
}

// K2's first round on the blocks from `block` on, in order, until one doesn't converge in it (or, with `twin_only`, at
// once): their roles, typed states (into `typed` as bytes too) and statement contexts. Returns the first block they
// didn't settle, or count; *typed_left gets the typed states its round left, and *windows_io (the group stacks'
// windows for `block`'s depth on entry) that block's windows. The loop calls nothing, and keeps the carry and the
// windows in copies whose addresses never leave it, so that its constants, the carry and the windows stay in registers
// from block to block. With one round and no loop around it, what the round reads of the block isn't hoisted ahead of
// it: a loop of rounds kept it all live across the loop, about 20 vectors and 20 masks a block stored to the stack
// before the round and loaded back in it.
static uint32_t parse_roles_rounds_avx512(ParseKernels* kernels, uint32_t count, uint8_t* typed, ParseStatementContext* contexts, uint64_t* rounds,
                                          const uint8_t* starts, bool twin_only, ParseRoleCarry* carry_io, ParseGroupWindows* windows_io, uint32_t block,
                                          uint64_t* typed_left)
{
    ParseRoleCarry carry = *carry_io;
    ParseGroupWindows windows = *windows_io;
    bool converging = !twin_only;
    uint64_t typed_mask = 0;
    while (block < count && converging)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        ParseStatementContext context;
        const uint32_t taken = parse_roles_block_avx512(kernels, block, live, &carry, &windows, starts, 0, BUSTER_PARSE_ROLE_LOOP_ROUNDS, &typed_mask, &context);
        converging = taken != 0;
        if (converging)
        {
            rounds[taken] += 1;
            parse_roles_settled(kernels, typed, contexts, &carry, block, live, typed_mask, context);
            block += 64;
        }
    }
    *carry_io = carry;
    *windows_io = windows;
    *typed_left = typed_mask;
    return block < count ? block : count;
}


// A block whose first round didn't settle it: the rounds after it, from the roles that round left in kernels->roles and
// the typed states it left in *typed_io, as the rounds would go on, with the block's windows (in which, if they settle
// it, the next block's go). Returns the rounds the block took, the first one's included, or 0 if they didn't settle it
// either. Out of line, so that the first round's loop has no loop of rounds.
__attribute__((noinline)) static uint32_t parse_roles_block_more_avx512(ParseKernels* kernels, uint32_t block, uint32_t live, ParseRoleCarry* carry, ParseGroupWindows* windows,
                                                                         uint64_t* typed_io, ParseStatementContext* context_out)
{
    const uint64_t typed_start = *typed_io;
    const uint32_t taken = parse_roles_block_avx512(kernels, block, live, carry, windows, kernels->roles, &typed_start,
                                                    BUSTER_PARSE_ROLE_ROUNDS - BUSTER_PARSE_ROLE_LOOP_ROUNDS, typed_io, context_out);
    return taken != 0 ? taken + BUSTER_PARSE_ROLE_LOOP_ROUNDS : 0;
}

// K2 in AVX-512: the blocks in order, each in rounds; a block the rounds don't settle runs the twin's loop. Each block's
// enclosing openers and depths come from K1's view of it (kernels->bracket_blocks). `typed` is a byte per token (the
// twin's array, which the fallback and the group stacks read), and `groups` holds the stacks of an entry per depth.
// `contexts` gets each block's statement contexts, which the vector K5 reads. `rounds` (BUSTER_PARSE_ROLE_ROUNDS + 1
// counters) counts the blocks by the rounds they took, index 0 for those the twin did. In a check, `starts` gives every
// token the role its rounds start from instead of its kind's guess (else 0), and `twin_only` sends every block to the
// twin's loop.
static void parse_roles_starting_avx512(ParseKernels* kernels, uint32_t count, uint8_t* typed, const ParseGroupStacks* groups, ParseStatementContext* contexts,
                                        uint64_t* rounds, const uint8_t* starts, bool twin_only)
{
    ParseRoleCarry carry = {
        .previous_feature = 0,
        .before_feature = 0,
        .typed = 0,
        .group_roles = groups->roles,
        .group_typed = groups->typed,
        .group_keywords = groups->keywords,
    };
    ParseGroupWindows windows = parse_group_windows_load(&carry, 0);
    uint32_t block = 0;
    uint32_t end = 0;
    while (block < count)
    {
        // The statics of the next BUSTER_PARSE_ROLE_CHUNK blocks, then their rounds, which read them from the L1 cache
        // (on sqlite3.c, one pass over the whole file first took as long).
        if (block == end)
        {
            end = count - block > BUSTER_PARSE_ROLE_CHUNK * 64 ? block + BUSTER_PARSE_ROLE_CHUNK * 64 : count;
            parse_role_statics_avx512(kernels, block, end);
        }
        uint64_t typed_mask = 0;
        block = parse_roles_rounds_avx512(kernels, end, typed, contexts, rounds, starts, twin_only, &carry, &windows, block, &typed_mask);
        if (block < end)
        {
            const uint32_t live = end - block < 64 ? end - block : 64;
            ParseStatementContext context;
            const uint32_t taken = twin_only ? 0 : parse_roles_block_more_avx512(kernels, block, live, &carry, &windows, &typed_mask, &context);
            if (taken != 0)
            {
                parse_roles_settled(kernels, typed, contexts, &carry, block, live, typed_mask, context);
            }
            else
            {
                // The twin writes the group stacks a token at a time: the next block's windows come from them.
                typed_mask = parse_roles_twin_block(kernels, block, live, typed, &carry, &context);
                contexts[block / 64] = context;
                carry.typed = live == 64 ? typed_mask : typed_mask << (64 - live);
                windows = parse_group_windows_load(&carry, block + 64 < count ? kernels->depths[block + 64] : 0);
            }
            rounds[taken] += 1;
            block += 64;
        }
    }
}

static void parse_roles_avx512(ParseKernels* kernels, uint32_t count, uint8_t* typed, const ParseGroupStacks* groups, ParseStatementContext* contexts, uint64_t* rounds)
{
    parse_roles_starting_avx512(kernels, count, typed, groups, contexts, rounds, 0, false);
}

// ---------------------------------------------------------------------------------------------------------------------
// K3's classification, K1's second part and K5 in AVX-512

// The same with a scalar load per identifier, over the block's identifier mask.
static void parse_classify_mask_avx512(ParseKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count)
{
    const __m512i identifier = _mm512_set1_epi8(PARSE_KIND_IDENTIFIER);
    parse_classify_begin(kernels);
    uint32_t visible = 0;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i kind = _mm512_loadu_si512(kinds + block);
        uint64_t lanes = _mm512_cmpeq_epi8_mask(kind, identifier);
        uint64_t type_names = 0;
        while (lanes != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(lanes);
            lanes &= lanes - 1;
            type_names |= (uint64_t)parse_classify_typedef(kernels, block + lane, names[block + lane], &visible) << lane;
        }
        _mm512_storeu_si512(kernels->vkinds + block, _mm512_mask_mov_epi8(kind, type_names, _mm512_set1_epi8(PARSE_VKIND_TYPE_NAME)));
        _mm512_storeu_si512(kernels->flags + block, _mm512_setzero_si512());
    }
    parse_classify_declarators(kernels);
}

// K1's second part in AVX-512: the matching runs over the ? case default and `:` tokens only (a mask per block, then
// the twin's stack), and the depth of their groups is a prefix sum of their deltas, added to each token's depth.
static void parse_secondary_avx512(ParseKernels* kernels, uint32_t count, uint32_t* secondary)
{
#if !BUSTER_HOST_VECTOR
    parse_secondary_scalar(kernels, count, secondary);
#else
    uint32_t open = 0;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i role = _mm512_loadu_si512(kernels->roles + block);
        uint64_t lanes = (parse_equal8(role, PARSE_ROLE_OPEN_TERNARY) | parse_equal8(role, PARSE_ROLE_OPEN_CASE) | parse_equal8(role, PARSE_ROLE_CLOSE_TERNARY) |
                          parse_equal8(role, PARSE_ROLE_CLOSE_CASE) | parse_equal8(role, PARSE_ROLE_LABEL)) &
                         (count - block < 64 ? (((uint64_t)1 << (count - block)) - 1) : ~(uint64_t)0);
        while (lanes != 0)
        {
            const uint32_t i = block + (uint32_t)__builtin_ctzll(lanes);
            lanes &= lanes - 1;
            if (kernels->roles[i] == PARSE_ROLE_OPEN_TERNARY || kernels->roles[i] == PARSE_ROLE_OPEN_CASE)
            {
                secondary[open] = i;
                open += 1;
            }
            else
            {
                const uint32_t depth = kernels->depths[i];
                while (open != 0 && kernels->depths[secondary[open - 1]] > depth)
                {
                    kernels->flags[secondary[open - 1]] = 1;
                    kernels->unbalanced = true;
                    open -= 1;
                }
                if (open != 0 && kernels->depths[secondary[open - 1]] == depth)
                {
                    const uint32_t opener = secondary[open - 1];
                    open -= 1;
                    kernels->roles[i] = kernels->roles[opener] == PARSE_ROLE_OPEN_CASE ? PARSE_ROLE_CLOSE_CASE : PARSE_ROLE_CLOSE_TERNARY;
                }
                else
                {
                    const uint8_t previous = kernels->vkinds[(int64_t)i - 1];
                    kernels->roles[i] = PARSE_ROLE_LABEL;
                    kernels->flags[i] |= previous != PARSE_KIND_IDENTIFIER && previous != PARSE_VKIND_TYPE_NAME;
                }
            }
        }
    }
    while (open != 0)
    {
        kernels->flags[secondary[open - 1]] = 1;
        kernels->unbalanced = true;
        open -= 1;
    }
    // The node depths: +1 at an opener, -1 at a closer, exclusive prefix sums, added to the depth of ( [ {, less one
    // at a closer of any group.
    int8_t deltas[128] = { 0 };
    deltas[PARSE_ROLE_OPEN_TERNARY] = 1;
    deltas[PARSE_ROLE_OPEN_CASE] = 1;
    deltas[PARSE_ROLE_CLOSE_TERNARY] = -1;
    deltas[PARSE_ROLE_CLOSE_CASE] = -1;
    int8_t closers[128] = { 0 };
    for (uint32_t r = 0; r < PARSE_ROLE_COUNT; r += 1)
    {
        closers[r] = (int8_t)(parse_role_deltas[r] < 0);
    }
    const __m512i delta_low = _mm512_loadu_si512(deltas);
    const __m512i delta_high = _mm512_loadu_si512(deltas + 64);
    const __m512i closer_low = _mm512_loadu_si512(closers);
    const __m512i closer_high = _mm512_loadu_si512(closers + 64);
    uint32_t inner = 0;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i role = _mm512_loadu_si512(kernels->roles + block);
        const __m512i delta = _mm512_permutex2var_epi8(delta_low, role, delta_high);
        const __m512i after = parse_prefix_sum8(delta);
        // The secondary depth before each token, less one at a closer.
        const __m512i before = _mm512_sub_epi8(_mm512_sub_epi8(after, delta), _mm512_permutex2var_epi8(closer_low, role, closer_high));
        const __m512i base = _mm512_set1_epi32((int)inner);
        for (uint32_t q = 0; q < 4; q += 1)
        {
            const __m128i quarter = q == 0 ? _mm512_extracti32x4_epi32(before, 0) : q == 1 ? _mm512_extracti32x4_epi32(before, 1) :
                                    q == 2 ? _mm512_extracti32x4_epi32(before, 2) : _mm512_extracti32x4_epi32(before, 3);
            const uint32_t* depths = kernels->depths + block + 16 * q;
            _mm512_storeu_si512(kernels->node_depths + block + 16 * q, _mm512_add_epi32(_mm512_loadu_si512(depths), _mm512_add_epi32(base, _mm512_cvtepi8_epi32(quarter))));
        }
        // The last live lane from the register: a byte reloaded from a store of `after` met that store before its data
        // was ready, in some builds once a block (about 0.1 cycles a token on sqlite3.c, 0.15 on the libc corpus).
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __m512i last = _mm512_permutexvar_epi8(_mm512_set1_epi8((char)(live - 1)), after);
        inner += (uint32_t)(int32_t)(int8_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(last));
    }
#endif
}

// parse_after_colons over the `{` tokens after a resolved colon only.
static void parse_after_colons_avx512(ParseKernels* kernels, uint32_t count)
{
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i previous = _mm512_loadu_si512(kernels->roles + block - 1);
        const __m512i kind = _mm512_loadu_si512(kernels->vkinds + block);
        uint64_t lanes = (parse_equal8(previous, PARSE_ROLE_CLOSE_CASE) | parse_equal8(previous, PARSE_ROLE_LABEL)) & parse_equal8(kind, PARSE_KIND_L_BRACE) &
                         (count - block < 64 ? (((uint64_t)1 << (count - block)) - 1) : ~(uint64_t)0);
        // Token 0 has no colon before it.
        lanes &= block == 0 ? ~(uint64_t)1 : ~(uint64_t)0;
        while (lanes != 0)
        {
            const uint32_t i = block + (uint32_t)__builtin_ctzll(lanes);
            lanes &= lanes - 1;
            kernels->roles[i] = PARSE_ROLE_OPEN_BLOCK;
            kernels->flags[i] = 0;
        }
    }
}

static BUSTER_BAKED_FRONTEND uint8_t parse_statement_classes[128];

static void parse_statement_tables_init(void)
{
    memset(parse_statement_classes, 0, sizeof(parse_statement_classes));
    parse_statement_classes[PARSE_ROLE_TERMINATOR] = BUSTER_PARSE_STATEMENT_TERMINATOR;
    parse_statement_classes[PARSE_ROLE_CLOSE_CONDITION] = BUSTER_PARSE_STATEMENT_HEAD;
    parse_statement_classes[PARSE_ROLE_CLOSE_FOR] = BUSTER_PARSE_STATEMENT_HEAD;
    parse_statement_classes[PARSE_ROLE_CLOSE_CASE] = BUSTER_PARSE_STATEMENT_HEAD;
    parse_statement_classes[PARSE_ROLE_LABEL] = BUSTER_PARSE_STATEMENT_HEAD;
    parse_statement_classes[PARSE_ROLE_DO] = BUSTER_PARSE_STATEMENT_DO;
    parse_statement_classes[PARSE_ROLE_ELSE] = BUSTER_PARSE_STATEMENT_ELSE;
    parse_statement_classes[PARSE_ROLE_OPEN_BLOCK] = BUSTER_PARSE_STATEMENT_OPEN_BLOCK;
    parse_statement_classes[PARSE_ROLE_OPEN_BODY] = BUSTER_PARSE_STATEMENT_OPEN_BODY;
    parse_statement_classes[PARSE_ROLE_CLOSE_BLOCK] = BUSTER_PARSE_STATEMENT_CLOSE;
    parse_statement_classes[PARSE_ROLE_CLOSE_BODY] = BUSTER_PARSE_STATEMENT_CLOSE;
}

// The events of a block from their classes and statement contexts.
BUSTER_INLINE uint64_t parse_statement_events8(__m512i classes, uint64_t context, __mmask64 live_mask)
{
    const uint64_t contextual = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_CONTEXTUAL)) & context;
    const uint64_t always = _mm512_test_epi8_mask(classes, _mm512_set1_epi8((char)BUSTER_PARSE_STATEMENT_ALWAYS));
    return (contextual | always) & live_mask;
}

// The first event after `token`, or BUSTER_PARSE_NONE.
static uint32_t parse_statement_next(const ParseKernels* kernels, uint32_t count, const ParseStatementContext* contexts, uint32_t token)
{
    uint32_t block = token - token % 64;
    uint64_t after = token % 64 == 63 ? 0 : ~(uint64_t)0 << (token % 64 + 1);
    uint64_t events = 0;
    bool searching = true;
    while (searching)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const __m512i classes = parse_table8(parse_statement_classes, _mm512_loadu_si512(kernels->roles + block));
        events = parse_statement_events8(classes, contexts[block / 64].blocks, live_mask) & after;
        after = ~(uint64_t)0;
        searching = events == 0 && block + 64 < count;
        block += searching ? 64 : 0;
    }
    return events != 0 ? block + (uint32_t)__builtin_ctzll(events) : BUSTER_PARSE_NONE;
}

BUSTER_INLINE bool parse_statement_is_head(const ParseKernels* kernels, uint32_t token)
{
    return token != BUSTER_PARSE_NONE && (parse_statement_classes[kernels->roles[token]] & BUSTER_PARSE_STATEMENT_HEADS) != 0;
}

BUSTER_INLINE bool parse_statement_bit(uint64_t mask, uint32_t token)
{
    return ((mask >> (token % 64)) & 1) != 0;
}

// The innermost head of the chain that owned the statement ending at `end`, if `end` is a terminator (the event before
// it) or a block's closer (its owner, which the closer's `previous` holds); it need not be a head, and then the chain
// is empty or `end` ends no statement.
BUSTER_INLINE uint32_t parse_statement_chain(const ParseKernels* kernels, const uint32_t* previous, uint32_t end)
{
    const uint8_t classes = end != BUSTER_PARSE_NONE ? parse_statement_classes[kernels->roles[end]] : 0;
    return (classes & BUSTER_PARSE_STATEMENT_ENDS) != 0 ? previous[end] : BUSTER_PARSE_NONE;
}

// One event the vector part leaves, in order: an else (its if), a do, a terminator after a head's `)` (that head's,
// unless the head is a do-while's while), or a while's `)` after the end of a statement while a do waits (the do's, if
// the chain down from that statement holds one).
static void parse_statement_sparse(ParseKernels* kernels, uint32_t count, const ParseStatementContext* contexts, uint32_t* previous, ParseStatementPass* state,
                                   uint32_t i)
{
    const uint8_t role = kernels->roles[i];
    if (role == PARSE_ROLE_ELSE)
    {
        // The first if or do down the chain from the statement that ended before the else: an if is the else's.
        uint32_t head = parse_statement_chain(kernels, previous, previous[i]);
        bool matched = false;
        bool searching = true;
        while (searching)
        {
            const bool is_head = parse_statement_is_head(kernels, head);
            const bool is_do = is_head && kernels->roles[head] == PARSE_ROLE_DO;
            matched = is_head && parse_statement_bit(contexts[head / 64].ifs, head);
            searching = is_head && !is_do && !matched;
            head = searching ? previous[head] : head;
        }
        if (matched)
        {
            kernels->parents[i] = head;
            // From now on the else stands for its if in the chains.
            previous[i] = previous[head];
        }
        else
        {
            state->else_without_if = true;
        }
    }
    else if (role == PARSE_ROLE_DO)
    {
        state->open_dos += 1;
    }
    else if (role == PARSE_ROLE_TERMINATOR)
    {
        const uint32_t head = previous[i];
        if (head == state->pending_while)
        {
            // A do-while's `;`: the parent of the do and of the while's `)`, and owned by what owned the do. In the
            // chains it stands for the do-statement, whose owner is the do's.
            const uint32_t owner = previous[state->pending_do];
            kernels->parents[state->pending_do] = i;
            kernels->parents[head] = i;
            if (parse_statement_is_head(kernels, owner))
            {
                kernels->parents[i] = owner;
            }
            previous[i] = owner;
            state->pending_while = BUSTER_PARSE_NONE;
        }
        else
        {
            kernels->parents[i] = head;
        }
    }
    else if (state->open_dos != 0)
    {
        // A while after the end of a statement is a do's if the first do down the chain comes before the chain ends.
        uint32_t head = parse_statement_chain(kernels, previous, previous[i]);
        while (parse_statement_is_head(kernels, head) && kernels->roles[head] != PARSE_ROLE_DO)
        {
            head = previous[head];
        }
        if (parse_statement_is_head(kernels, head))
        {
            // Its `;` must come next: the next event, and the token right after the `)`.
            const uint32_t next = parse_statement_next(kernels, count, contexts, i);
            state->twin |= next == BUSTER_PARSE_NONE || next != i + 1 || kernels->roles[next] != PARSE_ROLE_TERMINATOR;
            state->pending_while = i;
            state->pending_do = head;
            state->open_dos -= 1;
        }
    }
}

// A block's openers and closers of blocks in order, without a branch on which: an opener puts its statement's owner on
// the stack (for a lane of `owned`, the event before it: before it in `events`, else `carried`, or `positions[lane]`
// in a block that interrupts a level; else BUSTER_PARSE_NONE), and its closer takes it as its parent, and as its
// `previous`, where the chains find the chain its statement ended. Each one's slot is the depth before the block and
// the openers and closers before it, so the iterations chain only through the stack. The stores a lane doesn't make go
// to `sink`, a slot of a live array that nothing reads, which keeps the compiler from turning them back into branches.
// Returns the depth after the block.
static uint32_t parse_statement_brackets(uint64_t opens, uint64_t closes, uint64_t owned, uint64_t events, uint32_t carried, const uint32_t* positions,
                                         uint32_t block, uint32_t depth, uint32_t* owners, uint32_t* parents, uint32_t* previous, uint32_t* sink)
{
    uint64_t brackets = opens | closes;
    while (brackets != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(brackets);
        brackets &= brackets - 1;
        const uint64_t bit = (uint64_t)1 << lane;
        const uint64_t below = bit - 1;
        const bool open = (opens & bit) != 0;
        const int64_t before = (int64_t)depth + __builtin_popcountll(opens & below) - __builtin_popcountll(closes & below);
        // A closer with nothing open (an unbalanced file, which the fallback takes whole) leaves the stack alone.
        const bool pop = !open && before > 0;
        const int64_t raw = before - (open ? 0 : 1);
        const uint32_t slot = (uint32_t)(raw & ~(raw >> 63));
        const uint32_t popped = owners[slot];
        const uint64_t earlier = events & below;
        const uint32_t last = block + 63 - (uint32_t)__builtin_clzll(earlier | 1);
        const uint32_t previous_event = positions != 0 ? positions[lane] : __builtin_unpredictable(earlier != 0) ? last : carried;
        const uint32_t owner = __builtin_unpredictable((owned & bit) != 0) ? previous_event : BUSTER_PARSE_NONE;
        uint32_t* push = __builtin_unpredictable(open) ? &owners[slot] : sink;
        *push = owner;
        uint32_t* parent = __builtin_unpredictable(pop && popped != BUSTER_PARSE_NONE) ? &parents[block + lane] : sink;
        *parent = popped;
        uint32_t* chain = __builtin_unpredictable(open) ? sink : &previous[block + lane];
        *chain = pop ? popped : BUSTER_PARSE_NONE;
    }
    const int64_t after = (int64_t)depth + __builtin_popcountll(opens) - __builtin_popcountll(closes);
    return after > 0 ? (uint32_t)after : 0;
}

// One block of K5 in AVX-512 with events (see above), whose classes and statement contexts are `classes` and `context`.
static void parse_statement_block(ParseKernels* kernels, uint32_t count, const ParseStatementContext* contexts, uint32_t* previous, ParseStatementPass* state,
                                  ParseStatementCounts* counts, uint32_t block, __m512i classes, ParseStatementContext context, uint64_t events)
{
    const uint64_t open_blocks = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_OPEN_BLOCK)) & events;
    const uint64_t opens = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_OPEN_BLOCK | BUSTER_PARSE_STATEMENT_OPEN_BODY)) & events;
    const uint64_t closes = _mm512_test_epi8_mask(classes, _mm512_set1_epi8((char)BUSTER_PARSE_STATEMENT_CLOSE)) & events;
    // Whether the block interrupts a level: it holds the opener of a block that isn't a statement inside another block,
    // or the closer of one.
    uint64_t candidates = opens & ~(open_blocks & context.blocks);
    bool interrupting = state->level_count != 0 && state->levels[state->level_count - 1].closer < block + 64;
    while (candidates != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(candidates);
        candidates &= candidates - 1;
        const uint64_t below = ((uint64_t)1 << lane) - 1;
        interrupting |= (int64_t)state->block_depth + __builtin_popcountll(opens & below) - __builtin_popcountll(closes & below) > 0;
    }
    const uint32_t carried = state->last;
    uint32_t positions[64];
    __m512i previous_positions[4];
    __m512i previous_classes;
    if (interrupting)
    {
        // One event at a time, each level's last event kept while a block that isn't a statement interrupts it.
        counts->interrupted_blocks += 1;
        uint8_t class_bytes[64];
        uint8_t previous_bytes[64];
        _mm512_storeu_si512(class_bytes, classes);
        uint32_t depth = state->block_depth;
        uint64_t lanes = events;
        while (lanes != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(lanes);
            lanes &= lanes - 1;
            const uint32_t i = block + lane;
            positions[lane] = state->last;
            previous_bytes[lane] = state->last_classes;
            const ParseStatementLevel* top = state->level_count != 0 ? &state->levels[state->level_count - 1] : 0;
            const bool open = (class_bytes[lane] & (BUSTER_PARSE_STATEMENT_OPEN_BLOCK | BUSTER_PARSE_STATEMENT_OPEN_BODY)) != 0;
            const bool statement = (class_bytes[lane] & BUSTER_PARSE_STATEMENT_OPEN_BLOCK) != 0 && parse_statement_bit(context.blocks, i);
            if (top != 0 && top->closer == i)
            {
                state->last = top->last;
                state->last_classes = top->last_classes;
                state->level_count -= 1;
            }
            else
            {
                if (open && !statement && depth != 0)
                {
                    // Its closer is the opener's parent (K4).
                    state->levels[state->level_count] = (ParseStatementLevel){ .last = state->last, .closer = kernels->parents[i], .last_classes = state->last_classes };
                    state->level_count += 1;
                }
                state->last = i;
                state->last_classes = class_bytes[lane];
            }
            depth += open ? 1 : 0;
            depth -= (class_bytes[lane] & BUSTER_PARSE_STATEMENT_CLOSE) != 0 && depth != 0 ? 1 : 0;
        }
        for (uint32_t q = 0; q < 4; q += 1)
        {
            previous_positions[q] = _mm512_maskz_loadu_epi32((__mmask16)(events >> (16 * q)), positions + 16 * q);
        }
        previous_classes = _mm512_maskz_loadu_epi8(events, previous_bytes);
    }
    else
    {
        // Each event's previous event: the event lanes compressed, shifted up a lane and expanded back; the first one's is
        // the event carried from the blocks before.
        const __m512i dense = _mm512_maskz_compress_epi8(events, parse_shifts.lanes8);
        const __m512i before = _mm512_maskz_expand_epi8(events, _mm512_permutex2var_epi8(dense, parse_shifts.up8[0], _mm512_set1_epi8((char)0xFF)));
        const uint64_t first = events & (0 - events);
        previous_classes = _mm512_mask_mov_epi8(_mm512_permutexvar_epi8(before, classes), first, _mm512_set1_epi8((char)state->last_classes));
        const __m512i block_vector = _mm512_set1_epi32((int)block);
        const __m512i carried_vector = _mm512_set1_epi32((int)carried);
        for (uint32_t q = 0; q < 4; q += 1)
        {
            const __m128i quarter = q == 0 ? _mm512_extracti32x4_epi32(before, 0) : q == 1 ? _mm512_extracti32x4_epi32(before, 1) :
                                    q == 2 ? _mm512_extracti32x4_epi32(before, 2) : _mm512_extracti32x4_epi32(before, 3);
            previous_positions[q] = _mm512_mask_mov_epi32(_mm512_add_epi32(block_vector, _mm512_cvtepu8_epi32(quarter)), (__mmask16)(first >> (16 * q)), carried_vector);
        }
        const uint32_t last = 63 - (uint32_t)__builtin_clzll(events);
        state->last = block + last;
        state->last_classes = (uint8_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_set1_epi8((char)last), classes)));
    }
    const uint64_t after_head = _mm512_test_epi8_mask(previous_classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_HEADS)) & events;
    const uint64_t owned_blocks = open_blocks & context.blocks & after_head;
    const uint64_t dos = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_DO)) & events;
    uint64_t sparse = (_mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_ELSE)) & events) | dos;
    uint64_t deferred = 0;
    if (state->open_dos != 0 || state->sparse_dos || dos != 0)
    {
        // While a do may wait for its while: a while's `)` after the end of a statement may be its, and a terminator
        // after a while's `)` its `;`. The sparse loop decides.
        deferred = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_TERMINATOR)) & events &
                   _mm512_test_epi8_mask(previous_classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_WHILE));
        sparse |= deferred | (_mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_WHILE)) & events &
                              _mm512_test_epi8_mask(previous_classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_ENDS)));
    }
    const uint64_t owned = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_OWNED)) & after_head & ~deferred;
    for (uint32_t q = 0; q < 4; q += 1)
    {
        _mm512_mask_storeu_epi32(kernels->parents + block + 16 * q, (__mmask16)(owned >> (16 * q)), previous_positions[q]);
        _mm512_mask_storeu_epi32(previous + block + 16 * q, (__mmask16)(events >> (16 * q)), previous_positions[q]);
    }
    // The blocks' openers and closers, an opener's owner the event before it.
    state->block_depth = parse_statement_brackets(opens, closes, owned_blocks, events, carried, interrupting ? positions : 0, block, state->block_depth, state->owners,
                                                  kernels->parents, previous, state->sink);
    // The block before's sparse events, then this block's later: a block behind, the loads of `previous` find the
    // stores to it done.
    uint64_t behind = state->sparse;
    while (behind != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(behind);
        behind &= behind - 1;
        parse_statement_sparse(kernels, count, contexts, previous, state, state->sparse_block + lane);
    }
    counts->sparse += (uint64_t)__builtin_popcountll(sparse);
    state->sparse_block = block;
    state->sparse = sparse;
    state->sparse_dos = dos != 0;
}

// K5 in AVX-512 (see above), from each block's statement contexts (K2's), with `previous` a slot per token for each
// event's previous event in its level, `owners` count + 2 slots (one per block open, and the sink), and `levels` room
// for the levels interrupted, one per token at most. Writes the parents parse_statements writes when it returns
// PARSE_STATEMENTS_OK; when the twin would return false it returns PARSE_STATEMENTS_ELSE_WITHOUT_IF, and then, as for
// the twin, the parents don't matter: the fallback takes the file.
static ParseStatementsVerdict parse_statements_avx512(ParseKernels* kernels, uint32_t count, const ParseStatementContext* contexts, uint32_t* previous,
                                                      uint32_t* owners, ParseStatementLevel* levels, ParseStatementCounts* counts)
{
    ParseStatementPass state = {
        .last = BUSTER_PARSE_NONE,
        .levels = levels,
        .level_count = 0,
        .block_depth = 0,
        .owners = owners,
        .sink = owners + count + 1,
        .open_dos = 0,
        .pending_while = BUSTER_PARSE_NONE,
        .pending_do = BUSTER_PARSE_NONE,
        .sparse_block = 0,
        .sparse = 0,
        .sparse_dos = false,
        .last_classes = 0,
        .else_without_if = false,
        .twin = false,
    };
    for (uint32_t block = 0; block < count; block += 64)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const ParseStatementContext context = contexts[block / 64];
        const __m512i table_classes = parse_table8(parse_statement_classes, _mm512_loadu_si512(kernels->roles + block));
        const uint64_t events = parse_statement_events8(table_classes, context.blocks, live_mask);
        if (events != 0)
        {
            const uint64_t heads = _mm512_test_epi8_mask(table_classes, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_HEAD));
            const __m512i classes = _mm512_mask_mov_epi8(table_classes, heads & context.whiles, _mm512_set1_epi8(BUSTER_PARSE_STATEMENT_WHILE));
            counts->blocks += 1;
            parse_statement_block(kernels, count, contexts, previous, &state, counts, block, classes, context, events);
        }
    }
    while (state.sparse != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(state.sparse);
        state.sparse &= state.sparse - 1;
        parse_statement_sparse(kernels, count, contexts, previous, &state, state.sparse_block + lane);
    }
    const bool twin = state.twin || state.open_dos != 0 || state.pending_while != BUSTER_PARSE_NONE;
    return twin ? PARSE_STATEMENTS_TWIN : state.else_without_if ? PARSE_STATEMENTS_ELSE_WITHOUT_IF : PARSE_STATEMENTS_OK;
}

// The nearest next lane whose key is below the lane's threshold, in byte lanes: the lane index, or 64 or more if none.
BUSTER_INLINE __m512i parse_next_smaller8(__m512i keys, __m512i thresholds)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    const __m512i one = _mm512_set1_epi8(1);
    __m512i windows[6];
    windows[0] = keys;
    for (uint32_t k = 1; k < 6; k += 1)
    {
        windows[k] = _mm512_min_epu8(windows[k - 1], parse_down8(windows[k - 1], k - 1, fill));
    }
    __m512i p = _mm512_add_epi8(parse_shifts.lanes8, one);
    for (int32_t k = 5; k >= 0; k -= 1)
    {
        const __m512i v = _mm512_permutexvar_epi8(p, windows[k]);
        const __m512i below = _mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one);
        const __m512i step = _mm512_set1_epi8((char)(1 << k));
        // p stays below 128: it starts at 64 or less and grows by at most 63.
        p = _mm512_add_epi8(p, _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// A block's node depths relative to `base`, as bytes, and the block's lowest and highest node depths.
BUSTER_INLINE __m512i parse_relative_depths8(const uint32_t* node_depths, uint32_t block, __mmask64 live_mask, uint32_t* lowest, uint32_t* highest)
{
    __m512i low = _mm512_set1_epi32(-1);
    __m512i high = _mm512_setzero_si512();
    __m512i depth[4];
    for (uint32_t q = 0; q < 4; q += 1)
    {
        const __mmask16 live = (__mmask16)(live_mask >> (16 * q));
        depth[q] = _mm512_loadu_si512(node_depths + block + 16 * q);
        low = _mm512_mask_min_epu32(low, live, low, depth[q]);
        high = _mm512_mask_max_epu32(high, live, high, depth[q]);
    }
    *lowest = _mm512_reduce_min_epu32(low);
    *highest = _mm512_reduce_max_epu32(high);
    const __m512i base = _mm512_set1_epi32((int)*lowest);
    __m512i result = _mm512_setzero_si512();
    result = _mm512_inserti32x4(result, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depth[0], base)), 0);
    result = _mm512_inserti32x4(result, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depth[1], base)), 1);
    result = _mm512_inserti32x4(result, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depth[2], base)), 2);
    result = _mm512_inserti32x4(result, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depth[3], base)), 3);
    return result;
}

BUSTER_INLINE ParseKeys8 parse_block_keys8(const uint8_t* roles, __m512i relative, uint32_t block)
{
    const __m512i role = _mm512_loadu_si512(roles + block);
    // relative < 8, so a 16-bit shift by 5 moves each byte within itself.
    const __m512i base = _mm512_slli_epi16(relative, 5);
    ParseKeys8 result = {
        .keys = _mm512_add_epi8(base, parse_lookup8(parse_parent_tables.levels, role)),
        .left = _mm512_add_epi8(base, parse_lookup8(parse_parent_tables.left_thresholds, role)),
        .right = _mm512_add_epi8(base, parse_lookup8(parse_parent_tables.right_thresholds, role)),
        .rules = parse_lookup8(parse_parent_tables.rules, role),
    };
    return result;
}

BUSTER_INLINE void parse_top_reset(ParseTop* top)
{
    top->keys = _mm512_setzero_si512();
    top->low = _mm512_set1_epi16(-1);
    top->high = _mm512_set1_epi16(-1);
    top->count = 0;
    top->held = 0;
}

// The window's first `reach` candidates from memory (the rest as below the bottom), for a stack that holds `count`.
BUSTER_INLINE void parse_top_reload(ParseTop* top, const ParseSlots* slots, uint32_t reach)
{
    const __mmask32 lanes = reach == BUSTER_PARSE_TOP_LANES ? ~(__mmask32)0 : (((__mmask32)1 << reach) - 1);
    const uint32_t slot = slots->capacity - top->count;
    top->keys = _mm512_maskz_loadu_epi16(lanes, slots->keys + slot);
    top->low = _mm512_mask_loadu_epi16(_mm512_set1_epi16(-1), lanes, slots->low + slot);
    top->high = _mm512_mask_loadu_epi16(_mm512_set1_epi16(-1), lanes, slots->high + slot);
    top->held = top->count < reach ? top->count : reach;
}

// How many of the `below` candidates under the window a minimum hides: those whose key isn't below it, a run from the
// nearest, counted 32 at a time. The padding's key 0 ends the run.
static uint32_t parse_slots_hidden(const ParseSlots* slots, uint32_t below, uint16_t minimum)
{
    uint32_t hidden = 0;
    bool popping = true;
    while (popping)
    {
        const __m512i keys = _mm512_loadu_si512(slots->keys + slots->capacity - below + hidden);
        const uint32_t run = (uint32_t)__builtin_popcount(_mm512_cmpge_epu16_mask(keys, _mm512_set1_epi16((short)minimum)));
        hidden += run;
        popping = run == 32 && hidden < below;
    }
    return hidden < below ? hidden : below;
}

// The nearest of the stack's candidates below `skip`, the window's, whose key is below `threshold`: a binary search, as
// keys decrease from the nearest. Its position, or BUSTER_PARSE_NONE with key 0.
static uint32_t parse_slots_search(const ParseSlots* slots, uint32_t count, uint32_t skip, uint32_t threshold, uint16_t* key)
{
    uint32_t low = slots->capacity - count + skip;
    uint32_t high = slots->capacity;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (slots->keys[middle] < threshold)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    const bool found = low < slots->capacity;
    *key = found ? slots->keys[low] : 0;
    return found ? ((uint32_t)slots->high[low] << 16) | slots->low[low] : BUSTER_PARSE_NONE;
}

// The nearest window candidate below each lane's threshold: the first of the window's 32 keys (relative to the block,
// saturated to bytes, nearest first, so non-increasing; lanes 32 to 63 zero) below it, 0 to 31, or 32 if none is. A
// binary search for how many lead it at or above the threshold: steps of 16, 8, 4, 2, 1 and 1.
BUSTER_INLINE __m512i parse_top_search8(__m512i window, __m512i thresholds)
{
    const __m512i one = _mm512_set1_epi8(1);
    __m512i c = _mm512_setzero_si512();
    for (int32_t k = 4; k >= -1; k -= 1)
    {
        const uint32_t step = k >= 0 ? 1u << k : 1u;
        const __m512i v = _mm512_permutexvar_epi8(_mm512_add_epi8(c, _mm512_set1_epi8((char)(step - 1))), window);
        // 0xFF where v is at or above the threshold, so the search moves past it.
        const __m512i past = _mm512_sub_epi8(_mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one), one);
        c = _mm512_add_epi8(c, _mm512_and_si512(past, _mm512_set1_epi8((char)step)));
    }
    return c;
}

// The lanes' candidates from memory for the lanes in `deep`, whose candidate is under the window (rare: no lane of
// sqlite3 or the libc corpus has one), into a block's candidates as 16-bit keys and 32-bit positions.
static void parse_top_deep(const ParseSlots* slots, uint32_t count, uint32_t reach, __mmask64 deep, __m512i thresholds, uint32_t base, uint32_t* positions,
                           uint16_t* keys)
{
    uint8_t threshold_bytes[64];
    _mm512_storeu_si512(threshold_bytes, thresholds);
    while (deep != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(deep);
        deep &= deep - 1;
        positions[lane] = parse_slots_search(slots, count, reach, base * BUSTER_PARSE_LEVELS + threshold_bytes[lane], &keys[lane]);
    }
}

// One block's (or part's) window work, after its in-block search: the lanes' candidates (in the block where p is below
// 64, else in the window, else in memory under it), then the pop and the push. `block_keys` are its keys relative to
// base * 32 (dead lanes 0xFF), `p` and `thresholds` its search's, and `push_keys` and `push_lanes` the keys and block
// lanes from which `minima` picks its candidates for later blocks, nearest first; `minimum` holds the block's least key
// in every lane. `reach` is the window's lanes: BUSTER_PARSE_TOP_LANES, or fewer in the check of the paths through
// memory.
BUSTER_INLINE ParseFound8 parse_top_block8(ParseTop* top, const ParseSlots* slots, uint32_t reach, __m512i block_keys, __m512i thresholds, __m512i p,
                                           __m512i push_keys, __m512i push_lanes, __mmask64 minima, __m512i minimum, uint32_t block, uint32_t base, __mmask64 live)
{
    const __m512i ones = _mm512_set1_epi16(-1);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i offset = _mm512_set1_epi16((short)(base * BUSTER_PARSE_LEVELS));
    // The window's keys relative to the block, saturated: a key below the block's lowest depth is below every key in it,
    // and one too far above it above every threshold.
    const __m512i window = _mm512_zextsi256_si512(_mm512_cvtusepi16_epi8(_mm512_subs_epu16(top->keys, offset)));
    const __m512i c = parse_top_search8(window, thresholds);
    const __mmask64 found = _mm512_cmplt_epu8_mask(p, _mm512_set1_epi8(64));
    ParseFound8 result;
    // Keys: the block's from its key bytes, the window's from its keys (index 32 picks the second table's 0).
    const __m512i inside_keys = _mm512_permutexvar_epi8(p, block_keys);
    for (uint32_t h = 0; h < 2; h += 1)
    {
        const __m512i inside = _mm512_add_epi16(offset, _mm512_maskz_permutexvar_epi8(BUSTER_PARSE_WORD_BYTES, parse_shifts.words[h], inside_keys));
        const __m512i outside = _mm512_permutex2var_epi16(top->keys, _mm512_maskz_permutexvar_epi8(BUSTER_PARSE_WORD_BYTES, parse_shifts.words[h], c), zero);
        result.keys[h] = _mm512_mask_mov_epi16(outside, (__mmask32)(found >> (32 * h)), inside);
    }
    // Positions: the block's are block + lane; the window's from their halves, put together as 32-bit lanes by unpacking
    // in an order that leaves them in lane order.
    const __m512i block_vector = _mm512_set1_epi32((int)block);
    for (uint32_t h = 0; h < 2; h += 1)
    {
        const __m512i index = _mm512_maskz_permutexvar_epi8(BUSTER_PARSE_WORD_BYTES, parse_shifts.unpacked[h], c);
        const __m512i low = _mm512_permutex2var_epi16(top->low, index, ones);
        const __m512i high = _mm512_permutex2var_epi16(top->high, index, ones);
        const __m512i outside[2] = {
            _mm512_unpacklo_epi16(low, high),
            _mm512_unpackhi_epi16(low, high),
        };
        for (uint32_t q = 0; q < 2; q += 1)
        {
            const __m512i inside = _mm512_add_epi32(block_vector, _mm512_maskz_permutexvar_epi8(BUSTER_PARSE_DWORD_BYTES, parse_shifts.dwords[2 * h + q], p));
            result.positions[2 * h + q] = _mm512_mask_mov_epi32(outside[q], (__mmask16)(found >> (32 * h + 16 * q)), inside);
        }
    }
    // Lanes whose candidate is under the window: before the push, which may write over the slots they read.
    const __mmask64 deep = top->count > reach ? _mm512_mask_cmpge_epu8_mask(live & ~found, c, _mm512_set1_epi8((char)reach)) : 0;
    if (deep != 0)
    {
        uint32_t position_lanes[64];
        uint16_t key_lanes[64];
        for (uint32_t q = 0; q < 4; q += 1)
        {
            _mm512_storeu_si512(position_lanes + 16 * q, result.positions[q]);
        }
        _mm512_storeu_si512(key_lanes, result.keys[0]);
        _mm512_storeu_si512(key_lanes + 32, result.keys[1]);
        parse_top_deep(slots, top->count, reach, deep, thresholds, base, position_lanes, key_lanes);
        for (uint32_t q = 0; q < 4; q += 1)
        {
            result.positions[q] = _mm512_loadu_si512(position_lanes + 16 * q);
        }
        result.keys[0] = _mm512_loadu_si512(key_lanes);
        result.keys[1] = _mm512_loadu_si512(key_lanes + 32);
    }
    // The pop: the window's candidates whose key isn't below the block's minimum are hidden, nearest first; the padding's
    // key 0 is below every real key.
    const __mmask32 kept = _mm256_cmplt_epu8_mask(_mm512_castsi512_si256(window), _mm512_castsi512_si256(minimum));
    const uint32_t hidden = (uint32_t)__builtin_popcount(~kept);
    uint32_t popped = hidden;
    if (hidden == top->held && top->count > top->held)
    {
        // The window emptied: the candidates under it may be hidden too.
        const uint16_t least = (uint16_t)(base * BUSTER_PARSE_LEVELS + (uint32_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(minimum)) % 256);
        popped += parse_slots_hidden(slots, top->count - top->held, least);
    }
    // The push: the block's candidates, nearest first, into memory and onto the window.
    const uint32_t n = (uint32_t)__builtin_popcountll(minima);
    const __m512i new_keys = _mm512_maskz_compress_epi8(minima, push_keys);
    const __m512i new_lanes = _mm512_maskz_compress_epi8(minima, push_lanes);
    const __m512i keys = _mm512_add_epi16(offset, _mm512_cvtepu8_epi16(_mm512_castsi512_si256(new_keys)));
    const __m512i low = _mm512_add_epi16(_mm512_set1_epi16((short)(block & 0xFFFF)), _mm512_cvtepu8_epi16(_mm512_castsi512_si256(new_lanes)));
    const __m512i high = _mm512_set1_epi16((short)(block >> 16));
    const uint32_t count = top->count - popped + n;
    const uint32_t slot = slots->capacity - count;
    const __mmask32 stored = n >= 32 ? ~(__mmask32)0 : (((__mmask32)1 << n) - 1);
    _mm512_mask_storeu_epi16(slots->keys + slot, stored, keys);
    _mm512_mask_storeu_epi16(slots->low + slot, stored, low);
    _mm512_mask_storeu_epi16(slots->high + slot, stored, high);
    if (n > 32)
    {
        const __mmask32 rest = ((__mmask32)1 << (n - 32)) - 1;
        _mm512_mask_storeu_epi16(slots->keys + slot + 32, rest, _mm512_add_epi16(offset, _mm512_cvtepu8_epi16(_mm512_extracti64x4_epi64(new_keys, 1))));
        _mm512_mask_storeu_epi16(slots->low + slot + 32, rest,
                                 _mm512_add_epi16(_mm512_set1_epi16((short)(block & 0xFFFF)), _mm512_cvtepu8_epi16(_mm512_extracti64x4_epi64(new_lanes, 1))));
        _mm512_mask_storeu_epi16(slots->high + slot + 32, rest, high);
    }
    // The kept candidates move up to the window's start, then down past the pushed ones.
    const __mmask32 after = n >= 32 ? 0 : ~(((__mmask32)1 << n) - 1);
    const __mmask32 reached = reach == BUSTER_PARSE_TOP_LANES ? ~(__mmask32)0 : (((__mmask32)1 << reach) - 1);
    top->keys = _mm512_maskz_mov_epi16(reached, _mm512_mask_expand_epi16(keys, after, _mm512_maskz_compress_epi16(kept, top->keys)));
    top->low = _mm512_mask_mov_epi16(ones, reached, _mm512_mask_expand_epi16(low, after, _mm512_mask_compress_epi16(ones, kept, top->low)));
    top->high = _mm512_mask_mov_epi16(ones, reached, _mm512_mask_expand_epi16(high, after, _mm512_mask_compress_epi16(ones, kept, top->high)));
    const uint32_t held = top->held - hidden + n;
    top->count = count;
    top->held = held < reach ? held : reach;
    if (top->held < reach && top->count > top->held)
    {
        parse_top_reload(top, slots, reach);
    }
    return result;
}

// parse_previous_smaller8's search on its table: a lane's nearest previous lane below its threshold, or a lane of 64 or
// more (bit 7 set) if none is.
BUSTER_INLINE __m512i parse_search_up8(const __m512i* windows, __m512i thresholds)
{
    const __m512i one = _mm512_set1_epi8(1);
    __m512i p = _mm512_sub_epi8(parse_shifts.lanes8, one);
    for (int32_t k = 5; k >= 0; k -= 1)
    {
        const __m512i v = _mm512_permutexvar_epi8(p, windows[k]);
        const __m512i below = _mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one);
        const __m512i step = _mm512_set1_epi8((char)(1 << k));
        p = _mm512_sub_epi8(p, _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// parse_next_smaller8's: the nearest next lane below the threshold, or 64 or more.
BUSTER_INLINE __m512i parse_search_down8(const __m512i* windows, __m512i thresholds)
{
    const __m512i one = _mm512_set1_epi8(1);
    __m512i p = _mm512_add_epi8(parse_shifts.lanes8, one);
    for (int32_t k = 5; k >= 0; k -= 1)
    {
        const __m512i v = _mm512_permutexvar_epi8(p, windows[k]);
        const __m512i below = _mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one);
        const __m512i step = _mm512_set1_epi8((char)(1 << k));
        p = _mm512_add_epi8(p, _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// Every block's in-block work for both passes. `works` has room for a group of BUSTER_PARSE_PREPARED_TOGETHER blocks
// past the last, and the token arrays BUSTER_PARSE_TOKEN_PADDING tokens of padding, which the last group may read.
static void parse_top_prepare8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, ParseBlockWork* works)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    for (uint32_t first = 0; first < count; first += 64 * BUSTER_PARSE_PREPARED_TOGETHER)
    {
        __mmask64 live_masks[BUSTER_PARSE_PREPARED_TOGETHER];
        __m512i relative[BUSTER_PARSE_PREPARED_TOGETHER];
        uint32_t lowest[BUSTER_PARSE_PREPARED_TOGETHER];
        uint32_t highest[BUSTER_PARSE_PREPARED_TOGETHER];
        ParseKeys8 keys[BUSTER_PARSE_PREPARED_TOGETHER];
        __m512i block_keys[BUSTER_PARSE_PREPARED_TOGETHER];
        __m512i up[BUSTER_PARSE_PREPARED_TOGETHER][6];
        __m512i down[BUSTER_PARSE_PREPARED_TOGETHER][6];
        for (uint32_t b = 0; b < BUSTER_PARSE_PREPARED_TOGETHER; b += 1)
        {
            const uint32_t block = first + 64 * b;
            // A block past the end has no live lane.
            const uint32_t live = block >= count ? 0 : count - block < 64 ? count - block : 64;
            live_masks[b] = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
            relative[b] = _mm512_maskz_mov_epi8(live_masks[b], parse_relative_depths8(node_depths, block, live_masks[b], &lowest[b], &highest[b]));
        }
        for (uint32_t b = 0; b < BUSTER_PARSE_PREPARED_TOGETHER; b += 1)
        {
            keys[b] = parse_block_keys8(roles, relative[b], first + 64 * b);
            block_keys[b] = _mm512_mask_mov_epi8(fill, live_masks[b], keys[b].keys);
            up[b][0] = block_keys[b];
            down[b][0] = block_keys[b];
        }
        for (uint32_t k = 1; k < 6; k += 1)
        {
            for (uint32_t b = 0; b < BUSTER_PARSE_PREPARED_TOGETHER; b += 1)
            {
                up[b][k] = _mm512_min_epu8(up[b][k - 1], parse_up8(up[b][k - 1], k - 1, fill));
                down[b][k] = _mm512_min_epu8(down[b][k - 1], parse_down8(down[b][k - 1], k - 1, fill));
            }
        }
        __m512i lefts[BUSTER_PARSE_PREPARED_TOGETHER];
        __m512i rights[BUSTER_PARSE_PREPARED_TOGETHER];
        const __m512i one = _mm512_set1_epi8(1);
        for (uint32_t b = 0; b < BUSTER_PARSE_PREPARED_TOGETHER; b += 1)
        {
            lefts[b] = _mm512_sub_epi8(parse_shifts.lanes8, one);
            rights[b] = _mm512_add_epi8(parse_shifts.lanes8, one);
        }
        // The searches of parse_search_up8 and parse_search_down8, step by step.
        for (int32_t k = 5; k >= 0; k -= 1)
        {
            const __m512i step = _mm512_set1_epi8((char)(1 << k));
            for (uint32_t b = 0; b < BUSTER_PARSE_PREPARED_TOGETHER; b += 1)
            {
                const __m512i left = _mm512_permutexvar_epi8(lefts[b], up[b][k]);
                const __m512i right = _mm512_permutexvar_epi8(rights[b], down[b][k]);
                const __m512i left_below = _mm512_min_epu8(_mm512_subs_epu8(keys[b].left, left), one);
                const __m512i right_below = _mm512_min_epu8(_mm512_subs_epu8(keys[b].right, right), one);
                lefts[b] = _mm512_sub_epi8(lefts[b], _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), left_below), step));
                rights[b] = _mm512_add_epi8(rights[b], _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), right_below), step));
            }
        }
        for (uint32_t b = 0; b < BUSTER_PARSE_PREPARED_TOGETHER; b += 1)
        {
            ParseBlockWork* work = &works[first / 64 + b];
            // A block that doesn't fit gets the rest too, unused.
            work->base = highest[b] - lowest[b] <= BUSTER_PARSE_BYTE_DEPTH_RANGE ? lowest[b] : BUSTER_PARSE_NONE;
            // A candidate is below every key between it and the block's end on its pass's side.
            const __m512i prefix = _mm512_min_epu8(up[b][5], parse_up8(up[b][5], 5, fill));
            const __m512i suffix = _mm512_min_epu8(down[b][5], parse_down8(down[b][5], 5, fill));
            const __mmask64 suffix_minima = _mm512_cmplt_epu8_mask(block_keys[b], parse_down8(suffix, 0, fill));
            work->right_minima = _mm512_cmplt_epu8_mask(block_keys[b], parse_up8(prefix, 0, fill));
            work->left_minima = _mm512_movepi8_mask(_mm512_permutexvar_epi8(parse_shifts.reverse8, _mm512_movm_epi8(suffix_minima)));
            work->minimum = (uint8_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(suffix));
            _mm512_store_si512(work->keys, block_keys[b]);
            _mm512_store_si512(work->left_thresholds, keys[b].left);
            _mm512_store_si512(work->right_thresholds, keys[b].right);
            _mm512_store_si512(work->lefts, lefts[b]);
            _mm512_store_si512(work->rights, rights[b]);
        }
    }
}

// The prefix minima of byte lanes: lane i gets the least of lanes 0 to i.
BUSTER_INLINE __m512i parse_prefix_minima8(__m512i x)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    for (uint32_t k = 0; k < 6; k += 1)
    {
        x = _mm512_min_epu8(x, parse_up8(x, k, fill));
    }
    return x;
}

// A block's L and L's key into the left pass's outputs; `partial` stores only the live lanes.
BUSTER_INLINE void parse_top_store_lefts8(const ParseFound8* found, uint32_t block, __mmask64 live, bool partial, uint32_t* lefts, uint16_t* left_keys)
{
    for (uint32_t h = 0; h < 2; h += 1)
    {
        if (partial)
        {
            _mm512_mask_storeu_epi16(left_keys + block + 32 * h, (__mmask32)(live >> (32 * h)), found->keys[h]);
        }
        else
        {
            _mm512_storeu_si512(left_keys + block + 32 * h, found->keys[h]);
        }
    }
    for (uint32_t q = 0; q < 4; q += 1)
    {
        if (partial)
        {
            _mm512_mask_storeu_epi32(lefts + block + 16 * q, (__mmask16)(live >> (16 * q)), found->positions[q]);
        }
        else
        {
            _mm512_storeu_si512(lefts + block + 16 * q, found->positions[q]);
        }
    }
}

// One block or part (the live lanes) of the left pass: L by parse_previous_smaller8 in the block, else from the
// window. Its candidates for later blocks are its suffix minima, which in the reversed block are its prefix minima,
// nearest first.
BUSTER_INLINE void parse_top_left8_block(ParseTop* top, const ParseSlots* slots, uint32_t reach, const uint8_t* roles, __m512i relative, uint32_t base, uint32_t block,
                                         __mmask64 live, bool partial, uint32_t* lefts, uint16_t* left_keys)
{
    const ParseKeys8 keys = parse_block_keys8(roles, relative, block);
    const __m512i block_keys = _mm512_mask_mov_epi8(_mm512_set1_epi8((char)0xFF), live, keys.keys);
    const __m512i p = parse_previous_smaller8(block_keys, keys.left);
    const __m512i reversed = _mm512_permutexvar_epi8(parse_shifts.reverse8, block_keys);
    const __m512i prefix = parse_prefix_minima8(reversed);
    // A lane is a candidate if its key is below every key nearer the block's end; dead lanes (0xFF) never are.
    const __mmask64 minima = _mm512_cmplt_epu8_mask(reversed, parse_up8(prefix, 0, _mm512_set1_epi8((char)0xFF)));
    const __m512i minimum = _mm512_permutexvar_epi8(parse_shifts.last8, prefix);
    const ParseFound8 found = parse_top_block8(top, slots, reach, block_keys, keys.left, p, reversed, parse_shifts.reverse8, minima, minimum, block, base, live);
    parse_top_store_lefts8(&found, block, live, partial, lefts, left_keys);
}

// A part of a block, lanes [start, start + size): the live lanes among them, and their depths relative to the part's
// lowest, which it returns in *base; *fits says whether they span at most BUSTER_PARSE_BYTE_DEPTH_RANGE.
BUSTER_INLINE __mmask64 parse_top_part(const uint32_t* node_depths, uint32_t block, __mmask64 live, uint32_t start, uint32_t size, __m512i* relative, uint32_t* base,
                                       bool* fits)
{
    const __mmask64 lanes = (size == 64 ? ~(__mmask64)0 : ((((__mmask64)1 << size) - 1) << start)) & live;
    uint32_t highest;
    // Dead lanes' depths are 0, so that parse_block_keys8's 16-bit shift never carries a byte into a live neighbor.
    *relative = _mm512_maskz_mov_epi8(lanes, parse_relative_depths8(node_depths, block, lanes, base, &highest));
    *fits = highest - *base <= BUSTER_PARSE_BYTE_DEPTH_RANGE;
    return lanes;
}

// One prepared block of the left pass: its window work on the in-block work of parse_top_prepare8_avx512.
BUSTER_INLINE void parse_top_left8_prepared(ParseTop* top, const ParseSlots* slots, uint32_t reach, const ParseBlockWork* work, uint32_t block, __mmask64 live,
                                            bool partial, uint32_t* lefts, uint16_t* left_keys)
{
    const __m512i block_keys = _mm512_load_si512(work->keys);
    const __m512i reversed = _mm512_permutexvar_epi8(parse_shifts.reverse8, block_keys);
    const ParseFound8 found = parse_top_block8(top, slots, reach, block_keys, _mm512_load_si512(work->left_thresholds), _mm512_load_si512(work->lefts), reversed,
                                               parse_shifts.reverse8, work->left_minima, _mm512_set1_epi8((char)work->minimum), block, work->base, live);
    parse_top_store_lefts8(&found, block, live, partial, lefts, left_keys);
}

// K4's left pass with the stack's top in registers, on the prepared blocks.
BUSTER_INLINE void parse_top_left8_pass(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, const ParseBlockWork* works,
                                        const ParseSlots* slots, uint32_t reach, uint64_t* wide_blocks)
{
    ParseTop top;
    parse_top_reset(&top);
    for (uint32_t block = 0; block < count; block += 64)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const ParseBlockWork* work = &works[block / 64];
        if (work->base != BUSTER_PARSE_NONE)
        {
            parse_top_left8_prepared(&top, slots, reach, work, block, live_mask, live != 64, lefts, left_keys);
        }
        else
        {
            // Aligned parts from the block's start, each the largest that fits.
            *wide_blocks += 1;
            __m512i relative;
            uint32_t base;
            uint32_t start = 0;
            while (start < live)
            {
                uint32_t size = start == 0 ? 32 : 1u << __builtin_ctz(start);
                size = size > 32 ? 32 : size;
                bool part_fits = false;
                __mmask64 lanes = 0;
                while (!part_fits)
                {
                    lanes = parse_top_part(node_depths, block, live_mask, start, size, &relative, &base, &part_fits);
                    part_fits |= size == 1;
                    size = part_fits ? size : size / 2;
                }
                parse_top_left8_block(&top, slots, reach, roles, relative, base, block, lanes, true, lefts, left_keys);
                start += size;
            }
        }
    }
}

// The parents of a block's live lanes from their R (`found`), their L and their roles' rules, as
// parse_parents_right8_block picks them.
BUSTER_INLINE void parse_top_parents8(const ParseFound8* found, __m512i rules, uint32_t block, __mmask64 live, bool partial, const uint32_t* lefts, const uint16_t* left_keys,
                                      uint32_t* parents)
{
    const __m512i weak_ties_table = _mm512_loadu_si512(parse_parent_tables.weak_ties);
    // 32 lanes at a time with 16-bit keys. No real key is 0, which marks no candidate.
    const __mmask64 rule_left = parse_equal8(rules, PARSE_RULE_LEFT);
    const __mmask64 rule_right = _mm512_cmpge_epu8_mask(rules, _mm512_set1_epi8(PARSE_RULE_RIGHT));
    for (uint32_t h = 0; h < 2; h += 1)
    {
        const __m512i left_key = _mm512_loadu_si512(left_keys + block + 32 * h);
        const __m512i right_key = found->keys[h];
        const __m512i weak_ties = _mm512_permutexvar_epi16(_mm512_and_si512(left_key, _mm512_set1_epi16(31)), weak_ties_table);
        const __mmask32 weak = _mm512_test_epi16_mask(weak_ties, _mm512_set1_epi16(1));
        const __mmask32 ties = _mm512_test_epi16_mask(weak_ties, _mm512_set1_epi16(2));
        const __mmask32 greater = _mm512_cmpgt_epu16_mask(left_key, right_key);
        const __mmask32 equal = _mm512_cmpeq_epi16_mask(left_key, right_key);
        const __mmask32 left_valid = _mm512_test_epi16_mask(left_key, left_key);
        const __mmask32 right_valid = _mm512_test_epi16_mask(right_key, right_key);
        __m512i left[2];
        for (uint32_t q = 0; q < 2; q += 1)
        {
            left[q] = _mm512_loadu_si512(lefts + block + 32 * h + 16 * q);
        }
        const __mmask32 select_left = left_valid & (~right_valid | (~weak & (greater | (equal & ties))));
        const __mmask32 take_left = (__mmask32)(rule_left >> (32 * h)) | (~(__mmask32)(rule_right >> (32 * h)) & select_left);
        for (uint32_t q = 0; q < 2; q += 1)
        {
            const __m512i parent = _mm512_mask_mov_epi32(found->positions[2 * h + q], (__mmask16)(take_left >> (16 * q)), left[q]);
            if (partial)
            {
                _mm512_mask_storeu_epi32(parents + block + 32 * h + 16 * q, (__mmask16)(live >> (32 * h + 16 * q)), parent);
            }
            else
            {
                _mm512_storeu_si512(parents + block + 32 * h + 16 * q, parent);
            }
        }
    }
}

// One block or part of the right pass: R by parse_next_smaller8 in the block, else from the window, then the parent
// by the role's rule. Its candidates for earlier blocks are its prefix minima.
BUSTER_INLINE void parse_top_right8_block(ParseTop* top, const ParseSlots* slots, uint32_t reach, const uint8_t* roles, __m512i relative, uint32_t base, uint32_t block,
                                          __mmask64 live, bool partial, const uint32_t* lefts, const uint16_t* left_keys, uint32_t* parents)
{
    const ParseKeys8 keys = parse_block_keys8(roles, relative, block);
    const __m512i block_keys = _mm512_mask_mov_epi8(_mm512_set1_epi8((char)0xFF), live, keys.keys);
    const __m512i p = parse_next_smaller8(block_keys, keys.right);
    const __m512i prefix = parse_prefix_minima8(block_keys);
    const __mmask64 minima = _mm512_cmplt_epu8_mask(block_keys, parse_up8(prefix, 0, _mm512_set1_epi8((char)0xFF)));
    const __m512i minimum = _mm512_permutexvar_epi8(parse_shifts.last8, prefix);
    const ParseFound8 found = parse_top_block8(top, slots, reach, block_keys, keys.right, p, block_keys, parse_shifts.lanes8, minima, minimum, block, base, live);
    parse_top_parents8(&found, keys.rules, block, live, partial, lefts, left_keys, parents);
}

// One prepared block of the right pass.
BUSTER_INLINE void parse_top_right8_prepared(ParseTop* top, const ParseSlots* slots, uint32_t reach, const uint8_t* roles, const ParseBlockWork* work, uint32_t block,
                                             __mmask64 live, bool partial, const uint32_t* lefts, const uint16_t* left_keys, uint32_t* parents)
{
    const __m512i block_keys = _mm512_load_si512(work->keys);
    const ParseFound8 found = parse_top_block8(top, slots, reach, block_keys, _mm512_load_si512(work->right_thresholds), _mm512_load_si512(work->rights), block_keys,
                                               parse_shifts.lanes8, work->right_minima, _mm512_set1_epi8((char)work->minimum), block, work->base, live);
    parse_top_parents8(&found, parse_lookup8(parse_parent_tables.rules, _mm512_loadu_si512(roles + block)), block, live, partial, lefts, left_keys, parents);
}

// K4's right pass with the stack's top in registers, from the last block to the first, on the prepared blocks; a block
// whose depths don't fit runs as aligned parts from its end, each the largest that fits.
BUSTER_INLINE void parse_top_right8_pass(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, const uint32_t* lefts, const uint16_t* left_keys,
                                         const ParseBlockWork* works, uint32_t* parents, const ParseSlots* slots, uint32_t reach)
{
    ParseTop top;
    parse_top_reset(&top);
    for (uint32_t b = (count + 63) / 64; b-- > 0;)
    {
        const uint32_t block = b * 64;
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const ParseBlockWork* work = &works[b];
        if (work->base != BUSTER_PARSE_NONE)
        {
            parse_top_right8_prepared(&top, slots, reach, roles, work, block, live_mask, live != 64, lefts, left_keys, parents);
        }
        else
        {
            uint32_t end = live;
            while (end > 0)
            {
                uint32_t size = 1u << __builtin_ctz(end);
                size = size > 32 ? 32 : size;
                bool part_fits = false;
                __mmask64 lanes = 0;
                __m512i relative;
                uint32_t part_base;
                while (!part_fits)
                {
                    lanes = parse_top_part(node_depths, block, live_mask, end - size, size, &relative, &part_base, &part_fits);
                    part_fits |= size == 1;
                    size = part_fits ? size : size / 2;
                }
                parse_top_right8_block(&top, slots, reach, roles, relative, part_base, block, lanes, true, lefts, left_keys, parents);
                end -= size;
            }
        }
    }
}

// The passes alone, as the vector path runs them, for the benchmark's stages.
static void parse_top_left8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, const ParseBlockWork* works,
                                   const ParseSlots* slots, uint64_t* wide_blocks)
{
    parse_top_left8_pass(roles, node_depths, count, lefts, left_keys, works, slots, BUSTER_PARSE_TOP_LANES, wide_blocks);
}

static void parse_top_right8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, const uint32_t* lefts, const uint16_t* left_keys,
                                    const ParseBlockWork* works, uint32_t* parents, const ParseSlots* slots)
{
    parse_top_right8_pass(roles, node_depths, count, lefts, left_keys, works, parents, slots, BUSTER_PARSE_TOP_LANES);
}

// K4 with the stack's top in registers: every block's in-block work, then the left pass, then the right pass.
static void parse_top_parents8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, ParseBlockWork* works,
                                      uint32_t* parents, const ParseSlots* slots, uint64_t* wide_blocks)
{
    parse_top_prepare8_avx512(roles, node_depths, count, works);
    parse_top_left8_avx512(roles, node_depths, count, lefts, left_keys, works, slots, wide_blocks);
    parse_top_right8_avx512(roles, node_depths, count, lefts, left_keys, works, parents, slots);
}

// The same with a window of `reach` lanes, fewer than BUSTER_PARSE_TOP_LANES, so that the check takes the paths through
// memory (deep lanes, reloads, pops under the window) that real stacks never need.
static __attribute__((unused)) void parse_top_parents8_reach(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, ParseBlockWork* works,
                                     uint32_t* parents, const ParseSlots* slots, uint32_t reach, uint64_t* wide_blocks)
{
    parse_top_prepare8_avx512(roles, node_depths, count, works);
    parse_top_left8_pass(roles, node_depths, count, lefts, left_keys, works, slots, reach, wide_blocks);
    parse_top_right8_pass(roles, node_depths, count, lefts, left_keys, works, parents, slots, reach);
}

// Memory for the stack's slots, with their padding.
static bool parse_slots_reserve(ParseSlots* slots, Arena* arena, uint64_t capacity)
{
    const uint64_t count = capacity + BUSTER_PARSE_TOP_LANES;
    slots->keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    slots->low = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    slots->high = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    slots->capacity = (uint32_t)capacity;
    const bool result = slots->keys != 0 && slots->low != 0 && slots->high != 0;
    if (result)
    {
        for (uint64_t i = capacity; i < count; i += 1)
        {
            slots->keys[i] = 0;
            slots->low[i] = 0xFFFF;
            slots->high[i] = 0xFFFF;
        }
    }
    return result;
}

static BUSTER_BAKED_FRONTEND ParseCheckVectorTables parse_check_vector;

// K6's vector tables, from its tables and the twin's own functions: every class's refinement, mode and group the
// kernel looks up is the twin's.
static void parse_check_vector_init(void)
{
    ParseCheckVectorTables* v = &parse_check_vector;
    const ParseCheckTables* t = &parse_check_tables;
    memset(v, 0, sizeof(*v));
    // The kinds: classes with the same row of follows are one left kind; with the same column in allows, one kind there,
    // and in ends one kind there.
    uint8_t left_class[128];
    uint8_t allows_class[128];
    uint8_t ends_class[128];
    for (uint32_t c = 0; c < 128; c += 1)
    {
        uint32_t k = 0;
        while (k < v->left_kinds && memcmp(&t->follows[left_class[k]], &t->follows[c], sizeof(ParseClassSet)) != 0)
        {
            k += 1;
        }
        if (k == v->left_kinds)
        {
            left_class[k] = (uint8_t)c;
            v->left_kinds += 1;
        }
        v->left[c] = (uint8_t)k;
        for (uint32_t which = 0; which < 2; which += 1)
        {
            const ParseClassSet* table = which == 0 ? t->allows : t->ends;
            uint8_t* classes = which == 0 ? allows_class : ends_class;
            uint32_t* kinds = which == 0 ? &v->allows_kinds : &v->ends_kinds;
            k = 0;
            bool found = false;
            while (!found && k < *kinds)
            {
                bool same = true;
                for (uint32_t m = 0; same && m < 64; m += 1)
                {
                    same = parse_class_set_has(&table[m], (uint8_t)c) == parse_class_set_has(&table[m], classes[k]);
                }
                found = same;
                k += !same;
            }
            if (!found)
            {
                classes[k] = (uint8_t)c;
                *kinds += 1;
            }
            (which == 0 ? v->allows_kind : v->ends_kind)[c] = (uint8_t)k;
        }
    }
    assert(v->left_kinds <= 64 && v->allows_kinds <= 64 && v->ends_kinds <= 64);
    for (uint32_t l = 0; l < v->left_kinds; l += 1)
    {
        for (uint32_t c = 0; c < 128; c += 1)
        {
            v->follows[l * 16 + c / 8] |= (uint8_t)(parse_class_set_has(&t->follows[left_class[l]], (uint8_t)c) << (c % 8));
        }
    }
    for (uint32_t m = 0; m < 64; m += 1)
    {
        for (uint32_t k = 0; k < v->allows_kinds; k += 1)
        {
            v->allows[m * 8 + k / 8] |= (uint8_t)(parse_class_set_has(&t->allows[m], allows_class[k]) << (k % 8));
        }
        for (uint32_t k = 0; k < v->ends_kinds; k += 1)
        {
            v->ends[m * 8 + k / 8] |= (uint8_t)(parse_class_set_has(&t->ends[m], ends_class[k]) << (k % 8));
        }
        v->mode_bits[m] = (uint8_t)((((t->named_modes >> m) & 1) != 0 ? BUSTER_PARSE_CHECK_NAMED : 0) |
                                    (((t->declaration_modes >> m) & 1) != 0 ? BUSTER_PARSE_CHECK_DECLARATION : 0) |
                                    (m == PARSE_CHECK_EXPRESSION ? BUSTER_PARSE_CHECK_IN_EXPRESSION : 0));
    }
    for (uint32_t i = 0; i < 64; i += 1)
    {
        v->bit[i] = (uint8_t)(1u << (i % 8));
    }
    // By class: the sets and what the class means to the kernel.
    for (uint32_t c = 0; c < 128; c += 1)
    {
        const uint8_t k = (uint8_t)c;
        const bool transparent = k == PARSE_CLASS_ATTRIBUTE_KEYWORD || k == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER || k == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ||
                                 k == PARSE_CLASS_TYPED_ATTRIBUTE;
        v->class_bits[c] = (uint8_t)((parse_class_set_has(&t->before_name, k) ? BUSTER_PARSE_CHECK_BEFORE_NAME : 0) |
                                     (parse_class_set_has(&t->after_name, k) ? BUSTER_PARSE_CHECK_AFTER_NAME : 0) |
                                     (parse_class_set_has(&t->specifier_ends, k) ? BUSTER_PARSE_CHECK_SPECIFIER_END : 0) |
                                     (parse_class_set_has(&t->operand_starts, k) ? BUSTER_PARSE_CHECK_OPERAND_START : 0) |
                                     (transparent ? BUSTER_PARSE_CHECK_TRANSPARENT : 0) |
                                     (k == PARSE_ROLE_CLOSE_STRUCT || k == PARSE_ROLE_CLOSE_ENUM ? BUSTER_PARSE_CHECK_BODY_CLOSER : 0) |
                                     (k == PARSE_ROLE_ASSIGN || k == PARSE_ROLE_COMMA || k == PARSE_ROLE_BITFIELD ? BUSTER_PARSE_CHECK_SWITCH : 0) |
                                     (k == PARSE_ROLE_OPEN_CALL || k == PARSE_ROLE_OPEN_SUBSCRIPT || k == PARSE_ROLE_POSTFIX || k == PARSE_ROLE_MEMBER_OPERATOR ||
                                              k == PARSE_CLASS_ARROW ?
                                          BUSTER_PARSE_CHECK_POSTFIX : 0));
        v->typed[c] = k < PARSE_CLASS_COUNT ? k : PARSE_ROLE_ERROR;
        v->item[c] = parse_check_item(PARSE_GROUP_BLOCK, k);
        v->designator[c] = parse_check_item(PARSE_GROUP_INITIALIZER, k);
        v->part_after[c] = parse_check_part_after(k, PARSE_PART_DECLARATION);
        v->opener_group[c] = parse_check_group(k, PARSE_CHECK_NONE, PARSE_ROLE_NONE);
        v->keyword_group[c] = parse_check_group(PARSE_ROLE_OPEN_KEYWORD, PARSE_CHECK_NONE, k) != PARSE_GROUP_OTHER ?
                                  parse_check_group(PARSE_ROLE_OPEN_KEYWORD, PARSE_CHECK_NONE, k) : parse_check_group(PARSE_ROLE_OPEN_SPECIFIER, PARSE_CHECK_NONE, k);
        v->variant[c] = k == PARSE_ROLE_OPEN_CALL ? 1 : k == PARSE_ROLE_OPEN_GROUP ? 2 : k == PARSE_ROLE_OPEN_SUBSCRIPT ? 3 : 0;
        v->closer_kind[c] = c < PARSE_ROLE_COUNT ? parse_check_closer_kind(k) : PARSE_KIND_R_PAREN;
    }
    v->typed[PARSE_ROLE_SPECIFIER] = PARSE_CLASS_TYPED;
    v->typed[PARSE_CLASS_STORAGE] = PARSE_CLASS_TYPED_STORAGE;
    v->typed[PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER] = PARSE_CLASS_TYPED_ATTRIBUTE;
    for (uint32_t variant = 1; variant < 4; variant += 1)
    {
        const uint8_t opener = variant == 1 ? PARSE_ROLE_OPEN_CALL : variant == 2 ? PARSE_ROLE_OPEN_GROUP : PARSE_ROLE_OPEN_SUBSCRIPT;
        for (uint32_t m = 0; m < 64; m += 1)
        {
            v->mode_group[variant * 64 + m] = m < PARSE_CHECK_MODE_COUNT ? parse_check_group(opener, (uint8_t)m, PARSE_ROLE_NONE) : PARSE_GROUP_OTHER;
        }
    }
    // By role.
    static const uint8_t refined[8] = { 0, PARSE_ROLE_OPERAND, PARSE_ROLE_SPECIFIER, PARSE_ROLE_PREFIX, PARSE_ROLE_LEAF_RIGHT, PARSE_ROLE_QUALIFIER_LEFT, PARSE_ROLE_RETURN,
                                        PARSE_ROLE_OPEN_CASE };
    for (uint32_t r = 0; r < 128; r += 1)
    {
        const uint8_t role = r < PARSE_ROLE_COUNT ? (uint8_t)r : PARSE_ROLE_ERROR;
        uint32_t refine = 0;
        for (uint32_t k = 1; k < 8; k += 1)
        {
            refine = refined[k] == role ? k : refine;
        }
        const uint8_t pred = parse_role_preds[role];
        v->role_bits[r] = (uint8_t)(refine | (parse_role_rules[role] == PARSE_RULE_OPENER ? BUSTER_PARSE_CHECK_OPENER : 0) |
                                    (parse_role_deltas[role] < 0 ? BUSTER_PARSE_CHECK_CLOSER : 0) |
                                    (pred == PARSE_PRED_ITEM || pred == PARSE_PRED_HEAD ? BUSTER_PARSE_CHECK_ITEM_AFTER : 0) |
                                    (role == PARSE_ROLE_LEAF_RIGHT || role == PARSE_ROLE_CLOSE_SPECIFIER ? BUSTER_PARSE_CHECK_RARE : 0));
    }
    uint32_t refinement_rows = 0;
    for (uint32_t vkind = 0; vkind < 128; vkind += 1)
    {
        uint8_t row[8] = { 0 };
        for (uint32_t k = 1; k < 8; k += 1)
        {
            row[k] = vkind < BUSTER_PARSE_VKINDS ? parse_check_class(refined[k], (uint8_t)vkind, 0, false) : refined[k];
        }
        uint32_t found = 0;
        while (found < refinement_rows && memcmp(v->refine + found * 8, row, sizeof(row)) != 0)
        {
            found += 1;
        }
        if (found == refinement_rows)
        {
            assert(refinement_rows < sizeof(v->refine) / sizeof(row));
            memcpy(v->refine + found * 8, row, sizeof(row));
            refinement_rows += 1;
        }
        v->refine_index[vkind] = (uint8_t)(found * 8);
    }
    // The pseudo-groups: (group, item's place) with the same modes in each state are one.
    uint8_t rows[128][6];
    for (uint32_t g = 0; g < 32; g += 1)
    {
        for (uint32_t place = 0; place < 4; place += 1)
        {
            uint8_t row[6];
            const uint8_t item = (uint8_t)(place + 1 < 3 ? place + 1 : 3);
            row[0] = parse_check_mode((uint8_t)g, true, BUSTER_PARSE_ITEM_OTHER, PARSE_PART_DECLARATION, item);
            row[1] = parse_check_mode((uint8_t)g, false, BUSTER_PARSE_ITEM_OTHER, PARSE_PART_DECLARATION, item);
            row[2] = parse_check_mode((uint8_t)g, false, BUSTER_PARSE_ITEM_ASSERTION, PARSE_PART_DECLARATION, item);
            row[3] = parse_check_mode((uint8_t)g, false, BUSTER_PARSE_ITEM_DECLARATION, PARSE_PART_INITIALIZER, item);
            row[4] = parse_check_mode((uint8_t)g, false, BUSTER_PARSE_ITEM_DECLARATION, PARSE_PART_WIDTH, item);
            row[5] = parse_check_mode((uint8_t)g, false, BUSTER_PARSE_ITEM_DECLARATION, PARSE_PART_DECLARATION, item);
            uint32_t p = 0;
            while (p < v->pseudo_groups && memcmp(rows[p], row, 6) != 0)
            {
                p += 1;
            }
            if (p == v->pseudo_groups)
            {
                memcpy(rows[p], row, 6);
                v->pseudo_groups += 1;
            }
            v->pseudo[g * 4 + place] = (uint8_t)p;
        }
    }
    assert(v->pseudo_groups * 6 <= 256);
    for (uint32_t p = 0; p < v->pseudo_groups; p += 1)
    {
        memcpy(&v->modes[p * 6], rows[p], 6);
    }
    for (uint32_t kind = 0; kind < 8; kind += 1)
    {
        for (uint32_t part = 0; part < 8; part += 1)
        {
            v->states[kind * 8 + part] = (uint8_t)(kind == BUSTER_PARSE_ITEM_ASSERTION ? 2 : kind != BUSTER_PARSE_ITEM_DECLARATION ? 1 :
                                                   part == PARSE_PART_INITIALIZER ? 3 : part == PARSE_PART_WIDTH ? 4 : 5);
        }
    }
    for (uint32_t vkind = 0; vkind < 128; vkind += 1)
    {
        const uint8_t k = (uint8_t)vkind;
        const bool always = parse_check_valid_after_tag(k, PARSE_KIND_END, false);
        const bool colon = !always && parse_check_valid_after_tag(k, PARSE_KIND_END, true);
        const bool unless = !always && parse_check_valid_after_tag(k, PARSE_KIND_END, false) != parse_check_valid_after_tag(k, PARSE_KIND_TYPE, false);
        const bool type = k == PARSE_KIND_TYPE || k == PARSE_KIND_STRUCT || k == PARSE_KIND_ENUM || k == PARSE_KIND_TYPEOF || k == PARSE_VKIND_TYPE_NAME;
        v->after_tag[vkind] = (uint8_t)((always && parse_check_valid_after_tag(k, PARSE_KIND_TYPE, false) ? BUSTER_PARSE_CHECK_TAG_ALWAYS : 0) |
                                        (colon ? BUSTER_PARSE_CHECK_TAG_COLON : 0) | (unless || (always && !parse_check_valid_after_tag(k, PARSE_KIND_TYPE, false)) ?
                                                                                      BUSTER_PARSE_CHECK_TAG_UNLESS_TYPE : 0) |
                                        (type ? BUSTER_PARSE_CHECK_TAG_TYPE : 0));
    }
}

// The bit of a 64-by-64 table (`table`: 512 bytes, row * 8 + column / 8) at each lane's row and column.
BUSTER_INLINE __mmask64 parse_check_bit(const uint8_t* table, __m512i row, __m512i column)
{
    const __m512i index = _mm512_add_epi8(_mm512_slli_epi16(_mm512_and_si512(row, _mm512_set1_epi8(0x0F)), 3), _mm512_srli_epi16(_mm512_and_si512(column, _mm512_set1_epi8(0x38)), 3));
    const __m512i quarter = _mm512_srli_epi16(_mm512_and_si512(row, _mm512_set1_epi8(0x30)), 4);
    // Rows 16q to 16q + 15 are the q-th 128 bytes.
    __m512i bytes = parse_table8(table, index);
    bytes = _mm512_mask_mov_epi8(bytes, _mm512_cmpeq_epi8_mask(quarter, _mm512_set1_epi8(1)), parse_table8(table + 128, index));
    bytes = _mm512_mask_mov_epi8(bytes, _mm512_cmpeq_epi8_mask(quarter, _mm512_set1_epi8(2)), parse_table8(table + 256, index));
    bytes = _mm512_mask_mov_epi8(bytes, _mm512_cmpeq_epi8_mask(quarter, _mm512_set1_epi8(3)), parse_table8(table + 384, index));
    const __m512i bit = _mm512_permutexvar_epi8(_mm512_and_si512(column, _mm512_set1_epi8(0x07)), _mm512_load_si512(parse_check_vector.bit));
    return _mm512_test_epi8_mask(bytes, bit);
}

// The bit of a 64-by-128 table (`table`: 1,024 bytes, row * 16 + column / 8) at each lane's row and column: the
// 128-byte chunk is the row's bits 3 to 5.
BUSTER_INLINE __mmask64 parse_check_bit128(const uint8_t* table, __m512i row, __m512i column)
{
    const __m512i index = _mm512_add_epi8(_mm512_slli_epi16(_mm512_and_si512(row, _mm512_set1_epi8(0x07)), 4), _mm512_srli_epi16(_mm512_and_si512(column, _mm512_set1_epi8(0x78)), 3));
    const __m512i chunk = _mm512_srli_epi16(_mm512_and_si512(row, _mm512_set1_epi8(0x38)), 3);
    __m512i bytes = parse_table8(table, index);
    for (uint32_t k = 1; k < 8; k += 1)
    {
        bytes = _mm512_mask_mov_epi8(bytes, _mm512_cmpeq_epi8_mask(chunk, _mm512_set1_epi8((char)k)), parse_table8(table + 128 * k, index));
    }
    const __m512i bit = _mm512_permutexvar_epi8(_mm512_and_si512(column, _mm512_set1_epi8(0x07)), _mm512_load_si512(parse_check_vector.bit));
    return _mm512_test_epi8_mask(bytes, bit);
}

// A 256-entry table at each lane's byte.
BUSTER_INLINE __m512i parse_check_table256(const uint8_t* table, __m512i index)
{
    const __m512i low = parse_table8(table, index);
    const __m512i high = parse_table8(table + 128, index);
    return _mm512_mask_blend_epi8(_mm512_movepi8_mask(index), low, high);
}

// The common classes, before the sparse name-dependent refinements. The seven role refinements have 28 distinct
// rows over virtual kinds; their table and the kind-to-row map replace a lookup and a blend for each role.
BUSTER_INLINE __m512i parse_check_classes_avx512(__m512i role, __m512i vkind, __m512i role_bits, __mmask64 typed)
{
    const ParseCheckVectorTables* v = &parse_check_vector;
    const __m512i refine = _mm512_and_si512(role_bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_REFINE));
    const __m512i index = _mm512_or_si512(parse_table8(v->refine_index, vkind), refine);
    __m512i classes = _mm512_mask_mov_epi8(role, _mm512_test_epi8_mask(refine, refine), parse_check_table256(v->refine, index));
    // `->` is the member operator that only an expression may use.
    classes = _mm512_mask_mov_epi8(classes, parse_equal8(role, PARSE_ROLE_MEMBER_OPERATOR) & parse_equal8(vkind, PARSE_KIND_ARROW), _mm512_set1_epi8(PARSE_CLASS_ARROW));
    classes = _mm512_mask_mov_epi8(classes, typed, parse_table8(v->typed, classes));
    return classes;
}

// The nearest previous lane whose key is below the lane's threshold, from the windows of minima of the keys (as
// parse_previous_smaller8 builds them): the lane, or a byte with bit 7 set if none in the block.
BUSTER_INLINE __m512i parse_check_search(const __m512i* windows, __m512i thresholds)
{
    const __m512i one = _mm512_set1_epi8(1);
    __m512i p = _mm512_sub_epi8(parse_shifts.lanes8, one);
    for (int32_t k = 5; k >= 0; k -= 1)
    {
        const __m512i window = _mm512_permutexvar_epi8(p, windows[k]);
        const __m512i below = _mm512_min_epu8(_mm512_subs_epu8(thresholds, window), one);
        p = _mm512_sub_epi8(p, _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), below), _mm512_set1_epi8((char)(1 << k))));
    }
    return p;
}

// The state after a tag's body (BUSTER_PARSE_CHECK_BODY_CLOSER) before token `i` of the block (`lane`), where the
// token before it is an attribute specifier's part: walked back past the attribute specifiers to the token before
// them. `before_block` is the frame's at the lane's depth before the block, for a walk that leaves it.
static bool parse_check_after_body_walk(const ParseKernels* kernels, uint32_t block, uint32_t lane, bool before_block)
{
    // The token at the lane's depth before it: the token before it, or for a closer its opener.
    uint32_t token = block + lane - 1;
    bool walking = true;
    bool result = false;
    while (walking)
    {
        const uint8_t c = kernels->check_classes[token];
        const bool closer = c == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER || c == PARSE_CLASS_TYPED_ATTRIBUTE;
        const bool transparent = closer || c == PARSE_CLASS_ATTRIBUTE_KEYWORD || c == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER;
        result = c == PARSE_ROLE_CLOSE_STRUCT || c == PARSE_ROLE_CLOSE_ENUM;
        uint32_t previous = closer ? kernels->enclosing[token] : token;
        walking = transparent && previous != BUSTER_PARSE_NONE && previous > 0;
        previous = walking ? previous - 1 : previous;
        // A walk past the block's start takes the frame's state there.
        if (walking && previous < block)
        {
            result = before_block;
            walking = false;
        }
        token = previous;
    }
    return result;
}

// K6 on the block of tokens [block, block + live), from the frames and the carry, which it leaves as the twin would.
// Returns false, leaving them and the flags alone, if it can't take the block.
static bool parse_check_block_avx512(ParseKernels* kernels, const uint32_t* names, const uint8_t* line_flags, const uint8_t* typed, uint32_t block, uint32_t live,
                                     const ParseCheckFrames* frames, ParseCheckCarry* carry)
{
    const ParseCheckVectorTables* v = &parse_check_vector;
    const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
    // The levels: the node depths less the block's least, plus one.
    __m512i depths[4];
    __m512i lowest = _mm512_set1_epi32(-1);
    __m512i highest = _mm512_setzero_si512();
    for (uint32_t q = 0; q < 4; q += 1)
    {
        const __mmask16 quarter = (__mmask16)(live_mask >> (16 * q));
        depths[q] = _mm512_maskz_loadu_epi32(quarter, kernels->node_depths + block + 16 * q);
        lowest = _mm512_mask_min_epu32(lowest, quarter, lowest, depths[q]);
        highest = _mm512_mask_max_epu32(highest, quarter, highest, depths[q]);
    }
    const uint32_t lo = _mm512_reduce_min_epu32(lowest);
    const uint32_t hi = _mm512_reduce_max_epu32(highest);
    bool result = hi - lo <= 63;
    if (result)
    {
        const __m512i base = _mm512_set1_epi32((int)lo - 1);
        __m512i level = _mm512_castsi128_si512(_mm512_cvtepi32_epi8(_mm512_sub_epi32(depths[0], base)));
        level = _mm512_inserti32x4(level, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depths[1], base)), 1);
        level = _mm512_inserti32x4(level, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depths[2], base)), 2);
        level = _mm512_inserti32x4(level, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depths[3], base)), 3);
        // Lanes past the block's end at the last lane's level.
        level = _mm512_mask_mov_epi8(_mm512_set1_epi8((char)(kernels->node_depths[block + live - 1] - lo + 1)), live_mask, level);
        const uint32_t previous_depth = carry->previous_depth;
        const uint8_t previous_level = (uint8_t)(previous_depth + 1 < lo ? 0 : previous_depth - lo + 1 > 65 ? 65 : previous_depth - lo + 1);
        const __m512i level_before = parse_up8(level, 0, _mm512_set1_epi8((char)previous_level));
        // The classes.
        const __m512i role = _mm512_loadu_si512(kernels->roles + block);
        const __m512i vkind = _mm512_loadu_si512(kernels->vkinds + block);
        const __m512i role_bits = parse_table8(v->role_bits, role);
        const __mmask64 typed_lanes = _mm512_test_epi8_mask(_mm512_loadu_si512(typed + block), _mm512_set1_epi8(1));
        __m512i c = parse_check_classes_avx512(role, vkind, role_bits, typed_lanes);
        ParseCheckBlock state;
        _mm512_store_si512(state.classes, c);
        // A keyword's class by its name (the builtins, _BitInt).
        uint64_t rare = _mm512_test_epi8_mask(role_bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_RARE)) & live_mask &
                        (parse_equal8(vkind, PARSE_KIND_TYPE_ARGUMENT) | parse_equal8(vkind, PARSE_KIND_TYPEOF));
        while (rare != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(rare);
            rare &= rare - 1;
            const uint32_t i = block + lane;
            state.classes[lane] = parse_check_class(kernels->roles[i], kernels->vkinds[i], names[i], typed[i] != 0);
        }
        c = _mm512_load_si512(state.classes);
        __m512i c_before = parse_up8(c, 0, _mm512_set1_epi8((char)carry->previous));
        const __m512i role_before = parse_up8(role, 0, _mm512_set1_epi8((char)carry->previous_role));
        // The starts.
        const __mmask64 closers = _mm512_test_epi8_mask(role_bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_CLOSER));
        const __mmask64 first = _mm512_cmpgt_epu8_mask(level, level_before) | (block == 0 ? 1 : 0);
        const __mmask64 after_item = _mm512_test_epi8_mask(parse_table8(v->role_bits, role_before), _mm512_set1_epi8(BUSTER_PARSE_CHECK_ITEM_AFTER));
        const __mmask64 starts = ~closers & (first | after_item | parse_equal8(c_before, PARSE_ROLE_SEPARATOR)) & live_mask;
        // The keys and the three searches.
        const __m512i class_bits = parse_table8(v->class_bits, c);
        const __m512i rank = _mm512_mask_mov_epi8(_mm512_mask_mov_epi8(_mm512_set1_epi8(2), _mm512_test_epi8_mask(class_bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_SWITCH)),
                                                                       _mm512_set1_epi8(1)),
                                                  starts, _mm512_setzero_si512());
        const __m512i level4 = _mm512_slli_epi16(_mm512_and_si512(_mm512_sub_epi8(level, _mm512_set1_epi8(1)), _mm512_set1_epi8(0x3F)), 2);
        const __m512i keys = _mm512_add_epi8(level4, rank);
        __m512i windows[6];
        windows[0] = keys;
        for (uint32_t k = 1; k < 6; k += 1)
        {
            windows[k] = _mm512_min_epu8(windows[k - 1], parse_up8(windows[k - 1], k - 1, _mm512_set1_epi8((char)0xFF)));
        }
        const __m512i enclosing = parse_check_search(windows, level4);
        const __m512i item_search = parse_check_search(windows, _mm512_add_epi8(level4, _mm512_set1_epi8(1)));
        const __m512i part_search = parse_check_search(windows, _mm512_add_epi8(level4, _mm512_set1_epi8(2)));
        const __mmask64 enclosed = ~_mm512_movepi8_mask(enclosing) & live_mask;
        const __m512i item_start = _mm512_mask_mov_epi8(item_search, starts, parse_shifts.lanes8);
        const __mmask64 item_found = ~_mm512_movepi8_mask(item_start) & live_mask;
        const __mmask64 part_found = ~_mm512_movepi8_mask(part_search) & live_mask;
        // What the frames say, by level.
        const __m512i at = _mm512_sub_epi8(level, _mm512_set1_epi8(1));
        const __m512i frame_group = _mm512_permutexvar_epi8(at, _mm512_loadu_si512(frames->group + lo));
        const __m512i frame_kind = _mm512_permutexvar_epi8(at, _mm512_loadu_si512(frames->kind + lo));
        const __m512i frame_part = _mm512_permutexvar_epi8(at, _mm512_loadu_si512(frames->part + lo));
        const __m512i frame_items = _mm512_permutexvar_epi8(at, _mm512_loadu_si512(frames->items + lo));
        // The item's place: a start first in its group is the first; else the one after the item before (that item's
        // start found in the block, or the frame's, or the carry's for the block's first lane).
        const __m512i start_class = _mm512_permutexvar_epi8(item_start, c);
        const __m512i previous_start = parse_up8(item_start, 0, _mm512_set1_epi8((char)0xFF));
        const __mmask64 previous_found = ~_mm512_movepi8_mask(previous_start) & ~(__mmask64)1;
        const __mmask64 previous_first = _mm512_movepi8_mask(_mm512_permutexvar_epi8(previous_start, _mm512_movm_epi8(first)));
        const __m512i next_of_frame = _mm512_min_epu8(_mm512_add_epi8(frame_items, _mm512_set1_epi8(1)), _mm512_set1_epi8(3));
        __m512i start_items = _mm512_mask_mov_epi8(next_of_frame, previous_found, _mm512_mask_mov_epi8(_mm512_set1_epi8(3), previous_first, _mm512_set1_epi8(2)));
        start_items = _mm512_mask_mov_epi8(start_items, 1, _mm512_set1_epi8((char)(carry->previous_items + 1 < 3 ? carry->previous_items + 1 : 3)));
        start_items = _mm512_mask_mov_epi8(start_items, first, _mm512_set1_epi8(1));
        const __m512i items = _mm512_mask_mov_epi8(frame_items, item_found, _mm512_permutexvar_epi8(item_start, start_items));
        const __m512i place = _mm512_min_epu8(_mm512_max_epu8(items, _mm512_set1_epi8(1)), _mm512_set1_epi8(3));
        const __m512i part_class = _mm512_permutexvar_epi8(part_search, c);
        const __m512i part = _mm512_mask_mov_epi8(frame_part, part_found, parse_table8(v->part_after, part_class));
        const __m512i variant = parse_table8(v->variant, c);
        const __m512i opener_group = parse_table8(v->opener_group, c);
        const __m512i keyword_group = parse_table8(v->keyword_group, c_before);
        const __mmask64 by_keyword = parse_equal8(c, PARSE_ROLE_OPEN_SPECIFIER) | parse_equal8(c, PARSE_ROLE_OPEN_KEYWORD);
        const __mmask64 by_attribute = parse_equal8(c, PARSE_ROLE_OPEN_ATTRIBUTE);
        const __m512i attribute_group = _mm512_mask_mov_epi8(_mm512_set1_epi8(PARSE_GROUP_ATTRIBUTE), parse_equal8(c_before, PARSE_CLASS_ASM_KEYWORD),
                                                             _mm512_set1_epi8(PARSE_GROUP_ASM_LABEL));
        // The rounds: a lane's group is the one its opener (in the block) opens, or its frame's. Each opener's group
        // depends only on lanes before it, so the groups have one fixed point, which a round that changes nothing has
        // reached; the rounds start from what each opener would open in an expression, as most nested groups are.
        const __m512i guess_by_mode = parse_check_table256(v->mode_group, _mm512_add_epi8(_mm512_slli_epi16(_mm512_and_si512(variant, _mm512_set1_epi8(0x03)), 6),
                                                                                         _mm512_set1_epi8(PARSE_CHECK_EXPRESSION)));
        __m512i guess = _mm512_mask_mov_epi8(opener_group, _mm512_test_epi8_mask(variant, variant), guess_by_mode);
        guess = _mm512_mask_mov_epi8(guess, by_keyword, keyword_group);
        guess = _mm512_mask_mov_epi8(guess, by_attribute, attribute_group);
        __m512i group = _mm512_mask_mov_epi8(frame_group, enclosed, _mm512_permutexvar_epi8(enclosing, guess));
        __m512i kind = frame_kind;
        __m512i mode = _mm512_setzero_si512();
        __m512i opened = _mm512_setzero_si512();
        bool settled = false;
        for (uint32_t round = 0; round < BUSTER_PARSE_CHECK_ROUNDS && !settled; round += 1)
        {
            // Each lane's item and mode from its group.
            const __m512i start_kind = _mm512_mask_mov_epi8(_mm512_mask_mov_epi8(parse_table8(v->item, start_class), parse_equal8(group, PARSE_GROUP_ENUM),
                                                                                 _mm512_set1_epi8(BUSTER_PARSE_ITEM_DECLARATION)),
                                                            parse_equal8(group, PARSE_GROUP_INITIALIZER), parse_table8(v->designator, start_class));
            kind = _mm512_mask_mov_epi8(frame_kind, item_found, start_kind);
            const __m512i pseudo = parse_table8(v->pseudo, _mm512_add_epi8(_mm512_slli_epi16(_mm512_and_si512(group, _mm512_set1_epi8(0x1F)), 2),
                                                                           _mm512_sub_epi8(place, _mm512_set1_epi8(1))));
            const __m512i state_index = _mm512_add_epi8(_mm512_slli_epi16(_mm512_and_si512(kind, _mm512_set1_epi8(0x07)), 3), _mm512_and_si512(part, _mm512_set1_epi8(0x07)));
            const __m512i state = _mm512_mask_mov_epi8(_mm512_permutexvar_epi8(state_index, _mm512_load_si512(v->states)), starts, _mm512_setzero_si512());
            const __m512i pseudo6 = _mm512_add_epi8(_mm512_add_epi8(_mm512_slli_epi16(_mm512_and_si512(pseudo, _mm512_set1_epi8(0x3F)), 2),
                                                                    _mm512_slli_epi16(_mm512_and_si512(pseudo, _mm512_set1_epi8(0x7F)), 1)),
                                                    state);
            mode = parse_check_table256(v->modes, pseudo6);
            // The groups the openers open.
            const __m512i by_mode = parse_check_table256(v->mode_group, _mm512_add_epi8(_mm512_slli_epi16(_mm512_and_si512(variant, _mm512_set1_epi8(0x03)), 6), mode));
            opened = _mm512_mask_mov_epi8(opener_group, _mm512_test_epi8_mask(variant, variant), by_mode);
            opened = _mm512_mask_mov_epi8(opened, by_keyword, keyword_group);
            opened = _mm512_mask_mov_epi8(opened, by_attribute, attribute_group);
            const __m512i next = _mm512_mask_mov_epi8(frame_group, enclosed, _mm512_permutexvar_epi8(enclosing, opened));
            settled = (_mm512_cmpneq_epi8_mask(next, group) & live_mask) == 0;
            group = next;
        }
        result = settled;
        if (result)
        {
            _mm512_store_si512(state.groups, group);
            _mm512_store_si512(state.kinds, kind);
            _mm512_store_si512(state.parts, part);
            _mm512_store_si512(state.items, items);
            _mm512_store_si512(state.modes, mode);
            _mm512_store_si512(state.opened, opened);
            _mm512_store_si512(state.levels, level);
            _mm512_store_si512(state.starts, _mm512_movm_epi8(starts));
            const __m512i mode_before = parse_up8(mode, 0, _mm512_set1_epi8((char)carry->previous_mode));
            const __m512i items_before = parse_up8(items, 0, _mm512_set1_epi8((char)carry->previous_items));
            const __m512i group_before = parse_up8(group, 0, _mm512_set1_epi8((char)carry->previous_group));
            const __m512i opened_before = parse_up8(opened, 0, _mm512_set1_epi8((char)frames->group[kernels->node_depths[block] + 1]));
            const __mmask64 starts_before = (starts << 1) | (carry->previous_start != 0);
            const __mmask64 empty = closers & _mm512_cmpeq_epi8_mask(level, level_before);
            const __m512i inner_group = _mm512_mask_mov_epi8(group_before, empty, opened_before);
            const __mmask64 openers = _mm512_test_epi8_mask(role_bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_OPENER));
            _mm512_mask_storeu_epi8(kernels->check_groups + block, live_mask,
                                    _mm512_mask_mov_epi8(_mm512_mask_mov_epi8(_mm512_set1_epi8(PARSE_GROUP_OTHER), openers, opened), closers, inner_group));
            const __m512i inner_items = _mm512_maskz_mov_epi8(~empty, items_before);
            // _Alignas's closer, by its group's type and the typed state before its keyword.
            uint64_t alignas = parse_equal8(role, PARSE_ROLE_CLOSE_SPECIFIER) & parse_equal8(inner_group, PARSE_GROUP_ALIGNAS) & live_mask;
            while (alignas != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctzll(alignas);
                alignas &= alignas - 1;
                const uint32_t opener = kernels->enclosing[block + lane];
                state.classes[lane] = opener >= 2 && typed[opener - 2] != 0 ? PARSE_CLASS_TYPED_ALIGNAS_CLOSE : PARSE_CLASS_ALIGNAS_CLOSE;
            }
            c = _mm512_load_si512(state.classes);
            c_before = parse_up8(c, 0, _mm512_set1_epi8((char)carry->previous));
            const __m512i c_before2 = _mm512_permutex2var_epi8(c, parse_shifts.up2_with_fill,
                                                               _mm512_castsi128_si512(_mm_cvtsi32_si128((int)(carry->before_previous | (carry->previous << 8)))));
            const __m512i bits = parse_table8(v->class_bits, c);
            const __m512i bits_before = parse_table8(v->class_bits, c_before);
            const __m512i allows_kind = parse_table8(v->allows_kind, c);
            const __m512i ends_kind_before = parse_table8(v->ends_kind, c_before);
            const __m512i left_before = parse_table8(v->left, c_before);
            const __mmask64 previous_last = _mm512_cmplt_epu8_mask(level, level_before);
            const __m512i mode_bits = _mm512_permutexvar_epi8(mode, _mm512_load_si512(v->mode_bits));
            const __m512i mode_bits_before = _mm512_permutexvar_epi8(mode_before, _mm512_load_si512(v->mode_bits));
            const __m512i named_bits = _mm512_mask_mov_epi8(mode_bits, previous_last, mode_bits_before);
            __mmask64 failed = ~parse_check_bit128(v->follows, left_before, c);
            failed |= ~parse_check_bit(v->allows, mode, allows_kind);
            failed |= previous_last & ~parse_check_bit(v->ends, mode_before, ends_kind_before);
            const __mmask64 skip_exempt = _mm512_test_epi8_mask(bits_before, _mm512_set1_epi8(BUSTER_PARSE_CHECK_SPECIFIER_END)) &
                                          (parse_equal8(c, PARSE_ROLE_TERMINATOR) | parse_equal8(c, PARSE_ROLE_BITFIELD));
            failed |= _mm512_test_epi8_mask(named_bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_NAMED)) &
                      _mm512_test_epi8_mask(bits_before, _mm512_set1_epi8(BUSTER_PARSE_CHECK_BEFORE_NAME)) &
                      _mm512_test_epi8_mask(bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_AFTER_NAME)) & ~skip_exempt;
            failed |= parse_equal8(role, PARSE_ROLE_LABEL) & ~(starts_before & parse_equal8(c_before, PARSE_ROLE_OPERAND));
            const __mmask64 terminator_before = parse_equal8(c_before, PARSE_ROLE_TERMINATOR);
            failed |= parse_equal8(group, PARSE_GROUP_FOR) & parse_equal8(role, PARSE_ROLE_TERMINATOR) & parse_equal8(items, 3);
            failed |= parse_equal8(role, PARSE_ROLE_CLOSE_FOR) & ((terminator_before & ~parse_equal8(inner_items, 2)) | (~terminator_before & ~parse_equal8(inner_items, 3)));
            failed |= parse_equal8(role, PARSE_ROLE_CLOSE_KEYWORD) & ~parse_equal8(inner_items, 2) &
                      ~(parse_equal8(inner_group, PARSE_GROUP_ASSERT) & parse_equal8(inner_items, 1));
            failed |= empty & (parse_equal8(inner_group, PARSE_GROUP_SUBSCRIPT) | parse_equal8(inner_group, PARSE_GROUP_DESIGNATOR));
            failed |= parse_equal8(role, PARSE_ROLE_ERROR) | (closers & _mm512_cmpneq_epi8_mask(vkind, parse_table8(v->closer_kind, role)));
            failed |= parse_equal8(c_before2, PARSE_CLASS_LABEL_ADDRESS) & _mm512_test_epi8_mask(bits, _mm512_set1_epi8((char)BUSTER_PARSE_CHECK_POSTFIX));
            failed |= parse_equal8(c_before, PARSE_CLASS_STAR) & ~_mm512_test_epi8_mask(mode_bits_before, _mm512_set1_epi8(BUSTER_PARSE_CHECK_DECLARATION)) &
                      ~_mm512_test_epi8_mask(bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_OPERAND_START));
            // After a tag's body, or a tag and a line break: what may follow a type. The body's closer just before, the
            // frame's state for the block's first lane, and a walk past attribute specifiers for the rare rest.
            const __mmask64 same_level = _mm512_cmpeq_epi8_mask(level, level_before);
            __mmask64 after_body = same_level & _mm512_test_epi8_mask(bits_before, _mm512_set1_epi8(BUSTER_PARSE_CHECK_BODY_CLOSER));
            after_body = (after_body & ~(__mmask64)1) | (frames->after_body[kernels->node_depths[block]] != 0 ? 1 : 0);
            uint64_t walks = same_level & _mm512_test_epi8_mask(bits_before, _mm512_set1_epi8(BUSTER_PARSE_CHECK_TRANSPARENT)) & ~(__mmask64)1 & live_mask;
            _mm512_mask_storeu_epi8(kernels->check_classes + block, live_mask, c);
            while (walks != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctzll(walks);
                walks &= walks - 1;
                const uint32_t depth = kernels->node_depths[block + lane];
                after_body |= (uint64_t)parse_check_after_body_walk(kernels, block, lane, frames->after_body[depth] != 0) << lane;
            }
            const __m512i after_tag = parse_table8(v->after_tag, vkind);
            const __m512i next_after_tag = parse_table8(v->after_tag, _mm512_loadu_si512(kernels->vkinds + block + 1));
            const __mmask64 valid_after_tag = _mm512_test_epi8_mask(after_tag, _mm512_set1_epi8(BUSTER_PARSE_CHECK_TAG_ALWAYS)) |
                                              (_mm512_test_epi8_mask(after_tag, _mm512_set1_epi8(BUSTER_PARSE_CHECK_TAG_COLON)) & parse_equal8(group, PARSE_GROUP_STRUCT)) |
                                              (_mm512_test_epi8_mask(after_tag, _mm512_set1_epi8(BUSTER_PARSE_CHECK_TAG_UNLESS_TYPE)) &
                                               ~_mm512_test_epi8_mask(next_after_tag, _mm512_set1_epi8(BUSTER_PARSE_CHECK_TAG_TYPE)));
            const __mmask64 line_start = _mm512_test_epi8_mask(_mm512_loadu_si512(line_flags + block), _mm512_set1_epi8(BUSTER_PP_START_OF_LINE));
            failed |= ((after_body & ~_mm512_test_epi8_mask(bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_TRANSPARENT))) | (parse_equal8(c_before, PARSE_ROLE_TAG) & line_start)) &
                      ~valid_after_tag;
            // Statement expressions, rare: a block first in parentheses in a function, and last.
            uint64_t blocks = _mm512_test_epi8_mask(mode_bits, _mm512_set1_epi8(BUSTER_PARSE_CHECK_IN_EXPRESSION)) &
                              (parse_equal8(c, PARSE_ROLE_OPEN_BLOCK) | parse_equal8(c, PARSE_ROLE_CLOSE_BLOCK)) & live_mask;
            while (blocks != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctzll(blocks);
                blocks &= blocks - 1;
                const uint32_t i = block + lane;
                bool fails = false;
                if (state.classes[lane] == PARSE_ROLE_CLOSE_BLOCK)
                {
                    fails = kernels->roles[i + 1] != PARSE_ROLE_CLOSE_GROUP;
                }
                else
                {
                    // The group at depth 1: the latest token at depth 0 before the lane opened it, or the frame has it.
                    uint8_t outer = frames->group[1];
                    bool searching = true;
                    for (uint32_t back = lane; searching && back-- > 0;)
                    {
                        searching = kernels->node_depths[block + back] != 0;
                        outer = searching ? outer : state.opened[back];
                    }
                    fails = (lane > 0 ? state.classes[lane - 1] : carry->previous) != PARSE_ROLE_OPEN_GROUP || outer != PARSE_GROUP_BLOCK;
                }
                failed |= (uint64_t)fails << lane;
            }
            failed &= live_mask;
            _mm512_mask_storeu_epi8(kernels->flags + block, failed, _mm512_set1_epi8(1));
            // The frames of the depths still open after the block: from the last lane at each, in order (an opener's
            // push before its group's tokens), with the state after a tag's body that lane leaves.
            const __m512i live_levels = _mm512_mask_mov_epi8(_mm512_set1_epi8((char)0xFF), live_mask, level);
            __m512i suffix = parse_down8(live_levels, 0, _mm512_set1_epi8((char)0xFF));
            for (uint32_t k = 0; k < 6; k += 1)
            {
                suffix = _mm512_min_epu8(suffix, parse_down8(suffix, k, _mm512_set1_epi8((char)0xFF)));
            }
            uint64_t last = _mm512_cmpgt_epu8_mask(suffix, level) & live_mask;
            while (last != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctzll(last);
                last &= last - 1;
                const uint32_t i = block + lane;
                const uint32_t depth = kernels->node_depths[i];
                const uint8_t lane_class = state.classes[lane];
                const uint8_t lane_bits = v->class_bits[lane_class];
                const bool body = (lane_bits & BUSTER_PARSE_CHECK_BODY_CLOSER) != 0 || (((after_body >> lane) & 1) != 0 && (lane_bits & BUSTER_PARSE_CHECK_TRANSPARENT) != 0);
                frames->group[depth] = state.groups[lane];
                frames->kind[depth] = state.kinds[lane];
                frames->part[depth] = state.kinds[lane] == BUSTER_PARSE_ITEM_DECLARATION ?
                                          (state.starts[lane] ? v->part_after[lane_class] : parse_check_part_after(lane_class, state.parts[lane])) :
                                          PARSE_PART_DECLARATION;
                frames->items[depth] = state.items[lane];
                frames->after_body[depth] = body;
                if ((v->role_bits[kernels->roles[i]] & BUSTER_PARSE_CHECK_OPENER) != 0)
                {
                    frames->group[depth + 1] = state.opened[lane];
                    frames->kind[depth + 1] = BUSTER_PARSE_ITEM_OTHER;
                    frames->part[depth + 1] = PARSE_PART_START;
                    frames->items[depth + 1] = 0;
                    frames->typed[depth + 1] = i >= 2 && typed[i - 2] != 0;
                    frames->after_body[depth + 1] = 0;
                }
            }
            const uint32_t end = live - 1;
            *carry = (ParseCheckCarry){
                .before_previous = end > 0 ? state.classes[end - 1] : carry->previous,
                .previous = state.classes[end],
                .previous_role = kernels->roles[block + end],
                .previous_mode = state.modes[end],
                .previous_items = state.items[end],
                .previous_start = state.starts[end] != 0,
                .previous_group = state.groups[end],
                .previous_depth = kernels->node_depths[block + end],
            };
        }
    }
    return result;
}

// K6 in AVX-512: every block by parse_check_block_avx512, or by the twin when that can't take it. `twin_blocks`, if not
// 0, gets the number of blocks the twin took. In a check, `twin_period` (else 0) sends every block whose index is 1
// modulo it to the twin, so that blocks of each kind follow the other's.
static void parse_check_avx512_period(ParseKernels* kernels, const uint32_t* names, const uint8_t* line_flags, uint32_t count, const uint8_t* typed,
                                      uint64_t* twin_blocks, uint32_t twin_period)
{
    const ParseCheckFrames frames = parse_check_frames(kernels);
    ParseCheckCarry carry;
    parse_check_begin(&frames, &carry);
    uint64_t twins = 0;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        const bool twin = twin_period != 0 && (block / 64) % twin_period == 1;
        if (twin || !parse_check_block_avx512(kernels, names, line_flags, typed, block, live, &frames, &carry))
        {
            parse_check_range(kernels, names, line_flags, typed, block, block + live, &frames, &carry, kernels->check_classes, 0, 0);
            twins += 1;
        }
    }
    parse_check_end(kernels, count, &carry, 0);
    // The bodies, found 64 roles at a time.
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __mmask64 live = count - block < 64 ? (((__mmask64)1 << (count - block)) - 1) : ~(__mmask64)0;
        uint64_t bodies = _mm512_mask_cmpeq_epi8_mask(live, _mm512_maskz_loadu_epi8(live, kernels->roles + block), _mm512_set1_epi8(PARSE_ROLE_OPEN_BODY));
        while (bodies != 0)
        {
            parse_check_body(kernels, block + (uint32_t)__builtin_ctzll(bodies), 0);
            bodies &= bodies - 1;
        }
    }
    if (twin_blocks != 0)
    {
        *twin_blocks += twins;
    }
}

static void parse_check_avx512(ParseKernels* kernels, const uint32_t* names, const uint8_t* line_flags, uint32_t count, const uint8_t* typed, uint64_t* twin_blocks)
{
    parse_check_avx512_period(kernels, names, line_flags, count, typed, twin_blocks, 0);
}

// parse_function_role on the block of tokens from `block`, the lanes of `live_mask`, after K6.
BUSTER_INLINE void parse_function_roles_block_avx512(ParseKernels* kernels, uint32_t block, __mmask64 live_mask)
{
    const __m512i role = _mm512_maskz_loadu_epi8(live_mask, kernels->roles + block);
    const __mmask64 parameters = parse_equal8(_mm512_maskz_loadu_epi8(live_mask, kernels->check_groups + block), PARSE_GROUP_PARAMETERS) & live_mask;
    const __mmask64 opens = parameters & parse_equal8(role, PARSE_ROLE_OPEN_CALL);
    const __mmask64 closes = parameters & parse_equal8(role, PARSE_ROLE_CLOSE_CALL);
    const __m512i function = _mm512_mask_mov_epi8(_mm512_set1_epi8(PARSE_ROLE_CLOSE_FUNCTION), opens, _mm512_set1_epi8(PARSE_ROLE_OPEN_FUNCTION));
    _mm512_mask_storeu_epi8(kernels->roles + block, opens | closes, function);
}

// K2's by-depth stacks, each from the start of a page and a quarter page after the one before it: a block stores its
// open openers 64 bytes at a time from its depth, and loads the 64 entries 128 below it, around depth 0 for most code.
// A stack whose depth 0 fell 40 bytes before the end of a page (as the arena laid them out for sqlite3.c) split every
// such store across two pages: K2 took 4.1 cycles a token on sqlite3.c, and 4.7 to 5.2 where the stack landed at a few
// offsets in its page, against 3.9 to 4.0 at every offset with the stacks aligned.
static uint8_t* parse_depth_stack_reserve(Arena* arena, uint64_t slots, uint64_t quarter)
{
    uint8_t* bytes = (uint8_t*)arena_push_array(arena, slots + BUSTER_PARSE_DEPTH_PADDING + 1024 * quarter, 1, 4096);
    return bytes != 0 ? bytes + 1024 * quarter + BUSTER_PARSE_DEPTH_PADDING : 0;
}

// The C driver's reader takes the parse's temporaries' pages again (ParseOptions.scratch), but only as far as its own
// columns reach: the pages the parse touches past them stay to the compile's end. So these buffers are pushed in the
// order of where they're touched, each a statement of its own (an initializer list's order of evaluation is C's to
// choose). First K4's stack in memory, touched at its slots' ends (each push, and the tail's 32 slots); last the
// statement pass's levels, touched from their start by level, then the twin statements' records, which the vector path
// touches only when K5 leaves a unit to the twin. Last of the buffers, K4's slots kept three huge pages at 90 to 96 MB
// of sqlite3.c's scratch, and the levels one at 82 MB, past the reader's 77 MB (#628).
static bool parse_vector_buffers_reserve(ParseVectorBuffers* buffers, Arena* arena, uint64_t capacity)
{
    const uint64_t slots = capacity + 2 * BUSTER_PARSE_TOKEN_PADDING;
    ParseSlots stack = { 0 };
    const bool stack_reserved = parse_slots_reserve(&stack, arena, capacity);
    uint32_t* open_stack = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots + BUSTER_PARSE_DEPTH_PADDING);
    uint8_t* group_roles = parse_depth_stack_reserve(arena, slots, 0);
    uint8_t* group_typed = parse_depth_stack_reserve(arena, slots, 1);
    uint8_t* group_keywords = parse_depth_stack_reserve(arena, slots, 2);
    uint8_t* typed = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots);
    uint32_t* lefts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    uint16_t* left_keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, slots);
    ParseBlockWork* works = BUSTER_ARENA_PUSH_ARRAY(arena, ParseBlockWork, slots / 64 + BUSTER_PARSE_PREPARED_TOGETHER);
    uint32_t* secondary = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    ParseStatementContext* contexts = BUSTER_ARENA_PUSH_ARRAY(arena, ParseStatementContext, slots / 64 + 1);
    uint32_t* statement_previous = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    uint32_t* statement_owners = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    ParseStatementLevel* statement_levels = BUSTER_ARENA_PUSH_ARRAY(arena, ParseStatementLevel, slots);
    ParseStatement* statements = BUSTER_ARENA_PUSH_ARRAY(arena, ParseStatement, slots);
    *buffers = (ParseVectorBuffers){
        .open_stack = open_stack != 0 ? open_stack + BUSTER_PARSE_DEPTH_PADDING : 0,
        .groups = {
            .roles = group_roles,
            .typed = group_typed,
            .keywords = group_keywords,
        },
        .typed = typed,
        .lefts = lefts,
        .left_keys = left_keys,
        .works = works,
        .secondary = secondary,
        .statements = statements,
        .contexts = contexts,
        .statement_previous = statement_previous,
        .statement_owners = statement_owners,
        .statement_levels = statement_levels,
        .slots = stack,
        .rounds = { 0 },
        .wide_blocks = 0,
        .statement_counts = { 0 },
        .check_twins = 0,
    };
    bool result = stack_reserved && open_stack != 0 && group_roles != 0 && group_typed != 0 && group_keywords != 0 && typed != 0 && lefts != 0 && left_keys != 0 &&
                  works != 0 && secondary != 0 && contexts != 0 && statement_previous != 0 && statement_owners != 0 && statement_levels != 0 && statements != 0;
    if (result)
    {
        // Each parse writes the stacks' entries below depth 0, where nothing is open: K1 reads the 16 below a block's start,
        // K2 the 128 (a top-level token's group is the entry at depth -1). A unit whose brackets don't balance puts openers
        // there, and a borrowed scratch holds its owner's bytes: read unwritten, they gave a later unit's top-level tokens
        // a group, and K5 settled statements that a fresh parser's doesn't (#2086).
        for (int32_t i = -BUSTER_PARSE_DEPTH_PADDING; i < 0; i += 1)
        {
            buffers->open_stack[i] = BUSTER_PARSE_NONE;
            group_roles[i] = PARSE_ROLE_NONE;
            group_typed[i] = 0;
            group_keywords[i] = PARSE_KIND_END;
        }
    }
    return result;
}

// The benchmark runs traversal kernels while retaining its workspace. Production does not: reserving these nine
// unused columns there separated K4's active slots and fallback scratch from the prefix reused by checking (#628).
static bool parse_vector_postorder_reserve(ParseVectorBuffers* buffers, Arena* arena, uint64_t capacity)
{
    const uint64_t slots = capacity + 2 * BUSTER_PARSE_TOKEN_PADDING;
    buffers->last_children = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    buffers->before = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    buffers->postorder = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    buffers->positions = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    buffers->subtree_ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    buffers->first_children = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    buffers->next_siblings = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    buffers->subtree_starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    buffers->sizes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    return buffers->last_children != 0 && buffers->before != 0 && buffers->postorder != 0 && buffers->positions != 0 && buffers->subtree_ends != 0 &&
           buffers->first_children != 0 && buffers->next_siblings != 0 && buffers->subtree_starts != 0 && buffers->sizes != 0;
}

// The deepest node depth, which decides whether K4's 16-bit keys can hold it.
static uint32_t parse_deepest(const ParseKernels* kernels, uint32_t count)
{
    __m512i deepest = _mm512_setzero_si512();
    for (uint32_t block = 0; block < count; block += 16)
    {
        const __mmask16 live = count - block < 16 ? (__mmask16)((1u << (count - block)) - 1) : (__mmask16)0xFFFF;
        deepest = _mm512_max_epu32(deepest, _mm512_maskz_loadu_epi32(live, kernels->node_depths + block));
    }
    return _mm512_reduce_max_epu32(deepest);
}

// K4 as the vector path runs it: in byte lanes with the stack's top in registers, but for nesting too deep for its
// 16-bit keys, which the twin takes.
static void parse_parents_vector(ParseKernels* kernels, uint32_t count, ParseVectorBuffers* buffers)
{
    if (parse_deepest(kernels, count) <= BUSTER_PARSE_MAX_VECTOR_DEPTH)
    {
        parse_top_parents8_avx512(kernels->roles, kernels->node_depths, count, buffers->lefts, buffers->left_keys, buffers->works, kernels->parents, &buffers->slots,
                                  &buffers->wide_blocks);
    }
    else
    {
        parse_parents_scalar(kernels, count);
    }
}

// K5 as the vector path runs it, after K4: in AVX-512, or where only the twin's stack decides, K4 again (for its
// parents, which K5 overwrote) and the twin. Returns false if an else has no if.
static bool parse_statements_vector(ParseKernels* kernels, uint32_t count, ParseVectorBuffers* buffers)
{
    const ParseStatementsVerdict verdict = parse_statements_avx512(kernels, count, buffers->contexts, buffers->statement_previous, buffers->statement_owners,
                                                                   buffers->statement_levels, &buffers->statement_counts);
    bool result = verdict == PARSE_STATEMENTS_OK;
    if (verdict == PARSE_STATEMENTS_TWIN)
    {
        buffers->statement_counts.twins += 1;
        parse_parents_vector(kernels, count, buffers);
        result = parse_statements(kernels, count, buffers->statements);
    }
    return result;
}

// The kernels read a GNU attribute's arguments as an expression's, where the scalar parser reads availability's,
// external_source_symbol's, type_tag_for_datatype's, objc_bridge_related's, swift_newtype's and swift_wrapper's as a balanced
// run of tokens (#1447,
// parse_scalar_custom_attribute):
// a top-level declaration with such a name before a `(` goes to the scalar parser, its first token flagged. A unit whose
// names have none (nearly every one: the interner finds no such name) looks at no token; one that has them, 16 tokens a
// compare.
static void parse_flag_custom_attributes(ParseKernels* kernels, const ParseInput* input)
{
    IdentId names[BUSTER_PARSE_CUSTOM_ATTRIBUTES] = { 0 };
    bool any = false;
    for (uint32_t s = 0; input->meanings != NULL && s < BUSTER_PARSE_CUSTOM_ATTRIBUTES; s += 1)
    {
        names[s] = input->meanings->custom_attributes[s];
        any |= names[s] != 0;
    }
    const uint32_t count = input->count;
    for (uint32_t block = 0; any && block < count; block += 16)
    {
        const __mmask16 live = count - block < 16 ? (__mmask16)((1u << (count - block)) - 1) : (__mmask16)0xffff;
        const __m512i loaded = _mm512_maskz_loadu_epi32(live, input->names + block);
        __mmask16 found = 0;
        for (uint32_t s = 0; s < BUSTER_PARSE_CUSTOM_ATTRIBUTES; s += 1)
        {
            found |= names[s] != 0 ? _mm512_mask_cmpeq_epi32_mask(live, loaded, _mm512_set1_epi32((int32_t)names[s])) : 0;
        }
        for (uint32_t selected = found; selected != 0; selected &= selected - 1)
        {
            const uint32_t t = block + (uint32_t)__builtin_ctz(selected);
            kernels->flags[t] |= t + 1 < count && input->kinds[t + 1] == PARSE_KIND_L_PAREN ? 1u : 0u;
        }
    }
}

// Every kernel in AVX-512 in turn, then the fallback, into kernels->parents: the data-parallel parser.
static ParseKernelResult parse_vector(ParseKernels* kernels, const ParseInput* input, const IdentId* predeclared, ParseVectorBuffers* buffers)
{
    ParseKernelResult result = { .flagged_tokens = 0, .everything = false };
    const uint8_t* kinds = input->kinds;
    const uint32_t* names = input->names;
    const uint32_t count = input->count;
    kernels->unbalanced = false;
    kernels->unsupported = false;
    parse_kernels_predeclared(kernels, predeclared, true);
    // K1, on ( [ {.
    kernels->unbalanced = !parse_brackets_avx512(kinds, parse_kind_deltas, count, kernels->depths, kernels->enclosing, buffers->open_stack, kernels->bracket_blocks);
    // K3.
    if (!kernels->unbalanced)
    {
        parse_typedefs(kernels, kinds, names, count);
    }
    parse_classify_mask_avx512(kernels, kinds, names, count);
    // K2.
    parse_roles_avx512(kernels, count, buffers->typed, &buffers->groups, buffers->contexts, buffers->rounds);
    // K1's second part, and the roles after the colons it resolved.
    parse_secondary_avx512(kernels, count, buffers->secondary);
    parse_after_colons_avx512(kernels, count);
    // K4, K5 and K6.
    parse_parents_vector(kernels, count, buffers);
    const bool statements_ok = parse_statements_vector(kernels, count, buffers);
    result.everything = kernels->unbalanced || kernels->unsupported || !statements_ok;
    // A unit the kernels can't take goes to the scalar parser whole, without K6's flags. Where its brackets don't
    // balance, or K5 can't settle its statements, the kernels before K6 leave some of what it reads unwritten, so it
    // would flag tokens by what a parse's reused temporaries held before (#528).
    if (!result.everything)
    {
        parse_check_avx512(kernels, names, input->flags, count, buffers->typed, &buffers->check_twins);
        parse_flag_custom_attributes(kernels, input);
        for (uint32_t block = 0; block < count; block += 64)
        {
            const __mmask64 live = count - block < 64 ? (((__mmask64)1 << (count - block)) - 1) : ~(__mmask64)0;
            const __m512i flags = _mm512_maskz_loadu_epi8(live, kernels->flags + block);
            result.flagged_tokens += (uint64_t)__builtin_popcountll(_mm512_test_epi8_mask(flags, flags));
            parse_function_roles_block_avx512(kernels, block, live);
        }
    }
    result.fallback = parse_fallback(kernels, input, result.everything);
    parse_typedefs_clear(kernels);
    parse_kernels_predeclared(kernels, predeclared, false);
    return result;
}

static BUSTER_BAKED_FRONTEND bool parse_tables_ready;

// The tables every parse reads, built once, and baked into the executable (baked.h): a compile of the C driver's starts
// with them built. bake-tables runs this from the checker's unit, which reaches the home's copies as every unit does.
BUSTER_PARSER_ENTRY bool parse_tables_initialize(void)
{
    if (!parse_tables_ready)
    {
        parse_kind_table_initialize();
        parse_role_info_initialize();
        parse_kernels_init_tables();
        parse_shifts_init();
        parse_parent_tables_init();
        parse_role_tables_init();
        parse_statement_tables_init();
        parse_check_tables_init();
        parse_check_vector_init();
        parse_tables_ready = true;
    }
    return true;
}

// Whether the tables are built: from the start, in a baked executable.
BUSTER_PARSER_ENTRY __attribute__((unused)) bool parse_tables_initialized(void)
{
    return parse_tables_ready;
}

BUSTER_PARSER_ENTRY bool parse_initialize(Parser* parser, const ParseOptions* options, CLanguage language)
{
    parse_tables_initialize();
    *parser = (Parser){
        .options = *options,
        .language = language,
    };
    parser->scratch = arena_reserve(1ull << 36);
    return parser->scratch.capacity != 0;
}

// A padded column of `count` entries of `size` bytes in arena `column`, from its token 0 (the padding is before it).
static void* parse_column(Parser* parser, uint32_t column, uint64_t count, uint64_t size)
{
    const uint64_t slots = count + 2 * BUSTER_PARSE_TOKEN_PADDING;
    arena_release(&parser->columns[column]);
    parser->columns[column] = arena_reserve(slots * size + 64);
    uint8_t* bytes = arena_push_array(&parser->columns[column], slots, size, 64);
    return bytes != 0 ? bytes + BUSTER_PARSE_TOKEN_PADDING * size : 0;
}

// Padded columns as parse_column's, `count` of them, the ith of `entries[i]` entries of `sizes[i]` bytes, all in arena
// `column` as its tables (arena_reserve_tables, #1128): columns that a parse reserves together take one mapping and one
// discard, their first pages each base pages as in an arena of its own. Where the address space is unbounded, each
// table takes a power of two of bytes, from BUSTER_ARENA_SMALL_BYTES: a pooled arena of tables serves only its own
// layout, which units of a few sizes then share, where each unit's token count would make one of its own. Under a
// finite RLIMIT_AS or compact reservations (arena_reservation_limit), each takes its own bytes, so that the columns map
// no more than arenas of their own did (#1203's review). Sets *columns[i] to each, or every one to null if they can't be
// reserved.
static bool parse_columns(Parser* parser, uint32_t column, uint32_t count, const uint64_t* entries, const uint64_t* sizes, void** columns[])
{
    ArenaTable tables[8];
    const bool rounded = arena_reservation_limit() == BUSTER_ARENA_MAX_CAPACITY;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint64_t bytes = (entries[i] + 2 * BUSTER_PARSE_TOKEN_PADDING) * sizes[i] + 64;
        tables[i] = (ArenaTable){
            .bytes = !rounded ? bytes : bytes <= BUSTER_ARENA_SMALL_BYTES ? BUSTER_ARENA_SMALL_BYTES : 1ull << (64 - __builtin_clzll(bytes - 1)),
            .small = BUSTER_ARENA_SMALL_BYTES,
        };
    }
    arena_release(&parser->columns[column]);
    parser->columns[column] = arena_reserve_tables(tables, count);
    for (uint32_t i = 0; i < count; i += 1)
    {
        *columns[i] = tables[i].base != 0 ? (uint8_t*)tables[i].base + BUSTER_PARSE_TOKEN_PADDING * sizes[i] : 0;
    }
    return parser->columns[column].base != 0;
}

// The tree's columns for the unit's tokens, in arena 5 (parse_columns).
static bool parse_tree_columns_reserve(Parser* parser, ParseTree* tree)
{
    const uint64_t count = parser->tokens.count;
    const uint64_t entries[] = { count, count, count };
    const uint64_t sizes[] = { 1, 4, 4 };
    void** columns[] = { (void**)&tree->roles, (void**)&tree->parents, (void**)&tree->depths };
    return parse_columns(parser, 5, BUSTER_ARRAY_LENGTH(columns), entries, sizes, columns);
}

// Whether token `t` is a number where a C23 attribute's identifier goes: right after `[[`, or after a comma. The last is
// only a candidate: the sparse history/context walk below proves that it is an attribute's name, rather than an
// expression or an attribute argument. A scope's place is one of these: a number before `::` elsewhere, in a GNU list
// (`__attribute__((__clang__::x))`), stays a number, where clang's GNU list ends with "expected ')'" (#147).
// `attributes`: whether a `[[` comes before `t` in the unit, without which a comma at `t` is no attribute list's.
BUSTER_INLINE bool parse_clang_scope_place(const ParseTokens* tokens, uint32_t t, bool attributes)
{
    const bool name = tokens->kinds[(int64_t)t - 1] == PARSE_KIND_L_SQUARE && tokens->kinds[(int64_t)t - 2] == PARSE_KIND_L_SQUARE;
    const bool comma = attributes && tokens->kinds[(int64_t)t - 1] == PARSE_KIND_COMMA;
    return tokens->kinds[t] == PARSE_KIND_NUMBER && (tokens->flags[t] & BUSTER_PP_FROM_MACRO) != 0 && (name || comma);
}

// clang's reading of `__clang__`'s expansion where a C23 attribute's identifier goes (#147, clang's
// TryParseCXX11AttributeIdentifier): a number the macro's own expansion made, outside any other macro's and its arguments,
// right after `[[` or after a comma in that list, is `_Clang`, with clang's warning, which the checker gives it (it knows such a token by
// its source, a number). The token becomes that identifier before the names' meanings are sized. The places are found 64
// tokens at a time, as masks of the kinds around each token (the columns' padding covers the edges), and each one's
// expansion is looked up, the history recorded first where it was deferred; the scalar path tests each token, the twin.
static bool parse_clang_scope_numbers(Parser* parser, Preprocessor* pp)
{
    ParseTokens* tokens = &parser->tokens;
    const uint32_t count = tokens->count;
    Arena* scratch = parser->options.scratch != NULL ? parser->options.scratch : &parser->scratch;
    const uint64_t mark = scratch->length;
    uint32_t* groups = NULL;
    uint32_t group_count = 0;
    uint32_t scanned = 0;
    bool result = true;
    const IdentId clang = intern_find(pp->table, (const uint8_t*)"__clang__", 9);
    IdentId spelling = 0;
    // The history, which a deferred one replays the unit for (pp_history_ensure), is read only where a place is found. A
    // macro's number after a comma, in any call's arguments, is one only past the unit's first `[[` (#628): before it,
    // the first such number made every unit with macros replay its preprocessing for its history, and keep the replay.
    bool history = false;
    bool asked = false;
    bool attributes = false;
    for (uint32_t first = 0; result && clang != 0 && first < count; first += 64)
    {
        uint64_t places = 0;
        const uint32_t length = count - first < 64 ? count - first : 64;
#if BUSTER_HOST_VECTOR
        if (!BUSTER_SCALAR_PATH(parser->options.scalar))
        {
            const __mmask64 live = _bzhi_u64(~UINT64_C(0), length);
            const __m512i kinds = _mm512_loadu_si512(tokens->kinds + first);
            const __m512i before = _mm512_loadu_si512(tokens->kinds + (int64_t)first - 1);
            const __m512i earlier = _mm512_loadu_si512(tokens->kinds + (int64_t)first - 2);
            const __m512i own_flags = _mm512_loadu_si512(tokens->flags + first);
            const __mmask64 number = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_NUMBER)) &
                                     _mm512_test_epi8_mask(own_flags, _mm512_set1_epi8(BUSTER_PP_FROM_MACRO));
            const __mmask64 name = _mm512_cmpeq_epi8_mask(before, _mm512_set1_epi8(PARSE_KIND_L_SQUARE)) &
                                   _mm512_cmpeq_epi8_mask(earlier, _mm512_set1_epi8(PARSE_KIND_L_SQUARE));
            const __mmask64 comma = _mm512_cmpeq_epi8_mask(before, _mm512_set1_epi8(PARSE_KIND_COMMA));
            // The tokens right after a `[[`: a comma candidate counts from the unit's first.
            const __mmask64 opened = live & name;
            const uint64_t past = attributes ? ~UINT64_C(0) : opened != 0 ? ~((UINT64_C(1) << _tzcnt_u64(opened)) - 1) : 0;
            attributes |= opened != 0;
            places = live & number & (name | (comma & past));
        }
        else
#endif
        {
            for (uint32_t i = 0; i < length; i += 1)
            {
                const uint32_t t = first + i;
                attributes |= tokens->kinds[(int64_t)t - 1] == PARSE_KIND_L_SQUARE && tokens->kinds[(int64_t)t - 2] == PARSE_KIND_L_SQUARE;
                places |= parse_clang_scope_place(tokens, t, attributes) ? UINT64_C(1) << i : 0;
            }
        }
        for (; result && places != 0; places &= places - 1)
        {
            const uint32_t t = first + (uint32_t)__builtin_ctzll(places);
            history = asked ? history : pp_history_ensure(pp);
            asked = true;
            const uint32_t origin = history ? pp_history_at(pp, tokens->sources[t]) : 0;
            const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
            bool expanded = origin != 0 && (origin & ~BUSTER_PP_ORIGIN_INDEX) == 0 && index < pp->history_count &&
                            pp->history[index].name == clang && pp->history[index].parent == 0;
            if (expanded && t > 0 && tokens->kinds[t - 1] == PARSE_KIND_COMMA)
            {
                // Only a direct __clang__ expansion after a comma needs enclosing-delimiter state. Scan its prefix
                // once, monotonically across all such uses, so long attribute lists and deeply nested arguments
                // stay linear. The ordinary path keeps no stack or additional token-wide column.
                groups = groups != NULL ? groups : BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
                result = groups != NULL;
                for (; result && scanned < t; scanned += 1)
                {
                    const uint8_t kind = tokens->kinds[scanned];
                    if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
                    {
                        groups[group_count++] = scanned;
                    }
                    else if (parse_kind_is_closer(kind) && group_count != 0)
                    {
                        group_count -= 1;
                    }
                }
                const uint32_t open = result && group_count != 0 ? groups[group_count - 1] : 0;
                expanded = open > 0 && tokens->kinds[open] == PARSE_KIND_L_SQUARE && tokens->kinds[open - 1] == PARSE_KIND_L_SQUARE;
            }
            spelling = expanded && spelling == 0 ? intern_find_or_add(pp->table, (const uint8_t*)"_Clang", 6) : spelling;
            if (expanded && spelling != 0)
            {
                tokens->kinds[t] = PARSE_KIND_IDENTIFIER;
                tokens->names[t] = spelling;
            }
        }
    }
    arena_pop_to(scratch, mark);
    return result;
}

// The meaning of every name the unit may use, ordinary but for clang's predeclared typedef names.
static bool parse_prepare_meanings(Parser* parser, InternTable* table)
{
    bool result = true;
    IdentId* predeclared = parser->predeclared;
    for (uint32_t i = 0; result && i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        predeclared[i] = intern_find_or_add(table, (const uint8_t*)parse_predeclared_typedefs[i], (uint32_t)strlen(parse_predeclared_typedefs[i]));
        result = predeclared[i] != 0;
    }
    const uint32_t capacity = table->count;
    // In the tokens' arena where they fit, as they do unless the table grew by more than parse_translation_unit allowed.
    parser->meanings.meanings = !result ? 0 : capacity <= parser->meaning_room ? parser->meaning_column : parse_column(parser, 9, capacity, 1);
    parser->meanings.capacity = capacity;
    parser->meanings.table = table;
    static const char* const custom_spellings[BUSTER_PARSE_CUSTOM_ATTRIBUTES] = {
        "availability", "__availability__", "external_source_symbol", "__external_source_symbol__", "type_tag_for_datatype", "__type_tag_for_datatype__",
        "objc_bridge_related", "__objc_bridge_related__", "swift_newtype", "__swift_newtype__", "swift_wrapper", "__swift_wrapper__",
    };
    for (uint32_t s = 0; s < BUSTER_PARSE_CUSTOM_ATTRIBUTES; s += 1)
    {
        parser->meanings.custom_attributes[s] = intern_find(table, (const uint8_t*)custom_spellings[s], (uint32_t)strlen(custom_spellings[s]));
    }
    result = result && parser->meanings.meanings != 0;
    if (result)
    {
        memset(parser->meanings.meanings, PARSE_MEANING_ORDINARY, capacity);
        for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
        {
            parser->meanings.meanings[predeclared[i]] = PARSE_MEANING_TYPEDEF;
        }
    }
    return result;
}

// Parses the unit the preprocessor `pp` produced (pp_run's output). Returns false if it has errors or the parser ran out
// of memory (parser->fatal); the diagnostics are in parser->diagnostics (parse_print_diagnostics prints them).
BUSTER_PARSER_ENTRY bool parse_translation_unit(Parser* parser, Preprocessor* pp)
{
    const PPTokens* output = &pp->output;
    parser->preprocessor = pp;
    bool result = output->count <= BUSTER_PARSE_MAX_TOKENS / 2;
    const uint64_t capacity = 2 * (uint64_t)output->count + 64;
    parser->diagnostic_count = 0;
    parser->postorder = 0;
    parser->sizes = 0;
    parser->positions = 0;
    parser->ends = 0;
    parser->first_children = 0;
    parser->next_siblings = 0;
    parser->errors = 0;
    parser->recovered_parameter_types = false;
    parser->fatal = result ? PARSE_MESSAGE_NONE : PARSE_MESSAGE_TOO_MANY_TOKENS;
    parser->statistics = (ParseStatistics){ 0 };
    parser->gnu = (GnuSyntax){ 0 };
    arena_release(&parser->gnu_arena);
    if (result)
    {
        // The columns, all or none.
        ParseTokens* tokens = &parser->tokens;
        const uint64_t pragmas = (uint64_t)output->count + 1;
        // With the names' meanings, one for each name the intern table has and those parse_clang_scope_numbers
        // (`_Clang`) and parse_prepare_meanings may add, and where the address space is unbounded, the diagnostics, at
        // most one for each token and 16 more: two arenas fewer for each unit (#652). Sized before conversion, the
        // diagnostics take room for the tokens' bound, twice the output's; under a finite RLIMIT_AS that would map 12
        // bytes more for each token of a large unit than it can use, so they keep an arena of their own, sized by the
        // tokens converted (#1279's review).
        const uint64_t room = (uint64_t)pp->table->count + BUSTER_PARSE_PREDECLARED_TYPEDEFS + 1;
        const bool unbounded = arena_reservation_limit() == BUSTER_ARENA_MAX_CAPACITY;
        const uint64_t entries[] = { capacity, capacity, capacity, pragmas, pragmas, capacity, room, capacity + 16 };
        const uint64_t sizes[] = { 1, 4, 4, 4, 4, 1, 1, sizeof(ParseDiagnostic) };
        void** columns[] = { (void**)&tokens->kinds, (void**)&tokens->names, (void**)&tokens->sources, (void**)&tokens->pragma_sources, (void**)&tokens->pragma_positions,
                             (void**)&tokens->flags, (void**)&parser->meaning_column, (void**)&parser->diagnostic_column };
        parser->diagnostic_column = 0;
        result = parse_columns(parser, 0, BUSTER_ARRAY_LENGTH(columns) - !unbounded, entries, sizes, columns);
        parser->meaning_room = result ? (uint32_t)room : 0;
        tokens->capacity = (uint32_t)capacity;
        parser->fatal = result ? PARSE_MESSAGE_NONE : PARSE_MESSAGE_OUT_OF_MEMORY;
    }
    if (result)
    {
        if (BUSTER_SCALAR_PATH(parser->options.scalar))
        {
            parse_tokens_from_output_scalar(output, &parser->tokens);
        }
        else
        {
            parse_tokens_from_output(output, &parser->tokens);
        }
        parse_mark_pragma_packs(pp, &parser->tokens);
        const bool normalized = parse_clang_scope_numbers(parser, pp);
        const uint32_t count = parser->tokens.count;
        parser->diagnostic_capacity = count + 16;
        parser->diagnostics = parser->diagnostic_column != 0 ? parser->diagnostic_column :
                              parse_column(parser, 8, parser->diagnostic_capacity, sizeof(ParseDiagnostic));
        result = normalized && parser->diagnostics != 0 && parse_prepare_meanings(parser, pp->table);
        parser->fatal = result ? PARSE_MESSAGE_NONE : PARSE_MESSAGE_OUT_OF_MEMORY;
    }
    // The parse's temporaries, each step's popped after it (ParseOptions.scratch).
    Arena* temporary = parser->options.scratch != 0 ? parser->options.scratch : &parser->scratch;
    const uint64_t temporary_mark = temporary->length;
    const bool scalar_path = BUSTER_SCALAR_PATH(parser->options.scalar);
    bool scalar = result && scalar_path;
    if (result && !scalar)
    {
        // The vector path, with the scalar parser on what it flags; an error there sends the whole unit to the scalar
        // parser, for its diagnostics. The kernels' tree is the tree's columns, and their other arrays temporaries:
        // sqlite3.c's took 63 MB, which the C driver's checker takes again (#528).
        const uint32_t count = parser->tokens.count;
        ParseTree tree = { 0 };
        const bool reserved = parse_tree_columns_reserve(parser, &tree) && parse_kernels_reserve_tree(&parser->kernels, temporary, count, parser->meanings.capacity, &tree) &&
                              parse_vector_buffers_reserve(&parser->buffers, temporary, count);
        if (reserved)
        {
            const ParseInput input = {
                .kinds = parser->tokens.kinds,
                .names = parser->tokens.names,
                .flags = parser->tokens.flags,
                .count = count,
                .language = parser->language,
                .meanings = &parser->meanings,
                .scratch = temporary,
                .diagnostics = parser->diagnostics,
                .diagnostic_capacity = parser->diagnostic_capacity,
            };
            const ParseKernelResult vector = parse_vector(&parser->kernels, &input, parser->predeclared, &parser->buffers);
            parser->statistics.fallback_items = vector.fallback.items;
            parser->statistics.fallback_tokens = vector.fallback.tokens;
            parser->statistics.flagged_tokens = vector.flagged_tokens;
            parser->statistics.whole = vector.everything || vector.fallback.errors;
            scalar = vector.fallback.errors;
            parser->tree = (ParseTree){ .roles = parser->kernels.roles, .depths = parser->kernels.node_depths, .parents = parser->kernels.parents };
        }
        else
        {
            parser->errors += 1;
            parser->fatal = PARSE_MESSAGE_OUT_OF_MEMORY;
            result = false;
        }
        arena_pop_to(temporary, temporary_mark);
    }
    if (result && scalar && !scalar_path)
    {
        // The fallback's declarations may have left typedef names' meanings changed: the scalar parser starts afresh.
        result = parse_prepare_meanings(parser, pp->table);
        parser->fatal = result ? PARSE_MESSAGE_NONE : PARSE_MESSAGE_OUT_OF_MEMORY;
    }
    if (result && scalar)
    {
        const ParseScalarResult parsed = parse_tree_columns_reserve(parser, &parser->tree) ?
            parse_scalar(parser->tokens.kinds, parser->tokens.names, parser->tokens.flags, parser->tokens.count, parser->language, &parser->meanings, &parser->tree,
                         temporary, parser->diagnostics, parser->diagnostic_capacity, &parser->diagnostic_count) :
            (ParseScalarResult){ .errors = 0, .out_of_memory = true, .retyped = false, .undecided = false };
        parser->errors = parsed.errors + parsed.out_of_memory;
        parser->fatal = parsed.out_of_memory ? PARSE_MESSAGE_OUT_OF_MEMORY : PARSE_MESSAGE_NONE;
        result = parser->errors == 0;
    }
    else if (result)
    {
        parser->diagnostic_count = 0;
    }
    if (result && parser->options.extension_metadata)
    {
        // Reserve an upper bound virtually; only the exact measured arrays are committed. Each record requires
        // at least one source token, so this covers all three arrays and their alignment without count overflow.
        const uint64_t bytes = (uint64_t)parser->tokens.count * (sizeof(GnuAttributeGroup) + sizeof(GnuAttribute) + sizeof(GnuAsmLabel)) + 64;
        parser->gnu_arena = arena_reserve(bytes);
        const GnuSyntaxStatus status = parser->gnu_arena.base != 0 ?
            gnu_syntax_build(&parser->tokens, &parser->tree, &parser->gnu_arena, &parser->gnu, BUSTER_SCALAR_PATH(parser->options.scalar)) : GNU_SYNTAX_MEMORY;
        result = status == GNU_SYNTAX_OK;
        if (!result)
        {
            parser->errors += 1;
            parser->fatal = status == GNU_SYNTAX_MEMORY ? PARSE_MESSAGE_OUT_OF_MEMORY : PARSE_MESSAGE_INVALID_GNU_METADATA;
        }
    }
    if (result && !parser->options.tree_only)
    {
        // The postorder and the tree's columns (and with ParseOptions.sizes the subtree sizes), with the temporaries' room
        // for the passes.
        const uint32_t count = parser->tokens.count;
        const bool sizes = parser->options.sizes;
        const uint64_t entries[] = { count, count, count, count, count, count };
        const uint64_t widths[] = { 4, 4, 4, 4, 4, 4 };
        void** columns[] = { (void**)&parser->postorder,      (void**)&parser->positions,     (void**)&parser->ends,
                             (void**)&parser->first_children, (void**)&parser->next_siblings, (void**)&parser->sizes };
        const bool reserved = parse_columns(parser, 11, sizes ? 6 : 5, entries, widths, columns);
        uint32_t* last_children = BUSTER_ARENA_PUSH_ARRAY(temporary, uint32_t, (uint64_t)count + 1);
        uint32_t* before = BUSTER_ARENA_PUSH_ARRAY(temporary, uint32_t, (uint64_t)count + 1);
        uint32_t* starts = sizes ? BUSTER_ARENA_PUSH_ARRAY(temporary, uint32_t, (uint64_t)count + 1) : 0;
        result = reserved && last_children != 0 && before != 0 && (!sizes || starts != 0);
        if (result)
        {
            parse_tree_columns(parser->tree.parents, count, last_children, before, parser->postorder, parser->positions, parser->ends, parser->first_children,
                               parser->next_siblings, starts, parser->sizes);
        }
        else
        {
            parser->errors += 1;
            parser->fatal = PARSE_MESSAGE_OUT_OF_MEMORY;
        }
        arena_pop_to(temporary, temporary_mark);
    }
    return result;
}


// Prints the parse's diagnostics, as clang does: "file:line:column: error: message", then what stopped it, if anything did.
// Their places come from the expansion history, recorded first if it was deferred (pp_history_ensure).
BUSTER_PARSER_ENTRY void parse_print_diagnostics(const Parser* parser, Preprocessor* pp)
{
    if (parser->diagnostic_count != 0)
    {
        pp_history_ensure(pp);
    }
    for (uint32_t d = 0; d < parser->diagnostic_count; d += 1)
    {
        const ParseDiagnostic* diagnostic = &parser->diagnostics[d];
        char text[256];
        parse_diagnostic_message(parser, pp, diagnostic, text, sizeof(text));
        if (diagnostic->place == BUSTER_PARSE_AT_TOKEN && diagnostic->token < parser->tokens.count)
        {
            // An expected closer's note at its opener (#147): "to match this '('".
            const uint32_t opener = diagnostic->note;
            const uint8_t opener_kind = opener < parser->tokens.count ? parser->tokens.kinds[opener] : PARSE_KIND_END;
            const PPOutputDiagnosticNote note = {
                .token = opener < parser->tokens.count ? parser->tokens.sources[opener] : 0,
                .message = opener_kind == PARSE_KIND_L_SQUARE ? "to match this '['" : opener_kind == PARSE_KIND_L_BRACE ? "to match this '{'" : "to match this '('",
            };
            const uint32_t source = parser->tokens.sources[diagnostic->token];
            // Published where the parser knew it (ParseDiagnostic.publication), after an earlier error of its token's.
            const uint32_t known = diagnostic->publication != 0 && diagnostic->publication < parser->tokens.count ? parser->tokens.sources[diagnostic->publication] : source;
            pp_diagnose_output_notes_at(pp, diagnostic->error ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING, source, known, &note, opener_kind != PARSE_KIND_END ? 1u : 0u, "%s", text);
        }
        else
        {
            // A parser diagnostic's location can be before the next token or at the unit's end. Its publication
            // boundary is where the parser knew it: at the token it read next, so a preprocessing diagnostic between
            // the previous token and that one sorts first, as clang's interleaved parse does.
            uint32_t position = diagnostic->token < parser->tokens.count ? parser->tokens.sources[diagnostic->token] : pp->output.count;
            const uint32_t saved = pp->diagnostic_position;
            pp->diagnostic_position = position;
            // An expected closer's note at its opener (#147), where the error is at the unit's end.
            const uint32_t opener = diagnostic->note;
            const uint8_t opener_kind = opener < parser->tokens.count ? parser->tokens.kinds[opener] : PARSE_KIND_END;
            const PPDiagnosticNote note = {
                .location = opener < parser->tokens.count ? parse_primary_location(pp, parser->tokens.sources[opener]) : 0,
                .message = opener_kind == PARSE_KIND_L_SQUARE ? "to match this '['" : opener_kind == PARSE_KIND_L_BRACE ? "to match this '{'" : "to match this '('",
            };
            pp_diagnose_note(pp, diagnostic->error ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING, parse_diagnostic_location(parser, pp, diagnostic),
                             opener_kind != PARSE_KIND_END ? &note : NULL, "%s", text);
            pp->diagnostic_position = saved;
        }
    }
    if (parser->fatal != PARSE_MESSAGE_NONE)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "%s", parse_messages[parser->fatal]);
    }
}

// `compiler [-fsyntax-only] [options] file.c`: preprocesses and parses the file, with -E's options, and prints the
// diagnostics as clang does. For now that is all the compiler does: nothing is written. Returns the exit status: 0 for
// a unit without errors, 1 otherwise. A reservation that fails is reported, and what was reserved is released.
static __attribute__((unused)) int run_parse_command(int count, char** arguments)
{
    arena_failure = (ArenaFailure){ 0 };
    int result = 1;
    Arena arena = arena_reserve(1ull << 30);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    bool scalar = false;
    // --scalar runs the scalar twins of both the preprocessor and the parser.
    for (int i = 0; i < count; i += 1)
    {
        scalar |= strcmp(arguments[i], "--scalar") == 0;
    }
    const bool parsed = arena.base != 0 && pp_parse_options(count, arguments, &options, &arena, &input, &output, &markers);
    if (arena.base == 0 || (!parsed && arena_failure.bytes != 0))
    {
        arena_report_failure(stderr, "parsing", 1ull << 36);
    }
    else if (!parsed)
    {
        fprintf(stderr, "usage: compiler [-fsyntax-only] [-I dir] [-isystem dir] [-iquote dir] [-idirafter dir] [-D name[=value]] [-U name] "
                        "[-include file] [-std=...] [-trigraphs] [-fno-trigraphs] [-ffreestanding] [-O...] [-nostdinc] [--predefines file] [--scalar] file\n");
    }
    else
    {
        static InternTable table;
        static Preprocessor pp;
        static Parser parser;
        const ParseOptions parse_options = { .scalar = scalar, .tree_only = true };
        // The history only for diagnostics, which record it when there are any (PPOptions.defer_history).
        options.defer_history = true;
        if (!intern_initialize(&table) || !pp_initialize(&pp, &options, &table) || !parse_initialize(&parser, &parse_options, options.language))
        {
            // The preprocessor's setup says why it failed, when it can (an invalid SOURCE_DATE_EPOCH, say).
            if (!pp.fatal)
            {
                arena_report_failure(stderr, "parsing", 1ull << 36);
            }
        }
        else
        {
            const bool preprocessed = pp_run(&pp, input);
            const bool parsed_unit = preprocessed && parse_translation_unit(&parser, &pp);
            if (preprocessed)
            {
                parse_print_diagnostics(&parser, &pp);
            }
            // The diagnostics' places may have needed the expansion history, whose replay can fail (a fatal error).
            result = parsed_unit && !pp.fatal ? 0 : 1;
        }
        // What a failed reservation left reserved too.
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&table);
    }
    arena_release(&arena);
    return result;
}
