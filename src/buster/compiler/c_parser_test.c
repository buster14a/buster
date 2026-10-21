// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-parser: c_parser.c's checks (AGENTS.md, "Parser tests"):
//   - snippets (parse_test_snippets): valid and invalid code, each with the first error clang gives (or none), which our
//     parser must give at the same line and column with the same message; with --clang, each expectation is checked
//     against clang too.
//   - files: every .c file under each path, preprocessed and parsed, must be accepted; with --clang, clang decides,
//     and the parser must accept exactly what `clang -fsyntax-only` accepts.
//   - mutations (--mutations N): N mutated copies of the files (a token deleted, duplicated, or swapped with the next,
//     in the -E text, with spaces kept around the change), each checked against clang: a file clang accepts must be
//     accepted, and one whose first error is a "Parse Issue" must be rejected with its first error on the same line.
//
// main.c includes this file after c_parser.c. Layout: running the parser on a file (ParseTestOutcome,
// parse_test_parse_file), running clang (parse_test_clang), the snippets, the files and the mutations, then
// run_parser_tests.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>

extern char** environ;

// What one file's parse gave: whether it preprocessed and parsed, and its first diagnostic's place and message.
typedef struct ParseTestOutcome ParseTestOutcome;
struct ParseTestOutcome
{
    bool preprocessed;
    bool parsed;
    bool out_of_memory;
    uint32_t errors;
    // The first error's presumed line and column (0 if none), and its message.
    uint32_t line;
    uint32_t column;
    char message[256];
    uint32_t tokens;
};

// Preprocesses and parses `path` with `options`, by the scalar parser alone if `scalar`. Diagnostics are not printed.
static ParseTestOutcome parse_test_parse_file(const char* path, const PPOptions* options, bool scalar)
{
    ParseTestOutcome result = { 0 };
    static InternTable table;
    static Preprocessor pp;
    static Parser parser;
    const ParseOptions parse_options = { .scalar = scalar };
    if (intern_initialize(&table) && pp_initialize(&pp, options, &table) && parse_initialize(&parser, &parse_options, options->language))
    {
        // The preprocessor's own diagnostics go to stderr; a file here is valid C for the preprocessor.
        result.preprocessed = pp_run(&pp, path);
        if (result.preprocessed)
        {
            result.parsed = parse_translation_unit(&parser, &pp);
            result.errors = parser.errors;
            result.tokens = parser.tokens.count;
            for (uint32_t d = 0; d < parser.diagnostic_count && result.line == 0; d += 1)
            {
                const ParseDiagnostic* diagnostic = &parser.diagnostics[d];
                if (diagnostic->error)
                {
                    const uint32_t location = parse_diagnostic_location(&parser, &pp, diagnostic);
                    const uint32_t range = pp_find_range(&pp, location);
                    if (range != BUSTER_PP_NONE && pp.ranges[range].file != BUSTER_PP_NONE)
                    {
                        const char* name;
                        uint32_t name_length;
                        pp_line_of(&pp, range, location, &result.column);
                        result.line = pp_presumed(&pp, location, &name, &name_length);
                    }
                    parse_diagnostic_message(&parser, &pp, diagnostic, result.message, sizeof(result.message));
                }
            }
        }
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&table);
    }
    else
    {
        result.out_of_memory = true;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Running clang

// What clang said of one file: whether it rejected it, and its first error's line, column, message and category.
typedef struct ParseTestClang ParseTestClang;
struct ParseTestClang
{
    bool rejected;
    uint32_t line;
    uint32_t column;
    bool parse_issue;
    char message[256];
};

// Runs `clang -fsyntax-only` with `arguments` on `count` files at once, and reads each file's first error from its
// output into `results`. Returns false if clang couldn't be run.
static bool parse_test_clang(const char* clang_path, const char* const* arguments, uint32_t argument_count, char** paths, uint32_t count, ParseTestClang* results)
{
    bool result = true;
    for (uint32_t i = 0; i < count; i += 1)
    {
        results[i] = (ParseTestClang){ 0 };
    }
    int pipe_fds[2];
    result = pipe(pipe_fds) == 0;
    pid_t child = -1;
    if (result)
    {
        const uint32_t fixed = 8;
        char** argv = malloc(sizeof(char*) * (fixed + argument_count + count + 1));
        result = argv != 0;
        if (result)
        {
            uint32_t n = 0;
            argv[n++] = (char*)clang_path;
            argv[n++] = "-fsyntax-only";
            argv[n++] = "-w";
            argv[n++] = "-fdiagnostics-show-category=name";
            argv[n++] = "-fno-caret-diagnostics";
            argv[n++] = "-fno-color-diagnostics";
            argv[n++] = "-ferror-limit=0";
            for (uint32_t a = 0; a < argument_count; a += 1)
            {
                argv[n++] = (char*)arguments[a];
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                argv[n++] = paths[i];
            }
            argv[n] = 0;
            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], 2);
            posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
            posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
            posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
            result = posix_spawnp(&child, clang_path, &actions, 0, argv, environ) == 0;
            posix_spawn_file_actions_destroy(&actions);
            free(argv);
        }
        close(pipe_fds[1]);
    }
    if (result)
    {
        FILE* stream = fdopen(pipe_fds[0], "r");
        char line[4096];
        while (stream != 0 && fgets(line, sizeof(line), stream) != 0)
        {
            // "path:line:column: error: message [category]"
            for (uint32_t i = 0; i < count; i += 1)
            {
                const size_t length = strlen(paths[i]);
                if (!results[i].rejected && strncmp(line, paths[i], length) == 0 && line[length] == ':')
                {
                    unsigned line_number = 0;
                    unsigned column = 0;
                    int consumed = 0;
                    if (sscanf(line + length, ":%u:%u: error: %n", &line_number, &column, &consumed) == 2 && consumed != 0)
                    {
                        results[i].rejected = true;
                        results[i].line = line_number;
                        results[i].column = column;
                        results[i].parse_issue = strstr(line, "Parse Issue]") != 0;
                        char* message = line + length + consumed;
                        char* bracket = strrchr(message, '[');
                        const size_t message_length = bracket != 0 && bracket > message ? (size_t)(bracket - message - 1) : strcspn(message, "\n");
                        snprintf(results[i].message, sizeof(results[i].message), "%.*s", (int)message_length, message);
                    }
                    else if (strstr(line, ": fatal error: ") != 0)
                    {
                        results[i].rejected = true;
                    }
                }
            }
        }
        if (stream != 0)
        {
            fclose(stream);
        }
        else
        {
            close(pipe_fds[0]);
        }
        int status = 0;
        result = waitpid(child, &status, 0) == child && WIFEXITED(status);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The snippets

// A snippet and the first error clang gives for it ("" for none), as "line:column: message", in gnu17 unless `std`
// says otherwise.
typedef struct ParseTestSnippet ParseTestSnippet;
struct ParseTestSnippet
{
    const char* source;
    const char* error;
    const char* std;
};

static const ParseTestSnippet parse_test_snippets[] =
{
    // Valid code.
    { "int x;\nint f(int a, int b) { return a + b * 2; }\n", "", 0 },
    { "struct S { int a : 3; char *p; } s = { 1, \"x\" };\n", "", 0 },
    { "int x; void g(void) { if (x) x = 1; else { for (int i = 0; i < 3; i++) x += i; } do x--; while (x); switch (x) { case 1: break; default: ; } }\n", "", 0 },
    { "typedef int T; T (x); int f(T (x)); int g(int (x));\n", "", 0 },
    { "typedef int T; void f(void) { int T; T = 1; }\n", "", 0 },
    { "struct S { int x }; struct S2 { int x; int y };\n", "", 0 },
    { "void f(void) { L: }\n", "", 0 },
    { "void f(void) { a: int x; }\n", "", 0 },
    { "[[deprecated]] int x; int y [[maybe_unused]];\n", "", 0 },
    { "enum E : int { A }; enum F : unsigned char;\n", "", 0 },
    { "int x = (int){1}; int a[3] = { [1 ... 2] = 0 };\n", "", 0 },
    { "void f(int x) { asm volatile (\"\" : [r] \"=r\" (x) : \"r\" (x) : \"memory\"); }\n", "", 0 },
    { "int f(a, b) int a; char b; { return a + b; }\n", "", 0 },
    { "int x = 1 ? : 2;\n", "", 0 },
    { "int f(void) { return ({ int y = 1; y; }); }\n", "", 0 },
    { "void f(void) { __label__ a, b; a: b: ; }\n", "", 0 },
    { "_Static_assert(1, \"x\"); _Static_assert(1); struct S { _Static_assert(1, \"y\"); int x; };\n", "", 0 },
    { "int x = sizeof(int) + _Alignof(int) + __builtin_offsetof(struct { int a; }, a);\n", "", 0 },
    { "int x = _Generic(1, int: 2, default: 3);\n", "", 0 },
    { "static_assert(1); auto x = 1; constexpr int y = 2; bool b = true; void *p = nullptr;\n", "", "c23" },
    // Invalid code, with clang 23's first error.
    { "int x y;\n", "1:6: expected ';' after top level declarator", 0 },
    { "void f(int a) { a + ; }\n", "1:21: expected expression", 0 },
    { "int f(void) { return 1 }\n", "1:23: expected ';' after return statement", 0 },
    { "int f(void) { int x = 1\nreturn x; }\n", "1:24: expected ';' at end of declaration", 0 },
    { "int a[3 4];\n", "1:9: expected ']'", 0 },
    { "struct { x; } s;\n", "1:10: type name requires a specifier or qualifier", 0 },
    { "void f(void) { if (1) }\n", "1:23: expected statement", 0 },
    { "enum E { A B };\n", "1:11: missing ',' between enumerators", 0 },
    { "enum E { A = 1 2 };\n", "1:16: expected '}' or ','", 0 },
    { "enum E {};\n", "1:9: use of empty enum", 0 },
    { "enum E { 5 };\n", "1:10: expected identifier", 0 },
    { "int f(int a) { return (a; }\n", "1:25: expected ')'", 0 },
    { "void f(int a) { f(a)); }\n", "1:21: extraneous ')' before ';'", 0 },
    { "void f(int a) { a ]; }\n", "1:19: extraneous ']' before ';'", 0 },
    { "struct S { int a; } int y;\n", "1:20: expected ';' after struct", 0 },
    { "int ();\n", "1:6: expected identifier or '('", 0 },
    { "int (*);\n", "1:7: expected identifier or '('", 0 },
    { "int *;\n", "1:6: expected identifier or '('", 0 },
    { "int a = 1, ;\n", "1:12: expected identifier or '('", 0 },
    { "void f(void) { int (*)(void); }\n", "1:22: expected identifier or '('", 0 },
    { "5;\n", "1:1: expected identifier or '('", 0 },
    { "}\n", "1:1: extraneous closing brace ('}')", 0 },
    { "int f(int x int y);\n", "1:13: expected ')'", 0 },
    { "int f(int, ..., int);\n", "1:15: expected ')'", 0 },
    { "int a = ;\n", "1:9: expected expression", 0 },
    { "void f(void) { if (1) else; }\n", "1:23: expected expression", 0 },
    { "void f(void) { else ; }\n", "1:16: expected expression", 0 },
    { "void f(void) { for (;) ; }\n", "1:22: expected ';' in 'for' statement specifier", 0 },
    { "void f(void) { for (;;;) ; }\n", "1:23: expected expression", 0 },
    { "void f(void) { goto 5; }\n", "1:21: expected identifier", 0 },
    { "void f(void) { while(1) break 5; }\n", "1:30: expected ';' after break statement", 0 },
    { "void f(void) { while(1) continue x; }\n", "1:34: named 'continue' is only supported in C2y", 0 },
    { "void f(void) { int x; x = (int); }\n", "1:32: expected expression", 0 },
    { "void f(void) { int x; x.; }\n", "1:25: expected identifier", 0 },
    { "void f(int a) { a ? 1; }\n", "1:22: expected ':'", 0 },
    { "void f(void) { f(,); }\n", "1:18: expected expression", 0 },
    { "void f(void) { f(1,); }\n", "1:20: expected expression", 0 },
    { "void f(void) { do ; }\n", "1:21: expected 'while' in do/while loop", 0 },
    { "void f(void) { do ; while (1) }\n", "1:30: expected ';' after do/while statement", 0 },
    { "int (*p)(int;\n", "1:13: expected ')'", 0 },
    { "int x = sizeof int;\n", "1:15: expected parentheses around type name in sizeof expression", 0 },
    { "void f(void) { if (1) int y; }\n", "1:23: expected expression", 0 },
    { "void f(void) { int g(void) { return 1; } }\n", "1:28: function definition is not allowed here", 0 },
    { "struct S { + };\n", "1:12: type name requires a specifier or qualifier", 0 },
    { "int x = (int y)1;\n", "1:14: expected ')'", 0 },
    { "int x = sizeof(int y);\n", "1:20: expected ')'", 0 },
    { "void f(void) { if 1; }\n", "1:19: expected '(' after 'if'", 0 },
    { "void f(void) { while }\n", "1:22: expected '(' after 'while'", 0 },
    { "struct;\n", "1:1: declaration of anonymous struct must be a definition", 0 },
    { "void f(void) { asm(\"nop\") x; }\n", "1:26: expected ';' after asm statement", 0 },
    { "asm(\"nop\") x;\n", "1:11: expected ';' after top-level asm block", 0 },
    { "_Static_assert(1, \"x\") int y;\n", "1:23: expected ';' after '_Static_assert'", 0 },
    { "void f(void) { goto x y; }\n", "1:22: expected ';' after goto statement", 0 },
    { "int x = __builtin_offsetof(int y, z);\n", "1:31: expected ','", 0 },
    { "void f(void) { switch(1) { default 1; } }\n", "1:35: expected ':' after 'default'", 0 },
    { "int x = _Generic(1, int 1);\n", "1:24: expected ':'", 0 },
    { "int x = (1 ? 2 : );\n", "1:18: expected expression", 0 },
    { "enum E { A = 1 = 2 };\n", "1:16: expected '}' or ','", 0 },
    { "void f(int a) { switch (a) { case 1 = 2: ; } }\n", "1:36: expected ':' after 'case'", 0 },
    { "void f(int a) { switch (a) { case 1, 2: ; } }\n", "1:36: expected ':' after 'case'", 0 },
    { "struct S { int a : 1 = 2; };\n", "1:21: expected ';' at end of declaration list", 0 },
    { "_Static_assert(1 = 2, \"x\");\n", "1:17: expected ','", 0 },
    { "void f(int a) { a ... 2; }\n", "1:18: expected ';' after expression", 0 },
    { "void f(int x) { asm (\"\" : \"=r\" [r] (x)); }\n", "1:32: expected '(' after 'asm operand'", 0 },
    { "int x = ({ 1; 2; });\n", "1:9: statement expression not allowed at file scope", 0 },
    { "void f(void) { int x; __label__ a; }\n", "1:23: expected expression", 0 },
    { "void f(void) { return }\n", "1:23: expected expression", 0 },
    { "void f(void) { int x }\n", "1:21: expected ';' at end of declaration", 0 },
    { "void f(int x) { x = 1 }\n", "1:22: expected ';' after expression", 0 },
    { "int x = { 1, 2 } }\n", "1:17: expected ';' after top level declarator", 0 },
    { "struct {;\n\n\n", "3:1: expected '}'", 0 },
    { "void f(void) {\n  int x;\n\n", "3:1: expected '}'", 0 },
    { "int x\n\n", "1:6: expected ';' after top level declarator", 0 },
    { "int x = (1 +\n\n", "2:1: expected expression", 0 },
};

// Writes `text` to `path`. Returns false if it can't.
static bool parse_test_write(const char* path, const char* text, size_t length)
{
    FILE* file = fopen(path, "w");
    const bool result = file != 0 && fwrite(text, 1, length, file) == length;
    if (file != 0)
    {
        fclose(file);
    }
    return result;
}

// The options a snippet is preprocessed with: no system headers, and its language mode.
static PPOptions parse_test_snippet_options(const ParseTestSnippet* snippet)
{
    PPOptions result = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    if (snippet->std != 0)
    {
        pp_parse_standard(snippet->std, &result.language);
    }
    return result;
}

// Each snippet's first error (or none) against its expectation, and with `clang` the expectation against clang's.
static uint32_t parse_test_check_snippets(const char* directory, const char* clang)
{
    uint32_t failures = 0;
    const uint32_t count = (uint32_t)(sizeof(parse_test_snippets) / sizeof(parse_test_snippets[0]));
    char path[4096];
    for (uint32_t i = 0; i < count; i += 1)
    {
        const ParseTestSnippet* snippet = &parse_test_snippets[i];
        snprintf(path, sizeof(path), "%s/snippet_%u.c", directory, i);
        bool good = parse_test_write(path, snippet->source, strlen(snippet->source));
        const PPOptions options = parse_test_snippet_options(snippet);
        for (uint32_t scalar = 0; good && scalar < 2; scalar += 1)
        {
            const ParseTestOutcome outcome = parse_test_parse_file(path, &options, scalar != 0);
            char got[320] = "";
            if (outcome.errors != 0)
            {
                snprintf(got, sizeof(got), "%u:%u: %s", outcome.line, outcome.column, outcome.message);
            }
            if (!outcome.preprocessed || strcmp(got, snippet->error) != 0)
            {
                failures += 1;
                printf("snippet %u (%s parser): expected \"%s\", got \"%s\"%s\n    %s", i, scalar ? "scalar" : "vector", snippet->error, got,
                       outcome.preprocessed ? "" : " (not preprocessed)", snippet->source);
            }
        }
        if (good && clang != 0)
        {
            char standard[32];
            snprintf(standard, sizeof(standard), "-std=%s", snippet->std != 0 ? snippet->std : "gnu17");
            const char* arguments[] = { standard };
            char* paths[] = { path };
            ParseTestClang theirs;
            good = parse_test_clang(clang, arguments, 1, paths, 1, &theirs);
            char expected[320] = "";
            if (good && theirs.rejected)
            {
                snprintf(expected, sizeof(expected), "%u:%u: %s", theirs.line, theirs.column, theirs.message);
            }
            if (!good || strcmp(expected, snippet->error) != 0 || (theirs.rejected && !theirs.parse_issue))
            {
                failures += 1;
                printf("snippet %u: clang gives \"%s\"%s, the table \"%s\"\n    %s", i, expected, theirs.rejected && !theirs.parse_issue ? " (not a parse issue)" : "",
                       snippet->error, snippet->source);
            }
        }
        remove(path);
    }
    printf("snippets: %u, %u failures%s\n", count, failures, clang != 0 ? " (with clang)" : "");
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// The files and the mutations

// The .c files under `path` (or `path` itself), appended to `paths` (up to `capacity`, each malloc'd), in sorted order
// within each directory. Returns how many there are now.
static uint32_t parse_test_collect(const char* path, char** paths, uint32_t count, uint32_t capacity)
{
    struct stat status;
    uint32_t result = count;
    if (stat(path, &status) == 0 && S_ISDIR(status.st_mode))
    {
        DIR* directory = opendir(path);
        struct dirent** entries = 0;
        const int n = directory != 0 ? scandir(path, &entries, 0, alphasort) : 0;
        for (int i = 0; i < n; i += 1)
        {
            const char* name = entries[i]->d_name;
            if (name[0] != '.')
            {
                char child[4096];
                snprintf(child, sizeof(child), "%s/%s", path, name);
                result = parse_test_collect(child, paths, result, capacity);
            }
            free(entries[i]);
        }
        free(entries);
        if (directory != 0)
        {
            closedir(directory);
        }
    }
    else if (strlen(path) > 2 && strcmp(path + strlen(path) - 2, ".c") == 0 && result < capacity)
    {
        paths[result] = strdup(path);
        result += 1;
    }
    return result;
}

// A run of the tests' pseudo-random numbers (SplitMix64, public domain).
static uint64_t parse_test_random(uint64_t* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// The -E text of `path` with `options` (no line markers): what clang and the parser read for the mutations. Null if the
// file doesn't preprocess; `free` it.
static char* parse_test_preprocessed(const char* path, const PPOptions* options, size_t* length)
{
    static InternTable table;
    static Preprocessor pp;
    char* result = 0;
    *length = 0;
    if (intern_initialize(&table) && pp_initialize(&pp, options, &table) && pp_run(&pp, path))
    {
        FILE* stream = open_memstream(&result, length);
        const bool printed = stream != 0 && pp_print(&pp, stream, false);
        if (stream != 0)
        {
            fclose(stream);
        }
        if (!printed)
        {
            free(result);
            result = 0;
        }
    }
    pp_release(&pp);
    intern_release(&table);
    return result;
}

// One mutation of `text`: a token deleted, duplicated, or swapped with the next (`kind` 0, 1, 2), with a space on each
// side of what changed, so that no two tokens join. `starts` and `ends` are the text's tokens. The result is malloc'd.
static char* parse_test_mutate(const char* text, size_t length, const uint32_t* starts, const uint32_t* ends, uint32_t token, uint32_t kind, size_t* mutated_length)
{
    const uint32_t start = starts[token];
    const uint32_t end = ends[token];
    const uint32_t next_start = starts[token + 1];
    const uint32_t next_end = ends[token + 1];
    char* result = malloc(length + 2 * (end - start) + 8);
    size_t n = 0;
    memcpy(result, text, start);
    n = start;
    if (kind == 0)
    {
        result[n++] = ' ';
        memcpy(result + n, text + end, length - end);
        n += length - end;
    }
    else if (kind == 1)
    {
        memcpy(result + n, text + start, end - start);
        n += end - start;
        result[n++] = ' ';
        memcpy(result + n, text + start, length - start);
        n += length - start;
    }
    else
    {
        // The next token, the gap, then this one.
        result[n++] = ' ';
        memcpy(result + n, text + next_start, next_end - next_start);
        n += next_end - next_start;
        memcpy(result + n, text + end, next_start - end);
        n += next_start - end;
        memcpy(result + n, text + start, end - start);
        n += end - start;
        result[n++] = ' ';
        memcpy(result + n, text + next_end, length - next_end);
        n += length - next_end;
    }
    *mutated_length = n;
    return result;
}

// What the comparisons with clang found.
typedef struct ParseTestTally ParseTestTally;
struct ParseTestTally
{
    uint64_t files;
    uint64_t accepted;
    uint64_t parse_issues;
    uint64_t other_issues;
    uint64_t other_rejected;
    uint64_t mismatches;
    uint64_t printed;
};

// Our outcome for a file against clang's: a file clang accepts must parse; one whose first error is a parse issue must
// fail with its first error on the same line.
static void parse_test_compare(ParseTestTally* tally, const char* label, const ParseTestOutcome* ours, const ParseTestClang* theirs, const char* detail)
{
    bool match = true;
    tally->files += 1;
    if (!theirs->rejected)
    {
        tally->accepted += 1;
        match = ours->preprocessed && ours->errors == 0;
    }
    else if (theirs->parse_issue)
    {
        tally->parse_issues += 1;
        match = ours->preprocessed && ours->errors != 0 && ours->line == theirs->line;
    }
    else
    {
        tally->other_issues += 1;
        tally->other_rejected += ours->errors != 0 || !ours->preprocessed;
    }
    if (!match)
    {
        tally->mismatches += 1;
        if (tally->printed < 40)
        {
            tally->printed += 1;
            printf("%s%s: clang: %s%u:%u: %s; ours: %s%u:%u: %s\n", label, detail, theirs->rejected ? "" : "accepted ", theirs->line, theirs->column, theirs->message,
                   ours->errors != 0 ? "" : "accepted ", ours->line, ours->column, ours->message);
        }
    }
}

#define BUSTER_PARSE_TEST_BATCH 32

// The mutations: `count` mutated copies of the files, spread over them in turn, each checked against clang.
static uint32_t parse_test_mutations(char** files, uint32_t file_count, const PPOptions* options, const char* const* clang_arguments, uint32_t clang_argument_count,
                                     const char* clang, uint64_t count, uint64_t seed, const char* directory, bool keep)
{
    uint32_t failures = 0;
    ParseTestTally tally = { 0 };
    uint64_t state = seed;
    PPOptions plain = *options;
    // The mutated files are -E's text: nothing to include.
    plain.no_standard_includes = true;
    plain.include_count = 0;
    plain.macro_count = 0;
    char* batch_paths[BUSTER_PARSE_TEST_BATCH];
    char batch_details[BUSTER_PARSE_TEST_BATCH][160];
    ParseTestClang batch_results[BUSTER_PARSE_TEST_BATCH];
    for (uint32_t b = 0; b < BUSTER_PARSE_TEST_BATCH; b += 1)
    {
        batch_paths[b] = malloc(4096);
    }
    uint64_t done = 0;
    for (uint32_t f = 0; done < count && f < file_count * 64; f += 1)
    {
        const char* file = files[f % file_count];
        size_t length = 0;
        char* text = parse_test_preprocessed(file, options, &length);
        uint8_t* kinds = text != 0 ? malloc(BUSTER_C_TOKENIZER_CAPACITY(length)) : 0;
        uint32_t* starts = text != 0 ? malloc(sizeof(uint32_t) * BUSTER_C_TOKENIZER_CAPACITY(length)) : 0;
        uint32_t* ends = text != 0 ? malloc(sizeof(uint32_t) * BUSTER_C_TOKENIZER_CAPACITY(length)) : 0;
        const int64_t tokens = text != 0 ? c_tokenize((const uint8_t*)text, length, kinds, starts, ends, 0) : 0;
        // This file's share of the mutations, at least one batch.
        const uint64_t share = (count - done + file_count - 1 - f % file_count) / (file_count - f % file_count);
        uint64_t mutations = share != 0 ? share : 1;
        while (tokens > 2 && mutations != 0 && done < count)
        {
            const uint32_t in_batch = mutations < BUSTER_PARSE_TEST_BATCH ? (uint32_t)mutations : BUSTER_PARSE_TEST_BATCH;
            for (uint32_t b = 0; b < in_batch; b += 1)
            {
                const uint32_t token = (uint32_t)(parse_test_random(&state) % (uint64_t)(tokens - 1));
                const uint32_t kind = (uint32_t)(parse_test_random(&state) % 3);
                size_t mutated_length = 0;
                char* mutated = parse_test_mutate(text, length, starts, ends, token, kind, &mutated_length);
                snprintf(batch_paths[b], 4096, "%s/mutation_%u.c", directory, b);
                parse_test_write(batch_paths[b], mutated, mutated_length);
                snprintf(batch_details[b], sizeof(batch_details[b]), " (%s token %u, \"%.*s\")", kind == 0 ? "deleted" : kind == 1 ? "duplicated" : "swapped",
                         token, (int)(ends[token] - starts[token] < 40 ? ends[token] - starts[token] : 40), text + starts[token]);
                free(mutated);
            }
            if (!parse_test_clang(clang, clang_arguments, clang_argument_count, batch_paths, in_batch, batch_results))
            {
                printf("can't run %s\n", clang);
                failures += 1;
                mutations = 0;
            }
            for (uint32_t b = 0; b < in_batch && failures == 0; b += 1)
            {
                const ParseTestOutcome ours = parse_test_parse_file(batch_paths[b], &plain, false);
                char label[4096];
                const uint64_t mismatches = tally.mismatches;
                snprintf(label, sizeof(label), "%s", file);
                parse_test_compare(&tally, label, &ours, &batch_results[b], batch_details[b]);
                if (keep && tally.mismatches != mismatches)
                {
                    // Kept for a look: the mismatch's mutated file.
                    char kept[4200];
                    snprintf(kept, sizeof(kept), "%s/mismatch_%llu.c", directory, (unsigned long long)mismatches);
                    rename(batch_paths[b], kept);
                    printf("    kept as %s\n", kept);
                }
                remove(batch_paths[b]);
            }
            mutations -= in_batch;
            done += in_batch;
        }
        free(kinds);
        free(starts);
        free(ends);
        free(text);
    }
    for (uint32_t b = 0; b < BUSTER_PARSE_TEST_BATCH; b += 1)
    {
        free(batch_paths[b]);
    }
    printf("mutations: %llu (clang accepted %llu, a parse issue first in %llu, another issue first in %llu, of which we rejected %llu), %llu mismatches\n",
           (unsigned long long)tally.files, (unsigned long long)tally.accepted, (unsigned long long)tally.parse_issues, (unsigned long long)tally.other_issues,
           (unsigned long long)tally.other_rejected, (unsigned long long)tally.mismatches);
    return failures + (uint32_t)(tally.mismatches != 0);
}

// The files: each must preprocess and parse, and with `clang`, the parser must accept exactly what clang accepts.
static uint32_t parse_test_files(char** files, uint32_t file_count, const PPOptions* options, const char* const* clang_arguments, uint32_t clang_argument_count,
                                 const char* clang)
{
    uint32_t failures = 0;
    ParseTestTally tally = { 0 };
    uint64_t tokens = 0;
    for (uint32_t f = 0; f < file_count; f += BUSTER_PARSE_TEST_BATCH)
    {
        const uint32_t in_batch = file_count - f < BUSTER_PARSE_TEST_BATCH ? file_count - f : BUSTER_PARSE_TEST_BATCH;
        ParseTestClang theirs[BUSTER_PARSE_TEST_BATCH] = { 0 };
        if (clang != 0 && !parse_test_clang(clang, clang_arguments, clang_argument_count, files + f, in_batch, theirs))
        {
            printf("can't run %s\n", clang);
            failures += 1;
        }
        for (uint32_t b = 0; b < in_batch; b += 1)
        {
            const ParseTestOutcome ours = parse_test_parse_file(files[f + b], options, false);
            const ParseTestOutcome scalar = parse_test_parse_file(files[f + b], options, true);
            tokens += ours.tokens;
            parse_test_compare(&tally, files[f + b], &ours, &theirs[b], "");
            if (ours.errors != scalar.errors || ours.line != scalar.line || ours.column != scalar.column || strcmp(ours.message, scalar.message) != 0)
            {
                failures += 1;
                printf("%s: the paths differ: %u:%u: %s, and the scalar parser's %u:%u: %s\n", files[f + b], ours.line, ours.column, ours.message, scalar.line, scalar.column,
                       scalar.message);
            }
        }
    }
    printf("files: %llu (%llu tokens), clang accepted %llu, rejected %llu%s, %llu mismatches\n", (unsigned long long)tally.files, (unsigned long long)tokens,
           (unsigned long long)tally.accepted, (unsigned long long)(tally.parse_issues + tally.other_issues), clang != 0 ? "" : " (without clang: every file must parse)",
           (unsigned long long)tally.mismatches);
    return failures + (uint32_t)(tally.mismatches != 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

// `test-parser [--clang [PATH]] [--mutations N] [--seed N] [--keep] [options] [file or directory...]`, with -E's
// options for the files.
static int run_parser_tests(int argument_count, char** arguments)
{
    uint32_t failures = 0;
    const char* clang = 0;
    uint64_t mutations = 0;
    uint64_t seed = 1;
    bool keep = false;
    char** rest = malloc(sizeof(char*) * ((size_t)argument_count + 2));
    int rest_count = 0;
    const char** clang_arguments = malloc(sizeof(char*) * ((size_t)argument_count + 2));
    uint32_t clang_argument_count = 0;
    char** inputs = malloc(sizeof(char*) * ((size_t)argument_count + 1));
    uint32_t input_count = 0;
    bool valid = rest != 0 && clang_arguments != 0 && inputs != 0;
    for (int i = 0; valid && i < argument_count; i += 1)
    {
        const char* argument = arguments[i];
        if (strcmp(argument, "--clang") == 0)
        {
            const bool path = i + 1 < argument_count && arguments[i + 1][0] != '-' && strchr(arguments[i + 1], '/') != 0 && strstr(arguments[i + 1], "clang") != 0;
            clang = path ? arguments[i + 1] : "clang";
            i += path;
        }
        else if (strcmp(argument, "--mutations") == 0 && i + 1 < argument_count)
        {
            mutations = strtoull(arguments[i + 1], 0, 10);
            i += 1;
        }
        else if (strcmp(argument, "--seed") == 0 && i + 1 < argument_count)
        {
            seed = strtoull(arguments[i + 1], 0, 10);
            i += 1;
        }
        else if (strcmp(argument, "--keep") == 0)
        {
            keep = true;
        }
        else if (argument[0] != '-')
        {
            inputs[input_count] = arguments[i];
            input_count += 1;
        }
        else
        {
            // -E's options, for our preprocessor and for clang (but --predefines, ours only, and its file).
            rest[rest_count] = arguments[i];
            rest_count += 1;
            if (strcmp(argument, "--predefines") == 0 && i + 1 < argument_count)
            {
                rest[rest_count] = arguments[i + 1];
                rest_count += 1;
                i += 1;
            }
            else
            {
                const bool takes = (strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                    strcmp(argument, "-include") == 0 || strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0) && i + 1 < argument_count;
                clang_arguments[clang_argument_count] = argument;
                clang_argument_count += 1;
                if (takes)
                {
                    rest[rest_count] = arguments[i + 1];
                    rest_count += 1;
                    clang_arguments[clang_argument_count] = arguments[i + 1];
                    clang_argument_count += 1;
                    i += 1;
                }
            }
        }
    }
    // The options, through -E's parser, with a placeholder input.
    PPOptions options;
    Arena arena = arena_reserve(1ull << 26);
    const char* placeholder;
    const char* output;
    bool markers;
    rest[rest_count] = "placeholder.c";
    valid = valid && pp_parse_options(rest_count + 1, rest, &options, &arena, &placeholder, &output, &markers);
    char standard[32];
    snprintf(standard, sizeof(standard), "-std=%s%u", options.language.gnu ? "gnu" : "c", options.language.standard % 100);
    clang_arguments[clang_argument_count] = standard;
    clang_argument_count += valid;
    char directory[4096];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-test-parser-XXXXXX", temporary != 0 ? temporary : "/tmp");
    valid = valid && mkdtemp(directory) != 0;
    if (!valid)
    {
        printf("usage: test-parser [--clang [PATH]] [--mutations N] [--seed N] [--keep] [-E's options] [file or directory...]\n");
        failures += 1;
    }
    else
    {
        failures += parse_test_check_snippets(directory, clang);
        char** files = malloc(sizeof(char*) * (1u << 20));
        uint32_t file_count = 0;
        for (uint32_t i = 0; i < input_count; i += 1)
        {
            file_count = parse_test_collect(inputs[i], files, file_count, 1u << 20);
        }
        if (file_count != 0)
        {
            failures += parse_test_files(files, file_count, &options, clang_arguments, clang_argument_count, clang);
        }
        if (file_count != 0 && mutations != 0 && clang != 0)
        {
            failures += parse_test_mutations(files, file_count, &options, clang_arguments, clang_argument_count, clang, mutations, seed, directory, keep);
        }
        else if (mutations != 0)
        {
            printf("--mutations needs --clang and files\n");
            failures += 1;
        }
        for (uint32_t i = 0; i < file_count; i += 1)
        {
            free(files[i]);
        }
        free(files);
        if (!keep)
        {
            rmdir(directory);
        }
    }
    printf("test-parser: %s\n", failures == 0 ? "passed" : "FAILED");
    arena_release(&arena);
    free(rest);
    free(clang_arguments);
    free(inputs);
    return failures == 0 ? 0 : 1;
}
