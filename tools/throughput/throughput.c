/* Reproducible compiler-throughput harness, not a generated-program benchmark.
 * Ownership: this executable owns deterministic inputs and per-process timing;
 * build.c owns compiler construction. No third-party library is required.
 * Map: tp_generate (workloads), tp_measure (commands), tp_run (paired trials),
 * tp_compare (strict raw-sample replay and CI decision), tp_self_test (tests).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include "platform.h"
#include "hash.h"
#include "stats.h"
#include <stdarg.h>
#include <inttypes.h>
#include <limits.h>
#ifdef _WIN32
#pragma comment(lib, "psapi.lib")
#endif

#define TP_SCHEMA 1
#define TP_CASES 6
#define TP_MAX_JOBS 32
#define TP_ROUNDS 2
#define TP_MAX_FLAGS 32
#define TP_RAW_HEADER "round,pair,order,variant,job,wall_seconds,user_seconds,system_seconds,peak_rss_bytes,cycles,instructions,branches,branch_misses,cache_references,cache_misses,arena_calls,arena_bytes,output_bytes,source_bytes,source_lines,source_functions,output_sha256\n"

static char const* const tp_case_names[TP_CASES] = {
    "tiny_startup", "large_function", "many_functions", "symbol_table", "control_flow", "backend_pressure"};
static char const* const tp_modes[4] = {"none", "mir-stack", "fast", "quality"};

typedef struct TpConfig
{
    char const* command;
    char const* output;
    char const* baseline;
    char const* candidate;
    char const* baseline_id;
    char const* candidate_id;
    char const* profile;
    char const* self_host_root;
    char const* self_host_generated;
    char const* allocation_baseline;
    char const* allocation_candidate;
    char const* flags[TP_MAX_FLAGS];
    unsigned flag_count;
    unsigned mode_mask, pairs, warmups, timeout, seed, scale;
    int cpu, pmu, require_pmu, guard, identical;
} TpConfig;

typedef struct TpWorkload
{
    char name[64];
    char path[TP_PATH_CAP];
    char hash[65];
    uint64_t bytes, lines, functions;
} TpWorkload;

typedef struct TpJob
{
    TpWorkload workload;
    unsigned mode;
    unsigned stage;
} TpJob;

typedef struct TpRow
{
    TpProcess process;
    double arena_calls, arena_bytes;
    uint64_t output_bytes, source_bytes, source_lines, source_functions;
    char output_hash[65];
    int present;
} TpRow;

static void tp_error(char const* format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "throughput: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static int tp_path(char out[TP_PATH_CAP], char const* root, char const* leaf)
{
    int length = snprintf(out, TP_PATH_CAP, "%s/%s", root, leaf);
    return length >= 0 && length < TP_PATH_CAP;
}

static int tp_number(char const* text, unsigned* value)
{
    char* end;
    errno = 0;
    unsigned long n = strtoul(text, &end, 10);
    int ok = text[0] >= '0' && text[0] <= '9' && !*end && errno == 0 && n <= UINT_MAX;
    if (ok)
    {
        *value = (unsigned)n;
    }
    return ok;
}

static int tp_json_string(FILE* file, char const* text)
{
    int ok = fputc('"', file) != EOF;
    for (unsigned char const* p = (unsigned char const*)text; *p && ok; ++p)
    {
        if (*p == '"' || *p == '\\')
        {
            ok = fputc('\\', file) != EOF && fputc(*p, file) != EOF;
        }
        else if (*p < 32)
        {
            ok = fprintf(file, "\\u%04x", *p) >= 0;
        }
        else
        {
            ok = fputc(*p, file) != EOF;
        }
    }
    ok = fputc('"', file) != EOF && ok;
    return ok;
}

static void tp_json_number(FILE* file, double value)
{
    if (isfinite(value))
    {
        fprintf(file, "%.17g", value);
    }
    else
    {
        fputs("null", file);
    }
}

static uint32_t tp_random(uint32_t* state)
{
    /* A fixed specified generator, not the platform-dependent libc rand(). */
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int tp_options(int argc, char** argv, TpConfig* config)
{
    *config = (TpConfig){0};
    config->command = argc > 1 ? argv[1] : "help";
    config->output = "build/throughput-results";
    config->baseline_id = "unspecified";
    config->candidate_id = "unspecified";
    config->profile = "ci";
    config->mode_mask = 15;
    config->pairs = 20;
    config->warmups = 2;
    config->timeout = 120;
    config->seed = 20260907;
    config->scale = 1;
    config->cpu = -1;
    config->guard = 1;
    int ok = 1;
    for (int i = 2; i < argc && ok; ++i)
    {
        char const* key = argv[i];
        if (!strcmp(key, "--pmu"))
        {
            config->pmu = 1;
        }
        else if (!strcmp(key, "--require-pmu"))
        {
            config->pmu = 1;
            config->require_pmu = 1;
        }
        else if (!strcmp(key, "--no-guard"))
        {
            config->guard = 0;
        }
        else if (!strcmp(key, "--require-identical-output"))
        {
            config->identical = 1;
        }
        else if (i + 1 >= argc)
        {
            tp_error("missing value after %s", key);
            ok = 0;
        }
        else
        {
            char const* value = argv[++i];
            if (!strcmp(key, "--output")) config->output = value;
            else if (!strcmp(key, "--baseline")) config->baseline = value;
            else if (!strcmp(key, "--candidate")) config->candidate = value;
            else if (!strcmp(key, "--baseline-id")) config->baseline_id = value;
            else if (!strcmp(key, "--candidate-id")) config->candidate_id = value;
            else if (!strcmp(key, "--profile")) config->profile = value;
            else if (!strcmp(key, "--self-host-root")) config->self_host_root = value;
            else if (!strcmp(key, "--self-host-generated")) config->self_host_generated = value;
            else if (!strcmp(key, "--allocation-baseline")) config->allocation_baseline = value;
            else if (!strcmp(key, "--allocation-candidate")) config->allocation_candidate = value;
            else if (!strcmp(key, "--pairs")) ok = tp_number(value, &config->pairs);
            else if (!strcmp(key, "--warmups")) ok = tp_number(value, &config->warmups);
            else if (!strcmp(key, "--timeout")) ok = tp_number(value, &config->timeout);
            else if (!strcmp(key, "--seed")) ok = tp_number(value, &config->seed);
            else if (!strcmp(key, "--scale")) ok = tp_number(value, &config->scale);
            else if (!strcmp(key, "--cpu"))
            {
                unsigned cpu;
                if (!strcmp(value, "auto"))
                {
                    config->cpu = tp_first_allowed_cpu();
                    ok = config->cpu >= 0;
                }
                else
                {
                    ok = tp_number(value, &cpu) && cpu < 65536;
                    if (ok) config->cpu = (int)cpu;
                }
            }
            else if (!strcmp(key, "--mode"))
            {
                config->mode_mask = 0;
                for (unsigned m = 0; m < 4; ++m)
                {
                    if (!strcmp(value, tp_modes[m])) config->mode_mask |= 1u << m;
                }
                if (!strcmp(value, "all")) config->mode_mask = 15;
                ok = config->mode_mask != 0;
            }
            else if (!strcmp(key, "--flag"))
            {
                ok = config->flag_count < TP_MAX_FLAGS &&
                     strcmp(value, "-c") && strcmp(value, "-E") && strcmp(value, "-S") &&
                     strncmp(value, "-o", 2) && strcmp(value, "-fno-register-allocator") &&
                     strncmp(value, "-fregister-allocator", 20) &&
                     strncmp(value, "-fsource-metrics", 16);
                if (ok) config->flags[config->flag_count++] = value;
            }
            else
            {
                tp_error("unknown option %s", key);
                ok = 0;
            }
            if (!ok) tp_error("invalid value for %s: %s", key, value);
        }
    }
    ok = ok && config->pairs > 0 && config->pairs <= TP_MAX_PAIRS && config->warmups >= 1 &&
         config->warmups <= 20 && config->timeout > 0 && config->timeout <= 3600 &&
         config->seed > 0 && config->scale > 0 && config->scale <= 64 &&
         (!strcmp(config->profile, "smoke") || !strcmp(config->profile, "ci") || !strcmp(config->profile, "full"));
    if (config->guard && config->pairs < 20 && !strcmp(config->command, "run"))
    {
        tp_error("a guard requires at least 20 pairs in EACH of two rounds; use --no-guard for smoke measurements");
        ok = 0;
    }
    if (!!config->allocation_baseline != !!config->allocation_candidate ||
        !!config->self_host_root != !!config->self_host_generated)
    {
        tp_error("both allocation compilers, or both self-host root/generated paths, must be supplied together");
        ok = 0;
    }
    if (!ok) tp_error("invalid options; run 'throughput help'");
    return ok;
}

static int tp_generate(TpConfig const* config, char const* root, TpWorkload workloads[TP_CASES])
{
    unsigned multiplier = !strcmp(config->profile, "smoke") ? 1 : !strcmp(config->profile, "ci") ? 8 : 64;
    multiplier *= config->scale;
    int ok = 1;
    for (unsigned kind = 0; kind < TP_CASES && ok; ++kind)
    {
        TpWorkload* workload = workloads + kind;
        memset(workload, 0, sizeof(*workload));
        snprintf(workload->name, sizeof(workload->name), "%s", tp_case_names[kind]);
        char leaf[128];
        snprintf(leaf, sizeof(leaf), "%s.c", workload->name);
        ok = tp_path(workload->path, root, leaf);
        FILE* file = ok ? fopen(workload->path, "wb") : NULL;
        if (!file)
        {
            tp_error("cannot create workload %s", leaf);
            ok = 0;
        }
        else
        {
            uint32_t random = config->seed ^ (kind * UINT32_C(2654435761));
            if (!random) random = 1;
            fprintf(file, "/* buster-throughput schema=%d seed=%u profile=%s scale=%u case=%s */\n",
                    TP_SCHEMA, config->seed, config->profile, config->scale, workload->name);
            if (kind == 0)
            {
                fputs("unsigned tiny(unsigned x)\n{\n    return x + 1u;\n}\n", file);
                workload->functions = 1;
            }
            else if (kind == 1)
            {
                fputs("unsigned large_function(unsigned x, unsigned y)\n{\n", file);
                for (unsigned i = 0; i < 128 * multiplier; ++i)
                {
                    fprintf(file, "    x = (x ^ (y + %uu)) * 33u + (x >> 7);\n", tp_random(&random));
                }
                fputs("    return x;\n}\n", file);
                workload->functions = 1;
            }
            else if (kind == 2)
            {
                workload->functions = 64 * multiplier;
                for (unsigned i = 0; i < workload->functions; ++i)
                {
                    fprintf(file, "unsigned small_%05u(unsigned x)\n{\n    return (x * %uu) ^ (x >> 3);\n}\n", i, tp_random(&random) | 1u);
                }
            }
            else if (kind == 3)
            {
                unsigned symbols = 256 * multiplier;
                for (unsigned i = 0; i < symbols; ++i)
                {
                    fprintf(file, "volatile unsigned shared_symbol_with_a_long_common_prefix_%06u = %uu;\n", i, tp_random(&random));
                }
                fputs("unsigned read_symbols(unsigned x)\n{\n", file);
                for (unsigned i = 0; i < symbols; ++i)
                {
                    /* A permutation references EVERY symbol exactly once. */
                    unsigned symbol = (i * 131u) % symbols;
                    fprintf(file, "    x ^= shared_symbol_with_a_long_common_prefix_%06u;\n", symbol);
                }
                fputs("    return x;\n}\n", file);
                workload->functions = 1;
            }
            else if (kind == 4)
            {
                workload->functions = 8 * multiplier;
                for (unsigned f = 0; f < workload->functions; ++f)
                {
                    fprintf(file, "unsigned control_%04u(unsigned x, unsigned y)\n{\n", f);
                    for (unsigned i = 0; i < 16; ++i)
                    {
                        fprintf(file, "    if ((x ^ y) & %uu) { x += y; } else { y ^= x + %uu; }\n", 1u << (i % 31), tp_random(&random));
                        fputs("    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }\n", file);
                        fputs("    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }\n", file);
                    }
                    fputs("    return x ^ y;\n}\n", file);
                }
            }
            else
            {
                fputs("extern unsigned opaque(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);\n", file);
                workload->functions = 8 * multiplier;
                for (unsigned f = 0; f < workload->functions; ++f)
                {
                    fprintf(file, "unsigned backend_%04u(unsigned x, unsigned y)\n{\n", f);
                    for (unsigned v = 0; v < 32; ++v)
                    {
                        uint32_t x_add = tp_random(&random);
                        uint32_t y_multiply = tp_random(&random) | 1u;
                        fprintf(file, "    unsigned v%u = (x + %uu) ^ (y * %uu);\n", v, x_add, y_multiply);
                    }
                    for (unsigned r = 0; r < 8; ++r)
                    {
                        fprintf(file, "    x = opaque(v%u, v%u, v%u, v%u, v%u, v%u, v%u, v%u);\n",
                                r, r + 1, r + 2, r + 3, r + 4, r + 5, r + 6, r + 7);
                        for (unsigned v = 0; v < 32; ++v)
                        {
                            fprintf(file, "    v%u = (v%u + x) ^ (v%u >> %u);\n", v, v, (v + 1) % 32, v % 31 + 1);
                        }
                    }
                    fputs("    return x", file);
                    for (unsigned v = 0; v < 32; ++v) fprintf(file, " ^ v%u", v);
                    fputs(";\n}\n", file);
                }
            }
            ok = !ferror(file);
            if (fclose(file) != 0) ok = 0;
            if (ok) ok = tp_hash_file(workload->path, workload->hash, &workload->bytes, &workload->lines);
        }
    }
    return ok;
}

static int tp_read_metrics(char const* path, TpRow* row)
{
    FILE* file = fopen(path, "rb");
    row->arena_calls = NAN;
    row->arena_bytes = NAN;
    unsigned seen = 0;
    int ok = file != NULL;
    if (file)
    {
        char line[16384];
        while (ok && fgets(line, sizeof(line), file))
        {
            size_t length = strlen(line);
            if (!length || line[length - 1] != '\n') { ok = 0; break; }
            char* equal = strchr(line, '=');
            if (!equal) { ok = 0; break; }
            *equal = 0;
            unsigned key = !strcmp(line, "lexed.translated_bytes") ? 1u :
                           !strcmp(line, "lexed.translated_lines") ? 2u :
                           !strcmp(line, "allocation.arena_calls") ? 4u :
                           !strcmp(line, "allocation.arena_bytes") ? 8u : 0u;
            if (key)
            {
                char* end;
                errno = 0;
                unsigned long long value = strtoull(equal + 1, &end, 10);
                ok = !(seen & key) && equal[1] >= '0' && equal[1] <= '9' && !errno &&
                     (*end == '\n' || (*end == '\r' && end[1] == '\n'));
                seen |= key;
                if (key == 1) row->source_bytes = value;
                else if (key == 2) row->source_lines = value;
                else if (key == 4) row->arena_calls = (double)value;
                else row->arena_bytes = (double)value;
            }
        }
        ok = ok && !ferror(file) && (seen & 3) == 3 && row->source_bytes && row->source_lines &&
             ((seen & 12) == 0 || (seen & 12) == 12);
        if (fclose(file) != 0) ok = 0;
    }
    if (!ok) tp_error("missing, malformed or incomplete source metrics: %s", path);
    return ok;
}

static int tp_sample_csv(FILE* file, unsigned round, unsigned pair, unsigned order, unsigned variant, unsigned job, TpRow const* row)
{
    TpProcess const* process = &row->process;
    fprintf(file, "%u,%u,%u,%u,%u,%.17g,%.17g,%.17g,%.17g", round, pair, order, variant, job,
            process->wall_seconds, process->user_seconds, process->system_seconds, process->peak_rss_bytes);
    for (unsigned i = 0; i < TP_COUNTERS; ++i)
    {
        if (isfinite(process->counters[i])) fprintf(file, ",%.17g", process->counters[i]);
        else fputs(",NA", file);
    }
    if (isfinite(row->arena_calls)) fprintf(file, ",%.17g", row->arena_calls);
    else fputs(",NA", file);
    if (isfinite(row->arena_bytes)) fprintf(file, ",%.17g", row->arena_bytes);
    else fputs(",NA", file);
    fprintf(file, ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s\n",
            row->output_bytes, row->source_bytes, row->source_lines, row->source_functions, row->output_hash);
    return !ferror(file) && fflush(file) == 0;
}

typedef struct TpArguments
{
    char* values[TP_MAX_FLAGS + 24];
    char mode[128];
    char metrics[TP_PATH_CAP + 32];
    char include_generated[TP_PATH_CAP + 3];
    unsigned count;
} TpArguments;

static void tp_compile_arguments(TpConfig const* config, TpJob const* job,
                                 char const* compiler, char const* artifact,
                                 char const* metrics, TpArguments* arguments)
{
    char** args = arguments->values;
    unsigned argc = 0;
    snprintf(arguments->mode, sizeof(arguments->mode), "-fregister-allocator=%s", tp_modes[job->mode]);
    snprintf(arguments->metrics, sizeof(arguments->metrics), "-fsource-metrics=%s", metrics);
    args[argc++] = (char*)compiler;
    args[argc++] = "cc";
    args[argc++] = job->stage ? "-g" : "-g0";
    args[argc++] = "-O0";
    args[argc++] = arguments->metrics;
    if (job->stage)
    {
        args[argc++] = "-Isrc";
        snprintf(arguments->include_generated, sizeof(arguments->include_generated), "-I%s", config->self_host_generated);
        args[argc++] = arguments->include_generated;
        args[argc++] = "-DBUSTER_UNITY_BUILD=1";
        args[argc++] = "-DBUSTER_INCLUDE_TESTS=0";
#ifndef _WIN32
        args[argc++] = "-lm";
#endif
    }
    else
    {
        args[argc++] = "-c";
    }
    for (unsigned i = 0; i < config->flag_count; ++i) args[argc++] = (char*)config->flags[i];
    /* Buster's -O options reset the allocator. The measured mode must be
     * selected AFTER every default and user-provided optimization option. */
    args[argc++] = arguments->mode;
    args[argc++] = (char*)job->workload.path;
    args[argc++] = "-o";
    args[argc++] = (char*)artifact;
    args[argc] = NULL;
    arguments->count = argc;
}

static int tp_measure(TpConfig const* config, TpJob const* job, char const* compiler,
                       char const* directory, char const* output_root, unsigned variant,
                       char const* sample_id, int pmu, TpRow* row, FILE* commands, FILE* capabilities)
{
    memset(row, 0, sizeof(*row));
    row->source_bytes = job->workload.bytes;
    row->source_lines = job->workload.lines;
    row->source_functions = job->workload.functions;
    char artifact[TP_PATH_CAP], metrics[TP_PATH_CAP], log[TP_PATH_CAP];
    char leaf[256];
    snprintf(leaf, sizeof(leaf), "%s-%s-%u%s", job->workload.name, tp_modes[job->mode], variant, job->stage ? ".exe" : ".o");
    int ok = tp_path(artifact, output_root, leaf);
    snprintf(leaf, sizeof(leaf), "%s.metrics", sample_id);
    ok = ok && tp_path(metrics, output_root, leaf);
    snprintf(leaf, sizeof(leaf), "%s.log", sample_id);
    ok = ok && tp_path(log, output_root, leaf);
    TpArguments arguments;
    if (ok)
    {
        tp_compile_arguments(config, job, compiler, artifact, metrics, &arguments);
        /* Reject a stale artifact rather than accidentally benchmarking a
         * failed compiler that left last iteration's output behind. */
        ok = (remove(artifact) == 0 || errno == ENOENT) && (remove(metrics) == 0 || errno == ENOENT);
    }
    if (ok)
    {
        fputs("{\"sample\":", commands); tp_json_string(commands, sample_id);
        fputs(",\"cwd\":", commands); tp_json_string(commands, directory);
        fputs(",\"argv\":[", commands);
        for (unsigned i = 0; i < arguments.count; ++i)
        {
            if (i) fputc(',', commands);
            tp_json_string(commands, arguments.values[i]);
        }
        fputs("]}\n", commands);
        ok = fflush(commands) == 0;
    }
    if (ok)
    {
        row->process = tp_process(arguments.values, directory, log, config->timeout, config->cpu, pmu);
        TpProcess const* p = &row->process;
        ok = !p->launch_error && !p->timed_out && !p->signal_number && p->exit_code == 0 && p->wall_seconds > 0.0;
        if (!ok)
        {
            tp_error("sample %s failed: exit=%d signal=%d timeout=%d launch_error=%d; see %s",
                     sample_id, p->exit_code, p->signal_number, p->timed_out, p->launch_error, log);
        }
        fprintf(capabilities, "{\"sample\":"); tp_json_string(capabilities, sample_id);
        fprintf(capabilities, ",\"exit_code\":%d,\"signal\":%d,\"timeout\":%d,\"launch_error\":%d,\"pmu\":{",
                p->exit_code, p->signal_number, p->timed_out, p->launch_error);
        for (unsigned i = 0; i < TP_COUNTERS; ++i)
        {
            if (i) fputc(',', capabilities);
            tp_json_string(capabilities, tp_counter_names[i]);
            fprintf(capabilities, ":{\"errno\":%d,\"running_fraction\":", p->counter_errors[i]);
            tp_json_number(capabilities, p->running_fraction[i]);
            fputc('}', capabilities);
            if (config->require_pmu && pmu && !isfinite(p->counters[i])) ok = 0;
        }
        fputs("}}\n", capabilities);
        if (fflush(capabilities) != 0) ok = 0;
    }
    if (ok)
    {
        uint64_t ignored_lines;
        ok = tp_hash_file(artifact, row->output_hash, &row->output_bytes, &ignored_lines) && row->output_bytes > 0;
        if (!ok) tp_error("sample %s did not produce a nonempty artifact", sample_id);
        ok = tp_read_metrics(metrics, row) && ok;
        if (job->stage && !row->source_lines)
        {
            tp_error("self-host sample has no source metrics: %s", metrics);
            ok = 0;
        }
        row->present = ok;
    }
    return ok;
}

// Only Linux metadata snapshots copy procfs/sysfs capability files.
#ifdef __linux__
static int tp_copy_file(char const* source, char const* target)
{
    FILE* input = fopen(source, "rb");
    FILE* output = input ? fopen(target, "wb") : NULL;
    int ok = input && output;
    if (ok)
    {
        unsigned char buffer[32768];
        size_t count;
        while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0 && ok)
        {
            ok = fwrite(buffer, 1, count, output) == count;
        }
        ok = ok && !ferror(input);
    }
    if (input && fclose(input) != 0) ok = 0;
    if (output && fclose(output) != 0) ok = 0;
    return ok;
}

#endif

static int tp_metadata(TpConfig const* config, char const* root, char const* baseline, char const* candidate,
                       TpJob const* jobs, unsigned job_count)
{
    char path[TP_PATH_CAP];
    int ok = tp_path(path, root, "metadata.json");
    FILE* file = ok ? fopen(path, "wb") : NULL;
    ok = file != NULL;
    if (ok)
    {
        time_t now = time(NULL);
        struct tm const* utc = gmtime(&now);
        char timestamp[64] = "unknown";
        if (utc) strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", utc);
        fprintf(file, "{\"schema\":%d,\"created_utc\":", TP_SCHEMA); tp_json_string(file, timestamp);
        fprintf(file, ",\"seed\":%u,\"pairs_per_round\":%u,\"rounds\":%u,\"warmups\":%u,\"cpu\":%d,\"profile\":",
                config->seed, config->pairs, TP_ROUNDS, config->warmups, config->cpu);
        tp_json_string(file, config->profile);
        fprintf(file, ",\"scale\":%u,\"cache_policy\":\"warm-filesystem-new-process\",\"clock\":\"monotonic\",", config->scale);
        fputs("\"wall_scope\":\"process launch through wait completion; no PMU or allocation instrumentation\",", file);
        fputs("\"cpu_scope\":\"OS child user plus system CPU time\",\"rss_scope\":\"OS per-child peak; not concurrent tree sum\",", file);
        fputs("\"pmu_scope\":\"separate diagnostic replay; user-space inherited hardware events; >=90% running required\",", file);
        fputs("\"allocation_scope\":\"separate explicitly instrumented compiler replay; arena calls and requested bytes\",", file);
        fputs("\"compiler_provenance\":[", file);
        char const* compilers[2] = {baseline, candidate};
        char const* ids[2] = {config->baseline_id, config->candidate_id};
        for (unsigned i = 0; i < 2 && ok; ++i)
        {
            uint64_t bytes = 0, lines = 0;
            char digest[65];
            ok = tp_hash_file(compilers[i], digest, &bytes, &lines);
            if (i) fputc(',', file);
            fputs("{\"path\":", file); tp_json_string(file, compilers[i]);
            fputs(",\"revision_label\":", file); tp_json_string(file, ids[i]);
            fputs(",\"sha256\":", file); tp_json_string(file, ok ? digest : "unreadable");
            fprintf(file, ",\"bytes\":%" PRIu64 "}", bytes);
        }
        fputs("],\"allocation_compilers\":[", file);
        char const* probes[2] = {config->allocation_baseline, config->allocation_candidate};
        for (unsigned i = 0; i < 2 && ok && probes[i]; ++i)
        {
            uint64_t bytes = 0, lines = 0;
            char digest[65];
            ok = tp_hash_file(probes[i], digest, &bytes, &lines);
            if (i) fputc(',', file);
            fputs("{\"path\":", file); tp_json_string(file, probes[i]);
            fputs(",\"sha256\":", file); tp_json_string(file, ok ? digest : "unreadable");
            fprintf(file, ",\"bytes\":%" PRIu64 "}", bytes);
        }
        fputs("],\"flags\":[", file);
        for (unsigned i = 0; i < config->flag_count; ++i)
        {
            if (i) fputc(',', file);
            tp_json_string(file, config->flags[i]);
        }
        fputs("],\"environment\":{", file);
        static char const* const environment[] = {"PATH", "CC", "CFLAGS", "CPPFLAGS", "LDFLAGS", "CPATH", "C_INCLUDE_PATH", "LIBRARY_PATH",
            "SDKROOT", "MACOSX_DEPLOYMENT_TARGET", "BUSTER_SINGLE_THREADED", "BUSTER_TEST_JOBS", "ASAN_OPTIONS", "UBSAN_OPTIONS", "LD_PRELOAD",
            "RUNNER_OS", "RUNNER_ARCH", "ImageOS", "ImageVersion"};
        for (unsigned i = 0; i < sizeof(environment) / sizeof(environment[0]); ++i)
        {
            if (i) fputc(',', file);
            tp_json_string(file, environment[i]); fputc(':', file);
            char const* value = getenv(environment[i]);
            if (value) tp_json_string(file, value); else fputs("null", file);
        }
        fputs("},\"jobs\":[", file);
        for (unsigned i = 0; i < job_count; ++i)
        {
            TpWorkload const* w = &jobs[i].workload;
            if (i) fputc(',', file);
            fprintf(file, "{\"job\":%u,\"name\":", i); tp_json_string(file, w->name);
            fputs(",\"mode\":", file); tp_json_string(file, tp_modes[jobs[i].mode]);
            fputs(",\"source\":", file); tp_json_string(file, w->path);
            fputs(",\"sha256\":", file); tp_json_string(file, w->hash);
            fprintf(file, ",\"bytes\":%" PRIu64 ",\"physical_lines\":%" PRIu64 ",\"defined_functions\":%" PRIu64 "}",
                    w->bytes, w->lines, w->functions);
        }
        fputs("]}\n", file);
        ok = ok && !ferror(file);
        if (fclose(file) != 0) ok = 0;
    }
#ifdef __linux__
    /* These snapshots retain CPU model/microcode, topology, affinity, kernel
     * restrictions and cgroup quotas, not an invented machine identity. */
    static char const* const sources[] = {"/proc/cpuinfo", "/proc/version", "/proc/self/status", "/proc/sys/kernel/perf_event_paranoid",
        "/sys/fs/cgroup/cpu.max", "/sys/fs/cgroup/cpuset.cpus.effective", "/sys/devices/system/cpu/smt/active",
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_driver"};
    static char const* const names[] = {"cpuinfo.txt", "kernel.txt", "affinity-and-host-status.txt", "perf_event_paranoid.txt",
        "cpu-quota.txt", "cpuset.txt", "smt.txt", "governor.txt", "frequency-driver.txt"};
    for (unsigned i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i)
    {
        if (tp_path(path, root, names[i])) (void)tp_copy_file(sources[i], path);
    }
#else
    if (tp_path(path, root, "host.txt"))
    {
        FILE* host = fopen(path, "wb");
        if (host)
        {
#ifdef _WIN32
            SYSTEM_INFO info;
            GetNativeSystemInfo(&info);
            fprintf(host, "architecture=%u\nprocessors=%lu\nprocessor_identifier=%s\n",
                    info.wProcessorArchitecture, (unsigned long)info.dwNumberOfProcessors,
                    getenv("PROCESSOR_IDENTIFIER") ? getenv("PROCESSOR_IDENTIFIER") : "unavailable");
#else
            fputs("platform=macOS\nPMU=unavailable\naffinity=unsupported; requests fail rather than silently ignoring them\n", host);
#endif
            fclose(host);
        }
    }
#endif
    return ok;
}

static int tp_completion(char const* root, unsigned jobs, unsigned pairs, int guard, int write_file)
{
    static char const* const files[] = {"samples.csv", "telemetry.csv", "metadata.json", "jobs.tsv", "commands.jsonl", "capabilities.jsonl"};
    char path[TP_PATH_CAP];
    int ok = tp_path(path, root, "complete.txt");
    FILE* file = ok ? fopen(path, write_file ? "wb" : "rb") : NULL;
    ok = file != NULL;
    if (ok)
    {
        if (write_file)
        {
            fprintf(file, "schema=%d jobs=%u pairs=%u rounds=%d guard=%d\n", TP_SCHEMA, jobs, pairs, TP_ROUNDS, guard);
        }
        else
        {
            char line[256], extra;
            unsigned schema, actual_jobs, actual_pairs, rounds, actual_guard;
            ok = fgets(line, sizeof(line), file) && sscanf(line, "schema=%u jobs=%u pairs=%u rounds=%u guard=%u %c",
                    &schema, &actual_jobs, &actual_pairs, &rounds, &actual_guard, &extra) == 5 &&
                    schema == TP_SCHEMA && actual_jobs == jobs && actual_pairs == pairs && rounds == TP_ROUNDS && actual_guard == (unsigned)guard;
        }
        for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]) && ok; ++i)
        {
            uint64_t bytes, lines;
            char hash[65];
            ok = tp_path(path, root, files[i]) && tp_hash_file(path, hash, &bytes, &lines);
            if (write_file && ok)
            {
                fprintf(file, "%s %s\n", hash, files[i]);
            }
            else if (ok)
            {
                char actual[65], name[128], line[256], extra;
                ok = fgets(line, sizeof(line), file) && sscanf(line, "%64s %127s %c", actual, name, &extra) == 2 &&
                     !strcmp(actual, hash) && !strcmp(name, files[i]);
            }
        }
        if (!write_file && fgetc(file) != EOF) ok = 0;
        ok = ok && !ferror(file);
        if (fclose(file) != 0) ok = 0;
    }
    return ok;
}

static size_t tp_row_index(unsigned job, unsigned round, unsigned variant, unsigned pair, unsigned pairs)
{
    return (((size_t)job * TP_ROUNDS + round) * 2 + variant) * pairs + pair;
}

static int tp_parse_double(char const* text, double* value, int optional)
{
    int ok;
    if (optional && !strcmp(text, "NA"))
    {
        *value = NAN;
        ok = 1;
    }
    else
    {
        char* end;
        errno = 0;
        *value = strtod(text, &end);
        ok = text[0] && !*end && !errno && isfinite(*value) && *value >= 0.0;
    }
    return ok;
}

static int tp_parse_u64(char const* text, uint64_t* value)
{
    char* end;
    errno = 0;
    *value = (uint64_t)strtoull(text, &end, 10);
    return text[0] >= '0' && text[0] <= '9' && !*end && !errno;
}

static int tp_load_samples(char const* root, char const* name, unsigned jobs, unsigned pairs, int require_complete, TpRow* rows)
{
    char path[TP_PATH_CAP];
    int ok = tp_path(path, root, name);
    FILE* file = ok ? fopen(path, "rb") : NULL;
    ok = file != NULL;
    if (ok)
    {
        char line[4096];
        ok = fgets(line, sizeof(line), file) && !strcmp(line, TP_RAW_HEADER);
        size_t records = 0;
        unsigned* orders = (unsigned*)calloc((size_t)jobs * TP_ROUNDS * pairs, sizeof(unsigned));
        ok = ok && orders != NULL;
        while (ok && fgets(line, sizeof(line), file))
        {
            size_t length = strlen(line);
            if (!length || line[length - 1] != '\n') { ok = 0; break; }
            line[length - 1] = 0;
            char* fields[22];
            unsigned count = 1;
            fields[0] = line;
            for (char* p = line; *p && ok; ++p)
            {
                if (*p == ',')
                {
                    *p = 0;
                    if (count == 22) { ok = 0; break; }
                    fields[count++] = p + 1;
                }
            }
            if (!ok || count != 22) { ok = 0; break; }
            unsigned round, pair, order, variant, job;
            ok = tp_number(fields[0], &round) && tp_number(fields[1], &pair) && tp_number(fields[2], &order) &&
                 tp_number(fields[3], &variant) && tp_number(fields[4], &job) &&
                 round < TP_ROUNDS && pair < pairs && order < 2 && variant < 2 && job < jobs;
            if (!ok) break;
            TpRow* row = rows + tp_row_index(job, round, variant, pair, pairs);
            if (row->present) { ok = 0; break; }
            size_t order_index = ((size_t)job * TP_ROUNDS + round) * pairs + pair;
            unsigned order_bit = 1u << order;
            if (orders[order_index] & order_bit) { ok = 0; break; }
            orders[order_index] |= order_bit;
            double* fixed[] = {&row->process.wall_seconds, &row->process.user_seconds, &row->process.system_seconds, &row->process.peak_rss_bytes};
            for (unsigned i = 0; i < 4 && ok; ++i) ok = tp_parse_double(fields[5 + i], fixed[i], 0);
            for (unsigned i = 0; i < TP_COUNTERS && ok; ++i) ok = tp_parse_double(fields[9 + i], &row->process.counters[i], 1);
            ok = ok && tp_parse_double(fields[15], &row->arena_calls, 1) && tp_parse_double(fields[16], &row->arena_bytes, 1) &&
                 tp_parse_u64(fields[17], &row->output_bytes) && tp_parse_u64(fields[18], &row->source_bytes) &&
                 tp_parse_u64(fields[19], &row->source_lines) && tp_parse_u64(fields[20], &row->source_functions) &&
                 row->process.wall_seconds > 0.0 && row->process.peak_rss_bytes > 0.0 && row->output_bytes > 0 &&
                 row->source_bytes > 0 && row->source_lines > 0 && strlen(fields[21]) == 64;
            for (unsigned i = 0; i < 64 && ok; ++i)
            {
                char c = fields[21][i];
                ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }
            if (ok)
            {
                memcpy(row->output_hash, fields[21], 65);
                row->present = 1;
                ++records;
            }
        }
        ok = ok && !ferror(file) && (!require_complete || records == (size_t)jobs * TP_ROUNDS * 2 * pairs);
        for (unsigned job = 0; job < jobs && ok; ++job)
        {
            for (unsigned round = 0; round < TP_ROUNDS && ok; ++round)
            {
                unsigned present = 0;
                for (unsigned pair = 0; pair < pairs; ++pair)
                {
                    for (unsigned variant = 0; variant < 2; ++variant)
                        present += rows[tp_row_index(job, round, variant, pair, pairs)].present != 0;
                }
                ok = present == 2 * pairs || (!require_complete && present == 0);
            }
        }
        free(orders);
        if (fclose(file) != 0) ok = 0;
    }
    if (!ok) tp_error("malformed, duplicated, missing, or invalid samples; comparison refused");
    return ok;
}

static double tp_metric(TpRow const* row, unsigned metric)
{
    double result = NAN;
    if (metric == 0) result = row->process.wall_seconds;
    else if (metric == 1) result = row->process.user_seconds + row->process.system_seconds;
    else if (metric == 2) result = row->process.peak_rss_bytes;
    else if (metric < 9) result = row->process.counters[metric - 3];
    else if (metric == 9) result = row->arena_calls;
    else if (metric == 10) result = row->arena_bytes;
    else if (metric == 11) result = (double)row->output_bytes;
    else if (metric == 12) result = (double)row->source_lines / row->process.wall_seconds;
    else if (metric == 13 && row->source_functions) result = (double)row->source_functions / row->process.wall_seconds;
    else if (metric == 14) result = (double)row->source_bytes / row->process.wall_seconds;
    return result;
}

static char const* const tp_summary_names[] = {"summary.json", "summary.md"};

static int tp_remove_summaries(char const* root)
{
    int ok = 1;
    for (unsigned i = 0; i < sizeof(tp_summary_names) / sizeof(tp_summary_names[0]); ++i)
    {
        char path[TP_PATH_CAP];
        int removed = tp_path(path, root, tp_summary_names[i]);
        if (removed)
        {
            removed = remove(path) == 0 || errno == ENOENT;
        }
        if (!removed)
        {
            tp_error("cannot remove stale or invalid summary %s in %s", tp_summary_names[i], root);
        }
        /* Attempt both reports even when one cannot be removed. */
        ok = removed && ok;
    }
    return ok;
}

static int tp_compare(char const* root)
{
    char path[TP_PATH_CAP];
    unsigned jobs = 0, pairs = 0, guard = 0, schema = 0, rounds = 0;
    /* A rejected replay must not leave an earlier verdict publishable.
     * Only derived reports are removed; sealed evidence remains intact. */
    int ok = tp_remove_summaries(root) && tp_path(path, root, "complete.txt");
    FILE* config = ok ? fopen(path, "rb") : NULL;
    ok = config != NULL;
    if (config)
    {
        char line[256], extra;
        ok = fgets(line, sizeof(line), config) && sscanf(line, "schema=%u jobs=%u pairs=%u rounds=%u guard=%u %c",
                &schema, &jobs, &pairs, &rounds, &guard, &extra) == 5 && schema == TP_SCHEMA && rounds == TP_ROUNDS &&
                jobs > 0 && jobs <= TP_MAX_JOBS && pairs > 0 && pairs <= TP_MAX_PAIRS && guard <= 1 && (!guard || pairs >= 20);
        fclose(config);
    }
    if (ok) ok = tp_completion(root, jobs, pairs, (int)guard, 0);
    TpRow* rows = ok ? (TpRow*)calloc((size_t)jobs * TP_ROUNDS * 2 * pairs, sizeof(TpRow)) : NULL;
    ok = ok && rows != NULL;
    if (ok) ok = tp_load_samples(root, "samples.csv", jobs, pairs, 1, rows);
    TpRow* probes = ok ? (TpRow*)calloc((size_t)jobs * TP_ROUNDS * 2 * 3, sizeof(TpRow)) : NULL;
    ok = ok && probes != NULL;
    if (ok) ok = tp_load_samples(root, "telemetry.csv", jobs, 3, 0, probes);
    char names[TP_MAX_JOBS][128];
    memset(names, 0, sizeof(names));
    FILE* manifest = ok && tp_path(path, root, "jobs.tsv") ? fopen(path, "rb") : NULL;
    ok = ok && manifest != NULL;
    if (manifest)
    {
        char line[256], extra;
        for (unsigned i = 0; i < jobs && ok; ++i)
        {
            unsigned number;
            ok = fgets(line, sizeof(line), manifest) && sscanf(line, "%u %127s %c", &number, names[i], &extra) == 2 && number == i;
        }
        if (fgetc(manifest) != EOF) ok = 0;
        fclose(manifest);
    }
    FILE* json = ok && tp_path(path, root, tp_summary_names[0]) ? fopen(path, "wb") : NULL;
    FILE* markdown = ok && tp_path(path, root, tp_summary_names[1]) ? fopen(path, "wb") : NULL;
    ok = ok && json && markdown;
    int regressions = 0, inconclusive = 0;
    if (ok)
    {
        double alpha = 0.01 / (double)(jobs * 2);
        fprintf(json, "{\"schema\":%d,\"guard_enabled\":%s,\"family_alpha\":0.01,\"per_test_alpha\":%.17g,\"comparisons\":[",
                TP_SCHEMA, guard ? "true" : "false", alpha);
        fputs("# Compiler throughput comparison\n\nSame-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).\n\n", markdown);
        fputs("| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |\n|---|---:|---:|---:|---:|---:|---|\n", markdown);
        static char const* const metric_names[15] = {"wall_seconds", "cpu_seconds", "peak_rss_bytes", "cycles", "instructions", "branches",
            "branch_misses", "cache_references", "cache_misses", "arena_calls", "arena_bytes", "output_bytes", "lines_per_second", "functions_per_second", "bytes_per_second"};
        for (unsigned job = 0; job < jobs && ok; ++job)
        {
            /* The same variant must do the same work and emit the same bytes
             * throughout the run. A/B output equality is a separate option. */
            for (unsigned variant = 0; variant < 2 && ok; ++variant)
            {
                TpRow const* first = rows + tp_row_index(job, 0, variant, 0, pairs);
                for (unsigned round = 0; round < TP_ROUNDS && ok; ++round)
                {
                    for (unsigned pair = 0; pair < pairs && ok; ++pair)
                    {
                        TpRow const* row = rows + tp_row_index(job, round, variant, pair, pairs);
                        ok = row->present && row->output_bytes == first->output_bytes && !strcmp(row->output_hash, first->output_hash) &&
                             row->source_bytes == first->source_bytes && row->source_lines == first->source_lines && row->source_functions == first->source_functions;
                    }
                }
            }
            TpRow const* first_a = rows + tp_row_index(job, 0, 0, 0, pairs);
            TpRow const* first_b = rows + tp_row_index(job, 0, 1, 0, pairs);
            ok = ok && first_a->source_bytes == first_b->source_bytes && first_a->source_lines == first_b->source_lines && first_a->source_functions == first_b->source_functions;
            if (!ok)
            {
                tp_error("workload %s changed work or emitted nondeterministic output; comparison refused", names[job]);
                break;
            }
            if (job) fputc(',', json);
            fputs("{\"name\":", json); tp_json_string(json, names[job]);
            fputs(",\"medians\":{", json);
            double medians[2][15];
            for (unsigned metric = 0; metric < 15; ++metric)
            {
                if (metric) fputc(',', json);
                tp_json_string(json, metric_names[metric]); fputs(":[", json);
                for (unsigned variant = 0; variant < 2; ++variant)
                {
                    double values[TP_MAX_PAIRS * TP_ROUNDS];
                    unsigned count = 0;
                    for (unsigned round = 0; round < TP_ROUNDS; ++round)
                    {
                        for (unsigned pair = 0; pair < pairs; ++pair)
                        {
                            double value = tp_metric(rows + tp_row_index(job, round, variant, pair, pairs), metric);
                            if (isfinite(value)) values[count++] = value;
                        }
                    }
                    tp_sort(values, count);
                    /* Partial optional telemetry is not treated as a complete
                     * series. A zero event count remains a real zero. */
                    medians[variant][metric] = count == pairs * TP_ROUNDS ? tp_median_sorted(values, count) : NAN;
                    if (variant) fputc(',', json);
                    tp_json_number(json, medians[variant][metric]);
                }
                fputc(']', json);
            }
            fputs("},\"tests\":[", json);
            int confirmed = 0, uncertain = 0;
            double wall_ratio = 1.0;
            for (unsigned gate_metric = 0; gate_metric < 2; ++gate_metric)
            {
                unsigned metric = gate_metric ? 2 : 0;
                unsigned round_regressions = 0;
                for (unsigned round = 0; round < TP_ROUNDS; ++round)
                {
                    double a[TP_MAX_PAIRS], b[TP_MAX_PAIRS];
                    for (unsigned pair = 0; pair < pairs; ++pair)
                    {
                        a[pair] = tp_metric(rows + tp_row_index(job, round, 0, pair, pairs), metric);
                        b[pair] = tp_metric(rows + tp_row_index(job, round, 1, pair, pairs), metric);
                    }
                    TpAssessment assessment = tp_assess(a, b, pairs, gate_metric ? 0.20 : 0.15, gate_metric ? 16777216.0 : 0.002, alpha);
                    ok = ok && assessment.valid;
                    round_regressions += assessment.regression != 0;
                    if (!gate_metric) wall_ratio *= sqrt(assessment.ratio_median);
                    if (assessment.ratio_high > (gate_metric ? 1.20 : 1.15) && !assessment.regression) uncertain = 1;
                    if (gate_metric || round) fputc(',', json);
                    fprintf(json, "{\"metric\":\"%s\",\"round\":%u,\"median_ratio\":%.17g,\"ci_low\":", metric_names[metric], round, assessment.ratio_median);
                    tp_json_number(json, assessment.ratio_low);
                    fputs(",\"ci_high\":", json); tp_json_number(json, assessment.ratio_high);
                    fprintf(json, ",\"baseline_relative_mad\":%.17g,\"candidate_relative_mad\":%.17g,\"margin_exceedances\":%u,\"pairs\":%u,\"p_value\":%.17g,\"regression\":%s}",
                            assessment.baseline_relative_mad, assessment.candidate_relative_mad, assessment.wins, pairs, assessment.p_value,
                            assessment.regression ? "true" : "false");
                }
                if (round_regressions == TP_ROUNDS) confirmed = 1;
                else if (round_regressions) uncertain = 1;
            }
            char const* decision = confirmed ? "regression" : uncertain ? "inconclusive" : "no substantial regression detected";
            if (!guard) decision = "diagnostic (guard disabled)";
            regressions += guard && confirmed;
            inconclusive += guard && !confirmed && uncertain;
            fputs("],\"decision\":", json); tp_json_string(json, decision); fputc('}', json);
            fprintf(markdown, "| %s | %.3f | %.3f | %+.2f%% | %.2f / %.2f | %.0f | %s |\n", names[job],
                    medians[0][0] * 1000.0, medians[1][0] * 1000.0, (wall_ratio - 1.0) * 100.0,
                    medians[0][2] / 1048576.0, medians[1][2] / 1048576.0, medians[1][12], decision);
        }
        fputs("],\"telemetry\":[", json);
        unsigned emitted = 0;
        for (unsigned job = 0; job < jobs && ok; ++job)
        {
            for (unsigned kind = 0; kind < 2; ++kind)
            {
                if (!probes[tp_row_index(job, kind, 0, 0, 3)].present) continue;
                if (emitted++) fputc(',', json);
                fputs("{\"job\":", json); tp_json_string(json, names[job]);
                fprintf(json, ",\"kind\":\"%s\",\"repeats\":3,\"diagnostic_only\":true,\"metrics\":{", kind ? "allocations" : "pmu");
                unsigned begin = kind ? 9 : 3, end = kind ? 11 : 9;
                for (unsigned metric = begin; metric < end; ++metric)
                {
                    if (metric != begin) fputc(',', json);
                    tp_json_string(json, metric_names[metric]); fputs(":[", json);
                    for (unsigned variant = 0; variant < 2; ++variant)
                    {
                        if (variant) fputc(',', json);
                        double values[3]; unsigned count = 0;
                        for (unsigned repeat = 0; repeat < 3; ++repeat)
                        {
                            TpRow const* row = probes + tp_row_index(job, kind, variant, repeat, 3);
                            TpRow const* reference = rows + tp_row_index(job, 0, variant, 0, pairs);
                            ok = ok && !strcmp(row->output_hash, reference->output_hash) &&
                                 row->source_bytes == reference->source_bytes && row->source_lines == reference->source_lines;
                            double value = tp_metric(row, metric);
                            if (isfinite(value)) values[count++] = value;
                        }
                        if (kind && count != 3) ok = 0;
                        fprintf(json, "{\"available_samples\":%u,\"median\":", count);
                        if (count) { tp_sort(values, count); tp_json_number(json, tp_median_sorted(values, count)); }
                        else fputs("null", json);
                        fputc('}', json);
                    }
                    fputc(']', json);
                }
                fputs("}}", json);
            }
        }
        fprintf(json, "],\"confirmed_regressions\":%d,\"inconclusive_cases\":%d,\"valid\":%s}\n", regressions, inconclusive, ok ? "true" : "false");
        fprintf(markdown, "\nConfirmed regressions: **%d**. Inconclusive cases: **%d**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.\n\nHardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.\n", regressions, inconclusive);
        ok = ok && !ferror(json) && !ferror(markdown);
    }
    if (json && fclose(json) != 0) ok = 0;
    if (markdown && fclose(markdown) != 0) ok = 0;
    free(rows);
    free(probes);
    /* Work/output and stream errors can be discovered after reports open.
     * Close both streams before discarding their incomplete verdicts. */
    if (!ok) (void)tp_remove_summaries(root);
    int result = !ok ? 2 : regressions ? 1 : 0;
    if (!ok) tp_error("incomplete or incompatible result bundle; no performance verdict");
    else
    {
        fprintf(stdout, "THROUGHPUT_RESULT confirmed_regressions=%d inconclusive=%d guard=%u exit=%d\n", regressions, inconclusive, guard, result);
        if (getenv("GITHUB_ACTIONS") && inconclusive)
            fprintf(stdout, "::warning title=Compiler throughput inconclusive::%d cases have insufficient evidence; inspect summary.md. No equivalence claim or automatic retry.\n", inconclusive);
        if (getenv("GITHUB_ACTIONS") && regressions)
            fprintf(stdout, "::error title=Compiler throughput regression::%d cases exceeded the practical margin with confirmation in both rounds. See summary.md and raw samples.\n", regressions);
    }
    return result;
}

#include "tree.h"

static int tp_mkdirs(char const* path)
{
    char copy[TP_PATH_CAP];
    int ok = strlen(path) < sizeof(copy);
    if (ok)
    {
        strcpy(copy, path);
        for (size_t i = 1; copy[i] && ok; ++i)
        {
            if (copy[i] == '/' || copy[i] == '\\')
            {
#ifdef _WIN32
                if (i == 2 && copy[1] == ':') continue;
#endif
                char delimiter = copy[i];
                copy[i] = 0;
                ok = tp_mkdir(copy);
                copy[i] = delimiter;
            }
        }
        ok = ok && tp_mkdir(copy);
    }
    return ok;
}

static int tp_self_host_hash(TpConfig const* config, char digest[65])
{
    char src[TP_PATH_CAP], source_hash[65], generated_hash[65];
    int ok = tp_path(src, config->self_host_root, "src") && tp_hash_tree(src, source_hash) && tp_hash_tree(config->self_host_generated, generated_hash);
    if (ok)
    {
        TpHash hash;
        tp_hash_init(&hash);
        tp_hash_add(&hash, source_hash, 65);
        tp_hash_add(&hash, generated_hash, 65);
        tp_hash_finish(&hash, digest);
    }
    return ok;
}

static int tp_run(TpConfig config)
{
    char root[TP_PATH_CAP], directory[TP_PATH_CAP], baseline[TP_PATH_CAP], candidate[TP_PATH_CAP];
    char input_dir[TP_PATH_CAP], output_dir[TP_PATH_CAP], path[TP_PATH_CAP];
    char self_root[TP_PATH_CAP], self_generated[TP_PATH_CAP];
    char allocation_compilers[2][TP_PATH_CAP];
    int ok = config.baseline && config.candidate && tp_absolute(config.output, root) && tp_absolute(".", directory) &&
             tp_absolute(config.baseline, baseline) && tp_absolute(config.candidate, candidate) &&
             tp_path(input_dir, root, "inputs") && tp_path(output_dir, root, "artifacts");
    if (ok && config.self_host_root)
    {
        ok = tp_absolute(config.self_host_root, self_root) && tp_absolute(config.self_host_generated, self_generated);
        config.self_host_root = self_root;
        config.self_host_generated = self_generated;
    }
    if (ok && config.allocation_baseline)
    {
        ok = tp_absolute(config.allocation_baseline, allocation_compilers[0]) && tp_absolute(config.allocation_candidate, allocation_compilers[1]);
    }
    if (ok)
    {
        struct stat exists;
        ok = tp_path(path, root, "metadata.json") && stat(path, &exists) != 0 && errno == ENOENT;
        if (!ok) tp_error("result directory already contains a run; choose a new --output directory");
    }
    ok = ok && tp_mkdirs(input_dir) && tp_mkdirs(output_dir);
    TpWorkload workloads[TP_CASES];
    if (ok) ok = tp_generate(&config, input_dir, workloads);
    TpJob jobs[TP_MAX_JOBS];
    unsigned job_count = 0;
    if (ok)
    {
        for (unsigned i = 0; i < TP_CASES; ++i)
        {
            for (unsigned mode = 0; mode < 4; ++mode)
            {
                if (config.mode_mask & (1u << mode)) jobs[job_count++] = (TpJob){workloads[i], mode, 0};
            }
        }
    }
    char frozen_hash[65] = {0};
    if (ok && config.self_host_root)
    {
        ok = tp_self_host_hash(&config, frozen_hash);
        if (ok)
        {
            for (unsigned stage = 1; stage <= 2; ++stage)
            {
                TpJob* job = jobs + job_count++;
                memset(job, 0, sizeof(*job));
                job->mode = 2;
                job->stage = stage;
                snprintf(job->workload.name, sizeof(job->workload.name), "self_host_stage%u", stage);
                ok = ok && tp_path(job->workload.path, self_root, "src/buster/apps/ide/ide.c");
                memcpy(job->workload.hash, frozen_hash, sizeof(frozen_hash));
            }
        }
        if (!ok) tp_error("cannot freeze self-host input trees (regular files/directories only)");
    }
    char compiler_hashes[4][65];
    char const* immutable_compilers[4] = {baseline, candidate, allocation_compilers[0], allocation_compilers[1]};
    unsigned immutable_count = config.allocation_baseline ? 4 : 2;
    for (unsigned i = 0; i < immutable_count && ok; ++i)
    {
        uint64_t bytes, lines;
        ok = tp_hash_file(immutable_compilers[i], compiler_hashes[i], &bytes, &lines);
    }
    if (ok) ok = tp_metadata(&config, root, baseline, candidate, jobs, job_count);
    FILE* manifest = ok && tp_path(path, root, "jobs.tsv") ? fopen(path, "wb") : NULL;
    ok = ok && manifest != NULL;
    if (manifest)
    {
        for (unsigned i = 0; i < job_count; ++i) fprintf(manifest, "%u\t%s/%s\n", i, jobs[i].workload.name, tp_modes[jobs[i].mode]);
        if (fclose(manifest) != 0) ok = 0;
    }
    FILE* samples = ok && tp_path(path, root, "samples.csv") ? fopen(path, "wb") : NULL;
    FILE* commands = ok && tp_path(path, root, "commands.jsonl") ? fopen(path, "wb") : NULL;
    FILE* capabilities = ok && tp_path(path, root, "capabilities.jsonl") ? fopen(path, "wb") : NULL;
    FILE* telemetry = ok && tp_path(path, root, "telemetry.csv") ? fopen(path, "wb") : NULL;
    ok = ok && samples && commands && capabilities && telemetry;
    if (samples) fputs(TP_RAW_HEADER, samples);
    if (telemetry) fputs(TP_RAW_HEADER, telemetry);
    TpRow first[TP_MAX_JOBS][2];
    memset(first, 0, sizeof(first));
    char stage1[2][TP_PATH_CAP], stage2[2][TP_PATH_CAP];
    for (unsigned variant = 0; variant < 2; ++variant)
    {
        char leaf[128];
        snprintf(leaf, sizeof(leaf), "self_host_stage1-fast-%u.exe", variant);
        if (ok) ok = tp_path(stage1[variant], output_dir, leaf);
        snprintf(leaf, sizeof(leaf), "self_host_stage2-fast-%u.exe", variant);
        if (ok) ok = tp_path(stage2[variant], output_dir, leaf);
    }
    char const* compilers[2] = {baseline, candidate};
    /* Warmups initialize the file cache but never reuse a compiler process.
     * The ordered preparation also materializes stage1 before stage2 uses it. */
    for (unsigned job = 0; job < job_count && ok; ++job)
    {
        for (unsigned warmup = 0; warmup < config.warmups && ok; ++warmup)
        {
            for (unsigned variant = 0; variant < 2 && ok; ++variant)
            {
                TpRow row;
                char sample_id[128];
                snprintf(sample_id, sizeof(sample_id), "warmup-j%u-w%u-v%u", job, warmup, variant);
                char const* compiler = jobs[job].stage == 2 ? stage1[variant] : compilers[variant];
                ok = tp_measure(&config, jobs + job, compiler, jobs[job].stage ? self_root : directory, output_dir, variant,
                                sample_id, 0, &row, commands, capabilities);
                if (ok && isfinite(row.arena_calls))
                {
                    tp_error("timing compiler is allocation-instrumented; supply it as --allocation-baseline/candidate instead");
                    ok = 0;
                }
                if (ok && first[job][variant].present)
                {
                    ok = !strcmp(first[job][variant].output_hash, row.output_hash);
                    if (!ok) tp_error("nondeterministic warmup output in job %u variant %u", job, variant);
                }
                else if (ok)
                {
                    first[job][variant] = row;
                }
            }
        }
        if (ok && config.identical && strcmp(first[job][0].output_hash, first[job][1].output_hash))
        {
            tp_error("baseline/candidate output differs for %s/%s", jobs[job].workload.name, tp_modes[jobs[job].mode]);
            ok = 0;
        }
    }
    uint32_t random = config.seed;
    unsigned block_first[TP_MAX_JOBS] = {0};
    for (unsigned round = 0; round < TP_ROUNDS && ok; ++round)
    {
        for (unsigned pair = 0; pair < config.pairs && ok; ++pair)
        {
            unsigned order[TP_MAX_JOBS];
            for (unsigned j = 0; j < job_count; ++j)
            {
                order[j] = j;
                if (!(pair & 1)) block_first[j] = tp_random(&random) & 1u;
            }
            for (unsigned remaining = job_count; remaining > 1; --remaining)
            {
                unsigned swap = tp_random(&random) % remaining;
                unsigned temp = order[remaining - 1];
                order[remaining - 1] = order[swap];
                order[swap] = temp;
            }
            for (unsigned slot = 0; slot < job_count && ok; ++slot)
            {
                unsigned job = order[slot];
                unsigned first_variant = block_first[job] ^ (pair & 1u);
                for (unsigned position = 0; position < 2 && ok; ++position)
                {
                    unsigned variant = first_variant ^ position;
                    TpRow row;
                    char sample_id[128];
                    snprintf(sample_id, sizeof(sample_id), "timing-r%u-p%u-j%u-v%u", round, pair, job, variant);
                    char const* compiler = jobs[job].stage == 2 ? stage1[variant] : compilers[variant];
                    ok = tp_measure(&config, jobs + job, compiler, jobs[job].stage ? self_root : directory, output_dir, variant,
                                    sample_id, 0, &row, commands, capabilities);
                    if (ok && strcmp(first[job][variant].output_hash, row.output_hash))
                    {
                        tp_error("nondeterministic artifact in %s", sample_id);
                        ok = 0;
                    }
                    if (ok) ok = tp_sample_csv(samples, round, pair, position, variant, job, &row);
                }
            }
            fprintf(stdout, "THROUGHPUT_PROGRESS round=%u/%u pair=%u/%u valid=%d\n", round + 1, TP_ROUNDS, pair + 1, config.pairs, ok);
            fflush(stdout);
        }
    }
    /* PMU and allocation perturbations are deliberately NOT timing samples.
     * Three deterministic replays preserve per-invocation counters and output
     * identity; replay type is visible in commands/capabilities sample names.
     * CSV round=0 is PMU; round=1 is an allocation-instrumented compiler.
     */
    char allocation_stage1[2][TP_PATH_CAP];
    if (ok && config.allocation_baseline && config.self_host_root)
    {
        TpConfig instrumented = config;
        if (instrumented.flag_count == TP_MAX_FLAGS) ok = 0;
        else instrumented.flags[instrumented.flag_count++] = "-DBUSTER_BENCH_ALLOCATIONS=1";
        TpJob job = jobs[job_count - 2];
        snprintf(job.workload.name, sizeof(job.workload.name), "self_host_allocation_compiler");
        for (unsigned variant = 0; variant < 2 && ok; ++variant)
        {
            TpRow row;
            char sample_id[128], leaf[128];
            snprintf(sample_id, sizeof(sample_id), "allocation-stage-prepare-v%u", variant);
            ok = tp_measure(&instrumented, &job, compilers[variant], self_root, output_dir, variant, sample_id, 0, &row, commands, capabilities);
            snprintf(leaf, sizeof(leaf), "self_host_allocation_compiler-fast-%u.exe", variant);
            ok = ok && tp_path(allocation_stage1[variant], output_dir, leaf);
        }
    }
    for (unsigned kind = 0; kind < 2 && ok; ++kind)
    {
        if ((!kind && !config.pmu) || (kind && !config.allocation_baseline)) continue;
        for (unsigned job = 0; job < job_count && ok; ++job)
        {
            for (unsigned repeat = 0; repeat < 3 && ok; ++repeat)
            {
                for (unsigned position = 0; position < 2 && ok; ++position)
                {
                    unsigned variant = position ^ (repeat & 1u);
                    TpRow row;
                    char sample_id[128];
                    snprintf(sample_id, sizeof(sample_id), "%s-j%u-p%u-v%u", kind ? "allocations" : "pmu", job, repeat, variant);
                    char const* compiler = kind ? (jobs[job].stage == 2 ? allocation_stage1[variant] : allocation_compilers[variant]) :
                                           jobs[job].stage == 2 ? stage1[variant] : compilers[variant];
                    ok = tp_measure(&config, jobs + job, compiler, jobs[job].stage ? self_root : directory, output_dir, variant,
                                    sample_id, !kind, &row, commands, capabilities);
                    if (ok && kind && (!isfinite(row.arena_calls) || !isfinite(row.arena_bytes)))
                    {
                        tp_error("allocation probe compiler did not emit allocation.arena_calls/arena_bytes: %s", compiler);
                        ok = 0;
                    }
                    if (ok && strcmp(first[job][variant].output_hash, row.output_hash))
                    {
                        tp_error("telemetry probe changed the artifact: %s", sample_id);
                        ok = 0;
                    }
                    if (ok) ok = tp_sample_csv(telemetry, kind, repeat, position, variant, job, &row);
                }
            }
        }
    }
    /* Two generations compiled from a FROZEN tree need not equal a compiler
     * built from a different source revision. The meaningful fixed point is
     * stage2 == stage3, after both producers implement the frozen source. The
     * project's normal test_self_host remains the canonical own-source gate. */
    for (unsigned variant = 0; variant < 2 && ok && config.self_host_root; ++variant)
    {
        TpJob job = jobs[job_count - 1];
        snprintf(job.workload.name, sizeof(job.workload.name), "self_host_stage3");
        job.stage = 3;
        TpRow row;
        char sample_id[128];
        snprintf(sample_id, sizeof(sample_id), "fixed-point-v%u", variant);
        ok = tp_measure(&config, &job, stage2[variant], self_root, output_dir, variant, sample_id, 0, &row, commands, capabilities);
        if (ok && strcmp(first[job_count - 1][variant].output_hash, row.output_hash))
        {
            tp_error("frozen-source stage2/stage3 fixed point differs for variant %u", variant);
            ok = 0;
        }
    }
    if (ok && config.self_host_root)
    {
        char after[65];
        ok = tp_self_host_hash(&config, after) && !strcmp(frozen_hash, after);
        if (!ok) tp_error("frozen self-host source or generated headers changed during the run");
    }
    if (ok)
    {
        for (unsigned i = 0; i < immutable_count && ok; ++i)
        {
            uint64_t bytes, lines;
            char hash[65];
            ok = tp_hash_file(immutable_compilers[i], hash, &bytes, &lines) && !strcmp(hash, compiler_hashes[i]);
            if (!ok) tp_error("compiler changed during measurement: %s", immutable_compilers[i]);
        }
        /* Generated inputs must also remain immutable. */
        for (unsigned job = 0; job < job_count && ok; ++job)
        {
            if (!jobs[job].stage)
            {
                uint64_t bytes, lines;
                char hash[65];
                ok = tp_hash_file(jobs[job].workload.path, hash, &bytes, &lines) && !strcmp(hash, jobs[job].workload.hash);
            }
        }
    }
    if (samples && fclose(samples) != 0) ok = 0;
    if (commands && fclose(commands) != 0) ok = 0;
    if (capabilities && fclose(capabilities) != 0) ok = 0;
    if (telemetry && fclose(telemetry) != 0) ok = 0;
    if (ok) ok = tp_completion(root, job_count, config.pairs, config.guard, 1);
    int result = ok ? tp_compare(root) : 2;
    if (!ok) tp_error("run incomplete; partial logs retained; no performance verdict");
    return result;
}

static int tp_self_test(void)
{
    unsigned assertions = 0, failures = 0;
#define TP_TEST(condition) do { ++assertions; if (!(condition)) { ++failures; fprintf(stderr, "SELF_TEST failure line %d: %s\n", __LINE__, #condition); } } while (0)
    TpHash hash;
    char digest[65];
    tp_hash_init(&hash);
    tp_hash_finish(&hash, digest);
    TP_TEST(!strcmp(digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    tp_hash_init(&hash);
    tp_hash_add(&hash, "a", 1);
    tp_hash_add(&hash, "bc", 2);
    tp_hash_finish(&hash, digest);
    TP_TEST(!strcmp(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    tp_hash_init(&hash);
    for (unsigned i = 0; i < 1000000; ++i) tp_hash_add(&hash, "a", 1);
    tp_hash_finish(&hash, digest);
    TP_TEST(!strcmp(digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    TP_TEST(fabs(tp_sign_tail(20, 20) - 1.0 / 1048576.0) < 1e-15);
    TP_TEST(fabs(tp_sign_tail(20, 18) - 211.0 / 1048576.0) < 1e-15);
    TP_TEST(fabs(tp_sign_tail(20, 0) - 1.0) < 1e-15);
    double a[TP_MAX_PAIRS], b[TP_MAX_PAIRS];
    for (unsigned i = 0; i < TP_MAX_PAIRS; ++i) { a[i] = 1.0; b[i] = 1.0; }
    TpAssessment assessment = tp_assess(a, b, 20, 0.15, 0.002, 0.01 / 48);
    TP_TEST(assessment.valid && !assessment.regression && assessment.ratio_median == 1.0);
    TP_TEST(assessment.ratio_low == 1.0 && assessment.ratio_high == 1.0);
    for (unsigned i = 0; i < 20; ++i) b[i] = 1.30;
    assessment = tp_assess(a, b, 20, 0.15, 0.002, 0.01 / 48);
    TP_TEST(assessment.valid && assessment.regression);
    for (unsigned i = 0; i < 20; ++i) b[i] = 1.05;
    assessment = tp_assess(a, b, 20, 0.15, 0.002, 0.01 / 48);
    TP_TEST(assessment.valid && !assessment.regression);
    b[0] = 1000.0;
    assessment = tp_assess(a, b, 20, 0.15, 0.002, 0.01 / 48);
    TP_TEST(assessment.valid && !assessment.regression);
    for (unsigned i = 0; i < 20; ++i) { a[i] = 0.001; b[i] = 0.002; }
    assessment = tp_assess(a, b, 20, 0.15, 0.002, 0.01 / 48);
    TP_TEST(assessment.valid && !assessment.regression);
    for (unsigned i = 0; i < 20; ++i) { a[i] = 1.0 + (double)i * 0.1; b[i] = a[i] * (i & 1 ? 1.5 : 0.8); }
    assessment = tp_assess(a, b, 20, 0.15, 0.002, 0.01 / 48);
    TP_TEST(assessment.valid && !assessment.regression);
    b[0] = NAN;
    TP_TEST(!tp_assess(a, b, 20, 0.15, 0.002, 0.01).valid);
    TP_TEST(!tp_assess(a, b, 0, 0.15, 0.002, 0.01).valid);
    TP_TEST(!tp_assess(a, b, TP_MAX_PAIRS + 1, 0.15, 0.002, 0.01).valid);
    unsigned n;
    TP_TEST(tp_number("123", &n) && n == 123);
    TP_TEST(!tp_number("-1", &n));
    TP_TEST(!tp_number("0x1", &n));
    TP_TEST(!tp_number("4294967296", &n));
    double parsed;
    TP_TEST(tp_parse_double("NA", &parsed, 1) && isnan(parsed));
    TP_TEST(!tp_parse_double("nan", &parsed, 1));
    TP_TEST(!tp_parse_double("inf", &parsed, 1));
    TP_TEST(!tp_parse_double("-1", &parsed, 0));
    TP_TEST(!tp_parse_double("1x", &parsed, 0));
    TP_TEST(!tp_parse_double("NA", &parsed, 0));
    TP_TEST(tp_parse_double("0", &parsed, 1) && parsed == 0);
    uint64_t integer;
    TP_TEST(!tp_parse_u64("18446744073709551616", &integer));
    TP_TEST(!tp_parse_u64("-1", &integer));
    TP_TEST(tp_parse_u64("18446744073709551615", &integer) && integer == UINT64_MAX);
    fprintf(stdout, "THROUGHPUT_SELF_TEST assertions=%u failures=%u\n", assertions, failures);
#undef TP_TEST
    return failures ? 1 : 0;
}

static void tp_help(void)
{
    fputs("Compiler throughput (native C; schema 1)\n\n"
          "  throughput generate --output DIR [--profile smoke|ci|full] [--seed N] [--scale N]\n"
          "  throughput run --baseline IDE --candidate IDE --output NEW_DIR [options]\n"
          "  throughput compare --output RESULT_DIR\n"
          "  throughput self-test\n\n"
          "Options: --pairs N (20+ for guard; two rounds), --warmups N, --mode all|none|mir-stack|fast|quality,\n"
          "--timeout SECONDS, --cpu N|auto, --flag ARG (repeatable), --baseline-id LABEL, --candidate-id LABEL,\n"
          "--pmu (separate replays), --require-pmu, --allocation-baseline IDE --allocation-candidate IDE,\n"
          "--self-host-root FROZEN_TREE --self-host-generated GENERATED_DIR, --require-identical-output,\n"
          "--no-guard (explicit diagnostic/smoke mode; no performance pass claimed).\n\n"
          "Exit: 0 no confirmed regression (inspect inconclusive warnings), 1 confirmed regression,\n"
          "2 invalid/incomplete run. Never compare unrelated hosts or reuse an old result directory.\n", stdout);
}

int main(int argc, char** argv)
{
    TpConfig config;
    int result = 2;
    if (tp_options(argc, argv, &config))
    {
        if (!strcmp(config.command, "help") || !strcmp(config.command, "--help"))
        {
            tp_help();
            result = 0;
        }
        else if (!strcmp(config.command, "self-test"))
        {
            result = tp_self_test();
        }
        else if (!strcmp(config.command, "generate"))
        {
            char root[TP_PATH_CAP];
            TpWorkload workloads[TP_CASES];
            int ok = tp_absolute(config.output, root) && tp_mkdirs(root) && tp_generate(&config, root, workloads);
            result = ok ? 0 : 2;
            if (ok)
            {
                for (unsigned i = 0; i < TP_CASES; ++i) printf("%s bytes=%" PRIu64 " lines=%" PRIu64 " functions=%" PRIu64 " sha256=%s\n",
                        workloads[i].name, workloads[i].bytes, workloads[i].lines, workloads[i].functions, workloads[i].hash);
            }
        }
        else if (!strcmp(config.command, "run"))
        {
            result = tp_run(config);
        }
        else if (!strcmp(config.command, "compare"))
        {
            result = tp_compare(config.output);
        }
        else
        {
            tp_error("unknown command: %s", config.command);
        }
    }
    return result;
}
