// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-parser: c_parser.c's checks (AGENTS.md, "Parser tests"). Every unit goes through both paths, the vector path
// (with its fallback) and the scalar parser alone, which must agree: on a valid unit the same tree, which must keep the
// tree's invariants and whose postorder must be a depth-first walk's; and each kernel against its scalar twin. On:
//   - snippets (parse_test_snippets): valid and invalid code, each with the first error clang gives (or none), which our
//     parser must give at the same line and column with the same message; with --clang, each expectation is checked
//     against clang too.
//   - without files, generated inputs: random streams of tokens, the snippets at every block alignment, and deep
//     nesting; and the driver (run_parse_command): its statuses and messages, runs that must leave nothing behind, and
//     every reservation of a run failed in turn, in children whose address space is limited.
//   - files: every .c file under each path, preprocessed and parsed, must be accepted; with --clang, clang decides,
//     and the parser must accept exactly what `clang -fsyntax-only` accepts.
//   - mutations (--mutations N): N mutated copies of the files (a token deleted, duplicated, or swapped with the next,
//     in the -E text, with spaces kept around the change). With --clang, each is checked against clang: a file clang
//     accepts must be accepted, and one whose first error is a "Parse Issue" must be rejected with its first error on
//     the same line. Without, each goes through both paths, which must agree as on any file.
//
// main.c includes this file after c_parser.c. Layout: running the parser on a file (ParseTestOutcome,
// parse_test_parse_file), running clang (parse_test_clang), the snippets, the trees, the kernels, the paths on one
// file, K6's failures on valid code (--explain), a look at a tree (--print-tree), the files and the mutations, the
// generated inputs, the driver, then run_parser_tests.

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

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

// The outcome of a parse: its errors, and the first one's presumed line and column and message.
static ParseTestOutcome parse_test_outcome(const Parser* parser, Preprocessor* pp)
{
    ParseTestOutcome result = { .preprocessed = true, .parsed = parser->errors == 0, .errors = parser->errors, .tokens = parser->tokens.count };
    for (uint32_t d = 0; d < parser->diagnostic_count && result.line == 0; d += 1)
    {
        const ParseDiagnostic* diagnostic = &parser->diagnostics[d];
        if (diagnostic->error)
        {
            const uint32_t location = parse_diagnostic_location(parser, pp, diagnostic);
            const uint32_t range = pp_find_range(pp, location);
            if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
            {
                const char* name;
                uint32_t name_length;
                pp_line_of(pp, range, location, &result.column);
                result.line = pp_presumed(pp, location, &name, &name_length);
            }
            parse_diagnostic_message(parser, pp, diagnostic, result.message, sizeof(result.message));
        }
    }
    return result;
}

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
        if (pp_run(&pp, path))
        {
            parse_translation_unit(&parser, &pp);
            result = parse_test_outcome(&parser, &pp);
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

// A run of the tests' pseudo-random numbers (SplitMix64, public domain).
static uint64_t parse_test_random(uint64_t* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
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

// Starts `clang -fsyntax-only` with `arguments` on `count` files at once, its standard error into `output` (a file in
// memory, test_process_file: a pipe would stall clang while another process's is read). False if it couldn't start.
static bool parse_test_clang_spawn(const char* clang_path, const char* const* arguments, uint32_t argument_count, char** paths, uint32_t count, FILE* output, pid_t* child)
{
    const uint32_t fixed = 8;
    char** argv = malloc(sizeof(char*) * (fixed + argument_count + count + 1));
    bool result = argv != 0;
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
        posix_spawn_file_actions_adddup2(&actions, fileno(output), 2);
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        // clang reads CPATH and C_INCLUDE_PATH, which the preprocessor doesn't: without them, both search alike.
        uint32_t variables = 0;
        while (environ[variables] != 0)
        {
            variables += 1;
        }
        char** environment = malloc(sizeof(char*) * (variables + 1));
        uint32_t kept = 0;
        for (uint32_t v = 0; environment != 0 && v < variables; v += 1)
        {
            if (strncmp(environ[v], "CPATH=", 6) != 0 && strncmp(environ[v], "C_INCLUDE_PATH=", 15) != 0)
            {
                environment[kept] = environ[v];
                kept += 1;
            }
        }
        if (environment != 0)
        {
            environment[kept] = 0;
        }
        result = environment != 0 && posix_spawnp(child, clang_path, &actions, 0, argv, environment) == 0;
        free(environment);
        posix_spawn_file_actions_destroy(&actions);
        free(argv);
    }
    return result;
}

// Reads each of `count` files' first error from clang's output (whose wait status is `status`) into `results`: one in
// a header the file includes counts for the file. False if clang didn't parse the files: an error no file accounts for
// (clang's driver's, for an unknown option), or a failing exit with no file rejected.
static bool parse_test_clang_parse(FILE* output, int status, char** paths, uint32_t count, ParseTestClang* results)
{
    bool result = fseek(output, 0, SEEK_SET) == 0;
    char line[4096];
    // The file the last "In file included from" line named, whose header the errors after it are in (clang names the
    // includes again only where they change), and whether an error came that no file accounts for.
    static const char included[] = "In file included from ";
    uint32_t including = BUSTER_PARSE_NONE;
    bool unaccounted = false;
    while (result && fgets(line, sizeof(line), output) != 0)
    {
        bool accounted = false;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const size_t length = strlen(paths[i]);
            const size_t prefix = sizeof(included) - 1;
            if (strncmp(line, included, prefix) == 0 && strncmp(line + prefix, paths[i], length) == 0 && line[prefix + length] == ':')
            {
                including = i;
            }
            accounted |= strncmp(line, paths[i], length) == 0 && line[length] == ':';
        }
        const bool error = strstr(line, "error: ") != 0;
        if (error && !accounted && strncmp(line, included, sizeof(included) - 1) != 0 && including != BUSTER_PARSE_NONE && !results[including].rejected)
        {
            // An error in a header: the file that includes it is rejected, at a place in the header.
            results[including].rejected = true;
            results[including].parse_issue = strstr(line, "Parse Issue]") != 0;
            snprintf(results[including].message, sizeof(results[including].message), "%.*s", (int)strcspn(line, "\n"), line);
        }
        unaccounted |= error && !accounted && including == BUSTER_PARSE_NONE;
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
    bool rejected = false;
    for (uint32_t i = 0; i < count; i += 1)
    {
        rejected |= results[i].rejected;
    }
    return result && !ferror(output) && WIFEXITED(status) && !unaccounted && (WEXITSTATUS(status) == 0 || rejected);
}

// Waits for a process parse_test_clang_spawn started, and reads its files' first errors (parse_test_clang_parse).
static bool parse_test_clang_read(FILE* output, pid_t child, char** paths, uint32_t count, ParseTestClang* results)
{
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = waitpid(child, &status, 0);
    }
    while (waited < 0 && errno == EINTR);
    return waited == child && parse_test_clang_parse(output, status, paths, count, results);
}

// How many clang processes parse_test_clang runs its files on: at most one per CPU it may run on and one per
// BUSTER_PARSE_TEST_CLANG_FILES files, and at most BUSTER_PARSE_TEST_CLANG_PROCESSES (#465).
#define BUSTER_PARSE_TEST_CLANG_FILES 8
#define BUSTER_PARSE_TEST_CLANG_PROCESSES 8

static uint32_t parse_test_clang_processes(uint32_t count)
{
    const uint32_t cpus = test_process_cpus();
    uint32_t result = (count + BUSTER_PARSE_TEST_CLANG_FILES - 1) / BUSTER_PARSE_TEST_CLANG_FILES;
    result = result < cpus ? result : cpus;
    result = result < BUSTER_PARSE_TEST_CLANG_PROCESSES ? result : BUSTER_PARSE_TEST_CLANG_PROCESSES;
    return result != 0 ? result : 1;
}

// parse_test_clang's runs through the oracle cache (oracle_cache.c, #542): a clang process per file, as many at once as
// parse_test_clang_processes gives for one file per process, its dependencies listed, each one's errors read for its
// file. A file's result is the one a process on several gives it, since clang checks each file on its own.
static bool parse_test_clang_cached(const char* clang_path, const char* const* arguments, uint32_t argument_count, char** paths, uint32_t count, ParseTestClang* results)
{
    uint32_t variables = 0;
    while (environ[variables] != 0)
    {
        variables += 1;
    }
    const uint32_t cpus = test_process_cpus();
    const uint32_t at_once = cpus < BUSTER_PARSE_TEST_CLANG_PROCESSES ? cpus : BUSTER_PARSE_TEST_CLANG_PROCESSES;
    const uint32_t fixed = 7;
    char** environment = calloc((size_t)variables + 1, sizeof(char*));
    char** argvs = calloc((size_t)at_once * (fixed + argument_count + 2), sizeof(char*));
    OracleRun runs[BUSTER_PARSE_TEST_CLANG_PROCESSES];
    bool result = environment != 0 && argvs != 0;
    uint32_t kept = 0;
    for (uint32_t v = 0; result && v < variables; v += 1)
    {
        // clang reads CPATH and C_INCLUDE_PATH, which the preprocessor doesn't: without them, both search alike.
        if (strncmp(environ[v], "CPATH=", 6) != 0 && strncmp(environ[v], "C_INCLUDE_PATH=", 15) != 0)
        {
            environment[kept] = environ[v];
            kept += 1;
        }
    }
    uint32_t started = 0;
    uint32_t finished = 0;
    while (environment != 0 && argvs != 0 && finished < count)
    {
        if (started < count && started - finished < at_once)
        {
            char** argv = argvs + (size_t)(started % at_once) * (fixed + argument_count + 2);
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
            argv[n++] = paths[started];
            argv[n] = 0;
            runs[started % at_once] = (OracleRun){
                .purpose = "parser: clang -fsyntax-only",
                .arguments = argv,
                .environment = environment,
                .dependencies = true,
            };
            result = oracle_run_start(&runs[started % at_once]) && result;
            started += 1;
        }
        else
        {
            OracleRun* run = &runs[finished % at_once];
            result = oracle_run_finish(run) && result;
            FILE* output = result ? test_process_file() : 0;
            result = output != 0 && fwrite(run->errors, 1, run->errors_length, output) == run->errors_length &&
                     parse_test_clang_parse(output, run->status, paths + finished, 1, results + finished) && result;
            if (output != 0)
            {
                fclose(output);
            }
            oracle_run_release(run);
            finished += 1;
        }
    }
    free(environment);
    free(argvs);
    return result && environment != 0 && argvs != 0;
}

// Runs `clang -fsyntax-only` with `arguments` on `count` files, and reads each file's first error into `results`, as
// parse_test_clang_parse does. With the oracle cache on, a file at a time through it (parse_test_clang_cached); else
// the files are split in order among parse_test_clang_processes processes that run at once, each one's output read for
// its own files: the same results as one process on them all, since clang checks each file on its own. Returns false if
// a process couldn't be run, or didn't parse its files.
static bool parse_test_clang(const char* clang_path, const char* const* arguments, uint32_t argument_count, char** paths, uint32_t count, ParseTestClang* results)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        results[i] = (ParseTestClang){ 0 };
    }
    const bool cached = oracle_cache_active(oracle_cache());
    const uint32_t processes = cached ? 0 : parse_test_clang_processes(count);
    FILE* outputs[BUSTER_PARSE_TEST_CLANG_PROCESSES] = { 0 };
    pid_t children[BUSTER_PARSE_TEST_CLANG_PROCESSES] = { 0 };
    bool started[BUSTER_PARSE_TEST_CLANG_PROCESSES] = { 0 };
    bool result = !cached || parse_test_clang_cached(clang_path, arguments, argument_count, paths, count, results);
    for (uint32_t p = 0; p < processes; p += 1)
    {
        const uint32_t first = (uint32_t)((uint64_t)count * p / processes);
        const uint32_t end = (uint32_t)((uint64_t)count * (p + 1) / processes);
        outputs[p] = result ? test_process_file() : 0;
        started[p] = outputs[p] != 0 && parse_test_clang_spawn(clang_path, arguments, argument_count, paths + first, end - first, outputs[p], &children[p]);
        result = started[p];
    }
    for (uint32_t p = 0; p < processes; p += 1)
    {
        const uint32_t first = (uint32_t)((uint64_t)count * p / processes);
        const uint32_t end = (uint32_t)((uint64_t)count * (p + 1) / processes);
        const bool read = started[p] && parse_test_clang_read(outputs[p], children[p], paths + first, end - first, results + first);
        result = result && read;
        if (outputs[p] != 0)
        {
            fclose(outputs[p]);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The snippets

// A snippet and the first error clang gives for it ("" for none), as "line:column: message", in gnu17 unless `std`
// says otherwise. clang's first error must be a parse issue, but where `semantic` says it is one of the errors clang's
// semantic analysis gives as the parser hands it a declaration it took as clang's parser does (an unknown type name, a
// missing type specifier, a type name where an expression would be). A `semantic` snippet without an error is one
// clang's semantic analysis rejects and the parser takes.
typedef struct ParseTestSnippet ParseTestSnippet;
struct ParseTestSnippet
{
    const char* source;
    const char* error;
    const char* std;
    bool semantic;
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
    // A typedef's first declarator after an enum's underlying type and body is a type name (K3 skips the base).
    { "typedef int U; typedef enum E : unsigned short { A } T; typedef enum : const U { B } V, W; int n = sizeof(T) + sizeof(V) + sizeof(W);\n", "", 0 },
    // GNU attributes among an enum's base's specifiers, which clang takes as the base type's (#428's review): before,
    // between and after them, with a body or without, and where a typedef's declarator follows the body (K3 skips them).
    { "enum E : int __attribute__((unused)); enum F : int __attribute__((unused)) { A };\n", "", 0 },
    { "enum G : __attribute__((unused)) unsigned __attribute__((unused)) char __attribute__((unused)) { B };\n", "", 0 },
    { "typedef enum H : int __attribute__((unused)) { C } T; T t = C; int n = sizeof(T);\n", "", 0 },
    { "int x = (int){1}; int a[3] = { [1 ... 2] = 0 };\n", "", 0 },
    // GNU's single array designator may omit '=', including a range, a prefix operand and a braced initializer.
    { "int a[] = { [0] 1, [3] 4, [4] -1, [5] (int)2 };\n", "", 0 },
    { "int a[2] = { [0 ... 1] +1 }; int b[1][2] = { [0] { 1, 2 } };\n", "", "c17" },
    // GNU's old-style field designator uses ':' in place of '.member =', including a typedef-shadowing member.
    { "struct p { int x, y; }; struct p q = { y: 2, x: 1 };\n", "", 0 },
    { "typedef int T; struct p { int T, x; }; struct p q = { T: 1, x: 2 };\n", "", "c17" },
    { "void f(int x) { asm volatile (\"\" : [r] \"=r\" (x) : \"r\" (x) : \"memory\"); }\n", "", 0 },
    { "int f(a, b) int a; char b; { return a + b; }\n", "", 0 },
    { "int x = 1 ? : 2;\n", "", 0 },
    { "void f(int a, int b) { a = a ? b, a : b; for (b = a ? 1, 2 : 3; b; ) b--; }\n", "", 0 },
    { "int f(void) { return ({ int y = 1; y; }); }\n", "", 0 },
    { "void f(void) { __label__ a, b; a: b: ; }\n", "", 0 },
    { "_Static_assert(1, \"x\"); _Static_assert(1); struct S { _Static_assert(1, \"y\"); int x; };\n", "", 0 },
    { "int x = sizeof(int) + _Alignof(int) + __builtin_offsetof(struct { int a; }, a);\n", "", 0 },
    { "int x = _Generic(1, int: 2, default: 3);\n", "", 0 },
    { "static_assert(1); auto x = 1; constexpr int y = 2; bool b = true; void *p = nullptr;\n", "", "c23" },
    // An unnamed bit-field starts with ':', including after a comma on the preceding line.
    { "struct S { unsigned char :1,\n :1,x:1,y:5; };\n", "", 0 },
    { "struct S { unsigned x:1,\n :1,y:1; };\n", "", 0 },
    { "union U { unsigned x,\n :3; };\n", "", 0 },
    { "typedef unsigned T; struct S { T :2,\n :3,x:1; };\n", "", 0 },
    { "struct S { unsigned :1,\n :1 ? 2 : 3,x:4; };\n", "", 0 },
    // GNU's reserved spellings remain keywords in strict C; plain asm and (before C23) typeof do not.
    { "__extension__ typedef __signed__ __int128 Wide;\n__inline __const int *aliases(__const__ int *__restrict p, __volatile__ int *__restrict__ q) { return p; }\n__inline__ int widths(void) { return __alignof__(Wide) + __alignof(__signed int); }\n", "", "c17" },
    { "int asm, typeof; int f(void) { asm = typeof + 1; return asm; }\n", "", "c17" },
    { "int x; __typeof(x) y; __typeof__(const int) z; __typeof__(int (*)(int)) fp;\nint f(void) { __typeof__(&x) p = &x; __typeof__(*p) value = *p; return value; }\n", "", "c17" },
    { "int a[3]; typeof(a) b; typeof(int (*)(int)) f;\nint g(int x) { typeof((x + 1)) y = x; return y; }\n", "", 0 },
    { "const int x = 1; typeof(x) y = 2; typeof_unqual(x) z;\nint f(void) { typeof(int) n = 3; return n + y; }\n", "", "c23" },
    // A typeof operand follows the visible binding; nested statement expressions and __extension__ keep their scope.
    { "typedef int T; int f(void) { int T = 3; __typeof__(T) x = T; { typedef char T; __typeof__(T) y = 1; x += y; } return x; }\n", "", 0 },
    { "int f(int x) { __auto_type y = ({ __auto_type z = x + 1; z; }); return ({ __typeof__(y) z = y; z + 1; }); }\n", "", 0 },
    { "__extension__ typedef __int128 Wide;\nint f(int x) { __extension__ Wide y = x; return __extension__ ({ int z = (int)y; __extension__ (z ?: 1); }); }\n", "", "c17" },
    { "const char *f(void) { const char *names[] = { __func__, __FUNCTION__, __PRETTY_FUNCTION__ }; return names[0]; }\n", "", "c17" },
    { "struct S { int empty[0]; int value; }; int a[4] = { [0 ... 3] = 2 };\nint f(int x) { switch (x) { case -2 ... 2: return a[x + 2]; default: return x ?: 4; } }\n", "", 0 },
    { "__typeof__ int x;\n", "1:12: expected '(' after '__typeof__'", 0 },
    { "__typeof__() x;\n", "1:12: expected expression", 0 },
    { "__typeof__(int, char) x;\n", "1:15: expected ')'", 0 },
    { "__typeof__(int x;\n", "1:16: expected ')'", 0 },
    { "__typeof__(({ int x = 1; x; })) y;\n", "1:12: statement expression not allowed at file scope", 0 },
    // Invalid code, with clang 23's first error.
    { "struct S { unsigned :1,\n :; };\n", "2:3: expected expression", 0 },
    { "int x,\n:1;\n", "1:6: expected ';' at end of declaration", 0 },
    { "struct S {\n  int x;\n  int broken =\n};\n", "3:13: expected ';' at end of declaration list", 0 },
    { "struct T { int member = 1; };\n", "1:22: expected ';' at end of declaration list", 0 },
    { "union U { int (*callback)(void) = 0; };\n", "1:32: expected ';' at end of declaration list", 0 },
    { "struct V { int x:3 = 1; };\n", "1:19: expected ';' at end of declaration list", 0 },
    // GNU attributes after a bit-field's width end it: the member's, more of them, then `,` or `;`, or the body's `}` after
    // the last member (clang's warning). Anything else is a missing `;`, at the token for a `:` (clang's typo for one).
    { "enum { N = 3 }; struct S { int a : 3 __attribute__((aligned(4))), b : N __attribute__((packed)) __attribute__(()); long : 0 __attribute__((aligned(8)));"
      " char c : (1 + 2) __attribute__((unused)) };\n", "", 0 },
    { "struct S { int x : 3 __attribute__((packed)) + 1; };\n", "1:45: expected ';' at end of declaration list", 0 },
    { "struct S { int x : 3 __attribute__((packed)) : 4; };\n", "1:46: expected ';' at end of declaration list", 0 },
    { "struct S { int x : 3 __attribute__((packed)) [4]; };\n", "1:45: expected ';' at end of declaration list", 0 },
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
    { "struct S { union { int a; }\n};\n", "2:1: expected member name or ';' after declaration specifiers", 0 },
    { "struct T { int a; } }\n", "1:21: expected identifier or '('", 0 },
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
    // A declarator's group holds one declarator.
    { "int (*f, g);\n", "1:8: expected ')'", 0 },
    { "struct S { int (*f, g); };\n", "1:19: expected ')'", 0 },
    { "void m(void (*f (void *), int a));\n", "1:25: expected ')'", 0 },
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
    // A computed goto's `*` takes the whole expression after it, as clang's ParseGotoStatement does: a conditional, a
    // sum, a comma, an assignment, a `*` of its own and a group; and an operand must follow it.
    { "void f(int n) { void *p = &&a; goto *p ? &&a : &&b; goto *p + 0; goto *n, p; goto *p = p; goto **(void **)&p; goto *(p); a: b:; }\n", "", 0 },
    { "void f(void) { goto *; }\n", "1:22: expected expression", 0 },
    { "void f(void *p) { goto * const p; }\n", "1:26: expected expression", 0 },
    { "void f(void *p) { goto *p + ; }\n", "1:29: expected expression", 0 },
    // Any other operator after goto is an error: the vector path gives it the `*`'s role, and its checks send it to the
    // scalar parser.
    { "void f(int x) { goto &x; }\n", "1:22: expected identifier", 0 },
    { "void f(void) { a: goto &&a; }\n", "1:24: expected identifier", 0 },
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
    // sizeof of a compound literal, whose parentheses aren't sizeof's.
    { "int x = sizeof (int){1};\nint y = sizeof (int[]){1,2,3};\nint z = sizeof (int[]){1,2}[0];\n", "", 0 },
    // A declarator's name is in scope once its declarator is complete: the inner T's array size still sees the typedef,
    // its initializer and later declarators the object.
    { "typedef int T; void f(void) { int T[sizeof((T)0)]; }\n", "", 0 },
    { "typedef int T; void m(void) { int T = (T)1; }\n", "1:42: expected ';' at end of declaration", 0 },
    { "typedef int T; void k(int T, int u[sizeof(T)]);\n", "", 0 },
    // A definition's parameters stay in scope through the rest of its declarator and attributes, into its body, and
    // only there: after the body, the typedef is back.
    { "typedef int T;\nint (*h(int T))(int) { (void)T; return 0; }\n", "", 0 },
    { "int g(int x) __attribute__((unused)) { return x; }\n", "", 0 },
    { "typedef int T; int f(int T) { return T; } T g;\n", "", 0 },
    // Whether a body may follow is what the declarator makes its name first: a function through groups and later
    // suffixes, not a pointer to one, and only for a declaration's one declarator.
    { "void (f(void)) {}\nint (*g(void))[3] { return 0; }\n", "", 0 },
    { "int (q)(int x) { return x; }\nint *(r)(void) { return 0; }\n", "", 0 },
    { "int (*p)(int) { return 0; }\n", "1:14: expected ';' after top level declarator", 0 },
    // A function's typedef declaration cannot carry a body, even though the declarator is a function's.
    { "typedef int f(int x) { return x; }\n", "1:1: function definition declared 'typedef'", 0 },
    { "typedef int f(int x) { return x; }\nint g(void) { return f(1); }\n", "1:1: function definition declared 'typedef'", 0 },
    { "int typedef f(void) { return 0; }\n", "1:5: function definition declared 'typedef'", 0 },
    { "int f(int a), g(int b) { return b; }\n", "1:23: expected ';' after top level declarator", 0 },
    { "int (*f(void)) x\n", "1:16: expected function body after function declarator", 0 },
    // The end of an identifier is where its spelling ends: a UCN and a line splice make it longer than its bytes.
    { "int \\u00E9\n", "1:11: expected ';' after top level declarator", 0 },
    { "int ve\\\nctor\n", "2:5: expected ';' after top level declarator", 0 },
    // An enumeration constant's scope begins after its enumerator, value included.
    { "typedef int T; void f(void) { enum { T = sizeof((T)0) }; }\n", "", 0 },
    { "typedef int T; void f(void) { enum { A = sizeof(T), T, B = T }; int y = T; }\n", "", 0 },
    { "typedef int T; void f(void) { enum { A = sizeof(T), T = 2, B = T }; int y = T; }\n", "", 0 },
    { "typedef int T; void f(void) { enum { T = 1 } x; T y; }\n", "1:50: expected ';' after expression", 0 },
    // Selection and iteration statements are blocks, and so is each of their substatements; a labeled statement isn't.
    { "typedef int T; void f(void) { if (sizeof(enum { T = 1 })) { int y = T; } else { int z = T; } T x; }\n", "", 0 },
    { "typedef int T; void f(void) { while (sizeof(enum { T = 1 })) (void)T; switch (sizeof(enum { T = 1 })) { case T: ; } T x; }\n", "", 0 },
    { "typedef int T; void f(void) { do (void)sizeof(enum { T = 1 }); while (0); do {} while (sizeof(enum { T = 1 }) + T); T x; }\n", "", 0 },
    { "typedef int T; void f(void) { if (1) (void)sizeof(enum { T = 1 }); else (void)(T)1; for (;;) (void)sizeof(enum { T = 1 }); T x; }\n", "", 0 },
    { "typedef int T; void f(void) { do (void)sizeof(enum { T = 1 }); while (T); }\n", "1:71: unexpected type name 'T': expected expression", 0, true },
    { "typedef int T; void f(void) { L: (void)sizeof(enum { T = 1 }); T x; }\n", "1:65: expected ';' after expression", 0 },
    // A definition's parameters are in scope in its body, and not in the rest of its declarator or its attributes;
    // what its parameter list declares is.
    { "typedef int T; int (*f(int T))[sizeof((T)0)] { return 0; }\n", "", 0 },
    { "typedef int T; int (*g(int T))(T) { (void)T; return 0; }\n", "", 0 },
    { "typedef int T; int h(int T) __attribute__((aligned(sizeof((T)0)))) { return T; }\n", "", 0 },
    { "typedef int T; int f(enum { T = 1 } a) { return T + a; }\n", "", 0 },
    // __builtin_offsetof's member designator: members' names whatever the ordinary names mean, `.` and subscripts.
    { "typedef int T; struct S { int T; int m[2]; }; int x = __builtin_offsetof(struct S, T) + __builtin_offsetof(struct S, m[1]);\n", "", 0 },
    { "typedef int T; struct U { int T; }; struct S { struct U m[2]; }; int x = __builtin_offsetof(struct S, m[sizeof(T) - 3].T);\n", "", 0 },
    { "struct S { int a; }; int x = __builtin_offsetof(struct S, a + 1);\n", "1:61: expected ')'", 0 },
    { "struct S { int a; }; int x = __builtin_offsetof(struct S, (a));\n", "1:59: expected identifier", 0 },
    { "struct S { int a; }; int x = __builtin_offsetof(struct S, a->b);\n", "1:60: expected ')'", 0 },
    // An initializer's designators take `.`, never `->`.
    { "struct S { int a; } s = { ->a = 1 };\n", "1:27: expected expression", 0 },
    { "struct S { struct { int b; } a; } s = { .a->b = 1 };\n", "1:43: expected '=' or another designator", 0 },
    // Only a single array subscript allows GNU's missing '='; field and chained designators still require it.
    { "struct S { int a; } s = { .a 1 };\n", "1:30: expected '=' or another designator", 0 },
    { "int a[1][2] = { [0][1] 1 };\n", "1:24: expected '=' or another designator", 0 },
    { "struct S { int a[1]; } s = { .a[0] 1 };\n", "1:36: expected '=' or another designator", 0 },
    // K&R definitions: the name's own identifier list, through the declarator's groups, in C17 and before.
    { "int (f(x)) int x; { return x; }\nint (*g(y)) int y; { return 0; }\n", "", 0 },
    { "int f(x) int x; 5\n", "1:17: expected function body after function declarator", 0 },
    { "int f(x) int x; { return x; }\n", "1:7: unknown type name 'x'", "c23", true },
    { "int g(const x);\n", "1:13: unknown type name 'x'", "c23", true },
    // A K&R definition's parameter declarations declare its parameters, each once, with `register` at most, without
    // initializers, in a scope of their own, which the body ends; and its identifier list names each once.
    { "int f(a, a) { return 0; }\n", "1:10: redefinition of parameter 'a'", 0 },
    { "int f(a) int a; int b; { return a; }\n", "1:21: parameter named 'b' is missing", 0 },
    { "int f(a, b) int a, b, a; { return 0; }\n", "1:23: redefinition of parameter 'a'", 0 },
    { "int f(a) int a; int; { return 0; }\n", "1:17: declaration does not declare a parameter", 0 },
    { "int f(a) static int a; { return 0; }\n", "1:10: invalid storage class specifier in function declarator", 0 },
    { "int f(a, b) register int a; int b; { return a + b; }\n", "", 0 },
    { "int f(a) int a = 1, b; { return 0; }\n", "1:15: expected ';' at end of declaration", 0 },
    { "int f(a) int g(void) { return 0; }\n", "1:14: parameter named 'g' is missing", 0 },
    { "int f(a) int a __attribute__((unused)), b; { return 0; }\n", "1:41: parameter named 'b' is missing", 0 },
    { "typedef int B; int f(a) enum { B } a; { B x; return 0; }\n", "", 0 },
    { "int f(a, b) int a,\n{ return 0; }\n", "2:1: expected identifier or '('", 0 },
    { "int f(a) 5\n", "1:10: expected function body after function declarator", 0 },
    { "int f(a) int a; __extension__ int b; { return 0; }\n", "1:17: expected function body after function declarator", 0 },
    // The list's names are ordinary identifiers in the body, where a K&R list may name them again (which only clang's
    // semantic analysis rejects, there).
    { "int f(a) { int g(a, b); return 0; }\n", "", 0, true },
    // An attribute list where clang takes none: after a K&R identifier list, an operand or a designator's member.
    { "int f(a) [[deprecated]] int a; { return a; }\n", "1:10: an attribute list cannot appear here", 0 },
    { "int a[2]; int b = a[[0]];\n", "1:20: an attribute list cannot appear here", 0 },
    { "struct S { int a; }; int x = __builtin_offsetof(struct S, a [[x]]);\n", "1:61: an attribute list cannot appear here", 0 },
    // An identifier where a type would be is the name, with an implicit int, if what follows may follow a name, and
    // else an unknown type name.
    { "int f(int a, x);\n", "", "c89" },
    { "int f(x y);\n", "1:7: unknown type name 'x'", 0, true },
    { "int f(int a, x);\n", "1:14: type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int", 0, true },
    { "x y;\n", "1:1: unknown type name 'x'", 0, true },
    { "struct S { x *y; };\n", "1:12: unknown type name 'x'", 0, true },
    { "struct S { [[deprecated]] x; };\n", "1:27: type name requires a specifier or qualifier", 0 },
    { "x;\n", "1:1: a type specifier is required for all declarations", "c23", true },
    { "__extension__ x;\n", "1:15: type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int", 0, true },
    { "__extension__;\n", "", 0 },
    // A declarator without specifiers is an implicit int, as an identifier is; its group holds the name.
    { "(x);\n", "1:2: type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int", 0, true },
    { "(x);\n", "", "c89" },
    { "(\"x\");\n", "1:2: expected identifier or '('", 0 },
    { "static (x);\n", "1:9: type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int", 0, true },
    // Clang finishes the declarator before diagnosing its missing return/type specifier.
    { "f(A,\n12);\n", "2:1: expected identifier", 0 },
    { "typedef BOOLEAN\n(NTAPI *Fn)(int);\n", "2:2: unknown type name 'NTAPI'", 0, true },
    { "f(a)\nextern int x;\n", "2:1: invalid storage class specifier in function declarator", 0 },
    { "f(void);\n", "1:1: type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int", 0, true },
    { "f(a) int a; {return a;}\n", "1:1: type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int", 0, true },
    // The primary diagnostic belongs to the invocation; macro spelling remains available as an expansion note.
    { "#define TYPE Missing\nTYPE x;\n", "2:1: unknown type name 'Missing'", 0, true },
    { "#define TYPE Missing\n#define OUTER TYPE\nOUTER x;\n", "3:1: unknown type name 'Missing'", 0, true },
    // Missing separators follow the outer invocation's physical end, including substituted arguments and splices.
    { "#define END return 1\nint f(void){ END }\n", "2:17: expected ';' after return statement", 0 },
    { "#define RET(x) return x\nint f(void){ RET(2) }\n", "2:20: expected ';' after return statement", 0 },
    { "#define ID(x) x\nint f(void){ ID(return 3) }\n", "2:26: expected ';' after return statement", 0 },
    { "#define INNER return 4\n#define OUTER INNER\nint f(void){ OUTER }\n", "3:19: expected ';' after return statement", 0 },
    { "#define RET(x) return x\n#define ALIAS RET\nint f(void){ ALIAS(5) }\n", "3:22: expected ';' after return statement", 0 },
    { "#define RET(x) return x\n#define WRAP(x) RET(x)\nint f(void){ WRAP(6) }\n", "3:21: expected ';' after return statement", 0 },
    { "#define RET(x) return x\n#define ID(x) x\nint f(void){ ID(RET(7)) }\n", "3:24: expected ';' after return statement", 0 },
    { "#define RET(x) return x\n#define CAT(a,b) a##b\n#define VALUE8 8\nint f(void){ RET(CAT(VALUE,8)) }\n", "4:31: expected ';' after return statement", 0 },
    { "#define END return 1\nint f(void){ EN\\\nD }\n", "3:2: expected ';' after return statement", 0 },
    { "#define RET(x) return x\nint f(void){ RET\\\n(9) }\n", "3:4: expected ';' after return statement", 0 },
    // Inside an unfinished expansion, the next token remains the primary location, including argument spelling.
    { "#define M return 1 return 2;\nint f(void){ M }\n", "2:14: expected ';' after return statement", 0 },
    { "#define D int x=1 int y=2;\nint f(void){ D return 0; }\n", "2:14: expected ';' at end of declaration", 0 },
    { "#define E(a) a int y=2;\nint f(void){ E(int x=1) return 0; }\n", "2:14: expected ';' at end of declaration", 0 },
    { "#define A(x) x\nint f(void){ A(return 1 return 2;) }\n", "2:25: expected ';' after return statement", 0 },
    // A _Static_assert's message is string literals.
    { "_Static_assert(1, \"x\" \"y\" \"z\");\n", "", 0 },
    { "_Static_assert(1, \"x\" + 1);\n", "1:23: expected ')'", 0 },
    { "_Static_assert(1, \"x\", 2);\n", "1:22: expected ')'", 0 },
    // A type name where an operand would be.
    { "typedef int T; int y = sizeof T;\n", "1:30: expected parentheses around type name in sizeof expression", 0 },
    { "typedef int T; int y = 1 ? T : 2;\n", "1:28: unexpected type name 'T': expected expression", 0, true },
    // A K&R definition's identifier list is its name's, whatever suffixes its return type has.
    { "int (*f(x))(int) int x; { return 0; }\nint (*g(y))[3] int y; { return 0; }\n", "", 0 },
    // A tag's definition may end a _Generic association's type name.
    { "int x = _Generic(0, struct S { int a; }: 1, default: 0);\nint y = _Generic(0, enum E { A }: 1, default: 0);\n", "", "c17" },
    // A GNU attribute list's items may be empty, the last one too.
    { "int x __attribute__((unused,)); int f(int a __attribute__((unused,))); enum E { A __attribute__((deprecated,)) };\nint y __attribute__((,unused,,deprecated,));\n", "", 0 },
    // C23's attribute lists: names, perhaps in a scope and with arguments, separated by commas, any item empty; clang
    // expects the `,` or the `]` after the token before.
    { "[[]] int a; [[,]] int b; [[deprecated,,maybe_unused]] int c; [[gnu::aligned(8), ]] int d; [[__gnu__::__unused__, x ::y]] int e;\n", "", 0 },
    { "[[123]] int x;\n", "1:3: expected ']'", 0 },
    { "[[gnu::unused gnu::deprecated]] int x;\n", "1:14: expected ','", "c23" },
    { "[[x(1)   (2)]] int x;\n", "1:7: expected ','", 0 },
    { "[[x,  1]] int x;\n", "1:5: expected ']'", 0 },
    { "[[gnu::]] int x;\n", "1:8: expected identifier", 0 },
    // A label's name after `&&`, an asm operand's symbolic name and asm goto's labels are names, whatever the ordinary
    // names mean; asm goto's colons come to its labels, and they to one at least.
    { "typedef int T, L; void f(int x) { void *p = &&T; asm goto(\"\" : [T] \"=r\"(x) : [U] \"r\"(x) : : L); goto *p; T: L:; }\n", "", 0 },
    { "void f(int x) { asm(\"\" : [42] \"=r\"(x)); }\n", "1:27: expected identifier", 0 },
    { "void f(int x) { asm(\"\" : [x + 1] \"=r\"(x)); }\n", "1:29: expected ']'", 0 },
    { "void f(int x) { asm(\"\" : [a]); }\n", "1:29: expected string literal in 'asm'", 0 },
    { "void f(void) { asm goto(\"\" ::::); }\n", "1:32: expected identifier", 0 },
    { "void f(void) { asm goto(\"\" :::: a + b); a: b:; }\n", "1:35: expected ')'", 0 },
    { "void f(void) { asm goto(\"\" :); }\n", "1:29: expected ':'", 0 },
    { "void f(void) { asm goto(\"\" ::: \"memory\"); }\n", "1:40: expected ':'", 0 },
    // An asm statement's template, constraints and clobbers are string literals, its operands in parentheses and its
    // labels names, which nothing continues; an asm label is string literals, and a top-level asm block its template.
    { "int x asm(\"a\" \"b\"), y __asm__(\"c\") = 1; void f(int z) { asm(\"a\" \"b\" : \"=r\" \"m\"(z), [b] \"=r\"(z) : \"r\"(z) : \"memory\", \"cc\"); }\n", "", 0 },
    { "void f(void) { asm(\"\" + 1); }\n", "1:23: expected ')'", 0 },
    { "void f(void) { asm(\"a\", \"b\"); }\n", "1:23: expected ')'", 0 },
    { "void f(int x) { asm(\"\" : \"=r\"(x) + 1); }\n", "1:34: expected ')'", 0 },
    { "void f(void) { asm(\"\" ::: \"memory\" + 1); }\n", "1:36: expected ')'", 0 },
    { "void f(void) { asm(\"\" ::: \"a\",); }\n", "1:31: expected string literal in 'asm'", 0 },
    { "void f(int x) { asm(\"\" ::: x); }\n", "1:28: expected ')'", 0 },
    { "void f(int x) { asm(\"\" : \"=r\"(x),); }\n", "1:34: expected string literal in 'asm'", 0 },
    { "int x; asm(\"\" : \"=r\"(x));\n", "1:15: expected ')'", 0 },
    { "int x asm(\"y\" + 1);\n", "1:15: expected ')'", 0 },
    { "int x asm(y);\n", "1:11: expected string literal in 'asm'", 0 },
    // Local labels never bind ordinary names, asm qualifiers have exact identities, and only one `::` token is a scope.
    { "typedef int T; void f(void) { __label__ L,T; T x; goto L; L: T:; }\n", "", 0 },
    { "typedef int T; void f(void) { __label__ T; T x; T:; }\n", "", 0 },
    { "void f(void) { __label__ *L; }\n", "1:26: expected identifier", 0 },
    { "void f(void) { __label__ L = 3; }\n", "1:27: expected ';' at end of declaration", 0 },
    { "void f(void) { __label__ L,; }\n", "1:28: expected identifier", 0 },
    { "void f(void) { __label__; }\n", "1:25: expected identifier", 0 },
    { "void f(void) { __label__ int; }\n", "1:26: expected identifier", 0 },
    { "void f(void) { __label__ L(void); }\n", "1:27: expected ';' at end of declaration", 0 },
    { "void f(void) { __label__ L[3]; }\n", "1:27: expected ';' at end of declaration", 0 },
    { "void f(void) { __label__ L M; }\n", "1:27: expected ';' at end of declaration", 0 },
    { "__label__ L;\n", "1:1: expected identifier or '('", 0 },
    { "struct S { __label__ L; };\n", "1:12: type name requires a specifier or qualifier", 0 },
    { "void f(__label__ L);\n", "1:8: expected parameter declarator", 0 },
    { "void f(void) { if (1) __label__ L; }\n", "1:23: expected expression", 0 },
    { "int __label__ L;\n", "1:5: expected identifier or '('", 0 },
    { "void f(void) { asm const(\"\"); }\n", "1:20: expected 'volatile', 'inline', 'goto', or '('", 0, true },
    { "void f(void) { asm static(\"\"); }\n", "1:20: expected 'volatile', 'inline', 'goto', or '('", 0, true },
    { "void f(void) { asm restrict(\"\"); }\n", "1:20: expected 'volatile', 'inline', 'goto', or '('", 0, true },
    { "void f(void) { asm volatile __volatile__(\"\"); }\n", "1:29: duplicate asm qualifier 'volatile'", 0, true },
    { "void f(void) { asm inline __inline__(\"\"); }\n", "1:27: duplicate asm qualifier 'inline'", 0, true },
    { "void f(void) { asm goto goto(\"\" :::: L); L:; }\n", "1:25: duplicate asm qualifier 'goto'", 0, true },
    { "asm volatile(\"\");\n", "1:5: meaningless 'volatile' on asm outside function", 0, true },
    { "int x __asm__ inline(\"y\");\n", "1:15: meaningless 'inline' on asm outside function", 0, true },
    { "__asm__ const(\"\");\n", "1:9: expected '(' after 'asm'", 0 },
    { "void f(void) { asm volatile inline(\"\"); asm __inline __volatile__(\"\"); asm __inline__ __volatile(\"\"); }\n", "", 0 },
    { "#define C :\n[[gnu:C unused]] int x;\n", "2:6: expected ','", "c23" },
    { "#define C ::\n[[gnu C unused]] int x;\n", "", "c23" },
    { "#define C :\n[[gnu C: unused]] int x;\n", "2:6: expected ','", "c23" },
    // GNU label attributes need no empty statement before the next statement, declaration, block or closing brace.
    { "void f(void) { L: __attribute__((unused)) return; }\n", "", 0 },
    { "void f(void) { L: __attribute__((unused)) f(); }\n", "", 0 },
    { "void f(void) { L: __attribute__((cold)) __attribute__((unused)) { return; } }\n", "", 0 },
    { "void f(void) { L: __attribute__((unused)) }\n", "", 0 },
    { "void f(void) { L: __attribute__((unused)) int x; }\n", "", 0 },
    // Statement attributes have the same continuation at a block-item boundary and after a control head.
    { "void f(void) { __attribute__((noinline)) f(); }\n", "", 0 },
    { "void f(void) { __attribute__((always_inline)) f(); }\n", "", 0 },
    { "void f(void) { __attribute__((__noinline__)) f(); }\n", "", 0 },
    { "void f(void) { if (1) __attribute__((noinline)) f(); }\n", "", 0 },
    { "void f(void) { if (1) __attribute__((noinline)) { f(); } }\n", "", 0 },
    { "void f(void) { if (1) __attribute__((unused)) int x; }\n", "1:47: expected expression", 0 },
    { "void f(void) { if (1) __attribute__((noinline)) }\n", "1:49: expected statement", 0 },
    { "void f(void) { __attribute__((noinline)) }\n", "1:42: expected statement", 0 },
    { "void f(int x) { switch (x) { case 0: __attribute__((always_inline)) } }\n", "1:69: expected statement", 0 },
    { "void f(int x) { switch (x) { default: __attribute__((__noinline__)) } }\n", "1:69: expected statement", 0 },
    { "void f(void) { __attribute__((__always_inline__, noinline)) }\n", "1:61: expected statement", 0 },
    { "void f(void) { __attribute__((unknown)) x; }\n", "1:41: type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int", 0, true },
    { "void f(void) { __attribute__((unused)) __extension__ int x; }\n", "1:40: expected identifier or '('", 0 },
    { "void f(void) { L: __attribute__((unused)) __extension__ int x; }\n", "1:57: expected expression", 0 },
    // Attribute applicability belongs to semantic checking; the parser retains even an invalid statement attribute.
    { "void f(void) { __attribute__((unused)); }\n", "", 0, true },
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
            const bool kind_differs = theirs.rejected && theirs.parse_issue == snippet->semantic;
            const bool grammatical = snippet->semantic && snippet->error[0] == 0;
            if (!good || (grammatical ? !theirs.rejected || theirs.parse_issue : strcmp(expected, snippet->error) != 0 || kind_differs))
            {
                failures += 1;
                printf("snippet %u: clang gives \"%s\"%s, the table \"%s\"%s\n    %s", i, expected,
                       kind_differs ? (theirs.parse_issue ? " (a parse issue)" : " (not a parse issue)") : "", snippet->error,
                       grammatical ? " (and a semantic issue from clang)" : "", snippet->source);
            }
        }
        remove(path);
    }
    printf("snippets: %u, %u failures%s\n", count, failures, clang != 0 ? " (with clang)" : "");
    return failures;
}

// Trees checked by hand, which both paths must build: each fixture's parent of every token (-1 for the root). They
// are the cases #127 names: `f(a ? b, c : d, e)` calls f with two arguments, `a ? (b, c) : d` and `e`; `(T) * p` is a
// cast of `*p` where T names a type and a product where it doesn't; an if-statement with an else (c_parser.c's
// example); and the declarator `(*a[3])(double)`, whose spine from the name out is array, pointer, function. Then
// #431's review's computed gotos: each `*` the goto's child and the whole expression after it the `*`'s (the `:` of
// `p ? p : p`, the `+` of `p + 0` and the comma of `p, p`), which the vector path builds without its fallback.
typedef struct ParseTestFixture ParseTestFixture;
struct ParseTestFixture
{
    const char* source;
    uint32_t count;
    int32_t parents[40];
    // Whether the vector path must keep every top-level declaration (none for the scalar parser's fallback).
    bool vector;
};

static const ParseTestFixture parse_test_fixtures[] =
{
    { "int r = f(a ? b, c : d, e);\n", 16, { 15, 2, 0, 14, 14, 10, 10, 8, 10, 8, 12, 10, 14, 12, 2, -1 } },
    { "typedef int T;\nint x = (T) * p;\nint y = (U) * p;\n", 22,
      { 3, 0, 1, -1, 12, 6, 4, 9, 9, 6, 9, 10, -1, 21, 15, 13, 18, 18, 19, 15, 19, -1 } },
    { "void g(int a, int b, int c, int x, int y, int z) { if (a) x = b * c; else y = -z; }\n", 39,
      { 38, 20, 20, 5, 3, 8, 5, 6, 11, 8, 9, 14, 11, 12, 17, 14, 15, 20, 17, 18, 0, 38, 25, 25, 25, 38, 27, 31, 29, 27, 29, 25, 25, 34, 37, 34, 35,
        32, -1 } },
    { "int (*a[3])(double);\n", 12, { 11, 7, 7, 6, 6, 6, 2, 10, 10, 10, 0, -1 } },
    { "void f(void *p) { goto *p ? p : p; goto *p + 0; goto *p, p; }\n", 29,
      { 28, 6, 6, 6, 3, 4, 0, 28, 15, 8, 13, 13, 13, 9, 13, 28, 21, 16, 19, 17, 19, 28, 27, 22, 25, 23, 25, 28, -1 }, true },
};

// Each fixture through both paths, against its parents.
static uint32_t parse_test_check_fixtures(const char* directory)
{
    uint32_t failures = 0;
    const uint32_t count = (uint32_t)(sizeof(parse_test_fixtures) / sizeof(parse_test_fixtures[0]));
    char path[4096];
    for (uint32_t i = 0; i < count; i += 1)
    {
        const ParseTestFixture* fixture = &parse_test_fixtures[i];
        snprintf(path, sizeof(path), "%s/fixture_%u.c", directory, i);
        const bool written = parse_test_write(path, fixture->source, strlen(fixture->source));
        const ParseTestSnippet snippet = { .source = fixture->source, .error = "", .std = 0 };
        const PPOptions options = parse_test_snippet_options(&snippet);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            static InternTable table;
            static Preprocessor pp;
            static Parser parser;
            const ParseOptions parse_options = { .scalar = scalar != 0 };
            bool parsed = written && intern_initialize(&table) && pp_initialize(&pp, &options, &table) && parse_initialize(&parser, &parse_options, options.language) &&
                          pp_run(&pp, path) && parse_translation_unit(&parser, &pp) && parser.tokens.count == fixture->count;
            uint32_t first = BUSTER_PARSE_NONE;
            for (uint32_t t = 0; parsed && t < fixture->count && first == BUSTER_PARSE_NONE; t += 1)
            {
                const uint32_t expected = fixture->parents[t] < 0 ? BUSTER_PARSE_ROOT : (uint32_t)fixture->parents[t];
                first = parser.tree.parents[t] == expected ? BUSTER_PARSE_NONE : t;
            }
            if (!parsed || first != BUSTER_PARSE_NONE)
            {
                failures += 1;
                printf("fixture %u (%s path): %s%s%u\n    %s", i, scalar ? "scalar" : "vector", parsed ? "" : "doesn't parse to its tokens",
                       parsed ? "the parent differs at token " : "", parsed ? first : 0, fixture->source);
            }
            else if (fixture->vector && scalar == 0 && parser.statistics.fallback_items != 0)
            {
                failures += 1;
                printf("fixture %u: the vector path's checks sent %llu top-level declarations to the fallback\n    %s", i,
                       (unsigned long long)parser.statistics.fallback_items, fixture->source);
            }
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&table);
        }
        remove(path);
    }
    printf("fixtures: %u trees checked by hand, %u failures\n", count, failures);
    return failures;
}

// The oracle itself: clang that fails before it reads the file (an option it doesn't know) is no answer, and an error
// in a header the file includes rejects the file.
static uint32_t parse_test_check_oracle(const char* directory, const char* clang)
{
    uint32_t failures = 0;
    char path[4096];
    char header[4096];
    snprintf(path, sizeof(path), "%s/oracle.c", directory);
    snprintf(header, sizeof(header), "%s/oracle.h", directory);
    const bool written = parse_test_write(path, "#include \"oracle.h\"\nint y;\n", 27) && parse_test_write(header, "int x\n", 6);
    char* paths[] = { path };
    ParseTestClang result;
    const char* unknown[] = { "--buster-test-unknown-option" };
    const bool refused = written && !parse_test_clang(clang, unknown, 1, paths, 1, &result);
    const bool included = written && parse_test_clang(clang, 0, 0, paths, 1, &result) && result.rejected;
    failures += !refused;
    failures += !included;
    if (!refused || !included)
    {
        printf("oracle: %s%s\n", refused ? "" : "a clang that failed before reading its input counted as an answer; ",
               included ? "" : "an error in an included header didn't reject its file");
    }
    remove(path);
    remove(header);
    printf("oracle: %u failures\n", failures);
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// The trees

// How many more reports of differences the checks print: the mutations' runs can find many, and the first ones say
// enough.
static uint32_t parse_test_report_budget = 200;

static void parse_test_report(const char* format, ...) __attribute__((format(printf, 1, 2)));

static void parse_test_report(const char* format, ...)
{
    if (parse_test_report_budget != 0)
    {
        parse_test_report_budget -= 1;
        va_list arguments;
        va_start(arguments, format);
        vprintf(format, arguments);
        va_end(arguments);
    }
}

// The tree's invariants: every parent is another token or the root, no deeper than its child; every opener's parent is
// a closer after it; and following parents always reaches the root (no cycle). `marks` has a byte per token. Returns
// the first token that breaks one, or BUSTER_PARSE_NONE.
static uint32_t parse_test_check_tree(const ParseTree* tree, uint32_t count, uint8_t* marks)
{
    uint32_t result = BUSTER_PARSE_NONE;
    for (uint32_t i = 0; i < count && result == BUSTER_PARSE_NONE; i += 1)
    {
        const uint32_t parent = tree->parents[i];
        const bool valid = parent == BUSTER_PARSE_ROOT || (parent < count && parent != i && tree->depths[parent] <= tree->depths[i]);
        const bool opener = parse_role_rules[tree->roles[i]] == PARSE_RULE_OPENER;
        if (!valid || (opener && (parent == BUSTER_PARSE_ROOT || parent < i)))
        {
            result = i;
        }
    }
    // 0: not visited; 1: on the current path; 2: reaches the root.
    memset(marks, 0, count);
    for (uint32_t i = 0; i < count && result == BUSTER_PARSE_NONE; i += 1)
    {
        uint32_t node = i;
        while (node != BUSTER_PARSE_ROOT && marks[node] == 0)
        {
            marks[node] = 1;
            node = tree->parents[node];
        }
        if (node != BUSTER_PARSE_ROOT && marks[node] == 1)
        {
            result = node;
        }
        node = i;
        while (node != BUSTER_PARSE_ROOT && marks[node] == 1)
        {
            marks[node] = 2;
            node = tree->parents[node];
        }
    }
    return result;
}

// The postorder of a depth-first walk with an explicit stack, children in source order: the reference for a postorder's
// `written` tokens, and with `sizes` (else 0), for each position's subtree size. The walk also checks the round trip:
// each node's own token comes between its children before it and those after it, so an in-order walk must give the
// tokens back in source order. Returns the first position where they differ (or count for the round trip), or
// BUSTER_PARSE_NONE; *expected gets the walk's token there.
static uint32_t parse_test_check_postorder(const uint32_t* parents, uint32_t count, const uint32_t* postorder, uint32_t written, const uint32_t* sizes, Arena* scratch,
                                           uint32_t* expected)
{
    uint32_t result = BUSTER_PARSE_NONE;
    *expected = BUSTER_PARSE_NONE;
    const uint64_t mark = scratch->length;
    const uint64_t slots = (uint64_t)count + 1;
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* cursors = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* children = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* nodes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* next = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* firsts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    if (offsets == 0 || cursors == 0 || children == 0 || nodes == 0 || next == 0 || firsts == 0)
    {
        result = 0;
    }
    else
    {
        memset(offsets, 0, slots * sizeof(uint32_t));
        for (uint32_t i = 0; i < count; i += 1)
        {
            offsets[parents[i] != BUSTER_PARSE_NONE ? parents[i] + 1 : 0] += parents[i] != BUSTER_PARSE_NONE;
        }
        for (uint32_t i = 1; i <= count; i += 1)
        {
            offsets[i] += offsets[i - 1];
        }
        memcpy(cursors, offsets, slots * sizeof(uint32_t));
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (parents[i] != BUSTER_PARSE_NONE)
            {
                children[cursors[parents[i]]] = i;
                cursors[parents[i]] += 1;
            }
        }
        uint32_t position = 0;
        // The in-order walk's next token: each node comes before its first child after it.
        uint32_t source = 0;
        bool round_trip = true;
        for (uint32_t root = 0; root < count; root += 1)
        {
            uint32_t depth = 0;
            if (parents[root] == BUSTER_PARSE_NONE)
            {
                nodes[0] = root;
                next[0] = offsets[root];
                firsts[0] = position;
                depth = 1;
            }
            while (depth != 0)
            {
                const uint32_t top = nodes[depth - 1];
                if (next[depth - 1] < offsets[top + 1])
                {
                    const uint32_t child = children[next[depth - 1]];
                    // Before the first child after the node, the node itself in order.
                    if (child > top && (next[depth - 1] == offsets[top] || children[next[depth - 1] - 1] < top))
                    {
                        round_trip &= source == top;
                        source += 1;
                    }
                    next[depth - 1] += 1;
                    nodes[depth] = child;
                    next[depth] = offsets[child];
                    firsts[depth] = position;
                    depth += 1;
                }
                else
                {
                    // A node with no child after it comes after all of them.
                    if (offsets[top] == offsets[top + 1] || children[offsets[top + 1] - 1] < top)
                    {
                        round_trip &= source == top;
                        source += 1;
                    }
                    const uint32_t size = position - firsts[depth - 1] + 1;
                    if (result == BUSTER_PARSE_NONE && (position >= written || postorder[position] != top || (sizes != 0 && sizes[position] != size)))
                    {
                        result = position;
                        *expected = top;
                    }
                    position += 1;
                    depth -= 1;
                }
            }
        }
        // A cycle leaves tokens unwalked.
        result = result == BUSTER_PARSE_NONE && (position != count || written != count) ? position : result;
        result = result == BUSTER_PARSE_NONE && (!round_trip || source != count) ? count : result;
    }
    arena_pop_to(scratch, mark);
    return result;
}

// The check of the postorders on trees of its own: `a = b * c;` as the parser builds it passes, with its sizes; the
// same with a parent that makes a subtree two runs, and with a cycle, fails.
static uint32_t parse_test_check_postorder_self(Arena* scratch)
{
    uint32_t failures = 0;
    // a = b * c ;  ->  ; <- = <- (a, * <- (b, c))
    const uint32_t good[] = { 1, 5, 3, 1, 3, BUSTER_PARSE_NONE };
    const uint32_t postorder[] = { 0, 2, 4, 3, 1, 5 };
    const uint32_t sizes[] = { 1, 1, 1, 3, 5, 6 };
    uint32_t expected;
    failures += parse_test_check_postorder(good, 6, postorder, 6, sizes, scratch, &expected) != BUSTER_PARSE_NONE;
    const uint32_t wrong_sizes[] = { 1, 1, 1, 2, 5, 6 };
    failures += parse_test_check_postorder(good, 6, postorder, 6, wrong_sizes, scratch, &expected) == BUSTER_PARSE_NONE;
    // b's parent the `;`: the `*`'s subtree is c alone and b sits between the `=`'s children.
    const uint32_t split[] = { 1, 5, 5, 1, 3, BUSTER_PARSE_NONE };
    uint32_t last_children[7];
    uint32_t walk[7];
    const uint32_t written = parse_postorder(split, 6, last_children, walk);
    failures += parse_test_check_postorder(split, 6, walk, written, 0, scratch, &expected) == BUSTER_PARSE_NONE;
    const uint32_t cycle[] = { 1, 0, BUSTER_PARSE_NONE };
    failures += parse_test_check_postorder(cycle, 3, postorder, 0, 0, scratch, &expected) == BUSTER_PARSE_NONE;
    if (failures != 0)
    {
        printf("the postorders' check fails on its own trees\n");
    }
    return failures;
}

// The role the paths can tell: a function declarator's parentheses look like a call's (both are postfix groups after
// an operand, with the same tree), so they compare as one.
static uint8_t parse_test_canonical_role(uint8_t role)
{
    uint8_t result = role;
    if (role == PARSE_ROLE_OPEN_FUNCTION)
    {
        result = PARSE_ROLE_OPEN_CALL;
    }
    else if (role == PARSE_ROLE_CLOSE_FUNCTION)
    {
        result = PARSE_ROLE_CLOSE_CALL;
    }
    return result;
}

// The tokens around `token`, the token itself marked, as one report.
static void parse_test_print_context(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    char text[1024];
    uint32_t length = 0;
    const uint32_t begin = token > 12 ? token - 12 : 0;
    const uint32_t end = token + 12 < parser->tokens.count ? token + 12 : parser->tokens.count;
    for (uint32_t i = begin; i < end && length < sizeof(text) - 64; i += 1)
    {
        const uint32_t start = pp->output.locations[parser->tokens.sources[i]];
        const uint32_t stop = parse_token_end(parser, pp, i);
        const uint8_t* bytes = pp_location_bytes(pp, start);
        const uint32_t size = bytes != 0 && stop > start ? (stop - start < 40 ? stop - start : 40) : 0;
        length += (uint32_t)snprintf(text + length, sizeof(text) - length, i == token ? " >>%.*s<<" : " %.*s", (int)size, (const char*)bytes);
    }
    parse_test_report("   %s%s\n", text, token >= parser->tokens.count ? " >><end><<" : "");
}

// The postorders of a tree, the walk's and the parser's own (with its subtree sizes), against a depth-first walk.
static uint32_t parse_test_postorders(const Parser* parser, Preprocessor* pp, const ParseTree* tree, uint32_t count, const char* name, Arena* scratch)
{
    uint32_t failures = 0;
    const uint64_t mark = scratch->length;
    const uint64_t slots = (uint64_t)count + 1;
    uint32_t* last_children = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* postorder = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    if (last_children != 0 && postorder != 0)
    {
        const uint32_t written = parse_postorder(tree->parents, count, last_children, postorder);
        uint32_t expected = BUSTER_PARSE_NONE;
        const uint32_t walk = parse_test_check_postorder(tree->parents, count, postorder, written, 0, scratch, &expected);
        const uint32_t found = walk < written ? postorder[walk] : BUSTER_PARSE_NONE;
        uint32_t sorted_expected = BUSTER_PARSE_NONE;
        const uint32_t sorted = parse_test_check_postorder(tree->parents, count, parser->postorder, count, parser->sizes, scratch, &sorted_expected);
        if (walk != BUSTER_PARSE_NONE || sorted != BUSTER_PARSE_NONE)
        {
            failures += 1;
            parse_test_report("%s: the postorder differs from a depth-first walk's at position %d (walk) and %d (the parser's, with sizes): token %d where the walk has "
                              "%d%s\n",
                              name, (int)walk, (int)sorted, (int)found, (int)expected, sorted == count ? " (the in-order walk isn't the source order)" : "");
            if (expected != BUSTER_PARSE_NONE)
            {
                parse_test_print_context(parser, pp, expected);
                for (uint32_t t = expected > 6 ? expected - 6 : 0; t < expected + 40 && t < count; t += 1)
                {
                    parse_test_report("    %u %s depth %u parent %d\n", t, parse_role_names[tree->roles[t]], tree->depths[t], (int)tree->parents[t]);
                }
            }
        }
    }
    else
    {
        failures += 1;
    }
    arena_pop_to(scratch, mark);
    return failures;
}

// The vector path's tree against the scalar parser's, both of one unit: the same parents, depths and (canonical) roles
// on every token, the tree's invariants, and its postorders. Returns the number of failures.
static uint32_t parse_test_compare_trees(const Parser* vector, const Parser* scalar, Preprocessor* pp, const char* name, Arena* scratch)
{
    uint32_t failures = 0;
    const uint32_t count = scalar->tokens.count;
    uint32_t first = BUSTER_PARSE_NONE;
    for (uint32_t i = 0; i < count && first == BUSTER_PARSE_NONE; i += 1)
    {
        const bool same = vector->tree.parents[i] == scalar->tree.parents[i] && vector->tree.depths[i] == scalar->tree.depths[i] &&
                          parse_test_canonical_role(vector->tree.roles[i]) == parse_test_canonical_role(scalar->tree.roles[i]);
        first = same ? BUSTER_PARSE_NONE : i;
    }
    if (first != BUSTER_PARSE_NONE)
    {
        failures += 1;
        parse_test_report("%s: the vector path's tree differs at token %u: role %s, depth %u, parent %d against the scalar parser's %s, %u, %d\n", name, first,
               parse_role_names[vector->tree.roles[first]], vector->tree.depths[first], (int)vector->tree.parents[first], parse_role_names[scalar->tree.roles[first]],
               scalar->tree.depths[first], (int)scalar->tree.parents[first]);
        parse_test_print_context(scalar, pp, first);
    }
    const uint64_t mark = scratch->length;
    uint8_t* marks = arena_push(scratch, (uint64_t)count + 1, 1);
    const uint32_t broken = marks != 0 ? parse_test_check_tree(&scalar->tree, count, marks) : 0;
    arena_pop_to(scratch, mark);
    if (broken != BUSTER_PARSE_NONE)
    {
        failures += 1;
        parse_test_report("%s: the tree's invariants break at token %u\n", name, broken);
        parse_test_print_context(scalar, pp, broken);
    }
    failures += parse_test_postorders(scalar, pp, &scalar->tree, count, name, scratch);
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// The kernels

// An arena of `capacity` bytes after a page and before 4 GB and its own size of address space that may not be touched:
// the kernels' checks put every array there, so that a load past an array's padding faults, and so does one whose
// 32-bit index wrapped below 0 (`i - 1` at token 0 is 2^32 - 1 elements on). parse_test_release_guarded releases it.
static Arena parse_test_guarded_arena(uint64_t capacity)
{
    const uint64_t page = 4096;
    const uint64_t bytes = (capacity + page - 1) & ~(page - 1);
    uint8_t* mapping = mmap(0, page + bytes + (4ull << 30) + bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    Arena result = { 0 };
    if (mapping != MAP_FAILED && mprotect(mapping + page, bytes, PROT_READ | PROT_WRITE) == 0)
    {
        result.base = mapping + page;
        result.capacity = bytes;
    }
    else if (mapping != MAP_FAILED)
    {
        munmap(mapping, page + bytes + (4ull << 30) + bytes);
    }
    return result;
}

static void parse_test_release_guarded(Arena* arena)
{
    if (arena->base != 0)
    {
        munmap(arena->base - 4096, 4096 + arena->capacity + (4ull << 30) + arena->capacity);
    }
    *arena = (Arena){ 0 };
}

// What the kernel checks did over every unit: blocks K2's rounds left to the twin's loop from random roles, K4's blocks
// whose depths span more than byte lanes hold, and units K5 left to its twin (whose stack alone decides them).
typedef struct ParseTestKernelTally ParseTestKernelTally;
struct ParseTestKernelTally
{
    uint64_t units;
    uint64_t random_twins;
    uint64_t wide_blocks;
    uint64_t statement_twins;
    uint64_t check_twins;
    uint64_t fallback_items;
};

// Each kernel's output from the vector kernel, kept for the comparison with its twin's.
typedef struct ParseTestCopies ParseTestCopies;
struct ParseTestCopies
{
    uint32_t* depths;
    uint32_t* enclosing;
    ParseBracketBlock* bracket_blocks;
    uint8_t* vkinds;
    uint8_t* classified_flags;
    uint8_t* roles;
    uint8_t* flags;
    uint8_t* typed;
    uint8_t* twin_typed;
    uint32_t* node_depths;
    uint32_t* parents;
    uint8_t* starts;
    uint8_t* twin_flags;
    uint8_t* random_roles;
    uint32_t* secondary;
    ParseStatementContext* oracle_contexts;
    ParseStatement* statements;
    ParseDiagnostic* diagnostics;
};

// The first token in [0, count) where two byte columns differ, or BUSTER_PARSE_NONE.
static uint32_t parse_test_first_byte(const uint8_t* a, const uint8_t* b, uint32_t count)
{
    uint32_t result = BUSTER_PARSE_NONE;
    for (uint32_t i = 0; i < count && result == BUSTER_PARSE_NONE; i += 1)
    {
        result = a[i] != b[i] ? i : BUSTER_PARSE_NONE;
    }
    return result;
}

// The same for 32-bit columns.
static uint32_t parse_test_first_word(const uint32_t* a, const uint32_t* b, uint32_t count)
{
    uint32_t result = BUSTER_PARSE_NONE;
    for (uint32_t i = 0; i < count && result == BUSTER_PARSE_NONE; i += 1)
    {
        result = a[i] != b[i] ? i : BUSTER_PARSE_NONE;
    }
    return result;
}

// The first token whose block's statement contexts differ between two sets of them, or BUSTER_PARSE_NONE.
static uint32_t parse_test_first_context(const ParseStatementContext* a, const ParseStatementContext* b, uint32_t count)
{
    uint32_t result = BUSTER_PARSE_NONE;
    for (uint32_t block = 0; block < count && result == BUSTER_PARSE_NONE; block += 64)
    {
        const ParseStatementContext* x = &a[block / 64];
        const ParseStatementContext* y = &b[block / 64];
        const uint64_t differ = (x->blocks ^ y->blocks) | (x->ifs ^ y->ifs) | (x->whiles ^ y->whiles);
        result = differ != 0 ? block + (uint32_t)__builtin_ctzll(differ) : BUSTER_PARSE_NONE;
    }
    return result;
}

// Reports one kernel's difference at `token` (if any) and returns 1, or returns 0.
static uint32_t parse_test_kernel_differs(const Parser* parser, Preprocessor* pp, const char* name, const char* kernel, uint32_t token)
{
    uint32_t result = 0;
    if (token != BUSTER_PARSE_NONE)
    {
        parse_test_report("%s: %s differs at token %u\n", name, kernel, token);
        parse_test_print_context(parser, pp, token);
        result = 1;
    }
    return result;
}

// Every role/kind refinement, with either typed state and with both in one block, against the scalar classifier.
// Unused virtual kinds preserve the original role and its typed variant.
static uint32_t parse_test_check_k6_tables(void)
{
    parse_tables_initialize();
    uint32_t failures = 0;
    for (uint32_t r = 0; r < PARSE_ROLE_COUNT; r += 1)
    {
        const __m512i roles = _mm512_set1_epi8((char)r);
        const __m512i role_bits = parse_table8(parse_check_vector.role_bits, roles);
        for (uint32_t first = 0; first < 128; first += 64)
        {
            uint8_t kinds[64];
            for (uint32_t lane = 0; lane < 64; lane += 1)
            {
                kinds[lane] = (uint8_t)(first + lane);
            }
            for (uint32_t state = 0; state < 3; state += 1)
            {
                const __mmask64 typed = state == 0 ? 0 : state == 1 ? ~(__mmask64)0 : 0xaaaaaaaaaaaaaaaaull;
                uint8_t expected[64];
                for (uint32_t lane = 0; lane < 64; lane += 1)
                {
                    const bool is_typed = ((typed >> lane) & 1) != 0;
                    uint8_t value = (uint8_t)r;
                    if (kinds[lane] < BUSTER_PARSE_VKINDS)
                    {
                        value = parse_check_class((uint8_t)r, kinds[lane], 0, is_typed);
                    }
                    else if (is_typed)
                    {
                        value = r == PARSE_ROLE_SPECIFIER ? PARSE_CLASS_TYPED : r == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ? PARSE_CLASS_TYPED_ATTRIBUTE : (uint8_t)r;
                    }
                    expected[lane] = value;
                }
                const __m512i actual = parse_check_classes_avx512(roles, _mm512_loadu_si512(kinds), role_bits, typed);
                const uint64_t different = _mm512_cmpneq_epi8_mask(actual, _mm512_loadu_si512(expected));
                if (different != 0)
                {
                    parse_test_report("K6 refinement differs: role %u, kind %u, typed state %u\n", r, first + (uint32_t)__builtin_ctzll(different), state);
                    failures += 1;
                }
            }
        }
    }
    printf("K6 compact tables: %u failures\n", failures);
    return failures;
}

// K5 in AVX-512 against the twin, from the parents K4 left in kernels->parents: the same verdict, and when it is true,
// the same parents. kernels->parents then holds the twin's. A unit the vector kernel leaves to the twin counts in the
// tally. Returns 1 if they differ.
static uint32_t parse_test_check_statements(const Parser* parser, Preprocessor* pp, const char* name, ParseKernels* kernels, ParseVectorBuffers* buffers,
                                            ParseTestCopies* copies, uint32_t count, ParseTestKernelTally* tally)
{
    uint32_t result = 0;
    parse_statement_contexts(kernels, count, copies->oracle_contexts);
    uint32_t* parents = kernels->parents;
    memcpy(copies->parents, parents, (uint64_t)count * sizeof(uint32_t));
    const bool twin_ok = parse_statements(kernels, count, copies->statements);
    kernels->parents = copies->parents;
    const ParseStatementsVerdict verdict = parse_statements_avx512(kernels, count, copies->oracle_contexts, buffers->statement_previous, buffers->statement_owners,
                                                                   buffers->statement_levels, &buffers->statement_counts);
    kernels->parents = parents;
    tally->statement_twins += verdict == PARSE_STATEMENTS_TWIN;
    const uint32_t first = verdict == PARSE_STATEMENTS_OK && twin_ok ? parse_test_first_word(parents, copies->parents, count) : BUSTER_PARSE_NONE;
    if (verdict != PARSE_STATEMENTS_TWIN && (twin_ok != (verdict == PARSE_STATEMENTS_OK) || first != BUSTER_PARSE_NONE))
    {
        parse_test_report("%s: K5 differs: twin %s, vector %s\n", name, twin_ok ? "true" : "false", verdict == PARSE_STATEMENTS_OK ? "true" : "false");
        if (first != BUSTER_PARSE_NONE)
        {
            parse_test_report("  at token %u: twin %d, vector %d\n", first, (int)parents[first], (int)copies->parents[first]);
            parse_test_print_context(parser, pp, first);
        }
        result = 1;
    }
    return result;
}

// K2 from other starts than its kinds' guesses: from random roles, its rounds must end at the twin's roles, flags and
// statement contexts (the check that ends them may not stop early), and so must they with every block in the twin's
// loop (which, from the guesses, the corpora send no block to). kernels->roles and kernels->flags are the twin's on
// entry, and again on return.
static uint32_t parse_test_check_role_starts(const Parser* parser, Preprocessor* pp, const char* name, ParseKernels* kernels, ParseVectorBuffers* buffers,
                                             ParseTestCopies* copies, uint32_t count, ParseTestKernelTally* tally)
{
    uint32_t result = 0;
    memcpy(copies->starts, kernels->roles, count);
    memcpy(copies->twin_flags, kernels->flags, count);
    uint64_t state = 0x9E3779B97F4A7C15ull ^ count;
    for (uint32_t trial = 0; trial < 3; trial += 1)
    {
        // Two trials from random roles, then every block through the twin's loop.
        const bool twin_only = trial == 2;
        for (uint32_t i = 0; i < count + 64; i += 1)
        {
            copies->random_roles[i] = (uint8_t)(parse_test_random(&state) % PARSE_ROLE_COUNT);
        }
        memcpy(kernels->flags, copies->classified_flags, count);
        uint64_t rounds[BUSTER_PARSE_ROLE_ROUNDS + 1] = { 0 };
        parse_roles_starting_avx512(kernels, count, copies->typed, &buffers->groups, buffers->contexts, rounds, twin_only ? 0 : copies->random_roles, twin_only);
        tally->random_twins += twin_only ? 0 : rounds[0];
        uint32_t first = BUSTER_PARSE_NONE;
        for (uint32_t i = 0; i < count && first == BUSTER_PARSE_NONE; i += 1)
        {
            first = kernels->roles[i] != copies->starts[i] || kernels->flags[i] != copies->twin_flags[i] ? i : BUSTER_PARSE_NONE;
        }
        if (first == BUSTER_PARSE_NONE && !kernels->unbalanced)
        {
            first = parse_test_first_context(buffers->contexts, copies->oracle_contexts, count);
        }
        if (first == BUSTER_PARSE_NONE)
        {
            first = parse_test_first_byte(copies->typed, copies->twin_typed, count);
        }
        result += parse_test_kernel_differs(parser, pp, name, twin_only ? "K2 with every block in the twin's loop" : "K2 from random roles", first);
    }
    memcpy(kernels->roles, copies->starts, count);
    memcpy(kernels->flags, copies->twin_flags, count);
    return result;
}

// Each kernel against its scalar twin on the unit `parser` read (its tokens), each from the twins' outputs before it,
// as the spike checked them: K1 (depths, enclosing openers and every block's view), K3's classification, K2 (roles,
// flags, typed states and statement contexts, also from other starts), K1's second part and the roles after colons, K4
// with the stack's top in registers and with windows of 1, 3 and 8 lanes (whose stacks go through memory), and K5.
// Then the twins together, with the fallback (parse_twins), whose tree must be `scalar`'s when the unit is valid.
// Returns the number of differences.
static uint32_t parse_test_check_kernels(Parser* parser, const Parser* scalar, Preprocessor* pp, const char* name, ParseTestKernelTally* tally)
{
    uint32_t result = 0;
    const uint32_t count = parser->tokens.count;
    const uint8_t* kinds = parser->tokens.kinds;
    const uint32_t* names = parser->tokens.names;
    const uint64_t slots = (uint64_t)count + 2 * BUSTER_PARSE_TOKEN_PADDING;
    Arena arena = parse_test_guarded_arena(slots * 1024 + (uint64_t)parser->meanings.capacity + (64ull << 20));
    ParseKernels kernels_storage;
    ParseKernels* kernels = &kernels_storage;
    ParseVectorBuffers buffers;
    ParseTestCopies copies = { 0 };
    bool reserved = arena.capacity != 0 && parse_kernels_reserve(kernels, &arena, count, parser->meanings.capacity) && parse_vector_buffers_reserve(&buffers, &arena, count);
    if (reserved)
    {
        copies = (ParseTestCopies){
            .depths = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, slots),
            .enclosing = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, slots),
            .bracket_blocks = BUSTER_ARENA_PUSH_ARRAY(&arena, ParseBracketBlock, slots / 64 + 2),
            .vkinds = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots),
            .classified_flags = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots),
            .roles = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots),
            .flags = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots),
            .typed = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots),
            .twin_typed = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots),
            .node_depths = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, slots),
            .parents = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, slots),
            .starts = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots),
            .twin_flags = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots),
            .random_roles = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots),
            .secondary = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, slots),
            .oracle_contexts = BUSTER_ARENA_PUSH_ARRAY(&arena, ParseStatementContext, slots / 64 + 2),
            .statements = BUSTER_ARENA_PUSH_ARRAY(&arena, ParseStatement, slots),
            .diagnostics = BUSTER_ARENA_PUSH_ARRAY(&arena, ParseDiagnostic, slots),
        };
        reserved = copies.depths != 0 && copies.enclosing != 0 && copies.bracket_blocks != 0 && copies.vkinds != 0 && copies.classified_flags != 0 && copies.roles != 0 &&
                   copies.flags != 0 && copies.typed != 0 && copies.twin_typed != 0 && copies.node_depths != 0 && copies.parents != 0 && copies.starts != 0 &&
                   copies.twin_flags != 0 && copies.random_roles != 0 && copies.secondary != 0 && copies.oracle_contexts != 0 && copies.statements != 0 &&
                   copies.diagnostics != 0;
    }
    if (!reserved)
    {
        parse_test_report("%s: can't reserve the kernel checks' memory\n", name);
        result += 1;
    }
    else
    {
        tally->units += 1;
        kernels->unbalanced = false;
        kernels->unsupported = false;
        parse_kernels_predeclared(kernels, parser->predeclared, true);
        // K1 on ( [ {: the vector kernel into the copies, the twin into the kernels' arrays, and the blocks' views (which
        // K2 reads) from the twin's.
        const bool twin_balanced = parse_brackets_scalar(kinds, parse_kind_deltas, count, kernels->depths, kernels->enclosing, kernels->stack);
        const bool vector_balanced = parse_brackets_avx512(kinds, parse_kind_deltas, count, copies.depths, copies.enclosing, buffers.open_stack, copies.bracket_blocks);
        uint32_t first = BUSTER_PARSE_NONE;
        for (uint32_t i = 0; i < count && first == BUSTER_PARSE_NONE && twin_balanced; i += 1)
        {
            first = kernels->depths[i] != copies.depths[i] || kernels->enclosing[i] != copies.enclosing[i] ? i : BUSTER_PARSE_NONE;
        }
        parse_bracket_blocks(kinds, parse_kind_deltas, kernels->depths, kernels->enclosing, count, kernels->bracket_blocks);
        for (uint32_t block = 0; block < count && first == BUSTER_PARSE_NONE && twin_balanced; block += 64)
        {
            first = memcmp(&kernels->bracket_blocks[block / 64], &copies.bracket_blocks[block / 64], sizeof(ParseBracketBlock)) != 0 ? block : BUSTER_PARSE_NONE;
        }
        if (twin_balanced != vector_balanced)
        {
            parse_test_report("%s: K1 differs: balanced %d, and %d in AVX-512\n", name, twin_balanced, vector_balanced);
            result += 1;
        }
        result += parse_test_kernel_differs(parser, pp, name, "K1", first);
        // K3: the typedef pass, then the classification in AVX-512 against the twin.
        kernels->unbalanced = !twin_balanced;
        if (!kernels->unbalanced)
        {
            parse_typedefs(kernels, kinds, names, count);
        }
        parse_classify_mask_avx512(kernels, kinds, names, count);
        memcpy(copies.vkinds, kernels->vkinds, count);
        memcpy(copies.classified_flags, kernels->flags, count);
        parse_classify_scalar(kernels, kinds, names, count);
        first = parse_test_first_byte(copies.vkinds, kernels->vkinds, count);
        first = first != BUSTER_PARSE_NONE ? first : parse_test_first_byte(copies.classified_flags, kernels->flags, count);
        result += parse_test_kernel_differs(parser, pp, name, "K3's classification", first);
        memcpy(copies.classified_flags, kernels->flags, count);
        // K2: the vector kernel, then the twin, which the later kernels read; and the statement contexts against those
        // from the twin's roles.
        parse_roles_avx512(kernels, count, copies.typed, &buffers.groups, buffers.contexts, buffers.rounds);
        memcpy(copies.roles, kernels->roles, count);
        memcpy(copies.flags, kernels->flags, count);
        memcpy(kernels->flags, copies.classified_flags, count);
        parse_roles_scalar(kernels, count, copies.twin_typed);
        first = parse_test_first_byte(copies.roles, kernels->roles, count);
        first = first != BUSTER_PARSE_NONE ? first : parse_test_first_byte(copies.flags, kernels->flags, count);
        first = first != BUSTER_PARSE_NONE ? first : parse_test_first_byte(copies.typed, copies.twin_typed, count);
        if (first != BUSTER_PARSE_NONE)
        {
            parse_test_report("%s: K2 differs at token %u: twin %s%s, vector %s%s\n", name, first, parse_role_names[kernels->roles[first]], kernels->flags[first] ? " (flagged)" : "",
                   parse_role_names[copies.roles[first]], copies.flags[first] ? " (flagged)" : "");
            parse_test_print_context(parser, pp, first);
            result += 1;
        }
        if (!kernels->unbalanced)
        {
            parse_statement_contexts(kernels, count, copies.oracle_contexts);
            result += parse_test_kernel_differs(parser, pp, name, "K2's statement contexts", parse_test_first_context(buffers.contexts, copies.oracle_contexts, count));
        }
        result += parse_test_check_role_starts(parser, pp, name, kernels, &buffers, &copies, count, tally);
        // K1's second part and the roles after colons: in AVX-512 into the copies, then the twins.
        memcpy(copies.roles, kernels->roles, count);
        memcpy(copies.flags, kernels->flags, count);
        const bool unbalanced = kernels->unbalanced;
        parse_secondary_avx512(kernels, count, buffers.secondary);
        parse_after_colons_avx512(kernels, count);
        const bool vector_unbalanced = kernels->unbalanced;
        memcpy(copies.node_depths, kernels->node_depths, (uint64_t)count * sizeof(uint32_t));
        uint8_t* vector_roles = copies.starts;
        uint8_t* vector_flags = copies.twin_flags;
        memcpy(vector_roles, kernels->roles, count);
        memcpy(vector_flags, kernels->flags, count);
        memcpy(kernels->roles, copies.roles, count);
        memcpy(kernels->flags, copies.flags, count);
        kernels->unbalanced = unbalanced;
        parse_secondary_scalar(kernels, count, copies.secondary);
        parse_after_colons(kernels, count);
        first = parse_test_first_byte(vector_roles, kernels->roles, count);
        first = first != BUSTER_PARSE_NONE ? first : parse_test_first_byte(vector_flags, kernels->flags, count);
        first = first != BUSTER_PARSE_NONE ? first : parse_test_first_word(copies.node_depths, kernels->node_depths, count);
        if (vector_unbalanced != kernels->unbalanced)
        {
            parse_test_report("%s: K1's second part differs: unbalanced %d, and %d in AVX-512\n", name, kernels->unbalanced, vector_unbalanced);
            result += 1;
        }
        result += parse_test_kernel_differs(parser, pp, name, "K1's second part", first);
        parse_typedefs_clear(kernels);
        // K4: the twin, then the vector kernel with the stack's top in registers and with windows of fewer lanes.
        parse_parents_scalar(kernels, count);
        if (parse_deepest(kernels, count) <= BUSTER_PARSE_MAX_VECTOR_DEPTH)
        {
            static const uint32_t reaches[] = { BUSTER_PARSE_TOP_LANES, 1, 3, 8 };
            for (uint32_t r = 0; r < sizeof(reaches) / sizeof(reaches[0]); r += 1)
            {
                uint64_t wide = 0;
                if (reaches[r] == BUSTER_PARSE_TOP_LANES)
                {
                    parse_top_parents8_avx512(kernels->roles, kernels->node_depths, count, buffers.lefts, buffers.left_keys, buffers.works, copies.parents, &buffers.slots,
                                              &wide);
                    tally->wide_blocks += wide;
                }
                else
                {
                    parse_top_parents8_reach(kernels->roles, kernels->node_depths, count, buffers.lefts, buffers.left_keys, buffers.works, copies.parents, &buffers.slots,
                                             reaches[r], &wide);
                }
                first = parse_test_first_word(kernels->parents, copies.parents, count);
                if (first != BUSTER_PARSE_NONE)
                {
                    parse_test_report("%s: K4 with a window of %u lanes differs at token %u: twin %d, vector %d\n", name, reaches[r], first, (int)kernels->parents[first],
                           (int)copies.parents[first]);
                    parse_test_print_context(parser, pp, first);
                    result += 1;
                }
            }
        }
        // K5, from the twin's K4.
        if (!kernels->unbalanced)
        {
            result += parse_test_check_statements(parser, pp, name, kernels, &buffers, &copies, count, tally);
        }
        // K6 in AVX-512 against its twin, from the twins' roles: the same flags and classes, alone and with every second
        // or third block the twin's.
        {
            const ParseCheckFrames frames = parse_check_frames(kernels);
            memcpy(copies.flags, kernels->flags, count);
            parse_check_scalar(kernels, names, parser->tokens.flags, count, copies.twin_typed, &frames, kernels->check_classes, 0, 0);
            memcpy(copies.roles, kernels->check_classes, count);
            memcpy(copies.starts, kernels->flags, count);
            static const uint32_t periods[] = { 0, 2, 3 };
            for (uint32_t p = 0; p < sizeof(periods) / sizeof(periods[0]); p += 1)
            {
                memcpy(kernels->flags, copies.flags, count);
                uint64_t twins = 0;
                parse_check_avx512_period(kernels, names, parser->tokens.flags, count, copies.twin_typed, &twins, periods[p]);
                tally->check_twins += p == 0 ? twins : 0;
                first = parse_test_first_byte(copies.starts, kernels->flags, count);
                first = first != BUSTER_PARSE_NONE ? first : parse_test_first_byte(copies.roles, kernels->check_classes, count);
                if (first != BUSTER_PARSE_NONE)
                {
                    parse_test_report("%s: K6 (every %u blocks the twin's) differs at token %u: twin flag %u class %s, vector flag %u class %s\n", name, periods[p], first,
                                      copies.starts[first], parse_class_names[copies.roles[first]], kernels->flags[first], parse_class_names[kernels->check_classes[first]]);
                    parse_test_print_context(parser, pp, first);
                    result += 1;
                }
            }
            memcpy(kernels->flags, copies.starts, count);
        }
        parse_kernels_predeclared(kernels, parser->predeclared, false);
        // The twins together, with the fallback: on a valid unit, the scalar parser's tree.
        const ParseInput input = {
            .kinds = kinds,
            .names = names,
            .flags = parser->tokens.flags,
            .count = count,
            .language = parser->language,
            .meanings = &parser->meanings,
            .scratch = &parser->scratch,
            .diagnostics = copies.diagnostics,
            .diagnostic_capacity = (uint32_t)slots,
        };
        const ParseKernelResult twins = parse_twins(kernels, &input, parser->predeclared, copies.typed, copies.statements);
        tally->fallback_items += twins.fallback.items;
        if (scalar->errors == 0 && !twins.fallback.errors)
        {
            first = BUSTER_PARSE_NONE;
            for (uint32_t i = 0; i < count && first == BUSTER_PARSE_NONE; i += 1)
            {
                const bool same = kernels->parents[i] == scalar->tree.parents[i] && kernels->node_depths[i] == scalar->tree.depths[i] &&
                                  parse_test_canonical_role(kernels->roles[i]) == parse_test_canonical_role(scalar->tree.roles[i]);
                first = same ? BUSTER_PARSE_NONE : i;
            }
            if (first != BUSTER_PARSE_NONE)
            {
                parse_test_report("%s: the twins' tree differs at token %u: role %s, depth %u, parent %d against the scalar parser's %s, %u, %d\n", name, first,
                       parse_role_names[kernels->roles[first]], kernels->node_depths[first], (int)kernels->parents[first], parse_role_names[scalar->tree.roles[first]],
                       scalar->tree.depths[first], (int)scalar->tree.parents[first]);
                parse_test_print_context(parser, pp, first);
                result += 1;
            }
        }
        else if (scalar->errors == 0 && !parser->statistics.whole)
        {
            // The vector path took the unit to the scalar parser too, as a declaration may make a typedef name ordinary.
            parse_test_report("%s: the twins' fallback found an error the scalar parser didn't\n", name);
            result += 1;
        }
    }
    parse_test_release_guarded(&arena);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The paths on one file

// Whether two parsers read the same tokens from one unit (the input conversion's vector path and its twin): kinds,
// names, sources, flags and pragmas. Returns the first token that differs, or BUSTER_PARSE_NONE.
static uint32_t parse_test_compare_tokens(const ParseTokens* a, const ParseTokens* b)
{
    uint32_t result = a->count != b->count || a->pragma_count != b->pragma_count ? 0 : BUSTER_PARSE_NONE;
    for (uint32_t i = 0; i < a->count && result == BUSTER_PARSE_NONE; i += 1)
    {
        const bool same = a->kinds[i] == b->kinds[i] && a->names[i] == b->names[i] && a->sources[i] == b->sources[i] && a->flags[i] == b->flags[i];
        result = same ? BUSTER_PARSE_NONE : i;
    }
    for (uint32_t i = 0; i < a->pragma_count && result == BUSTER_PARSE_NONE; i += 1)
    {
        const bool same = a->pragma_sources[i] == b->pragma_sources[i] && a->pragma_positions[i] == b->pragma_positions[i];
        result = same ? BUSTER_PARSE_NONE : a->pragma_positions[i];
    }
    return result;
}

// Omitting the later stages' traversal must preserve every parser token, tree and diagnostic. The ordinary runs
// above still build and independently check both traversal columns; this run also proves their arenas stay absent.
static uint32_t parse_test_tree_only(const Parser* full, Preprocessor* pp, const char* path)
{
    static Parser tree;
    ParseOptions options = full->options;
    options.tree_only = true;
    bool same = parse_initialize(&tree, &options, full->language);
    if (same)
    {
        const bool parsed = parse_translation_unit(&tree, pp);
        same = parsed == (full->errors == 0) && tree.errors == full->errors && tree.fatal == full->fatal &&
               tree.diagnostic_count == full->diagnostic_count && parse_test_compare_tokens(&tree.tokens, &full->tokens) == BUSTER_PARSE_NONE &&
               tree.postorder == 0 && tree.sizes == 0 && tree.columns[11].base == 0 && tree.columns[12].base == 0;
        for (uint32_t d = 0; same && d < full->diagnostic_count; d += 1)
        {
            const ParseDiagnostic* a = &full->diagnostics[d];
            const ParseDiagnostic* b = &tree.diagnostics[d];
            same = a->token == b->token && a->argument == b->argument && a->message == b->message && a->place == b->place && a->error == b->error;
        }
        if (same && parsed)
        {
            const uint64_t count = full->tokens.count;
            same = memcmp(tree.tree.roles, full->tree.roles, count) == 0 && memcmp(tree.tree.parents, full->tree.parents, count * sizeof(uint32_t)) == 0 &&
                   memcmp(tree.tree.depths, full->tree.depths, count * sizeof(uint32_t)) == 0;
        }
    }
    parse_release(&tree);
    if (!same)
    {
        parse_test_report("%s: tree-only parsing differs from the full %s parser\n", path, full->options.scalar ? "scalar" : "vector");
    }
    return same ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// The handoff (#335)

// A column of `bytes` bytes ending exactly at a PROT_NONE page, so that a read or write past its last element faults; a
// column of none points at the page itself. parse_test_handoff_release unmaps it.
static uint8_t* parse_test_handoff_column(uint64_t bytes)
{
    const uint64_t page = 4096;
    const uint64_t room = (bytes + page - 1) & ~(page - 1);
    uint8_t* mapping = mmap(0, room + page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    uint8_t* result = 0;
    if (mapping != MAP_FAILED)
    {
        if (room == 0 || mprotect(mapping, room, PROT_READ | PROT_WRITE) == 0)
        {
            result = mapping + room - bytes;
        }
        else
        {
            munmap(mapping, room + page);
        }
    }
    return result;
}

static void parse_test_handoff_release(uint8_t* column, uint64_t bytes)
{
    const uint64_t page = 4096;
    const uint64_t room = (bytes + page - 1) & ~(page - 1);
    if (column != 0)
    {
        munmap(column + bytes - room, room + page);
    }
}

// An output stream's tokens, from `state`: identifiers whose ids are keywords' (below the table), other predefined
// names', and names past them, other tokens, `::`, a '#' inside a line, pragmas ('#' starting a line, its tokens and
// PRAGMA_END) and a '#' that starts a line with no PRAGMA_END before the next line, as a macro may make. `forced`
// puts a special token (by `special`) at token `at`, unless at is past the count.
static void parse_test_handoff_stream(PPTokens* output, uint32_t count, uint64_t* state, uint32_t at, uint32_t special)
{
    const uint8_t others[] = { C_TOKEN_L_PAREN, C_TOKEN_R_PAREN, C_TOKEN_SEMI, C_TOKEN_COMMA, C_TOKEN_COLON, C_TOKEN_NUMERIC_CONSTANT, C_TOKEN_STRING_LITERAL };
    uint32_t i = 0;
    while (i < count)
    {
        const uint64_t r = parse_test_random(state);
        const uint32_t pick = i == at ? 20 + special : (uint32_t)(r % 24);
        uint8_t kind = C_TOKEN_RAW_IDENTIFIER;
        uint32_t value = 0;
        uint8_t flags = (uint8_t)((r >> 8) % 4 == 0 ? BUSTER_PP_START_OF_LINE : 0) | (uint8_t)((r >> 10) % 2 == 0 ? BUSTER_PP_SPACE : 0);
        uint32_t length = 1;
        if (pick < 8)
        {
            value = 1 + (uint32_t)((r >> 16) % (BUSTER_PP_KEYWORD_TABLE + 8));
        }
        else if (pick < 11)
        {
            value = BUSTER_PP_KEYWORD_TABLE + (uint32_t)((r >> 16) % 100000);
        }
        else if (pick < 19)
        {
            kind = others[(r >> 16) % sizeof(others)];
        }
        else if (pick == 19 || pick == 20)
        {
            kind = C_TOKEN_COLONCOLON;
        }
        else if (pick == 21)
        {
            kind = C_TOKEN_HASH;
            flags &= (uint8_t)~BUSTER_PP_START_OF_LINE;
        }
        else
        {
            // A pragma, or a '#' starting a line with none, of up to 70 tokens (past a block), some identifiers.
            kind = C_TOKEN_HASH;
            flags |= BUSTER_PP_START_OF_LINE;
            length = 1 + (uint32_t)((r >> 16) % 70);
        }
        // A group ends before the forced token.
        length = at > i && at < i + length ? at - i : length;
        output->kinds[i] = kind;
        output->values[i] = value;
        output->flags[i] = flags;
        const bool pragma = pick == 22 || (pick == 23 && (r >> 40) % 2 == 0);
        uint32_t j = 1;
        while (j < length && i + j < count)
        {
            const uint64_t t = parse_test_random(state);
            const bool last = j + 1 == length || i + j + 1 == count;
            output->kinds[i + j] = last && pragma ? BUSTER_PP_KIND_PRAGMA_END : t % 3 == 0 ? C_TOKEN_COMMA : C_TOKEN_RAW_IDENTIFIER;
            output->values[i + j] = output->kinds[i + j] == C_TOKEN_RAW_IDENTIFIER ? 1 + (uint32_t)((t >> 8) % (BUSTER_PP_KEYWORD_TABLE + 8)) : 0;
            // A pragma's tokens never start a line; a '#' without PRAGMA_END ends where a token does.
            output->flags[i + j] = !pragma && last ? BUSTER_PP_START_OF_LINE : (uint8_t)((t >> 20) % 2 == 0 ? BUSTER_PP_SPACE : 0);
            j += 1;
        }
        i += j;
    }
    output->count = count;
}

// The handoff's oracle: the output as the keyword pass and the conversion must leave it and give it to the parser,
// from C's rules for the language mode (c_name_is_keyword, not the preprocessor's table), the pragma's shape, and
// parse_kind_table and parse_keyword_kinds for the kinds. Writes the resolved kinds to `resolved` and the parser's
// tokens to `tokens` (unpadded).
static void parse_test_handoff_oracle(const PPTokens* output, CLanguage language, uint8_t* resolved, ParseTokens* tokens)
{
    for (uint32_t i = 0; i < output->count; i += 1)
    {
        const uint32_t id = output->values[i];
        const bool keyword = output->kinds[i] == C_TOKEN_RAW_IDENTIFIER && id != 0 && id < BUSTER_PP_KEYWORD_TABLE && id < C_NAME_COUNT && c_name_is_keyword(id, language);
        resolved[i] = keyword ? (uint8_t)(BUSTER_PP_KIND_KEYWORD + id - 1) : output->kinds[i];
    }
    uint32_t w = 0;
    tokens->pragma_count = 0;
    uint32_t i = 0;
    while (i < output->count)
    {
        const uint8_t kind = resolved[i];
        uint32_t end = i + 1;
        bool pragma = false;
        if (kind == C_TOKEN_HASH && (output->flags[i] & BUSTER_PP_START_OF_LINE) != 0)
        {
            while (end < output->count && resolved[end] != BUSTER_PP_KIND_PRAGMA_END && (output->flags[end] & BUSTER_PP_START_OF_LINE) == 0)
            {
                end += 1;
            }
            pragma = end < output->count && resolved[end] == BUSTER_PP_KIND_PRAGMA_END;
        }
        if (pragma)
        {
            tokens->pragma_sources[tokens->pragma_count] = i;
            tokens->pragma_positions[tokens->pragma_count] = w;
            tokens->pragma_count += 1;
            i = end + 1;
        }
        else
        {
            uint8_t parse_kind = parse_kind_table[kind];
            const bool is_keyword = kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END;
            if (is_keyword)
            {
                parse_kind = PARSE_KIND_UNKNOWN;
                for (uint32_t k = 0; k < sizeof(parse_keyword_kinds) / sizeof(parse_keyword_kinds[0]); k += 1)
                {
                    parse_kind = parse_keyword_kinds[k].name == output->values[i] ? parse_keyword_kinds[k].kind : parse_kind;
                }
            }
            tokens->kinds[w] = kind == C_TOKEN_COLONCOLON ? PARSE_KIND_COLON : parse_kind;
            tokens->names[w] = kind == C_TOKEN_RAW_IDENTIFIER || is_keyword ? output->values[i] : 0;
            tokens->sources[w] = i;
            tokens->flags[w] = output->flags[i];
            w += 1;
            if (kind == C_TOKEN_COLONCOLON)
            {
                tokens->kinds[w] = PARSE_KIND_COLON;
                tokens->names[w] = 0;
                tokens->sources[w] = i;
                tokens->flags[w] = BUSTER_PARSE_COLONCOLON_SECOND;
                w += 1;
            }
            i += 1;
        }
    }
    tokens->count = w;
}

// Whether the conversion's tokens are the oracle's, the padding around them PARSE_KIND_END and zeros.
static bool parse_test_handoff_same(const ParseTokens* got, const ParseTokens* expected)
{
    bool same = parse_test_compare_tokens(got, expected) == BUSTER_PARSE_NONE;
    for (uint32_t p = 0; same && p < BUSTER_PARSE_TOKEN_PADDING; p += 1)
    {
        const int64_t before = -1 - (int64_t)p;
        const uint64_t after = (uint64_t)got->count + p;
        same = got->kinds[before] == PARSE_KIND_END && got->kinds[after] == PARSE_KIND_END && got->names[before] == 0 && got->names[after] == 0 &&
               got->sources[before] == 0 && got->sources[after] == 0 && got->flags[before] == 0 && got->flags[after] == 0;
    }
    return same;
}

// The handoff on generated output streams: every count from 0 to 200 and some to 5,000, with a special token ('#'
// inside a line, a pragma's '#', a '#' with no PRAGMA_END, or `::`) forced at each of the blocks' edges (0, 62 to 66,
// 126 to 130, 190 to 194), each stream's columns ending at a PROT_NONE page. Both paths start from the stream as the
// preprocessor left it: the keyword pass's vector path and the conversion's, and their scalar twins. Each must give
// the oracle's resolved kinds and parser tokens, with the padding; a read or a write past an input column faults.
static uint32_t parse_test_check_handoff(CLanguage language, uint64_t seed)
{
    uint32_t failures = 0;
    uint64_t cases = 0;
    static Preprocessor pp;
    static Parser parser;
    memset(&pp, 0, sizeof(pp));
    pp.vectorized = true;
    for (uint32_t id = 0; id < BUSTER_PP_KEYWORD_TABLE; id += 1)
    {
        pp.keyword_kinds[id] = id < C_NAME_COUNT && c_name_is_keyword(id, language) ? (uint8_t)(BUSTER_PP_KIND_KEYWORD + id - 1) : C_TOKEN_RAW_IDENTIFIER;
    }
    const ParseOptions options = { 0 };
    const uint32_t edges[] = { 0, 62, 63, 64, 65, 66, 126, 127, 128, 129, 130, 190, 191, 192, 193, 194, BUSTER_PARSE_NONE };
    uint64_t state = seed ^ 0x335335335ull;
    const bool ready = parse_initialize(&parser, &options, language);
    failures += !ready;
    for (uint32_t count = 0; ready && count <= 5000 && failures < 8; count += count < 200 ? 1 : 397)
    {
        const uint64_t capacity = 2 * (uint64_t)count + 64;
        uint8_t* kinds = parse_test_handoff_column(count);
        uint8_t* flags = parse_test_handoff_column(count);
        uint8_t* values = parse_test_handoff_column((uint64_t)count * 4);
        uint8_t* original = malloc((uint64_t)count + 1);
        uint8_t* resolved = malloc((uint64_t)count + 1);
        ParseTokens expected = {
            .kinds = malloc(capacity),
            .names = malloc(capacity * 4),
            .sources = malloc(capacity * 4),
            .flags = malloc(capacity),
            .pragma_sources = malloc(((uint64_t)count + 1) * 4),
            .pragma_positions = malloc(((uint64_t)count + 1) * 4),
        };
        ParseTokens got = {
            .kinds = parse_column(&parser, 0, capacity, 1),
            .names = parse_column(&parser, 1, capacity, 4),
            .sources = parse_column(&parser, 2, capacity, 4),
            .pragma_sources = parse_column(&parser, 3, (uint64_t)count + 1, 4),
            .pragma_positions = parse_column(&parser, 4, (uint64_t)count + 1, 4),
            .flags = parse_column(&parser, 10, capacity, 1),
            .capacity = (uint32_t)capacity,
        };
        const bool allocated = kinds != 0 && flags != 0 && values != 0 && original != 0 && resolved != 0 && expected.kinds != 0 && expected.names != 0 && expected.sources != 0 &&
                               expected.flags != 0 && expected.pragma_sources != 0 && expected.pragma_positions != 0 && got.kinds != 0 && got.names != 0 && got.sources != 0 &&
                               got.pragma_sources != 0 && got.pragma_positions != 0 && got.flags != 0;
        failures += !allocated;
        for (uint32_t e = 0; allocated && e < sizeof(edges) / sizeof(edges[0]) && failures < 8; e += 1)
        {
            for (uint32_t special = 0; special < (edges[e] == BUSTER_PARSE_NONE ? 1u : 4u); special += 1)
            {
                pp.output = (PPTokens){ .kinds = kinds, .flags = flags, .values = (uint32_t*)values, .count = count, .capacity = count };
                parse_test_handoff_stream(&pp.output, count, &state, edges[e], special);
                memcpy(original, kinds, count);
                parse_test_handoff_oracle(&pp.output, language, resolved, &expected);
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    memcpy(kinds, original, count);
                    const uint64_t padded = capacity + 2 * BUSTER_PARSE_TOKEN_PADDING;
                    memset(got.kinds - BUSTER_PARSE_TOKEN_PADDING, 0x5a, padded);
                    memset(got.names - BUSTER_PARSE_TOKEN_PADDING, 0x5a, padded * sizeof(*got.names));
                    memset(got.sources - BUSTER_PARSE_TOKEN_PADDING, 0x5a, padded * sizeof(*got.sources));
                    memset(got.flags - BUSTER_PARSE_TOKEN_PADDING, 0x5a, padded);
                    memset(got.pragma_sources, 0x5a, ((uint64_t)count + 1) * sizeof(*got.pragma_sources));
                    memset(got.pragma_positions, 0x5a, ((uint64_t)count + 1) * sizeof(*got.pragma_positions));
                    switch (path)
                    {
                        case 0:
                            pp_resolve_keywords_vector(&pp);
                            parse_tokens_from_output(&pp.output, &got);
                            break;
                        case 1:
                            pp_resolve_keywords_scalar(&pp);
                            parse_tokens_from_output_scalar(&pp.output, &got);
                            break;
                    }
                    const uint32_t kind = parse_test_first_byte(kinds, resolved, count);
                    if (kind != BUSTER_PARSE_NONE || !parse_test_handoff_same(&got, &expected))
                    {
                        static const char* const names[] = { "keyword pass and conversion", "their twins" };
                        parse_test_report("handoff: %s differs from the oracle on %u tokens (special %u at %u), first resolved kind at %u\n", names[path], count, special,
                                          edges[e], kind);
                        failures += 1;
                    }
                    cases += 1;
                }
            }
        }
        parse_test_handoff_release(kinds, count);
        parse_test_handoff_release(flags, count);
        parse_test_handoff_release(values, (uint64_t)count * 4);
        free(original);
        free(resolved);
        free(expected.kinds);
        free(expected.names);
        free(expected.sources);
        free(expected.flags);
        free(expected.pragma_sources);
        free(expected.pragma_positions);
    }
    parse_release(&parser);
    memset(&pp, 0, sizeof(pp));
    printf("handoff: %llu streams by 2 paths against the oracle, %u failures\n", (unsigned long long)cases / 2, failures);
    return failures;
}

// One file through both paths: preprocessed once, then parsed by the vector path and by the scalar parser alone, which
// must read the same tokens and give the same outcome, and on a valid unit the same tree, with the tree's invariants
// and postorders; then each kernel against its twin. *outcome gets the vector path's outcome. Returns the number of
// failures.
static uint32_t parse_test_check_file(const char* path, const PPOptions* options, ParseTestOutcome* outcome, ParseTestKernelTally* tally)
{
    uint32_t failures = 0;
    static InternTable table;
    static Preprocessor pp;
    static Parser vector;
    static Parser scalar;
    const ParseOptions vector_options = { .scalar = false };
    const ParseOptions scalar_options = { .scalar = true };
    *outcome = (ParseTestOutcome){ 0 };
    if (intern_initialize(&table) && pp_initialize(&pp, options, &table) && parse_initialize(&vector, &vector_options, options->language) &&
        parse_initialize(&scalar, &scalar_options, options->language))
    {
        if (pp_run(&pp, path))
        {
            parse_translation_unit(&vector, &pp);
            parse_translation_unit(&scalar, &pp);
            failures += parse_test_tree_only(&vector, &pp, path);
            failures += parse_test_tree_only(&scalar, &pp, path);
            *outcome = parse_test_outcome(&vector, &pp);
            const ParseTestOutcome alone = parse_test_outcome(&scalar, &pp);
            const uint32_t token = parse_test_compare_tokens(&vector.tokens, &scalar.tokens);
            if (token != BUSTER_PARSE_NONE)
            {
                failures += 1;
                parse_test_report("%s: the input's vector path and its twin differ at token %u\n", path, token);
            }
            else if (outcome->errors != alone.errors || outcome->line != alone.line || outcome->column != alone.column || strcmp(outcome->message, alone.message) != 0)
            {
                failures += 1;
                parse_test_report("%s: the paths differ: %u errors, first %u:%u: %s, and the scalar parser's %u, %u:%u: %s\n", path, outcome->errors, outcome->line, outcome->column,
                       outcome->message, alone.errors, alone.line, alone.column, alone.message);
            }
            else
            {
                if (alone.errors == 0)
                {
                    failures += parse_test_compare_trees(&vector, &scalar, &pp, path, &vector.scratch);
                }
                failures += parse_test_check_kernels(&vector, &scalar, &pp, path, tally);
            }
        }
        parse_release(&scalar);
        parse_release(&vector);
        pp_release(&pp);
        intern_release(&table);
    }
    else
    {
        outcome->out_of_memory = true;
        failures += 1;
    }
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// K6's failures on valid code

// A failure's signature: which checks failed, the class before, the token's class and its mode.
typedef struct ParseTestFailure ParseTestFailure;
struct ParseTestFailure
{
    uint32_t key;
    uint32_t count;
    char example[1100];
};

#define BUSTER_PARSE_TEST_FAILURES 4096

// `--explain`: every file's units through the twins, with K6's reasons, tallied by signature; the commonest are printed
// with an example each. Valid code that fails a check goes to the scalar parser for nothing, so these say what the
// checks miss.
static uint32_t parse_test_explain(char** files, uint32_t file_count, const PPOptions* options)
{
    static ParseTestFailure failures[BUSTER_PARSE_TEST_FAILURES];
    uint32_t failure_count = 0;
    uint64_t tokens = 0;
    uint64_t flagged = 0;
    uint64_t k3_flagged = 0;
    uint32_t k3_reports = 0;
    for (uint32_t f = 0; f < file_count; f += 1)
    {
        static InternTable table;
        static Preprocessor pp;
        static Parser parser;
        const ParseOptions parse_options = { .scalar = false };
        if (intern_initialize(&table) && pp_initialize(&pp, options, &table) && parse_initialize(&parser, &parse_options, options->language) && pp_run(&pp, files[f]) &&
            parse_translation_unit(&parser, &pp))
        {
            const uint32_t count = parser.tokens.count;
            const uint64_t slots = (uint64_t)count + 2 * BUSTER_PARSE_TOKEN_PADDING;
            Arena arena = arena_reserve(slots * 64 + (uint64_t)parser.meanings.capacity + (64ull << 20));
            ParseKernels kernels;
            uint8_t* typed = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots);
            ParseStatement* statements = BUSTER_ARENA_PUSH_ARRAY(&arena, ParseStatement, slots);
            uint8_t* classes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots);
            uint8_t* modes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots);
            uint8_t* reasons = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots);
            if (parse_kernels_reserve(&kernels, &arena, count, parser.meanings.capacity) && typed != 0 && statements != 0 && classes != 0 && modes != 0 && reasons != 0)
            {
                const uint8_t* kinds = parser.tokens.kinds;
                const uint32_t* names = parser.tokens.names;
                // The twins up to K5, as parse_twins runs them, then K6 with its reasons.
                parse_kernels_predeclared(&kernels, parser.predeclared, true);
                kernels.unbalanced = !parse_brackets_scalar(kinds, parse_kind_deltas, count, kernels.depths, kernels.enclosing, kernels.stack);
                if (!kernels.unbalanced)
                {
                    parse_typedefs(&kernels, kinds, names, count);
                }
                parse_classify_scalar(&kernels, kinds, names, count);
                // K3's own flags: uses of names some block declares as typedefs, and uses before a name's typedef.
                for (uint32_t i = 0; i < count; i += 1)
                {
                    if (kernels.flags[i] != 0 && k3_reports < 12)
                    {
                        k3_reports += 1;
                        const uint32_t start = pp.output.locations[parser.tokens.sources[i]];
                        const uint32_t stop = parse_token_end(&parser, &pp, i);
                        const uint8_t* bytes = pp_location_bytes(&pp, start);
                        printf("K3 flags token %u, %.*s (first typedef declarator at %u)\n", i, (int)(stop - start < 40 ? stop - start : 40), (const char*)bytes,
                               kernels.typedef_first[names[i]]);
                    }
                    k3_flagged += kernels.flags[i] != 0;
                }
                parse_roles_scalar(&kernels, count, typed);
                parse_secondary_scalar(&kernels, count, kernels.stack);
                parse_after_colons(&kernels, count);
                parse_parents_scalar(&kernels, count);
                parse_statements(&kernels, count, statements);
                memset(kernels.flags, 0, count);
                const ParseCheckFrames frames = parse_check_frames(&kernels);
                parse_check_scalar(&kernels, names, parser.tokens.flags, count, typed, &frames, classes, modes, reasons);
                tokens += count;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    flagged += reasons[i] != 0;
                    if (reasons[i] != 0)
                    {
                        // An END failure is the token before's; its mode and class are its own.
                        const uint32_t at = (reasons[i] & BUSTER_PARSE_CHECK_END) != 0 && i > 0 && reasons[i] == BUSTER_PARSE_CHECK_END ? i - 1 : i;
                        const uint8_t before = at > 0 ? classes[at - 1] : PARSE_ROLE_NONE;
                        const uint32_t key = (uint32_t)reasons[i] << 24 | (uint32_t)before << 16 | (uint32_t)classes[at] << 8 | modes[at];
                        uint32_t k = 0;
                        while (k < failure_count && failures[k].key != key)
                        {
                            k += 1;
                        }
                        if (k == failure_count && failure_count < BUSTER_PARSE_TEST_FAILURES)
                        {
                            failures[k].key = key;
                            failures[k].count = 0;
                            failure_count += 1;
                            const uint32_t budget = parse_test_report_budget;
                            parse_test_report_budget = 0;
                            char text[1024];
                            uint32_t length = 0;
                            for (uint32_t t = at > 10 ? at - 10 : 0; t < at + 6 && t < count && length < sizeof(text) - 64; t += 1)
                            {
                                const uint32_t start = pp.output.locations[parser.tokens.sources[t]];
                                const uint32_t stop = parse_token_end(&parser, &pp, t);
                                const uint8_t* bytes = pp_location_bytes(&pp, start);
                                const uint32_t size = bytes != 0 && stop > start ? (stop - start < 30 ? stop - start : 30) : 0;
                                length += (uint32_t)snprintf(text + length, sizeof(text) - length, t == at ? " >>%.*s<<" : " %.*s", (int)size, (const char*)bytes);
                            }
                            parse_test_report_budget = budget;
                            snprintf(failures[k].example, sizeof(failures[k].example), "%s", text);
                        }
                        if (k < failure_count)
                        {
                            failures[k].count += 1;
                        }
                    }
                }
                parse_typedefs_clear(&kernels);
                parse_kernels_predeclared(&kernels, parser.predeclared, false);
            }
            arena_release(&arena);
        }
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&table);
    }
    // The commonest first.
    for (uint32_t a = 0; a < failure_count; a += 1)
    {
        for (uint32_t b = a + 1; b < failure_count; b += 1)
        {
            if (failures[b].count > failures[a].count)
            {
                const ParseTestFailure swap = failures[a];
                failures[a] = failures[b];
                failures[b] = swap;
            }
        }
    }
    printf("K3 flags %llu tokens; K6 on %llu tokens: %llu fail, %u signatures\n", (unsigned long long)k3_flagged, (unsigned long long)tokens, (unsigned long long)flagged,
           failure_count);
    static const char* const check_names[] = { "pair", "mode", "end", "name", "label", "clauses", "error", "tag" };
    for (uint32_t k = 0; k < failure_count && k < 80; k += 1)
    {
        const uint32_t key = failures[k].key;
        char checks[64] = "";
        for (uint32_t bit = 0; bit < 8; bit += 1)
        {
            if ((key >> 24) & (1u << bit))
            {
                strcat(checks, checks[0] != 0 ? "+" : "");
                strcat(checks, check_names[bit]);
            }
        }
        printf("%7u %-10s %-24s %-24s %-24s\n        %s\n", failures[k].count, checks, parse_class_names[(key >> 16) & 0xFF], parse_class_names[(key >> 8) & 0xFF],
               parse_check_mode_names[key & 0xFF], failures[k].example);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// A look at a tree

// `--print-tree FILE [FIRST LAST]`: each token of the file's unit (from FIRST to LAST, by the main file's lines) with its
// kind, and its role, node depth and parent by the vector path and by the scalar parser, a difference marked.
static uint32_t parse_test_print_tree(const char* path, const PPOptions* options, uint32_t first_line, uint32_t last_line)
{
    uint32_t failures = 0;
    static InternTable table;
    static Preprocessor pp;
    static Parser vector;
    static Parser scalar;
    const ParseOptions vector_options = { .scalar = false };
    const ParseOptions scalar_options = { .scalar = true };
    if (intern_initialize(&table) && pp_initialize(&pp, options, &table) && parse_initialize(&vector, &vector_options, options->language) &&
        parse_initialize(&scalar, &scalar_options, options->language) && pp_run(&pp, path))
    {
        parse_translation_unit(&vector, &pp);
        parse_translation_unit(&scalar, &pp);
        printf("vector path: %u errors%s; scalar parser: %u errors\n", vector.errors, vector.statistics.whole ? " (the whole unit went to the scalar parser)" : "",
               scalar.errors);
        for (uint32_t t = 0; t < scalar.tokens.count && t < vector.tokens.count; t += 1)
        {
            const uint32_t location = pp.output.locations[scalar.tokens.sources[t]];
            const char* name;
            uint32_t name_length;
            const uint32_t line = pp_presumed(&pp, location, &name, &name_length);
            if (line >= first_line && line <= last_line)
            {
                const uint32_t end = parse_token_end(&scalar, &pp, t);
                const uint8_t* bytes = pp_location_bytes(&pp, location);
                const int length = bytes != 0 && end > location ? (int)(end - location < 24 ? end - location : 24) : 0;
                const bool differ = vector.tree.parents[t] != scalar.tree.parents[t] || vector.tree.depths[t] != scalar.tree.depths[t] ||
                                    parse_test_canonical_role(vector.tree.roles[t]) != parse_test_canonical_role(scalar.tree.roles[t]);
                printf("%7u %5u  %-24.*s %-14s | %-24s %3u %7d | %-24s %3u %7d%s\n", t, line, length, (const char*)bytes, parse_kind_names[scalar.tokens.kinds[t]],
                       parse_role_names[vector.tree.roles[t]], vector.tree.depths[t], (int)vector.tree.parents[t], parse_role_names[scalar.tree.roles[t]],
                       scalar.tree.depths[t], (int)scalar.tree.parents[t], differ ? "  <<" : "");
            }
        }
    }
    else
    {
        printf("%s: can't parse\n", path);
        failures += 1;
    }
    parse_release(&scalar);
    parse_release(&vector);
    pp_release(&pp);
    intern_release(&table);
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// The files and the mutations

// The .c files under `path` (or `path` itself), appended to `paths` (up to `capacity`, each malloc'd): depth first, in
// sorted order within each directory, from a stack of the paths still to visit. Returns how many there are now.
static uint32_t parse_test_collect(const char* path, char** paths, uint32_t count, uint32_t capacity)
{
    uint32_t result = count;
    uint32_t stack_capacity = 64;
    char** stack = malloc(sizeof(char*) * stack_capacity);
    uint32_t depth = stack != 0;
    if (stack != 0)
    {
        stack[0] = strdup(path);
    }
    while (depth != 0)
    {
        depth -= 1;
        char* current = stack[depth];
        struct stat status;
        if (current != 0 && stat(current, &status) == 0 && S_ISDIR(status.st_mode))
        {
            struct dirent** entries = 0;
            const int n = scandir(current, &entries, 0, alphasort);
            // In reverse, so that the first entry is the next visited.
            for (int i = n - 1; i >= 0; i -= 1)
            {
                const char* name = entries[i]->d_name;
                if (name[0] != '.')
                {
                    if (depth == stack_capacity)
                    {
                        char** grown = realloc(stack, sizeof(char*) * 2 * stack_capacity);
                        stack = grown != 0 ? grown : stack;
                        stack_capacity *= grown != 0 ? 2 : 1;
                    }
                    if (depth < stack_capacity)
                    {
                        char child[4096];
                        snprintf(child, sizeof(child), "%s/%s", current, name);
                        stack[depth] = strdup(child);
                        depth += 1;
                    }
                }
                free(entries[i]);
            }
            free(entries);
            free(current);
        }
        else if (current != 0 && strlen(current) > 2 && strcmp(current + strlen(current) - 2, ".c") == 0 && result < capacity)
        {
            paths[result] = current;
            result += 1;
        }
        else
        {
            free(current);
        }
    }
    free(stack);
    return result;
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

// The mutations: `count` mutated copies of the files, spread over them in turn, each checked against clang, or without
// `clang`, by both paths, which must agree as they must on any file (parse_test_check_file).
static uint32_t parse_test_mutations(char** files, uint32_t file_count, const PPOptions* options, const char* const* clang_arguments, uint32_t clang_argument_count,
                                     const char* clang, uint64_t count, uint64_t seed, const char* directory, bool keep)
{
    uint32_t failures = 0;
    ParseTestTally tally = { 0 };
    ParseTestKernelTally kernels = { 0 };
    uint64_t rejected = 0;
    uint64_t differ = 0;
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
            if (clang != 0 && !parse_test_clang(clang, clang_arguments, clang_argument_count, batch_paths, in_batch, batch_results))
            {
                printf("can't run %s\n", clang);
                failures += 1;
                mutations = 0;
            }
            for (uint32_t b = 0; b < in_batch && failures == 0; b += 1)
            {
                const uint64_t mismatches = tally.mismatches;
                if (clang != 0)
                {
                    const ParseTestOutcome ours = parse_test_parse_file(batch_paths[b], &plain, false);
                    char label[4096];
                    snprintf(label, sizeof(label), "%s", file);
                    parse_test_compare(&tally, label, &ours, &batch_results[b], batch_details[b]);
                }
                else
                {
                    ParseTestOutcome ours;
                    const uint32_t budget = parse_test_report_budget;
                    const uint32_t differences = parse_test_check_file(batch_paths[b], &plain, &ours, &kernels);
                    tally.files += 1;
                    rejected += ours.errors != 0;
                    differ += differences != 0;
                    tally.mismatches += differences != 0;
                    if (differences != 0 && budget != 0)
                    {
                        printf("    in %s%s\n", file, batch_details[b]);
                    }
                }
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
    if (clang != 0)
    {
        printf("mutations: %llu (clang accepted %llu, a parse issue first in %llu, another issue first in %llu, of which we rejected %llu), %llu mismatches\n",
               (unsigned long long)tally.files, (unsigned long long)tally.accepted, (unsigned long long)tally.parse_issues, (unsigned long long)tally.other_issues,
               (unsigned long long)tally.other_rejected, (unsigned long long)tally.mismatches);
    }
    else
    {
        printf("mutations: %llu by both paths (the scalar parser rejected %llu), %llu where they differ; the fallback took %llu top-level declarations\n",
               (unsigned long long)tally.files, (unsigned long long)rejected, (unsigned long long)differ, (unsigned long long)kernels.fallback_items);
    }
    return failures + (uint32_t)(tally.mismatches != 0);
}

// The files: each must preprocess and parse, and with `clang`, the parser must accept exactly what clang accepts.
static uint32_t parse_test_files(char** files, uint32_t file_count, const PPOptions* options, const char* const* clang_arguments, uint32_t clang_argument_count,
                                 const char* clang)
{
    uint32_t failures = 0;
    ParseTestTally tally = { 0 };
    ParseTestKernelTally kernels = { 0 };
    uint32_t paths_differ = 0;
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
            ParseTestOutcome ours;
            const uint32_t differences = parse_test_check_file(files[f + b], options, &ours, &kernels);
            failures += differences;
            paths_differ += differences != 0;
            tokens += ours.tokens;
            parse_test_compare(&tally, files[f + b], &ours, &theirs[b], "");
        }
    }
    printf("files: %llu (%llu tokens), clang accepted %llu, rejected %llu%s, %llu mismatches\n", (unsigned long long)tally.files, (unsigned long long)tokens,
           (unsigned long long)tally.accepted, (unsigned long long)(tally.parse_issues + tally.other_issues), clang != 0 ? "" : " (without clang: every file must parse)",
           (unsigned long long)tally.mismatches);
    printf("paths: %llu units, %u where the paths or a kernel and its twin differ; the fallback took %llu top-level declarations; K2 left %llu blocks to the "
           "twin's loop from random roles, K4 had %llu blocks in 16-bit lanes, K5 left %llu units to its twin, K6 %llu blocks\n",
           (unsigned long long)kernels.units, paths_differ, (unsigned long long)kernels.fallback_items, (unsigned long long)kernels.random_twins,
           (unsigned long long)kernels.wide_blocks, (unsigned long long)kernels.statement_twins, (unsigned long long)kernels.check_twins);
    return failures + (uint32_t)(tally.mismatches != 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Generated inputs

// The pieces random streams are made of: C's tokens and some runs of them, every one valid on its own.
static const char* const parse_test_pieces[] =
{
    "a", "b", "T", "x", "f", "0", "1", "'c'", "\"s\"", "1.5",
    "int", "char", "long", "unsigned", "const", "static", "extern", "typedef", "struct", "union", "enum", "void",
    "if", "else", "while", "for", "do", "switch", "case", "default", "return", "break", "continue", "goto", "sizeof",
    "__attribute__", "__extension__", "asm", "_Alignas", "_Static_assert", "__builtin_offsetof", "__builtin_va_arg",
    "+", "-", "*", "/", "%", "=", "==", "!=", "<", ">", "<=", ">=", "&&", "||", "!", "~", "&", "|", "^", "<<", ">>",
    "++", "--", "+=", "-=", "?", ":", ",", ";", ".", "->", "...",
    "(", ")", "[", "]", "{", "}",
    "int a;", "T x;", "f(a, b)", "a[0]", "(int)a", "struct S { int a; };", "{ a = 1; }", "if (a) b; else x;", "x = a ? b : 1;",
};

// The opening pieces and their closers, for balanced streams.
static const char parse_test_openers[] = "([{";
static const char parse_test_closers[] = ")]}";

// A random stream of `pieces` pieces into `text` (of `capacity` bytes), with `typedef int T;` first half the time.
// With `balanced`, every opener gets its closer: an opener goes on a stack and a closer comes only for the stack's
// top, and the stack is emptied at the end. Returns the length.
static size_t parse_test_stream(char* text, size_t capacity, uint64_t* state, uint32_t pieces, bool balanced)
{
    size_t length = 0;
    char stack[256];
    uint32_t open = 0;
    if (parse_test_random(state) % 2 == 0)
    {
        length += (size_t)snprintf(text + length, capacity - length, "typedef int T;\n");
    }
    const uint32_t piece_count = sizeof(parse_test_pieces) / sizeof(parse_test_pieces[0]);
    for (uint32_t p = 0; p < pieces && length + 64 < capacity; p += 1)
    {
        const uint32_t pick = (uint32_t)(parse_test_random(state) % (piece_count + 6));
        if (balanced && pick >= piece_count)
        {
            // A bracket: an opener, or the top's closer.
            const uint32_t which = (pick - piece_count) % 3;
            if (pick - piece_count < 3 && open < sizeof(stack))
            {
                stack[open] = parse_test_closers[which];
                open += 1;
                length += (size_t)snprintf(text + length, capacity - length, "%c ", parse_test_openers[which]);
            }
            else if (open != 0)
            {
                open -= 1;
                length += (size_t)snprintf(text + length, capacity - length, "%c ", stack[open]);
            }
        }
        else
        {
            const char* piece = pick < piece_count ? parse_test_pieces[pick] : (pick - piece_count < 3 ? "(" : ")");
            const bool bracket = strlen(piece) == 1 && strchr("()[]{}", piece[0]) != 0;
            if (!balanced || !bracket)
            {
                length += (size_t)snprintf(text + length, capacity - length, "%s%s", piece, parse_test_random(state) % 8 == 0 ? "\n" : " ");
            }
        }
    }
    while (open != 0 && length + 4 < capacity)
    {
        open -= 1;
        length += (size_t)snprintf(text + length, capacity - length, "%c ", stack[open]);
    }
    length += (size_t)snprintf(text + length, capacity - length, "\n");
    return length;
}

// `count` random streams of up to 300 pieces, half of them balanced, each through both paths (parse_test_check_file).
static uint32_t parse_test_streams(uint64_t count, uint64_t seed, const PPOptions* options, const char* directory, bool keep)
{
    uint32_t failures = 0;
    uint64_t state = seed ^ 0x5DEECE66Dull;
    ParseTestKernelTally tally = { 0 };
    uint64_t rejected = 0;
    static char text[1 << 16];
    char path[4200];
    snprintf(path, sizeof(path), "%s/stream.c", directory);
    for (uint64_t s = 0; s < count; s += 1)
    {
        const uint32_t pieces = 1 + (uint32_t)(parse_test_random(&state) % 300);
        const size_t length = parse_test_stream(text, sizeof(text), &state, pieces, s % 2 == 1);
        parse_test_write(path, text, length);
        ParseTestOutcome ours;
        const uint32_t differences = parse_test_check_file(path, options, &ours, &tally);
        rejected += ours.errors != 0;
        if (differences != 0)
        {
            failures += 1;
            if (keep)
            {
                char kept[4300];
                snprintf(kept, sizeof(kept), "%s/stream_%llu.c", directory, (unsigned long long)s);
                rename(path, kept);
                printf("    kept as %s\n", kept);
            }
        }
    }
    remove(path);
    printf("streams: %llu random streams (half with balanced brackets), %llu rejected, %u where the paths or a kernel and its twin differ\n", (unsigned long long)count,
           (unsigned long long)rejected, failures);
    return failures;
}

// Every block alignment: each snippet's source, and `file`'s -E text if given, after 0 to 63 file-scope `;`.
static uint32_t parse_test_alignments(const char* file, const PPOptions* options, const char* directory)
{
    uint32_t failures = 0;
    ParseTestKernelTally tally = { 0 };
    uint32_t inputs = 0;
    char path[4200];
    snprintf(path, sizeof(path), "%s/aligned.c", directory);
    size_t file_length = 0;
    char* file_text = file != 0 ? parse_test_preprocessed(file, options, &file_length) : 0;
    PPOptions plain = *options;
    plain.no_standard_includes = true;
    plain.include_count = 0;
    plain.macro_count = 0;
    const uint32_t snippets = sizeof(parse_test_snippets) / sizeof(parse_test_snippets[0]);
    for (uint32_t i = 0; i <= snippets; i += 1)
    {
        const char* source = i < snippets ? parse_test_snippets[i].source : file_text;
        const size_t length = source != 0 ? strlen(source) : 0;
        char* text = source != 0 ? malloc(length + 200) : 0;
        for (uint32_t shift = 0; text != 0 && shift < 64; shift += 1)
        {
            for (uint32_t k = 0; k < shift; k += 1)
            {
                text[2 * k] = ';';
                text[2 * k + 1] = ' ';
            }
            memcpy(text + 2 * shift, source, length);
            parse_test_write(path, text, 2 * shift + length);
            ParseTestOutcome ours;
            const PPOptions* used = i < snippets ? &plain : &plain;
            failures += parse_test_check_file(path, used, &ours, &tally) != 0;
            inputs += 1;
        }
        free(text);
    }
    free(file_text);
    remove(path);
    printf("alignments: %u inputs (the snippets%s at 64 alignments), %u where the paths or a kernel and its twin differ\n", inputs, file != 0 ? " and the file" : "",
           failures);
    return failures;
}

// Deep nesting, 100,000 levels: parentheses in an initializer and in a function, blocks, declarator groups, subscripts,
// ternaries (and commas inside them), casts and if-statements' heads, and a run of 100,000 attributes, each through both
// paths, which must agree (and not grow the native stack, nor take time that grows faster than the input: each token
// costs the parser the same whatever is open around it).
static uint32_t parse_test_nesting(const PPOptions* options, const char* directory)
{
    uint32_t failures = 0;
    ParseTestKernelTally tally = { 0 };
    enum { DEPTH = 100000 };
    static const char* const shapes[][4] =
    {
        // Before each level, the middle, after each level, and around the whole.
        { "(", "1", ")", "int x = %s;\n" },
        { "(", "0", ")", "int e(void) { return %s; }\n" },
        { "{", "x;", "}", "void f(int x) %s\n" },
        { "(", "y", ")", "int %s;\n" },
        { "a[", "0", "]", "int g(int *a) { return %s; }\n" },
        { "1 ? ", "2", " : 3", "int z = %s;\n" },
        { "1 ? 0, ", "2", " : 3", "int m(void) { return %s; }\n" },
        { "(int)", "w", "", "long h(int w) { return %s; }\n" },
        { "if (q) ", "q = 0;", "", "void k(int q) { %s }\n" },
        { "__attribute__((unused)) ", "a", "", "int %s;\n" },
    };
    char path[4200];
    snprintf(path, sizeof(path), "%s/deep.c", directory);
    for (uint32_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s += 1)
    {
        const size_t before = strlen(shapes[s][0]);
        const size_t middle = strlen(shapes[s][1]);
        const size_t after = strlen(shapes[s][2]);
        const size_t inner_length = DEPTH * (before + after) + middle;
        char* inner = malloc(inner_length + 1);
        char* text = malloc(inner_length + 256);
        size_t n = 0;
        for (uint32_t d = 0; d < DEPTH; d += 1)
        {
            memcpy(inner + n, shapes[s][0], before);
            n += before;
        }
        memcpy(inner + n, shapes[s][1], middle);
        n += middle;
        for (uint32_t d = 0; d < DEPTH; d += 1)
        {
            memcpy(inner + n, shapes[s][2], after);
            n += after;
        }
        inner[n] = 0;
        const int length = snprintf(text, inner_length + 256, shapes[s][3], inner);
        parse_test_write(path, text, (size_t)length);
        ParseTestOutcome ours;
        const uint32_t differences = parse_test_check_file(path, options, &ours, &tally);
        if (differences != 0)
        {
            printf("nesting: the %s...%s shape differs\n", shapes[s][0], shapes[s][2]);
        }
        failures += differences != 0;
        printf("nesting: %u levels of `%s` ... `%s`: %s\n", (unsigned)DEPTH, shapes[s][0], shapes[s][2], ours.errors != 0 ? ours.message : "parsed");
        free(inner);
        free(text);
    }
    remove(path);
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// The driver

// A figure of /proc/self/status (`key` with its colon, as "VmSize:"), in bytes, or 0 if it can't be read.
static uint64_t parse_test_status_bytes(const char* key)
{
    char text[8192];
    const int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    const ssize_t length = fd >= 0 ? read(fd, text, sizeof(text) - 1) : -1;
    if (fd >= 0)
    {
        close(fd);
    }
    text[length > 0 ? length : 0] = 0;
    const char* at = strstr(text, key);
    return at != 0 ? 1024 * strtoull(at + strlen(key), 0, 10) : 0;
}

// The lowest free descriptor: one the run left open shows as a higher one.
static int parse_test_lowest_descriptor(void)
{
    const int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (fd >= 0)
    {
        close(fd);
    }
    return fd;
}

// Reads what a run printed to `capture` into `text`, and empties `capture` for the next run.
static void parse_test_read_capture(int capture, char* text, size_t capacity)
{
    const ssize_t length = pread(capture, text, capacity - 1, 0);
    text[length > 0 ? length : 0] = 0;
    if (ftruncate(capture, 0) != 0 || lseek(capture, 0, SEEK_SET) != 0)
    {
        text[0] = 0;
    }
}

// Runs the driver on `arguments` (or with `preprocess`, -E's, run_preprocessor_command, whose arguments start with
// "-E") with its stderr in `capture` (an empty file, which it leaves empty), and what it printed in `text`. Returns its
// exit status.
static int parse_test_drive(char** arguments, int count, int capture, char* text, size_t capacity, bool preprocess)
{
    const int saved = dup(2);
    int result = -1;
    if (saved >= 0 && dup2(capture, 2) == 2)
    {
        result = preprocess ? run_preprocessor_command(count, arguments) : run_parse_command(count, arguments);
        dup2(saved, 2);
    }
    if (saved >= 0)
    {
        close(saved);
    }
    parse_test_read_capture(capture, text, capacity);
    return result;
}

// What the driver gets: a file with `text`, no file by the name, a directory, a file past what the preprocessor maps,
// or no argument at all.
typedef enum ParseTestDriverInput
{
    PARSE_TEST_DRIVER_TEXT,
    PARSE_TEST_DRIVER_MISSING,
    PARSE_TEST_DRIVER_DIRECTORY,
    PARSE_TEST_DRIVER_LARGE,
    PARSE_TEST_DRIVER_NOTHING,
} ParseTestDriverInput;

// The driver's cases: its input, an option before it (or none), and what it must do: its exit status and exactly what
// it prints (with %s for the input's path), or with `prefix`, what it prints first.
typedef struct ParseTestDriverCase ParseTestDriverCase;
struct ParseTestDriverCase
{
    ParseTestDriverInput input;
    const char* text;
    const char* option;
    int status;
    const char* message;
    bool prefix;
};

static const ParseTestDriverCase parse_test_driver_cases[] =
{
    { PARSE_TEST_DRIVER_TEXT, "int x;\n", 0, 0, "", false },
    { PARSE_TEST_DRIVER_TEXT, "", 0, 0, "", false },
    { PARSE_TEST_DRIVER_TEXT, "int x\n", 0, 1, "%s:1:6: error: expected ';' after top level declarator\n", false },
    { PARSE_TEST_DRIVER_TEXT, "int x\n", "--scalar", 1, "%s:1:6: error: expected ';' after top level declarator\n", false },
    { PARSE_TEST_DRIVER_TEXT, "#error stop\n", 0, 1, "%s:1:2: error: stop\n", false },
    { PARSE_TEST_DRIVER_TEXT, "#include \"driver-none.h\"\n", 0, 1, "%s:1:10: fatal error: 'driver-none.h' file not found\n", false },
    { PARSE_TEST_DRIVER_MISSING, 0, 0, 1, "fatal error: can't read '%s': No such file or directory\n", false },
    { PARSE_TEST_DRIVER_DIRECTORY, 0, 0, 1, "fatal error: can't read '%s': Is a directory\n", false },
    { PARSE_TEST_DRIVER_LARGE, 0, 0, 1, "fatal error: can't read '%s': File too large\n", false },
    { PARSE_TEST_DRIVER_NOTHING, 0, 0, 1, "usage: compiler [-fsyntax-only] ", true },
    { PARSE_TEST_DRIVER_TEXT, "int x;\n", "--bogus", 1, "unknown argument: --bogus\nusage: compiler [-fsyntax-only] ", true },
    { PARSE_TEST_DRIVER_TEXT, "int asm, typeof; __typeof__(int) x; __asm__(\"\");\n", "-fno-gnu-keywords", 0, "", false },
    { PARSE_TEST_DRIVER_TEXT, "int asm, typeof; __typeof__(int) x; __asm__(\"\");\n", "-fno-asm", 0, "", false },
    // Errors after a macro's expansion, placed after the invocation by its expansion history, which the driver records
    // only for its diagnostics (PPOptions.defer_history).
    { PARSE_TEST_DRIVER_TEXT, "#define DECL int x\nDECL\n", 0, 1, "%s:2:5: error: expected ';' after top level declarator\n", false },
    { PARSE_TEST_DRIVER_TEXT, "#define END(a) a\nint y = END(1)\n", 0, 1, "%s:2:15: error: expected ';' after top level declarator\n", false },
    // A warning, which the driver prints by preprocessing the unit again for its history: with code after it; and in a
    // unit of directives alone, whose output is empty, after a #if line of 300 expansions, for which the replay's history
    // grows while it runs (parse_test_driver_limits fails that growth too, which the empty output wouldn't show).
    { PARSE_TEST_DRIVER_TEXT, "#warning only\nint x;\n", 0, 0, "%s:1:2: warning: only\n", false },
    { PARSE_TEST_DRIVER_TEXT,
      "#define A(x) x\n#define B A(1)+A(1)+A(1)+A(1)+A(1)+A(1)+A(1)+A(1)\n#define C B+B+B+B+B+B+B+B\n#define D C+C+C+C\n#if D\n#endif\n#warning only\n", 0, 0,
      "%s:7:2: warning: only\n", false },
};

// Whether a run of case `c` did what it must.
static bool parse_test_driver_expected(const ParseTestDriverCase* c, const char* path, int status, const char* text)
{
    char expected[1024];
    snprintf(expected, sizeof(expected), c->message, path);
    return status == c->status && (c->prefix ? strncmp(text, expected, strlen(expected)) == 0 : strcmp(text, expected) == 0);
}

// A run in a child whose address space may grow `limit` bytes past `base` (the parent's): its exit status (-1 if it
// didn't report), how far past `base` its address space grew, and whether it left the address space and the
// descriptors as they were. The child writes them to `shared`, a page both see.
typedef struct ParseTestProbe ParseTestProbe;
struct ParseTestProbe
{
    int status;
    uint64_t peak;
    bool clean;
    char text[1024];
};

static ParseTestProbe parse_test_probe(char** arguments, int count, int capture, uint64_t base, uint64_t limit, ParseTestProbe* shared, bool preprocess)
{
    *shared = (ParseTestProbe){ .status = -1 };
    const pid_t child = fork();
    if (child == 0)
    {
        struct rlimit space;
        bool limited = getrlimit(RLIMIT_AS, &space) == 0 && base + limit <= space.rlim_max;
        space.rlim_cur = base + limit;
        limited = limited && setrlimit(RLIMIT_AS, &space) == 0 && dup2(capture, 2) == 2;
        const int before = parse_test_lowest_descriptor();
        const int status = !limited ? -1 : preprocess ? run_preprocessor_command(count, arguments) : run_parse_command(count, arguments);
        const int after = parse_test_lowest_descriptor();
        shared->peak = parse_test_status_bytes("VmPeak:") - base;
        shared->clean = parse_test_status_bytes("VmSize:") == base && after == before;
        shared->status = status;
        _exit(0);
    }
    int child_status = 0;
    pid_t waited = child;
    while (child > 0 && (waited = waitpid(child, &child_status, 0)) < 0 && errno == EINTR)
    {
    }
    ParseTestProbe result = *shared;
    result.status = child > 0 && waited == child && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0 ? result.status : -1;
    parse_test_read_capture(capture, result.text, sizeof(result.text));
    return result;
}

// Checks a run with a limit, in which a reservation failed: it must fail with status 1 having said why (a fatal error),
// before its address space reached the limit, and leave the address space and the descriptors as they were.
static uint32_t parse_test_check_probe(const ParseTestProbe* probe, uint64_t limit, const char* name)
{
    const bool failed = probe->status == 1 && strstr(probe->text, "fatal error: ") != 0 && probe->peak <= limit;
    const uint32_t result = !failed || !probe->clean;
    if (result != 0)
    {
        parse_test_report("driver: %s with %llu bytes of address space: status %d, peak %llu, %s, printed \"%s\"\n", name, (unsigned long long)limit,
                          probe->status, (unsigned long long)probe->peak, probe->clean ? "clean" : "left memory or a descriptor behind", probe->text);
    }
    return result;
}

// Every reservation and mapping a run of the driver on `arguments` makes, failed in turn, from the last to the first:
// a run stops at the first that fails, so in a child whose address space may grow a page less than a run reached, the
// last fails, and the child's peak is what the run had before it; a page less than that, the one before fails; and so
// on, a run each, down to the first.
static uint32_t parse_test_driver_limits(char** arguments, int count, int capture, ParseTestProbe* shared, const char* name, bool preprocess)
{
    uint32_t failures = 0;
    char text[1024];
    // A run without a limit, which also grows the stack as far as a run takes it, and one in a child, for its peak.
    const int status = parse_test_drive(arguments, count, capture, text, sizeof(text), preprocess);
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t base = parse_test_status_bytes("VmSize:");
    const ParseTestProbe whole = parse_test_probe(arguments, count, capture, base, 1ull << 46, shared, preprocess);
    const bool same = whole.status == status && strcmp(whole.text, text) == 0 && whole.clean && whole.peak != 0 && base != 0;
    if (!same)
    {
        parse_test_report("driver: %s: a run in a child gave status %d and \"%s\" (peak %llu), not %d and \"%s\"\n", name, whole.status, whole.text,
                          (unsigned long long)whole.peak, status, text);
    }
    failures += !same;
    uint32_t runs = 0;
    uint64_t peak = whole.peak;
    while (failures == 0 && peak != 0 && runs < 10000)
    {
        const uint64_t limit = peak - page;
        const ParseTestProbe probe = parse_test_probe(arguments, count, capture, base, limit, shared, preprocess);
        failures += parse_test_check_probe(&probe, limit, name);
        peak = probe.peak;
        runs += 1;
    }
    failures += runs >= 10000;
    printf("driver: %s: %u reservations and mappings failed in turn, each reported and released\n", name, runs);
    return failures;
}

// The driver (run_parse_command, the compiler's default command): each case's status and text, 16 runs of each that
// must leave the process's address space and descriptors as they were, and every reservation of a run on a valid and
// an invalid file, and on files with a warning, whose replay for the history fails too, failed in turn
// (parse_test_driver_limits).
static uint32_t parse_test_driver(const char* directory)
{
    uint32_t failures = 0;
    enum { CASES = BUSTER_ARRAY_LENGTH(parse_test_driver_cases) };
    char paths[CASES][4096];
    char capture_path[4096];
    snprintf(capture_path, sizeof(capture_path), "%s/driver-stderr", directory);
    const int capture = open(capture_path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    failures += capture < 0;
    for (uint32_t c = 0; c < CASES; c += 1)
    {
        const ParseTestDriverCase* driver_case = &parse_test_driver_cases[c];
        snprintf(paths[c], sizeof(paths[c]), "%s/driver-%u.c", directory, c);
        if (driver_case->input == PARSE_TEST_DRIVER_TEXT)
        {
            failures += !parse_test_write(paths[c], driver_case->text, strlen(driver_case->text));
        }
        else if (driver_case->input == PARSE_TEST_DRIVER_DIRECTORY)
        {
            failures += mkdir(paths[c], 0700) != 0;
        }
        else if (driver_case->input == PARSE_TEST_DRIVER_LARGE)
        {
            // A sparse file one byte past the preprocessor's limit: nothing is written or mapped.
            const int fd = open(paths[c], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
            failures += fd < 0 || ftruncate(fd, (off_t)(UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK) + 1) != 0;
            if (fd >= 0)
            {
                close(fd);
            }
        }
    }
    ParseTestProbe* shared = mmap(0, sizeof(ParseTestProbe), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    failures += shared == MAP_FAILED;
    // Each case once to warm up (the stack, the C library's first calls), then 16 times each.
    uint64_t base = 0;
    int lowest = -1;
    uint32_t runs = 0;
    for (uint32_t round = 0; failures == 0 && round < 17; round += 1)
    {
        for (uint32_t c = 0; c < CASES; c += 1)
        {
            const ParseTestDriverCase* driver_case = &parse_test_driver_cases[c];
            char* arguments[2];
            int count = 0;
            if (driver_case->option != 0)
            {
                arguments[count] = (char*)driver_case->option;
                count += 1;
            }
            if (driver_case->input != PARSE_TEST_DRIVER_NOTHING)
            {
                arguments[count] = paths[c];
                count += 1;
            }
            char text[1024];
            const int status = parse_test_drive(arguments, count, capture, text, sizeof(text), false);
            const bool expected = parse_test_driver_expected(driver_case, paths[c], status, text);
            const uint64_t size = parse_test_status_bytes("VmSize:");
            const int descriptor = parse_test_lowest_descriptor();
            const bool clean = round == 0 || (size == base && descriptor == lowest);
            if (!expected || !clean)
            {
                parse_test_report("driver: case %u (%s%s%s), run %u: status %d, %s, printed \"%s\"\n", c, driver_case->option != 0 ? driver_case->option : "",
                                  driver_case->option != 0 ? " " : "", driver_case->input == PARSE_TEST_DRIVER_NOTHING ? "no file" : paths[c], round, status,
                                  clean ? "clean" : "left memory or a descriptor behind", text);
                failures += 1;
            }
            runs += round != 0;
        }
        if (round == 0)
        {
            base = parse_test_status_bytes("VmSize:");
            lowest = parse_test_lowest_descriptor();
        }
    }
    printf("driver: %u cases, %u runs: the expected statuses and messages, nothing left behind\n", (unsigned)CASES, runs);
    // An invalid SOURCE_DATE_EPOCH: the preprocessor's setup fails with a fatal error, which `parse` and -E must print
    // alone, though they defer the history's diagnostics (#412's review).
    char epoch[256] = { 0 };
    const char* had = getenv("SOURCE_DATE_EPOCH");
    if (had != 0)
    {
        snprintf(epoch, sizeof(epoch), "%s", had);
    }
    for (uint32_t preprocess = 0; failures == 0 && preprocess < 2; preprocess += 1)
    {
        char* arguments[] = { (char*)"-E", (char*)"-o", (char*)"/dev/null", paths[0] };
        char text[1024];
        setenv("SOURCE_DATE_EPOCH", "invalid", 1);
        const int status = parse_test_drive(preprocess != 0 ? arguments : arguments + 3, preprocess != 0 ? 4 : 1, capture, text, sizeof(text), preprocess != 0);
        if (had != 0)
        {
            setenv("SOURCE_DATE_EPOCH", epoch, 1);
        }
        else
        {
            unsetenv("SOURCE_DATE_EPOCH");
        }
        if (status != 1 || strcmp(text, "fatal error: invalid SOURCE_DATE_EPOCH: expected a nonnegative decimal integer in range\n") != 0)
        {
            parse_test_report("driver: %s with an invalid SOURCE_DATE_EPOCH: status %d, printed \"%s\"\n", preprocess != 0 ? "-E" : "parse", status, text);
            failures += 1;
        }
    }
    if (failures == 0)
    {
        char* valid[] = { paths[0] };
        char* invalid[] = { paths[2] };
        char* warned[] = { paths[CASES - 2] };
        char* warned_empty[] = { paths[CASES - 1] };
        failures += parse_test_driver_limits(valid, 1, capture, shared, "a valid file", false);
        failures += parse_test_driver_limits(invalid, 1, capture, shared, "an invalid file", false);
        failures += parse_test_driver_limits(warned, 1, capture, shared, "a file with a warning", false);
        failures += parse_test_driver_limits(warned_empty, 1, capture, shared, "a warning and directives alone", false);
        // -E prints the warnings the same way, its status pp_run's (to /dev/null).
        char* preprocess_warned[] = { (char*)"-E", (char*)"-o", (char*)"/dev/null", paths[CASES - 2] };
        char* preprocess_warned_empty[] = { (char*)"-E", (char*)"-o", (char*)"/dev/null", paths[CASES - 1] };
        failures += parse_test_driver_limits(preprocess_warned, 4, capture, shared, "-E of a file with a warning", true);
        failures += parse_test_driver_limits(preprocess_warned_empty, 4, capture, shared, "-E of a warning and directives alone", true);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(ParseTestProbe));
    }
    for (uint32_t c = 0; c < CASES; c += 1)
    {
        if (parse_test_driver_cases[c].input == PARSE_TEST_DRIVER_DIRECTORY)
        {
            rmdir(paths[c]);
        }
        else
        {
            unlink(paths[c]);
        }
    }
    if (capture >= 0)
    {
        close(capture);
    }
    unlink(capture_path);
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

// `test-parser [--clang [PATH]] [--mutations N] [--seed N] [--keep] [--reports N] [options] [file or directory...]`, with
// -E's options for the files; --reports sets how many differences the checks print (200).
static int run_parser_tests(int argument_count, char** arguments)
{
    // SOURCE_DATE_EPOCH fixed where it isn't set, so that the compilers' runs it compares with can be kept
    // (oracle_epoch_begin, oracle_cache.c).
    const bool epoch = oracle_epoch_begin();
    uint32_t failures = 0;
    const char* clang = 0;
    uint64_t mutations = 0;
    uint64_t seed = 1;
    bool keep = false;
    const char* print_tree = 0;
    bool explain = false;
    uint64_t streams = 2000;
    uint32_t first_line = 0;
    uint32_t last_line = UINT32_MAX;
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
        else if (strcmp(argument, "--print-tree") == 0 && i + 1 < argument_count)
        {
            print_tree = arguments[i + 1];
            i += 1;
            if (i + 2 < argument_count && arguments[i + 1][0] >= '0' && arguments[i + 1][0] <= '9')
            {
                first_line = (uint32_t)strtoul(arguments[i + 1], 0, 10);
                last_line = (uint32_t)strtoul(arguments[i + 2], 0, 10);
                i += 2;
            }
        }
        else if (strcmp(argument, "--explain") == 0)
        {
            explain = true;
        }
        else if (strcmp(argument, "--streams") == 0 && i + 1 < argument_count)
        {
            streams = strtoull(arguments[i + 1], 0, 10);
            i += 1;
        }
        else if (strcmp(argument, "--reports") == 0 && i + 1 < argument_count)
        {
            parse_test_report_budget = (uint32_t)strtoul(arguments[i + 1], 0, 10);
            i += 1;
        }
        else if (argument[0] != '-')
        {
            inputs[input_count] = arguments[i];
            input_count += 1;
        }
        else
        {
            // -E's options go to both preprocessors; our own valued options and --scalar stay on our side.
            rest[rest_count] = arguments[i];
            rest_count += 1;
            if (pp_own_option_with_value(argument) && i + 1 < argument_count)
            {
                rest[rest_count] = arguments[i + 1];
                rest_count += 1;
                i += 1;
            }
            else if (strcmp(argument, "--scalar") != 0)
            {
                // Everything but our own options goes to clang too.
                const bool takes = (strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                    strcmp(argument, "-include") == 0 || strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 ||
                                    strcmp(argument, "-x") == 0 || strcmp(argument, "-target") == 0) && i + 1 < argument_count;
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
        printf("usage: test-parser [--clang [PATH]] [--mutations N] [--seed N] [--keep] [--reports N] [--print-tree FILE [FIRST LAST]] [-E's options] [file or directory...]\n");
        failures += 1;
    }
    else if (print_tree != 0)
    {
        failures += parse_test_print_tree(print_tree, &options, first_line, last_line);
        rmdir(directory);
    }
    else
    {
        // The process's oracle cache, set up before any part runs clang through it (oracle_cache.c, #542).
        OracleCache* oracle = oracle_cache();
        failures += parse_test_check_postorder_self(&arena);
        failures += parse_test_check_k6_tables();
        failures += parse_test_check_handoff(options.language, seed);
        failures += parse_test_check_snippets(directory, clang);
        failures += clang != 0 ? parse_test_check_oracle(directory, clang) : 0;
        failures += parse_test_check_fixtures(directory);
        if (input_count == 0)
        {
            // The generated inputs, with -E's options but no includes.
            PPOptions plain = options;
            plain.no_standard_includes = true;
            plain.include_count = 0;
            failures += parse_test_streams(streams, seed, &plain, directory, keep);
            failures += parse_test_alignments(0, &plain, directory);
            failures += parse_test_nesting(&plain, directory);
            failures += parse_test_driver(directory);
        }
        char** files = malloc(sizeof(char*) * (1u << 20));
        uint32_t file_count = 0;
        for (uint32_t i = 0; i < input_count; i += 1)
        {
            file_count = parse_test_collect(inputs[i], files, file_count, 1u << 20);
        }
        if (file_count != 0 && explain)
        {
            failures += parse_test_explain(files, file_count, &options);
        }
        else if (file_count != 0)
        {
            failures += parse_test_files(files, file_count, &options, clang_arguments, clang_argument_count, clang);
        }
        if (file_count != 0 && mutations != 0)
        {
            failures += parse_test_mutations(files, file_count, &options, clang_arguments, clang_argument_count, clang, mutations, seed, directory, keep);
        }
        else if (mutations != 0)
        {
            printf("--mutations needs files\n");
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
        oracle_cache_report(oracle, "test-parser");
    }
    printf("test-parser: %s\n", failures == 0 ? "passed" : "FAILED");
    arena_release(&arena);
    free(rest);
    free(clang_arguments);
    free(inputs);
    const int returned = failures == 0 ? 0 : 1;
    oracle_epoch_end(epoch);
    return returned;
}
