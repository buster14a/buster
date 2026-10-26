// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The preprocessor tests (c_preprocessor.c), for the test-preprocessor command:
//
//   build/compiler test-preprocessor [--clang] [--keep] [--fuzz N] [--seed N] [--std c17|c23] [--jobs N] [clang-style options] [file...]
//
// Without files, it runs the built-in cases (pp_test_cases): small sources on the topics of the C standard's examples
// (6.10.4.5), the cases where GCC's and clang's behavior was probed (docs/c-preprocessor.md), and inputs past the
// vectorized kernels' limits, each written to a new directory under $TMPDIR (or /tmp) and preprocessed by the vectorized and the
// scalar kernels, whose output streams must be the same token for token (kind, flags, value and location), whose
// directive tables must be the same (pp_same_directive_tables), whose tokens' spellings must be the expected ones, and
// which must give the expected number of warnings. A case can name more files and options for both sides (%d stands
// for its directory), and the text -E prints with line markers. Then __TIMESTAMP__ must be each file's modification
// time (pp_test_timestamps), and the -f options of #281's families must be refused or taken as clang's -E needs
// (pp_test_option_refusals). With --clang, each case is also preprocessed by `clang -E`: it must give the same
// spellings, and its line markers must put every token where ours do (pp_compare_placed). More checks compare what the
// cases can't, since the files' runs read clang's predefines: our built-in predefines with `clang -dM -E` in 22 option
// sets (pp_test_predefines), and in each language mode and -f option set of the keywords (pp_test_keyword_sets)
// __is_identifier of every predefined name (pp_test_keywords), and in each language mode the feature queries of every
// feature name (pp_test_features), with clang's; and each refused option set must change clang's -E, and each taken one
// give clang's tokens (pp_test_option_refusals).
//
// After the cases comes the fuzz batch (c_preprocessor_fuzz.c, #109): --fuzz generated cases in each mode (1,500 by
// default, 0 for none), from --seed, by both kernels, which must agree, and with --clang against clang too.
//
// With files, each is preprocessed with the options (-I, -isystem, -D, -std=, -ffreestanding, ... as for -E) by both
// kernels, which must agree, and with --clang compared with `clang -E` and the same options, spellings and line
// markers: our side then reads clang's own predefines (`clang -dM -E` with the options, as --predefines), so that both
// start from the same macros, and clang gets ours' search for --buster-headers (pp_clang_arguments, whose links' names
// in its text, as clang escapes them, become those of the files they lead to). The first difference is printed with
// the tokens around it (pp_test_files). In both, the vectorized printer's text must be the scalar printer's byte for
// byte, with line markers and without, and must lex back into the stream's tokens. The macros at the file's end
// also compare against clang -dM with our own predefines (pp_test_macros).
//
// main.c includes this file after c_preprocessor.c. Layout: the token spellings of a stream and of a text, the
// comparison, clang's side (pp_test_clang), the line markers' places (PPPlaced), the cases, the files, the predefines,
// keywords, features, -f option sets and timestamps checks, and run_preprocessor_tests.

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#define BUSTER_PP_TEST_CONTEXT 6
#define BUSTER_PP_TEST_MAX_OUTPUT (1ull << 32)

// Set while the fuzz runs minimize a difference (c_preprocessor_fuzz.c): the comparisons below then print nothing.
static bool pp_test_quiet;

// A sequence of token spellings: the bytes, and each token's offset and length.
typedef struct PPSpellings PPSpellings;
struct PPSpellings
{
    Arena arena;
    uint8_t* bytes;
    uint64_t length;
    uint64_t* offsets;
    uint32_t* lengths;
    uint32_t* locations; // The stream's locations (0 for a text's tokens).
    uint8_t* kinds; // Lexical kinds: keywords become identifiers, built-in integers become pp-numbers.
    uint64_t count;
    uint64_t capacity;
};

static bool pp_spellings_initialize(PPSpellings* spellings, uint64_t capacity, uint64_t bytes)
{
    spellings->arena = arena_reserve(capacity * 17 + bytes + 1024);
    spellings->offsets = arena_push_array(&spellings->arena, capacity, sizeof(uint64_t), 64);
    spellings->lengths = arena_push_array(&spellings->arena, capacity, sizeof(uint32_t), 64);
    spellings->locations = arena_push_array(&spellings->arena, capacity, sizeof(uint32_t), 64);
    spellings->kinds = arena_push(&spellings->arena, capacity, 64);
    spellings->bytes = arena_push(&spellings->arena, bytes, 64);
    spellings->length = 0;
    spellings->count = 0;
    spellings->capacity = capacity;
    return spellings->offsets != 0 && spellings->lengths != 0 && spellings->locations != 0 && spellings->kinds != 0 && spellings->bytes != 0;
}

static void pp_spellings_add(PPSpellings* spellings, const uint8_t* bytes, uint32_t length, uint32_t location, uint8_t kind)
{
    if (spellings->count < spellings->capacity)
    {
        memcpy(spellings->bytes + spellings->length, bytes, length);
        spellings->offsets[spellings->count] = spellings->length;
        spellings->lengths[spellings->count] = length;
        spellings->locations[spellings->count] = location;
        spellings->kinds[spellings->count] = kind & (uint8_t)~BUSTER_C_TOKEN_START_OF_LINE;
        spellings->length += length;
        spellings->count += 1;
    }
}

// The spellings of a preprocessor's output stream (without the pragmas' end markers), as -E spells them: a pragma
// line's identifiers as written (pp_spelled_as_written), and an #embed's data as its values and the commas between them.
static bool pp_stream_spellings(Preprocessor* pp, PPSpellings* spellings)
{
    const PPTokens* out = &pp->output;
    Scratch scratch = scratch_begin(0);
    uint64_t bytes = 0;
    uint64_t embedded = 0;
    uint32_t pragma = BUSTER_PP_PRAGMA_OUTSIDE;
    bool result = true;
    for (uint32_t i = 0; result && i < out->count; i += 1)
    {
        const uint8_t kind = out->kinds[i];
        const uint64_t mark = scratch.arena->length;
        const uint8_t* written = 0;
        uint32_t data = 0;
        if (kind == BUSTER_PP_KIND_EMBED)
        {
            pp_embed_bytes(pp, out->values[i], &data);
            embedded += 2 * (uint64_t)data;
        }
        bytes += pp_spelled_as_written(pragma, kind) ? pp_written_spelling(pp, out->values[i], out->locations[i], scratch.arena, &written)
                 : kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END) ? intern_length(pp->table, out->values[i])
                 : kind == BUSTER_PP_KIND_PRAGMA_END ? 0 : kind == BUSTER_PP_KIND_INTEGER ? 24 : kind == BUSTER_PP_KIND_EMBED ? 4 * (uint64_t)data : out->values[i] - out->locations[i];
        result = !pp_spelled_as_written(pragma, kind) || written != 0;
        arena_pop_to(scratch.arena, mark);
        pragma = pp_pragma_state(pp, pragma, kind, out->locations[i]);
    }
    result = result && pp_spellings_initialize(spellings, (uint64_t)out->count + embedded + 1, bytes + 64);
    uint8_t* buffer = arena_push(scratch.arena, bytes + 64, 1);
    result = result && buffer != 0;
    pragma = BUSTER_PP_PRAGMA_OUTSIDE;
    for (uint32_t i = 0; result && i < out->count; i += 1)
    {
        const uint8_t kind = out->kinds[i];
        if (kind == BUSTER_PP_KIND_EMBED)
        {
            uint32_t length;
            const uint8_t* data = pp_embed_bytes(pp, out->values[i], &length);
            for (uint32_t b = 0; b < length; b += 1)
            {
                char digits[4];
                const int written = snprintf(digits, sizeof(digits), "%u", data[b]);
                if (b != 0)
                {
                    pp_spellings_add(spellings, (const uint8_t*)",", 1, out->locations[i], C_TOKEN_COMMA);
                }
                pp_spellings_add(spellings, (const uint8_t*)digits, (uint32_t)written, out->locations[i], C_TOKEN_NUMERIC_CONSTANT);
            }
        }
        else if (kind != BUSTER_PP_KIND_PRAGMA_END)
        {
            const uint64_t mark = scratch.arena->length;
            const uint8_t* spelling = 0;
            const uint32_t length = pp_spelled_as_written(pragma, kind) ? pp_written_spelling(pp, out->values[i], out->locations[i], scratch.arena, &spelling)
                                                                        : pp_spelling(pp, kind, out->values[i], out->locations[i], buffer, &spelling);
            result = spelling != 0;
            const uint8_t lexed = kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END ? C_TOKEN_RAW_IDENTIFIER
                                  : kind == BUSTER_PP_KIND_INTEGER                                  ? C_TOKEN_NUMERIC_CONSTANT
                                                                                                    : kind;
            if (result)
            {
                pp_spellings_add(spellings, spelling, length, out->locations[i], lexed);
            }
            arena_pop_to(scratch.arena, mark);
        }
        pragma = pp_pragma_state(pp, pragma, kind, out->locations[i]);
    }
    scratch_end(scratch);
    return result;
}

// The lexer's flags a run's printed text lexes back with, and clang's text and a case's expected text: the mode's, without
// trigraphs (no printer leaves one) and with C_TOKENIZER_NO_DOLLARS where '$' is punctuation (pp_dollar_punctuation, the
// scalar lexer's, not the stream's pp_split_dollars).
static uint32_t pp_test_lex_flags(const Preprocessor* pp)
{
    return (pp->refine_flags & ~(uint32_t)C_TOKENIZER_TRIGRAPHS) | (pp_dollar_punctuation(&pp->options) ? C_TOKENIZER_NO_DOLLARS : 0);
}

// Lexes a text as the language mode does: c_tokenize with the mode's `flags` (pp_test_lex_flags), then the prefixed
// literals the mode doesn't have split, by `split` (pp_split_kinds). Returns the count, or -1.
static int64_t pp_test_lex(const uint8_t* text, uint64_t length, uint64_t split, uint32_t flags, uint8_t* kinds, uint32_t* starts, uint32_t* ends)
{
    int64_t result = c_tokenize(text, length, kinds, starts, ends, flags);
    const uint32_t splits = result > 0 ? pp_count_splits_scalar(text, kinds, starts, ends, (uint32_t)result, split) : 0;
    result = splits != 0 ? pp_split_prefixes(text, kinds, starts, ends, (uint32_t)result, splits, split) : result;
    return result;
}

// The spellings of a text's tokens, as the language mode lexes them (`split` and `flags`, as pp_test_lex takes them).
static bool pp_text_spellings(const uint8_t* text, uint64_t length, uint64_t split, uint32_t flags, PPSpellings* spellings)
{
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(length);
    Arena arena = arena_reserve(capacity * 9 + 256);
    uint8_t* kinds = arena_push(&arena, capacity, 64);
    uint32_t* starts = arena_push_array(&arena, capacity, sizeof(uint32_t), 64);
    uint32_t* ends = arena_push_array(&arena, capacity, sizeof(uint32_t), 64);
    const int64_t count = kinds != 0 && starts != 0 && ends != 0 ? pp_test_lex(text, length, split, flags, kinds, starts, ends) : -1;
    bool result = count >= 0 && pp_spellings_initialize(spellings, (uint64_t)count + 1, length + 64);
    for (int64_t i = 0; result && i < count; i += 1)
    {
        pp_spellings_add(spellings, text + starts[i], ends[i] - starts[i], 0, kinds[i]);
    }
    arena_release(&arena);
    return result;
}

// Compares two spelling sequences; prints the first difference with the tokens around it (and where ours came from).
static bool pp_compare_spellings(Preprocessor* pp, const PPSpellings* ours, const PPSpellings* theirs, const char* name, const char* their_name)
{
    uint64_t i = 0;
    while (i < ours->count && i < theirs->count && ours->kinds[i] == theirs->kinds[i] && ours->lengths[i] == theirs->lengths[i] &&
           memcmp(ours->bytes + ours->offsets[i], theirs->bytes + theirs->offsets[i], ours->lengths[i]) == 0)
    {
        i += 1;
    }
    const bool result = i == ours->count && i == theirs->count;
    if (!result && !pp_test_quiet)
    {
        printf("%s: token %lu differs (%lu tokens, %s %lu)\n", name, (unsigned long)i, (unsigned long)ours->count, their_name, (unsigned long)theirs->count);
        if (i < ours->count && i < theirs->count && ours->kinds[i] != theirs->kinds[i])
        {
            printf("  lexical kinds: ours %u, %s %u\n", ours->kinds[i], their_name, theirs->kinds[i]);
        }
        const uint64_t first = i > BUSTER_PP_TEST_CONTEXT ? i - BUSTER_PP_TEST_CONTEXT : 0;
        for (int side = 0; side < 2; side += 1)
        {
            const PPSpellings* spellings = side == 0 ? ours : theirs;
            printf("  %-6s", side == 0 ? "ours" : their_name);
            for (uint64_t k = first; k < spellings->count && k < i + BUSTER_PP_TEST_CONTEXT; k += 1)
            {
                printf("%s%.*s", k == i ? " >>" : " ", (int)(spellings->lengths[k] < 60 ? spellings->lengths[k] : 60), spellings->bytes + spellings->offsets[k]);
            }
            printf("\n");
        }
        if (pp != 0 && i < ours->count && ours->locations[i] != 0)
        {
            const uint32_t range = pp_find_range(pp, ours->locations[i]);
            if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
            {
                uint32_t column;
                const uint32_t line = pp_line_of(pp, range, ours->locations[i], &column);
                printf("  ours is spelled at %s:%u:%u\n", pp->files[pp->ranges[range].file].path, line, column);
            }
        }
    }
    return result;
}

// Are two runs' output streams the same, token for token?
static bool pp_same_streams(const Preprocessor* a, const Preprocessor* b, const char* name)
{
    const PPTokens* x = &a->output;
    const PPTokens* y = &b->output;
    uint32_t i = 0;
    while (i < x->count && i < y->count && x->kinds[i] == y->kinds[i] && x->flags[i] == y->flags[i] && x->values[i] == y->values[i] &&
           x->locations[i] == y->locations[i])
    {
        i += 1;
    }
    const bool history = a->history_count == b->history_count && a->substitution_count == b->substitution_count && a->generated_count == b->generated_count &&
                         a->history_source_count == b->history_source_count && a->history_run_count == b->history_run_count &&
                         memcmp(a->history, b->history, (uint64_t)a->history_count * sizeof(PPExpansion)) == 0 &&
                         memcmp(a->substitutions, b->substitutions, (uint64_t)a->substitution_count * sizeof(PPSubstitution)) == 0 &&
                         memcmp(a->generated, b->generated, (uint64_t)a->generated_count * sizeof(PPGenerated)) == 0 &&
                         memcmp(a->history_sources, b->history_sources, (uint64_t)a->history_source_count * sizeof(PPSource)) == 0 &&
                         memcmp(a->history_runs, b->history_runs, (uint64_t)a->history_run_count * sizeof(PPHistoryRun)) == 0;
    const bool result = i == x->count && i == y->count && history;
    if (!result && !pp_test_quiet)
    {
        if (!history)
        {
            printf("%s: scalar/vector expansion history differs (expansions %u/%u, substitutions %u/%u, generated %u/%u, runs %u/%u)\n", name,
                   a->history_count, b->history_count, a->substitution_count, b->substitution_count, a->generated_count, b->generated_count,
                   a->history_run_count, b->history_run_count);
        }
        printf("%s: the vectorized and scalar kernels differ at token %u of %u/%u", name, i, x->count, y->count);
        if (i < x->count && i < y->count)
        {
            printf(" (kind %u/%u, flags %u/%u, value %u/%u, location %u/%u)", x->kinds[i], y->kinds[i], x->flags[i], y->flags[i], x->values[i], y->values[i],
                   x->locations[i], y->locations[i]);
        }
        printf("\n");
    }
    return result;
}

// Do two runs' files have the same directive tables (kinds, line ends, conditional pairing and guards)? The runs read
// the same files in the same order, so their file data line up.
static bool pp_same_directive_tables(const Preprocessor* a, const Preprocessor* b, const char* name)
{
    bool result = a->data_count == b->data_count;
    for (uint32_t f = 0; result && f < a->data_count; f += 1)
    {
        const PPFileData* x = &a->datas[f];
        const PPFileData* y = &b->datas[f];
        result = x->prepared == y->prepared && x->directive_count == y->directive_count && x->guard == y->guard && x->raw_error_count == y->raw_error_count;
        for (uint32_t e = 0; result && e < x->raw_error_count; e += 1)
        {
            result = x->raw_errors[e].token == y->raw_errors[e].token && x->raw_errors[e].offset == y->raw_errors[e].offset && x->raw_errors[e].kind == y->raw_errors[e].kind;
        }
        for (uint32_t d = 0; result && x->prepared && d < x->directive_count; d += 1)
        {
            result = x->directive_hash[d] == y->directive_hash[d] && x->directive_end[d] == y->directive_end[d] &&
                     x->directive_kind[d] == y->directive_kind[d] && x->directive_next[d] == y->directive_next[d];
            if (!result && !pp_test_quiet)
            {
                printf("%s: the kernels' directive tables differ in file %u at directive %u (kind %u/%u, next %u/%u)\n", name, f, d,
                       x->directive_kind[d], y->directive_kind[d], x->directive_next[d], y->directive_next[d]);
            }
        }
    }
    if (!result && a->data_count != b->data_count && !pp_test_quiet)
    {
        printf("%s: the kernels read %u and %u files\n", name, a->data_count, b->data_count);
    }
    return result;
}

// Preprocesses `path` with `options`, in a fresh table: `pp` must be released by the caller. Returns whether it ran
// without errors. The run keeps its expansion starts, since its text is printed with line markers too.
static bool pp_test_run(Preprocessor* pp, InternTable* table, const PPOptions* options, const char* path)
{
    PPOptions with_starts = *options;
    with_starts.expansion_starts = true;
    // The statistics too: the files' checks compare the inclusions skipped by guards with clang's.
    with_starts.timing = true;
    bool result = intern_initialize(table) && pp_initialize(pp, &with_starts, table);
    if (result)
    {
        result = pp_run(pp, path);
    }
    return result;
}

// A run as the drivers make one, with its history deferred (PPOptions.defer_history), against one that records it, both
// by the vectorized kernels without expansion starts: the same stream, errors and warnings, and the same diagnostics
// printed byte for byte, notes and all (the deferred run prints its own by recording the history at its end); then a
// diagnostic at the first output token a macro made (or the last token) through pp_diagnose_output, which records the
// deferred history first, printed alike; and the same history, with nothing more printed. Returns whether they agree.
static bool pp_test_deferred_history(const PPOptions* options, const char* path, const char* label)
{
    static InternTable tables[2];
    static Preprocessor runs[2];
    char* texts[2] = { 0, 0 };
    size_t lengths[2] = { 0, 0 };
    FILE* streams[2];
    bool ready[2];
    for (uint32_t k = 0; k < 2; k += 1)
    {
        PPOptions run = *options;
        run.scalar = false;
        run.expansion_starts = false;
        run.defer_history = k == 1;
        streams[k] = open_memstream(&texts[k], &lengths[k]);
        run.diagnostics = streams[k];
        ready[k] = streams[k] != 0 && intern_initialize(&tables[k]) && pp_initialize(&runs[k], &run, &tables[k]);
        if (ready[k])
        {
            pp_run(&runs[k], path);
        }
        if (streams[k] != 0)
        {
            fflush(streams[k]);
        }
    }
    bool result = ready[0] && ready[1] && pp_tokens_same(&runs[0].output, &runs[1].output) && runs[0].errors == runs[1].errors &&
                  runs[0].warnings == runs[1].warnings && lengths[0] == lengths[1] && (lengths[0] == 0 || memcmp(texts[0], texts[1], lengths[0]) == 0);
    if (!result)
    {
        printf("%s: with its history deferred, a run gives other tokens or diagnostics (%u and %u errors, %u and %u warnings, %zu and %zu bytes printed)\n",
               label, runs[0].errors, runs[1].errors, runs[0].warnings, runs[1].warnings, lengths[0], lengths[1]);
    }
    const uint32_t count = runs[0].output.count;
    uint32_t probe = count;
    for (uint32_t i = 0; result && probe == count && i < count; i += 1)
    {
        probe = (runs[0].output.flags[i] & BUSTER_PP_FROM_MACRO) ? i : probe;
    }
    probe = probe == count && count != 0 ? count - 1 : probe;
    for (uint32_t k = 0; result && probe < count && k < 2; k += 1)
    {
        pp_diagnose_output(&runs[k], PP_SEVERITY_WARNING, probe, "deferred history probe");
        fflush(streams[k]);
    }
    const bool probed = result && lengths[0] == lengths[1] && (lengths[0] == 0 || memcmp(texts[0], texts[1], lengths[0]) == 0);
    const size_t printed = lengths[1];
    const bool recorded = probed && pp_history_ensure(&runs[1]);
    if (streams[1] != 0)
    {
        fflush(streams[1]);
    }
    const bool same = recorded && lengths[1] == printed && pp_same_streams(&runs[0], &runs[1], label);
    if (result && !same)
    {
        printf("%s: the deferred history, recorded, %s\n", label,
               !probed ? "prints a diagnostic at an output token otherwise" : recorded ? "differs from the history the run recorded" : "is missing");
    }
    result = same;
    for (uint32_t k = 0; k < 2; k += 1)
    {
        if (ready[k])
        {
            pp_release(&runs[k]);
        }
        intern_release(&tables[k]);
        if (streams[k] != 0)
        {
            fclose(streams[k]);
        }
        free(texts[k]);
    }
    return result;
}

// What pp_test_system_levels compared: the units, those whose runs couldn't say (inexact), and their output tokens at a
// system level, all and those a macro made.
static uint64_t pp_test_system_units;
static uint64_t pp_test_system_inexact;
static uint64_t pp_test_system_tokens;
static uint64_t pp_test_system_made;
static char pp_test_system_inexact_first[256];

// The system levels a compile's checker judges by, from the runs the main loop kept (pp_system_levels), against their
// twin by the expansion history (pp_system_levels_history), on run `a`, which has its history; and the runs, which both
// kernels' runs `a` and `b` must keep alike. A unit whose runs can't say is counted, not compared. Returns whether they
// agree.
static bool pp_test_system_levels(const Preprocessor* a, const Preprocessor* b, const char* label)
{
    bool result = a->system_run_count == b->system_run_count && a->system_runs_inexact == b->system_runs_inexact;
    for (uint32_t r = 0; result && r < a->system_run_count; r += 1)
    {
        const PPSystemRun* x = &a->system_runs[r];
        const PPSystemRun* y = &b->system_runs[r];
        result = x->token == y->token && x->range == y->range && x->from == y->from && x->level == y->level;
    }
    if (!result)
    {
        printf("%s: the kernels keep other system runs (%u and %u)\n", label, a->system_run_count, b->system_run_count);
    }
    const uint32_t count = a->output.count;
    const bool compared = result && !a->system_runs_inexact;
    uint8_t* levels = compared ? malloc((uint64_t)count * 2 + 2) : 0;
    if (compared && levels == 0)
    {
        printf("%s: no memory to compare the system levels\n", label);
        result = false;
    }
    if (levels != 0)
    {
        uint8_t* twin = levels + count + 1;
        pp_system_levels(a, levels);
        pp_system_levels_history(a, twin);
        uint32_t i = 0;
        while (i < count && levels[i] == twin[i])
        {
            i += 1;
        }
        if (i < count)
        {
            printf("%s: output token %u (kind %u, %s, location %u) is at system level %u by the runs and %u by the history\n", label, i,
                   a->output.kinds[i], (a->output.flags[i] & BUSTER_PP_FROM_MACRO) ? "a macro made it" : "no macro made it", a->output.locations[i], levels[i],
                   twin[i]);
            result = false;
        }
        for (uint32_t t = 0; t < count; t += 1)
        {
            const bool system = levels[t] != BUSTER_PP_USER_FILE;
            pp_test_system_tokens += system;
            pp_test_system_made += system && (a->output.flags[t] & BUSTER_PP_FROM_MACRO);
        }
        free(levels);
    }
    pp_test_system_units += levels != 0;
    if (a->system_runs_inexact && pp_test_system_inexact == 0)
    {
        snprintf(pp_test_system_inexact_first, sizeof(pp_test_system_inexact_first), "%s", label);
    }
    pp_test_system_inexact += a->system_runs_inexact;
    return result;
}

// Both kernels again as a compile runs them, without the expansion starts that -E's line markers need: the main loop
// takes some direct paths only then (pp_expand_file_direct, pp_emit_simple), so the streams, the expansion history and
// the statistics (but the timing ones) must agree in this configuration too, and the vectorized run's with its history
// deferred, as the drivers run it (pp_test_deferred_history). Returns whether they do.
static bool pp_test_compile_kernels(const PPOptions* options, const char* path, const char* name)
{
    static InternTable tables[2];
    static Preprocessor runs[2];
    bool ready[2];
    bool ran[2];
    for (uint32_t k = 0; k < 2; k += 1)
    {
        PPOptions run = *options;
        run.scalar = k == 1;
        run.expansion_starts = false;
        run.timing = true;
        ready[k] = intern_initialize(&tables[k]) && pp_initialize(&runs[k], &run, &tables[k]);
        ran[k] = ready[k] && pp_run(&runs[k], path);
    }
    char label[4200];
    snprintf(label, sizeof(label), "%s (as a compile runs it)", name);
    bool result = ready[0] && ready[1] && pp_same_streams(&runs[0], &runs[1], label);
    if (result && (ran[0] != ran[1] || runs[0].errors != runs[1].errors || runs[0].warnings != runs[1].warnings))
    {
        printf("%s: the vectorized and scalar kernels give %u and %u errors and %u and %u warnings\n", label, runs[0].errors, runs[1].errors, runs[0].warnings,
               runs[1].warnings);
        result = false;
    }
    PPStatistics statistics[2] = { runs[0].statistics, runs[1].statistics };
    for (uint32_t k = 0; k < 2; k += 1)
    {
        statistics[k].include_ticks = 0;
        statistics[k].embed_ticks = 0;
    }
    if (result && memcmp(&statistics[0], &statistics[1], sizeof(PPStatistics)) != 0)
    {
        printf("%s: the vectorized and scalar kernels count different statistics\n", label);
        result = false;
    }
    result = result && pp_test_system_levels(&runs[0], &runs[1], label);
    for (uint32_t k = 0; k < 2; k += 1)
    {
        if (ready[k])
        {
            pp_release(&runs[k]);
        }
        intern_release(&tables[k]);
    }
    return result && pp_test_deferred_history(options, path, label);
}

// The text one of the printers gives for a run, in memory (null if it fails); `free` it.
static char* pp_test_print(Preprocessor* pp, bool vector, bool markers, size_t* length)
{
    char* result = 0;
    *length = 0;
    FILE* stream = open_memstream(&result, length);
    const bool printed = stream != 0 && (vector ? pp_print_vector(pp, stream, markers) : pp_print_scalar(pp, stream, markers));
    if (stream != 0)
    {
        fclose(stream);
    }
    if (!printed)
    {
        free(result);
        result = 0;
    }
    return result;
}

// An unterminated raw token runs to EOF when printed text is lexed again, taking the printer's final newline into
// its spelling. Use the stream's known last-token boundary only when every original byte matches and EXACTLY one
// final newline follows. Internal whitespace, missing/changed bytes and extra trailing text must still fail. Restore
// the comparison view afterward so line-marker comparisons retain the complete printed token on both sides.
static bool pp_compare_printed_spellings(Preprocessor* pp, const PPSpellings* ours, PPSpellings* theirs, const char* name, const char* their_name)
{
    const uint64_t last = ours->count != 0 ? ours->count - 1 : 0;
    const uint32_t length = ours->count != 0 ? ours->lengths[last] : 0;
    const bool tail = ours->count != 0 && ours->count == theirs->count && ours->kinds[last] == C_TOKEN_UNKNOWN && theirs->kinds[last] == C_TOKEN_UNKNOWN &&
                      pp_raw_string_start(ours->bytes + ours->offsets[last], length) && theirs->lengths[last] == (uint64_t)length + 1 &&
                      memcmp(ours->bytes + ours->offsets[last], theirs->bytes + theirs->offsets[last], length) == 0 &&
                      theirs->bytes[theirs->offsets[last] + length] == '\n';
    if (tail)
    {
        theirs->lengths[last] -= 1;
    }
    const bool result = pp_compare_spellings(pp, ours, theirs, name, their_name);
    if (tail)
    {
        theirs->lengths[last] += 1;
    }
    return result;
}

// The text -E prints for a run, which must lex into the stream's spellings (with the EOF boundary above); the vectorized printer's must be the scalar
// printer's byte for byte, with line markers and without.
static bool pp_test_printed(Preprocessor* pp, const PPSpellings* ours, const char* name)
{
    size_t length = 0;
    char* text = pp_test_print(pp, true, false, &length);
    bool result = text != 0;
    for (uint32_t markers = 0; result && markers < 2; markers += 1)
    {
        size_t vector_length = 0;
        size_t scalar_length = 0;
        char* vector_text = pp_test_print(pp, true, markers != 0, &vector_length);
        char* scalar_text = pp_test_print(pp, false, markers != 0, &scalar_length);
        result = vector_text != 0 && scalar_text != 0 && vector_length == scalar_length && memcmp(vector_text, scalar_text, vector_length) == 0;
        if (!result && !pp_test_quiet)
        {
            size_t at = 0;
            while (vector_text != 0 && scalar_text != 0 && at < vector_length && at < scalar_length && vector_text[at] == scalar_text[at])
            {
                at += 1;
            }
            printf("%s: the printers differ%s at byte %zu of %zu/%zu\n", name, markers ? " with line markers" : "", at, vector_length, scalar_length);
        }
        free(vector_text);
        free(scalar_text);
    }
    PPSpellings printed = { 0 };
    result = result && pp_text_spellings((const uint8_t*)text, length, pp->split_kinds, pp_test_lex_flags(pp), &printed);
    if (result)
    {
        char label[256];
        snprintf(label, sizeof(label), "%s (printed)", name);
        result = pp_compare_printed_spellings(pp, ours, &printed, label, "-E");
    }
    arena_release(&printed.arena);
    free(text);
    return result;
}

typedef enum PPClangStatus
{
    PP_CLANG_ERROR,
    PP_CLANG_ACCEPTED,
    PP_CLANG_REJECTED,
} PPClangStatus;

typedef struct PPClangOutput PPClangOutput;
struct PPClangOutput
{
    PPClangStatus status;
    char* text;
    size_t length;
    char* diagnostics;
};

// A child writes into temporary files, so neither a full stdout nor a full stderr pipe can deadlock the parent.
// Read only bounded output, check every I/O operation, and keep failed/partial output separate from acceptance.
static bool pp_test_read_output(FILE* stream, char** text, size_t* length)
{
    bool result = fflush(stream) == 0 && fseek(stream, 0, SEEK_END) == 0;
    const long size = result ? ftell(stream) : -1;
    result = result && size >= 0 && (uint64_t)size < BUSTER_PP_TEST_MAX_OUTPUT && fseek(stream, 0, SEEK_SET) == 0;
    *text = result ? malloc((size_t)size + 1) : 0;
    result = result && *text != 0;
    if (result)
    {
        *length = (size_t)size;
        result = fread(*text, 1, *length, stream) == *length && ferror(stream) == 0;
        (*text)[*length] = 0;
    }
    return result;
}

static PPClangOutput pp_test_clang_process(const char* executable, char** arguments, int argument_count, const char* path, bool macros)
{
    PPClangOutput result = { 0 };
    FILE* output = tmpfile();
    FILE* diagnostics = tmpfile();
    char** argv = calloc((size_t)argument_count + 9, sizeof(char*));
    bool ready = output != 0 && diagnostics != 0 && argv != 0;
    pid_t child = -1;
    if (ready)
    {
        int n = 0;
        argv[n++] = (char*)executable;
        for (int i = 0; i < argument_count; i += 1)
        {
            argv[n++] = arguments[i];
        }
        argv[n++] = "-E";
        if (macros)
        {
            argv[n++] = "-dM";
            argv[n++] = "-x";
            argv[n++] = "c";
        }
        argv[n++] = (char*)path;
        child = fork();
        if (child == 0)
        {
            unsetenv("CPATH");
            unsetenv("C_INCLUDE_PATH");
            if (dup2(fileno(output), STDOUT_FILENO) < 0 || dup2(fileno(diagnostics), STDERR_FILENO) < 0)
            {
                _exit(126);
            }
            execvp(executable, argv);
            perror(executable);
            _exit(127);
        }
        int status = 0;
        pid_t waited = -1;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        }
        while (waited < 0 && child > 0 && errno == EINTR);
        size_t diagnostic_length = 0;
        ready = waited == child && child > 0 && pp_test_read_output(output, &result.text, &result.length) &&
                pp_test_read_output(diagnostics, &result.diagnostics, &diagnostic_length);
        // Exit 1 alone does not establish an input rejection: a driver can also report a failed frontend or I/O.
        const char* message = result.diagnostics != 0 ? result.diagnostics : "";
        const bool rejected = strstr(message, "error:") != 0 && strstr(message, "clang: error:") == 0 &&
                              strstr(message, "unable to execute command") == 0 && strstr(message, "PLEASE submit a bug report") == 0 &&
                              strstr(message, "LLVM ERROR") == 0 && strstr(message, "No space left on device") == 0 &&
                              strstr(message, "Input/output error") == 0 && strstr(message, "Cannot allocate memory") == 0;
        if (ready && WIFEXITED(status) && (WEXITSTATUS(status) == 0 || (WEXITSTATUS(status) == 1 && rejected)))
        {
            result.status = WEXITSTATUS(status) == 0 ? PP_CLANG_ACCEPTED : PP_CLANG_REJECTED;
        }
        else
        {
            fprintf(stderr, "%s: Clang oracle could not run reliably (wait status %d)\n", path, status);
        }
    }
    if (output != 0)
    {
        fclose(output);
    }
    if (diagnostics != 0)
    {
        fclose(diagnostics);
    }
    free(argv);
    return result;
}

// Legacy fixture callers may compare the partial output of an intentionally rejected case. File-oracle callers
// inspect PPClangOutput.status instead: a rejection is an explicit skip, a process/I/O failure is a failed run.
// clang's predefines for `arguments` (clang -dM -E of an empty input), written to `predefines_path`.
static bool pp_test_clang_predefines(char** arguments, int argument_count, const char* predefines_path)
{
    PPClangOutput macros = pp_test_clang_process("clang", arguments, argument_count, "/dev/null", true);
    bool result = macros.status == PP_CLANG_ACCEPTED;
    FILE* out = result ? fopen(predefines_path, "w") : 0;
    result = out != 0 && fwrite(macros.text, 1, macros.length, out) == macros.length;
    if (out != 0)
    {
        result = fclose(out) == 0 && result;
    }
    free(macros.text);
    free(macros.diagnostics);
    return result;
}

static char* pp_test_clang(char** arguments, int argument_count, const char* path, const char* predefines_path, size_t* length)
{
    char* result = 0;
    *length = 0;
    const bool ready = predefines_path == 0 || pp_test_clang_predefines(arguments, argument_count, predefines_path);
    if (ready)
    {
        PPClangOutput output = pp_test_clang_process("clang", arguments, argument_count, path, false);
        if (output.diagnostics != 0)
        {
            fputs(output.diagnostics, stderr);
        }
        result = output.status != PP_CLANG_ERROR ? output.text : 0;
        *length = output.length;
        free(output.status == PP_CLANG_ERROR ? output.text : 0);
        free(output.diagnostics);
    }
    return result;
}

// The tokens of a text -E printed with line markers, and where the markers put each, as a compiler reading the text
// takes them (GNU line markers, `# line "name" flags`): its presumed line, and its file on the include stack as a
// state, which is the file's name as the marker spells it, its system level (flag 3, or 3 and 4) and its includer's
// state. Flag 1 enters a file, 2 goes back to the includer, and a marker without either renames the current file. A
// marker's line isn't lexed.
typedef struct PPPlacedState PPPlacedState;
struct PPPlacedState
{
    uint64_t name;
    uint32_t name_length;
    uint32_t parent;
    uint8_t system;
};

typedef struct PPPlaced PPPlaced;
struct PPPlaced
{
    Arena arena;
    PPSpellings spellings;
    const uint8_t* text;
    uint32_t* lines;
    uint32_t* states;
    PPPlacedState* records;
    uint32_t record_count;
};

// Is the line at `text[at]` a marker? Its line, name (escaped, as an offset and length) and flags if so.
static bool pp_marker_line(const uint8_t* text, uint64_t at, uint64_t end, uint32_t* line, uint64_t* name, uint32_t* name_length, bool flags[5])
{
    bool result = at + 2 < end && text[at] == '#' && text[at + 1] == ' ' && text[at + 2] >= '0' && text[at + 2] <= '9';
    uint64_t i = at + 2;
    uint64_t number = 0;
    while (result && i < end && text[i] >= '0' && text[i] <= '9')
    {
        number = number * 10 + (uint64_t)(text[i] - '0');
        i += 1;
    }
    result = result && number <= UINT32_MAX && i + 1 < end && text[i] == ' ' && text[i + 1] == '"';
    i += 2;
    *name = i;
    while (result && i < end && text[i] != '"')
    {
        i += text[i] == '\\' ? 2 : 1;
    }
    result = result && i < end;
    *name_length = (uint32_t)(i - *name);
    *line = (uint32_t)number;
    i += 1;
    memset(flags, 0, 5);
    while (result && i + 1 < end && text[i] == ' ' && text[i + 1] >= '1' && text[i + 1] <= '4')
    {
        flags[text[i + 1] - '0'] = true;
        i += 2;
    }
    return result;
}

static bool pp_placed_tokens(const uint8_t* text, uint64_t length, uint64_t split, uint32_t flags, PPPlaced* placed)
{
    uint64_t line_count = 1;
    for (uint64_t i = 0; i < length; i += 1)
    {
        line_count += text[i] == '\n';
    }
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(length);
    placed->arena = arena_reserve(length + 64 + line_count * 24 + capacity * 17 + 4096);
    placed->text = text;
    uint8_t* blank = arena_push(&placed->arena, length + 64, 64);
    uint64_t* line_starts = arena_push_array(&placed->arena, line_count + 1, sizeof(uint64_t), 8);
    uint32_t* line_lines = arena_push_array(&placed->arena, line_count + 1, sizeof(uint32_t), 4);
    uint32_t* line_states = arena_push_array(&placed->arena, line_count + 1, sizeof(uint32_t), 4);
    placed->records = arena_push_array(&placed->arena, line_count + 2, sizeof(PPPlacedState), 8);
    uint8_t* kinds = arena_push(&placed->arena, capacity, 64);
    uint32_t* starts = arena_push_array(&placed->arena, capacity, sizeof(uint32_t), 64);
    uint32_t* ends = arena_push_array(&placed->arena, capacity, sizeof(uint32_t), 64);
    bool result = blank != 0 && line_starts != 0 && line_lines != 0 && line_states != 0 && placed->records != 0 && kinds != 0 && starts != 0 && ends != 0;
    if (result)
    {
        memcpy(blank, text, length);
        memset(blank + length, 0, 64);
        // State 0: before any marker.
        placed->records[0] = (PPPlacedState){ .parent = UINT32_MAX };
        placed->record_count = 1;
        uint32_t state = 0;
        uint32_t line = 1;
        uint64_t at = 0;
        for (uint64_t k = 0; k < line_count; k += 1)
        {
            uint64_t end = at;
            while (end < length && text[end] != '\n')
            {
                end += 1;
            }
            line_starts[k] = at;
            uint32_t number;
            uint64_t name;
            uint32_t name_length;
            bool flags[5];
            if (pp_marker_line(text, at, end, &number, &name, &name_length, flags))
            {
                const uint32_t includer = placed->records[state].parent;
                const uint32_t parent = flags[1] ? state : flags[2] ? (includer != UINT32_MAX ? placed->records[includer].parent : UINT32_MAX) : includer;
                placed->records[placed->record_count] = (PPPlacedState){
                    .name = name,
                    .name_length = name_length,
                    .parent = parent,
                    .system = (uint8_t)(flags[3] ? (flags[4] ? 2 : 1) : 0),
                };
                state = placed->record_count;
                placed->record_count += 1;
                memset(blank + at, ' ', end - at);
                line = number;
                line_lines[k] = 0;
            }
            else
            {
                line_lines[k] = line;
                line += 1;
            }
            line_states[k] = state;
            at = end + 1;
        }
        line_starts[line_count] = UINT64_MAX;
        const int64_t count = pp_test_lex(blank, length, split, flags, kinds, starts, ends);
        result = count >= 0 && pp_spellings_initialize(&placed->spellings, (uint64_t)count + 1, length + 64);
        placed->lines = result ? arena_push_array(&placed->arena, (uint64_t)count + 1, sizeof(uint32_t), 4) : 0;
        placed->states = result ? arena_push_array(&placed->arena, (uint64_t)count + 1, sizeof(uint32_t), 4) : 0;
        result = result && placed->lines != 0 && placed->states != 0;
        uint64_t k = 0;
        for (int64_t i = 0; result && i < count; i += 1)
        {
            while (line_starts[k + 1] <= starts[i])
            {
                k += 1;
            }
            pp_spellings_add(&placed->spellings, blank + starts[i], ends[i] - starts[i], 0, kinds[i]);
            placed->lines[i] = line_lines[k];
            placed->states[i] = line_states[k];
        }
    }
    return result;
}

static void pp_placed_release(PPPlaced* placed)
{
    arena_release(&placed->spellings.arena);
    arena_release(&placed->arena);
}

// Are two states the same stack: the same names and system levels down to the root?
static bool pp_same_placed_states(const PPPlaced* a, uint32_t x, const PPPlaced* b, uint32_t y)
{
    bool result = true;
    while (result && x != UINT32_MAX && y != UINT32_MAX)
    {
        const PPPlacedState* p = &a->records[x];
        const PPPlacedState* q = &b->records[y];
        result = p->name_length == q->name_length && p->system == q->system && memcmp(a->text + p->name, b->text + q->name, p->name_length) == 0;
        x = p->parent;
        y = q->parent;
    }
    return result && x == y;
}

static void pp_print_placed_state(const PPPlaced* placed, uint32_t state)
{
    for (uint32_t at = state; at != UINT32_MAX; at = placed->records[at].parent)
    {
        const PPPlacedState* record = &placed->records[at];
        printf("%s\"%.*s\"%s", at == state ? "" : " < ", (int)record->name_length, (const char*)placed->text + record->name,
               record->system == 2 ? " 3 4" : record->system == 1 ? " 3" : "");
    }
}

// Do two texts with line markers put the same tokens on the same presumed lines, in the same include stacks? Prints the
// first token whose place differs, with its stacks.
static bool pp_compare_placed(const PPPlaced* ours, const PPPlaced* theirs, const char* name, const char* their_name)
{
    const uint64_t count = ours->spellings.count < theirs->spellings.count ? ours->spellings.count : theirs->spellings.count;
    uint64_t i = 0;
    uint32_t last_ours = UINT32_MAX;
    uint32_t last_theirs = UINT32_MAX;
    bool same = true;
    while (same && i < count)
    {
        same = ours->lines[i] == theirs->lines[i];
        if (same && (ours->states[i] != last_ours || theirs->states[i] != last_theirs))
        {
            same = pp_same_placed_states(ours, ours->states[i], theirs, theirs->states[i]);
            last_ours = ours->states[i];
            last_theirs = theirs->states[i];
        }
        i += same;
    }
    const bool result = same && ours->spellings.count == theirs->spellings.count;
    if (!result)
    {
        printf("%s: the line markers place token %lu (of %lu, %s %lu) differently\n", name, (unsigned long)i, (unsigned long)ours->spellings.count, their_name,
               (unsigned long)theirs->spellings.count);
        for (int side = 0; side < 2 && i < count; side += 1)
        {
            const PPPlaced* placed = side == 0 ? ours : theirs;
            const PPSpellings* spellings = &placed->spellings;
            printf("  %-6s line %u of ", side == 0 ? "ours" : their_name, placed->lines[i]);
            pp_print_placed_state(placed, placed->states[i]);
            printf(":");
            const uint64_t first = i > BUSTER_PP_TEST_CONTEXT ? i - BUSTER_PP_TEST_CONTEXT : 0;
            for (uint64_t k = first; k < spellings->count && k < i + BUSTER_PP_TEST_CONTEXT; k += 1)
            {
                printf("%s%.*s", k == i ? " >>" : " ", (int)(spellings->lengths[k] < 60 ? spellings->lengths[k] : 60), spellings->bytes + spellings->offsets[k]);
            }
            printf("\n");
        }
    }
    return result;
}

// Our line markers against clang's (`theirs`, from pp_test_clang): the vectorized printer's text with markers must
// place every token as clang's does.
static bool pp_test_placed(Preprocessor* pp, const PPPlaced* theirs, const char* name)
{
    size_t length = 0;
    char* text = pp_test_print(pp, true, true, &length);
    PPPlaced ours = { 0 };
    const bool result = text != 0 && pp_placed_tokens((const uint8_t*)text, length, pp->split_kinds, pp_test_lex_flags(pp), &ours) && pp_compare_placed(&ours, theirs, name, "clang");
    pp_placed_release(&ours);
    free(text);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The cases

// A file a case writes besides case.c: its path in the case's directory (at most one directory deep), and its text.
typedef struct PPTestFile PPTestFile;
struct PPTestFile
{
    const char* path;
    const char* text;
    // The text's length, for one with zero bytes in it (an #embed's resource); 0 for strlen's.
    uint32_t length;
    // A link instead of a text: a symbolic one to `link` as it is written, or (`hard`) a hard one to the case's file
    // `link`, which comes earlier.
    const char* link;
    bool hard;
};

typedef struct PPTestCase PPTestCase;
struct PPTestCase
{
    const char* name;
    const char* source;
    // Another file the source can include as "other.h", or null.
    const char* other;
    const char* standard;
    // The expected output's text (compared token by token).
    const char* expected;
    // Whether the case is meant to fail (an error). Its expected output is then compared only if it has one, to check
    // how the run went on after the error (clang's goes on the same way).
    bool fails;
    // The warnings each run must give (redefinitions).
    uint32_t warnings;
    // Options for both sides, separated by spaces, where %d stands for the case's directory; or null.
    const char* options;
    // More files, ended by one without a path; or null.
    const PPTestFile* files;
    // The text -E prints with line markers (compared byte for byte, %d for the directory), or null.
    const char* markers;
    // The errors each run must give, for a case that fails; 0 doesn't count them.
    uint32_t errors;
};

static const PPTestFile pp_test_files_include_next[] = {
    { "first/wrapper.h", "LAYER_ONE\n#include_next <wrapper.h>\n" },
    { "second/wrapper.h", "LAYER_TWO\n" },
    { 0, 0 },
};

static const PPTestFile pp_test_files_renamed[] = {
    { "inc.h", "int in_header;\n#line 50 \"renamed.h\"\nint after;\n" },
    { 0, 0 },
};

static const PPTestFile pp_test_files_collecting_exit[] = {
    { "outer.h", "#include \"other.h\"\n" },
    { 0, 0 },
};

static const PPTestFile pp_test_files_collecting_return[] = {
    { "outer.h", "#include \"other.h\"\nb)\nouter_after\n" },
    { 0, 0 },
};

static const PPTestFile pp_test_files_stack[] = {
    { "a.h", "a_token\n#include \"b.h\"\na_after\n" },
    { "b.h", "b_token\n" },
    { "sys/s.h", "sys_first\n\n\n\n\n\n\n\n\n\n\nsys_far\n" },
    { "p.h", "user_before\n#pragma GCC system_header\nuser_after\n" },
    { 0, 0 },
};

// A file whose name is what a header name taken from the raw string literal R"(other.h)" would be.
static const PPTestFile pp_test_files_raw_header[] = {
    { "\"(other.h)", "found_by_a_raw_string\n" },
    { 0, 0 },
};

// Angle header bytes that resemble an unterminated raw literal must not consume following source.
static const PPTestFile pp_test_files_opaque_raw[] = {
    { "R\"(x", "header\n" },
    { "a\\>R\"(x", "escaped_header\n" },
    { 0, 0 },
};

// Runtime header queries may get their identity through a macro. Raw-looking header bytes remain header bytes;
// earlier and later macro definitions keep the prepared view that supplied their token indexes.
static const PPTestFile pp_test_files_raw_query[] = {
    { "R\"(x", "header\n" },
    { "raw-plain.h", "ordinary\n" },
    { "raw-view.h", "#ifndef RAW_REENTER\n#define RAW_REENTER\n#define RAW_BEFORE 11\n#if RAW_QUERY(<R\"(x>) // )\"\nouter\n#endif\n#define RAW_AFTER 22\n#include \"raw-view.h\"\nRAW_BEFORE RAW_AFTER\n#else\n#if RAW_QUERY(<R\"(x>) // )\"\ninner\n#endif\n#endif\n" },
    { "raw-once.h", "#if RAW_QUERY(<R\"(x>)\nonce\n#endif\n#pragma once\n" },
    { "raw-import.h", "#if RAW_QUERY(<R\"(x>)\nimported\n#endif\n" },
    { "raw-guard.h", "#ifndef RAW_GUARD\n#define RAW_GUARD\n#if RAW_QUERY(<R\"(x>) // )\"\nguarded\n#endif\n#endif\n" },
    { 0, 0 },
};

// #embed's resources: text, bytes of every kind (a zero, 255, the edges of one, two and three digits), every value in
// turn, nothing, and headers that end with an #embed, with and without a newline after it.
// #252's review's #embed files: a resource in a directory of its own, for a name with "//" (and one whose name starts
// with '*', for "/*"), and a header that becomes a system header before it embeds one.
static const PPTestFile pp_test_files_embed_review[] = {
    { "dir/bytes.bin", "A\n" },
    { "dir/*x.bin", "B\n" },
    { "hi.txt", "Hi!\n" },
    { "sys.h", "unsigned char a[] = {\n#pragma GCC system_header\n#embed \"hi.txt\" limit(1) suffix(, 300)\n};\n" },
    { "sys_indent.h", "unsigned char b[] = {\n#pragma GCC system_header\n  #embed \"hi.txt\" limit(1) suffix(, 300)\n};\n" },
    { "sys_blank.h", "unsigned char c[] = {\n#pragma GCC system_header\n\n#embed \"hi.txt\" limit(1) suffix(, 300)\n};\n" },
    { 0, 0 },
};

static const PPTestFile pp_test_files_embed[] = {
    { "hi.txt", "Hi!\n" },
    { "bin.dat", "\x00\xff\x7f\x80\x09\x0a\x63\x64", 8 },
    {
        "bytes.dat",
        "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f"
        "\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\x3e\x3f"
        "\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f"
        "\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x7b\x7c\x7d\x7e\x7f"
        "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
        "\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"
        "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf"
        "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff",
        256,
    },
    { "empty.txt", "" },
    { "embedlast.h", "a\n#embed \"hi.txt\" limit(1)\n" },
    { "embedlast_nonl.h", "a\n#embed \"hi.txt\" limit(1)" },
    { "tail.h", "x\n" },
    { 0, 0 },
};

// --embed-dir's resource, and headers in a directory of their own that embed one beside them, first thing and after
// blank lines and a comment.
static const PPTestFile pp_test_files_embed_directories[] = {
    { "dir/x.bin", "ED" },
    { "sub/inc.h", "#embed \"y.bin\"\n" },
    { "sub/late.h", "\n// first\n#embed \"y.bin\"\n" },
    { "sub/y.bin", "Y" },
    { 0, 0 },
};

// #119's header names and search: names with "//", "/*...*/" and a backslash written out, the file a comment that runs
// on to the next line makes a name of ("a b.h"), a computed name's file, #include_next through three directories, a
// header that includes itself, and one file reached through a symbolic link and a hard link, with #pragma once and
// with a guard. Then #254's review's: the file a header name too long for a path would name if its identifier were
// expanded, headers named " x.h" and "  x.h", each alone in a directory, and a header that includes itself to the depth
// limit and there includes another three times.
static const PPTestFile pp_test_files_includes[] = {
    { "a/b.h", "ab\n" },
    { "dir/*x.h", "opaque_header\n" },
    { "dir/*x.bin", "B\n" },
    { "a/*x*/b.h", "axb\n" },
    { "a\\b.h", "backslash\n" },
    { "a b.h", "joined\n" },
    { "c.h", "computed\n" },
    { "d1/n.h", "one\n#include_next <n.h>\n" },
    { "d2/n.h", "two\n#include_next <n.h>\n" },
    { "d3/n.h", "three\n" },
    { "self.h", "#include \"self.h\"\nx\n" },
    { "real.h", "#pragma once\nonce\n" },
    { .path = "sym.h", .link = "real.h" },
    { .path = "hard.h", .link = "real.h", .hard = true },
    { "guarded.h", "#ifndef G\n#define G\nguarded\n#endif\n" },
    { .path = "gsym.h", .link = "guarded.h" },
    { "local.h", "local\n" },
    { "short.h", "short\n" },
    { "one/ x.h", "one_space\n" },
    { "two/  x.h", "two_spaces\n" },
    { "loop.h", "#if __INCLUDE_LEVEL__ < 199\n#include \"loop.h\"\n#else\n#include \"leaf.h\"\n#include \"leaf.h\"\n#include \"leaf.h\"\n#endif\n" },
    { "leaf.h", "LEAF\n" },
    { 0, 0 },
};

// #254's second review's headers: a name with a backslash before a '>', and one reached through "//".
static const PPTestFile pp_test_files_header_tails[] = {
    { "a\\>b.h", "int escaped;\n" },
    { " x.h", "int one_space;\n" },
    { "x.h", "int x_h;\n" },
    { "dir/x.h", "int dir_x;\n" },
    { 0, 0 },
};

// 4,096 'A's: an identifier, and a header name too long for any path (#254's review).
#define BUSTER_PP_TEST_A64 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define BUSTER_PP_TEST_A512 BUSTER_PP_TEST_A64 BUSTER_PP_TEST_A64 BUSTER_PP_TEST_A64 BUSTER_PP_TEST_A64 BUSTER_PP_TEST_A64 BUSTER_PP_TEST_A64 BUSTER_PP_TEST_A64 BUSTER_PP_TEST_A64
#define BUSTER_PP_TEST_A4096 BUSTER_PP_TEST_A512 BUSTER_PP_TEST_A512 BUSTER_PP_TEST_A512 BUSTER_PP_TEST_A512 BUSTER_PP_TEST_A512 BUSTER_PP_TEST_A512 BUSTER_PP_TEST_A512 BUSTER_PP_TEST_A512
_Static_assert(sizeof(BUSTER_PP_TEST_A4096) == 4097, "BUSTER_PP_TEST_A4096 must be 4,096 letters");

static const PPTestCase pp_test_cases[] = {
    // The topics of C 6.10.4.5's examples, in cases of our own: rescanning and redefinitions, # and ## with indirection,
    // placemarkers, and variadic macros (the expected text is clang's output).
    { "rescanning, redefinitions and painting", "#define n 4\n#define twice(v) twice(n + (v))\n#undef n\n#define n 5\n#define alias twice\n#define box box{1}\n#define open alias(-\n#define call(f) f(k)\n#define k 7,8\n#define id(v) v\n#define kw() long\n#define one(v) v\n#define join(p,q) p ## q\n#define quote(v) # v\ntwice(a-1) * twice(twice(box)) + id(id(alias)(2) - id)(3);\nalias(n*(9,10)-k) & open 6) | call\n(twice)^call(call);\nkw() arr[one()] = { one(4), join(5,6), join(7,), join(,8), join(,) };\nchar s[2][4] = { quote(abc), quote() };\n",
      0, "c17", "twice(5 + (a-1)) * twice(5 + (twice(5 + (box{1})))) + twice(5 + (2)) - id(3);\ntwice(5 + (5*(9,10)-7,8)) & twice(5 + (- 6)) | twice(5 + (7,8))^call(7,8);\nlong arr[] = { 4, 56, 7, 8, };\nchar s[2][4] = { \"abc\", \"\" };\n", false },
    { "# and ## with indirection", "#define quote(v) # v\n#define xquote(v) quote(v)\n#define trace(a, b) log(\"v\" # a \" is %d, v\" # b \" is %s\", \\\n v ## a, v ## b)\n#define PART_OF(n) part ## n\n#define cat(a, b) a ## b\n#define xcat(a, b) cat(a, b)\n#define UPDOWN \"top\"\n#define DOWN DOWN \" bottom\"\ntrace(3, 4);\nputs(quote(strcmp(\"a\\0b\", \"a\", '\\5') // dropped\n != 0) quote(! #\\n), out);\nxquote(PART_OF(7).inc)\ncat(UP, DOWN);\nxcat(UP, DOWN)\n",
      0, "c17", "log(\"v\" \"3\" \" is %d, v\" \"4\" \" is %s\", v3, v4);\nputs(\"strcmp(\\\"a\\\\0b\\\", \\\"a\\\", '\\\\5') != 0\" \"! #\\n\", out);\n\"part7.inc\"\n\"top\";\n\"top\" \" bottom\"\n", false },
    { "placemarkers", "#define tri(a,b,c) a ## b ## c\nint v[] = { tri(9,8,7), tri(,6,5), tri(4,,3), tri(2,1,),\n tri(10,,), tri(,20,), tri(,,30), tri(,,) };\n",
      0, "c17", "\nint v[] = { 987, 65, 43, 21,\n 10, 20, 30, };\n", false },
    { "variadic macros", "#define log_all(...) logf(level, __VA_ARGS__)\n#define names(...) show(#__VA_ARGS__)\n#define check(c, ...) ((c) ? show(#c) : \\\n emit(__VA_ARGS__))\nlog_all(\"start\");\nlog_all(\"n = %d\\n\", n);\nnames(red, green and blue);\ncheck(a < b, \"a is %d, b is %d\", a, b);\n",
      0, "c17", "logf(level, \"start\");\nlogf(level, \"n = %d\\n\", n);\nshow(\"red, green and blue\");\n((a < b) ? show(\"a < b\") : emit(\"a is %d, b is %d\", a, b));\n", false },
    // The cases the notes probed with GCC and clang (preprocessor-logical-spacing, preprocessor-expansion-boundaries).
    { "splice makes a function-like macro", "#define F\\\n(x) x + 10\nF(2)\n", 0, "c17", "2 + 10\n", false },
    { "comment makes an object-like macro", "#define F/**/(x) x + 10\nF(2)\n", 0, "c17", "(x) x + 10(2)\n", false },
    { "stringize across a splice and a comment", "#define S(x) #x\nS(a\\\n+b)\nS(a/**/+b)\n", 0, "c17", "\"a+b\"\n\"a +b\"\n", false },
    { "redefinition with other spacing", "#define X(x) x+1\n#define X(x) x + 1\nX(2)\n", 0, "c17", "2 + 1\n", false, 1 },
    { "a disabled macro's last token", "#define foo(x) bar x\nfoo(foo) (2)\n", 0, "c17", "bar foo (2)\n", false },
    // M's template finds N painted while N's expansion is under way, which it mustn't keep (pp_template_names) for the
    // M(2) after it, whose N expands.
    { "a name painted while its macro is disabled, then expanded", "#define N M(1)\n#define M(a) N a\nN\nM(2)\nint x = M(3);\n", 0, "c17",
      "N 1\nM(1) 2\nint x = M(1) 3;\n", false },
    // Templates of a few tokens whose names' bodies make their expansions longer than a template writes: past 16 tokens,
    // exactly 16 and 17 (pp_template_size).
    { "expansions past a template's tokens",
      "#define LONG a b c d e f g h i j\n#define M(x) LONG x LONG\n#define SEVEN 1 2 3 4 5 6 7\n#define B16(x) SEVEN x SEVEN x\n#define B17(x) SEVEN x SEVEN x x\nM(1)\nB16(.)\nB17(.)\n",
      0, "c17", "a b c d e f g h i j 1 a b c d e f g h i j\n1 2 3 4 5 6 7 . 1 2 3 4 5 6 7 .\n1 2 3 4 5 6 7 . 1 2 3 4 5 6 7 . .\n", false },
    // An empty expansion of a template with a space between its tokens leaves that space before the next token.
    { "an empty expansion's spaces", "#define E(a,b) a b\n#define F(a) a\n[E(,)] [E(,)x] (F()y)\n", 0, "c17", "[ ] [ x] (y)\n", false },
    // One template's names found again after each change of the macros' table (Preprocessor.macro_epoch): its direct
    // object redefined longer, pushed, redefined, popped and undefined, invoked in a file's text and collected.
    { "a template's names across #define, #undef, push_macro and pop_macro",
      "#define D 1\n#define F(x) D x\n#define G F(g)\nF(a) G\n#undef D\n#define D 2 3\nF(b) G\n#pragma push_macro(\"D\")\n#undef D\n#define D 4 5 6\nF(c) G\n"
      "#pragma pop_macro(\"D\")\nF(d) G\n#undef D\nF(e) G\n",
      0, "c17", "1 a 1 g\n2 3 b 2 3 g\n4 5 6 c 4 5 6 g\n2 3 d 2 3 g\nD e D g\n", false },
    { "an alias's invocation", "#define F(x) x\n#define ALIAS F\nALIAS (7)\n", 0, "c17", "7\n", false },
    { "adjacent tokens don't paste", "#define PLUS +\n+PLUS\n", 0, "c17", "+ +\n", false },
    { "an unused argument isn't expanded", "#define BAD(x,y) x\n#define DROP(x) 0\nDROP(BAD(1))\n", 0, "c17", "0\n", false },
    { "a stringized argument isn't expanded", "#define BAD(x,y) x\n#define S(x) #x\nS(BAD(1))\n", 0, "c17", "\"BAD(1)\"\n", false },
    { "a dead arm's macros still expand", "#define BAD(x,y) x\n#if 0 && BAD(1)\n#endif\n", 0, "c17", "", true },
    { "a dead arm's division isn't evaluated", "#if 0 && (1/0)\nx\n#else\ny\n#endif\n", 0, "c17", "y\n", false },
    { "a dead arm's division skips an #error", "#if 0 && (1/0)\n#error unexpected\n#endif\nint x;\n", 0, "c17", "int x;\n", false },
    { "a pp-number without exponent digits in a skipped group", "#if 0\nint discarded=1e+;\n#endif\nint retained;\n", 0, "c17", "int retained;\n", false },
    // 1e+ stays one pp-number: the error ("exponent has no digits") is the parser's.
    { "a pp-number without exponent digits", "int x=1e+;\n", 0, "c17", "int x=1e+;\n", false },
    // Redefinitions are compared by spelling (1 and 01, [ and <:, parameter names) and whitespace, after splices: GCC
    // and clang warn about A, B and C only.
    { "redefinitions with other spellings", "#define A 1\n#define A 01\n#define B [\n#define B <:\n#define C(x) x\n#define C(y) y\n#define D (1 + 2)\n"
                                            "#define D (1 + 2)\n#define E(x) x\n#define E(x)  x\n#define F(x) x  +  1\n#define F(x) x + 1\n#define G 1\\\n0\n"
                                            "#define G 10\n#define H \"a\\\nb\"\n#define H \"ab\"\nA B C(3) D E(4) F(5) G H\n",
      0, "c17", "01 <: 3 (1 + 2) 4 5 + 1 10 \"ab\"\n", false, 3 },
    { "true in C17", "#if true\nyes\n#else\nno\n#endif\n", 0, "c17", "no\n", false },
    { "true in C23", "#if true\nyes\n#else\nno\n#endif\n", 0, "c23", "yes\n", false },
    { "__VA_OPT__ of an argument that expands to nothing", "#define EMPTY\n#define V(...) 1 __VA_OPT__(+ 2)\nV()\nV(EMPTY)\nV(x)\n", 0, "c23", "1\n1\n1 + 2\n", false },
    // Probes of clang (docs/c-preprocessor.md).
    { "GNU comma and __VA_OPT__", "#define F(x, ...) f(x __VA_OPT__(,) __VA_ARGS__)\nF(1) F(1,2) F(1,)\n#define C(a,...) g(a, ## __VA_ARGS__)\nC(1) C(1,) C(1,2)\n",
      0, "c23", "f(1) f(1, 2) f(1)\ng(1) g(1,) g(1,2)\n", false },
    { "pastes", "#define CAT(a,b) a##b\nCAT(x,y) CAT(,y) CAT(x,) CAT(,) CAT(1,e) CAT(<,<=) CAT(+,) CAT(L, \"x\") CAT(1,.e+)\n", 0, "c17",
      "xy y x 1e <<= + L\"x\" 1. e+\n", false },
    { "stringize spacing and escapes", "#define H(x) # x\nH(  a   \"\\n\"  ) H() H( + ) H('\\'' @ L\"x\")\n", 0, "c17",
      "\"a \\\"\\\\n\\\"\" \"\" \"+\" \"'\\\\'' @ L\\\"x\\\"\"\n", false },
    { "a function-like name without arguments", "#define EMPTY\n#define LPAREN (\n#define f(x) [x]\nf EMPTY (1)\nf LPAREN 2)\n#define g f\ng(3)\n", 0, "c17",
      "f (1)\nf ( 2)\n[3]\n", false },
    { "builtins", "a __COUNTER__ __COUNTER__ __INCLUDE_LEVEL__ __has_attribute(noreturn) __has_builtin(__builtin_expect) __has_feature(c_static_assert)\n"
                  "#define X noreturn\n#if __has_attribute(X) && __has_include(\"other.h\") && !__has_include(\"missing.h\") && defined X && !defined(Y)\nb\n#endif\n"
                  "#define G(x) __LINE__ x\nint a = G(\n1\n);\n",
      "", "c17", "a 0 1 0 1 1 1\nb\nint a = 9 1;\n", false },
    { "include guard and #pragma once", "#include \"other.h\"\n#include \"other.h\"\nother_t x;\n",
      "#ifndef OTHER_H\n#define OTHER_H\ntypedef int other_t;\n#endif\n", "c17", "typedef int other_t;\nother_t x;\n", false },
    { "pragmas", "#pragma once\n#pragma GCC system_header\n#pragma STDC FP_CONTRACT ON\n#define P(x) _Pragma(#x) after\nP(omp parallel for)\n#pragma push_macro(\"X\")\n"
                 "#define X 1\n#pragma pop_macro(\"X\")\nX\n",
      0, "c17", "#pragma STDC FP_CONTRACT ON\n#pragma omp parallel for\nafter\nX\n", false, 2 },
    { "#if arithmetic", "#if -1 < 0u\nunsigned\n#endif\n#if (2 || 1/0) && (0 ? 1/0 : 3) == 3 && 0x10 == 16 && 010 == 8 && 0b101 == 5 && 1'000 == 1000 && 'A' == 65 && '\\377' < 0\nok\n#endif\n"
                     "#if (-9223372036854775807 - 1) / -1 == (-9223372036854775807 - 1) && 18446744073709551615u == -1\nwraps\n#endif\n#if 1 ? 2 : 3 ? 4 : 5\nternary\n#endif\n",
      0, "c23", "ok\nwraps\nternary\n", false },
    { "#elif chains and nesting", "#define A 2\n#if A == 1\none\n#elif A == 2\n#ifdef A\ntwo\n#else\nnot\n#endif\n#elif 1/0\nthree\n#else\nfour\n#endif\n#ifndef B\nb\n#elifdef A\nno\n#endif\n",
      0, "c23", "two\nb\n", false },
    // Argument scanning past its fast path's limits: an argument across two 64-token blocks, parentheses nested 70 deep,
    // 70 arguments, and a variadic argument of 69 commas.
    { "an argument across blocks", "#define F(a, b) [a | b]\nF(( 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9 , 10 , 11 , 12 , 13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 , 24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 ), x)\nF(x, ( 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9 , 10 , 11 , 12 , 13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 , 24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 ))\n", 0, "c17",
      "[( 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9 , 10 , 11 , 12 , 13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 , 24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 ) | x]\n[x | ( 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9 , 10 , 11 , 12 , 13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 , 24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 )]\n", false },
    { "parentheses nested 70 deep", "#define F(a, b) [a | b]\nF(((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((x)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))), y)\n", 0, "c17", "[((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((x)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))) | y]\n", false },
    { "70 arguments", "#define M(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69) a0 a69\nM(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69)\n#define V(first, ...) <first> __VA_ARGS__\nV(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69)\n", 0, "c17",
      "0 69\n<0> 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69\n", false },
    // Conditional pairing across 32-directive blocks: 40 nested groups, and a group with 100 directives inside.
    { "conditionals nested 40 deep", "#if 1\na0\n#if 1\na1\n#if 1\na2\n#if 1\na3\n#if 1\na4\n#if 1\na5\n#if 1\na6\n#if 1\na7\n#if 1\na8\n#if 1\na9\n#if 1\na10\n#if 1\na11\n#if 1\na12\n#if 1\na13\n#if 1\na14\n#if 1\na15\n#if 1\na16\n#if 1\na17\n#if 1\na18\n#if 1\na19\n#if 1\na20\n#if 1\na21\n#if 1\na22\n#if 1\na23\n#if 1\na24\n#if 1\na25\n#if 1\na26\n#if 1\na27\n#if 1\na28\n#if 1\na29\n#if 1\na30\n#if 1\na31\n#if 1\na32\n#if 1\na33\n#if 1\na34\n#if 1\na35\n#if 1\na36\n#if 1\na37\n#if 1\na38\n#if 1\na39\ncore\n#else\nb39\n#endif\n#else\nb38\n#endif\n#else\nb37\n#endif\n#else\nb36\n#endif\n#else\nb35\n#endif\n#else\nb34\n#endif\n#else\nb33\n#endif\n#else\nb32\n#endif\n#else\nb31\n#endif\n#else\nb30\n#endif\n#else\nb29\n#endif\n#else\nb28\n#endif\n#else\nb27\n#endif\n#else\nb26\n#endif\n#else\nb25\n#endif\n#else\nb24\n#endif\n#else\nb23\n#endif\n#else\nb22\n#endif\n#else\nb21\n#endif\n#else\nb20\n#endif\n#else\nb19\n#endif\n#else\nb18\n#endif\n#else\nb17\n#endif\n#else\nb16\n#endif\n#else\nb15\n#endif\n#else\nb14\n#endif\n#else\nb13\n#endif\n#else\nb12\n#endif\n#else\nb11\n#endif\n#else\nb10\n#endif\n#else\nb9\n#endif\n#else\nb8\n#endif\n#else\nb7\n#endif\n#else\nb6\n#endif\n#else\nb5\n#endif\n#else\nb4\n#endif\n#else\nb3\n#endif\n#else\nb2\n#endif\n#else\nb1\n#endif\n#else\nb0\n#endif\n", 0, "c17", "a0\na1\na2\na3\na4\na5\na6\na7\na8\na9\na10\na11\na12\na13\na14\na15\na16\na17\na18\na19\na20\na21\na22\na23\na24\na25\na26\na27\na28\na29\na30\na31\na32\na33\na34\na35\na36\na37\na38\na39\ncore\n", false },
    { "a group around 50 others", "#if 0\n#ifdef X0\nx0\n#endif\n#ifdef X1\nx1\n#endif\n#ifdef X2\nx2\n#endif\n#ifdef X3\nx3\n#endif\n#ifdef X4\nx4\n#endif\n#ifdef X5\nx5\n#endif\n#ifdef X6\nx6\n#endif\n#ifdef X7\nx7\n#endif\n#ifdef X8\nx8\n#endif\n#ifdef X9\nx9\n#endif\n#ifdef X10\nx10\n#endif\n#ifdef X11\nx11\n#endif\n#ifdef X12\nx12\n#endif\n#ifdef X13\nx13\n#endif\n#ifdef X14\nx14\n#endif\n#ifdef X15\nx15\n#endif\n#ifdef X16\nx16\n#endif\n#ifdef X17\nx17\n#endif\n#ifdef X18\nx18\n#endif\n#ifdef X19\nx19\n#endif\n#ifdef X20\nx20\n#endif\n#ifdef X21\nx21\n#endif\n#ifdef X22\nx22\n#endif\n#ifdef X23\nx23\n#endif\n#ifdef X24\nx24\n#endif\n#ifdef X25\nx25\n#endif\n#ifdef X26\nx26\n#endif\n#ifdef X27\nx27\n#endif\n#ifdef X28\nx28\n#endif\n#ifdef X29\nx29\n#endif\n#ifdef X30\nx30\n#endif\n#ifdef X31\nx31\n#endif\n#ifdef X32\nx32\n#endif\n#ifdef X33\nx33\n#endif\n#ifdef X34\nx34\n#endif\n#ifdef X35\nx35\n#endif\n#ifdef X36\nx36\n#endif\n#ifdef X37\nx37\n#endif\n#ifdef X38\nx38\n#endif\n#ifdef X39\nx39\n#endif\n#ifdef X40\nx40\n#endif\n#ifdef X41\nx41\n#endif\n#ifdef X42\nx42\n#endif\n#ifdef X43\nx43\n#endif\n#ifdef X44\nx44\n#endif\n#ifdef X45\nx45\n#endif\n#ifdef X46\nx46\n#endif\n#ifdef X47\nx47\n#endif\n#ifdef X48\nx48\n#endif\n#ifdef X49\nx49\n#endif\n#else\nok\n#endif\n", 0, "c17", "ok\n", false },
    // Unbalanced groups, which the vectorized pairing leaves to the stack.
    { "an #endif too many", "#if 1\na\n#endif\n#endif\nb\n", 0, "c17", "", true },
    { "an #elif after #else", "#if 0\na\n#else\nb\n#elif 1\nc\n#endif\n", 0, "c17", "", true },
    { "an unterminated group", "#if 1\na\n#if 0\nb\n#endif\n", 0, "c17", "", true },
    // The main loop's frames: directives among a macro's arguments (a #if line expanded there, a #define), which the main
    // loop collects a token at a time; builtins' operands from other contexts, and a #define in one; _Pragma from a
    // macro; a computed #include; #line with a macro, and a line marker.
    { "directives among a macro's arguments", "#define F(a, b) [a | b]\n#define ONE 1\nF(x,\n#if ONE + 1 == 2\nyes\n#else\nno\n#endif\n)\nF(\n#ifdef ONE\np\n#endif\n, q)\nF(r,\n#define LATE 7\nLATE)\nLATE\n#define G F\nG(\n#if defined(ONE) && __has_include(\"other.h\")\ns\n#endif\n, t)\n", "", "c17", "[x | yes]\n[p | q]\n[r | 7]\n7\n[s | t]\n", false },
    { "builtins' operands, a computed #include and #line", "#define H __has_attribute\nint a = H(noreturn) + H(\nnot_an_attribute\n);\nint b = __has_attribute(\n#define Z 1\nnoreturn) + Z;\n#define PR _Pragma\nPR(\"message(\\\"hi\\\")\") after\n#define HDR \"other.h\"\n#include HDR\n#define N 100\n#line N\nint line = __LINE__;\n# 50 \"renamed.c\"\nint again = __LINE__;\n#if __has_include(HDR) && !__has_include(\"missing.h\")\nboth\n#endif\n", "typedef int other_t;\n", "c17",
      "int a = 1 + 0;\nint b = 1 + 1;\n#pragma message(\"hi\")\nafter\ntypedef int other_t;\nint line = 100;\nint again = 50;\nboth\n", false, 1 },
    { "unterminated invocation", "#define f(x) x\nf(1\n", 0, "c17", "", true },
    // An identifier is printed as it is interned (a UCN as UTF-8), but stringized as written, without its splices.
    { "identifiers written with UCNs and splices", "#define S(x) #x\ncaf\\u00e9 S(caf\\u00e9) S(caf\xC3\xA9) S(fo\\\no) S(\\U000000E9t\\\n\\u00e9)\n", 0,
      "c17", "caf\xC3\xA9 \"caf\\u00e9\" \"caf\xC3\xA9\" \"foo\" \"\\U000000E9t\\u00e9\"\n", false },    // Review fixes (#193): a missing header by absolute path, as __has_include asks and as #include and -include name it
    // (an error, not a crash).
    { "a missing absolute header", "#if __has_include(\"/buster-missing-directory/absent.h\") || __has_include(</buster-missing-directory/absent.h>)\nfound\n#else\nmissing\n#endif\n",
      0, "c17", "missing\n", false, 0, 0, 0 },
    // __VA_OPT__ on either side of ## (C 6.10.5.2): a placemarker when it gives no tokens, else its first or last token
    // pasted; an empty argument at an end of its content is a placemarker there (P1, P2), and content of placemarkers
    // alone gives none (P4(, , 1): clang's ab, where GCC gives a b).
    { "__VA_OPT__ next to ##", "#define E1 z\n#define EMPTY\n#define F(...) __VA_OPT__(x) ## y\nF() F(1)\n#define G(...) prefix __VA_OPT__(x) ## y\nG() G(1)\n#define H(...) __VA_OPT__() ## y\nH() H(1)\n#define R(...) a ## __VA_OPT__(b c)\nR() R(1)\n#define S(...) a ## __VA_OPT__(__VA_ARGS__ q)\nS() S(E1) S(1 2)\n#define T(x, ...) __VA_OPT__(x) ## x\nT(1) T(1, 2)\n#define U(...) __VA_OPT__(a ## b) ## c\nU() U(1)\n#define W(...) __VA_OPT__(x)##__VA_OPT__(y)\nW() W(1)\n#define X(a, ...) a ## __VA_OPT__(a) ## a\nX(1) X(1, 2)\n#define Y(...) __VA_OPT__(__VA_ARGS__) ## y\nY() Y(E1) Y(1 2) Y(E1 E1)\n#define Z(p, ...) p ## __VA_OPT__() ## p\nZ(1) Z(1, 2)\n#define P(...) __VA_OPT__(x y) ## __VA_OPT__(z w)\nP() P(1)\n#define P1(x, ...) __VA_OPT__(a x) ## y\nP1(E1, 1) P1(EMPTY, 1) P1(, 1)\n#define P2(x, ...) a ## __VA_OPT__(x b)\nP2(, 1) P2(E1, 1) P2(c, 1)\n#define P4(x, y, ...) a ## __VA_OPT__(x y) ## b\nP4(, , 1) P4(c, , 1) P4(, d, 1) P4(c, d, 1)\n#define S2(...) L ## #__VA_OPT__(a b)\nS2() S2(1)\n#define V2(...) w __VA_OPT__( __VA_ARGS__ ) ## v\nV2() V2( 1 ) V2(1 2)\n",
      0, "c23", "y xy\nprefix y prefix xy\ny y\na ab c\na az q a1 2 q\n1 11\nc abc\nxy\n11 111\ny zy 1 2y z zy\n11 11\nx yz w\na zy a y a y\na b az b ac b\nab ac b a db ac db\nL\"\" L\"a b\"\nw v w 1v w 1 2v\n", false, 0, 0, 0 },
    // An empty expansion's or empty argument's leading space goes to the next token, as clang does.
    { "spaces through empty expansions", "#define EMPTY\n#define S(x) #x\n#define EXPAND(x) S(x)\nEXPAND(a EMPTY+b)\n#define F(x) a x+b\nEXPAND(F())\n#define G()\nEXPAND(a G()+b)\n#define H(x) a x\nEXPAND(H()+b)\n#define E2 EMPTY\nEXPAND(a E2+b)\n#define J(x, y) [x y]\nEXPAND(J(, +))\n#define K(x) x\nEXPAND(a K()+b)\n#define V(...) a __VA_OPT__(b)c\nEXPAND(V() V(1))\n",
      0, "c23", "\"a +b\"\n\"a +b\"\n\"a +b\"\n\"a +b\"\n\"a +b\"\n\"[ +]\"\n\"a +b\"\n\"a c a bc\"\n", false, 0, 0, 0 },
    // #if and #elif lines that pp_condition_direct evaluates without a capture: names through chains of object-like
    // macros to numbers and to names of no macro, defined X and defined(X), C23's true through a macro, and a
    // self-referential name, which it leaves to the capture.
    { "#if lines without a capture", "#define ONE 1\n#define TWO 2\n#define CHAIN ONE\n#define CHAIN2 CHAIN\n#define NAME undefined_name\n#define TRUE_NAME true\n#define SELF SELF\n#if CHAIN2 + TWO == 3 && defined(ONE) && defined TWO && !defined(THREE)\na\n#elif CHAIN\nb\n#endif\n#if NAME || SELF\nc\n#elif defined CHAIN2 && CHAIN2 > 0\nd\n#endif\n#if TRUE_NAME\ne\n#endif\n#if 0\n#elif TWO*TWO == 4\nf\n#endif\n#ifdef ONE\n#if ONE ? CHAIN : TWO\ng\n#endif\n#endif\n",
      0, "c23", "a\nd\ne\nf\ng\n", false },
    // Names on such lines that pp_condition_direct leaves to the capture without reading their bodies (#410's review): an
    // empty object-like macro, one defined at the end of an included file (its body begins at the file's last token), a
    // builtin, a function-like macro and one that pastes; and an empty one alone, an error.
    { "#if lines of macros without a body of one token", "#include \"other.h\"\n#define EMPTY\n#define ZERO 0\n#define F() 1\n#define P 1 ## 0\n#if EMPTY 1\na\n#endif\n#if EMPTY_LAST 2 == 2 && ZERO == 0\nb\n#endif\n#if __LINE__ > 0\nc\n#endif\n#if F() == 1\nd\n#endif\n#if P == 10\ne\n#endif\n",
      "#define EMPTY_LAST\n", "c17", "a\nb\nc\nd\ne\n", false },
    { "#if of an empty macro", "#define EMPTY\n#if EMPTY\nx\n#endif\ny\n", 0, "c17", "y\n", true, 0, 0, 0, 0, 1 },
    // Invocations read from a file's text whose names have no space before them: an empty expansion's trailing space, and
    // a nonempty one's, go to the next token (pp_invoke_file's, as pp_finish_simple's).
    { "spaces after invocations in a file's text", "#define E(a, b) a b\n#define F(a, b, c) [a b c]\n#define G(a)\n#define H(a, b) a b\nx=E(,)+1;\ny=E( , )-2;\nz=F(,,)*3;\nw=G(1)+4;\nv=H(,)H(,)5;\nu=H(1,)+6;\n",
      0, "c17", "x= +1;\ny= -2;\nz=[ ]*3;\nw=+4;\nv= 5;\nu=1 +6;\n", false },
    { "__has_include in a macro's argument in #if", "#define ID(x) x\n#if ID(__has_include(\"other.h\")) && ID(!__has_include(\"missing.h\"))\npresent\n#else\nabsent\n#endif\n",
      "x\n", "c17", "present\n", false, 0, 0, 0 },
    { "header names with line splices", "#include \"oth\\\ner.h\"\n#include <oth\\\ner.h>\n",
      "spliced\n", "c17", "spliced\nspliced\n", false, 0, "-I %d", 0 },
    { "a repeated -I and #include_next", "#include <wrapper.h>\n",
      0, "c17", "LAYER_ONE\nLAYER_TWO\n", false, 0, "-I %d/first -I %d/first -I %d/second -I %d/first", pp_test_files_include_next },
    { "_Pragma of a macro", "#define P \"omp parallel for\"\n_Pragma(P)\n#define Q(x) _Pragma(x)\nQ(\"STDC FP_CONTRACT ON\")\n#define R _Pragma(P)\nR after\n",
      0, "c17", "#pragma omp parallel for\n#pragma STDC FP_CONTRACT ON\n#pragma omp parallel for\nafter\n", false, 0, 0, 0 },
    { "#line without a file name", "#line 100 \"virtual.h\"\n#line 200\n__FILE__ __LINE__\n# 300\n__FILE__ __LINE__\n",
      0, "c17", "\"virtual.h\" 200\n\"virtual.h\" 300\n", false, 0, 0, 0 },
    { "__is_identifier", "#define M int\n__is_identifier(x) __is_identifier(int) __is_identifier(M) __is_identifier(1) __is_identifier(+) __is_identifier(bool) __is_identifier(__attribute__) __is_identifier(defined) __is_identifier(typeof) __is_identifier(asm)\n",
      0, "gnu17", "1 0 1 0 0 1 0 1 0 0\n", false, 0, 0, 0 },
    { "__is_identifier in C23", "__is_identifier(bool) __is_identifier(typeof) __is_identifier(asm) __is_identifier(restrict) __is_identifier(inline)\n",
      0, "c23", "0 0 1 0 0\n", false, 0, 0, 0 },
    // GNU's keywords follow the last -f[no-]gnu-keywords, or else the last of GCC's -f[no-]asm, or else the mode (#281).
    { "-fno-gnu-keywords", "__is_identifier(asm) __is_identifier(typeof) __is_identifier(inline)\n", 0, "gnu89", "1 1 1\n", false, 0, "-fno-gnu-keywords", 0 },
    { "-fasm in c89", "__is_identifier(asm) __is_identifier(typeof) __is_identifier(inline)\n", 0, "c89", "0 0 0\n", false, 0, "-fasm", 0 },
    { "-fgnu-keywords before -fno-asm", "__is_identifier(asm) __is_identifier(typeof)\n", 0, "c17", "0 0\n", false, 0, "-fgnu-keywords -fno-asm", 0 },
    { "-fno-gnu-keywords in C23", "__is_identifier(asm) __is_identifier(typeof)\n", 0, "c23", "1 0\n", false, 0, "-fasm -fno-gnu-keywords", 0 },
    // '$' is punctuation in C with -fno-dollars-in-identifiers, as in assembly, which -fdollars-in-identifiers gives
    // identifiers with '$' (#281).
    { "-fno-dollars-in-identifiers", "#define bb BB\n#define S(x) #x\naa$bb 1$bb $bb S(aa$bb) S($ bb$) S(caf\\u00e9$bb)\n", 0, "c17",
      "aa $ BB 1 $ BB $ BB \"aa$bb\" \"$ bb$\" \"caf\\u00e9$bb\"\n", false, 0, "-fno-dollars-in-identifiers", 0 },
    { "pasting '$' as punctuation", "#define cat(a, b) a ## b\ncat(x, $)\n", 0, "c17", "x $\n", true, 0, "-fdollars-in-identifiers -fno-dollars-in-identifiers", 0 },
    { "-fdollars-in-identifiers in assembly", "#define bb BB\naa$bb $bb\n", 0, "gnu17", "aa$bb $bb\n", false, 0, "-x assembler-with-cpp -fdollars-in-identifiers", 0 },
    // What follows a '$' joins the tokens after it as clang lexes them (#283's review): a literal's prefix (u8'x' only
    // from C23), and a pp-number's continuation; and a quote C23's digit separators free, which takes the rest of the line.
    { "'$' before prefixed literals and pp-numbers", "#define L changed\n#define u changed\n#define U changed\n#define u8 changed\n#define e changed\n#define S_(...) #__VA_ARGS__\n#define S(...) S_(__VA_ARGS__)\nS($L\"x\") S($u\"x\") S($U'x') S($u8\"x\") S(a$L\"x\") S($u8'x') S($1.e) S(x$1.5) S(1$e+5)\n", 0, "c17",
      "\"$L\\\"x\\\"\" \"$u\\\"x\\\"\" \"$U'x'\" \"$u8\\\"x\\\"\" \"a$L\\\"x\\\"\" \"$changed'x'\" \"$1.e\" \"x$1.5\" \"1$changed+5\"\n", false, 0, "-fno-dollars-in-identifiers", 0 },
    { "'$' before prefixed literals in C23", "#define L changed\n#define u changed\n#define U changed\n#define u8 changed\n#define e changed\n#define S_(...) #__VA_ARGS__\n#define S(...) S_(__VA_ARGS__)\nS($L\"x\") S($u\"x\") S($U'x') S($u8\"x\") S(a$L\"x\") S($u8'x') S($1.e) S(x$1.5) S(1$e+5)\n", 0, "c23",
      "\"$L\\\"x\\\"\" \"$u\\\"x\\\"\" \"$U'x'\" \"$u8\\\"x\\\"\" \"a$L\\\"x\\\"\" \"$u8'x'\" \"$1.e\" \"x$1.5\" \"1$changed+5\"\n", false, 0, "-fno-dollars-in-identifiers", 0 },
    { "'$' before prefixed literals in assembly", "#define L changed\n#define u changed\n#define U changed\n#define u8 changed\n#define e changed\n#define S_(...) #__VA_ARGS__\n#define S(...) S_(__VA_ARGS__)\nS($L\"x\") S($u\"x\") S($U'x') S($u8\"x\") S(a$L\"x\") S($u8'x') S($1.e) S(x$1.5) S(1$e+5)\n", 0, "gnu17",
      "\"$L\\\"x\\\"\" \"$u\\\"x\\\"\" \"$U'x'\" \"$u8\\\"x\\\"\" \"a$L\\\"x\\\"\" \"$changed'x'\" \"$1.e\" \"x$1.5\" \"1$changed+5\"\n", false, 0, "-x assembler-with-cpp", 0 },
    { "'$' before a prefixed literal across a line splice", "#define L changed\n#define S_(...) #__VA_ARGS__\n#define S(...) S_(__VA_ARGS__)\nS($L\\\n\"y\") $L\\\n\"z\"\n", 0, "c17", "\"$L\\\"y\\\"\" $ L\"z\"\n", false, 0, "-fno-dollars-in-identifiers", 0 },
    { "'$' before a prefixed literal across a line splice in assembly", "#define L changed\n#define S_(...) #__VA_ARGS__\n#define S(...) S_(__VA_ARGS__)\nS($L\\\n\"y\") $L\\\n\"z\"\n", 0, "gnu17", "\"$L\\\"y\\\"\" $ L\"z\"\n", false, 0, "-x assembler-with-cpp", 0 },
    { "a quote after '$' that a digit separator frees", "#define x X\n$1'2' + x\nnext x\n", 0, "c23", "$ 1'2 ' + x\nnext X\n", false, 0, "-fno-dollars-in-identifiers", 0 },
    // Character constants in #if are unsigned with -funsigned-char (or -fno-signed-char), and a multi-character one's
    // first byte isn't sign-extended (#281).
    { "-funsigned-char in #if", "#if '\\xff' == 255 && 'a' - 'b' > 0 && -'\\x01' > 0 && '\\377\\377' == 65535 && '\\377abc' > 0\nyes\n#endif\n",
      0, "c17", "yes\n", false, 1, "-fsigned-char -fno-signed-char", 0 },
    { "signed char in #if", "#if '\\xff' == -1 && 'a' - 'b' < 0 && '\\377\\377' == 65535 && '\\377abc' < 0 && 'abcde' == 0x62636465\nyes\n#endif\n",
      0, "c17", "yes\n", false, 3, "-funsigned-char -fno-unsigned-char", 0 },
    // Raw string literals (#285): R and LR ones in GNU modes from C99 on, u8R, uR and UR ones from C11 (whose prefixes
    // they need), in any mode with -fraw-string-literals and in none with -fno-raw-string-literals, in skipped groups
    // too; their spellings as written after the opening quote (line splices and trigraphs kept), pasted, stringized
    // (line breaks as \n), as _Pragma's operand (the body) and #line's and a line marker's file name (the body);
    // neither a header name nor (from a string) __has_warning's name, but its body; after '$' where '$' is punctuation;
    // and -E's lines, which count the line breaks in an unknown token (including an invalid raw delimiter) and in no other.
    { "raw string literals in a GNU mode", "#define R RR\n#define LR LRR\n#define u8R u8RR\n#define uR uRR\n#define UR URR\nR\"x(a \"b\n c)x\" LR\"(\\\"\\\\)\" u8R\"(d)\" uR\"(e)\" UR\"y(f)y\" R\"0123456789abcdef(g)0123456789abcdef\" R(h) xR\"(i)\" 1R\"(j)\"\nafter\n", 0, "gnu17", "R\"x(a \"b\n c)x\" LR\"(\\\"\\\\)\" u8R\"(d)\" uR\"(e)\" UR\"y(f)y\" R\"0123456789abcdef(g)0123456789abcdef\" RR(h) xR\"(i)\" 1R\"(j)\"\nafter\n", false, 0, 0, 0, "# 1 \"%d/case.c\"\n\n\n\n\n\nR\"x(a \"b\n c)x\" LR\"(\\\"\\\\)\" u8R\"(d)\" uR\"(e)\" UR\"y(f)y\" R\"0123456789abcdef(g)0123456789abcdef\" RR(h) xR \"(i)\" 1R\"(j)\"\n\nafter\n" },
    { "raw string literals in gnu99", "#define uR uRR\n#define UR URR\n#define u8R u8RR\nR\"(a)\" LR\"(b)\" uR\"(c)\" UR\"(d)\" u8R\"(e)\"\n", 0, "gnu99", "R\"(a)\" LR\"(b)\" uRR \"(c)\" URR \"(d)\" u8RR \"(e)\"\n", false, 0, 0, 0 },
    { "no raw string literals in an ISO mode", "#define R RR\n#define LR LRR\nR\"(a)\" LR\"(b)\"\n", 0, "c17", "RR \"(a)\" LRR \"(b)\"\n", false, 0, 0, 0 },
    { "-fraw-string-literals in an ISO mode", "#define R RR\nR\"(a)\" u8R\"(b)\" R\"(?\?=)\" ?\?=\n", 0, "c17", "R\"(a)\" u8R\"(b)\" R\"(?\?=)\" #\n", false, 0, "-fraw-string-literals", 0 },
    { "-fno-raw-string-literals in a GNU mode", "#define R RR\nR\"(a)\"\n", 0, "gnu17", "RR \"(a)\"\n", false, 0, "-fno-raw-string-literals", 0 },
    { "raw string literals over lines and directives", "#if 0\nR\"(\n#endif\n)\"\n#endif\nR\"(\n#define X 1\n)\" X\nafter\n", 0, "gnu17", "R\"(\n#define X 1\n)\" X\nafter\n", false, 0, 0, 0 },
    { "raw string literals keep their splices and trigraphs", "a R\"(x\\\ny?\?=)\" b ?\?=\nL\\\nR\"(z\\\nw)\" c\n", 0, "c17", "a R\"(x\\\ny?\?=)\" b #\nLR\"(z\\\nw)\" c\n", false, 0, "-fraw-string-literals", 0 },
    { "raw string literals pasted and stringized", "#define CAT(a,b) a##b\n#define S(x) #x\nCAT(R,\"(x)\") CAT(L,R\"(y)\") CAT(u8,R\"(z)\") S(R\"(a\nb)\") S(R\"x(e\"f\\)x\") S(LR\"(\\\n)\")\n", 0, "gnu17", "R\"(x)\" LR\"(y)\" u8R\"(z)\" \"R\\\"(a\\nb)\\\"\" \"R\\\"x(e\\\"f\\\\)x\\\"\" \"LR\\\"(\\\\\\n)\\\"\"\n", false, 0, 0, 0 },
    { "raw string literals in _Pragma, #line and a line marker", "_Pragma(R\"(omp parallel for)\")\n_Pragma(LR\"x(message(\"ab\"))x\")\n#line 7 R\"(f\\g.c)\"\n__FILE__ __LINE__\n# 9 R\"(h\"i.c)\"\n__FILE__\n", 0, "gnu17", "#pragma omp parallel for\n#pragma message(\"ab\")\n\"f\\\\g.c\" 7\n\"h\\\"i.c\"\n", false, 1, 0, 0 },
    { "raw string literals in __has_warning", "#if __has_warning(R\"(-Wmisleading-indentation)\") && __has_warning(\"-Wmisleading\" R\"(-indentation)\")\nyes\n#endif\n", 0, "gnu17", "yes\n", false, 0, 0, 0 },
    { "a raw string literal isn't a header name", "#if __has_include(R\"(other.h)\")\nfound\n#else\nnot\n#endif\n", 0, "gnu17", "not\n", true, 0, 0, pp_test_files_raw_header },
    { "a raw string literal isn't an #include's header name", "#include R\"(other.h)\"\n", "x\n", "gnu17", "", true, 0, 0, 0 },
    { "raw string literals after '$' as punctuation", "#define R RR\n$R\"(a\"b)\" 1$R\"(c)\" a$LR\"(d)\" 1$e+R\"(e)\" $u8R\"(f)\" $xR\"(g)\"\n", 0, "gnu17", "$R\"(a\"b)\" 1$R\"(c)\" a$LR\"(d)\" 1$e+R\"(e)\" $u8R\"(f)\" $xR\"(g)\"\n", false, 0, "-fno-dollars-in-identifiers", 0 },
    { "raw string literals after '$' in assembly", "#define R RR\n$R\"(a\"b)\" 1$R\"(c)\" a$LR\"(d)\" $xR\"(g)\"\n", 0, "gnu17", "$R\"(a\"b)\" 1$R\"(c)\" a$LR\"(d)\" $xR\"(g)\"\n", false, 0, "-x assembler-with-cpp", 0 },
    { "line breaks in an unknown token and in a raw string literal", "R\"x\ny\" z\nw\nR\"(a\nb)\" c\nd\n", 0, "gnu17", "R\"x\ny\" z\nw\nR\"(a\nb)\" c\nd\n", true, 0, 0, 0, "# 1 \"%d/case.c\"\nR\"x\ny\" z\nw\nR\"(a\nb)\" c\n\nd\n" },
    { "invalid raw delimiter keeps its line splice", "R\"a\\\nb\" c\n", 0, "gnu17", "R\"a\\\nb\" c\n", true, 0, 0, 0,
      "# 1 \"%d/case.c\"\nR\"a\\\nb\" c\n" },
    { .name = "invalid raw delimiters at active file scope", .source = "R\"bad space\" LR\"bad)\" uR\"bad\\\" UR\"abcdefghijklmnopq\" u8R\"bad\n\"\n",
      .standard = "gnu17", .expected = "R\"bad space\" LR\"bad)\" uR\"bad\\\" UR\"abcdefghijklmnopq\" u8R\"bad\n\"\n", .fails = true, .errors = 5 },
    { .name = "unterminated raw literal", .source = "before R\"tag(body\n", .standard = "gnu17", .expected = "before R\"tag(body\n", .fails = true, .errors = 1 },
    { .name = "invalid raw prefix with phase two splice", .source = "L\\\nR\"bad space\"\n", .standard = "gnu17", .expected = "LR\"bad space\"\n", .fails = true, .errors = 1 },
    { .name = "unused invalid raw macro diagnoses once", .source = "#define BAD R\"bad space\"\nBAD BAD\n", .standard = "gnu17", .expected = "R\"bad space\" R\"bad space\"\n", .fails = true, .errors = 1 },
    { .name = "unused invalid raw macro argument diagnoses", .source = "#define DROP(x) kept\nDROP(R\"bad space\")\n", .standard = "gnu17", .expected = "kept\n", .fails = true, .errors = 1 },
    { .name = "invalid raw in a condition diagnoses after expansion", .source = "#define DROP(x) 1\n#if DROP(R\"bad space\")\nkept\n#endif\n", .standard = "gnu17", .expected = "kept\n", .fails = true, .errors = 1 },
    { .name = "skipped invalid raw literals suppress diagnostics", .source = "#if 0\nR\"bad space\"\n#define BAD LR\"bad)\"\n#endif\nkept\n", .standard = "gnu17", .expected = "kept\n" },
    { .name = "an untaken elif still diagnoses invalid raw literals", .source = "#if 1\nkept\n#elif R\"bad space\"\n#endif\n", .standard = "gnu17", .expected = "kept\n", .fails = true, .errors = 1 },
    { .name = "invalid raw syntax is ordinary text with raw disabled", .source = "R\"bad space\"\n", .standard = "gnu17", .options = "-fno-raw-string-literals", .expected = "R \"bad space\"\n" },
    { .name = "invalid raw syntax with forced C89 raw literals", .source = "R\"bad space\"\n", .standard = "c89", .options = "-fraw-string-literals", .expected = "R\"bad space\"\n", .fails = true, .errors = 1 },
    { .name = "invalid raw syntax in assembly", .source = "R\"bad space\"\n", .standard = "gnu17", .options = "-x assembler-with-cpp", .expected = "R\"bad space\"\n", .fails = true, .errors = 1 },
    // Named UCNs (#452) are refused where the lexer reads one in a token, as an invalid raw string literal's delimiter is
    // diagnosed (pp_test_named_ucns has their places), and never in a skipped group, a literal or a comment, whose text
    // clang's -E keeps as written.
    { .name = "named UCNs in skipped groups, literals and comments",
      .source = "#if 0\nint \\N{LATIN SMALL LETTER E WITH ACUTE};\n#define BAD \\N{DIGIT ZERO}\n#endif\n/* \\N{LATIN SMALL LETTER E WITH ACUTE} */ // \\N{DIGIT ZERO}\n"
                "\"\\N{LATIN SMALL LETTER E WITH ACUTE}\" '\\N{DIGIT ZERO}' kept\n",
      .standard = "gnu17", .expected = "\"\\N{LATIN SMALL LETTER E WITH ACUTE}\" '\\N{DIGIT ZERO}' kept\n" },
    { .name = "a named UCN in an identifier", .source = "int \\N{LATIN SMALL LETTER E WITH ACUTE} = 1;\nint f(void) { return \\u00e9; }\n", .standard = "gnu17",
      .expected = "", .fails = true, .errors = 1 },
    { .name = "a named UCN in a macro diagnoses once", .source = "#define E \\N{LATIN SMALL LETTER E WITH ACUTE}\nE E\n", .standard = "gnu17", .expected = "",
      .fails = true, .errors = 1 },
    { .name = "a named UCN in an unused macro argument", .source = "#define DROP(x) kept\nDROP(a\\N{GREEK SMALL LETTER ALPHA})\n", .standard = "gnu17",
      .expected = "", .fails = true, .errors = 1 },
    { .name = "a named UCN in #if", .source = "#if \\N{GREEK SMALL LETTER ALPHA} || 1\nkept\n#endif\n", .standard = "gnu17", .expected = "", .fails = true,
      .errors = 1 },
    { .name = "named UCNs a line splice splits", .source = "a\\\\\nN{GREEK SMALL LETTER ALPHA} b\\N{GREEK\\\n SMALL LETTER ALPHA} 1\\N{DIGIT ONE}\n",
      .standard = "gnu17", .expected = "", .fails = true, .errors = 3 },
    { .name = "names no character has", .source = "a\\N{greek small letter alpha} \\N{NOT A NAME}\n", .standard = "gnu17", .expected = "", .fails = true,
      .errors = 2 },
    { .name = "opaque raw-looking query with no lexical overlap", .source = "#define HI __has_include\n#if HI(<R\"bad\"x>)\nbad\n#endif\nafter\n", .standard = "gnu17", .expected = "after\n" },
    { "raw-looking direct angle header preserves following source", "#include <R\"(x>\nafter\n", 0, "gnu17", "header\nafter\n", false, 0, "-I%d", pp_test_files_opaque_raw },
    { "raw strings resume after an opaque direct header", "#include <R\"(x> R\"(a\nb)\"\nafter\n", 0, "gnu17", "header\nafter\n", false, 1, "-I%d", pp_test_files_opaque_raw },
    { "escaped greater in a raw-looking direct header", "#include <a\\>R\"(x>\nafter\n", 0, "gnu17", "escaped_header\nafter\n", false, 0, "-I%d", pp_test_files_opaque_raw },
    { "spliced raw-looking direct angle header", "#include <R\\\n\"(x>\nafter\n", 0, "gnu17", "header\nafter\n", false, 0, "-I%d", pp_test_files_opaque_raw },
    { "raw-looking warning text preserves following source", "#warning R\"(\nafter\n", 0, "gnu17", "after\n", false, 1, 0, 0 },
    { "raw-looking error text preserves following source", "#error R\"(\nafter\n", 0, "gnu17", "after\n", true, 0, 0, 0 },
    { "repeated raw-looking direct headers use one recovery", "#include <R\"(x>\n#include <R\"(x>\n#include <R\"(x>\n#include <R\"(x>\n#include <R\"(x>\n#include <R\"(x>\n#include <R\"(x>\n#include <R\"(x>\n#include <R\"(x>\nafter\n", 0, "gnu17", "header\nheader\nheader\nheader\nheader\nheader\nheader\nheader\nheader\nafter\n", false, 0, "-I%d", pp_test_files_opaque_raw },
    { "a macro-named raw header query restores later source", "#define RAW_QUERY __has_include\n#define RAW_BEFORE 11\n#if RAW_QUERY(<R\"(x>)\nhit\n#endif\n#define RAW_AFTER 22\nRAW_BEFORE RAW_AFTER\n",
      0, "gnu17", "hit 11 22", false, 0, "-I%d", pp_test_files_raw_query },
    { "successive raw header queries in one condition", "#define RAW_QUERY __has_include\n#if RAW_QUERY(<R\"(x>) && RAW_QUERY(<R\"(x>)\ntwo\n#endif\nafter\n",
      0, "gnu17", "two after", false, 0, "-I%d", pp_test_files_raw_query },
    { "a raw header query with a splice", "#define RAW_QUERY __has_include\n#if RAW_QUERY(<R\\\n\"(x>)\nspliced\n#endif\nafter\n",
      0, "gnu17", "spliced after", false, 0, "-I%d", pp_test_files_raw_query },
    { "a macro-named raw embed query restores later source", "#define RAW_QUERY __has_embed\n#if RAW_QUERY(<R\"(x>) == 1\nresource\n#endif\nafter\n",
      0, "gnu23", "resource after", false, 0, "--embed-dir=%d", pp_test_files_raw_query },
    { "a multiline raw macro argument remains opaque", "#define DROP(x) 1\n#if DROP(R\"(a\nb)\")\nkept\n#endif\nafter\n",
      0, "gnu17", "kept after", false, 0, 0, 0 },
    { "raw query private views preserve recursive macro snapshots", "#define RAW_QUERY __has_include\n#include \"raw-view.h\"\nRAW_BEFORE RAW_AFTER\n",
      0, "gnu17", "outer inner 11 22 11 22", false, 0, "-I%d", pp_test_files_raw_query },
    { "raw query private views retain once, import and guard identity", "#define RAW_QUERY __has_include\n#include \"raw-once.h\"\n#include \"raw-once.h\"\n#import \"raw-import.h\"\n#import \"raw-import.h\"\n#include \"raw-guard.h\"\n#include \"raw-guard.h\"\nafter\n",
      0, "gnu17", "once imported guarded after", false, 0, "-I%d", pp_test_files_raw_query },
    { "an excluded raw-looking direct header stays ordinary text", "#if 0\n#include <R\"(x>\n#endif\nafter\n",
      0, "gnu17", "", true, 0, "-I%d", pp_test_files_raw_query },
    { "an excluded raw-looking diagnostic stays ordinary text", "#if 0\n#warning R\"(\n#endif\nafter\n",
      0, "gnu17", "", true, 0, 0, 0 },
    { "a malformed raw-looking active diagnostic is opaque", "#warning R\"bad\nafter\n",
      0, "gnu17", "after", false, 1, 0, 0 },
    { "a malformed raw-looking excluded diagnostic stays ordinary text", "#if 0\n#warning R\"bad\n#endif\nafter\n",
      0, "gnu17", "", true, 0, 0, 0 },
    // c89 lexes neither digraphs nor "//" comments, as clang's -E has it: "%:define" is no directive, a digraph is two
    // punctuators ("<%=" is '<' and "%=", and "<::>" '<', "::" and '>'), and "//" two slashes, of which the second starts
    // a block comment before a '*'. Pasting a digraph's characters or "//" forms no token (#288).
    { "c89 without digraphs or // comments", "%:define D 1\nD <: :> <% %> %: %:%: <%= <::>\na //* c */ b\nx // y\n#define S(x) #x\nS(<:) S(a //* c */ b) S(%:%:)\n#define F(x) %:x\n#define C(a, b) a %:%: b\nF(y) C(p, q)\n#define X a // b\nX\n",
      0, "c89", "% : define D 1\nD < : : > < % % > % : % : % : < %= < :: >\na / b\nx / / y\n\"<:\" \"a / b\" \"%:%:\"\n% : y p % : % : q\na / / b\n", false, 0, 0, 0 },
    { "pasting a digraph's characters in c89", "#define P(a, b) a ## b\nP(<, :)\n", 0, "c89", "< :\n", true, 0, 0, 0 },
    { "pasting // in c89", "#define P(a, b) a ## b\nP(/, /)\n", 0, "c89", "/ /\n", true, 0, 0, 0 },
    { "pasting %: and %: in c89", "#define P(a, b) a ## b\nP(%:, %:)\n", 0, "c89", "% : % :\n", true, 0, 0, 0 },
    // C94 (iso9899:199409) is c89 with digraphs, and __STDC_VERSION__ 199409L (#288).
    { "C94's digraphs without // comments", "%:define D 1\nD __STDC_VERSION__ <%= <::>\nx // y\n#define C(a, b) a %:%: b\n#define F(x) %:x\n#define P(a, b) a ## b\nC(p, q) F(y) P(%:, %:) P(<, :)\n",
      0, "iso9899:199409", "1 199409L <% = <: :>\nx / / y\npq \"y\" %:%: <:\n", false, 0, 0, 0 },
    // The last -f[no-]digraphs wins over the mode: c89 with -fdigraphs is C94, C94 with -fno-digraphs is c89 (no
    // __STDC_VERSION__), and any other mode has no digraphs with -fno-digraphs, but its "//" comments (#288).
    { "-fdigraphs in c89", "%:define D 1\nD __STDC_VERSION__ x // y\n", 0, "c89", "1 199409L x / / y\n", false, 0, "-fno-digraphs -fdigraphs", 0 },
    { "-fno-digraphs in C94", "%:define D 1\nD __STDC_VERSION__\n", 0, "iso9899:199409", "% : define D 1\nD __STDC_VERSION__\n", false, 0, "-fdigraphs -fno-digraphs", 0 },
    { "-fno-digraphs", "%:define D 1\nD <%= <::> a // b\n", 0, "gnu17", "% : define D 1\nD < %= < :: > a\n", false, 0, "-fno-digraphs", 0 },
    { "-fno-digraphs in gnu89", "%:define D 1\nD __STDC_VERSION__ a // b\n", 0, "gnu89", "% : define D 1\nD __STDC_VERSION__ a\n", false, 0, "-fno-digraphs", 0 },
    { "short wchar in #if", "#if (L'\\0' - 1) > 0 && L'\\xffff' - 65536 > 0\nyes\n#endif\n",
      0, "c17", "yes\n", false, 0, "-fno-short-wchar -fshort-wchar", 0 },
    { "default wchar after short wchar in #if", "#if (L'\\0' - 1) < 0 && L'\\xffff' - 65536 < 0\nyes\n#endif\n",
      0, "c17", "yes\n", false, 0, "-fshort-wchar -fno-short-wchar", 0 },
    { "true in C99", "#if true\nyes\n#else\nno\n#endif\n",
      0, "c99", "no\n", false, 0, 0, 0 },
    // Where clang and GCC differ, clang's answer: a #line continued on the next line counts from the line of its # (GCC
    // gives 100), and __BASE_FILE__ in a forced include is <built-in> (GCC gives the main file).
    { "#line continued on the next line", "#line \\\n100\n__LINE__\n",
      0, "c17", "101\n", false, 0, 0, 0 },
    { "__BASE_FILE__ in a forced include", "main\n",
      "__BASE_FILE__\n", "c17", "\"<built-in>\"\nmain\n", false, 0, "-include %d/other.h", 0 },
    // #193's re-review: a directory under both -iquote and -I (#include_next goes on from the directory this inclusion
    // was found in, not the file's first), spliced string literals from macros, pending spaces in #__VA_OPT__,
    // __BASE_FILE__ after #line (the presumed root, as clang walks to it), and #line continued with its number indented
    // (clang counts from the line it lexes the number on).
    { "#include_next through -iquote and -I", "#include \"wrapper.h\"\n",
      0, "c17", "LAYER_ONE\nLAYER_ONE\nLAYER_TWO\n", false, 0, "-iquote %d/first -I %d/first -I %d/second", pp_test_files_include_next },
    { "computed header names with line splices", "#define H \"oth\\\ner.h\"\n#include H\n#if __has_include(H) && __has_include(\"oth\\\ner.h\")\nfound\n#endif\n",
      "spliced\n", "c17", "spliced\nfound\n", false, 0, 0, 0 },
    { "#__VA_OPT__ after empty arguments", "#define F(x, ...) #__VA_OPT__(a x+b)\nF(, 1)\n#define G(x, ...) #__VA_OPT__(a x+b x-c)\nG(, 1)\n#define K(x, ...) #__VA_OPT__(a x) y\nK(, 1)\n",
      0, "c23", "\"a +b\"\n\"a +b -c\"\n\"a\" y\n", false, 0, 0, 0 },
    { "__BASE_FILE__ after #line", "#line 1 \"logical.c\"\n__BASE_FILE__\n#include \"other.h\"\n",
      "__BASE_FILE__\n", "c17", "\"logical.c\"\n\"logical.c\"\n", false, 0, 0, 0 },
    { "#line continued, its number indented", "#line \\\n 100\n__LINE__\n#line \\\n200\n__LINE__\n# \\\n 400\n__LINE__\n#line \\\r\n 500\n__LINE__\n",
      0, "c17", "100\n201\n400\n500\n", false, 0, 0, 0 },
    // #193's third review: -E's line markers give the presumed positions, as clang's do: #line's numbers and names in
    // the main file and in an included one, a line that starts with a macro on the line of its name (also when the
    // invocation spans lines), the line after a _Pragma's pragma, flag 1 and 2 for each file the stack goes into and
    // back to, 3 for system headers (from -isystem, and after #pragma GCC system_header) on every marker, and names
    // escaped as clang writes them.
    { "line markers: #line in the main file and in an included one", "#line 100 \"logical.c\"\nint x;\nint y;\n#line 7\nint z;\n#include \"inc.h\"\nint w;\n",
      0, "c17", "int x; int y; int z; int in_header; int after; int w;", false, 0, 0, pp_test_files_renamed,
      "# 1 \"%d/case.c\"\n# 100 \"logical.c\"\nint x;\nint y;\n# 7 \"logical.c\"\nint z;\n# 1 \"%d/inc.h\" 1\nint in_header;\n# 50 \"renamed.h\"\nint after;\n# 9 \"logical.c\" 2\nint w;\n" },
    { "line markers: lines that start with macros, and the line after _Pragma", "#define E int a;\n#define F(x) x\nE\nE\n\nE\nF(int\nb;)\nc;\n\n\n\n\n\n\n\n\n\n\n\nE\nd;\n_Pragma(\"bar\") t\nu\n",
      0, "c17", "int a; int a; int a; int b; c; int a; d; #pragma bar\nt u", false, 0, 0, 0,
      "# 1 \"%d/case.c\"\n\n\nint a;\nint a;\n\nint a;\nint b;\n\nc;\n# 21 \"%d/case.c\"\nint a;\nd;\n#pragma bar\n# 23 \"%d/case.c\"\nt\nu\n" },
    { "line markers: the include stack, system headers and escaped names", "#include \"a.h\"\nmain_token\n#include <s.h>\n#include \"p.h\"\n#line 5 \"a\\\\b\\\"c.c\"\nescaped\n#line 7 \"\xc3\xb1.c\"\nutf8\n",
      0, "c17", "a_token b_token a_after main_token sys_first sys_far user_before user_after escaped utf8", false, 0, "-isystem %d/sys", pp_test_files_stack,
      "# 1 \"%d/case.c\"\n# 1 \"%d/a.h\" 1\na_token\n# 1 \"%d/b.h\" 1\nb_token\n# 3 \"%d/a.h\" 2\na_after\n# 2 \"%d/case.c\" 2\nmain_token\n# 1 \"%d/sys/s.h\" 1 3\nsys_first\n# 12 \"%d/sys/s.h\" 3\nsys_far\n# 4 \"%d/case.c\" 2\n# 1 \"%d/p.h\" 1\nuser_before\n# 3 \"%d/p.h\" 3\nuser_after\n# 5 \"a\\\\b\\\"c.c\" 2\nescaped\n# 7 \"\\303\\261.c\"\nutf8\n" },
    // #193's third review: GNU comma elision when a variadic macro's only argument is omitted (C17 keeps the comma),
    // the space before a stringized __VA_OPT__ (outside the string), a definition checked where it is made, the
    // signedness of UTF-16, UTF-32 and (C23) UTF-8 character constants in #if, the standard features each mode has,
    // and #line's number and file name read without their line splices, with the file name's escapes decoded (clang
    // rejects numeric ones).
    { "comma elision with an omitted sole variadic argument (GNU)", "#define F(...) call(0, ##__VA_ARGS__)\nF()\nF(1)\n#define G(x, ...) call(x, ##__VA_ARGS__)\nG(0)\nG(0,)\nG(0, 1)\n",
      0, "gnu17", "call(0)\ncall(0,1)\ncall(0)\ncall(0,)\ncall(0, 1)\n", false, 0, 0, 0 },
    { "comma elision with an omitted sole variadic argument (C17)", "#define F(...) call(0, ##__VA_ARGS__)\nF()\nF(1)\n#define G(x, ...) call(x, ##__VA_ARGS__)\nG(0)\nG(0,)\nG(0, 1)\n",
      0, "c17", "call(0,)\ncall(0,1)\ncall(0)\ncall(0,)\ncall(0, 1)\n", false, 0, 0, 0 },
    { "the space before a stringized __VA_OPT__", "#define S(x) #x\n#define E(x) S(x)\n#define F(x, ...) a x#__VA_OPT__(b)\nE(F(, 1))\n#define G(x, ...) a x#__VA_OPT__()\nE(G(, 1))\n#define H(x, ...) a x#__VA_OPT__(b c)\nE(H(, 1))\n#define K(x, ...) a x #__VA_OPT__(b)\nE(K(, 1))\nE(F(, ))\n",
      0, "c23", "\"a \\\"b\\\"\"\n\"a \\\"\\\"\"\n\"a \\\"b c\\\"\"\n\"a \\\"b\\\"\"\n\"a \\\"\\\"\"\n", false, 0, 0, 0 },
    { "an unused definition whose # names no parameter", "#define F(x) # y\nOK\n",
      0, "c17", "", true, 0, 0, 0 },
    { "UTF-16, UTF-32 and wide character constants' signedness in #if", "#if u'a' < -1\nunsigned_char16\n#else\nwrong_signed_char16\n#endif\n#if U'a' < -1\nunsigned_char32\n#endif\n#if 'a' < -1\nwrong\n#else\nsigned_char\n#endif\n#if L'a' < -1\nwrong\n#else\nsigned_wchar\n#endif\n",
      0, "gnu17", "unsigned_char16\nunsigned_char32\nsigned_char\nsigned_wchar\n", false, 0, 0, 0 },
    { "UTF-8 character constants' signedness in #if (C23)", "#if u8'a' < -1\nunsigned_char8\n#else\nwrong_signed_char8\n#endif\n",
      0, "c23", "unsigned_char8\n", false, 0, 0, 0 },
    { "standard features in C99", "__has_feature(c_alignas) __has_feature(c_alignof) __has_feature(c_atomic) __has_feature(c_generic_selections) __has_feature(c_static_assert) __has_feature(c_thread_local) __has_feature(c_fixed_enum)\n__has_extension(c_alignas) __has_extension(c_atomic) __has_extension(c_generic_selections) __has_extension(c_fixed_enum)\n",
      0, "c99", "0 0 0 0 0 0 0\n1 1 1 1\n", false, 0, 0, 0 },
    { "standard features in GNU17", "__has_feature(c_alignas) __has_feature(c_alignof) __has_feature(c_atomic) __has_feature(c_generic_selections) __has_feature(c_static_assert) __has_feature(c_thread_local) __has_feature(c_fixed_enum)\n__has_extension(c_alignas) __has_extension(c_atomic) __has_extension(c_generic_selections) __has_extension(c_fixed_enum)\n",
      0, "gnu17", "1 1 1 1 1 1 0\n1 1 1 1\n", false, 0, 0, 0 },
    { "standard features in C23", "__has_feature(c_alignas) __has_feature(c_alignof) __has_feature(c_atomic) __has_feature(c_generic_selections) __has_feature(c_static_assert) __has_feature(c_thread_local) __has_feature(c_fixed_enum)\n__has_extension(c_alignas) __has_extension(c_atomic) __has_extension(c_generic_selections) __has_extension(c_fixed_enum)\n",
      0, "c23", "1 1 1 1 1 1 1\n1 1 1 1\n", false, 0, 0, 0 },
    { "#line's operands as phase 2 leaves them", "#line 1\\\n00\n__LINE__\n#line 100 \"virtual\\\n.c\"\n__FILE__\n#line 300 \"a\\\\b.c\"\n__FILE__\n#line 400 \"\\u00f1.c\"\n__FILE__\n#line 500 \"\\n.c\"\n__FILE__\n",
      0, "c17", "101\n\"virtual.c\"\n\"a\\\\b.c\"\n\"\xc3\xb1.c\"\n\"\\n.c\"\n", false, 0, 0, 0 },
    { "#line with a numeric escape in its file name", "#line 200 \"\\x76irtual.c\"\n__FILE__\n",
      0, "c17", "", true, 0, 0, 0 },
    // Lexing by the language mode, as clang lexes: the u, U and u8 prefixes are C11's (u8 on a character constant C23's),
    // so before them each is an identifier and the literal after it, a paste that would form one isn't a token, and
    // trigraphs are replaced in the ISO modes before C23. And the directives clang rejects among a macro's arguments.
    { "literal prefixes before C11", "#define u [u]\n#define U [U]\n#define u8 [u8]\n#define L [L]\nu'a' U'a' u\"s\" U\"s\" u8\"s\" u8'a' L'a' L\"s\"\n",
      0, "c99", "[u]'a' [U]'a' [u]\"s\" [U]\"s\" [u8]\"s\" [u8]'a' L'a' L\"s\"\n", false, 0, 0, 0 },
    { "u8 character constants before C23", "#define u [u]\n#define U [U]\n#define u8 [u8]\n#define L [L]\nu'a' U'a' u\"s\" U\"s\" u8\"s\" u8'a' L'a' L\"s\"\n",
      0, "gnu17", "u'a' U'a' u\"s\" U\"s\" u8\"s\" [u8]'a' L'a' L\"s\"\n", false, 0, 0, 0 },
    { "literal prefixes in C23", "#define u [u]\n#define U [U]\n#define u8 [u8]\n#define L [L]\nu'a' U'a' u\"s\" U\"s\" u8\"s\" u8'a' L'a' L\"s\"\n",
      0, "c23", "u'a' U'a' u\"s\" U\"s\" u8\"s\" u8'a' L'a' L\"s\"\n", false, 0, 0, 0 },
    { "a paste that forms u8'a' before C23", "#define P(a, b) a ## b\nP(u8, 'a')\n",
      0, "gnu17", "u8 'a'\n", true, 0, 0, 0 },
    { "#pragma among a macro's arguments", "#define F(x) [x]\nF(a\n#pragma omp parallel\nb)\nafter\n",
      0, "c17", "[a b]\nafter\n", true, 0, 0, 0 },
    { "#include among a macro's arguments", "#define F(x) [x]\nF(a\n#include \"other.h\"\nb)\nafter\n",
      "inside\n", "c17", "[a b]\nafter\n", true, 0, 0, 0 },
    // Clang's flag for a macro's arguments is one flag, not a depth: an invocation with arguments in a directive among
    // them clears it, and #pragma and #include are taken after it, the arguments going on past the included file's end
    // (#109's seed 2106).
    // #370: FileChanged leaves a header while arguments are collected, before the expansion reaches the printer.
    // Keep the expansion's presumed line, in the file the callback selected, without changing the actual origins.
    { "line markers: an invocation crosses an included file's end", "#include \"other.h\"\nb)\nafter\n",
      "#define F(x) [x]\nF(a\n", "c17", "[a b] after", false, 0, 0, 0,
      "# 1 \"%d/case.c\"\n# 1 \"%d/other.h\" 1\n# 2 \"%d/case.c\" 2\n[a b]\nafter\n" },
    { "line markers: an invocation after output crosses an included file's end", "#include \"other.h\"\nb)\nafter\n",
      "#define F(x) [x]\nbefore F(a", "c17", "before [a b] after", false, 0, 0, 0 },
    { "line markers: argument collection exits two included files", "#include \"outer.h\"\nb)\nafter\n",
      "#define F(x) [x]\nF(a\n", "c17", "[a b] after", false, 0, 0, pp_test_files_collecting_exit },
    { "line markers: argument collection returns to an included file", "#include \"outer.h\"\nafter\n",
      "#define F(x) [x]\nF(a\n", "c17", "[a b] outer_after after", false, 0, 0, pp_test_files_collecting_return },
    { "line markers: crossed files keep builtin and presumed locations", "#line 100 \"logical.c\"\n#include \"other.h\"\n__LINE__ __FILE__)\nafter\n",
      "#define F(x) [x]\n#line 80 \"logical.h\"\nF(__LINE__ __FILE__\n", "c17", "[80 \"logical.h\" 101 \"logical.c\"] after", false, 0, 0, 0 },
    { "line markers: an empty expansion crosses an included file's end", "#include \"other.h\"\nb)\nafter\n",
      "#define F(x)\nF(a\n", "c17", "after", false, 0, 0, 0 },
    { "line markers: an empty expansion crossing an include leaves no output", "#include \"other.h\"\nb)\n",
      "#define F(x)\nF(a\n", "c17", "", false, 0, 0, 0,
      "# 1 \"%d/case.c\"\n# 1 \"%d/other.h\" 1\n# 2 \"%d/case.c\" 2\n" },
    { "line markers: a system include ends while collecting arguments", "#include <other.h>\nb)\nafter\n",
      "#define F(x) [x]\nF(a\n", "c17", "[a b] after", false, 0, "-isystem %d", 0 },
    { "line markers: a crossed invocation spans printer batches", "#include \"other.h\"\nb)\nafter\n",
      "#define F(x) x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x\nF(a\n",
      "c17", "a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b a b after", false, 0, 0, 0 },
    { "an invocation in a #if among a macro's arguments clears clang's flag", "#define F(x) [x]\n#define G() 1\nF(a\n#if G()\n#endif\n#pragma omp parallel\n#include \"other.h\"\nb)\nafter\n",
      "inside\n", "c17", "#pragma omp parallel\n[a inside b]\nafter\n", false, 0, 0, 0 },
    { "an object-like macro in a #if among a macro's arguments keeps clang's flag", "#define F(x) [x]\n#define G 1\nF(a\n#if G\n#endif\n#pragma omp parallel\nb)\nafter\n",
      0, "c17", "[a b]\nafter\n", true, 0, 0, 0 },
    { "#pragma lines among a macro's arguments after an invocation in a #if", "#define f(x) x\n#define g() 1\n#define h(y) y\nf(a\n#if g()\n#endif\nh(\n#pragma foo\n)\n#pragma bar\nb)\n",
      0, "c17", "#pragma foo\n#pragma bar\na b\n", false, 0, 0, 0 },
    { "a file twice by #include among a macro's arguments", "#define F(x) [x]\n#define G() 1\nF(a\n#if G()\n#endif\n#include \"other.h\"\n#include \"other.h\"\nb)\nafter\n",
      "inside\n", "c17", "[a inside inside b]\nafter\n", false, 0, 0, 0 },
    { "fuzzing, seed 2106 case 190642: #pragma among arguments after an invocation in a #elif", "#\tdefine ppf_190642_b(\\\n ) \n#define ppf_190642_h( )   \n ppf_190642_h       \f (   \\\n     \n#if   \\\n   ( ppf_190642_f ) \\\n!=  \\\nppf_g1\n#endif\n/**/#ifdef\\\n     \t    \t  /**/  /* c */   ppf_190642_bppf_190642_h\n#elif   \\\n  _Pragma(\"ppf_if\") ppf_190642_e\\\n\?       \f /* c */ppf_190642_b(/*\n*/ ) \\\n ppf_190642_d: ( /**/  (ppf_190642_h ) )>/* c */__has_c_attribute(gnu::unused)\\\n \?/**/ppf_190642_a/* c */:__STDC__\n#pragma          \n#if (\f 1u/**/)\n#endif\n#endif /* c */  \n     ) /**/ \n",
      0, "c23", "#pragma ppf_if\n#pragma\n", false, 0, 0, 0 },
    { "trigraphs in the ISO modes before C23", "\?\?=define X 1\nX \?\?( \?\?)\n#define S(x) #x\nS(\?\?=) \"\?\?(\"\n",
      0, "c17", "1 [ ]\n\"#\" \"[\"\n", false, 0, 0, 0 },
    { "trigraphs ignored in the GNU modes", "\?\?=define X 1\nX \?\?( \?\?)\n#define S(x) #x\nS(\?\?=) \"\?\?(\"\n",
      0, "gnu17", "\? \? =define X 1\nX \? \?( \? \?)\n\"\?\?=\" \"\?\?(\"\n", false, 0, 0, 0 },
    // #202's review: the split prefix's identifier ends before a line splice that comes before the quote, and a stringized
    // string is final (with trigraphs on, its "??=" stays), while a paste is lexed again, as clang lexes it.
    { "a line splice before the quote of a split literal", "#define u X\nu\\\n'a'\n#define U Z\nU\?\?/\n'b'\n",
      0, "c99", "X 'a' Z 'b'\n", false, 0, 0, 0 },
    { "a line splice before the quote of u8'a' before C23", "#define u8 Y\nu8\\\n'a'\nu\\\n8'c'\n",
      0, "c17", "Y 'a' Y 'c'\n", false, 0, 0, 0 },
    { "a trigraph that a stringized argument spells, and one a paste forms", "#define S(x) #x\n#define P(a, b) a ## b\n#define XP(a, b) P(a, b)\nS(\?\?\\\n=)\nXP(L, S(\?\?\\\n=))\n",
      0, "c17", "\"\?\?=\"\nL\"#\"\n", false, 0, 0, 0 },
    { "#include of a missing absolute path", "#include \"/buster-missing-directory/absent.h\"\nafter\n", 0, "c17", "", true },
    { "-include of a missing absolute path", "x\n", 0, "c17", "", true, 0, "-include /buster-missing-directory/absent.h", 0 },
    // #109's differential fuzzing against clang: a case for each difference it found (minimized) and for each that
    // probing around one found, with clang's -P text, which test-preprocessor --clang checks again.
    { "-undef", "__STDC__ __STDC_VERSION__ __STDC_HOSTED__ __STDC_UTF_16__ __STDC_UTF_32__\n__STDC_EMBED_NOT_FOUND__ __STDC_EMBED_FOUND__ __STDC_EMBED_EMPTY__\n__x86_64__ __clang__ __BUSTER__ __GNUC__ __CHAR_BIT__\n", 0, "c23", "1 202311L 1 1 1\n0 1 2\n__x86_64__ __clang__ __BUSTER__ __GNUC__ __CHAR_BIT__\n", false, 0, "-undef" },
    // Assembly's language macro stays with -undef, as the standard ones do: a header shared with C keeps its C apart.
    { "-undef in assembly", "#ifdef __ASSEMBLER__\nassembly\n#else\nc\n#endif\n__STDC_VERSION__ __STDC_HOSTED__ __x86_64__\n", 0, "c17", "assembly\n201710L 1 __x86_64__\n", false, 0,
      "-undef -x assembler-with-cpp" },
    { "feature answers' spelling", "__has_c_attribute(nodiscard) __has_c_attribute(deprecated) __has_c_attribute(fallthrough) __has_c_attribute(maybe_unused)\n__has_c_attribute(gnu::packed) __has_attribute(cold) __has_attribute(__nonnull__) __has_builtin(__builtin_expect) __has_feature(c_atomic) __has_extension(c_atomic)\n__has_cpp_attribute(nodiscard) __has_declspec_attribute(dllexport)\n#if __has_embed(\"/dev/null\") == 2\nempty\n#endif\n", 0, "c23", "202003L 201904L 201910L 202106L\n1 1 1 1 1 1\n__has_cpp_attribute(nodiscard) 0\nempty\n", false, 0 },
    { "__has_warning", "__has_warning(\"-Wall\") __has_warning(\"-Wunused-macros\") __has_warning(\"-Wppf-none\") __has_warning(\"-W\" \"shadow\") __has_warning(\"-Rpass\")\n", 0, "c17", "1 1 0 1 0\n", false, 0 },
    { "__VA_OPT__ without variadic parameters", "#define F(a) [__VA_OPT__(x a) a]\nF(1)\n", 0, "c23", "\n[ 1]\n", false, 1 },
    { "a stringized trailing backslash", "#define S(x) #x\nS(\\) S(a \\) S(\\\\)\n", 0, "c17", "\n\"\" \"a \" \"\\\\\"\n", false, 2 },
    { "an unterminated comment", "a\n/* b\nc\n", 0, "c17", "a\n", true, 0 },
    { "a warning's comment text preserves following source", "#warning /* never closed\ntail\n", 0, "c17", "tail\n", false, 1 },
    { "an error's comment text preserves following source", "#error /* never closed\ntail\n", 0, "c17", "tail\n", true, 0 },
    { "a warning's quote is text", "#warning '\ntail\n", 0, "c17", "tail\n", false, 1 },
    { "a warning's comment ends at its logical newline", "#warning /*\ninside\n*/\ntail\n", 0, "c17", "inside\n*/\ntail\n", false, 1 },
    { "a warning's spliced name and text", "#war\\\nning /* op\\\nen\ntail\n", 0, "c17", "tail\n", false, 1 },
    { "a warning in a repeated include", "#include \"other.h\"\n#include \"other.h\"\n", "#warning /* never closed\ntail\n", "c17", "tail\ntail\n", false, 2 },
    { "a warning preserves macro snapshots", "#define BEFORE(x) [x]\n#warning /* never closed\n#define AFTER(y) {y}\nBEFORE(AFTER(7))\n", 0, "c17", "[{7}]\n", false, 1 },
    { "a skipped warning is still lexed raw", "#if 0\n#warning /* never closed\n#endif\ntail\n", 0, "c17", "", true, 0 },
    { "a skipped error is still lexed raw", "#if 0\n#error /* never closed\n#endif\ntail\n", 0, "c17", "", true, 0 },
    { "a comma in #if", "#if 1, 2\nyes\n#endif\n#if (1, 2)\nparenthesized\n#endif\n#if 0 ? 1, 2 : 3\nconditional\n#endif\n", 0, "c17", "", true, 0 },
    { "digit separators before C23", "#define N 1'000\nx N\n", 0, "c17", "\nx 1'000\n", false, 0 },
    { "digit separators in C23", "#define N 1'000\nx N y\n#if 1'0 == 10\nten\n#endif\n", 0, "c23", "\nx 1'000 y\nten\n", false, 0 },
    { "target queries", "__is_target_arch(x86_64) __is_target_arch(amd64) __is_target_arch(i386) __is_target_vendor(pc) __is_target_vendor(apple)\n__is_target_os(linux) __is_target_os(windows) __is_target_environment(gnu) __is_target_environment(msvc)\n__is_target_variant_os(linux) __is_target_variant_environment(gnu)\n", 0, "c17", "1 1 0 1 0\n1 0 1 0\n0 0\n", false, 0 },
    { "UCNs pasted and stringized", "#define P(a, b) a ## b\n#define S(x) #x\n#define T(x) S(x)\nT(P(caf\\u00e9, x) P(y, z))\nT(P(caf\\u00e9, x)P(y, z))\nP(caf\\u00e9, x)\n", 0, "c17", "\"caf\\u00e9x yz\"\n\"caf\\u00e9xyz\"\ncaf\xC3""\xA9""x\n", false, 0 },
    { "UCNs in pragma lines", "#pragma ppf caf\\u00e9 x\\u0301 caf\xC3""\xA9""\n_Pragma(\"ppf caf\\\\u00e9\")\n#define M caf\\u00e9\n#pragma omp M\n#pragma STDC FP_CONTRACT x\\u0301\n", 0, "c17", "#pragma ppf caf\\u00e9 x\\u0301 caf\xC3""\xA9""\n#pragma ppf caf\\u00e9\n#pragma omp caf\\u00e9\n#pragma STDC FP_CONTRACT x\\u0301\n", false, 0 },
    { "_Pragma in an expanded argument", "#define S(x) #x\n#define T(x) S(x) [x]\nT(_Pragma(\"ppf one\") a)\n#define P(a, b) a ## b\n#define Q(x, y) P(x, y) [x]\nQ(_Pragma(\"ppf two\") l, r)\n#define PR _Pragma(\"ppf three\")\nT(PR b)\n#define D(x) x x\nD(_Pragma(\"ppf four\"))\n", 0, "c17", "\"_Pragma(\\\"ppf one\\\") a\" [\n#pragma ppf one\n a]\n#pragma ppf two\n lr [\n#pragma ppf two\n l]\n\"_Pragma(\\\"ppf three\\\") b\" [\n#pragma ppf three\n b]\n#pragma ppf four\n#pragma ppf four\n", false, 0 },
    { "a _Pragma's unterminated comment", "_Pragma(\"ppf /* x\")\ny\n", 0, "c17", "#pragma ppf\ny\n", true, 0 },
    { "pasting a comment's start", "#define P(a, b) a ## b\nP(/, *) x\n", 0, "c17", "\n/ * x\n", true, 0 },
    { "arguments expanded in order of use", "#define F(a, b) b a\nF(__COUNTER__, __COUNTER__)\n#define G(a, ...) __VA_OPT__(x) a\nG(__COUNTER__, __COUNTER__)\n#define H(a, b) #a b a\nH(__COUNTER__, __COUNTER__)\n#define K(a, ...) [__VA_OPT__(a)]\nK(__has_include)\nK(_Pragma(\"ppf\"))\n", 0, "c23", "\n0 1\nx 3\n\"__COUNTER__\" 4 5\n[]\n[]\n", false, 0 },
    { "#pragma message's forms", "#pragma message(\"a\\x41\\n\")\n#pragma message \"b\"\n#pragma message(\"c\" \"d\")\n#pragma GCC warning \"e\"\n#pragma GCC warning (\"f\")\n#define MSG \"g\"\n#pragma message(MSG)\n_Pragma(\"message(\\\"h\\\")\")\n", 0, "c17", "#pragma message(\"aA\\012\")\n#pragma message(\"b\")\n#pragma message(\"cd\")\n#pragma GCC warning \"e\"\n#pragma GCC warning \"f\"\n#pragma message(\"g\")\n#pragma message(\"h\")\n", false, 7 },
    { "#pragma GCC error", "#pragma GCC error \"stop\"\nx\n", 0, "c17", "#pragma GCC error \"stop\"\nx\n", true, 0 },
    { "diagnostic pragmas", "#pragma clang diagnostic push\n#pragma GCC diagnostic push\n#pragma clang diagnostic ignored \"-Wunused-macros\"\n#pragma GCC diagnostic warning \"-W\" \"shadow\"\n#pragma clang diagnostic ignored \"-Wppf-none\"\n#pragma clang diagnostic remark \"-Wall\"\n#pragma clang diagnostic ignored \"-Rpass\"\n#pragma clang diagnostic ignored \"-Weverything\"\n#pragma clang diagnostic pop\n#pragma clang diagnostic pop\n#pragma clang diagnostic pop\n#pragma clang diagnostic\n#pragma GCC diagnostic ignored\n", 0, "c17", "#pragma clang diagnostic push\n#pragma GCC diagnostic push\n#pragma clang diagnostic ignored \"-Wunused-macros\"\n#pragma GCC diagnostic warning \"-Wshadow\"\n#pragma clang diagnostic ignored \"-Rpass\"\n#pragma clang diagnostic ignored \"-Weverything\"\n#pragma clang diagnostic pop\n#pragma clang diagnostic pop\n", true, 4 },
    { "omp pragmas expand", "#define N 4\n#define PAR parallel\n#pragma omp PAR num_threads(N)\n_Pragma(\"omp for N\")\n#pragma ppf N\n#pragma STDC FP_CONTRACT N\n", 0, "c17", "#pragma omp parallel num_threads(4)\n#pragma omp for 4\n#pragma ppf N\n#pragma STDC FP_CONTRACT N\n", false, 0 },
    { "silent pragmas", "#pragma mark - x\n#pragma region a\n#pragma endregion a\n#pragma clang max_tokens_here 1\n#pragma once\n#pragma GCC system_header\nafter\n", 0, "c17", "#pragma clang max_tokens_here 1\nafter\n", false, 2 },
    { "push_macro and pop_macro", "#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\nX\n#pragma pop_macro(\"X\")\nX\n#pragma pop_macro(\"X\")\nX\n#pragma push_macro(\"Y\") tail\n", 0, "c17", "2\n1\n1\n", false, 0 },
    { "push_macro malformed", "#pragma push_macro(X)\n", 0, "c17", "\n", true, 0 },
    { "GCC poison", "#pragma GCC poison p q\nx\n#ifdef p\n#endif\n", 0, "c17", "\nx\n", true, 0 },
    { "GCC poison used", "#pragma GCC poison p\np\n", 0, "c17", "\np\n", true, 0 },
    { "GCC dependency", "#pragma GCC dependency \"other.h\"\nx\n", "int other;\n", "c17", "\nx\n", false, 0 },
    { "GCC dependency missing", "#pragma GCC dependency \"ppf_missing.h\"\nx\n", 0, "c17", "", true, 0 },
    { "assume_nonnull regions", "#pragma clang assume_nonnull begin\nx\n#pragma clang assume_nonnull end\n#pragma clang arc_cf_code_audited begin\n#pragma clang arc_cf_code_audited end\n#pragma clang unsafe_buffer_usage begin\n#pragma clang unsafe_buffer_usage end\n", 0, "c17", "#pragma clang assume_nonnull begin\nx\n#pragma clang assume_nonnull end\n", false, 0 },
    { "an assume_nonnull region left open", "#pragma clang assume_nonnull begin\nx\n", 0, "c17", "#pragma clang assume_nonnull begin\nx\n", true, 0 },
    { "macro notes", "#define M 1\n#pragma clang deprecated(M)\n#pragma clang deprecated(M, \"use N\")\n#pragma clang final(M)\n#pragma clang restrict_expansion(M)\nM\n", 0, "c17", "1\n", false, 0 },
    { "macro notes malformed", "#pragma clang deprecated(ppf_undefined)\n", 0, "c17", "\n", true, 0 },
    { "#ident and #sccs", "#ident \"one\"\n#sccs \"two\"\n#define ID \"three\"\n#ident ID\n#ident L\"four\" tail\n", 0, "c17", "#ident \"one\"\n#ident \"two\"\n#ident \"three\"\n#ident L\"four\"\n", false, 1 },
    { "#ident malformed", "#ident x\n", 0, "c17", "\n", true, 0 },
    { "__has_embed", "#if __has_embed(\"/dev/null\") == 2 && __has_embed(\"ppf_missing\") == 0 && __has_embed(<ppf_missing>) == 0\nok\n#endif\n#if __has_embed(\"/dev/null\" limit(0)) == 2 && __has_embed(\"/dev/null\" prefix(x) suffix(y) if_empty(z)) == 2 && __has_embed(\"/dev/null\" ppf::unknown) == 0\nparameters\n#endif\n#if __has_embed(\"other.h\" clang::offset(1) limit(2)) == 1\nfound\n#endif\n", "int other;\n", "c23", "\nok\nparameters\nfound\n", false, 0 },
    { "__has_embed outside #if", "__has_embed(\"/dev/null\")\n", 0, "c23", "", true, 0 },
    { "UCN identifiers stringized", "#define S(x) #x\n#define T(x) S(x)\n#define P(a, b) a ## b\nT(P(\\u00e9, \\u00e9) P(\\U000000E9x, y))\n", 0, "c17", "\"\\u00e9\\u00e9 \\U000000E9xy\"\n", false, 0 },
    { "#pragma GCC poison's uses", "#define BEFORE p\n#pragma GCC poison p\nBEFORE\n#if 0\np\n#ifdef p\n#endif\n#endif\n#define P(a, b) a ## b\n#pragma GCC poison p\n", 0, "c17", "p\n", false, 0 },
    { "a poisoned name read from the file", "#pragma GCC poison p\np\n", 0, "c17", "\np\n", true, 0 },
    { "a poisoned name in #ifdef", "#pragma GCC poison p\n#ifdef p\n#endif\n", 0, "c17", "\n", true, 0 },
    { "a poisoned name among arguments", "#define F(x) 1\n#pragma GCC poison p\nF(p)\n", 0, "c17", "1\n", true, 0 },
    { "a poisoned name from a paste", "#define P(a, b) a ## b\n#pragma GCC poison pq\nP(p, q)\n", 0, "c17", "pq\n", true, 0 },
    { "a poisoned name in _Pragma's text", "#pragma GCC poison p\n_Pragma(\"ppf p\")\n", 0, "c17", "\n#pragma ppf p\n", true, 0 },
    { "poisoning a macro", "#define p 1\n#pragma GCC poison p\n#if 0\np\n#endif\n#define Q p\n", 0, "c17", "\n", true, 1 },
    { "poisoning defined", "#pragma GCC poison defined\n", 0, "c17", "\n", false, 0 },
    { "two ## in a row", "#define A x ## ## y\nA\n", 0, "c17", "\nx ## y\n", true, 0 },
    { "two ## and an empty argument", "#define F(y) x ## ## y z\nF()\n#define G(y, w) x ## ## y w\nG(,q)\n#define H(x, y) x ## ## y\nH(,1)\n", 0, "c17", "\nxz\nxq\n##1\n", false, 0 },
    { "GNU's comma paste after an argument", "#define G(...) __VA_ARGS__ ## __VA_ARGS__\nG(,)\n#define D(c, e...) e ## e\nD( , , )\n", 0, "c17", "\n,,\n, ,\n", false, 0 },
    { "#line with digit separators", "#line 1'000\n__LINE__\n", 0, "c23", "1000\n", false, 0 },
    { "line marker flags", "# 3 \"h\" 3 4\n__LINE__\n# 7 \"g\" 3\n__LINE__\n# 9 \"f\"\n__LINE__\n", 0, "c17", "3\n7\n9\n", false, 0 },
    { "a line marker's flag 2 without 1", "# 5 \"f\" 2\n", 0, "c17", "\n", true, 0 },
    { "a line marker's invalid flags", "# 5 \"f\" 1 2\n", 0, "c17", "\n", true, 0 },
    { "#include with extra tokens", "#include \"other.h\" y\n#define H \"other.h\"\n#include H z\nx\n", "int other;\n", "c17", "int other;\n\nint other;\nx\n", false, 2 },
    { "extra tokens after directives", "#define X\n#undef X y\n#if 0\n#elifdef X y\n#else x\n#endif x\n#if 1\n#else x\n#endif\n", 0, "c23", "\n", false, 5 },
    { "#elifdef without a name", "#if 0\n#elifdef\n#endif\n", 0, "c23", "\n", true, 0 },
    { "#ifdef defined", "#ifdef defined\nyes\n#else\nno\n#endif\n", 0, "c17", "no\n", false, 0 },
    { "defined from macros in #if", "#define ID(x) x\n#if ID(defined Y)\nyes\n#else\nno\n#endif\n#define D defined\n#define X\n#if D X\nyes\n#endif\n#define DX defined(X)\n#if DX\nyes\n#endif\n", 0, "c17", "no\nyes\nyes\n", false, 0 },
    { "defined from an argument that expands", "#define ID(x) x\n#define X\n#if ID(defined X)\n#endif\n", 0, "c17", "\n", true, 0 },
    { "an inactive __VA_OPT__'s spacing", "#define S(x) #x\n#define T(x) S(x)\n#define D(c, ...) #__VA_OPT__( c)\nT((D(x)))\n#define E(c, ...) [__VA_OPT__( c)]\nT(E(q))\n", 0, "c23", "\"(\\\"\\\" )\"\n\"[ ]\"\n", false, 0 },
    { "an empty __VA_OPT__ expands nothing", "#define H(d, a, ...) __VA_OPT__()\nH(,,H())\n", 0, "c23", "\n", false, 0 },
    { "__VA_ARGS__ as a parameter", "#define F(__VA_ARGS__) [__VA_ARGS__]\nF(1)\n#define G(__VA_ARGS__, ...) [__VA_ARGS__]\nG(1, 2)\n", 0, "c23", "\n[1]\n[1]\n", false, 0 },
    { "#__VA_ARGS__ in a named variadic macro", "#define F(x...) #__VA_ARGS__\n", 0, "c17", "\n", true, 0 },
    { "right shifts in #if", "#if (~0u >> 97) == 1 && (8 >> 4294967297) == 4 && (-8 >> 64) == -1 && (4 >> 0x100000000) == 4 && (1 << 64) == 0\nok\n#endif\n", 0, "c23", "\nok\n", false, 0 },
    { "push_macro allows a redefinition", "#define X 1\n#pragma push_macro(\"X\")\n#define X 2\nX\n#pragma pop_macro(\"X\")\nX\n", 0, "c17", "2\n1\n", false, 0 },
    { "#pragma once with extra tokens", "#pragma once x\n", 0, "c17", "\n", false, 2 },
    // An attribute query reads its operand expanded, a token at a time, to the ')' that closes it, wherever that comes
    // from; and its scope (for __has_c_attribute) after a name it reads as it is.
    { "an attribute query's operand read expanded", "#define R ) + 1\n#define D noreturn )\n__has_attribute(x R\n__has_attribute(D\n#define G gnu::\n__has_c_attribute(G noreturn)\n#define E\n__has_attribute(x E)\n#if __has_attribute(D\nyes\n#endif\n#define F(a) a\nF(__has_attribute(x R))\n",
      0, "c23", "\n0 + 1\n1\n1\n0\nyes\n0 + 1 )\n", false, 0 },
    { "an attribute query's operand across a directive", "__has_attribute(x\n#define Y 1\n) Y\n#define Z(a) a\n__has_attribute(\n#if 1\nnoreturn\n#endif\n) Z(1)\n", 0, "c17", "0 1\n1 1\n", false, 0 },
    { "an attribute query's operand nested", "#define RR ) )\n__has_attribute(__has_attribute(x RR\n", 0, "c17", "", true, 0 },
    { "a scope only in __has_c_attribute", "__has_attribute(gnu::noreturn)\n", 0, "c23", "", true, 0 },
    { "a macro after __has_c_attribute's name", "#define E\n__has_c_attribute(x E)\n", 0, "c23", "", true, 0 },
    { "an attribute query's operand unterminated", "__has_attribute(x\n", 0, "c17", "", true, 0 },
    { "a scoped attribute query's operand at the end of the file", "__has_c_attribute(x", 0, "c23", "", true, 0 },
    // A _Pragma in a directive's line runs, as clang reads the line: its pragma line comes first.
    { "_Pragma in directives' lines", "#if _Pragma(\"ppf a\") 1\nyes\n#endif\n#line _Pragma(\"ppf b\") 5\n__LINE__\n#pragma message(_Pragma(\"ppf c\") \"s\")\n__has_attribute(_Pragma(\"ppf d\") x) after\n",
      0, "c17", "#pragma ppf a\nyes\n#pragma ppf b\n5\n#pragma ppf c\n#pragma message(\"s\")\n#pragma ppf d\n0 after\n", false, 1 },
    // Spaces in a substitution are clang's NextTokGetsSpace: none after ##, and __VA_OPT__'s tokens and ')' give the next
    // token one even when the __VA_OPT__ gives nothing.
    { "spaces around __VA_OPT__", "#define S(x) #x\n#define T(x) S(x)\n#define B(...) >= ## __VA_OPT__( )( )\nT(B())\n#define C(...) >= ## __VA_OPT__(x)( )\nT(C())\n#define D(...) x __VA_OPT__(a )y\nT(D(1))\n#define E(...) x #__VA_OPT__()y\nT(E())\n#define F(...) x ## __VA_OPT__()y\nT(F())\n",
      0, "c23", "\n\">= ( )\"\n\">=( )\"\n\"x a y\"\n\"x \\\"\\\" y\"\n\"xy\"\n", false, 0 },
    { "GNU's comma paste's spaces and placemarkers", "#define S(...) #__VA_ARGS__\n#define T(...) S(__VA_ARGS__)\n#define f(x, ...) [x , ## __VA_ARGS__]\nT(f(a,b))\nT(f(a, b))\n#define g(p, q, ...) [p ,q ## __VA_ARGS__]\nT(g(a,))\n#define h(p, ...) [, __VA_OPT__(__VA_ARGS__ ## a) ## __VA_ARGS__ ## b]\nT(h(u))\n#define X(p, ...) [p ## , ## __VA_ARGS__]\nT(X(a))\n",
      0, "c17", "\n\"[a ,b]\"\n\"[a , b]\"\n\"[a]\"\n\"[, b]\"\n\"[a]\"\n", false, 0 },
    { "GNU's comma paste with ## after __VA_ARGS__", "#define e(d, ...) , ## __VA_ARGS__ ## )\ne( )\n", 0, "c23", "", true, 0 },
    // GNU's comma before __VA_OPT__ whose variadic argument follows empty arguments pasted onto it: clang drops their
    // placemarkers with their ##, and the comma stays apart (seed 54's case 566182, C23).
    { "GNU's comma before __VA_OPT__ past empty arguments", "#define c(d, e...) e ## __VA_OPT__( d ## e)\nc(,,)\n#define h(d, g, ...) , ## __VA_OPT__(d ## g ## __VA_ARGS__)\nh(,,y)\n",
      0, "c23", "\n,,\n,y\n", false, 0 },
    // There the variadic argument keeps its first token's own space, which a string shows (seed 70's case 208758, C17).
    { "GNU's comma before __VA_OPT__ past empty arguments, stringized", "#define vs(...) #__VA_ARGS__\n#define vt(...) vs(__VA_ARGS__)\n#define vm(p, q, ...) __VA_OPT__(,) ## __VA_OPT__( q ## __VA_ARGS__ )\nvt(vm( ,,u ))\nvt(vm( ,, u ))\n",
      0, "c17", "\n\",u\"\n\", u\"\n", false, 0 },
    // The campaigns' differences (docs/preprocessor-fuzz.md): a function-like macro without parameters substitutes
    // nothing, so its __VA_OPT__ and a # before it stay tokens (TokenLexer::Init).
    { "__VA_OPT__ in a macro without parameters", "#define Z() a ## __VA_OPT__(b) # __VA_OPT__(c)\nZ()\n#define Y() __VA_OPT__ ( )\nY()\n", 0, "c23", "\na__VA_OPT__(b) # __VA_OPT__(c)\n__VA_OPT__ ( )\n", false, 3 },
    // A token right after a line splice starts at the splice for clang's lexer: an expansion's ')' or name there gives
    // __LINE__ the splice's line, and so does a #line's number.
    { "__LINE__ at a ')' that starts at a line splice", "#define L() __LINE__\nL(\n\\\n)\n#define M __LINE__\nx\n\\\nM\n", 0, "c17", "\n3\nx\n7\n", false, 0 },
    { "#line's number after a _Pragma and an empty macro", "#lin\\\ne _Pragma(\"ppf_lin\\\ne\") 010\n__LINE__\n#define E\n#line E \\\n 30\n__LINE__\n#line E \\\n40\n__LINE__\n",
      0, "c17", "\n#pragma ppf_line\n10\n30\n41\n", false, 1 },
    { "__LINE__ at a ')' that starts at a trigraph's line splice", "#define L() __LINE__\nL(\n?\?/\n)\n", 0, "c17", "\n3\n", false, 0 },
    // A line marker's tokens after its digits are read expanded (Preprocessor::HandleDigitDirective's Lex).
    { "a line marker's name and flags from macros", "#define F \"named.c\"\n# 20 F\n__FILE__ __LINE__\n#define E\n# 30 E\n__LINE__\n", 0, "c17", "\n\"named.c\" 20\n30\n", false, 0 },
    // A _Pragma's text gets a newline after it (prepare_PragmaString), which splices away a `\` that ends it.
    { "a _Pragma's text that ends in a backslash", "#define P(x) _Pragma(x)\nP(\"\\\\\")\nx\n", 0, "c17", "\n#pragma\nx\n", false, 0 },
    // Character constants in #if as clang's CharLiteralParser reads them.
    { "character constants' values", "#if 'ab' == 24930 && '\\xff\\x01' == 65281 && '\\377' == -1 && L'\\xffffffff' == -1 && u'\\xffff' == 65535 && U'\\U0010FFFF' == 0x10FFFF && '\\e' == 27 && '\\q' == 'q'\nok\n#endif\n",
      0, "c17", "\nok\n", false, 3 },
    { "a character constant's UCN past ASCII", "#if '\\u00e9'\n#endif\n", 0, "c17", "", true, 0 },
    { "UCNs of basic characters in C23 character constants", "#if '\\u0041' == 65 && u8'\\u0024' == 36\nyes\n#endif\n", 0, "c23", "\nyes\n", false, 0 },
    { "a UCN of a basic character before C23", "#if '\\u0041' == 65\n#endif\n", 0, "c17", "", true, 0 },
    { "several characters in a wide character constant", "#if L'ab'\n#endif\n", 0, "c17", "", true, 0 },
    { "an octal escape out of range", "#if '\\400' == 0\nzero\n#endif\n", 0, "c17", "\nzero\n", true, 0 },
    // _Pragma reads its '(', string literal and ')' expanded, a token at a time (Preprocessor::Handle_Pragma).
    { "_Pragma's operand read expanded", "#define A(c) ( #c\n_Pragma A(ppf a) )\n#define P (\"ppf p\")\n_Pragma P after\n#define S \"ppf s\"\n_Pragma(S)\n",
      0, "c17", "\n#pragma ppf a\n#pragma ppf p\nafter\n#pragma ppf s\n", false, 0 },
    { "_Pragma without its operand", "_Pragma x y\n", 0, "c17", "", true, 0 },
    // A __VA_OPT__'s content pastes left to right with a ## before it, as clang's TokenLexer does: (.5 e) .5, and GNU's
    // comma before `__VA_ARGS__ ##` in it (VaArgsPseudoPaste).
    { "__VA_OPT__'s content pasted left to right", "#define C(...) .5 ## __VA_OPT__(e ## .5)\nC(1)\n#define V(...) __VA_OPT__(, ) ## __VA_OPT__(__VA_ARGS__ ## __VA_ARGS__)\nV(u)\n",
      0, "c23", "\n.5e.5\n,uu\n", false, 0 },
    // A prefix the mode doesn't have before an empty character constant is an identifier before C23 (`u8` and `''`), so
    // pasting them forms two tokens there, and one in C23.
    { "a prefix pasted onto an empty character constant before C23", "#define P(a, b) a ## b\nP(u8,'')\n", 0, "c17", "", true, 0 },
    { "a prefix pasted onto an empty character constant in C23", "#define P(a, b) a ## b\nP(u8,'')\n", 0, "c23", "\nu8''\n", false, 0 },
    // Among a macro's arguments clang's macro expansion is off, and only #if and #elif turn it back on: #line's and
    // #ident's operands stay as written.
    { "#line among a macro's arguments, unexpanded", "#define F(x) [x]\n#define N 10\nF(\n#line N\n)\n", 0, "c17", "", true, 0 },
    { "#line among a macro's arguments", "#define F(x) [x]\nF(a\n#line 20\n__LINE__)\n__LINE__\n", 0, "c17", "\n[a 20]\n21\n", false, 0 },
    { "#ident among a macro's arguments, unexpanded", "#define F(x) [x]\n#define ID \"s\"\nF(\n#ident ID\n)\n#ident ID\n", 0, "c17", "\n[]\n#ident \"s\"\n", true, 0 },
    // After a _Pragma that prints nothing, clang's lexer starts a line, which its printer moves to when the next token
    // is on another one.
    { "the token after a _Pragma that prints nothing", "#define h(x) _Pragma(x)\na h(\"once\") \\\n    b\n#define g(x) _Pragma(x) y\nc g(\"once\") \\\nd\n",
      0, "c17", "\na\nb\nc y d\n", false, 2 },
    // A name of a macro whose expansion an argument is read from is painted there: the expansion ends at the ')' after
    // it, before the argument expands (Preprocessor::ReadMacroCallArgumentList).
    { "a disabled macro's name among its arguments", "#define e a ( e\n#define a(...) __VA_ARGS__\ne )\n#define f g ( f\n#define g(x) [x]\nf )\n", 0, "c17", "\ne\n[f]\n", false, 0 },
    // A token that started a line among a macro's arguments (but the first) starts one in clang's printer too, which
    // moves to the expansion's line when that is another one: the name's, after a line splice.
    { "line markers: a line among a macro's arguments", "#define h(...) __VA_ARGS__\nL\\\n h(\nU\n\"#\")\nx\\\n  h(7\n  y\nz)\n", 0, "c17", "L U \"#\" x 7 y z", false, 0, 0, 0,
      "# 1 \"%d/case.c\"\n\nL U\n\"#\"\n\n\nx 7\ny z\n" },
    // A paste's result takes its left operand's line start among the arguments, as clang's TokenLexer::pasteTokens
    // gives it the left one's flags, so the printer moves to the expansion's line at the result, not at the token
    // after it (seed 14's case 452, C23).
    { "line markers: a paste of a line among a macro's arguments", "#define E(a) X a ## a .\nx \\\ny \\\nE(f\nz)\n", 0, "c17", "x y X f zf z .", false, 0, 0, 0,
      "# 1 \"%d/case.c\"\n\nx y X f\nzf z .\n" },
    // Directives that read only part of their line expanded, and the rest as it is (Preprocessor::CheckEndOfDirective):
    // #ident its string, #line its number, its name and one token more, a computed #include its header name and one token
    // more, and push_macro and pop_macro their '(', string and ')'. What comes after never expands.
    { "#ident's string read expanded, and the rest as it is", "#define c(x) x\n#define d(e) e\n#define h d(\"s\") c(\n#ident h\n#ident \"t\" _Pragma(\"message(\\\"m\\\")\")\n#ident \"u\" __COUNTER__\n__COUNTER__\n",
      0, "c17", "\n#ident \"s\"\n#ident \"t\"\n#ident \"u\"\n0\n", false, 3 },
    { "#line's operands read expanded, and the rest as it is", "#define c(x) x\n#line 10 \"f.c\" x c(\n__LINE__\n#line 20 \"g.c\" x __COUNTER__\n__COUNTER__ __LINE__\n", 0, "c17", "\n10\n0 20\n", false, 2 },
    { "a computed #include's name read expanded, and the rest as it is", "#define c(x) x\n#define H \"other.h\"\n#include H x c(\nafter\n", "in_other\n", "c17", "\nin_other\nafter\n", false, 1 },
    { "push_macro's and pop_macro's operands read expanded, and the rest as it is", "#define c(x) x\n#define m 1\n#pragma push_macro(\"m\") x c(\n#undef m\n#define m 2\n#pragma pop_macro(\"m\") y c(\nm\n",
      0, "c17", "\n1\n", false, 0 },
    // A pragma line that a directive among a macro's arguments gives while a _Pragma waits for its '(' stays before the
    // expansion, where the _Pragma has read it (seed 45's case 610823 crashed reading past it).
    { "a pragma line among a macro's arguments while a _Pragma waits", "_Pragma\n#define f(a, b)\nf(a\n#if _Pragma(\"ppf_if\") 1\n#endif\n,b)\nafter\n", 0, "c17",
      "\n#pragma ppf_if\nafter\n", true, 0 },
    {
        .name = "builtin and feature names are raw, attribute names may expand",
        .source = "#define BUILTIN_NAME __builtin_expect\n#define FEATURE_NAME c_atomic\n#define ATTRIBUTE_NAME __const__\n"
                  "#if __has_builtin(__is_target_arch) && __has_builtin(__is_target_os)\nbuiltin_names\n#endif\n"
                  "#if __has_builtin(BUILTIN_NAME) || __has_feature(FEATURE_NAME) || __has_extension(FEATURE_NAME)\nwrong_expansion\n#else\nraw_names\n#endif\n"
                  "#if __has_attribute(ATTRIBUTE_NAME) && __is_target_arch(x86_64) && __is_target_os(linux)\nattribute_and_target\n#endif\n",
        .standard = "gnu17",
        .expected = "builtin_names raw_names attribute_and_target",
    },
    {
        .name = "__has_constexpr_builtin's name is raw, a macro's too",
        .source = "#define CLZ __builtin_clz\n__has_constexpr_builtin(CLZ)\n__has_constexpr_builtin(__builtin_clz)\n#define __builtin_clz 0\n"
                  "__has_constexpr_builtin(__builtin_clz)\n",
        .standard = "gnu17",
        .expected = "0 1 1",
    },
    {
        .name = "the oracle recognizes ext_vector_type with either attribute spelling",
        .source = "#if __has_attribute(ext_vector_type) && __has_attribute(__ext_vector_type__)\nvector_attribute\n#endif\n",
        .standard = "gnu17",
        .expected = "vector_attribute",
    },
    {
        .name = "assume_nonnull end uses the printer's current line",
        .source = "_Pragma(\"clang assume_nonnull begin\")\n#define SILENT 1\n_Pragma(\"clang assume_nonnull end\")\nint first = __LINE__;\n"
                  "#pragma clang assume_nonnull begin\nint *pointer;\n#pragma clang assume_nonnull end\nint last = __LINE__;\n",
        .standard = "gnu17",
        .expected = "#pragma clang assume_nonnull begin\n#pragma clang assume_nonnull end\nint first = 4;\n"
                    "#pragma clang assume_nonnull begin\nint *pointer;\n#pragma clang assume_nonnull end\nint last = 8;\n",
    },
    {
        .name = "assume_nonnull end after a presumed-file change",
        .source = "#pragma clang assume_nonnull begin\n#line 500 \"logical-nullability.h\"\nint *pointer;\n#define SILENT 1\n"
                  "_Pragma(\"clang assume_nonnull end\")\nint after = __LINE__;\n",
        .standard = "gnu17",
        .expected = "#pragma clang assume_nonnull begin\nint *pointer;\n#pragma clang assume_nonnull end\nint after = 503;\n",
    },
    // #embed (C23 6.10.3), as clang 23 has it: the data, one token that -E prints as its values; the parameters, read
    // expanded, with clang's errors; the files found as clang finds them; and where -E prints it all: no token of an
    // #embed starts a line, a line starts at a file change before them, and the next token of the file joins their line
    // when no newline outside comments is between them.
    {
        .name = "#embed: data, limit, clang::offset, prefix, suffix and if_empty",
        .source = "int a[] = {\n#embed \"hi.txt\"\n};\nint b[] = {\n#embed \"bin.dat\"\n};\nint c[] = { 1,\n"
                  "#embed \"hi.txt\" limit(2) prefix(0,) suffix(,9)\n};\nint d[] = {\n"
                  "#embed \"empty.txt\" if_empty(42) prefix(7,)\n};\n#define LIM 1\nint e[] = {\n"
                  "#embed \"bin.dat\" limit(LIM+1) clang::offset(1)\n};\nint f[] = {\n"
                  "#embed \"hi.txt\" __limit__(3) __suffix__(;)\n};\nint g[] = {\n"
                  "#embed \"hi.txt\" limit(0) if_empty(0) clang::offset(9)\n#embed \"hi.txt\" clang::offset(4)\n"
                  "#embed \"hi.txt\" clang::offset(18446744073709551615u) limit(-1u)\n};\nx\n",
        .standard = "c23",
        .expected = "int a[] = {72, 105, 33, 10 }; int b[] = {0, 255, 127, 128, 9, 10, 99, 100 }; int c[] = { 1,0,72, "
                    "105 ,9}; int d[] = {42}; int e[] = {255, 127 }; int f[] = {72, 105, 33 ;}; int g[] = {0 }; x",
        .files = pp_test_files_embed,
    },
    {
        .name = "#embed: every byte's value, in pieces of every length",
        .source = "#embed \"bytes.dat\"\nx\n#embed \"bytes.dat\" clang::offset(250)\ny\n"
                  "#embed \"bytes.dat\" limit(17) clang::offset(7)\nz\n",
        .standard = "c23",
        .expected = "0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, "
                    "27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, "
                    "51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, "
                    "75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, "
                    "99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, "
                    "118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, "
                    "137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, "
                    "156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, "
                    "175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, "
                    "194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, "
                    "213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, "
                    "232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, "
                    "251, 252, 253, 254, 255 x 250, 251, 252, 253, 254, 255 y 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, "
                    "18, 19, 20, 21, 22, 23 z",
        .files = pp_test_files_embed,
    },
    {
        .name = "#embed: the next token joins the data's line",
        .source = "#define E\na1\n#embed \"hi.txt\" limit(1)\nb1 b2\na2\n#embed \"hi.txt\" limit(1)\n   b3\na3\n"
                  "#embed \"hi.txt\" limit(1)\n\nb4\na5\n#embed \"hi.txt\" limit(1)\n/* c\n*/ b5\na6\n"
                  "#embed \"hi.txt\" limit(1) // c\nb6\na7\n#embed \"hi.txt\" limit(1)\n#define Q\nb7\na8\n"
                  "#embed \"hi.txt\" limit(1)\nE b8\na9\n#embed \"empty.txt\"\nb9\na10\n"
                  "#embed \"empty.txt\" if_empty(z)\nb10\na11\n#include \"embedlast.h\"\nb11\na12\n"
                  "#include \"embedlast_nonl.h\"\nb12\na13\n#embed \"hi.txt\" limit(1)\nE\nb13\n",
        .standard = "c23",
        .expected = "a1 72 b1 b2\na2 72 b3\na3 72\nb4\na5 72 b5\na6 72 b6\na7 72\nb7\na8 72 b8\na9\nb9\na10 z b10\n"
                    "a11\na 72\nb11\na12\na 72\nb12\na13 72\nb13\n",
        .files = pp_test_files_embed,
    },
    {
        .name = "#embed: a file change before it starts a line",
        .source = "first\n#include \"tail.h\"\n\n#embed \"hi.txt\" limit(1)\nz\n#line 50\n\n"
                  "#embed \"hi.txt\" limit(2)\nw\n",
        .standard = "c23",
        .expected = "first x 72 z 72, 105 w",
        .files = pp_test_files_embed,
    },
    {
        .name = "#embed: computed names and macros among the parameters",
        .source = "#define F \"hi.txt\"\n#define P prefix(1,) limit(2)\n#define N 3\n#define L limit\n#define E\n"
                  "#embed F limit(N - 2)\n#embed \"hi.txt\" P\n#embed \"hi.txt\" L(1) suffix(N)\n"
                  "#embed E \"hi.txt\" E limit(1)\nx\n",
        .standard = "c23",
        .expected = "72 1,72, 105 72 3 72 x",
        .files = pp_test_files_embed,
    },
    {
        .name = "#embed: --embed-dir, angled names and the including file's directory",
        .source = "#embed <x.bin>\n#embed \"x.bin\"\na\n#include \"sub/inc.h\"\nb\n#include \"sub/late.h\"\nc\n"
                  "#if __has_embed(<x.bin>) == 1 && __has_embed(\"x.bin\") == 1 && __has_embed(\"y.bin\") == 0\nfound\n"
                  "#endif\n",
        .standard = "c23",
        .expected = "69, 68 69, 68 a\n89\nb\n89\nc\nfound\n",
        .options = "--embed-dir=%d/dir",
        .files = pp_test_files_embed_directories,
    },
    {
        .name = "#embed before C23, an extension",
        .source = "#embed \"hi.txt\" clang::offset(2)\nx\n",
        .standard = "c17",
        .expected = "33, 10 x",
        .warnings = 1,
        .files = pp_test_files_embed,
    },
    {
        .name = "#embed: the parameters' errors drop the directive",
        .source = "a\n#embed \"hi.txt\" limit(defined(X))\nb\n#embed \"hi.txt\" prefix(a] suffix(b)\nc\n"
                  "#embed \"hi.txt\" prefix((a)\nd\n#embed \"hi.txt\" 1\ne\n#embed \"hi.txt\" clang::\nf\n"
                  "#embed \"hi.txt\" limit(1)x\ng\n#embed \"hi.txt\" limit(1 2)\nh\n#embed \"hi.txt\" limit()\ni\n"
                  "#embed \"hi.txt\" prefix(a{b)c})\nj\n#embed \"hi.txt\" limit(-1)\nk\n#embed \"hi.txt\" int\nl\n"
                  "#embed\nm\n#embed \"/dev/null\"\nn\n#embed \"hi.txt\" clang::offset(1) clang::offset(2)\no\n"
                  "#embed \"hi.txt\" prefix(((a)b)c)\np\n#embed \"\"\nq\n#embed \"hi.txt\" limit(1 +)\nr\n"
                  "#embed \"hi.txt\" limit((1)\ns\n#embed \"hi.txt\" prefix({) limit(1)\nt\n",
        .standard = "c23",
        .expected = "a b c d e f g h i j k l m n 33, 10 o p q r s{72 t",
        .fails = true,
        .files = pp_test_files_embed,
    },
    {
        .name = "#embed: a file that isn't there",
        .source = "#embed \"missing.txt\"\nx\n",
        .standard = "c23",
        .expected = "",
        .fails = true,
        .files = pp_test_files_embed,
    },
    {
        .name = "#embed: an angled name without --embed-dir",
        .source = "#embed <hi.txt>\nx\n",
        .standard = "c23",
        .expected = "",
        .fails = true,
        .files = pp_test_files_embed,
    },
    {
        .name = "#embed in a skipped group",
        .source = "#if 0\n#embed \"missing.txt\"\n#embed\n#endif\nok\n",
        .standard = "c23",
        .expected = "ok",
    },
    // #252's review, against clang 23: an #embed's parameters past an angled name in which "//" began a comment for the
    // file's tokens, so that the line is lexed again from the '>', and the errors of a token the name's
    // '>' starts; an #embed after #pragma GCC system_header, which starts its line where the system header's line marker
    // puts it; and comment delimiters that a line splice splits, after an #embed and in a #line's comment.
    {
        .name = "#252's review: an #embed's parameters past an angled name with \"//\"",
        .source = "int a[] = {\n#embed <dir//bytes.bin> limit(0) if_empty(42)\n};\nint b[] = {\n#embed <dir//bytes.bin> limit(1)\n};\n"
                  "int c[] = {\n#embed <dir//bytes.bin> clang::offset(1)\n};\nint d[] = {\n#embed <dir//bytes.bin> /* c */ limit(1) /* d */\n};\n"
                  "int e[] = {\n#embed <dir/\\\n/bytes.bin> limit(1)\n};\nx\n",
        .standard = "c23",
        .expected = "int a[] = {42}; int b[] = {65 }; int c[] = {10 }; int d[] = {65 }; int e[] = {65 }; x",
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "#252's review: a token an #embed name's '>' starts",
        .source = "int a[] = {\n#embed <dir/bytes.bin>> limit(1)\n};\nint b[] = {\n#embed <dir/bytes.bin>= limit(1)\n};\nx\n",
        .standard = "c23",
        .expected = "int a[] = { }; int b[] = { }; x",
        .fails = true,
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "#252's review: an #embed after #pragma GCC system_header",
        .source = "#include \"sys.h\"\nint after;\n",
        .standard = "c23",
        .expected = "unsigned char a[] = { 72, 300 }; int after;",
        .files = pp_test_files_embed_review,
        .markers = "# 1 \"%d/case.c\"\n# 1 \"%d/sys.h\" 1\nunsigned char a[] = {\n# 3 \"%d/sys.h\" 3\n72, 300 };\n# 2 \"%d/case.c\" 2\nint after;\n",
    },
    // #252's second review, against clang 23: the line lexed again past a "//" name runs on through a block comment's
    // newlines, as clang's does, and the file goes on after it (a #define in that comment is none, and the next token
    // joins the data's line); a gap of line splices alone in it isn't a space (STR(a\ +b) is "a+b"); and the system
    // header's note starts where the printer resumes after #pragma GCC system_header, before an #embed indented or after
    // a blank line.
    {
        .name = "#252's second review: an #embed's line past a \"//\" name, through a block comment's newlines",
        .source = "int a[] = {\n#embed <dir//bytes.bin> limit(1) /* comment\n*/ suffix(,42)\n};\nint b[] = {\n#embed <dir//bytes.bin> prefix(1 /* c\n */ ,) limit(1)\n};\n"
                  "int c[] = {\n#embed <dir//bytes.bin> limit(1) /* c\n#define X 1\n*/\n};\nX\n#define STR(x) #x\nint d[] = {\n"
                  "#embed <dir//bytes.bin> prefix(STR(a\\\n+b),) limit(1)\n};\nend\n",
        .standard = "c23",
        .expected = "int a[] = {65 ,42}; int b[] = {1 ,65 }; int c[] = {65 }; X int d[] = {\"a+b\",65 }; end",
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "#252's second review: an #embed after #pragma GCC system_header, indented or after a blank line",
        .source = "#include \"sys_indent.h\"\n#include \"sys_blank.h\"\nint after;\n",
        .standard = "c23",
        .expected = "unsigned char b[] = { 72, 300 }; unsigned char c[] = { 72, 300 }; int after;",
        .files = pp_test_files_embed_review,
        .markers = "# 1 \"%d/case.c\"\n# 1 \"%d/sys_indent.h\" 1\nunsigned char b[] = {\n# 3 \"%d/sys_indent.h\" 3\n72, 300 };\n# 2 \"%d/case.c\" 2\n"
                   "# 1 \"%d/sys_blank.h\" 1\nunsigned char c[] = {\n# 3 \"%d/sys_blank.h\" 3\n72, 300 };\n# 3 \"%d/case.c\" 2\nint after;\n",
    },
    {
        .name = "#252's review: comment delimiters a line splice splits, after an #embed and in a #line's comment",
        .source = "int a[] = {\n#embed \"hi.txt\" limit(1)\n/* comment *\\\n/\n\n};\nint b[] = {\n#embed \"hi.txt\" limit(1)\n/\\\n* comment */\n\n};\n"
                  "int c[] = {\n#embed \"hi.txt\" limit(1)\n/\\\n* comment\n*/ };\n#line 100 /* x *\\\n/\n#embed \"hi.txt\" limit(1)\nint z;\n",
        .standard = "c23",
        .expected = "int a[] = {72 }; int b[] = {72 }; int c[] = {72 }; 72 int z;",
        .files = pp_test_files_embed_review,
    },
    // #252's third review, against clang 23, on lines lexed again past a name with "//" (or "/*"): a block comment right
    // after the name carries the line on (the file's tokens had nothing past the name to differ); directives that a
    // comment in such a line hides, which the file's conditional pairing counted (an #endif there leaves the #else, #elif
    // and #elifdef after it to a count of the nesting, through #if, #ifdef, #ifndef and #elifndef); poisoned names in it,
    // each an error once, and none in its comments, where the file's tokens may have them; an unterminated block comment
    // at its end (clang's error); comments that end and begin again on its lines, each piece lexed once; and the file's
    // next token, which joins the data's line from where the line lexed again ends (`// */` is in its comment).
    {
        .name = "#252's third review: a block comment right after an #embed's \"//\" name",
        .source = "int a[] = {\n#embed <dir//bytes.bin> /* comment\n*/\n};\nint b[] = {\n#embed <dir//bytes.bin> /* c\n*/ limit(1)\n};\n"
                  "int c[] = {\n#embed <dir//bytes.bin> /* c */ /* d\n*/ limit(1) /* e\n*/\n};\nafter\n",
        .standard = "c23",
        .expected = "int a[] = {65, 10 }; int b[] = {65 }; int c[] = {65 }; after",
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "#252's third review: directives hidden in an #embed's line lexed again",
        .source = "#if 1\n#embed <dir//bytes.bin> limit(1) /* comment\n#endif\n*/\n#else\nbad\n#endif\ngood\n"
                  "#ifdef __STDC__\n#embed <dir//bytes.bin> limit(1) /* comment\n#endif\n*/\n#elifdef __STDC__\nbad\n#else\n#ifndef X\nbad\n"
                  "#elifndef Y\nbad\n#else\nbad\n#endif\n#if 1\n#endif\nbad\n#endif\ngood2\n",
        .standard = "c23",
        .expected = "65 good 65 good2",
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "#252's third review: poisoned names in an #embed's line lexed again",
        .source = "#pragma GCC poison bad\nint a[] = {\n#embed <dir//bytes.bin> limit(1) prefix(bad,)\n};\nint b[] = {\n"
                  "#embed <dir/bytes.bin>> limit(1) > bad\n};\nint c[] = {\n#embed <dir/bytes.bin> limit(1) prefix(bad,)\n};\nx\n",
        .standard = "c23",
        .expected = "int a[] = {bad,65 }; int b[] = { }; int c[] = {bad,65 }; x",
        .fails = true,
        .errors = 4,
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "#252's third review: poisoned names in comments of an #embed's line lexed again",
        .source = "#pragma GCC poison bad\nint a[] = {\n#embed <dir/*x.bin> limit(1) /*/ > bad */\n};\nint b[] = {\n"
                  "#embed <dir//bytes.bin> limit(1) /* bad\n*/\n};\nx\n",
        .standard = "c23",
        .expected = "int a[] = {66 }; int b[] = {65 }; x",
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "#252's third review: an unterminated block comment in an #embed's line lexed again",
        .source = "int a[] = {\n#embed <dir//bytes.bin> limit(1) /* unclosed\n};\n",
        .standard = "c23",
        .expected = "int a[] = {65",
        .fails = true,
        .errors = 1,
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "#252's third review: comments that end and begin again in an #embed's line lexed again",
        .source = "int a[] = {\n#embed <dir//bytes.bin> limit(1) prefix(/*\n*/1,/*\n*/2, /* x */ /*\n\n*/3,) /* y */ /*\n*/ suffix(,/*\n\n*/4)\n};\nend\n",
        .standard = "c23",
        .expected = "int a[] = {1,2,3,65,4}; end",
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "#252's third review: the token after an #embed's line lexed again",
        .source = "int a[] = {\n#embed <dir//bytes.bin> limit(1) /* comment\n// */\n, __builtin_LINE()\n};\nint b[] = {\n"
                  "#embed <dir//bytes.bin> limit(1) /* comment\n// */\n\n, 3\n};\n",
        .standard = "c23",
        .expected = "int a[] = {65 , __builtin_LINE() }; int b[] = {65 , 3 };",
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
        .markers = "# 1 \"%d/case.c\"\nint a[] = {65 , __builtin_LINE()\n\n\n\n};\nint b[] = {65\n\n\n\n, 3\n};\n",
    },
    {
        .name = "__has_embed: clang's limit and clang::offset",
        .source = "#if __has_embed(\"hi.txt\") == 1 && __has_embed(\"hi.txt\" limit(4)) == 1 && __has_embed(\"hi.txt\" "
                  "limit(5)) == 2 && __has_embed(\"hi.txt\" limit(100)) == 2\nlimits\n#endif\n"
                  "#if __has_embed(\"hi.txt\" clang::offset(3)) == 1 && __has_embed(\"hi.txt\" clang::offset(4)) == 2 &"
                  "& __has_embed(\"hi.txt\" clang::offset(9)) == 2 && __has_embed(\"empty.txt\") == 2\noffsets\n"
                  "#endif\n"
                  "#if __has_embed(<hi.txt>) == 0 && __has_embed(\"hi.txt\" foo) == 0 && __has_embed(\"hi.txt\" foo(1))"
                  " == 0 && __has_embed(\"hi.txt\" __if_empty__(x)) == 1 && __has_embed(\"hi.txt\" clang::offset(1) lim"
                  "it(3)) == 1 && __has_embed(\"hi.txt\" clang::offset(1) limit(4)) == 2\nothers\n#endif\n",
        .standard = "c23",
        .expected = "limits offsets others",
        .files = pp_test_files_embed,
    },
    // #119's done-when checks, against clang 23: header names written out ("//" and "/*...*/" are part of the name,
    // and a comment that runs on to the next line makes clang join the line's tokens), computed ones, #include_next
    // through three directories, an inactive group's includes, a cycle stopped at clang's depth limit (an error, after
    // which the file isn't entered again), one record through links, clang's recoveries (an angled name found as a
    // quoted one, a name found without what it starts and ends with besides letters and digits), and the diagnostics'
    // places.
    {
        .name = "#119: header names written out, a comment that runs on to the next line among them",
        .source = "#include <a//b.h>\n#include <a/*x*/b.h>\n#include \"a\\b.h\"\n#include <a/*x\n*/b.h>\n",
        .standard = "c17",
        .expected = "ab axb backslash joined",
        .options = "-I%d",
        .files = pp_test_files_includes,
    },
    {
        .name = "#119: computed names, joined as clang joins them",
        .source = "#define H <c.h>\n#include H\n#define Q \"c.h\"\n#include Q\n#define L <\n#define R >\n#include L "
                  "c.h R\n",
        .standard = "c17",
        .expected = "computed computed computed",
        .fails = true,
        .warnings = 1,
        .options = "-I%d",
        .files = pp_test_files_includes,
    },
    {
        .name = "#119: #include_next through three directories",
        .source = "#include <n.h>\n",
        .standard = "c17",
        .expected = "one two three",
        .options = "-I%d/d1 -I%d/d2 -I%d/d3",
        .files = pp_test_files_includes,
    },
    {
        .name = "#119: an inactive group's includes aren't looked for",
        .source = "#if 0\n#include \"missing.h\"\n#include <missing.h>\n#elif 0\n#include \"missing2.h\"\n#else\n"
                  "#endif\nok\n",
        .standard = "c17",
        .expected = "ok",
        .files = pp_test_files_includes,
    },
    {
        .name = "#119: an include cycle stops at clang's depth limit",
        .source = "#include \"self.h\"\nend\n",
        .standard = "c17",
        .expected = "x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x "
                    "x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x "
                    "x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x "
                    "x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x "
                    "x x x end",
        .fails = true,
        .files = pp_test_files_includes,
    },
    {
        .name = "#119: one file through a symbolic link and a hard link",
        .source = "#include \"real.h\"\n#include \"sym.h\"\n#include \"hard.h\"\n#include \"guarded.h\"\n#include "
                  "\"gsym.h\"\nend\n",
        .standard = "c17",
        .expected = "once guarded end",
        .files = pp_test_files_includes,
    },
    {
        .name = "#119: clang's recoveries from a name not found",
        .source = "#include <local.h>\na\n#include \"local.h \"\nb\n#include \" local.h\"\nc\n#include \"local.h.\"\n"
                  "d\n",
        .standard = "c17",
        .expected = "local a local b local c local d",
        .fails = true,
        .warnings = 2,
        .files = pp_test_files_includes,
    },
    {
        .name = "#119: an include's diagnostics, at clang's places",
        .source = "#include\n#include 1\n#include \"local.h\" x\n#include <local.h\n#include \"\"\n#include <>\nz\n",
        .standard = "c17",
        .expected = "local z",
        .fails = true,
        .warnings = 1,
        .options = "-I%d",
        .files = pp_test_files_includes,
    },
    // #254's review, against clang 23: a header name written out that is too long for any path is one still, refused
    // (clang's fatal error about the path, which the history cases compare), and never a computed one whose identifier
    // expands to another header's name, nor is an #embed's longer than the buffer it reads into; __has_include's and
    // __has_embed's operands written in their #if line are read as the line writes them, and the same operands from a
    // macro or an argument joined from their tokens; and a header that the depth limit refused is included as clang
    // marks it: three #includes of it give one error, and an #include after the cycle unwinds doesn't enter it.
    {
        .name = "#254's review: a header name written out, too long for a path",
        .source = "#define " BUSTER_PP_TEST_A4096 " short.h\n#include <" BUSTER_PP_TEST_A4096 ">\nEND\n",
        .standard = "c17",
        .expected = "",
        .fails = true,
        .options = "-I%d",
        .files = pp_test_files_includes,
    },
    {
        .name = "#254's review: an #embed name written out, longer than its buffer",
        .source = "#define " BUSTER_PP_TEST_A4096 "A short.h\n#embed <" BUSTER_PP_TEST_A4096 "A>\nEND\n",
        .standard = "c23",
        .expected = "",
        .fails = true,
        .options = "--embed-dir=%d",
        .files = pp_test_files_includes,
    },
    {
        .name = "#254's review: __has_include's and __has_embed's operands as their line writes them, \" x.h\" alone",
        .source = "#define ID(x) x\n#define HI __has_include\n#define ARG <  x.h>\n#if __has_include(<  x.h>)\nraw\n#endif\n#if HI(<  x.h>)\nhi\n#endif\n"
                  "#if ID(__has_include(<  x.h>))\nargument\n#endif\n#if __has_include(ARG)\nmacro\n#endif\n#if __has_embed(<  x.h>) == 0 && "
                  "ID(__has_embed(<  x.h>)) == 1\nembed\n#endif\n#if __has_include(<  x.h>)\n#include <  x.h>\n#endif\nvalid_translation_unit\n",
        .standard = "c23",
        .expected = "argument macro embed valid_translation_unit",
        .options = "-I%d/one --embed-dir=%d/one",
        .files = pp_test_files_includes,
    },
    {
        .name = "#254's review: __has_include's and __has_embed's operands as their line writes them, \"  x.h\" alone",
        .source = "#define ID(x) x\n#define HI __has_include\n#define ARG <  x.h>\n#if __has_include(<  x.h>)\nraw\n#endif\n#if HI(<  x.h>)\nhi\n#endif\n"
                  "#if ID(__has_include(<  x.h>))\nargument\n#endif\n#if __has_include(ARG)\nmacro\n#endif\n#if __has_embed(<  x.h>) == 1 && "
                  "__has_embed(<  x.h> limit(0)) == 2 && ID(__has_embed(<  x.h>)) == 0\nembed\n#endif\n#include <  x.h>\n",
        .standard = "c23",
        .expected = "raw hi embed two_spaces",
        .options = "-I%d/two --embed-dir=%d/two",
        .files = pp_test_files_includes,
    },
    {
        .name = "header-query continuation retains dollar punctuation and wide literal prefixes",
        .source = "#define L changed\n#define S_(...) #__VA_ARGS__\n#define S(...) S_(__VA_ARGS__)\n"
                  "#if __has_include(<dir//x.h>) && _Pragma(S(ppf $L\"x\")) 1\nyes\n#endif\n",
        .standard = "c17",
        .expected = "#pragma ppf $L\"x\"\nyes\n",
        .options = "-fno-dollars-in-identifiers -I%d",
        .files = pp_test_files_header_tails,
    },
    // #254's second review, against clang 23: a backslash in a header name written out takes the character after it into
    // the name, a '>' too, as clang's LexAngledStringLiteral reads it; and the rest of the line past such a name is what
    // clang lexes on from its '>', where the file's tokens ran past it: a "//" in the name began a comment there (the
    // #if line's `&& 0` and `|| defined(NOPE)` count, an #include's `junk` is a warning and __has_embed's parameters are
    // read, also on past a block comment's newlines, and the file goes on after that line), or a token the '>' starts
    // ("x.h>>", "x.h>=", "x.h>>=") stays in the operand, an error.
    {
        .name = "#254's second review: a backslash in a header name written out escapes its '>'",
        .source = "#if __has_include(<a\\>b.h>) && __has_include(\"a\\>b.h\")\nyes\n#endif\n#include <a\\>b.h>\nend\n",
        .standard = "c17",
        .expected = "yes int escaped; end",
        .options = "-I%d",
        .files = pp_test_files_header_tails,
    },
    {
        .name = "#254's second review: the rest of a line past a header name with \"//\"",
        .source = "#if __has_include(<dir//x.h>) && 1\nyes2\n#endif\n#if __has_include(<dir//x.h>) && 0\nno3\n#endif\n"
                  "#if __has_include(<dir//x.h>) || defined(NOPE)\nyes4\n#endif\n#if __has_embed(<dir//x.h> limit(1)) == 1\nyes5\n#endif\n"
                  "#if __has_embed(<dir//x.h> limit(0)) == 2\nyes6\n#endif\n#include <dir//x.h> junk\nend\n",
        .standard = "c23",
        .expected = "yes2 yes4 yes5 yes6 int dir_x; end",
        .warnings = 1,
        .options = "-I%d --embed-dir=%d",
        .files = pp_test_files_header_tails,
    },
    {
        .name = "#254's second review: a \"//\" name's line, through a block comment's newlines",
        .source = "#include <dir//x.h> /* c\n*/ junk\nmid\n#if __has_include(<dir//x.h>) /* c\n#define Y 1\n*/ && 0\nno\n#endif\n#ifdef Y\nY_defined\n#endif\n"
                  "#if __has_include(<dir//x.h>) /* c\n*/ || 0\nyes\n#endif\nend\n",
        .standard = "c17",
        .expected = "int dir_x; mid yes end",
        .warnings = 1,
        .options = "-I%d",
        .files = pp_test_files_header_tails,
    },
    {
        .name = "#254's second review: a token a header query's '>' starts stays in its operand",
        .source = "#if __has_include(<x.h>>)\nwrong1\n#endif\n#if __has_include(<x.h>=)\nwrong2\n#endif\n#if __has_include(<x.h>>=)\nwrong3\n#endif\n"
                  "#if __has_include(<a\\\\>b.h>)\nwrong4\n#endif\nend\n",
        .standard = "c17",
        .expected = "end",
        .fails = true,
        .options = "-I%d",
        .files = pp_test_files_header_tails,
    },
    {
        .name = "#254's review: a header refused at the depth limit is included",
        .source = "#include \"loop.h\"\n#include \"leaf.h\"\nEND\n",
        .standard = "c17",
        .expected = "END",
        .fails = true,
        .files = pp_test_files_includes,
    },
    {
        .name = "-municode's UNICODE is <command line>'s first macro, before -D and -U",
        .source = "UNICODE\n",
        .standard = "gnu17",
        .expected = "2",
        .warnings = 1,
        .options = "-DUNICODE=2 -municode",
    },
    {
        .name = "-municode is a joined option, and a -U before it undefines its UNICODE",
        .source = "#ifdef UNICODE\ndefined\n#else\nundefined\n#endif\n",
        .standard = "gnu17",
        .expected = "undefined",
        .options = "-UUNICODE -municodeX",
    },
    {
        .name = "#254's third review: condition links ignore directives hidden by a raw query's comment",
        .source = "#if __has_include(<dir//x.h>) /* comment\n#else\n#define IGNORED */ && 0\nint wrong_branch;\n#endif\n"
                  "#ifdef IGNORED\nint wrong_define;\n#endif\n#if __has_include(<dir//x.h>) /* comment\n#elifdef IGNORED\n*/ && 0\nwrong\n"
                  "#else\nint real_else;\n#endif\nint end;\n",
        .standard = "c17",
        .expected = "int real_else; int end;",
        .options = "-I%d",
        .files = pp_test_files_header_tails,
    },
    // An ordinary comment delimiter belongs to an opaque header name too. Its original file token must not hide the
    // source after the name or report a comment error; a real comment after the name must still be diagnosed.
    {
        .name = "opaque header: an unclosed comment delimiter in an include name",
        .source = "#include <dir/*x.h>\nafter\n",
        .standard = "c17",
        .expected = "opaque_header after",
        .options = "-I%d",
        .files = pp_test_files_includes,
    },
    {
        .name = "opaque header: an unclosed comment delimiter in an embed name",
        .source = "int a[] = {\n#embed <dir/*x.bin> limit(1)\n};\nafter\n",
        .standard = "c23",
        .expected = "int a[] = {66}; after",
        .options = "--embed-dir=%d",
        .files = pp_test_files_embed_review,
    },
    {
        .name = "opaque header: an unclosed comment delimiter in a query name",
        .source = "#if __has_include(<dir/*x.h>) && __has_embed(<dir/*x.bin> limit(1)) == 1\nfound\n#else\nwrong\n#endif\n"
                  "#if __has_include(<dir/*x.h>) && 0\nwrong\n#else\nfalse_branch\n#endif\n"
                  "#if 0\nwrong\n#elif __has_include(<dir/*x.h>)\nelif_branch\n#endif\nafter\n",
        .standard = "c23",
        .expected = "found false_branch elif_branch after",
        .options = "-I%d --embed-dir=%d",
        .files = pp_test_files_includes,
    },
    {
        .name = "opaque header: a real unclosed comment after an opaque name",
        .source = "#include <dir/*x.h> /* unclosed\nafter\n",
        .standard = "c17",
        .expected = "opaque_header",
        .fails = true,
        .errors = 1,
        .options = "-I%d",
        .files = pp_test_files_includes,
    },
    {
        .name = "#254's third review: later queries retain raw names in a re-lexed suffix",
        .source = "#if __has_include(<dir//x.h>) && __has_include(<  x.h>)\nint wrong_branch;\n#else\nint expected_branch;\n#endif\n"
                  "#if __has_include(<dir//x.h>) && __has_include(<dir//x.h>) && __has_embed(<dir//x.h> limit(1)) == 1\nint repeated;\n#endif\n"
                  "#if __has_include(<dir//x.h>) && (1 + (2 * 3) == 7) && __has_include(<dir//x.h>)\nint parentheses;\n#endif\n",
        .standard = "c23",
        .expected = "int expected_branch; int repeated; int parentheses;",
        .options = "-I%d --embed-dir=%d",
        .files = pp_test_files_header_tails,
    },
};

// Writes `length` bytes of `text` to a file, or all of it up to its zero byte when `length` is 0; returns whether it could.
static bool pp_test_write_bytes(const char* path, const char* text, uint32_t length)
{
    FILE* file = fopen(path, "w");
    const size_t size = length != 0 ? length : strlen(text);
    const bool result = file != 0 && fwrite(text, 1, size, file) == size;
    if (file != 0)
    {
        fclose(file);
    }
    return result;
}

// Writes a text to a file; returns whether it could.
static bool pp_test_write_file(const char* path, const char* text)
{
    return pp_test_write_bytes(path, text, 0);
}

// Writes a case's files into its directory (other.h, and the others with their directories, or links); returns the
// main file's path (in `path`).
static bool pp_test_write_case(const PPTestCase* test, const char* directory, char* path, uint32_t capacity)
{
    snprintf(path, capacity, "%s/case.c", directory);
    bool result = pp_test_write_file(path, test->source);
    char other[4096];
    if (result && test->other != 0)
    {
        snprintf(other, sizeof(other), "%s/other.h", directory);
        result = pp_test_write_file(other, test->other);
    }
    for (uint32_t i = 0; result && test->files != 0 && test->files[i].path != 0; i += 1)
    {
        const PPTestFile* file = &test->files[i];
        // Each directory on the way, outermost first.
        for (const char* slash = strchr(file->path, '/'); slash != 0; slash = strchr(slash + 1, '/'))
        {
            snprintf(other, sizeof(other), "%s/%.*s", directory, (int)(slash - file->path), file->path);
            mkdir(other, 0700);
        }
        snprintf(other, sizeof(other), "%s/%s", directory, file->path);
        if (file->link != 0 && file->hard)
        {
            char target[4096];
            snprintf(target, sizeof(target), "%s/%s", directory, file->link);
            result = link(target, other) == 0;
        }
        else
        {
            result = file->link != 0 ? symlink(file->link, other) == 0 : pp_test_write_bytes(other, file->text, file->length);
        }
    }
    return result;
}

// Removes the files pp_test_write_case wrote besides case.c, and their directories, innermost first.
static void pp_test_remove_case(const PPTestCase* test, const char* directory)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/other.h", directory);
    unlink(path);
    for (uint32_t i = 0; test->files != 0 && test->files[i].path != 0; i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, test->files[i].path);
        unlink(path);
    }
    for (uint32_t i = 0; test->files != 0 && test->files[i].path != 0; i += 1)
    {
        const char* file = test->files[i].path;
        for (uint32_t end = (uint32_t)strlen(file); end > 0; end -= 1)
        {
            if (file[end - 1] == '/')
            {
                snprintf(path, sizeof(path), "%s/%.*s", directory, (int)(end - 1), file);
                rmdir(path);
            }
        }
    }
}

#define BUSTER_PP_TEST_MAX_ARGUMENTS 32

// A case's arguments, for pp_parse_options and clang: -std= and its options with %d replaced by the directory, written
// into `text`, then `path` after them (which clang's don't take). Returns how many come before `path`.
static int pp_test_arguments(const PPTestCase* test, const char* directory, const char* path, char* text, uint32_t capacity, char** arguments)
{
    uint32_t used = (uint32_t)snprintf(text, capacity, "-std=%s", test->standard) + 1;
    int count = 1;
    arguments[0] = text;
    const char* options = test->options != 0 ? test->options : "";
    while (*options != 0 && count + 1 < BUSTER_PP_TEST_MAX_ARGUMENTS && used < capacity)
    {
        while (*options == ' ')
        {
            options += 1;
        }
        if (*options != 0)
        {
            arguments[count] = text + used;
            count += 1;
            while (*options != 0 && *options != ' ' && used + 4096 < capacity)
            {
                if (options[0] == '%' && options[1] == 'd')
                {
                    used += (uint32_t)snprintf(text + used, capacity - used, "%s", directory);
                    options += 2;
                }
                else
                {
                    text[used] = *options;
                    used += 1;
                    options += 1;
                }
            }
            text[used] = 0;
            used += 1;
        }
    }
    arguments[count] = (char*)path;
    return count;
}

// A case's text with line markers (PPTestCase.markers, %d for its directory) against the vectorized printer's, byte for
// byte: the first line that differs is printed.
static bool pp_test_markers(Preprocessor* pp, const PPTestCase* test, const char* directory)
{
    size_t length = 0;
    char* text = pp_test_print(pp, true, true, &length);
    char* wanted = 0;
    size_t wanted_length = 0;
    FILE* stream = open_memstream(&wanted, &wanted_length);
    for (const char* at = test->markers; stream != 0 && *at != 0; at += 1)
    {
        if (at[0] == '%' && at[1] == 'd')
        {
            fputs(directory, stream);
            at += 1;
        }
        else
        {
            fputc(*at, stream);
        }
    }
    if (stream != 0)
    {
        fclose(stream);
    }
    const bool result = text != 0 && wanted != 0 && length == wanted_length && memcmp(text, wanted, length) == 0;
    if (!result && text != 0 && wanted != 0)
    {
        size_t line = 0;
        size_t at = 0;
        while (at < length && at < wanted_length && text[at] == wanted[at])
        {
            line = text[at] == '\n' ? at + 1 : line;
            at += 1;
        }
        const char* ours_end = memchr(text + line, '\n', length - line);
        const char* wanted_end = memchr(wanted + line, '\n', wanted_length - line);
        printf("%s: the line markers differ at byte %zu:\n  ours   %.*s\n  wanted %.*s\n", test->name, at, (int)((ours_end != 0 ? ours_end : text + length) - (text + line)),
               text + line, (int)((wanted_end != 0 ? wanted_end : wanted + wanted_length) - (wanted + line)), wanted + line);
    }
    free(text);
    free(wanted);
    return result;
}

// Output tokens' system levels written by hand: a case's main file, its header (as both sys/s.h, under -isystem, and
// other.h, beside it), each output token's level (u: a user file's, s: a system header's, e: an extern "C" one's), which
// both paths must give (pp_system_levels and its twin); or for a case whose runs can't say (`inexact`,
// Preprocessor.system_runs_inexact), the levels the history gives, which the checker then judges by.
typedef struct PPTestSystemCase PPTestSystemCase;
struct PPTestSystemCase
{
    const char* name;
    const char* source;
    const char* header;
    const char* levels;
    bool inexact;
};

static const PPTestSystemCase pp_test_system_cases[] = {
    // A system header's macros, a paste and a stringification in it, invoked in the main file, and the main file's
    // macro in the header: each token is where its outermost invocation is.
    { "macros across a system header", "#define USERMAC 1\n#include <s.h>\nSYSMAC SYSFN(a) CALLS USERMAC CAT(x, y) STR(q)\n",
      "#define SYSMAC user_token\n#define SYSFN(x) [x]\n#define CALLS USERMAC\n#define CAT(a, b) a ## b\n#define STR(x) #x\n"
      "int header = SYSMAC + USERMAC + CALLS + CAT(1, 2);\n",
      "sssssssssssuuuuuuuu" },
    { "line markers", "#define USER(x) x\n# 1 \"fake-system.h\" 3\n#define SYS(x) (x)\nSYS(1) USER(2)\n# 5 \"main.c\"\nSYS(3) USER(4)\n", "", "ssssuuuu" },
    { "an extern C header's line marker", "# 1 \"c-header.h\" 3 4\n#define E(x) x\nE(1) e\n", "", "ee" },
    { "#pragma GCC system_header", "#define MAINMAC main_value\n#include \"other.h\"\nint c = BEFORE;\n",
      "#define BEFORE before_value\nint a = BEFORE;\n#pragma GCC system_header\nint b = BEFORE + MAINMAC;\n", "uuuuusssssssuuuuu" },
    // A line marker read while the arguments of a system header's F are collected in the user's file: F's body is where
    // the invocation starts, before the marker, and its argument where it is read, after it; the runs would have both
    // after it, and the body's own place is the system header. The second F is all after the marker.
    { "a line marker among an invocation's arguments", "# 1 \"sys.h\" 3\n#define F(x) [x]\n# 5 \"user.c\"\nF(\n# 10 \"s.h\" 3\na) F(b)\n", "",
      "ususss", true },
};

// Each of pp_test_system_cases, preprocessed as a compile runs it with its history: both paths must give its levels, or
// its runs must say they can't. Returns whether every case does.
static bool pp_test_system_case_levels(const char* directory)
{
    uint32_t failures = 0;
    char system[4096];
    char header[4096];
    char other[4096];
    char path[4096];
    snprintf(system, sizeof(system), "%s/sys", directory);
    snprintf(header, sizeof(header), "%s/sys/s.h", directory);
    snprintf(other, sizeof(other), "%s/other.h", directory);
    snprintf(path, sizeof(path), "%s/system-levels.c", directory);
    const bool made = mkdir(system, 0700) == 0 || errno == EEXIST;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(pp_test_system_cases); c += 1)
    {
        const PPTestSystemCase* test = &pp_test_system_cases[c];
        static InternTable table;
        static Preprocessor pp;
        Arena arena = arena_reserve(1ull << 20);
        char* arguments[] = { (char*)"-std=c17", (char*)"-isystem", system, path };
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        bool result = made && arena.base != 0 && pp_test_write_file(header, test->header) && pp_test_write_file(other, test->header) &&
                      pp_test_write_file(path, test->source) && pp_parse_options(4, arguments, &options, &arena, &input, &output, &markers);
        const bool ready = result && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
        result = ready && pp_run(&pp, path) && pp.system_runs_inexact == test->inexact;
        const uint32_t count = pp.output.count;
        uint8_t* levels = result ? malloc((uint64_t)count * 2 + 2) : 0;
        result = levels != 0 && strlen(test->levels) == count;
        if (result)
        {
            pp_system_levels(&pp, levels);
            pp_system_levels_history(&pp, levels + count + 1);
        }
        for (uint32_t i = 0; result && i < count; i += 1)
        {
            const uint8_t wanted = test->levels[i] == 's' ? BUSTER_PP_SYSTEM_FILE : test->levels[i] == 'e' ? BUSTER_PP_EXTERN_C_FILE : BUSTER_PP_USER_FILE;
            result = (test->inexact || levels[i] == wanted) && levels[count + 1 + i] == wanted;
        }
        free(levels);
        if (!result)
        {
            printf("system levels: %s: %u output tokens, %s, not the levels written for it\n", test->name, count,
                   pp.system_runs_inexact ? "runs that can't say" : "runs that can");
        }
        failures += !result;
        if (ready)
        {
            pp_release(&pp);
        }
        intern_release(&table);
        arena_release(&arena);
    }
    unlink(path);
    unlink(header);
    unlink(other);
    rmdir(system);
    printf("system levels: %u of %lu cases written by hand differ\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(pp_test_system_cases));
    return failures == 0;
}

static bool pp_test_case(const PPTestCase* test, const char* directory, bool with_clang)
{
    char path[4096];
    bool result = pp_test_write_case(test, directory, path, sizeof(path));
    char text[16384];
    char* arguments[BUSTER_PP_TEST_MAX_ARGUMENTS + 1];
    const int argument_count = pp_test_arguments(test, directory, path, text, sizeof(text), arguments);
    Arena arena = arena_reserve(1ull << 20);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    result = result && pp_parse_options(argument_count + 1, arguments, &options, &arena, &input, &output, &markers);
    // The cases are compared with clang: the feature queries answer as clang's.
    options.clang_features = true;
    static InternTable tables[2];
    static Preprocessor runs[2];
    bool ran[2] = { false, false };
    for (uint32_t k = 0; result && k < 2; k += 1)
    {
        options.scalar = k == 1;
        // A case that fails prints its error; the expected output is then not compared.
        ran[k] = pp_test_run(&runs[k], &tables[k], &options, path);
    }
    if (result)
    {
        result = pp_same_streams(&runs[0], &runs[1], test->name) && pp_same_directive_tables(&runs[0], &runs[1], test->name) && ran[0] == ran[1] &&
                 ran[0] != test->fails && pp_test_compile_kernels(&options, path, test->name);
        if (ran[0] == test->fails)
        {
            printf("%s: %s\n", test->name, test->fails ? "should fail but doesn't" : "fails");
        }
        for (uint32_t k = 0; k < 2; k += 1)
        {
            if (runs[k].warnings != test->warnings)
            {
                printf("%s: %u warnings (%s kernels), not %u\n", test->name, runs[k].warnings, k == 0 ? "vectorized" : "scalar", test->warnings);
                result = false;
            }
            if (test->errors != 0 && runs[k].errors != test->errors)
            {
                printf("%s: %u errors (%s kernels), not %u\n", test->name, runs[k].errors, k == 0 ? "vectorized" : "scalar", test->errors);
                result = false;
            }
        }
    }
    if (result && (!test->fails || test->expected[0] != 0))
    {
        PPSpellings ours = { 0 };
        PPSpellings expected = { 0 };
        result = pp_stream_spellings(&runs[0], &ours) && pp_text_spellings((const uint8_t*)test->expected, strlen(test->expected), runs[0].split_kinds, pp_test_lex_flags(&runs[0]), &expected) &&
                 pp_compare_spellings(&runs[0], &ours, &expected, test->name, "wanted") && pp_test_printed(&runs[0], &ours, test->name) &&
                 (test->markers == 0 || pp_test_markers(&runs[0], test, directory));
        if (result && with_clang)
        {
            size_t length;
            char* clang_text = pp_test_clang(arguments, argument_count, path, 0, &length);
            PPPlaced theirs = { 0 };
            result = clang_text != 0 && pp_placed_tokens((const uint8_t*)clang_text, length, runs[0].split_kinds, pp_test_lex_flags(&runs[0]), &theirs) &&
                     pp_compare_printed_spellings(&runs[0], &ours, &theirs.spellings, test->name, "clang") && pp_test_placed(&runs[0], &theirs, test->name);
            pp_placed_release(&theirs);
            free(clang_text);
        }
        arena_release(&ours.arena);
        arena_release(&expected.arena);
    }
    for (uint32_t k = 0; k < 2; k += 1)
    {
        pp_release(&runs[k]);
        intern_release(&tables[k]);
    }
    arena_release(&arena);
    pp_test_remove_case(test, directory);
    return result;
}

// #embed's lookups that the cases can't set up (#252's review): an empty --embed-dir= is the working directory, with no
// '/' put before the name (clang's LookupEmbedFile), from the case's directory as the working directory; and __has_embed
// answers from a file's size without reading it, as clang's EvaluateHasEmbed does (LookupEmbedFile without opening the
// file), also for a sparse file of 5 GiB, past what #embed reads, and one it may not read. Both cases run as the others
// do (pp_test_case), with clang too under --clang; the run's working directory is restored after.
static bool pp_test_embed_lookups(const char* directory, bool with_clang)
{
    static const PPTestFile files[] = {
        { "bytes.bin", "XY" },
        { 0, 0 },
    };
    const PPTestCase cases[] = {
        {
            .name = "#252's review: an empty --embed-dir= is the working directory",
            .source = "#embed <bytes.bin>\nx\n",
            .standard = "c23",
            .expected = "88, 89 x",
            .options = "--embed-dir=",
            .files = files,
        },
        {
            .name = "#252's review: __has_embed of files it only looks at",
            .source = "#if __has_embed(\"big.bin\") == 1 && __has_embed(\"big.bin\" limit(1)) == 1\nbig\n#endif\n"
                      "#if __has_embed(\"locked.bin\") == 1 && __has_embed(\"locked.bin\" clang::offset(2)) == 2\nlocked\n#endif\n",
            .standard = "c23",
            .expected = "big locked",
        },
    };
    char big[4096];
    char locked[4096];
    snprintf(big, sizeof(big), "%s/big.bin", directory);
    snprintf(locked, sizeof(locked), "%s/locked.bin", directory);
    const int big_fd = open(big, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    bool result = big_fd >= 0 && ftruncate(big_fd, 5ll << 30) == 0;
    if (big_fd >= 0)
    {
        close(big_fd);
    }
    result = result && pp_test_write_file(locked, "AB") && chmod(locked, 0) == 0;
    const int saved = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    result = result && saved >= 0 && chdir(directory) == 0;
    uint32_t failures = 0;
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        failures += !pp_test_case(&cases[i], directory, with_clang);
    }
    const bool back = saved >= 0 && fchdir(saved) == 0;
    if (saved >= 0)
    {
        close(saved);
    }
    unlink(big);
    unlink(locked);
    printf("embed lookups: %u of %lu fail%s\n", result ? failures : (uint32_t)BUSTER_ARRAY_LENGTH(cases), (unsigned long)BUSTER_ARRAY_LENGTH(cases),
           result ? "" : " (the files or the working directory couldn't be set up)");
    return result && back && failures == 0;
}

// Each fresh candidate's expected open/fstat/read counts, independently of the counters themselves. Repeat hits and
// misses first in one unit, then through a second unit's empty path table sharing the first unit's cache. The alias
// has the same identity as value.h: it still needs open/fstat, but no second read. A directory needs fstat too; the
// symlink loop's failed open does not. None of these searches prepares or executes a file.
static bool pp_test_lookup_probes(const PPOptions* base, const char* missing_root)
{
    static const struct
    {
        const char* name;
        bool found;
        uint32_t opens;
        uint32_t fstats;
        uint32_t reads;
    } cases[] = {
        { "absent.h", false, 2, 0, 0 },
        { "absent.h", false, 0, 0, 0 },
        { "value.h", true, 2, 1, 1 },
        { "value.h", true, 0, 0, 0 },
        { "alias.h", true, 2, 1, 0 },
        { "folder.h", false, 2, 1, 0 },
        { "folder.h", false, 0, 0, 0 },
        { "loop.h", false, 2, 0, 0 },
        { "loop.h", false, 0, 0, 0 },
    };
    static InternTable table;
    static Preprocessor units[2];
    units[0] = (Preprocessor){ 0 };
    units[1] = (Preprocessor){ 0 };
    bool result = intern_initialize(&table);
    for (uint32_t u = 0; result && u < 2; u += 1)
    {
        PPOptions options = *base;
        options.files = u == 0 ? 0 : units[0].cache;
        Preprocessor* pp = &units[u];
        result = pp_initialize(pp, &options, &table);
        uint64_t expected_opens = 0;
        uint64_t expected_fstats = 0;
        uint64_t expected_reads = 0;
        uint64_t expected_found = 0;
        for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
        {
            uint32_t found_in = BUSTER_PP_NONE;
            const uint32_t file = pp_find_include(pp, (const uint8_t*)cases[c].name, (uint32_t)strlen(cases[c].name), true, false, BUSTER_PP_NONE,
                                                   BUSTER_PP_NONE, &found_in, BUSTER_PP_NONE);
            expected_opens += u == 0 ? cases[c].opens : 0;
            expected_fstats += u == 0 ? cases[c].fstats : 0;
            expected_reads += u == 0 ? cases[c].reads : 0;
            expected_found += cases[c].found;
            result = (file != BUSTER_PP_NONE) == cases[c].found && found_in == (cases[c].found ? 1u : BUSTER_PP_NONE) &&
                     pp->probes.opens == expected_opens && pp->probes.fstats == expected_fstats && pp->probes.reads == expected_reads && pp->probes.maps == 0 &&
                     pp->statistics.include_searches == c + 1 && pp->statistics.include_found == expected_found &&
                     pp->statistics.include_opens == expected_opens && pp->statistics.include_fstats == expected_fstats;
            if (!result)
            {
                printf("lookup accounting: unit %u case %u (%s): opens %lu/%lu, fstats %lu/%lu, reads %lu/%lu, found %u\n", u, c, cases[c].name,
                       (unsigned long)pp->probes.opens, (unsigned long)expected_opens, (unsigned long)pp->probes.fstats, (unsigned long)expected_fstats,
                       (unsigned long)pp->probes.reads, (unsigned long)expected_reads, file != BUSTER_PP_NONE);
            }
        }
        if (result)
        {
            // Too long for any candidate: no system call, even though the search itself is counted.
            uint8_t long_name[PATH_MAX];
            memset(long_name, 'a', sizeof(long_name));
            uint32_t found_in = BUSTER_PP_NONE;
            const uint32_t long_file = pp_find_include(pp, long_name, sizeof(long_name), true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found_in, BUSTER_PP_NONE);
            result = long_file == BUSTER_PP_NONE && pp->probes.opens == expected_opens && pp->probes.fstats == expected_fstats &&
                     pp->statistics.include_searches == BUSTER_ARRAY_LENGTH(cases) + 1 && pp->statistics.include_ticks != 0 && pp->input_tokens == 0 &&
                     pp->cache->prepared_records == 0 && pp->ticks[PP_PHASE_LEX] == 0 && pp->ticks[PP_PHASE_REFINE] == 0 && pp->ticks[PP_PHASE_DIRECTIVES] == 0;
            // A missing main-file path keeps ENOENT and has no fstat, whether first probed or cached by the other unit.
            const uint32_t root = pp_lookup_path(pp, missing_root, (uint32_t)strlen(missing_root), BUSTER_PP_USER_FILE);
            result = result && root != BUSTER_PP_NONE && pp->files[root].data == BUSTER_PP_NONE && pp->files[root].error == ENOENT &&
                     pp->probes.opens == expected_opens + (u == 0 ? 1u : 0u) && pp->probes.fstats == expected_fstats &&
                     pp->statistics.include_opens == expected_opens && pp->statistics.include_fstats == expected_fstats;
            // The cached ELOOP is fatal for a reported search (#include or __has_include), without new I/O. The
            // unreported searches above deliberately treated it as unavailable.
            const uint32_t loop = pp_find_include(pp, (const uint8_t*)"loop.h", 6, true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found_in, 0);
            result = result && loop == BUSTER_PP_NONE && pp->fatal && pp->errors == 1 &&
                     pp->probes.opens == expected_opens + (u == 0 ? 1u : 0u) && pp->probes.fstats == expected_fstats;
        }
    }
    pp_release(&units[1]);
    pp_release(&units[0]);
    intern_release(&table);
    return result;
}

// Real directive/query dispatch must produce the same tokens, locations, history and tables with vector/scalar
// kernels and with timing disabled. The independent fixture counts include failed and repeated cached searches,
// stat calls for every embed query (there is no embed path cache), and setup stats for duplicate/missing directories.
static bool pp_test_lookup_accounting(const char* directory)
{
    char first[4200];
    char second[4200];
    char missing[4200];
    char path[4200];
    char header[4300];
    char alias[4300];
    char folder[4300];
    char loop[4300];
    char bytes[4300];
    char missing_root[4300];
    snprintf(first, sizeof(first), "%s/account-first", directory);
    snprintf(second, sizeof(second), "%s/account-second", directory);
    snprintf(missing, sizeof(missing), "%s/account-missing", directory);
    snprintf(path, sizeof(path), "%s/account.c", directory);
    snprintf(header, sizeof(header), "%s/value.h", second);
    snprintf(alias, sizeof(alias), "%s/alias.h", second);
    snprintf(folder, sizeof(folder), "%s/folder.h", first);
    snprintf(loop, sizeof(loop), "%s/loop.h", first);
    snprintf(bytes, sizeof(bytes), "%s/data.bin", second);
    snprintf(missing_root, sizeof(missing_root), "%s/root.c", missing);
    const char* const directories[] = { first, second, first, missing };
    PPOptions options = {
        .language = {
            .standard = 2023,
        },
        .no_standard_includes = true,
        .include_directories = directories,
        .include_directory_count = BUSTER_ARRAY_LENGTH(directories),
        .embed_directories = directories,
        .embed_directory_count = 2,
        .timing = true,
        .expansion_starts = true,
        .quiet = true,
    };
    static const char source[] =
        "#if __has_include(<absent.h>) || __has_include(<absent.h>)\n#error unexpected header\n#endif\n"
        "#if __has_include(<value.h>)\n#include <value.h>\n#include <value.h>\n#endif\n"
        "#if __has_include(<folder.h>) || __has_include(<other-absent.h>)\n#error unexpected header\n#endif\n"
        "#if __has_embed(<absent.bin>) || __has_embed(<absent.bin>)\n#error unexpected resource\n#endif\n"
        "#if __has_embed(<data.bin>) != 1 || __has_embed(<data.bin>) != 1\n#error missing resource\n#endif\n"
        "#embed <data.bin>\n";
    bool result = mkdir(first, 0700) == 0 && mkdir(second, 0700) == 0 && mkdir(folder, 0700) == 0 && symlink("loop.h", loop) == 0 &&
                  pp_test_write_file(path, source) && pp_test_write_file(header, "#ifndef ACCOUNTED_H\n#define ACCOUNTED_H\n#define ACCOUNTED 7\nint accounted = ACCOUNTED;\n#endif\n") &&
                  link(header, alias) == 0 && pp_test_write_file(bytes, "AB");
    result = result && pp_test_lookup_probes(&options, missing_root);
    // The successful run needs ordinary missing candidates: a symlink loop is fatal even for __has_include, and is
    // tested separately above. Print any unexpected diagnostic from this run rather than hiding its failure's cause.
    options.quiet = false;
    static InternTable tables[3];
    static Preprocessor runs[3];
    for (uint32_t k = 0; result && k < 3; k += 1)
    {
        options.scalar = k == 1;
        options.timing = k != 2;
        Preprocessor* pp = &runs[k];
        result = intern_initialize(&tables[k]) && pp_initialize(pp, &options, &tables[k]) && pp_run(pp, path);
        const PPStatistics* stats = &pp->statistics;
        // Ten file open attempts: the unit, eight include candidates, and the embed's bytes. Four reach fstat: the
        // unit, header, directory and resource. The three files' nonempty bytes are read once each.
        result = result && pp->probes.opens == 10 && pp->probes.fstats == 4 && pp->probes.reads == 3 && pp->probes.maps == 0;
        if (options.timing)
        {
            result = result && stats->directory_stats == 4 && stats->include_searches == 7 && stats->include_found == 3 && stats->include_opens == 8 &&
                     stats->include_fstats == 2 && stats->include_ticks != 0 && stats->embed_searches == 5 && stats->embed_found == 3 && stats->embed_stats == 10 &&
                     stats->embed_ticks != 0;
        }
        else
        {
            result = result && stats->directory_stats == 0 && stats->include_searches == 0 && stats->include_found == 0 && stats->include_opens == 0 &&
                     stats->include_fstats == 0 && stats->include_ticks == 0 && stats->embed_searches == 0 && stats->embed_found == 0 && stats->embed_stats == 0 &&
                     stats->embed_ticks == 0;
            for (uint32_t phase = 0; phase < PP_PHASE_COUNT; phase += 1)
            {
                result = result && pp->ticks[phase] == 0;
            }
        }
        if (!result)
        {
            printf("lookup accounting: run %u: file open/fstat/read %lu/%lu/%lu, include search/found/open/fstat %lu/%lu/%lu/%lu, "
                   "embed search/found/stat %lu/%lu/%lu, setup stat %lu, errors %u\n", k,
                   (unsigned long)pp->probes.opens, (unsigned long)pp->probes.fstats, (unsigned long)pp->probes.reads,
                   (unsigned long)stats->include_searches, (unsigned long)stats->include_found, (unsigned long)stats->include_opens,
                   (unsigned long)stats->include_fstats, (unsigned long)stats->embed_searches, (unsigned long)stats->embed_found,
                   (unsigned long)stats->embed_stats, (unsigned long)stats->directory_stats, pp->errors);
        }
        if (result && k != 0)
        {
            result = pp_same_streams(&runs[0], pp, "lookup accounting") && pp_same_directive_tables(&runs[0], pp, "lookup accounting");
        }
    }
    if (result)
    {
        static const char expected[] = "int accounted = 7; 65, 66";
        PPSpellings ours = { 0 };
        PPSpellings wanted = { 0 };
        result = pp_stream_spellings(&runs[0], &ours) &&
                 pp_text_spellings((const uint8_t*)expected, sizeof(expected) - 1, runs[0].split_kinds, pp_test_lex_flags(&runs[0]), &wanted) &&
                 pp_compare_spellings(&runs[0], &ours, &wanted, "lookup accounting", "wanted");
        arena_release(&ours.arena);
        arena_release(&wanted.arena);
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        pp_release(&runs[k]);
        intern_release(&tables[k]);
    }
    unlink(path);
    unlink(header);
    unlink(alias);
    unlink(loop);
    unlink(bytes);
    rmdir(folder);
    rmdir(first);
    rmdir(second);
    printf("lookup accounting: %s (file and metadata attempts, unit/shared caches, scalar/vector and timing off)\n", result ? "as expected" : "WRONG");
    return result;
}

// Prescanning ID's argument and expanding #if's operand copy O's body into captures, never into the final output.
// Of the nine final tokens, plain/ok are file runs, standalone O is a direct object copy, ID(O) is a buffer run, and
// __LINE__ is the one individually emitted token. O expands directly three times, but only its standalone use copies
// to the final output. The second source has one direct expansion into a capture and no output at all.
// In both sources, pp_start_condition copies O == 3 to the arguments buffer and pp_start_capture runs that buffer.
// After O expands, pp_step_buffer copies the two remaining tokens (== and 3) into the condition capture, which
// pp_finish_condition discards. Thus aggregate buffer work must exceed final buffer copies by at least two tokens.
static bool pp_test_final_copy_accounting(void)
{
    static const char* const sources[] = {
        "plain\n#define O 1 + 2\nO\n#define ID(x) x\nID(O)\n#if O == 3\nok\n#endif\n__LINE__\n",
        "#define O 1 + 2\n#if O == 3\n#endif\n",
    };
    static InternTable tables[3];
    static Preprocessor runs[3];
    bool result = true;
    for (uint32_t s = 0; result && s < BUSTER_ARRAY_LENGTH(sources); s += 1)
    {
        for (uint32_t k = 0; result && k < 3; k += 1)
        {
            const PPOptions options = {
                .language = {
                    .standard = 2017,
                    .gnu = true,
                },
                .scalar = k == 1,
                .no_standard_includes = true,
                .timing = k != 2,
                .expansion_starts = true,
                .quiet = true,
            };
            Preprocessor* pp = &runs[k];
            result = intern_initialize(&tables[k]) && pp_initialize(pp, &options, &tables[k]) &&
                     pp_run_source(pp, "copy-accounting.c", (const uint8_t*)sources[s], (uint32_t)strlen(sources[s]));
            const PPStatistics* stats = &pp->statistics;
            const bool counted = s == 0 && options.timing;
            const uint64_t direct_expansions = options.timing ? (s == 0 ? 3u : 1u) : 0;
            const bool buffer_work = options.timing ? stats->buffer_run_tokens >= stats->final_buffer_run_tokens + 2 : stats->buffer_run_tokens == 0;
            const uint64_t copied = stats->final_file_run_tokens + stats->final_buffer_run_tokens + stats->final_direct_object_tokens;
            result = result && pp->output.count == (s == 0 ? 9u : 0u) && stats->final_file_run_tokens == (counted ? 2u : 0u) &&
                     stats->final_buffer_run_tokens == (counted ? 3u : 0u) && stats->final_direct_object_tokens == (counted ? 3u : 0u) &&
                     copied <= pp->output.count && stats->direct_expansions == direct_expansions && buffer_work &&
                     (!counted || pp->output.count - copied == 1);
            if (!result)
            {
                printf("final copy accounting: source %u run %u: output %u, file/buffer/object copies %lu/%lu/%lu, direct expansions %lu/%lu, buffer work %lu\n", s, k, pp->output.count,
                       (unsigned long)stats->final_file_run_tokens, (unsigned long)stats->final_buffer_run_tokens,
                       (unsigned long)stats->final_direct_object_tokens, (unsigned long)stats->direct_expansions, (unsigned long)direct_expansions,
                       (unsigned long)stats->buffer_run_tokens);
            }
            if (result && k != 0)
            {
                result = pp_same_streams(&runs[0], pp, "final copy accounting") && pp_same_directive_tables(&runs[0], pp, "final copy accounting");
            }
        }
        for (uint32_t k = 0; k < 3; k += 1)
        {
            pp_release(&runs[k]);
            intern_release(&tables[k]);
        }
    }
    printf("final copy accounting: %s (captures excluded, direct objects, empty output, scalar/vector and timing off)\n", result ? "as expected" : "WRONG");
    return result;
}

// An unterminated literal that ends in a line splice is spelled as clang cleans it (pp_trailing_splice): with the
// character after the splice, the newline that ends the literal, or at the file's end the NUL that ends clang's buffer
// (#109's seed 3110). Each source's last token, by both kernels, must be clang's spelling, byte for byte (clang 23's
// -E); the last two keep their own.
static bool pp_test_trailing_splices(void)
{
    typedef struct PPTrailingSplice
    {
        const char* source;
        const char* spelling;
        uint32_t length;
    } PPTrailingSplice;
    static const PPTrailingSplice cases[] = {
        { "'\\\n", "'\0", 2 },
        { "'a\\\n", "'a\0", 3 },
        { "'\\\\\n", "'\\\0", 3 },
        { "'\\ \n", "'\0", 2 },
        { "'\\\r\n", "'\0", 2 },
        { "\"a\\\n\\\n", "\"a\0", 3 },
        { "u8\"\\\n", "u8\"\0", 4 },
        { "x '\\\\\n\\\n", "'\\\0", 3 },
        { "'\\\n\n", "'\n", 2 },
        { "'\\\nx\n", "'x", 2 },
        { "'\\", "'\\", 2 },
    };
    static InternTable tables[2];
    static Preprocessor runs[2];
    uint32_t failures = 0;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        for (uint32_t k = 0; k < 2; k += 1)
        {
            const PPOptions options = {
                .language = {
                    .standard = 2017,
                    .gnu = true,
                },
                .scalar = k == 1,
                .no_standard_includes = true,
                .quiet = true,
            };
            Preprocessor* pp = &runs[k];
            const bool ran = intern_initialize(&tables[k]) && pp_initialize(pp, &options, &tables[k]);
            if (ran)
            {
                pp_run_source(pp, "trailing-splice.c", (const uint8_t*)cases[c].source, (uint32_t)strlen(cases[c].source));
            }
            const PPTokens* out = &pp->output;
            uint8_t buffer[16];
            const uint8_t* spelling = 0;
            const uint32_t last = ran && out->count != 0 ? out->count - 1 : 0;
            const bool fits = ran && out->count != 0 && out->kinds[last] == C_TOKEN_UNKNOWN && out->values[last] - out->locations[last] < sizeof(buffer);
            const uint32_t length = fits ? pp_spelling(pp, out->kinds[last], out->values[last], out->locations[last], buffer, &spelling) : 0;
            if (!fits || length != cases[c].length || memcmp(spelling, cases[c].spelling, length) != 0)
            {
                printf("trailing splices: case %u, %s kernels: spelled with %u bytes, not clang's %u\n", c, k == 0 ? "vectorized" : "scalar", length, cases[c].length);
                failures += 1;
            }
            if (ran)
            {
                pp_release(pp);
            }
            intern_release(&tables[k]);
        }
    }
    printf("trailing splices: %u of %lu runs fail\n", failures, 2 * (unsigned long)BUSTER_ARRAY_LENGTH(cases));
    return failures == 0;
}

// The scalar loop must not need the vector path's cached macro-name bits. Prepare every identifier first, define the
// macros, then make every known name's bit disagree with macro_of before each use file. Repeating this after undef
// and redefine prevents their normal bit updates from hiding a lookup through the cache. The third run selects the
// scalar kernel directly, as the benchmarks do, without changing PPOptions.scalar.
static bool pp_test_scalar_reference(const char* directory)
{
    static const char* const names[] =
    {
        "scalar-definitions.c",
        "scalar-use.c",
        "scalar-changed.c",
    };
    static const char* const sources[] =
    {
        "#define A 7\n#define ALIAS A\n#define ID(x) x\n#define SELF SELF\n#define F(x) x + F(x)\n"
        "#define PLAIN ordinary\n#define ATTR noreturn\n#define CHANGE 1\n#define GONE 2\n#define EMPTY\n"
        "#define POISONED bad\n#pragma GCC poison bad\n",
        "ordinary A ALIAS ID(A) ID(SELF) F(A) PLAIN\n__COUNTER__ __LINE__\n"
        "#if defined(A) && !defined(ordinary) && __has_attribute(ATTR)\ncondition\n#endif\nPOISONED\n"
        "#undef GONE\n#undef CHANGE\n#define CHANGE 3\n#define NEW 4\nGONE CHANGE NEW\n"
        "#embed <scalar-reference.bin> limit(1) prefix(A,) suffix(,ID(CHANGE))\n",
        "A ALIAS ID(CHANGE) GONE NEW SELF __COUNTER__\n#if !defined(GONE) && defined(CHANGE)\nchanged\n#endif\n"
        "#pragma GCC poison later\n__has_c_attribute(x EMPTY)\nbad later\n",
    };
    static const char expected[] =
        "ordinary 7 7 7 SELF 7 + F(7) ordinary 0 2 condition bad GONE 3 4 7, 65, 3\n"
        "7 7 3 GONE 4 SELF 1 changed 0 bad later\n";
    char bytes[4096];
    snprintf(bytes, sizeof(bytes), "%s/scalar-reference.bin", directory);
    const char* directories[] =
    {
        directory,
    };
    static InternTable tables[3];
    static Preprocessor runs[3];
    char* diagnostics[3] = { 0 };
    bool result = pp_test_write_file(bytes, "AB");
    for (uint32_t k = 0; result && k < 3; k += 1)
    {
        size_t length = 0;
        FILE* stream = open_memstream(&diagnostics[k], &length);
        const PPOptions options =
        {
            .language =
            {
                .standard = 2023,
                .gnu = true,
            },
            .scalar = k == 1,
            .no_standard_includes = true,
            .clang_features = true,
            .embed_directories = directories,
            .embed_directory_count = BUSTER_ARRAY_LENGTH(directories),
            .expansion_starts = true,
            .diagnostics = stream,
        };
        Preprocessor* pp = &runs[k];
        result = stream != 0 && intern_initialize(&tables[k]) && pp_initialize(pp, &options, &tables[k]);
        uint32_t files[BUSTER_ARRAY_LENGTH(sources)];
        if (result)
        {
            pp->vectorized = k == 0;
        }
        for (uint32_t s = 0; result && s < BUSTER_ARRAY_LENGTH(sources); s += 1)
        {
            files[s] = pp_virtual_file(pp, names[s], sources[s], (uint32_t)strlen(sources[s]), BUSTER_PP_USER_FILE);
            result = files[s] != BUSTER_PP_NONE && pp_prepare_data(pp, &pp->datas[pp->files[files[s]].data], names[s]);
        }
        result = result && pp_run_prelude(pp);
        if (result)
        {
            pp_run_file(pp, files[0]);
            result = pp->errors == 0 && pp->warnings == 0 && !pp->fatal;
        }
        for (uint32_t s = 1; result && s < BUSTER_ARRAY_LENGTH(sources); s += 1)
        {
            if (!pp->vectorized)
            {
                for (IdentId id = 0; id < pp->table->count; id += 1)
                {
                    const uint64_t bit = 1ull << (id % 64);
                    if (pp->macro_of[id] != 0)
                    {
                        pp->is_macro[id / 64] &= ~bit;
                    }
                    else
                    {
                        pp->is_macro[id / 64] |= bit;
                    }
                }
            }
            const bool ran = pp_run_main(pp, files[s]);
            // EMPTY must remain unexpanded after the scoped query's first name, making its operand invalid. The
            // preexisting poisoned sentinel `bad` and newly poisoned `later` each diagnose one use on the last line.
            result = ran == (s == 1) && pp->errors == (s == 1 ? 0u : 3u) && pp->warnings == 0 && !pp->fatal;
            if (!result)
            {
                printf("scalar reference: run %u file %u: ran %u, errors %u, warnings %u, fatal %u\n", k, s, ran, pp->errors, pp->warnings, pp->fatal);
            }
        }
        if (stream != 0)
        {
            fclose(stream);
        }
        result = result && diagnostics[k] != 0 && strstr(diagnostics[k], "builtin feature check macro requires a parenthesized identifier") != 0 &&
                 strstr(diagnostics[k], "attempt to use a poisoned identifier") != 0;
        if (result && k != 0)
        {
            result = pp_same_streams(&runs[0], pp, "scalar reference") && pp_same_directive_tables(&runs[0], pp, "scalar reference") &&
                     strcmp(diagnostics[0], diagnostics[k]) == 0;
        }
    }
    if (result)
    {
        PPSpellings ours = { 0 };
        PPSpellings wanted = { 0 };
        result = pp_stream_spellings(&runs[0], &ours) &&
                 pp_text_spellings((const uint8_t*)expected, sizeof(expected) - 1, runs[0].split_kinds, pp_test_lex_flags(&runs[0]), &wanted) &&
                 pp_compare_spellings(&runs[0], &ours, &wanted, "scalar reference", "wanted");
        arena_release(&ours.arena);
        arena_release(&wanted.arena);
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        if (!result && diagnostics[k] != 0)
        {
            printf("scalar reference run %u diagnostics:\n%s", k, diagnostics[k]);
        }
        free(diagnostics[k]);
        pp_release(&runs[k]);
        intern_release(&tables[k]);
    }
    unlink(bytes);
    printf("scalar reference: %s (inverted macro-name bits, definitions, captures, builtins, poison and kernel override)\n", result ? "as expected" : "WRONG");
    return result;
}

// c_predefines_test.c's: drops the -dM lines of clang's patch release's version macros and of __BUSTER__.
static void predefines_test_drop_versions(char* text);

// The macros defined at the end of a file's translation unit (#121), with our own predefines for the options: -dM
// against `clang -dM -E` with the same arguments, byte for byte but for the version macros of clang's patch release
// and our __BUSTER__. So the whole table is clang's, the predefines and every definition and #undef.
static bool pp_test_macros(int argument_count, char** arguments, const PPOptions* base, const char* path)
{
    PPOptions options = *base;
    options.predefines_path = 0;
    static InternTable table;
    static Preprocessor pp;
    char* ours = 0;
    size_t ours_length = 0;
    FILE* stream = open_memstream(&ours, &ours_length);
    bool result = stream != 0 && intern_initialize(&table);
    if (result)
    {
        result = pp_initialize(&pp, &options, &table) && pp_run(&pp, path) && pp_print_macros(&pp, stream);
        pp_release(&pp);
        intern_release(&table);
    }
    if (stream != 0)
    {
        fclose(stream);
    }
    PPClangOutput clang = result ? pp_test_clang_process("clang", arguments, argument_count, path, true) : (PPClangOutput){ 0 };
    char* theirs = clang.status == PP_CLANG_ACCEPTED ? clang.text : 0;
    result = theirs != 0 && ours != 0;
    if (result)
    {
        predefines_test_drop_versions(ours);
        predefines_test_drop_versions(theirs);
        result = strcmp(ours, theirs) == 0;
    }
    if (!result && ours != 0 && theirs != 0)
    {
        // The first line that differs.
        uint32_t line = 1;
        size_t at = 0;
        for (; ours[at] != 0 && ours[at] == theirs[at]; at += 1)
        {
            line += ours[at] == '\n';
        }
        const char* our_line = ours + at;
        const char* their_line = theirs + at;
        while (our_line > ours && our_line[-1] != '\n')
        {
            our_line -= 1;
            their_line -= 1;
        }
        printf("%s: -dM differs from clang's at line %u:\n  ours:  %.*s\n  clang: %.*s\n", path, line, (int)strcspn(our_line, "\n"), our_line,
               (int)strcspn(their_line, "\n"), their_line);
    }
    else if (!result)
    {
        printf("%s: -dM failed (ours %s, clang %s)\n", path, ours != 0 ? "ran" : "failed", theirs != 0 ? "ran" : "failed");
    }
    free(ours);
    free(clang.text);
    free(clang.diagnostics);
    return result;
}

// Assembly's printed bytes matter to GNU as even when a C token comparison treats the output as equivalent.
static bool pp_test_assembly_printing(const char* directory)
{
    static const char adjacency[] = "#define D() $\n#define F() foo\n#define N() 1\nD()foo\nD()7\nF()$\nN()$\nD()D()\n";
    static const struct
    {
        const char* name;
        const char* source;
        const char* expected;
        bool assembly;
        // An option after the mode's, or null: where '$' is punctuation follows -f[no-]dollars-in-identifiers (#281).
        const char* option;
    } cases[] = {
        {
            "assembly immediates and dollar symbols",
            "#define FOO 4\nmovl $FOO, %eax\nfoo$bar: jmp foo$bar\n",
            "movl $4, %eax\nfoo$bar: jmp foo$bar\n",
            true,
        },
        {
            "assembly macro arguments separated by whitespace",
            "#define V 5\nload $V %eax\n",
            "load $5 %eax\n",
            true,
        },
        {
            "assembly explicit whitespace around dollars",
            "$ FOO\nFOO $\n$/**/FOO\nFOO/**/$\n",
            "$ FOO\nFOO $\n$ FOO\nFOO $\n",
            true,
        },
        {
            "assembly dollar adjacency after macro expansion",
            adjacency,
            "$foo\n$7\nfoo$\n1$\n$$\n",
            true,
        },
        {
            "C dollar tokens still need separation",
            adjacency,
            "$ foo\n$ 7\nfoo $\n1 $\n$ $\n",
            false,
        },
        {
            "C with '$' as punctuation prints it as assembly",
            adjacency,
            "$foo\n$7\nfoo$\n1$\n$$\n",
            false,
            "-fno-dollars-in-identifiers",
        },
        {
            "assembly with '$' in identifiers separates it as C",
            adjacency,
            "$ foo\n$ 7\nfoo $\n1 $\n$ $\n",
            true,
            "-fdollars-in-identifiers",
        },
        {
            "C pragma identifiers stop at dollar punctuation",
            "#pragma ppf a$b\n",
            "#pragma ppf a$b\n",
            false,
            "-fno-dollars-in-identifiers",
        },
        {
            "assembly pragma identifiers stop at dollar punctuation",
            "#pragma ppf a$b\n",
            "#pragma ppf a$b\n",
            true,
        },
    };
    // Put '$' before, at and after each side of a 64-token boundary, including a partial final chunk.
    static const uint32_t padding[] = { 0, 62, 63, 64, 126, 127, 128 };
    uint32_t failures = 0;
    char path[4096];
    snprintf(path, sizeof(path), "%s/dollar-printing.S", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases) + BUSTER_ARRAY_LENGTH(padding); i += 1)
    {
        const bool boundary = i >= BUSTER_ARRAY_LENGTH(cases);
        const uint32_t prefix = boundary ? padding[i - BUSTER_ARRAY_LENGTH(cases)] : 0;
        char padded[256];
        memset(padded, ';', prefix);
        memcpy(padded + prefix, "$tail\n", sizeof("$tail\n"));
        const char* name = boundary ? "assembly dollars at token boundaries" : cases[i].name;
        const char* source = boundary ? padded : cases[i].source;
        const char* expected = boundary ? padded : cases[i].expected;
        const bool assembly = boundary || cases[i].assembly;
        const char* option = boundary ? 0 : cases[i].option;
        char* arguments[] = { "-std=gnu17", "-x", assembly ? "assembler-with-cpp" : "c", option != 0 ? (char*)option : path, path };
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        const int argument_count = (int)BUSTER_ARRAY_LENGTH(arguments) - (option == 0);
        bool passed = pp_test_write_file(path, source) && pp_parse_options(argument_count, arguments, &options, &arena, &input, &output, &markers);
        static InternTable tables[2];
        static Preprocessor runs[2];
        bool ran[2] = { false, false };
        for (uint32_t kernel = 0; passed && kernel < 2; kernel += 1)
        {
            options.scalar = kernel != 0;
            ran[kernel] = pp_test_run(&runs[kernel], &tables[kernel], &options, path);
        }
        passed = passed && ran[0] && ran[1] && pp_same_streams(&runs[0], &runs[1], name) && pp_same_directive_tables(&runs[0], &runs[1], name);
        for (uint32_t kernel = 0; passed && kernel < 2; kernel += 1)
        {
            for (uint32_t marking = 0; passed && marking < 2; marking += 1)
            {
                size_t vector_length = 0;
                size_t scalar_length = 0;
                char* vector_text = pp_test_print(&runs[kernel], true, marking != 0, &vector_length);
                char* scalar_text = pp_test_print(&runs[kernel], false, marking != 0, &scalar_length);
                passed = vector_text != 0 && scalar_text != 0 && vector_length == scalar_length && memcmp(vector_text, scalar_text, vector_length) == 0;
                passed = passed && (marking != 0 || (vector_length == strlen(expected) && memcmp(vector_text, expected, vector_length) == 0));
                if (!passed)
                {
                    printf("%s: dollar-printing case %u (%u leading tokens), %s kernels%s differs\n", name, i, prefix,
                           kernel == 0 ? "vectorized" : "scalar", marking != 0 ? " with line markers" : "");
                    if (marking == 0 && vector_text != 0)
                    {
                        printf("  ours: %s  wanted: %s", vector_text, expected);
                    }
                }
                free(vector_text);
                free(scalar_text);
            }
        }
        failures += !passed;
        for (uint32_t kernel = 0; kernel < 2; kernel += 1)
        {
            pp_release(&runs[kernel]);
            intern_release(&tables[kernel]);
        }
        arena_release(&arena);
    }
    unlink(path);
    printf("dollar printing: %u of %lu cases fail\n", failures, (unsigned long)(BUSTER_ARRAY_LENGTH(cases) + BUSTER_ARRAY_LENGTH(padding)));
    return failures == 0;
}

static int64_t pp_directive_test_clang_guards(char** arguments, int argument_count, const char* path);

// One accepted file, by both kernels, and (when supplied) against Clang's already-collected output. Adds to *guards
// (if it isn't null) the inclusions the run skipped by their guards, which pp_test_file compares with clang's (#120).
static bool pp_test_file_output(const PPOptions* base, const char* path, bool with_clang, const char* text, size_t length, uint64_t* tokens, uint64_t* guards)
{
    PPOptions options = *base;
    static InternTable tables[2];
    static Preprocessor runs[2];
    bool ran[2];
    for (uint32_t k = 0; k < 2; k += 1)
    {
        options.scalar = k == 1;
        ran[k] = pp_test_run(&runs[k], &tables[k], &options, path);
    }
    bool result = ran[0] && ran[1] && pp_same_streams(&runs[0], &runs[1], path) && pp_same_directive_tables(&runs[0], &runs[1], path) &&
                  pp_test_compile_kernels(&options, path, path);
    *tokens += runs[0].output.count;
    PPSpellings ours = { 0 };
    result = result && pp_stream_spellings(&runs[0], &ours) && pp_test_printed(&runs[0], &ours, path);
    if (result && with_clang)
    {
        PPPlaced theirs = { 0 };
        result = text != 0 && pp_placed_tokens((const uint8_t*)text, length, runs[0].split_kinds, pp_test_lex_flags(&runs[0]), &theirs) && pp_compare_spellings(&runs[0], &ours, &theirs.spellings, path, "clang") &&
                 pp_test_placed(&runs[0], &theirs, path);
        pp_placed_release(&theirs);
    }
    if (guards != 0)
    {
        *guards += runs[0].statistics.skipped_guards;
    }
    arena_release(&ours.arena);
    for (uint32_t k = 0; k < 2; k += 1)
    {
        pp_release(&runs[k]);
        intern_release(&tables[k]);
    }
    return result;
}

// `bytes` (`length` of them) as clang writes a file's name in a line marker (raw_ostream::write_escaped, as
// pp_print_marker_line writes it: \\, \", \t, \n, and a byte outside ' ' to '~' as three octal digits), into `out` (room
// for 4 times the length). Returns the escaped length.
static size_t pp_test_escaped_name(const char* bytes, size_t length, char* out)
{
    size_t n = 0;
    for (size_t i = 0; i < length; i += 1)
    {
        const uint8_t c = (uint8_t)bytes[i];
        const char escape = c == '\\' ? '\\' : c == '"' ? '"' : c == '\t' ? 't' : c == '\n' ? 'n' : 0;
        if (escape != 0)
        {
            out[n] = '\\';
            out[n + 1] = escape;
            n += 2;
        }
        else if (c >= ' ' && c <= '~')
        {
            out[n] = (char)c;
            n += 1;
        }
        else
        {
            out[n] = '\\';
            out[n + 1] = (char)('0' + (c >> 6));
            out[n + 2] = (char)('0' + ((c >> 3) & 7));
            out[n + 3] = (char)('0' + (c & 7));
            n += 4;
        }
    }
    return n;
}

// `*text` (`*length` bytes) with each `from` (`from_length` bytes, not 0) in it replaced by `to`. Returns whether it
// could; `*text` is then a new text.
static bool pp_test_replace(char** text, size_t* length, const char* from, size_t from_length, const char* to, size_t to_length)
{
    const char* end = *text + *length;
    size_t count = 0;
    for (const char* at = memmem(*text, *length, from, from_length); at != 0; at = memmem(at + from_length, (size_t)(end - at) - from_length, from, from_length))
    {
        count += 1;
    }
    char* replaced = malloc(*length + count * to_length + 1);
    const bool result = replaced != 0;
    const char* rest = *text;
    size_t written = 0;
    for (const char* at = result ? memmem(rest, *length, from, from_length) : 0; at != 0; at = memmem(rest, (size_t)(end - rest), from, from_length))
    {
        memcpy(replaced + written, rest, (size_t)(at - rest));
        written += (size_t)(at - rest);
        memcpy(replaced + written, to, to_length);
        written += to_length;
        rest = at + from_length;
    }
    if (result)
    {
        memcpy(replaced + written, rest, (size_t)(end - rest));
        written += (size_t)(end - rest);
        replaced[written] = 0;
        free(*text);
        *text = replaced;
        *length = written;
    }
    return result;
}

// clang's text (`*text`, `*length` bytes) with the files it found through pp_clang_arguments' links (in `links`) named as
// the files they lead to, in clang's resource directory, where ours finds them with --buster-headers: in its line
// markers, which escape the names (pp_test_escaped_name), as the links' directory is under $TMPDIR, whose name can hold
// any byte (#255's review). None of clang 23's headers uses __FILE__, the other place its text would name one. Returns
// whether it could; `*text` is then a new text.
static bool pp_test_resource_names(char** text, size_t* length, const char* links)
{
    char resource[PATH_MAX];
    const bool found = pp_clang_resource_directory(resource, sizeof(resource), 0);
    const size_t links_length = strlen(links);
    const size_t resource_length = found ? strlen(resource) : 0;
    char* names = found && links_length != 0 ? malloc(4 * (links_length + resource_length)) : 0;
    char* to = names != 0 ? names + 4 * links_length : 0;
    const size_t from_length = names != 0 ? pp_test_escaped_name(links, links_length, names) : 0;
    const size_t to_length = names != 0 ? pp_test_escaped_name(resource, resource_length, to) : 0;
    const bool result = names != 0 && pp_test_replace(text, length, names, from_length, to, to_length);
    free(names);
    return result;
}

// One of test-preprocessor's files (pp_test_files): by both kernels, and with `with_clang` against clang with
// `arguments` (pp_clang_arguments, whose links are in `links`, or null without them), whose predefines ours reads.
static bool pp_test_file(int argument_count, char** arguments, const char* links, const PPOptions* base, const char* path, bool with_clang, const char* directory,
                         bool predefined, uint64_t* tokens)
{
    PPOptions options = *base;
    char predefines[4096];
    snprintf(predefines, sizeof(predefines), "%s/predefines.h", directory);
    size_t length = 0;
    // With `predefined`, clang's predefines for the options are in the directory already (pp_test_files writes them once
    // for its files, which share the options).
    char* text = with_clang ? pp_test_clang(arguments, argument_count, path, predefined ? 0 : predefines, &length) : 0;
    const bool named = text == 0 || links == 0 || pp_test_resource_names(&text, &length, links);
    options.predefines_path = with_clang ? predefines : options.predefines_path;
    uint64_t guards = 0;
    bool result = (!with_clang || text != 0) && named && pp_test_file_output(&options, path, with_clang, text, length, tokens, &guards);
    const int64_t clang_guards = result && with_clang ? pp_directive_test_clang_guards(arguments, argument_count, path) : -1;
    if (result && with_clang && clang_guards != (int64_t)guards)
    {
        printf("%s: %lu inclusions skipped by their guards, clang %ld\n", path, (unsigned long)guards, (long)clang_guards);
        result = false;
    }
    // Then, with our own predefines, the macros at the file's end against clang's.
    result = result && (!with_clang || pp_test_macros(argument_count, arguments, base, path));
    free(text);
    return result;
}

static uint32_t pp_test_files(int passed_count, char** passed, int file_count, char** files, bool with_clang, const char* directory, uint64_t* tokens);

static int pp_test_compare_lines(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

// The #define lines of a predefines text, sorted, without the macros that name clang's patch release (which the 9700X's
// and the generator's clang differ in) and our own __BUSTER__: `lines` gets pointers into `text`, whose newlines become
// NULs. Returns the count.
static uint32_t pp_test_predefine_lines(char* text, size_t length, char** lines, uint32_t capacity)
{
    static const char* const skipped[] = { "#define __clang_patchlevel__ ", "#define __clang_version__ ", "#define __VERSION__ ", "#define __BUSTER__ " };
    uint32_t count = 0;
    char* line = text;
    for (size_t i = 0; i < length; i += 1)
    {
        if (text[i] == '\n')
        {
            // clang writes a space after the name of a macro without a body: lines are compared without trailing spaces.
            text[i] = 0;
            for (size_t k = i; k > (size_t)(line - text) && text[k - 1] == ' '; k -= 1)
            {
                text[k - 1] = 0;
            }
            // Only the definitions: the predefines also push two of them (#pragma push_macro), which -dM doesn't list.
            bool kept = strncmp(line, "#define ", 8) == 0;
            for (uint32_t k = 0; kept && k < BUSTER_ARRAY_LENGTH(skipped); k += 1)
            {
                kept = strncmp(line, skipped[k], strlen(skipped[k])) != 0;
            }
            if (kept && count < capacity)
            {
                lines[count] = line;
                count += 1;
            }
            line = text + i + 1;
        }
    }
    qsort(lines, count, sizeof(lines[0]), pp_test_compare_lines);
    return count;
}

#define BUSTER_PP_TEST_MAX_PREDEFINES 1024

// The built-in predefines for each language mode, and with -ffreestanding and -O2, must be clang's (`clang -dM -E` with
// the same options) line for line: the cases and the files under --clang read clang's own, so only this checks ours.
static bool pp_test_predefines(const char* directory)
{
    static const char* const modes[][2] = {
        { "-std=c89", 0 }, { "-std=gnu89", 0 }, { "-std=c99", 0 }, { "-std=gnu99", 0 }, { "-std=c11", 0 }, { "-std=gnu11", 0 },
        { "-std=c17", 0 }, { "-std=gnu17", 0 }, { "-std=c23", 0 }, { "-std=gnu23", 0 }, { "-ffreestanding", 0 }, { "-O2", 0 },
        { "-std=iso9899:199409", 0 }, { "-std=c89", "-fdigraphs" }, { "-std=iso9899:199409", "-fno-digraphs" }, { "-std=gnu89", "-fno-digraphs" },
        { "-std=c99", "-ffreestanding" }, { "-std=c23", "-O2" },
        { "-march=x86-64", 0 },
        { "-march=x86-64-v2", 0 },
        { "-march=x86-64-v3", 0 },
        { "-march=x86-64-v4", 0 },
        { "-march=znver5", 0 },
        { "-march=native", 0 },
        { "-march=x86-64", "-mtune=znver5" },
        { "-march=znver5", "-mtune=generic" },
    };
    uint32_t failures = 0;
    char clang_path[4096];
    char empty_path[4096];
    snprintf(clang_path, sizeof(clang_path), "%s/predefines.h", directory);
    snprintf(empty_path, sizeof(empty_path), "%s/empty.c", directory);
    pp_test_write_file(empty_path, "");
    for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(modes); m += 1)
    {
        char* arguments[3] = { (char*)modes[m][0], (char*)modes[m][1], 0 };
        const int count = modes[m][1] != 0 ? 2 : 1;
        arguments[count] = empty_path;
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        bool passed = pp_parse_options(count + 1, arguments, &options, &arena, &input, &output, &markers);
        char* ours = malloc(BUSTER_PP_PREDEFINES_BYTES);
        uint32_t flt_eval_method;
        const uint32_t made = passed && ours != 0 ? pp_predefines(&options, ours, &flt_eval_method) : BUSTER_PP_PREDEFINES_BYTES + 1;
        passed &= made <= BUSTER_PP_PREDEFINES_BYTES;
        size_t ours_length = passed ? made : 0;
        size_t clang_length = 0;
        free(pp_test_clang(arguments, count, empty_path, clang_path, &clang_length));
        FILE* file = fopen(clang_path, "r");
        char* theirs = malloc(1 << 20);
        clang_length = file != 0 && theirs != 0 ? fread(theirs, 1, (1 << 20) - 1, file) : 0;
        if (file != 0)
        {
            fclose(file);
        }
        static char* our_lines[BUSTER_PP_TEST_MAX_PREDEFINES];
        static char* their_lines[BUSTER_PP_TEST_MAX_PREDEFINES];
        const uint32_t our_count = ours != 0 ? pp_test_predefine_lines(ours, ours_length, our_lines, BUSTER_PP_TEST_MAX_PREDEFINES) : 0;
        const uint32_t their_count = theirs != 0 ? pp_test_predefine_lines(theirs, clang_length, their_lines, BUSTER_PP_TEST_MAX_PREDEFINES) : 0;
        passed = passed && their_count != 0 && our_count == their_count;
        uint32_t reported = 0;
        for (uint32_t i = 0, j = 0; (i < our_count || j < their_count) && reported < 4;)
        {
            const int order = i == our_count ? 1 : j == their_count ? -1 : strcmp(our_lines[i], their_lines[j]);
            if (order != 0)
            {
                printf("predefines %s%s%s: %s %s\n", modes[m][0], modes[m][1] != 0 ? " " : "", modes[m][1] != 0 ? modes[m][1] : "", order < 0 ? "only ours:" : "only clang's:",
                       order < 0 ? our_lines[i] : their_lines[j]);
                reported += 1;
                passed = false;
            }
            i += order <= 0;
            j += order >= 0;
        }
        failures += !passed;
        free(ours);
        free(theirs);
        arena_release(&arena);
    }
    unlink(clang_path);
    unlink(empty_path);
    printf("predefines: %u of %lu option sets differ from clang -dM\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(modes));
    return failures == 0;
}

// Clang's keyword candidates, chosen apart from BUSTER_C_NAMES so that a keyword missing from it still fails
// pp_test_keywords: every spelling of a keyword, alias or type trait in clang 23's TokenKinds.def,
// TransformTypeTraits.def, OpenCLImageTypes.def and HLSLIntangibleTypes.def, and the ARM keyword attributes of its
// RegularKeywordAttrInfo.inc (names only, separated by spaces). Most are keywords only in other languages.
static const char pp_test_keyword_candidates[] =
    "_Accum _Alignas _Alignof _Atomic _BitInt _Bool _Complex _Countof _Decimal128 _Decimal32 _Decimal64 _Defer _ExtInt _Float16 _Fract _Generic "
    "_Imaginary _Nonnull _Noreturn _Null_unspecified _Nullable _Nullable_result _Sat _Static_assert _Thread_local __FUNCDNAME__ __FUNCSIG__ "
    "__FUNCTION__ __PRETTY_FUNCTION__ __alignof __alignof__ __arm_agnostic __arm_in __arm_inout __arm_locally_streaming __arm_new __arm_out "
    "__arm_preserves __arm_streaming __arm_streaming_compatible __array_extent __array_rank __asm __asm__ __attribute __attribute__ __auto_type "
    "__bf16 __bool __bridge __bridge_retain __bridge_retained __bridge_transfer __builtin_COLUMN __builtin_FILE __builtin_FILE_NAME "
    "__builtin_FUNCSIG __builtin_FUNCTION __builtin_LINE __builtin_alignof __builtin_astype __builtin_available __builtin_bit_cast "
    "__builtin_choose_expr __builtin_convertvector __builtin_ge_synthesizes_from_spaceship __builtin_gt_synthesizes_from_spaceship "
    "__builtin_hlsl_is_constant_buffer_element_compatible __builtin_hlsl_is_intangible __builtin_hlsl_is_scalarized_layout_compatible "
    "__builtin_hlsl_is_typed_resource_element_compatible __builtin_is_cpp_trivially_relocatable __builtin_is_implicit_lifetime "
    "__builtin_is_virtual_base_of __builtin_le_synthesizes_from_spaceship __builtin_lt_synthesizes_from_spaceship __builtin_offsetof "
    "__builtin_omp_required_simd_align __builtin_ptrauth_type_discriminator __builtin_source_location __builtin_structured_binding_size "
    "__builtin_sycl_unique_stable_name __builtin_types_compatible_p __builtin_va_arg __builtin_vectorelements __can_pass_in_regs __cdecl "
    "__char16_t __char32_t __complex __complex__ __const __const__ __constant __contravariant __covariant __datasizeof __declspec __decltype "
    "__extension__ __fastcall __finally __float128 __forceinline __fp16 __func__ __funcref __generic __global __has_nothrow_assign "
    "__has_nothrow_constructor __has_nothrow_copy __has_nothrow_move_assign __has_trivial_assign __has_trivial_constructor __has_trivial_copy "
    "__has_trivial_destructor __has_trivial_move_assign __has_trivial_move_constructor __has_unique_object_representations "
    "__has_virtual_destructor __ibm128 __if_exists __if_not_exists __imag __imag__ __include_macros __inline __inline__ __int128 __int16 __int32 "
    "__int64 __int8 __interface __is_abstract __is_aggregate __is_arithmetic __is_array __is_assignable __is_base_of __is_bitwise_cloneable "
    "__is_bounded_array __is_class __is_complete_type __is_compound __is_const __is_constructible __is_convertible __is_convertible_to "
    "__is_destructible __is_empty __is_enum __is_final __is_floating_point __is_function __is_fundamental __is_integral __is_interface_class "
    "__is_layout_compatible __is_literal __is_literal_type __is_lvalue_expr __is_lvalue_reference __is_member_function_pointer "
    "__is_member_object_pointer __is_member_pointer __is_nothrow_assignable __is_nothrow_constructible __is_nothrow_convertible "
    "__is_nothrow_destructible __is_object __is_pod __is_pointer __is_pointer_interconvertible_base_of __is_polymorphic __is_reference "
    "__is_rvalue_expr __is_rvalue_reference __is_same __is_same_as __is_scalar __is_scoped_enum __is_sealed __is_signed __is_standard_layout "
    "__is_trivial __is_trivially_assignable __is_trivially_constructible __is_trivially_copyable __is_trivially_destructible "
    "__is_trivially_equality_comparable __is_trivially_relocatable __is_unbounded_array __is_union __is_unsigned __is_void __is_volatile "
    "__kernel __kindof __label__ __leave __local __module_private__ __multiple_inheritance __noinline__ __null __nullptr __ob_trap __ob_wrap "
    "__objc_no __objc_yes __pascal __pixel __preprocessed_import __preprocessed_module __private __private_extern__ __private_macro __ptr32 "
    "__ptr64 __ptrauth __public_macro __read_only __read_write __real __real__ __reference_binds_to_temporary "
    "__reference_constructs_from_temporary __reference_converts_from_temporary __regcall __restrict __restrict__ __signed __signed__ "
    "__single_inheritance __sptr __stdcall __super __thiscall __thread __try __typeof __typeof__ __typeof_unqual __typeof_unqual__ __unaligned "
    "__unknown_anytype __uptr __uuidof __vector __vectorcall __virtual_inheritance __volatile __volatile__ __w64 __wchar_t __write_only _alignof "
    "_asm _cdecl _declspec _fastcall _finally _forceinline _inline _int16 _int32 _int64 _int8 _leave _multiple_inheritance _pascal _ptr32 _ptr64 "
    "_restrict _stdcall _thiscall _try _unaligned _uptr _uuidof _vectorcall _virtual_inheritance _w64 addrspace_cast alignas alignof amp ampamp "
    "ampequal and and_eq arrow arrowstar asm assert at attr_openmp auto autoreleasepool available binary_data bitand bitor bool break caret "
    "caretcaret caretequal case catch cbuffer char char16_t char32_t char8_t char_constant class co_await co_return co_yield code_completion "
    "colon coloncolon column_major comma comment compatibility_alias compl concept const const_cast constant consteval constexpr constinit "
    "continue cxxscope decltype default define defined defs delete do double double_t dynamic dynamic_cast elif elifdef elifndef ellipsis else "
    "embed encode end endif enum eod eof equal equalequal error exclaim exclaimequal explicit export extern false finally float float_t for "
    "friend generic global goto greater greaterequal greatergreater greatergreaterequal greatergreatergreater groupshared half hash hashat "
    "hashhash header_name header_unit ident identifier if ifdef ifndef implementation import in include include_next inline inout int interface "
    "jmp_buf kernel l_brace l_paren l_square less lessequal lessless lesslessequal lesslessless line local long minus minusequal minusminus "
    "module module_begin module_end module_include module_name mutable namespace new noexcept non_type non_type_dependent non_type_undeclared "
    "not not_eq not_keyword not_notable nullptr numeric_constant operator optional or or_eq out overload_set pack_indexing_type package percent "
    "percentequal period periodstar pipe pipeequal pipepipe plus plusequal plusplus pragma pragma_align pragma_attribute pragma_captured "
    "pragma_cx_limited_range pragma_dump pragma_export pragma_fenv_access pragma_fenv_access_ms pragma_fenv_round pragma_float_control pragma_fp "
    "pragma_fp_contract pragma_loop_hint pragma_ms_pointers_to_members pragma_ms_pragma pragma_ms_vtordisp pragma_msstruct pragma_openacc "
    "pragma_openacc_end pragma_opencl_extension pragma_openmp pragma_openmp_end pragma_pack pragma_parser_crash pragma_redefine_extname "
    "pragma_riscv pragma_unused pragma_vis pragma_weak pragma_weakalias primary_expr private property protected protocol public question r_brace "
    "r_paren r_square raw_identifier read_only read_write register reinterpret_cast repl_input_end required requires restrict return row_major "
    "sccs selector semi short sigjmp_buf signed sizeof slash slashequal spaceship star starequal static static_assert static_cast string_literal "
    "struct switch synchronized synthesize tbuffer template template_id this thread_local throw tilde true try typedef typeid typename typeof "
    "typeof_unqual ucontext_t unassert undef union unknown unsigned using utf16_char_constant utf16_string_literal utf32_char_constant "
    "utf32_string_literal utf8_char_constant utf8_string_literal vec_step virtual void volatile warning wchar_t while wide_char_constant "
    "wide_string_literal write_only xor xor_eq ";

// Clang's feature names, for __has_feature and __has_extension: every FEATURE and EXTENSION of clang 23's Features.def
// (names only, separated by spaces).
static const char pp_test_feature_names[] =
    "address_sanitizer alignment_sanitizer arc_cf_code_audited array_bounds_sanitizer assume_nonnull attribute_analyzer_noreturn "
    "attribute_availability attribute_availability_app_extension attribute_availability_driverkit attribute_availability_in_templates "
    "attribute_availability_swift attribute_availability_tvos attribute_availability_watchos attribute_availability_with_message "
    "attribute_availability_with_replacement attribute_availability_with_strict attribute_availability_with_version_underscores "
    "attribute_cf_consumed attribute_cf_returns_not_retained attribute_cf_returns_on_parameters attribute_cf_returns_retained "
    "attribute_deprecated_with_message attribute_deprecated_with_replacement attribute_diagnose_if_objc attribute_ext_vector_type "
    "attribute_ns_consumed attribute_ns_consumes_self attribute_ns_returns_not_retained attribute_ns_returns_retained attribute_objc_ivar_unused "
    "attribute_objc_method_family attribute_overloadable attribute_unavailable_with_message attribute_unused_on_fields blocks bool_sanitizer "
    "builtin_sanitizer c_alignas c_alignof c_atomic c_attributes c_countof c_fixed_enum c_generic_selection_with_controlling_type "
    "c_generic_selections c_static_assert c_thread_local c_thread_safety_attributes cfi_cast_strict_sanitizer cfi_derived_cast_sanitizer "
    "cfi_icall_sanitizer cfi_mfcall_sanitizer cfi_nvcall_sanitizer cfi_sanitizer cfi_unrelated_cast_sanitizer cfi_vcall_sanitizer "
    "clang_atomic_attributes coverage_sanitizer cuda_implicit_host_device_templates cuda_noinline_keyword cxx_abi_relative_vtable "
    "cxx_access_control_sfinae cxx_aggregate_nsdmi cxx_alias_templates cxx_alignas cxx_alignof cxx_atomic cxx_attributes "
    "cxx_attributes_on_using_declarations cxx_auto_type cxx_binary_literals cxx_constexpr cxx_constexpr_string_builtins "
    "cxx_contextual_conversions cxx_decltype cxx_decltype_auto cxx_decltype_incomplete_return_types cxx_default_function_template_args "
    "cxx_defaulted_functions cxx_delegating_constructors cxx_deleted_functions cxx_exceptions cxx_explicit_conversions "
    "cxx_explicit_this_parameter cxx_fixed_enum cxx_generalized_initializers cxx_generalized_nttp cxx_generic_lambdas cxx_implicit_moves "
    "cxx_inheriting_constructors cxx_init_captures cxx_inline_namespaces cxx_lambdas cxx_local_type_template_args cxx_noexcept "
    "cxx_nonstatic_member_init cxx_nullptr cxx_override_control cxx_range_for cxx_raw_string_literals cxx_reference_qualified_functions "
    "cxx_relaxed_constexpr cxx_return_type_deduction cxx_rtti cxx_rvalue_references cxx_static_assert cxx_strong_enums cxx_thread_local "
    "cxx_trailing_return cxx_type_aware_allocators cxx_unicode_literals cxx_unrestricted_unions cxx_user_literals cxx_variable_templates "
    "cxx_variadic_templates dataflow_sanitizer datasizeof define_target_os_macros enum_sanitizer enumerator_attributes experimental_library "
    "ext_vector_type_boolean float_cast_overflow_sanitizer function_sanitizer gnu_asm gnu_asm_constexpr_strings gnu_asm_goto_with_outputs "
    "gnu_asm_goto_with_outputs_full has_nothrow_assign has_nothrow_constructor has_nothrow_copy has_trivial_assign has_trivial_constructor "
    "has_trivial_copy has_trivial_destructor has_virtual_destructor hwaddress_sanitizer integer_divide_by_zero_sanitizer is_abstract is_base_of "
    "is_class is_constructible is_convertible_to is_empty is_enum is_final is_literal is_pod is_polymorphic is_sealed is_standard_layout "
    "is_trivial is_trivially_assignable is_trivially_constructible is_trivially_copyable is_union kcfi kcfi_arity leak_sanitizer matrix_types "
    "matrix_types_scalar_division memory_sanitizer memtag_globals memtag_heap memtag_stack modules nonnull_attribute_sanitizer null_sanitizer "
    "nullability nullability_nullable_result nullability_on_arrays nullability_on_classes numerical_stability_sanitizer objc_arc objc_arc_fields "
    "objc_arc_weak objc_arr objc_array_literals objc_bool objc_boxed_expressions objc_boxed_nsvalue_expressions objc_bridge_id "
    "objc_bridge_id_on_typedefs objc_c_static_assert objc_class_property objc_constant_literals objc_cxx_static_assert "
    "objc_default_synthesize_properties objc_dictionary_literals objc_fixed_enum objc_generics objc_generics_variance objc_instancetype "
    "objc_kindof objc_modules objc_nonfragile_abi objc_property_explicit_atomic objc_protocol_qualifier_mangling objc_subscripting "
    "objc_weak_class object_size_sanitizer overflow_behavior_types overloadable_unmarked ownership_holds ownership_returns ownership_takes "
    "pointer_overflow_sanitizer pragma_clang_attribute_external_declaration pragma_clang_attribute_namespaces pragma_stdc_cx_limited_range "
    "ptrauth_calls ptrauth_elf_got ptrauth_function_pointer_type_discrimination ptrauth_indirect_gotos ptrauth_init_fini "
    "ptrauth_init_fini_address_discrimination ptrauth_intrinsics ptrauth_member_function_pointer_type_discrimination ptrauth_objc_interface_sel "
    "ptrauth_objc_isa ptrauth_objc_method_list_pointer ptrauth_objc_signable_class ptrauth_qualifier ptrauth_returns "
    "ptrauth_signed_block_descriptors ptrauth_type_info_vtable_pointer_discrimination ptrauth_vtable_pointer_address_discrimination "
    "ptrauth_vtable_pointer_type_discrimination realtime_sanitizer return_sanitizer returns_nonnull_attribute_sanitizer safe_stack scudo "
    "shadow_call_stack shift_base_sanitizer shift_exponent_sanitizer shift_sanitizer signed_integer_overflow_sanitizer "
    "speculative_load_hardening statement_attributes_with_gnu_syntax swiftasynccc swiftcc thread_sanitizer tls type_sanitizer "
    "undefined_behavior_sanitizer undefined_behavior_sanitizer_finegrained_feature_checks underlying_type unreachable_sanitizer "
    "vla_bound_sanitizer xray_instrument ";

// Writes `S(name) first(name) second(name)` (without `second` when it is null) for each name of a list separated by
// spaces, a line each, to a file that defines S to stringize its argument, so that the output names what it answers for.
static void pp_test_write_names(FILE* file, const char* names, const char* first, const char* second)
{
    const char* at = names;
    while (*at != 0)
    {
        const char* end = strchr(at, ' ');
        const int length = (int)(end != 0 ? end - at : (long)strlen(at));
        fprintf(file, "S(%.*s) %s(%.*s)", length, at, first, length, at);
        if (second != 0)
        {
            fprintf(file, " %s(%.*s)", second, length, at);
        }
        fputc('\n', file);
        at += length + (end != 0);
    }
}

#define BUSTER_PP_TEST_SET_ARGUMENTS 4

// The language modes, as sets of options (null after the last).
static const char* const pp_test_mode_sets[][BUSTER_PP_TEST_SET_ARGUMENTS] = {
    { "-std=c89" }, { "-std=iso9899:199409" }, { "-std=gnu89" }, { "-std=c99" }, { "-std=gnu99" }, { "-std=c11" }, { "-std=gnu11" }, { "-std=c17" }, { "-std=gnu17" },
    { "-std=c23" }, { "-std=gnu23" },
};

// The -f options that change clang's keywords, and those beside them that leave them (#281): GNU's keywords turned on in
// modes without them and off in modes with them; -f[no-]gnu-keywords beside GCC's -f[no-]asm in both orders (it wins:
// clang's driver passes it after them), and each in both orders; each family buster refuses turned off again, which it
// takes, and -fno-zvector and -fchar8_t, which clang ignores in C.
static const char* const pp_test_keyword_sets[][BUSTER_PP_TEST_SET_ARGUMENTS] = {
    { "-std=c89", "-fgnu-keywords" },
    { "-std=gnu89", "-fno-gnu-keywords" },
    { "-std=c17", "-fgnu-keywords" },
    { "-std=gnu17", "-fno-gnu-keywords" },
    { "-std=c23", "-fgnu-keywords" },
    { "-std=gnu23", "-fno-gnu-keywords" },
    { "-std=c89", "-fasm" },
    { "-std=gnu89", "-fno-asm" },
    { "-std=c17", "-fasm", "-fno-gnu-keywords" },
    { "-std=c17", "-fno-gnu-keywords", "-fasm" },
    { "-std=gnu17", "-fgnu-keywords", "-fno-asm" },
    { "-std=gnu17", "-fno-asm", "-fgnu-keywords" },
    { "-std=c17", "-fno-asm", "-fasm" },
    { "-std=gnu17", "-fasm", "-fno-asm" },
    { "-std=c17", "-fno-gnu-keywords", "-fgnu-keywords" },
    { "-std=gnu17", "-fgnu-keywords", "-fno-gnu-keywords" },
    { "-fms-extensions", "-fno-ms-extensions" },
    { "-fno-ms-compatibility" },
    { "-fborland-extensions", "-fno-borland-extensions" },
    { "-fdeclspec", "-fno-declspec" },
    { "-fno-zvector" },
    { "-fzos-extensions", "-fno-zos-extensions" },
    { "-ffixed-point", "-fno-fixed-point" },
    { "-fdefer-ts", "-fno-defer-ts" },
    { "-fchar8_t" },
};

// A file preprocessed with each of `count` sets of options, by both kernels and with clang's predefines for the set,
// must give clang's tokens. Returns how many sets differ.
static uint32_t pp_test_sets(const char* directory, const char* path, const char* label, const char* const (*sets)[BUSTER_PP_TEST_SET_ARGUMENTS], uint32_t count)
{
    uint32_t result = 0;
    for (uint32_t m = 0; m < count; m += 1)
    {
        char* arguments[BUSTER_PP_TEST_SET_ARGUMENTS + 1];
        int n = 0;
        char shown[512];
        uint32_t used = 0;
        for (; n < BUSTER_PP_TEST_SET_ARGUMENTS && sets[m][n] != 0; n += 1)
        {
            arguments[n] = (char*)sets[m][n];
            used += (uint32_t)snprintf(shown + used, sizeof(shown) - used, "%s%s", n != 0 ? " " : "", sets[m][n]);
        }
        arguments[n] = (char*)path;
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        uint64_t tokens = 0;
        bool passed = pp_parse_options(n + 1, arguments, &options, &arena, &input, &output, &markers);
        options.clang_features = true;
        passed = passed && pp_test_file(n, arguments, 0, &options, path, true, directory, false, &tokens);
        if (!passed)
        {
            printf("%s: the answers differ from clang's with %s\n", label, shown);
        }
        result += !passed;
        arena_release(&arena);
    }
    return result;
}

// pp_test_sets in each language mode.
static uint32_t pp_test_modes(const char* directory, const char* path, const char* label)
{
    return pp_test_sets(directory, path, label, pp_test_mode_sets, BUSTER_ARRAY_LENGTH(pp_test_mode_sets));
}

// __is_identifier of every predefined name and of clang's keyword candidates, in each language mode and with each of
// pp_test_keyword_sets, must be clang's: 0 exactly for the mode's keywords.
static bool pp_test_keywords(const char* directory)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/keywords.c", directory);
    FILE* file = fopen(path, "w");
    bool result = file != 0;
    uint32_t count = 0;
    if (result)
    {
        fputs("#define S(x) #x\n", file);
        for (uint32_t name = 1; name < C_NAME_COUNT; name += 1)
        {
            // Not __VA_ARGS__ and __VA_OPT__, which clang warns about outside a macro's body.
            if (name != C_NAME_VA_ARGS && name != C_NAME_VA_OPT)
            {
                fprintf(file, "S(%s) __is_identifier(%s)\n", c_name_spellings[name], c_name_spellings[name]);
                count += 1;
            }
        }
        pp_test_write_names(file, pp_test_keyword_candidates, "__is_identifier", 0);
        fclose(file);
    }
    for (const char* at = pp_test_keyword_candidates; *at != 0; at += 1)
    {
        count += *at == ' ';
    }
    const uint32_t failures = result ? pp_test_modes(directory, path, "keywords") : 1;
    const uint32_t set_failures = result ? pp_test_sets(directory, path, "keywords", pp_test_keyword_sets, BUSTER_ARRAY_LENGTH(pp_test_keyword_sets)) : 1;
    unlink(path);
    printf("keywords: %u of %lu modes and %u of %lu -f option sets differ from clang's __is_identifier (%u names)\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(pp_test_mode_sets), set_failures,
           (unsigned long)BUSTER_ARRAY_LENGTH(pp_test_keyword_sets), count);
    return result && failures == 0 && set_failures == 0;
}

// __has_feature and __has_extension of every feature name, in each language mode, must be clang's: the standard
// features of the mode and the extensions it offers.
static bool pp_test_features(const char* directory)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/features.c", directory);
    FILE* file = fopen(path, "w");
    bool result = file != 0;
    uint32_t count = 0;
    if (result)
    {
        fputs("#define S(x) #x\n", file);
        pp_test_write_names(file, pp_test_feature_names, "__has_feature", "__has_extension");
        fclose(file);
    }
    for (const char* at = pp_test_feature_names; *at != 0; at += 1)
    {
        count += *at == ' ';
    }
    const uint32_t failures = result ? pp_test_modes(directory, path, "features") : 1;
    unlink(path);
    printf("features: %u of %lu modes differ from clang's __has_feature and __has_extension (%u names)\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(pp_test_mode_sets), count);
    return result && failures == 0;
}

// Sends this process's standard error to a temporary file until pp_test_release_stderr, so that refused option sets'
// messages (and the oracle's about clang's) stay out of the log. Returns the descriptor to restore, or -1.
static int pp_test_catch_stderr(FILE** caught)
{
    *caught = tmpfile();
    fflush(stderr);
    int result = *caught != 0 ? dup(STDERR_FILENO) : -1;
    if (result >= 0 && dup2(fileno(*caught), STDERR_FILENO) < 0)
    {
        close(result);
        result = -1;
    }
    return result;
}

// Restores standard error, and puts what was written to it since pp_test_catch_stderr in `messages` (`capacity` bytes,
// the last a NUL).
static void pp_test_release_stderr(int saved, FILE* caught, char* messages, size_t capacity)
{
    fflush(stderr);
    messages[0] = 0;
    if (saved >= 0)
    {
        dup2(saved, STDERR_FILENO);
        close(saved);
        rewind(caught);
        messages[fread(messages, 1, capacity - 1, caught)] = 0;
    }
    if (caught != 0)
    {
        fclose(caught);
    }
}

// The private query view is per inclusion, while cache records and once/guard identity remain canonical. Compare
// a fresh unit-owned cache with a cold and warm shared cache, and retain the shared columns' fingerprints across units.
static bool pp_test_raw_query_views(const char* directory)
{
    const PPTestCase test = {
        .name = "raw query private-view lifetime",
        .source = "#define RAW_QUERY __has_include\n#include \"raw-view.h\"\n#include \"raw-once.h\"\n#include \"raw-once.h\"\n#import \"raw-import.h\"\n#import \"raw-import.h\"\n#include \"raw-guard.h\"\n#include \"raw-guard.h\"\nRAW_BEFORE RAW_AFTER\n",
        .standard = "gnu17",
        .expected = "outer inner 11 22 once imported guarded 11 22",
        .options = "-I%d",
        .files = pp_test_files_raw_query,
    };
    char path[4096];
    bool result = pp_test_write_case(&test, directory, path, sizeof(path));
    char text[16384];
    char* arguments[BUSTER_PP_TEST_MAX_ARGUMENTS + 1];
    const int count = pp_test_arguments(&test, directory, path, text, sizeof(text), arguments);
    Arena arena = arena_reserve(1ull << 20);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    result = result && pp_parse_options(count + 1, arguments, &options, &arena, &input, &output, &markers);
    options.timing = true;
    static InternTable table;
    static Preprocessor run;
    static FileCache cache;
    for (uint32_t kernel = 0; result && kernel < 2; kernel += 1)
    {
        options.scalar = kernel != 0;
        uint64_t hashes[16] = { 0 };
        uint32_t records = 0;
        bool cached = false;
        for (uint32_t pass = 0; result && pass < 3; pass += 1)
        {
            if (pass == 0)
            {
                options.files = 0;
                result = pp_test_run(&run, &table, &options, path);
            }
            else
            {
                if (pass == 1)
                {
                    cached = intern_initialize(&table) && file_cache_initialize(&cache, &table, pp_prepare_options(&options));
                }
                options.files = &cache;
                result = cached && pp_initialize(&run, &options, &table) && pp_run(&run, path);
            }
            PPSpellings ours = { 0 };
            PPSpellings expected = { 0 };
            result = result && pp_stream_spellings(&run, &ours) &&
                     pp_text_spellings((const uint8_t*)test.expected, strlen(test.expected), run.split_kinds, pp_test_lex_flags(&run), &expected) &&
                     pp_compare_spellings(&run, &ours, &expected, test.name, "wanted");
            result = result && run.statistics.skipped_once != 0 && run.statistics.skipped_guards != 0;
            uint32_t views = 0;
            for (uint32_t d = 0; d < run.data_count; d += 1)
            {
                views += run.datas[d].lexical_bytes != 0;
            }
            result = result && views >= 4;
            if (pass != 0)
            {
                result = result && cache.record_count <= BUSTER_ARRAY_LENGTH(hashes) && (pass == 1 || records == cache.record_count);
                records = cache.record_count;
                for (uint32_t r = 0; result && r < records; r += 1)
                {
                    const PPFileData* data = &cache.records[r];
                    const uint64_t hash = intern_hash_bytes(data->kinds, data->token_count) ^
                                          intern_hash_bytes((const uint8_t*)data->starts, (uint64_t)data->token_count * sizeof(uint32_t)) ^
                                          intern_hash_bytes((const uint8_t*)data->values, (uint64_t)data->token_count * sizeof(uint32_t));
                    result = data->lexical_bytes == 0 && (pass == 1 || hash == hashes[r]);
                    hashes[r] = hash;
                }
            }
            arena_release(&ours.arena);
            arena_release(&expected.arena);
            pp_release(&run);
            if (pass == 0)
            {
                intern_release(&table);
            }
        }
        if (cached)
        {
            file_cache_release(&cache);
            intern_release(&table);
        }
    }
    arena_release(&arena);
    pp_test_remove_case(&test, directory);
    printf("raw query private views: %s (fresh, cold shared and warm shared caches, both kernels)\n", result ? "pass" : "FAIL");
    return result;
}

// Cross both former repair limits and the old uint8_t counter's wrap. Definitions between repairs retain old data
// indexes: their final expansions must still agree with Clang after every later private view has been prepared.
static bool pp_test_raw_query_capacity(const char* directory, bool with_clang)
{
    char source[65536];
    char expected[16384];
    PPTestCase test = { .name = "unlimited opaque recovery", .source = source, .standard = "gnu17", .expected = expected, .options = "-I%d", .files = pp_test_files_raw_query };
    static const uint32_t counts[] = { 8, 9, 64, 65, 257 };
    bool result = true;
    for (uint32_t family = 0; result && family < 2; family += 1)
    {
        for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(counts); c += 1)
        {
            const uint32_t repairs = counts[c];
            uint32_t length = (uint32_t)snprintf(source, sizeof(source), "#define RAW_QUERY __has_include\n");
            uint32_t wanted = 0;
            for (uint32_t i = 0; i < repairs; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "#define SNAP_%u %u\n", i, i);
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, family == 0 ? "#if RAW_QUERY(<R\"(x>)\nhit\n#endif\n" : "#include <R\"(x>\n");
                wanted += (uint32_t)snprintf(expected + wanted, sizeof(expected) - wanted, family == 0 ? "hit " : "header ");
            }
            for (uint32_t i = 0; i < repairs; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "SNAP_%u ", i);
                wanted += (uint32_t)snprintf(expected + wanted, sizeof(expected) - wanted, "%u ", i);
            }
            snprintf(source + length, sizeof(source) - length, "tail\n");
            snprintf(expected + wanted, sizeof(expected) - wanted, "tail\n");
            result = pp_test_case(&test, directory, with_clang);
        }
    }
    printf("opaque recovery: %s (8/9/64/65/257 queries and direct repairs, macro snapshots, both kernels)\n", result ? "pass" : "FAIL");
    return result;
}

// #452's refusal of named universal character names (\N{...}): a case for pp_test_named_ucns, with the places
// ("line:column" each, in order) of the errors it must give, each followed by '*' where clang reads the named UCN
// without a diagnostic: past an identifier's first character (its tryConsumeIdentifierUCN reads it without a token).
typedef struct PPNamedUcnCase PPNamedUcnCase;
struct PPNamedUcnCase
{
    const char* name;
    const char* source;
    const char* standard;
    const char* places;
};

// Appends the places (as line << 32 | column, `shift` columns to the left) of the diagnostics of `text` whose line holds
// `message` to `places`, which has room for `capacity` and holds `*count`. Returns false if one has no place or no room.
static bool pp_test_named_places(const char* text, const char* message, uint32_t shift, uint64_t* places, uint32_t capacity, uint32_t* count)
{
    bool result = true;
    for (const char* at = text != 0 ? text : ""; result && *at != 0;)
    {
        const char* end = strchr(at, '\n');
        const size_t length = end != 0 ? (size_t)(end - at) : strlen(at);
        char line[1024];
        const size_t kept = length < sizeof(line) - 1 ? length : sizeof(line) - 1;
        memcpy(line, at, kept);
        line[kept] = 0;
        if (strstr(line, message) != 0)
        {
            uint32_t row = 0;
            uint32_t column = 0;
            result = sscanf(line, "%*[^:]:%u:%u:", &row, &column) == 2 && column > shift && *count < capacity;
            if (result)
            {
                places[*count] = (uint64_t)row << 32 | (column - shift);
                *count += 1;
            }
        }
        at = end != 0 ? end + 1 : at + length;
    }
    return result;
}

// Sorts `count` places and drops their repeats: the number left.
static uint32_t pp_test_named_unique(uint64_t* places, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const uint64_t place = places[i];
        uint32_t j = i;
        while (j > 0 && places[j - 1] > place)
        {
            places[j] = places[j - 1];
            j -= 1;
        }
        places[j] = place;
    }
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (result == 0 || places[result - 1] != places[i])
        {
            places[result] = places[i];
            result += 1;
        }
    }
    return result;
}

// Named UCNs, which buster refuses where the lexer reads one in a token (#452): each is an error at its backslash, as
// many as there are, by both kernels, wherever clang's lexer reads it as it reads a raw string literal's delimiter (in
// active text, a directive's line, a macro's definition once and an unused argument, and not in a skipped group, a
// literal or a comment); with or without line splices, in an identifier, a pp-number and an unknown token, beside \u, \U
// and UTF-8 spellings of the same character, which are no error. With `with_clang`, clang must put a diagnostic at each
// of those places but the ones inside identifiers, and no other: its -pedantic warning that a named UCN is its extension,
// for a name Unicode has, or its error that a name is none, at the name, three columns after the backslash of the \N{
// the cases write without splices.
// clang accepts a name Unicode has where buster refuses it, so the -E texts are never compared.
static bool pp_test_named_ucns(const char* directory, bool with_clang)
{
    static const PPNamedUcnCase cases[] = {
        {
            .name = "a named UCN beside the character's other spellings",
            .source = "int \\N{LATIN SMALL LETTER E WITH ACUTE} = 1;\nint f(void) { return \xC3\xA9; }\nint g(void) { return \\u00e9 + \\U000000E9 + \\u{e9}; }\n",
            .standard = "gnu17",
            .places = "1:5",
        },
        {
            .name = "named UCNs in tokens, directives and macros",
            .source = "x\\N{GREEK SMALL LETTER ALPHA}y\n"
                      "1\\N{DIGIT ONE}\n"
                      "\\N{GREEK SMALL LETTER ALPHA}\n"
                      "x\\\\\nN{GREEK SMALL LETTER ALPHA}\n"
                      "z\\N{GREEK SMALL\\\n LETTER ALPHA}\n"
                      "#define A \\N{GREEK SMALL LETTER ALPHA}\n"
                      "A A\n"
                      "#define DROP(x) kept\n"
                      "DROP(\\N{GREEK SMALL LETTER ALPHA})\n"
                      "#if \\N{GREEK SMALL LETTER ALPHA}\n"
                      "#elif \\N{GREEK SMALL LETTER ALPHA}\n"
                      "#endif\n"
                      "#if 0\n"
                      "\\N{GREEK SMALL LETTER ALPHA}\n"
                      "#endif\n"
                      "\"\\N{GREEK SMALL LETTER ALPHA}\" '\\N{DIGIT ZERO}' /* \\N{GREEK SMALL LETTER ALPHA} */ // \\N{DIGIT ZERO}\n",
            .standard = "gnu17",
            .places = "1:2* 2:2 3:1 4:2* 6:2* 8:11 11:6 12:5 13:7",
        },
        {
            .name = "a named UCN whose backslash is a trigraph",
            .source = "x?" "?/N{GREEK SMALL LETTER ALPHA} ?" "?/N{DIGIT ZERO}\n",
            .standard = "c17",
            .places = "1:2* 1:33",
        },
        {
            .name = "names Unicode doesn't have",
            .source = "\\N{LATIN SMALL LETTER NOT A THING}\na\\N{latin small letter a}\n",
            .standard = "gnu17",
            .places = "1:1 2:2",
        },
    };
    uint32_t failures = 0;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const PPTestCase test = { .name = cases[c].name, .source = cases[c].source, .standard = cases[c].standard, .expected = "" };
        char path[4096];
        bool result = pp_test_write_case(&test, directory, path, sizeof(path));
        char text[16384];
        char* arguments[BUSTER_PP_TEST_MAX_ARGUMENTS + 1];
        const int argument_count = pp_test_arguments(&test, directory, path, text, sizeof(text), arguments);
        uint64_t wanted[32];
        uint32_t wanted_count = 0;
        // Those clang diagnoses too.
        uint64_t diagnosed[32];
        uint32_t diagnosed_count = 0;
        for (const char* at = cases[c].places; result && *at != 0;)
        {
            uint32_t row = 0;
            uint32_t column = 0;
            int used = 0;
            result = sscanf(at, " %u:%u%n", &row, &column, &used) == 2 && wanted_count < BUSTER_ARRAY_LENGTH(wanted);
            if (result)
            {
                wanted[wanted_count] = (uint64_t)row << 32 | column;
                wanted_count += 1;
                at += used;
                const bool silent = *at == '*';
                diagnosed[diagnosed_count] = wanted[wanted_count - 1];
                diagnosed_count += !silent;
                at += silent;
            }
        }
        for (uint32_t k = 0; result && k < 2; k += 1)
        {
            Arena arena = arena_reserve(1ull << 20);
            PPOptions options;
            const char* input;
            const char* output;
            bool markers;
            result = pp_parse_options(argument_count + 1, arguments, &options, &arena, &input, &output, &markers);
            char* diagnostics = 0;
            size_t length = 0;
            FILE* stream = result ? open_memstream(&diagnostics, &length) : 0;
            options.scalar = k == 1;
            options.diagnostics = stream;
            static InternTable table;
            static Preprocessor pp;
            const bool ready = stream != 0 && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
            const bool ran = ready && pp_run(&pp, path);
            const uint32_t errors = ready ? pp.errors : 0;
            if (ready)
            {
                pp_release(&pp);
                intern_release(&table);
            }
            result = stream != 0 && fclose(stream) == 0 && ready && !ran;
            uint64_t places[32];
            uint32_t count = 0;
            result = result && pp_test_named_places(diagnostics, "error: \\N{...} escapes are not supported", 0, places, BUSTER_ARRAY_LENGTH(places), &count);
            // Every error is one of these, each once and in order.
            result = result && errors == count && count == wanted_count && memcmp(places, wanted, count * sizeof(uint64_t)) == 0;
            if (!result)
            {
                printf("named UCNs: %s (%s kernels): %u errors, not the %u places written for it:\n%s", cases[c].name, k == 0 ? "vectorized" : "scalar",
                       errors, wanted_count, diagnostics != 0 ? diagnostics : "");
            }
            free(diagnostics);
            arena_release(&arena);
        }
        if (result && with_clang)
        {
            char* clang_arguments[BUSTER_PP_TEST_MAX_ARGUMENTS + 3];
            for (int i = 0; i < argument_count; i += 1)
            {
                clang_arguments[i] = arguments[i];
            }
            clang_arguments[argument_count] = "-pedantic";
            clang_arguments[argument_count + 1] = "-fno-caret-diagnostics";
            PPClangOutput clang = pp_test_clang_process("clang", clang_arguments, argument_count + 2, path, false);
            uint64_t places[64];
            uint32_t count = 0;
            result = clang.status != PP_CLANG_ERROR &&
                     pp_test_named_places(clang.diagnostics, "warning: named escape sequences are a Clang extension", 0, places, BUSTER_ARRAY_LENGTH(places), &count) &&
                     pp_test_named_places(clang.diagnostics, "is not a valid Unicode character name", 3, places, BUSTER_ARRAY_LENGTH(places), &count);
            count = result ? pp_test_named_unique(places, count) : 0;
            result = result && count == diagnosed_count && memcmp(places, diagnosed, count * sizeof(uint64_t)) == 0;
            if (!result)
            {
                printf("named UCNs: %s: clang's diagnostics aren't at the %u places written for it:\n%s", cases[c].name, diagnosed_count,
                       clang.diagnostics != 0 ? clang.diagnostics : "");
            }
            free(clang.text);
            free(clang.diagnostics);
        }
        failures += !result;
        pp_test_remove_case(&test, directory);
    }
    printf("named UCNs: %u of %lu cases fail%s\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(cases), with_clang ? " (Clang oracle)" : "");
    return failures == 0;
}

// #455's review: a named UCN at each place where the vector kernels' search (pp_scan_named_ucn_chunks) changes what it
// reads: its backslash in the last byte of a 64-byte chunk and of a bitmap word's 4 KB and in the first, and with
// trigraphs its ??/ across those (its '/' a chunk's or a word's first byte). Lines of "x" before it put it there, and
// after it more of them make the file four windows of 4 KB, which the vectorized kernels prepare a window at a time
// (PPOptions.window_bytes), so that it falls at windows' ends too; then whole, and by the scalar kernels. Each run
// must give the one error, at the UCN's place.
static bool pp_test_named_ucn_boundaries(const char* directory)
{
    static const uint32_t places[] = { 0, 1, 62, 63, 64, 65, 126, 127, 4030, 4031, 4094, 4095, 4096, 4097, 8190, 8191, 8192, 8193, 12286, 12287, 12288 };
    static char source[20480];
    char path[4096];
    snprintf(path, sizeof(path), "%s/case.c", directory);
    uint32_t failures = 0;
    uint32_t runs = 0;
    for (uint32_t trigraphs = 0; trigraphs < 2; trigraphs += 1)
    {
        for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(places); p += 1)
        {
            const uint32_t at = places[p];
            uint32_t length = 0;
            for (uint32_t line = 0; line < at / 2; line += 1)
            {
                source[length] = 'x';
                source[length + 1] = '\n';
                length += 2;
            }
            if (at % 2 != 0)
            {
                source[length] = ' ';
                length += 1;
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%sN{GREEK SMALL LETTER ALPHA}\n", trigraphs != 0 ? "?" "?/" : "\\");
            while (length < 16384 + 64)
            {
                source[length] = 'x';
                source[length + 1] = '\n';
                length += 2;
            }
            bool result = pp_test_write_bytes(path, source, length);
            char standard[] = "-std=gnu17";
            char iso[] = "-std=c17";
            char* arguments[] = { trigraphs != 0 ? iso : standard, path, 0 };
            for (uint32_t run = 0; result && run < 3; run += 1)
            {
                Arena arena = arena_reserve(1ull << 20);
                PPOptions options;
                const char* input;
                const char* output;
                bool markers;
                result = pp_parse_options(2, arguments, &options, &arena, &input, &output, &markers);
                char* diagnostics = 0;
                size_t size = 0;
                FILE* stream = result ? open_memstream(&diagnostics, &size) : 0;
                options.scalar = run == 2;
                options.window_bytes = run == 0 ? 4096 : 0;
                options.diagnostics = stream;
                static InternTable table;
                static Preprocessor pp;
                const bool ready = stream != 0 && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
                const bool ran = ready && pp_run(&pp, path);
                const uint32_t errors = ready ? pp.errors : 0;
                if (ready)
                {
                    pp_release(&pp);
                    intern_release(&table);
                }
                result = stream != 0 && fclose(stream) == 0 && ready && !ran;
                uint64_t found[2];
                uint32_t count = 0;
                result = result && pp_test_named_places(diagnostics, "error: \\N{...} escapes are not supported", 0, found, BUSTER_ARRAY_LENGTH(found), &count);
                result = result && errors == 1 && count == 1 && found[0] == ((uint64_t)(at / 2 + 1) << 32 | (at % 2 + 1));
                if (!result)
                {
                    printf("named UCNs: at byte %u%s (%s): %u errors, not one at %u:%u:\n%s", at, trigraphs != 0 ? " as a trigraph" : "",
                           run == 0 ? "vectorized kernels, windows of 4 KB" : run == 1 ? "vectorized kernels, whole" : "scalar kernels", errors, at / 2 + 1,
                           at % 2 + 1, diagnostics != 0 ? diagnostics : "");
                }
                free(diagnostics);
                arena_release(&arena);
                runs += 1;
            }
            failures += !result;
        }
    }
    unlink(path);
    printf("named UCNs: %u of %lu places at chunks', words' and windows' edges fail (%u runs)\n", failures, (unsigned long)(2 * BUSTER_ARRAY_LENGTH(places)), runs);
    return failures == 0;
}

// An identifier of BUSTER_PP_TEST_NAMED_UCNS named UCNs in a row (#455's review): each an error, by both kernels (the
// vectorized ones prepare its file of about 1.1 MB a window at a time, and the scalar ones whole, both recording the
// errors after its refinement, when an identifier's value is its id), in time in proportion to its length. Finding each UCN's identifier's end from its start took time in
// proportion to the square of it: about 40,000 times the identifier's bytes, tens of seconds, well past the bound. The
// errors are counted, not printed (PPOptions.quiet): a column is found by reading back to its line's start.
#define BUSTER_PP_TEST_NAMED_UCNS 40000
#define BUSTER_PP_TEST_NAMED_UCN_SECONDS 10.0

static bool pp_test_named_ucn_scaling(const char* directory)
{
    static const char ucn[] = "\\N{GREEK SMALL LETTER ALPHA}";
    const uint32_t length = 1 + BUSTER_PP_TEST_NAMED_UCNS * (uint32_t)(sizeof(ucn) - 1) + 1;
    char* source = malloc(length);
    char path[4096];
    snprintf(path, sizeof(path), "%s/case.c", directory);
    bool result = source != 0;
    if (result)
    {
        source[0] = 'x';
        for (uint32_t i = 0; i < BUSTER_PP_TEST_NAMED_UCNS; i += 1)
        {
            memcpy(source + 1 + i * (sizeof(ucn) - 1), ucn, sizeof(ucn) - 1);
        }
        source[length - 1] = '\n';
        result = pp_test_write_bytes(path, source, length);
    }
    free(source);
    char standard[] = "-std=gnu17";
    char* arguments[] = { standard, path, 0 };
    for (uint32_t k = 0; result && k < 2; k += 1)
    {
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        result = pp_parse_options(2, arguments, &options, &arena, &input, &output, &markers);
        options.scalar = k == 1;
        options.quiet = true;
        static InternTable table;
        static Preprocessor pp;
        struct timespec begin;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &begin);
        const bool ready = result && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
        const bool ran = ready && pp_run(&pp, path);
        clock_gettime(CLOCK_MONOTONIC, &end);
        const double seconds = (double)(end.tv_sec - begin.tv_sec) + (double)(end.tv_nsec - begin.tv_nsec) * 1e-9;
        const uint32_t errors = ready ? pp.errors : 0;
        if (ready)
        {
            pp_release(&pp);
            intern_release(&table);
        }
        result = ready && !ran && errors == BUSTER_PP_TEST_NAMED_UCNS && seconds < BUSTER_PP_TEST_NAMED_UCN_SECONDS;
        printf("named UCNs: %u in one identifier (%s kernels): %u errors in %.3f s%s\n", BUSTER_PP_TEST_NAMED_UCNS, k == 0 ? "vectorized" : "scalar", errors,
               seconds, result ? "" : ", not one each in time");
        arena_release(&arena);
    }
    unlink(path);
    return result;
}

// The count limit is gone, but real allocation/index exhaustion and an unchanged mask still fail closed. Restore
// injected capacities before release; no failed repair may publish a new data index or overwrite a canonical view.
static bool pp_test_raw_recovery_failures(void)
{
    static const uint8_t source[] = "#warning /*\ntail\n";
    bool result = true;
    for (uint32_t fault = 0; result && fault < 3; fault += 1)
    {
        static InternTable table;
        static Preprocessor pp;
        const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .quiet = true };
        const bool initialized = intern_initialize(&table) && pp_initialize(&pp, &options, &table);
        result = initialized && pp_run_source(&pp, "recovery-failure.c", source, sizeof(source) - 1);
        const uint32_t count = pp.data_count;
        const uint64_t capacity = pp.arena.capacity;
        if (result)
        {
            const uint32_t canonical = pp.files[pp.main_file].data;
            const uint32_t view = count - 1;
            uint32_t range = 0;
            while (range < pp.range_count && pp.ranges[range].file != pp.main_file)
            {
                range += 1;
            }
            result = range < pp.range_count && pp.datas[view].lexical_bytes != 0;
            if (result)
            {
                pp.context_count = 1;
                pp.contexts[0] = (PPContext){ .kind = BUSTER_PP_CONTEXT_FILE, .file = pp.main_file, .data = fault == 0 ? canonical : view, .range = range };
                pp.arena.capacity = fault == 0 ? pp.arena.length : capacity;
                pp.data_count = fault == 1 ? BUSTER_PP_MAX_FILES : count;
                const bool recovered = pp_recover_opaque_span(&pp, 0, 0, 9, 11);
                result = !recovered && pp.fatal && pp.errors == 1 && pp.contexts[0].data == (fault == 0 ? canonical : view) &&
                         pp.data_count == (fault == 1 ? BUSTER_PP_MAX_FILES : count) && pp.datas[canonical].lexical_bytes == 0 &&
                         memcmp(pp.datas[canonical].bytes, source, sizeof(source) - 1) == 0;
            }
        }
        pp.arena.capacity = capacity;
        pp.data_count = count;
        pp_release(&pp);
        intern_release(&table);
    }
    printf("opaque recovery failure guards: %s (allocation, data indexes, progress)\n", result ? "pass" : "FAIL");
    return result;
}

// #361: a part of the prelude that can't be made (its arena push or virtual file, out of memory or past the files table)
// is fatal, and the run fails without running the parts after it. Each fault is injected after pp_initialize: the arena's
// capacity cut below a part's push (a -D or -include of a megabyte makes the part's push the one that fails), or the
// data table filled to leave the parts before it room. The controls: the same options with nothing taken away, and no
// -include with room for every other part but none for an include file, which must then not be made.
static bool pp_test_prelude_failures(void)
{
    enum
    {
        PP_TEST_PRELUDE_LONG = 1u << 20,
    };
    typedef struct
    {
        const char* name;
        // Which long option the run takes: 0 none, 1 a -D, 2 an -include.
        uint32_t long_option;
        // Whether the arena's capacity is cut to the reference prelude's bytes and a page, so that the command line's push
        // fails, or with `past_command_line` the long option's room besides, so that the push of the long part's file fails.
        bool cut_arena;
        bool past_command_line;
        // Data records left free before the run (0 leaves the table).
        uint32_t free_data;
        // The fatal error's text, or null for a run that must succeed; strerror(ENOMEM)'s follows it when `out_of_memory`,
        // as pp_virtual_file_error gives it.
        const char* message;
        bool out_of_memory;
        // Whether -D X=1 must have taken effect.
        bool defined;
    } PPTestPreludeFault;
    static const PPTestPreludeFault faults[] = {
        { "command line's arena", 1, true, false, 0, "out of memory for the command line's -D, -U and -include options", false, false },
        { "-D and -U file's arena", 1, true, true, 0, "can't make the file of the command line's -D and -U options: ", true, false },
        { "-include file's arena", 2, true, true, 0, "can't make the file of the command line's -include options: ", true, true },
        { "-D and -U file's record", 0, false, false, 1, "can't make the file of the command line's -D and -U options: too many files", false, false },
        { "-include file's record", 2, false, false, 2, "can't make the file of the command line's -include options: too many files", false, true },
        { "no -include, no room for its file", 0, false, false, 3, 0, false, true },
        { "a megabyte's -D, nothing taken away", 1, false, false, 0, 0, false, true },
    };
    static const uint8_t source[] = "X Y\n";
    char* long_text = malloc(PP_TEST_PRELUDE_LONG + 8);
    bool result = long_text != 0;
    // The reference: the prelude's bytes with -D X=1 -U Y alone.
    uint64_t reference = 0;
    if (result)
    {
        static InternTable table;
        static Preprocessor pp;
        static const char* const macros[] = { "DX=1", "UY" };
        const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .quiet = true, .macros = macros, .macro_count = 2 };
        result = intern_initialize(&table) && pp_initialize(&pp, &options, &table);
        const uint64_t initial = pp.arena.length;
        result = result && pp_run_prelude(&pp);
        reference = pp.arena.length - initial;
        pp_release(&pp);
        intern_release(&table);
    }
    for (uint32_t f = 0; result && f < BUSTER_ARRAY_LENGTH(faults); f += 1)
    {
        const PPTestPreludeFault* fault = &faults[f];
        static InternTable table;
        static Preprocessor pp;
        // The long -D's value, or the long -include's name, of PP_TEST_PRELUDE_LONG bytes.
        memset(long_text, 'v', PP_TEST_PRELUDE_LONG + 7);
        long_text[PP_TEST_PRELUDE_LONG + 7] = 0;
        memcpy(long_text, "DLONG=", 6);
        const char* macros[3] = { "DX=1", "UY", long_text };
        const char* includes[1] = { long_text + 7 };
        char* diagnostics = 0;
        size_t diagnostics_length = 0;
        FILE* stream = open_memstream(&diagnostics, &diagnostics_length);
        const PPOptions options = {
            .language = { .standard = 2017, .gnu = true },
            .macros = macros,
            .macro_count = fault->long_option == 1 ? 3 : 2,
            .includes = includes,
            .include_count = fault->long_option == 2 ? 1 : 0,
            .diagnostics = stream,
        };
        bool ran = false;
        bool checked = stream != 0 && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
        const uint64_t capacity = pp.arena.capacity;
        const uint32_t data_count = pp.data_count;
        if (checked)
        {
            const uint64_t long_room = PP_TEST_PRELUDE_LONG + 7 + 16;
            if (fault->cut_arena)
            {
                pp.arena.capacity = pp.arena.length + reference + (fault->past_command_line ? long_room + 32 + 32 : 0) + 4096;
            }
            if (fault->free_data != 0)
            {
                pp.data_count = BUSTER_PP_MAX_FILES - fault->free_data;
            }
            ran = pp_run_source(&pp, "prelude.c", source, sizeof(source) - 1);
            fflush(stream);
            const IdentId x = intern_find_or_add(&table, (const uint8_t*)"X", 1);
            const IdentId y = intern_find_or_add(&table, (const uint8_t*)"Y", 1);
            const IdentId long_name = intern_find_or_add(&table, (const uint8_t*)"LONG", 4);
            const bool x_defined = x != 0 && pp.macro_of[x] != 0;
            const bool long_defined = long_name != 0 && pp.macro_of[long_name] != 0;
            const char* first = diagnostics != 0 ? strstr(diagnostics, "fatal error: ") : 0;
            if (fault->message == 0)
            {
                checked = ran && !pp.fatal && pp.errors == 0 && pp.output.count == 2 && first == 0 && y != 0 && pp.macro_of[y] == 0 &&
                          long_defined == (fault->long_option == 1);
            }
            else
            {
                // One fatal error, the part's, and nothing after it: no main file's tokens, no -D after a failed -D file.
                char message[256];
                snprintf(message, sizeof(message), "%s%s", fault->message, fault->out_of_memory ? strerror(ENOMEM) : "");
                checked = !ran && pp.fatal && pp.output.count == 0 && first != 0 && strstr(first, message) != 0 &&
                          strstr(first + 1, "fatal error: ") == 0 && !long_defined;
            }
            checked = checked && x_defined == fault->defined;
            if (!checked)
            {
                printf("prelude failure '%s': ran %u, fatal %u, errors %u, output %u, X %u, LONG %u, diagnostics: %s\n", fault->name, ran, pp.fatal, pp.errors,
                       pp.output.count, x_defined, long_defined, diagnostics != 0 ? diagnostics : "(none)");
            }
        }
        if (stream != 0)
        {
            fclose(stream);
        }
        free(diagnostics);
        pp.arena.capacity = capacity;
        pp.data_count = data_count;
        pp_release(&pp);
        intern_release(&table);
        result = checked;
    }
    free(long_text);
    printf("prelude failures: %s (%lu faults and controls)\n", result ? "pass" : "FAIL", (unsigned long)BUSTER_ARRAY_LENGTH(faults));
    return result;
}

// An option set of #281's families, or one beside them (%d stands for the test's directory): `refused` is in the message
// of the option pp_parse_options must refuse, or null for a set it must take without a message.
typedef struct PPTestOptionSet PPTestOptionSet;
struct PPTestOptionSet
{
    const char* arguments[BUSTER_PP_TEST_SET_ARGUMENTS];
    const char* refused;
};

static const PPTestOptionSet pp_test_option_sets[] = {
    // Refused: each family buster doesn't follow, where its last option turns it on; -fzvector whatever follows it (clang's
    // driver ignores -fno-zvector); and the options without a -fno- form.
    { { "-fms-extensions" }, "-fms-extensions" },
    { { "-fno-ms-extensions", "-fms-extensions" }, "-fms-extensions" },
    { { "-fms-compatibility" }, "-fms-compatibility" },
    { { "-fborland-extensions" }, "-fborland-extensions" },
    { { "-fno-declspec", "-fdeclspec" }, "-fdeclspec" },
    { { "-fzvector" }, "-fzvector" },
    { { "-fzvector", "-fno-zvector" }, "-fzvector" },
    // clang's -mzvector is -fzvector (predefines_alias), and -mno-zvector is -fno-zvector.
    { { "-mzvector" }, "-fzvector" },
    { { "-mzvector", "-mno-zvector" }, "-fzvector" },
    { { "-fno-zos-extensions", "-fzos-extensions" }, "-fzos-extensions" },
    { { "-ffixed-point" }, "-ffixed-point" },
    { { "-fdefer-ts" }, "-fdefer-ts" },
    { { "-fno-directives-only", "-fdirectives-only" }, "-fdirectives-only" },
    { { "-fkeep-system-includes" }, "-fkeep-system-includes" },
    { { "-fminimize-whitespace" }, "-fminimize-whitespace" },
    { { "-fuse-line-directives" }, "-fuse-line-directives" },
    { { "-frewrite-includes" }, "-frewrite-includes" },
    { { "-frewrite-imports" }, "-frewrite-imports" },
    // clang fails without the module it would otherwise build into its cache, so that nothing is written.
    { { "-fno-implicit-modules", "-fno-modules", "-fmodules" }, "-fmodules" },
    { { "-fimplicit-module-maps" }, "-fimplicit-module-maps" },
    { { "-fbuiltin-module-map" }, "-fbuiltin-module-map" },
    { { "-fmodule-name=probe" }, "-fmodule-name=probe" },
    { { "-fmodule-map-file=%d/missing.modulemap" }, "-fmodule-map-file=" },
    { { "-fdriver-only" }, "-fdriver-only" },
    { { "-fproc-stat-report" }, "-fproc-stat-report" },
    { { "-fmacro-prefix-map=%d=/remapped" }, "-fmacro-prefix-map=" },
    { { "-ffile-prefix-map=%d=/remapped" }, "-ffile-prefix-map=" },
    { { "-fexec-charset=UTF-8", "-fexec-charset=latin1" }, "-fexec-charset=latin1" },
    { { "-finput-charset=UTF8" }, "-finput-charset=UTF8" },
    // Taken: the modeled families, each refused family turned off again, -fzvector in assembly, digraphs on and off in
    // any mode (c89 with them is C94, and C94 without them c89: #288), and raw strings off (buster lexes none), each in
    // c17, where clang's lexes none either (a GNU mode's would differ); and UTF-8 charsets in any case.
    { { "-std=c17", "-fgnu-keywords" }, 0 },
    { { "-std=c17", "-fno-raw-string-literals", "-fraw-string-literals" }, 0 },
    { { "-std=gnu89", "-fraw-string-literals" }, 0 },
    { { "-std=gnu99", "-fraw-string-literals", "-fno-raw-string-literals", "-fraw-string-literals" }, 0 },
    { { "-std=gnu23", "-fno-raw-string-literals" }, 0 },
    { { "-x", "assembler-with-cpp", "-fno-raw-string-literals" }, 0 },
    { { "-std=gnu17", "-fno-raw-string-literals", "-fno-asm" }, 0 },
    { { "-std=c17", "-fno-dollars-in-identifiers" }, 0 },
    { { "-std=c17", "-fno-signed-char" }, 0 },
    { { "-std=c17", "-funsigned-char", "-fno-unsigned-char" }, 0 },
    { { "-std=c17", "-fms-extensions", "-fno-ms-extensions" }, 0 },
    { { "-std=c17", "-fms-compatibility", "-fno-ms-compatibility" }, 0 },
    { { "-std=c17", "-fborland-extensions", "-fno-borland-extensions" }, 0 },
    { { "-std=c17", "-fdeclspec", "-fno-declspec" }, 0 },
    { { "-std=c17", "-fno-zvector" }, 0 },
    { { "-std=c17", "-mno-zvector" }, 0 },
    { { "-std=c17", "-x", "assembler-with-cpp", "-fzvector" }, 0 },
    { { "-std=c17", "-fzos-extensions", "-fno-zos-extensions" }, 0 },
    { { "-std=c17", "-ffixed-point", "-fno-fixed-point" }, 0 },
    { { "-std=c17", "-fdefer-ts", "-fno-defer-ts" }, 0 },
    { { "-std=c17", "-fno-digraphs", "-fdigraphs" }, 0 },
    { { "-std=gnu89", "-fno-raw-string-literals", "-fdigraphs" }, 0 },
    { { "-std=c17", "-fdigraphs", "-fno-digraphs" }, 0 },
    { { "-std=c89", "-fdigraphs" }, 0 },
    { { "-std=iso9899:199409", "-fno-digraphs" }, 0 },
    { { "-std=iso9899:199409" }, 0 },
    { { "-std=c89" }, 0 },
    { { "-std=gnu17", "-fraw-string-literals", "-fno-raw-string-literals" }, 0 },
    { { "-std=c17", "-fshort-wchar", "-fno-short-wchar" }, 0 },
    // -fshort-wchar is predefines_option's, which #if's wide character constants follow.
    { { "-std=c17", "-fno-short-wchar", "-fshort-wchar" }, 0 },
    { { "-std=c17", "-fdirectives-only", "-fno-directives-only" }, 0 },
    { { "-std=c17", "-fkeep-system-includes", "-fno-keep-system-includes" }, 0 },
    { { "-std=c17", "-fminimize-whitespace", "-fno-minimize-whitespace" }, 0 },
    { { "-std=c17", "-fuse-line-directives", "-fno-use-line-directives" }, 0 },
    { { "-std=c17", "-frewrite-includes", "-fno-rewrite-includes" }, 0 },
    { { "-std=c17", "-frewrite-imports", "-fno-rewrite-imports" }, 0 },
    { { "-std=c17", "-fmodules", "-fno-modules" }, 0 },
    { { "-std=c17", "-fimplicit-module-maps", "-fno-implicit-module-maps" }, 0 },
    { { "-std=c17", "-fmodules-driver" }, 0 },
    { { "-std=c17", "-fexec-charset=latin1", "-fexec-charset=UTF-8" }, 0 },
    { { "-std=c17", "-finput-charset=Utf-8" }, 0 },
};

// What the families change in clang's -E: the names that are keywords under them, '$', digraphs and __STDC_VERSION__
// (C94's with -fdigraphs in c89), "//" comments (none in c89 and C94), raw strings, character constants' signs,
// __FILE__ (remapped) and __MODULE__ (named), and a system header for the output modes and the modules.
static const char pp_test_option_probe[] =
    "#include <stddef.h>\n"
    "__is_identifier(__int64) __is_identifier(_pascal) __is_identifier(__declspec) __is_identifier(__vector) __is_identifier(__ptr32)\n"
    "__is_identifier(_Accum) __is_identifier(_Defer) __is_identifier(asm) __is_identifier(typeof)\n"
    "#define bb BB\naa$bb 1$bb\n"
    "%:define DIGRAPH __STDC_VERSION__\nDIGRAPH <: :> <%= a //* c */ b x // y\n"
    "#define R RR\n#define LR LRR\n#define uR uRR\n#define UR URR\n#define u8R u8RR\n"
    "R\"x(raw)x\" LR\"(l)\" uR\"(u)\" UR\"(U)\" u8R\"(8)\"\n"
    "#if (L'\\0' - 1) < 0\nwide_signed\n#endif\n"
    "#if '\\xff' < 0\nchar_signed\n#endif\n"
    "__FILE__ __MODULE__\n";

// pp_test_option_sets: each set refused or taken as it says. With --clang, clang's -E of pp_test_option_probe must
// change with each refused set (from the set's -std= alone, or from no options), which is why buster refuses it, and
// each taken set must give clang's tokens, by both kernels.
static bool pp_test_option_refusals(const char* directory, bool with_clang)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/options.c", directory);
    bool result = pp_test_write_file(path, pp_test_option_probe);
    uint32_t wrong = 0;
    uint32_t unchanged = 0;
    uint32_t different = 0;
    uint32_t refused_count = 0;
    // clang's -E of the probe with each base a refused set is compared with (its -std=, or none: "" here), run once each.
    const char* base_keys[4] = { 0 };
    PPClangOutput bases[4];
    memset(bases, 0, sizeof(bases));
    uint32_t base_count = 0;
    for (uint32_t s = 0; result && s < BUSTER_ARRAY_LENGTH(pp_test_option_sets); s += 1)
    {
        const PPTestOptionSet* set = &pp_test_option_sets[s];
        char text[16384];
        char* arguments[BUSTER_PP_TEST_SET_ARGUMENTS + 1];
        char shown[1024];
        uint32_t used = 0;
        uint32_t shown_used = 0;
        int n = 0;
        for (; n < BUSTER_PP_TEST_SET_ARGUMENTS && set->arguments[n] != 0; n += 1)
        {
            // The argument with %d replaced by the directory.
            arguments[n] = text + used;
            for (const char* at = set->arguments[n]; *at != 0 && used + 4200 < sizeof(text); at += 1)
            {
                const bool directory_here = at[0] == '%' && at[1] == 'd';
                used += directory_here ? (uint32_t)snprintf(text + used, sizeof(text) - used, "%s", directory) : (uint32_t)snprintf(text + used, sizeof(text) - used, "%c", *at);
                at += directory_here;
            }
            used += 1;
            shown_used += (uint32_t)snprintf(shown + shown_used, sizeof(shown) - shown_used, "%s%s", n != 0 ? " " : "", set->arguments[n]);
        }
        arguments[n] = path;
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        char messages[4096];
        FILE* caught;
        const int saved = pp_test_catch_stderr(&caught);
        const bool taken = pp_parse_options(n + 1, arguments, &options, &arena, &input, &output, &markers);
        pp_test_release_stderr(saved, caught, messages, sizeof(messages));
        const bool right = set->refused != 0 ? !taken && strstr(messages, set->refused) != 0 : taken && messages[0] == 0;
        if (!right)
        {
            printf("option sets: %s is %s (%s)\n", shown, taken ? "taken" : "refused", messages[0] != 0 ? messages : "no message");
        }
        wrong += !right;
        refused_count += set->refused != 0;
        if (with_clang && right && set->refused != 0)
        {
            // clang's -E with the set, and with its -std= alone or nothing: with the oracle's own messages caught.
            const int base = strncmp(arguments[0], "-std=", 5) == 0;
            const char* key = base != 0 ? set->arguments[0] : "";
            uint32_t b = 0;
            while (b < base_count && strcmp(base_keys[b], key) != 0)
            {
                b += 1;
            }
            const int keep = pp_test_catch_stderr(&caught);
            PPClangOutput with = pp_test_clang_process("clang", arguments, n, path, false);
            if (b == base_count && b < BUSTER_ARRAY_LENGTH(bases))
            {
                base_keys[b] = key;
                bases[b] = pp_test_clang_process("clang", arguments, base, path, false);
                base_count += 1;
            }
            pp_test_release_stderr(keep, caught, messages, sizeof(messages));
            const PPClangOutput* without = b < base_count ? &bases[b] : 0;
            const bool changed = without != 0 && (with.status != without->status || with.length != without->length ||
                                                  (with.length != 0 && memcmp(with.text, without->text, with.length) != 0));
            if (!changed)
            {
                printf("option sets: clang's -E doesn't change with %s, which buster refuses\n", shown);
            }
            unchanged += !changed;
            free(with.text);
            free(with.diagnostics);
        }
        else if (with_clang && right)
        {
            // The tokens alone, from clang's predefines, not pp_test_file's -dM too: clang's -dM leaves out every macro
            // defined after an unknown token (the probe's '$' as punctuation), and its -x c would override a set's -x.
            uint64_t tokens = 0;
            char predefines[4096];
            snprintf(predefines, sizeof(predefines), "%s/predefines.h", directory);
            size_t length = 0;
            char* text = pp_test_clang(arguments, n, path, predefines, &length);
            PPOptions with_predefines = options;
            with_predefines.predefines_path = predefines;
            const bool same = text != 0 && pp_test_file_output(&with_predefines, path, true, text, length, &tokens, 0);
            free(text);
            if (!same)
            {
                printf("option sets: buster's tokens with %s differ from clang's\n", shown);
            }
            different += !same;
        }
        arena_release(&arena);
    }
    for (uint32_t b = 0; b < base_count; b += 1)
    {
        free(bases[b].text);
        free(bases[b].diagnostics);
    }
    unlink(path);
    printf("option sets: %u of %lu refused or taken wrongly", wrong, (unsigned long)BUSTER_ARRAY_LENGTH(pp_test_option_sets));
    if (with_clang)
    {
        printf(", %u of %u refused ones change nothing in clang's -E and %u of %lu taken ones differ from clang's", unchanged, refused_count, different,
               (unsigned long)BUSTER_ARRAY_LENGTH(pp_test_option_sets) - refused_count);
    }
    printf("\n");
    return result && wrong == 0 && unchanged == 0 && different == 0;
}

// One option set (words separated by spaces) for `path`, against clang with the same options: the queries in the
// oracle's mode, which answer from them. False when the answers differ or either side refuses the options.
static bool pp_test_option_set(const char* directory, const char* path, const char* set)
{
    char words[512];
    char* arguments[BUSTER_PP_TEST_MAX_ARGUMENTS + 1];
    int count = 0;
    snprintf(words, sizeof(words), "%s", set);
    for (char* word = strtok(words, " "); word != 0 && count < BUSTER_PP_TEST_MAX_ARGUMENTS; word = strtok(0, " "))
    {
        arguments[count] = word;
        count += 1;
    }
    arguments[count] = (char*)path;
    Arena arena = arena_reserve(1ull << 20);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    uint64_t tokens = 0;
    bool result = pp_parse_options(count + 1, arguments, &options, &arena, &input, &output, &markers);
    options.clang_features = true;
    result = result && pp_test_file(count, arguments, 0, &options, path, true, directory, false, &tokens);
    if (!result)
    {
        printf("feature options: the answers differ from clang's with %s\n", set);
    }
    arena_release(&arena);
    return result;
}

// The queries clang answers from the options, against clang's: __has_feature and __has_extension of every feature name
// under sanitizer options (the sanitizers they leave on, c_predefines.c's predefines_sanitizer_feature), and the four
// __is_target_* of names around x86_64-linux-gnu's (in other cases, with prefixes, and other ABIs') under each kind of
// target triple.
static bool pp_test_option_features(const char* directory)
{
    static const char* const sanitizer_sets[] = {
        "-fsanitize=address",
        "-fsanitize=kernel-address",
        "-fsanitize=hwaddress",
        "-fsanitize=kernel-hwaddress",
        "-fsanitize=thread",
        "-fsanitize=memory",
        "-fsanitize=kernel-memory",
        "-fsanitize=leak",
        "-fsanitize=address,leak",
        "-fsanitize=address -fno-sanitize=kernel-address",
        "-fsanitize=address -fsanitize=thread -fno-sanitize=address",
        "-fsanitize=undefined",
        "-fsanitize=undefined -fwrapv",
        "-fsanitize=undefined -fno-strict-overflow -fno-wrapv",
        "-fsanitize=undefined -fwrapv-pointer",
        "-fsanitize=signed-integer-overflow -fwrapv",
        "-fsanitize=undefined -fsanitize-ignore-for-ubsan-feature=alignment",
        "-fsanitize=alignment -fsanitize-ignore-for-ubsan-feature=undefined",
        "-fsanitize=undefined -mexecute-only",
        "-fsanitize=undefined-trap -O1",
        "-fsanitize=object-size",
        "-fsanitize=object-size -O2",
        "-fsanitize=object-size -O00",
        "-fsanitize=integer,nullability,implicit-conversion,bounds,float-divide-by-zero",
        "-fsanitize=fuzzer",
        "-fsanitize=fuzzer -fno-sanitize=fuzzer-no-link",
        "-fsanitize=fuzzer -fsanitize-coverage=0",
        "-fsanitize-coverage=trace-pc-guard",
        "-fsanitize-coverage=stack-depth",
        "-fsanitize-coverage=pc-table,trace-div,trace-gep",
        "-fsanitize-coverage=trace-pc-guard -fno-sanitize-coverage=trace-pc-guard",
        "-fsanitize=scudo -fsanitize-coverage=trace-pc-guard",
        "-fsanitize-coverage=trace-pc-guard -fsanitize=scudo -fno-sanitize=scudo",
        "-fsanitize=cfi -flto -fvisibility=hidden",
        "-fsanitize=cfi -flto -fvisibility=hidden -fsanitize-cfi-cross-dso",
        "-fsanitize=cfi-icall,cfi-cast-strict -flto",
        "-fsanitize=kcfi",
        "-fsanitize=safe-stack",
        "-fsanitize=shadow-call-stack",
        "-fsanitize=dataflow",
        "-fsanitize=numerical",
        "-fsanitize=type",
        "-fsanitize=realtime",
        "-fsanitize=alloc-token",
        "-fsanitize=vptr",
        "-fsanitize=memtag,address,undefined -fno-sanitize=all",
    };
    static const char* const target_sets[] = {
        "-std=gnu17",
        "--target=x86_64-pc-linux-gnu",
        "--target=x86_64-linux-gnu",
        "--target=x86_64--linux-gnu",
        "--target=x86_64-none-linux-gnu",
        "--target=x86_64-PC-linux-gnu",
        "--target=amd64-Foo-linux-gnu",
        "-target x86_64-unknown-linux-gnu",
    };
    static const char target_names[] =
        "x86_64 X86_64 x86_64h x86_64_lfi AMD64 amd64 i386 x86 x86_64x linux Linux LINUX linux2 linuxfoo lin darwin macos gnu GNU gnu2 gnux32 gnuabi "
        "gnuabi64 gnueabi gnueabihf gnuf gnuf32 gnus gnusf2 gnut64 gnu_ilp32 musl unknown Unknown pc PC Pc none foo Foo apple";
    char features[4096];
    char targets[4096];
    snprintf(features, sizeof(features), "%s/option_features.c", directory);
    snprintf(targets, sizeof(targets), "%s/option_targets.c", directory);
    FILE* file = fopen(features, "w");
    bool result = file != 0;
    if (file != 0)
    {
        fputs("#define S(x) #x\n", file);
        pp_test_write_names(file, pp_test_feature_names, "__has_feature", "__has_extension");
        result = fclose(file) == 0 && result;
    }
    file = fopen(targets, "w");
    result &= file != 0;
    for (const char* at = target_names; file != 0 && *at != 0;)
    {
        const char* end = strchr(at, ' ');
        const int length = (int)(end != 0 ? end - at : (long)strlen(at));
        fprintf(file, "%.*s: __is_target_arch(%.*s) __is_target_vendor(%.*s) __is_target_os(%.*s) __is_target_environment(%.*s)\n", length, at, length, at,
                length, at, length, at, length, at);
        at += length + (end != 0);
    }
    if (file != 0)
    {
        result = fclose(file) == 0 && result;
    }
    uint32_t failures = 0;
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(sanitizer_sets); i += 1)
    {
        failures += !pp_test_option_set(directory, features, sanitizer_sets[i]);
    }
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(target_sets); i += 1)
    {
        failures += !pp_test_option_set(directory, targets, target_sets[i]);
    }
    unlink(features);
    unlink(targets);
    printf("feature options: %u of %u sanitizer and target option sets differ from clang's answers\n", failures,
           (uint32_t)(BUSTER_ARRAY_LENGTH(sanitizer_sets) + BUSTER_ARRAY_LENGTH(target_sets)));
    return result && failures == 0;
}

#include "c_preprocessor_time_test.c"

// __TIMESTAMP__ is the modification time of the file it is read in (local time, here UTC), or SOURCE_DATE_EPOCH's time
// when that is set: a file and the header it includes, written with times of their own, give each theirs (both kernels,
// and with --clang clang too). The environment is restored after.
static bool pp_test_timestamps(const char* directory, bool with_clang)
{
    static const struct
    {
        const char* epoch;
        const char* expected;
        const char* date;
        const char* time;
    } runs[] = {
        { 0, "\"Sat Jan  1 00:00:00 2000\"\n\"Mon Jan  1 00:00:00 2001\"\n", 0, 0 },
        { "1234567890", "\"Fri Feb 13 23:31:30 2009\"\n\"Fri Feb 13 23:31:30 2009\"\n", "Feb 13 2009", "23:31:30" },
        { "0", "\"Thu Jan  1 00:00:00 1970\"\n\"Thu Jan  1 00:00:00 1970\"\n", "Jan  1 1970", "00:00:00" },
        { "0000", "\"Thu Jan  1 00:00:00 1970\"\n\"Thu Jan  1 00:00:00 1970\"\n", "Jan  1 1970", "00:00:00" },
        { "253402300799", "\"Fri Dec 31 23:59:59 9999\"\n\"Fri Dec 31 23:59:59 9999\"\n", "Dec 31 9999", "23:59:59" },
    };
    char path[4096];
    char header[4096];
    snprintf(path, sizeof(path), "%s/timestamp.c", directory);
    snprintf(header, sizeof(header), "%s/timestamp.h", directory);
    const struct timeval main_time[2] = { { .tv_sec = 946684800 }, { .tv_sec = 946684800 } };
    const struct timeval header_time[2] = { { .tv_sec = 978307200 }, { .tv_sec = 978307200 } };
    bool result = pp_test_write_file(path, "__TIMESTAMP__\n#include \"timestamp.h\"\n") && pp_test_write_file(header, "__TIMESTAMP__\n") &&
                  utimes(path, main_time) == 0 && utimes(header, header_time) == 0;
    const char* saved_zone = getenv("TZ");
    const char* saved_epoch = getenv("SOURCE_DATE_EPOCH");
    char* zone = saved_zone != 0 ? strdup(saved_zone) : 0;
    char* epoch = saved_epoch != 0 ? strdup(saved_epoch) : 0;
    setenv("TZ", "UTC", 1);
    tzset();
    result = result && pp_test_time_errors(path);
    for (uint32_t r = 0; result && r < BUSTER_ARRAY_LENGTH(runs); r += 1)
    {
        if (runs[r].epoch != 0)
        {
            setenv("SOURCE_DATE_EPOCH", runs[r].epoch, 1);
        }
        else
        {
            unsetenv("SOURCE_DATE_EPOCH");
        }
        const PPTestCase test = { .name = runs[r].epoch != 0 ? "__TIMESTAMP__ with SOURCE_DATE_EPOCH" : "__TIMESTAMP__ of each file", .standard = "c17", .expected = runs[r].expected };
        char text[256];
        char* arguments[BUSTER_PP_TEST_MAX_ARGUMENTS + 1];
        const int argument_count = pp_test_arguments(&test, directory, path, text, sizeof(text), arguments);
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        result = pp_parse_options(argument_count + 1, arguments, &options, &arena, &input, &output, &markers);
        options.clang_features = true;
        static InternTable tables[2];
        static Preprocessor preprocessors[2];
        for (uint32_t k = 0; k < 2; k += 1)
        {
            options.scalar = k == 1;
            result = result && pp_test_run(&preprocessors[k], &tables[k], &options, path);
            if (result && runs[r].epoch != 0)
            {
                result = preprocessors[k].has_epoch && strcmp(preprocessors[k].date, runs[r].date) == 0 && strcmp(preprocessors[k].time, runs[r].time) == 0;
            }
        }
        PPSpellings ours = { 0 };
        PPSpellings expected = { 0 };
        result = result && pp_same_streams(&preprocessors[0], &preprocessors[1], test.name) && pp_stream_spellings(&preprocessors[0], &ours) &&
                 pp_text_spellings((const uint8_t*)test.expected, strlen(test.expected), preprocessors[0].split_kinds, pp_test_lex_flags(&preprocessors[0]), &expected) &&
                 pp_compare_spellings(&preprocessors[0], &ours, &expected, test.name, "wanted");
        if (result && with_clang)
        {
            size_t length;
            char* clang_text = pp_test_clang(arguments, argument_count, path, 0, &length);
            PPPlaced theirs = { 0 };
            result = clang_text != 0 && pp_placed_tokens((const uint8_t*)clang_text, length, preprocessors[0].split_kinds, pp_test_lex_flags(&preprocessors[0]), &theirs) &&
                     pp_compare_spellings(&preprocessors[0], &ours, &theirs.spellings, test.name, "clang");
            pp_placed_release(&theirs);
            free(clang_text);
        }
        arena_release(&ours.arena);
        arena_release(&expected.arena);
        for (uint32_t k = 0; k < 2; k += 1)
        {
            pp_release(&preprocessors[k]);
            intern_release(&tables[k]);
        }
        arena_release(&arena);
    }
    if (zone != 0)
    {
        setenv("TZ", zone, 1);
    }
    else
    {
        unsetenv("TZ");
    }
    if (epoch != 0)
    {
        setenv("SOURCE_DATE_EPOCH", epoch, 1);
    }
    else
    {
        unsetenv("SOURCE_DATE_EPOCH");
    }
    tzset();
    free(zone);
    free(epoch);
    unlink(path);
    unlink(header);
    printf("timestamps: %s\n", result ? "each file's modification time, and SOURCE_DATE_EPOCH's" : "differ");
    return result;
}

// The fuzz batch's options, which c_preprocessor_fuzz.c (included after this file) runs with pp_fuzz.
#define BUSTER_PP_FUZZ_CASES 1500
#define BUSTER_PP_FUZZ_MAX_JOBS 256

typedef struct PPFuzzOptions PPFuzzOptions;
struct PPFuzzOptions
{
    // Cases per mode, ids first to first + count - 1 (BUSTER_PP_FUZZ_CASES from 0 by default: test-preprocessor's
    // batch, which needs no clang).
    uint64_t count;
    uint64_t first;
    uint64_t seed;
    // C17, then C23.
    bool modes[2];
    bool with_clang;
    const char* clang_path;
    uint32_t jobs;
};

// The text of an #embed's data: pp_embed_text_vector must write pp_embed_text_scalar's bytes, which must be the values
// in decimal separated by ", " (from snprintf), for every count to 300 of random bytes and of every value in turn, with
// and without a separator first; and at the end of a readable page before a PROT_NONE one, where a read past the bytes
// faults. The output has the vector kernel's 64 bytes of slack.
static bool pp_test_embed_text(uint64_t seed)
{
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 2 * page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = mapping != MAP_FAILED && mprotect(mapping + page_bytes, page_bytes, PROT_NONE) == 0;
    static uint8_t vector[5 * 300 + 64];
    static uint8_t scalar[5 * 300 + 64];
    static char reference[5 * 300 + 64];
    uint32_t checks = 0;
    uint32_t failures = 0;
    uint64_t state = seed;
    for (uint32_t pattern = 0; result && pattern < 2; pattern += 1)
    {
        for (uint32_t count = 0; count <= 300; count += 1)
        {
            uint8_t* bytes = mapping + page_bytes - count;
            for (uint32_t i = 0; i < count; i += 1)
            {
                state = state * 6364136223846793005ull + 1442695040888963407ull;
                bytes[i] = pattern == 0 ? (uint8_t)(state >> 56) : (uint8_t)(i + count);
            }
            for (uint32_t separated = 0; separated < 2; separated += 1)
            {
                const uint64_t vector_length = pp_embed_text_vector(bytes, count, separated != 0, vector);
                const uint64_t scalar_length = pp_embed_text_scalar(bytes, count, separated != 0, scalar);
                uint64_t reference_length = 0;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    reference_length += (uint64_t)snprintf(reference + reference_length, sizeof(reference) - reference_length, "%s%u", separated || i != 0 ? ", " : "", bytes[i]);
                }
                const bool same = vector_length == scalar_length && scalar_length == reference_length && memcmp(vector, scalar, scalar_length) == 0 &&
                                  memcmp(scalar, reference, reference_length) == 0;
                if (!same && failures < 5)
                {
                    printf("embed text: %u bytes (%s, %s): vector %.*s\n  scalar %.*s\n  wanted %.*s\n", count, pattern == 0 ? "random" : "in turn",
                           separated ? "separated" : "first", (int)vector_length, (const char*)vector, (int)scalar_length, (const char*)scalar, (int)reference_length, reference);
                }
                failures += !same;
                checks += 1;
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 2 * page_bytes);
    }
    printf("embed text: %u checks, %u failures\n", checks, failures);
    return result && failures == 0;
}

// The names reserved to the implementation that other headers and programs test, which buster's headers must define
// where clang's do: the rest (the guards and helpers clang names __CLANG_..., and buster __BUSTER_...) can differ.
static const char* const pp_test_header_protocols[] = {
    "__STDDEF_H", "__STDARG_H", "__STDBOOL_H", "__STDALIGN_H", "__STDNORETURN_H", "__ISO646_H", "__STDCKDINT_H", "__GNUC_VA_LIST", "__va_copy",
    "__bool_true_false_are_defined", "__alignas_is_defined", "__alignof_is_defined", "__noreturn_is_defined", "__STDC_VERSION_STDCKDINT_H__",
    "__int8_t_defined", "__uint32_t_defined", "__intptr_t_defined",
};

// A macro a header defines, from a -dM line: its name (NUL-terminated in the line), and its parameters' count, -1 for an
// object-like one (a variadic one's `...` counts).
typedef struct PPTestMacro PPTestMacro;
struct PPTestMacro
{
    const char* name;
    int32_t parameters;
};

static int pp_test_compare_macros(const void* a, const void* b)
{
    return strcmp(((const PPTestMacro*)a)->name, ((const PPTestMacro*)b)->name);
}

// Whether a header's macro of this name is compared with clang's: one not reserved to the implementation, or a protocol.
static bool pp_test_header_compared(const char* name)
{
    bool result = !(name[0] == '_' && name[1] == '_');
    for (uint32_t i = 0; !result && i < BUSTER_ARRAY_LENGTH(pp_test_header_protocols); i += 1)
    {
        result = strcmp(name, pp_test_header_protocols[i]) == 0;
    }
    return result;
}

#define BUSTER_PP_TEST_HEADER_LINES 4096

// The compared macros (pp_test_header_compared) that clang's -dM output `text` has and the predefined ones (`predefines`,
// sorted) don't, sorted by name. Their names point into `text`, which this changes. Returns how many.
static uint32_t pp_test_header_macros(char* text, size_t length, char** predefines, uint32_t predefine_count, PPTestMacro* macros, uint32_t capacity)
{
    static char* lines[BUSTER_PP_TEST_HEADER_LINES];
    const uint32_t line_count = pp_test_predefine_lines(text, length, lines, BUSTER_PP_TEST_HEADER_LINES);
    uint32_t result = 0;
    for (uint32_t i = 0; i < line_count; i += 1)
    {
        char* line = lines[i];
        const bool predefined = bsearch(&line, predefines, predefine_count, sizeof(predefines[0]), pp_test_compare_lines) != 0;
        if (!predefined && strncmp(line, "#define ", 8) == 0)
        {
            char* name = line + 8;
            char* end = name;
            while (*end == '_' || (*end >= 'a' && *end <= 'z') || (*end >= 'A' && *end <= 'Z') || (*end >= '0' && *end <= '9'))
            {
                end += 1;
            }
            int32_t parameters = -1;
            if (*end == '(')
            {
                parameters = end[1] == ')' ? 0 : 1;
                for (const char* c = end + 1; *c != 0 && *c != ')'; c += 1)
                {
                    parameters += *c == ',';
                }
            }
            *end = 0;
            if (pp_test_header_compared(name) && result < capacity)
            {
                macros[result] = (PPTestMacro){ .name = name, .parameters = parameters };
                result += 1;
            }
        }
    }
    qsort(macros, result, sizeof(macros[0]), pp_test_compare_macros);
    return result;
}

// A text's tokens' spellings as strings: the declarations before the first line naming a checked macro (each a ';'
// ended statement at brace depth 0, sorted, with the members of max_align_t clang names its own way renamed), and after
// each check's marker (`buster_check_NAME_`) what the macro expanded to. Without `comments`, lines that start with
// "//" or "/ /" are dropped: clang's own headers' line comments, which strict C89 lexes as tokens.
typedef struct PPTestExpansions PPTestExpansions;
struct PPTestExpansions
{
    char** declarations;
    uint32_t declaration_count;
    char** names;
    char** expansions;
    uint32_t expansion_count;
    char* storage;
};

static int pp_test_compare_strings(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static bool pp_test_header_expansions(char* text, size_t length, bool comments, PPTestExpansions* result)
{
    *result = (PPTestExpansions){ 0 };
    if (!comments)
    {
        size_t kept = 0;
        size_t line = 0;
        while (line < length)
        {
            size_t end = line;
            while (end < length && text[end] != '\n')
            {
                end += 1;
            }
            size_t first = line;
            while (first < end && (text[first] == ' ' || text[first] == '\t'))
            {
                first += 1;
            }
            const bool comment = end - first >= 2 && text[first] == '/' && (text[first + 1] == '/' || (text[first + 1] == ' ' && end - first >= 3 && text[first + 2] == '/'));
            if (!comment)
            {
                memmove(text + kept, text + line, end - line + (end < length));
                kept += end - line + (end < length);
            }
            line = end + 1;
        }
        length = kept;
    }
    PPSpellings spellings = { 0 };
    bool valid = pp_text_spellings((const uint8_t*)text, length, 0, 0, &spellings);
    const uint64_t count = valid ? spellings.count : 0;
    result->storage = malloc(2 * (spellings.length + count) + 64);
    result->declarations = malloc((count + 1) * sizeof(char*));
    result->names = malloc((count + 1) * sizeof(char*));
    result->expansions = malloc((count + 1) * sizeof(char*));
    valid = valid && result->storage != 0 && result->declarations != 0 && result->names != 0 && result->expansions != 0;
    char* out = result->storage;
    char* statement = out;
    int64_t depth = 0;
    bool in_declarations = true;
    for (uint64_t i = 0; valid && i <= count; i += 1)
    {
        const char* spelling = i < count ? (const char*)spellings.bytes + spellings.offsets[i] : "";
        uint32_t spelled = i < count ? spellings.lengths[i] : 0;
        const bool marker = i < count && spelled > 14 && memcmp(spelling, "buster_check_", 13) == 0 && spelling[spelled - 1] == '_';
        if (marker || i == count)
        {
            // End what came before: the last statement, or the last expansion.
            if (in_declarations && out != statement)
            {
                out[-1] = 0;
                result->declarations[result->declaration_count] = statement;
                result->declaration_count += 1;
            }
            else if (!in_declarations)
            {
                out[out != statement ? -1 : 0] = 0;
                out += out == statement;
                result->expansions[result->expansion_count - 1] = statement;
            }
            in_declarations = false;
            if (marker)
            {
                memcpy(out, spelling + 13, spelled - 14);
                out[spelled - 14] = 0;
                result->names[result->expansion_count] = out;
                result->expansion_count += 1;
                out += spelled - 13;
                statement = out;
            }
        }
        else
        {
            static const char* const renames[][2] = { { "__clang_max_align_nonce1", "__max_align_ll" }, { "__clang_max_align_nonce2", "__max_align_ld" } };
            for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(renames); r += 1)
            {
                if (spelled == strlen(renames[r][0]) && memcmp(spelling, renames[r][0], spelled) == 0)
                {
                    spelling = renames[r][1];
                    spelled = (uint32_t)strlen(renames[r][1]);
                }
            }
            memcpy(out, spelling, spelled);
            out[spelled] = ' ';
            out += spelled + 1;
            depth += spelled == 1 && spelling[0] == '{';
            depth -= spelled == 1 && spelling[0] == '}';
            if (in_declarations && depth == 0 && spelled == 1 && spelling[0] == ';')
            {
                out[-1] = 0;
                result->declarations[result->declaration_count] = statement;
                result->declaration_count += 1;
                statement = out;
            }
        }
    }
    qsort(result->declarations, result->declaration_count, sizeof(char*), pp_test_compare_strings);
    arena_release(&spellings.arena);
    return valid;
}

// Frees what pp_test_header_expansions made: a zeroed record (one it never filled) frees nothing.
static void pp_test_expansions_release(PPTestExpansions* expansions)
{
    free(expansions->storage);
    free(expansions->declarations);
    free(expansions->names);
    free(expansions->expansions);
}

// The primary diagnostics clang gave for the test's file `path` (lines that start with it), for comparing two runs:
// notes and the headers' own lines name the headers' paths, which differ.
static void pp_test_primary_diagnostics(const char* diagnostics, const char* path, char* out, size_t capacity)
{
    size_t used = 0;
    const size_t path_length = strlen(path);
    out[0] = 0;
    for (const char* line = diagnostics != 0 ? diagnostics : ""; *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t length = end != 0 ? (size_t)(end - line) : strlen(line);
        if (length > path_length && memcmp(line, path, path_length) == 0 && used + length + 2 < capacity)
        {
            memcpy(out + used, line, length);
            used += length;
            out[used] = '\n';
            used += 1;
            out[used] = 0;
        }
        line += length + (end != 0);
    }
}

// Whether two texts' expansions (pp_test_header_expansions), clang's headers' and buster's, are the same: each check's
// name and tokens, and with `declarations` the declarations. Prints the first difference, after `label`.
static bool pp_test_compare_expansions(const PPTestExpansions* theirs, const PPTestExpansions* mine, bool declarations, const char* label)
{
    bool result = true;
    for (uint32_t k = 0; result && k < theirs->expansion_count; k += 1)
    {
        result = k < mine->expansion_count && strcmp(theirs->names[k], mine->names[k]) == 0 && strcmp(theirs->expansions[k], mine->expansions[k]) == 0;
        if (!result)
        {
            printf("%s: %s expands to \"%s\" in clang's, \"%s\" in ours\n", label, theirs->names[k], theirs->expansions[k], k < mine->expansion_count ? mine->expansions[k] : "(nothing)");
        }
    }
    for (uint32_t k = 0; result && declarations && k < theirs->declaration_count; k += 1)
    {
        result = k < mine->declaration_count && strcmp(theirs->declarations[k], mine->declarations[k]) == 0;
        if (!result)
        {
            printf("%s: a declaration differs:\n  clang's %s\n  ours    %s\n", label, theirs->declarations[k], k < mine->declaration_count ? mine->declarations[k] : "(nothing)");
        }
    }
    if (result && ((declarations && theirs->declaration_count != mine->declaration_count) || theirs->expansion_count != mine->expansion_count))
    {
        printf("%s: %u declarations and %u checks in clang's, %u and %u in ours\n", label, theirs->declaration_count, theirs->expansion_count, mine->declaration_count,
               mine->expansion_count);
        result = false;
    }
    return result;
}

// Counts clang's warnings and errors in `diagnostics`: those in the unit `path` itself (`*primary`), and those in the
// headers it includes (`*elsewhere`), which clang shows only with -Wsystem-headers.
static void pp_test_count_diagnostics(const char* diagnostics, const char* path, uint32_t* primary, uint32_t* elsewhere)
{
    const size_t path_length = strlen(path);
    *primary = 0;
    *elsewhere = 0;
    for (const char* line = diagnostics != 0 ? diagnostics : ""; *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t length = end != 0 ? (size_t)(end - line) : strlen(line);
        const char* warning = memmem(line, length, ": warning:", 10);
        const char* error = memmem(line, length, ": error:", 8);
        const bool in_unit = length > path_length && memcmp(line, path, path_length) == 0 && line[path_length] == ':';
        *primary += (warning != 0 || error != 0) && in_unit;
        *elsewhere += (warning != 0 || error != 0) && !in_unit;
        line += length + (end != 0);
    }
}

// Appends a formatted text to `buffer` (`capacity` bytes, `*length` of them used), if it fits. Returns whether it did;
// the buffer's text is as it was if not.
static __attribute__((format(printf, 4, 5))) bool pp_test_append(char* buffer, size_t capacity, size_t* length, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int written = buffer != 0 && *length < capacity ? vsnprintf(buffer + *length, capacity - *length, format, arguments) : -1;
    va_end(arguments);
    const bool result = written >= 0 && (size_t)written < capacity - *length;
    if (result)
    {
        *length += (size_t)written;
    }
    else if (buffer != 0 && *length < capacity)
    {
        buffer[*length] = 0;
    }
    return result;
}

// Appends `#include <NAME.h>` for each of `count` names.
static bool pp_test_append_includes(char* buffer, size_t capacity, size_t* length, const char* const* names, uint32_t count)
{
    bool result = true;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        result = pp_test_append(buffer, capacity, length, "#include <%s.h>\n", names[i]);
    }
    return result;
}

// Our preprocessor with --buster-headers on `path` in `mode` (and -ffreestanding if `freestanding`, and `option` if it
// isn't null), which must give the tokens of clang's -P text of it with buster's headers (`clang`), and warn no more
// than clang does in the unit itself: it reports warnings in system headers too, which clang hides without
// -Wsystem-headers, and the headers must give none. Returns whether it does, printing what differs after `name`.
static bool pp_test_buster_run(const char* mode, bool freestanding, const char* option, const char* path, const PPClangOutput* clang, const char* name)
{
    uint32_t clang_warnings = 0;
    uint32_t clang_elsewhere = 0;
    pp_test_count_diagnostics(clang->diagnostics, path, &clang_warnings, &clang_elsewhere);
    char* arguments[5];
    int count = 0;
    arguments[count++] = (char*)mode;
    if (freestanding)
    {
        arguments[count++] = "-ffreestanding";
    }
    arguments[count++] = "--buster-headers";
    if (option != 0)
    {
        arguments[count++] = (char*)option;
    }
    arguments[count++] = (char*)path;
    Arena arena = arena_reserve(1ull << 20);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    static InternTable table;
    static Preprocessor pp;
    bool result = pp_parse_options(count, arguments, &options, &arena, &input, &output, &markers);
    // This compares clang's preprocessing of the headers, including their feature-query branches.
    options.clang_features = true;
    result = result && pp_test_run(&pp, &table, &options, path);
    if (result && pp.warnings > clang_warnings)
    {
        printf("%s: our preprocessor warns %u times, clang %u times in the unit\n", name, pp.warnings, clang_warnings);
        result = false;
    }
    PPSpellings mine = { 0 };
    PPSpellings theirs = { 0 };
    result = result && clang->status == PP_CLANG_ACCEPTED && pp_stream_spellings(&pp, &mine) &&
             pp_text_spellings((const uint8_t*)clang->text, clang->length, pp.split_kinds, pp_test_lex_flags(&pp), &theirs) &&
             pp_compare_spellings(&pp, &mine, &theirs, name, "clang");
    arena_release(&mine.arena);
    arena_release(&theirs.arena);
    pp_release(&pp);
    intern_release(&table);
    arena_release(&arena);
    return result;
}

// --buster-headers' search as clang's arguments for a unit with no directories of its own, written at `arguments`:
// -nobuiltininc, then -isystem buster's directory `ours` and -isystem the links' directory `links`
// (pp_clang_fallback_links), where ours searches buster's directory and clang's resource directory after it, as
// pp_clang_arguments gives it then. Returns how many it wrote.
static int pp_test_buster_search(char** arguments, const char* ours, const char* links)
{
    arguments[0] = "-nobuiltininc";
    arguments[1] = "-isystem";
    arguments[2] = (char*)ours;
    arguments[3] = "-isystem";
    arguments[4] = (char*)links;
    return 5;
}

// buster's headers, in the order pp_test_headers checks them.
static const char* const pp_test_header_names[] = { "stddef", "stdarg", "limits", "stdint", "float", "stdbool", "stdalign", "stdnoreturn", "iso646", "stdckdint", "stdatomic" };

// The C library's headers that define some of buster's headers' macros themselves (<wchar.h> WCHAR_MIN and WCHAR_MAX,
// <math.h> INFINITY and NAN, <unistd.h> __intptr_t_defined) or ask buster's for their parts (__need_size_t and the
// like); and clang's headers that buster doesn't supply, which --buster-headers finds in clang's resource directory
// (its <inttypes.h>, <tgmath.h> and <endian.h> include the C library's).
static const char* const pp_test_system_headers[] = { "wchar", "math", "unistd", "sys/types", "stdio", "stdlib", "string", "signal", "time", "uchar", "wctype", "locale" };
static const char* const pp_test_clang_headers[] = { "immintrin", "x86intrin", "cpuid", "mm_malloc", "unwind", "inttypes", "tgmath", "endian" };

#define BUSTER_PP_TEST_HEADER_TEXT (1u << 20)

// buster's headers among the C library's and clang's other ones (pp_test_system_headers, pp_test_clang_headers), in
// one mode, hosted or `freestanding`: two units, the others first and then buster's 11, and buster's first, each
// followed by `checks` (the mode's check lines, whose #include lines then include nothing). clang preprocesses each
// with its own headers, and with buster's in the search --buster-headers makes (-nobuiltininc -isystem `ours` -isystem
// `links`: pp_test_buster_search), both with -Wsystem-headers, and:
// - with buster's, clang warns about nothing in the headers (with its own, glibc's C23 widths in <limits.h> and the
//   line comments of <stdckdint.h> in strict C89 give warnings), and gives the unit itself the same diagnostics;
// - each compared macro expands to the same tokens with either. The declarations aren't compared: clang's freestanding
//   <stdint.h> leaves helper macros defined (__int_least16_t and the like) that rename glibc's typedefs of those names
//   after it;
// - our preprocessor with --buster-headers gives clang's tokens with buster's, and no warnings in the headers.
// Strict C89 has no // comments, which clang's other headers are full of and reads as tokens there, where our lexer
// reads them as comments: in that mode the units leave clang's other headers out. Returns how many units differ.
static uint32_t pp_test_header_orders(const char* directory, const char* ours, const char* links, const char* mode, bool freestanding, const char* checks)
{
    static char text[BUSTER_PP_TEST_HEADER_TEXT];
    const bool strict_c89 = strcmp(mode, "-std=c89") == 0;
    char path[4096];
    snprintf(path, sizeof(path), "%s/header-orders.c", directory);
    char* theirs[4];
    char* mine[9];
    int their_count = 0;
    int my_count = 0;
    theirs[their_count++] = (char*)mode;
    mine[my_count++] = (char*)mode;
    if (freestanding)
    {
        theirs[their_count++] = "-ffreestanding";
        mine[my_count++] = "-ffreestanding";
    }
    my_count += pp_test_buster_search(mine + my_count, ours, links);
    theirs[their_count++] = "-Wsystem-headers";
    mine[my_count++] = "-Wsystem-headers";
    theirs[their_count++] = "-P";
    mine[my_count++] = "-P";
    uint32_t result = 0;
    for (uint32_t order = 0; order < 2; order += 1)
    {
        char label[256];
        snprintf(label, sizeof(label), "headers among others, %s first, %s, %s", order == 0 ? "theirs" : "ours", mode, freestanding ? "freestanding" : "hosted");
        size_t length = 0;
        bool same = order == 0 || pp_test_append_includes(text, sizeof(text), &length, pp_test_header_names, BUSTER_ARRAY_LENGTH(pp_test_header_names));
        same = same && pp_test_append_includes(text, sizeof(text), &length, pp_test_system_headers, BUSTER_ARRAY_LENGTH(pp_test_system_headers));
        same = same && (strict_c89 || pp_test_append_includes(text, sizeof(text), &length, pp_test_clang_headers, BUSTER_ARRAY_LENGTH(pp_test_clang_headers)));
        same = same && (order == 1 || pp_test_append_includes(text, sizeof(text), &length, pp_test_header_names, BUSTER_ARRAY_LENGTH(pp_test_header_names)));
        same = same && pp_test_append(text, sizeof(text), &length, "%s", checks);
        if (!same)
        {
            printf("%s: the unit doesn't fit in %u bytes\n", label, BUSTER_PP_TEST_HEADER_TEXT);
        }
        same = same && pp_test_write_file(path, text);
        PPClangOutput their_text = same ? pp_test_clang_process("clang", theirs, their_count, path, false) : (PPClangOutput){ 0 };
        PPClangOutput my_text = same ? pp_test_clang_process("clang", mine, my_count, path, false) : (PPClangOutput){ 0 };
        same = same && their_text.status == PP_CLANG_ACCEPTED && my_text.status == PP_CLANG_ACCEPTED;
        // No warning in the headers with buster's, and the unit's own diagnostics (ATOMIC_VAR_INIT is deprecated in
        // C17) as with clang's.
        uint32_t primary = 0;
        uint32_t elsewhere = 0;
        pp_test_count_diagnostics(my_text.diagnostics, path, &primary, &elsewhere);
        if (same && elsewhere != 0)
        {
            printf("%s: with ours, clang warns in the headers:\n%s", label, my_text.diagnostics);
            same = false;
        }
        static char their_diagnostics[1 << 16];
        static char my_diagnostics[1 << 16];
        pp_test_primary_diagnostics(their_text.diagnostics, path, their_diagnostics, sizeof(their_diagnostics));
        pp_test_primary_diagnostics(my_text.diagnostics, path, my_diagnostics, sizeof(my_diagnostics));
        if (same && strcmp(their_diagnostics, my_diagnostics) != 0)
        {
            printf("%s: the diagnostics differ:\n  clang's %s\n  ours    %s\n", label, their_diagnostics, my_diagnostics);
            same = false;
        }
        // Ours first, while clang's text is whole: pp_test_header_expansions drops the comment lines from it.
        same = same && pp_test_buster_run(mode, freestanding, 0, path, &my_text, label);
        PPTestExpansions their_expansions = { 0 };
        PPTestExpansions my_expansions = { 0 };
        same = same && pp_test_header_expansions(their_text.text, their_text.length, !strict_c89, &their_expansions) &&
               pp_test_header_expansions(my_text.text, my_text.length, !strict_c89, &my_expansions) &&
               pp_test_compare_expansions(&their_expansions, &my_expansions, false, label);
        pp_test_expansions_release(&their_expansions);
        pp_test_expansions_release(&my_expansions);
        free(their_text.text);
        free(their_text.diagnostics);
        free(my_text.text);
        free(my_text.diagnostics);
        result += !same;
    }
    unlink(path);
    return result;
}

// -nobuiltininc and -nostdinc leave out buster's headers and clang's after them alike, as clang's leave out its resource
// directory: which headers a unit finds (__has_include of buster's, clang's and the C library's) with our preprocessor,
// --buster-headers and each, must be what clang finds with it alone; and with neither, what clang finds searched as
// --buster-headers searches (pp_test_header_orders). Returns how many of the three runs differ.
static uint32_t pp_test_header_options(const char* directory, const char* ours, const char* links)
{
    static const char* const names[] = { "stddef", "stdatomic", "stdint", "immintrin", "inttypes", "stdio" };
    static const char* const options[] = { 0, "-nobuiltininc", "-nostdinc" };
    char path[4096];
    char text[1024];
    snprintf(path, sizeof(path), "%s/header-options.c", directory);
    size_t length = 0;
    bool written = true;
    for (uint32_t i = 0; written && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        written = pp_test_append(text, sizeof(text), &length, "#if __has_include(<%s.h>)\nhas_%s\n#endif\n", names[i], names[i]);
    }
    written = written && pp_test_write_file(path, text);
    uint32_t result = 0;
    for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
    {
        char* arguments[8];
        int count = 0;
        arguments[count++] = "-std=gnu17";
        if (options[o] != 0)
        {
            arguments[count++] = (char*)options[o];
        }
        else
        {
            count += pp_test_buster_search(arguments + count, ours, links);
        }
        arguments[count++] = "-P";
        PPClangOutput clang_text = written ? pp_test_clang_process("clang", arguments, count, path, false) : (PPClangOutput){ 0 };
        char name[128];
        snprintf(name, sizeof(name), "headers with --buster-headers%s%s", options[o] != 0 ? " " : "", options[o] != 0 ? options[o] : "");
        result += !pp_test_buster_run("-std=gnu17", false, options[o], path, &clang_text, name);
        free(clang_text.text);
        free(clang_text.diagnostics);
    }
    unlink(path);
    return result;
}

// A quoted include beside one of clang's headers (pp_find_include): with --buster-headers, a name buster's directory has
// comes from buster's, as the search gives it, and any other from beside the includer; without, from beside it. No
// header of clang 23's includes one of buster's names so, so this asks the search itself, for an include in clang's
// <immintrin.h>. Returns whether each is found where it should be.
static bool pp_test_header_beside(void)
{
    bool result = true;
    for (uint32_t buster = 0; result && buster < 2; buster += 1)
    {
        char* arguments[2] = { buster != 0 ? "--buster-headers" : "-std=gnu17", "unit.c" };
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        static InternTable table;
        static Preprocessor pp;
        char resource[PATH_MAX];
        char includer_path[PATH_MAX];
        result = pp_parse_options(2, arguments, &options, &arena, &input, &output, &markers) && intern_initialize(&table) && pp_initialize(&pp, &options, &table) &&
                 pp_clang_resource_directory(resource, sizeof(resource), 0) && (size_t)snprintf(includer_path, sizeof(includer_path), "%s/immintrin.h", resource) < sizeof(includer_path);
        // The search directory clang's headers are in: the fallback, or the resource directory.
        uint32_t search = BUSTER_PP_NONE;
        for (uint32_t i = 0; result && i < pp.directory_count; i += 1)
        {
            search = strcmp(pp.directories[i], resource) == 0 ? i : search;
        }
        result = result && search != BUSTER_PP_NONE && (buster == 0 || search == pp.fallback_directory);
        const uint32_t includer = result ? pp_lookup_path(&pp, includer_path, (uint32_t)strlen(includer_path), BUSTER_PP_SYSTEM_FILE) : BUSTER_PP_NONE;
        result = includer != BUSTER_PP_NONE && pp.files[includer].data != BUSTER_PP_NONE;
        uint32_t found_in = BUSTER_PP_NONE;
        const uint32_t stddef = result ? pp_find_include(&pp, (const uint8_t*)"stddef.h", 8, false, false, includer, search, &found_in, BUSTER_PP_NONE) : BUSTER_PP_NONE;
        const char* expected = buster != 0 ? pp.buster_directory : resource;
        const size_t expected_length = expected != 0 ? strlen(expected) : 0;
        const bool stddef_right = stddef != BUSTER_PP_NONE && strncmp(pp.files[stddef].path, expected, expected_length) == 0 && pp.files[stddef].path[expected_length] == '/';
        const uint32_t other = result ? pp_find_include(&pp, (const uint8_t*)"mmintrin.h", 10, false, false, includer, search, &found_in, BUSTER_PP_NONE) : BUSTER_PP_NONE;
        const bool other_right = other != BUSTER_PP_NONE && found_in == BUSTER_PP_NONE && strncmp(pp.files[other].path, resource, strlen(resource)) == 0;
        if (result && (!stddef_right || !other_right))
        {
            printf("headers: \"stddef.h\" and \"mmintrin.h\" beside clang's <immintrin.h>%s: %s and %s\n", buster != 0 ? " with --buster-headers" : "",
                   stddef != BUSTER_PP_NONE ? pp.files[stddef].path : "(none)", other != BUSTER_PP_NONE ? pp.files[other].path : "(none)");
        }
        else if (!result)
        {
            printf("headers: can't set up the search beside clang's <immintrin.h>%s\n", buster != 0 ? " with --buster-headers" : "");
        }
        result = result && stddef_right && other_right;
        pp_release(&pp);
        intern_release(&table);
        arena_release(&arena);
    }
    return result;
}

// clang's resource directory and buster's directory given again (#255's review), each as -I, -isystem and -idirafter,
// and the resource directory also through a symbolic link, with a trailing slash and with /./: ours keeps them or drops
// them for its own as pp_dedupe_directories decides, by the directories' identity, and clang must get the search it
// kept (pp_clang_arguments). A unit of <stdint.h> and <stdatomic.h> (and hosted, clang's <inttypes.h>, whose
// #include_next goes on past the directory it is found in) must compare in each, hosted, and freestanding with -I as
// well. Returns how many of the runs differ, adding the runs to `*count`.
static uint32_t pp_test_header_duplicates(const char* directory, uint32_t* count, uint64_t* tokens)
{
    char resource[PATH_MAX];
    char ours[PATH_MAX];
    char slash[PATH_MAX + 2];
    char dot[PATH_MAX + 4];
    char link[4200];
    char unit[4200];
    snprintf(link, sizeof(link), "%s/resource-link", directory);
    snprintf(unit, sizeof(unit), "%s/header-duplicates.c", directory);
    const bool found = pp_clang_resource_directory(resource, sizeof(resource), 0) && pp_buster_headers_directory(ours, sizeof(ours), 0);
    const char* last = found ? strrchr(resource, '/') : 0;
    const bool written = last != 0 && symlink(resource, link) == 0 &&
                         pp_test_write_file(unit, "#include <stdint.h>\n#include <stdatomic.h>\n#if __STDC_HOSTED__\n#include <inttypes.h>\n#endif\n"
                                                  "INT8_MAX ATOMIC_INT_LOCK_FREE\n");
    snprintf(slash, sizeof(slash), "%s/", found ? resource : "");
    snprintf(dot, sizeof(dot), "%.*s/.%s", (int)(last != 0 ? last - resource : 0), resource, last != 0 ? last : "");
    const struct
    {
        const char* option;
        const char* path;
        bool freestanding;
    } cases[] = {
        { "-I", resource, false },
        { "-I", resource, true },
        { "-I", slash, false },
        { "-I", dot, false },
        { "-I", link, false },
        { "-isystem", resource, false },
        { "-isystem", dot, false },
        { "-isystem", link, false },
        { "-idirafter", resource, false },
        { "-idirafter", link, false },
        { "-I", ours, false },
        { "-isystem", ours, false },
        { "-idirafter", ours, false },
    };
    uint32_t result = 0;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char* passed[4] = { "--buster-headers", (char*)cases[c].option, (char*)cases[c].path, "-ffreestanding" };
        char* files[1] = { unit };
        const uint32_t differ = written ? pp_test_files(cases[c].freestanding ? 4 : 3, passed, 1, files, true, directory, tokens) : 1;
        if (differ != 0)
        {
            printf("headers: %s %s%s with --buster-headers differs from clang\n", cases[c].option, cases[c].path, cases[c].freestanding ? " -ffreestanding" : "");
        }
        result += differ;
        *count += 1;
    }
    unlink(unit);
    unlink(link);
    return result;
}

// clang's links in a directory whose name clang escapes in its line markers, as $TMPDIR can give it (#255's review): with
// a quote, a backslash or a byte past ASCII in it, the unit at `unit` must compare as under any other name. Returns how
// many of the runs differ, adding the runs to `*count`.
static uint32_t pp_test_header_escapes(const char* directory, const char* unit, uint32_t* count, uint64_t* tokens)
{
    static const char* const names[] = { "quote\"d", "back\\slash", "caf\xc3\xa9" };
    uint32_t result = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        char escaped[4200];
        char predefines[4300];
        snprintf(escaped, sizeof(escaped), "%s/%s", directory, names[i]);
        snprintf(predefines, sizeof(predefines), "%s/predefines.h", escaped);
        char* passed[1] = { "--buster-headers" };
        char* files[1] = { (char*)unit };
        const uint32_t differ = mkdir(escaped, 0700) == 0 ? pp_test_files(1, passed, 1, files, true, escaped, tokens) : 1;
        if (differ != 0)
        {
            printf("headers: the unit with clang's links under %s differs from clang\n", escaped);
        }
        unlink(predefines);
        rmdir(escaped);
        result += differ;
        *count += 1;
    }
    return result;
}

// test-preprocessor's files with --buster-headers (pp_test_files), against clang searched as the flag searches
// (pp_clang_arguments, #255's reviews). A unit includes clang's <inttypes.h>, whose #include_next reaches the C
// library's, which includes buster's <stdint.h>; then more of buster's headers, clang's <cpuid.h> and a line of their
// macros. Its <limits.h> is an -isystem directory's, which buster's directory must follow though the flag comes first:
// it gives a token, whose include stack shows which came first, and includes buster's next. clang's line markers name
// the files it finds through the links, and they must be named as ours names them (pp_test_resource_names), also under
// names clang escapes (pp_test_header_escapes). Then which headers __has_include finds, with the flag alone, with
// -nobuiltininc and with -nostdinc, which leave buster's search out of clang's arguments. Every such run has an -iquote
// whose value spells -nostdinc: a directory's name (there is none), not the option. Last, the directories ours drops as
// repeated (pp_test_header_duplicates). Returns how many files differ.
static uint32_t pp_test_header_files(const char* directory)
{
    static const char* const options[] = { 0, "-nobuiltininc", "-nostdinc" };
    static const char* const names[] = { "stddef", "stdatomic", "stdint", "immintrin", "inttypes", "stdio" };
    char unit[4096];
    char queries[4096];
    char system[4096];
    char limits[4200];
    char text[1024];
    snprintf(unit, sizeof(unit), "%s/header-files.c", directory);
    snprintf(queries, sizeof(queries), "%s/header-queries.c", directory);
    snprintf(system, sizeof(system), "%s/header-system", directory);
    snprintf(limits, sizeof(limits), "%s/limits.h", system);
    size_t length = 0;
    bool written = mkdir(system, 0700) == 0;
    for (uint32_t i = 0; written && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        written = pp_test_append(text, sizeof(text), &length, "#if __has_include(<%s.h>)\nhas_%s\n#endif\n", names[i], names[i]);
    }
    written = written && pp_test_write_file(queries, text) && pp_test_write_file(limits, "system_limits\n#include_next <limits.h>\n") &&
              pp_test_write_file(unit, "#include <inttypes.h>\n#include <stddef.h>\n#include <limits.h>\n#include <stdatomic.h>\n#include <cpuid.h>\n"
                                       "PRId64 INT64_MAX SIZE_MAX CHAR_BIT ATOMIC_INT_LOCK_FREE offsetof(struct s, m) bit_AVX512F\n");
    uint32_t result = 0;
    uint32_t count = 0;
    uint64_t tokens = 0;
    for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
    {
        char* passed[6] = { "--buster-headers", "-isystem", system, "-iquote", "-nostdinc", (char*)options[o] };
        char* files[2] = { queries, unit };
        const int file_count = o == 0 ? 2 : 1;
        result += written ? pp_test_files(options[o] != 0 ? 6 : 5, passed, file_count, files, true, directory, &tokens) : (uint32_t)file_count;
        count += (uint32_t)file_count;
    }
    const uint32_t escapes = written ? pp_test_header_escapes(directory, unit, &count, &tokens) : 0;
    const uint32_t duplicates = pp_test_header_duplicates(directory, &count, &tokens);
    result += escapes + duplicates;
    unlink(unit);
    unlink(queries);
    unlink(limits);
    rmdir(system);
    printf("headers: %u of %u files differ from clang with --buster-headers, as test-preprocessor compares files (%lu tokens): buster's and clang's headers after an "
           "-isystem directory's, and under links whose names clang escapes (%u differ); __has_include with the flag alone, with -nobuiltininc and with -nostdinc; "
           "and clang's resource directory and buster's given again in 13 ways (%u differ)\n",
           result, count, (unsigned long)tokens, escapes, duplicates);
    return result;
}

#define BUSTER_PP_TEST_COMMAND_DEFINITIONS 300

// benchmark-preprocessor's clang run (pp_clang_preprocess) of a command longer than its first fixed array held (#255's
// review: past 244 arguments the rest were dropped, and clang read -E as an -isystem's value, and linked): 300 -D
// options and --buster-headers' search after them. The buster-only valueless options must not reach clang. clang's
// text must have the last -D's macro defined and <cpuid.h> found through the links. Returns whether it does.
static bool pp_test_clang_command(const char* directory)
{
    char path[4096];
    char output[4096];
    char source[256];
    snprintf(path, sizeof(path), "%s/clang-command.c", directory);
    snprintf(output, sizeof(output), "%s/clang-command.i", directory);
    snprintf(source, sizeof(source), "#if __has_include(<cpuid.h>) && defined(BUSTER_TEST_%u)\nlinks_and_definitions BUSTER_TEST_%u\n#endif\n",
             BUSTER_PP_TEST_COMMAND_DEFINITIONS - 1, BUSTER_PP_TEST_COMMAND_DEFINITIONS - 1);
    static char definitions[BUSTER_PP_TEST_COMMAND_DEFINITIONS][32];
    char* passed[BUSTER_PP_TEST_COMMAND_DEFINITIONS + 4];
    int count = 0;
    passed[count++] = "--buster-headers";
    passed[count++] = "--scalar";
    passed[count++] = "--clang-features";
    for (uint32_t i = 0; i < BUSTER_PP_TEST_COMMAND_DEFINITIONS; i += 1)
    {
        snprintf(definitions[i], sizeof(definitions[i]), "-DBUSTER_TEST_%u=%u", i, i);
        passed[count++] = definitions[i];
    }
    passed[count] = path;
    Arena arena = arena_reserve(1ull << 20);
    PPOptions options;
    const char* input;
    const char* written_to;
    bool markers;
    char ours[PATH_MAX];
    char links[4200];
    snprintf(links, sizeof(links), "%s/clang-command-XXXXXX", directory);
    bool linked = false;
    int clang_count = 0;
    bool result = pp_test_write_file(path, source) && pp_parse_options(count + 1, passed, &options, &arena, &input, &written_to, &markers);
    char** clang = result ? pp_clang_arguments(&options, count, passed, ours, links, &linked, &clang_count) : 0;
    result = clang != 0 && pp_clang_preprocess(clang_count, clang, output, path);
    FILE* file = result ? fopen(output, "r") : 0;
    char text[256] = { 0 };
    char expected[64];
    snprintf(expected, sizeof(expected), "links_and_definitions %u", BUSTER_PP_TEST_COMMAND_DEFINITIONS - 1);
    result = file != 0 && fread(text, 1, sizeof(text) - 1, file) != 0 && strstr(text, expected) != 0;
    if (file != 0)
    {
        fclose(file);
    }
    printf("clang command: %d arguments to clang, %s\n", clang_count, result ? "all of them read" : "not all of them read");
    if (linked)
    {
        pp_clang_remove_links(links);
    }
    free(clang);
    unlink(output);
    unlink(path);
    arena_release(&arena);
    return result;
}

// buster's own headers (src/buster/include, #117's item 6) against clang's resource directory, for each header, language
// mode and hosted or freestanding run. clang preprocesses the header with each set, its own and ours
// (-nobuiltininc -isystem), and:
// - the macros the header defines (-dM, less the predefined ones) must have the same names, but for the guards and
//   helpers reserved to the implementation (two underscores first), which each names its own way; of those, the ones
//   other headers test (pp_test_header_protocols) are compared too;
// - each compared macro, named on a line of its own (a function-like one with placeholder arguments), must expand to
//   the same tokens;
// - the declarations the header makes must be the same, in any order (clang's <stdint.h> declares the widest types
//   first), but for the names of max_align_t's members;
// - clang must accept or reject both alike, with the same primary diagnostics.
// In strict C89, clang's own <stdint.h> and <stdckdint.h> put their // comments in the output as tokens (C89 has no
// such comments): those lines are dropped from its side. Then our preprocessor with --buster-headers must give clang's
// tokens with ours, for every header in each mode, with no warnings from them; the headers must work among the C
// library's and clang's other ones, which --buster-headers finds in clang's resource directory after buster's
// (pp_test_header_orders); -nobuiltininc and -nostdinc must leave out both (pp_test_header_options); a quoted name
// beside one of clang's headers must come from buster's when buster's has it (pp_test_header_beside);
// test-preprocessor's files must compare with clang under --buster-headers (pp_test_header_files); and clang's command
// for the benchmark must have every argument (pp_test_clang_command).
// The modes pp_test_headers checks each header in, hosted and freestanding.
static const char* const pp_test_header_modes[] = { "-std=c89", "-std=c99", "-std=c11", "-std=c17", "-std=c23", "-std=gnu89", "-std=gnu17", "-std=gnu23" };

// What a part of the checks that run in child processes (pp_test_children) found: a mode and hosting's header sets
// (pp_test_header_set), or a range of the cases (pp_test_case_part) with what they added to the pp_test_system_
// counters.
typedef struct PPTestChildCounts PPTestChildCounts;
struct PPTestChildCounts
{
    uint32_t sets;
    uint32_t failures;
    uint32_t our_failures;
    uint32_t order_failures;
    uint64_t system_units;
    uint64_t system_inexact;
    uint64_t system_tokens;
    uint64_t system_made;
    uint64_t tokens;
    char system_inexact_first[256];
    bool result;
};

// Mode m's sets, hosted or freestanding: each header's macros and expansions with clang's headers and with ours, our
// preprocessor on all of them, and the headers among the C library's and clang's (pp_test_header_orders). Its files go
// in `directory`, which is the set's own; `empty` is an empty file and `links` clang's resource headers but ours.
static PPTestChildCounts pp_test_header_set(const char* directory, const char* ours, const char* links, const char* empty, uint32_t m, uint32_t hosting)
{
    PPTestChildCounts counts = { 0 };
    char path[4096];
    snprintf(path, sizeof(path), "%s/headers.c", directory);
    bool result = true;
    static PPTestMacro theirs[BUSTER_PP_TEST_HEADER_LINES];
    static PPTestMacro mine[BUSTER_PP_TEST_HEADER_LINES];
    static char* predefines[BUSTER_PP_TEST_HEADER_LINES];
    // Every compared macro of every header, for the runs of our preprocessor: the check lines. And one header's.
    static char checks[BUSTER_PP_TEST_HEADER_TEXT];
    static char text[BUSTER_PP_TEST_HEADER_TEXT];
    char* arguments[8] = { (char*)pp_test_header_modes[m], hosting != 0 ? "-ffreestanding" : 0 };
    const int base = hosting != 0 ? 2 : 1;
    arguments[base] = "-nobuiltininc";
    arguments[base + 1] = "-isystem";
    arguments[base + 2] = (char*)ours;
    const char* hosted = hosting != 0 ? "freestanding" : "hosted";
    PPClangOutput predefined = pp_test_clang_process("clang", arguments, base, empty, true);
    const uint32_t predefine_count = predefined.status == PP_CLANG_ACCEPTED ? pp_test_predefine_lines(predefined.text, predefined.length, predefines, BUSTER_PP_TEST_HEADER_LINES) : 0;
    result = predefined.status == PP_CLANG_ACCEPTED;
    size_t checks_length = 0;
    bool checks_fit = true;
    for (uint32_t h = 0; result && h < BUSTER_ARRAY_LENGTH(pp_test_header_names); h += 1)
    {
        char source[64];
        char label[128];
        snprintf(source, sizeof(source), "#include <%s.h>\n", pp_test_header_names[h]);
        snprintf(label, sizeof(label), "headers: <%s.h>, %s, %s", pp_test_header_names[h], pp_test_header_modes[m], hosted);
        checks_fit = checks_fit && pp_test_append(checks, sizeof(checks), &checks_length, "%s", source);
        result = pp_test_write_file(path, source);
        PPClangOutput their_macros = pp_test_clang_process("clang", arguments, base, path, true);
        PPClangOutput my_macros = pp_test_clang_process("clang", arguments, base + 3, path, true);
        const uint32_t their_count = their_macros.status == PP_CLANG_ACCEPTED ? pp_test_header_macros(their_macros.text, their_macros.length, predefines, predefine_count, theirs, BUSTER_PP_TEST_HEADER_LINES) : 0;
        const uint32_t my_count = my_macros.status == PP_CLANG_ACCEPTED ? pp_test_header_macros(my_macros.text, my_macros.length, predefines, predefine_count, mine, BUSTER_PP_TEST_HEADER_LINES) : 0;
        bool same = their_macros.status == PP_CLANG_ACCEPTED && my_macros.status == PP_CLANG_ACCEPTED;
        // The names, merged.
        uint32_t i = 0;
        uint32_t j = 0;
        while (same && (i < their_count || j < my_count))
        {
            const int order = i == their_count ? 1 : j == my_count ? -1 : strcmp(theirs[i].name, mine[j].name);
            if (order != 0)
            {
                printf("%s: only %s defines %s\n", label, order < 0 ? "clang's" : "ours", order < 0 ? theirs[i].name : mine[j].name);
                same = false;
            }
            i += order <= 0;
            j += order >= 0;
        }
        // The check lines: each compared macro named alone, or invoked with placeholder arguments.
        size_t text_length = 0;
        bool fits = pp_test_append(text, sizeof(text), &text_length, "%s", source);
        for (uint32_t k = 0; same && fits && k < their_count; k += 1)
        {
            char line[512];
            size_t line_length = 0;
            fits = pp_test_append(line, sizeof(line), &line_length, "buster_check_%s_ %s", theirs[k].name, theirs[k].name);
            for (int32_t a = 0; fits && theirs[k].parameters >= 0 && a <= theirs[k].parameters; a += 1)
            {
                const char* open = a == 0 ? "(" : "";
                fits = a < theirs[k].parameters ? pp_test_append(line, sizeof(line), &line_length, "%s%sa%d", open, a == 0 ? "" : ", ", a)
                                                : pp_test_append(line, sizeof(line), &line_length, "%s)", open);
            }
            fits = fits && pp_test_append(text, sizeof(text), &text_length, "%s\n", line);
            checks_fit = checks_fit && pp_test_append(checks, sizeof(checks), &checks_length, "%s\n", line);
        }
        if (same && !fits)
        {
            printf("%s: the check lines don't fit in %u bytes\n", label, BUSTER_PP_TEST_HEADER_TEXT);
            same = false;
        }
        if (same)
        {
            result = pp_test_write_file(path, text);
            arguments[base + 3] = "-P";
            char* with_p[9];
            memcpy(with_p, arguments, sizeof(arguments));
            with_p[base] = "-P";
            PPClangOutput their_text = pp_test_clang_process("clang", with_p, base + 1, path, false);
            PPClangOutput my_text = pp_test_clang_process("clang", arguments, base + 4, path, false);
            // Both zeroed first, so that each can be released whichever of the runs fails.
            PPTestExpansions their_expansions = { 0 };
            PPTestExpansions my_expansions = { 0 };
            const bool strict_c89 = m == 0;
            same = their_text.status == my_text.status && their_text.status != PP_CLANG_ERROR &&
                   pp_test_header_expansions(their_text.text, their_text.length, !strict_c89, &their_expansions) &&
                   pp_test_header_expansions(my_text.text, my_text.length, true, &my_expansions) &&
                   pp_test_compare_expansions(&their_expansions, &my_expansions, true, label);
            static char their_diagnostics[1 << 16];
            static char my_diagnostics[1 << 16];
            pp_test_primary_diagnostics(their_text.diagnostics, path, their_diagnostics, sizeof(their_diagnostics));
            pp_test_primary_diagnostics(my_text.diagnostics, path, my_diagnostics, sizeof(my_diagnostics));
            if (same && strcmp(their_diagnostics, my_diagnostics) != 0)
            {
                printf("%s: the diagnostics differ:\n  clang's %s\n  ours    %s\n", label, their_diagnostics, my_diagnostics);
                same = false;
            }
            pp_test_expansions_release(&their_expansions);
            pp_test_expansions_release(&my_expansions);
            free(their_text.text);
            free(their_text.diagnostics);
            free(my_text.text);
            free(my_text.diagnostics);
        }
        free(their_macros.text);
        free(their_macros.diagnostics);
        free(my_macros.text);
        free(my_macros.diagnostics);
        counts.sets += 1;
        counts.failures += !same;
    }
    free(predefined.text);
    free(predefined.diagnostics);
    if (result && !checks_fit)
    {
        printf("headers: the check lines of %s, %s don't fit in %u bytes\n", pp_test_header_modes[m], hosted, BUSTER_PP_TEST_HEADER_TEXT);
        result = false;
    }
    // Our preprocessor with --buster-headers, on every header and check line of the mode at once, against clang
    // with our headers; then the headers among the C library's and clang's other ones.
    if (result)
    {
        result = pp_test_write_file(path, checks);
        char* with_p[9];
        memcpy(with_p, arguments, sizeof(arguments));
        with_p[base + 3] = "-P";
        PPClangOutput clang_text = pp_test_clang_process("clang", with_p, base + 4, path, false);
        char name[128];
        snprintf(name, sizeof(name), "headers, ours preprocessed, %s, %s", pp_test_header_modes[m], hosted);
        counts.our_failures += !pp_test_buster_run(pp_test_header_modes[m], hosting != 0, 0, path, &clang_text, name);
        free(clang_text.text);
        free(clang_text.diagnostics);
        counts.order_failures += pp_test_header_orders(directory, ours, links, pp_test_header_modes[m], hosting != 0, checks);
    }
    unlink(path);
    counts.result = result;
    return counts;
}

#define BUSTER_PP_TEST_HEADER_SETS (2 * BUSTER_ARRAY_LENGTH(pp_test_header_modes))
#define BUSTER_PP_TEST_CHILDREN 64
// The parts the cases run in: several per CPU, so that parts of slow cases don't leave the others idle.
#define BUSTER_PP_TEST_CASE_PARTS 32

// Cases [first, end) of pp_test_cases in `directory`, as run_preprocessor_fixture_tests runs them all: their failures,
// and what they added to the pp_test_system_ counters.
static PPTestChildCounts pp_test_case_part(const char* directory, bool with_clang, uint32_t first, uint32_t end)
{
    pp_test_system_units = 0;
    pp_test_system_inexact = 0;
    pp_test_system_tokens = 0;
    pp_test_system_made = 0;
    pp_test_system_inexact_first[0] = 0;
    PPTestChildCounts counts = { .result = true };
    for (uint32_t i = first; i < end; i += 1)
    {
        counts.failures += !pp_test_case(&pp_test_cases[i], directory, with_clang);
    }
    // What the cases leave, which run_preprocessor_fixture_tests removes from its own directory.
    static const char* const names[] = { "case.c", "other.h", "predefines.h" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        char path[4200];
        snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
        unlink(path);
    }
    counts.system_units = pp_test_system_units;
    counts.system_inexact = pp_test_system_inexact;
    counts.system_tokens = pp_test_system_tokens;
    counts.system_made = pp_test_system_made;
    snprintf(counts.system_inexact_first, sizeof(counts.system_inexact_first), "%s", pp_test_system_inexact_first);
    return counts;
}

// Files [first, end) of test-preprocessor's files, as pp_test_files runs them all (pp_test_file, whose clang
// predefines are in `directory`): how many differ, and what they added to the tokens and the pp_test_system_ counters.
static PPTestChildCounts pp_test_file_part(int argument_count, char** arguments, const char* links, const PPOptions* options, char** files, uint32_t first,
                                           uint32_t end, bool with_clang, const char* directory)
{
    pp_test_system_units = 0;
    pp_test_system_inexact = 0;
    pp_test_system_tokens = 0;
    pp_test_system_made = 0;
    pp_test_system_inexact_first[0] = 0;
    PPTestChildCounts counts = { .result = true };
    for (uint32_t i = first; i < end; i += 1)
    {
        counts.failures += !pp_test_file(argument_count, arguments, links, options, files[i], with_clang, directory, true, &counts.tokens);
    }
    counts.system_units = pp_test_system_units;
    counts.system_inexact = pp_test_system_inexact;
    counts.system_tokens = pp_test_system_tokens;
    counts.system_made = pp_test_system_made;
    snprintf(counts.system_inexact_first, sizeof(counts.system_inexact_first), "%s", pp_test_system_inexact_first);
    return counts;
}

// Checks that run in child processes: which, and what their parts read.
typedef enum PPTestChildWork
{
    PP_TEST_CHILD_HEADERS,
    PP_TEST_CHILD_CASES,
    PP_TEST_CHILD_FILES,
} PPTestChildWork;

typedef struct PPTestChildren PPTestChildren;
struct PPTestChildren
{
    PPTestChildWork work;
    uint32_t parts;
    const char* directory;
    const char* ours;
    const char* links;
    const char* empty;
    bool with_clang;
    char** arguments;
    int argument_count;
    const PPOptions* options;
    char** files;
    uint32_t count;
};

// Part `part` of the work, in `own`, a directory of its own.
static PPTestChildCounts pp_test_child_part(const PPTestChildren* children, uint32_t part, const char* own)
{
    PPTestChildCounts counts = { 0 };
    const uint32_t cases = BUSTER_ARRAY_LENGTH(pp_test_cases);
    switch (children->work)
    {
        case PP_TEST_CHILD_HEADERS:
            counts = pp_test_header_set(own, children->ours, children->links, children->empty, part / 2, part % 2);
            break;
        case PP_TEST_CHILD_CASES:
            counts = pp_test_case_part(own, children->with_clang, (uint32_t)((uint64_t)cases * part / children->parts),
                                       (uint32_t)((uint64_t)cases * (part + 1) / children->parts));
            break;
        case PP_TEST_CHILD_FILES:
            counts = pp_test_file_part(children->argument_count, children->arguments, children->links, children->options, children->files,
                                       (uint32_t)((uint64_t)children->count * part / children->parts),
                                       (uint32_t)((uint64_t)children->count * (part + 1) / children->parts), children->with_clang, children->directory);
            break;
    }
    return counts;
}

// The work's parts, at most one per CPU at once (#465): each in a child process (test_process.c) with a directory of
// its own (directory/headers-N, cases-N or files-N), its output printed in the parts' order and its counts sent back.
// Returns their counts added up, with the first part's system_inexact_first that has one.
static PPTestChildCounts pp_test_children(const PPTestChildren* children)
{
    const uint32_t cpus = test_process_cpus();
    const uint32_t parts = children->parts < BUSTER_PP_TEST_CHILDREN ? children->parts : BUSTER_PP_TEST_CHILDREN;
    const uint32_t running = cpus < parts ? cpus : parts;
    TestProcess processes[BUSTER_PP_TEST_CHILDREN];
    PPTestChildCounts total = { .result = parts == children->parts };
    uint32_t started = 0;
    for (uint32_t finished = 0; finished < parts; finished += 1)
    {
        while (started < parts && started - finished < running)
        {
            if (test_process_start(&processes[started]))
            {
                char own[4200];
                static const char* const kinds[] = { "headers", "cases", "files" };
                snprintf(own, sizeof(own), "%s/%s-%u", children->directory, kinds[children->work], started);
                PPTestChildCounts counts = { 0 };
                if (mkdir(own, 0700) == 0)
                {
                    counts = pp_test_child_part(children, started, own);
                    if (rmdir(own) != 0)
                    {
                        printf("test-preprocessor: %s isn't empty after its part\n", own);
                        counts.result = false;
                    }
                }
                else
                {
                    printf("test-preprocessor: can't make %s\n", own);
                }
                test_process_exit(&processes[started], &counts, sizeof(counts));
            }
            started += 1;
        }
        PPTestChildCounts counts;
        const bool ran = test_process_finish(&processes[finished], &counts, sizeof(counts));
        if (!ran)
        {
            static const char* const works[] = { "header sets", "cases", "files" };
            printf("test-preprocessor: part %u of %u of the %s didn't run to the end\n", finished, parts, works[children->work]);
        }
        total.sets += counts.sets;
        total.failures += counts.failures;
        total.our_failures += counts.our_failures;
        total.order_failures += counts.order_failures;
        if (counts.system_inexact != 0 && total.system_inexact == 0)
        {
            snprintf(total.system_inexact_first, sizeof(total.system_inexact_first), "%s", counts.system_inexact_first);
        }
        total.system_units += counts.system_units;
        total.system_inexact += counts.system_inexact;
        total.system_tokens += counts.system_tokens;
        total.system_made += counts.system_made;
        total.tokens += counts.tokens;
        total.result = total.result && ran && counts.result;
    }
    return total;
}

// test-preprocessor's files: each of `files` (`file_count` of them) with the options `passed` (`passed_count`), by both
// kernels, and with `with_clang` against clang with the same options (pp_test_file), but --buster-headers, for which
// clang gets ours' search (pp_clang_arguments, with its links in `directory`). Adds the files' output tokens to
// `*tokens`. Returns how many differ: all of them if the options can't be read or clang's search can't be made.
static uint32_t pp_test_files(int passed_count, char** passed, int file_count, char** files, bool with_clang, const char* directory, uint64_t* tokens)
{
    Arena arena = arena_reserve(1ull << 30);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    // pp_parse_options wants one input: give it the first file, and use its options for every file.
    char** with_input = calloc((size_t)passed_count + 2, sizeof(char*));
    char ours[PATH_MAX];
    char links[4200];
    snprintf(links, sizeof(links), "%s/clang-search-XXXXXX", directory);
    bool linked = false;
    int clang_count = 0;
    bool ready = with_input != 0 && file_count != 0;
    if (ready)
    {
        memcpy(with_input, passed, (size_t)passed_count * sizeof(char*));
        with_input[passed_count] = files[0];
    }
    ready = ready && pp_parse_options(passed_count + 1, with_input, &options, &arena, &input, &output, &markers);
    if (ready)
    {
        options.clang_features = true;
    }
    char** clang = ready && with_clang ? pp_clang_arguments(&options, passed_count, passed, ours, links, &linked, &clang_count) : 0;
    ready = ready && (!with_clang || clang != 0);
    // clang's predefines for the options, which every file's run reads (pp_test_file), once for them all.
    char predefines[4096];
    snprintf(predefines, sizeof(predefines), "%s/predefines.h", directory);
    ready = ready && (!with_clang || pp_test_clang_predefines(clang, clang_count, predefines));
    // A part per file (or per range of them, past BUSTER_PP_TEST_CHILDREN files), at most one per CPU at once
    // (pp_test_children), their output in the files' order.
    const PPTestChildren children = {
        .work = PP_TEST_CHILD_FILES,
        .parts = file_count < BUSTER_PP_TEST_CHILDREN ? (uint32_t)file_count : BUSTER_PP_TEST_CHILDREN,
        .count = (uint32_t)file_count,
        .directory = directory,
        .links = linked ? links : 0,
        .with_clang = with_clang,
        .arguments = clang,
        .argument_count = clang_count,
        .options = &options,
        .files = files,
    };
    const PPTestChildCounts counts = ready ? pp_test_children(&children) : (PPTestChildCounts){ 0 };
    pp_test_system_units += counts.system_units;
    pp_test_system_inexact += counts.system_inexact;
    pp_test_system_tokens += counts.system_tokens;
    pp_test_system_made += counts.system_made;
    snprintf(pp_test_system_inexact_first, sizeof(pp_test_system_inexact_first), "%s", counts.system_inexact_first);
    *tokens += counts.tokens;
    const uint32_t result = ready ? counts.failures + !counts.result : (uint32_t)file_count;
    if (linked)
    {
        pp_clang_remove_links(links);
    }
    free(with_input);
    free(clang);
    arena_release(&arena);
    return result;
}

static bool pp_test_headers(const char* directory)
{
    char ours[PATH_MAX];
    bool result = pp_buster_headers_directory(ours, sizeof(ours), 0);
    if (!result)
    {
        printf("headers: no src/buster/include beside the executable's build directory\n");
    }
    char empty[4096];
    char links[4096];
    snprintf(empty, sizeof(empty), "%s/headers-empty.c", directory);
    snprintf(links, sizeof(links), "%s/clang-headers-XXXXXX", directory);
    result = result && pp_test_write_file(empty, "");
    const bool linked = result && pp_clang_fallback_links(ours, links);
    if (result && !linked)
    {
        printf("headers: can't link clang's resource headers into %s\n", links);
    }
    result = linked;
    const PPTestChildren children = {
        .work = PP_TEST_CHILD_HEADERS,
        .parts = BUSTER_PP_TEST_HEADER_SETS,
        .directory = directory,
        .ours = ours,
        .links = links,
        .empty = empty,
    };
    const PPTestChildCounts counts = result ? pp_test_children(&children) : (PPTestChildCounts){ 0 };
    result = result && counts.result;
    const uint32_t sets = counts.sets;
    const uint32_t failures = counts.failures;
    const uint32_t our_failures = counts.our_failures;
    const uint32_t order_failures = counts.order_failures;
    const uint32_t option_failures = result ? pp_test_header_options(directory, ours, links) : 0;
    const bool beside = result && pp_test_header_beside();
    const uint32_t file_failures = result ? pp_test_header_files(directory) : 0;
    const bool command = result && pp_test_clang_command(directory);
    unlink(empty);
    pp_clang_remove_links(links);
    printf("headers: %u of %u header, mode and hosting sets differ from clang's; our preprocessor differs from clang on %u of %lu modes with them; %u of %lu units "
           "among the C library's and clang's other headers differ; %u of 3 runs with -nobuiltininc, -nostdinc and neither find other headers than clang; "
           "quoted names beside clang's headers are found %s\n",
           failures, sets, our_failures, (unsigned long)(2 * BUSTER_ARRAY_LENGTH(pp_test_header_modes)), order_failures,
           (unsigned long)(4 * BUSTER_ARRAY_LENGTH(pp_test_header_modes)), option_failures,
           beside ? "where they should be" : "elsewhere");
    return result && failures == 0 && our_failures == 0 && order_failures == 0 && option_failures == 0 && beside && file_failures == 0 && command;
}

static bool pp_fuzz(const PPFuzzOptions* options);
static bool pp_fuzz_print_case(const PPFuzzOptions* options, uint64_t id);

// A decimal option's value (--fuzz, --seed, --jobs). Returns false if it isn't one.
static bool pp_test_number(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const unsigned long long parsed = text != 0 ? strtoull(text, &end, 10) : 0;
    const bool result = text != 0 && text[0] >= '0' && text[0] <= '9' && end != text && *end == 0 && errno == 0;
    if (result)
    {
        *value = parsed;
    }
    return result;
}

// The default capability inventory requires source checking and both required consumers; action folding alone
// remains private to its action API. Clang's oracle mode retains its independent frozen tables and raw operands.
static bool pp_test_implemented_features(const char* directory)
{
    static const char source[] =
        "A __has_builtin(__builtin_expect) __has_constexpr_builtin(__builtin_clz) __has_feature(c_static_assert) __has_extension(c_atomic) "
        "__has_attribute(packed) __has_attribute(__packed__) __has_c_attribute(gnu::packed) __has_warning(\"-Wreserved-identifier\") "
        "__is_identifier(int) __is_target_arch(x86_64)\n"
        "#define CLZ __builtin_clz\n#define __builtin_clz 0\nB __has_constexpr_builtin(CLZ) __has_constexpr_builtin(__builtin_clz)\n"
        "#define OWN_BUILTIN __is_target_arch\nC __has_builtin(OWN_BUILTIN) __has_builtin(__is_target_arch)\n"
        "#define __builtin_expect 0\nD __has_builtin(__builtin_expect) __has_builtin(__builtin_expect_typo)\n";
    static const char* const expected[2] =
    {
        "A 0 0 0 0 0 0 0 1 0 1 B 0 0 C 0 1 D 0 0\n",
        "A 1 1 1 1 1 1 1 1 0 1 B 0 1 C 0 1 D 1 0\n",
    };
    static const uint32_t standards[] =
    {
        2017,
        2023,
    };
    char path[4200];
    snprintf(path, sizeof(path), "%s/case.c", directory);
    pp_test_write_file(path, source);
    bool result = true;
    for (uint32_t mode = 0; mode < BUSTER_ARRAY_LENGTH(standards); mode += 1)
    {
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            for (uint32_t clang = 0; clang < 2; clang += 1)
            {
                char* arguments[2] = { path, 0 };
                Arena arena = arena_reserve(1ull << 20);
                PPOptions options;
                const char* input;
                const char* output;
                bool markers;
                bool passed = pp_parse_options(1, arguments, &options, &arena, &input, &output, &markers);
                options.language.standard = standards[mode];
                options.scalar = scalar != 0;
                options.clang_features = clang != 0;
                static InternTable table;
                static Preprocessor pp;
                PPSpellings ours = { 0 };
                PPSpellings wanted = { 0 };
                passed = passed && pp_test_run(&pp, &table, &options, path) && pp_stream_spellings(&pp, &ours) &&
                         pp_text_spellings((const uint8_t*)expected[clang], strlen(expected[clang]), pp.split_kinds, pp_test_lex_flags(&pp), &wanted) &&
                         pp_compare_spellings(&pp, &ours, &wanted, clang != 0 ? "clang's feature answers" : "implemented feature answers", "wanted");
                pp_release(&pp);
                intern_release(&table);
                arena_release(&ours.arena);
                arena_release(&wanted.arena);
                arena_release(&arena);
                result &= passed;
            }
        }
    }
    // This explicit acceptance inventory is independent of the catalog's generation macros. Recognized special
    // identities have no source capability; only the existing integer actions have standalone folding capability.
    static const struct
    {
        CBuiltinId id;
        const char* name;
        bool fold;
    } inventory[] =
    {
        { CBUILTIN_BSWAP16, "__builtin_bswap16", true },
        { CBUILTIN_BSWAP32, "__builtin_bswap32", true },
        { CBUILTIN_BSWAP64, "__builtin_bswap64", true },
        { CBUILTIN_CLZ, "__builtin_clz", true },
        { CBUILTIN_CLZL, "__builtin_clzl", true },
        { CBUILTIN_CLZLL, "__builtin_clzll", true },
        { CBUILTIN_CTZ, "__builtin_ctz", true },
        { CBUILTIN_CTZL, "__builtin_ctzl", true },
        { CBUILTIN_CTZLL, "__builtin_ctzll", true },
        { CBUILTIN_POPCOUNT, "__builtin_popcount", true },
        { CBUILTIN_POPCOUNTL, "__builtin_popcountl", true },
        { CBUILTIN_POPCOUNTLL, "__builtin_popcountll", true },
        { CBUILTIN_PARITY, "__builtin_parity", true },
        { CBUILTIN_PARITYL, "__builtin_parityl", true },
        { CBUILTIN_PARITYLL, "__builtin_parityll", true },
        { CBUILTIN_FFS, "__builtin_ffs", true },
        { CBUILTIN_FFSL, "__builtin_ffsl", true },
        { CBUILTIN_FFSLL, "__builtin_ffsll", true },
        { CBUILTIN_ADD_OVERFLOW, "__builtin_add_overflow", true },
        { CBUILTIN_SUB_OVERFLOW, "__builtin_sub_overflow", true },
        { CBUILTIN_MUL_OVERFLOW, "__builtin_mul_overflow", true },
        { CBUILTIN_VA_START, "__builtin_va_start", false },
        { CBUILTIN_C23_VA_START, "__builtin_c23_va_start", false },
        { CBUILTIN_VA_ARG, "__builtin_va_arg", false },
        { CBUILTIN_VA_COPY, "__builtin_va_copy", false },
        { CBUILTIN_VA_END, "__builtin_va_end", false },
        { CBUILTIN_OFFSETOF, "__builtin_offsetof", false },
        { CBUILTIN_EXPECT, "__builtin_expect", false },
        { CBUILTIN_UNREACHABLE, "__builtin_unreachable", false },
        { CBUILTIN_TRAP, "__builtin_trap", false },
        { CBUILTIN_ASSUME_ALIGNED, "__builtin_assume_aligned", false },
        { CBUILTIN_PREFETCH, "__builtin_prefetch", false },
        { CBUILTIN_CONSTANT_P, "__builtin_constant_p", false },
        { CBUILTIN_TYPES_COMPATIBLE_P, "__builtin_types_compatible_p", false },
        { CBUILTIN_CHOOSE_EXPR, "__builtin_choose_expr", false },
        { CBUILTIN_OBJECT_SIZE, "__builtin_object_size", false },
        { CBUILTIN_HUGE_VAL, "__builtin_huge_val", false },
        { CBUILTIN_HUGE_VALF, "__builtin_huge_valf", false },
        { CBUILTIN_HUGE_VALL, "__builtin_huge_vall", false },
        { CBUILTIN_INF, "__builtin_inf", false },
        { CBUILTIN_INFF, "__builtin_inff", false },
        { CBUILTIN_INFL, "__builtin_infl", false },
        { CBUILTIN_NAN, "__builtin_nan", false },
        { CBUILTIN_NANF, "__builtin_nanf", false },
        { CBUILTIN_NANL, "__builtin_nanl", false },
        { CBUILTIN_ISNAN, "__builtin_isnan", false },
        { CBUILTIN_ISINF, "__builtin_isinf", false },
        { CBUILTIN_ISFINITE, "__builtin_isfinite", false },
        { CBUILTIN_SIGNBIT, "__builtin_signbit", false },
        { CBUILTIN_FPCLASSIFY, "__builtin_fpclassify", false },
        { CBUILTIN_ISINF_SIGN, "__builtin_isinf_sign", false },
        { CBUILTIN_FABS, "__builtin_fabs", false },
        { CBUILTIN_FABSF, "__builtin_fabsf", false },
        { CBUILTIN_FABSL, "__builtin_fabsl", false },
        { CBUILTIN_FLT_ROUNDS, "__builtin_flt_rounds", false },
        { CBUILTIN_CREAL, "__builtin_creal", false },
        { CBUILTIN_CREALF, "__builtin_crealf", false },
        { CBUILTIN_CREALL, "__builtin_creall", false },
        { CBUILTIN_CIMAG, "__builtin_cimag", false },
        { CBUILTIN_CIMAGF, "__builtin_cimagf", false },
        { CBUILTIN_CIMAGL, "__builtin_cimagl", false },
        { CBUILTIN_SQRT, "__builtin_sqrt", false },
        { CBUILTIN_SQRTF, "__builtin_sqrtf", false },
        { CBUILTIN_COPYSIGN, "__builtin_copysign", false },
        { CBUILTIN_COPYSIGNF, "__builtin_copysignf", false },
        { CBUILTIN_CEIL, "__builtin_ceil", false },
        { CBUILTIN_CEILF, "__builtin_ceilf", false },
        { CBUILTIN_FLOOR, "__builtin_floor", false },
        { CBUILTIN_FLOORF, "__builtin_floorf", false },
        { CBUILTIN_TRUNC, "__builtin_trunc", false },
        { CBUILTIN_TRUNCF, "__builtin_truncf", false },
        { CBUILTIN_NEARBYINT, "__builtin_nearbyint", false },
        { CBUILTIN_NEARBYINTF, "__builtin_nearbyintf", false },
        { CBUILTIN_RINT, "__builtin_rint", false },
        { CBUILTIN_RINTF, "__builtin_rintf", false },
        { CBUILTIN_MEMCPY, "__builtin_memcpy", false },
        { CBUILTIN_MEMSET, "__builtin_memset", false },
        { CBUILTIN_MEMMOVE, "__builtin_memmove", false },
        { CBUILTIN_MEMCMP, "__builtin_memcmp", false },
        { CBUILTIN_STRLEN, "__builtin_strlen", false },
        { CBUILTIN_ALLOCA, "__builtin_alloca", false },
        { CBUILTIN_UMULL_OVERFLOW, "__builtin_umull_overflow", false },
        { CBUILTIN_ATOMIC_LOAD, "__atomic_load", false },
        { CBUILTIN_ATOMIC_LOAD_N, "__atomic_load_n", false },
        { CBUILTIN_ATOMIC_STORE, "__atomic_store", false },
        { CBUILTIN_ATOMIC_STORE_N, "__atomic_store_n", false },
        { CBUILTIN_ATOMIC_EXCHANGE, "__atomic_exchange", false },
        { CBUILTIN_ATOMIC_EXCHANGE_N, "__atomic_exchange_n", false },
        { CBUILTIN_ATOMIC_COMPARE_EXCHANGE, "__atomic_compare_exchange", false },
        { CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N, "__atomic_compare_exchange_n", false },
        { CBUILTIN_ATOMIC_FETCH_ADD, "__atomic_fetch_add", false },
        { CBUILTIN_ATOMIC_FETCH_SUB, "__atomic_fetch_sub", false },
        { CBUILTIN_ATOMIC_FETCH_AND, "__atomic_fetch_and", false },
        { CBUILTIN_ATOMIC_FETCH_XOR, "__atomic_fetch_xor", false },
        { CBUILTIN_ATOMIC_FETCH_OR, "__atomic_fetch_or", false },
        { CBUILTIN_ATOMIC_FETCH_NAND, "__atomic_fetch_nand", false },
        { CBUILTIN_ATOMIC_ADD_FETCH, "__atomic_add_fetch", false },
        { CBUILTIN_ATOMIC_SUB_FETCH, "__atomic_sub_fetch", false },
        { CBUILTIN_ATOMIC_AND_FETCH, "__atomic_and_fetch", false },
        { CBUILTIN_ATOMIC_XOR_FETCH, "__atomic_xor_fetch", false },
        { CBUILTIN_ATOMIC_OR_FETCH, "__atomic_or_fetch", false },
        { CBUILTIN_ATOMIC_NAND_FETCH, "__atomic_nand_fetch", false },
        { CBUILTIN_ATOMIC_TEST_AND_SET, "__atomic_test_and_set", false },
        { CBUILTIN_ATOMIC_CLEAR, "__atomic_clear", false },
        { CBUILTIN_ATOMIC_THREAD_FENCE, "__atomic_thread_fence", false },
        { CBUILTIN_ATOMIC_SIGNAL_FENCE, "__atomic_signal_fence", false },
        { CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE, "__atomic_always_lock_free", false },
        { CBUILTIN_ATOMIC_IS_LOCK_FREE, "__atomic_is_lock_free", false },
        { CBUILTIN_SYNC_FETCH_AND_ADD, "__sync_fetch_and_add", false },
        { CBUILTIN_SYNC_FETCH_AND_SUB, "__sync_fetch_and_sub", false },
        { CBUILTIN_SYNC_FETCH_AND_OR, "__sync_fetch_and_or", false },
        { CBUILTIN_SYNC_FETCH_AND_AND, "__sync_fetch_and_and", false },
        { CBUILTIN_SYNC_FETCH_AND_XOR, "__sync_fetch_and_xor", false },
        { CBUILTIN_SYNC_FETCH_AND_NAND, "__sync_fetch_and_nand", false },
        { CBUILTIN_SYNC_ADD_AND_FETCH, "__sync_add_and_fetch", false },
        { CBUILTIN_SYNC_SUB_AND_FETCH, "__sync_sub_and_fetch", false },
        { CBUILTIN_SYNC_OR_AND_FETCH, "__sync_or_and_fetch", false },
        { CBUILTIN_SYNC_AND_AND_FETCH, "__sync_and_and_fetch", false },
        { CBUILTIN_SYNC_XOR_AND_FETCH, "__sync_xor_and_fetch", false },
        { CBUILTIN_SYNC_NAND_AND_FETCH, "__sync_nand_and_fetch", false },
        { CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP, "__sync_bool_compare_and_swap", false },
        { CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP, "__sync_val_compare_and_swap", false },
        { CBUILTIN_SYNC_SYNCHRONIZE, "__sync_synchronize", false },
        { CBUILTIN_SYNC_LOCK_TEST_AND_SET, "__sync_lock_test_and_set", false },
        { CBUILTIN_SYNC_LOCK_RELEASE, "__sync_lock_release", false },
        { CBUILTIN_FRAME_ADDRESS, "__builtin_frame_address", false },
        { CBUILTIN_RETURN_ADDRESS, "__builtin_return_address", false },
        { CBUILTIN_EXTRACT_RETURN_ADDR, "__builtin_extract_return_addr", false },
        { CBUILTIN_FROB_RETURN_ADDR, "__builtin_frob_return_addr", false },
        { CBUILTIN_ELEMENTWISE_ABS, "__builtin_elementwise_abs", false },
        { CBUILTIN_ELEMENTWISE_ADD_SAT, "__builtin_elementwise_add_sat", false },
        { CBUILTIN_ELEMENTWISE_BITREVERSE, "__builtin_elementwise_bitreverse", false },
        { CBUILTIN_ELEMENTWISE_CLZG, "__builtin_elementwise_clzg", false },
        { CBUILTIN_ELEMENTWISE_FMA, "__builtin_elementwise_fma", false },
        { CBUILTIN_ELEMENTWISE_FSHL, "__builtin_elementwise_fshl", false },
        { CBUILTIN_ELEMENTWISE_FSHR, "__builtin_elementwise_fshr", false },
        { CBUILTIN_ELEMENTWISE_MAX, "__builtin_elementwise_max", false },
        { CBUILTIN_ELEMENTWISE_MIN, "__builtin_elementwise_min", false },
        { CBUILTIN_ELEMENTWISE_POPCOUNT, "__builtin_elementwise_popcount", false },
        { CBUILTIN_ELEMENTWISE_SQRT, "__builtin_elementwise_sqrt", false },
        { CBUILTIN_ELEMENTWISE_SUB_SAT, "__builtin_elementwise_sub_sat", false },
        { CBUILTIN_REDUCE_ADD, "__builtin_reduce_add", false },
        { CBUILTIN_REDUCE_AND, "__builtin_reduce_and", false },
        { CBUILTIN_REDUCE_MAX, "__builtin_reduce_max", false },
        { CBUILTIN_REDUCE_MIN, "__builtin_reduce_min", false },
        { CBUILTIN_REDUCE_MUL, "__builtin_reduce_mul", false },
        { CBUILTIN_REDUCE_OR, "__builtin_reduce_or", false },
        { CBUILTIN_SHUFFLEVECTOR, "__builtin_shufflevector", false },
        { CBUILTIN_CONVERTVECTOR, "__builtin_convertvector", false },
        { CBUILTIN_NONDETERMINISTIC_VALUE, "__builtin_nondeterministic_value", false },
        { CBUILTIN_NONTEMPORAL_LOAD, "__builtin_nontemporal_load", false },
        { CBUILTIN_NONTEMPORAL_STORE, "__builtin_nontemporal_store", false },
        { CBUILTIN_BIT_CAST, "__builtin_bit_cast", false },
    };
    result &= BUSTER_ARRAY_LENGTH(inventory) == BUSTER_ARRAY_LENGTH(cbuiltin_catalog);
    const uint32_t compile_capabilities = BUSTER_CBUILTIN_CAP_SOURCE | BUSTER_CBUILTIN_CAP_CONSTEXPR |
                                          BUSTER_CBUILTIN_CAP_CHECKER | BUSTER_CBUILTIN_CAP_TIER0 | BUSTER_CBUILTIN_CAP_TIER1;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cbuiltin_catalog); i += 1)
    {
        result &= (cbuiltin_catalog[i].capabilities & compile_capabilities) == 0;
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(inventory); i += 1)
    {
        const char* name = inventory[i].name;
        const uint32_t length = (uint32_t)strlen(name);
        result &= cbuiltin_resolve(name, length) == inventory[i].id;
        result &= cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_FOLD) == inventory[i].fold;
        result &= !cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_SOURCE);
        result &= !cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR);
        result &= !cbuiltin_has_capability((const uint8_t*)name, length, compile_capabilities);
        result &= !cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_FOLD | BUSTER_CBUILTIN_CAP_SOURCE);
        result &= !cbuiltin_has_capability((const uint8_t*)name, length, 0);
        result &= !cbuiltin_has_capability((const uint8_t*)name, length, 0x80);
        for (uint32_t previous = 0; previous < i; previous += 1)
        {
            result &= inventory[previous].id != inventory[i].id && strcmp(inventory[previous].name, name) != 0;
        }
        for (uint32_t mode = 0; mode < BUSTER_ARRAY_LENGTH(standards); mode += 1)
        {
            const PPOptions defaults = { .language = { .standard = standards[mode], .gnu = true } };
            result &= pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)name, length) == 0;
            result &= pp_feature(&defaults, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, (const uint8_t*)name, length) == 0;
        }
    }
    static const char* const own[] =
    {
        "__is_target_arch",
        "__is_target_vendor",
        "__is_target_os",
        "__is_target_environment",
    };
    const PPOptions defaults = { .language = { .standard = 2017, .gnu = true } };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(own); i += 1)
    {
        const uint32_t length = (uint32_t)strlen(own[i]);
        result &= pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)own[i], length) == 1;
        result &= pp_feature(&defaults, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, (const uint8_t*)own[i], length) == 0;
        result &= cbuiltin_resolve(own[i], length) == CBUILTIN_NONE;
    }
    static const char* const unknown[] =
    {
        "",
        "__builtin_",
        "__builtin_cl",
        "__builtin_clz_",
        "__builtin_clzllx",
        "_builtin_clz",
        "__BUILTIN_clz",
        "__builtin_va_start ",
        "__builtin_c23_va_startx",
        "__atomic_load_nn",
        "__sync_synchroniz",
        "__is_target_arch_",
        "__is_target_osx",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unknown); i += 1)
    {
        const uint32_t length = (uint32_t)strlen(unknown[i]);
        result &= cbuiltin_resolve(unknown[i], length) == CBUILTIN_NONE;
        result &= !cbuiltin_has_capability((const uint8_t*)unknown[i], length, BUSTER_CBUILTIN_CAP_FOLD);
        result &= !cbuiltin_has_capability((const uint8_t*)unknown[i], length, BUSTER_CBUILTIN_CAP_SOURCE);
        result &= !cbuiltin_has_capability((const uint8_t*)unknown[i], length, BUSTER_CBUILTIN_CAP_CONSTEXPR);
        result &= pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)unknown[i], length) == 0;
        result &= pp_feature(&defaults, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, (const uint8_t*)unknown[i], length) == 0;
    }
    // Every name ends immediately before an unreadable page, with no terminator or padding. Zero/oversized
    // lengths at the unreadable boundary must return without inspecting it.
    const long page_size = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = page_size > 0 ? mmap(0, (size_t)page_size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    if (mapping == MAP_FAILED)
    {
        result = false;
    }
    else
    {
        uint8_t* end = mapping + page_size;
        const bool guarded = mprotect(end, (size_t)page_size, PROT_NONE) == 0;
        result &= guarded;
        if (guarded)
        {
            result &= cbuiltin_resolve((const char*)end, 0) == CBUILTIN_NONE;
            result &= cbuiltin_resolve((const char*)end, UINT32_MAX) == CBUILTIN_NONE;
            result &= cbuiltin_resolve(0, 1) == CBUILTIN_NONE;
            result &= !cbuiltin_has_capability(end, 0, BUSTER_CBUILTIN_CAP_FOLD);
            result &= !cbuiltin_has_capability(end, UINT32_MAX, BUSTER_CBUILTIN_CAP_FOLD);
            result &= !cbuiltin_has_capability(0, 1, BUSTER_CBUILTIN_CAP_SOURCE);
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(inventory); i += 1)
            {
                const uint32_t length = (uint32_t)strlen(inventory[i].name);
                uint8_t* name = end - length;
                memcpy(name, inventory[i].name, length);
                result &= cbuiltin_resolve((const char*)name, length) == inventory[i].id;
                result &= cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_FOLD) == inventory[i].fold;
                result &= !cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_SOURCE);
                result &= !cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR);
                result &= pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, name, length) == 0;
                result &= pp_feature(&defaults, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, name, length) == 0;
                name[0] = '!';
                result &= cbuiltin_resolve((const char*)name, length) == CBUILTIN_NONE;
                result &= !cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_FOLD);
            }
        }
        munmap(mapping, (size_t)page_size * 2);
    }
    printf("feature answers and %zu catalog names: %s (source acceptance by default, clang's independent oracle mode)\n",
           BUSTER_ARRAY_LENGTH(inventory), result ? "as expected" : "WRONG");
    return result;
}

// c_predefines_test.c's checks and generator: main.c includes it after this file.
static bool predefines_test_run(const char* directory, uint64_t seed);
static bool predefines_test_print_tables(const char* directory);
static bool predefines_test_target(void);
static bool predefines_test_refused(void);

#include "c_preprocessor_history_test.c"
#include "c_preprocessor_directives_test.c"
#include "c_preprocessor_order_test.c"
#include "c_preprocessor_batch_test.c"
#include "c_preprocessor_semantic_test.c"
#include "c_preprocessor_windows_test.c"

// A semantic consumer diagnoses an original pragma operand after preprocessing has discarded its temporary
// buffers. Test that boundary directly, including argument prescan and distinct presumed definition/use files.
static bool pp_test_pragma_origins(const char* directory, bool with_clang)
{
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t line;
    } fixtures[] =
    {
        {
            "physical directive",
            "#line 41 \"pragma-origin-input.c\"\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\n"
            "#pragma STDC FENV_ACCESS MAYBE\nint value;\n",
            42,
        },
        {
            "direct operator",
            "#line 41 \"pragma-origin-input.c\"\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\n"
            "_Pragma(\"STDC FENV_ACCESS MAYBE\")\nint value;\n",
            42,
        },
        {
            "macro body operator",
            "#line 11 \"pragma-origin-definition.h\"\n#define CHECK_PRAGMA _Pragma(\"STDC FENV_ACCESS MAYBE\")\n"
            "#line 41 \"pragma-origin-input.c\"\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\nCHECK_PRAGMA\nint value;\n",
            42,
        },
        {
            "forwarded argument operator",
            "#line 11 \"pragma-origin-definition.h\"\n#define CHECK_PRAGMA _Pragma(\"STDC FENV_ACCESS MAYBE\")\n#define FORWARD(x) x\n"
            "#line 41 \"pragma-origin-input.c\"\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\nFORWARD(CHECK_PRAGMA)\nint value;\n",
            42,
        },
        {
            "multiline forwarded argument operator",
            "#line 11 \"pragma-origin-definition.h\"\n#define CHECK_PRAGMA _Pragma(\"STDC FENV_ACCESS MAYBE\")\n#define FORWARD(x) x\n"
            "#line 41 \"pragma-origin-input.c\"\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\nFORWARD(\nCHECK_PRAGMA\n)\nint value;\n",
            43,
        },
    };
    char path[4096];
    snprintf(path, sizeof(path), "%s/pragma-origin.c", directory);
    uint32_t failures = 0;
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        bool passed = pp_test_write_file(path, fixtures[fixture].source);
        static Preprocessor runs[2];
        static InternTable tables[2];
        char* diagnostics[2] = { 0 };
        for (uint32_t mode = 0; passed && mode < 2; mode += 1)
        {
            size_t length = 0;
            FILE* stream = open_memstream(diagnostics + mode, &length);
            const PPOptions options =
            {
                .language = { .standard = 2017, .gnu = true },
                .scalar = mode != 0,
                .semantic_pragmas = true,
                .diagnostics = stream,
            };
            bool valid = stream != NULL && intern_initialize(tables + mode) && pp_initialize(runs + mode, &options, tables + mode) &&
                         pp_run(runs + mode, path);
            uint32_t diagnosed = 0;
            for (uint32_t token = 0; valid && token < runs[mode].output.count; token += 1)
            {
                if (pp_token_is_name(runs + mode, &runs[mode].output, token, runs[mode].output.count, "MAYBE"))
                {
                    pp_diagnose_output(runs + mode, PP_SEVERITY_ERROR, token, "expected 'ON' or 'OFF' or 'DEFAULT' in pragma");
                    diagnosed += 1;
                }
            }
            if (stream != NULL)
            {
                valid = fclose(stream) == 0 && valid;
            }
            char file[512] = { 0 };
            uint32_t line = 0;
            uint32_t column = 0;
            valid = valid && diagnosed == 1 && diagnostics[mode] != NULL &&
                    sscanf(diagnostics[mode], "%511[^:]:%u:%u:", file, &line, &column) == 3 &&
                    strcmp(file, "pragma-origin-input.c") == 0 && line == fixtures[fixture].line && column != 0;
            passed &= valid;
        }
        passed = passed && pp_same_streams(runs, runs + 1, fixtures[fixture].name) && strcmp(diagnostics[0], diagnostics[1]) == 0;
        for (uint32_t token = 0; passed && token < runs[0].output.count; token += 1)
        {
            passed = pp_history_at(runs, token) == pp_history_at(runs + 1, token);
        }
        char* oracle = passed && with_clang ? pp_test_clang_diagnostics(path, directory, true) : NULL;
        if (passed && with_clang)
        {
            char file[512] = { 0 };
            uint32_t line = 0;
            uint32_t column = 0;
            passed = oracle != NULL && sscanf(oracle, "%511[^:]:%u:%u:", file, &line, &column) == 3 &&
                     strcmp(file, "pragma-origin-input.c") == 0 && line == fixtures[fixture].line && column != 0;
        }
        if (!passed)
        {
            printf("pragma origin %s differs: %s\n", fixtures[fixture].name, diagnostics[0] != NULL ? diagnostics[0] : "unavailable");
        }
        failures += !passed;
        free(oracle);
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            free(diagnostics[mode]);
            pp_release(runs + mode);
            intern_release(tables + mode);
        }
    }
    unlink(path);
    printf("pragma origins: %u of %zu fail (scalar/vector%s)\n", failures, BUSTER_ARRAY_LENGTH(fixtures), with_clang ? ", Clang first file/line" : "");
    const bool result = failures == 0;
    return result;
}

static int run_preprocessor_fixture_tests(int argument_count, char** arguments)
{
    bool with_clang = false;
    bool keep = false;
    // The fuzz batch (c_preprocessor_fuzz.c), which runs with the cases: --fuzz cases per mode, --seed, --std for one
    // mode, --jobs workers.
    PPFuzzOptions fuzz = {
        .count = BUSTER_PP_FUZZ_CASES,
        .seed = 1,
        .modes = {
            true,
            true,
        },
        .clang_path = "clang",
        .jobs = 1,
    };
    bool generate_targets = false;
    uint64_t id_order = 153;
    bool valid = true;
    // --print-case: print that fuzz case (as clang reads it), and nothing else.
    bool print_case = false;
    uint64_t printed_case = 0;
    // The arguments without ours, for clang and pp_parse_options; the files are those that don't start with '-' and
    // aren't an option's value.
    bool tables = false;
    uint64_t count = BUSTER_PP_DIRECTIVE_TEST_SOURCES;
    char** passed = calloc((size_t)argument_count + 1, sizeof(char*));
    char** clang_passed = calloc((size_t)argument_count + 1, sizeof(char*));
    char** files = calloc((size_t)argument_count + 1, sizeof(char*));
    int passed_count = 0;
    int clang_passed_count = 0;
    int file_count = 0;
    for (int i = 0; passed != 0 && clang_passed != 0 && files != 0 && i < argument_count; i += 1)
    {
        const char* argument = arguments[i];
        const char* next = i + 1 < argument_count ? arguments[i + 1] : 0;
        const bool ours = pp_own_option_with_value(argument);
        const bool takes_value = ours || strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                 strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-include") == 0 || strcmp(argument, "-target") == 0;
        uint64_t number = 0;
        if (strcmp(argument, "--id-order") == 0 && i + 1 < argument_count)
        {
            char* end;
            errno = 0;
            i += 1;
            id_order = strtoull(arguments[i], &end, 10);
            valid = valid && errno == 0 && end != arguments[i] && *end == 0 && arguments[i][0] != '-';
        }
        else if (strcmp(argument, "--id-order") == 0)
        {
            valid = false;
        }
        else if (strcmp(argument, "--clang") == 0)
        {
            with_clang = true;
        }
        else if (strcmp(argument, "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argument, "--fuzz") == 0 || strcmp(argument, "--seed") == 0 || strcmp(argument, "--jobs") == 0 || strcmp(argument, "--first") == 0)
        {
            valid = valid && pp_test_number(next, &number);
            fuzz.count = argument[2] == 'f' && argument[3] == 'u' ? number : fuzz.count;
            fuzz.first = argument[2] == 'f' && argument[3] == 'i' ? number : fuzz.first;
            fuzz.seed = argument[2] == 's' ? number : fuzz.seed;
            fuzz.jobs = argument[2] == 'j' ? (uint32_t)(number < UINT32_MAX ? number : UINT32_MAX) : fuzz.jobs;
            i += 1;
        }
        else if (strcmp(argument, "--print-case") == 0)
        {
            print_case = true;
            valid = valid && pp_test_number(next, &printed_case);
            i += 1;
        }
        else if (strcmp(argument, "--std") == 0)
        {
            fuzz.modes[0] = next != 0 && strcmp(next, "c17") == 0;
            fuzz.modes[1] = next != 0 && strcmp(next, "c23") == 0;
            valid = valid && (fuzz.modes[0] || fuzz.modes[1]);
            i += 1;
        }
        else if (strcmp(argument, "--generate-targets") == 0)
        {
            generate_targets = true;
        }
        else if (strcmp(argument, "--tables") == 0)
        {
            tables = true;
        }
        else if (strcmp(argument, "--count") == 0 && i + 1 < argument_count)
        {
            i += 1;
            count = strtoull(arguments[i], 0, 10);
        }
        else if (argument[0] == '-')
        {
            passed[passed_count++] = arguments[i];
            if (!ours)
            {
                clang_passed[clang_passed_count++] = arguments[i];
            }
            if (takes_value && i + 1 < argument_count)
            {
                i += 1;
                passed[passed_count++] = arguments[i];
                if (!ours)
                {
                    clang_passed[clang_passed_count++] = arguments[i];
                }
            }
        }
        else
        {
            files[file_count++] = arguments[i];
        }
    }
    fuzz.with_clang = with_clang;
    if (!valid || fuzz.jobs == 0 || fuzz.jobs > BUSTER_PP_FUZZ_MAX_JOBS)
    {
        fprintf(stderr, "usage: test-preprocessor [--clang] [--keep] [--id-order N] [--fuzz N] [--first N] [--seed N] [--std c17|c23] [--jobs N] [--print-case N] [options] [file...]\n");
    }
    char directory[4096];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-pp-test-XXXXXX", temporary != 0 ? temporary : "/tmp");
    const bool created = valid && !print_case && fuzz.jobs != 0 && fuzz.jobs <= BUSTER_PP_FUZZ_MAX_JOBS && passed != 0 && clang_passed != 0 && files != 0 && mkdtemp(directory) != 0;
    bool passed_all = created;
    if (passed_all && generate_targets)
    {
        // c_predefines.c's generated tables, from clang (c_predefines_test.c).
        passed_all = predefines_test_print_tables(directory);
    }
    else if (passed_all && file_count == 0)
    {
        // The cases in parts that run at once (pp_test_children), each in a directory of its own, their output in the
        // cases' order.
        const PPTestChildren children = {
            .work = PP_TEST_CHILD_CASES,
            .parts = BUSTER_PP_TEST_CASE_PARTS,
            .directory = directory,
            .with_clang = with_clang,
        };
        const PPTestChildCounts cases = pp_test_children(&children);
        pp_test_system_units += cases.system_units;
        pp_test_system_inexact += cases.system_inexact;
        pp_test_system_tokens += cases.system_tokens;
        pp_test_system_made += cases.system_made;
        snprintf(pp_test_system_inexact_first, sizeof(pp_test_system_inexact_first), "%s", cases.system_inexact_first);
        const uint32_t failures = cases.failures + !cases.result;
        printf("cases: %u of %lu fail\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(pp_test_cases));
        printf("system levels: %llu units by their runs and their history alike, %llu whose runs can't say%s%s%s; %llu tokens at a system level, %llu made "
               "by macros\n",
               (unsigned long long)pp_test_system_units, (unsigned long long)pp_test_system_inexact, pp_test_system_inexact != 0 ? " (the first: " : "",
               pp_test_system_inexact_first, pp_test_system_inexact != 0 ? ")" : "", (unsigned long long)pp_test_system_tokens,
               (unsigned long long)pp_test_system_made);
        passed_all = failures == 0;
        passed_all &= pp_test_system_case_levels(directory);
        passed_all &= pp_test_macro_batches(directory, with_clang);
        passed_all &= pp_test_semantic_pragmas(directory, with_clang);
        passed_all &= pp_test_timestamps(directory, with_clang);
        passed_all &= pp_test_implemented_features(directory);
        passed_all &= predefines_test_target();
        passed_all &= predefines_test_refused();
        passed_all &= pp_test_history(directory, with_clang);
        passed_all &= pp_test_pragma_origins(directory, with_clang);
        passed_all &= pp_test_embed_text(fuzz.seed);
        passed_all &= pp_test_embed_lookups(directory, with_clang);
        passed_all &= pp_test_lookup_accounting(directory);
        passed_all &= pp_test_final_copy_accounting();
        passed_all &= pp_test_trailing_splices();
        passed_all &= pp_test_scalar_reference(directory);
        passed_all &= pp_test_windows(fuzz.seed);
        passed_all &= pp_test_line_ends();
        passed_all &= pp_test_window_units(directory, fuzz.seed);
        passed_all &= pp_test_directives(directory, with_clang, fuzz.seed, count);
        passed_all &= pp_test_id_order(directory, id_order);
        passed_all &= pp_test_assembly_printing(directory);
        passed_all &= pp_test_raw_query_views(directory);
        passed_all &= pp_test_raw_query_capacity(directory, with_clang);
        passed_all &= pp_test_raw_recovery_failures();
        passed_all &= pp_test_named_ucns(directory, with_clang);
        passed_all &= pp_test_named_ucn_scaling(directory);
        passed_all &= pp_test_named_ucn_boundaries(directory);
        passed_all &= pp_test_prelude_failures();
        passed_all &= pp_test_option_refusals(directory, with_clang);
        if (with_clang)
        {
            passed_all &= pp_test_predefines(directory);
            passed_all &= pp_test_headers(directory);
            passed_all &= predefines_test_run(directory, fuzz.seed);
            passed_all &= pp_test_keywords(directory);
            passed_all &= pp_test_features(directory);
            passed_all &= pp_test_option_features(directory);
        }
        if (fuzz.count != 0)
        {
            fflush(stdout);
            passed_all &= pp_fuzz(&fuzz);
        }
    }
    else if (passed_all && tables)
    {
        passed_all = pp_directive_test_paths(files, file_count);
    }
    else if (passed_all)
    {
        uint64_t tokens = 0;
        const uint32_t failures = pp_test_files(passed_count, passed, file_count, files, with_clang, directory, &tokens);
        printf("files: %u of %d differ (%lu tokens)\n", failures, file_count, (unsigned long)tokens);
        printf("system levels: %llu units by their runs and their history alike, %llu whose runs can't say%s%s%s; %llu tokens at a system level, %llu made "
               "by macros\n",
               (unsigned long long)pp_test_system_units, (unsigned long long)pp_test_system_inexact, pp_test_system_inexact != 0 ? " (the first: " : "",
               pp_test_system_inexact_first, pp_test_system_inexact != 0 ? ")" : "", (unsigned long long)pp_test_system_tokens,
               (unsigned long long)pp_test_system_made);
        passed_all = failures == 0;
    }
    if (valid && print_case)
    {
        passed_all = pp_fuzz_print_case(&fuzz, printed_case);
    }
    else if (!keep && created)
    {
        // The files the cases and the clang runs write, then the directory.
        static const char* const names[] = { "case.c", "other.h", "predefines.h" };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            char path[4200];
            snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
            unlink(path);
        }
        rmdir(directory);
    }
    free(passed);
    free(clang_passed);
    free(files);
    return passed_all ? 0 : 1;
}
