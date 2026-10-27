// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The predefined macros' tests (c_predefines.c) and the generator of its tables, which test-preprocessor runs:
//
//   build/compiler test-preprocessor --clang [--seed N]    (with the preprocessor's other checks)
//   build/compiler test-preprocessor --generate-targets     (prints c_predefines.c's generated tables)
//
// The generator asks clang (`clang` on PATH, with CPATH and C_INCLUDE_PATH unset) for the tables: the -march CPUs of
// x86-64 (from its error for an unknown one) and of -mtune; the features, from the IR's "target-features" of every
// CPU and the names -march=native passes; each feature's `implies` closure (the features a configuration that turns
// on nothing else has after -mFEATURE) and `implied_by` closure (the ones -mno-FEATURE turns off); each CPU's
// features, but those only the soft rules add (checked by turning each rule's trigger off); and each feature's own
// macros, those `config(implies)` defines and `config(implies without it)` doesn't, where config(T) is -march=x86-64
// with exactly the features T. The macros no single feature owns must be the two c_predefines.c writes itself.
//
// With --clang (predefines_test_run):
//   - tables: the generator's tables must be the compiled ones, field by field;
//   - options: our predefines (pp_predefines, after pp_parse_options) must be `clang -dM -E`'s for the same options,
//     line for line but for the version macros of clang's patch release and __BUSTER__, and both must accept or reject
//     the options alike: every -march CPU and native; every feature flag on and off, on x86-64 and on CPUs that have
//     the most features; each -f, -O, -g and -m option c_predefines.c reads, alone and in the orders that matter; and
//     random combinations of them (--seed);
//   - -dM: `-E -dM` of a file with every kind of definition must be `clang -dM -E`'s byte for byte, but __BUSTER__;
//   - __FLT_EVAL_METHOD__ under -ffp-eval-method and without SSE, as `clang -E` gives it.
//
// main.c includes this file after c_preprocessor_test.c. Layout: running clang (PredefinesTestJob), the generator
// (predefines_test_generate, predefines_test_print_tables), and the checks.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>

extern char** environ;

#define BUSTER_PREDEFINES_TEST_MAX_ARGUMENTS 256
#define BUSTER_PREDEFINES_TEST_PARALLEL 16

// One clang run: its arguments (after "clang"), the file its standard input reads (null for none), and its standard
// output and exit status once run.
typedef struct PredefinesTestJob PredefinesTestJob;
struct PredefinesTestJob
{
    const char* arguments[BUSTER_PREDEFINES_TEST_MAX_ARGUMENTS];
    uint32_t argument_count;
    const char* input;
    char* output;
    size_t length;
    int status;
};

// A list of runs, with room for their flags' text.
typedef struct PredefinesTestJobs PredefinesTestJobs;
struct PredefinesTestJobs
{
    PredefinesTestJob* jobs;
    uint32_t count;
    uint32_t capacity;
    Arena text;
};

static bool predefines_test_jobs_initialize(PredefinesTestJobs* jobs, uint32_t capacity)
{
    jobs->jobs = calloc(capacity, sizeof(PredefinesTestJob));
    jobs->count = 0;
    jobs->capacity = capacity;
    jobs->text = arena_reserve(64ull << 20);
    return jobs->jobs != 0 && jobs->text.base != 0;
}

static void predefines_test_jobs_release(PredefinesTestJobs* jobs)
{
    for (uint32_t i = 0; jobs->jobs != 0 && i < jobs->count; i += 1)
    {
        free(jobs->jobs[i].output);
    }
    free(jobs->jobs);
    arena_release(&jobs->text);
    *jobs = (PredefinesTestJobs){ 0 };
}

// A new run, or null if the list is full.
static PredefinesTestJob* predefines_test_job(PredefinesTestJobs* jobs)
{
    PredefinesTestJob* result = jobs->count < jobs->capacity ? &jobs->jobs[jobs->count] : 0;
    if (result != 0)
    {
        *result = (PredefinesTestJob){ 0 };
        jobs->count += 1;
    }
    return result;
}

// Appends an argument (copied into the list's text); false if it doesn't fit.
static bool predefines_test_argument(PredefinesTestJobs* jobs, PredefinesTestJob* job, const char* argument)
{
    const size_t length = strlen(argument);
    char* copy = arena_push(&jobs->text, length + 1, 1);
    const bool result = copy != 0 && job->argument_count + 1 < BUSTER_PREDEFINES_TEST_MAX_ARGUMENTS;
    if (result)
    {
        memcpy(copy, argument, length + 1);
        job->arguments[job->argument_count] = copy;
        job->argument_count += 1;
    }
    return result;
}

// The run's input: `path`, which its standard input reads, and `-` as its last argument, so that its command and result
// don't name the test's directory and the oracle cache can keep them (oracle_cache.c).
static bool predefines_test_input(PredefinesTestJobs* jobs, PredefinesTestJob* job, const char* path)
{
    const size_t length = strlen(path);
    char* copy = arena_push(&jobs->text, length + 1, 1);
    const bool result = copy != 0 && predefines_test_argument(jobs, job, "-");
    if (result)
    {
        memcpy(copy, path, length + 1);
        job->input = copy;
    }
    return result;
}

// Appends the space-separated words of `text` as arguments.
static bool predefines_test_words(PredefinesTestJobs* jobs, PredefinesTestJob* job, const char* text)
{
    bool result = true;
    const char* p = text;
    while (result && *p != 0)
    {
        while (*p == ' ')
        {
            p += 1;
        }
        const char* start = p;
        while (*p != ' ' && *p != 0)
        {
            p += 1;
        }
        if (p > start)
        {
            char word[512];
            const size_t length = (size_t)(p - start) < sizeof(word) - 1 ? (size_t)(p - start) : sizeof(word) - 1;
            memcpy(word, start, length);
            word[length] = 0;
            result = predefines_test_argument(jobs, job, word);
        }
    }
    return result;
}

// The first job in [first, job) with `job`'s arguments and input, or `job` if none has them.
static uint32_t predefines_test_same_job(const PredefinesTestJobs* jobs, uint32_t first, uint32_t job)
{
    const PredefinesTestJob* wanted = &jobs->jobs[job];
    uint32_t result = job;
    for (uint32_t i = first; result == job && i < job; i += 1)
    {
        const PredefinesTestJob* other = &jobs->jobs[i];
        bool same = other->argument_count == wanted->argument_count && (other->input == 0) == (wanted->input == 0) &&
                    (other->input == 0 || strcmp(other->input, wanted->input) == 0);
        for (uint32_t a = 0; same && a < wanted->argument_count; a += 1)
        {
            same = strcmp(other->arguments[a], wanted->arguments[a]) == 0;
        }
        result = same ? i : job;
    }
    return result;
}

// The oracle cache's key of a job (oracle_cache.c): clang's part (`tool`), its arguments, the variables of its
// environment that change its result and its input's bytes. False if the input can't be read.
static bool predefines_test_key(const PredefinesTestJob* job, OracleKey tool, char* const* environment, OracleKey* key)
{
    OracleHash hash;
    oracle_hash_begin(&hash, oracle_cache(), "predefines: clang, its standard output and exit status");
    oracle_hash_bytes(&hash, tool.bytes, sizeof(tool.bytes));
    const uint64_t count = job->argument_count;
    oracle_hash_bytes(&hash, &count, sizeof(count));
    for (uint32_t a = 0; a < job->argument_count; a += 1)
    {
        oracle_hash_text(&hash, job->arguments[a]);
    }
    oracle_hash_environment(&hash, environment);
    const bool result = job->input != 0 ? oracle_hash_file(&hash, job->input) : (oracle_hash_text(&hash, "/dev/null"), true);
    *key = oracle_hash_end(&hash);
    return result;
}

// Runs jobs [first, count) as `clang ARGUMENTS`, at most test_process_cpus() (and BUSTER_PREDEFINES_TEST_PARALLEL) at a
// time, each one's standard input from its input (/dev/null without one), its standard output into a file of
// `directory` and then into memory, and its standard error to /dev/null. CPATH and C_INCLUDE_PATH are left out of their
// environment. A job with the arguments and input of an earlier one in the range takes its status and output rather
// than run clang again (#465): the files the jobs read don't change while they run. With the oracle cache on
// (oracle_cache.c, #542), a job whose entry is there takes it rather than run (`use`), and each job that runs is kept,
// and checked against its entry (`check`). Returns false if a process can't start, an input can't be read or an entry
// is stale.
static bool predefines_test_run_jobs(PredefinesTestJobs* jobs, uint32_t first, const char* directory)
{
    // The environment without CPATH and C_INCLUDE_PATH.
    uint32_t variables = 0;
    while (environ[variables] != 0)
    {
        variables += 1;
    }
    char** environment = calloc((size_t)variables + 1, sizeof(char*));
    pid_t* pids = calloc(jobs->count + 1, sizeof(pid_t));
    uint32_t* sources = calloc(jobs->count + 1, sizeof(uint32_t));
    OracleKey* keys = calloc(jobs->count + 1, sizeof(OracleKey));
    bool* taken = calloc(jobs->count + 1, sizeof(bool));
    bool result = environment != 0 && pids != 0 && sources != 0 && keys != 0 && taken != 0;
    uint32_t kept = 0;
    for (uint32_t i = 0; result && i < variables; i += 1)
    {
        if (strncmp(environ[i], "CPATH=", 6) != 0 && strncmp(environ[i], "C_INCLUDE_PATH=", 15) != 0)
        {
            environment[kept] = environ[i];
            kept += 1;
        }
    }
    OracleCache* cache = oracle_cache();
    const bool cached = result && oracle_cache_active(cache);
    OracleKey tool = { 0 };
    if (cached)
    {
        OracleHash hash;
        oracle_hash_begin(&hash, cache, "predefines: clang");
        result = oracle_hash_tool(&hash, "clang");
        tool = oracle_hash_end(&hash);
    }
    const uint32_t cpus = test_process_cpus();
    const uint32_t parallel = cpus < BUSTER_PREDEFINES_TEST_PARALLEL ? cpus : BUSTER_PREDEFINES_TEST_PARALLEL;
    uint32_t next = first;
    uint32_t running = 0;
    while (result && (next < jobs->count || running > 0))
    {
        while (result && next < jobs->count && running < parallel)
        {
            // A job with an earlier one's arguments and input runs no clang: it takes that one's output below.
            sources[next] = predefines_test_same_job(jobs, first, next);
            PredefinesTestJob* job = &jobs->jobs[next];
            if (sources[next] == next && cached)
            {
                result = predefines_test_key(job, tool, environment, &keys[next]);
                taken[next] = result && oracle_cache_take(cache, keys[next], &job->status, &job->output, &job->length);
            }
            if (result && sources[next] == next && !taken[next])
            {
                char path[4200];
                snprintf(path, sizeof(path), "%s/clang-%u.out", directory, next);
                const char* argv[BUSTER_PREDEFINES_TEST_MAX_ARGUMENTS + 2];
                argv[0] = "clang";
                for (uint32_t a = 0; a < job->argument_count; a += 1)
                {
                    argv[a + 1] = job->arguments[a];
                }
                argv[job->argument_count + 1] = 0;
                posix_spawn_file_actions_t actions;
                posix_spawn_file_actions_init(&actions);
                posix_spawn_file_actions_addopen(&actions, 0, job->input != 0 ? job->input : "/dev/null", O_RDONLY, 0);
                posix_spawn_file_actions_addopen(&actions, 1, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
                pid_t pid;
                result = posix_spawnp(&pid, "clang", &actions, 0, (char* const*)argv, environment) == 0;
                posix_spawn_file_actions_destroy(&actions);
                pids[next] = result ? pid : 0;
                running += result;
            }
            next += 1;
        }
        if (running > 0)
        {
            int status;
            const pid_t done = wait(&status);
            for (uint32_t i = first; i < next; i += 1)
            {
                if (pids[i] == done && done > 0)
                {
                    jobs->jobs[i].status = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
                    pids[i] = 0;
                }
            }
            running -= done > 0;
            result &= done > 0;
        }
    }
    uint32_t stale = 0;
    for (uint32_t i = first; result && i < jobs->count; i += 1)
    {
        if (sources[i] != i)
        {
            const PredefinesTestJob* source = &jobs->jobs[sources[i]];
            char* copy = malloc(source->length + 1);
            result = copy != 0;
            if (result)
            {
                memcpy(copy, source->output, source->length + 1);
                jobs->jobs[i].output = copy;
                jobs->jobs[i].length = source->length;
                jobs->jobs[i].status = source->status;
            }
        }
        else if (!taken[i])
        {
            char path[4200];
            snprintf(path, sizeof(path), "%s/clang-%u.out", directory, i);
            FILE* file = fopen(path, "r");
            char* text = 0;
            size_t length = 0;
            FILE* collected = open_memstream(&text, &length);
            char chunk[1 << 16];
            size_t n;
            while (file != 0 && collected != 0 && (n = fread(chunk, 1, sizeof(chunk), file)) > 0)
            {
                fwrite(chunk, 1, n, collected);
            }
            if (file != 0)
            {
                fclose(file);
            }
            if (collected != 0)
            {
                fclose(collected);
            }
            jobs->jobs[i].output = text;
            jobs->jobs[i].length = length;
            result = text != 0;
            unlink(path);
            // The run's result, kept, and checked against its entry where it has one.
            if (result && cached && !oracle_cache_keep(cache, keys[i], jobs->jobs[i].status, text, length))
            {
                const PredefinesTestJob* job = &jobs->jobs[i];
                printf("predefines: the oracle cache's entry for clang");
                for (uint32_t a = 0; a < job->argument_count; a += 1)
                {
                    printf(" %s", job->arguments[a]);
                }
                printf(" (input %s) is stale\n", job->input != 0 ? job->input : "/dev/null");
                stale += 1;
            }
        }
    }
    free(environment);
    free(pids);
    free(sources);
    free(keys);
    free(taken);
    return result && stale == 0;
}

// Removes, in place, the lines of a -dM text (if it isn't null) that define the version macros of clang's patch release
// or our __BUSTER__.
static void predefines_test_drop_versions(char* text)
{
    static const char* const dropped[] = { "#define __clang_patchlevel__ ", "#define __clang_version__ ", "#define __VERSION__ ", "#define __BUSTER__ " };
    char* to = text;
    for (const char* line = text; line != 0 && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t length = end != 0 ? (size_t)(end - line) + 1 : strlen(line);
        bool kept = true;
        for (uint32_t i = 0; kept && i < BUSTER_ARRAY_LENGTH(dropped); i += 1)
        {
            kept = strncmp(line, dropped[i], strlen(dropped[i])) != 0;
        }
        if (kept)
        {
            memmove(to, line, length);
            to += length;
        }
        line += length;
    }
    if (to != 0)
    {
        *to = 0;
    }
}

// The #define lines of a -dM text, sorted, without trailing spaces (clang writes one after a macro without a body), and
// without the version macros of clang's patch release (which the generator's and the 9700X's clang differ in) and
// our __BUSTER__. `lines` gets pointers into a copy in `arena`. Returns the count, or BUSTER_PP_NONE if it doesn't fit.
static uint32_t predefines_test_lines(const char* text, size_t length, Arena* arena, char*** lines)
{
    static const char* const skipped[] = { "#define __clang_patchlevel__ ", "#define __clang_version__ ", "#define __VERSION__ ", "#define __BUSTER__ " };
    char* copy = arena_push(arena, length + 1, 1);
    uint32_t capacity = 1;
    for (size_t i = 0; i < length; i += 1)
    {
        capacity += text[i] == '\n';
    }
    char** result_lines = BUSTER_ARENA_PUSH_ARRAY(arena, char*, capacity);
    uint32_t count = copy != 0 && result_lines != 0 ? 0 : BUSTER_PP_NONE;
    if (count == 0)
    {
        memcpy(copy, text, length);
        copy[length] = '\n';
        char* line = copy;
        for (size_t i = 0; i <= length; i += 1)
        {
            if (copy[i] == '\n')
            {
                copy[i] = 0;
                for (size_t k = i; k > (size_t)(line - copy) && copy[k - 1] == ' '; k -= 1)
                {
                    copy[k - 1] = 0;
                }
                // Only the definitions: our predefines also push two of them (#pragma push_macro).
                bool kept = strncmp(line, "#define ", 8) == 0;
                for (uint32_t s = 0; kept && s < BUSTER_ARRAY_LENGTH(skipped); s += 1)
                {
                    kept = strncmp(line, skipped[s], strlen(skipped[s])) != 0;
                }
                if (kept)
                {
                    result_lines[count] = line;
                    count += 1;
                }
                line = copy + i + 1;
            }
        }
        qsort(result_lines, count, sizeof(char*), pp_test_compare_lines);
        *lines = result_lines;
    }
    return count;
}

// ---------------------------------------------------------------------------------------------------------------------
// The generator

#define BUSTER_PREDEFINES_TEST_MAX_FEATURES (BUSTER_PREDEFINES_FEATURE_WORDS * 64)
#define BUSTER_PREDEFINES_TEST_MAX_CPUS 256

// What the generator learns from clang: the tables' contents.
typedef struct PredefinesTestTables PredefinesTestTables;
struct PredefinesTestTables
{
    char feature_names[BUSTER_PREDEFINES_TEST_MAX_FEATURES][32];
    uint32_t feature_count;
    PredefinesFlag flags[BUSTER_PREDEFINES_TEST_MAX_FEATURES];
    PredefinesFeatures implies[BUSTER_PREDEFINES_TEST_MAX_FEATURES];
    PredefinesFeatures implied_by[BUSTER_PREDEFINES_TEST_MAX_FEATURES];
    char* macros[BUSTER_PREDEFINES_TEST_MAX_FEATURES];
    char cpu_names[BUSTER_PREDEFINES_TEST_MAX_CPUS][32];
    char cpu_macros[BUSTER_PREDEFINES_TEST_MAX_CPUS][32];
    PredefinesFeatures cpu_features[BUSTER_PREDEFINES_TEST_MAX_CPUS];
    uint32_t cpu_count;
    char tune_names[BUSTER_PREDEFINES_TEST_MAX_CPUS][32];
    uint32_t tune_count;
};

static int32_t predefines_test_feature_find(const PredefinesTestTables* tables, const char* name, size_t length)
{
    int32_t result = -1;
    for (uint32_t i = 0; i < tables->feature_count && result < 0; i += 1)
    {
        result = strlen(tables->feature_names[i]) == length && memcmp(tables->feature_names[i], name, length) == 0 ? (int32_t)i : -1;
    }
    return result;
}

// Adds a feature name (if new); false if the table is full.
static bool predefines_test_feature_add(PredefinesTestTables* tables, const char* name, size_t length)
{
    bool result = predefines_test_feature_find(tables, name, length) >= 0;
    if (!result && tables->feature_count < BUSTER_PREDEFINES_TEST_MAX_FEATURES && length < 32)
    {
        memcpy(tables->feature_names[tables->feature_count], name, length);
        tables->feature_names[tables->feature_count][length] = 0;
        tables->feature_count += 1;
        result = true;
    }
    return result;
}

static void predefines_test_set(PredefinesFeatures* set, uint32_t feature)
{
    set->words[feature / 64] |= 1ull << (feature % 64);
}

static void predefines_test_clear(PredefinesFeatures* set, uint32_t feature)
{
    set->words[feature / 64] &= ~(1ull << (feature % 64));
}

// The IR's "target-features"="+a,-b,...": the +s into `on` and the -s into `off`; with `add`, unknown names are added to
// the tables (else they fail). Returns false if there is no such attribute or a name fails.
static bool predefines_test_ir_features(PredefinesTestTables* tables, const char* text, PredefinesFeatures* on, PredefinesFeatures* off, bool add)
{
    static const char key[] = "\"target-features\"=\"";
    const char* p = text != 0 ? strstr(text, key) : 0;
    bool result = p != 0;
    *on = (PredefinesFeatures){ 0 };
    *off = (PredefinesFeatures){ 0 };
    p = result ? p + sizeof(key) - 1 : 0;
    while (result && *p != '"' && *p != 0)
    {
        const char sign = *p;
        const char* start = p + 1;
        p = start;
        while (*p != ',' && *p != '"' && *p != 0)
        {
            p += 1;
        }
        result = !add || predefines_test_feature_add(tables, start, (size_t)(p - start));
        const int32_t feature = predefines_test_feature_find(tables, start, (size_t)(p - start));
        result &= feature >= 0;
        if (result)
        {
            predefines_test_set(sign == '+' ? on : off, (uint32_t)feature);
        }
        p += *p == ',';
    }
    return result;
}

// The names clang lists after "valid target CPU values are: " in its error for an unknown CPU, into `names`.
static uint32_t predefines_test_cpu_list(const char* text, char names[][32], uint32_t capacity)
{
    static const char key[] = "valid target CPU values are: ";
    const char* p = text != 0 ? strstr(text, key) : 0;
    uint32_t count = 0;
    p = p != 0 ? p + sizeof(key) - 1 : 0;
    while (p != 0 && *p != '\n' && *p != 0 && count < capacity)
    {
        const char* start = p;
        while (*p != ',' && *p != '\n' && *p != 0)
        {
            p += 1;
        }
        const size_t length = (size_t)(p - start) < 31 ? (size_t)(p - start) : 31;
        memcpy(names[count], start, length);
        names[count][length] = 0;
        count += 1;
        p += *p == ',';
        p += *p == ' ';
    }
    return count;
}

// The output of a command line run by the shell, standard error included (for clang's error lists), or null.
static char* predefines_test_shell(const char* command)
{
    FILE* pipe = popen(command, "r");
    char* text = 0;
    size_t length = 0;
    FILE* collected = open_memstream(&text, &length);
    char chunk[1 << 16];
    size_t n;
    while (pipe != 0 && collected != 0 && (n = fread(chunk, 1, sizeof(chunk), pipe)) > 0)
    {
        fwrite(chunk, 1, n, collected);
    }
    if (pipe != 0)
    {
        pclose(pipe);
    }
    if (collected != 0)
    {
        fclose(collected);
    }
    return text;
}

static bool predefines_test_is_apx(const char* name)
{
    static const char* const apx[] = { "egpr", "push2pop2", "ppx", "ndd", "ccmp", "nf", "cf", "zu", "jmpabs" };
    bool result = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(apx); i += 1)
    {
        result |= strcmp(name, apx[i]) == 0;
    }
    return result;
}

static int predefines_test_compare_names(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b);
}

// The flag that turns feature f on (and its -mno- form off), as c_predefines.c's flag kinds name them: none for the
// features without one.
static void predefines_test_flags(const PredefinesTestTables* tables, uint32_t f, char* on, char* off)
{
    const char* name = tables->feature_names[f];
    on[0] = 0;
    off[0] = 0;
    if (strcmp(name, "inline-asm-use-gpr32") == 0)
    {
        strcpy(on, "-mapx-inline-asm-use-gpr32");
    }
    else if (tables->flags[f] == PREDEFINES_FLAG_APX)
    {
        sprintf(on, "-mapx-features=%s", name);
        sprintf(off, "-mno-apx-features=%s", name);
    }
    else if (tables->flags[f] == PREDEFINES_FLAG_PLAIN)
    {
        sprintf(on, "-m%s", name);
        sprintf(off, "-mno-%s", name);
    }
}

// A run of clang -dM -E -march=x86-64 with exactly the features `target`: -mno- of each of x86-64's own features and
// of the soft rules' features outside it, then its features' flags.
static bool predefines_test_configuration(PredefinesTestJobs* jobs, const PredefinesTestTables* tables, PredefinesFeatures target, const char* empty)
{
    static const char* const resettable[] = { "sse", "sse2", "mmx", "x87", "fxsr", "popcnt", "crc32", "xsave" };
    PredefinesTestJob* job = predefines_test_job(jobs);
    bool result = job != 0 && predefines_test_words(jobs, job, "-dM -E -march=x86-64");
    for (uint32_t r = 0; result && r < BUSTER_ARRAY_LENGTH(resettable); r += 1)
    {
        const int32_t g = predefines_test_feature_find(tables, resettable[r], strlen(resettable[r]));
        if (g >= 0 && !predefines_has(target, (uint32_t)g))
        {
            char flag[64];
            snprintf(flag, sizeof(flag), "-mno-%s", resettable[r]);
            result = predefines_test_argument(jobs, job, flag);
        }
    }
    for (uint32_t g = 0; result && g < tables->feature_count; g += 1)
    {
        char on[64];
        char off[64];
        predefines_test_flags(tables, g, on, off);
        if (predefines_has(target, g) && on[0] != 0)
        {
            result = predefines_test_argument(jobs, job, on);
        }
    }
    result = result && predefines_test_words(jobs, job, "-x c") && predefines_test_input(jobs, job, empty);
    return result;
}

// The tables, from clang (see the file's comment), into `tables`. `directory` holds the runs' files. Returns false
// with a message if a run fails or clang's answers don't fit the model.
static bool predefines_test_generate(PredefinesTestTables* tables, const char* directory)
{
    *tables = (PredefinesTestTables){ 0 };
    char empty[4200];
    char source[4200];
    snprintf(empty, sizeof(empty), "%s/predefines-empty.c", directory);
    snprintf(source, sizeof(source), "%s/predefines-function.c", directory);
    FILE* file = fopen(empty, "w");
    bool result = file != 0;
    if (file != 0)
    {
        fclose(file);
    }
    file = fopen(source, "w");
    result &= file != 0;
    if (file != 0)
    {
        fputs("void f(void) {}\n", file);
        fclose(file);
    }
    // The CPUs of -march and -mtune, from clang's errors for unknown ones.
    char* march = result ? predefines_test_shell("env -u CPATH -u C_INCLUDE_PATH clang -march=buster-unknown -E -x c /dev/null 2>&1") : 0;
    char* mtune = result ? predefines_test_shell("env -u CPATH -u C_INCLUDE_PATH clang -mtune=buster-unknown -E -x c /dev/null 2>&1") : 0;
    char* native = result ? predefines_test_shell("env -u CPATH -u C_INCLUDE_PATH clang -### -march=native -E -x c /dev/null 2>&1") : 0;
    tables->cpu_count = predefines_test_cpu_list(march, tables->cpu_names, BUSTER_PREDEFINES_TEST_MAX_CPUS);
    tables->tune_count = predefines_test_cpu_list(mtune, tables->tune_names, BUSTER_PREDEFINES_TEST_MAX_CPUS);
    result &= tables->cpu_count != 0 && tables->tune_count != 0 && native != 0;
    PredefinesTestJobs jobs = { 0 };
    result = result && predefines_test_jobs_initialize(&jobs, 4096);
    // Round 1: every CPU's IR, which names the features.
    for (uint32_t c = 0; result && c < tables->cpu_count; c += 1)
    {
        PredefinesTestJob* job = predefines_test_job(&jobs);
        char flag[64];
        snprintf(flag, sizeof(flag), "-march=%s", tables->cpu_names[c]);
        result = job != 0 && predefines_test_words(&jobs, job, "-S -emit-llvm -o -") && predefines_test_argument(&jobs, job, flag) &&
                 predefines_test_words(&jobs, job, "-x c") && predefines_test_input(&jobs, job, source);
    }
    result = result && predefines_test_run_jobs(&jobs, 0, directory);
    for (uint32_t c = 0; result && c < tables->cpu_count; c += 1)
    {
        PredefinesFeatures on;
        PredefinesFeatures off;
        result = predefines_test_ir_features(tables, jobs.jobs[c].output, &on, &off, true);
        if (!result)
        {
            printf("predefines: no features for -march=%s\n", tables->cpu_names[c]);
        }
    }
    // The names -march=native passes as -target-feature, x87 (a CPU's, which no host list has) and
    // inline-asm-use-gpr32 (-mapx-inline-asm-use-gpr32's).
    static const char key[] = "\"-target-feature\" \"";
    for (const char* p = native != 0 ? strstr(native, key) : 0; result && p != 0; p = strstr(p, key))
    {
        p += sizeof(key) - 1 + 1;
        const char* end = strchr(p, '"');
        result = end != 0 && predefines_test_feature_add(tables, p, (size_t)(end - p));
    }
    result = result && predefines_test_feature_add(tables, "x87", 3) && predefines_test_feature_add(tables, "inline-asm-use-gpr32", 20);
    qsort(tables->feature_names, tables->feature_count, sizeof(tables->feature_names[0]), predefines_test_compare_names);
    for (uint32_t f = 0; f < tables->feature_count; f += 1)
    {
        const char* name = tables->feature_names[f];
        const bool none = strcmp(name, "64bit") == 0 || strcmp(name, "cmov") == 0 || strcmp(name, "cx8") == 0 || strcmp(name, "inline-asm-use-gpr32") == 0;
        tables->flags[f] = none ? PREDEFINES_FLAG_NONE : predefines_test_is_apx(name) ? PREDEFINES_FLAG_APX : PREDEFINES_FLAG_PLAIN;
    }
    // Round 2: each CPU with each soft rule's trigger off, and each feature's closures.
    static const char* const soft[4][2] = { { "sse4.2", "popcnt" }, { "sse", "mmx" }, { "avx", "xsave" }, { "sse4.2", "crc32" } };
    static const char* const resettable[] = { "sse", "sse2", "mmx", "x87", "fxsr", "popcnt", "crc32", "xsave" };
    const uint32_t round2 = jobs.count;
    for (uint32_t c = 0; result && c < tables->cpu_count; c += 1)
    {
        for (uint32_t s = 0; result && s < BUSTER_ARRAY_LENGTH(soft); s += 1)
        {
            PredefinesTestJob* job = predefines_test_job(&jobs);
            char flag[64];
            char trigger[64];
            snprintf(flag, sizeof(flag), "-march=%s", tables->cpu_names[c]);
            snprintf(trigger, sizeof(trigger), "-mno-%s", soft[s][0]);
            result = job != 0 && predefines_test_words(&jobs, job, "-S -emit-llvm -o -") && predefines_test_argument(&jobs, job, flag) &&
                     predefines_test_argument(&jobs, job, trigger) && predefines_test_words(&jobs, job, "-x c") && predefines_test_input(&jobs, job, source);
        }
    }
    const uint32_t round2_features = jobs.count;
    for (uint32_t f = 0; result && f < tables->feature_count; f += 1)
    {
        char on[64];
        char off[64];
        predefines_test_flags(tables, f, on, off);
        // The up closure: -march=x86-64 with x86-64's features and the soft rules' off, but f's own -mno- (the driver
        // keeps a feature's last flag only), then f's flag.
        PredefinesTestJob* job = predefines_test_job(&jobs);
        result = job != 0 && predefines_test_words(&jobs, job, "-S -emit-llvm -o - -march=x86-64");
        for (uint32_t r = 0; result && r < BUSTER_ARRAY_LENGTH(resettable); r += 1)
        {
            if (strcmp(resettable[r], tables->feature_names[f]) != 0)
            {
                char flag[64];
                snprintf(flag, sizeof(flag), "-mno-%s", resettable[r]);
                result = predefines_test_argument(&jobs, job, flag);
            }
        }
        result = result && (on[0] == 0 || predefines_test_argument(&jobs, job, on)) && predefines_test_words(&jobs, job, "-x c") &&
                 predefines_test_input(&jobs, job, source);
        // The down closure: -march=x86-64 and f's -mno- flag.
        job = result ? predefines_test_job(&jobs) : 0;
        result = job != 0 && predefines_test_words(&jobs, job, "-S -emit-llvm -o - -march=x86-64") && (off[0] == 0 || predefines_test_argument(&jobs, job, off)) &&
                 predefines_test_words(&jobs, job, "-x c") && predefines_test_input(&jobs, job, source);
    }
    result = result && predefines_test_run_jobs(&jobs, round2, directory);
    for (uint32_t c = 0; result && c < tables->cpu_count; c += 1)
    {
        PredefinesFeatures off;
        result = predefines_test_ir_features(tables, jobs.jobs[c].output, &tables->cpu_features[c], &off, false);
        for (uint32_t s = 0; result && s < BUSTER_ARRAY_LENGTH(soft); s += 1)
        {
            PredefinesFeatures without;
            result = predefines_test_ir_features(tables, jobs.jobs[round2 + 4 * c + s].output, &without, &off, false);
            const int32_t trigger = predefines_test_feature_find(tables, soft[s][0], strlen(soft[s][0]));
            const int32_t added = predefines_test_feature_find(tables, soft[s][1], strlen(soft[s][1]));
            result &= trigger >= 0 && added >= 0;
            if (result && predefines_has(tables->cpu_features[c], (uint32_t)trigger) && predefines_has(tables->cpu_features[c], (uint32_t)added) &&
                !predefines_has(without, (uint32_t)added))
            {
                // Only the soft rule gave it: the CPU's own features don't have it.
                predefines_test_clear(&tables->cpu_features[c], (uint32_t)added);
            }
        }
    }
    const int32_t cmov = predefines_test_feature_find(tables, "cmov", 4);
    const int32_t cx8 = predefines_test_feature_find(tables, "cx8", 3);
    const int32_t bits64 = predefines_test_feature_find(tables, "64bit", 5);
    result &= cmov >= 0 && cx8 >= 0 && bits64 >= 0;
    for (uint32_t f = 0; result && f < tables->feature_count; f += 1)
    {
        char on[64];
        char off[64];
        predefines_test_flags(tables, f, on, off);
        PredefinesFeatures ignored;
        if (on[0] != 0)
        {
            result = predefines_test_ir_features(tables, jobs.jobs[round2_features + 2 * f].output, &tables->implies[f], &ignored, false);
            // x86-64's features that no flag turns off.
            if (f != (uint32_t)cmov)
            {
                predefines_test_clear(&tables->implies[f], (uint32_t)cmov);
            }
            if (f != (uint32_t)cx8)
            {
                predefines_test_clear(&tables->implies[f], (uint32_t)cx8);
            }
            predefines_test_clear(&tables->implies[f], (uint32_t)bits64);
        }
        else
        {
            predefines_test_set(&tables->implies[f], f);
        }
        if (off[0] != 0)
        {
            result = result && predefines_test_ir_features(tables, jobs.jobs[round2_features + 2 * f + 1].output, &ignored, &tables->implied_by[f], false);
        }
        else
        {
            predefines_test_set(&tables->implied_by[f], f);
        }
        result = result && predefines_has(tables->implies[f], f) && predefines_has(tables->implied_by[f], f);
        if (!result)
        {
            printf("predefines: feature %s: no closures from clang\n", tables->feature_names[f]);
        }
    }
    // The closures must agree: g is in f's implies exactly when f is in g's implied_by, where both have flags.
    for (uint32_t f = 0; result && f < tables->feature_count; f += 1)
    {
        for (uint32_t g = 0; result && g < tables->feature_count; g += 1)
        {
            const bool flagged = tables->flags[f] != PREDEFINES_FLAG_NONE && tables->flags[g] != PREDEFINES_FLAG_NONE;
            result = !flagged || predefines_has(tables->implies[f], g) == predefines_has(tables->implied_by[g], f);
            if (!result)
            {
                printf("predefines: %s implies %s in one closure but not in the other\n", tables->feature_names[f], tables->feature_names[g]);
            }
        }
    }
    // Round 3: config(implies(f)) with and without f, the empty and the full configuration, and each CPU's macros.
    const uint32_t round3 = jobs.count;
    for (uint32_t f = 0; result && f < tables->feature_count; f += 1)
    {
        PredefinesFeatures without = tables->implies[f];
        predefines_test_clear(&without, f);
        result = predefines_test_configuration(&jobs, tables, without, empty) && predefines_test_configuration(&jobs, tables, tables->implies[f], empty);
    }
    PredefinesFeatures all = { 0 };
    for (uint32_t g = 0; g < tables->feature_count; g += 1)
    {
        predefines_test_set(&all, g);
    }
    result = result && predefines_test_configuration(&jobs, tables, (PredefinesFeatures){ 0 }, empty) && predefines_test_configuration(&jobs, tables, all, empty);
    const uint32_t round3_cpus = jobs.count;
    for (uint32_t c = 0; result && c < tables->cpu_count; c += 1)
    {
        PredefinesTestJob* job = predefines_test_job(&jobs);
        char flag[64];
        snprintf(flag, sizeof(flag), "-march=%s", tables->cpu_names[c]);
        result = job != 0 && predefines_test_words(&jobs, job, "-dM -E") && predefines_test_argument(&jobs, job, flag) && predefines_test_words(&jobs, job, "-x c") &&
                 predefines_test_input(&jobs, job, empty);
    }
    result = result && predefines_test_run_jobs(&jobs, round3, directory);
    Arena arena = arena_reserve(1ull << 30);
    char** owned = BUSTER_ARENA_PUSH_ARRAY(&arena, char*, 1 << 16);
    uint32_t owned_count = 0;
    result &= owned != 0;
    for (uint32_t f = 0; result && f < tables->feature_count; f += 1)
    {
        const PredefinesTestJob* without = &jobs.jobs[round3 + 2 * f];
        const PredefinesTestJob* with = &jobs.jobs[round3 + 2 * f + 1];
        char** without_lines = 0;
        char** with_lines = 0;
        const uint32_t without_count = predefines_test_lines(without->output, without->length, &arena, &without_lines);
        const uint32_t with_count = predefines_test_lines(with->output, with->length, &arena, &with_lines);
        result = without->status == 0 && with->status == 0 && without_count != BUSTER_PP_NONE && with_count != BUSTER_PP_NONE && with_count != 0;
        // Its own macros: the lines with it has and without it hasn't. None may go away.
        char* text = 0;
        size_t length = 0;
        FILE* collected = result ? open_memstream(&text, &length) : 0;
        uint32_t i = 0;
        uint32_t j = 0;
        while (result && (i < with_count || j < without_count))
        {
            const int order = i == with_count ? 1 : j == without_count ? -1 : strcmp(with_lines[i], without_lines[j]);
            if (order < 0)
            {
                fprintf(collected, "%s\n", with_lines[i]);
                owned[owned_count] = with_lines[i];
                owned_count += owned_count + 1 < (1u << 16);
            }
            else if (order > 0)
            {
                printf("predefines: turning on %s removes %s\n", tables->feature_names[f], without_lines[j]);
                result = false;
            }
            i += order <= 0;
            j += order >= 0;
        }
        if (collected != 0)
        {
            fclose(collected);
        }
        tables->macros[f] = text;
        result &= text != 0;
    }
    // The macros of the full configuration that no feature owns, nor the empty configuration has: exactly the two
    // c_predefines.c writes from several features.
    qsort(owned, owned_count, sizeof(char*), pp_test_compare_lines);
    char** none_lines = 0;
    char** all_lines = 0;
    const PredefinesTestJob* none = &jobs.jobs[round3_cpus - 2];
    const PredefinesTestJob* full = &jobs.jobs[round3_cpus - 1];
    const uint32_t none_count = result ? predefines_test_lines(none->output, none->length, &arena, &none_lines) : 0;
    const uint32_t all_count = result ? predefines_test_lines(full->output, full->length, &arena, &all_lines) : 0;
    uint32_t combinations = 0;
    for (uint32_t i = 0; result && i < all_count; i += 1)
    {
        const char* line = all_lines[i];
        const bool in_none = bsearch(&line, none_lines, none_count, sizeof(char*), pp_test_compare_lines) != 0;
        const bool in_owned = bsearch(&line, owned, owned_count, sizeof(char*), pp_test_compare_lines) != 0;
        if (!in_none && !in_owned)
        {
            const bool expected = strcmp(line, "#define __APX_F__ 1") == 0 || strcmp(line, "#define __APX_INLINE_ASM_USE_GPR32__ 1") == 0;
            if (!expected)
            {
                printf("predefines: %s needs several features, which c_predefines.c doesn't know\n", line);
            }
            combinations += 1;
            result &= expected;
        }
    }
    result &= combinations == 2;
    // Each CPU's macro name, from its __tune_X__ (and __X and __X__ must be there too).
    for (uint32_t c = 0; result && c < tables->cpu_count; c += 1)
    {
        const char* macros = jobs.jobs[round3_cpus + c].output;
        static const char tune[] = "#define __tune_";
        const char* at = macros != 0 ? strstr(macros, tune) : 0;
        tables->cpu_macros[c][0] = 0;
        if (at != 0)
        {
            at += sizeof(tune) - 1;
            const char* end = strstr(at, "__ 1\n");
            const size_t length = end != 0 && (size_t)(end - at) < 31 ? (size_t)(end - at) : 0;
            memcpy(tables->cpu_macros[c], at, length);
            tables->cpu_macros[c][length] = 0;
            char check[128];
            snprintf(check, sizeof(check), "#define __%s 1\n", tables->cpu_macros[c]);
            result = length != 0 && strstr(macros, check) != 0;
            snprintf(check, sizeof(check), "#define __%s__ 1\n", tables->cpu_macros[c]);
            result = result && strstr(macros, check) != 0;
        }
    }
    if (!result)
    {
        printf("predefines: the generator failed (is clang 23 on PATH?)\n");
    }
    arena_release(&arena);
    predefines_test_jobs_release(&jobs);
    free(march);
    free(mtune);
    free(native);
    unlink(empty);
    unlink(source);
    return result;
}

// A C string literal of `text`.
static void predefines_test_print_string(FILE* stream, const char* text)
{
    fputc('"', stream);
    for (const char* p = text; *p != 0; p += 1)
    {
        if (*p == '\n')
        {
            fputs("\\n", stream);
        }
        else
        {
            if (*p == '"' || *p == '\\')
            {
                fputc('\\', stream);
            }
            fputc(*p, stream);
        }
    }
    fputc('"', stream);
}

// --generate-targets: c_predefines.c's generated block, from clang.
static bool predefines_test_print_tables(const char* directory)
{
    static PredefinesTestTables tables;
    const bool result = predefines_test_generate(&tables, directory);
    if (result)
    {
        static const char* const flag_names[] = { "PREDEFINES_FLAG_NONE", "PREDEFINES_FLAG_PLAIN", "PREDEFINES_FLAG_APX" };
        // clang's version number, from its first line ("clang version 23.1.3 ..."), so that every build of it writes the same.
        char* version = predefines_test_shell("clang --version 2>&1 | head -1");
        const char* found = version != 0 ? strstr(version, "version ") : 0;
        const char* number = found != 0 ? found + 8 : version != 0 ? version : "";
        printf("// GENERATED: clang %.*s's x86-64 features and CPUs, by `./build/compiler test-preprocessor --generate-targets`\n",
               (int)strcspn(number, " \n"), number);
        free(version);
        printf("// (docs/c-preprocessor.md, \"The predefined macros\"). Each feature, sorted by name: how the command line names it, its\n");
        printf("// `implies` and `implied_by` closures and its own macros. Each -march CPU of x86-64: its macros' name and its features.\n");
        printf("static const PredefinesFeature predefines_features[] = {\n");
        for (uint32_t f = 0; f < tables.feature_count; f += 1)
        {
            printf("    { \"%s\", %s, { { 0x%llxull, 0x%llxull } }, { { 0x%llxull, 0x%llxull } }, ", tables.feature_names[f], flag_names[tables.flags[f]],
                   (unsigned long long)tables.implies[f].words[0], (unsigned long long)tables.implies[f].words[1], (unsigned long long)tables.implied_by[f].words[0],
                   (unsigned long long)tables.implied_by[f].words[1]);
            predefines_test_print_string(stdout, tables.macros[f]);
            printf(" },\n");
        }
        printf("};\n\nstatic const PredefinesCPU predefines_cpus[] = {\n");
        for (uint32_t c = 0; c < tables.cpu_count; c += 1)
        {
            printf("    { \"%s\", ", tables.cpu_names[c]);
            if (tables.cpu_macros[c][0] != 0)
            {
                printf("\"%s\"", tables.cpu_macros[c]);
            }
            else
            {
                printf("0");
            }
            printf(", { { 0x%llxull, 0x%llxull } } },\n", (unsigned long long)tables.cpu_features[c].words[0], (unsigned long long)tables.cpu_features[c].words[1]);
        }
        printf("};\n\n// The -mtune names (clang's for x86-64, 32-bit CPUs among them; \"generic\" and \"native\" too).\n");
        printf("static const char* const predefines_tunes[] = {\n   ");
        uint32_t column = 3;
        for (uint32_t t = 0; t < tables.tune_count; t += 1)
        {
            const uint32_t width = (uint32_t)strlen(tables.tune_names[t]) + 4;
            if (column + width > 150)
            {
                printf("\n   ");
                column = 3;
            }
            printf(" \"%s\",", tables.tune_names[t]);
            column += width;
        }
        printf("\n};\n// END GENERATED\n");
        for (uint32_t f = 0; f < tables.feature_count; f += 1)
        {
            free(tables.macros[f]);
        }
    }
    return result;
}

// The generator's tables must be the compiled ones.
static bool predefines_test_tables(const char* directory)
{
    static PredefinesTestTables tables;
    bool result = predefines_test_generate(&tables, directory);
    uint32_t differences = 0;
    differences += result && tables.feature_count != BUSTER_ARRAY_LENGTH(predefines_features);
    differences += result && tables.cpu_count != BUSTER_ARRAY_LENGTH(predefines_cpus);
    differences += result && tables.tune_count != BUSTER_ARRAY_LENGTH(predefines_tunes);
    for (uint32_t f = 0; result && f < tables.feature_count && f < BUSTER_ARRAY_LENGTH(predefines_features); f += 1)
    {
        const PredefinesFeature* ours = &predefines_features[f];
        const bool same = strcmp(ours->name, tables.feature_names[f]) == 0 && ours->flag == tables.flags[f] &&
                          memcmp(&ours->implies, &tables.implies[f], sizeof(PredefinesFeatures)) == 0 &&
                          memcmp(&ours->implied_by, &tables.implied_by[f], sizeof(PredefinesFeatures)) == 0 && strcmp(ours->macros, tables.macros[f]) == 0;
        if (!same && differences < 8)
        {
            printf("predefines tables: feature %s differs from clang's %s\n", ours->name, tables.feature_names[f]);
        }
        differences += !same;
    }
    for (uint32_t c = 0; result && c < tables.cpu_count && c < BUSTER_ARRAY_LENGTH(predefines_cpus); c += 1)
    {
        const PredefinesCPU* ours = &predefines_cpus[c];
        const bool same = strcmp(ours->name, tables.cpu_names[c]) == 0 && strcmp(ours->macro != 0 ? ours->macro : "", tables.cpu_macros[c]) == 0 &&
                          memcmp(&ours->features, &tables.cpu_features[c], sizeof(PredefinesFeatures)) == 0;
        if (!same && differences < 8)
        {
            printf("predefines tables: CPU %s differs from clang's %s\n", ours->name, tables.cpu_names[c]);
        }
        differences += !same;
    }
    for (uint32_t t = 0; result && t < tables.tune_count && t < BUSTER_ARRAY_LENGTH(predefines_tunes); t += 1)
    {
        differences += strcmp(predefines_tunes[t], tables.tune_names[t]) != 0;
    }
    for (uint32_t f = 0; f < tables.feature_count; f += 1)
    {
        free(tables.macros[f]);
    }
    printf("predefines tables: %u features, %u CPUs and %u -mtune names, %u differences from clang's%s\n", tables.feature_count, tables.cpu_count, tables.tune_count,
           differences, differences != 0 ? " (regenerate them with --generate-targets)" : "");
    return result && differences == 0;
}

// Whether two texts have the same words (runs of non-blank characters), as -E -P's text is compared: its spacing and
// blank lines aren't part of the contract.
static bool predefines_test_same_words(const char* a, const char* b)
{
    bool result = true;
    bool more = true;
    const char* p = a;
    const char* q = b;
    while (result && more)
    {
        while (*p == ' ' || *p == '\n' || *p == '\t')
        {
            p += 1;
        }
        while (*q == ' ' || *q == '\n' || *q == '\t')
        {
            q += 1;
        }
        size_t p_length = 0;
        size_t q_length = 0;
        while (p[p_length] != 0 && p[p_length] != ' ' && p[p_length] != '\n' && p[p_length] != '\t')
        {
            p_length += 1;
        }
        while (q[q_length] != 0 && q[q_length] != ' ' && q[q_length] != '\n' && q[q_length] != '\t')
        {
            q_length += 1;
        }
        result = p_length == q_length && memcmp(p, q, p_length) == 0;
        more = p_length != 0;
        p += p_length;
        q += q_length;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The options

// The option sets compared with clang besides the CPUs and the feature flags: each option c_predefines.c reads, and
// the orders that matter (last-wins families, the floating-point state machine, the soft rules, unification).
static const char* const predefines_test_option_sets[] = {
    "-O0", "-O1", "-O2", "-O3", "-O4", "-O9", "-O", "-Og", "-Os", "-Oz", "-Ofast", "-O2 -O0", "-Ofast -O2", "-O2 -Ofast", "-Ofast -fno-fast-math",
    "-O2 -fno-inline", "-O2 -fno-inline-functions", "-O2 -finline-hint-functions", "-O2 -fno-inline -finline", "-O2 -finline-functions -fno-inline-functions",
    "-ffast-math", "-ffast-math -fno-fast-math", "-ffast-math -fno-finite-math-only", "-ffast-math -fmath-errno", "-ffinite-math-only", "-fno-math-errno",
    "-funsafe-math-optimizations", "-funsafe-math-optimizations -ffinite-math-only -fno-math-errno", "-fno-unsafe-math-optimizations -ffast-math",
    "-ffp-model=fast", "-ffp-model=aggressive", "-ffp-model=precise", "-ffp-model=strict", "-ffp-model=strict -ffast-math", "-ffast-math -ffp-model=strict",
    "-Ofast -ffp-model=fast", "-Ofast -ffp-model=aggressive", "-ffast-math -ftrapping-math", "-ffast-math -frounding-math", "-ffast-math -fsigned-zeros",
    "-fno-honor-infinities -fno-honor-nans", "-ffast-math -fhonor-nans", "-ffp-exception-behavior=strict -ffast-math", "-ffast-math -ffp-exception-behavior=strict",
    "-ffast-math -ffp-exception-behavior=ignore", "-ffast-math -fno-approx-func", "-ffast-math -fno-associative-math", "-ffast-math -fno-reciprocal-math",
    "-ffp-model=bogus", "-ffp-eval-method=bogus", "-Obogus", "-funsigned-char", "-fno-signed-char", "-funsigned-char -fsigned-char", "-fno-unsigned-char",
    "-fshort-wchar", "-fshort-wchar -fno-short-wchar", "-fPIC", "-fpic", "-fPIE", "-fpie", "-fno-pic", "-fno-PIE", "-fPIC -fno-pie", "-fno-pie -fpic",
    "-fstack-protector", "-fstack-protector-strong", "-fstack-protector-all", "-fno-stack-protector", "-fstack-protector-all -fstack-protector",
    "-ffreestanding", "-ffreestanding -g", "-ffreestanding -fasynchronous-unwind-tables", "-ffreestanding -funwind-tables", "-fno-asynchronous-unwind-tables",
    "-fno-asynchronous-unwind-tables -g", "-fno-asynchronous-unwind-tables -fexceptions", "-fno-dwarf2-cfi-asm", "-ffreestanding -fdwarf2-cfi-asm",
    "-g -g0 -ffreestanding", "-g0 -g -ffreestanding", "-ffreestanding -gline-tables-only", "-ffreestanding -gsplit-dwarf", "-ffreestanding -fsanitize=address",
    "-fexceptions", "-fexceptions -fno-exceptions", "-fsjlj-exceptions", "-fseh-exceptions", "-fdwarf-exceptions", "-fgnuc-version=0", "-fgnuc-version=14.2.0",
    "-fgnuc-version=5", "-fgnuc-version=0 -std=c23", "-fgnuc-version=12 -std=c23", "-fgnuc-version=4.2.1.5", "-fgnuc-version=", "-fgnu89-inline",
    "-std=c89 -fno-gnu89-inline", "-std=c99 -fgnu89-inline -fno-gnu89-inline", "-pthread", "-fcf-protection", "-fcf-protection=branch", "-fcf-protection=return",
    "-fcf-protection=none", "-fcf-protection=bogus", "-fsanitize=address", "-fsanitize=thread", "-fsanitize=hwaddress", "-fsanitize=kernel-address",
    "-fsanitize=kernel-hwaddress", "-fsanitize=alloc-token", "-fsanitize=address -fno-sanitize=address", "-fsanitize=address -fno-sanitize=all",
    "-fsanitize=undefined", "-fsanitize=memory", "-fsanitize=address,thread", "-fblocks", "-fblocks -fno-blocks", "-fopenmp", "-fopenmp=libgomp",
    "-fopenmp=libiomp5", "-fopenmp -fopenmp-version=31", "-fopenmp -fopenmp-version=45", "-fopenmp -fopenmp-version=52", "-fopenmp -fopenmp-version=60",
    "-fopenmp -fno-openmp", "-fopenacc", "-fdefine-target-os-macros", "-fpascal-strings", "-fno-constant-cfstrings", "-fprofile-generate",
    "-fprofile-instr-generate", "-fcs-profile-generate", "-fprofile-generate -fno-profile-generate", "-fprofile-instr-generate -fno-profile-instr-generate",
    "-mcmodel=kernel", "-mcmodel=medium", "-mcmodel=large", "-mcmodel=tiny", "-mlong-double-64", "-mlong-double-128", "-mlong-double-80",
    "-std=iso9899:199409", "-ansi", "-std=c9x", "-std=gnu1x", "--target=x86_64-linux-gnu", "--target=x86_64-pc-linux-gnu", "-mtune=znver5", "-mtune=i386",
    "-mtune=generic", "-mtune=bogus", "-march=bogus", "-march=i686", "-mabi=sysv", "-mabi=ms", "-mfpmath=sse", "-mfpmath=387", "-mno-sse -mfpmath=387",
    "-mno-sse -mfpmath=sse", "-mfpmath=both", "-mgeneral-regs-only", "-mgeneral-regs-only -mavx2", "-mavx2 -mgeneral-regs-only", "-mgeneral-regs-only -mx87",
    "-mavx2 -mno-avx2", "-mavx2 -mno-avx -mavx2", "-mno-xsave -mavx", "-mavx -mno-xsave", "-mno-popcnt -msse4.2", "-mno-crc32 -msse4.2", "-mno-mmx -msse",
    "-march=znver5 -mno-sse4.2 -mpopcnt", "-msse4", "-mno-sse4", "-msse4 -mno-sse4", "-m80387 -mno-80387", "-mapxf", "-mno-apxf", "-mapxf -mno-apxf",
    "-mapx-features=egpr,ndd", "-mapxf -mno-apx-features=jmpabs", "-mapx-features=bogus", "-mapx-inline-asm-use-gpr32", "-mapxf -mapx-inline-asm-use-gpr32",
    "-mapx-features=egpr -mapx-inline-asm-use-gpr32", "-march=native -mno-avx512f", "-mno-avx512f -march=native", "-march=znver4 -march=znver5",
    // The sanitizers' rules past the pairs predefines_test_options generates: clang's driver reads them after every
    // option, so a later -fno-sanitize= resolves a pair, and each sanitizer keeps its own state.
    "-fsanitize=address -fsanitize=thread -fno-sanitize=address", "-fsanitize=address -fno-sanitize=kernel-address",
    "-fsanitize=hwaddress -fno-sanitize=kernel-hwaddress", "-fsanitize=thread,address -fno-sanitize=address -fsanitize=memory",
    "-fsanitize=address,kernel-address,thread", "-fno-sanitize=address -fsanitize=address", "-fsanitize=", "-fsanitize=address,", "-fno-sanitize=",
    "-fsanitize=all", "-fsanitize=all -fno-sanitize=all", "-fsanitize=address,all", "-fsanitize=bogus", "-fno-sanitize=bogus", "-fsanitize=Address",
    "-fsanitize=memtag", "-fsanitize=memtag-stack", "-fsanitize=memtag -fno-sanitize=memtag", "-fsanitize=memtag -fno-sanitize=memtag-stack,memtag-heap,memtag-globals",
    "-fsanitize=objc-cast", "-fsanitize=shadow-call-stack", "-fsanitize=pointer-compare", "-fsanitize=pointer-subtract,address",
    "-fsanitize=address,pointer-compare -fno-sanitize=address", "-fno-sanitize=pointer-compare -fsanitize=pointer-compare", "-fsanitize=cfi",
    "-fsanitize=cfi -flto", "-fsanitize=cfi -flto -fvisibility=hidden", "-fsanitize=cfi -flto=thin -fvisibility=default",
    "-fsanitize=cfi -flto -fno-lto -fvisibility=hidden", "-fsanitize=cfi-icall -flto", "-fsanitize=cfi-icall -flto=none", "-fsanitize=cfi-icall -flto=jobserver",
    "-fsanitize=cfi -flto -fvisibility=bogus", "-flto=bogus", "-flto=bogus -fno-lto", "-flto=bogus -flto", "-fvisibility=bogus -fvisibility=hidden",
    "-fvisibility=bogus -fvisibility-ms-compat", "-fsanitize=cfi-mfcall -fsanitize-cfi-cross-dso -flto -fvisibility=hidden",
    "-fsanitize=cfi -fsanitize-cfi-cross-dso -flto -fvisibility=hidden", "-fsanitize=kcfi,cfi -flto -fvisibility=hidden", "-fsanitize=function -mcmodel=large",
    "-fsanitize=undefined -mcmodel=large", "-fsanitize=vptr -fno-rtti", "-fsanitize=vptr -fno-rtti -frtti", "-fno-rtti -fsanitize=undefined",
    "-fsanitize=function -mexecute-only", "-fsanitize=kcfi -mpure-code", "-fsanitize=kcfi -mexecute-only -mno-execute-only", "-fsanitize=undefined -mexecute-only",
    "-fsanitize=realtime,object-size", "-fsanitize=realtime,object-size -O2", "-fsanitize=realtime,object-size -O00", "-fsanitize=realtime,object-size -O2 -O0",
    "-fsanitize=fuzzer", "-fsanitize-coverage=func,bb", "-fsanitize-coverage=bogus", "-fno-sanitize-coverage=bogus", "-fsanitize-coverage=trace-pc-guard",
    "-fsanitize-coverage=1", "-fsanitize-ignore-for-ubsan-feature=bogus", "-fsanitize=undefined -fsanitize-ignore-for-ubsan-feature=all",
    // -gmodules turns debug information on unless -gno-modules follows it, -g0 or not; -gcodeview is one of the -g kinds.
    "-ffreestanding -gmodules", "-ffreestanding -gmodules -g0", "-ffreestanding -g0 -gmodules", "-ffreestanding -gmodules -gno-modules",
    "-ffreestanding -gmodules -gno-modules -g0", "-ffreestanding -gno-modules", "-ffreestanding -gcodeview", "-ffreestanding -gcodeview -g0",
    "-ffreestanding -g0 -gcodeview", "-ffreestanding -gdwarf64", "-ffreestanding -gdwarf32", "-ffreestanding -gdwarf-6", "-gstabs", "-gstabs+", "-gcoff",
    "-gxcoff", "-gvms", "-gtoggle",
    // Targets: any vendor, which __is_target_vendor reads (the feature tests check it).
    "--target=x86_64-PC-linux-gnu", "--target=x86_64-none-linux-gnu", "--target=x86_64--linux-gnu", "--target=amd64-Foo-linux-gnu", "--target=x86_64-apple-linux-gnu",
    "-target x86_64-unknown-linux-gnu", "--target=x86_64-linux-gnu -fsanitize=memtag", "--target=x86_64-linux-gnu -mabi=ms",
    // The options clang's driver reads only the last of: an earlier invalid one doesn't count.
    "-march=bogus -march=znver4", "-mtune=bogus -mtune=generic", "-mfpmath=bogus -mfpmath=sse", "-mcmodel=bogus -mcmodel=small", "-mcmodel=large -mcmodel=small",
    "-fcf-protection=bogus -fcf-protection", "-fgnuc-version=1.2.3.4 -fgnuc-version=5", "-Obogus -O2", "-O2 -Obogus", "-mabi=ms -mabi=sysv", "-mabi=sysv -mabi=ms",
    "--target=bogus --target=x86_64-linux-gnu", "-std=bogus -std=c17", "-std=c17 -std=bogus", "-ffp-model=bogus -ffp-model=fast",
    "-ffp-eval-method=bogus -ffp-eval-method=source", "-mapx-features=bogus -mapx-features=egpr",
    // -Ofast as the last -O ignores every -ffp-model= but aggressive without reading its value (#109's seed 1101).
    "-Ofast -ffp-model=bogus", "-ffp-model=bogus -Ofast", "-O2 -Ofast -ffp-model=bogus", "-Ofast -O2 -ffp-model=bogus", "-Ofast -ffp-model=bogus -fno-fast-math",
    "-ffp-model=bogus -Ofast -ffp-model=precise", "-ffp-model=bogus -Ofast -O3",
    "-march=sierraforest -fPIE -O2 -Ofast -mfsgsbase -mno-rdrnd -ffp-model=bogus -ffp-model=fast -mno-usermsr -mno-kl -mno-avx10.2",
    // -fveclib=: ArmPL and SLEEF turn math errno off where they come, whatever follows; the last is checked.
    "-fveclib=SLEEF -fveclib=none", "-fveclib=none -fveclib=SLEEF", "-fveclib=ArmPL -fmath-errno -fveclib=SVML", "-fveclib=bogus", "-fveclib=bogus -fveclib=none",
    "-fveclib=SVML", "-fveclib=libmvec", "-fveclib=AMDLIBM", "-fveclib=Darwin_libsystem_m",
    // OpenMP: on with the last of -fopenmp, -fopenmp= and -fno-openmp, with the last -fopenmp='s runtime.
    "-fopenmp=bogus", "-fopenmp=bogus -fopenmp", "-fopenmp=bogus -fno-openmp", "-fopenmp=libgomp -fopenmp", "-fopenmp -fopenmp=libgomp",
    "-fopenmp=libgomp -fopenmp=libomp", "-fopenmp=libomp -fno-openmp -fopenmp",
    // The -m options that change no macro (predefines_neutral; its flags come alone besides these): the hardening flags
    // against each other, and each value option's rule at its edges, as clang's driver and then cc1 read the value.
    "-mretpoline -mlvi-cfi", "-mretpoline -mlvi-hardening", "-mspeculative-load-hardening -mlvi-cfi", "-mretpoline-external-thunk -mlvi-cfi",
    "-mretpoline -mno-retpoline -mretpoline-external-thunk -mlvi-cfi", "-mretpoline-external-thunk -mno-speculative-load-hardening -mlvi-cfi", "-mseses",
    "-mseses -mlvi-hardening", "-mseses -mretpoline", "-mseses -mno-lvi-cfi", "-mseses -mno-lvi-cfi -mlvi-cfi", "-mno-lvi-cfi -mlvi-cfi -mseses -mretpoline",
    "-mseses -mretpoline-external-thunk", "-mseses -mretpoline-external-thunk -mno-retpoline", "-mseses -mno-seses -mretpoline -mlvi-cfi",
    "-mretpoline -mspeculative-load-hardening", "-mlvi-hardening -mlvi-cfi", "-mlvi-hardening -mno-lvi-hardening -mretpoline", "-mspeculative-load-hardening -mseses",
    "-mlvi-hardening -mseses -mno-lvi-cfi", "-mseses -mno-lvi-cfi -mretpoline", "-mseses -mno-lvi-cfi -mspeculative-load-hardening",
    "-mseses -mno-lvi-cfi -mretpoline-external-thunk", "-mno-lvi-cfi -mseses", "-malign-branch-boundary=16", "-malign-branch-boundary=8", "-malign-branch-boundary=48", "-malign-branch-boundary=032",
    "-malign-branch-boundary=2147483648", "-malign-branch-boundary=4294967296", "-malign-branch-boundary=+32", "-malign-branch-boundary=0x20", "-malign-branch=",
    "-malign-branch=,", "-malign-branch=jcc,,jmp", "-malign-branch=fused,jcc,jmp,call,ret,indirect", "-malign-branch=jcc+jmp", "-malign-branch=bogus", "-masm=att",
    "-masm=intel", "-masm=bogus", "-masm=", "-masm=bogus -masm=att", "-masm=att -masm=bogus", "-mfunction-return=keep", "-mfunction-return=thunk-extern",
    "-mfunction-return=thunk", "-mfunction-return=thunk-extern -mcmodel=large", "-mcmodel=large -mfunction-return=thunk-extern -mcmodel=small",
    "-mfunction-return=thunk-extern -mcmodel=large -mfunction-return=keep", "-mharden-sls=all", "-mharden-sls=none", "-mharden-sls=return",
    "-mharden-sls=indirect-jmp", "-mharden-sls=bogus", "-mharden-sls=all,return", "-mlarge-data-threshold=bogus", "-mlarge-data-threshold=",
    "-mcmodel=medium -mlarge-data-threshold=bogus", "-mcmodel=medium -mlarge-data-threshold=65536", "-mcmodel=large -mlarge-data-threshold=0x10",
    "-mcmodel=large -mlarge-data-threshold=4294967296", "-mcmodel=medium -mlarge-data-threshold=", "-mcmodel=kernel -mlarge-data-threshold=bogus",
    "-mlarge-data-threshold=5 -mcmodel=medium", "-mcmodel=medium -mlarge-data-threshold=08", "-mlinker-version=bogus", "-mlinker-version=",
    "-mpad-max-prefix-size=0", "-mpad-max-prefix-size=4294967295", "-mpad-max-prefix-size=4294967296", "-mpad-max-prefix-size=-1", "-mpad-max-prefix-size=",
    "-mprefer-vector-width=none", "-mprefer-vector-width=512", "-mprefer-vector-width=0512", "-mprefer-vector-width=bogus", "-mprefer-vector-width=-1",
    "-mprefer-vector-width=", "-mprintf-kind=bogus", "-mprintf-kind=", "-mrecip", "-mrecip=", "-mrecip=all", "-mrecip=all:2", "-mrecip=all:22",
    "-mrecip=none,divf", "-mrecip=divf,!sqrtd:3", "-mrecip=div,divf", "-mrecip=divf,div", "-mrecip=divd,divd", "-mrecip=divh,div", "-mrecip=vec-sqrt",
    "-mrecip=!all", "-mrecip=bogus", "-mrecip=,,", "-mrecip=div:x", "-mrecip=div:1:2", "-mrecip=div:", "-mrecip=!", "-mrecip=:1", "-mrecip=default:1",
    "-mrecip=all,none", "-mrecip=bogus -mrecip", "-mrecip -mrecip=bogus", "-mrecip=!div,vec-divh,sqrtf:0,!vec-sqrtd:9", "-mregparm=0", "-mregparm=3",
    "-mregparm=0x3", "-mregparm=08", "-mregparm=-1", "-mregparm=", "-mregparm=4294967296", "-mregparm=bogus", "-mstack-alignment=0", "-mstack-alignment=16",
    "-mstack-alignment=24", "-mstack-alignment=016", "-mstack-alignment=08", "-mstack-alignment=-0", "-mstack-alignment=-16", "-mstack-alignment=4294967296",
    "-mstack-alignment=2147483648", "-mstack-alignment=", "-mstack-alignment=0x10", "-mstack-probe-size=", "-mstack-probe-size=4096", "-mstack-probe-size=0x1000",
    "-mstack-probe-size=010", "-mstack-probe-size=08", "-mstack-probe-size=bogus", "-mstack-probe-size=-1", "-mstack-probe-size=4294967296",
    "-mstack-probe-size=0b101", "-mstack-probe-size=0o17", "-mstack-probe-size=0X1F", "-mstack-probe-size=0x", "-mstack-protector-guard-offset=0",
    "-mstack-protector-guard-offset=-8", "-mstack-protector-guard-offset=-0", "-mstack-protector-guard-offset=2147483647", "-mstack-protector-guard-offset=2147483648",
    "-mstack-protector-guard-offset=-2147483648", "-mstack-protector-guard-offset=-2147483649", "-mstack-protector-guard-offset=010",
    "-mstack-protector-guard-offset=08", "-mstack-protector-guard-offset=-010", "-mstack-protector-guard-offset=-08", "-mstack-protector-guard-offset=0x10",
    "-mstack-protector-guard-offset=", "-mstack-protector-guard-offset=+5", "-mstack-protector-guard-symbol=__stack_chk_guard",
    "-mstack-protector-guard-symbol=a.b", "-mstack-protector-guard-symbol=1abc", "-mstack-protector-guard-symbol=", "-mstack-protector-guard-symbol=_",
    "-mstack-protector-guard=tls", "-mstack-protector-guard=global", "-mstack-protector-guard=sysreg", "-mstack-protector-guard=bogus -mstack-protector-guard=tls",
    "-mstack-protector-guard-reg=fs", "-mstack-protector-guard-reg=gs", "-mstack-protector-guard-reg=es", "-mtls-dialect=gnu", "-mtls-dialect=gnu2",
    "-mtls-dialect=bogus", "-mtls-dialect=", "-mtls-dialect=bogus -mtls-dialect=gnu", "-mtls-dialect=gnu -mtls-dialect=bogus",
    // The -m options with macros of their own: both constant CFString families, and -municode's UNICODE (from
    // <command line>); and -m options clang refuses too: unknown (a misspelled ISA switch among them), another target's,
    // and big-endian x86-64.
    "-mconstant-cfstrings", "-mno-constant-cfstrings", "-mno-constant-cfstrings -mconstant-cfstrings", "-mno-constant-cfstrings -fconstant-cfstrings",
    "-fno-constant-cfstrings -mconstant-cfstrings", "-municode", "-municode -municode", "-municodeX", "-mavx512", "-mavx512f2", "-mbogus", "-mno-bogus", "-m68000",
    "-mfpu=neon", "-mindirect-branch=thunk", "-mbig-endian", "-mrtd", "-mfentry -mnop-mcount", "-mrecord-mcount",
    // clang's -m aliases (predefines_aliases; each alone besides these), against their options in either order: the
    // last of either spelling wins.
    "-fpascal-strings -mno-pascal-strings", "-mpascal-strings -fno-pascal-strings", "-mno-pascal-strings -fpascal-strings",
    "-fno-pascal-strings -mpascal-strings", "-mpascal-strings -mno-pascal-strings", "-mno-pascal-strings -mpascal-strings", "-fno-zvector -mno-zvector",
    "-mno-zvector -fno-zvector", "-fatomic-ignore-denormal-mode -mno-unsafe-fp-atomics",
    "-mno-unsafe-fp-atomics -fatomic-ignore-denormal-mode", "-munsafe-fp-atomics -fno-atomic-ignore-denormal-mode", "-mx87 -mno-fp-ret-in-387",
    "-mno-fp-ret-in-387 -mx87", "-mno-fp-ret-in-387 -m80387", "-mno-fp-ret-in-387 -mfpmath=387", "-mno-fp-ret-in-387 -mfpmath=sse", "-mips1 -march=x86-64",
    "-march=znver4 -mips32r6", "-mips64r6 -march=znver4", "-mips1 -march=native", "-mips5 -mtune=znver5", "-marm", "-msmall-data-limit=8",
    "-miphoneos-version-min=10",
};

// Our predefines for `arguments` (as pp_parse_options and pp_predefines make them) into `*text`, or false where the
// options are refused. `input` is the input file pp_parse_options wants.
static bool predefines_test_ours(const char* arguments, const char* input, char** text, uint32_t* length)
{
    char* words[BUSTER_PREDEFINES_TEST_MAX_ARGUMENTS];
    char storage[4096];
    int count = 0;
    snprintf(storage, sizeof(storage), "%s", arguments);
    for (char* word = strtok(storage, " "); word != 0 && count < BUSTER_PREDEFINES_TEST_MAX_ARGUMENTS - 2; word = strtok(0, " "))
    {
        words[count] = word;
        count += 1;
    }
    words[count] = (char*)input;
    Arena arena = arena_reserve(1ull << 24);
    PPOptions options;
    const char* parsed_input;
    const char* output;
    bool markers;
    bool result = pp_parse_options(count + 1, words, &options, &arena, &parsed_input, &output, &markers);
    *text = result ? malloc(BUSTER_PP_PREDEFINES_BYTES + 32) : 0;
    uint32_t flt_eval_method;
    *length = *text != 0 ? pp_predefines(&options, *text, &flt_eval_method) : BUSTER_PP_PREDEFINES_BYTES + 1;
    result = result && *length <= BUSTER_PP_PREDEFINES_BYTES;
    // -municode's UNICODE, which pp_run puts in <command line> (the sets have no -D or -U): -dM prints it too.
    if (result && options.predefines.unicode)
    {
        *length += (uint32_t)sprintf(*text + *length, "#define UNICODE 1\n");
    }
    arena_release(&arena);
    return result;
}

// One comparison: clang's run (status and -dM text) against ours for `arguments`. Prints the first differences.
static bool predefines_test_compare(const PredefinesTestJob* clang, const char* arguments, const char* input)
{
    char* ours = 0;
    uint32_t our_length = 0;
    const bool accepted = predefines_test_ours(arguments, input, &ours, &our_length);
    bool result = accepted == (clang->status == 0);
    if (!result)
    {
        printf("predefines '%s': clang %s it, buster %s it\n", arguments, clang->status == 0 ? "accepts" : "refuses", accepted ? "accepts" : "refuses");
    }
    if (result && accepted)
    {
        Arena arena = arena_reserve(1ull << 26);
        char** our_lines = 0;
        char** their_lines = 0;
        const uint32_t our_count = predefines_test_lines(ours, our_length, &arena, &our_lines);
        const uint32_t their_count = predefines_test_lines(clang->output, clang->length, &arena, &their_lines);
        result = our_count != BUSTER_PP_NONE && their_count != BUSTER_PP_NONE && their_count != 0;
        uint32_t reported = 0;
        for (uint32_t i = 0, j = 0; result && (i < our_count || j < their_count);)
        {
            const int order = i == our_count ? 1 : j == their_count ? -1 : strcmp(our_lines[i], their_lines[j]);
            if (order != 0 && reported < 4)
            {
                printf("predefines '%s': %s %s\n", arguments, order < 0 ? "only ours:" : "only clang's:", order < 0 ? our_lines[i] : their_lines[j]);
                reported += 1;
            }
            i += order <= 0;
            j += order >= 0;
        }
        result &= reported == 0;
        arena_release(&arena);
    }
    free(ours);
    return result;
}

// A random number from a SplitMix64 state.
static uint64_t predefines_test_random(uint64_t* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// Every CPU, every feature flag (on and off, on x86-64 and on CPUs with the most features), the option sets, and
// `random` random combinations from `seed`, against `clang -dM -E` with the same options.
static bool predefines_test_options(const char* directory, uint64_t seed, uint32_t random)
{
    char empty[4200];
    snprintf(empty, sizeof(empty), "%s/predefines-empty.c", directory);
    FILE* file = fopen(empty, "w");
    bool result = file != 0;
    if (file != 0)
    {
        fclose(file);
    }
    PredefinesTestJobs jobs = { 0 };
    result = result && predefines_test_jobs_initialize(&jobs, 8192);
    char** sets = calloc(8192, sizeof(char*));
    uint32_t set_count = 0;
    result &= sets != 0;
    // The sets: CPUs, flags, the list, and random ones, each as a space-separated string in the jobs' text arena.
    char line[4096];
#define BUSTER_PREDEFINES_TEST_SET(...)                                                                                 \
    do                                                                                                                  \
    {                                                                                                                   \
        snprintf(line, sizeof(line), __VA_ARGS__);                                                                      \
        char* copy = arena_push(&jobs.text, strlen(line) + 1, 1);                                                       \
        if (copy != 0 && set_count < 8192)                                                                              \
        {                                                                                                               \
            memcpy(copy, line, strlen(line) + 1);                                                                       \
            sets[set_count] = copy;                                                                                     \
            set_count += 1;                                                                                             \
        }                                                                                                               \
    } while (0)
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(predefines_cpus); c += 1)
    {
        BUSTER_PREDEFINES_TEST_SET("-march=%s", predefines_cpus[c].name);
    }
    BUSTER_PREDEFINES_TEST_SET("-march=native");
    for (uint32_t f = 0; result && f < BUSTER_ARRAY_LENGTH(predefines_features); f += 1)
    {
        const PredefinesFeature* feature = &predefines_features[f];
        if (feature->flag == PREDEFINES_FLAG_PLAIN)
        {
            BUSTER_PREDEFINES_TEST_SET("-m%s", feature->name);
            BUSTER_PREDEFINES_TEST_SET("-mno-%s", feature->name);
            BUSTER_PREDEFINES_TEST_SET("-march=diamondrapids -mno-%s", feature->name);
            BUSTER_PREDEFINES_TEST_SET("-march=znver5 -mno-%s", feature->name);
            BUSTER_PREDEFINES_TEST_SET("-march=native -mno-%s", feature->name);
            BUSTER_PREDEFINES_TEST_SET("-march=native -m%s", feature->name);
        }
        else if (feature->flag == PREDEFINES_FLAG_APX)
        {
            BUSTER_PREDEFINES_TEST_SET("-mapx-features=%s", feature->name);
            BUSTER_PREDEFINES_TEST_SET("-march=diamondrapids -mno-apx-features=%s", feature->name);
        }
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(predefines_test_option_sets); i += 1)
    {
        BUSTER_PREDEFINES_TEST_SET("%s", predefines_test_option_sets[i]);
    }
    // Each -m flag that changes no macro, alone, and each hardening flag and its -mno- form.
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(predefines_neutral_flags); i += 1)
    {
        BUSTER_PREDEFINES_TEST_SET("%s", predefines_neutral_flags[i]);
    }
    for (uint32_t h = 0; h < PREDEFINES_HARDENING_COUNT; h += 1)
    {
        BUSTER_PREDEFINES_TEST_SET("-m%s", predefines_hardening_names[h]);
        BUSTER_PREDEFINES_TEST_SET("-mno-%s", predefines_hardening_names[h]);
    }
    // Each of clang's -m aliases buster reads as its option, alone.
    // -mzvector's -fzvector, which pp_take_family refuses (test-preprocessor's option sets check it), is left out.
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(predefines_aliases); i += 1)
    {
        if (strcmp(predefines_aliases[i][1], "-fzvector") != 0)
        {
            BUSTER_PREDEFINES_TEST_SET("%s", predefines_aliases[i][0]);
        }
    }
    // The sanitizers clang's incompatible pairs name and those with macros: each alone, each pair, and each pair with
    // either one turned off after it, which clang accepts.
    static const char* const sanitizers[] = {
        "address", "kernel-address", "hwaddress", "kernel-hwaddress", "thread", "memory", "kernel-memory", "leak",
        "safe-stack", "scudo", "type", "realtime", "alloc-token", "kcfi", "function", "undefined",
    };
    for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(sanitizers); a += 1)
    {
        BUSTER_PREDEFINES_TEST_SET("-fsanitize=%s", sanitizers[a]);
        for (uint32_t b = a + 1; b < BUSTER_ARRAY_LENGTH(sanitizers); b += 1)
        {
            BUSTER_PREDEFINES_TEST_SET("-fsanitize=%s,%s", sanitizers[a], sanitizers[b]);
            BUSTER_PREDEFINES_TEST_SET("-fsanitize=%s,%s -fno-sanitize=%s", sanitizers[a], sanitizers[b], sanitizers[a]);
            BUSTER_PREDEFINES_TEST_SET("-fsanitize=%s,%s -fno-sanitize=%s", sanitizers[a], sanitizers[b], sanitizers[b]);
        }
    }
    // Each of them and each other group with -fno-sanitize=all after it, which turns off the group's own bit too (memtag's,
    // which x86-64 doesn't support, is refused only while it is on), and before it; and "all" beside a group in -fsanitize=,
    // which clang refuses and names with the group.
    static const char* const groups[] = {
        "memtag", "nullability", "shift", "cfi", "undefined-trap", "implicit-integer-truncation", "implicit-integer-arithmetic-value-change",
        "implicit-integer-conversion", "implicit-conversion", "integer", "bounds",
    };
    for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(sanitizers) + BUSTER_ARRAY_LENGTH(groups); a += 1)
    {
        const char* name = a < BUSTER_ARRAY_LENGTH(sanitizers) ? sanitizers[a] : groups[a - BUSTER_ARRAY_LENGTH(sanitizers)];
        BUSTER_PREDEFINES_TEST_SET("-fsanitize=%s -fno-sanitize=all", name);
        BUSTER_PREDEFINES_TEST_SET("-fno-sanitize=all -fsanitize=%s", name);
        BUSTER_PREDEFINES_TEST_SET("-fsanitize=%s,memtag -fno-sanitize=all,%s", name, name);
    }
    BUSTER_PREDEFINES_TEST_SET("%s", "-fsanitize=all,memtag");
    BUSTER_PREDEFINES_TEST_SET("%s", "-fsanitize=memtag -fno-sanitize=all -fsanitize=address");
    // Random combinations: a -march (or none), then up to eight feature flags, special flags and other options.
    uint64_t state = seed;
    for (uint32_t r = 0; r < random; r += 1)
    {
        size_t used = 0;
        line[0] = 0;
        if (predefines_test_random(&state) % 10 < 7)
        {
            const uint64_t pick = predefines_test_random(&state) % (BUSTER_ARRAY_LENGTH(predefines_cpus) + 1);
            used += (size_t)snprintf(line + used, sizeof(line) - used, "-march=%s", pick < BUSTER_ARRAY_LENGTH(predefines_cpus) ? predefines_cpus[pick].name : "native");
        }
        const uint32_t count = 1 + (uint32_t)(predefines_test_random(&state) % 8);
        for (uint32_t k = 0; k < count && used + 64 < sizeof(line); k += 1)
        {
            const uint64_t kind = predefines_test_random(&state) % 10;
            if (kind < 6)
            {
                uint32_t f = (uint32_t)(predefines_test_random(&state) % BUSTER_ARRAY_LENGTH(predefines_features));
                while (predefines_features[f].flag != PREDEFINES_FLAG_PLAIN)
                {
                    f = (f + 1) % BUSTER_ARRAY_LENGTH(predefines_features);
                }
                used += (size_t)snprintf(line + used, sizeof(line) - used, " -m%s%s", predefines_test_random(&state) % 2 ? "no-" : "", predefines_features[f].name);
            }
            else if (kind < 9)
            {
                const uint64_t pick = predefines_test_random(&state) % BUSTER_ARRAY_LENGTH(predefines_test_option_sets);
                used += (size_t)snprintf(line + used, sizeof(line) - used, " %s", predefines_test_option_sets[pick]);
            }
            else
            {
                // A flag that changes no macro, a hardening flag (on or off), which clang weighs against the others, or an
                // alias of clang's.
                const uint64_t flags = BUSTER_ARRAY_LENGTH(predefines_neutral_flags);
                const uint64_t pick = predefines_test_random(&state) % (flags + 2 * PREDEFINES_HARDENING_COUNT + BUSTER_ARRAY_LENGTH(predefines_aliases));
                const uint64_t hardening = pick - flags;
                const uint64_t alias = pick - flags - 2 * PREDEFINES_HARDENING_COUNT;
                if (pick < flags)
                {
                    used += (size_t)snprintf(line + used, sizeof(line) - used, " %s", predefines_neutral_flags[pick]);
                }
                else if (hardening < 2 * PREDEFINES_HARDENING_COUNT)
                {
                    used += (size_t)snprintf(line + used, sizeof(line) - used, " -m%s%s", hardening % 2 ? "no-" : "", predefines_hardening_names[hardening / 2]);
                }
                else
                {
                    // -mzvector's next entry, -mno-zvector, for it (pp_take_family refuses -fzvector).
                    const uint64_t taken = alias + (strcmp(predefines_aliases[alias][1], "-fzvector") == 0);
                    used += (size_t)snprintf(line + used, sizeof(line) - used, " %s", predefines_aliases[taken][0]);
                }
            }
        }
        char copy_line[4096];
        snprintf(copy_line, sizeof(copy_line), "%s", line);
        BUSTER_PREDEFINES_TEST_SET("%s", copy_line);
    }
#undef BUSTER_PREDEFINES_TEST_SET
    for (uint32_t s = 0; result && s < set_count; s += 1)
    {
        PredefinesTestJob* job = predefines_test_job(&jobs);
        result = job != 0 && predefines_test_words(&jobs, job, "-dM -E") && predefines_test_words(&jobs, job, sets[s]) && predefines_test_words(&jobs, job, "-x c") &&
                 predefines_test_input(&jobs, job, empty);
    }
    result = result && predefines_test_run_jobs(&jobs, 0, directory);
    uint32_t failures = 0;
    for (uint32_t s = 0; result && s < set_count; s += 1)
    {
        failures += !predefines_test_compare(&jobs.jobs[s], sets[s], empty);
    }
    printf("predefines: %u of %u option sets differ from clang -dM (%u random from seed %llu)\n", failures, set_count, random, (unsigned long long)seed);
    predefines_test_jobs_release(&jobs);
    free(sets);
    unlink(empty);
    return result && failures == 0;
}

// -E -dM of a file with every kind of definition, and __FLT_EVAL_METHOD__, against clang.
static bool predefines_test_dump(const char* directory)
{
    char path[4200];
    snprintf(path, sizeof(path), "%s/predefines-dump.c", directory);
    FILE* file = fopen(path, "w");
    bool result = file != 0;
    if (file != 0)
    {
        fputs("#define EMPTY\n#define OBJECT 1 + 2\n#define SPACED   a   /* c */ b\n#define FUNCTION(a, b) ((a) + (b))\n#define NO_PARAMETERS() x\n"
              "#define VARIADIC(f, ...) f(__VA_ARGS__)\n#define GNU(args...) g(args)\n#define ONLY(...) __VA_ARGS__\n#define STRINGIZE(x) #x\n"
              "#define PASTE(a,b) a ## b\n#define OPT(x, ...) x __VA_OPT__(, ) __VA_ARGS__\n#define LITERALS \"s\" 'c' 1.5e+3 L\"w\" u8\"u\"\n"
              "#define SPLICED a\\\n  b\n#define caf\\u00e9 \\u00e9t\\u00e9 caf\\u00e9\n#define PUNCTUATION <: :> <% %> %: %:%: ... -> ++ <<=\n"
              "#undef OBJECT\n#define OBJECT 2\n",
              file);
        fclose(file);
    }
    PredefinesTestJobs jobs = { 0 };
    result = result && predefines_test_jobs_initialize(&jobs, 16);
    static const char* const evaluation[] = { "", "-ffp-eval-method=source", "-ffp-eval-method=double", "-ffp-eval-method=extended", "-mno-sse", "-mno-sse2" };
    char value_path[4200];
    snprintf(value_path, sizeof(value_path), "%s/predefines-eval.c", directory);
    file = result ? fopen(value_path, "w") : 0;
    result &= file != 0;
    if (file != 0)
    {
        fputs("__FLT_EVAL_METHOD__\n#ifdef __FLT_EVAL_METHOD__\ndefined\n#endif\n", file);
        fclose(file);
    }
    PredefinesTestJob* job = result ? predefines_test_job(&jobs) : 0;
    result = job != 0 && predefines_test_words(&jobs, job, "-dM -E -x c") && predefines_test_input(&jobs, job, path);
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(evaluation); i += 1)
    {
        job = predefines_test_job(&jobs);
        result = job != 0 && predefines_test_words(&jobs, job, "-E -P") && predefines_test_words(&jobs, job, evaluation[i]) &&
                 predefines_test_words(&jobs, job, "-x c") && predefines_test_input(&jobs, job, value_path);
    }
    result = result && predefines_test_run_jobs(&jobs, 0, directory);
    // Ours: the -E command's own -dM printer, into a memory stream.
    uint32_t failures = 0;
    for (uint32_t run = 0; result && run < 1 + BUSTER_ARRAY_LENGTH(evaluation); run += 1)
    {
        char options_text[256];
        snprintf(options_text, sizeof(options_text), "%s", run == 0 ? "-dM" : evaluation[run - 1]);
        char* words[16];
        int count = 0;
        for (char* word = strtok(options_text, " "); word != 0 && count < 14; word = strtok(0, " "))
        {
            words[count] = word;
            count += 1;
        }
        words[count] = run == 0 ? path : value_path;
        Arena arena = arena_reserve(1ull << 24);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        bool done = pp_parse_options(count + 1, words, &options, &arena, &input, &output, &markers);
        options.expansion_starts = false;
        static InternTable table;
        static Preprocessor pp;
        char* text = 0;
        size_t length = 0;
        FILE* stream = open_memstream(&text, &length);
        done = done && stream != 0 && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
        if (done)
        {
            done = pp_run(&pp, input) && (run == 0 ? pp_print_macros(&pp, stream) : pp_print(&pp, stream, false));
            pp_release(&pp);
            intern_release(&table);
        }
        if (stream != 0)
        {
            fclose(stream);
        }
        // Ours has __BUSTER__ where clang has nothing, and the version macros of clang's patch release differ between the
        // clangs the tables were generated with (23.1.3) and the 9700X's (23.1.1): the -dM texts are compared without them.
        char* ours = text != 0 ? text : calloc(1, 1);
        PredefinesTestJob* clang = &jobs.jobs[run];
        if (run == 0)
        {
            predefines_test_drop_versions(ours);
            predefines_test_drop_versions(clang->output);
        }
        // -E -P's text: compared without its blank lines and trailing spaces.
        const bool same = done && clang->status == 0 && clang->output != 0 &&
                          (run == 0 ? strcmp(ours, clang->output) == 0 : predefines_test_same_words(ours, clang->output));
        if (!same)
        {
            printf("predefines %s: buster's\n%s\nclang's\n%s\n", run == 0 ? "-dM" : evaluation[run - 1], ours, clang->output != 0 ? clang->output : "");
        }
        failures += !same;
        free(ours);
        arena_release(&arena);
    }
    printf("predefines: -dM and __FLT_EVAL_METHOD__, %u of %lu differ from clang's\n", failures, (unsigned long)(1 + BUSTER_ARRAY_LENGTH(evaluation)));
    predefines_test_jobs_release(&jobs);
    unlink(path);
    unlink(value_path);
    return result && failures == 0;
}

// predefines_target, without clang: the map to x86_target.c's features names, for each, a feature with its macro;
// x86_target_parse's six profiles give its targets bit for bit; and -m switches, clang's other CPUs and -mtune give
// what they resolve to.
static bool predefines_test_target(void)
{
    bool result = true;
    for (uint32_t f = 0; f < X86_FEATURE_COUNT; f += 1)
    {
        const char* name = predefines_x86_features[f];
        const char* macro = x86_features[f].macro;
        const int32_t index = name != 0 ? predefines_feature_index(name, strlen(name)) : -1;
        char line[128];
        snprintf(line, sizeof(line), "#define %s 1\n", macro != 0 ? macro : "");
        const bool named = name != 0 ? index >= 0 && macro != 0 && strstr(predefines_features[index].macros, line) != 0 : macro == 0;
        if (!named)
        {
            printf("predefines target: x86_target.c's feature %u (%s) isn't clang's %s\n", f, macro != 0 ? macro : "no macro", name != 0 ? name : "(none)");
        }
        result &= named;
    }
    static const char* const profiles[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native" };
    for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(profiles); p += 1)
    {
        const PredefinesOptions options = { .march = profiles[p] };
        X86Target expected;
        X86Target target;
        X86Architecture tune;
        const bool same = x86_target_parse(profiles[p], strlen(profiles[p]), &expected) && predefines_target(&options, &target, &tune) &&
                          target.features == expected.features && target.architecture == expected.architecture &&
                          target.native_znver5 == expected.native_znver5 && tune == X86_ARCH_BASELINE;
        if (!same)
        {
            printf("predefines target: -march=%s gives features %#llx (architecture %u), x86_target_parse %#llx (%u)\n", profiles[p], (unsigned long long)target.features,
                   target.architecture, (unsigned long long)expected.features, expected.architecture);
        }
        result &= same;
    }
    // -mavx2 on x86-64 (with what avx2 implies and the soft rules add), -mno-avx512f on znver5, skylake, and -mtune.
    const uint64_t avx512 = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) | BUSTER_X86_FEATURE(AVX512DQ) |
                            BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BF16) | BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512IFMA) |
                            BUSTER_X86_FEATURE(AVX512VBMI) | BUSTER_X86_FEATURE(AVX512VBMI2) | BUSTER_X86_FEATURE(AVX512VNNI) |
                            BUSTER_X86_FEATURE(AVX512VP2INTERSECT) | BUSTER_X86_FEATURE(AVX512VPOPCNTDQ);
    uint16_t avx2[1] = { (uint16_t)(predefines_feature("avx2") << 1 | 1) };
    uint16_t no_avx512f[1] = { (uint16_t)(predefines_feature("avx512f") << 1) };
    const PredefinesOptions with_avx2 = { .features = avx2, .feature_count = 1, .mtune = "znver5" };
    const PredefinesOptions without_avx512f = { .march = "znver5", .features = no_avx512f, .feature_count = 1, .mtune = "skylake" };
    const PredefinesOptions skylake = { .march = "skylake", .mtune = "generic" };
    X86Target target;
    X86Architecture tune;
    const uint64_t v2 = BUSTER_X86_FEATURE(SSE3) | BUSTER_X86_FEATURE(SSSE3) | BUSTER_X86_FEATURE(SSE41) | BUSTER_X86_FEATURE(SSE42) |
                        BUSTER_X86_FEATURE(POPCNT) | BUSTER_X86_FEATURE(CRC32);
    bool switches = predefines_target(&with_avx2, &target, &tune) &&
                    target.features == (v2 | BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(XSAVE)) &&
                    target.architecture == X86_ARCH_BASELINE && tune == X86_ARCH_ZNVER5;
    X86Target znver5;
    switches = switches && x86_target_parse("znver5", 6, &znver5) && predefines_target(&without_avx512f, &target, &tune) &&
               target.features == (znver5.features & ~avx512) && target.architecture == X86_ARCH_ZNVER5 && tune == X86_ARCH_OTHER;
    switches = switches && predefines_target(&skylake, &target, &tune) && target.architecture == X86_ARCH_OTHER && tune == X86_ARCH_BASELINE &&
               (target.features & BUSTER_X86_FEATURE(AVX2)) != 0 && (target.features & (avx512 | BUSTER_X86_FEATURE(RDTSCP) | BUSTER_X86_FEATURE(CLFLUSH))) == 0;
    if (!switches)
    {
        printf("predefines target: -mavx2, -march=znver5 -mno-avx512f, -march=skylake or -mtune give the wrong target\n");
    }
    result &= switches;
    printf("predefines target: %s (x86_target.c's features, its six profiles, -m switches, other CPUs and -mtune)\n", result ? "as expected" : "WRONG");
    return result;
}

// The -m options buster refuses though clang takes them, without clang: other targets' options, which clang takes on
// x86-64 warning that it ignores them, those that make it compile for another target, and the separate-value ones. Each
// must be refused, as an unknown one is, and each -m flag that changes no macro taken.
static bool predefines_test_refused(void)
{
    static const char* const refused[] = {
        "-mthumb", "-moutline-atomics", "-mno-outline-atomics", "-mglibc", "-mcf-branch-label-scheme=unlabeled", "-miamcu", "-m32", "-mllvm", "-mavx512",
    };
    // Their errors aren't the test's output.
    fflush(stderr);
    const int saved = dup(STDERR_FILENO);
    const int null = open("/dev/null", O_WRONLY);
    bool result = saved >= 0 && null >= 0 && dup2(null, STDERR_FILENO) >= 0;
    uint32_t wrong = 0;
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(refused) + BUSTER_ARRAY_LENGTH(predefines_neutral_flags); i += 1)
    {
        const bool taken = i >= BUSTER_ARRAY_LENGTH(refused);
        const char* option = taken ? predefines_neutral_flags[i - BUSTER_ARRAY_LENGTH(refused)] : refused[i];
        char* text = 0;
        uint32_t length = 0;
        const bool accepted = predefines_test_ours(option, "/dev/null", &text, &length);
        free(text);
        if (accepted != taken)
        {
            printf("predefines: buster %s %s\n", accepted ? "takes" : "refuses", option);
            wrong += 1;
        }
    }
    fflush(stderr);
    result = saved >= 0 && dup2(saved, STDERR_FILENO) >= 0 && result;
    if (saved >= 0)
    {
        close(saved);
    }
    if (null >= 0)
    {
        close(null);
    }
    printf("predefines: -m options refused though clang takes them, and those taken: %s\n", result && wrong == 0 ? "as expected" : "WRONG");
    return result && wrong == 0;
}

// The checks --clang runs (see the file's comment).
static bool predefines_test_run(const char* directory, uint64_t seed)
{
    bool result = predefines_test_tables(directory);
    result &= predefines_test_options(directory, seed, 400);
    result &= predefines_test_dump(directory);
    oracle_cache_report(oracle_cache(), "predefines");
    return result;
}
