// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The 9700X workflow's selector: what a pull request's job runs on the 9700X, from the files the merge changes and the
// workflow's steps it changes. The `changes` job (.github/workflows/9700x.yml) builds it with the hosted runner's cc,
// checks it (--self-test) and runs it:
//
//   changes --event pull_request --files LIST --base-workflow BASE.yml --workflow HEAD.yml [--root DIR] [--output FILE]
//   changes --event push --workflow HEAD.yml [--output FILE]
//   changes --event push --files LIST --base-workflow BASE.yml --workflow HEAD.yml [--root DIR] [--output FILE]
//   changes --event merge_group --workflow HEAD.yml [--output FILE]
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
// A push sets code alone and measure to all. A push given its first-parent change (the nightly mode's, docs/ci.md) is
// targeted instead: it decides as a pull request on that change, but code is always true: every main revision runs its
// own hosted rows, including documentation-only changes (#1271; plain alone in the nightly mode, #2057), and
// a push of several commits can bring code that its head's own change doesn't show. A merge-queue candidate
// (`merge_group`, #1430) runs the full integration contract whatever it changes, documentation alone included: code,
// measure all and the performance readers' qualification, which its hosted rows' shard 1 runs; the other outputs gate
// the 9700X job's steps, which a candidate never runs. `changes_decide` is the
// decision; `changes_self_test` checks it on fixtures and checks the tables against the workflow and the sources at
// --root (the repository by default), and checks the workflow's policies: the oracle cache's (changes_oracle_policy),
// main's trigger and the hosted scope the evidence job decides (changes_main_policy), the hosted rows' workflow
// (.github/workflows/hosted-correctness.yml, which the test job calls for each row: changes_row_policy) and its shards,
// their list, their N and the counts the test job gives each row alike (changes_shard_policy), and the vector row's probe, target and claims against host_isa.h's
// predicate and the build's flags (changes_vector_policy, #2033). The row workflow's steps are not the selector's: a
// change to that file runs the steps it gates on an output (the performance readers' qualification). The selector is CI
// tooling outside the compile path and has no vector path: it reads a few hundred lines and files.
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
// The hosted correctness row that 9700x.yml's test job calls: its shards, and the build of a portable row's compiler.
#define BUSTER_CHANGES_ROW_WORKFLOW ".github/workflows/hosted-correctness.yml"
#define BUSTER_CHANGES_ROW_WORKFLOW_USES "./" BUSTER_CHANGES_ROW_WORKFLOW
// The test job's rows: both host architectures, `native` everywhere but on pull requests (#2033), and the vector row,
// one call whose sanitizer lists its configurations: a pull request's plain one in place of `native`, and the plain and
// sanitizer configurations as one pool of items wherever the sanitizer rows run (not in the `plain` scope elsewhere).
#define BUSTER_CHANGES_HOST_ARCHES "        host_arch: ${{ fromJSON(github.event_name == 'pull_request' && '[\"x86-64\"]' || '[\"x86-64\", \"native\"]') }}"
#define BUSTER_CHANGES_VECTOR_ROWS "        include: ${{ fromJSON(github.event_name == 'pull_request' && '[{\"host_arch\":\"vector\",\"sanitizer\":\"none\",\"optimization\":3}]' || needs.evidence.outputs.scope == 'plain' && '[]' || '[{\"host_arch\":\"vector\",\"sanitizer\":\"none address thread memory\",\"optimization\":3}]') }}"
// The host predicate, the build's explicit vector target and the vector row's probe of it (changes_vector_policy).
#define BUSTER_CHANGES_HOST_ISA "src/buster/compiler/host_isa.h"
#define BUSTER_CHANGES_SETTINGS "cmake/BusterSettings.cmake"

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
    CHANGES_SQLITE_STABLE,
    CHANGES_FIXED_COUNT,
} ChangesFixed;

static const char* const changes_fixed_names[CHANGES_FIXED_COUNT] = {
    [CHANGES_BASELINES] = "baselines",
    [CHANGES_FOREIGN] = "foreign",
    [CHANGES_BRANCH_SIZING] = "branch_sizing",
    [CHANGES_PERFORMANCE] = "performance",
    [CHANGES_PREPROCESSOR_CLANG] = "preprocessor_clang",
    [CHANGES_SQLITE_STABLE] = "sqlite_stable",
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
        .sources = "c_parser(_impl)?|c_parser_binding_trace|c_parser_bindings[a-z0-9_]*|c_bindings[a-z0-9_]*",
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
        .sources = "ir|ir_validate|ir_optimize[a-z0-9_]*|ir_thread[a-z0-9_]*",
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
    // and the emission), measure-compile, which time the whole compiler, and the baked tables a compile starts with
    // (baked.c, #590), the same for every pull request but one that changes how it is timed. The daily run measures
    // main.
    {
        .name = "compile_time",
        .sources = "c_check_clock|c_checker_driver|c_check_program_emit|c_check_program_lower|c_check_program_batch|"
                   "compile_measure|baked|c_check_input|c_checker|c_parser(_impl)?|lane|c_check_program",
    },
    // #138's and #1189's programs, speedtest1 and Lua's scripts, by both tiers, clang -O0 and -O2 and tcc
    // (measure-tier1-programs): where Tier 1's input or pipeline changes what --tier1 compiles them to (the checker's
    // lowering into typed functions, the emission's Tier 1 step, the IR's lowering, promotion and optimizer, and Tier
    // 1's selection, allocation, frames and emission), or the measurement does (its harness, and profile.c's timed
    // processes, counters and medians, and compile_measure.c's file helpers, which it calls). The daily run measures
    // main, Tier 0's column among them.
    {
        .name = "tier1_programs",
        .sources = "tier1_programs_measure|profile|compile_measure|c_check_program_lower[a-z0-9_]*|c_check_program_emit|c_check_tier1[a-z0-9_]*|ir|"
                   "ir_validate|ir_lower[a-z0-9_]*|ssa|ssa_ir|dominators|ir_optimize[a-z0-9_]*|ir_thread|tier1_[a-z0-9_]*",
    },
    // #1059's paired measurements (measure-helpers): the primitives' kernels and speedtest1 by both tiers, each as the
    // compiler lowers them and with --helper-forms=all, where what they compare changes (the direct forms' admission and
    // the helpers' planning: the helper planner, the atomic builtins' and the asm statements' plans, and the x86
    // semantics rows a placed statement's forms are admitted by) or the measurement does (its harness, the speedtest1
    // build it shares with measure-tier1-programs, and profile.c's and compile_measure.c's timing and files). The daily
    // and dispatched runs measure main.
    {
        .name = "helpers",
        .sources = "helpers_measure|tier1_programs_measure|profile|compile_measure|c_check_program_helpers|c_check_program_atomic|c_check_program_asm|x86_semantics",
    },
};

#define BUSTER_CHANGES_MODULE_COUNT (sizeof(changes_modules) / sizeof(changes_modules[0]))

typedef struct ChangesBenchmark ChangesBenchmark;
struct ChangesBenchmark
{
    const char* name;
    // A translation unit of its own (the build's *_benchmark.c, cmake/BusterSettings.cmake), whose includes it
    // measures; or, for one in main.c's units, `sources`.
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
        .sources = "assembler[a-z0-9_]*|branch_sizing|c_intern|c_lexer|c_predefines|c_preprocessor(_history)?(_impl)?|c_refine|lane|link|"
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
                   "c_types|c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|tier0[a-z0-9_]*|calling_convention(_builtins)?|"
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
                   "c_types_transparent|c_types_sugar|branch_sizing|calling_convention(_builtins)?|debug|foreign_test|lane|link|object_writer|"
                   "object_writer_test|tier0|unwind[a-z0-9_]*|x86_64_encoder|x86_semantics|x86_target[a-z0-9_]*|"
                   "x86_match|assembler[a-z0-9_]*|bind|import|runtime",
    },
    {
        .name = "check-lanes",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering[a-z0-9_]*|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|"
                   "c_types|c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|tier0[a-z0-9_]*|calling_convention(_builtins)?|"
                   "branch_sizing|unwind[a-z0-9_]*|debug|link|object_writer|import|lane|x86_64_encoder|x86_semantics|"
                   "x86_target[a-z0-9_]*|x86_match|assembler[a-z0-9_]*|bind|runtime",
    },
    {
        .name = "check-units",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering[a-z0-9_]*|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|"
                   "c_types|c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|tier0[a-z0-9_]*|calling_convention(_builtins)?|"
                   "branch_sizing|unwind[a-z0-9_]*|debug|link|object_writer|import|lane|x86_64_encoder|x86_semantics|"
                   "x86_target[a-z0-9_]*|x86_match|assembler[a-z0-9_]*|bind|runtime|profile",
    },
    {
        .name = "checker",
        .unit = "c_checker_benchmark.c",
    },
    {
        .name = "checker-bindings",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|"
                   "c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|bind|dynamic|executable(_impl)?|import|lane|link|"
                   "object_writer|runtime|snapshot|x86_64_encoder|x86_target",
    },
    {
        .name = "checker-deferred",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|"
                   "c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|bind|dynamic|executable(_impl)?|import|lane|link|"
                   "object_writer|runtime|snapshot|x86_64_encoder|x86_target",
    },
    {
        .name = "checker-source",
        .sources = "c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|"
                   "c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|bind|dynamic|executable(_impl)?|import|lane|link|"
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
        .name = "dominators",
        .unit = "dominators_benchmark.c",
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
        .sources = "c_bindings|c_gnu[a-z0-9_]*|c_intern|c_lexer[a-z0-9_]*|c_parser(_impl)?|c_parser_binding_trace|"
                   "c_predefines|c_preprocessor(_history)?(_impl)?|c_builtin_catalog|c_refine|lane|x86_target",
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
        // The IR of the functions -c compiles of real C (#131): their preparation's, as benchmark-backend-source's,
        // which decides the typed streams it lowers, and the IR's lowering, verification, promotion and #866's GVN.
        .name = "ir-source",
        .sources = "ir|ir_validate|ir_lower[a-z0-9_]*|ir_source[a-z0-9_]*|ir_optimize[a-z0-9_]*|ssa|ssa_ir|dominators|c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|"
                   "c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|c_lowering[a-z0-9_]*|c_parser[a-z0-9_]*|"
                   "c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|"
                   "tier0[a-z0-9_]*|calling_convention|branch_sizing|unwind[a-z0-9_]*|debug|link|lane|x86_64_encoder|"
                   "x86_semantics|x86_target[a-z0-9_]*|x86_match|assembler[a-z0-9_]*|object_writer|bind|import|runtime",
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
                   "c_types|c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|tier0[a-z0-9_]*|calling_convention(_builtins)?|"
                   "branch_sizing|unwind[a-z0-9_]*|debug|link|object_writer|import|lane|x86_64_encoder|x86_semantics|"
                   "x86_target[a-z0-9_]*|x86_match|assembler[a-z0-9_]*|bind|runtime",
    },
    {
        // #179's measure-mask-spills: its programs (the encoder's records), the Zen 5 harness's counters and the
        // host's features.
        .name = "mask-spills",
        .sources = "mask_spill[a-z0-9_]*|x86_64_encoder|x86_zen5_measure|x86_target",
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
                   "c_preprocessor(_history)?(_impl)?|c_builtin_catalog|c_refine|lane|x86_target",
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
        .sources = "streaming[a-z0-9_]*|unwind[a-z0-9_]*|debug|executable(_impl)?|link[a-z0-9_]*|object_writer[a-z0-9_]*|"
                   "c_types|calling_convention(_builtins)?|lane|tier0|tier0_generate|x86_64_encoder|x86_semantics|branch_sizing",
    },
    {
        // #939's certificates of tier1_cfg.c's allocations of real C's functions: the corpus's preparation, as
        // benchmark-tier1-cfg-source's (benchmark-backend-source's, then the IR's lowering and promotion and the
        // allocation problem's model), the allocation, its certificate's publication and check, and tier1_cfg_verify.
        .name = "tier1-certificate",
        .sources = "tier1_certificate[a-z0-9_]*|tier1_cfg[a-z0-9_]*|tier1_allocate|dataflow|ir|ir_validate|ir_lower[a-z0-9_]*|ir_source[a-z0-9_]*|"
                   "ssa|ssa_ir|dominators|c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|c_lexer[a-z0-9_]*|"
                   "c_lowering[a-z0-9_]*|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|"
                   "c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|tier0[a-z0-9_]*|calling_convention|branch_sizing|unwind[a-z0-9_]*|"
                   "debug|link|lane|x86_64_encoder|x86_semantics|x86_target[a-z0-9_]*|x86_match|assembler[a-z0-9_]*|object_writer|"
                   "bind|import|runtime",
    },
    {
        // #939's certificates of the CFG consumer's code of real C's functions, decoded from its records: the corpus's
        // preparation, as benchmark-tier1-select-source's (benchmark-backend-source's, then the IR's lowering, promotion
        // and optimization), selection, the emission it certifies, its decoding and the checker.
        .name = "tier1-code-certificate",
        .sources = "tier1_certificate[a-z0-9_]*|tier1_code_certificate_benchmark|tier1_emit[a-z0-9_]*|tier1_select[a-z0-9_]*|tier1_frame[a-z0-9_]*|"
                   "tier1_rematerialize|tier1_liveness|tier1_cfg[a-z0-9_]*|tier1_allocate|ir_optimize[a-z0-9_]*|dataflow|ir|ir_validate|ir_lower[a-z0-9_]*|"
                   "ir_source[a-z0-9_]*|ssa|ssa_ir|dominators|c_check[a-z0-9_]*|c_builtin[a-z0-9_]*|c_bindings|c_environment|c_gnu|c_intern|"
                   "c_lexer[a-z0-9_]*|c_lowering[a-z0-9_]*|c_parser[a-z0-9_]*|c_predefines[a-z0-9_]*|c_preprocessor[a-z0-9_]*|c_refine|c_types|"
                   "c_types_transparent|c_types_sugar|preprocessor_fuzz[a-z0-9_]*|tier0[a-z0-9_]*|calling_convention|branch_sizing|unwind[a-z0-9_]*|"
                   "debug|link|lane|x86_64_encoder|x86_semantics|x86_target[a-z0-9_]*|x86_match|assembler[a-z0-9_]*|object_writer|"
                   "bind|import|runtime",
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
    // A push decided on its first-parent change (`files`), as the nightly mode's pushes are.
    bool targeted;
    // A merge-queue candidate (#1430): the full integration contract, whatever it changes.
    bool merge_group;
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
    if (input->merge_group)
    {
        // The queue's candidate is the only per-merge correctness run (#1430): every hosted row whatever it changes,
        // and the performance readers' qualification, which the rows' shard 1 runs. The other gates select the 9700X
        // job's steps, which a candidate never runs (its evidence skips the hardware).
        result.code = true;
        result.all = true;
        result.fixed[CHANGES_PERFORMANCE] = true;
    }
    else if (input->push && !input->targeted)
    {
        // Every main push tests, including documentation alone; measurement selection is separate (#1271).
        result.code = true;
        result.all = true;
    }
    else
    {
        const TextList* files = &input->files;
        // docs/performance is an input of the performance readers' qualification (its manifest schema), which runs
        // in `test`: it counts as code.
        regex_t documentation = changes_regex("", "docs/.*|.*\\.md", "");
        bool warning_only = true;
        bool warning_source = false;
        for (uint32_t i = 0; i < files->count; i += 1)
        {
            Text file = files->items[i];
            bool document = regexec(&documentation, file.data, 0, NULL, 0) == 0 && !text_starts_with(file, "docs/performance/");
            bool test = text_starts_with(file, "src/buster/compiler/") && file.length >= strlen("_test.c") && memcmp(file.data + file.length - strlen("_test.c"), "_test.c", strlen("_test.c")) == 0;
            bool warning = text_equal_c(file, "src/buster/compiler/c_check_common_warnings.c") ||
                           text_equal_c(file, "src/buster/compiler/c_check_literal_warning.c");
            warning_source = warning_source || warning;
            warning_only = warning_only && (warning || document || test);
        }
        // These diagnostic-only sources are expected to leave successful object output unchanged. Other checker
        // sources may legitimately change it, including lowering paths outside the benchmark selectors' tables.
        result.fixed[CHANGES_SQLITE_STABLE] = warning_source && warning_only;
        for (uint32_t i = 0; i < files->count && !result.code; i += 1)
        {
            bool document = regexec(&documentation, files->items[i].data, 0, NULL, 0) == 0;
            result.code = !document || text_starts_with(files->items[i], "docs/performance/");
        }
        regfree(&documentation);
        result.fixed[CHANGES_BASELINES] = changes_files_match("src/buster/compiler/", "(link|floors)_benchmark\\.c|floors_input\\.c|runtime_paths\\.h", "", files);
        result.fixed[CHANGES_FOREIGN] = changes_files_match("src/buster/compiler/", "foreign_benchmark", "\\.c", files);
        result.fixed[CHANGES_BRANCH_SIZING] = changes_files_match("src/buster/compiler/", "x86_64_encoder|branch_sizing(_benchmark|_generate)?", "\\.c", files);
        // The qualification's inputs: the readers and tools, the schema, and the pages the publisher reads at the
        // pull request's commit (tools/performance/publish.c); and its steps and harnesses' pin, which are the hosted
        // row workflow's, whose steps the selector doesn't read: any change to that file runs them.
        result.fixed[CHANGES_PERFORMANCE] = changes_files_match("", "tools/performance|docs/performance|web/perf", "/.*", files) ||
                                            list_contains(files, text_c(BUSTER_CHANGES_ROW_WORKFLOW));
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
        // Global compilation inputs affect every plain benchmark: the build's description (build.sh, CMakeLists.txt
        // and cmake/, whose flags, link and post-link step make the executable) and the host ISA header. The scalar
        // intrinsic headers are not global inputs here: the full-vector 9700X build does not include them.
        result.all = list_contains(files, text_c("build.sh")) || list_contains(files, text_c("CMakeLists.txt")) ||
                     list_contains(files, text_c("src/buster/compiler/host_isa.h"));
        for (uint32_t i = 0; i < files->count && !result.all; i += 1)
        {
            result.all = text_starts_with(files->items[i], "cmake/");
        }
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
        // A targeted push tests regardless: its tree, not its head's change alone, is what main has now.
        result.code = result.code || input->push;
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
        .name = "the hosted row workflow holds the readers' qualification steps and their harnesses' pin",
        .files = BUSTER_CHANGES_ROW_WORKFLOW,
        .outputs_true = "code performance",
        .outputs_false = "checker baselines foreign",
    },
    {
        .name = "Tier 1's program measurement: its harness, its timing and Tier 1's input and pipeline",
        .files = "src/buster/compiler/tier1_programs_measure.c src/buster/compiler/profile.c src/buster/compiler/c_check_program_lower.c "
                 "src/buster/compiler/tier1_select.c src/buster/compiler/ir_lower.c",
        .outputs_true = "code tier1_programs",
    },
    {
        .name = "the helpers' measurement: its harness",
        .files = "src/buster/compiler/helpers_measure.c",
        .outputs_true = "code helpers",
    },
    {
        .name = "the asm statements' plans alone run the helpers' measurement",
        .files = "src/buster/compiler/c_check_program_asm.c",
        .outputs_true = "code helpers",
    },
    {
        .name = "the helpers' tests alone start no helpers measurement",
        .files = "src/buster/compiler/c_check_program_helpers_test.c src/buster/compiler/c_check_program_asm_test.c",
        .outputs_true = "code",
        .outputs_false = "helpers",
    },
    {
        .name = "the timing alone runs Tier 1's program measurement",
        .files = "src/buster/compiler/profile.c",
        .outputs_true = "tier1_programs",
    },
    {
        .name = "the checker's lowering alone feeds Tier 1's programs",
        .files = "src/buster/compiler/c_check_program_lower.c",
        .outputs_true = "tier1_programs",
    },
    {
        .name = "Tier 1's tests alone, or a source Tier 1 doesn't read, start no program measurement",
        .files = "src/buster/compiler/tier1_select_test.c src/buster/compiler/ir_lower_test.c src/buster/compiler/c_lexer.c",
        .outputs_true = "code",
        .outputs_false = "tier1_programs",
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
        .name = "diagnostic-only checker changes expect identical sqlite objects",
        .files = "src/buster/compiler/c_check_common_warnings.c src/buster/compiler/c_check_warning_driver_test.c docs/c-checker.md",
        .outputs_true = "checker sqlite_stable",
    },
    {
        .name = "mixed warning and atomic lowering changes don't require identical objects",
        .files = "src/buster/compiler/c_check_common_warnings.c src/buster/compiler/c_check_program_atomic.c",
        .outputs_true = "checker",
        .outputs_false = "sqlite_stable",
    },
    {
        .name = "a checker semantic change does not require identical objects",
        .files = "src/buster/compiler/c_check_constant.c",
        .outputs_true = "checker",
        .outputs_false = "sqlite_stable",
    },
    {
        .name = "warning tests alone don't assert object identity",
        .files = "src/buster/compiler/c_check_warning_driver_test.c",
        .outputs_false = "sqlite_stable",
    },
    {
        .name = "the reader produces the binding events: the default handoff's samples, not the modes' comparison",
        .files = "src/buster/compiler/c_check_input.c",
        .outputs_true = "checker checker_bindings compile_time",
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
        .name = "the parser implementation keeps the parser, binding and compile campaigns",
        .files = "src/buster/compiler/c_parser_impl.c",
        .outputs_true = "code parser_bindings compile_time",
        .outputs_false = "checker checker_bindings checker_binding_modes preprocessor_clang",
        .measured = "parser parser-bindings gnu checker-source checker-bindings checker-deferred backend-source "
                    "check-debug check-lanes ir-source lowering-source",
        .unmeasured = "lexer preprocessor x86-encoder",
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
        .name = "the preprocessor implementation keeps its runtime consumers",
        .files = "src/buster/compiler/c_preprocessor_impl.c",
        .outputs_true = "code",
        .outputs_false = "preprocessor_clang",
        .measured = "preprocessor parser parser-bindings gnu assembler checker-source backend-source",
        .unmeasured = "lexer x86-encoder",
    },
    {
        .name = "the history implementation keeps diagnostics and source consumers",
        .files = "src/buster/compiler/c_preprocessor_history_impl.c",
        .outputs_true = "code",
        .outputs_false = "preprocessor_clang",
        .measured = "preprocessor parser parser-bindings gnu assembler checker-source backend-source",
        .unmeasured = "lexer x86-encoder",
    },
    {
        .name = "the executable implementation keeps writer and transitive consumer measurements",
        .files = "src/buster/compiler/executable_impl.c",
        .outputs_true = "code",
        .measured = "executable snapshot link streaming foreign-strings checker-source checker-bindings checker-deferred",
        .unmeasured = "lexer preprocessor parser",
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
        .name = "the floors' shared-libc discovery",
        .files = "src/buster/compiler/floors_input.c",
        .outputs_true = "baselines",
    },
    {
        .name = "the installed runtime search paths",
        .files = "src/buster/compiler/runtime_paths.h",
        .outputs_true = "baselines",
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
        .measured = "ir ir-lowering ir-source",
    },
    {
        .name = "the IR of real C's functions",
        .files = "src/buster/compiler/ir_source_benchmark.c",
        .measured = "ir-source",
        .unmeasured = "ir ir-lowering backend-source",
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

// Pushes, documentation, the build's description, nothing at all, and a workflow the base doesn't have.
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

    // A targeted push (the nightly mode's) decides on its first-parent change as a pull request would, but always tests.
    ChangesInput targeted_documentation = {
        .push = true,
        .targeted = true,
        .files = changes_words("docs/ci.md"),
        .base_workflow = workflow,
        .head_workflow = workflow,
        .root = root,
    };
    result = changes_decide(&targeted_documentation);
    changes_expect(test, result.code && !result.all && !changes_any_gate(&result) && changes_measured_count(&result) == 0, "a targeted push of documentation tests and measures nothing", (Text){ 0 });
    ChangesInput targeted_types = {
        .push = true,
        .targeted = true,
        .files = changes_words("src/buster/compiler/c_types.c"),
        .base_workflow = workflow,
        .head_workflow = workflow,
        .root = root,
    };
    ChangesInput pull_types = targeted_types;
    pull_types.push = false;
    pull_types.targeted = false;
    result = changes_decide(&targeted_types);
    ChangesResult pull = changes_decide(&pull_types);
    changes_expect(test, result.code && changes_measures(&result, "types") && !result.all, "a targeted push measures its change's benchmarks", (Text){ 0 });
    changes_expect(test, strcmp(changes_measure(&result), changes_measure(&pull)) == 0 && memcmp(result.modules, pull.modules, sizeof(result.modules)) == 0 && memcmp(result.fixed, pull.fixed, sizeof(result.fixed)) == 0, "a targeted push selects what its pull request would", (Text){ 0 });

    // Both measurement modes must test every main revision, including empty and documentation-only changes (#1271).
    const char* pushed_files[] = {
        "",
        "README.md AGENTS.md",
        "docs/ci.md docs/notes.txt",
        "src/buster/compiler/c_types.c",
        "src/buster/compiler/c_checker_test.c",
        "build.sh CMakeLists.txt cmake/BusterSettings.cmake",
        ".github/workflows/9700x.yml",
        "tools/ci/qualification.c",
    };
    for (uint32_t i = 0; i < sizeof(pushed_files) / sizeof(pushed_files[0]); i += 1)
    {
        for (uint32_t targeted = 0; targeted < 2; targeted += 1)
        {
            ChangesInput revision = {
                .push = true,
                .targeted = targeted != 0,
                .files = changes_words(pushed_files[i]),
                .base_workflow = workflow,
                .head_workflow = workflow,
                .root = root,
            };
            ChangesResult selected = changes_decide(&revision);
            changes_expect(test, selected.code, "every main push tests in either measurement mode", text_c(pushed_files[i]));
        }
    }

    // A merge-queue candidate (#1430) runs the full integration contract whatever it changes, documentation alone and
    // nothing at all included: code, every plain benchmark and the performance readers' qualification (its hosted rows'
    // shard 1), and no gate of the 9700X job's steps, which a candidate never runs.
    const char* candidate_files[] = { "", "README.md docs/ci.md", "src/buster/compiler/c_types.c", "tools/ci/qualification.c" };
    for (uint32_t i = 0; i < sizeof(candidate_files) / sizeof(candidate_files[0]); i += 1)
    {
        ChangesInput candidate = {
            .merge_group = true,
            .files = changes_words(candidate_files[i]),
            .base_workflow = workflow,
            .head_workflow = workflow,
            .root = root,
        };
        ChangesResult full = changes_decide(&candidate);
        changes_expect(test, full.code && full.all && changes_output(&full, "performance"), "a merge-queue candidate runs the full contract", text_c(candidate_files[i]));
        changes_expect(test, strcmp(changes_measure(&full), "all") == 0, "a merge-queue candidate's measure is all", text_c(candidate_files[i]));
        full.fixed[CHANGES_PERFORMANCE] = false;
        changes_expect(test, !changes_any_gate(&full), "a merge-queue candidate opens no 9700X gate", text_c(candidate_files[i]));
    }

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

    ChangesInput description = {
        .files = changes_words("CMakeLists.txt"),
        .base_workflow = workflow,
        .head_workflow = workflow,
        .root = root,
    };
    result = changes_decide(&description);
    changes_expect(test, result.code && result.all && !changes_any_gate(&result), "CMakeLists.txt runs every plain benchmark", (Text){ 0 });

    ChangesInput settings = {
        .files = changes_words("cmake/BusterSettings.cmake"),
        .base_workflow = workflow,
        .head_workflow = workflow,
        .root = root,
    };
    result = changes_decide(&settings);
    changes_expect(test, result.code && result.all && !changes_any_gate(&result), "cmake/'s files run every plain benchmark", (Text){ 0 });
    changes_expect(test, strcmp(changes_measure(&result), "all") == 0, "cmake/'s files measure all", (Text){ 0 });

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
        .files = changes_words(BUSTER_CHANGES_WORKFLOW " " BUSTER_CHANGES_ROW_WORKFLOW),
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
            // This output is data for the comparison inside the step, not a condition that skips the step.
            bool sqlite_data = text_equal_c(all_gates.items[j], "sqlite_stable") &&
                               strstr(text_terminated(step->body), "SQLITE_EXPECT_STABLE: ${{ needs.changes.outputs.sqlite_stable }}") != NULL;
            changes_expect(test, code_or_measure || sqlite_data || list_contains(&gates, all_gates.items[j]) || strstr(text_terminated(step->body), text_concat(text_concat(text_c("needs.changes.outputs."), all_gates.items[j]), text_c(" == 'false'")).data) != NULL, "a gate the matcher reads", step->name);
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

// Reuse on the persistent host is safe only behind independent correctness on both hosted variants of this revision.
// Pull requests run the hosted plain builds alone; pushes to main and the periodic/manual runs add the sanitizer rows
// and the fuzz campaigns. The `evidence` job (tools/ci/nightly.c) must succeed and admit the 9700X job; a nightly
// dispatch may skip its hosted rows only where that job found its commit's own earlier run had passed them (`reused`);
// and skip fuzzing only in the plain scope, as a pull request does. A pull request's 9700X job skips the native
// correctness step (#1272); every other run's runs it before the measurements. Nightly backlog runs reuse the oracle cache, as
// pull requests do; the snapshot's comprehensive run checks it, as the daily run did. The hosted rows are the row
// workflow's jobs (`row`), which the test job calls for both host architectures: its shards check the oracle cache, and
// they and the build of a shared compiler check out the triggering revision, in every checkout of this repository there
// (a `ref` that doesn't follow a `repository`), such as the shards' one-file checkout of the vector draw's script (#2309).
// Keep the periodic/manual full recheck explicit; changes to this policy must update its counterexamples below too.
static bool changes_oracle_policy(Text workflow, Text row)
{
    uint32_t job = 0;
    bool hosted_check = false;
    bool hosted_variants = false;
    bool hosted_vector = false;
    bool hosted_calls = false;
    bool build_revision = false;
    bool shard_revision = false;
    bool other_revision = false;
    bool after_repository = false;
    bool native_needs = false;
    bool native_success = false;
    bool native_modes = false;
    bool native_revision = false;
    // The native correctness step skips pull requests alone (#1272): its `if` comes right after its id.
    bool native_step = false;
    bool after_native_id = false;
    size_t offset = 0;
    Text line;
    while (text_next_line(workflow, &offset, &line))
    {
        if (text_equal_c(line, "  test:"))
        {
            job = 1;
        }
        else if (text_equal_c(line, "  benchmarks:"))
        {
            job = 2;
        }
        else if (line.length > 2 && line.data[0] == ' ' && line.data[1] == ' ' && line.data[2] != ' ' && line.data[2] != '#')
        {
            job = 0;
        }
        hosted_variants |= job == 1 && text_equal_c(line, BUSTER_CHANGES_HOST_ARCHES);
        hosted_vector |= job == 1 && text_equal_c(line, BUSTER_CHANGES_VECTOR_ROWS);
        hosted_calls |= job == 1 && text_equal_c(line, "    uses: " BUSTER_CHANGES_ROW_WORKFLOW_USES);
        native_needs |= job == 2 && text_equal_c(line, "    needs: [changes, evidence, test, fuzz]");
        native_success |= job == 2 && text_equal_c(line, "    if: ${{ !cancelled() && github.repository_id == '1421805693' && (github.event_name != 'pull_request' || github.event.pull_request.draft == false) && needs.evidence.result == 'success' && needs.evidence.outputs.hardware == 'admit' && ((needs.test.result == 'success' && (needs.fuzz.result == 'success' || ((github.event_name == 'pull_request' || (github.event_name == 'workflow_dispatch' && needs.evidence.outputs.scope == 'plain')) && needs.fuzz.result == 'skipped'))) || (github.event_name == 'workflow_dispatch' && needs.evidence.outputs.hosted == 'reused' && needs.test.result == 'skipped' && needs.fuzz.result == 'skipped')) }}");
        native_modes |= job == 2 && text_equal_c(line, "      BUSTER_ORACLE_CACHE: ${{ (github.event_name == 'schedule' || (github.event_name == 'workflow_dispatch' && inputs.purpose != 'backlog')) && 'check' || 'use' }}");
        native_revision |= job == 2 && text_equal_c(line, "          ref: ${{ github.sha }}");
        const bool comment = line.length > 8 && memcmp(line.data, "        #", 9) == 0;
        native_step |= job == 2 && after_native_id && text_equal_c(line, "        if: github.event_name != 'pull_request'");
        after_native_id = job == 2 && (text_equal_c(line, "        id: native") || (after_native_id && comment));
    }
    offset = 0;
    job = 0;
    while (text_next_line(row, &offset, &line))
    {
        if (line.length > 2 && line.data[0] == ' ' && line.data[1] == ' ' && line.data[2] != ' ' && line.data[2] != '#')
        {
            job = text_equal_c(line, "  compiler:") ? 1 : text_equal_c(line, "  shards:") ? 2 : 0;
        }
        else if (line.length > 0 && line.data[0] != ' ' && line.data[0] != '#')
        {
            job = 0;
        }
        hosted_check |= job == 2 && text_equal_c(line, "      BUSTER_ORACLE_CACHE: check");
        build_revision |= job == 1 && text_equal_c(line, "          ref: ${{ github.sha }}");
        shard_revision |= job == 2 && text_equal_c(line, "          ref: ${{ github.sha }}");
        other_revision |= job != 0 && !after_repository && text_starts_with(line, "          ref: ") && !text_equal_c(line, "          ref: ${{ github.sha }}");
        after_repository = text_starts_with(line, "          repository: ");
    }
    return hosted_check && hosted_variants && hosted_vector && hosted_calls && build_revision && shard_revision && !other_revision && native_needs && native_success && native_modes && native_revision && native_step;
}

// The text with its first `before` from `from` on replaced by `after`, and whether there was one: a mutant for the
// policies' counterexamples.
static Text changes_mutant(Text text, const char* from, const char* before, const char* after, bool* found)
{
    const char* original = text_terminated(text);
    const char* section = from != NULL ? strstr(original, from) : original;
    const char* at = section != NULL ? strstr(section, before) : NULL;
    Text changed = text;
    *found = at != NULL;
    if (at != NULL)
    {
        const size_t prefix = (size_t)(at - original);
        const size_t suffix = prefix + strlen(before);
        changed = text_concat(text_concat((Text){ .data = text.data, .length = prefix }, text_c(after)),
                              (Text){ .data = text.data + suffix, .length = text.length - suffix });
    }
    return changed;
}

static void changes_test_oracle_policy(ChangesTest* test, Text workflow, Text row)
{
    changes_expect(test, changes_oracle_policy(workflow, row), "oracle policy: independent hosted checks, gated native reuse and periodic checks", (Text){ 0 });
    const char* source = text_terminated(workflow);
    changes_expect(test, strstr(source, "    types: [opened, reopened, synchronize, ready_for_review, converted_to_draft]") != NULL, "draft transitions trigger a fresh current-revision run", (Text){ 0 });
    changes_expect(test, strstr(source, "tools/ci/qualification.c") != NULL && strstr(source, "PR_DRAFT: ${{ github.event_name == 'pull_request' && github.event.pull_request.draft == true }}") != NULL, "aggregate qualification uses the tested draft-aware policy", (Text){ 0 });
    static const struct
    {
        const char* before;
        const char* after;
    } row_mutants[] = {
        { "      BUSTER_ORACLE_CACHE: check", "      BUSTER_ORACLE_CACHE: use" },
        { "\n  shards:\n", "\n  tests:\n" },
    };
    for (uint32_t i = 0; i < sizeof(row_mutants) / sizeof(row_mutants[0]); i += 1)
    {
        bool found = false;
        Text changed = changes_mutant(row, NULL, row_mutants[i].before, row_mutants[i].after, &found);
        changes_expect(test, found && !changes_oracle_policy(workflow, changed), "oracle policy rejects a weakened hosted row", text_c(row_mutants[i].before));
    }
    static const struct
    {
        const char* before;
        const char* after;
    } mutants[] = {
        { "    uses: " BUSTER_CHANGES_ROW_WORKFLOW_USES, "    uses: ./.github/workflows/absent.yml" },
        { "'[\"x86-64\", \"native\"]'", "'[\"native\"]'" },
        { "'[\"x86-64\", \"native\"]'", "'[\"x86-64\"]'" },
        { "github.event_name == 'pull_request' && '[\"x86-64\"]'", "github.event_name == 'pull_request' && '[]'" },
        { "github.event_name == 'pull_request' && '[\"x86-64\"]'", "github.event_name != 'push' && '[\"x86-64\"]'" },
        { "'[{\"host_arch\":\"vector\",\"sanitizer\":\"none\",\"optimization\":3}]'", "'[]'" },
        { "\"sanitizer\":\"none address thread memory\"", "\"sanitizer\":\"none\"" },
        { "\"sanitizer\":\"none address thread memory\"", "\"sanitizer\":\"none address memory\"" },
        { "    needs: [changes, evidence, test, fuzz]", "    needs: changes" },
        { "    needs: [changes, evidence, test, fuzz]", "    needs: [changes, evidence, test]" },
        { "    needs: [changes, evidence, test, fuzz]", "    needs: [changes, test, fuzz]" },
        // The evidence job must succeed and admit; hosted rows are skipped only by its evidenced reuse, on a dispatch.
        { " && needs.evidence.result == 'success' && needs.evidence.outputs.hardware == 'admit'", " && needs.evidence.outputs.hardware == 'admit'" },
        { "needs.evidence.outputs.hardware == 'admit' &&", "needs.evidence.outputs.hardware != 'skip' &&" },
        { "github.event_name == 'workflow_dispatch' && needs.evidence.outputs.hosted == 'reused' && ", "" },
        { "needs.evidence.outputs.hosted == 'reused' && needs.test.result == 'skipped'", "needs.test.result == 'skipped'" },
        { "(github.event_name == 'workflow_dispatch' && needs.evidence.outputs.scope == 'plain')", "needs.evidence.outputs.scope == 'plain'" },
        { "needs.test.result == 'success'", "needs.test.result != 'failure'" },
        { " && (needs.fuzz.result == 'success' || ((github.event_name == 'pull_request' || (github.event_name == 'workflow_dispatch' && needs.evidence.outputs.scope == 'plain')) && needs.fuzz.result == 'skipped'))", "" },
        { "needs.fuzz.result == 'success'", "needs.fuzz.result != 'failure'" },
        { " && (github.event_name != 'pull_request' || github.event.pull_request.draft == false)", "" },
        { "github.event.pull_request.draft == false", "github.event.pull_request.draft != true" },
        // Pull requests and plain-scope dispatches skip the fuzz campaigns; nothing else may.
        { "(github.event_name == 'pull_request' || (github.event_name == 'workflow_dispatch' && needs.evidence.outputs.scope == 'plain')) && needs.fuzz.result == 'skipped'", "needs.fuzz.result == 'skipped'" },
        { "      BUSTER_ORACLE_CACHE: ${{ (github.event_name == 'schedule' || (github.event_name == 'workflow_dispatch' && inputs.purpose != 'backlog')) && 'check' || 'use' }}",
          "      BUSTER_ORACLE_CACHE: ${{ github.event_name == 'schedule' && 'check' || 'use' }}" },
        { "      BUSTER_ORACLE_CACHE: ${{ (github.event_name == 'schedule' || (github.event_name == 'workflow_dispatch' && inputs.purpose != 'backlog')) && 'check' || 'use' }}",
          "      BUSTER_ORACLE_CACHE: ${{ (github.event_name == 'workflow_dispatch' && inputs.purpose != 'backlog') && 'check' || 'use' }}" },
        { "(github.event_name == 'workflow_dispatch' && inputs.purpose != 'backlog')) && 'check'", "github.event_name == 'workflow_dispatch') && 'check'" },
        { "&& 'check' || 'use'", "&& 'use' || 'use'" },
        // Native correctness is skipped on pull requests alone.
        { "        if: github.event_name != 'pull_request'\n        run: |\n          # The record tells", "        if: github.event_name == 'push'\n        run: |\n          # The record tells" },
        { "        if: github.event_name != 'pull_request'\n        run: |\n          # The record tells", "        if: false\n        run: |\n          # The record tells" },
    };
    const char* original = text_terminated(workflow);
    for (uint32_t i = 0; i < sizeof(mutants) / sizeof(mutants[0]); i += 1)
    {
        const char* found = strstr(original, mutants[i].before);
        Text changed = workflow;
        if (found != NULL)
        {
            const size_t prefix = (size_t)(found - original);
            const size_t suffix = prefix + strlen(mutants[i].before);
            changed = text_concat(text_concat((Text){ .data = workflow.data, .length = prefix }, text_c(mutants[i].after)),
                                  (Text){ .data = workflow.data + suffix, .length = workflow.length - suffix });
        }
        changes_expect(test, found != NULL && !changes_oracle_policy(changed, row), "oracle policy rejects weakened check/reuse boundary", text_c(mutants[i].before));
    }
    // Each job's first checkout, until the next job: the row's build and shards, and the 9700X job; and the shards' own
    // checkout after the one-file checkout of the vector draw's script.
    static const struct
    {
        bool row;
        const char* job;
    } jobs[] = {
        { true, "\n  compiler:\n" },
        { true, "\n  shards:\n" },
        { true, "\n      - name: Check out triggering commit\n        if: env.BUSTER_SHARD != ''\n" },
        { false, "\n  benchmarks:\n" },
    };
    for (uint32_t i = 0; i < sizeof(jobs) / sizeof(jobs[0]); i += 1)
    {
        bool found = false;
        Text changed = changes_mutant(jobs[i].row ? row : workflow, jobs[i].job, "          ref: ${{ github.sha }}", "          ref: main", &found);
        const bool valid = jobs[i].row ? changes_oracle_policy(workflow, changed) : changes_oracle_policy(changed, row);
        changes_expect(test, found && !valid, "oracle policy rejects a different tested revision", text_c(jobs[i].job));
    }
}

// Every main push, including documentation alone, runs its own hosted rows (#1271): every row before the nightly mode,
// the plain rows in it (#2057, until the merge queue carries the full suite; the nightly snapshot runs the sanitizer
// rows and fuzzing). Keep its unfiltered trigger and per-run concurrency separate from a PR's replaceable queue. The
// evidence glue tests pin scope and hosted=run for pushes; here the workflow must consume those decisions without
// reducing the matrix or suppressing fuzzing for a full scope.
// Every merge-queue candidate (#1430), documentation alone included, runs the full contract too: its trigger is
// `checks_requested` for main with no path filter, its concurrency group its own run's (the queue builds several at
// once), its selector the merge-group profile, and the required aggregate runs for it and reads its coverage step,
// which checks the candidate's jobs one by one.
// This pins the repository's YAML spelling, as the oracle policy does; it is not a general YAML evaluator.
static bool changes_main_policy(Text workflow)
{
    static const struct
    {
        uint32_t job;
        const char* line;
    } required[] = {
        { 0, "  group: buster-fast-9700x-${{ github.event.pull_request.number || (github.event_name == 'push' && format('push-{0}', github.run_id)) || (github.event_name == 'merge_group' && format('queue-{0}', github.run_id)) || (github.event_name == 'schedule' && 'schedule') || github.ref }}" },
        { 0, "  cancel-in-progress: ${{ github.event_name == 'pull_request' }}" },
        { 5, "    if: github.event_name == 'pull_request' || github.event_name == 'push' || github.event_name == 'merge_group' || (github.event_name == 'workflow_dispatch' && inputs.purpose == 'backlog')" },
        { 5, "          if [ \"$GITHUB_EVENT_NAME\" = merge_group ]; then" },
        { 5, "              \"$RUNNER_TEMP/changes\" --event merge_group --workflow \"$workflow\" --output \"$GITHUB_OUTPUT\"" },
        { 6, "    if: ${{ always() && needs.evidence.outputs.retired != 'true' && !(github.event_name == 'workflow_dispatch' && (inputs.purpose == 'backlog' || inputs.purpose == 'snapshot')) && !(github.event_name == 'push' && needs.evidence.outputs.hardware_reason == 'deferred') }}" },
        { 6, "      actions: read" },
        { 6, "        if: github.event_name == 'merge_group'" },
        { 6, "        run: bash tools/ci/nightly.sh coverage \"$RUNNER_TEMP/coverage\"" },
        { 6, "          COVERAGE_RESULT: ${{ steps.coverage.outcome }}" },
        { 4, "    needs: [changes, evidence, policy]" },
        { 4, "    if: ${{ !cancelled() && github.repository_id == '1421805693' && needs.evidence.result == 'success' && needs.evidence.outputs.hosted == 'run' && (needs.changes.result == 'skipped' || (needs.changes.result == 'success' && needs.changes.outputs.code != 'false')) && ((github.event_name != 'pull_request' && github.event_name != 'merge_group') || needs.policy.result == 'success') }}" },
        { 4, "    uses: ./.github/workflows/prepare-hosted-tools.yml" },
        { 1, "    needs: [changes, evidence, policy, prepare-hosted-tools]" },
        { 1, "    uses: " BUSTER_CHANGES_ROW_WORKFLOW_USES },
        { 1, "      hosted-tools: ${{ needs.prepare-hosted-tools.outputs.artifact-name }}" },
        { 1, "    if: ${{ !cancelled() && needs.prepare-hosted-tools.result == 'success' && github.repository_id == '1421805693' && needs.evidence.result == 'success' && needs.evidence.outputs.hosted == 'run' && (needs.changes.result == 'skipped' || (needs.changes.result == 'success' && needs.changes.outputs.code != 'false')) && ((github.event_name != 'pull_request' && github.event_name != 'merge_group') || needs.policy.result == 'success') }}" },
        { 1, "      fail-fast: ${{ github.event_name == 'pull_request' || github.event_name == 'merge_group' }}" },
        { 1, BUSTER_CHANGES_HOST_ARCHES },
        { 1, BUSTER_CHANGES_VECTOR_ROWS },
        { 1, "        sanitizer: ${{ fromJSON((github.event_name == 'pull_request' || needs.evidence.outputs.scope == 'plain') && '[\"none\"]' || '[\"none\", \"address\", \"thread\", \"memory\"]') }}" },
        { 1, "        optimization: [3]" },
        { 2, "    needs: [changes, evidence, prepare-hosted-tools]" },
        { 2, "          artifact-name: ${{ needs.prepare-hosted-tools.outputs.artifact-name }}" },
        { 2, "    if: ${{ !cancelled() && needs.prepare-hosted-tools.result == 'success' && github.repository_id == '1421805693' && github.event_name != 'pull_request' && needs.evidence.result == 'success' && needs.evidence.outputs.hosted == 'run' && needs.evidence.outputs.scope == 'full' && (needs.changes.result == 'skipped' || (needs.changes.result == 'success' && needs.changes.outputs.code != 'false')) }}" },
        { 2, "      fail-fast: ${{ github.event_name == 'merge_group' }}" },
        { 2, "          - entry: frontend" },
        { 2, "          - entry: elf-override" },
        { 2, "          - entry: crash-replay-proof" },
        { 2, "          ref: ${{ github.sha }}" },
    };
    bool seen[sizeof(required) / sizeof(required[0])] = { false };
    bool valid = true;
    bool push = false;
    bool queue = false;
    uint32_t pushes = 0;
    uint32_t branches = 0;
    uint32_t queues = 0;
    uint32_t queue_types = 0;
    uint32_t queue_branches = 0;
    uint32_t section = 0;
    uint32_t job = 3;
    size_t offset = 0;
    Text line;
    while (text_next_line(workflow, &offset, &line))
    {
        size_t indent = 0;
        while (indent < line.length && line.data[indent] == ' ')
        {
            indent += 1;
        }
        if (indent != line.length && line.data[indent] != '#')
        {
            if (indent == 0)
            {
                section = text_equal_c(line, "on:") ? 1 : text_equal_c(line, "concurrency:") ? 2 : text_equal_c(line, "jobs:") ? 3 : 0;
                push = false;
                queue = false;
                job = 3;
            }
            else if (section == 1 && indent == 2)
            {
                push = text_equal_c(line, "  push:");
                pushes += push;
                queue = text_equal_c(line, "  merge_group:");
                queues += queue;
            }
            else if (section == 1 && push)
            {
                // The main push has exactly its branch selector, no paths, paths-ignore or other filter.
                const bool branch = text_equal_c(line, "    branches: [main]");
                branches += branch;
                valid = valid && branch;
            }
            else if (section == 1 && queue)
            {
                // A merge-queue candidate for main: the one event type, and no path or other filter.
                const bool types = text_equal_c(line, "    types: [checks_requested]");
                const bool branch = text_equal_c(line, "    branches: [main]");
                queue_types += types;
                queue_branches += branch;
                valid = valid && (types || branch);
            }
            else if (section == 3 && indent == 2)
            {
                job = text_equal_c(line, "  test:") ? 1 : text_equal_c(line, "  fuzz:") ? 2 : text_equal_c(line, "  prepare-hosted-tools:") ? 4 : text_equal_c(line, "  changes:") ? 5 : text_equal_c(line, "  qualification:") ? 6 : 3;
            }
            const uint32_t current = section == 2 ? 0 : section == 3 ? job : 3;
            for (uint32_t i = 0; i < sizeof(required) / sizeof(required[0]); i += 1)
            {
                seen[i] |= current == required[i].job && text_equal_c(line, required[i].line);
            }
        }
    }
    for (uint32_t i = 0; i < sizeof(required) / sizeof(required[0]); i += 1)
    {
        valid = valid && seen[i];
    }
    return valid && pushes == 1 && branches == 1 && queues == 1 && queue_types == 1 && queue_branches == 1;
}

static void changes_test_main_policy(ChangesTest* test, Text workflow)
{
    changes_expect(test, changes_main_policy(workflow), "main policy: unfiltered pushes, independent runs and full hosted coverage", (Text){ 0 });
    static const struct
    {
        const char* section;
        const char* before;
        const char* after;
        const char* name;
    } mutants[] = {
        { "on:", "    branches: [main]", "    branches: [main]\n    paths-ignore:\n      - 'docs/**'\n      - '**.md'", "documentation path exclusion" },
        { "on:", "    branches: [main]", "    branches: [main]\n    paths:\n      - 'src/**'", "source-only path inclusion" },
        { "on:", "    branches: [main]", "    branches: [release]", "wrong push branch" },
        { "on:", "  push:", "  pull_request_target:", "missing main push" },
        { "  merge_group:", "    branches: [main]", "    branches: [main]\n    paths-ignore:\n      - 'docs/**'", "merge-queue documentation exclusion" },
        { "  merge_group:", "    branches: [main]", "    branches: [main]\n    paths:\n      - 'src/**'", "merge-queue source-only inclusion" },
        { "  merge_group:", "    branches: [main]", "    branches: [release]", "merge queue of another branch" },
        { "  merge_group:", "    types: [checks_requested]", "    types: [destroyed]", "merge queue without its checks" },
        { "  merge_group:", "  merge_group:\n    types: [checks_requested]\n    branches: [main]\n", "", "missing merge-queue trigger" },
        { "concurrency:", "  group: buster-fast-9700x-${{ github.event.pull_request.number || (github.event_name == 'push' && format('push-{0}', github.run_id)) || (github.event_name == 'merge_group' && format('queue-{0}', github.run_id)) || (github.event_name == 'schedule' && 'schedule') || github.ref }}", "  group: buster-fast-9700x-${{ github.event.pull_request.number || (github.event_name == 'schedule' && 'schedule') || github.ref }}", "shared main pending slot" },
        { "concurrency:", " || (github.event_name == 'merge_group' && format('queue-{0}', github.run_id))", "", "candidates share one pending slot" },
        { "  changes:", " || github.event_name == 'merge_group' || ", " || ", "a candidate skips the selector and its self-tests" },
        { "  changes:", "          if [ \"$GITHUB_EVENT_NAME\" = merge_group ]; then", "          if false; then", "a candidate decided by its change alone" },
        { "  changes:", "--event merge_group --workflow", "--event push --workflow", "a candidate decided as a push" },
        { "  qualification:", " && !(github.event_name == 'push' && needs.evidence.outputs.hardware_reason == 'deferred') }}", " && !(github.event_name == 'push' && needs.evidence.outputs.hardware_reason == 'deferred') && github.event_name != 'merge_group' }}", "a candidate without the required aggregate" },
        { "  qualification:", "        if: github.event_name == 'merge_group'\n", "        if: false\n", "a candidate's coverage unchecked" },
        { "  qualification:", "          COVERAGE_RESULT: ${{ steps.coverage.outcome }}\n", "", "the aggregate ignores the candidate's coverage" },
        { "  qualification:", "      actions: read\n", "", "the coverage step can't read the run's jobs" },
        { "concurrency:", "  cancel-in-progress: ${{ github.event_name == 'pull_request' }}", "  cancel-in-progress: true", "main cancellation" },
        { "concurrency:", "  cancel-in-progress: ${{ github.event_name == 'pull_request' }}", "  cancel-in-progress: false", "lost PR cancellation" },
        { "  test:", "    if: ${{ !cancelled() && needs.prepare-hosted-tools.result == 'success' && github.repository_id == '1421805693' && needs.evidence.result == 'success' && needs.evidence.outputs.hosted == 'run' && (needs.changes.result == 'skipped' || (needs.changes.result == 'success' && needs.changes.outputs.code != 'false')) && ((github.event_name != 'pull_request' && github.event_name != 'merge_group') || needs.policy.result == 'success') }}", "    if: ${{ !cancelled() && needs.prepare-hosted-tools.result == 'success' && github.repository_id == '1421805693' && needs.evidence.result == 'success' && needs.evidence.outputs.hardware == 'admit' && (needs.changes.result == 'skipped' || (needs.changes.result == 'success' && needs.changes.outputs.code != 'false')) && ((github.event_name != 'pull_request' && github.event_name != 'merge_group') || needs.policy.result == 'success') }}", "native reuse suppresses hosted tests" },
        { "  prepare-hosted-tools:", "needs.evidence.outputs.hosted == 'run'", "needs.evidence.outputs.hosted == 'reused'", "preparation excludes required hosted work" },
        { "  prepare-hosted-tools:", "    uses: ./.github/workflows/prepare-hosted-tools.yml", "    uses: ./.github/workflows/absent.yml", "missing preparation implementation" },
        { "  test:", "needs.prepare-hosted-tools.result == 'success' && ", "", "tests bypass preparation failure" },
        { "  test:", "needs.prepare-hosted-tools.result == 'success'", "needs.prepare-hosted-tools.result != 'failure'", "tests accept skipped preparation" },
        { "  test:", "    needs: [changes, evidence, policy, prepare-hosted-tools]", "    needs: [changes, evidence, policy]", "tests omit preparation dependency" },
        { "  test:", "      fail-fast: ${{ github.event_name == 'pull_request' || github.event_name == 'merge_group' }}", "      fail-fast: true", "main matrix fail-fast" },
        { "  test:", " || github.event_name == 'merge_group' }}", " }}", "a candidate's rows don't fail fast" },
        { "  test:", "((github.event_name != 'pull_request' && github.event_name != 'merge_group') || needs.policy.result == 'success')", "(github.event_name != 'pull_request' || needs.policy.result == 'success')", "a candidate's tests don't wait for the policy job" },
        { "  prepare-hosted-tools:", "((github.event_name != 'pull_request' && github.event_name != 'merge_group') || needs.policy.result == 'success')", "(github.event_name != 'pull_request' || needs.policy.result == 'success')", "a candidate's fuzzing doesn't wait for the policy job" },
        { "  test:", "'[\"x86-64\", \"native\"]'", "'[\"native\"]'", "missing baseline architecture" },
        { "  test:", "'[\"x86-64\", \"native\"]'", "'[\"x86-64\", \"vector\"]'", "main's native row replaced before its qualification" },
        { "  test:", "\"sanitizer\":\"none address thread memory\"", "\"sanitizer\":\"none\"", "the vector sanitizer configurations dropped" },
        { "  test:", "\"sanitizer\":\"none address thread memory\"", "\"sanitizer\":\"none address thread\"", "the vector memory configuration dropped" },
        { "  test:", " || needs.evidence.outputs.scope == 'plain' && '[]' || '[{", " || '[]' || '[{", "the vector sanitizer configurations never run" },
        { "  test:", "        sanitizer: ${{ fromJSON((github.event_name == 'pull_request' || needs.evidence.outputs.scope == 'plain') && '[\"none\"]' || '[\"none\", \"address\", \"thread\", \"memory\"]') }}", "        sanitizer: ${{ fromJSON((github.event_name == 'pull_request' || needs.evidence.outputs.scope == 'plain') && '[\"none\"]' || '[\"none\", \"thread\", \"memory\"]') }}", "missing address sanitizer" },
        { "  test:", "        sanitizer: ${{ fromJSON((github.event_name == 'pull_request' || needs.evidence.outputs.scope == 'plain') && '[\"none\"]' || '[\"none\", \"address\", \"thread\", \"memory\"]') }}", "        sanitizer: ${{ fromJSON((github.event_name == 'pull_request' || needs.evidence.outputs.scope == 'plain') && '[\"none\"]' || '[\"none\", \"address\", \"memory\"]') }}", "missing thread sanitizer" },
        { "  test:", "        sanitizer: ${{ fromJSON((github.event_name == 'pull_request' || needs.evidence.outputs.scope == 'plain') && '[\"none\"]' || '[\"none\", \"address\", \"thread\", \"memory\"]') }}", "        sanitizer: ${{ fromJSON((github.event_name == 'pull_request' || needs.evidence.outputs.scope == 'plain') && '[\"none\"]' || '[\"none\", \"address\", \"thread\"]') }}", "missing memory sanitizer" },
        { "  test:", "        sanitizer: ${{ fromJSON((github.event_name == 'pull_request' || needs.evidence.outputs.scope == 'plain') && '[\"none\"]' || '[\"none\", \"address\", \"thread\", \"memory\"]') }}", "        sanitizer: ${{ fromJSON((github.event_name != 'schedule' || needs.evidence.outputs.scope == 'plain') && '[\"none\"]' || '[\"none\", \"address\", \"thread\", \"memory\"]') }}", "push gets plain matrix" },
        { "  test:", "        optimization: [3]", "        optimization: [1]", "missing O3 optimization" },
        { "  test:", "    uses: " BUSTER_CHANGES_ROW_WORKFLOW_USES, "    uses: ./.github/workflows/absent.yml", "missing hosted row workflow" },
        { "  test:", "      hosted-tools: ${{ needs.prepare-hosted-tools.outputs.artifact-name }}", "      hosted-tools: ''", "rows without the verified preparation" },
        { "  fuzz:", "    if: ${{ !cancelled() && needs.prepare-hosted-tools.result == 'success' && github.repository_id == '1421805693' && github.event_name != 'pull_request' && needs.evidence.result == 'success' && needs.evidence.outputs.hosted == 'run' && needs.evidence.outputs.scope == 'full' && (needs.changes.result == 'skipped' || (needs.changes.result == 'success' && needs.changes.outputs.code != 'false')) }}", "    if: ${{ !cancelled() && needs.prepare-hosted-tools.result == 'success' && github.repository_id == '1421805693' && github.event_name != 'pull_request' && needs.evidence.result == 'success' && needs.evidence.outputs.hosted == 'run' && needs.evidence.outputs.hardware == 'admit' && (needs.changes.result == 'skipped' || (needs.changes.result == 'success' && needs.changes.outputs.code != 'false')) }}", "native reuse suppresses fuzzing" },
        { "  fuzz:", "needs.prepare-hosted-tools.result == 'success' && ", "", "fuzzing bypasses preparation failure" },
        { "  fuzz:", "needs.prepare-hosted-tools.result == 'success'", "needs.prepare-hosted-tools.result != 'failure'", "fuzzing accepts skipped preparation" },
        { "  fuzz:", "    needs: [changes, evidence, prepare-hosted-tools]", "    needs: [changes, evidence]", "fuzzing omits preparation dependency" },
        { "  fuzz:", "      fail-fast: ${{ github.event_name == 'merge_group' }}", "      fail-fast: true", "fuzz campaign cancellation" },
        { "  fuzz:", "      fail-fast: ${{ github.event_name == 'merge_group' }}", "      fail-fast: false", "a candidate's fuzz campaigns don't fail fast" },
        { "  fuzz:", "          - entry: frontend", "          - entry: removed", "missing frontend fuzzing" },
        { "  fuzz:", "          - entry: elf-override", "          - entry: removed", "missing ELF fuzzing" },
        { "  fuzz:", "          - entry: crash-replay-proof", "          - entry: removed", "missing replay proof" },
        { "  fuzz:", "          ref: ${{ github.sha }}", "          ref: main", "moving fuzz revision" },
    };
    const char* original = text_terminated(workflow);
    for (uint32_t i = 0; i < sizeof(mutants) / sizeof(mutants[0]); i += 1)
    {
        const char* section = strstr(original, mutants[i].section);
        const char* found = section != NULL ? strstr(section, mutants[i].before) : NULL;
        Text changed = workflow;
        if (found != NULL)
        {
            const size_t prefix = (size_t)(found - original);
            const size_t suffix = prefix + strlen(mutants[i].before);
            changed = text_concat(text_concat((Text){ .data = workflow.data, .length = prefix }, text_c(mutants[i].after)),
                                  (Text){ .data = workflow.data + suffix, .length = workflow.length - suffix });
        }
        changes_expect(test, found != NULL && !changes_main_policy(changed), "main policy rejects lost or reduced post-merge coverage", text_c(mutants[i].name));
    }
}

// The hosted rows' workflow, which the test job calls for each row (#2032; docs/ci.md, "Shared row products"). Both its
// jobs take the row's settings from the workflow's environment and check out the triggering commit with the verified
// hosted inputs. A portable row's (x86-64, never native) compiler is built once, by `hosted-product.sh build`, after the
// ownership check on its tree, packed with its identity and published under the attempt's name and the archive's
// SHA-256. Its shards require that build's success, check the archive against its SHA-256 and install it with
// `hosted-product.sh install`, and a native row's shards build their own. A vector configuration's item 1 builds,
// checks ownership, packs and publishes its product under a name with the attempt and the SHA-256, and its other items
// build nothing: they wait for it (`vector-draw.sh product`), download it by that name and install it by that SHA-256
// (#2033). Nothing comes from a cache or another run. This pins the repository's YAML spelling, as the other policies
// do; it is not a general YAML evaluator.
static bool changes_row_policy(Text row)
{
    static const struct
    {
        uint32_t job;
        const char* line;
    } required[] = {
        { 0, "  workflow_call:" },
        { 0, "  BUSTER_HOST_ARCH: ${{ inputs.host_arch }}" },
        { 0, "  BUSTER_SANITIZER: ${{ inputs.sanitizer }}" },
        { 0, "  BUSTER_SANITIZER_OPTIMIZATION: ${{ inputs.optimization }}" },
        { 1, "    if: inputs.host_arch == 'x86-64'" },
        { 1, "      artifact: ${{ steps.name.outputs.artifact }}" },
        { 1, "      digest: ${{ steps.pack.outputs.digest }}" },
        { 1, "          ref: ${{ github.sha }}" },
        { 1, "          artifact-name: ${{ inputs.hosted-tools }}" },
        { 1, "        run: bash tools/ci/hosted-product.sh build" },
        { 1, "        run: bash tools/ci/hosted-product.sh ownership" },
        { 1, "        run: bash tools/ci/hosted-product.sh pack \"$RUNNER_TEMP/compiler-product\"" },
        { 1, "          name: ${{ steps.name.outputs.artifact }}" },
        { 2, "    needs: compiler" },
        { 2, "    if: ${{ !cancelled() && inputs.host_arch != 'vector' }}" },
        { 2, "      fail-fast: ${{ github.event_name == 'pull_request' || github.event_name == 'merge_group' }}" },
        { 2, "          ref: ${{ github.sha }}" },
        { 2, "          artifact-name: ${{ inputs.hosted-tools }}" },
        { 2, "          BUILD: ${{ needs.compiler.result }}" },
        { 2, "          if [ \"$BUILD\" != success ]; then" },
        { 2, "          name: ${{ needs.compiler.outputs.artifact }}" },
        { 2, "          DIGEST: ${{ needs.compiler.outputs.digest }}" },
        { 2, "        run: bash tools/ci/hosted-product.sh install \"$RUNNER_TEMP/compiler-product-download\" \"$DIGEST\"" },
        { 2, "        if: env.BUSTER_SHARD != '' && (inputs.host_arch == 'native' || env.BUSTER_VECTOR_BUILDER == '1')" },
        { 2, "        run: bash tools/ci/hosted-product.sh build" },
        { 2, "        if: env.BUSTER_SHARD != '' && inputs.host_arch == 'vector' && env.BUSTER_VECTOR_BUILDER != '1'" },
        { 2, "        run: bash tools/ci/vector-draw.sh product \"$BUSTER_SANITIZER\"" },
        { 2, "          name: ${{ steps.product.outputs.name }}" },
        { 2, "          DIGEST: ${{ steps.product.outputs.digest }}" },
        { 2, "        run: bash tools/ci/hosted-product.sh install \"$RUNNER_TEMP/vector-product-download\" \"$DIGEST\"" },
        { 2, "        if: env.BUSTER_SHARD != '' && env.BUSTER_VECTOR_BUILDER == '1'" },
        { 2, "        run: bash tools/ci/hosted-product.sh ownership" },
        { 2, "        run: bash tools/ci/hosted-product.sh pack \"$RUNNER_TEMP/vector-product\"" },
        { 2, "          name: ${{ steps.probe.outputs.product }}-${{ env.BUSTER_SANITIZER }}-${{ steps.pack.outputs.digest }}" },
    };
    bool seen[sizeof(required) / sizeof(required[0])] = { false };
    // 0 outside `jobs:`; in it, 1 the compiler job, 2 the shards, 3 any other.
    bool jobs = false;
    uint32_t job = 0;
    size_t offset = 0;
    Text line;
    while (text_next_line(row, &offset, &line))
    {
        if (line.length > 0 && line.data[0] != ' ' && line.data[0] != '#')
        {
            jobs = text_equal_c(line, "jobs:");
            job = 0;
        }
        else if (jobs && line.length > 2 && line.data[0] == ' ' && line.data[1] == ' ' && line.data[2] != ' ' && line.data[2] != '#')
        {
            job = text_equal_c(line, "  compiler:") ? 1 : text_equal_c(line, "  shards:") ? 2 : 3;
        }
        for (uint32_t i = 0; i < sizeof(required) / sizeof(required[0]); i += 1)
        {
            seen[i] |= job == required[i].job && text_equal_c(line, required[i].line);
        }
    }
    bool valid = true;
    for (uint32_t i = 0; i < sizeof(required) / sizeof(required[0]); i += 1)
    {
        valid = valid && seen[i];
    }
    // Products stay in their run: no cache, and no download of another run's artifacts.
    const char* text = text_terminated(row);
    return valid && strstr(text, "actions/cache") == NULL && strstr(text, "run-id:") == NULL && strstr(text, "github-token:") == NULL;
}

static void changes_test_row_policy(ChangesTest* test, Text row)
{
    changes_expect(test, changes_row_policy(row), "row policy: a portable row's compiler built once, verified by its shards, from this run alone", (Text){ 0 });
    static const struct
    {
        const char* section;
        const char* before;
        const char* after;
        const char* name;
    } mutants[] = {
        { "  compiler:", "    if: inputs.host_arch == 'x86-64'", "    if: inputs.host_arch != 'none'", "a native row's product shared" },
        { "  compiler:", "          ref: ${{ github.sha }}", "          ref: main", "a build of another revision" },
        { "  compiler:", "          artifact-name: ${{ inputs.hosted-tools }}", "          artifact-name: ''", "a build without the verified preparation" },
        { "  compiler:", "        run: bash tools/ci/hosted-product.sh ownership", "        run: 'true'", "no ownership check on the build's tree" },
        { "  compiler:", "      digest: ${{ steps.pack.outputs.digest }}", "      digest: ''", "no digest for the shards" },
        { "  compiler:", "          name: ${{ steps.name.outputs.artifact }}", "          name: compiler", "a name without the attempt" },
        { "  shards:", "    needs: compiler", "    needs: []", "shards that don't wait for the build" },
        { "  shards:", "    if: ${{ !cancelled() && inputs.host_arch != 'vector' }}", "    if: ${{ always() && inputs.host_arch != 'vector' }}", "shards after a cancellation" },
        { "  shards:", "          ref: ${{ github.sha }}", "          ref: main", "shards of another revision" },
        { "  shards:", "          if [ \"$BUILD\" != success ]; then", "          if [ \"$BUILD\" = failure ]; then", "a skipped build passes" },
        { "  shards:", "          DIGEST: ${{ needs.compiler.outputs.digest }}", "          DIGEST: ''", "an unchecked digest" },
        { "  shards:", "\"$RUNNER_TEMP/compiler-product-download\" \"$DIGEST\"", "\"$RUNNER_TEMP/compiler-product-download\" \"$(cat \"$RUNNER_TEMP\"/compiler-product-download/*.sha256)\"", "a digest from the download" },
        { "  shards:", "          name: ${{ needs.compiler.outputs.artifact }}", "          name: ${{ needs.compiler.outputs.artifact }}\n          run-id: 1", "another run's product" },
        { "  shards:", "        if: env.BUSTER_SHARD != '' && (inputs.host_arch == 'native' || env.BUSTER_VECTOR_BUILDER == '1')", "        if: false", "native rows unbuilt" },
        { "  shards:", "        if: env.BUSTER_SHARD != '' && (inputs.host_arch == 'native' || env.BUSTER_VECTOR_BUILDER == '1')", "        if: env.BUSTER_SHARD != '' && inputs.host_arch != 'x86-64'", "every vector item builds its own" },
        { "  shards:", "        run: bash tools/ci/vector-draw.sh product \"$BUSTER_SANITIZER\"", "        run: 'true'", "a vector item that doesn't wait for its product" },
        { "  shards:", "          DIGEST: ${{ steps.product.outputs.digest }}", "          DIGEST: ''", "an unchecked vector digest" },
        { "  shards:", "          name: ${{ steps.product.outputs.name }}", "          name: ${{ steps.product.outputs.name }}\n          run-id: 1", "another run's vector product" },
        { "  shards:", "          name: ${{ steps.probe.outputs.product }}-${{ env.BUSTER_SANITIZER }}-${{ steps.pack.outputs.digest }}", "          name: ${{ steps.probe.outputs.product }}-${{ env.BUSTER_SANITIZER }}", "a vector product named without its SHA-256" },
        { "  shards:", "        run: bash tools/ci/hosted-product.sh pack \"$RUNNER_TEMP/vector-product\"", "        run: 'true'", "an unpacked vector product" },
        { "env:", "  BUSTER_SANITIZER: ${{ inputs.sanitizer }}", "  BUSTER_SANITIZER: none", "a row's settings not the workflow's" },
        { "jobs:", "  compiler:", "  compiler:\n    steps:\n      - uses: actions/cache/restore@0057852bfaa89a56745cba8c7296529d2fc39830 # v4", "a cached product" },
    };
    for (uint32_t i = 0; i < sizeof(mutants) / sizeof(mutants[0]); i += 1)
    {
        bool found = false;
        Text changed = changes_mutant(row, mutants[i].section, mutants[i].before, mutants[i].after, &found);
        changes_expect(test, found && !changes_row_policy(changed), "row policy rejects a weakened shared product", text_c(mutants[i].name));
    }
}

// The hosted rows' shards (docs/ci.md, #2031): each row runs as many shards as its caller's `shards` input says. The row
// workflow declares the input (a number, required); its shards job's matrix takes the list 1 to N from a lookup,
// `shard: ${{ fromJSON('[[], [1], [1, 2], ...]')[inputs.shards] }}`, whose element k must be exactly 1 to k; the job's
// name (`shard ${{ matrix.shard }} of ${{ inputs.shards }})`) takes the same N, and so does the run step, which the
// vector row's draws share: `--shard "$BUSTER_SHARD/$shards"`, `shards` the input, or in the vector row its item's
// configuration's count of items (BUSTER_VECTOR_SHARDS). The shard is the matrix's (`BUSTER_SHARD: ${{ matrix.shard }}`)
// or, in the vector row's draws, the item its collector assigned a candidate. The test job in 9700x.yml must pass
// `shards:` an expression whose every number (outside its quoted strings) is one the lookup has. `parallel --shard K/N`
// deals the inventory to N shards, so disagreeing values would leave commands unrun (or run some twice) with every job
// green. The vector row's items (BUSTER_VECTOR_ITEMS, #2033) are each configuration's (none, address, thread and memory,
// by their initials) shards 1 to N, each once, N one the lookup has; its first draw starts 3 candidates per item: a pull
// request's row, `none` alone, 3 per plain item, and the four configurations' pool 3 per item, in the candidates' list
// and in its collector's count alike.
static bool changes_vector_items(const char* text, uint32_t elements)
{
    static const char items[] = "\n  BUSTER_VECTOR_ITEMS: '";
    static const char initials[] = "natm";
    const char* found = strstr(text, items);
    // seen[configuration][shard], and each configuration's count.
    bool seen[4][64] = { { false } };
    uint32_t counts[4] = { 0 };
    uint32_t largest[4] = { 0 };
    bool valid = found != NULL && elements < 64;
    const char* at = valid ? found + sizeof(items) - 1 : NULL;
    // Each item a configuration's initial and a shard from 1, once, separated by single spaces and closed by "'".
    while (valid && *at != '\'')
    {
        const char* initial = *at != 0 ? strchr(initials, *at) : NULL;
        const uint32_t configuration = initial != NULL ? (uint32_t)(initial - initials) : 0;
        char* end = NULL;
        const unsigned long value = initial != NULL && at[1] >= '1' && at[1] <= '9' ? strtoul(at + 1, &end, 10) : 0;
        valid = value >= 1 && value <= elements && !seen[configuration][value] && end != NULL && (*end == '\'' || (end[0] == ' ' && end[1] != '\''));
        if (valid)
        {
            seen[configuration][value] = true;
            counts[configuration] += 1;
            largest[configuration] = value > largest[configuration] ? (uint32_t)value : largest[configuration];
            at = *end == '\'' ? end : end + 1;
        }
    }
    valid = valid && at[1] == '\n';
    // 1 to N each: as many items, each once, as the largest shard.
    uint32_t total = 0;
    for (uint32_t configuration = 0; valid && configuration < 4; configuration += 1)
    {
        valid = counts[configuration] >= 1 && largest[configuration] == counts[configuration];
        total += counts[configuration];
    }
    // The first draw's candidates: 1 to 3 times the plain items for `none`, else 1 to 3 times all of them.
    char plain[1024] = "";
    char pool[1024] = "";
    size_t plain_length = 0;
    size_t pool_length = 0;
    for (uint32_t candidate = 1; valid && candidate <= 3 * total && pool_length < sizeof(pool) - 16; candidate += 1)
    {
        if (candidate <= 3 * counts[0])
        {
            plain_length += (size_t)snprintf(plain + plain_length, sizeof(plain) - plain_length, candidate == 1 ? "%u" : ", %u", candidate);
        }
        pool_length += (size_t)snprintf(pool + pool_length, sizeof(pool) - pool_length, candidate == 1 ? "%u" : ", %u", candidate);
    }
    char list[2560];
    snprintf(list, sizeof(list), "\n        candidate: ${{ fromJSON(inputs.sanitizer == 'none' && '[%s]' || '[%s]') }}\n", plain, pool);
    char count[256];
    snprintf(count, sizeof(count), "\n      BUSTER_DRAW_CANDIDATES: ${{ inputs.sanitizer == 'none' && '%u' || '%u' }}\n", 3 * counts[0], 3 * total);
    const char* draw = strstr(text, "\n  draw-1:\n");
    const char* collector = draw != NULL ? strstr(draw + 1, "\n  collect-1:\n") : NULL;
    const char* next = collector != NULL ? strstr(collector + 1, "\n  draw-2:\n") : NULL;
    const char* candidates = draw != NULL ? strstr(draw, list) : NULL;
    const char* counted = collector != NULL ? strstr(collector, count) : NULL;
    return valid && candidates != NULL && collector != NULL && candidates < collector && counted != NULL && next != NULL && counted < next;
}

static bool changes_shard_policy(Text workflow, Text row)
{
    const char* text = text_terminated(row);
    static const char declared[] = "\n      shards:\n        description: ";
    static const char typed[] = "\n        type: number\n        required: true\n";
    const char* input = strstr(text, declared);
    const char* input_end = input != NULL ? strchr(input + sizeof(declared) - 1, '\n') : NULL;
    bool valid = input_end != NULL && strncmp(input_end, typed, sizeof(typed) - 1) == 0;
    const char* job = strstr(text, "\n  shards:\n");
    static const char lookup[] = "\n        shard: ${{ fromJSON('[";
    const char* found = valid && job != NULL ? strstr(job, lookup) : NULL;
    const char* at = found != NULL ? found + sizeof(lookup) - 1 : NULL;
    // The lookup's elements: the first empty, each next one more, `[1, ..., k]`, separated by ", " and closed by "]".
    uint32_t elements = 0;
    valid = at != NULL && strncmp(at, "[]", 2) == 0;
    at = valid ? at + 2 : at;
    while (valid && strncmp(at, ", [", 3) == 0)
    {
        at += 3;
        elements += 1;
        for (uint32_t k = 1; valid && k <= elements; k += 1)
        {
            char* end = NULL;
            const unsigned long value = *at >= '0' && *at <= '9' ? strtoul(at, &end, 10) : 0;
            const char* separator = k < elements ? ", " : "]";
            valid = value == k && end != NULL && strncmp(end, separator, strlen(separator)) == 0;
            at = valid ? end + strlen(separator) : at;
        }
    }
    static const char indexed[] = "]')[inputs.shards] }}\n";
    valid = valid && elements != 0 && strncmp(at, indexed, sizeof(indexed) - 1) == 0;
    const size_t length = job != NULL ? strlen(job) : 0;
    static const char name[] = "shard ${{ matrix.shard }} of ${{ inputs.shards }})";
    static const char count[] = "\n          shards=${{ inputs.shards }}\n"
                                "          if [ \"$BUSTER_HOST_ARCH\" = vector ]; then\n"
                                "              shards=$BUSTER_VECTOR_SHARDS\n"
                                "          fi\n";
    static const char command[] = "--shard \"$BUSTER_SHARD/$shards\"";
    static const char shard[] = "\n      BUSTER_SHARD: ${{ matrix.shard }}\n";
    valid = valid && memmem(job, length, name, sizeof(name) - 1) != NULL && memmem(job, length, count, sizeof(count) - 1) != NULL;
    valid = valid && memmem(job, length, command, sizeof(command) - 1) != NULL && memmem(job, length, shard, sizeof(shard) - 1) != NULL;
    // The vector row's items: each configuration's 1 to its count, which the lookup must have too.
    valid = valid && changes_vector_items(text, elements);
    // The caller's `shards:`, in the `with:` of the job that uses the row workflow: each number one the lookup has.
    const char* caller = strstr(text_terminated(workflow), "\n    uses: ./.github/workflows/hosted-correctness.yml\n    with:\n");
    const char* with_end = caller != NULL ? strstr(caller + 1, "\n\n") : NULL;
    static const char passed[] = "\n      shards: ${{ ";
    const char* given = caller != NULL ? strstr(caller, passed) : NULL;
    valid = valid && given != NULL && (with_end == NULL || given < with_end);
    const char* value = valid ? given + sizeof(passed) - 1 : NULL;
    uint32_t numbers = 0;
    bool quoted = false;
    while (valid && *value != '\n' && *value != 0)
    {
        quoted = *value == '\'' ? !quoted : quoted;
        char* end = NULL;
        const unsigned long number = !quoted && *value >= '0' && *value <= '9' ? strtoul(value, &end, 10) : 0;
        valid = end == NULL || (number >= 1 && number <= elements);
        numbers += end != NULL;
        value = end != NULL ? end : value + 1;
    }
    return valid && numbers != 0 && !quoted;
}

static void changes_test_shard_policy(ChangesTest* test, Text workflow, Text row)
{
    changes_expect(test, changes_shard_policy(workflow, row), "shard policy: the row's shard lookup, job name, command, vector items and its caller's counts agree", (Text){ 0 });
    static const struct
    {
        bool caller;
        const char* before;
        const char* after;
    } mutants[] = {
        { false, "[1, 2, 3, 4], ", "[1, 2, 4, 3], " },
        { false, "[1, 2, 3, 4], ", "[1, 2, 3], " },
        { false, "[1, 2, 3], ", "" },
        { false, "fromJSON('[[], [1], ", "fromJSON('[[1], [1], " },
        { false, "fromJSON('[[], [1], ", "fromJSON('[[], [0], " },
        { false, "[1, 2, 3, 4, 5, 6, 7, 8]]')", "[1, 2, 3, 4, 5, 6, 7, 8, 8]]')" },
        { false, "]')[inputs.shards] }}", "]')[inputs.optimization] }}" },
        { false, "of ${{ inputs.shards }})", "of 4)" },
        { false, "--shard \"$BUSTER_SHARD/$shards\"", "--shard \"$BUSTER_SHARD/4\"" },
        { false, "          shards=${{ inputs.shards }}\n", "          shards=4\n" },
        { false, "              shards=$BUSTER_VECTOR_SHARDS\n", "              shards=4\n" },
        { false, "        type: number\n        required: true\n      hosted-tools:", "        type: number\n        required: false\n      hosted-tools:" },
        { false, "\n      shards:\n        description: ", "\n      shard-count:\n        description: " },
        { false, "      BUSTER_SHARD: ${{ matrix.shard }}", "      BUSTER_SHARD: 1" },
        { false, " n1 n2 n3 n4'", " n1 n2 n3'" },
        { false, " n1 n2 n3 n4'", " n1 n2 n3 n3'" },
        { false, " n1 n2 n3 n4'", " n1 n2 n3 n4 n5'" },
        { false, " n1 n2 n3 n4'", " n0 n1 n2 n3'" },
        { false, " n1 n2 n3 n4'", " n1 n2 n4 n5'" },
        { false, " n1 n2 n3 n4'", " n1 n2 n3 n4 x1'" },
        { false, "'m1 m2 m3 m4 ", "'m1 m2 m3 m4 m5 m6 m7 m8 m9 " },
        { false, " a1 a2 a3 a4 ", " " },
        { false, " n1 n2 n3 n4'", " n1 n2 n3 n4 '" },
        { false, "'[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]'", "'[1, 2, 3, 4, 5, 6, 7, 8]'" },
        { false, ", 46, 47, 48]'", ", 46, 47]'" },
        { false, "      BUSTER_DRAW_CANDIDATES: ${{ inputs.sanitizer == 'none' && '12' || '48' }}", "      BUSTER_DRAW_CANDIDATES: ${{ inputs.sanitizer == 'none' && '12' || '36' }}" },
        { false, "      BUSTER_DRAW_CANDIDATES: ${{ inputs.sanitizer == 'none' && '12' || '48' }}", "      BUSTER_DRAW_CANDIDATES: '12'" },
        { true, "      shards: ${{ ", "      shards: ${{ 0 && 1 || " },
        { true, "      shards: ${{ ", "      shards: ${{ 9 && 1 || " },
        { true, "      shards: ${{ ", "      shard_count: ${{ " },
    };
    for (uint32_t i = 0; i < sizeof(mutants) / sizeof(mutants[0]); i += 1)
    {
        bool found = false;
        const Text changed = changes_mutant(mutants[i].caller ? workflow : row, NULL, mutants[i].before, mutants[i].after, &found);
        const bool rejected = mutants[i].caller ? !changes_shard_policy(changed, row) : !changes_shard_policy(workflow, changed);
        changes_expect(test, found && rejected, "shard policy rejects a shard lookup, N, vector items or caller's count that disagree", text_c(mutants[i].after));
    }
}

// The `defined(MACRO)` names of the first `#if` whose line starts with `prefix`, through its continuation lines (a
// trailing backslash), in order: host_isa.h's predicate, and the vector row's probe's copy of it.
static TextList changes_predicate_macros(Text text, const char* prefix)
{
    TextList macros = { 0 };
    size_t offset = 0;
    bool inside = false;
    bool done = false;
    Text line;
    while (!done && text_next_line(text, &offset, &line))
    {
        inside = inside || text_starts_with(line, prefix);
        if (inside)
        {
            const char* at = line.data;
            const char* end = line.data + line.length;
            bool more = true;
            while (more)
            {
                const char* found = memmem(at, (size_t)(end - at), "defined(", 8);
                const char* close = found != NULL ? memchr(found + 8, ')', (size_t)(end - found - 8)) : NULL;
                more = close != NULL;
                if (more)
                {
                    list_append(&macros, (Text){ .data = found + 8, .length = (size_t)(close - found - 8) });
                    at = close;
                }
            }
            done = line.length == 0 || line.data[line.length - 1] != '\\';
        }
    }
    return macros;
}

// The name __builtin_cpu_supports gives a predicate macro's feature: the macro without its underscores around it,
// lowercase, with `.` for its inner `_` (__SSE4_1__ is sse4.1); an empty text for a macro not so spelled.
static Text changes_feature_name(Text macro)
{
    Text name = { 0 };
    const bool spelled = macro.length > 4 && memcmp(macro.data, "__", 2) == 0 && memcmp(macro.data + macro.length - 2, "__", 2) == 0;
    if (spelled)
    {
        char* data = arena_push(macro.length - 4 + 1);
        for (size_t i = 0; i < macro.length - 4; i += 1)
        {
            const char c = macro.data[i + 2];
            data[i] = c == '_' ? '.' : (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        data[macro.length - 4] = 0;
        name = (Text){ .data = data, .length = macro.length - 4 };
    }
    return name;
}

// The vector row (#2033; docs/ci.md, "The vector row"). Its probe and the build's explicit vector target must be
// host_isa.h's predicate: the probe's #if names the predicate's macros, in its order; it asks __builtin_cpu_supports for
// x86-64-v4 and then each macro's feature, in that order, each on a line that prints the name it asks for; and it is
// built with the flags that cmake/BusterSettings.cmake gives BUSTER_HOST_ARCH=vector, character for character. A
// feature added to the predicate without the probe, or flags changed in one place alone, fail here, before a runner
// could pass a CPU the build's code doesn't run on. The draws' records and the configurations' products are this run
// attempt's (the prefixes' GITHUB_RUN_ATTEMPT) and never overwrite one another; candidates take only what their draw's
// collector assigns (tools/ci/vector-draw.sh take), which one job publishes once (assign); the draws run the shards' own
// steps (`*row-steps`) and each of the 4 draws has its collector; an item's configuration is checked after the take
// set it; and the verdict waits for every draw and collector, even after a failure, and reads every attempt's records.
// This pins the repository's spelling, as the other policies do; tests/ci/vector-draw.sh tests the script.
static bool changes_vector_policy(Text row, Text host_isa, Text settings)
{
    const TextList predicate = changes_predicate_macros(host_isa, "#if ");
    const TextList probe = changes_predicate_macros(row, "          #if !(");
    bool valid = predicate.count != 0 && predicate.count == probe.count;
    for (uint32_t i = 0; valid && i < predicate.count; i += 1)
    {
        valid = text_equal(predicate.items[i], probe.items[i]);
    }
    // The probe's questions, in order: x86-64-v4, then the predicate's features.
    const char* text = text_terminated(row);
    static const char ask[] = "__builtin_cpu_supports(\"";
    const char* at = strstr(text, ask);
    uint32_t asked = 0;
    while (valid && at != NULL)
    {
        const char* name = at + sizeof(ask) - 1;
        const char* close = strchr(name, '"');
        const Text feature = close != NULL ? (Text){ .data = name, .length = (size_t)(close - name) } : (Text){ 0 };
        const Text expected = asked == 0 ? text_c("x86-64-v4") : asked <= predicate.count ? changes_feature_name(predicate.items[asked - 1]) : (Text){ 0 };
        char line[256];
        snprintf(line, sizeof(line), "\n              qualified &= probe_feature(\"%.*s\", __builtin_cpu_supports(\"%.*s\"));\n",
                 (int)feature.length, feature.data, (int)feature.length, feature.data);
        valid = close != NULL && expected.length != 0 && text_equal(feature, expected) && strstr(text, line) != NULL;
        asked += 1;
        at = strstr(name, ask);
    }
    valid = valid && asked == predicate.count + 1;
    // The build's vector flags, on the line after its `vector` test, and the probe's build with the same.
    static const char test[] = "\n    if(arch STREQUAL \"vector\")\n        set(host_flags ";
    const char* settings_text = text_terminated(settings);
    const char* flags = strstr(settings_text, test);
    const char* end = flags != NULL ? strstr(flags + sizeof(test) - 1, ")\n") : NULL;
    const char* newline = flags != NULL ? strchr(flags + sizeof(test) - 1, '\n') : NULL;
    valid = valid && end != NULL && end < newline && strncmp(flags + sizeof(test) - 1, "-march=x86-64-v4 ", 17) == 0;
    if (valid)
    {
        const int length = (int)(end - (flags + sizeof(test) - 1));
        char build[1024];
        snprintf(build, sizeof(build), "\n          cc -std=c17 -O2 -Wall -Wextra -Werror %.*s \"$draw/probe.c\" -o \"$draw/probe\"\n",
                 length, flags + sizeof(test) - 1);
        valid = strstr(text, build) != NULL;
    }
    static const char* const required[] = {
        "\n          printf 'prefix=vector-O%s-%s\\n' \"$BUSTER_SANITIZER_OPTIMIZATION\" \"$GITHUB_RUN_ATTEMPT\" >> \"$GITHUB_OUTPUT\"\n",
        "\n          printf 'product=vector-product-O%s-%s\\n' \"$BUSTER_SANITIZER_OPTIMIZATION\" \"$GITHUB_RUN_ATTEMPT\" >> \"$GITHUB_OUTPUT\"\n",
        "\n          case \" $BUSTER_VECTOR_CONFIGURATIONS \" in\n              *\" $BUSTER_SANITIZER \"*) ;;\n",
        "\n          test \"${BUSTER_VECTOR_ITEM:0:1}\" = \"${BUSTER_SANITIZER:0:1}\"\n          test \"${BUSTER_VECTOR_ITEM:1}\" = \"$BUSTER_SHARD\"\n",
        "\n          name: ${{ steps.probe.outputs.prefix }}-probe-${{ env.BUSTER_DRAW_LEVEL }}-${{ env.BUSTER_DRAW_CANDIDATE }}-${{ steps.probe.outputs.verdict == 'qualified' && 'yes' || 'no' }}\n",
        "\n          bash \"$RUNNER_TEMP/vector-draw.sh\" take \"$BUSTER_DRAW_LEVEL\" \"$BUSTER_DRAW_CANDIDATE\" \"$VERDICT\" \"$RUNNER_TEMP/vector-draw/record\"\n",
        "\n          bash \"$RUNNER_TEMP/vector-draw.sh\" assign \"$BUSTER_DRAW_LEVEL\" \"$BUSTER_DRAW_CANDIDATES\" \"$BUSTER_DRAW_NEXT\"\n",
        "\n          name: ${{ steps.assign.outputs.name }}\n          path: ${{ runner.temp }}/vector-assignment\n          overwrite: false\n",
        "\n          bash \"$RUNNER_TEMP/vector-draw.sh\" verdict \"$RUNNER_TEMP/vector-records\"\n",
        "\n          pattern: vector-O${{ inputs.optimization }}-*\n",
        "\n    steps: &row-steps\n",
        "\n    steps: &collect-steps\n",
        "\n    needs: [draw-1, collect-1, draw-2, collect-2, draw-3, collect-3, draw-4, collect-4]\n    if: ${{ !cancelled() && inputs.host_arch == 'vector' }}\n",
        "\n          if [ \"$BUSTER_HOST_ARCH\" = vector ] && [ \"$path\" != vector ]; then\n",
    };
    for (uint32_t i = 0; i < sizeof(required) / sizeof(required[0]); i += 1)
    {
        valid = valid && strstr(text, required[i]) != NULL;
    }
    // Each draw runs the shards' steps, and each collector the first's; every vector record refuses to overwrite one.
    uint32_t draws = 0;
    for (const char* step = strstr(text, "\n    steps: *row-steps\n"); step != NULL; step = strstr(step + 1, "\n    steps: *row-steps\n"))
    {
        draws += 1;
    }
    uint32_t collectors = 0;
    for (const char* step = strstr(text, "\n    steps: *collect-steps\n"); step != NULL; step = strstr(step + 1, "\n    steps: *collect-steps\n"))
    {
        collectors += 1;
    }
    uint32_t records = 0;
    for (const char* record = strstr(text, "outputs.prefix }}-"); valid && record != NULL; record = strstr(record + 1, "outputs.prefix }}-"))
    {
        const char* next = strstr(record, "\n      - ");
        const char* overwrite = strstr(record, "\n          overwrite: false\n");
        valid = overwrite != NULL && (next == NULL || overwrite < next);
        records += 1;
    }
    uint32_t products = 0;
    for (const char* product = strstr(text, "outputs.product }}-"); valid && product != NULL; product = strstr(product + 1, "outputs.product }}-"))
    {
        const char* next = strstr(product, "\n      - ");
        const char* overwrite = strstr(product, "\n          overwrite: false\n");
        valid = overwrite != NULL && (next == NULL || overwrite < next);
        products += 1;
    }
    return valid && draws == 4 && collectors == 3 && records == 3 && products == 1;
}

static void changes_test_vector_policy(ChangesTest* test, Text row, Text host_isa, Text settings)
{
    changes_expect(test, changes_vector_policy(row, host_isa, settings), "vector policy: the probe and the build's vector target are host_isa.h's predicate; items assigned per attempt", (Text){ 0 });
    static const struct
    {
        uint32_t file;
        const char* before;
        const char* after;
        const char* name;
    } mutants[] = {
        { 1, "defined(__AVX512VNNI__) && ", "defined(__AVX512VNNI__) && defined(__AVX512IFMA__) && ", "a predicate feature the probe lacks" },
        { 1, "defined(__XSAVE__)", "defined(__XSAVE__) && defined(__SHA__)", "a predicate feature at the end the probe lacks" },
        { 0, "defined(__GFNI__) && ", "", "a probe #if without a predicate feature" },
        { 0, "              qualified &= probe_feature(\"gfni\", __builtin_cpu_supports(\"gfni\"));\n", "", "a probe that doesn't ask for a feature" },
        { 0, "              qualified &= probe_feature(\"x86-64-v4\", __builtin_cpu_supports(\"x86-64-v4\"));\n", "", "a probe that doesn't ask for x86-64-v4" },
        { 0, "probe_feature(\"vaes\", __builtin_cpu_supports(\"vaes\"))", "probe_feature(\"vaes\", __builtin_cpu_supports(\"aes\"))", "a probe that asks for another feature than it prints" },
        { 0, "probe_feature(\"sse4.1\", __builtin_cpu_supports(\"sse4.1\"))", "probe_feature(\"sse4.2\", __builtin_cpu_supports(\"sse4.2\"))", "a probe that asks for a feature the predicate doesn't name" },
        { 2, "-mavx512bitalg -mavx512vnni)", "-mavx512bitalg -mavx512vnni -mavx512ifma)", "build flags the probe isn't built with" },
        { 0, "-mavx512bitalg -mavx512vnni \"$draw/probe.c\"", "-mavx512bitalg \"$draw/probe.c\"", "a probe built without a flag of the build's" },
        { 2, "set(host_flags -march=x86-64-v4 ", "set(host_flags -march=znver5 ", "a vector target of one CPU model" },
        { 0, "'prefix=vector-O%s-%s\\n' \"$BUSTER_SANITIZER_OPTIMIZATION\" \"$GITHUB_RUN_ATTEMPT\"", "'prefix=vector-O%s-%s\\n' \"$BUSTER_SANITIZER_OPTIMIZATION\" 1", "records shared between attempts" },
        { 0, "'product=vector-product-O%s-%s\\n' \"$BUSTER_SANITIZER_OPTIMIZATION\" \"$GITHUB_RUN_ATTEMPT\"", "'product=vector-product-O%s-%s\\n' \"$BUSTER_SANITIZER_OPTIMIZATION\" 1", "products shared between attempts" },
        { 0, "-${{ steps.pack.outputs.digest }}\n          path: ${{ runner.temp }}/vector-product/product.tar.zst\n          compression-level: 0\n          overwrite: false", "-${{ steps.pack.outputs.digest }}\n          path: ${{ runner.temp }}/vector-product/product.tar.zst\n          compression-level: 0\n          overwrite: true", "a product that overwrites another" },
        { 0, "          test \"${BUSTER_VECTOR_ITEM:1}\" = \"$BUSTER_SHARD\"\n", "", "an item's shard unchecked" },
        { 0, "      BUSTER_DRAW_NEXT: '0'\n    steps: *collect-steps\n", "      BUSTER_DRAW_NEXT: '0'\n    steps: []\n", "a last draw without its collector" },
        { 0, "-${{ steps.probe.outputs.verdict == 'qualified' && 'yes' || 'no' }}", "-yes", "a verdict published as qualified whatever the probe found" },
        { 0, "          bash \"$RUNNER_TEMP/vector-draw.sh\" take ", "          true ", "candidates that take an item without an assignment" },
        { 0, "          name: ${{ steps.assign.outputs.name }}\n          path: ${{ runner.temp }}/vector-assignment\n          overwrite: false", "          name: ${{ steps.assign.outputs.name }}\n          path: ${{ runner.temp }}/vector-assignment\n          overwrite: true", "an assignment that overwrites another" },
        { 0, "-evidence-${{ env.BUSTER_VECTOR_ITEM }}\n          path: ${{ runner.temp }}/vector-draw/record\n          overwrite: false", "-evidence-${{ env.BUSTER_VECTOR_ITEM }}\n          path: ${{ runner.temp }}/vector-draw/record\n          overwrite: true", "evidence that overwrites another" },
        { 0, "      BUSTER_DRAW_LEVEL: '4'\n      BUSTER_DRAW_CANDIDATE: ${{ matrix.candidate }}\n    steps: *row-steps\n", "      BUSTER_DRAW_LEVEL: '4'\n      BUSTER_DRAW_CANDIDATE: ${{ matrix.candidate }}\n    steps: []\n", "a draw without the shards' steps" },
        { 0, "      BUSTER_DRAW_NEXT: '5'\n    steps: *collect-steps\n", "      BUSTER_DRAW_NEXT: '5'\n    steps: []\n", "a collector without the first's steps" },
        { 0, "    needs: [draw-1, collect-1, draw-2, collect-2, draw-3, collect-3, draw-4, collect-4]", "    needs: [draw-1, collect-1, draw-2, collect-2, draw-3, collect-3, draw-4]", "a verdict before the last draw's assignment" },
        { 0, "    if: ${{ !cancelled() && inputs.host_arch == 'vector' }}\n    runs-on: ubuntu-26.04\n    timeout-minutes: 10", "    if: inputs.host_arch == 'vector'\n    runs-on: ubuntu-26.04\n    timeout-minutes: 10", "a verdict skipped after a failure" },
        { 0, "          pattern: vector-O${{ inputs.optimization }}-*", "          pattern: vector-O${{ inputs.optimization }}-${{ github.run_attempt }}-*", "a verdict blind to the attempt a rerun keeps" },
        { 0, "          if [ \"$BUSTER_HOST_ARCH\" = vector ] && [ \"$path\" != vector ]; then", "          if false; then", "a scalar vector row" },
    };
    for (uint32_t i = 0; i < sizeof(mutants) / sizeof(mutants[0]); i += 1)
    {
        bool found = false;
        const Text original = mutants[i].file == 0 ? row : mutants[i].file == 1 ? host_isa : settings;
        const Text changed = changes_mutant(original, NULL, mutants[i].before, mutants[i].after, &found);
        const bool valid = changes_vector_policy(mutants[i].file == 0 ? changed : row, mutants[i].file == 1 ? changed : host_isa, mutants[i].file == 2 ? changed : settings);
        changes_expect(test, found && !valid, "vector policy rejects a probe, target or assignment out of step", text_c(mutants[i].name));
    }
}

static int changes_self_test(const char* root)
{
    ChangesTest test = { 0 };
    Text workflow;
    Text row;
    bool readable = read_file(path_join(root, text_c(BUSTER_CHANGES_WORKFLOW)), &workflow);
    changes_expect(&test, readable, "the workflow", text_c(BUSTER_CHANGES_WORKFLOW));
    bool row_readable = read_file(path_join(root, text_c(BUSTER_CHANGES_ROW_WORKFLOW)), &row);
    changes_expect(&test, row_readable, "the hosted row workflow", text_c(BUSTER_CHANGES_ROW_WORKFLOW));
    Text host_isa;
    Text settings;
    bool isa_readable = read_file(path_join(root, text_c(BUSTER_CHANGES_HOST_ISA)), &host_isa);
    changes_expect(&test, isa_readable, "the host predicate", text_c(BUSTER_CHANGES_HOST_ISA));
    bool settings_readable = read_file(path_join(root, text_c(BUSTER_CHANGES_SETTINGS)), &settings);
    changes_expect(&test, settings_readable, "the build's settings", text_c(BUSTER_CHANGES_SETTINGS));
    changes_test_split(&test);
    if (readable && row_readable)
    {
        changes_test_fixtures(&test, workflow, root);
        changes_test_events(&test, workflow, root);
        changes_test_workflow(&test, workflow, root);
        changes_test_mutation(&test, workflow, root);
        changes_test_sources(&test, root);
        changes_test_oracle_policy(&test, workflow, row);
        changes_test_main_policy(&test, workflow);
        changes_test_row_policy(&test, row);
        changes_test_shard_policy(&test, workflow, row);
    }
    if (row_readable && isa_readable && settings_readable)
    {
        changes_test_vector_policy(&test, row, host_isa, settings);
    }
    printf("changes --self-test: %u checks, %u failures\n", test.checks, test.failures);
    return test.failures != 0 ? 1 : 0;
}

// --- main ---

static const char* changes_usage =
    "usage: changes --event pull_request --files LIST --base-workflow BASE.yml --workflow HEAD.yml [--root DIR] [--output FILE]\n"
    "       changes --event push --workflow HEAD.yml [--output FILE]\n"
    "       changes --event push --files LIST --base-workflow BASE.yml --workflow HEAD.yml [--root DIR] [--output FILE]\n"
    "       changes --event merge_group --workflow HEAD.yml [--output FILE]\n"
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
    bool merge_group = event != NULL && strcmp(event, "merge_group") == 0;
    const bool targeted = push && files != NULL;
    const bool profile = merge_group && files == NULL && base_workflow == NULL;
    valid = valid && root != NULL && (self_test ? event == NULL : ((push && (files == NULL) == (base_workflow == NULL)) || (pull_request && files != NULL && base_workflow != NULL) || profile) && head_workflow != NULL);
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
            .targeted = targeted,
            .merge_group = merge_group,
            .root = root,
        };
        Text list = { .data = "", .length = 0 };
        bool read = read_file(head_workflow, &input.head_workflow);
        read = read && ((push && !targeted) || merge_group || (read_file(files, &list) && read_file(base_workflow, &input.base_workflow)));
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
