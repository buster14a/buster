// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The preprocessor's differential fuzzing (#109), for test-preprocessor --fuzz: run_preprocessor_fixture_tests runs
// pp_fuzz.
// Each case preprocessor_fuzz_generate.c makes is preprocessed as clang reads it alone (preprocessor_fuzz_batch_source:
// its #line, its source and its marker), by preprocessors of its own with the oracle's configuration, -undef -nostdinc
// and -std=c17 or -std=c23 (trigraphs in C17, as in clang's ISO modes). Its vectorized kernels and their scalar twins
// must agree on every case as on test-preprocessor's cases (pp_fuzz_kernels): the acceptance and the diagnostics'
// counts, the streams token for token, the directive tables, and the printers byte for byte, whose -P text must lex
// back into the stream. With clang, the vectorized run's acceptance and its -P text, split at its marker as clang's
// text is, must be clang's, through preprocessor_fuzz_compare (pp_fuzz_against_clang).
//
// Clang reads a block's cases in two batches, the ones we accept and the ones we reject, so that a batch of accepted
// cases usually exits cleanly. A case that differs is read again alone, and so is every case of one block in
// BUSTER_PP_FUZZ_SAMPLED: a batch that gives another result than the case alone is a fault of the oracle, which is
// counted and fails the run, never taken for the preprocessor's. A batch without a result has its cases read alone,
// and one without a result alone either is reported and fails the run. A difference on the case alone is printed, minimized
// by preprocessor_fuzz_minimize with the oracle that found it (preprocessor_fuzz_difference, defined here), printed
// again with both sides' results, and saved as fuzz_failure_N.c in the current directory.
//
// --jobs N forks N workers, each with a contiguous share of each mode's ids, which send their counts back through a
// pipe: the cases, and so the results, don't depend on N.
//
// main.c includes this file after c_preprocessor_test.c, whose helpers it uses (pp_same_streams,
// pp_same_directive_tables, pp_stream_spellings, pp_test_printed, pp_test_print and pp_test_quiet), and which has
// PPFuzzOptions, since run_preprocessor_fixture_tests parses them. Layout: the counts, our side of a case (pp_fuzz_run,
// pp_fuzz_output), the kernels' check, the oracles the minimizer calls, reports, a worker (pp_fuzz_worker), and
// pp_fuzz.

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// Cases generated and compared at once: one clang batch's.
#define BUSTER_PP_FUZZ_BLOCK BUSTER_PPF_CLANG_BATCH_COUNT
// The differences a worker prints and minimizes in each mode (it counts every one).
#define BUSTER_PP_FUZZ_REPORTED 4
// One block in this many has each case read by clang alone too, which must give the batch's results.
#define BUSTER_PP_FUZZ_SAMPLED 128
#define BUSTER_PP_FUZZ_ARENA_BYTES (1ull << 34)
// The numbers fuzz_failure_N.c may take.
#define BUSTER_PP_FUZZ_FAILURES 100000
// The main file's name, as clang names its standard input (each case's #line renames it).
#define BUSTER_PP_FUZZ_NAME "<stdin>"
// How much of an output a report prints.
#define BUSTER_PP_FUZZ_SHOWN 600

// What a worker did in one mode; pp_fuzz sums the workers'.
typedef struct PPFuzzTally PPFuzzTally;
struct PPFuzzTally
{
    uint64_t cases;
    // The cases we accept.
    uint64_t accepted;
    // The cases the vectorized kernels and their scalar twins disagree on.
    uint64_t kernels;
    // The cases compared with clang, the ones that differ from clang on the case alone, the ones clang read alone (to
    // confirm a difference, or sampled), and the ones whose batch gave another result than the case alone.
    uint64_t compared;
    uint64_t differences;
    uint64_t alone;
    uint64_t oracle;
    // The batches clang gave no result for, whose cases it read alone.
    uint64_t unbatched;
    // Cases a preprocessor, the generator, a clang process or the comparison couldn't give a result for.
    uint64_t invalid;
    // Cases compared with clang whose whole tokens clang's own tokens confirmed (pp_fuzz_check_ends), and those that agree
    // with clang only as far as their texts show: a token on a pragma's line that clang's text runs on past, which
    // clang's tokens don't have.
    uint64_t ends;
    uint64_t unverified;
    // Cases whose -P text can't lex back into their stream, as clang's can't (pp_fuzz_unprintable).
    uint64_t unprintable;
    // Differences minimized and saved, and the ones reported.
    uint64_t saved;
    uint64_t reported;
};

// A worker's preprocessors (the vectorized kernels' and the scalar twins'), and their intern tables, which each case
// resets.
typedef struct PPFuzzState PPFuzzState;
struct PPFuzzState
{
    // The vectorized kernels' run and the scalar twins', with expansion starts (for the printers' line markers), and
    // when both kernels run, theirs again as a compile runs them, without (pp_fuzz_run).
    Preprocessor runs[4];
    InternTable tables[4];
    bool tables_ready[4];
    bool ran[4];
    // The last check's text couldn't lex back into its stream (pp_fuzz_unprintable).
    bool unprintable;
};

static PPFuzzState pp_fuzz_state;

static const char* pp_fuzz_difference_name(PreprocessorFuzzDifference difference)
{
    static const char* const names[] = {
        "equal",
        "acceptance",
        "token count",
        "token kind",
        "token spelling",
        "the vectorized kernels and their scalar twins accept differently or give other numbers of diagnostics",
        "the vectorized kernels and their scalar twins give different streams",
        "the vectorized kernels and their scalar twins give different directive tables",
        "the vectorized and scalar printers give different text",
        "the -P text doesn't lex back into the stream",
        "invalid",
    };
    return difference < BUSTER_ARRAY_LENGTH(names) ? names[difference] : "unknown";
}

// ---------------------------------------------------------------------------------------------------------------------
// Our side of a case

// clang's oracle configuration (preprocessor_fuzz_clang): -undef -nostdinc -std=c17 or -std=c23. The runs keep their
// expansion starts, since the kernels' check prints them with line markers too.
static PPOptions pp_fuzz_options(bool c23, bool scalar, bool quiet)
{
    const PPOptions result = {
        .language = {
            .standard = c23 ? 2023 : 2017,
            .gnu = false,
        },
        .scalar = scalar,
        .no_standard_includes = true,
        .expansion_starts = true,
        .trigraphs = !c23,
        .undefine = true,
        .clang_features = true,
        .quiet = quiet,
    };
    return result;
}

// The text clang reads for a case alone, in `arena`.
static bool pp_fuzz_text(Arena* arena, const PreprocessorFuzzCase* test_case, String8* text)
{
    const uint64_t capacity = BUSTER_PPF_MAX_CASE_BYTES + 128;
    PreprocessorFuzzBuffer buffer = {
        .data = arena_push(arena, capacity, 64),
        .capacity = capacity,
    };
    buffer.valid = buffer.data != 0;
    const PreprocessorFuzzCase* cases[1] = {
        test_case,
    };
    const bool result = preprocessor_fuzz_batch_source(&buffer, cases, 1);
    if (result)
    {
        *text = (String8){
            .pointer = buffer.data,
            .length = buffer.length,
        };
    }
    return result;
}

// Preprocesses `text` with the first `kernels` sets of kernels: state->runs[0] the vectorized ones, state->runs[1] the
// scalar twins, each from a reset intern table; state->ran[k] says whether run k had no errors. With both sets, also
// state->runs[2] and [3], the same without expansion starts, as a compile runs them: the main loop takes some direct
// paths only then (pp_fuzz_kernels compares them too); and run 2 defers its history (PPOptions.defer_history), as -E
// and `parse` do, which pp_fuzz_kernels records by preprocessing the text again (pp_run_source's replay). The runs stay
// until pp_fuzz_release. Returns false if a preprocessor can't be set up.
static bool pp_fuzz_run(PPFuzzState* state, bool c23, bool quiet, String8 text, uint32_t kernels)
{
    bool result = text.length <= UINT32_MAX;
    const uint32_t runs = kernels == 2 ? 4 : kernels;
    for (uint32_t k = 0; result && k < runs; k += 1)
    {
        PPOptions options = pp_fuzz_options(c23, k % 2 == 1, quiet || k >= 2);
        options.expansion_starts = k < 2;
        options.defer_history = k == 2;
        const bool table = state->tables_ready[k] ? intern_reset(&state->tables[k]) : intern_initialize(&state->tables[k]);
        state->tables_ready[k] = table;
        result = table && pp_initialize(&state->runs[k], &options, &state->tables[k]);
        state->ran[k] = result && pp_run_source(&state->runs[k], BUSTER_PP_FUZZ_NAME, (const uint8_t*)text.pointer, (uint32_t)text.length);
    }
    return result;
}

static void pp_fuzz_release(PPFuzzState* state)
{
    for (uint32_t k = 0; k < 4; k += 1)
    {
        pp_release(&state->runs[k]);
        state->ran[k] = false;
    }
}

// The bytes of the file range `location` is in (null for a scratch spelling: clang writes each of those on a line of
// its own, so none is next to another token), with `location`'s offset in them and their length.
static const uint8_t* pp_fuzz_bytes(const Preprocessor* pp, uint32_t location, uint32_t* offset, uint32_t* available)
{
    const uint32_t range = pp_find_range(pp, location);
    const uint8_t* result = 0;
    if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
    {
        result = pp->ranges[range].bytes;
        *offset = location - pp->ranges[range].base;
        *available = pp->ranges[range].length;
    }
    return result;
}

// Where an identifier spelled from `offset` ends in the source: identifier characters, UCNs and line splices (with
// trigraphs, ??/ too), as clang's token's length counts them.
static uint32_t pp_fuzz_identifier_end(const uint8_t* bytes, uint32_t offset, uint32_t available, bool trigraphs)
{
    uint32_t at = offset;
    bool going = true;
    while (going && at < available)
    {
        const uint8_t c = bytes[at];
        const uint32_t backslash = c == '\\' ? 1 : trigraphs && c == '?' && at + 2 < available && bytes[at + 1] == '?' && bytes[at + 2] == '/' ? 3 : 0;
        uint32_t next = at + backslash;
        while (backslash != 0 && next < available && (bytes[next] == ' ' || bytes[next] == '\t'))
        {
            next += 1;
        }
        const bool splice = backslash != 0 && next < available && (bytes[next] == '\n' || bytes[next] == '\r');
        const bool ucn = backslash == 1 && at + 1 < available && (bytes[at + 1] == 'u' || bytes[at + 1] == 'U');
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c >= 0x80)
        {
            at += 1;
        }
        else if (splice)
        {
            at = next + (bytes[next] == '\r' && next + 1 < available && bytes[next + 1] == '\n' ? 2 : 1);
        }
        else if (ucn)
        {
            const uint32_t digits = bytes[at + 1] == 'u' ? 4 : 8;
            at += at + 2 + digits <= available ? 2 + digits : available - at;
        }
        else
        {
            going = false;
        }
    }
    return at;
}

// Whether clang's -E printer puts a space between `previous` and `token` that have no whitespace between them, as
// TokenConcatenation::AvoidConcat decides for C: none when they were next to each other in the source (clang's tokens
// start at the line splices before them), and otherwise when `token`'s first character would add to `previous`.
static bool pp_fuzz_avoid_concat(Preprocessor* pp, const PPTokens* out, uint32_t i)
{
    const uint8_t previous = out->kinds[i - 1] >= BUSTER_PP_KIND_KEYWORD && out->kinds[i - 1] < BUSTER_PP_KIND_PRAGMA_END ? C_TOKEN_RAW_IDENTIFIER : out->kinds[i - 1];
    const uint8_t kind = out->kinds[i] >= BUSTER_PP_KIND_KEYWORD && out->kinds[i] < BUSTER_PP_KIND_PRAGMA_END ? C_TOKEN_RAW_IDENTIFIER : out->kinds[i];
    const uint8_t before = i >= 2 ? out->kinds[i - 2] : 0;
    uint8_t buffer[512];
    const uint8_t* spelling = 0;
    const uint32_t spelled = kind == C_TOKEN_RAW_IDENTIFIER ? intern_length(pp->table, out->values[i]) : out->values[i] - out->locations[i];
    const uint32_t length = spelled < sizeof(buffer) ? pp_spelling(pp, out->kinds[i], out->values[i], out->locations[i], buffer, &spelling) : 0;
    const uint8_t first = kind == C_TOKEN_RAW_IDENTIFIER ? intern_bytes(pp->table, out->values[i])[0] : length != 0 ? spelling[0] : 0;
    bool result = false;
    switch (previous)
    {
        case C_TOKEN_RAW_IDENTIFIER:
        {
            if (kind == C_TOKEN_NUMERIC_CONSTANT)
            {
                result = first != '.';
            }
            else if (kind == C_TOKEN_RAW_IDENTIFIER || (kind >= C_TOKEN_WIDE_CHAR_CONSTANT && kind <= C_TOKEN_UTF32_CHAR_CONSTANT) ||
                     (kind >= C_TOKEN_WIDE_STRING_LITERAL && kind <= C_TOKEN_UTF32_STRING_LITERAL))
            {
                result = true;
            }
            else if (kind == C_TOKEN_CHAR_CONSTANT || kind == C_TOKEN_STRING_LITERAL)
            {
                // L before a narrow literal: in C, clang's printer takes no other prefix for one (IsStringPrefix has u,
                // U and u8 only for C++11), and joins U and "" into a UTF-32 string.
                const uint8_t* name = intern_bytes(pp->table, out->values[i - 1]);
                const uint32_t n = intern_length(pp->table, out->values[i - 1]);
                result = n == 1 && name[0] == 'L';
            }
        } break;
        case C_TOKEN_NUMERIC_CONSTANT:
        {
            result = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || (first >= '0' && first <= '9') || first == '_' || first == '$' || first == '.' ||
                     first == '+' || first == '-';
        } break;
        case C_TOKEN_PERIOD:
        {
            result = (first == '.' && before == C_TOKEN_PERIOD) || (first >= '0' && first <= '9');
        } break;
        case C_TOKEN_AMP:
        {
            result = first == '&';
        } break;
        case C_TOKEN_PLUS:
        {
            result = first == '+';
        } break;
        case C_TOKEN_MINUS:
        {
            result = first == '-' || first == '>';
        } break;
        case C_TOKEN_SLASH:
        {
            result = first == '*' || first == '/';
        } break;
        case C_TOKEN_LESS:
        {
            result = first == '<' || first == ':' || first == '%';
        } break;
        case C_TOKEN_GREATER:
        {
            result = first == '>';
        } break;
        case C_TOKEN_PIPE:
        {
            result = first == '|';
        } break;
        case C_TOKEN_PERCENT:
        {
            result = first == '>' || first == ':';
        } break;
        case C_TOKEN_COLON:
        {
            result = first == '>';
        } break;
        case C_TOKEN_HASH:
        {
            result = first == '#' || first == '@' || first == '%';
        } break;
        default:
        {
        } break;
    }
    // Before '=' or '==', the punctuators that would take it.
    const bool takes_equal = previous == C_TOKEN_AMP || previous == C_TOKEN_PLUS || previous == C_TOKEN_MINUS || previous == C_TOKEN_SLASH || previous == C_TOKEN_LESS ||
                             previous == C_TOKEN_GREATER || previous == C_TOKEN_PIPE || previous == C_TOKEN_PERCENT || previous == C_TOKEN_STAR ||
                             previous == C_TOKEN_EXCLAIM || previous == C_TOKEN_LESSLESS || previous == C_TOKEN_GREATERGREATER || previous == C_TOKEN_CARET ||
                             previous == C_TOKEN_EQUAL;
    result |= takes_equal && (kind == C_TOKEN_EQUAL || kind == C_TOKEN_EQUALEQUAL);
    if (result)
    {
        // Next to each other in the source, they lexed apart there: no space.
        uint32_t offset = 0;
        uint32_t available = 0;
        const uint8_t* bytes = pp_fuzz_bytes(pp, out->locations[i - 1], &offset, &available);
        uint32_t token_offset = 0;
        uint32_t token_available = 0;
        const uint8_t* token_bytes = pp_fuzz_bytes(pp, out->locations[i], &token_offset, &token_available);
        const bool identifier = previous == C_TOKEN_RAW_IDENTIFIER;
        const uint32_t end = identifier && bytes != 0 ? out->locations[i - 1] + (pp_fuzz_identifier_end(bytes, offset, available, pp->options.trigraphs) - offset) : out->values[i - 1];
        const uint32_t start = token_bytes != 0 ? out->locations[i] - (token_offset - pp_start_with_splices(token_bytes, token_offset, pp->options.trigraphs)) : out->locations[i];
        result = !(bytes != 0 && token_bytes != 0 && bytes == token_bytes && end == start);
    }
    return result;
}

// Whether clang's token has a leading space where ours has BUSTER_PP_SPACE: not for one whose line start is a name's
// that had only its line's start before it (BUSTER_PP_NAME_LINE), and for another that started a line among a macro's
// arguments (BUSTER_PP_LINE_SPACE), whose own flags clang keeps, only for whitespace or a comment between the line's
// start and the token (Lexer::SkipWhitespace looks at the character before it). One whose line start is another
// name's (BUSTER_PP_NAME_SPACE) has that name's space.
static bool pp_fuzz_leading_space(Preprocessor* pp, const PPTokens* out, uint32_t i)
{
    bool result = !(out->flags[i] & BUSTER_PP_NAME_LINE);
    if (result && (out->flags[i] & BUSTER_PP_LINE_SPACE) && !(out->flags[i] & BUSTER_PP_NAME_SPACE))
    {
        uint32_t offset = 0;
        uint32_t available = 0;
        const uint8_t* bytes = pp_fuzz_bytes(pp, out->locations[i], &offset, &available);
        const uint32_t start = bytes != 0 ? pp_start_with_splices(bytes, offset, pp->options.trigraphs) : 0;
        result = bytes == 0 || (start != 0 && bytes[start - 1] != '\n' && bytes[start - 1] != '\r');
    }
    return result;
}

// The presumed line of `location` in a file, or BUSTER_PP_NONE for a scratch spelling's.
static uint32_t pp_fuzz_presumed_line(Preprocessor* pp, uint32_t location)
{
    const uint32_t range = pp_find_range(pp, location);
    const char* name = 0;
    uint32_t length = 0;
    return range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE ? pp_presumed(pp, location, &name, &length) : BUSTER_PP_NONE;
}

// The presumed line of `location`, and its column in *column (0 for a scratch spelling's).
static uint32_t pp_fuzz_line_at(Preprocessor* pp, uint32_t location, uint32_t* column)
{
    const uint32_t range = pp_find_range(pp, location);
    *column = 0;
    if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
    {
        pp_line_of(pp, range, location, column);
    }
    return pp_fuzz_presumed_line(pp, location);
}

// The presumed line where -E places output token `i` when it starts a line, as pp_print_location places it (its
// expansion's start for a token a macro made, the directive's name for a #pragma's '#'), with *at the expansion starts'
// cursor, which moves forward; the place in *place, and its column in *column (0 for a scratch spelling's). Clang's
// printer has the place where its lexer starts the token there, with any line splices right before it.
static uint32_t pp_fuzz_place_line(Preprocessor* pp, uint32_t i, uint32_t* at, uint32_t* place, uint32_t* column)
{
    const PPTokens* out = &pp->output;
    while (*at < pp->expansion_start_count && pp->expansion_starts[*at].token <= i)
    {
        *at += 1;
    }
    const bool directive = out->kinds[i] == C_TOKEN_HASH && out->locations[i] == pp->hash_location && !(out->flags[i] & BUSTER_PP_FROM_MACRO) && i + 1 < out->count;
    const uint32_t location = (out->flags[i] & BUSTER_PP_FROM_MACRO) && *at != 0 ? pp->expansion_starts[*at - 1].location : directive ? out->locations[i + 1] : out->locations[i];
    *place = pp_lexed_start(pp, location);
    return pp_fuzz_line_at(pp, *place, column);
}

// Whether clang's token has a leading space where -P puts output token `i` first on a line, at `place`, which its
// printer's indentation counts (HandleWhitespaceBeforeTok: column 1 with one is column 2). Our BUSTER_PP_SPACE counts a
// line's start, clang's only whitespace or a comment before the token on its line: a token's own from a file or from a
// line among a macro's arguments (BUSTER_PP_LINE_SPACE), or for the first token of an expansion
// (BUSTER_PP_NAME_SPACE), its name's at the place.
static bool pp_fuzz_line_start_space(Preprocessor* pp, const PPTokens* out, uint32_t i, uint32_t place)
{
    bool result = (out->flags[i] & BUSTER_PP_SPACE) != 0;
    const bool name = (out->flags[i] & BUSTER_PP_NAME_SPACE) != 0;
    if (result && (out->flags[i] & BUSTER_PP_LINE_SPACE))
    {
        result = pp_fuzz_leading_space(pp, out, i);
    }
    else if (result && name && !(out->flags[i] & BUSTER_PP_FROM_MACRO))
    {
        // A file token's space is a name's only when an empty expansion before it passed one on (pp_with_pending).
        result = !(out->flags[i] & BUSTER_PP_NAME_LINE);
    }
    else if (result && (name || !(out->flags[i] & BUSTER_PP_FROM_MACRO)))
    {
        uint32_t offset = 0;
        uint32_t available = 0;
        const uint8_t* bytes = pp_fuzz_bytes(pp, name ? place : pp_lexed_start(pp, out->locations[i]), &offset, &available);
        result = bytes != 0 && offset != 0 && bytes[offset - 1] != '\n' && bytes[offset - 1] != '\r';
    }
    return result;
}

// Whether a string's spelling has a quote or a newline inside it, as stringizing an unterminated literal leaves them
// (clang escapes only literals' quotes): it lexes as several tokens, the last of which runs on to its line's end.
static bool pp_fuzz_broken_string(const uint8_t* bytes, uint32_t length)
{
    bool result = false;
    bool escaped = false;
    for (uint32_t k = 1; !result && k + 1 < length; k += 1)
    {
        result = (bytes[k] == '"' && !escaped) || bytes[k] == '\n' || bytes[k] == '\r';
        escaped = bytes[k] == '\\' && !escaped;
    }
    return result;
}

// Whether an unknown token's spelling runs on in a printed text past its own end: an unterminated literal's (a quote,
// after a literal's prefix: u8, u, U or L), which runs on to its line's end, or a stray `\`'s, which splices the line
// after it when it ends one. Another unknown token (`@`, or a `##` clang took out of a macro's argument) ends where
// the lexer ends it.
static bool pp_fuzz_runs_on(const uint8_t* spelling, uint64_t length)
{
    const uint64_t prefix = length >= 3 && spelling[0] == 'u' && spelling[1] == '8' ? 2 : length >= 2 && (spelling[0] == 'u' || spelling[0] == 'U' || spelling[0] == 'L') ? 1 : 0;
    return (prefix < length && (spelling[prefix] == '\'' || spelling[prefix] == '"')) || (length != 0 && spelling[0] == '\\');
}

// Whether a token's printed spelling doesn't lex back into it, so that the comparison takes it whole: an unknown token
// that runs on (pp_fuzz_runs_on) or a broken string (pp_fuzz_broken_string).
static bool pp_fuzz_breaks(uint8_t kind, const uint8_t* spelling, uint32_t length)
{
    return (kind == C_TOKEN_UNKNOWN && pp_fuzz_runs_on(spelling, length)) || (kind == C_TOKEN_STRING_LITERAL && pp_fuzz_broken_string(spelling, length));
}

// Our stream printed with clang's -E -P spacing, for the comparison with clang's text: clang's text can join two tokens
// that then lex as one (a number and an identifier that starts with a non-ASCII character, or a `'`; `:` and `:` in
// C), which our printer never does, so the comparison must see clang's spacing on both sides. A token that starts a
// line, and the one after a pragma's line, starts a new line; another gets a space when it had whitespace before it, or
// when clang's AvoidConcat asks for one (pp_fuzz_avoid_concat). Each token is spelled as our printers spell it (a
// pragma line's identifiers as written: pp_spelled_as_written). After a _Pragma that prints nothing, clang's lexer starts
// a line at the next token, and its printer moves to that token's line (its own, or its expansion's: PPExpansionStart
// in Preprocessor.silent_pragmas) only when it is another than the current one (MoveToLine), as after a line splice. A
// line's first token is indented to its column, as clang's printer indents it (HandleWhitespaceBeforeTok), which keeps
// it apart from a `\` that ends the line before. The tokens that don't lex back into themselves (pp_fuzz_breaks) go
// in *whole, in `arena`. `free` the text; null if it can't be made.
static char* pp_fuzz_clang_text(Preprocessor* pp, size_t* length, Arena* arena, PreprocessorFuzzWhole** whole, uint64_t* whole_count)
{
    char* result = 0;
    *length = 0;
    FILE* stream = open_memstream(&result, length);
    const PPTokens* out = &pp->output;
    uint64_t candidates = 0;
    for (uint32_t i = 0; i < out->count; i += 1)
    {
        candidates += out->kinds[i] == C_TOKEN_UNKNOWN || out->kinds[i] == C_TOKEN_STRING_LITERAL;
    }
    PreprocessorFuzzWhole* ranges = candidates != 0 ? arena_push_array(arena, candidates, sizeof(PreprocessorFuzzWhole), _Alignof(PreprocessorFuzzWhole)) : 0;
    uint64_t range_count = 0;
    bool line_start = true;
    bool valid = candidates == 0 || ranges != 0;
    uint32_t pragma = BUSTER_PP_PRAGMA_OUTSIDE;
    uint32_t current_line = BUSTER_PP_NONE;
    uint32_t silent = 0;
    uint32_t at = 0;
    Scratch scratch = scratch_begin(0);
    for (uint32_t i = 0; stream != 0 && valid && i < out->count; i += 1)
    {
        const uint8_t kind = out->kinds[i];
        const bool written = pp_spelled_as_written(pragma, kind);
        pragma = pp_pragma_state(pp, pragma, kind, out->locations[i]);
        uint32_t silent_place = BUSTER_PP_NONE;
        for (; silent < pp->silent_pragma_count && pp->silent_pragmas[silent].token <= i; silent += 1)
        {
            silent_place = pp->silent_pragmas[silent].token == i ? pp->silent_pragmas[silent].location : silent_place;
        }
        if (kind == BUSTER_PP_KIND_PRAGMA_END)
        {
            line_start = true;
        }
        else
        {
            uint32_t column = 0;
            uint32_t place = 0;
            bool starts = (out->flags[i] & BUSTER_PP_START_OF_LINE) || (line_start && i != 0);
            // A token that keeps clang's start of line on the line the printer is on, when that is presumed line 1: the
            // printer takes it for the file's first token (MoveToLine's IsFirstInFile) and indents it by its column in
            // place of a space, without AvoidConcat, so that it can join the token before.
            bool first_in_file = false;
            if (starts || i == 0)
            {
                const uint32_t line = pp_fuzz_place_line(pp, i, &at, &place, &column);
                current_line = line != BUSTER_PP_NONE ? line : current_line;
            }
            else if (out->flags[i] & BUSTER_PP_LINE_SPACE)
            {
                // A token that started a line among a macro's arguments keeps its start of line in clang (but an
                // argument's first): its printer moves to its expansion's line when that is another one.
                const uint32_t line = pp_fuzz_place_line(pp, i, &at, &place, &column);
                starts = line != BUSTER_PP_NONE && line != current_line;
                first_in_file = line == 1 && line == current_line;
                current_line = line != BUSTER_PP_NONE ? line : current_line;
            }
            else if (silent_place != BUSTER_PP_NONE)
            {
                // After a _Pragma that printed nothing: a line break when the token is on another line (where clang's
                // lexer starts it, with any line splices right before it).
                place = pp_lexed_start(pp, (out->flags[i] & BUSTER_PP_FROM_MACRO) ? silent_place : out->locations[i]);
                const uint32_t line = pp_fuzz_line_at(pp, place, &column);
                starts = line != BUSTER_PP_NONE && line != current_line;
                first_in_file = line == 1 && line == current_line;
                current_line = line != BUSTER_PP_NONE ? line : current_line;
            }
            // A first column with a leading space before the token (an empty argument or expansion before it) is 2; a
            // pragma line's '#' isn't indented (clang's printer writes "#pragma" after MoveToLine).
            column = column == 1 && pp_fuzz_line_start_space(pp, out, i, place) ? 2 : column;
            column = kind == C_TOKEN_HASH && out->locations[i] == pp->hash_location ? 0 : column;
            if (starts)
            {
                fputc('\n', stream);
            }
            if (starts || first_in_file)
            {
                for (uint32_t c = 1; c < column; c += 1)
                {
                    fputc(' ', stream);
                }
            }
            else if (!line_start && (((out->flags[i] & BUSTER_PP_SPACE) && pp_fuzz_leading_space(pp, out, i)) || pp_fuzz_avoid_concat(pp, out, i)))
            {
                fputc(' ', stream);
            }
            const uint64_t mark = scratch.arena->length;
            const uint32_t room = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END) ? intern_length(pp->table, out->values[i])
                                                                                                                                    : out->values[i] - out->locations[i];
            uint8_t* buffer = written ? 0 : arena_push(scratch.arena, (uint64_t)room + 1, 1);
            const uint8_t* spelling = 0;
            const uint32_t n = written         ? pp_written_spelling(pp, out->values[i], out->locations[i], scratch.arena, &spelling)
                               : buffer != 0 ? pp_spelling(pp, kind, out->values[i], out->locations[i], buffer, &spelling)
                                             : 0;
            valid = spelling != 0;
            const bool breaks = valid && pp_fuzz_breaks(kind, spelling, n);
            const long start = breaks ? ftell(stream) : 0;
            fwrite(spelling != 0 ? spelling : (const uint8_t*)"", 1, n, stream);
            const long end = breaks ? ftell(stream) : 0;
            valid = valid && (!breaks || (start >= 0 && end > start && end <= UINT32_MAX && range_count < candidates));
            if (valid && breaks)
            {
                ranges[range_count] = (PreprocessorFuzzWhole){
                    .start = (uint32_t)start,
                    .end = (uint32_t)end,
                    .kind = kind,
                    .pragma = pragma != BUSTER_PP_PRAGMA_OUTSIDE,
                };
                range_count += 1;
            }
            arena_pop_to(scratch.arena, mark);
            line_start = false;
        }
    }
    scratch_end(scratch);
    if (stream != 0)
    {
        fputc('\n', stream);
        fclose(stream);
    }
    if (!valid)
    {
        free(result);
        result = 0;
    }
    *whole = ranges;
    *whole_count = valid ? range_count : 0;
    return result;
}

// A run's result as clang's is given: rejected if it had errors, else its -P text with clang's spacing
// (pp_fuzz_clang_text) up to its marker, as preprocessor_fuzz_clang_split splits clang's, in `arena`, with the ranges
// of its tokens that the comparison takes whole. *lost is set when an accepted run's text doesn't have its marker in
// place; the output is then the whole text.
static PreprocessorFuzzOutput pp_fuzz_output(Preprocessor* pp, bool accepted, Arena* arena, bool* lost)
{
    PreprocessorFuzzOutput result = {
        .accepted = false,
        .valid = true,
    };
    *lost = false;
    if (accepted)
    {
        size_t length = 0;
        PreprocessorFuzzWhole* whole = 0;
        uint64_t whole_count = 0;
        char* text = pp_fuzz_clang_text(pp, &length, arena, &whole, &whole_count);
        result.valid = text != 0;
        if (result.valid)
        {
            const String8 printed = {
                .pointer = text,
                .length = length,
            };
            const uint64_t begin = arena->length;
            const bool split = preprocessor_fuzz_clang_split(arena, printed, 1, &result);
            if (split)
            {
                // The split's slice starts after the text's first spaces: the ranges inside it, from there.
                uint64_t offset = 0;
                while (offset < length && preprocessor_fuzz_clang_space(text[offset]))
                {
                    offset += 1;
                }
                result.valid = offset + result.text.length <= length && memcmp(text + offset, result.text.pointer, result.text.length) == 0;
                uint64_t kept = 0;
                const uint64_t slice_end = offset + result.text.length;
                for (uint64_t range = 0; result.valid && range < whole_count; range += 1)
                {
                    // A range past the slice's end only by the whitespace its spelling ends with, which the split left
                    // out, is cut there, and keeps those bytes as its tail (PreprocessorFuzzWhole).
                    const uint64_t over = whole[range].end > slice_end ? whole[range].end - slice_end : 0;
                    bool spaces = whole[range].start < slice_end;
                    for (uint64_t b = 0; spaces && b < over; b += 1)
                    {
                        spaces = preprocessor_fuzz_clang_space(text[slice_end + b]);
                    }
                    char* tail = spaces && over != 0 ? arena_push(arena, over, 1) : 0;
                    if (whole[range].start >= offset && spaces && (over == 0 || tail != 0))
                    {
                        if (over != 0)
                        {
                            memcpy(tail, text + slice_end, over);
                        }
                        whole[kept] = whole[range];
                        whole[kept].start -= (uint32_t)offset;
                        whole[kept].end = (uint32_t)(whole[range].end - over - offset);
                        whole[kept].tail_length = (uint32_t)over;
                        whole[kept].tail = tail;
                        kept += 1;
                    }
                    result.valid = !spaces || over == 0 || tail != 0;
                }
                result.whole = whole;
                result.whole_count = kept;
            }
            else
            {
                arena_pop_to(arena, begin);
                *lost = true;
                char* copy = length <= BUSTER_PPF_MAX_OUTPUT_BYTES ? arena_push(arena, length + 1, 1) : 0;
                if (copy != 0)
                {
                    memcpy(copy, text, length);
                    copy[length] = 0;
                }
                result = (PreprocessorFuzzOutput){
                    .text = {
                        .pointer = copy,
                        .length = length,
                    },
                    .whole = whole,
                    .whole_count = whole_count,
                    .accepted = true,
                    .valid = copy != 0,
                };
            }
            free(text);
        }
    }
    return result;
}

// Our result against clang's, by tokens: our text has clang's spacing (pp_fuzz_clang_text), so the two lex alike, its
// joins included, and clang's follows our tokens that don't lex back into themselves (preprocessor_fuzz_lex_printed).
// An accepted run whose marker is missing or misplaced differs from clang's, whose text always has it in place.
static PreprocessorFuzzDifference pp_fuzz_compare(Arena* scratch, PreprocessorFuzzOutput ours, bool lost, PreprocessorFuzzOutput theirs, bool c23)
{
    const PreprocessorFuzzDifference difference = preprocessor_fuzz_compare_mode(scratch, ours, theirs, c23);
    return lost && difference == BUSTER_PPF_EQUAL ? BUSTER_PPF_TOKEN_COUNT : difference;
}

// Compare independently observed Clang tokens with all of our whole tokens in the selected stream: ordinary output,
// or preserved pragma tokens. Match kinds and full spellings in order, including trailing literal bytes.
static PreprocessorFuzzDifference pp_fuzz_confirm_ends(PreprocessorFuzzOutput ours, const PreprocessorFuzzClangTokens* theirs, bool pragmas)
{
    uint64_t expected = 0;
    for (uint64_t w = 0; w < ours.whole_count; w += 1)
    {
        expected += ours.whole[w].pragma == pragmas;
    }
    uint64_t w = 0;
    uint64_t matched = 0;
    PreprocessorFuzzDifference result = BUSTER_PPF_EQUAL;
    for (uint64_t t = 0; result == BUSTER_PPF_EQUAL && t < theirs->count; t += 1)
    {
        const PreprocessorFuzzClangToken* token = &theirs->tokens[t];
        const bool unknown = token->kind.length == 7 && memcmp(token->kind.pointer, "unknown", 7) == 0 &&
                             pp_fuzz_runs_on((const uint8_t*)token->spelling.pointer, token->spelling.length);
        const bool string = token->kind.length == 14 && memcmp(token->kind.pointer, "string_literal", 14) == 0 &&
                            pp_fuzz_broken_string((const uint8_t*)token->spelling.pointer, (uint32_t)token->spelling.length);
        while (w < ours.whole_count && ours.whole[w].pragma != pragmas)
        {
            w += 1;
        }
        if (token->pragma == pragmas && (unknown || string))
        {
            if (w == ours.whole_count)
            {
                result = BUSTER_PPF_TOKEN_COUNT;
            }
            else
            {
                const PreprocessorFuzzWhole* mine = &ours.whole[w];
                const uint64_t length = (uint64_t)mine->end - mine->start;
                const bool same_kind = (mine->kind == C_TOKEN_UNKNOWN) == unknown;
                const bool same_spelling = token->spelling.length == length + mine->tail_length && memcmp(token->spelling.pointer, ours.text.pointer + mine->start, length) == 0 &&
                                           (mine->tail_length == 0 || memcmp(token->spelling.pointer + length, mine->tail, mine->tail_length) == 0);
                result = !same_kind ? BUSTER_PPF_TOKEN_KIND : !same_spelling ? BUSTER_PPF_TOKEN_SPELLING : BUSTER_PPF_EQUAL;
                w += 1;
                matched += 1;
            }
        }
    }
    return result == BUSTER_PPF_EQUAL && matched != expected ? BUSTER_PPF_TOKEN_COUNT : result;
}

// Where our whole tokens off pragma lines end, which clang's text followed (preprocessor_fuzz_lex_printed), clang's own
// tokens for the case alone confirm (preprocessor_fuzz_clang_tokens): its unknown tokens that run on (pp_fuzz_runs_on)
// and broken strings (pp_fuzz_broken_string), in order, must be those tokens of ours, of their kinds and spellings. A
// pragma's tokens are among neither. Returns the difference (BUSTER_PPF_EQUAL if there is none, or no such token of
// ours), or BUSTER_PPF_INVALID if requested token evidence can't be had. The optional external pragma oracle confirms
// preserved pragma tokens. Sets *checked when there were tokens to confirm, and *unverified when a continuing pragma
// token's end still has no independent evidence.
static PreprocessorFuzzDifference pp_fuzz_check_ends(Arena* scratch, const char* clang_path, bool c23, const PreprocessorFuzzCase* test_case, PreprocessorFuzzOutput ours, bool* checked,
                                                     bool* unverified)
{
    const uint64_t begin = scratch->length;
    uint64_t followed = 0;
    uint64_t unconfirmed = 0;
    for (uint64_t w = 0; ours.accepted && w < ours.whole_count; w += 1)
    {
        followed += !ours.whole[w].pragma;
        unconfirmed += ours.whole[w].pragma && preprocessor_fuzz_whole_runs_on(ours.text, &ours.whole[w]);
    }
    *checked = followed != 0;
    PreprocessorFuzzClangTokens theirs = { 0 };
    PreprocessorFuzzDifference result = BUSTER_PPF_EQUAL;
    if (*checked && !preprocessor_fuzz_clang_tokens(scratch, clang_path, c23, test_case, &theirs))
    {
        result = BUSTER_PPF_INVALID;
    }
    if (*checked && result == BUSTER_PPF_EQUAL)
    {
        result = pp_fuzz_confirm_ends(ours, &theirs, false);
    }
    const char* oracle = getenv("BUSTER_PPF_PRAGMA_ORACLE");
    if (result == BUSTER_PPF_EQUAL && unconfirmed != 0 && oracle != 0 && oracle[0] != 0)
    {
        PreprocessorFuzzClangTokens pragmas = { 0 };
        result = preprocessor_fuzz_clang_pragma_tokens(scratch, clang_path, oracle, c23, test_case, &pragmas) ? pp_fuzz_confirm_ends(ours, &pragmas, true) : BUSTER_PPF_INVALID;
        *checked = true;
        for (uint64_t p = 0; result == BUSTER_PPF_EQUAL && p < ours.whole_count; p += 1)
        {
            unconfirmed -= ours.whole[p].pragma && preprocessor_fuzz_whole_runs_on(ours.text, &ours.whole[p]);
        }
    }
    *unverified = result == BUSTER_PPF_EQUAL && unconfirmed != 0;
    arena_pop_to(scratch, begin);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The kernels

// Whether a stream's -P text can't lex back into it, as clang's text for it can't either: an unterminated literal runs
// to its line's end, so a token after it on its line lexes into it; a stray `\` that ends a line, or an unterminated
// literal that ends in one, splices it to the next; one that ends in a line splice is spelled with a newline; and
// stringizing an unterminated literal leaves its quote, or that newline, in the string (clang escapes only literals'
// quotes).
// Sets state->unprintable; pp_fuzz counts these cases.
static bool pp_fuzz_unprintable(Preprocessor* pp, PPFuzzState* state)
{
    const PPTokens* out = &pp->output;
    bool result = false;
    for (uint32_t i = 0; !result && i < out->count; i += 1)
    {
        if (out->kinds[i] == C_TOKEN_STRING_LITERAL)
        {
            result = pp_fuzz_broken_string(pp_location_bytes(pp, out->locations[i]), out->values[i] - out->locations[i]);
        }
        else if (out->kinds[i] == C_TOKEN_UNKNOWN)
        {
            const uint8_t* bytes = pp_location_bytes(pp, out->locations[i]);
            const uint32_t length = out->values[i] - out->locations[i];
            uint32_t quote = 0;
            while (quote < length && quote < 2 && (bytes[quote] == 'L' || bytes[quote] == 'u' || bytes[quote] == 'U' || bytes[quote] == '8'))
            {
                quote += 1;
            }
            const bool literal = quote < length && (bytes[quote] == '"' || bytes[quote] == '\'');
            const bool next_on_line = i + 1 < out->count && (out->flags[i + 1] & BUSTER_PP_START_OF_LINE) == 0 && out->kinds[i + 1] != BUSTER_PP_KIND_PRAGMA_END;
            // Its text ends its line with a `\`, a stray one's or an unterminated literal's: it splices the next. An
            // unterminated literal that ends in a line splice is spelled with the newline after it (pp_trailing_splice).
            const bool spliced = length != 0 && (bytes[length - 1] == '\n' || bytes[length - 1] == '\r');
            result = (literal && next_on_line) || (length != 0 && bytes[length - 1] == '\\' && !next_on_line) || (literal && spliced);
        }
    }
    state->unprintable = result;
    return result;
}

// The two runs of a case must agree as test-preprocessor's cases' do: the acceptance and the counts of errors and
// warnings, the streams token for token (kind, flags, value and location), the directive tables, and the printers byte
// for byte, with line markers and without; and the -P text must lex back into the stream's spellings, unless it can't
// (pp_fuzz_unprintable). As a compile runs them (runs 2 and 3), the acceptance, the counts, the streams and the history,
// run 2's recorded only when it is asked for (pp_history_ensure). Returns the first check that fails (BUSTER_PPF_EQUAL if
// none), which is printed under `name` unless pp_test_quiet.
static PreprocessorFuzzDifference pp_fuzz_kernels(PPFuzzState* state, const char* name)
{
    Preprocessor* runs = state->runs;
    PreprocessorFuzzDifference result = BUSTER_PPF_EQUAL;
    if (state->ran[0] != state->ran[1] || runs[0].errors != runs[1].errors || runs[0].warnings != runs[1].warnings)
    {
        result = BUSTER_PPF_KERNEL_RESULTS;
        if (!pp_test_quiet)
        {
            printf("%s: the vectorized and scalar kernels give %u and %u errors and %u and %u warnings\n", name, runs[0].errors, runs[1].errors, runs[0].warnings,
                   runs[1].warnings);
        }
    }
    else if (!pp_same_streams(&runs[0], &runs[1], name))
    {
        result = BUSTER_PPF_KERNEL_STREAMS;
    }
    else if (!pp_same_directive_tables(&runs[0], &runs[1], name))
    {
        result = BUSTER_PPF_KERNEL_TABLES;
    }
    else if (state->ran[2] != state->ran[3] || runs[2].errors != runs[3].errors || runs[2].warnings != runs[3].warnings)
    {
        result = BUSTER_PPF_KERNEL_RESULTS;
        if (!pp_test_quiet)
        {
            printf("%s (as a compile runs it): the vectorized and scalar kernels give %u and %u errors and %u and %u warnings\n", name, runs[2].errors,
                   runs[3].errors, runs[2].warnings, runs[3].warnings);
        }
    }
    else
    {
        // Run 2's history, deferred, recorded now by preprocessing the text again, must be the history run 3 recorded.
        char label[256];
        snprintf(label, sizeof(label), "%s (as a compile runs it)", name);
        const bool recorded = pp_history_ensure(&runs[2]);
        result = recorded && pp_same_streams(&runs[2], &runs[3], label) && pp_test_system_levels(&runs[2], &runs[3], label) ? BUSTER_PPF_EQUAL :
                                                                                                                       BUSTER_PPF_KERNEL_STREAMS;
        if (!recorded && !pp_test_quiet)
        {
            printf("%s: the deferred history couldn't be recorded\n", label);
        }
    }
    for (uint32_t markers = 0; result == BUSTER_PPF_EQUAL && markers < 2; markers += 1)
    {
        size_t vector_length = 0;
        size_t scalar_length = 0;
        char* vector_text = pp_test_print(&runs[0], true, markers != 0, &vector_length);
        char* scalar_text = pp_test_print(&runs[0], false, markers != 0, &scalar_length);
        if (vector_text == 0 || scalar_text == 0 || vector_length != scalar_length || memcmp(vector_text, scalar_text, vector_length) != 0)
        {
            result = BUSTER_PPF_KERNEL_PRINTERS;
            if (!pp_test_quiet)
            {
                printf("%s: the printers differ%s (%zu and %zu bytes)\n", name, markers ? " with line markers" : "", vector_length, scalar_length);
            }
        }
        free(vector_text);
        free(scalar_text);
    }
    if (result == BUSTER_PPF_EQUAL && !pp_fuzz_unprintable(&runs[0], state))
    {
        PPSpellings ours = { 0 };
        PPSpellings printed = { 0 };
        size_t length = 0;
        char* text = pp_test_print(&runs[0], true, false, &length);
        const bool lexed = text != 0 && pp_stream_spellings(&runs[0], &ours) && pp_text_spellings((const uint8_t*)text, length, runs[0].split_kinds, pp_test_lex_flags(&runs[0]), &printed);
        char label[160];
        snprintf(label, sizeof(label), "%s (printed)", name);
        result = lexed && pp_compare_spellings(&runs[0], &ours, &printed, label, "-E") ? BUSTER_PPF_EQUAL : BUSTER_PPF_KERNEL_PRINTED;
        arena_release(&ours.arena);
        arena_release(&printed.arena);
        free(text);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The oracles the minimizer calls

// BUSTER_PPF_ORACLE_CLANG: `source`, as case oracle->id alone, by the vectorized kernels against clang.
static PreprocessorFuzzDifference pp_fuzz_clang_difference(Arena* scratch, const PreprocessorFuzzOracle* oracle, String8 source)
{
    const uint64_t begin = scratch->length;
    PreprocessorFuzzDifference result = BUSTER_PPF_INVALID;
    const PreprocessorFuzzCase test_case = {
        .source = source,
        .id = oracle->id,
        .isolated = true,
    };
    String8 text = { 0 };
    if (pp_fuzz_text(scratch, &test_case, &text) && pp_fuzz_run(&pp_fuzz_state, oracle->c23, true, text, 1))
    {
        bool lost = false;
        const PreprocessorFuzzOutput ours = pp_fuzz_output(&pp_fuzz_state.runs[0], pp_fuzz_state.ran[0], scratch, &lost);
        PreprocessorFuzzOutput theirs = { 0 };
        if (ours.valid && preprocessor_fuzz_clang(scratch, oracle->clang_path, oracle->c23, &test_case, 1, true, &theirs))
        {
            result = pp_fuzz_compare(scratch, ours, lost, theirs, oracle->c23);
            bool checked = false;
            bool unverified = false;
            result = result == BUSTER_PPF_EQUAL ? pp_fuzz_check_ends(scratch, oracle->clang_path, oracle->c23, &test_case, ours, &checked, &unverified) : result;
        }
    }
    pp_fuzz_release(&pp_fuzz_state);
    arena_pop_to(scratch, begin);
    return result;
}

// BUSTER_PPF_ORACLE_KERNELS: `source`, as case oracle->id, by the vectorized kernels against their scalar twins.
static PreprocessorFuzzDifference pp_fuzz_kernels_difference(Arena* scratch, const PreprocessorFuzzOracle* oracle, String8 source)
{
    const uint64_t begin = scratch->length;
    PreprocessorFuzzDifference result = BUSTER_PPF_INVALID;
    const PreprocessorFuzzCase test_case = {
        .source = source,
        .id = oracle->id,
    };
    String8 text = { 0 };
    if (pp_fuzz_text(scratch, &test_case, &text) && pp_fuzz_run(&pp_fuzz_state, oracle->c23, true, text, 2))
    {
        const bool quiet = pp_test_quiet;
        pp_test_quiet = true;
        result = pp_fuzz_kernels(&pp_fuzz_state, "");
        pp_test_quiet = quiet;
    }
    pp_fuzz_release(&pp_fuzz_state);
    arena_pop_to(scratch, begin);
    return result;
}

static PreprocessorFuzzDifference preprocessor_fuzz_difference(Arena* scratch, const PreprocessorFuzzOracle* oracle, String8 source)
{
    PreprocessorFuzzDifference result = BUSTER_PPF_INVALID;
    switch (oracle->kind)
    {
        case BUSTER_PPF_ORACLE_PLANTED:
        {
            result = preprocessor_fuzz_planted_difference(scratch, source);
        } break;
        case BUSTER_PPF_ORACLE_CLANG:
        {
            result = pp_fuzz_clang_difference(scratch, oracle, source);
        } break;
        case BUSTER_PPF_ORACLE_KERNELS:
        {
            result = pp_fuzz_kernels_difference(scratch, oracle, source);
        } break;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Reports

static void pp_fuzz_print_output(const char* side, PreprocessorFuzzOutput output)
{
    if (!output.valid)
    {
        printf("  %-6s no result\n", side);
    }
    else if (!output.accepted)
    {
        printf("  %-6s rejected\n", side);
    }
    else
    {
        const int shown = output.text.length < BUSTER_PP_FUZZ_SHOWN ? (int)output.text.length : BUSTER_PP_FUZZ_SHOWN;
        printf("  %-6s %.*s%s\n", side, shown, output.text.pointer, output.text.length > BUSTER_PP_FUZZ_SHOWN ? " ..." : "");
    }
}

// Saves a minimized case as fuzz_failure_N.c, with the first N no file has. Returns N, or 0 if it can't.
static uint64_t pp_fuzz_save(String8 source)
{
    uint64_t number = 0;
    bool saved = false;
    bool taken = true;
    while (!saved && taken && number < BUSTER_PP_FUZZ_FAILURES)
    {
        number += 1;
        errno = 0;
        saved = preprocessor_fuzz_save(source, number);
        taken = !saved && errno == EEXIST;
    }
    return saved ? number : 0;
}

// Prints a difference that `oracle` found in a case, then minimizes it with that oracle, prints the minimized case with
// both sides' results on it (our diagnostics on stderr), and saves it.
static void pp_fuzz_report(Arena* arena, const PreprocessorFuzzOracle* oracle, uint64_t seed, const PreprocessorFuzzCase* test_case, PreprocessorFuzzDifference difference,
                           const PreprocessorFuzzOutput* ours, const PreprocessorFuzzOutput* theirs, PPFuzzTally* tally)
{
    const uint64_t begin = arena->length;
    const uint32_t family = preprocessor_fuzz_family(test_case->id);
    printf("fuzz: %s, seed %" PRIu64 ", case %" PRIu64 " (family %u, %s): %s\n", oracle->c23 ? "c23" : "c17", seed, test_case->id, family,
           preprocessor_fuzz_construct_names[family], pp_fuzz_difference_name(difference));
    if (ours != 0 && theirs != 0)
    {
        pp_fuzz_print_output("ours", *ours);
        pp_fuzz_print_output("clang", *theirs);
    }
    printf("--- source\n%.*s\n", (int)test_case->source.length, test_case->source.pointer);
    String8 minimized = { 0 };
    const bool quiet = pp_test_quiet;
    pp_test_quiet = true;
    const bool reduced = preprocessor_fuzz_minimize(arena, oracle, test_case->source, &minimized);
    pp_test_quiet = quiet;
    if (reduced)
    {
        const uint64_t number = pp_fuzz_save(minimized);
        tally->saved += number != 0;
        printf("--- minimized");
        if (number != 0)
        {
            printf(" (fuzz_failure_%" PRIu64 ".c)", number);
        }
        printf("\n%.*s\n", (int)minimized.length, minimized.pointer);
        fflush(stdout);
        // Both sides again on the minimized case, with our diagnostics.
        const PreprocessorFuzzCase reduced_case = {
            .source = minimized,
            .id = test_case->id,
            .isolated = true,
        };
        String8 text = { 0 };
        const uint32_t kernels = oracle->kind == BUSTER_PPF_ORACLE_KERNELS ? 2 : 1;
        if (pp_fuzz_text(arena, &reduced_case, &text) && pp_fuzz_run(&pp_fuzz_state, oracle->c23, false, text, kernels))
        {
            fflush(stderr);
            if (kernels == 2)
            {
                pp_fuzz_kernels(&pp_fuzz_state, "--- minimized");
            }
            else
            {
                bool lost = false;
                const PreprocessorFuzzOutput reduced_ours = pp_fuzz_output(&pp_fuzz_state.runs[0], pp_fuzz_state.ran[0], arena, &lost);
                PreprocessorFuzzOutput reduced_theirs = { 0 };
                preprocessor_fuzz_clang(arena, oracle->clang_path, oracle->c23, &reduced_case, 1, true, &reduced_theirs);
                pp_fuzz_print_output("ours", reduced_ours);
                pp_fuzz_print_output("clang", reduced_theirs);
            }
        }
        pp_fuzz_release(&pp_fuzz_state);
    }
    else
    {
        printf("--- the difference didn't minimize\n");
    }
    printf("---\n");
    fflush(stdout);
    arena_pop_to(arena, begin);
}

// ---------------------------------------------------------------------------------------------------------------------
// A worker

// Whether a run leaves something open that would reach into the next case in a batch, so that clang reads its case
// alone: an invocation, an operand, a conditional, a comment or a pragma region left open at the case's end, an #endif
// without its #if (Preprocessor.unterminated), or a diagnostic push left on the stack.
static bool pp_fuzz_leaks(const Preprocessor* pp)
{
    return pp->unterminated != 0 || pp->diagnostic_depth != 0;
}

// A source pp_fuzz_check_isolation runs, and whether it leaves something open into the next case of a batch.
typedef struct PPFuzzIsolation PPFuzzIsolation;
struct PPFuzzIsolation
{
    const char* source;
    bool leaks;
};

static const PPFuzzIsolation pp_fuzz_isolations[] = {
    { "x\n", false },
    { "#if 1\n", true },
    { "#endif\n", true },
    { "#define f(x) x\nf(\n", true },
    { "/* x\n", true },
    { "#pragma clang diagnostic push\n", true },
    { "#pragma clang diagnostic push\n#pragma clang diagnostic pop\n", false },
    { "#pragma clang unsafe_buffer_usage begin\n", true },
    { "#pragma clang assume_nonnull begin\n", true },
    { "#pragma clang arc_cf_code_audited begin\n", true },
    { "#pragma clang unsafe_buffer_usage begin\nx\n#pragma clang unsafe_buffer_usage end\n", false },
    // The text in a region ends in a line splice, which takes the end's line into its own (seed 43's case 409876).
    { "#pragma clang unsafe_buffer_usage begin\nx \\\n#pragma clang unsafe_buffer_usage end\n", true },
};

// Runs each source above in both modes: pp_fuzz_leaks must say whether clang reads it alone. Returns whether every
// source agrees.
static bool pp_fuzz_check_isolation(void)
{
    uint32_t failures = 0;
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp_fuzz_isolations); i += 1)
        {
            const PPFuzzIsolation* isolation = &pp_fuzz_isolations[i];
            const String8 text = {
                .pointer = (char*)isolation->source,
                .length = strlen(isolation->source),
            };
            const bool ran = pp_fuzz_run(&pp_fuzz_state, mode == 1, true, text, 1);
            const bool leaks = ran && pp_fuzz_leaks(&pp_fuzz_state.runs[0]);
            pp_fuzz_release(&pp_fuzz_state);
            if (!ran || leaks != isolation->leaks)
            {
                printf("fuzz isolation: %s, source %u %s\n", mode == 1 ? "c23" : "c17", i, !ran ? "can't run" : isolation->leaks ? "isn't read alone" : "is read alone");
                failures += 1;
            }
        }
    }
    printf("fuzz isolation: %u of %lu sources fail\n", failures, 2 * (unsigned long)BUSTER_ARRAY_LENGTH(pp_fuzz_isolations));
    return failures == 0;
}

// Our text and clang's for a case, which pp_fuzz_compare must find to differ as `difference` says: whitespace between
// tokens doesn't count, but a literal's contents and two tokens joined into one do (#263's review found a comparison of
// spellings without whitespace, which took `"a b" 0` for `"ab" 0`). A `whole_end` other than 0 makes [whole_start,
// whole_end) of ours a token that doesn't lex back into itself (PreprocessorFuzzOutput.whole), as pp_fuzz_output gives
// an unterminated literal's.
typedef struct PPFuzzComparison PPFuzzComparison;
struct PPFuzzComparison
{
    const char* ours;
    uint32_t whole_start;
    uint32_t whole_end;
    bool pragma;
    const char* theirs;
    PreprocessorFuzzDifference difference;
};

static const PPFuzzComparison pp_fuzz_comparisons[] = {
    { "\"a b\" 0\n", 0, 0, false, "\"a b\"\n 0\n", BUSTER_PPF_EQUAL },
    { "\"a b\" 0\n", 0, 0, false, "\"ab\" 0\n", BUSTER_PPF_TOKEN_SPELLING },
    { "'a b' 0\n", 0, 0, false, "'ab' 0\n", BUSTER_PPF_TOKEN_SPELLING },
    { "a b\n", 0, 0, false, "ab\n", BUSTER_PPF_TOKEN_COUNT },
    { "ab\n", 0, 0, false, "a\nb\n", BUSTER_PPF_TOKEN_COUNT },
    { "x\n  y\n", 0, 0, false, "x y\n", BUSTER_PPF_EQUAL },
    // An unterminated literal ends where ours does in both texts: the spaces after it don't count, but its own bytes
    // do, and so does a `'` after it on its line, which would close a character constant.
    { "'a  b x\n", 0, 5, false, "'a  b  x\n", BUSTER_PPF_EQUAL },
    { "'a  b x\n", 0, 5, false, "'a  b\nx\n", BUSTER_PPF_EQUAL },
    { "'a  b x\n", 0, 5, false, "'a b\nx\n", BUSTER_PPF_TOKEN_SPELLING },
    { "'a x 'y'\n", 0, 2, false, "'a  x 'y'\n", BUSTER_PPF_EQUAL },
    { "'a x 'y'\n", 0, 2, false, "'a x 'z'\n", BUSTER_PPF_TOKEN_SPELLING },
    // Without our range, it runs on to its line's end, spaces and all.
    { "'a x\n", 0, 0, false, "'a  x\n", BUSTER_PPF_TOKEN_SPELLING },
    // A string that stringizing an unterminated literal broke (pp_fuzz_broken_string) is whole too.
    { "\"\"a b\" x\n", 0, 6, false, "\"\"a b\"  x\n", BUSTER_PPF_EQUAL },
    { "\"\"a b\" x\n", 0, 6, false, "\"\"a  b\" x\n", BUSTER_PPF_TOKEN_COUNT },
    // A comment's `/` and a line splice's `\`, which only tokens printed next to each other make, are tokens.
    { "/*a b\n", 0, 0, false, "/*a  b\n", BUSTER_PPF_EQUAL },
    { "/*a*/b\n", 0, 0, false, "/*c*/b\n", BUSTER_PPF_TOKEN_SPELLING },
    { "//a b\n", 0, 0, false, "//a\nb\n", BUSTER_PPF_EQUAL },
    { "a \\\nb\n", 0, 0, false, "a \\\n b\n", BUSTER_PPF_EQUAL },
    { "a \\\nb\n", 0, 0, false, "a\nb\n", BUSTER_PPF_TOKEN_COUNT },
    { "a\\\nb\n", 1, 2, false, "a\\\n b\n", BUSTER_PPF_EQUAL },
    // At a pragma's line's end, which the literal is in both texts, clang's text gives its end. Elsewhere the text
    // can't show it, and clang's own tokens do (pp_fuzz_check_ends): the bytes the text took for the printer's are its.
    // On a pragma's line that goes on past it (`#pragma omp` expands its tokens), they can't either: unverified.
    { "#pragma x 'a \n", 10, 13, true, "#pragma x 'a  \n", BUSTER_PPF_TOKEN_SPELLING },
    { "'a \nx\n", 0, 3, false, "'a  \nx\n", BUSTER_PPF_EQUAL },
    { "#pragma x 'a )\n", 10, 12, true, "#pragma x 'a  )\n", BUSTER_PPF_EQUAL },
};

// Runs each pair above through pp_fuzz_compare, in both modes, as accepted outputs. Returns whether every pair gives its
// difference.
static bool pp_fuzz_check_comparison(void)
{
    Arena scratch = arena_reserve(BUSTER_PP_FUZZ_ARENA_BYTES);
    uint32_t failures = scratch.base == 0;
    for (uint32_t mode = 0; scratch.base != 0 && mode < 2; mode += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp_fuzz_comparisons); i += 1)
        {
            const PPFuzzComparison* comparison = &pp_fuzz_comparisons[i];
            const PreprocessorFuzzWhole whole = {
                .start = comparison->whole_start,
                .end = comparison->whole_end,
                .kind = C_TOKEN_UNKNOWN,
                .pragma = comparison->pragma,
            };
            const PreprocessorFuzzOutput ours = {
                .text = {
                    .pointer = (char*)comparison->ours,
                    .length = strlen(comparison->ours),
                },
                .whole = &whole,
                .whole_count = comparison->whole_end != 0,
                .accepted = true,
                .valid = true,
            };
            const PreprocessorFuzzOutput theirs = {
                .text = {
                    .pointer = (char*)comparison->theirs,
                    .length = strlen(comparison->theirs),
                },
                .accepted = true,
                .valid = true,
            };
            const PreprocessorFuzzDifference difference = pp_fuzz_compare(&scratch, ours, false, theirs, mode == 1);
            arena_pop_to(&scratch, 0);
            if (difference != comparison->difference)
            {
                printf("fuzz comparison: %s, pair %u gives %s, not %s\n", mode == 1 ? "c23" : "c17", i, pp_fuzz_difference_name(difference),
                       pp_fuzz_difference_name(comparison->difference));
                failures += 1;
            }
        }
    }
    arena_release(&scratch);
    printf("fuzz comparison: %u of %lu pairs fail\n", failures, 2 * (unsigned long)BUSTER_ARRAY_LENGTH(pp_fuzz_comparisons));
    return failures == 0;
}

// Our side of case `id`: generated into `arena`, preprocessed by both sets of kernels, which must agree, and our result
// (the vectorized run's) into *ours. Returns false if the case can't be generated or preprocessed.
static bool pp_fuzz_case(Arena* arena, Arena* scratch, const PPFuzzOptions* options, bool c23, uint64_t id, PreprocessorFuzzCase* test_case, PreprocessorFuzzOutput* ours,
                         bool* lost, PPFuzzTally* tally)
{
    String8 text = { 0 };
    bool result = preprocessor_fuzz_generate(arena, options->seed, id, c23, test_case) && pp_fuzz_text(arena, test_case, &text);
    result = result && pp_fuzz_run(&pp_fuzz_state, c23, true, text, 2);
    tally->cases += 1;
    if (result)
    {
        char name[96];
        snprintf(name, sizeof(name), "fuzz: %s, seed %" PRIu64 ", case %" PRIu64, c23 ? "c23" : "c17", options->seed, id);
        const bool quiet = pp_test_quiet;
        pp_test_quiet = quiet || tally->reported >= BUSTER_PP_FUZZ_REPORTED;
        const PreprocessorFuzzDifference kernels = pp_fuzz_kernels(&pp_fuzz_state, name);
        pp_test_quiet = quiet;
        tally->unprintable += pp_fuzz_state.unprintable;
        *ours = pp_fuzz_output(&pp_fuzz_state.runs[0], pp_fuzz_state.ran[0], arena, lost);
        tally->accepted += ours->valid && ours->accepted;
        test_case->isolated |= pp_fuzz_leaks(&pp_fuzz_state.runs[0]);
        result = ours->valid;
        pp_fuzz_release(&pp_fuzz_state);
        if (kernels != BUSTER_PPF_EQUAL)
        {
            tally->kernels += 1;
            if (tally->reported < BUSTER_PP_FUZZ_REPORTED)
            {
                tally->reported += 1;
                const PreprocessorFuzzOracle oracle = {
                    .kind = BUSTER_PPF_ORACLE_KERNELS,
                    .clang_path = options->clang_path,
                    .id = id,
                    .c23 = c23,
                };
                pp_fuzz_report(arena, &oracle, options->seed, test_case, kernels, 0, 0, tally);
            }
        }
    }
    else
    {
        pp_fuzz_release(&pp_fuzz_state);
        printf("fuzz: %s, seed %" PRIu64 ", case %" PRIu64 ": can't be generated or preprocessed\n", c23 ? "c23" : "c17", options->seed, id);
    }
    tally->invalid += !result;
    return result;
}

// A block's cases against clang: the ones we accept in one batch, then the ones we reject in another. A difference is
// confirmed with clang on the case alone, and in sampled blocks every case is read alone too: the batch must give what
// the case alone gives. Where clang's text followed our whole tokens, clang's own tokens confirm their ends
// (pp_fuzz_check_ends). A batch without a result (clang's output without its markers in place) has each of its cases
// read alone instead, and one that gives no result alone either is reported, as is a case the comparison can't be made
// for.
static void pp_fuzz_against_clang(Arena* arena, const PPFuzzOptions* options, bool c23, bool sampled, const PreprocessorFuzzCase* cases, const PreprocessorFuzzOutput* ours,
                                  const bool* lost, const bool* usable, uint64_t count, PPFuzzTally* tally)
{
    for (uint32_t group = 0; group < 2; group += 1)
    {
        PreprocessorFuzzCase grouped[BUSTER_PP_FUZZ_BLOCK];
        uint64_t indices[BUSTER_PP_FUZZ_BLOCK];
        PreprocessorFuzzOutput theirs[BUSTER_PP_FUZZ_BLOCK];
        bool answered[BUSTER_PP_FUZZ_BLOCK];
        uint64_t grouped_count = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            if (usable[i] && ours[i].accepted == (group == 0))
            {
                grouped[grouped_count] = cases[i];
                indices[grouped_count] = i;
                grouped_count += 1;
            }
        }
        const bool batched = grouped_count == 0 || preprocessor_fuzz_clang(arena, options->clang_path, c23, grouped, grouped_count, false, theirs);
        tally->unbatched += !batched;
        for (uint64_t k = 0; k < grouped_count; k += 1)
        {
            answered[k] = batched || preprocessor_fuzz_clang(arena, options->clang_path, c23, &grouped[k], 1, true, &theirs[k]);
            tally->alone += !batched;
            tally->invalid += !answered[k];
            if (!answered[k])
            {
                printf("fuzz: %s, seed %" PRIu64 ", case %" PRIu64 ": clang gave no result\n", c23 ? "c23" : "c17", options->seed, grouped[k].id);
            }
        }
        for (uint64_t k = 0; k < grouped_count; k += 1)
        {
            const uint64_t i = indices[k];
            PreprocessorFuzzDifference result = answered[k] ? pp_fuzz_compare(arena, ours[i], lost[i], theirs[k], c23) : BUSTER_PPF_EQUAL;
            PreprocessorFuzzOutput alone = theirs[k];
            if (answered[k] && (result != BUSTER_PPF_EQUAL || sampled))
            {
                // Confirmed on the case alone: the batch must give what the case alone gives.
                const bool alone_valid = !batched || preprocessor_fuzz_clang(arena, options->clang_path, c23, &grouped[k], 1, true, &alone);
                tally->alone += batched;
                const PreprocessorFuzzDifference batch = alone_valid ? preprocessor_fuzz_compare_mode(arena, theirs[k], alone, c23) : BUSTER_PPF_INVALID;
                if (batch != BUSTER_PPF_EQUAL && batch != BUSTER_PPF_INVALID)
                {
                    tally->oracle += 1;
                    printf("fuzz: %s, seed %" PRIu64 ", case %" PRIu64 ": clang's batch gives another result than the case alone\n", c23 ? "c23" : "c17",
                           options->seed, grouped[k].id);
                }
                result = batch != BUSTER_PPF_INVALID ? pp_fuzz_compare(arena, ours[i], lost[i], alone, c23) : BUSTER_PPF_INVALID;
            }
            bool checked = false;
            bool unverified = false;
            if (answered[k] && result == BUSTER_PPF_EQUAL)
            {
                result = pp_fuzz_check_ends(arena, options->clang_path, c23, &grouped[k], ours[i], &checked, &unverified);
            }
            // A comparison that can't be made leaves the case without a result.
            tally->compared += answered[k] && result != BUSTER_PPF_INVALID && !unverified;
            tally->ends += checked && result != BUSTER_PPF_INVALID;
            tally->unverified += unverified;
            tally->invalid += answered[k] && result == BUSTER_PPF_INVALID;
            if (unverified && !pp_test_quiet)
            {
                printf("fuzz: %s, seed %" PRIu64 ", case %" PRIu64 ": clang's pragma token ends are unverified\n", c23 ? "c23" : "c17", options->seed, grouped[k].id);
            }
            if (answered[k] && result == BUSTER_PPF_INVALID && !pp_test_quiet)
            {
                printf("fuzz: %s, seed %" PRIu64 ", case %" PRIu64 ": the comparison with clang gave no result\n", c23 ? "c23" : "c17", options->seed, grouped[k].id);
            }
            if (answered[k] && result != BUSTER_PPF_EQUAL && result != BUSTER_PPF_INVALID)
            {
                tally->differences += 1;
                if (tally->reported < BUSTER_PP_FUZZ_REPORTED)
                {
                    tally->reported += 1;
                    const PreprocessorFuzzOracle oracle = {
                        .kind = BUSTER_PPF_ORACLE_CLANG,
                        .clang_path = options->clang_path,
                        .id = grouped[k].id,
                        .c23 = c23,
                    };
                    pp_fuzz_report(arena, &oracle, options->seed, &grouped[k], result, &ours[i], &alone, tally);
                }
            }
        }
    }
}

// One worker's share of a mode's cases, ids [begin, end), a block at a time.
static void pp_fuzz_worker(const PPFuzzOptions* options, bool c23, uint64_t begin, uint64_t end, PPFuzzTally* tally)
{
    Arena arena = arena_reserve(BUSTER_PP_FUZZ_ARENA_BYTES);
    Arena scratch = arena_reserve(BUSTER_PP_FUZZ_ARENA_BYTES);
    const bool valid = arena.base != 0 && scratch.base != 0;
    tally->invalid += !valid;
    for (uint64_t first = begin; valid && first < end; first += BUSTER_PP_FUZZ_BLOCK)
    {
        const uint64_t count = end - first < BUSTER_PP_FUZZ_BLOCK ? end - first : BUSTER_PP_FUZZ_BLOCK;
        PreprocessorFuzzCase cases[BUSTER_PP_FUZZ_BLOCK];
        PreprocessorFuzzOutput ours[BUSTER_PP_FUZZ_BLOCK];
        bool lost[BUSTER_PP_FUZZ_BLOCK];
        bool usable[BUSTER_PP_FUZZ_BLOCK];
        for (uint64_t i = 0; i < count; i += 1)
        {
            usable[i] = pp_fuzz_case(&arena, &scratch, options, c23, first + i, &cases[i], &ours[i], &lost[i], tally);
        }
        if (options->with_clang)
        {
            const bool sampled = (first - begin) / BUSTER_PP_FUZZ_BLOCK % BUSTER_PP_FUZZ_SAMPLED == 0;
            pp_fuzz_against_clang(&arena, options, c23, sampled, cases, ours, lost, usable, count, tally);
        }
        arena_pop_to(&arena, 0);
    }
    arena_release(&arena);
    arena_release(&scratch);
}

// Sources the comparison once got wrong in its emulation of clang's -P printer (pp_fuzz_clang_text), minimized, with
// the ones probing around them found: with --clang, each must give clang's tokens in both modes
// (pp_fuzz_check_emulation), as test-preprocessor's cases pin what the preprocessor itself gives.
static const char* const pp_fuzz_emulation_cases[] = {
    // An expansion's first token takes its name's line start and space: a name that starts a line among a macro's
    // arguments with only the line's start before it gives no space, so clang prints `u8""`, one literal (seed 33's
    // case 446564, C23). So do a builtin, an object-like macro, a name a name gave, an empty expansion before the
    // token, a name that started a line in another macro's argument, and an argument and a paste first in the
    // expansion; a space or a comment before the name gives one.
    "#define e(...) __VA_ARGS__\n#define c(d, ...) #d\ne(u8\nc())\n",
    "#define e(...) __VA_ARGS__\ne(u8\n__FILE__)\n",
    "#define e(...) __VA_ARGS__\n#define S \"\"\ne(u8\nS)\n",
    "#define e(...) __VA_ARGS__\n#define A B\n#define B \"\"\ne(u8\nA)\n",
    "#define e(...) __VA_ARGS__\n#define E\ne(u8\nE\"\")\n",
    "#define e(...) __VA_ARGS__\n#define f(a) a\n#define g() \"\"\ne(f(u8\ng)())\n",
    "#define e(...) __VA_ARGS__\n#define F(x) x\ne(u8\nF( \"\"))\n",
    "#define e(...) __VA_ARGS__\n#define P(a, b) a ## b\ne(u8\nP(,\"\"))\n",
    "#define e(...) __VA_ARGS__\n#define c(d, ...) #d\ne(u8\n  c())\n",
    "#define e(...) __VA_ARGS__\n#define c(d, ...) #d\ne(u8\n/**/c())\n",
    // An empty expansion's name passes its space on to a token that starts the next line among a macro's arguments,
    // as clang ORs them: a space before either gives one.
    "#define e(...) __VA_ARGS__\n#define E\ne(u8 E\n\"\")\n",
    "#define e(...) __VA_ARGS__\n#define E\ne(u8\nE\n\"\")\n",
    "#define e(...) __VA_ARGS__\n#define E\ne(u8\n E\n\"\")\n",
    // In a file, only to the next token on the same line: clang's lexer clears the space at a newline, so after a `\`
    // that ends its line, the next line's token stays in column 1 (seed 62's case 47024, C17).
    "#define E\nu8 E\"\"\n",
    "#define e(...)\na\\ \n\\ e(\n)\nc\n",
    // A name at presumed line 1 from a line splice right before it, which clang's token starts at: its argument's line
    // there is the file's first to clang's printer, which joins `.5_z` and `4ll` (seed 65's case 702022, C17).
    "#define h(c, d) d\n# 1\n\\\nh((\n),.5_z\n4ll)\n",
    // Any token that keeps clang's start of line on the printer's line, when that is presumed line 1, is the file's first
    // to clang's printer (MoveToLine's IsFirstInFile), which indents it by its column in place of a space (#263's
    // review): a line among a macro's arguments joins the token before in column 1, and is a column further with a
    // leading space; the token after a _Pragma that printed nothing goes to its own column, or to its expansion's.
    "#define h(...) __VA_ARGS__\n# 1\nh(x\ny)\n",
    "#define h(...) __VA_ARGS__\n# 1\nh(x\n  y)\n",
    "# 1\na _Pragma(\"once\")b\n",
    "#define h(...) __VA_ARGS__\n# 1\nh(a _Pragma(\"once\")b)\n",
};

// Each source of pp_fuzz_emulation_cases against clang, alone, in both modes (pp_fuzz_clang_difference). Returns
// whether every one gives clang's tokens.
static bool pp_fuzz_check_emulation(const PPFuzzOptions* options)
{
    Arena scratch = arena_reserve(BUSTER_PP_FUZZ_ARENA_BYTES);
    uint32_t failures = scratch.base == 0;
    for (uint32_t mode = 0; scratch.base != 0 && mode < 2; mode += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp_fuzz_emulation_cases); i += 1)
        {
            const PreprocessorFuzzOracle oracle = {
                .kind = BUSTER_PPF_ORACLE_CLANG,
                .clang_path = options->clang_path,
                .id = i,
                .c23 = mode == 1,
            };
            const String8 source = {
                .pointer = (char*)pp_fuzz_emulation_cases[i],
                .length = strlen(pp_fuzz_emulation_cases[i]),
            };
            if (pp_fuzz_clang_difference(&scratch, &oracle, source) != BUSTER_PPF_EQUAL)
            {
                printf("fuzz emulation: %s, case %u differs from clang\n", mode == 1 ? "c23" : "c17", i);
                failures += 1;
            }
        }
    }
    arena_release(&scratch);
    printf("fuzz emulation: %u of %lu cases differ from clang\n", failures, 2 * (unsigned long)BUSTER_ARRAY_LENGTH(pp_fuzz_emulation_cases));
    return failures == 0;
}

// A source whose output has a token clang's text runs on past (an unterminated literal, or a string stringizing one
// broke), for pp_fuzz_check_ends_cases, and whether the texts' agreement is all there is to confirm it with: the token
// is on a pragma's line with more after it.
typedef struct PPFuzzEndsCase PPFuzzEndsCase;
struct PPFuzzEndsCase
{
    const char* source;
    bool unverified;
};

// One whose literal ends in two spaces, its line's end (#263's second review's), one whose tokens after it its line
// takes, a broken string, one among a macro's arguments, and one on a pragma's line that a `#pragma omp`, whose tokens
// are expanded, goes on past (seed 133's case 832692 in C17). Then literals that end the case's text in whitespace,
// which the case's text leaves out (PreprocessorFuzzWhole.tail): two spaces, the newline after a line splice
// (pp_trailing_splice), and a space a splice took into the literal (seed 3110's case 84082 in C23).
static const PPFuzzEndsCase pp_fuzz_ends_cases[] = {
    {
        "#define A 'a  \nA\n",
        false,
    },
    {
        "\\ '\\\n",
        false,
    },
    {
        "\\ '\\\n#define g \n",
        false,
    },
    {
        "#define A 'a  \nA\nx\n",
        false,
    },
    {
        "#define Q 'q\nQ y z\n",
        false,
    },
    {
        "#define S(x) #x\nS(\"a b\n) c\n",
        false,
    },
    {
        "#define F(x) x\nF('x\nb) c\n",
        false,
    },
    {
        "#define Q 'q\n#pragma omp Q x\n",
        true,
    },
    {
        "#define Q 'q  \n#pragma omp Q __COUNTER__\n__COUNTER__\n",
        true,
    },
    {
        "#define ID(x) x\n#define Q 'q  \n#pragma omp ID(Q) x\n",
        true,
    },
    {
        "#define Q 'q  \n_Pragma(\"omp Q x\")\n",
        true,
    },
    {
        "#define P(x) _Pragma(#x)\nP(## '' 0X2A)\n",
        true,
    },
    {
        "#define P(x) _Pragma(#x)\nP(0x1p3(a, '' b)\"\\n\")\n",
        true,
    },
    {
        "#define P(x) _Pragma(#x)\nP(['']9223372036854775807 ())\n",
        true,
    },
    {
        "#define Q 'q\n_Pragma(\"ppf_unknown '' Q\")\n",
        true,
    },
    {
        "#define Q 'q\n_Pragma(\"GCC ppf_unknown '' Q\")\n",
        true,
    },
    {
        "#define Q 'q\n_Pragma(\"clang ppf_unknown '' Q\")\n",
        true,
    },
};

// Each source above in each mode as it is, which must agree with clang, its ends confirmed by clang's tokens
// (pp_fuzz_check_ends), or unverified when the optional pragma oracle is unavailable; then with the first token one
// byte shorter, as #263's second review shortened it. It must differ or remain unverified. The first source puts the
// byte it lost among the printer's spaces: only clang's tokens show it. Returns whether every source gives its results.
static bool pp_fuzz_check_ends_cases(const PPFuzzOptions* options)
{
    Arena scratch = arena_reserve(BUSTER_PP_FUZZ_ARENA_BYTES);
    uint32_t failures = scratch.base == 0;
    for (uint32_t mode = 0; scratch.base != 0 && mode < 2; mode += 1)
    {
        const bool c23 = mode == 1;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp_fuzz_ends_cases); i += 1)
        {
            const PPFuzzEndsCase* ends_case = &pp_fuzz_ends_cases[i];
            const PreprocessorFuzzCase test_case = {
                .source = {
                    .pointer = (char*)ends_case->source,
                    .length = strlen(ends_case->source),
                },
                .id = i,
                .isolated = true,
            };
            String8 text = { 0 };
            PreprocessorFuzzOutput theirs = { 0 };
            const bool ran = pp_fuzz_text(&scratch, &test_case, &text) && pp_fuzz_run(&pp_fuzz_state, c23, true, text, 1) &&
                             preprocessor_fuzz_clang(&scratch, options->clang_path, c23, &test_case, 1, true, &theirs);
            PreprocessorFuzzDifference results[2] = {
                BUSTER_PPF_INVALID,
                BUSTER_PPF_INVALID,
            };
            PreprocessorFuzzDifference texts[2] = {
                BUSTER_PPF_INVALID,
                BUSTER_PPF_INVALID,
            };
            bool checked = false;
            bool unverified = false;
            bool shortened = false;
            bool shortened_unverified = false;
            bool counted = true;
            const char* oracle = getenv("BUSTER_PPF_PRAGMA_ORACLE");
            const bool expected_unverified = ends_case->unverified && (oracle == 0 || oracle[0] == 0);
            for (uint32_t pass = 0; ran && pass < 2; pass += 1)
            {
                Preprocessor* pp = &pp_fuzz_state.runs[0];
                for (uint32_t t = 0; pass == 1 && !shortened && t < pp->output.count; t += 1)
                {
                    const uint8_t kind = pp->output.kinds[t];
                    const uint32_t length = pp->output.values[t] - pp->output.locations[t];
                    const bool whole = pp_fuzz_breaks(kind, pp_location_bytes(pp, pp->output.locations[t]), length);
                    shortened = whole && length >= 2;
                    pp->output.values[t] -= shortened;
                }
                bool lost = false;
                const PreprocessorFuzzOutput ours = pp_fuzz_output(pp, pp_fuzz_state.ran[0], &scratch, &lost);
                texts[pass] = pp_fuzz_compare(&scratch, ours, lost, theirs, c23);
                bool pass_checked = false;
                bool pass_unverified = false;
                results[pass] = texts[pass] == BUSTER_PPF_EQUAL ? pp_fuzz_check_ends(&scratch, options->clang_path, c23, &test_case, ours, &pass_checked, &pass_unverified)
                                                                : texts[pass];
                checked |= pass == 0 && pass_checked;
                unverified |= pass == 0 && pass_unverified;
                shortened_unverified |= pass == 1 && pass_unverified;
                if (pass == 0 && ends_case->unverified)
                {
                    // The real comparison path must report this case separately, without counting its unchecked end
                    // as a verified comparison. The review's pragma source is accepted by both preprocessors.
                    const bool usable = true;
                    PPFuzzTally tally = { 0 };
                    const bool saved_quiet = pp_test_quiet;
                    pp_test_quiet = true;
                    pp_fuzz_against_clang(&scratch, options, c23, false, &test_case, &ours, &lost, &usable, 1, &tally);
                    pp_test_quiet = saved_quiet;
                    counted = tally.unverified == expected_unverified && tally.compared == !expected_unverified && tally.invalid == 0 && tally.differences == 0;
                }
            }
            pp_fuzz_release(&pp_fuzz_state);
            arena_pop_to(&scratch, 0);
            const bool agrees = results[0] == BUSTER_PPF_EQUAL && checked == !expected_unverified && unverified == expected_unverified;
            const bool differs = shortened && ((results[1] != BUSTER_PPF_EQUAL && results[1] != BUSTER_PPF_INVALID) || (expected_unverified && shortened_unverified));
            const bool only_tokens = i != 0 || (texts[1] == BUSTER_PPF_EQUAL && results[1] == BUSTER_PPF_TOKEN_SPELLING);
            if (!agrees || !differs || !only_tokens || !counted)
            {
                printf("fuzz ends: %s, case %u gives %s as it is and %s one byte shorter (by its text, %s)\n", c23 ? "c23" : "c17", i, pp_fuzz_difference_name(results[0]),
                       pp_fuzz_difference_name(results[1]), pp_fuzz_difference_name(texts[1]));
                failures += 1;
            }
        }
    }
    arena_release(&scratch);
    printf("fuzz ends: %u of %lu cases fail\n", failures, 2 * (unsigned long)BUSTER_ARRAY_LENGTH(pp_fuzz_ends_cases));
    return failures == 0;
}

// A comparison that can't be made, #263's second review's probe: our accepted output of 1,024 bytes, in an arena of
// 8 KB that the comparison's tokens don't fit, against clang's for `x`. pp_fuzz_against_clang must count the case as
// one without a result, not as compared or differing. Returns whether it does.
static bool pp_fuzz_check_failed_comparison(const PPFuzzOptions* options)
{
    Arena arena = arena_reserve(8 << 10);
    char* text = arena.base != 0 ? arena_push(&arena, 1024, 1) : 0;
    PPFuzzTally tally = { 0 };
    if (text != 0)
    {
        memset(text, 'a', 1024);
        const PreprocessorFuzzCase test_case = {
            .source = BUSTER_S8("x\n"),
        };
        const PreprocessorFuzzOutput ours = {
            .text = {
                .pointer = text,
                .length = 1024,
            },
            .accepted = true,
            .valid = true,
        };
        const bool lost = false;
        const bool usable = true;
        const bool quiet = pp_test_quiet;
        pp_test_quiet = true;
        pp_fuzz_against_clang(&arena, options, false, false, &test_case, &ours, &lost, &usable, 1, &tally);
        pp_test_quiet = quiet;
    }
    arena_release(&arena);
    const bool result = text != 0 && tally.invalid == 1 && tally.compared == 0 && tally.differences == 0;
    printf("fuzz failed comparison: %s\n", result ? "a case without a result" : "counted as a result");
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The run

// Prints case `id` of the options' seed and first mode as clang reads it alone, to reproduce it (--print-case).
static bool pp_fuzz_print_case(const PPFuzzOptions* options, uint64_t id)
{
    Arena arena = arena_reserve(1ull << 24);
    PreprocessorFuzzCase test_case = { 0 };
    String8 text = { 0 };
    const bool result = arena.base != 0 && preprocessor_fuzz_generate(&arena, options->seed, id, !options->modes[0], &test_case) && pp_fuzz_text(&arena, &test_case, &text);
    if (result)
    {
        fwrite(text.pointer, 1, text.length, stdout);
        fprintf(stderr, "case %" PRIu64 ": family %u (%s)%s\n", id, preprocessor_fuzz_family(id), preprocessor_fuzz_construct_names[preprocessor_fuzz_family(id)],
                test_case.isolated ? ", read by clang alone" : "");
    }
    arena_release(&arena);
    return result;
}

static void pp_fuzz_add(PPFuzzTally* total, const PPFuzzTally* part)
{
    total->cases += part->cases;
    total->accepted += part->accepted;
    total->kernels += part->kernels;
    total->compared += part->compared;
    total->differences += part->differences;
    total->alone += part->alone;
    total->oracle += part->oracle;
    total->unbatched += part->unbatched;
    total->invalid += part->invalid;
    total->ends += part->ends;
    total->unverified += part->unverified;
    total->unprintable += part->unprintable;
    total->saved += part->saved;
    total->reported += part->reported;
}

// Reads a worker's counts from its pipe. Returns false if it can't read them all.
static bool pp_fuzz_read_tallies(int descriptor, PPFuzzTally tallies[2])
{
    uint8_t* bytes = (uint8_t*)tallies;
    const uint64_t length = 2 * sizeof(PPFuzzTally);
    uint64_t done = 0;
    bool result = true;
    while (result && done < length)
    {
        const ssize_t count = read(descriptor, bytes + done, length - done);
        if (count > 0)
        {
            done += (uint64_t)count;
        }
        else if (count == 0 || errno != EINTR)
        {
            result = false;
        }
    }
    return result;
}

// Runs each mode the options ask for, with options->jobs workers, and prints each mode's counts. Returns whether every
// case agreed and gave results.
static bool pp_fuzz(const PPFuzzOptions* options)
{
    PPFuzzTally totals[2] = { 0 };
    bool result = options->jobs >= 1 && options->jobs <= BUSTER_PP_FUZZ_MAX_JOBS;
    // __DATE__, __TIME__ and __TIMESTAMP__ from one instant on both sides (clang reads the environment too).
    const char* epoch = getenv("SOURCE_DATE_EPOCH");
    char* saved_epoch = epoch != 0 ? strdup(epoch) : 0;
    setenv("SOURCE_DATE_EPOCH", BUSTER_PPF_EPOCH, 1);
    if (result && options->with_clang && !preprocessor_fuzz_clang_version(options->clang_path))
    {
        printf("fuzz: %s isn't clang 23\n", options->clang_path);
        result = false;
    }
    result = result && pp_fuzz_check_isolation();
    result = result && pp_fuzz_check_comparison();
    result = result && (!options->with_clang || pp_fuzz_check_emulation(options));
    result = result && (!options->with_clang || pp_fuzz_check_ends_cases(options));
    result = result && (!options->with_clang || pp_fuzz_check_failed_comparison(options));
    if (result && options->jobs == 1)
    {
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            if (options->modes[mode])
            {
                pp_fuzz_worker(options, mode == 1, options->first, options->first + options->count, &totals[mode]);
            }
        }
    }
    else if (result)
    {
        pid_t children[BUSTER_PP_FUZZ_MAX_JOBS];
        int pipes[BUSTER_PP_FUZZ_MAX_JOBS];
        uint32_t started = 0;
        fflush(stdout);
        fflush(stderr);
        for (uint32_t job = 0; result && job < options->jobs; job += 1)
        {
            int descriptors[2];
            result = pipe(descriptors) == 0;
            const pid_t child = result ? fork() : -1;
            result = result && child >= 0;
            if (result && child == 0)
            {
                close(descriptors[0]);
                PPFuzzTally tallies[2] = { 0 };
                for (uint32_t mode = 0; mode < 2; mode += 1)
                {
                    if (options->modes[mode])
                    {
                        const uint64_t begin = options->first + options->count * job / options->jobs;
                        const uint64_t end = options->first + options->count * (job + 1) / options->jobs;
                        pp_fuzz_worker(options, mode == 1, begin, end, &tallies[mode]);
                    }
                }
                fflush(stdout);
                fflush(stderr);
                const ssize_t written = write(descriptors[1], tallies, sizeof(tallies));
                _exit(written == (ssize_t)sizeof(tallies) ? 0 : 1);
            }
            if (result)
            {
                close(descriptors[1]);
                children[started] = child;
                pipes[started] = descriptors[0];
                started += 1;
            }
        }
        for (uint32_t job = 0; job < started; job += 1)
        {
            PPFuzzTally tallies[2] = { 0 };
            const bool read_all = pp_fuzz_read_tallies(pipes[job], tallies);
            close(pipes[job]);
            int status = 0;
            const bool exited = waitpid(children[job], &status, 0) == children[job] && WIFEXITED(status) && WEXITSTATUS(status) == 0;
            if (!read_all || !exited)
            {
                printf("fuzz: worker %u of %u failed\n", job, options->jobs);
                result = false;
            }
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                pp_fuzz_add(&totals[mode], &tallies[mode]);
            }
        }
    }
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        const PPFuzzTally* total = &totals[mode];
        if (options->modes[mode])
        {
            printf("fuzz: %s, seed %" PRIu64 ": %" PRIu64 " cases, %" PRIu64 " accepted; the kernels differ on %" PRIu64, mode == 1 ? "c23" : "c17", options->seed,
                   total->cases, total->accepted, total->kernels);
            if (options->with_clang)
            {
                printf("; %" PRIu64 " compared with clang (%" PRIu64 " with clang's tokens, %" PRIu64 " unverified), %" PRIu64 " differ (%" PRIu64
                       " read alone), %" PRIu64 " batches differ from the case alone, %" PRIu64 " batches read case by case",
                       total->compared, total->ends, total->unverified, total->differences, total->alone, total->oracle, total->unbatched);
            }
            printf("; %" PRIu64 " without a result; %" PRIu64 " whose text can't lex back, as clang's can't", total->invalid, total->unprintable);
            if (total->saved != 0)
            {
                printf("; %" PRIu64 " saved", total->saved);
            }
            printf("\n");
            const bool compared = !options->with_clang || (total->compared == total->cases && total->unverified == 0);
            result = result && total->cases == options->count && compared && total->kernels == 0 && total->differences == 0 && total->oracle == 0 && total->invalid == 0;
        }
    }
    if (saved_epoch != 0)
    {
        setenv("SOURCE_DATE_EPOCH", saved_epoch, 1);
        free(saved_epoch);
    }
    else
    {
        unsetenv("SOURCE_DATE_EPOCH");
    }
    return result;
}
