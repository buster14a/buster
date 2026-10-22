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
// start from the same macros. The first difference is printed with the tokens around it. Then, with our own
// predefines, the macros defined at the file's end (-dM) must be `clang -dM -E`'s (pp_test_macros). In both, the
// vectorized printer's text must be the scalar printer's byte for byte, with line markers and without, and must lex
// back into the stream's tokens.
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
#include <errno.h>

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
// line's identifiers as written (pp_spelled_as_written).
static bool pp_stream_spellings(Preprocessor* pp, PPSpellings* spellings)
{
    const PPTokens* out = &pp->output;
    Scratch scratch = scratch_begin(0);
    uint64_t bytes = 0;
    uint32_t pragma = BUSTER_PP_PRAGMA_OUTSIDE;
    bool result = true;
    for (uint32_t i = 0; result && i < out->count; i += 1)
    {
        const uint8_t kind = out->kinds[i];
        const uint64_t mark = scratch.arena->length;
        const uint8_t* written = 0;
        bytes += pp_spelled_as_written(pragma, kind) ? pp_written_spelling(pp, out->values[i], out->locations[i], scratch.arena, &written)
                 : kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END) ? intern_length(pp->table, out->values[i])
                 : kind == BUSTER_PP_KIND_PRAGMA_END ? 0 : kind == BUSTER_PP_KIND_INTEGER ? 24 : out->values[i] - out->locations[i];
        result = !pp_spelled_as_written(pragma, kind) || written != 0;
        arena_pop_to(scratch.arena, mark);
        pragma = pp_pragma_state(pp, pragma, kind, out->locations[i]);
    }
    result = result && pp_spellings_initialize(spellings, (uint64_t)out->count + 1, bytes + 64);
    uint8_t* buffer = arena_push(scratch.arena, bytes + 64, 1);
    result = result && buffer != 0;
    pragma = BUSTER_PP_PRAGMA_OUTSIDE;
    for (uint32_t i = 0; result && i < out->count; i += 1)
    {
        const uint8_t kind = out->kinds[i];
        if (kind != BUSTER_PP_KIND_PRAGMA_END)
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
        result = x->prepared == y->prepared && x->directive_count == y->directive_count && x->guard == y->guard;
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

// The text -E prints for a run, which must lex into the stream's spellings; the vectorized printer's must be the scalar
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
        result = pp_compare_spellings(pp, ours, &printed, label, "-E");
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
static char* pp_test_clang(char** arguments, int argument_count, const char* path, const char* predefines_path, size_t* length)
{
    char* result = 0;
    *length = 0;
    bool ready = true;
    if (predefines_path != 0)
    {
        PPClangOutput macros = pp_test_clang_process("clang", arguments, argument_count, "/dev/null", true);
        ready = macros.status == PP_CLANG_ACCEPTED;
        FILE* out = ready ? fopen(predefines_path, "w") : 0;
        ready = out != 0 && fwrite(macros.text, 1, macros.length, out) == macros.length;
        if (out != 0)
        {
            ready = fclose(out) == 0 && ready;
        }
        free(macros.text);
        free(macros.diagnostics);
    }
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

static const PPTestFile pp_test_files_stack[] = {
    { "a.h", "a_token\n#include \"b.h\"\na_after\n" },
    { "b.h", "b_token\n" },
    { "sys/s.h", "sys_first\n\n\n\n\n\n\n\n\n\n\nsys_far\n" },
    { "p.h", "user_before\n#pragma GCC system_header\nuser_after\n" },
    { 0, 0 },
};

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
    { "written pragma spelling with dollar punctuation", "#pragma ppf a$b\n", 0, "gnu17", "#pragma ppf a $ b\n", false, 0, "-fno-dollars-in-identifiers", 0 },
    { "written pragma spelling with dollar punctuation in assembly", "#pragma ppf a$b\n", 0, "gnu17", "#pragma ppf a $ b\n", false, 0, "-x assembler-with-cpp", 0 },
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
};

// Writes a text to a file; returns whether it could.
static bool pp_test_write_file(const char* path, const char* text)
{
    FILE* file = fopen(path, "w");
    const bool result = file != 0 && fputs(text, file) >= 0;
    if (file != 0)
    {
        fclose(file);
    }
    return result;
}

// Writes a case's files into its directory (other.h, and the others with their directories); returns the main file's
// path (in `path`).
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
        const char* slash = strchr(test->files[i].path, '/');
        if (slash != 0)
        {
            snprintf(other, sizeof(other), "%s/%.*s", directory, (int)(slash - test->files[i].path), test->files[i].path);
            mkdir(other, 0700);
        }
        snprintf(other, sizeof(other), "%s/%s", directory, test->files[i].path);
        result = pp_test_write_file(other, test->files[i].text);
    }
    return result;
}

// Removes the files pp_test_write_case wrote besides case.c, and their directories.
static void pp_test_remove_case(const PPTestCase* test, const char* directory)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/other.h", directory);
    unlink(path);
    for (uint32_t i = 0; test->files != 0 && test->files[i].path != 0; i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, test->files[i].path);
        unlink(path);
        const char* slash = strchr(test->files[i].path, '/');
        if (slash != 0)
        {
            snprintf(path, sizeof(path), "%s/%.*s", directory, (int)(slash - test->files[i].path), test->files[i].path);
            rmdir(path);
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
                 ran[0] != test->fails;
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
                     pp_compare_spellings(&runs[0], &ours, &theirs.spellings, test->name, "clang") && pp_test_placed(&runs[0], &theirs, test->name);
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
    bool result = ran[0] && ran[1] && pp_same_streams(&runs[0], &runs[1], path) && pp_same_directive_tables(&runs[0], &runs[1], path);
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

// One file, by both kernels, and with --clang against clang with the same options: the tokens, their places, and the
// inclusions skipped by their guards (#120), as many as clang's multiple-include optimization skips.
static bool pp_test_file(int argument_count, char** arguments, const PPOptions* base, const char* path, bool with_clang, const char* directory, uint64_t* tokens)
{
    PPOptions options = *base;
    char predefines[4096];
    snprintf(predefines, sizeof(predefines), "%s/predefines.h", directory);
    size_t length = 0;
    char* text = with_clang ? pp_test_clang(arguments, argument_count, path, predefines, &length) : 0;
    options.predefines_path = with_clang ? predefines : options.predefines_path;
    uint64_t guards = 0;
    bool result = (!with_clang || text != 0) && pp_test_file_output(&options, path, with_clang, text, length, tokens, &guards);
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
        passed = passed && pp_test_file(n, arguments, &options, path, true, directory, &tokens);
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
    { { "-std=c17", "-fno-raw-string-literals", "-fraw-string-literals" }, "-fraw-string-literals" },
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
    "#define R RR\nR\"x(raw)x\"\n"
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
            const bool same = text != 0 && pp_test_file_output(&with_predefines, path, true, text, length, &tokens);
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
    result = result && pp_test_file(count, arguments, &options, path, true, directory, &tokens);
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

// __TIMESTAMP__ is the modification time of the file it is read in (local time, here UTC), or SOURCE_DATE_EPOCH's time
// when that is set: a file and the header it includes, written with times of their own, give each theirs (both kernels,
// and with --clang clang too). The environment is restored after.
static bool pp_test_timestamps(const char* directory, bool with_clang)
{
    static const struct
    {
        const char* epoch;
        const char* expected;
    } runs[] = {
        { 0, "\"Sat Jan  1 00:00:00 2000\"\n\"Mon Jan  1 00:00:00 2001\"\n" },
        { "1234567890", "\"Fri Feb 13 23:31:30 2009\"\n\"Fri Feb 13 23:31:30 2009\"\n" },
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

// Outside the oracle's mode, the feature queries answer what buster implements (pp_implemented_*): of builtins only the
// preprocessor's __is_target_*, and no attribute, feature, extension or constexpr builtin; __has_warning answers as
// clang does in both modes, and so do the queries that aren't about the compiler (__has_include, __is_identifier,
// __is_target_*). With --clang-features, the same lines give clang's answers. Neither expands __has_constexpr_builtin's
// name, a macro's or not.
static bool pp_test_implemented_features(const char* directory)
{
    static const char source[] =
        "A __has_builtin(__builtin_expect) __has_constexpr_builtin(__builtin_clz) __has_feature(c_static_assert) __has_extension(c_atomic) "
        "__has_attribute(packed) __has_attribute(__packed__) __has_c_attribute(gnu::packed) __has_warning(\"-Wreserved-identifier\") "
        "__is_identifier(int) __is_target_arch(x86_64)\n"
        "#define CLZ __builtin_clz\n#define __builtin_clz 0\nB __has_constexpr_builtin(CLZ) __has_constexpr_builtin(__builtin_clz)\n";
    static const char* const expected[2] = { "A 0 0 0 0 0 0 0 1 0 1 B 0 0\n", "A 1 1 1 1 1 1 1 1 0 1 B 0 1\n" };
    char path[4200];
    snprintf(path, sizeof(path), "%s/case.c", directory);
    pp_test_write_file(path, source);
    bool result = true;
    for (uint32_t clang = 0; clang < 2; clang += 1)
    {
        char* arguments[2] = { path, 0 };
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        bool passed = pp_parse_options(1, arguments, &options, &arena, &input, &output, &markers);
        options.clang_features = clang == 1;
        static InternTable table;
        static Preprocessor pp;
        PPSpellings ours = { 0 };
        PPSpellings wanted = { 0 };
        passed = passed && pp_test_run(&pp, &table, &options, path) && pp_stream_spellings(&pp, &ours) &&
                 pp_text_spellings((const uint8_t*)expected[clang], strlen(expected[clang]), pp.split_kinds, pp_test_lex_flags(&pp), &wanted) &&
                 pp_compare_spellings(&pp, &ours, &wanted, clang == 1 ? "clang's feature answers" : "implemented feature answers", "wanted");
        pp_release(&pp);
        intern_release(&table);
        arena_release(&ours.arena);
        arena_release(&wanted.arena);
        arena_release(&arena);
        result &= passed;
    }
    // The preprocessor's own builtins are what __has_builtin answers 1 for by default.
    static const char* const own[] = { "__is_target_arch", "__is_target_vendor", "__is_target_os", "__is_target_environment" };
    const PPOptions defaults = { .language = { .standard = 2017, .gnu = true } };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(own); i += 1)
    {
        result &= pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)own[i], (uint32_t)strlen(own[i])) == 1;
    }
    printf("feature answers: %s (what buster implements by default, clang's with --clang-features)\n", result ? "as expected" : "WRONG");
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
        uint32_t failures = 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp_test_cases); i += 1)
        {
            failures += !pp_test_case(&pp_test_cases[i], directory, with_clang);
        }
        printf("cases: %u of %lu fail\n", failures, (unsigned long)BUSTER_ARRAY_LENGTH(pp_test_cases));
        passed_all = failures == 0;
        passed_all &= pp_test_timestamps(directory, with_clang);
        passed_all &= pp_test_implemented_features(directory);
        passed_all &= predefines_test_target();
        passed_all &= predefines_test_refused();
        passed_all &= pp_test_history(directory, with_clang);
        passed_all &= pp_test_directives(directory, with_clang, fuzz.seed, count);
        passed_all &= pp_test_id_order(directory, id_order);
        passed_all &= pp_test_assembly_printing(directory);
        passed_all &= pp_test_option_refusals(directory, with_clang);
        if (with_clang)
        {
            passed_all &= pp_test_predefines(directory);
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
        Arena arena = arena_reserve(1ull << 30);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        // pp_parse_options wants one input: give it the first file, and use its options for every file.
        char** with_input = calloc((size_t)passed_count + 2, sizeof(char*));
        memcpy(with_input, passed, (size_t)passed_count * sizeof(char*));
        with_input[passed_count] = files[0];
        passed_all = pp_parse_options(passed_count + 1, with_input, &options, &arena, &input, &output, &markers);
        // The files are compared with clang (with --clang) and taken from its corpora: the feature queries answer as
        // clang's.
        options.clang_features = true;
        uint32_t failures = 0;
        uint64_t tokens = 0;
        for (int i = 0; passed_all && i < file_count; i += 1)
        {
            failures += !pp_test_file(clang_passed_count, clang_passed, &options, files[i], with_clang, directory, &tokens);
        }
        printf("files: %u of %d differ (%lu tokens)\n", failures, file_count, (unsigned long)tokens);
        passed_all = passed_all && failures == 0;
        free(with_input);
        arena_release(&arena);
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
