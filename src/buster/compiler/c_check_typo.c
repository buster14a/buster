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
// Clang also offers keywords where it reads an expression or a statement's start, and on taking one parses the
// statement again with it (`whil;` gives "expected '(' after 'while'"); the bindings pass cannot follow that. Where a
// keyword, or a visible typedef's name that a statement's start would take, is as near as the best value, the name is
// left undeclared, without a suggestion, as clang leaves it only where it offers neither (docs/c-checker.md).
//
// Only a unit with an undeclared name pays: the search replays the binding stream's scope stack once (CheckTypoScope),
// with no per-typo walk of the stream. The vector path prefilters the visible names by length in AVX-512 and finds each
// distance with Myers's bit-parallel algorithm, the candidate's equality masks from byte compares; the scalar path, its
// twin and oracle, filters one name at a time and fills the dynamic-programming table.

#define BUSTER_CHECK_TYPO_LIMIT 50
// An undeclared function's call that made no search (CheckInput.implicit_function_corrections): one clang warns of, or
// a library function's, or a C89 unit's.
#define BUSTER_CHECK_TYPO_UNSEARCHED (UINT32_MAX - 1)

typedef enum CheckTypoMode
{
    // A value: an object, a function or an enumerator (CastExpressionIdValidator); before `=`, `->` or `.`, not a function.
    CHECK_TYPO_VALUE,
    // An undeclared function's call: a function alone (DeclFilterCCC<FunctionDecl>).
    CHECK_TYPO_FUNCTION,
} CheckTypoMode;

// The declarations visible at `cursor`, the binding stream's declare events replayed in order as c_bindings_replay does:
// the scope stack, innermost last, with each one's key and its name's length beside it.
typedef struct CheckTypoScope CheckTypoScope;
struct CheckTypoScope
{
    uint32_t* events;
    uint32_t* keys;
    uint32_t* lengths;
    uint32_t count;
    uint32_t cursor;
    uint32_t capacity;
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
    uint32_t corrected_count;
    uint32_t value_count;
    uint32_t function_cursor;
    uint32_t function_searches;
    uint32_t walk_searches;
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

// The scope at event `limit` (exclusive): the stream replayed from where the scope last stopped, which only moves on.
static bool check_typo_scope_advance(CheckInputReader* reader, CheckTypoScope* scope, uint32_t limit)
{
    const CBindings* bindings = &reader->bindings;
    bool valid = scope->events != NULL || bindings->capacity == 0;
    if (scope->events == NULL && bindings->capacity != 0)
    {
        scope->capacity = bindings->capacity;
        scope->events = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, scope->capacity);
        scope->keys = scope->events != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, scope->capacity) : NULL;
        scope->lengths = scope->keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, scope->capacity) : NULL;
        valid = scope->lengths != NULL;
    }
    const uint32_t end = limit < bindings->count ? limit : bindings->count;
    for (uint32_t e = scope->cursor; valid && e < end; e += 1)
    {
        const uint8_t operation = bindings->operation[e];
        if (operation == C_BINDING_RESTORE)
        {
            const uint32_t mark = bindings->declaration[e];
            scope->count = mark < scope->count ? mark : scope->count;
        }
        else if (operation == C_BINDING_DECLARE)
        {
            const uint32_t key = bindings->key[e];
            const uint32_t name = key % bindings->name_capacity;
            valid = scope->count < scope->capacity;
            if (valid)
            {
                scope->events[scope->count] = e;
                scope->keys[scope->count] = key;
                scope->lengths[scope->count] = intern_length(reader->pp->table, name);
                scope->count += 1;
            }
        }
    }
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

// The scope's names near enough to the typo's `length` to be taken (3 * |difference| <= length), in stack order.
static uint32_t check_typo_prefilter(const CheckTypoScope* scope, uint32_t name_capacity, uint32_t length, bool vector, uint32_t* candidates)
{
    uint32_t count = 0;
    uint32_t i = 0;
    if (vector)
    {
        const __m512i wanted = _mm512_set1_epi32((int)length);
        const __m512i limit = _mm512_set1_epi32((int)length);
        const __m512i three = _mm512_set1_epi32(3);
        const __m512i names = _mm512_set1_epi32((int)name_capacity);
        const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        for (; i < scope->count; i += 16)
        {
            const uint32_t left = scope->count - i;
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
        for (; i < scope->count; i += 1)
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

// Whether a name's declaration kind is one `mode` takes, before `next`.
static bool check_typo_accepted(uint8_t kind, CheckTypoMode mode, bool object_only)
{
    const bool value = kind == C_BINDING_OBJECT || kind == C_BINDING_ENUMERATOR || (kind == C_BINDING_FUNCTION && !object_only);
    return mode == CHECK_TYPO_FUNCTION ? kind == C_BINDING_FUNCTION : value;
}

// The declaration a typo at `token` corrects to, its declare event's, from the declarations visible at `limit` (an event
// index); or BUSTER_CHECK_NONE.
static uint32_t check_typo_search(CheckInputReader* reader, CheckTypoScope* scope, uint32_t token, uint32_t limit, CheckTypoMode mode)
{
    const Parser* parser = reader->parser;
    const CBindings* bindings = &reader->bindings;
    CheckTypo* typo = reader->typo;
    const uint32_t name = parser->tokens.names[token];
    const uint8_t* spelling = (const uint8_t*)intern_bytes(reader->pp->table, name);
    const uint32_t length = intern_length(reader->pp->table, name);
    const uint32_t bound = (length + 2) / 3;
    const bool vector = BUSTER_VECTOR_PATH(!parser->options.scalar);
    uint32_t result = BUSTER_CHECK_NONE;
    // The prefilter's compressed stores write whole vectors: room for one past the stack's.
    bool valid = length >= 3 && check_typo_scope_advance(reader, scope, limit) &&
                 check_typo_reserve(reader->arena, &typo->candidates, &typo->candidate_capacity, scope->count + 16, 0) &&
                 check_typo_reserve(reader->arena, &typo->distances, &typo->distance_capacity, scope->count + 16, 0) &&
                 check_typo_reserve(reader->arena, &typo->row, &typo->row_capacity, length + 1, 0);
    reader->output->resource |= length >= 3 && !valid;
    if (valid)
    {
        const uint32_t name_capacity = bindings->name_capacity;
        const uint32_t filtered = check_typo_prefilter(scope, name_capacity, length, vector, typo->candidates);
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
        // Each name's innermost declaration (the latest kept of its key), and of those the nearest the mode takes; a
        // keyword or a typedef as near, the conservative refusal above.
        const bool object_only = mode == CHECK_TYPO_VALUE && token + 1 < parser->tokens.count &&
                                 (parser->tokens.kinds[token + 1] == PARSE_KIND_EQUAL || parser->tokens.kinds[token + 1] == PARSE_KIND_ARROW ||
                                  parser->tokens.kinds[token + 1] == PARSE_KIND_PERIOD);
        uint32_t best = UINT32_MAX;
        uint32_t best_at = BUSTER_CHECK_NONE;
        uint32_t ties = 0;
        uint32_t other_best = UINT32_MAX;
        for (uint32_t k = 0; k < kept; k += 1)
        {
            const uint32_t at = typo->candidates[k];
            bool shadowed = false;
            for (uint32_t later = k + 1; later < kept; later += 1)
            {
                shadowed |= scope->keys[typo->candidates[later]] == scope->keys[at];
            }
            const uint8_t kind = bindings->kind[scope->events[at]];
            const uint32_t distance = typo->distances[k];
            const bool accepted = !shadowed && distance != 0 && check_typo_accepted(kind, mode, object_only);
            const bool rival = !shadowed && distance != 0 && mode == CHECK_TYPO_VALUE && kind == C_BINDING_TYPEDEF;
            ties = accepted && distance == best ? ties + 1 : accepted && distance < best ? 1 : ties;
            best_at = accepted && distance < best ? at : best_at;
            best = accepted && distance < best ? distance : best;
            other_best = rival && distance < other_best ? distance : other_best;
        }
        for (uint32_t w = 0; mode == CHECK_TYPO_VALUE && best != UINT32_MAX && w < BUSTER_ARRAY_LENGTH(check_typo_keywords); w += 1)
        {
            const char* keyword = check_typo_keywords[w];
            const uint32_t keyword_length = (uint32_t)strlen(keyword);
            const uint32_t difference = keyword_length > length ? keyword_length - length : length - keyword_length;
            if (3 * difference <= length && check_typo_keyword_offered(keyword, parser->language.standard, parser->language.gnu))
            {
                const uint32_t distance = check_typo_distance(typo, vector, (const uint8_t*)keyword, keyword_length, spelling, length);
                other_best = distance <= bound && distance < other_best ? distance : other_best;
            }
        }
        const bool unique = best != UINT32_MAX && ties == 1 && length / best >= 3 && other_best > best;
        result = unique ? scope->events[best_at] : BUSTER_CHECK_NONE;
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
        }
    }
    return whole && reader->typo != NULL;
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
// of the name it corrects to, or BUSTER_CHECK_NONE. A name just after a cast's `)` isn't corrected, as clang's validator
// there takes no value; it is searched all the same, toward the limit.
static uint32_t check_typo_value(CheckInputReader* reader, uint32_t token, uint32_t event)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const bool roles = parser->tree.roles != NULL && parser->tokens.kinds != NULL;
    const bool operand = roles && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                         parser->tree.roles[token] == PARSE_ROLE_OPERAND;
    const bool cast = operand && token != 0 && parser->tree.roles[token - 1] == PARSE_ROLE_CLOSE_CAST;
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
        const bool room = typo->value_count + typo->function_searches < BUSTER_CHECK_TYPO_LIMIT &&
                          typo->value_count + typo->function_searches + check_typo_calls_before(reader, &typo->value_calls, token) < BUSTER_CHECK_TYPO_LIMIT;
        if (room)
        {
            typo->value_tokens[typo->value_count++] = token;
        }
        result = room && !cast ? check_typo_search(reader, &typo->answers, token, event, CHECK_TYPO_VALUE) : BUSTER_CHECK_NONE;
    }
    return result;
}

// An undeclared function's call at `token`, read by the walk before its implicit declaration is bound: the declaration
// token of the function it may have meant, or BUSTER_CHECK_NONE. Past the limit's count of the calls' own searches
// alone none is made; the values' are counted in once they are known (check_typo_finish).
static uint32_t check_typo_function(CheckInputReader* reader, uint32_t token)
{
    uint32_t result = BUSTER_CHECK_NONE;
    if (!reader->typo_disabled && check_typo_ready(reader) && reader->typo->walk_searches < BUSTER_CHECK_TYPO_LIMIT)
    {
        reader->typo->walk_searches += 1;
        const uint32_t event = check_typo_search(reader, &reader->typo->walk, token, reader->bindings.count, CHECK_TYPO_FUNCTION);
        const uint32_t declaration = event != BUSTER_CHECK_NONE ? reader->bindings.declaration[event] : BUSTER_CHECK_NONE;
        // An implicitly declared function, a library's or another call's, is no suggestion of clang's here, though it is
        // as near as any other (ImplicitlyDefineFunction: "not being a particularly good idea").
        const bool implicit = declaration < reader->parser->tokens.count && reader->output->function_flags != NULL &&
                              (reader->output->function_flags[declaration] & CHECK_INPUT_FUNCTION_IMPLICIT) != 0;
        result = implicit ? BUSTER_CHECK_NONE : declaration;
    }
    return result;
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
        const bool over = searched && (searches >= BUSTER_CHECK_TYPO_LIMIT || searches + check_typo_calls_before(reader, &calls, token) >= BUSTER_CHECK_TYPO_LIMIT);
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
