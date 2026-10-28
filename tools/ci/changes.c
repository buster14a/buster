// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The 9700X workflow's selector: what a pull request's job runs on the 9700X, from the files the merge changes and the
// workflow's steps it changes. The `changes` job (.github/workflows/9700x.yml) builds it with the hosted runner's cc,
// checks it (--self-test) and runs it:
//
//   changes --event pull_request --files LIST --base-workflow BASE.yml --workflow HEAD.yml [--root DIR] [--output FILE]
//   changes --event push --workflow HEAD.yml [--output FILE]
//   changes --self-test [--root DIR]
//
// LIST holds the merge's changed paths, one a line (`git diff --no-renames --name-only HEAD^1 HEAD`); BASE.yml is the
// base's workflow, empty where it has none. It appends the outputs to FILE (GITHUB_OUTPUT) as `name=value` lines, in
// `changes_output_names`' order, and prints what it found. The outputs:
//
// - code: whether the merge changes anything but documentation (docs/**, *.md), docs/performance aside;
// - baselines, foreign, branch_sizing, performance, preprocessor_clang, and one per module of `changes_modules`:
//   whether the steps gated on it (`needs.changes.outputs.NAME != 'false'`) run, from the sources they measure or a
//   change to such a step;
// - measure: the plain benchmarks to run, `,NAME,NAME,` by `changes_benchmarks`' names, or `all`.
//
// A push sets code alone and measure to all: its pull request's run measured what it changes. `changes_decide` is the
// decision; `changes_self_test` checks it on fixtures and checks the tables against the workflow and the sources at
// --root (the repository by default). The selector is CI tooling outside the compile path and has no vector path: it
// reads a few hundred lines and files.
//
// Layout: Text and Arena; reading files; ChangesTables (changes_modules, changes_benchmarks); the workflow's steps
// (changes_split_steps); a unit's include closure (changes_includes); changes_decide; changes_print; the self-test;
// main.

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define BUSTER_CHANGES_ARENA_BYTES (UINT64_C(1) << 32)
#define BUSTER_CHANGES_PATH_BYTES 4096
#define BUSTER_CHANGES_WORKFLOW ".github/workflows/9700x.yml"

typedef struct Text Text;
struct Text
{
    const char* data;
    size_t length;
};

typedef struct TextList TextList;
struct TextList
{
    Text* items;
    uint32_t count;
    uint32_t capacity;
};

typedef struct Arena Arena;
struct Arena
{
    char* base;
    size_t used;
    size_t capacity;
};

// Every allocation comes from one reservation, which a run never releases: the process is short.
static Arena changes_arena;

static void changes_fatal(const char* message, const char* detail)
{
    fprintf(stderr, "changes: %s%s%s\n", message, detail != NULL ? ": " : "", detail != NULL ? detail : "");
    exit(2);
}

static void* arena_push(size_t size)
{
    size_t start = (changes_arena.used + 15) & ~(size_t)15;
    if (changes_arena.base == NULL || start > changes_arena.capacity || size > changes_arena.capacity - start)
    {
        changes_fatal("out of memory", NULL);
    }
    changes_arena.used = start + size;
    return changes_arena.base + start;
}

static Text text_c(const char* string)
{
    Text text = {
        .data = string,
        .length = strlen(string),
    };
    return text;
}

static bool text_equal(Text a, Text b)
{
    return a.length == b.length && (a.length == 0 || memcmp(a.data, b.data, a.length) == 0);
}

static bool text_equal_c(Text a, const char* b)
{
    return text_equal(a, text_c(b));
}

static bool text_starts_with(Text text, const char* prefix)
{
    size_t length = strlen(prefix);
    return text.length >= length && memcmp(text.data, prefix, length) == 0;
}

// A copy of the text with a NUL after it, for libc.
static const char* text_terminated(Text text)
{
    char* copy = arena_push(text.length + 1);
    if (text.length != 0)
    {
        memcpy(copy, text.data, text.length);
    }
    copy[text.length] = 0;
    return copy;
}

static Text text_concat(Text a, Text b)
{
    char* data = arena_push(a.length + b.length + 1);
    if (a.length != 0)
    {
        memcpy(data, a.data, a.length);
    }
    if (b.length != 0)
    {
        memcpy(data + a.length, b.data, b.length);
    }
    data[a.length + b.length] = 0;
    Text text = {
        .data = data,
        .length = a.length + b.length,
    };
    return text;
}

static void list_append(TextList* list, Text text)
{
    if (list->count == list->capacity)
    {
        uint32_t capacity = list->capacity != 0 ? list->capacity * 2 : 64;
        Text* items = arena_push(sizeof(Text) * capacity);
        if (list->count != 0)
        {
            memcpy(items, list->items, sizeof(Text) * list->count);
        }
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count] = text;
    list->count += 1;
}

static bool list_contains(const TextList* list, Text text)
{
    bool found = false;
    for (uint32_t i = 0; i < list->count && !found; i += 1)
    {
        found = text_equal(list->items[i], text);
    }
    return found;
}

// The next line of the text from *offset, without its newline; false at the end. A last line without a newline is a
// line, as awk and grep read it.
static bool text_next_line(Text text, size_t* offset, Text* line)
{
    bool found = *offset < text.length;
    if (found)
    {
        const char* start = text.data + *offset;
        const char* end = memchr(start, '\n', text.length - *offset);
        size_t length = end != NULL ? (size_t)(end - start) : text.length - *offset;
        line->data = start;
        line->length = length;
        *offset += length + (end != NULL ? 1 : 0);
    }
    return found;
}

// Each line a NUL-terminated item, for regexec.
static TextList text_lines(Text text)
{
    TextList list = { 0 };
    size_t offset = 0;
    Text line;
    while (text_next_line(text, &offset, &line))
    {
        list_append(&list, text_c(text_terminated(line)));
    }
    return list;
}

static const char* path_join(const char* root, Text path)
{
    char* joined = arena_push(strlen(root) + path.length + 2);
    sprintf(joined, "%s/%.*s", root, (int)path.length, path.data);
    return joined;
}

// The whole file, NUL-terminated; false when it can't be read.
static bool read_file(const char* path, Text* text)
{
    FILE* file = fopen(path, "rb");
    bool result = file != NULL;
    if (result)
    {
        size_t capacity = 1 << 16;
        size_t length = 0;
        char* data = malloc(capacity);
        while (result && data != NULL && !feof(file))
        {
            if (length == capacity)
            {
                capacity *= 2;
                char* grown = realloc(data, capacity);
                if (grown == NULL)
                {
                    free(data);
                }
                data = grown;
            }
            if (data != NULL)
            {
                length += fread(data + length, 1, capacity - length, file);
                result = !ferror(file);
            }
        }
        result = result && data != NULL;
        if (result)
        {
            *text = text_concat((Text){ .data = data, .length = length }, (Text){ 0 });
        }
        free(data);
        fclose(file);
    }
    return result;
}

static bool regular_file(const char* path)
{
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

// --- The tables ---

// The outputs whose steps are gated on them besides the modules', in `changes_output_names`' order.
typedef enum ChangesFixed
{
    CHANGES_BASELINES,
    CHANGES_FOREIGN,
    CHANGES_BRANCH_SIZING,
    CHANGES_PERFORMANCE,
    CHANGES_PREPROCESSOR_CLANG,
    CHANGES_FIXED_COUNT,
} ChangesFixed;

static const char* const changes_fixed_names[CHANGES_FIXED_COUNT] = {
    [CHANGES_BASELINES] = "baselines",
    [CHANGES_FOREIGN] = "foreign",
    [CHANGES_BRANCH_SIZING] = "branch_sizing",
    [CHANGES_PERFORMANCE] = "performance",
    [CHANGES_PREPROCESSOR_CLANG] = "preprocessor_clang",
};

typedef struct ChangesModule ChangesModule;
struct ChangesModule
{
    const char* name;
    const char* sources;
};

// Each module's conditioned benchmark samples (`--only`, `--after-scalar`, repeated processes) and the sources they
// measure under src/buster/compiler/, as ERE alternatives of the file's name without `.c`. Tests (*_test.c) don't
// change what the samples measure, and don't count: c_check[a-z0-9_]* matches every checker test, so that a pull
// request that only added fixtures paid the checker's 3 minutes of samples.
static const ChangesModule changes_modules[] = {
    {
        .name = "zen5",
        .sources = "x86_zen5[a-z0-9_]*",
    },
    {
        .name = "ssa",
        .sources = "ssa|ssa_program|ssa_benchmark|dominators",
    },
    {
        .name = "parser_bindings",
        .sources = "c_parser|c_parser_binding_trace|c_parser_bindings[a-z0-9_]*|c_bindings[a-z0-9_]*",
    },
    {
        .name = "environments",
        .sources = "c_environment[a-z0-9_]*",
    },
    {
        .name = "ir",
        .sources = "ir|ir_validate|ir_benchmark",
    },
    {
        .name = "ir_lowering",
        .sources = "ir|ir_validate|ir_lower|ir_lower_emit|ir_lower_benchmark",
    },
    {
        .name = "ir_optimize",
        .sources = "ir|ir_validate|ir_optimize[a-z0-9_]*",
    },
    {
        .name = "checker",
        .sources = "c_check[a-z0-9_]*",
    },
    // #336's binding handoff, the default's samples (direct, alone and after scalar work, by itself and through the
    // pipeline): the reader's retained answers (c_check_binding.c), the events' producers in the reader
    // (check_input_bind and check_input_resolve in c_check_input.c, the enumerators' and the constant readers':
    // docs/c-check-bindings.md's table), the stack and batch resolvers and the parser's replay that it was chosen
    // over. Its repeated processes took 170 s of every pull request that changed any of the checker's sources, which
    // the plain run measures in every mode.
    {
        .name = "checker_bindings",
        .sources = "c_check_binding[a-z0-9_]*|c_check_input|c_check_enum_input|c_check_constant|c_bindings[a-z0-9_]*|"
                   "c_parser_binding_trace|c_parser_bindings[a-z0-9_]*",
    },
    // The rest of #336's samples, the comparison it chose the default by: every mode on each input, and the old
    // default (parser-batch) alone and after scalar work. The modes differ only in the handoffs, which only these
    // sources change: the reader's retained answers and the samples' harness (c_check_binding*.c), the resolvers and
    // the parser's replay. In a day, 15 of the 23 pull requests' runs that paid all of #336's samples, 142 s, changed
    // none of these, only the events' producers, whose change the default's samples measure in about 32.
    {
        .name = "checker_binding_modes",
        .sources = "c_check_binding[a-z_]*|c_bindings[a-z_]*|c_parser_binding_trace|c_parser_bindings[a-z_]*",
    },
    // #395's -g, each path alone and after scalar work, where the rows' producers or their passes change: what the
    // source rows add to the emission (benchmark-check-debug's emit pass), and the unit built from the rows it makes
    // (the build pass: their lines, the rows kept and the sections). The emission and the lowering copy the entries'
    // tokens out, and Tier 0 writes each entry's first record, so a change to them changes the build pass's input.
    {
        .name = "check_debug",
        .sources = "c_check_debug[a-z0-9_]*|c_check_program_emit|c_check_program_lower|debug|tier0",
    },
    // Every pass in the same states (the synthetic raw rows, their lines and the rows kept, besides the two above),
    // where the rows' own passes change: c_check_debug.c's code and debug.c's. Every pass's samples took 22 s, most of
    // it 200 ms of scalar work before each of 21 samples of each pass, where the emit and build passes' take about 10;
    // in a day, 30 of the 39 pull requests' runs that paid them changed neither file, only the emission, the lowering
    // or Tier 0.
    {
        .name = "check_debug_rows",
        .sources = "c_check_debug[a-z_]*|debug",
    },
    {
        .name = "files",
        .sources = "c_files[a-z0-9_]*",
    },
    // #518's attribution of sqlite3.c's compile: the driver's stage clock, the places that mark its stages (the driver
    // and the emission) and measure-compile, which time the whole compiler, the same for every pull request but one
    // that changes how it is timed. The daily run measures main.
    {
        .name = "compile_time",
        .sources = "c_check_clock|c_checker_driver|c_check_program_emit|c_check_program_lower|c_check_program_batch|"
                   "compile_measure",
    },
};

#define BUSTER_CHANGES_MODULE_COUNT (sizeof(changes_modules) / sizeof(changes_modules[0]))

typedef struct ChangesBenchmark ChangesBenchmark;
struct ChangesBenchmark
{
    const char* name;
    // A translation unit of its own (build.sh's *_benchmark.c), whose includes it measures; or, for one in main.c's
    // units, `sources`.
    const char* unit;
    // ERE alternatives of the files under src/buster/compiler/ that define the functions it calls, transitively.
    const char* sources;
};

// The plain benchmarks, by the name their commands take after benchmark- or measure-, in strcmp's order (the
// self-test checks it): each runs where `contains(env.MEASURE, ',NAME,')`, on a pull request that changes what it
// measures or its own step, and on every push.
//
// The `sources` of main.c's units' benchmarks were checked against the units' LLVM IR on 2026-10-28 (#679): from each
// benchmark's command, every function and variable that a function's body or a variable's initializer names, in its
// unit and through the homes' entry points into theirs, reaches functions only in files its list names. Twelve lists
// had missed some: `[a-z_]*` matched no digit (c_check_x86_input.c, unwind_tier0.c), and the emission reaches the
// inline assembler, binding's room and the unwinder, checking the type table's transparent queries, the preprocessor
// its history and the builtin catalog, the streamed output branch sizing, and foreign debug sections zlib. The lists
// still name files their benchmarks don't reach (most of them tests); those only cost a run.
static const ChangesBenchmark changes_benchmarks[] = {
    {
        .name = "assembler",
        .sources = "assembler[a-z0-9_]*|branch_sizing|c_intern|c_lexer|c_predefines|c_preprocessor|c_refine|lane|link|"
                   "object_writer|x86_64_encoder|x86_match|x86_semantics|x86_target[a-z0-9_]*",
    },
    {
        .name = "backend",
        .unit = "tier0_benchmark.c",
    },
    {
        .name = "backend-source",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering[a-z0-9_]*|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|"
                   "c_types|c_types_transparent|preprocessor_fuzz[a-z0-9_]*|tier0[a-z0-9_]*|calling_convention(_builtins)?|"
                   "branch_sizing|unwind[a-z0-9_]*|debug|link|lane|x86_64_encoder|x86_semantics|x86_target[a-z0-9_]*|"
                   "x86_match|assembler[a-z0-9_]*|object_writer|bind|import|runtime",
    },
    {
        .name = "bind",
        .unit = "bind_benchmark.c",
    },
    {
        .name = "branch-sizing",
        .unit = "branch_sizing_benchmark.c",
    },
    {
        .name = "calling-convention",
        .unit = "calling_convention_benchmark.c",
    },
    {
        .name = "check-debug",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|"
                   "c_types_transparent|branch_sizing|calling_convention(_builtins)?|debug|foreign_test|lane|link|object_writer|"
                   "object_writer_test|tier0|unwind[a-z0-9_]*|x86_64_encoder|x86_semantics|x86_target[a-z0-9_]*|"
                   "x86_match|assembler[a-z0-9_]*|bind|import|runtime",
    },
    {
        .name = "check-lanes",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering[a-z0-9_]*|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|"
                   "c_types|c_types_transparent|preprocessor_fuzz[a-z0-9_]*|tier0[a-z0-9_]*|calling_convention(_builtins)?|"
                   "branch_sizing|unwind[a-z0-9_]*|debug|link|object_writer|import|lane|x86_64_encoder|x86_semantics|"
                   "x86_target[a-z0-9_]*|x86_match|assembler[a-z0-9_]*|bind|runtime",
    },
    {
        .name = "checker",
        .unit = "c_checker_benchmark.c",
    },
    {
        .name = "checker-bindings",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|"
                   "c_types_transparent|preprocessor_fuzz[a-z0-9_]*|bind|dynamic|executable|import|lane|link|"
                   "object_writer|runtime|snapshot|x86_64_encoder|x86_target",
    },
    {
        .name = "checker-deferred",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|"
                   "c_types_transparent|preprocessor_fuzz[a-z0-9_]*|bind|dynamic|executable|import|lane|link|"
                   "object_writer|runtime|snapshot|x86_64_encoder|x86_target",
    },
    {
        .name = "checker-source",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|"
                   "c_types_transparent|preprocessor_fuzz[a-z0-9_]*|bind|dynamic|executable|import|lane|link|"
                   "object_writer|runtime|snapshot|x86_64_encoder|x86_target",
    },
    {
        .name = "dataflow",
        .unit = "dataflow_benchmark.c",
    },
    {
        .name = "debug",
        .sources = "debug[a-z0-9_]*|lane",
    },
    {
        .name = "environments",
        .sources = "c_bindings[a-z0-9_]*|c_environment[a-z0-9_]*|c_types[a-z0-9_]*|lane",
    },
    {
        .name = "executable",
        .unit = "executable_benchmark.c",
    },
    {
        .name = "files",
        .unit = "c_files_benchmark.c",
    },
    {
        .name = "foreign",
        .unit = "foreign_benchmark.c",
    },
    {
        .name = "foreign-strings",
        .sources = "foreign[a-z0-9_]*|bind[a-z0-9_]*|dynamic[a-z0-9_]*|executable[a-z0-9_]*|import[a-z0-9_]*|"
                   "link[a-z0-9_]*|object_writer[a-z0-9_]*|runtime|c_lexer[a-z0-9_]*|lane|x86_64_encoder[a-z0-9_]*|"
                   "inflate",
    },
    {
        .name = "gnu",
        .sources = "c_bindings|c_gnu[a-z0-9_]*|c_intern|c_lexer[a-z0-9_]*|c_parser|c_parser_binding_trace|"
                   "c_predefines|c_preprocessor|c_preprocessor_history|c_builtin_catalog|c_refine|lane|x86_target",
    },
    {
        .name = "import",
        .unit = "import_benchmark.c",
    },
    {
        .name = "ir",
        .sources = "ir|ir_validate|ir_benchmark|lane",
    },
    {
        .name = "ir-lowering",
        .sources = "ir|ir_validate|ir_benchmark|ir_lower[a-z0-9_]*|ssa|ssa_ir|dataflow|dominators|tier0[a-z0-9_]*|"
                   "c_types|calling_convention(_builtins)?|link|object_writer|lane|x86_64_encoder|x86_semantics",
    },
    {
        .name = "ir-optimize",
        .sources = "ir|ir_validate|ir_optimize|ir_optimize_benchmark|ir_optimize_test|dominators|lane",
    },
    {
        .name = "ir-optimize-passes",
        .sources = "ir|ir_validate|ir_optimize[a-z0-9_]*|ssa|ssa_ir|dataflow|dominators|lane",
    },
    {
        .name = "lanes",
        .unit = "lane_benchmark.c",
    },
    {
        .name = "lexer",
        .unit = "c_lexer_benchmark.c",
    },
    {
        .name = "link",
        .unit = "link_benchmark.c",
    },
    {
        .name = "lowering",
        .sources = "c_lowering[a-z0-9_]*|lane",
    },
    {
        .name = "lowering-source",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering[a-z0-9_]*|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|"
                   "c_types|c_types_transparent|preprocessor_fuzz[a-z0-9_]*|tier0[a-z0-9_]*|calling_convention(_builtins)?|"
                   "branch_sizing|unwind[a-z0-9_]*|debug|link|object_writer|import|lane|x86_64_encoder|x86_semantics|"
                   "x86_target[a-z0-9_]*|x86_match|assembler[a-z0-9_]*|bind|runtime",
    },
    {
        .name = "object-writer",
        .unit = "object_writer_benchmark.c",
    },
    {
        .name = "parser",
        .unit = "c_parser_benchmark.c",
    },
    {
        .name = "parser-bindings",
        .sources = "c_bindings[a-z0-9_]*|c_gnu|c_intern|c_lexer[a-z0-9_]*|c_parser[a-z0-9_]*|c_predefines|"
                   "c_preprocessor|c_preprocessor_history|c_builtin_catalog|c_refine|lane|x86_target",
    },
    {
        .name = "preprocessor",
        .unit = "c_preprocessor_benchmark.c",
    },
    {
        .name = "preprocessor-fuzz",
        .sources = "preprocessor_fuzz[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_predefines[a-z0-9_]*|c_builtin_catalog|"
                   "c_intern|c_lexer|c_refine|lane|x86_target",
    },
    {
        .name = "refinement",
        .unit = "c_refine_benchmark.c",
    },
    {
        .name = "snapshot",
        .unit = "snapshot_benchmark.c",
    },
    {
        .name = "ssa",
        .sources = "dataflow|dominators|lane|ssa|ssa_benchmark|ssa_program",
    },
    {
        .name = "streaming",
        .sources = "streaming[a-z0-9_]*|unwind[a-z0-9_]*|debug|executable|link[a-z0-9_]*|object_writer[a-z0-9_]*|"
                   "c_types|calling_convention(_builtins)?|lane|tier0|tier0_generate|x86_64_encoder|x86_semantics|branch_sizing",
    },
    {
        .name = "types",
        .unit = "c_types_benchmark.c",
    },
    {
        .name = "unwind",
        .sources = "unwind[a-z0-9_]*|debug[a-z0-9_]*|c_types|calling_convention(_builtins)?|lane|tier0|tier0_generate|"
                   "x86_64_encoder|x86_semantics|branch_sizing",
    },
    {
        .name = "x86-disassembler",
        .unit = "x86_64_disassembler_benchmark.c",
    },
    {
        .name = "x86-encoder",
        .unit = "x86_64_encoder_benchmark.c",
    },
    {
        .name = "x86-semantics",
        .unit = "x86_semantics_benchmark.c",
    },
    {
        .name = "x86-zen5",
        .sources = "x86_64_encoder|x86_zen5[a-z0-9_]*",
    },
};

#define BUSTER_CHANGES_BENCHMARK_COUNT (sizeof(changes_benchmarks) / sizeof(changes_benchmarks[0]))

// The steps whose change runs branch sizing's qualification besides their gate: its benchmark's step is gated on
// MEASURE and on the output both.
static const char* const changes_branch_sizing_steps[] = {
    "Check-out-pinned-branch-sizing-measurement-harnesses",
    "Benchmark-branch-sizing",
};

// A path's ERE, matched against a whole line as grep -Ex matches it.
static regex_t changes_regex(const char* prefix, const char* body, const char* suffix)
{
    char* pattern = arena_push(strlen(prefix) + strlen(body) + strlen(suffix) + 8);
    sprintf(pattern, "^%s(%s)%s$", prefix, body, suffix);
    regex_t regex;
    int error = regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
    if (error != 0)
    {
        changes_fatal("invalid pattern", pattern);
    }
    return regex;
}

// Whether any of the lines matches the regex.
static bool changes_any_matches(const regex_t* regex, const TextList* lines)
{
    bool found = false;
    for (uint32_t i = 0; i < lines->count && !found; i += 1)
    {
        found = regexec(regex, lines->items[i].data, 0, NULL, 0) == 0;
    }
    return found;
}

static bool changes_files_match(const char* prefix, const char* body, const char* suffix, const TextList* lines)
{
    regex_t regex = changes_regex(prefix, body, suffix);
    bool found = changes_any_matches(&regex, lines);
    regfree(&regex);
    return found;
}

// --- The workflow's steps ---

typedef struct ChangesStep ChangesStep;
struct ChangesStep
{
    // The step's name, each run of characters other than letters and digits as one '-'.
    Text name;
    // Its lines, each with its newline, from its `- name:` line to the next step or job; the lines of the steps of
    // one name, one after another.
    Text body;
};

typedef struct ChangesSteps ChangesSteps;
struct ChangesSteps
{
    ChangesStep* items;
    uint32_t count;
};

static Text changes_step_name(Text line)
{
    const size_t prefix = strlen("      - name: ");
    char* name = arena_push(line.length + 1);
    size_t length = 0;
    bool run = false;
    for (size_t i = prefix; i < line.length; i += 1)
    {
        char c = line.data[i];
        bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (word)
        {
            name[length] = c;
            length += 1;
        }
        else if (!run)
        {
            name[length] = '-';
            length += 1;
        }
        run = !word;
    }
    name[length] = 0;
    Text text = {
        .data = name,
        .length = length,
    };
    return text;
}

// Adds the lines from start to end, the step's, to the step of its name: a slice of the workflow, but for a name
// that repeats, whose steps' lines go one after the other, and a last line without a newline, which awk prints with
// one.
static void changes_add_step(ChangesSteps* steps, Text workflow, Text name, size_t start, size_t end)
{
    Text body = {
        .data = workflow.data + start,
        .length = end - start,
    };
    if (body.length != 0 && body.data[body.length - 1] != '\n')
    {
        body = text_concat(body, text_c("\n"));
    }
    ChangesStep* step = NULL;
    for (uint32_t i = 0; i < steps->count && step == NULL; i += 1)
    {
        step = text_equal(steps->items[i].name, name) ? &steps->items[i] : NULL;
    }
    if (step == NULL)
    {
        step = &steps->items[steps->count];
        step->name = name;
        step->body = body;
        steps->count += 1;
    }
    else
    {
        step->body = text_concat(step->body, body);
    }
}

// The workflow's named steps, as awk split them before: a step starts at a line `      - name: NAME`, and ends at the
// next line that starts a job's key (two spaces, then not a space) or another step (`      - `).
static ChangesSteps changes_split_steps(Text workflow)
{
    uint32_t capacity = 0;
    size_t offset = 0;
    Text line;
    while (text_next_line(workflow, &offset, &line))
    {
        capacity += text_starts_with(line, "      - name: ") ? 1 : 0;
    }
    ChangesSteps steps = {
        .items = arena_push(sizeof(ChangesStep) * (capacity + 1)),
        .count = 0,
    };
    bool open = false;
    Text name = { 0 };
    size_t start = 0;
    offset = 0;
    size_t line_start = 0;
    while (text_next_line(workflow, &offset, &line))
    {
        bool job_key = line.length >= 3 && line.data[0] == ' ' && line.data[1] == ' ' && line.data[2] != ' ';
        if (open && (job_key || text_starts_with(line, "      - ")))
        {
            changes_add_step(&steps, workflow, name, start, line_start);
            open = false;
        }
        if (text_starts_with(line, "      - name: "))
        {
            name = changes_step_name(line);
            start = line_start;
            open = true;
        }
        line_start = offset;
    }
    if (open)
    {
        changes_add_step(&steps, workflow, name, start, workflow.length);
    }
    return steps;
}

static const ChangesStep* changes_find_step(const ChangesSteps* steps, Text name)
{
    const ChangesStep* found = NULL;
    for (uint32_t i = 0; i < steps->count && found == NULL; i += 1)
    {
        found = text_equal(steps->items[i].name, name) ? &steps->items[i] : NULL;
    }
    return found;
}

// Each NAME of PREFIX NAME in the text that the suffix follows (any, when it is empty): NAME the longest run of
// lowercase letters, digits and '-' (with dashes, as benchmarks' names) or '_' (without, as outputs' names).
static void changes_scan_names(Text text, const char* prefix, bool dashes, const char* suffix, TextList* names)
{
    size_t prefix_length = strlen(prefix);
    size_t suffix_length = strlen(suffix);
    for (size_t i = 0; i + prefix_length <= text.length; i += 1)
    {
        if (memcmp(text.data + i, prefix, prefix_length) == 0)
        {
            size_t start = i + prefix_length;
            size_t end = start;
            bool more = true;
            while (end < text.length && more)
            {
                char c = text.data[end];
                more = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == (dashes ? '-' : '_');
                end += more ? 1 : 0;
            }
            bool followed = text.length - end >= suffix_length && memcmp(text.data + end, suffix, suffix_length) == 0;
            if (end > start && followed)
            {
                list_append(names, (Text){ .data = text.data + start, .length = end - start });
            }
        }
    }
}

// The rest of the line past the prefix and the spaces after it, or the line itself without the prefix.
static Text changes_skip(Text line, const char* prefix)
{
    bool matched = text_starts_with(line, prefix);
    size_t skip = matched ? strlen(prefix) : 0;
    while (matched && skip < line.length && line.data[skip] == ' ')
    {
        skip += 1;
    }
    Text rest = {
        .data = line.data + skip,
        .length = line.length - skip,
    };
    return rest;
}

// The count of spaces the line starts with.
static size_t changes_indent(Text line)
{
    size_t indent = 0;
    while (indent < line.length && line.data[indent] == ' ')
    {
        indent += 1;
    }
    return indent;
}

// The words that end the here-documents the line starts, in order: each `<<WORD`, `<<-WORD`, `<<'WORD'` or
// `<<"WORD"`, but not a here-string's `<<<`, `<<-`'s as `-WORD`. A `<<` in a string or a comment counts too, which can
// only leave a command out of the step's.
static void changes_heredocs(Text line, TextList* words)
{
    size_t i = 0;
    while (i + 2 <= line.length)
    {
        bool starts = line.data[i] == '<' && line.data[i + 1] == '<' && (i + 2 == line.length || line.data[i + 2] != '<');
        bool string = line.data[i] == '<' && line.data[i + 1] == '<' && !starts;
        if (starts)
        {
            size_t at = i + 2;
            bool tabs = at < line.length && line.data[at] == '-';
            at += tabs ? 1 : 0;
            while (at < line.length && line.data[at] == ' ')
            {
                at += 1;
            }
            at += at < line.length && (line.data[at] == '\'' || line.data[at] == '"') ? 1 : 0;
            size_t start = at;
            bool more = true;
            while (at < line.length && more)
            {
                char c = line.data[at];
                more = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
                at += more ? 1 : 0;
            }
            if (at > start)
            {
                Text word = {
                    .data = line.data + start,
                    .length = at - start,
                };
                list_append(words, tabs ? text_concat(text_c("-"), word) : word);
            }
            i = at;
        }
        else
        {
            i += string ? 3 : 1;
        }
    }
}

// The benchmark the line runs, if it runs one: `./build/compiler benchmark-NAME` or `measure-NAME` after the line's
// indentation, a `run:` and a `taskset -c CPUS`, with NAME ending the line or followed by a space.
static void changes_step_line(Text line, TextList* commands)
{
    Text rest = changes_skip(changes_skip(line, ""), "run:");
    if (text_starts_with(rest, "taskset -c "))
    {
        rest = changes_skip(rest, "taskset -c ");
        size_t cpus = 0;
        while (cpus < rest.length && rest.data[cpus] != ' ')
        {
            cpus += 1;
        }
        rest = changes_skip((Text){ .data = rest.data + cpus, .length = rest.length - cpus }, " ");
    }
    const char* prefix = text_starts_with(rest, "./build/compiler benchmark-") ? "./build/compiler benchmark-" : "./build/compiler measure-";
    if (text_starts_with(rest, prefix))
    {
        TextList names = { 0 };
        changes_scan_names(rest, prefix, true, "", &names);
        size_t end = strlen(prefix) + (names.count != 0 ? names.items[0].length : 0);
        if (names.count != 0 && (end == rest.length || rest.data[end] == ' '))
        {
            list_append(commands, names.items[0]);
        }
    }
}

// Whether the line ends the here-document of the word: as the shell reads it, after the YAML block's indentation,
// the word alone, after tabs for `<<-`'s. Spaces the shell would see before it, or after it, make it the body's.
static bool changes_heredoc_ends(Text line, Text word, size_t block)
{
    bool tabs = word.length != 0 && word.data[0] == '-';
    Text bare = {
        .data = word.data + (tabs ? 1 : 0),
        .length = word.length - (tabs ? 1 : 0),
    };
    bool indented = block != SIZE_MAX && line.length >= block && changes_indent(line) >= block;
    size_t at = indented ? block : 0;
    while (indented && tabs && at < line.length && line.data[at] == '\t')
    {
        at += 1;
    }
    Text rest = {
        .data = line.data + at,
        .length = line.length - at,
    };
    return indented && text_equal(rest, bare);
}

// Where a line of a script leaves the shell's quoting: outside quotes (0), in single quotes (1), double quotes (2),
// ANSI-C quotes (3) or backquotes (4), from where the line before left it; the parentheses open outside quotes, its
// `(`s less its `)`s added to *depth, which stays below 0 once a `)` closes none (a `case` pattern's, whose matching
// isn't read: a later `(` mustn't seem to close it, so the script's lines after it count nothing); and whether the
// line ends in a backslash outside quotes, which joins the next line to its command. A comment, from a `#` that starts
// a word outside quotes, is read no further.
static uint32_t changes_shell_line(Text line, uint32_t quote, int64_t* depth, bool* continued)
{
    bool comment = false;
    *continued = false;
    size_t i = 0;
    while (i < line.length && !comment)
    {
        char c = line.data[i];
        char before = i != 0 ? line.data[i - 1] : ' ';
        bool escape = c == '\\' && quote != 1;
        *continued = escape && quote == 0 && i + 1 == line.length;
        bool word = before == ' ' || before == '\t' || before == ';' || before == '&' || before == '|' || before == '(' || before == ')';
        comment = quote == 0 && c == '#' && word;
        uint32_t opened = c == '\'' ? 1 : c == '"' ? 2 : c == '`' ? 4 : 0;
        opened = c == '$' && i + 1 < line.length && line.data[i + 1] == '\'' ? 3 : opened;
        bool closes = (quote == 1 && c == '\'') || (quote == 2 && c == '"') || (quote == 3 && c == '\'') || (quote == 4 && c == '`');
        bool bare = quote == 0 && !escape && !comment;
        int64_t step = bare && c == '(' ? 1 : bare && c == ')' ? -1 : 0;
        *depth = *depth < 0 ? *depth : *depth + step;
        uint32_t next = bare ? opened : quote;
        next = quote != 0 && !escape && closes ? 0 : next;
        i += escape || (quote == 0 && opened == 3) ? 2 : 1;
        quote = next;
    }
    return quote;
}

// The benchmarks a step's commands run, read from its `run:` key alone (at the step's keys' indentation, two past its
// `- name:` line's): the key's own line when it holds a plain scalar, whose continuation lines YAML folds into it, and
// each line of a `run: |` block, to the next line no more indented than the keys. A folded block (`run: >`) and other
// keys' values (`env:`, `with:`) run nothing here. A line runs `./build/compiler benchmark-NAME` or `measure-NAME`
// itself, after its indentation, a `run:` and a `taskset -c CPUS`, with NAME ending the line or followed by a space.
// The text of a comment, a string, another command's arguments or a here-document's body runs nothing: a line counts
// only where the shell starts a command there, outside quotes that lines before it opened, outside parentheses they
// left open, and not joined to the line before by a backslash (changes_shell_line); and the lines after one that
// starts here-documents are their bodies, up to the line of each one's word as the shell reads it, the indentation of
// the block's first line removed, and the block's rest when one never ends. Lines inside parentheses are an array's
// elements, but also a subshell's or a command substitution's commands, which count no more: a benchmark goes on a
// line of its own, and not after a `case` pattern's unmatched `)`, past which nothing counts. The shell's control flow
// isn't read: a command in a function never called, or after `if false`,
// counts as run. So the commands of the tests' list, which `parallel` reads from one, aren't the step's: a benchmark a
// MEASURE name gates runs in a step of its own, pinned.
static void changes_step_commands(Text body, TextList* commands)
{
    size_t offset = 0;
    Text line;
    TextList heredocs = { 0 };
    uint32_t ended = 0;
    size_t block = SIZE_MAX;
    size_t keys = SIZE_MAX;
    bool literal = false;
    uint32_t quote = 0;
    int64_t depth = 0;
    bool continued = false;
    while (text_next_line(body, &offset, &line))
    {
        size_t indent = changes_indent(line);
        bool blank = indent == line.length;
        keys = keys == SIZE_MAX && !blank ? indent + 2 : keys;
        bool key = !blank && indent <= keys;
        literal = literal && !key;
        block = literal && !blank && block == SIZE_MAX ? indent : block;
        Text value = changes_skip(changes_skip(line, ""), "run:");
        bool run = key && indent == keys && text_starts_with(changes_skip(line, ""), "run:");
        if (literal && ended < heredocs.count)
        {
            ended += changes_heredoc_ends(line, heredocs.items[ended], block) ? 1 : 0;
        }
        else if (literal)
        {
            bool starts = quote == 0 && depth == 0 && !continued;
            changes_heredocs(line, &heredocs);
            if (starts)
            {
                changes_step_line(line, commands);
            }
            quote = changes_shell_line(line, quote, &depth, &continued);
        }
        else if (run)
        {
            literal = text_equal_c(value, "|") || text_equal_c(value, "|-") || text_equal_c(value, "|+");
            block = SIZE_MAX;
            heredocs = (TextList){ 0 };
            ended = 0;
            quote = 0;
            depth = 0;
            continued = false;
            bool plain = value.length != 0 && value.data[0] != '|' && value.data[0] != '>';
            if (plain)
            {
                changes_step_line(line, commands);
            }
        }
    }
}

// The outputs a step is gated on, by `needs.changes.outputs.NAME != 'false'`.
static void changes_step_gates(Text body, TextList* gates)
{
    changes_scan_names(body, "needs.changes.outputs.", false, " != 'false'", gates);
}

// --- A unit's include closure ---

// The repository's files that the translation unit at `unit` (under the root) includes, by `#include "buster/..."`
// under src/, transitively, itself among them; paths as the root's, as the merge's list names them.
static TextList changes_includes(const char* root, Text unit)
{
    TextList seen = { 0 };
    TextList queue = { 0 };
    list_append(&queue, unit);
    for (uint32_t next = 0; next < queue.count; next += 1)
    {
        Text file = queue.items[next];
        const char* path = path_join(root, file);
        Text source;
        if (!list_contains(&seen, file) && regular_file(path) && read_file(path, &source))
        {
            list_append(&seen, file);
            size_t offset = 0;
            Text line;
            while (text_next_line(source, &offset, &line))
            {
                // ^[[:space:]]*#[[:space:]]*include[[:space:]]*"(buster/[^"]*)"
                size_t i = 0;
                while (i < line.length && strchr(" \t\v\f\r", line.data[i]) != NULL)
                {
                    i += 1;
                }
                bool hash = i < line.length && line.data[i] == '#';
                i += hash ? 1 : 0;
                while (hash && i < line.length && strchr(" \t\v\f\r", line.data[i]) != NULL)
                {
                    i += 1;
                }
                Text rest = {
                    .data = line.data + i,
                    .length = line.length - i,
                };
                bool include = hash && text_starts_with(rest, "include");
                i += include ? strlen("include") : 0;
                while (include && i < line.length && strchr(" \t\v\f\r", line.data[i]) != NULL)
                {
                    i += 1;
                }
                rest = (Text){ .data = line.data + i, .length = line.length - i };
                if (include && text_starts_with(rest, "\"buster/"))
                {
                    const char* name = rest.data + 1;
                    const char* close = memchr(name, '"', rest.length - 1);
                    if (close != NULL)
                    {
                        Text included = {
                            .data = name,
                            .length = (size_t)(close - name),
                        };
                        list_append(&queue, text_concat(text_c("src/"), included));
                    }
                }
            }
        }
    }
    return seen;
}

// Each unit's closure, read once per root: a self-test decides a hundred merges over the same sources.
static const char* changes_closure_root;
static bool changes_closure_read[BUSTER_CHANGES_BENCHMARK_COUNT];
static TextList changes_closures[BUSTER_CHANGES_BENCHMARK_COUNT];

static const TextList* changes_unit_closure(const char* root, uint32_t benchmark)
{
    if (changes_closure_root == NULL || strcmp(changes_closure_root, root) != 0)
    {
        changes_closure_root = root;
        memset(changes_closure_read, 0, sizeof(changes_closure_read));
    }
    if (!changes_closure_read[benchmark])
    {
        changes_closures[benchmark] = changes_includes(root, text_concat(text_c("src/buster/compiler/"), text_c(changes_benchmarks[benchmark].unit)));
        changes_closure_read[benchmark] = true;
    }
    return &changes_closures[benchmark];
}

// --- The decision ---

typedef struct ChangesInput ChangesInput;
struct ChangesInput
{
    bool push;
    // The merge's changed paths, each NUL-terminated.
    TextList files;
    Text base_workflow;
    Text head_workflow;
    const char* root;
};

typedef struct ChangesResult ChangesResult;
struct ChangesResult
{
    bool code;
    bool fixed[CHANGES_FIXED_COUNT];
    bool modules[BUSTER_CHANGES_MODULE_COUNT];
    bool all;
    bool benchmarks[BUSTER_CHANGES_BENCHMARK_COUNT];
    // The steps the merge changes or adds, by name.
    TextList changed_steps;
};

static void changes_set_gate(ChangesResult* result, Text gate)
{
    for (uint32_t i = 0; i < CHANGES_FIXED_COUNT; i += 1)
    {
        result->fixed[i] = result->fixed[i] || text_equal_c(gate, changes_fixed_names[i]);
    }
    for (uint32_t i = 0; i < BUSTER_CHANGES_MODULE_COUNT; i += 1)
    {
        result->modules[i] = result->modules[i] || text_equal_c(gate, changes_modules[i].name);
    }
}

static ChangesResult changes_decide(const ChangesInput* input)
{
    ChangesResult result = { 0 };
    if (input->push)
    {
        // push's paths-ignore leaves out documentation alone.
        result.code = true;
        result.all = true;
    }
    else
    {
        const TextList* files = &input->files;
        // docs/performance is an input of the performance readers' qualification (its manifest schema), which runs
        // in `test`: it counts as code.
        regex_t documentation = changes_regex("", "docs/.*|.*\\.md", "");
        for (uint32_t i = 0; i < files->count && !result.code; i += 1)
        {
            bool document = regexec(&documentation, files->items[i].data, 0, NULL, 0) == 0;
            result.code = !document || text_starts_with(files->items[i], "docs/performance/");
        }
        regfree(&documentation);
        result.fixed[CHANGES_BASELINES] = changes_files_match("src/buster/compiler/", "link|floors", "_benchmark\\.c", files);
        result.fixed[CHANGES_FOREIGN] = changes_files_match("src/buster/compiler/", "foreign_benchmark", "\\.c", files);
        result.fixed[CHANGES_BRANCH_SIZING] = changes_files_match("src/buster/compiler/", "x86_64_encoder|branch_sizing(_benchmark|_generate)?", "\\.c", files);
        // The qualification's inputs: the readers and tools, the schema, and the pages the publisher reads at the
        // pull request's commit (tools/performance/publish.c).
        result.fixed[CHANGES_PERFORMANCE] = changes_files_match("", "tools/performance|docs/performance|web/perf", "/.*", files);
        // benchmark-preprocessor's `clang -E` (--clang): clang's times, which only its harness changes.
        result.fixed[CHANGES_PREPROCESSOR_CLANG] = changes_files_match("src/buster/compiler/", "c_preprocessor_benchmark", "\\.c", files);
        TextList measured = { 0 };
        for (uint32_t i = 0; i < files->count; i += 1)
        {
            Text file = files->items[i];
            bool test = file.length >= strlen("_test.c") && memcmp(file.data + file.length - strlen("_test.c"), "_test.c", strlen("_test.c")) == 0;
            if (!test)
            {
                list_append(&measured, file);
            }
        }
        for (uint32_t i = 0; i < BUSTER_CHANGES_MODULE_COUNT; i += 1)
        {
            result.modules[i] = changes_files_match("src/buster/compiler/", changes_modules[i].sources, "\\.c", &measured);
        }
        // The steps of the workflow that the merge changes or adds, and the commands they run. A changed step gated on
        // an output runs, so that its own pull request runs what it changed.
        ChangesSteps base = changes_split_steps(input->base_workflow);
        ChangesSteps head = changes_split_steps(input->head_workflow);
        TextList commands = { 0 };
        for (uint32_t i = 0; i < head.count; i += 1)
        {
            const ChangesStep* step = &head.items[i];
            const ChangesStep* before = changes_find_step(&base, step->name);
            if (before == NULL || !text_equal(before->body, step->body))
            {
                list_append(&result.changed_steps, step->name);
                changes_step_commands(step->body, &commands);
                for (uint32_t j = 0; j < sizeof(changes_branch_sizing_steps) / sizeof(changes_branch_sizing_steps[0]); j += 1)
                {
                    result.fixed[CHANGES_BRANCH_SIZING] = result.fixed[CHANGES_BRANCH_SIZING] || text_equal_c(step->name, changes_branch_sizing_steps[j]);
                }
                TextList gates = { 0 };
                changes_step_gates(step->body, &gates);
                for (uint32_t j = 0; j < gates.count; j += 1)
                {
                    changes_set_gate(&result, gates.items[j]);
                }
            }
        }
        // In byte order, as the steps' files were listed.
        for (uint32_t i = 1; i < result.changed_steps.count; i += 1)
        {
            Text step = result.changed_steps.items[i];
            uint32_t j = i;
            while (j > 0 && strcmp(result.changed_steps.items[j - 1].data, step.data) > 0)
            {
                result.changed_steps.items[j] = result.changed_steps.items[j - 1];
                j -= 1;
            }
            result.changed_steps.items[j] = step;
        }
        // Global compilation inputs affect every plain benchmark. The scalar intrinsic headers are not global
        // inputs here: the full-vector 9700X build does not include them.
        result.all = list_contains(files, text_c("build.sh")) ||
                     list_contains(files, text_c("src/buster/compiler/host_isa.h"));
        for (uint32_t i = 0; i < BUSTER_CHANGES_BENCHMARK_COUNT && !result.all; i += 1)
        {
            const ChangesBenchmark* benchmark = &changes_benchmarks[i];
            bool changed = false;
            if (benchmark->unit != NULL)
            {
                const TextList* closure = changes_unit_closure(input->root, i);
                for (uint32_t j = 0; j < closure->count && !changed; j += 1)
                {
                    changed = list_contains(files, closure->items[j]);
                }
            }
            else
            {
                changed = changes_files_match("src/buster/compiler/", benchmark->sources, "\\.c", files);
            }
            result.benchmarks[i] = changed || list_contains(&commands, text_c(benchmark->name));
        }
    }
    return result;
}

// --- Output ---

static const char* changes_bool(bool value)
{
    return value ? "true" : "false";
}

// The `measure` output: `all`, or `,NAME,NAME,` in the table's order.
static const char* changes_measure(const ChangesResult* result)
{
    size_t bytes = 2;
    for (uint32_t i = 0; i < BUSTER_CHANGES_BENCHMARK_COUNT; i += 1)
    {
        bytes += strlen(changes_benchmarks[i].name) + 1;
    }
    char* measure = arena_push(bytes + 4);
    if (result->all)
    {
        strcpy(measure, "all");
    }
    else
    {
        strcpy(measure, ",");
        for (uint32_t i = 0; i < BUSTER_CHANGES_BENCHMARK_COUNT; i += 1)
        {
            if (result->benchmarks[i])
            {
                strcat(measure, changes_benchmarks[i].name);
                strcat(measure, ",");
            }
        }
    }
    return measure;
}

static void changes_write_outputs(FILE* output, const ChangesResult* result)
{
    fprintf(output, "code=%s\n", changes_bool(result->code));
    for (uint32_t i = 0; i < CHANGES_FIXED_COUNT; i += 1)
    {
        fprintf(output, "%s=%s\n", changes_fixed_names[i], changes_bool(result->fixed[i]));
    }
    for (uint32_t i = 0; i < BUSTER_CHANGES_MODULE_COUNT; i += 1)
    {
        fprintf(output, "%s=%s\n", changes_modules[i].name, changes_bool(result->modules[i]));
    }
    fprintf(output, "measure=%s\n", changes_measure(result));
}

static void changes_print(FILE* output, const ChangesResult* result)
{
    for (uint32_t i = 0; i < result->changed_steps.count; i += 1)
    {
        Text name = result->changed_steps.items[i];
        fprintf(output, "Changes the step %.*s\n", (int)name.length, name.data);
    }
    fprintf(output, "Changes code: %s\n", changes_bool(result->code));
    fprintf(output, "Changes the baselines' harnesses: %s\n", changes_bool(result->fixed[CHANGES_BASELINES]));
    fprintf(output, "Changes the foreign inputs' benchmark: %s\n", changes_bool(result->fixed[CHANGES_FOREIGN]));
    fprintf(output, "Changes the branch-sizing implementation or its qualification's steps: %s\n", changes_bool(result->fixed[CHANGES_BRANCH_SIZING]));
    fprintf(output, "Changes the performance readers, their harnesses' pin or their qualification's steps: %s\n", changes_bool(result->fixed[CHANGES_PERFORMANCE]));
    fprintf(output, "Changes the preprocessor benchmark's clang -E or its step: %s\n", changes_bool(result->fixed[CHANGES_PREPROCESSOR_CLANG]));
    for (uint32_t i = 0; i < BUSTER_CHANGES_MODULE_COUNT; i += 1)
    {
        fprintf(output, "Changes what %s's conditioned benchmark samples measure: %s\n", changes_modules[i].name, changes_bool(result->modules[i]));
    }
    fprintf(output, "Runs the plain benchmarks: %s\n", changes_measure(result));
}

// --- The self-test ---

typedef struct ChangesTest ChangesTest;
struct ChangesTest
{
    uint32_t checks;
    uint32_t failures;
    bool quiet;
};

static void changes_expect(ChangesTest* test, bool condition, const char* what, Text detail)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (!test->quiet)
        {
            fprintf(stderr, "FAIL: %s%s%.*s\n", what, detail.length != 0 ? ": " : "", (int)detail.length, detail.length != 0 ? detail.data : "");
        }
    }
}

static int64_t changes_fixed_index(Text name)
{
    int64_t index = -1;
    for (uint32_t i = 0; i < CHANGES_FIXED_COUNT && index < 0; i += 1)
    {
        index = text_equal_c(name, changes_fixed_names[i]) ? (int64_t)i : -1;
    }
    return index;
}

static int64_t changes_module_index(Text name)
{
    int64_t index = -1;
    for (uint32_t i = 0; i < BUSTER_CHANGES_MODULE_COUNT && index < 0; i += 1)
    {
        index = text_equal_c(name, changes_modules[i].name) ? (int64_t)i : -1;
    }
    return index;
}

static int64_t changes_benchmark_index(Text name)
{
    int64_t index = -1;
    for (uint32_t i = 0; i < BUSTER_CHANGES_BENCHMARK_COUNT && index < 0; i += 1)
    {
        index = text_equal_c(name, changes_benchmarks[i].name) ? (int64_t)i : -1;
    }
    return index;
}

// The output's value by name: code, a fixed one's or a module's.
static bool changes_output(const ChangesResult* result, const char* name)
{
    Text text = text_c(name);
    int64_t fixed = changes_fixed_index(text);
    int64_t module = changes_module_index(text);
    bool value = result->code;
    if (fixed >= 0)
    {
        value = result->fixed[fixed];
    }
    else if (module >= 0)
    {
        value = result->modules[module];
    }
    else if (!text_equal_c(text, "code"))
    {
        changes_fatal("no such output", name);
    }
    return value;
}

static bool changes_measures(const ChangesResult* result, const char* name)
{
    int64_t index = changes_benchmark_index(text_c(name));
    if (index < 0)
    {
        changes_fatal("no such benchmark", name);
    }
    return result->all || result->benchmarks[index];
}

// Whether any output but code is true.
static bool changes_any_gate(const ChangesResult* result)
{
    bool any = false;
    for (uint32_t i = 0; i < CHANGES_FIXED_COUNT; i += 1)
    {
        any = any || result->fixed[i];
    }
    for (uint32_t i = 0; i < BUSTER_CHANGES_MODULE_COUNT; i += 1)
    {
        any = any || result->modules[i];
    }
    return any;
}

// How many benchmarks the result measures by name (not by `all`).
static uint32_t changes_measured_count(const ChangesResult* result)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < BUSTER_CHANGES_BENCHMARK_COUNT; i += 1)
    {
        count += result->benchmarks[i] ? 1 : 0;
    }
    return count;
}

typedef struct ChangesFixture ChangesFixture;
struct ChangesFixture
{
    const char* name;
    // Changed paths, separated by spaces.
    const char* files;
    // Outputs that must be true, and outputs that must be false, separated by spaces ("" for none).
    const char* outputs_true;
    const char* outputs_false;
    // Benchmarks that must be measured, and benchmarks that must not be.
    const char* measured;
    const char* unmeasured;
};

// Pull requests that change files alone, with the outputs and benchmarks each must set. The workflow is unchanged.
static const ChangesFixture changes_fixtures[] = {
    {
        .name = "documentation alone",
        .files = "docs/c-checker.md AGENTS.md docs/notes.txt",
        .outputs_false = "code performance checker files",
        .unmeasured = "checker lexer",
    },
    {
        .name = "docs/performance alone counts as code, for the qualification",
        .files = "docs/performance/buster-manifest-v1.schema.json",
        .outputs_true = "code performance",
        .outputs_false = "checker baselines",
    },
    {
        .name = "web/perf's pages are the publisher's inputs",
        .files = "web/perf/style.css",
        .outputs_true = "code performance",
        .outputs_false = "foreign",
    },
    {
        .name = "tools/performance",
        .files = "tools/performance/core.c",
        .outputs_true = "code performance",
        .outputs_false = "checker",
    },
    {
        .name = "a checker test alone starts no conditioned samples, but the plain runs that check the twins",
        .files = "src/buster/compiler/c_checker_test.c src/buster/compiler/c_check_binding_test.c "
                 "src/buster/compiler/c_check_debug_test.c",
        .outputs_true = "code",
        .outputs_false = "checker checker_bindings checker_binding_modes check_debug check_debug_rows",
        .measured = "checker-source checker-bindings",
    },
    {
        .name = "the reader produces the binding events: the default handoff's samples, not the modes' comparison",
        .files = "src/buster/compiler/c_check_input.c",
        .outputs_true = "checker checker_bindings",
        .outputs_false = "checker_binding_modes parser_bindings check_debug",
        .measured = "checker-bindings checker-deferred lowering-source",
        .unmeasured = "lexer parser",
    },
    {
        .name = "the constant readers produce binding events too",
        .files = "src/buster/compiler/c_check_constant.c src/buster/compiler/c_check_enum_input.c",
        .outputs_true = "checker_bindings",
        .outputs_false = "checker_binding_modes",
    },
    {
        .name = "another checker source runs the checker's samples, not the handoff's",
        .files = "src/buster/compiler/c_check_vla_input.c",
        .outputs_true = "checker",
        .outputs_false = "checker_bindings checker_binding_modes",
    },
    {
        .name = "the binding handoff itself",
        .files = "src/buster/compiler/c_check_binding.c",
        .outputs_true = "checker checker_bindings checker_binding_modes",
    },
    {
        .name = "the handoff samples' harness",
        .files = "src/buster/compiler/c_check_binding_benchmark.c",
        .outputs_true = "checker_bindings checker_binding_modes",
        .measured = "checker-bindings",
    },
    {
        .name = "the resolvers",
        .files = "src/buster/compiler/c_bindings.c",
        .outputs_true = "checker_bindings checker_binding_modes parser_bindings",
    },
    {
        .name = "the parser's replay",
        .files = "src/buster/compiler/c_parser_bindings.c",
        .outputs_true = "checker_bindings checker_binding_modes parser_bindings",
        .outputs_false = "checker",
        .measured = "parser-bindings",
    },
    {
        .name = "a digit in a module's name",
        .files = "src/buster/compiler/x86_zen5.c",
        .outputs_true = "zen5",
        .measured = "x86-zen5",
        .unmeasured = "x86-encoder",
    },
    {
        .name = "the encoder: branch sizing's qualification, and the units that include it",
        .files = "src/buster/compiler/x86_64_encoder.c",
        .outputs_true = "branch_sizing",
        .outputs_false = "zen5",
        .measured = "x86-encoder x86-zen5 branch-sizing assembler",
    },
    {
        .name = "a unit's closure through its includes",
        .files = "src/buster/compiler/c_lexer.c",
        .outputs_false = "preprocessor_clang",
        .measured = "lexer preprocessor",
        .unmeasured = "x86-encoder backend",
    },
    {
        .name = "the preprocessor's times without clang's",
        .files = "src/buster/compiler/c_preprocessor.c",
        .outputs_false = "preprocessor_clang",
        .measured = "preprocessor",
    },
    {
        .name = "the preprocessor benchmark's harness, which times clang -E",
        .files = "src/buster/compiler/c_preprocessor_benchmark.c",
        .outputs_true = "preprocessor_clang",
        .outputs_false = "baselines foreign",
        .measured = "preprocessor",
        .unmeasured = "lexer",
    },
    {
        .name = "the linkers' harnesses",
        .files = "src/buster/compiler/link_benchmark.c src/buster/compiler/floors_benchmark.c",
        .outputs_true = "baselines",
        .outputs_false = "foreign",
        .measured = "link",
    },
    {
        .name = "the foreign linkers' harness",
        .files = "src/buster/compiler/foreign_benchmark.c",
        .outputs_true = "foreign",
        .outputs_false = "baselines",
        .measured = "foreign",
    },
    {
        .name = "SSA and dominators",
        .files = "src/buster/compiler/dominators.c",
        .outputs_true = "ssa",
        .outputs_false = "ir ir_lowering",
        .measured = "ssa ir-lowering",
    },
    {
        .name = "the shared IR",
        .files = "src/buster/compiler/ir.c",
        .outputs_true = "ir ir_lowering ir_optimize",
    },
    {
        .name = "the per-file jobs",
        .files = "src/buster/compiler/c_files.c",
        .outputs_true = "files",
        .measured = "files",
    },
    {
        .name = "Tier 0 writes the entries' first records: the emit and build passes' -g samples, not every pass's",
        .files = "src/buster/compiler/tier0.c",
        .outputs_true = "check_debug",
        .outputs_false = "check_debug_rows checker",
        .measured = "backend backend-source check-debug",
    },
    {
        .name = "the -g rows' own passes",
        .files = "src/buster/compiler/c_check_debug.c",
        .outputs_true = "check_debug check_debug_rows checker",
        .measured = "check-debug",
    },
    {
        .name = "debug.c's unit",
        .files = "src/buster/compiler/debug.c",
        .outputs_true = "check_debug check_debug_rows",
        .outputs_false = "checker",
        .measured = "debug",
    },
    {
        .name = "the backend's shared measurement measures both of its benchmarks",
        .files = "src/buster/compiler/tier0_benchmark_measure.c",
        .measured = "backend backend-source",
        .unmeasured = "checker checker-source",
    },
    {
        .name = "the compile's attribution",
        .files = "src/buster/compiler/compile_measure.c",
        .outputs_true = "code compile_time",
        .outputs_false = "checker files",
    },
    {
        .name = "the driver's stage marks",
        .files = "src/buster/compiler/c_checker_driver.c",
        .outputs_true = "code compile_time",
    },
    {
        .name = "the emission's stage marks",
        .files = "src/buster/compiler/c_check_program_emit.c",
        .outputs_true = "code compile_time check_debug",
        .outputs_false = "check_debug_rows",
    },
    {
        .name = "the expression templates' lowering time",
        .files = "src/buster/compiler/c_check_program_batch.c",
        .outputs_true = "code compile_time",
    },
    {
        .name = "the scalar lowering twin's time",
        .files = "src/buster/compiler/c_check_program_lower.c",
        .outputs_true = "code compile_time",
    },
    {
        .name = "the compiler's code outside the attribution's sources",
        .files = "src/buster/compiler/tier0.c src/buster/compiler/c_checker_driver_test.c",
        .outputs_false = "compile_time",
    },
    {
        .name = "immutable builtin ABI facts reach their consumers",
        .files = "src/buster/compiler/calling_convention_builtins.c",
        .measured = "calling-convention backend backend-source check-debug check-lanes ir-lowering lowering-source "
                    "streaming unwind",
    },
    {
        .name = "a checker source with a digit in its name",
        .files = "src/buster/compiler/c_check_x86_input.c",
        .outputs_true = "checker",
        .measured = "checker-source checker-bindings checker-deferred backend-source check-lanes lowering-source "
                    "check-debug",
    },
    {
        .name = "Tier 0's unwind tables, which the emission and the streamed output write",
        .files = "src/buster/compiler/unwind_tier0.c",
        .measured = "streaming unwind backend-source check-lanes lowering-source check-debug",
    },
    {
        .name = "the streamed output sizes its branches",
        .files = "src/buster/compiler/branch_sizing.c",
        .measured = "streaming unwind",
    },
    {
        .name = "the emission's inline assembly",
        .files = "src/buster/compiler/assembler_words.c",
        .measured = "backend-source check-lanes lowering-source check-debug",
    },
    {
        .name = "the emission's link room",
        .files = "src/buster/compiler/bind.c",
        .measured = "backend-source check-lanes lowering-source check-debug",
    },
    {
        .name = "the type table's transparent queries, which checking makes",
        .files = "src/buster/compiler/c_types_transparent.c",
        .measured = "checker-source checker-bindings checker-deferred backend-source",
    },
    {
        .name = "the preprocessor's history",
        .files = "src/buster/compiler/c_preprocessor_history.c",
        .measured = "gnu parser-bindings",
    },
    {
        .name = "the preprocessor's builtin queries",
        .files = "src/buster/compiler/c_builtin_catalog.c",
        .measured = "gnu parser-bindings",
    },
    {
        .name = "zlib, which foreign debug sections are read through",
        .files = "src/buster/compiler/inflate.c",
        .measured = "foreign-strings",
    },
};

// The names the text's tokens (separated by spaces) give.
static TextList changes_words(const char* words)
{
    TextList list = { 0 };
    Text text = text_c(words != NULL ? words : "");
    size_t i = 0;
    while (i < text.length)
    {
        size_t start = i;
        while (i < text.length && text.data[i] != ' ')
        {
            i += 1;
        }
        if (i > start)
        {
            list_append(&list, text_c(text_terminated((Text){ .data = text.data + start, .length = i - start })));
        }
        i += 1;
    }
    return list;
}

// The workflow with a line added to the step of this name: a pull request that changes that step alone.
static Text changes_edit_step(Text workflow, Text name)
{
    size_t offset = 0;
    size_t after = workflow.length;
    Text line;
    while (after == workflow.length && text_next_line(workflow, &offset, &line))
    {
        after = text_starts_with(line, "      - name: ") && text_equal(changes_step_name(line), name) ? offset : after;
    }
    Text before = {
        .data = workflow.data,
        .length = after,
    };
    Text rest = {
        .data = workflow.data + after,
        .length = workflow.length - after,
    };
    return text_concat(text_concat(before, text_c("        # edited\n")), rest);
}

static void changes_test_fixtures(ChangesTest* test, Text workflow, const char* root)
{
    for (uint32_t i = 0; i < sizeof(changes_fixtures) / sizeof(changes_fixtures[0]); i += 1)
    {
        const ChangesFixture* fixture = &changes_fixtures[i];
        ChangesInput input = {
            .files = changes_words(fixture->files),
            .base_workflow = workflow,
            .head_workflow = workflow,
            .root = root,
        };
        ChangesResult result = changes_decide(&input);
        Text name = text_c(fixture->name);
        changes_expect(test, result.changed_steps.count == 0, "an unchanged workflow changes no step", name);
        TextList outputs = changes_words(fixture->outputs_true);
        for (uint32_t j = 0; j < outputs.count; j += 1)
        {
            changes_expect(test, changes_output(&result, outputs.items[j].data), outputs.items[j].data, name);
        }
        outputs = changes_words(fixture->outputs_false);
        for (uint32_t j = 0; j < outputs.count; j += 1)
        {
            changes_expect(test, !changes_output(&result, outputs.items[j].data), text_concat(text_c("not "), outputs.items[j]).data, name);
        }
        TextList benchmarks = changes_words(fixture->measured);
        for (uint32_t j = 0; j < benchmarks.count; j += 1)
        {
            changes_expect(test, changes_measures(&result, benchmarks.items[j].data), text_concat(text_c("measures "), benchmarks.items[j]).data, name);
        }
        benchmarks = changes_words(fixture->unmeasured);
        for (uint32_t j = 0; j < benchmarks.count; j += 1)
        {
            changes_expect(test, !changes_measures(&result, benchmarks.items[j].data), text_concat(text_c("doesn't measure "), benchmarks.items[j]).data, name);
        }
    }
}

// Pushes, documentation, build.sh, nothing at all, and a workflow the base doesn't have.
static void changes_test_events(ChangesTest* test, Text workflow, const char* root)
{
    ChangesInput push = {
        .push = true,
        .head_workflow = workflow,
        .root = root,
    };
    ChangesResult result = changes_decide(&push);
    changes_expect(test, result.code && result.all && !changes_any_gate(&result), "a push: code, every plain benchmark, no gate", (Text){ 0 });
    changes_expect(test, strcmp(changes_measure(&result), "all") == 0, "a push measures all", (Text){ 0 });

    ChangesInput nothing = {
        .base_workflow = workflow,
        .head_workflow = workflow,
        .root = root,
    };
    result = changes_decide(&nothing);
    changes_expect(test, !result.code && !result.all && !changes_any_gate(&result) && changes_measured_count(&result) == 0, "a merge of nothing", (Text){ 0 });
    changes_expect(test, strcmp(changes_measure(&result), ",") == 0, "a merge of nothing measures ','", (Text){ 0 });

    ChangesInput build = {
        .files = changes_words("build.sh"),
        .base_workflow = workflow,
        .head_workflow = workflow,
        .root = root,
    };
    result = changes_decide(&build);
    changes_expect(test, result.code && result.all && !changes_any_gate(&result), "build.sh runs every plain benchmark", (Text){ 0 });

    ChangesInput host = {
        .files = changes_words("src/buster/compiler/host_isa.h"),
        .base_workflow = workflow,
        .head_workflow = workflow,
        .root = root,
    };
    result = changes_decide(&host);
    changes_expect(test, result.code && result.all && !changes_any_gate(&result), "the host ISA header runs every plain benchmark", (Text){ 0 });
    changes_expect(test, strcmp(changes_measure(&result), "all") == 0, "the host ISA header measures all", (Text){ 0 });

    ChangesInput added = {
        .files = changes_words(BUSTER_CHANGES_WORKFLOW),
        .base_workflow = { .data = "", .length = 0 },
        .head_workflow = workflow,
        .root = root,
    };
    result = changes_decide(&added);
    ChangesSteps steps = changes_split_steps(workflow);
    changes_expect(test, result.changed_steps.count == steps.count, "a new workflow changes every step", (Text){ 0 });
    changes_expect(test, result.code && changes_output(&result, "checker_bindings") && changes_output(&result, "performance"), "a new workflow runs its gated steps", (Text){ 0 });
}

// The workflow and the tables must agree, and each step gated on an output must run on the pull request that changes it
// alone, with the plain benchmarks it runs.
static void changes_test_workflow(ChangesTest* test, Text workflow, const char* root)
{
    for (uint32_t i = 1; i < BUSTER_CHANGES_BENCHMARK_COUNT; i += 1)
    {
        changes_expect(test, strcmp(changes_benchmarks[i - 1].name, changes_benchmarks[i].name) < 0, "benchmarks in strcmp's order", text_c(changes_benchmarks[i].name));
    }
    // Every output the workflow reads must be one the selector writes, and the `changes` job must declare each.
    TextList read = { 0 };
    changes_scan_names(workflow, "needs.changes.outputs.", false, "", &read);
    for (uint32_t i = 0; i < read.count; i += 1)
    {
        Text name = read.items[i];
        bool known = text_equal_c(name, "code") || text_equal_c(name, "measure") || changes_fixed_index(name) >= 0 || changes_module_index(name) >= 0;
        changes_expect(test, known, "the workflow reads an output the selector writes", name);
    }
    TextList declared = { 0 };
    changes_scan_names(workflow, "${{ steps.files.outputs.", false, " }}", &declared);
    TextList written = changes_words("code measure");
    for (uint32_t i = 0; i < CHANGES_FIXED_COUNT; i += 1)
    {
        list_append(&written, text_c(changes_fixed_names[i]));
    }
    for (uint32_t i = 0; i < BUSTER_CHANGES_MODULE_COUNT; i += 1)
    {
        list_append(&written, text_c(changes_modules[i].name));
    }
    for (uint32_t i = 0; i < written.count; i += 1)
    {
        changes_expect(test, list_contains(&declared, written.items[i]), "the changes job declares the output", written.items[i]);
    }
    changes_expect(test, declared.count == written.count, "the changes job declares the selector's outputs alone", (Text){ 0 });
    // Every MEASURE name a step reads must be a benchmark, and every benchmark a name a step reads.
    TextList measured = { 0 };
    changes_scan_names(workflow, "contains(env.MEASURE, ',", true, ",')", &measured);
    for (uint32_t i = 0; i < measured.count; i += 1)
    {
        changes_expect(test, changes_benchmark_index(measured.items[i]) >= 0, "a step's MEASURE name is a benchmark", measured.items[i]);
    }
    for (uint32_t i = 0; i < BUSTER_CHANGES_BENCHMARK_COUNT; i += 1)
    {
        changes_expect(test, list_contains(&measured, text_c(changes_benchmarks[i].name)), "a step runs the benchmark by MEASURE", text_c(changes_benchmarks[i].name));
    }
    // Each step changed alone: it alone changes, it runs where its gates are, and the benchmarks it runs are measured.
    ChangesSteps steps = changes_split_steps(workflow);
    uint32_t gated = 0;
    for (uint32_t i = 0; i < steps.count; i += 1)
    {
        const ChangesStep* step = &steps.items[i];
        ChangesInput input = {
            .files = changes_words(BUSTER_CHANGES_WORKFLOW),
            .base_workflow = workflow,
            .head_workflow = changes_edit_step(workflow, step->name),
            .root = root,
        };
        ChangesResult result = changes_decide(&input);
        changes_expect(test, result.changed_steps.count == 1 && text_equal(result.changed_steps.items[0], step->name), "the edited step alone changes", step->name);
        changes_expect(test, result.code, "a workflow's change is code", step->name);
        TextList gates = { 0 };
        changes_step_gates(step->body, &gates);
        TextList all_gates = { 0 };
        changes_scan_names(step->body, "needs.changes.outputs.", false, "", &all_gates);
        for (uint32_t j = 0; j < gates.count; j += 1)
        {
            changes_expect(test, changes_output(&result, text_terminated(gates.items[j])), text_concat(text_concat(text_c("the edited step runs: "), gates.items[j]), text_c(" in ")).data, step->name);
            gated += 1;
        }
        // A gate the matcher doesn't read (another comparison than != 'false') must not be one that skips the step.
        for (uint32_t j = 0; j < all_gates.count; j += 1)
        {
            bool code_or_measure = text_equal_c(all_gates.items[j], "code") || text_equal_c(all_gates.items[j], "measure");
            changes_expect(test, code_or_measure || list_contains(&gates, all_gates.items[j]) || strstr(text_terminated(step->body), text_concat(text_concat(text_c("needs.changes.outputs."), all_gates.items[j]), text_c(" == 'false'")).data) != NULL, "a gate the matcher reads", step->name);
        }
        TextList names = { 0 };
        changes_scan_names(step->body, "contains(env.MEASURE, ',", true, ",')", &names);
        TextList commands = { 0 };
        changes_step_commands(step->body, &commands);
        // A step finds its benchmarks by their command lines alone (changes_step_commands): one it ran through a
        // wrapper, split over lines, or only spelled in a comment or a string wouldn't run when changed.
        for (uint32_t j = 0; j < names.count; j += 1)
        {
            changes_expect(test, list_contains(&commands, names.items[j]), text_concat(text_concat(text_c("the step runs ./build/compiler benchmark- or measure-"), names.items[j]), text_c(" as a command")).data, step->name);
            changes_expect(test, changes_measures(&result, text_terminated(names.items[j])), "the edited step's benchmark runs", step->name);
        }
    }
    changes_expect(test, gated >= 20, "the workflow has its gated steps", (Text){ 0 });
}

// The workflow with each line that runs the command replaced by the template's lines, at its indentation, each `%`
// the command.
static Text changes_replace_command(Text workflow, const char* command, const char* template, uint32_t* count)
{
    Text result = {
        .data = "",
        .length = 0,
    };
    size_t offset = 0;
    size_t start = 0;
    Text line;
    *count = 0;
    while (text_next_line(workflow, &offset, &line))
    {
        Text rest = changes_skip(changes_skip(line, ""), "taskset -c \"$cpu\"");
        size_t length = strlen(command);
        bool runs = text_starts_with(rest, command) && (rest.length == length || rest.data[length] == ' ');
        if (runs)
        {
            size_t indent = (size_t)(rest.data - line.data);
            Text head = {
                .data = workflow.data + start,
                .length = (size_t)(line.data - (workflow.data + start)) + indent,
            };
            result = text_concat(result, head);
            for (const char* c = template; *c != 0; c += 1)
            {
                Text piece = *c == '%' ? rest : (Text){ .data = c, .length = 1 };
                result = text_concat(result, piece);
                result = *c == '\n' ? text_concat(result, (Text){ .data = line.data, .length = indent }) : result;
            }
            start = (size_t)(line.data + line.length - workflow.data);
            *count += 1;
        }
    }
    return text_concat(result, (Text){ .data = workflow.data + start, .length = workflow.length - start });
}

// The workflow with the step of this name's text from its `run:` key to the end of the command's line replaced, or
// nothing when the step, the key or the command isn't there.
static Text changes_replace_run(Text workflow, const char* name, const char* command, const char* replacement)
{
    const char* text = text_terminated(workflow);
    const char* step = strstr(text, name);
    const char* run = step != NULL ? strstr(step, "        run: |\n") : NULL;
    const char* found = run != NULL ? strstr(run, command) : NULL;
    const char* end = found != NULL ? strchr(found, '\n') : NULL;
    Text result = { 0 };
    if (end != NULL)
    {
        Text before = {
            .data = text,
            .length = (size_t)(run - text),
        };
        Text after = {
            .data = end + 1,
            .length = workflow.length - (size_t)(end + 1 - text),
        };
        result = text_concat(text_concat(before, text_c(replacement)), after);
    }
    return result;
}

// A benchmark's command made text that runs nothing (commented out and echoed, or a here-document's body) must fail
// the workflow's checks: the step would no longer run it.
static void changes_test_mutation(ChangesTest* test, Text workflow, const char* root)
{
    const char* templates[] = {
        "# %\necho \"%\"",
        "cat <<'EOF'\n%\nEOF",
        "cat <<'EOF'\n  EOF\n%\nEOF",
        "echo \"\n%\n\"",
        "args=(\n  %\n)\necho \"${args[@]}\"",
        "case yes in\n  yes)\n    args=(\n      %\n    )\n    echo \"${args[@]}\"\n    ;;\nesac",
    };
    for (uint32_t i = 0; i < sizeof(templates) / sizeof(templates[0]); i += 1)
    {
        uint32_t count = 0;
        Text mutant = changes_replace_command(workflow, "./build/compiler benchmark-types", templates[i], &count);
        changes_expect(test, count == 1 && strstr(text_terminated(mutant), "./build/compiler benchmark-types\n") != NULL, "the mutation replaces benchmark-types' command", text_c(templates[i]));
        ChangesTest mutated = {
            .quiet = true,
        };
        changes_test_workflow(&mutated, mutant, root);
        changes_expect(test, mutated.failures != 0, "the workflow's checks refuse a benchmark's command that runs nothing", text_c(templates[i]));
        ChangesSteps steps = changes_split_steps(mutant);
        const ChangesStep* step = changes_find_step(&steps, text_c("Benchmark-the-C-type-table"));
        TextList commands = { 0 };
        if (step != NULL)
        {
            changes_step_commands(step->body, &commands);
        }
        changes_expect(test, step != NULL && !list_contains(&commands, text_c("types")), "the mutated step runs no benchmark-types", text_c(templates[i]));
    }
    // And the step's script replaced: the command in an `env:` value, or folded into an `echo`'s arguments.
    const char* scripts[] = {
        "        env:\n          NOTE: |\n            ./build/compiler benchmark-types\n        run: echo \"benchmark disabled\"\n",
        "        run: >-\n          echo benchmark-disabled\n          ./build/compiler benchmark-types\n",
    };
    for (uint32_t i = 0; i < sizeof(scripts) / sizeof(scripts[0]); i += 1)
    {
        Text mutant = changes_replace_run(workflow, "      - name: Benchmark the C type table\n", "./build/compiler benchmark-types", scripts[i]);
        changes_expect(test, mutant.length != 0, "the mutation replaces benchmark-types' step's script", text_c(scripts[i]));
        ChangesTest mutated = {
            .quiet = true,
        };
        changes_test_workflow(&mutated, mutant.length != 0 ? mutant : workflow, root);
        changes_expect(test, mutant.length != 0 && mutated.failures != 0, "the workflow's checks refuse a benchmark that only a YAML value spells", text_c(scripts[i]));
    }
}

// Each module's and benchmark's alternatives must name a source that exists, and each unit must be a file: a pattern
// that names nothing selects nothing.
static void changes_test_sources(ChangesTest* test, const char* root)
{
    const char* directory = path_join(root, text_c("src/buster/compiler"));
    DIR* listing = opendir(directory);
    TextList sources = { 0 };
    if (listing != NULL)
    {
        struct dirent* entry = readdir(listing);
        while (entry != NULL)
        {
            Text name = text_c(entry->d_name);
            if (name.length > 2 && memcmp(name.data + name.length - 2, ".c", 2) == 0)
            {
                list_append(&sources, text_c(text_terminated(text_concat(text_c("src/buster/compiler/"), name))));
            }
            entry = readdir(listing);
        }
        closedir(listing);
    }
    changes_expect(test, sources.count > 100, "the sources", text_c(directory));
    for (uint32_t i = 0; i < BUSTER_CHANGES_MODULE_COUNT + BUSTER_CHANGES_BENCHMARK_COUNT; i += 1)
    {
        bool module = i < BUSTER_CHANGES_MODULE_COUNT;
        const char* name = module ? changes_modules[i].name : changes_benchmarks[i - BUSTER_CHANGES_MODULE_COUNT].name;
        const char* unit = module ? NULL : changes_benchmarks[i - BUSTER_CHANGES_MODULE_COUNT].unit;
        const char* patterns = module ? changes_modules[i].sources : changes_benchmarks[i - BUSTER_CHANGES_MODULE_COUNT].sources;
        if (unit != NULL)
        {
            changes_expect(test, regular_file(path_join(directory, text_c(unit))), "a benchmark's unit", text_c(unit));
        }
        else
        {
            const char* start = patterns;
            while (*start != 0)
            {
                const char* end = strchr(start, '|');
                size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
                Text alternative = {
                    .data = start,
                    .length = length,
                };
                bool found = changes_files_match("src/buster/compiler/", text_terminated(alternative), "\\.c", &sources);
                changes_expect(test, found, text_concat(text_c(name), text_c(": an alternative names a source")).data, alternative);
                start += length + (end != NULL ? 1 : 0);
            }
        }
    }
}

// The steps' split, names, commands and gates on a workflow written for them.
static void changes_test_split(ChangesTest* test)
{
    const char* workflow =
        "jobs:\n"
        "  test:\n"
        "    steps:\n"
        "      - name: Benchmark the C++ lexer's (fast) path\n"
        "        if: needs.changes.outputs.zen5 != 'false' || needs.changes.outputs.code == 'false'\n"
        "        run: |\n"
        "          taskset -c 2 ./build/compiler benchmark-x86-zen5 --only vector\n"
        "          ./build/compiler measure-x86-zen5\n"
        "      - uses: actions/checkout@v6\n"
        "        with:\n"
        "          path: x\n"
        "      - name: Benchmark the C++ lexer's (fast) path\n"
        "        run: echo again\n"
        "  # a comment at the jobs' level ends the step\n"
        "        trailing: not a step's\n";
    ChangesSteps steps = changes_split_steps(text_c(workflow));
    changes_expect(test, steps.count == 1, "one name, one step", (Text){ 0 });
    changes_expect(test, steps.count == 1 && text_equal_c(steps.items[0].name, "Benchmark-the-C-lexer-s-fast-path"), "a step's name as awk's gsub writes it", steps.count == 1 ? steps.items[0].name : (Text){ 0 });
    const char* body =
        "      - name: Benchmark the C++ lexer's (fast) path\n"
        "        if: needs.changes.outputs.zen5 != 'false' || needs.changes.outputs.code == 'false'\n"
        "        run: |\n"
        "          taskset -c 2 ./build/compiler benchmark-x86-zen5 --only vector\n"
        "          ./build/compiler measure-x86-zen5\n"
        "      - name: Benchmark the C++ lexer's (fast) path\n"
        "        run: echo again\n";
    changes_expect(test, steps.count == 1 && text_equal_c(steps.items[0].body, body), "a step's lines, both of its name's, to the next step or job", (Text){ 0 });
    TextList gates = { 0 };
    TextList commands = { 0 };
    if (steps.count == 1)
    {
        changes_step_gates(steps.items[0].body, &gates);
        changes_step_commands(steps.items[0].body, &commands);
    }
    changes_expect(test, gates.count == 1 && text_equal_c(gates.items[0], "zen5"), "a gate's name with a digit, and not an == gate", (Text){ 0 });
    changes_expect(test, commands.count == 2 && text_equal_c(commands.items[0], "x86-zen5") && text_equal_c(commands.items[1], "x86-zen5"), "a step's commands", (Text){ 0 });
    // Commands at a line's start, after `run:` and `taskset -c`, and nothing that only spells one: a here-document's
    // body ends at its word alone after the block's indentation (and tabs, for `<<-`), not at one indented further.
    const char* lines =
        "      - name: Fixture\n"
        "        run: ./build/compiler benchmark-lanes\n"
        "        run: taskset -c \"$cpu\" ./build/compiler benchmark-types --seed 1\n"
        "        run: |\n"
        "          ./build/compiler measure-x86-zen5 \\\n"
        "          # ./build/compiler benchmark-ssa\n"
        "          echo \"./build/compiler benchmark-ir\"\n"
        "          run_it ./build/compiler benchmark-dataflow\n"
        "          \"$compiler\" benchmark-bind\n"
        "          ./build/compiler benchmark-backend-x\n"
        "          ./build/compiler benchmark-parser;\n"
        "          ./build/compiler benchmark-\n"
        "          cat <<'EOF'\n"
        "          ./build/compiler benchmark-ssa\n"
        "          EOF\n"
        "          test \"$(tr ',' '\\n' <<< \"$cpus\" | wc -l)\" -ge 8\n"
        "          ./build/compiler measure-tier1-model\n"
        "          cat <<'EOF'\n"
        "            EOF\n"
        "          ./build/compiler benchmark-types\n"
        "          EOF \n"
        "          ./build/compiler benchmark-types\n"
        "          EOF\n"
        "          ./build/compiler parallel <<-TESTS && cat << \"MORE\"\n"
        "          \t./build/compiler benchmark-ir\n"
        "          \tTESTS\n"
        "          ./build/compiler benchmark-dataflow\n"
        "           MORE\n"
        "          \tMORE\n"
        "          ./build/compiler benchmark-ssa\n"
        "          MORE\n"
        "\n"
        "          ./build/compiler benchmark-bind <<EOF\n"
        "          ./build/compiler benchmark-lexer\n";
    TextList spelled = { 0 };
    changes_step_commands(text_c(lines), &spelled);
    const char* expected[] = { "lanes", "types", "x86-zen5", "backend-x", "tier1-model", "bind" };
    bool same = spelled.count == sizeof(expected) / sizeof(expected[0]);
    for (uint32_t i = 0; i < spelled.count && same; i += 1)
    {
        same = text_equal_c(spelled.items[i], expected[i]);
    }
    changes_expect(test, same, "the commands lines run, not those comments, strings, arguments and here-documents spell", (Text){ 0 });
    // A here-document outside a `run: |` block, whose lines the shell reads joined, never ends.
    const char* plain =
        "      - name: Fixture\n"
        "        run: cat <<EOF\n"
        "          EOF\n"
        "          ./build/compiler benchmark-ssa\n";
    TextList unended = { 0 };
    changes_step_commands(text_c(plain), &unended);
    changes_expect(test, unended.count == 0, "a here-document outside a block runs to the step's end", (Text){ 0 });
    // Only the `run:` key's script runs: not another key's value, a nested `run:`, or the lines a folded block joins.
    const char* values =
        "      - name: Fixture\n"
        "        env:\n"
        "          NOTE: |\n"
        "            ./build/compiler benchmark-types\n"
        "          run: ./build/compiler benchmark-ssa\n"
        "        with:\n"
        "          run: |\n"
        "            ./build/compiler benchmark-ir\n"
        "        run: >-\n"
        "          echo benchmark-disabled\n"
        "          ./build/compiler benchmark-types\n"
        "        run: |2\n"
        "          ./build/compiler benchmark-bind\n"
        "        run: |\n"
        "\n"
        "          ./build/compiler benchmark-lanes\n"
        "        env:\n"
        "          NOTE: |\n"
        "            ./build/compiler benchmark-dataflow\n";
    // Lines in quotes that lines before them opened, or joined to the line before, are arguments, not commands.
    const char* quoted =
        "      - name: Fixture\n"
        "        run: |\n"
        "          echo \"\n"
        "          ./build/compiler benchmark-types\n"
        "          \"\n"
        "          echo 'it'\"'\"'s\n"
        "          ./build/compiler benchmark-ssa\n"
        "          '\n"
        "          echo $'a\\'\n"
        "          ./build/compiler benchmark-ir\n"
        "          '\n"
        "          x=`\n"
        "          ./build/compiler benchmark-dataflow\n"
        "          `\n"
        "          echo done \\\n"
        "          ./build/compiler benchmark-bind\n"
        "          echo \"a \\\" # b\"; echo it\\'s # don't\n"
        "          ./build/compiler benchmark-lanes\n"
        "          echo 'a\\\n"
        "          ./build/compiler benchmark-backend\n"
        "          '\n"
        "          args=(\n"
        "            ./build/compiler benchmark-ir\n"
        "          )\n"
        "          x=$( (\n"
        "          ./build/compiler benchmark-dataflow\n"
        "          ) )\n"
        "          f() { echo \"(\"; }; y=$((1 + (2)))\n"
        "          ./build/compiler benchmark-types\n";
    // A `case` pattern's `)` closes no parenthesis: the array after it must not seem to close it, so nothing after it
    // counts (#471's review).
    const char* patterns =
        "      - name: Fixture\n"
        "        run: |\n"
        "          ./build/compiler benchmark-lanes\n"
        "          case yes in\n"
        "            yes)\n"
        "              args=(\n"
        "                ./build/compiler benchmark-types\n"
        "              )\n"
        "              printf '%s\\n' \"${args[@]}\"\n"
        "              ;;\n"
        "          esac\n"
        "          ./build/compiler benchmark-ssa\n";
    TextList cases = { 0 };
    changes_step_commands(text_c(patterns), &cases);
    changes_expect(test, cases.count == 1 && text_equal_c(cases.items[0], "lanes"), "nothing after a case pattern's unmatched parenthesis", (Text){ 0 });
    TextList arguments = { 0 };
    changes_step_commands(text_c(quoted), &arguments);
    changes_expect(test, arguments.count == 2 && text_equal_c(arguments.items[0], "lanes") && text_equal_c(arguments.items[1], "types"), "the lines the shell starts commands on, not quoted or joined ones", (Text){ 0 });
    TextList scripts = { 0 };
    changes_step_commands(text_c(values), &scripts);
    changes_expect(test, scripts.count == 1 && text_equal_c(scripts.items[0], "lanes"), "the run key's script alone, to the next key", (Text){ 0 });
    ChangesSteps none = changes_split_steps((Text){ .data = "", .length = 0 });
    changes_expect(test, none.count == 0, "an empty workflow has no steps", (Text){ 0 });
}

static int changes_self_test(const char* root)
{
    ChangesTest test = { 0 };
    Text workflow;
    bool readable = read_file(path_join(root, text_c(BUSTER_CHANGES_WORKFLOW)), &workflow);
    changes_expect(&test, readable, "the workflow", text_c(BUSTER_CHANGES_WORKFLOW));
    changes_test_split(&test);
    if (readable)
    {
        changes_test_fixtures(&test, workflow, root);
        changes_test_events(&test, workflow, root);
        changes_test_workflow(&test, workflow, root);
        changes_test_mutation(&test, workflow, root);
        changes_test_sources(&test, root);
    }
    printf("changes --self-test: %u checks, %u failures\n", test.checks, test.failures);
    return test.failures != 0 ? 1 : 0;
}

// --- main ---

static const char* changes_usage =
    "usage: changes --event pull_request --files LIST --base-workflow BASE.yml --workflow HEAD.yml [--root DIR] [--output FILE]\n"
    "       changes --event push --workflow HEAD.yml [--output FILE]\n"
    "       changes --self-test [--root DIR]\n";

int main(int argc, char** argv)
{
    void* base = mmap(NULL, BUSTER_CHANGES_ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    changes_arena.base = base != MAP_FAILED ? base : NULL;
    changes_arena.capacity = base != MAP_FAILED ? BUSTER_CHANGES_ARENA_BYTES : 0;
    const char* event = NULL;
    const char* files = NULL;
    const char* base_workflow = NULL;
    const char* head_workflow = NULL;
    const char* root = ".";
    const char* output = NULL;
    bool self_test = false;
    bool valid = true;
    for (int i = 1; i < argc && valid; i += 1)
    {
        const char* value = i + 1 < argc ? argv[i + 1] : NULL;
        bool takes = true;
        if (strcmp(argv[i], "--self-test") == 0)
        {
            self_test = true;
            takes = false;
        }
        else if (strcmp(argv[i], "--event") == 0)
        {
            event = value;
        }
        else if (strcmp(argv[i], "--files") == 0)
        {
            files = value;
        }
        else if (strcmp(argv[i], "--base-workflow") == 0)
        {
            base_workflow = value;
        }
        else if (strcmp(argv[i], "--workflow") == 0)
        {
            head_workflow = value;
        }
        else if (strcmp(argv[i], "--root") == 0)
        {
            root = value;
        }
        else if (strcmp(argv[i], "--output") == 0)
        {
            output = value;
        }
        else
        {
            valid = false;
        }
        valid = valid && (!takes || value != NULL);
        i += takes ? 1 : 0;
    }
    bool push = event != NULL && strcmp(event, "push") == 0;
    bool pull_request = event != NULL && strcmp(event, "pull_request") == 0;
    valid = valid && root != NULL && (self_test ? event == NULL : (push || (pull_request && files != NULL && base_workflow != NULL)) && head_workflow != NULL);
    int status = 2;
    if (!valid)
    {
        fputs(changes_usage, stderr);
    }
    else if (self_test)
    {
        status = changes_self_test(root);
    }
    else
    {
        ChangesInput input = {
            .push = push,
            .root = root,
        };
        Text list = { .data = "", .length = 0 };
        bool read = read_file(head_workflow, &input.head_workflow);
        read = read && (push || (read_file(files, &list) && read_file(base_workflow, &input.base_workflow)));
        if (!read)
        {
            fprintf(stderr, "changes: can't read an input: %s\n", strerror(errno));
        }
        else
        {
            input.files = text_lines(list);
            ChangesResult result = changes_decide(&input);
            FILE* destination = output != NULL ? fopen(output, "a") : stdout;
            if (destination == NULL)
            {
                fprintf(stderr, "changes: can't open %s: %s\n", output, strerror(errno));
            }
            else
            {
                changes_write_outputs(destination, &result);
                bool written = destination == stdout ? fflush(stdout) == 0 : fclose(destination) == 0;
                if (written)
                {
                    changes_print(stdout, &result);
                    status = 0;
                }
                else
                {
                    fprintf(stderr, "changes: can't write the outputs\n");
                }
            }
        }
    }
    return status;
}
