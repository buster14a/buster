// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// The preprocessor tests (c_preprocessor.c), for the test-preprocessor command:
//
//   build/compiler test-preprocessor [--clang] [--keep] [--fuzz N] [--seed N] [--std c17|c23] [--jobs N] [--fixtures DIR] [clang-style options] [file...]
//
// Without files, it runs the built-in cases (pp_test_cases, runtime data under tests/preprocessor since #2053; the
// comment above pp_test_fixture_root): small sources on the topics of the C standard's examples
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

#include "buster/compiler/test_deal.h"
#include "buster/compiler/test_paths.h"
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
#include <sys/resource.h>

#define BUSTER_PP_TEST_CONTEXT 6
#define BUSTER_PP_TEST_MAX_OUTPUT (1ull << 32)

// Keep the frontend's fixtures out of the other unity units. The Clang release checks also serve the checker,
// and the process helpers below serve its corpus; standalone preprocessing still reads the complete file.
#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND || BUSTER_UNIT == BUSTER_UNIT_CHECKER

// The Clang release the library-builtin table is pinned to (c_builtin_library.c's BUSTER_CLANG_REFERENCE_VERSION), read
// from `clang --version` by test-preprocessor --clang and test-checker-driver: a clang that isn't an official build of
// that release can answer some plain names' __has_builtin and implicit-declaration diagnostics differently, which those
// tests report as a difference and skip where the answer is known to depend on the build, rather than as a buster bug.
// Every upstream 23.x release has strlcat and strlcpy as GNU library builtins; Debian's and Ubuntu's clang packages
// (apt.llvm.org's too) remove them with debian/patches/23-strlcpy_strlcat_warning_removed.diff, whatever the release.
typedef struct ClangReferenceVersion ClangReferenceVersion;
struct ClangReferenceVersion
{
    bool named;
    bool matches;
    char version[64];
};

// The build `clang --version`'s first line names and whether it is an official build of the pinned reference release:
// its version ("clang version 23.1.4"), after the vendor word a distribution's build puts first ("Ubuntu clang version
// 23.1.6 (++20261103...)" is "Ubuntu 23.1.6"), since a vendor's build may patch clang's tables whatever its release.
static ClangReferenceVersion clang_reference_parse(const char* text)
{
    static const char prefix[] = "clang version ";
    ClangReferenceVersion result = { 0 };
    const char* found = text != NULL ? strstr(text, prefix) : NULL;
    if (found != NULL)
    {
        // The vendor: the line's text before "clang version ", without its trailing blanks.
        const char* line = found;
        while (line > text && line[-1] != '\n')
        {
            line -= 1;
        }
        size_t vendor = (size_t)(found - line);
        while (vendor != 0 && (line[vendor - 1] == ' ' || line[vendor - 1] == '\t'))
        {
            vendor -= 1;
        }
        found += sizeof(prefix) - 1;
        size_t length = strcspn(found, " \t\r\n(");
        const int written = snprintf(result.version, sizeof(result.version), "%.*s%s%.*s", (int)vendor, line, vendor != 0 ? " " : "", (int)length, found);
        result.named = length != 0 && written > 0 && (size_t)written < sizeof(result.version);
        result.version[result.named ? (size_t)written : 0] = 0;
        result.matches = result.named && strcmp(result.version, BUSTER_CLANG_REFERENCE_VERSION) == 0;
    }
    return result;
}

// Whether `text` names the reference release, into `version`, and the message that names both releases when it doesn't
// ("expected clang 23.1.4, found Ubuntu 23.1.6").
static bool clang_reference_compare(const char* text, char* version, size_t version_capacity, char* message, size_t message_capacity)
{
    const ClangReferenceVersion parsed = clang_reference_parse(text);
    if (version_capacity != 0)
    {
        snprintf(version, version_capacity, "%s", parsed.named ? parsed.version : "unavailable");
    }
    if (message_capacity != 0)
    {
        if (parsed.named)
        {
            snprintf(message, message_capacity, "expected clang %s, found %s", BUSTER_CLANG_REFERENCE_VERSION, parsed.version);
        }
        else
        {
            snprintf(message, message_capacity, "expected clang %s, found no clang version", BUSTER_CLANG_REFERENCE_VERSION);
        }
    }
    return parsed.matches;
}

// `clang_path --version`'s output, or NULL when it can't run.
static char* clang_reference_output(const char* clang_path)
{
    char* result = NULL;
    int descriptors[2] = { -1, -1 };
    const bool ready = clang_path != NULL && pipe(descriptors) == 0;
    const pid_t child = ready ? fork() : -1;
    if (ready && child == 0)
    {
        close(descriptors[0]);
        const bool redirected = dup2(descriptors[1], STDOUT_FILENO) >= 0 && dup2(descriptors[1], STDERR_FILENO) >= 0;
        close(descriptors[1]);
        if (redirected)
        {
            execlp(clang_path, clang_path, "--version", (char*)NULL);
        }
        _exit(127);
    }
    if (ready)
    {
        close(descriptors[1]);
        if (child > 0)
        {
            char text[4096];
            size_t length = 0;
            ssize_t count = 0;
            while (length + 1 < sizeof(text) && (count = read(descriptors[0], text + length, sizeof(text) - 1 - length)) != 0)
            {
                if (count > 0)
                {
                    length += (size_t)count;
                }
                else if (errno != EINTR)
                {
                    break;
                }
            }
            close(descriptors[0]);
            int status = 0;
            pid_t waited = -1;
            do
            {
                waited = waitpid(child, &status, 0);
            }
            while (waited < 0 && errno == EINTR);
            if (waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0)
            {
                text[length] = 0;
                result = strdup(text);
            }
        }
        else
        {
            close(descriptors[0]);
        }
    }
    return result;
}

// Reports whether `clang_path` is the pinned reference release's official build, into `version`; a mismatch prints both
// builds. False when it isn't the reference, and the caller then skips only the comparisons that depend on the build's
// table.
static bool clang_reference_report(const char* clang_path, char* version, size_t version_capacity)
{
    char* output = clang_reference_output(clang_path);
    char message[192];
    const bool result = clang_reference_compare(output, version, version_capacity, message, sizeof(message));
    if (!result)
    {
        printf("clang reference: %s\n", message);
    }
    free(output);
    return result;
}

// A shell script at `path` that runs `clang` with `options` (NULL for none) and its own arguments, and that answers
// --version with `release` as its first line (NULL to forward it too). `clang` goes in single quotes, each of its own
// quotes as '\'', so a path with spaces or the shell's metacharacters reaches exec as one word. False where it isn't
// written.
static bool clang_wrapper_write(const char* path, const char* clang, const char* release, const char* options)
{
    char script[8192];
    int length = snprintf(script, sizeof(script), "#!/bin/sh\n");
    if (release != NULL && length > 0 && (size_t)length < sizeof(script))
    {
        length += snprintf(script + length, sizeof(script) - (size_t)length,
                           "for argument in \"$@\"; do if [ \"$argument\" = \"--version\" ]; then printf '%s\\n'; exit 0; fi; done\n", release);
    }
    if (length > 0 && (size_t)length < sizeof(script))
    {
        length += snprintf(script + length, sizeof(script) - (size_t)length, "exec '");
    }
    const char* c = clang;
    for (; length > 0 && (size_t)length + 8 < sizeof(script) && *c != 0; c += 1)
    {
        length += *c == '\'' ? snprintf(script + length, sizeof(script) - (size_t)length, "'\\''") : snprintf(script + length, sizeof(script) - (size_t)length, "%c", *c);
    }
    // The whole path, or no script.
    length = *c == 0 ? length : -1;
    if (length > 0 && (size_t)length < sizeof(script))
    {
        length += snprintf(script + length, sizeof(script) - (size_t)length, "' %s \"$@\"\n", options != NULL ? options : "");
    }
    bool result = length > 0 && (size_t)length < sizeof(script);
    FILE* file = result ? fopen(path, "wb") : NULL;
    result = file != NULL && fwrite(script, 1, (size_t)length, file) == (size_t)length;
    result = file != NULL && fclose(file) == 0 && result && chmod(path, 0755) == 0;
    return result;
}

// The fake build's wrapper for the non-reference fixtures (#1131), `wrapper` in `directory`: its --version prints
// `release`, a whole first line, and it passes `options`, and reaches `clang` through a forwarding script in a
// directory whose name has a space, quotes and the shell's $, ; and *, so that the reference CI's runs check that an
// awkward configured clang path still reaches exec as one word. `trampoline` and `folder` are the files to remove; false where they aren't written.
static bool clang_release_wrapper(const char* directory, const char* clang, const char* release, const char* options, char* wrapper, size_t wrapper_capacity,
                                  char* trampoline, size_t trampoline_capacity, char* folder, size_t folder_capacity)
{
    const int folder_length = snprintf(folder, folder_capacity, "%s/fake clang 'q' $x;*", directory);
    const int trampoline_length = snprintf(trampoline, trampoline_capacity, "%s/clang", folder);
    const int wrapper_length = snprintf(wrapper, wrapper_capacity, "%s/clang-release", directory);
    bool result = folder_length > 0 && (size_t)folder_length < folder_capacity && trampoline_length > 0 && (size_t)trampoline_length < trampoline_capacity &&
                  wrapper_length > 0 && (size_t)wrapper_length < wrapper_capacity;
    result = result && (mkdir(folder, 0700) == 0 || errno == EEXIST) && clang_wrapper_write(trampoline, clang, NULL, NULL) &&
             clang_wrapper_write(wrapper, trampoline, release, options);
    return result;
}

// The version comparison's fixture: fake `clang --version` lines, so that no second compiler is needed, and the
// mismatch message naming both releases.
static bool pp_test_clang_reference(void)
{
    static const struct
    {
        const char* text;
        bool named;
        bool matches;
        const char* version;
    } cases[] = {
        { "clang version 23.1.4\nTarget: x86_64-pc-linux-gnu\n", true, true, "23.1.4" },
        { "clang version 23.1.4 (https://github.com/llvm/llvm-project.git llvmorg-23.1.4)\n", true, true, "23.1.4" },
        { "Ubuntu clang version 23.1.6 (++20261103094202+2875c6073a7e-1~exp1~20261103214217.95)\nTarget: x86_64-pc-linux-gnu\n", true, false,
          "Ubuntu 23.1.6" },
        // A distribution's build of the reference release isn't its official build: Debian's patches its table.
        { "Ubuntu clang version 23.1.4 (1ubuntu1)\n", true, false, "Ubuntu 23.1.4" },
        { "Debian clang version 23.1.4 (1)\n", true, false, "Debian 23.1.4" },
        { "Apple clang version 15.0.0 (clang-1500.3.9.4)\n", true, false, "Apple 15.0.0" },
        { "warning: a driver note\nclang version 23.1.4\n", true, true, "23.1.4" },
        { "clang version 23.1.40\n", true, false, "23.1.40" },
        { "not a compiler\n", false, false, "unavailable" },
    };
    bool result = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char version[64];
        char message[192];
        const bool matches = clang_reference_compare(cases[i].text, version, sizeof(version), message, sizeof(message));
        result &= matches == cases[i].matches && strcmp(version, cases[i].version) == 0;
        if (cases[i].named && !cases[i].matches)
        {
            result &= strstr(message, BUSTER_CLANG_REFERENCE_VERSION) != NULL && strstr(message, cases[i].version) != NULL;
        }
    }
    printf("clang reference: version comparison and mismatch reporting %s\n", result ? "as expected" : "WRONG");
    return result;
}

// FNV-1a of `size` bytes, from `digest`: the tests' recorded digests (TestDigests) fold their cases' outputs with it.
static uint64_t pp_kernel_fold(uint64_t digest, const void* bytes, uint64_t size)
{
    const uint8_t* at = bytes;
    for (uint64_t i = 0; i < size; i += 1)
    {
        digest = (digest ^ at[i]) * 0x100000001b3ull;
    }
    return digest;
}

static uint64_t pp_kernel_fold_u64(uint64_t digest, uint64_t value)
{
    return pp_kernel_fold(digest, &value, sizeof(value));
}

// A table of recorded digests, read from the repository's root (`path`): a line a comparison, its key (the comparison's
// site, '/', the case's name), a tab and the digest, recorded where the vectorized kernels and their scalar twins agreed
// (#2924). With the environment variable `record` set to a file, each comparison appends its line there instead, and the
// lines, sorted, make the table again. A case whose outputs hold its temporary directory's name (an interned path) has
// '-' for its digest, which any digest matches: two recordings with $TMPDIR of other lengths find them. The
// preprocessor's (pp_kernel_digests), the parser's (parse_kernel_digests) and the checker's (check_source_digests)
// share this. A table with `unrecorded` set lets a case it has no line for pass, once it has loaded lines: the
// checker's, whose cases added after the twins went have no agreement to record; `matched` and `unlisted` count its
// cases with and without a line.
typedef struct TestDigests TestDigests;
struct TestDigests
{
    const char* path;
    const char* record;
    char* text;
    char** keys;
    uint64_t* digests;
    bool* any;
    uint32_t count;
    uint32_t matched;
    uint32_t unlisted;
    bool loaded;
    bool unrecorded;
};

static void test_digests_load(TestDigests* table)
{
    table->loaded = true;
    FILE* file = fopen(table->path, "rb");
    long size = -1;
    if (file != 0 && fseek(file, 0, SEEK_END) == 0)
    {
        size = ftell(file);
    }
    if (size > 0 && fseek(file, 0, SEEK_SET) == 0)
    {
        table->text = malloc((size_t)size + 1);
        const bool read = table->text != 0 && fread(table->text, 1, (size_t)size, file) == (size_t)size;
        uint32_t lines = 0;
        for (long i = 0; read && i < size; i += 1)
        {
            lines += table->text[i] == '\n';
        }
        table->keys = read ? malloc(sizeof(char*) * (lines + 1)) : 0;
        table->digests = read ? malloc(sizeof(uint64_t) * (lines + 1)) : 0;
        table->any = read ? malloc(sizeof(bool) * (lines + 1)) : 0;
        if (table->keys != 0 && table->digests != 0 && table->any != 0)
        {
            table->text[size] = 0;
            char* line = table->text;
            while (*line != 0)
            {
                char* end = strchr(line, '\n');
                char* tab = strrchr(line, '\t');
                if (end != 0)
                {
                    *end = 0;
                    tab = strrchr(line, '\t');
                }
                if (tab != 0)
                {
                    *tab = 0;
                    table->keys[table->count] = line;
                    table->digests[table->count] = strtoull(tab + 1, 0, 16);
                    table->any[table->count] = tab[1] == '-';
                    table->count += 1;
                }
                line = end != 0 ? end + 1 : line + strlen(line);
            }
        }
    }
    if (file != 0)
    {
        fclose(file);
    }
}

// Whether `digest`, the comparison `site`'s on the case `name`, is the one `table` recorded (or, recording, whether its
// line was written). A case without a recorded digest fails, unless the table lets it pass (`unrecorded`) and has
// loaded lines; unless `quiet`, a failure says why.
static bool test_digest_matches(TestDigests* table, uint64_t digest, const char* site, const char* name, bool quiet)
{
    char key[4400];
    snprintf(key, sizeof(key), "%s/%s", site, name);
    for (char* c = key; *c != 0; c += 1)
    {
        *c = *c == '\t' || *c == '\n' ? ' ' : *c;
    }
    const char* record = getenv(table->record);
    bool result = false;
    if (record != 0 && *record != 0)
    {
        FILE* file = fopen(record, "a");
        result = file != 0 && fprintf(file, "%s\t%016llx\n", key, (unsigned long long)digest) > 0;
        result = file != 0 && fclose(file) == 0 && result;
    }
    else
    {
        if (!table->loaded)
        {
            test_digests_load(table);
        }
        uint32_t found = UINT32_MAX;
        for (uint32_t i = 0; found == UINT32_MAX && i < table->count; i += 1)
        {
            found = strcmp(table->keys[i], key) == 0 ? i : found;
        }
        table->matched += found != UINT32_MAX;
        table->unlisted += found == UINT32_MAX;
        result = found != UINT32_MAX ? table->any[found] || table->digests[found] == digest : table->unrecorded && table->count != 0;
        if (!result && !quiet)
        {
            printf("%s: %s %016llx (%s)\n", key, found == UINT32_MAX ? "no recorded digest for the vectorized kernels' outputs, which are" : "the vectorized kernels' outputs aren't their recorded digest, but",
                   (unsigned long long)digest, table->path);
        }
    }
    return result;
}

#endif
#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND

// The clang executable the oracle runs: "clang", or a fixture's wrapper that reports another release
// (pp_test_builtin_release). The release `clang --version` named, which the library-builtin checks read for the
// comparisons that depend on its table.
static const char* pp_test_clang_path = "clang";
static char pp_test_clang_version[64] = "";

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

// Whether -E spells an output token of `kind` at place `state` (pp_pragma_state) as written: an identifier of a pragma
// line after its name. The printer's own test of it is its step 0 (pp_print_vector_mode); the tests read the rule here.
static bool pp_spelled_as_written(uint32_t state, uint8_t kind)
{
    const bool identifier = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT);
    return state == BUSTER_PP_PRAGMA_TOKENS && identifier;
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
        // An unknown token's two more: -CC's `//` comment of a replacement list prints as a block comment.
        bytes += pp_spelled_as_written(pragma, kind) ? pp_written_spelling(pp, out->values[i], out->locations[i], scratch.arena, &written)
                 : kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END) ? intern_length(pp->table, out->values[i])
                 : kind == BUSTER_PP_KIND_PRAGMA_END ? 0 : kind == BUSTER_PP_KIND_INTEGER ? 24 : kind == BUSTER_PP_KIND_EMBED ? 4 * (uint64_t)data
                 : out->values[i] - out->locations[i] + (kind == C_TOKEN_UNKNOWN ? 2 : 0);
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
            // -CC's `// x` of a replacement list is the `/* x*/` the printer prints (pp_print_vector_mode).
            const bool converted = result && kind == C_TOKEN_UNKNOWN && pp->options.keep_macro_comments && pp_line_comment_spelling(spelling, length) &&
                                   pp_comment_in_directive(pp, out->locations[i]);
            uint8_t* block = converted ? arena_push(scratch.arena, (uint64_t)length + 2, 1) : 0;
            result = !converted || block != 0;
            if (converted && result)
            {
                memcpy(block, "/*", 2);
                memcpy(block + 2, spelling + 2, length - 2);
                memcpy(block + length, "*/", 2);
            }
            if (result)
            {
                pp_spellings_add(spellings, converted ? block : spelling, length + 2 * converted, out->locations[i], lexed);
            }
            arena_pop_to(scratch.arena, mark);
        }
        pragma = pp_pragma_state(pp, pragma, kind, out->locations[i]);
    }
    scratch_end(scratch);
    return result;
}

static void pp_test_directive_lines(char* text, size_t length, FILE* listing, const char* predefines);

// The lexer's flags a run's printed text lexes back with, and clang's text and a case's expected text: the mode's, without
// trigraphs (no printer leaves one) and with C_TOKENIZER_NO_DOLLARS where '$' is punctuation (pp_dollar_punctuation, the
// scalar lexer's, not the stream's pp_split_dollars).
static uint32_t pp_test_lex_flags(const Preprocessor* pp)
{
    // -C's and -CC's comments are tokens (#1380), in the stream as in the printed texts.
    return (pp->refine_flags & ~(uint32_t)C_TOKENIZER_TRIGRAPHS) | (pp_dollar_punctuation(&pp->options) ? C_TOKENIZER_NO_DOLLARS : 0) |
           (pp->options.keep_comments ? C_TOKENIZER_KEEP_COMMENTS : 0);
}

// The tokens of `count` that `split` (pp_split_kinds) splits, a token at a time: the rule pp_count_splits_vector keeps,
// which the expected and clang's texts are lexed by, apart from the stream's preparation.
static uint32_t pp_test_count_splits(const uint8_t* bytes, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends, uint32_t count, uint64_t split)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result += pp_splits_kind(split, kinds[i]) || ((kinds[i] & 0x7F) == C_TOKEN_UNKNOWN && pp_splits_unknown(bytes, starts[i], ends[i], split));
    }
    return result;
}

// Lexes a text as the language mode does: c_tokenize with the mode's `flags` (pp_test_lex_flags), then the prefixed
// literals the mode doesn't have split, by `split` (pp_split_kinds). Returns the count, or -1.
static int64_t pp_test_lex(const uint8_t* text, uint64_t length, uint64_t split, uint32_t flags, uint8_t* kinds, uint32_t* starts, uint32_t* ends)
{
    int64_t result = c_tokenize(text, length, kinds, starts, ends, flags);
    const uint32_t splits = result > 0 ? pp_test_count_splits(text, kinds, starts, ends, (uint32_t)result, split) : 0;
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

// ---------------------------------------------------------------------------------------------------------------------
// The vectorized kernels' recorded outputs (#2924)

#define BUSTER_PP_KERNEL_DIGESTS "tests/preprocessor-kernels/digests.tsv"

// What the comparisons of the vectorized kernels with their scalar twins read of a run (pp_same_streams,
// pp_same_directive_tables, the diagnostics' counts), FNV-1a: the errors and warnings, the stream token for token
// (kind, flags, value, location), the expansion history, and each file's directive table and raw errors. Every field is
// a count, an id or an offset into the run's own columns, never an address, so a digest is the same in every process.
static uint64_t pp_kernel_digest(const Preprocessor* pp)
{
    uint64_t digest = 0xcbf29ce484222325ull;
    const PPTokens* output = &pp->output;
    digest = pp_kernel_fold_u64(digest, (uint64_t)pp->errors << 32 | pp->warnings);
    digest = pp_kernel_fold_u64(digest, output->count);
    digest = pp_kernel_fold(digest, output->kinds, output->count);
    digest = pp_kernel_fold(digest, output->flags, output->count);
    digest = pp_kernel_fold(digest, output->values, (uint64_t)output->count * sizeof(uint32_t));
    digest = pp_kernel_fold(digest, output->locations, (uint64_t)output->count * sizeof(uint32_t));
    digest = pp_kernel_fold_u64(digest, (uint64_t)pp->history_count << 32 | pp->substitution_count);
    digest = pp_kernel_fold_u64(digest, (uint64_t)pp->generated_count << 32 | pp->history_source_count);
    digest = pp_kernel_fold_u64(digest, pp->history_run_count);
    digest = pp_kernel_fold(digest, pp->history, (uint64_t)pp->history_count * sizeof(PPExpansion));
    digest = pp_kernel_fold(digest, pp->substitutions, (uint64_t)pp->substitution_count * sizeof(PPSubstitution));
    digest = pp_kernel_fold(digest, pp->generated, (uint64_t)pp->generated_count * sizeof(PPGenerated));
    digest = pp_kernel_fold(digest, pp->history_sources, (uint64_t)pp->history_source_count * sizeof(PPSource));
    digest = pp_kernel_fold(digest, pp->history_runs, (uint64_t)pp->history_run_count * sizeof(PPHistoryRun));
    digest = pp_kernel_fold_u64(digest, pp->data_count);
    for (uint32_t f = 0; f < pp->data_count; f += 1)
    {
        const PPFileData* data = &pp->datas[f];
        digest = pp_kernel_fold_u64(digest, (uint64_t)data->prepared << 63 | (uint64_t)data->directive_count << 32 | data->raw_error_count);
        digest = pp_kernel_fold_u64(digest, data->guard);
        for (uint32_t e = 0; e < data->raw_error_count; e += 1)
        {
            digest = pp_kernel_fold_u64(digest, (uint64_t)data->raw_errors[e].token << 32 | data->raw_errors[e].offset);
            digest = pp_kernel_fold_u64(digest, data->raw_errors[e].kind);
        }
        const uint32_t directives = data->prepared ? data->directive_count : 0;
        digest = pp_kernel_fold(digest, data->directive_hash, (uint64_t)directives * sizeof(uint32_t));
        digest = pp_kernel_fold(digest, data->directive_end, (uint64_t)directives * sizeof(uint32_t));
        digest = pp_kernel_fold(digest, data->directive_kind, directives);
        digest = pp_kernel_fold(digest, data->directive_next, (uint64_t)directives * sizeof(uint32_t));
    }
    return digest;
}

static TestDigests pp_kernel_digests = { .path = BUSTER_PP_KERNEL_DIGESTS, .record = "BUSTER_PP_RECORD_DIGESTS" };


// Whether `digest`, the comparison `site`'s on the case `name`, is the preprocessor's recorded one.
static bool pp_kernel_digest_matches(uint64_t digest, const char* site, const char* name)
{
    return test_digest_matches(&pp_kernel_digests, digest, site, name, pp_test_quiet);
}

// Whether the vectorized run `pp` of the comparison `site` on the case `name` gives its recorded digest.
static bool pp_kernel_digest_check(const Preprocessor* pp, const char* site, const char* name)
{
    return pp_kernel_digest_matches(pp_kernel_digest(pp), site, name);
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
        run.timing = true;
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
    // The statistics but the timing ones, which the deferred run's paths (the templates among them) must count as the
    // recorded run's do.
    PPStatistics statistics[2] = { runs[0].statistics, runs[1].statistics };
    for (uint32_t k = 0; k < 2; k += 1)
    {
        statistics[k].include_ticks = 0;
        statistics[k].embed_ticks = 0;
    }
    if (result && memcmp(&statistics[0], &statistics[1], sizeof(PPStatistics)) != 0)
    {
        printf("%s: with its history deferred, a run counts different statistics\n", label);
        result = false;
    }
    // A compile gives its files' token columns back after the run (pp_release_file_columns): the replay that records the
    // deferred history then prepares the files again, and must give the same history. Its backend gives the output back
    // too (pp_release_output): the probe's diagnostic below asks for it, and the replay gives it back, which the streams'
    // comparison then checks token for token.
    if (ready[1])
    {
        pp_release_file_columns(&runs[1], false);
        // A reader that doesn't record the history has the output back from a replay without one, token for token, and the
        // history stays deferred; then the backend's release again, for the probe.
        pp_release_output(&runs[1], false);
        const bool deferred = runs[1].history_deferred;
        const bool kept = runs[1].output_released && (runs[1].output.kinds != NULL || runs[1].output.count != runs[0].output.count);
        const bool back = pp_output_ensure(&runs[1]) && pp_tokens_same(&runs[0].output, &runs[1].output) && runs[1].history_deferred == deferred;
        if (result && (kept || !back))
        {
            printf("%s: a released output %s\n", label, kept ? "keeps its columns or loses its count" : "comes back otherwise, or records the history");
            result = false;
        }
        pp_release_output(&runs[1], false);
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
    const bool recorded = probed && pp_history_ensure(&runs[1]) && pp_output_ensure(&runs[1]);
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

// pp_output_invocation_place, the file's own place of an output token's invocation, against its twin by the expansion
// history (pp_output_expansion_begin), on run `a`, which has its history: at the first and last token of each run of
// tokens a macro made, where the file places one, the history must place it there too. Returns whether they agree.
static bool pp_test_invocation_places(const Preprocessor* a, const char* label)
{
    bool result = true;
    const PPTokens* output = &a->output;
    for (uint32_t t = 0; result && t < output->count; t += 1)
    {
        const bool made = (output->flags[t] & BUSTER_PP_FROM_MACRO) != 0;
        const bool edge = made && (t == 0 || t + 1 == output->count || (output->flags[t - 1] & BUSTER_PP_FROM_MACRO) == 0 ||
                                   (output->flags[t + 1] & BUSTER_PP_FROM_MACRO) == 0);
        const uint32_t place = edge ? pp_output_invocation_place(a, t) : 0;
        const uint32_t twin = place != 0 ? pp_output_expansion_begin(a, t, pp_history_at(a, t)) : 0;
        if (place != twin)
        {
            printf("%s: output token %u's invocation is at %u by the file and at %u by the history\n", label, t, place, twin);
            result = false;
        }
    }
    return result;
}

// Both kernels again as a compile runs them, without the expansion starts that -E's line markers need: the main loop
// takes some direct paths only then (pp_expand_file_direct, pp_emit_simple), so the streams, the expansion history and
// the statistics (but the timing ones) must agree in this configuration too, and the vectorized run's with its history
// deferred, as the drivers run it (pp_test_deferred_history). Returns whether they do.
static bool pp_test_compile_kernels(const PPOptions* options, const char* path, const char* name, bool frozen)
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
    bool result = ready[0] && ready[1] && pp_same_streams(&runs[0], &runs[1], label) && (!frozen || pp_kernel_digest_check(&runs[0], "compile", name));
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
    result = result && pp_test_system_levels(&runs[0], &runs[1], label) && pp_test_invocation_places(&runs[0], label);
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

// The text -E prints for a run, in memory (null if it fails); `free` it.
static char* pp_test_print(Preprocessor* pp, bool markers, size_t* length)
{
    char* result = 0;
    *length = 0;
    FILE* stream = open_memstream(&result, length);
    const bool printed = stream != 0 && pp_print(pp, stream, markers);
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

// The text -E prints for a run, which must lex into the stream's spellings (with the EOF boundary above), and with a
// built-in case's `key` give its recorded digest; the text with line markers must print too (clang checks the markers:
// pp_test_markers).
static bool pp_test_printed(Preprocessor* pp, const PPSpellings* ours, const char* name, const char* key)
{
    size_t length = 0;
    char* text = pp_test_print(pp, false, &length);
    size_t marked_length = 0;
    char* marked = pp_test_print(pp, true, &marked_length);
    bool result = text != 0 && marked != 0;
    if (!result && !pp_test_quiet)
    {
        printf("%s: -E can't print the run%s\n", name, text != 0 ? " with line markers" : "");
    }
    result = result && (key == 0 || pp_kernel_digest_matches(pp_kernel_fold(0xcbf29ce484222325ull, text, length), "print", key));
    free(marked);
    PPSpellings printed = { 0 };
    if (result && (pp->options.dump_defines || pp->options.dump_includes))
    {
        pp_test_directive_lines(text, length, 0, 0);
    }
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

#endif

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

// This process's environment without CPATH and C_INCLUDE_PATH, which clang reads and the preprocessor doesn't: a
// calloc'd array of its strings, or null.
static char** pp_test_clang_environment(void)
{
    uint32_t variables = 0;
    while (environ[variables] != 0)
    {
        variables += 1;
    }
    char** result = calloc((size_t)variables + 1, sizeof(char*));
    uint32_t kept = 0;
    for (uint32_t i = 0; result != 0 && i < variables; i += 1)
    {
        if (strncmp(environ[i], "CPATH=", 6) != 0 && strncmp(environ[i], "C_INCLUDE_PATH=", 15) != 0)
        {
            result[kept] = environ[i];
            kept += 1;
        }
    }
    return result;
}

// `executable ARGUMENTS -E [-dM -x c] PATH`, through the oracle cache (oracle_cache.c, #542): its output and
// diagnostics in memory, bounded, and whether clang accepted the input, rejected it, or couldn't run reliably.
static PPClangOutput pp_test_clang_process(const char* executable, char** arguments, int argument_count, const char* path, bool macros)
{
    PPClangOutput result = { 0 };
    char** argv = calloc((size_t)argument_count + 9, sizeof(char*));
    char** environment = pp_test_clang_environment();
    bool ready = argv != 0 && environment != 0;
    int status = 0;
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
        argv[n] = 0;
        OracleRun run = {
            .purpose = "preprocessor: clang -E",
            .arguments = argv,
            .environment = environment,
            .dependencies = true,
        };
        ready = oracle_run(&run) && run.output_length < BUSTER_PP_TEST_MAX_OUTPUT && run.errors_length < BUSTER_PP_TEST_MAX_OUTPUT;
        status = run.status;
        result.text = run.output;
        result.length = run.output_length;
        result.diagnostics = run.errors;
    }
    // Exit 1 alone does not establish an input rejection: a driver can also report a failed frontend or I/O.
    const char* message = result.diagnostics != 0 ? result.diagnostics : "";
    const bool rejected = strstr(message, "error:") != 0 && strstr(message, "clang: error:") == 0 && strstr(message, "unable to execute command") == 0 &&
                          strstr(message, "PLEASE submit a bug report") == 0 && strstr(message, "LLVM ERROR") == 0 && strstr(message, "No space left on device") == 0 &&
                          strstr(message, "Input/output error") == 0 && strstr(message, "Cannot allocate memory") == 0;
    if (ready && WIFEXITED(status) && (WEXITSTATUS(status) == 0 || (WEXITSTATUS(status) == 1 && rejected)))
    {
        result.status = WEXITSTATUS(status) == 0 ? PP_CLANG_ACCEPTED : PP_CLANG_REJECTED;
    }
    else
    {
        fprintf(stderr, "%s: Clang oracle could not run reliably (wait status %d)\n", path, status);
    }
    free(argv);
    free(environment);
    return result;
}

#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND

// Legacy fixture callers may compare the partial output of an intentionally rejected case. File-oracle callers
// inspect PPClangOutput.status instead: a rejection is an explicit skip, a process/I/O failure is a failed run.
// clang's predefines for `arguments` (clang -dM -E of an empty input), written to `predefines_path`.
static bool pp_test_clang_predefines(char** arguments, int argument_count, const char* predefines_path)
{
    PPClangOutput macros = pp_test_clang_process(pp_test_clang_path, arguments, argument_count, "/dev/null", true);
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
        PPClangOutput output = pp_test_clang_process(pp_test_clang_path, arguments, argument_count, path, false);
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
// Whether line [at, end) of a printed text is one of -dD's or -dI's directives (pp_print_events): a #define, an #undef,
// or an include that ends in clang's ` /* clang -E -dI */`.
static bool pp_test_directive_line(const char* text, size_t at, size_t end)
{
    static const char note[] = " /* clang -E -dI */";
    const size_t length = end - at;
    const bool include = (length > 9 && memcmp(text + at, "#include ", 9) == 0) || (length > 14 && memcmp(text + at, "#include_next ", 14) == 0) ||
                         (length > 8 && memcmp(text + at, "#import ", 8) == 0);
    return (length > 8 && memcmp(text + at, "#define ", 8) == 0) || (length > 7 && memcmp(text + at, "#undef ", 7) == 0) ||
           (include && length > sizeof(note) - 1 && memcmp(text + end - (sizeof(note) - 1), note, sizeof(note) - 1) == 0);
}

// Whether a block comment is open at the end of line [at, end) of a printed text, if one is at its start (`inside`): a
// -dD #define's line goes on with its replacement list's comment (-CC). Literals and `//` comments open none.
static bool pp_test_open_comment(const char* text, size_t at, size_t end, bool inside)
{
    for (size_t i = at; i < end; i += 1)
    {
        const bool opens = !inside && text[i] == '/' && i + 1 < end && text[i + 1] == '*';
        const bool closes = inside && text[i] == '*' && i + 1 < end && text[i + 1] == '/';
        if (!inside && (text[i] == '"' || text[i] == '\''))
        {
            size_t j = i + 1;
            while (j < end && text[j] != text[i])
            {
                j += text[j] == '\\' ? 2 : 1;
            }
            i = j;
        }
        else if (!inside && text[i] == '/' && i + 1 < end && text[i + 1] == '/')
        {
            i = end;
        }
        inside = (inside && !closes) || opens;
        i += opens || closes;
    }
    return inside;
}

// -dD's and -dI's directives of a text -E printed with line markers (#1380): each blanked (spaces for its bytes, its line
// breaks kept), a #define's lines that a comment in its replacement list carries on with too, so that the text's tokens
// are the stream's and keep their places; and with `listing`, written there with its presumed file and line as the
// markers place it (`name:line: text`), but those of <built-in>, <command line> and `predefines` (--predefines' file, or
// null): clang's predefines and the command line's, whose order and set buster's don't follow.
static void pp_test_directive_lines(char* text, size_t length, FILE* listing, const char* predefines)
{
    size_t name = 0;
    uint32_t name_length = 0;
    uint32_t line = 1;
    bool inside = false;
    bool listed = false;
    for (size_t at = 0; at < length;)
    {
        const char* newline = memchr(text + at, '\n', length - at);
        const size_t end = newline != 0 ? (size_t)(newline - text) : length;
        uint32_t marked = 0;
        uint64_t marked_name = 0;
        uint32_t marked_length = 0;
        bool flags[5] = { false };
        if (!inside && pp_marker_line((const uint8_t*)text, at, end, &marked, &marked_name, &marked_length, flags))
        {
            line = marked;
            name = marked_name;
            name_length = marked_length;
        }
        else
        {
            const bool continued = inside;
            if (continued || pp_test_directive_line(text, at, end))
            {
                const bool virtual_file = (name_length == 10 && memcmp(text + name, "<built-in>", 10) == 0) ||
                                          (name_length == 14 && memcmp(text + name, "<command line>", 14) == 0) ||
                                          (predefines != 0 && strlen(predefines) == name_length && memcmp(text + name, predefines, name_length) == 0);
                listed = continued ? listed : listing != 0 && !virtual_file;
                if (listed && continued)
                {
                    fprintf(listing, "\n    %.*s", (int)(end - at), text + at);
                }
                else if (listed)
                {
                    fprintf(listing, "%.*s:%u: %.*s", (int)name_length, text + name, line, (int)(end - at), text + at);
                }
                inside = pp_test_open_comment(text, at, end, inside);
                if (listed && !inside)
                {
                    fputc('\n', listing);
                }
                memset(text + at, ' ', end - at);
            }
            line += 1;
        }
        at = end + 1;
    }
}

// -dD's and -dI's directives in our text with line markers against clang's, where each is and what it says
// (pp_test_directive_lines), then clang's blanked, as ours are for the comparisons of the tokens.
static bool pp_test_dumped_directives(Preprocessor* pp, char* clang_text, size_t clang_length, const char* name)
{
    size_t length = 0;
    char* text = pp_test_print(pp, true, &length);
    char* ours = 0;
    char* theirs = 0;
    size_t ours_length = 0;
    size_t theirs_length = 0;
    FILE* ours_stream = open_memstream(&ours, &ours_length);
    FILE* theirs_stream = open_memstream(&theirs, &theirs_length);
    bool result = text != 0 && ours_stream != 0 && theirs_stream != 0;
    if (result)
    {
        pp_test_directive_lines(text, length, ours_stream, pp->options.predefines_path);
        pp_test_directive_lines(clang_text, clang_length, theirs_stream, 0);
    }
    if (ours_stream != 0)
    {
        fclose(ours_stream);
    }
    if (theirs_stream != 0)
    {
        fclose(theirs_stream);
    }
    result = result && ours != 0 && theirs != 0 && ours_length == theirs_length && memcmp(ours, theirs, ours_length) == 0;
    if (!result && ours != 0 && theirs != 0)
    {
        printf("%s: -dD's or -dI's directives differ from clang's:\n  ours:\n%s  clang's:\n%s", name, ours, theirs);
    }
    free(text);
    free(ours);
    free(theirs);
    return result;
}

static bool pp_test_placed(Preprocessor* pp, const PPPlaced* theirs, const char* name)
{
    size_t length = 0;
    char* text = pp_test_print(pp, true, &length);
    if (text != 0 && (pp->options.dump_defines || pp->options.dump_includes))
    {
        pp_test_directive_lines(text, length, 0, 0);
    }
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

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>

// Runtime header queries may get their identity through a macro. Raw-looking header bytes remain header bytes;
// earlier and later macro definitions keep the prepared view that supplied their token indexes. Its own check
// (pp_test_raw_query_views) keeps it in C; the cases' copy is tests/preprocessor/trees' raw-query.
static const PPTestFile pp_test_files_raw_query[] = {
    { "R\"(x", "header\n" },
    { "raw-plain.h", "ordinary\n" },
    { "raw-view.h", "#ifndef RAW_REENTER\n#define RAW_REENTER\n#define RAW_BEFORE 11\n#if RAW_QUERY(<R\"(x>) // )\"\nouter\n#endif\n#define RAW_AFTER 22\n#include \"raw-view.h\"\nRAW_BEFORE RAW_AFTER\n#else\n#if RAW_QUERY(<R\"(x>) // )\"\ninner\n#endif\n#endif\n" },
    { "raw-once.h", "#if RAW_QUERY(<R\"(x>)\nonce\n#endif\n#pragma once\n" },
    { "raw-import.h", "#if RAW_QUERY(<R\"(x>)\nimported\n#endif\n" },
    { "raw-guard.h", "#ifndef RAW_GUARD\n#define RAW_GUARD\n#if RAW_QUERY(<R\"(x>) // )\"\nguarded\n#endif\n#endif\n" },
    { 0, 0 },
};

// The built-in cases (pp_test_cases) and the files they write beside case.c are runtime data (#2053), under the fixture
// root (tests/ under the working directory, or test-preprocessor's --fixtures DIR), read before the checks are dealt,
// so that editing or adding a case compiles, links and bakes nothing:
//
// - preprocessor/cases/catalog.tsv, first line `buster-fixtures v1 preprocessor-cases`, then a case a line not starting
//   with '#': ID (letters, digits, '.', '_' and '-'), STANDARD (-std='s, or -), FLAGS (comma-separated, or -: fails,
//   warnings=N, errors=N, other, markers, files=TREE), OPTIONS (both sides' options as one field, %d the case's
//   directory, or -) and NAME, tab-separated. Its files beside the catalog: ID.c, the source, and ID.expected, the
//   expected output; with `other`, ID.other.h (written as other.h); with `markers`, ID.markers (-E's text with line
//   markers).
// - preprocessor/trees/catalog.tsv, first line `buster-fixtures v1 preprocessor-trees`, then a file of a tree a line:
//   TREE, KIND, SOURCE and PATH, tab-separated, each tree's lines together and in order. PATH is the file's place in the
//   case's directory (relative, no "." or ".." part). KIND `text` writes SOURCE's bytes, a file beside the catalog, as
//   they are (zero bytes among them); `symlink` makes a symbolic link to SOURCE as it is written; `hard` a hard link to
//   SOURCE, an earlier PATH of the tree.
//
// A text field's file holds no zero byte. A missing, unreadable or malformed catalog, a missing file, a repeated ID, tree
// or path, an unknown flag or tree, a file in either directory that no line names, or no case fails the run: no case is
// skipped.
static const char* pp_test_fixture_root = "tests";
static const PPTestCase* pp_test_cases;
static uint32_t pp_test_case_count;

enum
{
    // The cases, the trees' files, and a file's bytes, at most.
    BUSTER_PP_TEST_FIXTURE_CASES = 8192,
    BUSTER_PP_TEST_FIXTURE_FILES = 4096,
    BUSTER_PP_TEST_FIXTURE_BYTES = 1 << 20,
};

// What the catalogs load into, kept for the run: the cases and their trees refer to its bytes.
typedef struct PPTestFixtures PPTestFixtures;
struct PPTestFixtures
{
    Arena storage;
    PPTestCase* cases;
    uint8_t* rows;
    size_t row_size;
    const char** identifiers;
    uint32_t count;
    PPTestFile* files;
    uint32_t file_count;
    // The trees: each's name and first file, its files ending with one without a path.
    const char** tree_names;
    uint32_t* tree_firsts;
    uint32_t tree_count;
    // The file names beside each catalog that its lines name.
    const char** named;
    uint32_t named_count;
    char problem[4400];
};

static PPTestFixtures pp_test_fixtures;

// The bytes of the file at `path`, with a zero byte after them, in the fixtures' storage, and their count; null, with the
// reason in the problem, when it can't be read, is larger than BUSTER_PP_TEST_FIXTURE_BYTES, or (unless `binary`) holds a
// zero byte.
static char* pp_test_fixture_read(PPTestFixtures* fixtures, const char* path, bool binary, uint64_t* length)
{
    char* bytes = 0;
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    const bool regular = file >= 0 && fstat(file, &status) == 0 && S_ISREG(status.st_mode);
    if (!regular)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "can't read %s", path);
    }
    else if (status.st_size > BUSTER_PP_TEST_FIXTURE_BYTES)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "%s is larger than %u bytes", path, (unsigned)BUSTER_PP_TEST_FIXTURE_BYTES);
    }
    else
    {
        const uint64_t size = (uint64_t)status.st_size;
        char* buffer = arena_push(&fixtures->storage, size + 1, 1);
        uint64_t done = 0;
        ssize_t got = 1;
        while (buffer != 0 && done < size && got > 0)
        {
            got = read(file, buffer + done, size - done);
            done += got > 0 ? (uint64_t)got : 0;
        }
        if (buffer == 0 || done != size)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "can't read %s whole", path);
        }
        else if (!binary && memchr(buffer, 0, size) != 0)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "%s holds a zero byte", path);
        }
        else
        {
            buffer[size] = 0;
            *length = size;
            bytes = buffer;
        }
    }
    if (file >= 0)
    {
        close(file);
    }
    return bytes;
}

// Whether `text` is 1 to 64 letters, digits, '.', '_' and '-'.
static bool pp_test_fixture_word(const char* text)
{
    const size_t length = strlen(text);
    bool word = length != 0 && length <= 64;
    for (size_t i = 0; word && i < length; i += 1)
    {
        const char c = text[i];
        word = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-';
    }
    return word;
}

// Whether `path` is a relative path of the case's directory: not empty, no leading '/', no empty, "." or ".." part.
static bool pp_test_fixture_path(const char* path)
{
    bool valid = path[0] != 0 && path[0] != '/';
    const char* part = path;
    while (valid && part != 0)
    {
        const char* slash = strchr(part, '/');
        const size_t length = slash != 0 ? (size_t)(slash - part) : strlen(part);
        valid = length != 0 && !(length == 1 && part[0] == '.') && !(length == 2 && part[0] == '.' && part[1] == '.');
        part = slash != 0 ? slash + 1 : 0;
    }
    return valid;
}

// `name`, a file beside the catalog being read, among those its lines name; false if a line named it already.
static bool pp_test_fixture_name(PPTestFixtures* fixtures, const char* name)
{
    bool fresh = fixtures->named_count < BUSTER_PP_TEST_FIXTURE_CASES * 4;
    for (uint32_t i = 0; fresh && i < fixtures->named_count; i += 1)
    {
        fresh = strcmp(fixtures->named[i], name) != 0;
    }
    if (fresh)
    {
        fixtures->named[fixtures->named_count] = name;
        fixtures->named_count += 1;
    }
    return fresh;
}

// Whether every file in `directory` but catalog.tsv is one the catalog's lines name, and nothing else is there.
static bool pp_test_fixture_orphans(PPTestFixtures* fixtures, const char* directory)
{
    DIR* listing = opendir(directory);
    bool clean = listing != 0;
    if (!clean)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "can't list %s", directory);
    }
    for (struct dirent* entry = clean ? readdir(listing) : 0; clean && entry != 0; entry = readdir(listing))
    {
        const char* name = entry->d_name;
        bool named = strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, "catalog.tsv") == 0;
        for (uint32_t i = 0; !named && i < fixtures->named_count; i += 1)
        {
            named = strcmp(fixtures->named[i], name) == 0;
        }
        if (!named)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "%s/%s is in no catalog line", directory, name);
            clean = false;
        }
    }
    if (listing != 0)
    {
        closedir(listing);
    }
    return clean;
}

// The catalog at `directory`/catalog.tsv, whose first line must be `header`: its lines not starting with '#' after the
// first, each cut into `count` tab-separated fields, given to `line` with its number; false, with the reason in the
// problem, for a catalog that can't be read, doesn't end with a newline, has another first line or a line of another
// count of fields, or for a line `line` refuses.
static bool pp_test_fixture_lines(PPTestFixtures* fixtures, const char* directory, const char* header, uint32_t count,
                                  bool (*line)(PPTestFixtures* fixtures, const char* directory, char** fields, uint32_t number))
{
    char path[PATH_MAX + 64];
    snprintf(path, sizeof(path), "%s/catalog.tsv", directory);
    uint64_t length = 0;
    char* catalog = pp_test_fixture_read(fixtures, path, false, &length);
    bool valid = catalog != 0;
    if (valid && (length == 0 || catalog[length - 1] != '\n'))
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "%s doesn't end with a newline", path);
        valid = false;
    }
    uint32_t number = 0;
    char* cursor = catalog;
    while (valid && cursor < catalog + length)
    {
        char* end = strchr(cursor, '\n');
        *end = 0;
        number += 1;
        if (number == 1)
        {
            valid = strcmp(cursor, header) == 0;
            if (!valid)
            {
                snprintf(fixtures->problem, sizeof(fixtures->problem), "%s's first line isn't '%s'", path, header);
            }
        }
        else if (cursor[0] != '#')
        {
            char* fields[5] = { cursor, 0, 0, 0, 0 };
            uint32_t found = 1;
            for (char* tab = strchr(cursor, '\t'); tab != 0 && found <= count; tab = strchr(tab + 1, '\t'))
            {
                *tab = 0;
                if (found < count)
                {
                    fields[found] = tab + 1;
                }
                found += 1;
            }
            valid = found == count;
            for (uint32_t f = 0; valid && f < count; f += 1)
            {
                valid = fields[f][0] != 0;
            }
            if (!valid)
            {
                snprintf(fixtures->problem, sizeof(fixtures->problem), "%s line %u hasn't %u nonempty tab-separated fields", path, number, count);
            }
            if (valid && !line(fixtures, directory, fields, number))
            {
                char reason[sizeof(fixtures->problem)];
                snprintf(reason, sizeof(reason), "%s", fixtures->problem[0] != 0 ? fixtures->problem : "invalid case metadata");
                snprintf(fixtures->problem, sizeof(fixtures->problem), "%s line %u: %s", path, number, reason);
                valid = false;
            }
        }
        cursor = end + 1;
    }
    return valid;
}

// A trees' catalog line: TREE, KIND, SOURCE and PATH.
static bool pp_test_fixture_tree_line(PPTestFixtures* fixtures, const char* directory, char** fields, uint32_t number)
{
    (void)number;
    const char* tree = fields[0];
    const char* kind = fields[1];
    const char* source = fields[2];
    const char* path = fields[3];
    const bool same = fixtures->tree_count != 0 && strcmp(fixtures->tree_names[fixtures->tree_count - 1], tree) == 0;
    bool valid = pp_test_fixture_word(tree) && pp_test_fixture_path(path) && fixtures->file_count + 2 < BUSTER_PP_TEST_FIXTURE_FILES;
    for (uint32_t t = 0; valid && !same && t < fixtures->tree_count; t += 1)
    {
        valid = strcmp(fixtures->tree_names[t], tree) != 0;
    }
    if (!valid)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "a bad tree or path, or a tree whose lines aren't together");
    }
    if (valid && !same)
    {
        // The previous tree's end, then this one's start.
        if (fixtures->tree_count != 0)
        {
            fixtures->files[fixtures->file_count] = (PPTestFile){ 0 };
            fixtures->file_count += 1;
        }
        fixtures->tree_names[fixtures->tree_count] = tree;
        fixtures->tree_firsts[fixtures->tree_count] = fixtures->file_count;
        fixtures->tree_count += 1;
    }
    const uint32_t first = valid ? fixtures->tree_firsts[fixtures->tree_count - 1] : 0;
    bool earlier = false;
    for (uint32_t i = first; valid && i < fixtures->file_count; i += 1)
    {
        valid = strcmp(fixtures->files[i].path, path) != 0;
        earlier = earlier || strcmp(fixtures->files[i].path, source) == 0;
    }
    if (!valid)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "the tree has %s already", path);
    }
    PPTestFile file = { .path = path };
    if (valid && strcmp(kind, "text") == 0)
    {
        char file_path[PATH_MAX + 256];
        snprintf(file_path, sizeof(file_path), "%s/%s", directory, source);
        uint64_t length = 0;
        valid = pp_test_fixture_word(source) && pp_test_fixture_name(fixtures, source);
        if (!valid)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "a bad or repeated file name %s", source);
        }
        file.text = valid ? pp_test_fixture_read(fixtures, file_path, true, &length) : 0;
        valid = file.text != 0 && length <= UINT32_MAX;
        file.length = (uint32_t)length;
    }
    else if (valid && strcmp(kind, "symlink") == 0)
    {
        file.link = source;
    }
    else if (valid && strcmp(kind, "hard") == 0 && earlier)
    {
        file.link = source;
        file.hard = true;
    }
    else if (valid)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "an unknown kind %s, or a hard link to no earlier path", kind);
        valid = false;
    }
    if (valid)
    {
        fixtures->files[fixtures->file_count] = file;
        fixtures->file_count += 1;
    }
    return valid;
}

// A case's text file ID.suffix, NUL-terminated, in `*text`; false, with the reason in the problem, when it can't be read.
static bool pp_test_fixture_text(PPTestFixtures* fixtures, const char* directory, const char* identifier, const char* suffix, const char** text)
{
    char name[128];
    snprintf(name, sizeof(name), "%s%s", identifier, suffix);
    char* stored = arena_push(&fixtures->storage, strlen(name) + 1, 1);
    bool valid = stored != 0;
    if (valid)
    {
        memcpy(stored, name, strlen(name) + 1);
        valid = pp_test_fixture_name(fixtures, stored);
    }
    char path[PATH_MAX + 256];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    uint64_t length = 0;
    *text = valid ? pp_test_fixture_read(fixtures, path, false, &length) : 0;
    return *text != 0;
}

// The smaller suites keep their own row types and execution loops. They share the catalog bounds,
// raw text reader and orphan check with the main regression cases. All rows live until the suite ends.
static void* pp_test_fixture_row(PPTestFixtures* fixtures, const char* id)
{
    bool valid = pp_test_fixture_word(id) && fixtures->count < BUSTER_PP_TEST_FIXTURE_CASES;
    for (uint32_t i = 0; valid && i < fixtures->count; i += 1)
    {
        valid = strcmp(id, fixtures->identifiers[i]) != 0;
    }
    void* row = NULL;
    if (valid)
    {
        row = fixtures->rows + fixtures->row_size * fixtures->count;
        memset(row, 0, fixtures->row_size);
        fixtures->identifiers[fixtures->count++] = id;
    }
    else
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "invalid or duplicate case ID '%s'", id);
    }
    return row;
}

static bool pp_test_fixture_table(PPTestFixtures* fixtures, const char* suite, const char* header, uint32_t fields, size_t row_size,
                                  bool (*read_row)(PPTestFixtures*, const char*, char**, uint32_t), const char** probe)
{
    *fixtures = (PPTestFixtures){
        .storage = arena_reserve(UINT64_C(1) << 27),
        .row_size = row_size,
    };
    fixtures->rows = arena_push(&fixtures->storage, row_size * BUSTER_PP_TEST_FIXTURE_CASES, 8);
    fixtures->named = arena_push(&fixtures->storage, sizeof(*fixtures->named) * BUSTER_PP_TEST_FIXTURE_CASES * 4, 8);
    fixtures->identifiers = arena_push(&fixtures->storage, sizeof(*fixtures->identifiers) * BUSTER_PP_TEST_FIXTURE_CASES, 8);
    char directory[PATH_MAX];
    const int length = snprintf(directory, sizeof(directory), "%s/preprocessor/%s", pp_test_fixture_root, suite);
    const bool ready = length >= 0 && (size_t)length < sizeof(directory) && fixtures->rows != NULL && fixtures->named != NULL && fixtures->identifiers != NULL;
    const bool valid = ready && (probe == NULL || pp_test_fixture_text(fixtures, directory, "probe", ".c", probe)) &&
                       pp_test_fixture_lines(fixtures, directory, header, fields, read_row) && fixtures->count != 0 && pp_test_fixture_orphans(fixtures, directory);
    if (!valid)
    {
        fprintf(stderr, "%s/catalog.tsv: %s\n", directory, fixtures->problem[0] ? fixtures->problem : "empty or invalid fixture table");
        fixtures->count = 0;
    }
    return valid;
}

// A count flag's decimal value, 1 to UINT32_MAX, in `*value`; false for another value or a repeated flag.
static bool pp_test_fixture_count(const char* digits, uint32_t* value)
{
    char* end = 0;
    const unsigned long long number = digits[0] >= '1' && digits[0] <= '9' ? strtoull(digits, &end, 10) : 0;
    const bool valid = *value == 0 && end != 0 && *end == 0 && number <= UINT32_MAX;
    *value = valid ? (uint32_t)number : *value;
    return valid;
}

// A cases' catalog line: ID, STANDARD, FLAGS, OPTIONS and NAME.
static bool pp_test_fixture_case_line(PPTestFixtures* fixtures, const char* directory, char** fields, uint32_t number)
{
    (void)number;
    const char* identifier = fields[0];
    bool valid = pp_test_fixture_word(identifier) && fixtures->count < BUSTER_PP_TEST_FIXTURE_CASES;
    for (uint32_t i = 0; valid && i < fixtures->count; i += 1)
    {
        valid = strcmp(fixtures->identifiers[i], identifier) != 0;
    }
    if (!valid)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "a bad or repeated ID %s", identifier);
    }
    PPTestCase test = {
        .name = fields[4],
        .standard = strcmp(fields[1], "-") != 0 ? fields[1] : 0,
        .options = strcmp(fields[3], "-") != 0 ? fields[3] : 0,
        .expected = "",
    };
    bool other = false;
    bool markers = false;
    char* flag = strcmp(fields[2], "-") != 0 ? fields[2] : 0;
    while (valid && flag != 0)
    {
        char* comma = strchr(flag, ',');
        if (comma != 0)
        {
            *comma = 0;
        }
        if (strcmp(flag, "fails") == 0 && !test.fails)
        {
            test.fails = true;
        }
        else if (strncmp(flag, "warnings=", 9) == 0)
        {
            valid = pp_test_fixture_count(flag + 9, &test.warnings);
        }
        else if (strncmp(flag, "errors=", 7) == 0)
        {
            valid = pp_test_fixture_count(flag + 7, &test.errors);
        }
        else if (strcmp(flag, "other") == 0 && !other)
        {
            other = true;
        }
        else if (strcmp(flag, "markers") == 0 && !markers)
        {
            markers = true;
        }
        else if (strncmp(flag, "files=", 6) == 0 && test.files == 0)
        {
            for (uint32_t t = 0; t < fixtures->tree_count; t += 1)
            {
                test.files = strcmp(fixtures->tree_names[t], flag + 6) == 0 ? fixtures->files + fixtures->tree_firsts[t] : test.files;
            }
            valid = test.files != 0;
        }
        else
        {
            valid = false;
        }
        if (!valid)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "an unknown, repeated or bad flag %s", flag);
        }
        flag = comma != 0 ? comma + 1 : 0;
    }
    valid = valid && pp_test_fixture_text(fixtures, directory, identifier, ".c", &test.source) &&
            pp_test_fixture_text(fixtures, directory, identifier, ".expected", &test.expected) &&
            (!other || pp_test_fixture_text(fixtures, directory, identifier, ".other.h", &test.other)) &&
            (!markers || pp_test_fixture_text(fixtures, directory, identifier, ".markers", &test.markers));
    if (valid)
    {
        fixtures->identifiers[fixtures->count] = identifier;
        fixtures->cases[fixtures->count] = test;
        fixtures->count += 1;
    }
    return valid;
}

// The catalogs' cases into pp_test_cases, once; false, with the reason printed, when either catalog or a file of theirs
// is wrong (the comment above pp_test_fixture_root).
static bool pp_test_cases_load(void)
{
    PPTestFixtures* fixtures = &pp_test_fixtures;
    bool valid = pp_test_cases != 0;
    if (!valid)
    {
        *fixtures = (PPTestFixtures){ .storage = arena_reserve(UINT64_C(1) << 28) };
        fixtures->cases = arena_push(&fixtures->storage, sizeof(PPTestCase) * BUSTER_PP_TEST_FIXTURE_CASES, 8);
        fixtures->identifiers = arena_push(&fixtures->storage, sizeof(const char*) * BUSTER_PP_TEST_FIXTURE_CASES, 8);
        fixtures->files = arena_push(&fixtures->storage, sizeof(PPTestFile) * BUSTER_PP_TEST_FIXTURE_FILES, 8);
        fixtures->tree_names = arena_push(&fixtures->storage, sizeof(const char*) * BUSTER_PP_TEST_FIXTURE_FILES, 8);
        fixtures->tree_firsts = arena_push(&fixtures->storage, sizeof(uint32_t) * BUSTER_PP_TEST_FIXTURE_FILES, 8);
        fixtures->named = arena_push(&fixtures->storage, sizeof(const char*) * BUSTER_PP_TEST_FIXTURE_CASES * 4, 8);
        valid = fixtures->cases != 0 && fixtures->identifiers != 0 && fixtures->files != 0 && fixtures->tree_names != 0 && fixtures->tree_firsts != 0 &&
                fixtures->named != 0;
        char directory[PATH_MAX];
        snprintf(directory, sizeof(directory), "%s/preprocessor/trees", pp_test_fixture_root);
        valid = valid && pp_test_fixture_lines(fixtures, directory, "buster-fixtures v1 preprocessor-trees", 4, pp_test_fixture_tree_line) &&
                pp_test_fixture_orphans(fixtures, directory);
        // The last tree's end.
        if (valid)
        {
            fixtures->files[fixtures->file_count] = (PPTestFile){ 0 };
            fixtures->file_count += 1;
            fixtures->named_count = 0;
        }
        snprintf(directory, sizeof(directory), "%s/preprocessor/cases", pp_test_fixture_root);
        valid = valid && pp_test_fixture_lines(fixtures, directory, "buster-fixtures v1 preprocessor-cases", 5, pp_test_fixture_case_line) &&
                pp_test_fixture_orphans(fixtures, directory);
        if (valid && fixtures->count == 0)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "%s/catalog.tsv names no case", directory);
            valid = false;
        }
        if (valid)
        {
            pp_test_cases = fixtures->cases;
            pp_test_case_count = fixtures->count;
        }
        else
        {
            printf("test-preprocessor: the cases (#2053): %s\n", fixtures->problem);
        }
    }
    return valid;
}

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
static bool pp_test_write_named_case(const PPTestCase* test, const char* directory, const char* name, char* path, uint32_t capacity)
{
    snprintf(path, capacity, "%s/%s", directory, name);
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

static bool pp_test_write_case(const PPTestCase* test, const char* directory, char* path, uint32_t capacity)
{
    return pp_test_write_named_case(test, directory, "case.c", path, capacity);
}

// Removes the files pp_test_write_case wrote besides case.c, and their directories, innermost first.
static void pp_test_remove_case(const PPTestCase* test, const char* directory)
{
    char path[4096];
    if (test->other != NULL)
    {
        snprintf(path, sizeof(path), "%s/other.h", directory);
        unlink(path);
    }
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
    char* text = pp_test_print(pp, true, &length);
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

static bool pp_test_system_row(PPTestFixtures* table, const char* dir, char** fields, uint32_t line)
{
    (void)line;
    PPTestSystemCase* row = pp_test_fixture_row(table, fields[0]);
    bool valid = row != NULL && (strcmp(fields[1], "exact") == 0 || strcmp(fields[1], "inexact") == 0);
    if (valid)
    {
        row->name = fields[2];
        row->inexact = strcmp(fields[1], "inexact") == 0;
        valid = pp_test_fixture_text(table, dir, fields[0], ".c", &row->source) && pp_test_fixture_text(table, dir, fields[0], ".h", &row->header) &&
                pp_test_fixture_text(table, dir, fields[0], ".expected", &row->levels) && strspn(row->levels, "use") == strlen(row->levels);
    }
    return valid;
}

// Each system-level case, preprocessed as a compile runs it with its history: both paths must give its levels, or
// its runs must say they can't. Returns whether every case does.
static bool pp_test_system_case_levels(const char* directory)
{
    PPTestFixtures fixtures;
    const bool loaded = pp_test_fixture_table(&fixtures, "system-levels", "buster-fixtures v1 preprocessor-system-levels", 3, sizeof(PPTestSystemCase), pp_test_system_row, NULL);
    const PPTestSystemCase* cases = (const PPTestSystemCase*)fixtures.rows;
    uint32_t failures = !loaded;
    char system[4096];
    char header[4096];
    char other[4096];
    char path[4096];
    snprintf(system, sizeof(system), "%s/sys", directory);
    snprintf(header, sizeof(header), "%s/sys/s.h", directory);
    snprintf(other, sizeof(other), "%s/other.h", directory);
    snprintf(path, sizeof(path), "%s/system-levels.c", directory);
    const bool made = mkdir(system, 0700) == 0 || errno == EEXIST;
    for (uint32_t c = 0; c < fixtures.count; c += 1)
    {
        const PPTestSystemCase* test = &cases[c];
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
    printf("system levels: %u of %lu cases written by hand differ\n", failures, (unsigned long)fixtures.count);
    arena_release(&fixtures.storage);
    return failures == 0;
}

// A built-in case's key among the recorded digests: its name and a hash of what it preprocesses (its sources, files,
// standard, options and markers), since cases that loops make share their names.
static void pp_test_case_key(const PPTestCase* test, char* key, uint64_t capacity)
{
    const char* const texts[] = { test->source, test->other, test->standard, test->options, test->markers };
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(texts); i += 1)
    {
        hash = pp_kernel_fold(hash, texts[i] != 0 ? texts[i] : "", texts[i] != 0 ? strlen(texts[i]) + 1 : 1);
    }
    for (const PPTestFile* file = test->files; file != 0 && file->path != 0; file += 1)
    {
        hash = pp_kernel_fold(hash, file->path, strlen(file->path) + 1);
        const uint64_t length = file->text == 0 ? 0 : file->length != 0 ? file->length : strlen(file->text);
        hash = pp_kernel_fold_u64(hash, length);
        hash = pp_kernel_fold(hash, file->text, length);
    }
    hash = pp_kernel_fold_u64(hash, (uint64_t)test->fails << 32 | test->warnings);
    snprintf(key, capacity, "%s #%016llx", test->name, (unsigned long long)hash);
}

static bool pp_test_case(const PPTestCase* test, const char* directory, bool with_clang)
{
    char key[4200];
    pp_test_case_key(test, key, sizeof(key));
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
                 ran[0] != test->fails && pp_test_compile_kernels(&options, path, key, true) && pp_kernel_digest_check(&runs[0], "case", key);
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
                 pp_compare_spellings(&runs[0], &ours, &expected, test->name, "wanted") && pp_test_printed(&runs[0], &ours, test->name, key) &&
                 (test->markers == 0 || pp_test_markers(&runs[0], test, directory));
        if (result && with_clang)
        {
            size_t length;
            char* clang_text = pp_test_clang(arguments, argument_count, path, 0, &length);
            PPPlaced theirs = { 0 };
            const bool plain = !runs[0].options.dump_defines && !runs[0].options.dump_includes;
            result = clang_text != 0 && (plain || pp_test_dumped_directives(&runs[0], clang_text, length, test->name)) &&
                     pp_placed_tokens((const uint8_t*)clang_text, length, runs[0].split_kinds, pp_test_lex_flags(&runs[0]), &theirs) &&
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
            const uint32_t root = pp_lookup_path(pp, missing_root, (uint32_t)strlen(missing_root), BUSTER_PP_NONE, 0, BUSTER_PP_USER_FILE);
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
    Arena search_arena = arena_reserve(1ull << 20);
    PPSearchDirectories search = { 0 };
    result = result && pp_prepare_search(&search, &search_arena, &options);
    static InternTable tables[3];
    static Preprocessor runs[3];
    for (uint32_t k = 0; result && k < 3; k += 1)
    {
        options.search = k == 0 ? NULL : &search;
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
            result = result && stats->directory_stats == (options.search == NULL ? 4u : 0u) && stats->include_searches == 7 && stats->include_found == 3 && stats->include_opens == 8 &&
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
            result = pp_same_streams(&runs[0], pp, "lookup accounting") && pp_same_directive_tables(&runs[0], pp, "lookup accounting") &&
                     (k != 1 || pp_kernel_digest_check(&runs[0], "lookup", "accounting"));
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
    arena_release(&search_arena);
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

// Whether two units' path tables are the same: every path each looked up, in the same order, with the same file found
// or none and the same error.
static bool pp_test_same_lookups(const Preprocessor* a, const Preprocessor* b)
{
    bool result = a->file_count == b->file_count;
    for (uint32_t f = 0; result && f < a->file_count; f += 1)
    {
        const PPFile* x = &a->files[f];
        const PPFile* y = &b->files[f];
        result = x->path_length == y->path_length && memcmp(x->path, y->path, x->path_length) == 0 && (x->data != BUSTER_PP_NONE) == (y->data != BUSTER_PP_NONE) &&
                 x->error == y->error;
        if (!result)
        {
            printf("directory listings: path %u: %s (%s, error %d) against %s (%s, error %d)\n", f, x->path, x->data != BUSTER_PP_NONE ? "found" : "none", x->error,
                   y->path, y->data != BUSTER_PP_NONE ? "found" : "none", y->error);
        }
    }
    return result;
}

#include "c_preprocessor_directory_read_test.c"

// A directory's listing (FileCacheDirectory, #653). After BUSTER_FILE_CACHE_LISTING_MISSES failed probes in a search
// directory, the cache reads its names once. A path whose first component the listing doesn't have then gets ENOENT
// without an open. Every search finds what it finds with listings off, the oracle: a cache whose listing_misses is 0
// gives the same paths, files and errors. Names the listing can't rule out are still opened: one it has in another
// case, a directory, a dangling link, "." and "..", a name past ASCII, and one longer than NAME_MAX (ENAMETOOLONG,
// not ENOENT). A second unit that shares the cache uses the listing at once. A directory with a name past ASCII is never
// listed, and one with more names than a first look reads (BUSTER_FILE_CACHE_SMALL_LISTING) is listed only at
// BUSTER_FILE_CACHE_LARGE_LISTING times the failed probes. On a file system whose lookups a listing can't answer for
// (file_cache_exact_names), nothing is listed and every candidate is opened.
static bool pp_test_directory_listings(const char* directory)
{
    char first[4200];
    char second[4200];
    char latin[4200];
    char large[4200];
    char sub[4300];
    char path[4400];
    snprintf(first, sizeof(first), "%s/listing-first", directory);
    snprintf(second, sizeof(second), "%s/listing-second", directory);
    snprintf(latin, sizeof(latin), "%s/listing-latin", directory);
    snprintf(large, sizeof(large), "%s/listing-large", directory);
    snprintf(sub, sizeof(sub), "%s/sub", first);
    bool result = mkdir(first, 0700) == 0 && mkdir(second, 0700) == 0 && mkdir(latin, 0700) == 0 && mkdir(large, 0700) == 0 && mkdir(sub, 0700) == 0;
    snprintf(path, sizeof(path), "%s/Mixed.H", first);
    result = result && pp_test_write_file(path, "int mixed;\n");
    snprintf(path, sizeof(path), "%s/inner.h", sub);
    result = result && pp_test_write_file(path, "int inner;\n");
    snprintf(path, sizeof(path), "%s/dangling.h", first);
    result = result && symlink("nowhere.h", path) == 0;
    snprintf(path, sizeof(path), "%s/\xc3\xa9.h", latin);
    result = result && pp_test_write_file(path, "int latin;\n");
    for (uint32_t i = 0; result && i < 40; i += 1)
    {
        snprintf(path, sizeof(path), "%s/h%02u.h", second, i);
        result = pp_test_write_file(path, "int header;\n");
    }
    // More names than a first look's buffer holds (each record is at least 24 bytes).
    for (uint32_t i = 0; result && i < 64; i += 1)
    {
        snprintf(path, sizeof(path), "%s/large-%02u.h", large, i);
        result = pp_test_write_file(path, "int large;\n");
    }
    // Whether this file system's lookups match its listings, so that `first` is listed at its
    // BUSTER_FILE_CACHE_LISTING_MISSES-th miss (a listing is tried there either way).
    struct statfs system;
    const bool listable = result && statfs(first, &system) == 0 && file_cache_exact_names((uint64_t)system.f_type);
    // The names after h00.h to h39.h, each with the opens it takes with `first` listed: its paths in `first` and
    // `second`, but for one the listing rules out.
    char long_name[301];
    memset(long_name, 'a', 298);
    memcpy(long_name + 298, ".h", 3);
    static const struct
    {
        const char* name;
        uint32_t opens;
    } names[] = {
        { "Mixed.H", 1 },
        { "mixed.h", 2 },
        { "MIXED.H", 2 },
        { "sub/inner.h", 1 },
        { "sub/absent.h", 2 },
        { "SUB/inner.h", 2 },
        { "nosub/x.h", 1 },
        { "dangling.h", 2 },
        { "./h00.h", 2 },
        { "../listing-second/h01.h", 1 },
        { "\xc3\xa9.h", 2 },
        { "zz.h", 1 },
        { "H02.h", 1 },
        { 0, 2 },
    };
    const char* const directories[] = { first, second };
    const char* const latin_directories[] = { latin, second };
    const char* const large_directories[] = { large };
    PPOptions options = {
        .language = {
            .standard = 2023,
        },
        .no_standard_includes = true,
        .include_directories = directories,
        .include_directory_count = BUSTER_ARRAY_LENGTH(directories),
        .quiet = true,
    };
    // Units 0 (listings) and 1 (the oracle) search every name; unit 2 shares unit 0's cache, unit 3 searches `latin`
    // first, and unit 4 `large` alone.
    static InternTable tables[5];
    static Preprocessor units[5];
    for (uint32_t u = 0; u < 5; u += 1)
    {
        units[u] = (Preprocessor){ 0 };
        options.files = u == 2 ? units[0].cache : 0;
        options.include_directories = u == 3 ? latin_directories : u == 4 ? large_directories : directories;
        options.include_directory_count = u == 4 ? 1 : 2;
        result = result && intern_initialize(&tables[u]) && pp_initialize(&units[u], &options, u == 2 ? &tables[0] : &tables[u]);
    }
    if (result)
    {
        units[1].cache->listing_misses = 0;
        units[1].cache->asking_misses = 0;
    }
    uint64_t expected_opens = 0;
    uint64_t expected_listed = 0;
    const uint32_t misses = BUSTER_FILE_CACHE_LISTING_MISSES;
    const uint32_t count = 40 + (uint32_t)BUSTER_ARRAY_LENGTH(names);
    for (uint32_t c = 0; result && c < count; c += 1)
    {
        char header[64];
        snprintf(header, sizeof(header), "h%02u.h", c);
        const char* name = c < 40 ? header : names[c - 40].name != 0 ? names[c - 40].name : long_name;
        const uint32_t probes = c < 40 ? 1 : names[c - 40].opens;
        const uint32_t oracle_opens = c < 40 ? 2 : 2 - (c - 40 == 0 || c - 40 == 3 || c - 40 == 9);
        const bool skips = listable && c >= misses && probes < oracle_opens;
        expected_opens += skips ? probes : oracle_opens;
        expected_listed += skips;
        uint32_t found[2];
        uint32_t files[2];
        for (uint32_t u = 0; u < 2; u += 1)
        {
            files[u] = pp_find_include(&units[u], (const uint8_t*)name, (uint32_t)strlen(name), true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found[u],
                                       BUSTER_PP_NONE);
        }
        result = (files[0] == BUSTER_PP_NONE) == (files[1] == BUSTER_PP_NONE) && found[0] == found[1] && units[0].probes.opens == expected_opens &&
                 units[0].probes.listed == expected_listed && units[0].probes.listings == (c + 1 >= misses ? 1u : 0u) &&
                 units[1].probes.listings == 0 && units[1].probes.listed == 0;
        if (!result)
        {
            printf("directory listings: %s: found %u/%u in %u/%u, opens %lu/%lu, listed %lu/%lu, listings %lu\n", name, files[0] != BUSTER_PP_NONE,
                   files[1] != BUSTER_PP_NONE, found[0], found[1], (unsigned long)units[0].probes.opens, (unsigned long)expected_opens,
                   (unsigned long)units[0].probes.listed, (unsigned long)expected_listed, (unsigned long)units[0].probes.listings);
        }
    }
    result = result && pp_test_same_lookups(&units[0], &units[1]);
    // The unit that shares the cache: a header the cache knows makes no system call, and a name the listing rules out
    // is opened in `second` alone.
    if (result)
    {
        uint32_t found;
        const uint32_t known = pp_find_include(&units[2], (const uint8_t*)"h05.h", 5, true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found, BUSTER_PP_NONE);
        const uint32_t absent = pp_find_include(&units[2], (const uint8_t*)"yy.h", 4, true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found, BUSTER_PP_NONE);
        result = known != BUSTER_PP_NONE && absent == BUSTER_PP_NONE && units[2].probes.opens == (listable ? 1u : 2u) && units[2].probes.listed == (listable ? 1u : 0u) &&
                 units[2].files[units[2].file_count - 2].error == ENOENT;
        if (!result)
        {
            printf("directory listings: the shared cache: opens %lu, listed %lu\n", (unsigned long)units[2].probes.opens, (unsigned long)units[2].probes.listed);
        }
    }
    // A directory with a name past ASCII: its listing is tried once, and every path in it is still opened.
    for (uint32_t c = 0; result && c < 40; c += 1)
    {
        char header[64];
        snprintf(header, sizeof(header), "h%02u.h", c);
        uint32_t found;
        const uint32_t file = pp_find_include(&units[3], (const uint8_t*)header, (uint32_t)strlen(header), true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found,
                                              BUSTER_PP_NONE);
        result = file != BUSTER_PP_NONE && found == 1 && units[3].probes.opens == 2 * (c + 1) && units[3].probes.listed == 0 &&
                 units[3].probes.listings == (c + 1 >= misses ? 1u : 0u);
        if (!result)
        {
            printf("directory listings: %s after a name past ASCII: opens %lu, listed %lu, listings %lu\n", header, (unsigned long)units[3].probes.opens,
                   (unsigned long)units[3].probes.listed, (unsigned long)units[3].probes.listings);
        }
    }
    // A directory of 64 names: its first look puts it off, and it is read whole at BUSTER_FILE_CACHE_LARGE_LISTING times
    // the misses.
    const uint32_t later = misses * BUSTER_FILE_CACHE_LARGE_LISTING;
    for (uint32_t c = 0; result && c < later + 12; c += 1)
    {
        char header[64];
        snprintf(header, sizeof(header), "m%03u.h", c);
        uint32_t found;
        const uint32_t file = pp_find_include(&units[4], (const uint8_t*)header, (uint32_t)strlen(header), true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found,
                                              BUSTER_PP_NONE);
        const uint32_t opens = listable ? (c < later ? c + 1 : later) : c + 1;
        const uint32_t looks = (c + 1 >= misses ? 1u : 0u) + (listable && c + 1 >= later ? 1u : 0u);
        result = file == BUSTER_PP_NONE && units[4].probes.opens == opens && units[4].probes.listed == c + 1 - opens && units[4].probes.listings == looks;
        if (!result)
        {
            printf("directory listings: %s in a large directory: opens %lu/%u, listed %lu, listings %lu/%u\n", header, (unsigned long)units[4].probes.opens, opens,
                   (unsigned long)units[4].probes.listed, (unsigned long)units[4].probes.listings, looks);
        }
    }
    for (uint32_t u = 5; u > 0; u -= 1)
    {
        pp_release(&units[u - 1]);
    }
    for (uint32_t u = 0; u < 5; u += 1)
    {
        intern_release(&tables[u]);
    }
    for (uint32_t i = 0; i < 64; i += 1)
    {
        snprintf(path, sizeof(path), "%s/large-%02u.h", large, i);
        unlink(path);
    }
    for (uint32_t i = 0; i < 40; i += 1)
    {
        snprintf(path, sizeof(path), "%s/h%02u.h", second, i);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/Mixed.H", first);
    unlink(path);
    snprintf(path, sizeof(path), "%s/inner.h", sub);
    unlink(path);
    snprintf(path, sizeof(path), "%s/dangling.h", first);
    unlink(path);
    snprintf(path, sizeof(path), "%s/\xc3\xa9.h", latin);
    unlink(path);
    rmdir(sub);
    rmdir(first);
    rmdir(second);
    rmdir(latin);
    rmdir(large);
    printf("directory listings: %s (%s; skipped opens, names a listing can't rule out, a shared cache, a name past ASCII, a large directory)\n",
           result ? "as expected" : "WRONG",
           listable ? "listed" : "this file system's directories aren't listed");
    return result;
}

// A path's first component's key (file_cache_component_key, #2873): each case's path, after a directory of one byte,
// with its component's length (0 for none) and its key: the name's bytes and zeros after them, sixteen in all. Each
// path's bytes end where a page that can't be read begins, so that a load past them faults; and a case's `tail`
// follows its path's bytes in a second run, in memory that can be read: a '/' or a dot there, past the path's end,
// changes nothing.
static bool pp_test_component_keys(void)
{
    static const struct
    {
        const char* path;
        uint32_t component;
        const char* name;
        const char* tail;
    } cases[] = {
        { "d/bits/a.h", 4, "bits", "/b" },
        { "d/b/a.h", 1, "b", "/" },
        // Names in the directory itself: of one byte, of fifteen, of sixteen and of more.
        { "d/z", 0, "", "/x/y" },
        { "d/zz.h", 0, "", "/x.h" },
        { "d/fifteen-bytes-x", 0, "", "/x" },
        { "d/sixteen-bytes-xy", 0, "", "/x" },
        { "d/a-name-of-more-than-sixteen-bytes.h", 0, "", "/x" },
        // As many bytes as a slot holds, and one more.
        { "d/fifteen-bytes-x/a.h", 15, "fifteen-bytes-x", "/" },
        { "d/sixteen-bytes-xy/a.h", 0, "", "/" },
        { "d/fifteen-bytes-x/", 15, "fifteen-bytes-x", "x" },
        // No component, "." and "..", and names with a dot.
        { "d//a.h", 0, "", "/" },
        { "d/./a.h", 0, "", "/" },
        { "d/../a.h", 0, "", "/" },
        { "d/.a/a.h", 2, ".a", "." },
        { "d/a./a.h", 2, "a.", "." },
        { "d/.../a.h", 3, "...", "." },
        { "d/.", 0, "", "./" },
        { "d/..", 0, "", "/" },
        // Nothing after the directory's '/'.
        { "d/", 0, "", "bits/a.h" },
        // A name past ASCII, and the next component's bytes, which are no part of the key.
        { "d/\xc3\xa9/a.h", 2, "\xc3\xa9", "/" },
        { "d/sys/types/more.h", 3, "sys", "/" },
    };
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    char* mapping = mmap(0, 2 * page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = mapping != MAP_FAILED && mprotect(mapping + page_bytes, page_bytes, PROT_NONE) == 0;
    const FileCacheDirectory directory = { .length = 1 };
    uint32_t failures = 0;
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const uint32_t length = (uint32_t)strlen(cases[c].path);
        const uint32_t tail = (uint32_t)strlen(cases[c].tail);
        uint8_t expected[16] = { 0 };
        memcpy(expected, cases[c].name, strlen(cases[c].name));
        for (uint32_t run = 0; run < 2; run += 1)
        {
            // The path's last byte is the page's last, or its tail's is.
            char* path = mapping + page_bytes - length - (run == 0 ? 0 : tail);
            memcpy(path, cases[c].path, length);
            memcpy(path + length, cases[c].tail, run == 0 ? 0 : tail);
            uint32_t component = UINT32_MAX;
            uint8_t key[16];
            _mm_storeu_si128((__m128i*)(void*)key, file_cache_component_key(&directory, path, length, &component));
            // Where a path has no component to count, its key is not read.
            const bool same = component == cases[c].component && (component == 0 || memcmp(key, expected, sizeof(key)) == 0);
            if (!same)
            {
                printf("component keys: %s%s: a component of %u bytes (%u)\n", cases[c].path, run == 0 ? "" : " with bytes after it", component, cases[c].component);
            }
            failures += !same;
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 2 * page_bytes);
    }
    result = result && failures == 0;
    printf("component keys: %s (%u cases, each at the end of a page and with bytes after it)\n", result ? "as expected" : "WRONG", (uint32_t)BUSTER_ARRAY_LENGTH(cases));
    return result;
}

// A search directory asked for a subdirectory (FileCacheDirectory.component_names, #2873). After
// BUSTER_FILE_CACHE_ASKING_MISSES failed probes under one first component of a search directory, the cache asks the
// directory for the component, once: a stat of the component's own path. A directory without it then gives ENOENT for
// every path under it, without an open. The test searches paths by groups under one component, each of new names,
// and expects after every group the opens, the asks and the answers the rule gives for what its two directories have
// there (the table's counts, with the asks at a component's third miss). Every search also finds what it finds with
// the asks off: the same cache with asking_misses 0 gives the same paths, files and errors, and opens every candidate
// an answer spared. What an ask can't rule out is still opened: a component the directory has, a regular file by its
// name (ENOTDIR, which counts no miss), another spelling of a component it lacks, a name it begins with and one that
// begins with it, "." and "..", and a component longer than a name can be. A dangling link and a name past ASCII are
// asked for as any other, and a name without a '/' never. A directory counts BUSTER_FILE_CACHE_COMPONENTS components,
// of names up to BUSTER_FILE_CACHE_COMPONENT_NAME bytes: one past either is opened every time, and a component's
// count stops at 255, so that it is asked for once. A second unit that shares the cache uses the answers at once. A
// directory with a listing is asked nothing more: its listing answers (file_cache_listed_absent).
static bool pp_test_directory_asks(const char* directory)
{
    // The asks' threshold the table's counts are for.
    const uint32_t asking = 3;
    char first[4200];
    char second[4200];
    char have[4300];
    char bits[4300];
    char path[4400];
    snprintf(first, sizeof(first), "%s/ask-first", directory);
    snprintf(second, sizeof(second), "%s/ask-second", directory);
    snprintf(have, sizeof(have), "%s/have", first);
    snprintf(bits, sizeof(bits), "%s/bits", second);
    bool result = mkdir(first, 0700) == 0 && mkdir(second, 0700) == 0 && mkdir(have, 0700) == 0 && mkdir(bits, 0700) == 0;
    snprintf(path, sizeof(path), "%s/inner.h", have);
    result = result && pp_test_write_file(path, "int inner;\n");
    snprintf(path, sizeof(path), "%s/plain", first);
    result = result && pp_test_write_file(path, "int plain;\n");
    snprintf(path, sizeof(path), "%s/gone", first);
    result = result && symlink("nowhere", path) == 0;
    // `second`'s headers under `bits`: one for each search of the first group, and one for the unit that shares the cache.
    for (uint32_t h = 0; result && h < 5; h += 1)
    {
        snprintf(path, sizeof(path), "%s/h%u.h", bits, h);
        result = pp_test_write_file(path, "int header;\n");
    }
    // A component longer than a name can be.
    static char long_component[300];
    memset(long_component, 'a', 298);
    long_component[298] = 0;
    // The groups: a first component (none for names in the directories themselves), the names searched under it by
    // their turn's number, whether a search finds its file, the searches, and what they add to the unit's opens, asks
    // and answers. `first` is searched before `second`; a directory's third miss under a component asks it.
    static const struct
    {
        const char* component;
        const char* file;
        bool found;
        uint16_t turns;
        uint16_t opens;
        uint16_t asks;
        uint16_t answered;
    } groups[] = {
        // `first` has no `bits`, `second` has it with these headers: `first` is opened three times, asked at the
        // third, and answers the fourth.
        { "bits", "h%u.h", true, 4, 7, 1, 1 },
        // Headers `second`'s `bits` lacks: `first` answers every time; `second` is opened every time, asked at its
        // third miss, and has the component.
        { "bits", "x%u.h", false, 4, 4, 1, 4 },
        // A component neither has: both are opened three times and asked, and both answer the fourth.
        { "none", "a%u.h", false, 4, 6, 2, 2 },
        // Another spelling of it, a name it begins with and one that begins with it are other components: opened.
        { "None", "a%u.h", false, 1, 2, 0, 0 },
        { "non", "a%u.h", false, 1, 2, 0, 0 },
        { "nonesuch", "a%u.h", false, 1, 2, 0, 0 },
        // A component `first` has, with its header: one open, and no miss.
        { "have", "inner.h", true, 1, 1, 0, 0 },
        // Headers `first`'s `have` lacks: `first` is opened every time and asked at its third miss; `second`, without
        // the component, is opened three times, asked, and answers the fourth.
        { "have", "x%u.h", false, 4, 7, 2, 1 },
        // A regular file by the component's name in `first`: ENOTDIR every time, which is no miss and asks nothing;
        // `second` lacks the component.
        { "plain", "a%u.h", false, 4, 7, 1, 1 },
        // A dangling link in `first` is a component it lacks.
        { "gone", "a%u.h", false, 4, 6, 2, 2 },
        // "." and ".." are no components to ask for, whatever follows them: every candidate is opened.
        { "./none", "e%u.h", false, 4, 8, 0, 0 },
        { "../nowhere", "a%u.h", false, 4, 8, 0, 0 },
        { "../ask-second/bits", "h0.h", true, 1, 1, 0, 0 },
        // A name of two bytes with one dot is a component as any other.
        { ".a", "a%u.h", false, 4, 6, 2, 2 },
        { "a.", "a%u.h", false, 4, 6, 2, 2 },
        // A name past ASCII.
        { "\xc3\xa9", "a%u.h", false, 4, 6, 2, 2 },
        // Names in the directories themselves, and paths under the long component (ENAMETOOLONG): opened every time.
        { "", "z%u.h", false, 4, 8, 0, 0 },
        { long_component, "a%u.h", false, 2, 4, 0, 0 },
        // Two names that differ past their eighth byte are two components: a slot's halves are both compared.
        { "samehead-a", "a%u.h", false, 4, 6, 2, 2 },
        { "samehead-b", "a%u.h", false, 1, 2, 0, 0 },
        // A name of as many bytes as a slot holds, and one of a byte more, which is not counted.
        { "fifteen-bytes-x", "a%u.h", false, 4, 6, 2, 2 },
        { "sixteen-bytes-xy", "a%u.h", false, 4, 8, 0, 0 },
        // The slots: `first` counts thirteen components by here (`plain` is none of its) and `second` fourteen, so
        // `first` has slots for three of these and `second` for two: a component without a slot is opened every time.
        { "c00", "a%u.h", false, 4, 6, 2, 2 },
        { "c01", "a%u.h", false, 4, 6, 2, 2 },
        { "c02", "a%u.h", false, 4, 7, 1, 1 },
        { "c03", "a%u.h", false, 4, 8, 0, 0 },
        { "c04", "a%u.h", false, 4, 8, 0, 0 },
        // Headers `second`'s `bits` lacks, more than a count holds: `first` answers, `second` is opened, and is not
        // asked for the component again when its count stops.
        { "bits", "m%u.h", false, 260, 260, 0, 260 },
    };
    const char* const directories[] = { first, second };
    PPOptions options = {
        .language = {
            .standard = 2023,
        },
        .no_standard_includes = true,
        .include_directories = directories,
        .include_directory_count = BUSTER_ARRAY_LENGTH(directories),
        .quiet = true,
    };
    // Units 0 (asks) and 1 (the asks off) search every name, with listings off; unit 2 shares unit 0's cache; units 3
    // (asks and listings) and 4 (both off) search until the directories are listed, and after.
    static InternTable tables[5];
    static Preprocessor units[5];
    for (uint32_t u = 0; u < 5; u += 1)
    {
        units[u] = (Preprocessor){ 0 };
        options.files = u == 2 ? units[0].cache : 0;
        result = result && intern_initialize(&tables[u]) && pp_initialize(&units[u], &options, u == 2 ? &tables[0] : &tables[u]);
    }
    result = result && units[0].cache->asking_misses == asking;
    if (result)
    {
        units[0].cache->listing_misses = 0;
        units[1].cache->listing_misses = 0;
        units[1].cache->asking_misses = 0;
        units[4].cache->listing_misses = 0;
        units[4].cache->asking_misses = 0;
    }
    uint64_t expected_opens = 0;
    uint64_t expected_asks = 0;
    uint64_t expected_answered = 0;
    for (uint32_t g = 0; result && g < BUSTER_ARRAY_LENGTH(groups); g += 1)
    {
        static char name[400];
        for (uint32_t turn = 0; result && turn < groups[g].turns; turn += 1)
        {
            const int prefix = snprintf(name, sizeof(name), "%s%s", groups[g].component, groups[g].component[0] != 0 ? "/" : "");
            snprintf(name + prefix, sizeof(name) - (size_t)prefix, groups[g].file, turn);
            uint32_t found[2];
            uint32_t files[2];
            for (uint32_t u = 0; u < 2; u += 1)
            {
                files[u] = pp_find_include(&units[u], (const uint8_t*)name, (uint32_t)strlen(name), true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found[u],
                                           BUSTER_PP_NONE);
            }
            // The asks change no search's result, and an answer is an open the unit without them makes.
            result = (files[0] != BUSTER_PP_NONE) == groups[g].found && (files[1] != BUSTER_PP_NONE) == groups[g].found && found[0] == found[1] &&
                     units[0].probes.listings == 0 && units[1].probes.opens == units[0].probes.opens + units[0].probes.listed && units[1].probes.asks == 0 &&
                     units[1].probes.listed == 0;
        }
        expected_opens += groups[g].opens;
        expected_asks += groups[g].asks;
        expected_answered += groups[g].answered;
        result = result && units[0].probes.opens == expected_opens && units[0].probes.asks == expected_asks && units[0].probes.listed == expected_answered;
        if (!result)
        {
            printf("directory asks: group %u, to %.40s: found %u, opens %lu/%lu, asks %lu/%lu, answered %lu/%lu, the opens without asks %lu\n", g, name,
                   groups[g].found, (unsigned long)units[0].probes.opens, (unsigned long)expected_opens, (unsigned long)units[0].probes.asks,
                   (unsigned long)expected_asks, (unsigned long)units[0].probes.listed, (unsigned long)expected_answered, (unsigned long)units[1].probes.opens);
        }
    }
    result = result && pp_test_same_lookups(&units[0], &units[1]);
    // The unit that shares the cache: `first` answers for `bits` at once, and both answer for `none`, without an ask.
    if (result)
    {
        uint32_t found;
        const uint32_t shared = pp_find_include(&units[2], (const uint8_t*)"bits/h4.h", 9, true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found, BUSTER_PP_NONE);
        const uint64_t opens = units[2].probes.opens;
        const uint32_t absent = pp_find_include(&units[2], (const uint8_t*)"none/z.h", 8, true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found, BUSTER_PP_NONE);
        result = shared != BUSTER_PP_NONE && opens == 1 && absent == BUSTER_PP_NONE && units[2].probes.opens == 1 && units[2].probes.listed == 3 &&
                 units[2].probes.asks == 0 && units[2].files[units[2].file_count - 2].error == ENOENT && units[2].files[units[2].file_count - 1].error == ENOENT;
        if (!result)
        {
            printf("directory asks: the shared cache: opens %lu, answered %lu, asks %lu\n", (unsigned long)units[2].probes.opens,
                   (unsigned long)units[2].probes.listed, (unsigned long)units[2].probes.asks);
        }
    }
    // With listings. Four searches under `none`: both directories are asked at its third miss and answer the fourth.
    // Then names in the directories themselves, which no ask answers, bring each directory to its
    // BUSTER_FILE_CACHE_LISTING_MISSES-th miss (three of them `none`'s) and its listing, which answers every name
    // after that one. Then `none` again, which the asks answer; and four searches under `have`, which `first` has
    // and `second` lacks: `second`'s listing answers them all, so that a listed directory is asked for nothing; where
    // this file system's directories aren't listed (lookups a listing can't answer for), `second` is opened three
    // times, asked, and answers the fourth, as `first` is opened every time and asked at its third miss.
    struct statfs system;
    const bool listable = result && statfs(first, &system) == 0 && file_cache_exact_names((uint64_t)system.f_type);
    const uint32_t flat = 40;
    const uint32_t listed_from = BUSTER_FILE_CACHE_LISTING_MISSES - asking;
    result = result && listed_from + 2 < flat;
    expected_opens = 0;
    expected_asks = 0;
    expected_answered = 0;
    uint64_t plain_opens = 0;
    for (uint32_t c = 0; result && c < 4 + flat + 5; c += 1)
    {
        char name[64];
        uint32_t opens = 2;
        uint32_t asks = 0;
        uint32_t answered = 0;
        uint32_t looks = 0;
        if (c < 4)
        {
            snprintf(name, sizeof(name), "none/a%u.h", c);
            opens = c < 3 ? 2 : 0;
            asks = c == 2 ? 2 : 0;
            answered = c < 3 ? 0 : 2;
        }
        else if (c < 4 + flat)
        {
            // The name that is a directory's listing-th miss is opened, and lists it; the names after it are answered.
            const uint32_t q = c - 4;
            snprintf(name, sizeof(name), "q%02u.h", q);
            const bool listed = listable && q >= listed_from;
            opens = listed ? 0 : 2;
            answered = listed ? 2 : 0;
            looks = q + 1 >= listed_from ? 2 : 0;
        }
        else if (c == 4 + flat)
        {
            snprintf(name, sizeof(name), "none/b.h");
            opens = 0;
            answered = 2;
            looks = 2;
        }
        else
        {
            const uint32_t a = c - 4 - flat;
            snprintf(name, sizeof(name), "have/v%u.h", a);
            opens = listable || a > 3 ? 1 : 2;
            asks = !listable && a == 3 ? 2 : 0;
            answered = listable || a > 3 ? 1 : 0;
            looks = 2;
        }
        expected_opens += opens;
        expected_asks += asks;
        expected_answered += answered;
        plain_opens += opens + answered;
        uint32_t found[2];
        uint32_t files[2];
        for (uint32_t u = 0; u < 2; u += 1)
        {
            files[u] = pp_find_include(&units[3 + u], (const uint8_t*)name, (uint32_t)strlen(name), true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found[u],
                                       BUSTER_PP_NONE);
        }
        result = files[0] == BUSTER_PP_NONE && files[1] == BUSTER_PP_NONE && units[3].probes.opens == expected_opens && units[3].probes.asks == expected_asks &&
                 units[3].probes.listed == expected_answered && units[3].probes.listings == looks && units[4].probes.opens == plain_opens &&
                 units[4].probes.asks == 0 && units[4].probes.listed == 0 && units[4].probes.listings == 0;
        if (!result)
        {
            printf("directory asks: with listings, %s: found %u/%u, opens %lu/%lu, asks %lu/%lu, answered %lu/%lu, listings %lu/%u, the opens without either %lu/%lu\n",
                   name, files[0] != BUSTER_PP_NONE, files[1] != BUSTER_PP_NONE, (unsigned long)units[3].probes.opens, (unsigned long)expected_opens,
                   (unsigned long)units[3].probes.asks, (unsigned long)expected_asks, (unsigned long)units[3].probes.listed, (unsigned long)expected_answered,
                   (unsigned long)units[3].probes.listings, looks, (unsigned long)units[4].probes.opens, (unsigned long)plain_opens);
        }
    }
    result = result && pp_test_same_lookups(&units[3], &units[4]);
    for (uint32_t u = 5; u > 0; u -= 1)
    {
        pp_release(&units[u - 1]);
    }
    for (uint32_t u = 0; u < 5; u += 1)
    {
        intern_release(&tables[u]);
    }
    for (uint32_t h = 0; h < 5; h += 1)
    {
        snprintf(path, sizeof(path), "%s/h%u.h", bits, h);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/inner.h", have);
    unlink(path);
    snprintf(path, sizeof(path), "%s/plain", first);
    unlink(path);
    snprintf(path, sizeof(path), "%s/gone", first);
    unlink(path);
    rmdir(have);
    rmdir(bits);
    rmdir(first);
    rmdir(second);
    printf("directory asks: %s (%s; a component a directory lacks, one it has, a file, a dangling link, other spellings, dots, a name past ASCII, a "
           "long one, the slots, a shared cache, listings)\n",
           result ? "as expected" : "WRONG", listable ? "listed" : "this file system's directories aren't listed");
    return result;
}

// The descriptors pp_test_exhaust_descriptors takes: as many as the lowered limit leaves, at most this many.
#define BUSTER_PP_TEST_SPARE_DESCRIPTORS 16

// What pp_test_exhaust_descriptors changed, for pp_test_restore_descriptors.
typedef struct PPTestDescriptors PPTestDescriptors;
struct PPTestDescriptors
{
    struct rlimit saved;
    int taken[BUSTER_PP_TEST_SPARE_DESCRIPTORS + 1];
    uint32_t count;
    bool limited;
};

// Leaves the process no descriptor to open: lowers RLIMIT_NOFILE's soft limit to a few past the lowest free
// descriptor, and takes every one below it, so that an open fails with EMFILE until pp_test_restore_descriptors.
static bool pp_test_exhaust_descriptors(PPTestDescriptors* state)
{
    *state = (PPTestDescriptors){ 0 };
    const int first = open("/dev/null", O_RDONLY | O_CLOEXEC);
    bool result = first >= 0 && getrlimit(RLIMIT_NOFILE, &state->saved) == 0;
    if (first >= 0)
    {
        state->taken[state->count] = first;
        state->count += 1;
    }
    struct rlimit lowered = state->saved;
    lowered.rlim_cur = (rlim_t)first + BUSTER_PP_TEST_SPARE_DESCRIPTORS;
    state->limited = result && lowered.rlim_cur < state->saved.rlim_cur && setrlimit(RLIMIT_NOFILE, &lowered) == 0;
    result = state->limited;
    int taken = result ? 0 : -1;
    while (result && taken >= 0 && state->count <= BUSTER_PP_TEST_SPARE_DESCRIPTORS)
    {
        taken = fcntl(first, F_DUPFD_CLOEXEC, 0);
        if (taken >= 0)
        {
            state->taken[state->count] = taken;
            state->count += 1;
        }
    }
    const int probe = result ? open("/dev/null", O_RDONLY | O_CLOEXEC) : -1;
    result = result && probe < 0 && errno == EMFILE;
    if (probe >= 0)
    {
        close(probe);
    }
    return result;
}

// Gives back what pp_test_exhaust_descriptors took: its descriptors, and the limit.
static void pp_test_restore_descriptors(PPTestDescriptors* state)
{
    for (uint32_t i = 0; i < state->count; i += 1)
    {
        close(state->taken[i]);
    }
    if (state->limited)
    {
        setrlimit(RLIMIT_NOFILE, &state->saved);
    }
    *state = (PPTestDescriptors){ 0 };
}

// The index of the path a unit looked up as `prefix`/`name`, or BUSTER_PP_NONE if it never did.
static uint32_t pp_test_looked_up(const Preprocessor* pp, const char* prefix, const char* name)
{
    char path[4400];
    snprintf(path, sizeof(path), "%s/%s", prefix, name);
    const uint32_t length = (uint32_t)strlen(path);
    uint32_t result = BUSTER_PP_NONE;
    for (uint32_t f = 0; result == BUSTER_PP_NONE && f < pp->file_count; f += 1)
    {
        result = pp->files[f].path_length == length && memcmp(pp->files[f].path, path, length) == 0 ? f : BUSTER_PP_NONE;
    }
    return result;
}

// #653's contract under descriptor exhaustion, which #1564's review asked to pin. Once a search directory is listed,
// a name its listing rules out gets ENOENT without an open, even with no descriptor left, so the search goes on to the
// next directory, whose probe fails with EMFILE and is the one reported. Without listings, the first directory's probe
// fails with EMFILE and ends the search there. A name the listing has is probed as before and fails with EMFILE in the
// first directory either way. The cache keeps each answer: after the descriptors come back, another unit sharing it
// gets the listed directory's ENOENT and the probed directory's EMFILE for those paths, without an open.
static bool pp_test_listing_descriptors(const char* directory)
{
    char first[4200];
    char second[4200];
    char path[4400];
    snprintf(first, sizeof(first), "%s/descriptors-first", directory);
    snprintf(second, sizeof(second), "%s/descriptors-second", directory);
    bool result = mkdir(first, 0700) == 0 && mkdir(second, 0700) == 0;
    snprintf(path, sizeof(path), "%s/listed.h", first);
    result = result && pp_test_write_file(path, "int listed;\n");
    for (uint32_t i = 0; result && i < 40; i += 1)
    {
        snprintf(path, sizeof(path), "%s/h%02u.h", second, i);
        result = pp_test_write_file(path, "int header;\n");
    }
    struct statfs system;
    const bool listable = result && statfs(first, &system) == 0 && file_cache_exact_names((uint64_t)system.f_type);
    const char* const directories[] = { first, second };
    PPOptions options = {
        .language = {
            .standard = 2023,
        },
        .no_standard_includes = true,
        .include_directories = directories,
        .include_directory_count = BUSTER_ARRAY_LENGTH(directories),
        .quiet = true,
    };
    // Unit 0 lists, unit 1 never does, and unit 2 shares unit 0's cache after the descriptors come back.
    static InternTable tables[3];
    static Preprocessor units[3];
    for (uint32_t u = 0; u < 2; u += 1)
    {
        units[u] = (Preprocessor){ 0 };
        result = result && intern_initialize(&tables[u]) && pp_initialize(&units[u], &options, &tables[u]);
    }
    if (result)
    {
        units[1].cache->listing_misses = 0;
    }
    // Every h??.h fails in `first` and is found in `second`: `first` is listed at its BUSTER_FILE_CACHE_LISTING_MISSES-th.
    for (uint32_t c = 0; result && c < 40; c += 1)
    {
        char name[16];
        snprintf(name, sizeof(name), "h%02u.h", c);
        for (uint32_t u = 0; result && u < 2; u += 1)
        {
            uint32_t found;
            result = pp_find_include(&units[u], (const uint8_t*)name, (uint32_t)strlen(name), true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found, BUSTER_PP_NONE) !=
                     BUSTER_PP_NONE;
        }
    }
    const bool listed = units[0].cache->listed_directories != 0;
    result = result && listed == listable && units[1].cache->listed_directories == 0;
    // With no descriptor left: a name nowhere (`absent.h`), and one only `first` has (`listed.h`), each a reported search
    // (an #include's) in both units.
    PPTestDescriptors descriptors = { 0 };
    const bool exhausted = result && pp_test_exhaust_descriptors(&descriptors);
    uint64_t opens[2] = { units[0].probes.opens, units[1].probes.opens };
    uint32_t absent[2] = { BUSTER_PP_NONE, BUSTER_PP_NONE };
    bool fatal[2] = { false, false };
    for (uint32_t u = 0; exhausted && u < 2; u += 1)
    {
        uint32_t found;
        absent[u] = pp_find_include(&units[u], (const uint8_t*)"absent.h", 8, true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found, 0);
        fatal[u] = units[u].fatal;
    }
    uint32_t has[2] = { BUSTER_PP_NONE, BUSTER_PP_NONE };
    for (uint32_t u = 0; exhausted && u < 2; u += 1)
    {
        // A unit's fatal error ends its run; a search after it still looks the path up, which this checks.
        uint32_t found;
        has[u] = pp_find_include(&units[u], (const uint8_t*)"listed.h", 8, true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found, BUSTER_PP_NONE);
    }
    // Also what an exhaustion that failed part of the way took.
    pp_test_restore_descriptors(&descriptors);
    result = result && exhausted;
    // `absent.h`: with the listing, `first`'s answer is ENOENT without an open and `second`'s probe fails with EMFILE;
    // without it, `first`'s probe fails with EMFILE and the search ends there.
    const uint32_t absent_first[2] = { pp_test_looked_up(&units[0], first, "absent.h"), pp_test_looked_up(&units[1], first, "absent.h") };
    const uint32_t absent_second[2] = { pp_test_looked_up(&units[0], second, "absent.h"), pp_test_looked_up(&units[1], second, "absent.h") };
    const int listed_error = listable ? ENOENT : EMFILE;
    result = result && absent[0] == BUSTER_PP_NONE && absent[1] == BUSTER_PP_NONE && fatal[0] && fatal[1] && absent_first[0] != BUSTER_PP_NONE &&
             units[0].files[absent_first[0]].error == listed_error && absent_first[1] != BUSTER_PP_NONE && units[1].files[absent_first[1]].error == EMFILE &&
             absent_second[1] == BUSTER_PP_NONE &&
             (listable ? absent_second[0] != BUSTER_PP_NONE && units[0].files[absent_second[0]].error == EMFILE : absent_second[0] == BUSTER_PP_NONE);
    // `listed.h`: probed in `first` in both units, failing with EMFILE, which an unreported search treats as no file
    // there and goes on past, to `second`, whose probe fails too.
    const uint32_t has_first[2] = { pp_test_looked_up(&units[0], first, "listed.h"), pp_test_looked_up(&units[1], first, "listed.h") };
    result = result && has[0] == BUSTER_PP_NONE && has[1] == BUSTER_PP_NONE && has_first[0] != BUSTER_PP_NONE && units[0].files[has_first[0]].error == EMFILE &&
             has_first[1] != BUSTER_PP_NONE && units[1].files[has_first[1]].error == EMFILE;
    // The opens each unit attempted under exhaustion: `absent.h`'s one (`second`'s with the listing, `first`'s without),
    // and `listed.h`'s two.
    const uint64_t made[2] = { units[0].probes.opens - opens[0], units[1].probes.opens - opens[1] };
    result = result && made[0] == 3u && made[1] == 3u;
    // Another unit through unit 0's cache, with descriptors again: the cached answers, and no open for them.
    units[2] = (Preprocessor){ 0 };
    options.files = units[0].cache;
    const bool shared = result && pp_initialize(&units[2], &options, &tables[0]);
    if (shared)
    {
        uint32_t found;
        const uint32_t again = pp_find_include(&units[2], (const uint8_t*)"absent.h", 8, true, false, BUSTER_PP_NONE, BUSTER_PP_NONE, &found, BUSTER_PP_NONE);
        const uint32_t again_first = pp_test_looked_up(&units[2], first, "absent.h");
        const uint32_t again_second = pp_test_looked_up(&units[2], second, "absent.h");
        result = again == BUSTER_PP_NONE && units[2].probes.opens == (listable ? 0u : 1u) && again_first != BUSTER_PP_NONE &&
                 units[2].files[again_first].error == listed_error &&
                 (listable ? again_second != BUSTER_PP_NONE && units[2].files[again_second].error == EMFILE : true);
    }
    result = result && shared;
    if (!result)
    {
        printf("listing descriptors: exhausted %u, listed %u (%u), absent.h fatal %u/%u, opens %lu/%lu\n", exhausted, listed, listable, fatal[0], fatal[1],
               (unsigned long)made[0], (unsigned long)made[1]);
    }
    for (uint32_t u = 3; u > 0; u -= 1)
    {
        if (u - 1 != 2 || shared)
        {
            pp_release(&units[u - 1]);
        }
    }
    for (uint32_t u = 0; u < 2; u += 1)
    {
        intern_release(&tables[u]);
    }
    for (uint32_t i = 0; i < 40; i += 1)
    {
        snprintf(path, sizeof(path), "%s/h%02u.h", second, i);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/listed.h", first);
    unlink(path);
    rmdir(first);
    rmdir(second);
    printf("listing descriptors: %s (%s; with no descriptor left, a ruled-out name is ENOENT and the next directory's EMFILE "
           "is reported; without listings, the first's)\n",
           result ? "as expected" : "WRONG", listable ? "listed" : "this file system's directories aren't listed");
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
                char source[32];
                snprintf(source, sizeof(source), "source %u", s);
                result = pp_same_streams(&runs[0], pp, "final copy accounting") && pp_same_directive_tables(&runs[0], pp, "final copy accounting") &&
                         (k != 1 || pp_kernel_digest_check(&runs[0], "final-copy", source));
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

// c_predefines_test.c's: drops the -dM lines of clang's patch release's version macros and of __BUSTER__.
static void predefines_test_drop_versions(char* text);

// Compare the macros after processing the source, with clang's flag-output macro disabled before the user's options.
// Later -D/-U options and source directives still affect it normally on both sides.
static bool pp_test_macros(int argument_count, char** arguments, const PPOptions* base, const char* path)
{
    PPOptions options = *base;
    options.predefines_path = 0;
    // -dM turns -C, -CC, -dD and -dI off, as clang's -dM does (pp_parse_options): no comment stays in a definition.
    options.keep_comments = false;
    options.keep_macro_comments = false;
    options.dump_defines = false;
    options.dump_includes = false;
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
    char** clang_arguments = calloc((size_t)argument_count + 1, sizeof(char*));
    result = result && clang_arguments != 0;
    if (result)
    {
        for (int i = 0; i < argument_count; i += 1)
        {
            clang_arguments[i] = arguments[i];
        }
    }
    PPClangOutput clang = result ? pp_test_clang_process("clang", clang_arguments, argument_count, path, true) : (PPClangOutput){ 0 };
    free(clang_arguments);
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
        char key[512];
        snprintf(key, sizeof(key), "%u %s", i, name);
        passed = passed && ran[0] && ran[1] && pp_same_streams(&runs[0], &runs[1], name) && pp_same_directive_tables(&runs[0], &runs[1], name) &&
                 pp_kernel_digest_check(&runs[0], "assembly", key);
        for (uint32_t kernel = 0; passed && kernel < 2; kernel += 1)
        {
            for (uint32_t marking = 0; passed && marking < 2; marking += 1)
            {
                size_t vector_length = 0;
                char* vector_text = pp_test_print(&runs[kernel], marking != 0, &vector_length);
                passed = vector_text != 0 && (marking != 0 || (vector_length == strlen(expected) && memcmp(vector_text, expected, vector_length) == 0));
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
                  pp_test_compile_kernels(&options, path, path, false);
    *tokens += runs[0].output.count;
    PPSpellings ours = { 0 };
    result = result && pp_stream_spellings(&runs[0], &ours) && pp_test_printed(&runs[0], &ours, path, 0);
    if (result && with_clang)
    {
        // -dD's and -dI's directives against clang's, then blanked in a copy of clang's text (pp_test_dumped_directives).
        const bool dumped = runs[0].options.dump_defines || runs[0].options.dump_includes;
        char* copy = dumped && text != 0 ? malloc(length + 1) : 0;
        if (copy != 0)
        {
            memcpy(copy, text, length);
        }
        const char* clang_text = dumped ? copy : text;
        PPPlaced theirs = { 0 };
        result = clang_text != 0 && (!dumped || pp_test_dumped_directives(&runs[0], copy, length, path)) &&
                 pp_placed_tokens((const uint8_t*)clang_text, length, runs[0].split_kinds, pp_test_lex_flags(&runs[0]), &theirs) &&
                 pp_compare_spellings(&runs[0], &ours, &theirs.spellings, path, "clang") && pp_test_placed(&runs[0], &theirs, path);
        pp_placed_release(&theirs);
        free(copy);
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

// The built-in predefines match clang's with its unsupported flag-output capability undefined. The cases and files
// under --clang read clang's own predefines separately.
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
        char* clang_arguments[3] = { (char*)modes[m][0], (char*)modes[m][1], 0 };
        free(pp_test_clang(clang_arguments, count, empty_path, clang_path, &clang_length));
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

// Named universal character names (\N{...}, #1447): a case for pp_test_named_ucns, with the errors and notes it must give
// ("line:column: kind: message" lines, in order), as clang 23 gives them.
typedef struct PPNamedUcnCase PPNamedUcnCase;
struct PPNamedUcnCase
{
    const char* name;
    const char* source;
    const char* standard;
    const char* expected;
    // Options for both sides (PPTestCase.options), or null; and whether the warnings count too, each without its
    // bracketed flag, which the preprocessor's warnings don't print (#1447).
    const char* options;
    bool warnings;
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

// The error and note lines of the diagnostics `text`, with `warnings` the warning lines too (each without its flag), each
// without the path before its "line:column:", into `lines` (room for `capacity`). Returns false if they don't fit.
static bool pp_test_named_lines(const char* text, char* lines, size_t capacity, bool warnings)
{
    size_t length = 0;
    bool fits = capacity != 0;
    lines[0] = 0;
    for (const char* at = text != 0 ? text : ""; fits && *at != 0;)
    {
        const char* end = strchr(at, '\n');
        const size_t size = end != 0 ? (size_t)(end - at) : strlen(at);
        const bool warning = warnings && memmem(at, size, ": warning: ", 11) != 0;
        const bool kept = memmem(at, size, ": error: ", 9) != 0 || memmem(at, size, ": note: ", 8) != 0 || warning;
        // (A warning's or a promoted one's flag, " [-W...]" at its end, left out.)
        const char* flag = warnings && size > 2 && at[size - 1] == ']' ? memmem(at, size, " [-W", 4) : 0;
        const size_t shown = flag != 0 ? (size_t)(flag - at) : size;
        // The path ends at the colon before the line number, the first followed by a digit.
        size_t skip = 0;
        while (kept && skip + 1 < size && !(at[skip] == ':' && at[skip + 1] >= '0' && at[skip + 1] <= '9'))
        {
            skip += 1;
        }
        skip += kept && skip + 1 < size ? 1 : 0;
        fits = !kept || shown - skip + 1 < capacity - length;
        if (kept && fits)
        {
            memcpy(lines + length, at + skip, shown - skip);
            length += shown - skip;
            lines[length++] = '\n';
            lines[length] = 0;
        }
        at = end != 0 ? end + 1 : at + size;
    }
    return fits;
}

// Named UCNs as clang reads them where they aren't in a literal (#1447): within an identifier or a pp-number, a name
// exactly Unicode's of a character the token may have is that character, without a diagnostic; where one starts a token
// (an identifier, or an unknown token), a name not exactly Unicode's is an error at the name, with a note where it is
// one loosely, and a character below U+00A0 an error at the backslash. In active text, a directive's line, a macro's
// definition and an unused argument, and not in a skipped group, a literal or a comment; with and without line splices
// and trigraphs, beside \u, \U and UTF-8 spellings of the same character. Each case's errors and notes must be the ones
// written for it, by both kernels, and with `with_clang` clang's (its -pedantic warnings aside).
static bool pp_test_named_ucns(const char* directory, bool with_clang)
{
    static const PPNamedUcnCase cases[] = {
        {
            .name = "a named UCN beside the character's other spellings",
            .source = "int \\N{LATIN SMALL LETTER E WITH ACUTE} = 1;\nint f(void) { return \xC3\xA9; }\nint g(void) { return \\u00e9 + \\U000000E9 + \\u{e9}; }\n",
            .standard = "gnu17",
            .expected = "",
        },
        {
            .name = "named UCNs in tokens, directives and macros",
            .source = "x\\N{GREEK SMALL LETTER ALPHA}y\n"
                      "1\\N{DIGIT ONE}\n"
                      "\\N{GREEK SMALL LETTER ALPHA}\n"
                      "x\\\\\nN{GREEK SMALL LETTER ALPHA}\n"
                      "z\\N{GREEK SMALL\\\n LETTER ALPHA}\n"
                      "#define A \\N{LATIN CAPITAL LETTER A}\n"
                      "A A\n"
                      "#define DROP(x) kept\n"
                      "DROP(\\N{LINE FEED})\n"
                      "#if \\N{GREEK SMALL LETTER ALPHA}\n"
                      "#elif \\N{LATIN SMALL LETTER NOT A THING}\n"
                      "#endif\n"
                      "#if 0\n"
                      "\\N{GREEK SMALL LETTER ALPHA} \\N{NOT A NAME}\n"
                      "#endif\n"
                      "\"\\N{GREEK SMALL LETTER ALPHA}\" '\\N{DIGIT ZERO}' /* \\N{GREEK SMALL LETTER ALPHA} */ // \\N{DIGIT ZERO}\n",
            .standard = "gnu17",
            .expected = "2:2: error: character '1' cannot be specified by a universal character name\n"
                        "8:11: error: character 'A' cannot be specified by a universal character name\n"
                        "11:6: error: universal character name refers to a control character\n"
                        "13:10: error: 'LATIN SMALL LETTER NOT A THING' is not a valid Unicode character name\n"
                        "13:7: error: invalid token at start of a preprocessor expression\n",
        },
        {
            .name = "named UCNs whose backslashes are trigraphs",
            .source = "x?" "?/N{GREEK SMALL LETTER ALPHA} ?" "?/N{DIGIT ZERO} ?" "?/N{greek small letter alpha}\n",
            .standard = "c17",
            .expected = "1:33: error: character '0' cannot be specified by a universal character name\n"
                        "1:55: error: 'greek small letter alpha' is not a valid Unicode character name\n"
                        "1:55: note: character names in Unicode escape sequences are sensitive to case and whitespaces\n",
        },
        {
            .name = "names Unicode doesn't have, and basic and control characters",
            .source = "\\N{LATIN SMALL LETTER NOT A THING}\na\\N{latin small letter a}\nb\\N{latin small letter e with acute}c \\N{LATIN SMALL LETTER E WITH ACUTE}\n"
                      "\\N{SPACE}\\N{NULL}\n",
            .standard = "gnu17",
            .expected = "1:4: error: 'LATIN SMALL LETTER NOT A THING' is not a valid Unicode character name\n"
                        "2:5: error: 'latin small letter a' is not a valid Unicode character name\n"
                        "2:5: note: character names in Unicode escape sequences are sensitive to case and whitespaces\n"
                        "2:2: error: character 'a' cannot be specified by a universal character name\n"
                        "3:5: error: 'latin small letter e with acute' is not a valid Unicode character name\n"
                        "3:5: note: character names in Unicode escape sequences are sensitive to case and whitespaces\n"
                        "4:1: error: character ' ' cannot be specified by a universal character name\n"
                        "4:10: error: universal character name refers to a control character\n",
        },
        {
            // -Wunicode's warnings (#1447): a `\N` clang reads as no UCN, where a token starts or an identifier goes on, its
            // backslash read as itself; a macro's definition's once, and none in a skipped group.
            .name = "a \\N clang reads as no universal character name",
            .source = "f\\N\n\\Nj\ng\\N{}\nh\\N{FOO\n#define X \\N\nX X\n#if 0\nk\\N\n#endif\n",
            .standard = "gnu17",
            .expected = "1:2: warning: incomplete universal character name; treating as '\\' followed by identifier\n"
                        "2:1: warning: incomplete universal character name; treating as '\\' followed by identifier\n"
                        "3:2: warning: empty delimited universal character name; treating as '\\' 'N' '{' '}'\n"
                        "4:2: warning: incomplete delimited universal character name; treating as '\\' 'N' '{' identifier\n"
                        "5:11: warning: incomplete universal character name; treating as '\\' followed by identifier\n",
            .warnings = true,
        },
        {
            // The same of a `\u` or `\U` clang reads as no universal character name (#1447): no hex digit after it, too few, or
            // a delimited one's empty braces or missing brace, each naming its letter; at the line splice before it, and none
            // in a skipped group.
            .name = "a \\u or \\U clang reads as no universal character name",
            .source = "f\\u\n\\Uj\ng\\u{}\nh\\u{12\ni\\u12\nj\\U0000004\nk\\u{zz}\n#if 0\nl\\u\n#endif\nm\\\n\\u\n",
            .standard = "gnu17",
            .expected = "1:2: warning: \\u used with no following hex digits; treating as '\\' followed by identifier\n"
                        "2:1: warning: \\U used with no following hex digits; treating as '\\' followed by identifier\n"
                        "3:2: warning: empty delimited universal character name; treating as '\\' 'u' '{' '}'\n"
                        "4:2: warning: incomplete delimited universal character name; treating as '\\' 'u' '{' identifier\n"
                        "5:2: warning: incomplete universal character name; treating as '\\' followed by identifier\n"
                        "6:2: warning: incomplete universal character name; treating as '\\' followed by identifier\n"
                        "7:2: warning: incomplete delimited universal character name; treating as '\\' 'u' '{' identifier\n"
                        "11:2: warning: \\u used with no following hex digits; treating as '\\' followed by identifier\n",
            .warnings = true,
        },
        {
            // A `\u` or `\U` clang reads whole that starts a token, of a character below U+00A0 or a surrogate (#1447): its
            // error at the backslash, whatever its digits' form; none of one past U+00A0 or past U+10FFFF.
            .name = "a \\u or \\U of a basic or control character, or a surrogate",
            .source = "\\u0041x g\\uD800 \\u0024a \\u0001b \\u{41}d e\\u0041 \\u{D800}h \\u007Fi \\u00e9j \\U00110000\n",
            .standard = "gnu17",
            .expected = "1:1: error: character 'A' cannot be specified by a universal character name\n"
                        "1:10: error: invalid universal character\n"
                        "1:17: error: character '$' cannot be specified by a universal character name\n"
                        "1:25: error: universal character name refers to a control character\n"
                        "1:33: error: character 'A' cannot be specified by a universal character name\n"
                        "1:42: error: character 'A' cannot be specified by a universal character name\n"
                        "1:49: error: invalid universal character\n"
                        "1:59: error: universal character name refers to a control character\n",
        },
        {
            // -pedantic's warning that a named UCN is clang's extension (#1447): one that starts a token, its name exactly
            // Unicode's, at its backslash and before its character's error; none within an identifier, of a name that isn't
            // Unicode's, in a skipped group, or in a macro's expansion (its definition's once).
            .name = "named UCNs under -pedantic",
            .source = "\\N{GREEK SMALL LETTER ALPHA}x a\\N{GREEK SMALL LETTER ALPHA} \\N{LATIN CAPITAL LETTER A} \\N{FOO}\n"
                      "#define Y \\N{GREEK SMALL LETTER ALPHA}\nY Y\n#if 0\n\\N{GREEK SMALL LETTER ALPHA}\n#endif\n\\N{greek small letter alpha}\n",
            .standard = "gnu17",
            .expected = "1:1: warning: named escape sequences are a Clang extension\n"
                        "1:61: warning: named escape sequences are a Clang extension\n"
                        "1:61: error: character 'A' cannot be specified by a universal character name\n"
                        "1:91: error: 'FOO' is not a valid Unicode character name\n"
                        "2:11: warning: named escape sequences are a Clang extension\n"
                        "7:4: error: 'greek small letter alpha' is not a valid Unicode character name\n"
                        "7:4: note: character names in Unicode escape sequences are sensitive to case and whitespaces\n",
            .options = "-pedantic",
            .warnings = true,
        },
        {
            .name = "named UCNs under -pedantic-errors",
            .source = "\\N{GREEK SMALL LETTER ALPHA}x\n",
            .standard = "gnu17",
            .expected = "1:1: error: named escape sequences are a Clang extension\n",
            .options = "-pedantic-errors",
            .warnings = true,
        },
        {
            .name = "named UCNs under -pedantic without the extension's warning",
            .source = "\\N{GREEK SMALL LETTER ALPHA}x\n",
            .standard = "gnu17",
            .expected = "",
            .options = "-pedantic -Wno-delimited-escape-sequence-extension",
            .warnings = true,
        },
        {
            // -Wunicode's options, -w and pragmas (#1934): off, promoted, silenced, and mapped from the pragma on.
            .name = "-Wno-unicode",
            .source = "f\\N\n",
            .standard = "gnu17",
            .expected = "",
            .options = "-Wno-unicode",
            .warnings = true,
        },
        {
            .name = "-Werror=unicode",
            .source = "f\\N\n",
            .standard = "gnu17",
            .expected = "1:2: error: incomplete universal character name; treating as '\\' followed by identifier\n",
            .options = "-Werror=unicode",
            .warnings = true,
        },
        {
            .name = "-w over -Werror",
            .source = "f\\N\n",
            .standard = "gnu17",
            .expected = "",
            .options = "-w -Werror",
            .warnings = true,
        },
        {
            .name = "-Wunicode's pragmas",
            .source = "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wunicode\"\nf\\N\n#pragma clang diagnostic pop\ng\\N\n"
                      "#pragma clang diagnostic error \"-Wunicode\"\nh\\N\n",
            .standard = "gnu17",
            .expected = "5:2: warning: incomplete universal character name; treating as '\\' followed by identifier\n"
                        "7:2: error: incomplete universal character name; treating as '\\' followed by identifier\n",
            .warnings = true,
        },
        {
            .name = "-pedantic's warning under a pragma",
            .source = "#pragma clang diagnostic ignored \"-Wpedantic\"\n\\N{GREEK SMALL LETTER ALPHA}x\n",
            .standard = "gnu17",
            .expected = "",
            .options = "-pedantic",
            .warnings = true,
        },
    };
    uint32_t failures = 0;
    enum
    {
        LINES = 4096,
    };
    char lines[LINES];
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const PPTestCase test = { .name = cases[c].name, .source = cases[c].source, .standard = cases[c].standard, .expected = "", .options = cases[c].options };
        char path[4096];
        bool result = pp_test_write_case(&test, directory, path, sizeof(path));
        char text[16384];
        char* arguments[BUSTER_PP_TEST_MAX_ARGUMENTS + 1];
        const int argument_count = pp_test_arguments(&test, directory, path, text, sizeof(text), arguments);
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
            if (ready)
            {
                pp_release(&pp);
                intern_release(&table);
            }
            result = stream != 0 && fclose(stream) == 0 && ready && ran == (strstr(cases[c].expected, ": error: ") == 0);
            result = result && pp_test_named_lines(diagnostics, lines, sizeof(lines), cases[c].warnings) && strcmp(lines, cases[c].expected) == 0;
            if (!result)
            {
                printf("named UCNs: %s (%s kernels): not the diagnostics written for it:\n%s", cases[c].name, k == 0 ? "vectorized" : "scalar",
                       diagnostics != 0 ? diagnostics : "");
            }
            free(diagnostics);
            arena_release(&arena);
        }
        if (result && with_clang)
        {
            PPClangOutput clang = pp_test_clang_process("clang", arguments, argument_count, path, false);
            result = clang.status != PP_CLANG_ERROR && pp_test_named_lines(clang.diagnostics, lines, sizeof(lines), cases[c].warnings) && strcmp(lines, cases[c].expected) == 0;
            if (!result)
            {
                printf("named UCNs: %s: clang's diagnostics aren't the ones written for it:\n%s", cases[c].name, clang.diagnostics != 0 ? clang.diagnostics : "");
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

// #455's review: a named UCN of a name Unicode doesn't have (#1447: an error at the name, three bytes past a backslash
// and five past a ??/) at each place where the vector kernels' search (pp_scan_named_ucn_chunks) changes what it
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
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%sN{GREEK SMALL LETTER NOT ALPHA}\n", trigraphs != 0 ? "?" "?/" : "\\");
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
                result = result && pp_test_named_places(diagnostics, "error: 'GREEK SMALL LETTER NOT ALPHA' is not a valid Unicode character name", trigraphs != 0 ? 5 : 3, found,
                                                        BUSTER_ARRAY_LENGTH(found), &count);
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

// BUSTER_PP_TEST_NAMED_UCNS named UCNs in a row (#455's review), in time in proportion to their count, by both kernels
// (the vectorized ones prepare a file of about 1.1 MB a window at a time, and the scalar ones whole): an identifier of
// them, which has no error, then misspelled names each of which starts a token and is an error, its name found loosely
// (#1447: a binary search, where a scan of every name would take most of a minute). Finding each UCN's identifier's end
// from its start took time in proportion to the square of their count. The errors are counted, not printed
// (PPOptions.quiet): a column is found by reading back to its line's start.
#define BUSTER_PP_TEST_NAMED_UCNS 40000
// The scaling oracle is timed under instrumentation too. Hosted TSan and MSan can exceed
// the ordinary budget while reporting every one of the 40,000 expected errors; retain a
// finite quadratic-regression guard for both instrumented recovery paths.
#if __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define BUSTER_PP_TEST_NAMED_UCN_SECONDS 30.0
#else
#define BUSTER_PP_TEST_NAMED_UCN_SECONDS 10.0
#endif

static bool pp_test_named_ucn_scaling(const char* directory)
{
    static const char* const ucns[2] = { "\\N{GREEK SMALL LETTER ALPHA}", "\\N{greek small letter alpha}" };
    char path[4096];
    snprintf(path, sizeof(path), "%s/case.c", directory);
    bool result = true;
    for (uint32_t u = 0; result && u < 2; u += 1)
    {
        const uint32_t size = (uint32_t)strlen(ucns[u]);
        const uint32_t length = 1 + BUSTER_PP_TEST_NAMED_UCNS * size + 1;
        char* source = malloc(length);
        result = source != 0;
        if (result)
        {
            source[0] = 'x';
            for (uint32_t i = 0; i < BUSTER_PP_TEST_NAMED_UCNS; i += 1)
            {
                memcpy(source + 1 + i * size, ucns[u], size);
            }
            source[length - 1] = '\n';
            result = pp_test_write_bytes(path, source, length);
        }
        free(source);
        char standard[] = "-std=gnu17";
        // This checks every misspelled UCN, independently of the command-line diagnostic publication budget.
        char limit[] = "-ferror-limit=0";
        char* arguments[] = { standard, limit, path, 0 };
        for (uint32_t k = 0; result && k < 2; k += 1)
        {
            Arena arena = arena_reserve(1ull << 20);
            PPOptions options;
            const char* input;
            const char* output;
            bool markers;
            result = pp_parse_options(3, arguments, &options, &arena, &input, &output, &markers);
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
            const uint32_t wanted = u == 0 ? 0 : BUSTER_PP_TEST_NAMED_UCNS;
            result = ready && ran == (wanted == 0) && errors == wanted && seconds < BUSTER_PP_TEST_NAMED_UCN_SECONDS;
            printf("named UCNs: %u %s (%s kernels): %u errors in %.3f s%s\n", BUSTER_PP_TEST_NAMED_UCNS, u == 0 ? "in one identifier" : "misspelled in a row",
                   k == 0 ? "vectorized" : "scalar", errors, seconds, result ? "" : ", not the errors wanted in time");
            arena_release(&arena);
        }
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

// #595's review: an argument expanded in place (pp_expand_argument_in_place) that runs out of room, as the history records
// it, ends the run before anything is substituted from it. X's body is 1,100 tokens, past the 1,024 origins a stack
// starts with, so expanding F(X)'s argument grows its origins first. The direct path expands in pp->arguments; the
// capture path — the scalar request — first captures the expansion. Limit the buffer
// each path actually fills: each fault must give one
// fatal error and no output token. The control, with nothing taken away, gives F's whole expansion in both modes.
static bool pp_test_direct_argument_failures(void)
{
    enum
    {
        PP_TEST_DIRECT_TOKENS = 1100,
    };
    static char source[PP_TEST_DIRECT_TOKENS * 2 + 64];
    uint32_t length = (uint32_t)snprintf(source, sizeof(source), "#define F(a) [a]\n#define X");
    for (uint32_t t = 0; t < PP_TEST_DIRECT_TOKENS; t += 1)
    {
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, " x");
    }
    length += (uint32_t)snprintf(source + length, sizeof(source) - length, "\nF(X)\n");
    bool result = true;
    for (uint32_t run = 0; run < 6; run += 1)
    {
        const uint32_t fault = run % 3;
        static InternTable table;
        static Preprocessor pp;
        const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .scalar = run >= 3, .quiet = true };
        const bool initialized = intern_initialize(&table) && pp_initialize(&pp, &options, &table);
        const uint32_t capacity = initialized ? pp.arguments.capacity : 0;
        const uint32_t captures = initialized ? pp.captures.capacity : 0;
        pp.fail_origin_growth = initialized && fault == 0;
        pp.arguments.capacity = initialized && fault == 1 ? 64 : capacity;
        pp.captures.capacity = initialized && fault == 1 && !pp.vectorized ? 64 : captures;
        const bool ran = initialized && pp_run_source(&pp, "direct-argument-failure.c", (const uint8_t*)source, length);
        const bool passed = initialized && (fault == 2 ? ran && !pp.fatal && pp.output.count == PP_TEST_DIRECT_TOKENS + 2
                                                       : !ran && pp.fatal && pp.errors == 1 && pp.output.count == 0);
        if (!passed)
        {
            printf("direct argument failure %u, scalar %u: ran %u, fatal %u, errors %u, output %u\n", fault, options.scalar, ran, initialized && pp.fatal,
                   initialized ? pp.errors : 0, initialized ? pp.output.count : 0);
        }
        result = result && passed;
        pp.arguments.capacity = capacity;
        pp.captures.capacity = captures;
        pp.fail_origin_growth = false;
        if (initialized)
        {
            pp_release(&pp);
        }
        intern_release(&table);
    }
    printf("direct argument failures: %s (origins' growth, the arguments' room, control)\n", result ? "pass" : "FAIL");
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

#include "c_preprocessor_forced_include_test.c"

// An option set of #281's families, or one beside them (%d stands for the test's directory): `refused` is in the message
// of the option pp_parse_options must refuse, or null for a set it must take without a message.
typedef struct PPTestOptionSet PPTestOptionSet;
struct PPTestOptionSet
{
    const char* arguments[BUSTER_PP_TEST_SET_ARGUMENTS];
    const char* refused;
};

static bool pp_test_option_row(PPTestFixtures* table, const char* directory, char** fields, uint32_t line)
{
    (void)directory;
    (void)line;
    PPTestOptionSet* row = pp_test_fixture_row(table, fields[0]);
    char* arguments = fields[2];
    uint32_t count = 0;
    bool valid = row != NULL && strlen(arguments) < 1000;
    if (valid)
    {
        row->refused = strcmp(fields[1], "-") == 0 ? NULL : fields[1];
    }
    while (valid && arguments != NULL)
    {
        char* argument = strsep(&arguments, " ");
        valid = *argument != 0 && count < BUSTER_PP_TEST_SET_ARGUMENTS;
        if (valid)
        {
            row->arguments[count++] = argument;
        }
    }
    return valid;
}

// Each option set is refused or taken as it says. With --clang, clang's -E of probe.c must
// change with each refused set (from the set's -std= alone, or from no options), which is why buster refuses it, and
// each taken set must give clang's tokens, by both kernels.
static bool pp_test_option_refusals(const char* directory, bool with_clang)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/options.c", directory);
    PPTestFixtures table;
    const char* probe = NULL;
    bool result = pp_test_fixture_table(&table, "option-refusals", "buster-fixtures v1 preprocessor-option-refusals", 3, sizeof(PPTestOptionSet), pp_test_option_row, &probe) &&
                  pp_test_write_file(path, probe);
    const PPTestOptionSet* sets = (const PPTestOptionSet*)table.rows;
    uint32_t wrong = 0;
    uint32_t unchanged = 0;
    uint32_t different = 0;
    uint32_t refused_count = 0;
    // clang's -E of the probe with each base a refused set is compared with (its -std=, or none: "" here), run once each.
    const char** base_keys = arena_push(&table.storage, sizeof(*base_keys) * table.count, 8);
    PPClangOutput* bases = arena_push(&table.storage, sizeof(*bases) * table.count, 8);
    result = result && base_keys != NULL && bases != NULL;
    uint32_t base_count = 0;
    for (uint32_t s = 0; result && s < table.count; s += 1)
    {
        const PPTestOptionSet* set = &sets[s];
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
            for (const char* at = set->arguments[n]; *at != 0; at += 1)
            {
                const bool substitute = at[0] == '%' && at[1] == 'd';
                const size_t size = substitute ? strlen(directory) : 1;
                if (used + size >= sizeof(text))
                {
                    result = false;
                    break;
                }
                memcpy(text + used, substitute ? directory : at, size);
                used += (uint32_t)size;
                at += substitute;
            }
            if (!result)
            {
                break;
            }
            text[used++] = 0;
            shown_used += (uint32_t)snprintf(shown + shown_used, sizeof(shown) - shown_used, "%s%s", n != 0 ? " " : "", set->arguments[n]);
        }
        if (!result)
        {
            fprintf(stderr, "option-refusals: expanded arguments for %s exceed the staging buffer\n", table.identifiers[s]);
            break;
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
            char* standard = NULL;
            const char* key = "";
            for (int i = 0; i < n; i += 1)
            {
                if (strncmp(arguments[i], "-std=", 5) == 0)
                {
                    standard = arguments[i];
                    key = set->arguments[i];
                }
            }
            uint32_t b = 0;
            while (b < base_count && strcmp(base_keys[b], key) != 0)
            {
                b += 1;
            }
            const int keep = pp_test_catch_stderr(&caught);
            PPClangOutput with = pp_test_clang_process("clang", arguments, n, path, false);
            if (b == base_count)
            {
                base_keys[b] = key;
                bases[b] = pp_test_clang_process("clang", &standard, standard != NULL, path, false);
                base_count += 1;
            }
            pp_test_release_stderr(keep, caught, messages, sizeof(messages));
            const PPClangOutput* without = &bases[b];
            const bool changed = with.status != without->status || with.length != without->length ||
                                 (with.length != 0 && memcmp(with.text, without->text, with.length) != 0);
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
    printf("option sets: %u of %lu refused or taken wrongly", wrong, (unsigned long)table.count);
    if (with_clang)
    {
        printf(", %u of %u refused ones change nothing in clang's -E and %u of %lu taken ones differ from clang's", unchanged, refused_count, different,
               (unsigned long)table.count - refused_count);
    }
    printf("\n");
    arena_release(&table.storage);
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
        char key[512];
        snprintf(key, sizeof(key), "%u %s", r, test.name);
        result = result && pp_same_streams(&preprocessors[0], &preprocessors[1], test.name) && pp_kernel_digest_check(&preprocessors[0], "timestamps", key) &&
                 pp_stream_spellings(&preprocessors[0], &ours) &&
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

// The fuzz batch's options, which c_preprocessor_fuzz.c (included after this file) runs with pp_fuzz_checks and
// pp_fuzz_batch.
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

// The text of an #embed's data: pp_embed_text_vector must write the values in decimal separated by ", " (from snprintf), for every count to 300 of random bytes and of every value in turn, with
// and without a separator first; and at the end of a readable page before a PROT_NONE one, where a read past the bytes
// faults. The output has the vector kernel's 64 bytes of slack.
static bool pp_test_embed_text(uint64_t seed)
{
    const uint64_t page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 2 * page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = mapping != MAP_FAILED && mprotect(mapping + page_bytes, page_bytes, PROT_NONE) == 0;
    static uint8_t vector[5 * 300 + 64];
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
                uint64_t reference_length = 0;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    reference_length += (uint64_t)snprintf(reference + reference_length, sizeof(reference) - reference_length, "%s%u", separated || i != 0 ? ", " : "", bytes[i]);
                }
                const bool same = vector_length == reference_length && memcmp(vector, reference, reference_length) == 0;
                if (!same && failures < 5)
                {
                    printf("embed text: %u bytes (%s, %s): ours %.*s\n  wanted %.*s\n", count, pattern == 0 ? "random" : "in turn",
                           separated ? "separated" : "first", (int)vector_length, (const char*)vector, (int)reference_length, reference);
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

// #615: sparse/high ids, absent names, repeated insertions and growth failures must keep the first ordinal. The
// scalar and vector small-list paths are checked against the insertion order, including the transition to hashing.
static bool pp_test_parameter_index(void)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    uint8_t* guarded = page_size > 0 ? mmap(0, (size_t)page_size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool result = guarded != MAP_FAILED && mprotect(guarded + page_size, (size_t)page_size, PROT_NONE) == 0;
    for (uint32_t count = 0; result && count <= BUSTER_PP_SMALL_PARAMETERS; count += 1)
    {
        IdentId* names = (IdentId*)(guarded + page_size) - count;
        for (uint32_t p = 0; p < count; p += 1)
        {
            names[p] = (p + 1) * 8192;
        }
        for (uint32_t mode = 0; result && mode < 2; mode += 1)
        {
            for (uint32_t p = 0; result && p <= count; p += 1)
            {
                const uint32_t wanted = p < count ? p : BUSTER_PP_NONE;
                result = pp_parameter_index_small(names, count, (p + 1) * 8192, mode == 0) == wanted &&
                         pp_parameter_index_small(names, count, (p + 1) * 8192 + 1, mode == 0) == BUSTER_PP_NONE &&
                         pp_parameter_index_small(names, count, 0, mode == 0) == BUSTER_PP_NONE;
            }
            if (result && count > 1)
            {
                names[count - 1] = names[0];
                result = pp_parameter_index_small(names, count, names[0], mode == 0) == 0;
                names[count - 1] = count * 8192;
            }
        }
    }
    if (guarded != MAP_FAILED)
    {
        munmap(guarded, (size_t)page_size * 2);
    }
    Scratch descriptor = scratch_begin(0);
    const uint64_t descriptor_capacity = descriptor.arena->capacity;
    descriptor.arena->capacity = descriptor.arena->length;
    PPParameterIndex* allocated = pp_parameter_index_allocate();
    result = result && allocated == 0 && descriptor.arena->length == descriptor.length;
    descriptor.arena->capacity = descriptor_capacity;
    scratch_end(descriptor);
    if (result)
    {
        Scratch outer = scratch_begin(0);
        allocated = pp_parameter_index_allocate();
        result = allocated != 0;
        for (uint32_t p = 0; result && p < BUSTER_PP_SMALL_PARAMETERS; p += 1)
        {
            uint32_t previous;
            result = pp_parameter_insert(allocated, p, p, &previous);
        }
        const uint64_t capacity = outer.arena->capacity;
        outer.arena->capacity = outer.arena->length;
        uint32_t previous;
        result = result && !pp_parameter_insert(allocated, BUSTER_PP_SMALL_PARAMETERS, BUSTER_PP_SMALL_PARAMETERS, &previous);
        outer.arena->capacity = capacity;
        if (allocated != 0)
        {
            pp_parameter_index_end(allocated);
        }
        result = result && outer.arena->length == outer.length;
        scratch_end(outer);
    }
    if (result)
    {
        Scratch outer = scratch_begin(0);
        IdentId names[BUSTER_PP_SMALL_PARAMETERS + 1];
        for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(names); p += 1)
        {
            names[p] = p;
        }
        const uint64_t capacity = outer.arena->capacity;
        outer.arena->capacity = outer.arena->length + sizeof(PPParameterIndex) + _Alignof(PPParameterIndex);
        allocated = pp_parameter_index_build(names, BUSTER_ARRAY_LENGTH(names));
        result = allocated == 0 && outer.arena->length == outer.length;
        outer.arena->capacity = capacity;
        scratch_end(outer);
    }
    PPParameterIndex index = { 0 };
    for (uint32_t p = 0; result && p < BUSTER_PP_MAX_PARAMETERS; p += 1)
    {
        const IdentId id = p * 8192;
        uint32_t previous;
        result = pp_parameter_insert(&index, id, p, &previous) && previous == BUSTER_PP_NONE;
        for (uint32_t q = 0; result && q <= p; q += 1)
        {
            result = pp_parameter_index(&index, q * 8192) == q && pp_parameter_index(&index, q * 8192 + 1) == BUSTER_PP_NONE;
        }
        result = result && pp_parameter_insert(&index, id, p + 1, &previous) && previous == p && index.count == p + 1;
    }
    pp_parameter_index_end(&index);
    for (uint32_t count = BUSTER_PP_SMALL_PARAMETERS; result && count <= 2 * BUSTER_PP_SMALL_PARAMETERS; count *= 2)
    {
        PPParameterIndex grown = { 0 };
        uint32_t previous;
        for (uint32_t p = 0; result && p < count; p += 1)
        {
            result = pp_parameter_insert(&grown, p, p, &previous);
        }
        Scratch scratch = scratch_begin(0);
        const uint64_t capacity = scratch.arena->capacity;
        scratch.arena->capacity = scratch.arena->length;
        result = result && !pp_parameter_insert(&grown, count, count, &previous) && grown.count == count;
        scratch.arena->capacity = capacity;
        for (uint32_t p = 0; result && p < count; p += 1)
        {
            result = pp_parameter_index(&grown, p) == p;
        }
        scratch_end(scratch);
        pp_parameter_index_end(&grown);
    }
    printf("macro parameter index: %s (guarded small views, sparse ids, first ordinals, misses and failed growth)\n", result ? "pass" : "FAIL");
    return result;
}

// Large definitions, unused and invoked twice, with replacement identifiers in reverse order. These exercise every
// stored ordinal rather than just the first/last, plus the small/vector/hash boundaries and the legal maximum.
static bool pp_test_many_parameters(const char* directory, bool with_clang)
{
    static const uint32_t counts[] = { 0, 1, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 1000, 2000, 4000, BUSTER_PP_MAX_PARAMETERS };
    bool result = pp_test_parameter_index();
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(counts); c += 1)
    {
        const uint32_t count = counts[c];
        char* source = 0;
        char* expected = 0;
        size_t source_length = 0;
        size_t expected_length = 0;
        FILE* input = open_memstream(&source, &source_length);
        FILE* output = open_memstream(&expected, &expected_length);
        result = input != 0 && output != 0;
        for (uint32_t unused = 0; result && unused < 2; unused += 1)
        {
            fprintf(input, "#define %s(", unused ? "UNUSED" : "F");
            for (uint32_t p = 0; p < count; p += 1)
            {
                fprintf(input, "%sp%u", p != 0 ? "," : "", p);
            }
            fprintf(input, ") other");
            for (uint32_t p = count; p != 0; p -= 1)
            {
                fprintf(input, " p%u", p - 1);
            }
            fprintf(input, "\n");
        }
        for (uint32_t use = 0; result && use < 2; use += 1)
        {
            fprintf(input, "F(");
            fprintf(output, "other");
            for (uint32_t p = 0; p < count; p += 1)
            {
                fprintf(input, "%s%u", p != 0 ? "," : "", p);
                fprintf(output, " %u", count - p - 1);
            }
            fprintf(input, ")\n");
            fprintf(output, "\n");
        }
        if (input != 0)
        {
            result = fclose(input) == 0 && result;
        }
        if (output != 0)
        {
            result = fclose(output) == 0 && result;
        }
        const PPTestCase test = {
            .name = "many macro parameters: reversed replacement and unused definition",
            .source = source,
            .standard = "c23",
            .expected = expected,
        };
        result = result && pp_test_case(&test, directory, with_clang);
        if (!result)
        {
            printf("many macro parameters: count %u failed\n", count);
        }
        free(source);
        free(expected);
    }
    static const PPTestCase operators[] = {
        {
            .name = "indexed parameters: #, ##, variadics and __VA_OPT__",
            .source = "#define F(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,...) #q a##q __VA_OPT__(#a q __VA_ARGS__) other\n"
                      "F(x,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,y)\n"
                      "F(x,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,y,z)\n"
                      "#undef F\n#define F(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q...) #q a##p q\n"
                      "F(x,1,2,3,4,5,6,7,8,9,10,11,12,13,14,y,z)\n"
                      "#undef F\n#define F(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,__VA_ARGS__,...) #__VA_ARGS__ __VA_ARGS__\n"
                      "F(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,first,last)\n",
            .standard = "gnu23",
            .expected = "\"y\" xy other \"y\" xy \"x\" y z other \"z\" xy z \"first\" first",
        },
        {
            .name = "indexed parameters: every # in an unused definition",
            .source = "#define F(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q) #a #b #c #d #e #f #g #h #i #j #k #l #m #n #o #p #q\nOK\n",
            .standard = "c23",
            .expected = "OK",
        },
    };
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(operators); c += 1)
    {
        result = pp_test_case(&operators[c], directory, with_clang);
    }
    // A duplicate before another syntax error, that error before the duplicate, and a valid over-limit list. Inspect
    // the diagnostic itself: merely checking failure would miss a changed diagnostic order or a wrapped count.
    static const uint32_t invalid_counts[] = { 16, 17, 33, 4097, 65536 };
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(invalid_counts); c += 1)
    {
        const uint32_t count = invalid_counts[c];
        for (uint32_t variant = 0; result && variant < 3; variant += 1)
        {
            char* source = 0;
            size_t length = 0;
            FILE* input = open_memstream(&source, &length);
            uint64_t column = 0;
            result = input != 0;
            if (result)
            {
                fprintf(input, "#define F(");
                for (uint32_t p = 0; p < count; p += 1)
                {
                    fprintf(input, "%sp%u", p != 0 ? "," : "", p);
                }
                column = (uint64_t)ftell(input) + (variant < 2 ? 2 : 4);
                column = variant == 2 && count > BUSTER_PP_MAX_PARAMETERS ? 9 : column;
                fprintf(input, "%s\n", variant == 0 ? ",p0,1) #bad" : variant == 1 ? ",1,p0) #bad" : ") #bad");
                result = fclose(input) == 0;
            }
            for (uint32_t mode = 0; result && mode < 2; mode += 1)
            {
                char* diagnostics = 0;
                size_t diagnostic_length = 0;
                FILE* stream = open_memstream(&diagnostics, &diagnostic_length);
                static Preprocessor pp;
                static InternTable table;
                const PPOptions options = { .scalar = mode != 0, .diagnostics = stream, .language = { .standard = 2023 } };
                const bool ready = stream != 0 && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
                const bool ran = ready && pp_run_source(&pp, "parameters.c", (const uint8_t*)source, length);
                result = ready && !ran && pp.errors == 1 && !pp.fatal;
                if (ready)
                {
                    pp_release(&pp);
                }
                intern_release(&table);
                if (stream != 0)
                {
                    result = fclose(stream) == 0 && result;
                }
                const char* wanted = variant == 0 ? "duplicate macro parameter name 'p0'" : variant == 1 ? "invalid token in macro parameter list"
                                    : count > BUSTER_PP_MAX_PARAMETERS ? "too many macro parameters" : "'#' is not followed by a macro parameter";
                char place[128];
                snprintf(place, sizeof(place), "parameters.c:1:%lu: error: ", (unsigned long)column);
                result = result && diagnostics != 0 && strstr(diagnostics, wanted) != 0 && strstr(diagnostics, place) != 0;
                if (!result)
                {
                    printf("macro parameter diagnostics: count %u variant %u mode %u: wanted %s, got %s\n", count, variant, mode, wanted,
                           diagnostics != 0 ? diagnostics : "(none)");
                }
                free(diagnostics);
            }
            free(source);
        }
    }
    printf("many macro parameters: %s (boundaries, maximum, operators and diagnostic order)%s\n", result ? "pass" : "FAIL", with_clang ? " (Clang oracle)" : "");
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

#include "c_preprocessor_header_diagnostics_test.c"

// buster's headers among the C library's and clang's other ones (pp_test_system_headers, pp_test_clang_headers), in
// one mode, hosted or `freestanding`: two units, the others first and then buster's 11, and buster's first, each
// followed by `checks` (the mode's check lines, whose #include lines then include nothing). clang preprocesses each
// with its own headers, and with buster's in the search --buster-headers makes (-nobuiltininc -isystem `ours` -isystem
// `links`: pp_test_buster_search), both with -Wsystem-headers, and:
// - with buster's, clang adds no header warning beyond those with its own headers (including clang/glibc warnings
//   such as glibc 2.39's __INT64_C redefinition), and gives the unit itself the same diagnostics;
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
    char* theirs[6];
    char* mine[11];
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
    theirs[their_count++] = "-fno-caret-diagnostics";
    mine[my_count++] = "-fno-caret-diagnostics";
    theirs[their_count++] = "-fno-color-diagnostics";
    mine[my_count++] = "-fno-color-diagnostics";
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
        // Subtract only identical reference header warnings, one occurrence at a time. The unit's own diagnostics
        // (ATOMIC_VAR_INIT is deprecated in C17) still compare independently below; neither run may fail.
        if (same && !pp_test_header_warnings(their_text.diagnostics, my_text.diagnostics, path))
        {
            printf("%s: with ours, clang adds header diagnostics:\nreference:\n%s\nours:\n%s", label,
                   their_text.diagnostics != NULL ? their_text.diagnostics : "", my_text.diagnostics != NULL ? my_text.diagnostics : "");
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
        for (uint32_t i = 0; result && i < pp.search.directory_count; i += 1)
        {
            search = strcmp(pp.search.directories[i], resource) == 0 ? i : search;
        }
        result = result && search != BUSTER_PP_NONE && (buster == 0 || search == pp.search.fallback_directory);
        const uint32_t includer = result ? pp_lookup_path(&pp, includer_path, (uint32_t)strlen(includer_path), BUSTER_PP_NONE, 0, BUSTER_PP_SYSTEM_FILE) : BUSTER_PP_NONE;
        result = includer != BUSTER_PP_NONE && pp.files[includer].data != BUSTER_PP_NONE;
        uint32_t found_in = BUSTER_PP_NONE;
        const uint32_t stddef = result ? pp_find_include(&pp, (const uint8_t*)"stddef.h", 8, false, false, includer, search, &found_in, BUSTER_PP_NONE) : BUSTER_PP_NONE;
        const char* expected = buster != 0 ? pp.search.buster_directory : resource;
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
    snprintf(links, sizeof(links), "%s/clang-command", directory);
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
// and what they added to the pp_test_system_ counters. Unless `keep` (--keep), the files the cases leave are removed.
static PPTestChildCounts pp_test_case_part(const char* directory, bool with_clang, bool keep, uint32_t first, uint32_t end)
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
    // What the cases leave, which run_preprocessor_fixture_tests removes from its own directory unless --keep.
    static const char* const names[] = { "case.c", "other.h", "predefines.h" };
    for (uint32_t i = 0; !keep && i < BUSTER_ARRAY_LENGTH(names); i += 1)
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
    // --keep: the parts' directories and the files the cases leave stay.
    bool keep;
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
    // The cases' parts take the first `count` cases, or every one.
    const uint32_t cases = children->count != 0 ? children->count : pp_test_case_count;
    switch (children->work)
    {
        case PP_TEST_CHILD_HEADERS:
            counts = pp_test_header_set(own, children->ours, children->links, children->empty, part / 2, part % 2);
            break;
        case PP_TEST_CHILD_CASES:
            counts = pp_test_case_part(own, children->with_clang, children->keep, (uint32_t)((uint64_t)cases * part / children->parts),
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
                    if (!children->keep && rmdir(own) != 0)
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
    snprintf(links, sizeof(links), "%s/clang-search", directory);
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

// The cases' parts' cleanup (#465's review): the first two cases in two parts, run as the cases are without --keep and
// with it, in a directory of their own. Without it, the parts' directories must be gone; with it, each part's must hold
// its last case's case.c. Prints a line and returns whether both were so.
static bool pp_test_case_retention(const char* directory)
{
    bool result = true;
    for (uint32_t keep = 0; keep < 2; keep += 1)
    {
        char own[4200];
        snprintf(own, sizeof(own), "%s/retention-%u", directory, keep);
        const PPTestChildren children = {
            .work = PP_TEST_CHILD_CASES,
            .parts = 2,
            .count = 2,
            .directory = own,
            .keep = keep != 0,
        };
        const bool made = mkdir(own, 0700) == 0;
        const PPTestChildCounts counts = made ? pp_test_children(&children) : (PPTestChildCounts){ 0 };
        bool as_wanted = made && counts.result && counts.failures == 0;
        for (uint32_t part = 0; part < 2; part += 1)
        {
            char path[4300];
            snprintf(path, sizeof(path), "%s/cases-%u/case.c", own, part);
            struct stat status;
            const bool kept = stat(path, &status) == 0;
            unlink(path);
            snprintf(path, sizeof(path), "%s/cases-%u", own, part);
            const bool left = rmdir(path) == 0;
            as_wanted = as_wanted && kept == (keep != 0) && left == (keep != 0);
        }
        as_wanted = rmdir(own) == 0 && as_wanted;
        if (!as_wanted)
        {
            printf("case retention: the parts' files %s\n", keep != 0 ? "weren't kept with --keep" : "were left without --keep");
        }
        result = result && as_wanted;
    }
    printf("case retention: the parts' files %s\n", result ? "go, and stay with --keep" : "WRONG");
    return result;
}

// The tree an executable's path names (pp_buster_tree): the published build/compiler's, a configuration's
// build/cmake/VARIANT/CONFIG/compiler's, the last build/ of a tree inside another's build/, and two levels up where no
// build/ directory is in the path; none for an executable in /build itself.
static bool pp_test_buster_tree(void)
{
    static const struct
    {
        const char* executable;
        const char* tree;
    } cases[] = {
        { "/home/user/buster/build/compiler", "/home/user/buster" },
        { "/home/user/buster/build/cmake/native/Release/compiler", "/home/user/buster" },
        { "/w/build/trees/buster/build/cmake/x86-64-g0/Debug/compiler", "/w/build/trees/buster" },
        { "/tmp/shifts/main/compiler-688", "/tmp/shifts" },
        { "/build/compiler", 0 },
    };
    bool result = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s", cases[i].executable);
        const bool found = pp_buster_tree(path);
        const bool right = cases[i].tree != 0 ? found && strcmp(path, cases[i].tree) == 0 : !found;
        if (!right)
        {
            printf("headers: %s's tree is %s, not %s\n", cases[i].executable, found ? path : "none", cases[i].tree != 0 ? cases[i].tree : "none");
        }
        result = result && right;
    }
    return result;
}

static bool pp_test_headers(const char* directory)
{
    char ours[PATH_MAX];
    const bool diagnostics = pp_test_header_warning_fixtures() && pp_test_header_warning_oracle(directory);
    const bool trees = pp_test_buster_tree();
    bool result = pp_buster_headers_directory(ours, sizeof(ours), 0);
    if (!result)
    {
        printf("headers: no src/buster/include beside the executable's build directory\n");
    }
    char empty[4096];
    char links[4096];
    snprintf(empty, sizeof(empty), "%s/headers-empty.c", directory);
    snprintf(links, sizeof(links), "%s/clang-headers", directory);
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
    return diagnostics && trees && result && failures == 0 && our_failures == 0 && order_failures == 0 && option_failures == 0 && beside && file_failures == 0 && command;
}

static bool pp_fuzz_checks(const PPFuzzOptions* options);
static bool pp_fuzz_batch(const PPFuzzOptions* options);
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

// __GCC_ASM_FLAG_OUTPUTS__: clang's 1 by default and in clang's feature mode, since the C driver lowers flag outputs
// (#147); -D and -U after it, and a --predefines file's own, as for any predefined macro.
static bool pp_test_asm_flag_predefine(const char* directory)
{
    static const char source[] = "#ifdef __GCC_ASM_FLAG_OUTPUTS__\nvalue __GCC_ASM_FLAG_OUTPUTS__\n#else\nabsent\n#endif\n";
    static const struct
    {
        const char* option;
        bool explicit_predefines;
        const char* expected;
    } cases[] = {
        { NULL, false, "value 1" },
        { "--clang-features", false, "value 1" },
        { "-D__GCC_ASM_FLAG_OUTPUTS__=7", false, "value 7" },
        { "-U__GCC_ASM_FLAG_OUTPUTS__", false, "absent" },
        { "-U__GCC_ASM_FLAG_OUTPUTS__", true, "absent" },
        { NULL, true, "value 3" },
        { "-D__GCC_ASM_FLAG_OUTPUTS__=7", true, "value 7" },
    };
    char path[4200];
    char predefines[4200];
    snprintf(path, sizeof(path), "%s/asm-flag.c", directory);
    snprintf(predefines, sizeof(predefines), "%s/asm-flag-predefines.h", directory);
    bool result = pp_test_write_file(path, source) && pp_test_write_file(predefines, "#define __GCC_ASM_FLAG_OUTPUTS__ 3\n");
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
        {
            char* arguments[3] = { (char*)cases[c].option, path, NULL };
            const int count = cases[c].option != NULL ? 2 : 1;
            if (cases[c].option == NULL)
            {
                arguments[0] = path;
            }
            Arena arena = arena_reserve(1ull << 20);
            PPOptions options;
            const char* input;
            const char* output;
            bool markers;
            bool passed = pp_parse_options(count, arguments, &options, &arena, &input, &output, &markers);
            options.scalar = scalar != 0;
            options.predefines_path = cases[c].explicit_predefines ? predefines : NULL;
            static InternTable table;
            static Preprocessor pp;
            PPSpellings ours = { 0 };
            PPSpellings wanted = { 0 };
            passed = passed && pp_test_run(&pp, &table, &options, path) && pp_stream_spellings(&pp, &ours) &&
                     pp_text_spellings((const uint8_t*)cases[c].expected, strlen(cases[c].expected), pp.split_kinds, pp_test_lex_flags(&pp), &wanted) &&
                     pp_compare_spellings(&pp, &ours, &wanted, "asm flag predefine", "wanted");
            pp_release(&pp);
            intern_release(&table);
            arena_release(&ours.arena);
            arena_release(&wanted.arena);
            arena_release(&arena);
            result &= passed;
        }
    }
    unlink(path);
    unlink(predefines);
    printf("asm flag predefine: %s\n", result ? "as expected" : "WRONG");
    return result;
}

#include "c_preprocessor_feature_test.c"
#else
// The feature fixtures first include this shared metadata; retain that definition order in the other units.
#include "c_builtin_intrinsic_catalog.c"
#endif
#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND

static bool pp_test_implemented_features(const char* directory)
{
    static const char source[] =
        "A __has_builtin(__builtin_expect) __has_constexpr_builtin(__builtin_clz) __has_feature(c_static_assert) __has_extension(c_atomic) "
        "__has_attribute(packed) __has_attribute(__packed__) __has_c_attribute(gnu::packed) __has_warning(\"-Wreserved-identifier\") "
        "__is_identifier(int) __is_target_arch(x86_64)\n"
        "#define CLZ __builtin_clz\n#define __builtin_clz 0\nB __has_constexpr_builtin(CLZ) __has_constexpr_builtin(__builtin_clz)\n"
        "#define OWN_BUILTIN __is_target_arch\nC __has_builtin(OWN_BUILTIN) __has_builtin(__is_target_arch)\n"
        "#define __builtin_expect 0\nD __has_builtin(__builtin_expect) __has_builtin(__builtin_expect_typo)\n"
        "E __has_attribute(packed_typo) __has_c_attribute(other::packed) __has_c_attribute(packed) __has_c_attribute(gnu::__packed__)\n"
        "F __has_attribute(__aligned__) __has_c_attribute(gnu::__aligned__) __has_feature(__c_static_assert__) __has_c_attribute(__fallthrough__)\n"
        "G __has_attribute(cleanup) __has_builtin(__atomic_load) __has_c_attribute(nodiscard)\n"
        "I __has_attribute(__cleanup__) __has_attribute(__constructor__) __has_attribute(__destructor__) __has_attribute(__weak__) __has_attribute(__overloadable__) "
        "__has_c_attribute(gnu::__cleanup__) __has_c_attribute(gnu::__constructor__) __has_c_attribute(gnu::__destructor__) __has_c_attribute(gnu::__weak__) __has_c_attribute(clang::__overloadable__)\n"
        "#define aligned packed_typo\n#define ATTR aligned\nH __has_attribute(aligned) __has_attribute(ATTR)\n"
        // A version, not 1, as clang answers it (pp_implemented_builtin_query).
        "J __has_builtin(__builtin_operator_new)\n"
        "K __has_c_attribute(deprecated) __has_c_attribute(__deprecated__) __has_c_attribute(maybe_unused) __has_c_attribute(__maybe_unused__) __has_c_attribute(__nodiscard__)\n"
        "L __has_c_attribute(deprecated_typo) __has_c_attribute(other::maybe_unused)\n"
        "#if __has_c_attribute(deprecated) == 201904L && __has_c_attribute(maybe_unused) == 202106L && __has_c_attribute(nodiscard) == 202003L\n"
        "diagnostic_attributes_available\n#else\n#error implemented diagnostic attributes unavailable\n#endif\n";
    static const char* const expected[2] =
    {
        "A 1 1 1 1 1 1 1 1 0 1 B 0 1 C 0 1 D 1 0 E 0 0 0 1 F 1 1 1 201910L G 1 1 202003L I 1 1 1 1 1 1 1 1 1 1 H 0 0\nJ 201802L\n"
        "K 201904L 201904L 202106L 202106L 202003L L 0 0 diagnostic_attributes_available\n",
        // The frozen Clang C-attribute oracle has no clang::overloadable entry; source capability is independent.
        "A 1 1 1 1 1 1 1 1 0 1 B 0 1 C 0 1 D 1 0 E 0 0 0 1 F 1 1 1 201910L G 1 1 202003L I 1 1 1 1 1 1 1 1 1 0 H 0 0\nJ 201802L\n"
        "K 201904L 201904L 202106L 202106L 202003L L 0 0 diagnostic_attributes_available\n",
    };
    static const uint32_t standards[] =
    {
        2017,
        2023,
    };
    char path[4200];
    snprintf(path, sizeof(path), "%s/case.c", directory);
    pp_test_write_file(path, source);
    bool result = pp_test_source_feature_matrix();
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
    // This explicit acceptance inventory is independent of the catalog's generation macros. A source entry needs
    // checking and native lowering, never the experimental Tier 1 path. Standalone folding is a separate fact.
    static const struct
    {
        CBuiltinId id;
        const char* name;
        bool fold;
        bool source;
        bool constant;
    } inventory[] =
    {
        { CBUILTIN_BSWAP16, "__builtin_bswap16", true, true, true },
        { CBUILTIN_BSWAP32, "__builtin_bswap32", true, true, true },
        { CBUILTIN_BSWAP64, "__builtin_bswap64", true, true, true },
        { CBUILTIN_CLZ, "__builtin_clz", true, true, true },
        { CBUILTIN_CLZL, "__builtin_clzl", true, true, true },
        { CBUILTIN_CLZLL, "__builtin_clzll", true, true, true },
        { CBUILTIN_CTZ, "__builtin_ctz", true, true, true },
        { CBUILTIN_CTZL, "__builtin_ctzl", true, true, true },
        { CBUILTIN_CTZLL, "__builtin_ctzll", true, true, true },
        { CBUILTIN_POPCOUNT, "__builtin_popcount", true, true, true },
        { CBUILTIN_POPCOUNTL, "__builtin_popcountl", true, true, true },
        { CBUILTIN_POPCOUNTLL, "__builtin_popcountll", true, true, true },
        { CBUILTIN_PARITY, "__builtin_parity", true, true, true },
        { CBUILTIN_PARITYL, "__builtin_parityl", true, true, true },
        { CBUILTIN_PARITYLL, "__builtin_parityll", true, true, true },
        { CBUILTIN_FFS, "__builtin_ffs", true, true, true },
        { CBUILTIN_FFSL, "__builtin_ffsl", true, true, true },
        { CBUILTIN_FFSLL, "__builtin_ffsll", true, true, true },
        { CBUILTIN_ADD_OVERFLOW, "__builtin_add_overflow", true, true, false },
        { CBUILTIN_SUB_OVERFLOW, "__builtin_sub_overflow", true, true, false },
        { CBUILTIN_MUL_OVERFLOW, "__builtin_mul_overflow", true, true, false },
        { CBUILTIN_VA_START, "__builtin_va_start", false, true, false },
        { CBUILTIN_C23_VA_START, "__builtin_c23_va_start", false, true, false },
        { CBUILTIN_VA_ARG, "__builtin_va_arg", false, true, false },
        { CBUILTIN_VA_COPY, "__builtin_va_copy", false, true, false },
        { CBUILTIN_VA_END, "__builtin_va_end", false, true, false },
        { CBUILTIN_OFFSETOF, "__builtin_offsetof", false, true, true },
        { CBUILTIN_EXPECT, "__builtin_expect", false, true, true },
        { CBUILTIN_UNREACHABLE, "__builtin_unreachable", false, true, false },
        { CBUILTIN_TRAP, "__builtin_trap", false, true, false },
        { CBUILTIN_ASSUME_ALIGNED, "__builtin_assume_aligned", false, true, false },
        { CBUILTIN_PREFETCH, "__builtin_prefetch", false, true, false },
        { CBUILTIN_CONSTANT_P, "__builtin_constant_p", false, true, true },
        { CBUILTIN_TYPES_COMPATIBLE_P, "__builtin_types_compatible_p", false, true, true },
        { CBUILTIN_CHOOSE_EXPR, "__builtin_choose_expr", false, true, true },
        { CBUILTIN_OBJECT_SIZE, "__builtin_object_size", false, true, false },
        { CBUILTIN_DYNAMIC_OBJECT_SIZE, "__builtin_dynamic_object_size", false, true, false },
        { CBUILTIN_HUGE_VAL, "__builtin_huge_val", false, true, false },
        { CBUILTIN_HUGE_VALF, "__builtin_huge_valf", false, true, false },
        { CBUILTIN_HUGE_VALL, "__builtin_huge_vall", false, true, false },
        { CBUILTIN_INF, "__builtin_inf", false, true, false },
        { CBUILTIN_INFF, "__builtin_inff", false, true, false },
        { CBUILTIN_INFL, "__builtin_infl", false, true, false },
        { CBUILTIN_NAN, "__builtin_nan", false, true, false },
        { CBUILTIN_NANF, "__builtin_nanf", false, true, false },
        { CBUILTIN_NANL, "__builtin_nanl", false, true, false },
        { CBUILTIN_ISNAN, "__builtin_isnan", false, true, false },
        { CBUILTIN_ISINF, "__builtin_isinf", false, true, false },
        { CBUILTIN_ISFINITE, "__builtin_isfinite", false, true, false },
        { CBUILTIN_SIGNBIT, "__builtin_signbit", false, true, false },
        { CBUILTIN_FPCLASSIFY, "__builtin_fpclassify", false, true, false },
        { CBUILTIN_ISINF_SIGN, "__builtin_isinf_sign", false, true, false },
        { CBUILTIN_FABS, "__builtin_fabs", false, true, false },
        { CBUILTIN_FABSF, "__builtin_fabsf", false, true, false },
        { CBUILTIN_FABSL, "__builtin_fabsl", false, true, false },
        { CBUILTIN_FLT_ROUNDS, "__builtin_flt_rounds", false, true, false },
        { CBUILTIN_CREAL, "__builtin_creal", false, true, false },
        { CBUILTIN_CREALF, "__builtin_crealf", false, true, false },
        { CBUILTIN_CREALL, "__builtin_creall", false, true, false },
        { CBUILTIN_CIMAG, "__builtin_cimag", false, true, false },
        { CBUILTIN_CIMAGF, "__builtin_cimagf", false, true, false },
        { CBUILTIN_CIMAGL, "__builtin_cimagl", false, true, false },
        { CBUILTIN_ISGREATER, "__builtin_isgreater", false, true, false },
        { CBUILTIN_ISGREATEREQUAL, "__builtin_isgreaterequal", false, true, false },
        { CBUILTIN_ISLESS, "__builtin_isless", false, true, false },
        { CBUILTIN_ISLESSEQUAL, "__builtin_islessequal", false, true, false },
        { CBUILTIN_ISLESSGREATER, "__builtin_islessgreater", false, true, false },
        { CBUILTIN_ISUNORDERED, "__builtin_isunordered", false, true, false },
        { CBUILTIN_ISNORMAL, "__builtin_isnormal", false, true, false },
        { CBUILTIN_ISSUBNORMAL, "__builtin_issubnormal", false, true, false },
        { CBUILTIN_ISZERO, "__builtin_iszero", false, true, false },
        { CBUILTIN_ISSIGNALING, "__builtin_issignaling", false, true, false },
        { CBUILTIN_ISFPCLASS, "__builtin_isfpclass", false, true, false },
        { CBUILTIN_SIGNBITF, "__builtin_signbitf", false, true, false },
        { CBUILTIN_SIGNBITL, "__builtin_signbitl", false, true, false },
        { CBUILTIN_CANONICALIZE, "__builtin_canonicalize", false, true, false },
        { CBUILTIN_CANONICALIZEF, "__builtin_canonicalizef", false, true, false },
        { CBUILTIN_CANONICALIZEL, "__builtin_canonicalizel", false, true, false },
        { CBUILTIN_CANONICALIZEF16, "__builtin_canonicalizef16", false, true, false },
        { CBUILTIN_SQRT, "__builtin_sqrt", false, true, false },
        { CBUILTIN_SQRTF, "__builtin_sqrtf", false, true, false },
        { CBUILTIN_COPYSIGN, "__builtin_copysign", false, true, false },
        { CBUILTIN_COPYSIGNF, "__builtin_copysignf", false, true, false },
        { CBUILTIN_CEIL, "__builtin_ceil", false, true, false },
        { CBUILTIN_CEILF, "__builtin_ceilf", false, true, false },
        { CBUILTIN_FLOOR, "__builtin_floor", false, true, false },
        { CBUILTIN_FLOORF, "__builtin_floorf", false, true, false },
        { CBUILTIN_TRUNC, "__builtin_trunc", false, true, false },
        { CBUILTIN_TRUNCF, "__builtin_truncf", false, true, false },
        { CBUILTIN_NEARBYINT, "__builtin_nearbyint", false, true, false },
        { CBUILTIN_NEARBYINTF, "__builtin_nearbyintf", false, true, false },
        { CBUILTIN_RINT, "__builtin_rint", false, true, false },
        { CBUILTIN_RINTF, "__builtin_rintf", false, true, false },
        { CBUILTIN_MEMCPY, "__builtin_memcpy", false, true, false },
        { CBUILTIN_MEMSET, "__builtin_memset", false, true, false },
        { CBUILTIN_MEMMOVE, "__builtin_memmove", false, true, false },
        { CBUILTIN_MEMCMP, "__builtin_memcmp", false, true, true },
        { CBUILTIN_STRLEN, "__builtin_strlen", false, true, true },
        { CBUILTIN_ALLOCA, "__builtin_alloca", false, true, false },
        { CBUILTIN_UMULL_OVERFLOW, "__builtin_umull_overflow", false, true, false },
        { CBUILTIN_SADD_OVERFLOW, "__builtin_sadd_overflow", false, true, false },
        { CBUILTIN_SADDL_OVERFLOW, "__builtin_saddl_overflow", false, true, false },
        { CBUILTIN_SADDLL_OVERFLOW, "__builtin_saddll_overflow", false, true, false },
        { CBUILTIN_SSUB_OVERFLOW, "__builtin_ssub_overflow", false, true, false },
        { CBUILTIN_SSUBL_OVERFLOW, "__builtin_ssubl_overflow", false, true, false },
        { CBUILTIN_SSUBLL_OVERFLOW, "__builtin_ssubll_overflow", false, true, false },
        { CBUILTIN_SMUL_OVERFLOW, "__builtin_smul_overflow", false, true, false },
        { CBUILTIN_SMULL_OVERFLOW, "__builtin_smull_overflow", false, true, false },
        { CBUILTIN_SMULLL_OVERFLOW, "__builtin_smulll_overflow", false, true, false },
        { CBUILTIN_UADD_OVERFLOW, "__builtin_uadd_overflow", false, true, false },
        { CBUILTIN_UADDL_OVERFLOW, "__builtin_uaddl_overflow", false, true, false },
        { CBUILTIN_UADDLL_OVERFLOW, "__builtin_uaddll_overflow", false, true, false },
        { CBUILTIN_USUB_OVERFLOW, "__builtin_usub_overflow", false, true, false },
        { CBUILTIN_USUBL_OVERFLOW, "__builtin_usubl_overflow", false, true, false },
        { CBUILTIN_USUBLL_OVERFLOW, "__builtin_usubll_overflow", false, true, false },
        { CBUILTIN_UMUL_OVERFLOW, "__builtin_umul_overflow", false, true, false },
        { CBUILTIN_UMULLL_OVERFLOW, "__builtin_umulll_overflow", false, true, false },
        { CBUILTIN_BITREVERSE8, "__builtin_bitreverse8", false, true, true },
        { CBUILTIN_BITREVERSE16, "__builtin_bitreverse16", false, true, true },
        { CBUILTIN_BITREVERSE32, "__builtin_bitreverse32", false, true, true },
        { CBUILTIN_BITREVERSE64, "__builtin_bitreverse64", false, true, true },
        { CBUILTIN_BITREVERSEG, "__builtin_bitreverseg", false, true, true },
        { CBUILTIN_BSWAPG, "__builtin_bswapg", false, true, true },
        { CBUILTIN_CLRSB, "__builtin_clrsb", false, true, true },
        { CBUILTIN_CLRSBL, "__builtin_clrsbl", false, true, true },
        { CBUILTIN_CLRSBLL, "__builtin_clrsbll", false, true, true },
        { CBUILTIN_CLZS, "__builtin_clzs", false, true, true },
        { CBUILTIN_CTZS, "__builtin_ctzs", false, true, true },
        { CBUILTIN_POPCOUNTG, "__builtin_popcountg", false, true, true },
        { CBUILTIN_CLZG, "__builtin_clzg", false, true, true },
        { CBUILTIN_CTZG, "__builtin_ctzg", false, true, true },
        { CBUILTIN_STDC_LEADING_ZEROS, "__builtin_stdc_leading_zeros", false, true, true },
        { CBUILTIN_STDC_LEADING_ONES, "__builtin_stdc_leading_ones", false, true, true },
        { CBUILTIN_STDC_TRAILING_ZEROS, "__builtin_stdc_trailing_zeros", false, true, true },
        { CBUILTIN_STDC_TRAILING_ONES, "__builtin_stdc_trailing_ones", false, true, true },
        { CBUILTIN_STDC_FIRST_LEADING_ZERO, "__builtin_stdc_first_leading_zero", false, true, true },
        { CBUILTIN_STDC_FIRST_LEADING_ONE, "__builtin_stdc_first_leading_one", false, true, true },
        { CBUILTIN_STDC_FIRST_TRAILING_ZERO, "__builtin_stdc_first_trailing_zero", false, true, true },
        { CBUILTIN_STDC_FIRST_TRAILING_ONE, "__builtin_stdc_first_trailing_one", false, true, true },
        { CBUILTIN_STDC_COUNT_ZEROS, "__builtin_stdc_count_zeros", false, true, true },
        { CBUILTIN_STDC_COUNT_ONES, "__builtin_stdc_count_ones", false, true, true },
        { CBUILTIN_STDC_HAS_SINGLE_BIT, "__builtin_stdc_has_single_bit", false, true, true },
        { CBUILTIN_STDC_BIT_WIDTH, "__builtin_stdc_bit_width", false, true, true },
        { CBUILTIN_STDC_BIT_FLOOR, "__builtin_stdc_bit_floor", false, true, true },
        { CBUILTIN_STDC_BIT_CEIL, "__builtin_stdc_bit_ceil", false, true, true },
        { CBUILTIN_STDC_ROTATE_LEFT, "__builtin_stdc_rotate_left", false, true, true },
        { CBUILTIN_STDC_ROTATE_RIGHT, "__builtin_stdc_rotate_right", false, true, true },
        { CBUILTIN_ADDCB, "__builtin_addcb", false, true, false },
        { CBUILTIN_ADDCS, "__builtin_addcs", false, true, false },
        { CBUILTIN_ADDC, "__builtin_addc", false, true, false },
        { CBUILTIN_ADDCL, "__builtin_addcl", false, true, false },
        { CBUILTIN_ADDCLL, "__builtin_addcll", false, true, false },
        { CBUILTIN_SUBCB, "__builtin_subcb", false, true, false },
        { CBUILTIN_SUBCS, "__builtin_subcs", false, true, false },
        { CBUILTIN_SUBC, "__builtin_subc", false, true, false },
        { CBUILTIN_SUBCL, "__builtin_subcl", false, true, false },
        { CBUILTIN_SUBCLL, "__builtin_subcll", false, true, false },
        { CBUILTIN_ATOMIC_LOAD, "__atomic_load", false, true, false },
        { CBUILTIN_ATOMIC_LOAD_N, "__atomic_load_n", false, true, false },
        { CBUILTIN_ATOMIC_STORE, "__atomic_store", false, true, false },
        { CBUILTIN_ATOMIC_STORE_N, "__atomic_store_n", false, true, false },
        { CBUILTIN_ATOMIC_EXCHANGE, "__atomic_exchange", false, true, false },
        { CBUILTIN_ATOMIC_EXCHANGE_N, "__atomic_exchange_n", false, true, false },
        { CBUILTIN_ATOMIC_COMPARE_EXCHANGE, "__atomic_compare_exchange", false, true, false },
        { CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N, "__atomic_compare_exchange_n", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_ADD, "__atomic_fetch_add", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_SUB, "__atomic_fetch_sub", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_AND, "__atomic_fetch_and", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_XOR, "__atomic_fetch_xor", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_OR, "__atomic_fetch_or", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_NAND, "__atomic_fetch_nand", false, true, false },
        { CBUILTIN_ATOMIC_ADD_FETCH, "__atomic_add_fetch", false, true, false },
        { CBUILTIN_ATOMIC_SUB_FETCH, "__atomic_sub_fetch", false, true, false },
        { CBUILTIN_ATOMIC_AND_FETCH, "__atomic_and_fetch", false, true, false },
        { CBUILTIN_ATOMIC_XOR_FETCH, "__atomic_xor_fetch", false, true, false },
        { CBUILTIN_ATOMIC_OR_FETCH, "__atomic_or_fetch", false, true, false },
        { CBUILTIN_ATOMIC_NAND_FETCH, "__atomic_nand_fetch", false, true, false },
        { CBUILTIN_ATOMIC_TEST_AND_SET, "__atomic_test_and_set", false, true, false },
        { CBUILTIN_ATOMIC_CLEAR, "__atomic_clear", false, true, false },
        { CBUILTIN_ATOMIC_THREAD_FENCE, "__atomic_thread_fence", false, true, false },
        { CBUILTIN_ATOMIC_SIGNAL_FENCE, "__atomic_signal_fence", false, true, false },
        { CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE, "__atomic_always_lock_free", false, true, true },
        { CBUILTIN_ATOMIC_IS_LOCK_FREE, "__atomic_is_lock_free", false, true, true },
        { CBUILTIN_ATOMIC_FETCH_MIN, "__atomic_fetch_min", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_MAX, "__atomic_fetch_max", false, true, false },
        { CBUILTIN_ATOMIC_MIN_FETCH, "__atomic_min_fetch", false, true, false },
        { CBUILTIN_ATOMIC_MAX_FETCH, "__atomic_max_fetch", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_UINC, "__atomic_fetch_uinc", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_UDEC, "__atomic_fetch_udec", false, true, false },
        { CBUILTIN_C11_ATOMIC_INIT, "__c11_atomic_init", false, true, false },
        { CBUILTIN_C11_ATOMIC_LOAD, "__c11_atomic_load", false, true, false },
        { CBUILTIN_C11_ATOMIC_STORE, "__c11_atomic_store", false, true, false },
        { CBUILTIN_C11_ATOMIC_EXCHANGE, "__c11_atomic_exchange", false, true, false },
        { CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_STRONG, "__c11_atomic_compare_exchange_strong", false, true, false },
        { CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_WEAK, "__c11_atomic_compare_exchange_weak", false, true, false },
        { CBUILTIN_C11_ATOMIC_FETCH_ADD, "__c11_atomic_fetch_add", false, true, false },
        { CBUILTIN_C11_ATOMIC_FETCH_SUB, "__c11_atomic_fetch_sub", false, true, false },
        { CBUILTIN_C11_ATOMIC_FETCH_AND, "__c11_atomic_fetch_and", false, true, false },
        { CBUILTIN_C11_ATOMIC_FETCH_OR, "__c11_atomic_fetch_or", false, true, false },
        { CBUILTIN_C11_ATOMIC_FETCH_XOR, "__c11_atomic_fetch_xor", false, true, false },
        { CBUILTIN_C11_ATOMIC_FETCH_NAND, "__c11_atomic_fetch_nand", false, true, false },
        { CBUILTIN_C11_ATOMIC_FETCH_MIN, "__c11_atomic_fetch_min", false, true, false },
        { CBUILTIN_C11_ATOMIC_FETCH_MAX, "__c11_atomic_fetch_max", false, true, false },
        { CBUILTIN_C11_ATOMIC_THREAD_FENCE, "__c11_atomic_thread_fence", false, true, false },
        { CBUILTIN_C11_ATOMIC_SIGNAL_FENCE, "__c11_atomic_signal_fence", false, true, false },
        { CBUILTIN_C11_ATOMIC_IS_LOCK_FREE, "__c11_atomic_is_lock_free", false, true, true },
        { CBUILTIN_SYNC_FETCH_AND_ADD, "__sync_fetch_and_add", false, true, false },
        { CBUILTIN_SYNC_FETCH_AND_SUB, "__sync_fetch_and_sub", false, true, false },
        { CBUILTIN_SYNC_FETCH_AND_OR, "__sync_fetch_and_or", false, true, false },
        { CBUILTIN_SYNC_FETCH_AND_AND, "__sync_fetch_and_and", false, true, false },
        { CBUILTIN_SYNC_FETCH_AND_XOR, "__sync_fetch_and_xor", false, true, false },
        { CBUILTIN_SYNC_FETCH_AND_NAND, "__sync_fetch_and_nand", false, true, false },
        { CBUILTIN_SYNC_ADD_AND_FETCH, "__sync_add_and_fetch", false, true, false },
        { CBUILTIN_SYNC_SUB_AND_FETCH, "__sync_sub_and_fetch", false, true, false },
        { CBUILTIN_SYNC_OR_AND_FETCH, "__sync_or_and_fetch", false, true, false },
        { CBUILTIN_SYNC_AND_AND_FETCH, "__sync_and_and_fetch", false, true, false },
        { CBUILTIN_SYNC_XOR_AND_FETCH, "__sync_xor_and_fetch", false, true, false },
        { CBUILTIN_SYNC_NAND_AND_FETCH, "__sync_nand_and_fetch", false, true, false },
        { CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP, "__sync_bool_compare_and_swap", false, true, false },
        { CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP, "__sync_val_compare_and_swap", false, true, false },
        { CBUILTIN_SYNC_SYNCHRONIZE, "__sync_synchronize", false, true, false },
        { CBUILTIN_SYNC_LOCK_TEST_AND_SET, "__sync_lock_test_and_set", false, true, false },
        { CBUILTIN_SYNC_LOCK_RELEASE, "__sync_lock_release", false, true, false },
        { CBUILTIN_SYNC_SWAP, "__sync_swap", false, true, false },
        { CBUILTIN_SYNC_FETCH_AND_MIN, "__sync_fetch_and_min", false, true, false },
        { CBUILTIN_SYNC_FETCH_AND_MAX, "__sync_fetch_and_max", false, true, false },
        { CBUILTIN_SYNC_FETCH_AND_UMIN, "__sync_fetch_and_umin", false, true, false },
        { CBUILTIN_SYNC_FETCH_AND_UMAX, "__sync_fetch_and_umax", false, true, false },
        { CBUILTIN_FRAME_ADDRESS, "__builtin_frame_address", false, true, false },
        { CBUILTIN_RETURN_ADDRESS, "__builtin_return_address", false, true, false },
        { CBUILTIN_EXTRACT_RETURN_ADDR, "__builtin_extract_return_addr", false, true, false },
        { CBUILTIN_FROB_RETURN_ADDR, "__builtin_frob_return_addr", false, true, false },
        { CBUILTIN_ELEMENTWISE_ABS, "__builtin_elementwise_abs", false, true, false },
        { CBUILTIN_ELEMENTWISE_ADD_SAT, "__builtin_elementwise_add_sat", false, true, false },
        { CBUILTIN_ELEMENTWISE_BITREVERSE, "__builtin_elementwise_bitreverse", false, true, false },
        { CBUILTIN_ELEMENTWISE_CLZG, "__builtin_elementwise_clzg", false, true, false },
        { CBUILTIN_ELEMENTWISE_FMA, "__builtin_elementwise_fma", false, true, false },
        { CBUILTIN_ELEMENTWISE_FSHL, "__builtin_elementwise_fshl", false, true, false },
        { CBUILTIN_ELEMENTWISE_FSHR, "__builtin_elementwise_fshr", false, true, false },
        { CBUILTIN_ELEMENTWISE_MAX, "__builtin_elementwise_max", false, true, false },
        { CBUILTIN_ELEMENTWISE_MIN, "__builtin_elementwise_min", false, true, false },
        { CBUILTIN_ELEMENTWISE_POPCOUNT, "__builtin_elementwise_popcount", false, true, false },
        { CBUILTIN_ELEMENTWISE_SQRT, "__builtin_elementwise_sqrt", false, true, false },
        { CBUILTIN_ELEMENTWISE_SUB_SAT, "__builtin_elementwise_sub_sat", false, true, false },
        { CBUILTIN_REDUCE_ADD, "__builtin_reduce_add", false, true, false },
        { CBUILTIN_REDUCE_AND, "__builtin_reduce_and", false, true, false },
        { CBUILTIN_REDUCE_MAX, "__builtin_reduce_max", false, true, false },
        { CBUILTIN_REDUCE_MIN, "__builtin_reduce_min", false, true, false },
        { CBUILTIN_REDUCE_MUL, "__builtin_reduce_mul", false, true, false },
        { CBUILTIN_REDUCE_OR, "__builtin_reduce_or", false, true, false },
        { CBUILTIN_SHUFFLEVECTOR, "__builtin_shufflevector", false, true, false },
        { CBUILTIN_CONVERTVECTOR, "__builtin_convertvector", false, true, false },
        { CBUILTIN_NONDETERMINISTIC_VALUE, "__builtin_nondeterministic_value", false, true, false },
        { CBUILTIN_NONTEMPORAL_LOAD, "__builtin_nontemporal_load", false, true, false },
        { CBUILTIN_NONTEMPORAL_STORE, "__builtin_nontemporal_store", false, true, false },
        { CBUILTIN_BIT_CAST, "__builtin_bit_cast", false, true, false },
        { CBUILTIN_ELEMENTWISE_ACOS, "__builtin_elementwise_acos", false, true, false },
        { CBUILTIN_ELEMENTWISE_ASIN, "__builtin_elementwise_asin", false, true, false },
        { CBUILTIN_ELEMENTWISE_ATAN, "__builtin_elementwise_atan", false, true, false },
        { CBUILTIN_ELEMENTWISE_COS, "__builtin_elementwise_cos", false, true, false },
        { CBUILTIN_ELEMENTWISE_COSH, "__builtin_elementwise_cosh", false, true, false },
        { CBUILTIN_ELEMENTWISE_EXP, "__builtin_elementwise_exp", false, true, false },
        { CBUILTIN_ELEMENTWISE_EXP10, "__builtin_elementwise_exp10", false, true, false },
        { CBUILTIN_ELEMENTWISE_EXP2, "__builtin_elementwise_exp2", false, true, false },
        { CBUILTIN_ELEMENTWISE_LOG, "__builtin_elementwise_log", false, true, false },
        { CBUILTIN_ELEMENTWISE_LOG10, "__builtin_elementwise_log10", false, true, false },
        { CBUILTIN_ELEMENTWISE_LOG2, "__builtin_elementwise_log2", false, true, false },
        { CBUILTIN_ELEMENTWISE_SIN, "__builtin_elementwise_sin", false, true, false },
        { CBUILTIN_ELEMENTWISE_SINH, "__builtin_elementwise_sinh", false, true, false },
        { CBUILTIN_ELEMENTWISE_TAN, "__builtin_elementwise_tan", false, true, false },
        { CBUILTIN_ELEMENTWISE_TANH, "__builtin_elementwise_tanh", false, true, false },
        { CBUILTIN_ELEMENTWISE_CEIL, "__builtin_elementwise_ceil", false, true, false },
        { CBUILTIN_ELEMENTWISE_FLOOR, "__builtin_elementwise_floor", false, true, false },
        { CBUILTIN_ELEMENTWISE_TRUNC, "__builtin_elementwise_trunc", false, true, false },
        { CBUILTIN_ELEMENTWISE_RINT, "__builtin_elementwise_rint", false, true, false },
        { CBUILTIN_ELEMENTWISE_NEARBYINT, "__builtin_elementwise_nearbyint", false, true, false },
        { CBUILTIN_ELEMENTWISE_ROUNDEVEN, "__builtin_elementwise_roundeven", false, true, false },
        { CBUILTIN_ELEMENTWISE_ROUND, "__builtin_elementwise_round", false, true, false },
        { CBUILTIN_ELEMENTWISE_CANONICALIZE, "__builtin_elementwise_canonicalize", false, true, false },
        { CBUILTIN_ELEMENTWISE_ATAN2, "__builtin_elementwise_atan2", false, true, false },
        { CBUILTIN_ELEMENTWISE_FMOD, "__builtin_elementwise_fmod", false, true, false },
        { CBUILTIN_ELEMENTWISE_POW, "__builtin_elementwise_pow", false, true, false },
        { CBUILTIN_ELEMENTWISE_COPYSIGN, "__builtin_elementwise_copysign", false, true, false },
        { CBUILTIN_ELEMENTWISE_MAXIMUM, "__builtin_elementwise_maximum", false, true, false },
        { CBUILTIN_ELEMENTWISE_MINIMUM, "__builtin_elementwise_minimum", false, true, false },
        { CBUILTIN_ELEMENTWISE_MAXNUM, "__builtin_elementwise_maxnum", false, true, false },
        { CBUILTIN_ELEMENTWISE_MINNUM, "__builtin_elementwise_minnum", false, true, false },
        { CBUILTIN_ELEMENTWISE_MAXIMUMNUM, "__builtin_elementwise_maximumnum", false, true, false },
        { CBUILTIN_ELEMENTWISE_MINIMUMNUM, "__builtin_elementwise_minimumnum", false, true, false },
        { CBUILTIN_ELEMENTWISE_LDEXP, "__builtin_elementwise_ldexp", false, true, false },
        { CBUILTIN_ELEMENTWISE_CTZG, "__builtin_elementwise_ctzg", false, true, false },
        { CBUILTIN_ELEMENTWISE_CLMUL, "__builtin_elementwise_clmul", false, true, false },
        { CBUILTIN_ELEMENTWISE_PDEP, "__builtin_elementwise_pdep", false, true, false },
        { CBUILTIN_ELEMENTWISE_PEXT, "__builtin_elementwise_pext", false, true, false },
        { CBUILTIN_REDUCE_XOR, "__builtin_reduce_xor", false, true, false },
        { CBUILTIN_REDUCE_MAXIMUM, "__builtin_reduce_maximum", false, true, false },
        { CBUILTIN_REDUCE_MINIMUM, "__builtin_reduce_minimum", false, true, false },
        { CBUILTIN_REDUCE_ASSOC_FADD, "__builtin_reduce_assoc_fadd", false, true, false },
        { CBUILTIN_REDUCE_IN_ORDER_FADD, "__builtin_reduce_in_order_fadd", false, true, false },
        { CBUILTIN_MASKED_LOAD, "__builtin_masked_load", false, true, false },
        { CBUILTIN_MASKED_STORE, "__builtin_masked_store", false, true, false },
        { CBUILTIN_MASKED_EXPAND_LOAD, "__builtin_masked_expand_load", false, true, false },
        { CBUILTIN_MASKED_COMPRESS_STORE, "__builtin_masked_compress_store", false, true, false },
        { CBUILTIN_MASKED_GATHER, "__builtin_masked_gather", false, true, false },
        { CBUILTIN_MASKED_SCATTER, "__builtin_masked_scatter", false, true, false },
        { CBUILTIN_MATRIX_TRANSPOSE, "__builtin_matrix_transpose", false, true, false },
        { CBUILTIN_MATRIX_COLUMN_MAJOR_LOAD, "__builtin_matrix_column_major_load", false, true, false },
        { CBUILTIN_MATRIX_COLUMN_MAJOR_STORE, "__builtin_matrix_column_major_store", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_FMAXIMUM, "__atomic_fetch_fmaximum", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_FMAXIMUM_NUM, "__atomic_fetch_fmaximum_num", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_FMINIMUM, "__atomic_fetch_fminimum", false, true, false },
        { CBUILTIN_ATOMIC_FETCH_FMINIMUM_NUM, "__atomic_fetch_fminimum_num", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_ADD_FETCH, "__scoped_atomic_add_fetch", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_AND_FETCH, "__scoped_atomic_and_fetch", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_COMPARE_EXCHANGE, "__scoped_atomic_compare_exchange", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_COMPARE_EXCHANGE_N, "__scoped_atomic_compare_exchange_n", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_EXCHANGE, "__scoped_atomic_exchange", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_EXCHANGE_N, "__scoped_atomic_exchange_n", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_ADD, "__scoped_atomic_fetch_add", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_AND, "__scoped_atomic_fetch_and", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_FMAXIMUM, "__scoped_atomic_fetch_fmaximum", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_FMAXIMUM_NUM, "__scoped_atomic_fetch_fmaximum_num", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_FMINIMUM, "__scoped_atomic_fetch_fminimum", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_FMINIMUM_NUM, "__scoped_atomic_fetch_fminimum_num", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_MAX, "__scoped_atomic_fetch_max", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_MIN, "__scoped_atomic_fetch_min", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_NAND, "__scoped_atomic_fetch_nand", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_OR, "__scoped_atomic_fetch_or", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_SUB, "__scoped_atomic_fetch_sub", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_UDEC, "__scoped_atomic_fetch_udec", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_UINC, "__scoped_atomic_fetch_uinc", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_FETCH_XOR, "__scoped_atomic_fetch_xor", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_LOAD, "__scoped_atomic_load", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_LOAD_N, "__scoped_atomic_load_n", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_MAX_FETCH, "__scoped_atomic_max_fetch", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_MIN_FETCH, "__scoped_atomic_min_fetch", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_NAND_FETCH, "__scoped_atomic_nand_fetch", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_OR_FETCH, "__scoped_atomic_or_fetch", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_STORE, "__scoped_atomic_store", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_STORE_N, "__scoped_atomic_store_n", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_SUB_FETCH, "__scoped_atomic_sub_fetch", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_THREAD_FENCE, "__scoped_atomic_thread_fence", false, true, false },
        { CBUILTIN_SCOPED_ATOMIC_XOR_FETCH, "__scoped_atomic_xor_fetch", false, true, false },
        { CBUILTIN_HIP_ATOMIC_COMPARE_EXCHANGE_STRONG, "__hip_atomic_compare_exchange_strong", false, true, false },
        { CBUILTIN_HIP_ATOMIC_COMPARE_EXCHANGE_WEAK, "__hip_atomic_compare_exchange_weak", false, true, false },
        { CBUILTIN_HIP_ATOMIC_EXCHANGE, "__hip_atomic_exchange", false, true, false },
        { CBUILTIN_HIP_ATOMIC_FETCH_ADD, "__hip_atomic_fetch_add", false, true, false },
        { CBUILTIN_HIP_ATOMIC_FETCH_AND, "__hip_atomic_fetch_and", false, true, false },
        { CBUILTIN_HIP_ATOMIC_FETCH_MAX, "__hip_atomic_fetch_max", false, true, false },
        { CBUILTIN_HIP_ATOMIC_FETCH_MIN, "__hip_atomic_fetch_min", false, true, false },
        { CBUILTIN_HIP_ATOMIC_FETCH_OR, "__hip_atomic_fetch_or", false, true, false },
        { CBUILTIN_HIP_ATOMIC_FETCH_SUB, "__hip_atomic_fetch_sub", false, true, false },
        { CBUILTIN_HIP_ATOMIC_FETCH_XOR, "__hip_atomic_fetch_xor", false, true, false },
        { CBUILTIN_HIP_ATOMIC_LOAD, "__hip_atomic_load", false, true, false },
        { CBUILTIN_HIP_ATOMIC_STORE, "__hip_atomic_store", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_COMPARE_EXCHANGE_STRONG, "__opencl_atomic_compare_exchange_strong", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_COMPARE_EXCHANGE_WEAK, "__opencl_atomic_compare_exchange_weak", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_EXCHANGE, "__opencl_atomic_exchange", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_FETCH_ADD, "__opencl_atomic_fetch_add", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_FETCH_AND, "__opencl_atomic_fetch_and", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_FETCH_MAX, "__opencl_atomic_fetch_max", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_FETCH_MIN, "__opencl_atomic_fetch_min", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_FETCH_OR, "__opencl_atomic_fetch_or", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_FETCH_SUB, "__opencl_atomic_fetch_sub", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_FETCH_XOR, "__opencl_atomic_fetch_xor", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_INIT, "__opencl_atomic_init", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_LOAD, "__opencl_atomic_load", false, true, false },
        { CBUILTIN_OPENCL_ATOMIC_STORE, "__opencl_atomic_store", false, true, false },
        { CBUILTIN_ALIGN_UP, "__builtin_align_up", false, true, true },
        { CBUILTIN_ALIGN_DOWN, "__builtin_align_down", false, true, true },
        { CBUILTIN_IS_ALIGNED, "__builtin_is_aligned", false, true, true },
        { CBUILTIN_EXPECT_WITH_PROBABILITY, "__builtin_expect_with_probability", false, true, true },
        { CBUILTIN_NANS, "__builtin_nans", false, true, false },
        { CBUILTIN_NANSF, "__builtin_nansf", false, true, false },
        { CBUILTIN_NANSL, "__builtin_nansl", false, true, false },
        { CBUILTIN_NANF16, "__builtin_nanf16", false, true, false },
        { CBUILTIN_NANSF16, "__builtin_nansf16", false, true, false },
        { CBUILTIN_INFF16, "__builtin_inff16", false, true, false },
        { CBUILTIN_HUGE_VALF16, "__builtin_huge_valf16", false, true, false },
        { CBUILTIN_NANF128, "__builtin_nanf128", false, true, false },
        { CBUILTIN_NANSF128, "__builtin_nansf128", false, true, false },
        { CBUILTIN_INFF128, "__builtin_inff128", false, true, false },
        { CBUILTIN_HUGE_VALF128, "__builtin_huge_valf128", false, true, false },
        { CBUILTIN_CLASSIFY_TYPE, "__builtin_classify_type", false, true, true },
        { CBUILTIN_EH_RETURN_DATA_REGNO, "__builtin_eh_return_data_regno", false, true, true },
        { CBUILTIN_DWARF_CFA, "__builtin_dwarf_cfa", false, true, false },
        { CBUILTIN_LAUNDER, "__builtin_launder", false, true, true },
        { CBUILTIN_ALLOCA_UNINITIALIZED, "__builtin_alloca_uninitialized", false, true, false },
        { CBUILTIN_ALLOCA_WITH_ALIGN, "__builtin_alloca_with_align", false, true, false },
        { CBUILTIN_ALLOCA_WITH_ALIGN_UNINITIALIZED, "__builtin_alloca_with_align_uninitialized", false, true, false },
        { CBUILTIN_ALLOW_RUNTIME_CHECK, "__builtin_allow_runtime_check", false, true, false },
        { CBUILTIN_ALLOW_SANITIZE_CHECK, "__builtin_allow_sanitize_check", false, true, false },
        { CBUILTIN_ANNOTATION, "__builtin_annotation", false, true, false },
        { CBUILTIN_ADDRESSOF, "__builtin_addressof", false, true, true },
        { CBUILTIN_FUNCTION_START, "__builtin_function_start", false, true, true },
        { CBUILTIN_COMPLEX, "__builtin_complex", false, true, true },
        { CBUILTIN_ARITHMETIC_FENCE, "__arithmetic_fence", false, true, true },
        { CBUILTIN_XRAY_CUSTOMEVENT, "__xray_customevent", false, true, false },
        { CBUILTIN_XRAY_TYPEDEVENT, "__xray_typedevent", false, true, false },
        { CBUILTIN_WARN_MEMSET_ZERO_LEN, "__warn_memset_zero_len", false, true, false },
        { CBUILTIN_TRIVIALLY_RELOCATE, "__builtin_trivially_relocate", false, true, false },
        { CBUILTIN_OPERATOR_NEW, "__builtin_operator_new", false, true, true },
        { CBUILTIN_OPERATOR_DELETE, "__builtin_operator_delete", false, true, true },
        { CBUILTIN_PTRAUTH_AUTH, "__builtin_ptrauth_auth", false, true, false },
        { CBUILTIN_PTRAUTH_AUTH_AND_RESIGN, "__builtin_ptrauth_auth_and_resign", false, true, false },
        { CBUILTIN_PTRAUTH_AUTH_LOAD_RELATIVE_AND_SIGN, "__builtin_ptrauth_auth_load_relative_and_sign", false, true, false },
        { CBUILTIN_PTRAUTH_AUTH_WITH_PC_AND_RESIGN, "__builtin_ptrauth_auth_with_pc_and_resign", false, true, false },
        { CBUILTIN_PTRAUTH_BLEND_DISCRIMINATOR, "__builtin_ptrauth_blend_discriminator", false, true, false },
        { CBUILTIN_PTRAUTH_SIGN_CONSTANT, "__builtin_ptrauth_sign_constant", false, true, true },
        { CBUILTIN_PTRAUTH_SIGN_GENERIC_DATA, "__builtin_ptrauth_sign_generic_data", false, true, false },
        { CBUILTIN_PTRAUTH_SIGN_UNAUTHENTICATED, "__builtin_ptrauth_sign_unauthenticated", false, true, false },
        { CBUILTIN_PTRAUTH_STRING_DISCRIMINATOR, "__builtin_ptrauth_string_discriminator", false, true, true },
        { CBUILTIN_PTRAUTH_STRIP, "__builtin_ptrauth_strip", false, true, false },
        { CBUILTIN_CPU_INIT, "__builtin_cpu_init", false, true, false },
        { CBUILTIN_CPU_IS, "__builtin_cpu_is", false, true, false },
        { CBUILTIN_CPU_SUPPORTS, "__builtin_cpu_supports", false, true, false },
        { CBUILTIN_PRESERVE_ACCESS_INDEX, "__builtin_preserve_access_index", false, true, false },
        { CBUILTIN_HLSL_RESOURCE_GETPOINTER_TYPED, "__builtin_hlsl_resource_getpointer_typed", false, true, false },
        { CBUILTIN_CLEAR_PADDING, "__builtin_clear_padding", false, true, false },
        { CBUILTIN_INVOKE, "__builtin_invoke", false, true, false },
        { CBUILTIN_EH_RETURN, "__builtin_eh_return", false, true, false },
        { CBUILTIN_CFSTRING_MAKE_CONSTANT_STRING, "__builtin___CFStringMakeConstantString", false, true, false },
        { CBUILTIN_NSSTRING_MAKE_CONSTANT_STRING, "__builtin___NSStringMakeConstantString", false, true, false },
        { CBUILTIN_MS_VA_START, "__builtin_ms_va_start", false, true, false },
        { CBUILTIN_MS_VA_END, "__builtin_ms_va_end", false, true, false },
        { CBUILTIN_MS_VA_COPY, "__builtin_ms_va_copy", false, true, false },
        { CBUILTIN_ZOS_VA_START, "__builtin_zos_va_start", false, true, false },
        { CBUILTIN_ZOS_VA_END, "__builtin_zos_va_end", false, true, false },
        { CBUILTIN_ZOS_VA_COPY, "__builtin_zos_va_copy", false, true, false },
        { CBUILTIN_INFER_ALLOC_TOKEN, "__builtin_infer_alloc_token", false, true, true },
        { CBUILTIN_CALL_WITH_STATIC_CHAIN, "__builtin_call_with_static_chain", false, true, false },
        { CBUILTIN_OS_LOG_FORMAT_BUFFER_SIZE, "__builtin_os_log_format_buffer_size", false, true, true },
        { CBUILTIN_OS_LOG_FORMAT, "__builtin_os_log_format", false, true, false },
        { CBUILTIN_DUMP_STRUCT, "__builtin_dump_struct", false, true, false },
        { CBUILTIN_AVAILABLE, "__builtin_available", false, true, false },
        { CBUILTIN_COUNTED_BY_REF, "__builtin_counted_by_ref", false, true, false },
    };
    result &= BUSTER_ARRAY_LENGTH(inventory) == BUSTER_ARRAY_LENGTH(cbuiltin_catalog);
    const uint32_t compile_capabilities = BUSTER_CBUILTIN_CAP_SOURCE | BUSTER_CBUILTIN_CAP_CONSTEXPR |
                                          BUSTER_CBUILTIN_CAP_CHECKER | BUSTER_CBUILTIN_CAP_TIER0 | BUSTER_CBUILTIN_CAP_TIER1;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cbuiltin_catalog); i += 1)
    {
        const uint32_t capabilities = cbuiltin_catalog[i].capabilities;
        result &= (capabilities & BUSTER_CBUILTIN_CAP_TIER1) == 0;
        result &= (capabilities & BUSTER_CBUILTIN_CAP_SOURCE) == 0 || (capabilities & BUSTER_CBUILTIN_SOURCE) == BUSTER_CBUILTIN_SOURCE;
        result &= (capabilities & BUSTER_CBUILTIN_CAP_CONSTEXPR) == 0 || (capabilities & BUSTER_CBUILTIN_SOURCE) == BUSTER_CBUILTIN_SOURCE;
        // cbuiltin_resolve refuses a name that doesn't start with "__" before it looks at the catalog, and cbuiltin_entry
        // one that starts with "__builtin_ia32_", the intrinsic catalog's.
        result &= cbuiltin_catalog[i].length >= 2 && cbuiltin_catalog[i].name[0] == '_' && cbuiltin_catalog[i].name[1] == '_';
        result &= !(cbuiltin_catalog[i].length > 15 && memcmp(cbuiltin_catalog[i].name, "__builtin_ia32_", 15) == 0);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(inventory); i += 1)
    {
        const char* name = inventory[i].name;
        const uint32_t length = (uint32_t)strlen(name);
        result &= cbuiltin_resolve(name, length) == inventory[i].id;
        result &= cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_FOLD) == inventory[i].fold;
        result &= cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_SOURCE) == inventory[i].source;
        result &= cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR) == inventory[i].constant;
        result &= !cbuiltin_has_capability((const uint8_t*)name, length, compile_capabilities);
        result &= cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_FOLD | BUSTER_CBUILTIN_CAP_SOURCE) == (inventory[i].fold && inventory[i].source);
        result &= !cbuiltin_has_capability((const uint8_t*)name, length, 0);
        result &= !cbuiltin_has_capability((const uint8_t*)name, length, 0x80);
        for (uint32_t previous = 0; previous < i; previous += 1)
        {
            result &= inventory[previous].id != inventory[i].id && strcmp(inventory[previous].name, name) != 0;
        }
        for (uint32_t mode = 0; mode < BUSTER_ARRAY_LENGTH(standards); mode += 1)
        {
            const PPOptions defaults = { .language = { .standard = standards[mode], .gnu = true } };
            // clang makes __builtin_c23_va_start a builtin in C23 alone (#148).
            const bool language = inventory[i].id != CBUILTIN_C23_VA_START || standards[mode] >= 2023;
            // operator_new's and operator_delete's answer is a version, as clang's (pp_implemented_builtin_query).
            const bool version = inventory[i].id == CBUILTIN_OPERATOR_NEW || inventory[i].id == CBUILTIN_OPERATOR_DELETE;
            const uint32_t answer = inventory[i].source && language ? (version ? 201802u : 1u) : 0u;
            result &= pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)name, length) == answer;
            result &= pp_feature(&defaults, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, (const uint8_t*)name, length) == (inventory[i].constant && language);
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
    // clang's sized legacy forms (#148): each of the 17 families' names with _1, _2, _4, _8 and _16 is its family's builtin,
    // with its capabilities; another suffix, and a family clang's table has no sized forms of, none.
    static const struct
    {
        CBuiltinId id;
        const char* name;
    } sized[] = {
        { CBUILTIN_SYNC_FETCH_AND_ADD, "__sync_fetch_and_add" },
        { CBUILTIN_SYNC_FETCH_AND_SUB, "__sync_fetch_and_sub" },
        { CBUILTIN_SYNC_FETCH_AND_OR, "__sync_fetch_and_or" },
        { CBUILTIN_SYNC_FETCH_AND_AND, "__sync_fetch_and_and" },
        { CBUILTIN_SYNC_FETCH_AND_XOR, "__sync_fetch_and_xor" },
        { CBUILTIN_SYNC_FETCH_AND_NAND, "__sync_fetch_and_nand" },
        { CBUILTIN_SYNC_ADD_AND_FETCH, "__sync_add_and_fetch" },
        { CBUILTIN_SYNC_SUB_AND_FETCH, "__sync_sub_and_fetch" },
        { CBUILTIN_SYNC_OR_AND_FETCH, "__sync_or_and_fetch" },
        { CBUILTIN_SYNC_AND_AND_FETCH, "__sync_and_and_fetch" },
        { CBUILTIN_SYNC_XOR_AND_FETCH, "__sync_xor_and_fetch" },
        { CBUILTIN_SYNC_NAND_AND_FETCH, "__sync_nand_and_fetch" },
        { CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP, "__sync_bool_compare_and_swap" },
        { CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP, "__sync_val_compare_and_swap" },
        { CBUILTIN_SYNC_LOCK_TEST_AND_SET, "__sync_lock_test_and_set" },
        { CBUILTIN_SYNC_LOCK_RELEASE, "__sync_lock_release" },
        { CBUILTIN_SYNC_SWAP, "__sync_swap" },
    };
    static const char* const suffixes[] = { "_1", "_2", "_4", "_8", "_16" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sized); i += 1)
    {
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(suffixes); s += 1)
        {
            char name[64];
            const uint32_t length = (uint32_t)snprintf(name, sizeof(name), "%s%s", sized[i].name, suffixes[s]);
            result &= cbuiltin_resolve(name, length) == sized[i].id;
            result &= cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_SOURCE);
            result &= !cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR);
            result &= pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)name, length) == 1;
            result &= pp_feature(&defaults, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, (const uint8_t*)name, length) == 0;
        }
    }
    static const char* const unsized[] = {
        "__sync_fetch_and_add_3",
        "__sync_fetch_and_add_32",
        "__sync_fetch_and_add_",
        "__sync_fetch_and_add_04",
        "__sync_synchronize_4",
        "__sync_fetch_and_max_4",
        "__sync_fetch_and_umin_8",
        "__sync_swap_1x",
        "__sync_swa_1",
        "__atomic_fetch_add_4",
        "__atomic_exchange_n_8",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unsized); i += 1)
    {
        const uint32_t length = (uint32_t)strlen(unsized[i]);
        result &= cbuiltin_resolve(unsized[i], length) == CBUILTIN_NONE;
        result &= !cbuiltin_has_capability((const uint8_t*)unsized[i], length, BUSTER_CBUILTIN_CAP_SOURCE);
        result &= pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)unsized[i], length) == 0;
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
                result &= cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_SOURCE) == inventory[i].source;
                result &= cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR) == inventory[i].constant;
                // In gnu17, where __builtin_c23_va_start is no builtin (#148).
                const bool language = inventory[i].id != CBUILTIN_C23_VA_START;
                const bool version = inventory[i].id == CBUILTIN_OPERATOR_NEW || inventory[i].id == CBUILTIN_OPERATOR_DELETE;
                const uint32_t answer = inventory[i].source && language ? (version ? 201802u : 1u) : 0u;
                result &= pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, name, length) == answer;
                result &= pp_feature(&defaults, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, name, length) == (inventory[i].constant && language);
                name[0] = '!';
                result &= cbuiltin_resolve((const char*)name, length) == CBUILTIN_NONE;
                result &= !cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_FOLD);
            }
            // The sized forms' suffixes are read from the name's end, and their families' names before them.
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sized); i += 1)
            {
                for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(suffixes); s += 1)
                {
                    char spelled[64];
                    const uint32_t length = (uint32_t)snprintf(spelled, sizeof(spelled), "%s%s", sized[i].name, suffixes[s]);
                    uint8_t* name = end - length;
                    memcpy(name, spelled, length);
                    result &= cbuiltin_resolve((const char*)name, length) == sized[i].id;
                    result &= cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_SOURCE);
                    // The name less its last byte, at the page: _1 of _16 a sized form too, an underscore alone none.
                    memcpy(end - (length - 1), spelled, length - 1);
                    const CBuiltinId cut = cbuiltin_resolve((const char*)end - (length - 1), length - 1);
                    result &= cut == (s == 4 ? sized[i].id : CBUILTIN_NONE);
                }
            }
        }
        munmap(mapping, (size_t)page_size * 2);
    }
    printf("feature answers and %zu catalog names: %s (source acceptance by default, clang's independent oracle mode)\n",
           BUSTER_ARRAY_LENGTH(inventory), result ? "as expected" : "WRONG");
    return result;
}

// c_predefines_test.c's checks and generator: main.c includes it after this file.
static bool predefines_test_tables(const char* directory);
static bool predefines_test_options(const char* directory, uint64_t seed, uint32_t random, uint32_t slice, uint32_t slices);
static bool predefines_test_dump(const char* directory);
static bool predefines_test_attributes(const char* directory);
static bool predefines_test_print_tables(const char* directory);
static bool predefines_test_target(void);
static bool predefines_test_refused(void);

#endif
#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND || BUSTER_UNIT == BUSTER_UNIT_CHECKER
#include "c_preprocessor_history_test.c"
#endif
#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND
#include "c_preprocessor_print_test.c"
#include "c_preprocessor_system_warning_test.c"
#include "c_preprocessor_warning_possible_test.c"
#include "c_preprocessor_directives_test.c"
#include "c_preprocessor_order_test.c"
#include "c_preprocessor_batch_test.c"
#include "c_preprocessor_semantic_test.c"
#include "c_preprocessor_windows_test.c"
#include "c_preprocessor_arguments_test.c"

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
        passed = passed && pp_same_streams(runs, runs + 1, fixtures[fixture].name) && strcmp(diagnostics[0], diagnostics[1]) == 0 &&
                 pp_kernel_digest_check(runs, "pragma-origin", fixtures[fixture].name);
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

// The in-memory fuzz boundary must refuse existing absolute files too, for
// includes, embeds and both existence queries. The enabled control proves that
// each fixture really can read the file without the option.
static bool pp_test_no_external_files(const char* directory)
{
    char path[PATH_MAX];
    const int written = snprintf(path, sizeof(path), "%s/fuzz-external.h", directory);
    bool result = written > 0 && written < (int)sizeof(path);
    FILE* file = result ? fopen(path, "w") : 0;
    result = result && file != 0;
    if (file != 0)
    {
        result = fputs("42\n", file) >= 0 && result;
        result = fclose(file) == 0 && result;
    }
    static const char* const formats[] = {
        "#include \"%s\"\n",
        "#embed \"%s\"\n",
        "#if !__has_include(\"%s\")\n#error missing\n#endif\n",
        "#if !__has_embed(\"%s\")\n#error missing\n#endif\n",
    };
    for (uint32_t kind = 0; result && kind < BUSTER_ARRAY_LENGTH(formats); kind += 1)
    {
        for (uint32_t blocked = 0; result && blocked < 2; blocked += 1)
        {
            char source[PATH_MAX + 128];
            const int length = snprintf(source, sizeof(source), formats[kind], path);
            static InternTable table;
            static Preprocessor pp;
            const PPOptions options = {
                .language = { .standard = 2023 },
                .no_standard_includes = true,
                .no_external_files = blocked != 0,
                .quiet = true,
                .timing = true,
            };
            const bool ready = length > 0 && length < (int)sizeof(source) && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
            const bool accepted = ready && pp_run_source(&pp, "memory-fuzz.c", (const uint8_t*)source, (uint32_t)length);
            result = ready && accepted == (blocked == 0);
            if (blocked != 0)
            {
                result = result && pp.probes.opens == 0 && pp.probes.fstats == 0 && pp.statistics.embed_stats == 0;
            }
            pp_release(&pp);
            intern_release(&table);
        }
    }
    unlink(path);
    if (!result)
    {
        fprintf(stderr, "in-memory include/embed boundary failed\n");
    }
    return result;
}

// --part K/N's deal of the checks (pp_test_sections) to N parts, each a command of its own that the parallel runner packs
// beside the others (#465's test-checker parts): each check weighs about what it takes, in tenths of a second of one CPU
// (on a 4-CPU Xeon VM, the cases' and the fuzz batch's workers counted whole), and the deal gives each check, the
// heaviest first (of two that weigh the same, the earlier), to the part that weighs the least so far (of those, the
// first), the deal the runner's shards take too (test_deal.h). The parts together run every check once; the weights only
// balance them, from a run on one CPU without the oracle cache (a check under 0.1 s weighs 1). A check's output and
// failures are its part's.
#define BUSTER_PP_TEST_MAX_SECTIONS 128
// The option sets' check against clang -dM (predefines_test_options), the heaviest, runs as this many checks, each a
// slice of the sets.
#define BUSTER_PP_TEST_PREDEFINES_SLICES 4

typedef struct PPTestDeal PPTestDeal;
struct PPTestDeal
{
    // This part, from 1, of `parts`.
    uint32_t part;
    uint32_t parts;
    // The first pass weighs the checks and runs none; the second runs those dealt to the part.
    bool collecting;
    // The checks met so far in this pass, and how many of them the part has.
    uint32_t count;
    uint32_t dealt;
    // The checks the first pass met, and whether the second met another sequence: a check whose weight isn't the one the
    // first pass recorded at its place, or one past them. Each part checks it, so that a check whose guard came to
    // depend on what another check does can't move between the parts unseen, the parts' union no longer every check
    // once.
    uint32_t collected;
    bool shifted;
    uint64_t weights[BUSTER_PP_TEST_MAX_SECTIONS];
    // Each check's part, from 0.
    uint64_t assigned[BUSTER_PP_TEST_MAX_SECTIONS];
};

// Whether the next check runs: never while the deal weighs them (it records the check's weight), and then if it is
// this part's.
static bool pp_test_section(PPTestDeal* deal, uint32_t weight)
{
    const uint32_t index = deal->count;
    bool runs = false;
    if (deal->collecting && index < BUSTER_PP_TEST_MAX_SECTIONS)
    {
        deal->weights[index] = weight;
    }
    else if (!deal->collecting)
    {
        deal->shifted |= index >= deal->collected || deal->weights[index] != weight;
        runs = !deal->shifted && deal->assigned[index] + 1 == deal->part;
    }
    deal->count += 1;
    return runs;
}

// A check `call` (a bool) of `weight` tenths of a second: its result if the part runs it, and true if not.
#define PP_TEST_SECTION(deal, weight, call) (pp_test_section((deal), (weight)) ? (call) : true)

// Deals the weighed checks to the parts as the runner deals its shards (test_deal: heaviest first, each to the lightest
// part so far). False if there are more checks than the deal can hold.
static bool pp_test_deal(PPTestDeal* deal)
{
    const bool valid = deal->count <= BUSTER_PP_TEST_MAX_SECTIONS && deal->parts >= 1;
    uint64_t order[BUSTER_PP_TEST_MAX_SECTIONS];
    uint64_t loads[BUSTER_PP_TEST_MAX_SECTIONS];
    const uint32_t count = valid ? deal->count : 0;
    test_deal(deal->weights, count, deal->parts, order, loads, deal->assigned);
    deal->collected = count;
    deal->dealt = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        deal->dealt += deal->assigned[i] + 1 == deal->part;
    }
    if (!valid)
    {
        printf("test-preprocessor: %u checks, more than the %u a deal holds\n", deal->count, BUSTER_PP_TEST_MAX_SECTIONS);
    }
    return valid;
}

// The cases in parts that run at once (pp_test_children), each in a directory of its own, their output in the cases'
// order, then the system levels they found.
static bool pp_test_cases_section(const char* directory, bool with_clang, bool keep)
{
    const PPTestChildren children = {
        .work = PP_TEST_CHILD_CASES,
        .parts = BUSTER_PP_TEST_CASE_PARTS,
        .directory = directory,
        .with_clang = with_clang,
        .keep = keep,
    };
    const PPTestChildCounts cases = pp_test_children(&children);
    pp_test_system_units += cases.system_units;
    pp_test_system_inexact += cases.system_inexact;
    pp_test_system_tokens += cases.system_tokens;
    pp_test_system_made += cases.system_made;
    snprintf(pp_test_system_inexact_first, sizeof(pp_test_system_inexact_first), "%s", cases.system_inexact_first);
    const uint32_t failures = cases.failures + !cases.result;
    printf("cases: %u of %lu fail (#2053: %s/preprocessor/cases)\n", failures, (unsigned long)pp_test_case_count, pp_test_fixture_root);
    printf("system levels: %llu units by their runs and their history alike, %llu whose runs can't say%s%s%s; %llu tokens at a system level, %llu made "
           "by macros\n",
           (unsigned long long)pp_test_system_units, (unsigned long long)pp_test_system_inexact, pp_test_system_inexact != 0 ? " (the first: " : "",
           pp_test_system_inexact_first, pp_test_system_inexact != 0 ? ")" : "", (unsigned long long)pp_test_system_tokens,
           (unsigned long long)pp_test_system_made);
    return failures == 0;
}

// One mode of the fuzz batch, without the harness's own checks (pp_fuzz_checks), which a section of their own runs.
static bool pp_test_fuzz_mode(const PPFuzzOptions* fuzz, uint32_t mode)
{
    PPFuzzOptions one = *fuzz;
    one.modes[0] = mode == 0;
    one.modes[1] = mode == 1;
    fflush(stdout);
    return pp_fuzz_batch(&one);
}

// test-preprocessor's checks without files, in their order, each through the deal (PP_TEST_SECTION). Returns whether
// every one that ran passed.
static bool pp_test_sections(PPTestDeal* deal, const char* directory, bool with_clang, bool keep, const PPFuzzOptions* fuzz, uint64_t id_order, uint64_t count)
{
    bool passed = true;
    passed &= PP_TEST_SECTION(deal, 103, pp_test_cases_section(directory, with_clang, keep));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_case_retention(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_no_external_files(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_clang_reference());
    passed &= PP_TEST_SECTION(deal, 1, test_process_test() == 0);
    passed &= PP_TEST_SECTION(deal, 1, pp_test_system_case_levels(directory));
    passed &= PP_TEST_SECTION(deal, 6, pp_test_macro_batches(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 8, pp_test_many_parameters(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 2, pp_test_semantic_pragmas(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_timestamps(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_implemented_features(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_asm_flag_predefine(directory));
    passed &= PP_TEST_SECTION(deal, 1, predefines_test_target());
    passed &= PP_TEST_SECTION(deal, 1, predefines_test_refused());
    passed &= PP_TEST_SECTION(deal, 11, pp_test_history(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_print_offsets());
    passed &= PP_TEST_SECTION(deal, 1, pp_test_print_spellings());
    passed &= PP_TEST_SECTION(deal, 1, pp_test_print_unspliced());
    passed &= PP_TEST_SECTION(deal, 1, pp_test_splice_possible());
    passed &= PP_TEST_SECTION(deal, 1, pp_test_print_batches(directory));
    passed &= PP_TEST_SECTION(deal, 3, pp_test_print_spelling_paths(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_print_lines(directory));
    passed &= PP_TEST_SECTION(deal, 6, pp_test_system_warnings(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_warning_possible(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_pragma_origins(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_embed_text(fuzz->seed));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_embed_lookups(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_lookup_accounting(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_directory_reads(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_directory_listings(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_component_keys());
    passed &= PP_TEST_SECTION(deal, 1, pp_test_directory_asks(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_listing_descriptors(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_final_copy_accounting());
    passed &= PP_TEST_SECTION(deal, 1, pp_test_trailing_splices());
    passed &= PP_TEST_SECTION(deal, 19, pp_test_windows(fuzz->seed));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_line_ends());
    passed &= PP_TEST_SECTION(deal, 5, pp_test_window_units(directory, fuzz->seed));
    passed &= PP_TEST_SECTION(deal, 28, pp_test_directives(directory, with_clang, fuzz->seed, count));
    passed &= PP_TEST_SECTION(deal, 130, pp_test_id_order(directory, id_order));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_assembly_printing(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_raw_query_views(directory));
    passed &= PP_TEST_SECTION(deal, 5, pp_test_raw_query_capacity(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_raw_recovery_failures());
    passed &= PP_TEST_SECTION(deal, 1, pp_test_named_ucns(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_named_ucn_scaling(directory));
    passed &= PP_TEST_SECTION(deal, 2, pp_test_named_ucn_boundaries(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_prelude_failures());
    passed &= PP_TEST_SECTION(deal, 1, pp_test_raw_forced_include_contexts(directory));
    passed &= PP_TEST_SECTION(deal, 1, pp_test_direct_argument_failures());
    passed &= PP_TEST_SECTION(deal, 2, pp_test_nested_arguments());
    passed &= PP_TEST_SECTION(deal, 1, pp_test_argument_links());
    passed &= PP_TEST_SECTION(deal, 4, pp_test_argument_slice_semantics(directory, with_clang));
    passed &= PP_TEST_SECTION(deal, 26, pp_test_option_refusals(directory, with_clang));
    if (with_clang)
    {
        passed &= PP_TEST_SECTION(deal, 9, pp_test_predefines(directory));
        passed &= PP_TEST_SECTION(deal, 221, pp_test_headers(directory));
        // predefines_test_run's checks (c_predefines_test.c), each a check of its own.
        passed &= PP_TEST_SECTION(deal, 145, predefines_test_tables(directory));
        for (uint32_t slice = 0; slice < BUSTER_PP_TEST_PREDEFINES_SLICES; slice += 1)
        {
            passed &= PP_TEST_SECTION(deal, 102, predefines_test_options(directory, fuzz->seed, 400, slice, BUSTER_PP_TEST_PREDEFINES_SLICES));
        }
        passed &= PP_TEST_SECTION(deal, 1, predefines_test_dump(directory));
        passed &= PP_TEST_SECTION(deal, 4, predefines_test_attributes(directory));
        passed &= PP_TEST_SECTION(deal, 35, pp_test_keywords(directory));
        passed &= PP_TEST_SECTION(deal, 10, pp_test_features(directory));
        passed &= PP_TEST_SECTION(deal, 49, pp_test_option_features(directory));
        passed &= PP_TEST_SECTION(deal, 1, pp_test_builtin_targets(directory));
        passed &= PP_TEST_SECTION(deal, 10, pp_test_builtin_release(directory));
    }
    if (fuzz->count != 0)
    {
        // The fuzz batch (c_preprocessor_fuzz.c): the harness's own checks, then each mode it runs.
        passed &= PP_TEST_SECTION(deal, 22, pp_fuzz_checks(fuzz));
        if (fuzz->modes[0])
        {
            passed &= PP_TEST_SECTION(deal, 115, pp_test_fuzz_mode(fuzz, 0));
        }
        if (fuzz->modes[1])
        {
            passed &= PP_TEST_SECTION(deal, 115, pp_test_fuzz_mode(fuzz, 1));
        }
    }
    return passed;
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
    // --part K/N: the checks without files that the deal gives part K of N (pp_test_sections).
    uint32_t part = 1;
    uint32_t parts = 1;
    bool parted = false;
    // --clang-server PATH: the oracle clang's runs through tools/clang-server (oracle_serve, oracle_cache.c), with
    // --differential each also run as before and compared.
    const char* clang_server = 0;
    bool differential = false;
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
        else if (strcmp(argument, "--clang-server") == 0)
        {
            clang_server = next;
            valid = valid && next != 0;
            i += 1;
        }
        else if (strcmp(argument, "--differential") == 0)
        {
            differential = true;
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
        else if (strcmp(argument, "--fixtures") == 0)
        {
            valid = valid && next != 0 && next[0] != 0;
            pp_test_fixture_root = next != 0 ? next : pp_test_fixture_root;
            i += 1;
        }
        else if (strcmp(argument, "--part") == 0)
        {
            // K/N: decimal K from 1 to N, a '/', and decimal N up to the deal's checks, digits alone on either side.
            const char* slash = next != 0 ? strchr(next, '/') : 0;
            char first_text[24] = { 0 };
            const size_t first_length = slash != 0 ? (size_t)(slash - next) : 0;
            // Without a value or its '/', there is nothing to copy, and `next` may be null.
            if (slash != 0 && first_length < sizeof(first_text))
            {
                memcpy(first_text, next, first_length);
            }
            uint64_t first = 0;
            uint64_t second = 0;
            valid = valid && !parted && slash != 0 && first_length < sizeof(first_text) && pp_test_number(first_text, &first) &&
                    pp_test_number(slash + 1, &second) && first >= 1 && first <= second && second <= BUSTER_PP_TEST_MAX_SECTIONS;
            part = (uint32_t)first;
            parts = (uint32_t)second;
            parted = true;
            i += 1;
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
    // --part deals the checks without files alone.
    valid = valid && (!parted || (file_count == 0 && !tables && !generate_targets && !print_case));
    if (!valid || fuzz.jobs == 0 || fuzz.jobs > BUSTER_PP_FUZZ_MAX_JOBS)
    {
        fprintf(stderr, "usage: test-preprocessor [--clang] [--clang-server PATH [--differential]] [--keep] [--id-order N] [--fuzz N] [--first N] [--seed N] [--std c17|c23] [--jobs N] [--part K/N] [--print-case N] [--fixtures DIRECTORY] [options] [file...]\n");
    }
    if (valid && clang_server != 0)
    {
        // Before the checks and the processes they fork: clang's oracle runs (by the name "clang", which
        // pp_test_clang_process and the predefines' jobs run it by) go to the server.
        oracle_serve(clang_server, "clang", differential);
    }
    char directory[4096];
    const char* temporary = oracle_temporary_directory();
    snprintf(directory, sizeof(directory), "%s/buster-pp-test-XXXXXX", temporary);
    const bool created = valid && !print_case && fuzz.jobs != 0 && fuzz.jobs <= BUSTER_PP_FUZZ_MAX_JOBS && passed != 0 && clang_passed != 0 && files != 0 && mkdtemp(directory) != 0;
    // The oracle cache, set up before the parts fork, so that their counts reach its report.
    OracleCache* oracle = oracle_cache();
    bool passed_all = created;
    if (passed_all && generate_targets)
    {
        // c_predefines.c's generated tables, from clang (c_predefines_test.c).
        passed_all = predefines_test_print_tables(directory);
    }
    else if (passed_all && file_count == 0 && !pp_test_cases_load())
    {
        // The built-in cases are runtime data (#2053): without them, no check runs.
        passed_all = false;
    }
    else if (passed_all && file_count == 0)
    {
        // The checks, all of them or --part's, in their order (pp_test_sections): first a pass that only weighs them,
        // then the deal, then the pass that runs this part's.
        PPTestDeal deal = {
            .part = part,
            .parts = parts,
            .collecting = true,
        };
        // The library-builtin table is pinned to BUSTER_CLANG_REFERENCE_VERSION's official build: a different build is
        // reported with both, and pp_test_builtin_targets then skips only the plain names that build is known to
        // classify differently.
        if (with_clang)
        {
            clang_reference_report(pp_test_clang_path, pp_test_clang_version, sizeof(pp_test_clang_version));
        }
        pp_test_sections(&deal, directory, with_clang, keep, &fuzz, id_order, count);
        passed_all = pp_test_deal(&deal);
        if (parts > 1)
        {
            printf("test-preprocessor: part %u of %u: %u of %u checks\n", part, parts, deal.dealt, deal.count);
        }
        fflush(stdout);
        deal.collecting = false;
        deal.count = 0;
        passed_all = passed_all && pp_test_sections(&deal, directory, with_clang, keep, &fuzz, id_order, count);
        // The checks the parts run are the deal's only if this pass met the first one's, check for check.
        if (deal.shifted || deal.count != deal.collected)
        {
            printf("test-preprocessor: the checks run aren't the ones dealt: %u of them against %u weighed%s\n", deal.count, deal.collected,
                   deal.shifted ? ", from one whose weight or place differs" : "");
            passed_all = false;
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
    oracle_cache_report(oracle, "test-preprocessor");
    passed_all = oracle_serve_end("test-preprocessor") && passed_all;
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

#endif
