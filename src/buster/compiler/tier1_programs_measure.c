// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// measure-tier1-programs (#138, #1189): the programs Tier 1 is measured by against clang -O2, each built by this
// compiler's Tier 0 and --tier1, clang -O0 and -O2, and tcc, then run as whole processes in a Williams design's order.
//
//   build/compiler measure-tier1-programs --clang PATH [--tcc PATH] --sqlite DIR --speedtest DIR --lua DIR --scripts DIR
//                                         [--runs N] [--warmups N] [--directory DIR] [--keep] [--uncounted]
//                                         [--variant OPTION]... [--check-code-size[=lua|sqlite|all]] [--sizes-only]
//
// The programs and their inputs are #1189's (buster-performance's tier1-gap-1189-2026-11-03, after the cc3387f record's):
// - speedtest1: DIR/sqlite3.c (--sqlite: the frozen corpus's SQLite 3.53.2 amalgamation) and DIR/speedtest1.c
//   (--speedtest: SQLite 3.53.2's test/speedtest1.c), with #136's flags; this compiler's builds and tcc's take both as
//   one translation unit (the direct path's one file, which renames speedtest1.c's static randomFunc), clang's compile
//   each to an object and link them, as the record's corpora.sh does. Each run uses #136's options from a fresh
//   database: before it, outside its time, its database's files and SQLite's temporary directory (SQLITE_TMPDIR) are
//   removed, as the record's run() removes and makes its directories again.
// - Lua 5.4.8's interpreter: lua.c and the library's 32 sources in --lua's DIR, as its Makefile's linux target builds
//   them (-std=gnu99 -DLUA_COMPAT_5_3 -DLUA_USE_LINUX, linked with -lm -ldl; tcc without -std=gnu99), running each of the
//   five scripts in --scripts' DIR (calls, tables, strings, arithmetic and closures).lua.
// --tier1's builds are `--tier1 -fno-asynchronous-unwind-tables`, as the records', and tcc's are left out without
// --tcc. Each --variant (up to BUSTER_TIER1_PROGRAMS_VARIANTS) adds a build of --tier1's with OPTION after its options
// (#172's `--variant --tier1-schedule=post`), named `--tier1 OPTION`, whose medians the report also gives against
// --tier1's. Before any timing, speedtest1's --verify must give one verification hash by every build, and each script's
// output by every build must be clang -O0's byte for byte.
//
// Then --runs rounds (11), after --warmups (1) in which every build runs every program once: each round runs every
// (build, program) pair once, in a Williams design's order (profile_ab_tree's), pinned where the caller pins this
// process (taskset), every run's wall time from its exec's release to wait4's return and its user cycles and
// instructions (profile_time_one: perf_event_open from its exec, all its threads). Every run must exit with status 0
// and be counted (but with --uncounted, for a host without the counters). It prints a line per run, warm-ups among
// them, `tier1-programs-run` and its phase, round, position, program, build, wall time in ns, user cycles,
// instructions, exit status and whether it was counted, tab-separated (the record's raw data); then every pair's
// medians with the wall time's range, then a table of speedtest1's medians and
// the sum of Lua's five scripts' medians by build, and each against clang -O2's and -O0's. DIR is a new directory under
// --directory ($TMPDIR, or /tmp: a tmpfs on the 9700X, which takes speedtest1's database and SQLite's temporary
// files), removed at the end unless --keep. docs/tier1.md has the 9700X's numbers.
//
// #1116: paired relocatable objects also compare the exact source bodies compiled by Tier 1 against clang -O0, each built
// by its --tier1 options and --tier1-no-inline: a function into which inlining expanded others' bodies has no counterpart
// in clang -O0's object, and inlining's own growth is #2541's measurement (docs/tier1-unit-inlining.md).
// SQLite is sqlite3.c alone with the flags below; Lua includes luac.c as well as the interpreter's 33 sources.
// Matching stays within each translation unit, so local names and the runtime's unity build cannot skew the sums.
// --check-code-size requires each corpus's complete Tier 1 function bytes <= its matching clang -O0 bytes; =lua or
// =sqlite enforces only that corpus. Every report still requires valid, nonempty evidence. --sizes-only needs only
// --clang, --sqlite and --lua, and performs no runtime measurements.

#include <dirent.h>

#define BUSTER_TIER1_PROGRAMS_BUILDS 5
#define BUSTER_TIER1_PROGRAMS_VARIANTS 4
#define BUSTER_TIER1_PROGRAMS_MAX_BUILDS (BUSTER_TIER1_PROGRAMS_BUILDS + BUSTER_TIER1_PROGRAMS_VARIANTS)
#define BUSTER_TIER1_PROGRAMS_SCRIPTS 5
// speedtest1, then the five scripts.
#define BUSTER_TIER1_PROGRAMS_LISTS (1 + BUSTER_TIER1_PROGRAMS_SCRIPTS)
#define BUSTER_TIER1_PROGRAMS_MAX_RUNS 1000
#define BUSTER_TIER1_PROGRAMS_ARGUMENTS 64
#define BUSTER_TIER1_PROGRAMS_SIZE_SQLITE 1u
#define BUSTER_TIER1_PROGRAMS_SIZE_LUA 2u
#define BUSTER_TIER1_PROGRAMS_SIZE_ALL (BUSTER_TIER1_PROGRAMS_SIZE_SQLITE | BUSTER_TIER1_PROGRAMS_SIZE_LUA)

typedef enum Tier1ProgramsBuild
{
    TIER1_PROGRAMS_TIER0,
    TIER1_PROGRAMS_TIER1,
    TIER1_PROGRAMS_CLANG_O0,
    TIER1_PROGRAMS_CLANG_O2,
    TIER1_PROGRAMS_TCC,
} Tier1ProgramsBuild;

static const char* const tier1_programs_build_names[BUSTER_TIER1_PROGRAMS_BUILDS] = { "Tier 0", "--tier1", "clang -O0", "clang -O2", "tcc" };
static const char* const tier1_programs_build_files[BUSTER_TIER1_PROGRAMS_BUILDS] = { "tier0", "tier1", "clang0", "clang2", "tcc" };
static const char* const tier1_programs_scripts[BUSTER_TIER1_PROGRAMS_SCRIPTS] = { "calls", "tables", "strings", "arithmetic", "closures" };

// Lua 5.4.8's interpreter: its Makefile's CORE_O and LIB_O, then lua.c.
static const char* const tier1_programs_lua_sources[] = {
    "lapi", "lcode", "lctype", "ldebug", "ldo", "ldump", "lfunc", "lgc", "llex", "lmem", "lobject", "lopcodes", "lparser", "lstate", "lstring", "ltable", "ltm", "lundump",
    "lvm", "lzio", "lauxlib", "lbaselib", "lcorolib", "ldblib", "liolib", "lmathlib", "loadlib", "loslib", "lstrlib", "ltablib", "lutf8lib", "linit", "lua",
};
#define BUSTER_TIER1_PROGRAMS_LUA_SOURCES (sizeof(tier1_programs_lua_sources) / sizeof(tier1_programs_lua_sources[0]))

// #136's compile options for speedtest1 (SQLite's test/speedtest.tcl at 3.53.2, but for -Os and -g), and its run's.
static const char* const tier1_programs_sqlite_flags[] = { "-DSQLITE_OMIT_LOAD_EXTENSION", "-DSQLITE_THREADSAFE=0", "-DSQLITE_ENABLE_MEMSYS5", "-DSQLITE_ENABLE_RTREE" };
static const char* const tier1_programs_speedtest_options[] = { "--shrink-memory", "--reprepare", "--stats", "--journal", "wal", "--size", "5", "--heap", "40000000", "64", "--testset", "mix1" };
#define BUSTER_TIER1_PROGRAMS_SQLITE_FLAGS (sizeof(tier1_programs_sqlite_flags) / sizeof(tier1_programs_sqlite_flags[0]))
#define BUSTER_TIER1_PROGRAMS_SPEEDTEST_OPTIONS (sizeof(tier1_programs_speedtest_options) / sizeof(tier1_programs_speedtest_options[0]))

typedef struct Tier1Programs Tier1Programs;
struct Tier1Programs
{
    const char* clang;
    // Null without --tcc: its builds are left out.
    const char* tcc;
    const char* sqlite;
    const char* speedtest;
    const char* lua;
    const char* scripts;
    uint32_t runs;
    uint32_t warmups;
    uint32_t build_count;
    // Each build's kind (Tier1ProgramsBuild: a variant's is --tier1's), its variant's option (null for none), its name
    // and its files' suffix. The fixed builds come first, at their Tier1ProgramsBuild indices, then the variants.
    uint8_t kinds[BUSTER_TIER1_PROGRAMS_MAX_BUILDS];
    const char* variants[BUSTER_TIER1_PROGRAMS_MAX_BUILDS];
    char names[BUSTER_TIER1_PROGRAMS_MAX_BUILDS][96];
    char files[BUSTER_TIER1_PROGRAMS_MAX_BUILDS][16];
    // --uncounted: a run need not be counted (a host without the counters, as a virtual machine's), its cycles and
    // instructions then 0.
    bool uncounted;
    uint32_t code_size_corpora;
    bool sizes_only;
    char directory[PATH_MAX];
    // Each build's executables, and the errors of every build's commands.
    char speedtests[BUSTER_TIER1_PROGRAMS_MAX_BUILDS][PATH_MAX];
    char luas[BUSTER_TIER1_PROGRAMS_MAX_BUILDS][PATH_MAX];
    char errors[PATH_MAX];
    // speedtest1's database, and SQLITE_TMPDIR's directory, made fresh before each of its runs.
    char database[PATH_MAX];
    char temporary[PATH_MAX];
    char script_paths[BUSTER_TIER1_PROGRAMS_SCRIPTS][PATH_MAX];
};

// Formats into one of `count` buffers of PATH_MAX bytes, which `path` is; false if it doesn't fit.
static bool tier1_programs_path(char* path, const char* format, const char* a, const char* b)
{
    const int length = snprintf(path, PATH_MAX, format, a, b);
    return length > 0 && length < PATH_MAX;
}

// Runs argv (argv[0] looked up in PATH as a shell does), its standard input /dev/null, its standard output into
// `output` (null for /dev/null) and its errors appended to `errors`. Returns whether it exited with status 0.
static bool tier1_programs_spawn(char* const* argv, const char* output, const char* errors)
{
    const pid_t child = fork();
    if (child == 0)
    {
        const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        const int out = output != NULL ? open(output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : null;
        const int err = open(errors, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (null != -1 && out != -1 && err != -1 && dup2(null, STDIN_FILENO) != -1 && dup2(out, STDOUT_FILENO) != -1 && dup2(err, STDERR_FILENO) != -1)
        {
            execvp(argv[0], argv);
        }
        _exit(127);
    }
    int status = 0;
    const bool waited = child != -1 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// The arguments of a command: `count` of them so far, room for BUSTER_TIER1_PROGRAMS_ARGUMENTS and the null after them.
typedef struct Tier1ProgramsCommand Tier1ProgramsCommand;
struct Tier1ProgramsCommand
{
    char* arguments[BUSTER_TIER1_PROGRAMS_ARGUMENTS + 1];
    // The -I options' text.
    char includes[2][PATH_MAX + 2];
    uint32_t count;
};

static void tier1_programs_add(Tier1ProgramsCommand* c, const char* argument)
{
    if (c->count < BUSTER_TIER1_PROGRAMS_ARGUMENTS)
    {
        c->arguments[c->count] = (char*)argument;
        c->count += 1;
        c->arguments[c->count] = NULL;
    }
}

// The compiler of build `b` and its options before a build's own: this executable, with --tier1's for Tier 1's build
// and a variant's option after them, clang with its -O level, or tcc.
static void tier1_programs_compiler(const Tier1Programs* p, uint32_t build, Tier1ProgramsCommand* c)
{
    const uint32_t b = p->kinds[build];
    c->count = 0;
    c->arguments[0] = NULL;
    if (b == TIER1_PROGRAMS_TIER0 || b == TIER1_PROGRAMS_TIER1)
    {
        tier1_programs_add(c, "/proc/self/exe");
    }
    else if (b == TIER1_PROGRAMS_TCC)
    {
        tier1_programs_add(c, p->tcc);
    }
    else
    {
        tier1_programs_add(c, p->clang);
        tier1_programs_add(c, b == TIER1_PROGRAMS_CLANG_O0 ? "-O0" : "-O2");
    }
    if (b == TIER1_PROGRAMS_TIER1)
    {
        tier1_programs_add(c, "--tier1");
        tier1_programs_add(c, "-fno-asynchronous-unwind-tables");
    }
    if (p->variants[build] != NULL)
    {
        tier1_programs_add(c, p->variants[build]);
    }
}

// Build `b`'s speedtest1 and Lua, each command's errors appended to p->errors. Returns whether every command succeeded.
static bool tier1_programs_build(Tier1Programs* p, uint32_t b)
{
    // speedtest1 as one translation unit (this compiler's direct path takes one file, and tcc's build as the record's),
    // or clang's object of each and a link.
    const bool ours = p->kinds[b] != TIER1_PROGRAMS_CLANG_O0 && p->kinds[b] != TIER1_PROGRAMS_CLANG_O2;
    const char* file = p->files[b];
    char unity[PATH_MAX];
    char objects[2][PATH_MAX];
    char sources[2][PATH_MAX];
    bool ok = tier1_programs_path(p->speedtests[b], "%s/speedtest1-%s", p->directory, file) && tier1_programs_path(p->luas[b], "%s/lua-%s", p->directory, file) &&
              tier1_programs_path(unity, "%s/%s", p->directory, "speedtest1-unity.c") && tier1_programs_path(sources[0], "%s/%s", p->sqlite, "sqlite3.c") &&
              tier1_programs_path(sources[1], "%s/%s", p->speedtest, "speedtest1.c") && tier1_programs_path(objects[0], "%s/sqlite3-%s.o", p->directory, file) &&
              tier1_programs_path(objects[1], "%s/speedtest1-%s.o", p->directory, file);
    Tier1ProgramsCommand c = { 0 };
    ok = ok && snprintf(c.includes[0], sizeof(c.includes[0]), "-I%s", p->sqlite) < (int)sizeof(c.includes[0]) &&
         snprintf(c.includes[1], sizeof(c.includes[1]), "-I%s", p->speedtest) < (int)sizeof(c.includes[1]);
    for (uint32_t step = 0; ok && step < (ours ? 1u : 3u); step += 1)
    {
        tier1_programs_compiler(p, b, &c);
        for (uint32_t f = 0; step < 2 && f < BUSTER_TIER1_PROGRAMS_SQLITE_FLAGS; f += 1)
        {
            tier1_programs_add(&c, tier1_programs_sqlite_flags[f]);
        }
        if (step < 2)
        {
            tier1_programs_add(&c, c.includes[0]);
            tier1_programs_add(&c, c.includes[1]);
        }
        if (!ours && step < 2)
        {
            tier1_programs_add(&c, "-c");
        }
        tier1_programs_add(&c, "-o");
        tier1_programs_add(&c, ours || step == 2 ? p->speedtests[b] : objects[step]);
        if (ours)
        {
            tier1_programs_add(&c, unity);
        }
        else if (step < 2)
        {
            tier1_programs_add(&c, sources[step]);
        }
        else
        {
            tier1_programs_add(&c, objects[1]);
            tier1_programs_add(&c, objects[0]);
        }
        if (ours || step == 2)
        {
            tier1_programs_add(&c, "-lm");
        }
        ok = c.count < BUSTER_TIER1_PROGRAMS_ARGUMENTS && tier1_programs_spawn(c.arguments, NULL, p->errors);
    }
    // Lua: one command of its 33 sources.
    char lua_paths[BUSTER_TIER1_PROGRAMS_LUA_SOURCES][PATH_MAX];
    tier1_programs_compiler(p, b, &c);
    if (p->kinds[b] != TIER1_PROGRAMS_TCC)
    {
        tier1_programs_add(&c, "-std=gnu99");
    }
    tier1_programs_add(&c, "-DLUA_COMPAT_5_3");
    tier1_programs_add(&c, "-DLUA_USE_LINUX");
    tier1_programs_add(&c, "-o");
    tier1_programs_add(&c, p->luas[b]);
    for (uint32_t s = 0; ok && s < BUSTER_TIER1_PROGRAMS_LUA_SOURCES; s += 1)
    {
        ok = snprintf(lua_paths[s], PATH_MAX, "%s/%s.c", p->lua, tier1_programs_lua_sources[s]) < PATH_MAX;
        tier1_programs_add(&c, lua_paths[s]);
    }
    tier1_programs_add(&c, "-lm");
    tier1_programs_add(&c, "-ldl");
    ok = ok && c.count < BUSTER_TIER1_PROGRAMS_ARGUMENTS && tier1_programs_spawn(c.arguments, NULL, p->errors);
    printf("measure-tier1-programs: %s's builds %s\n", p->names[b], ok ? "built" : "FAILED");
    return ok;
}

// One object's size experiment, with the same source options for the two compilers. Its report belongs to this
// invocation alone; the general errors file must not accumulate manifests from different translation units.
static bool tier1_programs_size_object(const Tier1Programs* p, uint32_t build, bool lua, const char* source, const char* object, const char* report)
{
    Tier1ProgramsCommand c = { 0 };
    tier1_programs_compiler(p, build, &c);
    const bool tier1 = p->kinds[build] == TIER1_PROGRAMS_TIER1;
    if (tier1)
    {
        tier1_programs_add(&c, "--tier1-function-sizes");
        // The bound is per function against clang -O0's same function, which an expansion of another function's body
        // makes no counterpart of (#2541 step 2): its objects keep every call.
        tier1_programs_add(&c, "--tier1-no-inline");
        // Object sizes need no parallel compilation; respect the hosted command group's single-CPU budget.
        tier1_programs_add(&c, "--lanes");
        tier1_programs_add(&c, "1");
    }
    if (lua)
    {
        tier1_programs_add(&c, "-std=gnu99");
        tier1_programs_add(&c, "-DLUA_COMPAT_5_3");
        tier1_programs_add(&c, "-DLUA_USE_LINUX");
    }
    else
    {
        for (uint32_t f = 0; f < BUSTER_TIER1_PROGRAMS_SQLITE_FLAGS; f += 1)
        {
            tier1_programs_add(&c, tier1_programs_sqlite_flags[f]);
        }
    }
    tier1_programs_add(&c, "-c");
    tier1_programs_add(&c, source);
    tier1_programs_add(&c, "-o");
    tier1_programs_add(&c, object);
    const bool ok = c.count < BUSTER_TIER1_PROGRAMS_ARGUMENTS && tier1_programs_spawn(c.arguments, NULL, report);
    if (!ok)
    {
        fprintf(stderr, "measure-tier1-programs: code-size compile failed: %s by %s; see %s\n", source, p->names[build], report);
    }
    return ok;
}

// Each corpus has its own gate: a small Lua result cannot hide a SQLite regression, nor can a fallback or a helper
// be mistaken for a Tier 1 body. Clang's object of a source is shared by the baseline and all requested variants.
static bool tier1_programs_sizes(Arena* arena, const Tier1Programs* p)
{
    bool ok = true;
    bool qualified = true;
    for (uint32_t corpus = 0; ok && corpus < 2; corpus += 1)
    {
        const bool lua = corpus != 0;
        const char* label = lua ? "lua" : "sqlite";
        const uint32_t sources = lua ? BUSTER_TIER1_PROGRAMS_LUA_SOURCES + 1 : 1;
        Tier1CodeSize totals[BUSTER_TIER1_PROGRAMS_MAX_BUILDS] = { 0 };
        for (uint32_t s = 0; ok && s < sources; s += 1)
        {
            const char* name = !lua ? "sqlite3" : s < BUSTER_TIER1_PROGRAMS_LUA_SOURCES ? tier1_programs_lua_sources[s] : "luac";
            char source[PATH_MAX];
            char oracle[PATH_MAX];
            char oracle_log[PATH_MAX];
            ok = snprintf(source, sizeof(source), "%s/%s.c", lua ? p->lua : p->sqlite, name) < (int)sizeof(source) &&
                 snprintf(oracle, sizeof(oracle), "%s/size-%s-%s-clang0.o", p->directory, label, name) < (int)sizeof(oracle) &&
                 snprintf(oracle_log, sizeof(oracle_log), "%s/size-%s-%s-clang0.txt", p->directory, label, name) < (int)sizeof(oracle_log);
            ok = ok && tier1_programs_size_object(p, TIER1_PROGRAMS_CLANG_O0, lua, source, oracle, oracle_log);
            for (uint32_t b = 0; ok && b < p->build_count; b += 1)
            {
                if (p->kinds[b] == TIER1_PROGRAMS_TIER1)
                {
                    char object[PATH_MAX];
                    char report[PATH_MAX];
                    char detail[256] = { 0 };
                    ok = snprintf(object, sizeof(object), "%s/size-%s-%s-%s.o", p->directory, label, name, p->files[b]) < (int)sizeof(object) &&
                         snprintf(report, sizeof(report), "%s/size-%s-%s-%s.txt", p->directory, label, name, p->files[b]) < (int)sizeof(report);
                    ok = ok && tier1_programs_size_object(p, b, lua, source, object, report);
                    const uint64_t mark = arena->length;
                    Tier1CodeSize unit = { 0 };
                    ok = ok && tier1_code_size_read(&unit, object, oracle, report, arena, detail, sizeof(detail));
                    if (ok)
                    {
                        tier1_code_size_add(&totals[b], &unit);
                    }
                    else
                    {
                        fprintf(stderr, "measure-tier1-programs: invalid code-size evidence for %s by %s: %s\n", source, p->names[b], detail);
                    }
                    arena_pop_to(arena, mark);
                }
            }
        }
        for (uint32_t b = 0; ok && b < p->build_count; b += 1)
        {
            if (p->kinds[b] == TIER1_PROGRAMS_TIER1)
            {
                char name[128];
                snprintf(name, sizeof(name), "%s/%s", label, p->files[b]);
                const uint32_t selected = lua ? BUSTER_TIER1_PROGRAMS_SIZE_LUA : BUSTER_TIER1_PROGRAMS_SIZE_SQLITE;
                const bool passed = tier1_code_size_report(&totals[b], name, (p->code_size_corpora & selected) != 0);
                qualified = qualified && passed;
            }
        }
    }
    return ok && qualified;
}

// speedtest1's database's files and SQLite's temporary directory removed, and the directory made again: a fresh database
// for the next run, outside its time. Returns whether the directory is there.
static bool tier1_programs_fresh(const Tier1Programs* p)
{
    static const char* const suffixes[] = { "", "-wal", "-shm", "-journal" };
    char path[PATH_MAX];
    for (uint32_t k = 0; k < sizeof(suffixes) / sizeof(suffixes[0]); k += 1)
    {
        if (tier1_programs_path(path, "%s%s", p->database, suffixes[k]))
        {
            unlink(path);
        }
    }
    DIR* directory = opendir(p->temporary);
    struct dirent* entry = directory != NULL ? readdir(directory) : NULL;
    while (entry != NULL)
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 && tier1_programs_path(path, "%s/%s", p->temporary, entry->d_name))
        {
            unlink(path);
        }
        entry = readdir(directory);
    }
    if (directory != NULL)
    {
        closedir(directory);
    }
    rmdir(p->temporary);
    return mkdir(p->temporary, 0755) == 0;
}

// The arguments of list `l` (speedtest1, or a script) of build `b`, into argv (room for its executable, the options, a
// database or script, and the null), with --verify before speedtest1's database when `verify`.
static void tier1_programs_list(const Tier1Programs* p, uint32_t b, uint32_t l, bool verify, char** argv)
{
    uint32_t count = 0;
    argv[count++] = (char*)(l == 0 ? p->speedtests[b] : p->luas[b]);
    for (uint32_t o = 0; l == 0 && o < BUSTER_TIER1_PROGRAMS_SPEEDTEST_OPTIONS; o += 1)
    {
        argv[count++] = (char*)tier1_programs_speedtest_options[o];
    }
    if (l == 0 && verify)
    {
        argv[count++] = "--verify";
    }
    argv[count++] = (char*)(l == 0 ? p->database : p->script_paths[l - 1]);
    argv[count] = NULL;
}

// speedtest1's verification hash by every build, and each script's output by every build against clang -O0's. Returns
// whether they all agree.
static bool tier1_programs_check(Arena* arena, Tier1Programs* p)
{
    char* argv[BUSTER_TIER1_PROGRAMS_SPEEDTEST_OPTIONS + 4];
    char outputs[BUSTER_TIER1_PROGRAMS_MAX_BUILDS][PATH_MAX];
    char reference[256] = { 0 };
    bool ok = true;
    for (uint32_t b = 0; ok && b < p->build_count; b += 1)
    {
        tier1_programs_list(p, b, 0, true, argv);
        ok = tier1_programs_path(outputs[b], "%s/verify-%s.txt", p->directory, p->files[b]) && tier1_programs_fresh(p) &&
             tier1_programs_spawn(argv, outputs[b], p->errors);
        const uint64_t mark = arena->length;
        const char* text = ok ? measure_compile_read(arena, outputs[b]) : NULL;
        const char* line = text != NULL ? strstr(text, "Verification Hash:") : NULL;
        const size_t length = line != NULL ? strcspn(line, "\n") : 0;
        ok = line != NULL && length < sizeof(reference);
        if (ok && b == 0)
        {
            memcpy(reference, line, length);
        }
        ok = ok && strlen(reference) == length && memcmp(reference, line, length) == 0;
        printf("measure-tier1-programs: %s's speedtest1 --verify: %.*s%s\n", p->names[b], (int)length, line != NULL ? line : "", ok ? "" : " (FAILED)");
        arena_pop_to(arena, mark);
    }
    for (uint32_t s = 0; ok && s < BUSTER_TIER1_PROGRAMS_SCRIPTS; s += 1)
    {
        for (uint32_t b = 0; ok && b < p->build_count; b += 1)
        {
            char name[64];
            snprintf(name, sizeof(name), "%s-%s.out", tier1_programs_scripts[s], p->files[b]);
            tier1_programs_list(p, b, 1 + s, false, argv);
            ok = tier1_programs_path(outputs[b], "%s/%s", p->directory, name) && tier1_programs_spawn(argv, outputs[b], p->errors);
        }
        for (uint32_t b = 0; ok && b < p->build_count; b += 1)
        {
            ok = measure_compile_same(arena, outputs[TIER1_PROGRAMS_CLANG_O0], outputs[b]);
        }
        printf("measure-tier1-programs: %s.lua's output by every build: %s\n", tier1_programs_scripts[s], ok ? "clang -O0's" : "DIFFERS (FAILED)");
    }
    return ok;
}

// One (build, list) pair's medians over the runs: wall time with its range, user cycles and instructions.
typedef struct Tier1ProgramsMedians Tier1ProgramsMedians;
struct Tier1ProgramsMedians
{
    uint64_t wall_ns;
    uint64_t wall_low;
    uint64_t wall_high;
    uint64_t cycles;
    uint64_t instructions;
};

// The warm-ups and the runs, every pair's medians into `medians` (by list, then build). Returns the failed runs.
static uint64_t tier1_programs_time(Arena* arena, Tier1Programs* p, Tier1ProgramsMedians* medians)
{
    const uint64_t slots = (uint64_t)p->build_count * BUSTER_TIER1_PROGRAMS_LISTS;
    const uint64_t mark = arena->length;
    ProfileTimed* timed = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileTimed, (uint64_t)p->runs * slots + 1);
    uint64_t* values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)p->runs + 1);
    char* argv[BUSTER_TIER1_PROGRAMS_SPEEDTEST_OPTIONS + 4];
    uint64_t failures = timed == NULL || values == NULL;
    printf("tier1-programs-run\tphase\tround\tposition\tprogram\tbuild\twall_ns\tuser_cycles\tinstructions\tstatus\tcounted\n");
    for (uint64_t run = 0; failures == 0 && run < (uint64_t)p->warmups + p->runs; run += 1)
    {
        const bool warmup = run < p->warmups;
        for (uint64_t position = 0; position < slots; position += 1)
        {
            const uint64_t s = warmup ? position : profile_ab_tree(run - p->warmups, position, slots);
            const uint32_t b = (uint32_t)(s % p->build_count);
            const uint32_t l = (uint32_t)(s / p->build_count);
            tier1_programs_list(p, b, l, false, argv);
            const bool fresh = l != 0 || tier1_programs_fresh(p);
            const ProfileTimed one = fresh ? profile_time_one(argv[0], argv) : (ProfileTimed){ .status = -1 };
            const int status = one.status != -1 && WIFEXITED(one.status) ? WEXITSTATUS(one.status) : -1;
            const bool passed = status == 0 && (one.counted || p->uncounted);
            failures += !passed;
            printf("tier1-programs-run\t%s\t%llu\t%llu\t%s\t%s\t%llu\t%llu\t%llu\t%d\t%d\n", warmup ? "warm-up" : "measured",
                   (unsigned long long)(warmup ? run : run - p->warmups) + 1, (unsigned long long)position + 1, l == 0 ? "speedtest1" : tier1_programs_scripts[l - 1],
                   p->files[b], (unsigned long long)one.wall_ns, (unsigned long long)one.cycles, (unsigned long long)one.instructions, status, one.counted);
            if (!passed)
            {
                printf("measure-tier1-programs: %s's %s: FAILED (status %d, counted %d)\n", p->names[b], l == 0 ? "speedtest1" : tier1_programs_scripts[l - 1], status,
                       one.counted);
            }
            if (!warmup)
            {
                timed[(run - p->warmups) * slots + s] = one;
            }
        }
    }
    for (uint64_t s = 0; failures == 0 && s < slots; s += 1)
    {
        Tier1ProgramsMedians* m = &medians[s];
        for (uint32_t r = 0; r < p->runs; r += 1)
        {
            values[r] = timed[(uint64_t)r * slots + s].cycles;
        }
        m->cycles = profile_median(values, p->runs);
        for (uint32_t r = 0; r < p->runs; r += 1)
        {
            values[r] = timed[(uint64_t)r * slots + s].instructions;
        }
        m->instructions = profile_median(values, p->runs);
        for (uint32_t r = 0; r < p->runs; r += 1)
        {
            values[r] = timed[(uint64_t)r * slots + s].wall_ns;
        }
        m->wall_ns = profile_median(values, p->runs);
        m->wall_low = values[0];
        m->wall_high = values[p->runs - 1];
    }
    arena_pop_to(arena, mark);
    return failures;
}

// Every pair's medians, then the table: speedtest1's medians and the sum of the scripts' by build, each against clang
// -O2's and -O0's.
static void tier1_programs_report(const Tier1Programs* p, const Tier1ProgramsMedians* medians)
{
    for (uint32_t l = 0; l < BUSTER_TIER1_PROGRAMS_LISTS; l += 1)
    {
        for (uint32_t b = 0; b < p->build_count; b += 1)
        {
            const Tier1ProgramsMedians* m = &medians[l * p->build_count + b];
            printf("%s by %s: %u runs, median %.3f ms wall (%.3f to %.3f), %llu user cycles, %llu instructions\n", l == 0 ? "speedtest1" : tier1_programs_scripts[l - 1],
                   p->names[b], p->runs, (double)m->wall_ns / 1e6, (double)m->wall_low / 1e6, (double)m->wall_high / 1e6, (unsigned long long)m->cycles,
                   (unsigned long long)m->instructions);
        }
    }
    // Rows by metric of speedtest1 and of Lua's sum, columns by build, then --tier1 against clang -O2 and -O0, and each
    // variant against --tier1.
    static const char* const metrics[3] = { "ms (median)", "user cycles, M", "instructions, M" };
    const uint32_t fixed = p->tcc != NULL ? BUSTER_TIER1_PROGRAMS_BUILDS : BUSTER_TIER1_PROGRAMS_BUILDS - 1;
    printf("\n| Program | ");
    for (uint32_t b = 0; b < p->build_count; b += 1)
    {
        printf("%s | ", p->names[b]);
    }
    printf("`--tier1` / clang -O2 | `--tier1` / clang -O0 |");
    for (uint32_t b = fixed; b < p->build_count; b += 1)
    {
        printf(" `%s` / `--tier1` |", p->names[b]);
    }
    printf("\n| --- |");
    for (uint32_t b = 0; b < p->build_count + 2 + (p->build_count - fixed); b += 1)
    {
        printf(" ---: |");
    }
    printf("\n");
    for (uint32_t program = 0; program < 2; program += 1)
    {
        for (uint32_t metric = 0; metric < 3; metric += 1)
        {
            double sums[BUSTER_TIER1_PROGRAMS_MAX_BUILDS] = { 0 };
            for (uint32_t b = 0; b < p->build_count; b += 1)
            {
                for (uint32_t l = program == 0 ? 0 : 1; l < (program == 0 ? 1u : BUSTER_TIER1_PROGRAMS_LISTS); l += 1)
                {
                    const Tier1ProgramsMedians* m = &medians[l * p->build_count + b];
                    sums[b] += metric == 0 ? (double)m->wall_ns / 1e6 : metric == 1 ? (double)m->cycles / 1e6 : (double)m->instructions / 1e6;
                }
            }
            printf("| %s, %s |", program == 0 ? "speedtest1" : "Lua's five scripts (sum of medians)", metrics[metric]);
            for (uint32_t b = 0; b < p->build_count; b += 1)
            {
                printf(" %.1f |", sums[b]);
            }
            // A metric --uncounted left at 0 has no ratio.
            for (uint32_t r = 0; r < 2; r += 1)
            {
                const double reference = sums[r == 0 ? TIER1_PROGRAMS_CLANG_O2 : TIER1_PROGRAMS_CLANG_O0];
                if (reference > 0)
                {
                    printf(" %.2f |", sums[TIER1_PROGRAMS_TIER1] / reference);
                }
                else
                {
                    printf(" none |");
                }
            }
            for (uint32_t b = fixed; b < p->build_count; b += 1)
            {
                if (sums[TIER1_PROGRAMS_TIER1] > 0)
                {
                    printf(" %.4f |", sums[b] / sums[TIER1_PROGRAMS_TIER1]);
                }
                else
                {
                    printf(" none |");
                }
            }
            printf("\n");
        }
    }
}

static int run_tier1_programs_measure(int argc, char** argv)
{
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    Tier1Programs p = { .runs = 11, .warmups = 1 };
    const char* parent = oracle_temporary_directory();
    bool keep = false;
    bool valid = arena.base != 0;
    const char* variant_options[BUSTER_TIER1_PROGRAMS_VARIANTS] = { 0 };
    uint32_t variant_count = 0;
    int i = 1;
    while (valid && i < argc)
    {
        uint64_t value = 0;
        const bool pair = i + 1 < argc;
        const bool size_all = strcmp(argv[i], "--check-code-size") == 0 || strcmp(argv[i], "--check-code-size=all") == 0;
        const bool size_lua = strcmp(argv[i], "--check-code-size=lua") == 0;
        const bool size_sqlite = strcmp(argv[i], "--check-code-size=sqlite") == 0;
        const bool flag = strcmp(argv[i], "--keep") == 0 || strcmp(argv[i], "--uncounted") == 0 ||
                          size_all || size_lua || size_sqlite || strcmp(argv[i], "--sizes-only") == 0;
        if (strcmp(argv[i], "--runs") == 0 && pair && profile_count_option(argv[i + 1], &value) && value <= BUSTER_TIER1_PROGRAMS_MAX_RUNS)
        {
            p.runs = (uint32_t)value;
        }
        else if (strcmp(argv[i], "--warmups") == 0 && pair && profile_decimal(profile_string(argv[i + 1]), &value) && value <= 1000)
        {
            p.warmups = (uint32_t)value;
        }
        else if (strcmp(argv[i], "--clang") == 0 && pair)
        {
            p.clang = argv[i + 1];
        }
        else if (strcmp(argv[i], "--tcc") == 0 && pair)
        {
            p.tcc = argv[i + 1];
        }
        else if (strcmp(argv[i], "--sqlite") == 0 && pair)
        {
            p.sqlite = argv[i + 1];
        }
        else if (strcmp(argv[i], "--speedtest") == 0 && pair)
        {
            p.speedtest = argv[i + 1];
        }
        else if (strcmp(argv[i], "--lua") == 0 && pair)
        {
            p.lua = argv[i + 1];
        }
        else if (strcmp(argv[i], "--scripts") == 0 && pair)
        {
            p.scripts = argv[i + 1];
        }
        else if (strcmp(argv[i], "--directory") == 0 && pair)
        {
            parent = argv[i + 1];
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argv[i], "--uncounted") == 0)
        {
            p.uncounted = true;
        }
        else if (size_all || size_lua || size_sqlite)
        {
            p.code_size_corpora |= size_all ? BUSTER_TIER1_PROGRAMS_SIZE_ALL : size_lua ? BUSTER_TIER1_PROGRAMS_SIZE_LUA : BUSTER_TIER1_PROGRAMS_SIZE_SQLITE;
        }
        else if (strcmp(argv[i], "--sizes-only") == 0)
        {
            p.sizes_only = true;
        }
        else if (strcmp(argv[i], "--variant") == 0 && pair && variant_count < BUSTER_TIER1_PROGRAMS_VARIANTS)
        {
            variant_options[variant_count] = argv[i + 1];
            variant_count += 1;
        }
        else
        {
            valid = false;
        }
        i += flag ? 1 : 2;
    }
    valid = valid && p.clang != NULL && p.sqlite != NULL && p.lua != NULL &&
            (p.sizes_only || (p.speedtest != NULL && p.scripts != NULL));
    p.build_count = p.tcc != NULL ? BUSTER_TIER1_PROGRAMS_BUILDS : BUSTER_TIER1_PROGRAMS_BUILDS - 1;
    for (uint32_t b = 0; b < p.build_count; b += 1)
    {
        p.kinds[b] = (uint8_t)b;
        snprintf(p.names[b], sizeof(p.names[b]), "%s", tier1_programs_build_names[b]);
        snprintf(p.files[b], sizeof(p.files[b]), "%s", tier1_programs_build_files[b]);
    }
    for (uint32_t v = 0; v < variant_count; v += 1)
    {
        const uint32_t b = p.build_count + v;
        p.kinds[b] = TIER1_PROGRAMS_TIER1;
        p.variants[b] = variant_options[v];
        snprintf(p.names[b], sizeof(p.names[b]), "--tier1 %.80s", variant_options[v]);
        snprintf(p.files[b], sizeof(p.files[b]), "variant%u", v + 1);
    }
    p.build_count += variant_count;
    const int length = valid ? snprintf(p.directory, sizeof(p.directory), "%s/measure-tier1-programs-XXXXXX", parent) : -1;
    valid = valid && length > 0 && (size_t)length + 64 < sizeof(p.directory);
    const bool made = valid && mkdtemp(p.directory) != NULL;
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: measure-tier1-programs --clang PATH [--tcc PATH] --sqlite DIR --speedtest DIR --lua DIR --scripts DIR [--runs N] [--warmups N] [--directory DIR] [--keep] "
                        "[--uncounted] [--variant OPTION]... [--check-code-size[=lua|sqlite|all]] [--sizes-only]\n"
                        "       measure-tier1-programs --sizes-only --clang PATH --sqlite DIR --lua DIR [--check-code-size[=lua|sqlite|all]] [--variant OPTION]... [--directory DIR] [--keep]\n");
    }
    else if (!made)
    {
        fprintf(stderr, "error: can't make a directory under %s: %s\n", parent, strerror(errno));
    }
    else
    {
        char unity[PATH_MAX];
        bool ok = tier1_programs_path(p.errors, "%s/%s", p.directory, "errors.txt") && tier1_programs_path(p.database, "%s/%s", p.directory, "speedtest1.db") &&
                  tier1_programs_path(p.temporary, "%s/%s", p.directory, "tmp") && tier1_programs_path(unity, "%s/%s", p.directory, "speedtest1-unity.c");
        for (uint32_t s = 0; ok && !p.sizes_only && s < BUSTER_TIER1_PROGRAMS_SCRIPTS; s += 1)
        {
            ok = snprintf(p.script_paths[s], PATH_MAX, "%s/%s.lua", p.scripts, tier1_programs_scripts[s]) < PATH_MAX;
        }
        // The direct path's one file: SQLite's amalgamation, then speedtest1.c, whose static randomFunc would redefine
        // sqlite3.c's static function of that name (#136's speedtest1-unity.c).
        if (ok && !p.sizes_only)
        {
            static const char unity_text[] = "#include \"sqlite3.c\"\n#define randomFunc speedtest1_randomFunc\n#include \"speedtest1.c\"\n";
            const int unity_file = open(unity, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            ok = unity_file != -1 && write(unity_file, unity_text, sizeof(unity_text) - 1) == (ssize_t)(sizeof(unity_text) - 1);
            if (unity_file != -1)
            {
                ok = close(unity_file) == 0 && ok;
            }
            // SQLite's temporary files in a directory of their own, made fresh with the database before each run.
            ok = ok && setenv("SQLITE_TMPDIR", p.temporary, 1) == 0;
        }
        if (p.sizes_only)
        {
            printf("measure-tier1-programs: function sizes only, in %s\n", p.directory);
        }
        else
        {
            printf("measure-tier1-programs: %u warm-ups, %u runs, %u builds, in %s\n", p.warmups, p.runs, p.build_count, p.directory);
        }
        for (uint32_t b = 0; ok && !p.sizes_only && b < p.build_count; b += 1)
        {
            ok = tier1_programs_build(&p, b);
        }
        ok = ok && (p.sizes_only || tier1_programs_check(&arena, &p));
        ok = ok && tier1_programs_sizes(&arena, &p);
        Tier1ProgramsMedians medians[BUSTER_TIER1_PROGRAMS_MAX_BUILDS * BUSTER_TIER1_PROGRAMS_LISTS] = { 0 };
        ok = ok && (p.sizes_only || tier1_programs_time(&arena, &p, medians) == 0);
        if (ok && !p.sizes_only)
        {
            tier1_programs_report(&p, medians);
        }
        else if (!ok)
        {
            // What the builds and runs wrote to their standard errors.
            const char* errors = measure_compile_read(&arena, p.errors);
            fprintf(stderr, "measure-tier1-programs: FAILED; the commands' errors:\n%s\n", errors != NULL ? errors : "");
        }
        result = ok ? 0 : 1;
        if (!keep && ok)
        {
            char* remove[] = { "rm", "-rf", p.directory, NULL };
            tier1_programs_spawn(remove, NULL, "/dev/null");
        }
        else
        {
            printf("measure-tier1-programs: retained files in %s\n", p.directory);
        }
    }
    arena_release(&arena);
    return result;
}
