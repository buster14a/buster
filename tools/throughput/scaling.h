/* Native multi-TU compile-and-link scaling on explicit, topology-checked CPU sets.
 * Ownership: the `scale` subcommand. It generates fixed-work multi-input
 * workloads outside timing, places each worker count on whole physical cores,
 * runs fresh compiler processes, and reports speedup next to CPU-work and
 * memory inflation. It is report-only: no threshold or CI guard.
 * Map: tp_scale_generate (shapes), tp_scale_place (CPU selection),
 * tp_scale_sample (one process + -fmetrics-out worker check),
 * tp_scale_diagnostics (ordered-failure determinism), tp_scale_summary
 * (scaling.json/scaling.md), tp_scale (orchestration, exit 0 valid/2 invalid).
 * Internal TU lanes only: concurrent independent compiler processes are a
 * separate experiment. Peak RSS is the compiler process's own high-water mark.
 */
#ifndef BUSTER_THROUGHPUT_SCALING_H
#define BUSTER_THROUGHPUT_SCALING_H

#define TP_SCALE_SCHEMA "buster-throughput-scaling-v1"
#define TP_SCALE_MAX_WORKERS 64
#define TP_SCALE_MAX_REPEATS 64
#define TP_SCALE_DEFAULT_REPEATS 10
/* Equal, skewed and tiny series keep these input counts for EVERY worker
 * count of a run: strong scaling compares fixed total work. */
#define TP_SCALE_INPUTS_PER_WORKER 4
#define TP_SCALE_TINY_INPUTS_PER_WORKER 16
#define TP_SCALE_EQUAL_UNITS 4
#define TP_SCALE_SKEWED_LARGE_UNITS 8
#define TP_SCALE_ALPHA 0.05
#define TP_SCALE_RSS_SCOPE "compiler process high-water mark (wait4 ru_maxrss / PeakWorkingSetSize); not a process-tree sum"
#define TP_SCALE_CSV_HEADER "series,inputs,workers,placement,cpus,iteration,order,wall_seconds,user_seconds,system_seconds,peak_rss_bytes,compilation_workers,intervals,artifact_bytes,artifact_sha256\n"

typedef enum TpScaleShape
{
    TP_SCALE_EQUAL,
    TP_SCALE_SKEWED,
    TP_SCALE_TINY,
    TP_SCALE_COUNT,
    TP_SCALE_SHAPES,
    TP_SCALE_DIAGNOSTIC = TP_SCALE_SHAPES
} TpScaleShape;

static char const* const tp_scale_shape_names[TP_SCALE_SHAPES + 1] = {"equal", "skewed", "tiny", "count", "diagnostic"};

typedef struct TpScalePoint
{
    TpCpuSet cpus;
    unsigned series, workers, expected_workers, observed_workers, samples;
    int smt;
    double wall[TP_SCALE_MAX_REPEATS], cpu[TP_SCALE_MAX_REPEATS], rss[TP_SCALE_MAX_REPEATS];
    char log_hash[65];
    int exit_code;
} TpScalePoint;

typedef struct TpScaleSeries
{
    char name[64];
    char directory[TP_PATH_CAP];
    char hash[65];
    char artifact_hash[65];
    uint64_t bytes, artifact_bytes;
    unsigned shape, inputs, first_point, point_count;
} TpScaleSeries;

typedef struct TpScaleMetrics
{
    unsigned inputs, ok, workers;
    char intervals[16];
} TpScaleMetrics;

typedef struct TpScaleRun
{
    TpConfig const* config;
    TpCpuSet requested;
    TpTopology* topology;
    TpScaleSeries* series;
    TpScalePoint* points;
    unsigned series_count, point_count, max_workers;
    char root[TP_PATH_CAP], samples[TP_PATH_CAP], compiler[TP_PATH_CAP], compiler_hash[65];
    char cpu_text[TP_CPU_SET_TEXT_CAP], excluded_text[TP_CPU_SET_TEXT_CAP], reason[512];
    FILE* csv;
    FILE* commands;
} TpScaleRun;

static void tp_scale_invalid(TpScaleRun* run, char const* format, ...)
{
    if (!run->reason[0])
    {
        va_list args;
        va_start(args, format);
        vsnprintf(run->reason, sizeof(run->reason), format, args);
        va_end(args);
        tp_error("scale invalid: %s", run->reason);
    }
}

/* Worker counts reuse the cpulist grammar: distinct, ascending, no zero. */
static int tp_scale_workers_parse(char const* text, TpCpuSet* workers)
{
    int ok = tp_cpu_set_parse(text, workers) && !tp_cpu_set_has(workers, 0);
    for (unsigned w = TP_SCALE_MAX_WORKERS + 1; w < TP_MAX_CPUS && ok; ++w) ok = !tp_cpu_set_has(workers, w);
    return ok;
}

static void tp_scale_function(FILE* file, unsigned series, unsigned tu, unsigned index, int tiny, uint32_t* random)
{
    if (tiny)
    {
        fprintf(file, "unsigned s%02u_t%04u_f%05u(unsigned x)\n{\n    return x + %uu;\n}\n", series, tu, index, tp_random(random));
    }
    else
    {
        fprintf(file, "unsigned s%02u_t%04u_f%05u(unsigned x, unsigned y)\n{\n", series, tu, index);
        for (unsigned i = 0; i < 8; ++i)
        {
            uint32_t mask = tp_random(random), add = tp_random(random);
            fprintf(file, "    if ((x ^ y) & %uu) { x += y * 33u; } else { y ^= x + %uu; }\n", mask, add);
            fputs("    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y >> 3; break; }\n", file);
        }
        fputs("    return x ^ y;\n}\n", file);
    }
}

/* Deterministic C sources. Every function name is unique to its series and TU,
 * and only TU 0 defines main, so every input set links as one program. The
 * diagnostic series puts two undeclared identifiers in different TUs. */
static int tp_scale_write_inputs(TpScaleRun* run, TpScaleSeries* series, unsigned index)
{
    TpConfig const* config = run->config;
    unsigned unit = (!strcmp(config->profile, "smoke") ? 1u : !strcmp(config->profile, "ci") ? 8u : 64u) * config->scale;
    char manifest[TP_PATH_CAP];
    int ok = tp_mkdirs(series->directory) && tp_path(manifest, series->directory, "inputs.tsv");
    FILE* list = ok ? fopen(manifest, "wb") : NULL;
    ok = list != NULL;
    for (unsigned tu = 0; tu < series->inputs && ok; ++tu)
    {
        char leaf[32], path[TP_PATH_CAP], hash[65];
        uint64_t bytes = 0, lines = 0;
        snprintf(leaf, sizeof(leaf), "tu%04u.c", tu);
        ok = tp_path(path, series->directory, leaf);
        FILE* file = ok ? fopen(path, "wb") : NULL;
        ok = file != NULL;
        if (file)
        {
            uint32_t random = config->seed ^ ((index * 4099u + tu) * UINT32_C(2654435761));
            if (!random) random = 1;
            fprintf(file, "/* buster-throughput-scaling schema=1 seed=%u profile=%s scale=%u series=%s tu=%u */\n",
                    config->seed, config->profile, config->scale, series->name, tu);
            int tiny = series->shape == TP_SCALE_TINY;
            unsigned units = tiny ? 1u : series->shape == TP_SCALE_SKEWED ? (tu == 0 ? TP_SCALE_SKEWED_LARGE_UNITS : 1u) :
                             TP_SCALE_EQUAL_UNITS;
            unsigned functions = tiny ? 1u : units * unit;
            for (unsigned f = 0; f < functions; ++f) tp_scale_function(file, index, tu, f, tiny, &random);
            if (series->shape == TP_SCALE_DIAGNOSTIC && (tu == 1 || tu + 1 == series->inputs))
                fprintf(file, "unsigned s%02u_t%04u_broken(void)\n{\n    return tp_scale_undeclared_%04u;\n}\n", index, tu, tu);
            if (tu == 0) fputs("int main(void)\n{\n    return 0;\n}\n", file);
            ok = !ferror(file);
            if (fclose(file) != 0) ok = 0;
        }
        ok = ok && tp_hash_file(path, hash, &bytes, &lines);
        if (ok)
        {
            series->bytes += bytes;
            ok = fprintf(list, "%s\t%" PRIu64 "\t%s\n", leaf, bytes, hash) > 0;
        }
    }
    if (list && fclose(list) != 0) ok = 0;
    uint64_t manifest_bytes = 0, manifest_lines = 0;
    ok = ok && tp_hash_file(manifest, series->hash, &manifest_bytes, &manifest_lines);
    return ok;
}

/* W=0 is the compiler's default (no -fcompile-jobs); W=1 the explicit
 * single-worker reference. Both run on one physical core. */
static int tp_scale_place(TpScaleRun* run, TpScalePoint* point)
{
    int ok = 1;
    if (point->smt)
    {
        point->cpus = run->requested;
    }
    else
    {
        ok = tp_topology_physical_prefix(run->topology, point->workers ? point->workers : 1, &point->cpus);
    }
    return ok;
}

static int tp_scale_add_point(TpScaleRun* run, unsigned series, unsigned workers, int smt)
{
    TpScalePoint* point = run->points + run->point_count;
    unsigned inputs = run->series[series].inputs;
    memset(point, 0, sizeof(*point));
    point->series = series;
    point->workers = workers;
    point->smt = smt;
    point->expected_workers = workers < 2 ? 1 : workers < inputs ? workers : inputs;
    int ok = tp_scale_place(run, point);
    if (ok)
    {
        run->series[series].point_count += 1;
        run->point_count += 1;
    }
    return ok;
}

static int tp_scale_generate(TpScaleRun* run, Arena* arena)
{
    TpConfig const* config = run->config;
    unsigned listed = run->config->scale_workers.count;
    unsigned capacity_series = TP_SCALE_SHAPES + 3 * listed + 1;
    unsigned capacity_points = 3 * (listed + 3) + 3 * 2 * listed + listed + 3;
    run->series = arena_allocate_zeroed(arena, TpScaleSeries, capacity_series);
    run->points = arena_allocate_zeroed(arena, TpScalePoint, capacity_points);
    int ok = run->series && run->points;
    for (unsigned shape = 0; shape <= TP_SCALE_DIAGNOSTIC && ok; ++shape)
    {
        if (shape < TP_SCALE_SHAPES && !(config->scale_shape_mask & (1u << shape))) continue;
        unsigned count_variants = shape == TP_SCALE_COUNT ? 3u : 1u;
        for (unsigned w = 0; w < TP_MAX_CPUS && ok; ++w)
        {
            int listed_worker = tp_cpu_set_has(&config->scale_workers, w);
            /* Count shapes run once per listed W > 1; the others once in all. */
            if (shape == TP_SCALE_COUNT ? !listed_worker || w < 2 : w != 0) continue;
            for (unsigned variant = 0; variant < count_variants && ok; ++variant)
            {
                TpScaleSeries* series = run->series + run->series_count;
                unsigned index = run->series_count;
                series->shape = shape;
                series->first_point = run->point_count;
                series->inputs = shape == TP_SCALE_TINY ? TP_SCALE_TINY_INPUTS_PER_WORKER * run->max_workers :
                                 shape == TP_SCALE_COUNT ? w - 1 + variant :
                                 shape == TP_SCALE_DIAGNOSTIC ? (run->max_workers + 1 > 3 ? run->max_workers + 1 : 3) :
                                 TP_SCALE_INPUTS_PER_WORKER * run->max_workers;
                if (shape == TP_SCALE_COUNT) snprintf(series->name, sizeof(series->name), "count-w%u-n%u", w, series->inputs);
                else snprintf(series->name, sizeof(series->name), "%s", tp_scale_shape_names[shape]);
                char leaf[96];
                snprintf(leaf, sizeof(leaf), "inputs/%s", series->name);
                ok = tp_path(series->directory, run->root, leaf) && tp_scale_write_inputs(run, series, index);
                run->series_count += ok;
                if (ok && shape == TP_SCALE_COUNT)
                {
                    ok = tp_scale_add_point(run, index, 1, 0) && tp_scale_add_point(run, index, w, 0);
                }
                else if (ok)
                {
                    ok = tp_scale_add_point(run, index, 0, 0) && tp_scale_add_point(run, index, 1, 0);
                    for (unsigned listed_w = 2; listed_w <= TP_SCALE_MAX_WORKERS && ok; ++listed_w)
                        if (tp_cpu_set_has(&config->scale_workers, listed_w)) ok = tp_scale_add_point(run, index, listed_w, 0);
                    if (ok && config->scale_allow_smt) ok = tp_scale_add_point(run, index, run->requested.count, 1);
                }
                if (!ok) tp_scale_invalid(run, "could not generate or place series %s", series->name);
            }
        }
    }
    return ok;
}

/* The header record of -fmetrics-out (docs/agents/driver.md, "Record text").
 * Readers take the fields they know; later versions only append fields. */
static int tp_scale_metrics(char const* path, TpScaleMetrics* metrics)
{
    memset(metrics, 0, sizeof(*metrics));
    FILE* file = fopen(path, "rb");
    char line[8192];
    unsigned seen = 0;
    int ok = file && fgets(line, sizeof(line), file) && !strncmp(line, "CC_METRICS ", 11);
    for (char* token = ok ? strtok(line + 11, " \r\n") : NULL; token; token = strtok(NULL, " \r\n"))
    {
        char* equal = strchr(token, '=');
        unsigned value = 0;
        if (equal)
        {
            *equal = 0;
            if (!strcmp(token, "inputs") && tp_number(equal + 1, &value)) { metrics->inputs = value; seen |= 1; }
            else if (!strcmp(token, "ok") && tp_number(equal + 1, &value)) { metrics->ok = value; seen |= 2; }
            else if (!strcmp(token, "compilation_workers") && tp_number(equal + 1, &value)) { metrics->workers = value; seen |= 4; }
            else if (!strcmp(token, "intervals") && strlen(equal + 1) < sizeof(metrics->intervals))
            {
                strcpy(metrics->intervals, equal + 1);
                seen |= 8;
            }
        }
    }
    if (file && fclose(file) != 0) ok = 0;
    return ok && seen == 15;
}

typedef struct TpScaleArguments
{
    char** values;
    char jobs[32];
    char metrics[TP_PATH_CAP + 16];
    unsigned count;
} TpScaleArguments;

static int tp_scale_arguments(TpScaleRun* run, TpScaleSeries const* series, TpScalePoint const* point,
                              char const* artifact, char const* metrics, char names[][16], TpScaleArguments* arguments)
{
    unsigned capacity = series->inputs + 10;
    char** args = (char**)calloc(capacity, sizeof(char*));
    unsigned argc = 0;
    if (args)
    {
        snprintf(arguments->jobs, sizeof(arguments->jobs), "-fcompile-jobs=%u", point->workers);
        snprintf(arguments->metrics, sizeof(arguments->metrics), "-fmetrics-out=%s", metrics);
        args[argc++] = run->compiler;
        args[argc++] = "cc";
        args[argc++] = "-g0";
        args[argc++] = "-O0";
        if (point->workers) args[argc++] = arguments->jobs;
        args[argc++] = arguments->metrics;
        /* Relative names in the series directory keep Windows command lines
         * short; consecutive C inputs with no -l are one link cohort. */
        for (unsigned tu = 0; tu < series->inputs; ++tu)
        {
            snprintf(names[tu], 16, "tu%04u.c", tu);
            args[argc++] = names[tu];
        }
        args[argc++] = "-o";
        args[argc++] = (char*)artifact;
        args[argc] = NULL;
    }
    arguments->values = args;
    arguments->count = argc;
    return args != NULL;
}

static void tp_scale_label(TpScaleRun const* run, TpScalePoint const* point, char* out, size_t capacity)
{
    if (point->workers) snprintf(out, capacity, "%s-w%u%s", run->series[point->series].name, point->workers, point->smt ? "-smt" : "");
    else snprintf(out, capacity, "%s-default", run->series[point->series].name);
}

/* One fresh compiler process. failure != 0 expects rejected source with no
 * artifact (the diagnostic series); otherwise exit 0, a nonempty artifact,
 * every input compiled and exactly the expected participating workers. */
static int tp_scale_sample(TpScaleRun* run, TpScalePoint* point, unsigned iteration, unsigned order, int timed, int failure)
{
    TpScaleSeries* series = run->series + point->series;
    char label[128], leaf[192], artifact[TP_PATH_CAP], metrics_path[TP_PATH_CAP], log[TP_PATH_CAP];
    tp_scale_label(run, point, label, sizeof(label));
    snprintf(leaf, sizeof(leaf), "%s.exe", label);
    int ok = tp_path(artifact, run->samples, leaf);
    snprintf(leaf, sizeof(leaf), "%s-%s%u.metrics", label, timed ? "i" : "warmup", iteration);
    ok = ok && tp_path(metrics_path, run->samples, leaf);
    snprintf(leaf, sizeof(leaf), "%s-%s%u.log", label, timed ? "i" : "warmup", iteration);
    ok = ok && tp_path(log, run->samples, leaf);
    char (*names)[16] = ok ? (char (*)[16])calloc(series->inputs, 16) : NULL;
    TpScaleArguments arguments = {0};
    ok = ok && names && tp_scale_arguments(run, series, point, artifact, metrics_path, names, &arguments);
    ok = ok && (remove(artifact) == 0 || errno == ENOENT) && (remove(metrics_path) == 0 || errno == ENOENT);
    if (ok)
    {
        fputs("{\"sample\":", run->commands); tp_json_string(run->commands, leaf);
        fputs(",\"cwd\":", run->commands); tp_json_string(run->commands, series->directory);
        fputs(",\"cpus\":", run->commands);
        char cpus[TP_CPU_SET_TEXT_CAP];
        ok = tp_cpu_set_format(&point->cpus, cpus, sizeof(cpus));
        tp_json_string(run->commands, cpus);
        fputs(",\"argv\":[", run->commands);
        for (unsigned i = 0; i < arguments.count; ++i)
        {
            if (i) fputc(',', run->commands);
            tp_json_string(run->commands, arguments.values[i]);
        }
        fputs("]}\n", run->commands);
        ok = fflush(run->commands) == 0 && ok;
    }
    TpProcess process = {0};
    if (ok)
    {
        process = tp_process_cpus(arguments.values, series->directory, log, run->config->timeout, &point->cpus, 0);
        int ended = !process.launch_error && !process.timed_out && !process.signal_number && process.wall_seconds > 0.0;
        ok = ended && (failure ? process.exit_code != 0 : process.exit_code == 0);
        if (!ok)
            tp_scale_invalid(run, "%s: exit=%d signal=%d timeout=%d launch_error=%d launch_stage=%s; see %s", label,
                             process.exit_code, process.signal_number, process.timed_out, process.launch_error,
                             tp_launch_stage_name(process.launch_stage), log);
    }
    char artifact_hash[65] = {0};
    uint64_t artifact_bytes = 0, lines = 0;
    if (ok && failure)
    {
        struct stat exists;
        ok = stat(artifact, &exists) != 0 && errno == ENOENT;
        if (!ok) tp_scale_invalid(run, "%s: rejected source left an artifact", label);
        char log_hash[65];
        ok = ok && tp_hash_file(log, log_hash, &artifact_bytes, &lines);
        if (ok)
        {
            point->exit_code = process.exit_code;
            memcpy(point->log_hash, log_hash, sizeof(log_hash));
        }
    }
    else if (ok)
    {
        TpScaleMetrics metrics;
        ok = tp_hash_file(artifact, artifact_hash, &artifact_bytes, &lines) && artifact_bytes > 0;
        if (!ok) tp_scale_invalid(run, "%s: no nonempty artifact", label);
        ok = ok && tp_scale_metrics(metrics_path, &metrics);
        if (!ok) tp_scale_invalid(run, "%s: missing or malformed CC_METRICS header in %s", label, metrics_path);
        if (ok && (metrics.inputs != series->inputs || metrics.ok != series->inputs))
        {
            tp_scale_invalid(run, "%s: compiler reported %u/%u ok inputs, expected %u", label, metrics.ok, metrics.inputs, series->inputs);
            ok = 0;
        }
        if (ok && metrics.workers != point->expected_workers)
        {
            tp_scale_invalid(run, "%s: compiler reported %u participating workers, expected %u (link-cohort path not exercised as requested)",
                             label, metrics.workers, point->expected_workers);
            ok = 0;
        }
        if (ok && !series->artifact_hash[0])
        {
            memcpy(series->artifact_hash, artifact_hash, sizeof(artifact_hash));
            series->artifact_bytes = artifact_bytes;
        }
        if (ok && strcmp(series->artifact_hash, artifact_hash))
        {
            tp_scale_invalid(run, "%s: artifact %s differs from %s for the same inputs", label, artifact_hash, series->artifact_hash);
            ok = 0;
        }
        double rss_budget = (double)run->config->scale_max_rss_mib * 1048576.0;
        if (ok && run->config->scale_max_rss_mib && !(process.peak_rss_bytes <= rss_budget))
        {
            tp_scale_invalid(run, "%s: peak RSS %.0f bytes exceeds the %u MiB budget", label, process.peak_rss_bytes,
                             run->config->scale_max_rss_mib);
            ok = 0;
        }
        if (ok)
        {
            point->observed_workers = metrics.workers;
            if (timed)
            {
                point->wall[point->samples] = process.wall_seconds;
                point->cpu[point->samples] = process.user_seconds + process.system_seconds;
                point->rss[point->samples] = process.peak_rss_bytes;
                point->samples += 1;
                fprintf(run->csv, "%s,%u,%u,%s,\"", series->name, series->inputs, point->workers, point->smt ? "smt" : "core");
                char cpus[TP_CPU_SET_TEXT_CAP];
                tp_cpu_set_format(&point->cpus, cpus, sizeof(cpus));
                fprintf(run->csv, "%s\",%u,%u,%.17g,%.17g,%.17g,%.17g,%u,%s,%" PRIu64 ",%s\n", cpus, iteration, order,
                        process.wall_seconds, process.user_seconds, process.system_seconds, process.peak_rss_bytes,
                        metrics.workers, metrics.intervals, artifact_bytes, artifact_hash);
                ok = !ferror(run->csv) && fflush(run->csv) == 0;
            }
        }
    }
    free(arguments.values);
    free(names);
    return ok;
}

/* Rejected source must fail identically at every worker count: the same exit
 * status and byte-identical ordered diagnostics, never a partial artifact. */
static int tp_scale_diagnostics(TpScaleRun* run, TpScaleSeries const* series)
{
    int ok = 1;
    TpScalePoint const* first = run->points + series->first_point;
    for (unsigned i = 0; i < series->point_count && ok; ++i)
    {
        TpScalePoint* point = run->points + series->first_point + i;
        ok = tp_scale_sample(run, point, 0, i, 1, 1);
        if (ok && (point->exit_code != first->exit_code || strcmp(point->log_hash, first->log_hash)))
        {
            tp_scale_invalid(run, "diagnostics at %u workers differ from the default run (exit %d vs %d)",
                             point->workers, point->exit_code, first->exit_code);
            ok = 0;
        }
    }
    return ok;
}

static void tp_scale_point_stats(TpScalePoint const* point, double* wall, double* low, double* high, double* cpu, double* rss)
{
    double sorted[TP_SCALE_MAX_REPEATS];
    memcpy(sorted, point->wall, sizeof(double) * point->samples);
    tp_sort(sorted, point->samples);
    *wall = tp_median_sorted(sorted, point->samples);
    tp_interval(sorted, point->samples, TP_SCALE_ALPHA, low, high);
    memcpy(sorted, point->cpu, sizeof(double) * point->samples);
    tp_sort(sorted, point->samples);
    *cpu = tp_median_sorted(sorted, point->samples);
    memcpy(sorted, point->rss, sizeof(double) * point->samples);
    tp_sort(sorted, point->samples);
    *rss = tp_median_sorted(sorted, point->samples);
}

static int tp_scale_metadata(TpScaleRun* run)
{
    TpConfig const* config = run->config;
    char path[TP_PATH_CAP], text[TP_CPU_SET_TEXT_CAP];
    int ok = tp_path(path, run->root, "scaling-metadata.json");
    FILE* file = ok ? fopen(path, "wb") : NULL;
    ok = file != NULL;
    if (file)
    {
        fprintf(file, "{\"schema\":\"%s\",\"compiler\":", TP_SCALE_SCHEMA);
        tp_json_string(file, run->compiler);
        fprintf(file, ",\"compiler_sha256\":\"%s\",\"profile\":", run->compiler_hash);
        tp_json_string(file, config->profile);
        fprintf(file, ",\"seed\":%u,\"scale\":%u,\"repeats\":%u,\"warmups\":%u,\"timeout_seconds\":%u,\"max_rss_mib\":%u,",
                config->seed, config->scale, config->scale_repeats, config->warmups, config->timeout, config->scale_max_rss_mib);
        fputs("\"cpu_set\":", file);
        tp_json_string(file, run->cpu_text);
        fputs(",\"excluded_cpus\":", file);
        tp_json_string(file, run->excluded_text);
        fputs(",\"cpu_set_permitted\":true,\"topology_root\":", file);
        tp_json_string(file, config->topology_root ? config->topology_root : TP_TOPOLOGY_ROOT);
        fprintf(file, ",\"logical_cpus\":%u,\"physical_cores\":%u,\"smt_siblings_in_set\":%u,\"allow_smt\":%s,\"topology\":[",
                run->topology->count, run->topology->physical_cores, run->topology->count - run->topology->physical_cores,
                config->scale_allow_smt ? "true" : "false");
        for (unsigned i = 0; i < run->topology->count; ++i)
        {
            TpCpuTopology const* cpu = run->topology->cpus + i;
            fprintf(file, "%s{\"cpu\":%u,\"package\":%d,\"core_id\":%d,\"core\":%u,\"siblings\":%u}", i ? "," : "",
                    cpu->cpu, cpu->package, cpu->core_id, cpu->core, cpu->siblings);
        }
        fputs("],\"rss_scope\":\"" TP_SCALE_RSS_SCOPE "\",\"isolation\":\"unverified\",\"smt_sibling_idle\":\"unverified\","
              "\"excluded\":[\"concurrent independent compiler processes\",\"input generation\",\"generated-program execution\"],"
              "\"series\":[", file);
        for (unsigned s = 0; s < run->series_count; ++s)
        {
            TpScaleSeries const* series = run->series + s;
            fprintf(file, "%s{\"name\":\"%s\",\"shape\":\"%s\",\"inputs\":%u,\"input_bytes\":%" PRIu64 ",\"inputs_sha256\":\"%s\",\"points\":[",
                    s ? "," : "", series->name, tp_scale_shape_names[series->shape], series->inputs, series->bytes, series->hash);
            for (unsigned i = 0; i < series->point_count; ++i)
            {
                TpScalePoint const* point = run->points + series->first_point + i;
                ok = tp_cpu_set_format(&point->cpus, text, sizeof(text)) && ok;
                fprintf(file, "%s{\"workers\":%u,\"placement\":\"%s\",\"expected_workers\":%u,\"cpus\":\"%s\"}", i ? "," : "",
                        point->workers, point->smt ? "smt" : "core", point->expected_workers, text);
            }
            fputs("]}", file);
        }
        fputs("]}\n", file);
        ok = !ferror(file) && ok;
        if (fclose(file) != 0) ok = 0;
    }
    return ok;
}

/* Report-only decision record. Speedup, efficiency and inflation compare each
 * point with the same series' explicit one-worker reference. The speedup
 * bounds combine the two order-statistic median intervals conservatively. */
static int tp_scale_summary(TpScaleRun* run, int valid)
{
    char path[TP_PATH_CAP];
    int ok = tp_path(path, run->root, "scaling.json");
    FILE* json = ok ? fopen(path, "wb") : NULL;
    FILE* markdown = NULL;
    ok = json != NULL;
    if (ok && valid)
    {
        ok = tp_path(path, run->root, "scaling.md");
        markdown = ok ? fopen(path, "wb") : NULL;
        ok = markdown != NULL;
    }
    if (ok)
    {
        fprintf(json, "{\"schema\":\"%s\",\"status\":\"%s\",\"reason\":", TP_SCALE_SCHEMA, valid ? "valid" : "invalid");
        tp_json_string(json, run->reason);
        fprintf(json, ",\"alpha\":%.17g,\"decision\":\"report-only\",\"series\":[", TP_SCALE_ALPHA);
        if (markdown)
        {
            fprintf(markdown, "# Multi-TU scaling (report only)\n\nCompiler `%s` (sha256 `%s`); CPU set `%s`%s%s%s; "
                    "%u logical CPUs on %u physical cores. Wall is the exec-to-exit median; the interval is the %.0f%% "
                    "order-statistic median interval. CPU is user+system. RSS: %s.\n",
                    run->compiler, run->compiler_hash, run->cpu_text, run->excluded_text[0] ? " (housekeeping core `" : "",
                    run->excluded_text, run->excluded_text[0] ? "` excluded)" : "", run->topology->count, run->topology->physical_cores,
                    (1.0 - TP_SCALE_ALPHA) * 100.0, TP_SCALE_RSS_SCOPE);
        }
        for (unsigned s = 0; valid && s < run->series_count; ++s)
        {
            TpScaleSeries const* series = run->series + s;
            if (series->shape == TP_SCALE_DIAGNOSTIC)
            {
                fprintf(json, "%s{\"name\":\"%s\",\"diagnostics_identical\":true,\"exit_code\":%d}", s ? "," : "",
                        series->name, run->points[series->first_point].exit_code);
                continue;
            }
            TpScalePoint const* reference = NULL;
            for (unsigned i = 0; i < series->point_count; ++i)
                if (run->points[series->first_point + i].workers == 1) reference = run->points + series->first_point + i;
            double reference_wall = NAN, reference_low = NAN, reference_high = NAN, reference_cpu = NAN, reference_rss = NAN;
            if (reference) tp_scale_point_stats(reference, &reference_wall, &reference_low, &reference_high, &reference_cpu, &reference_rss);
            fprintf(json, "%s{\"name\":\"%s\",\"inputs\":%u,\"artifact_sha256\":\"%s\",\"artifact_identical\":true,\"points\":[",
                    s ? "," : "", series->name, series->inputs, series->artifact_hash);
            if (markdown)
            {
                fprintf(markdown, "\n## %s (%u inputs)\n\n| workers | placement | observed workers | wall s | interval | speedup | efficiency | CPU s | CPU inflation | RSS MiB | RSS inflation |\n"
                        "|---|---|---:|---:|---|---:|---:|---:|---:|---:|---:|\n", series->name, series->inputs);
            }
            for (unsigned i = 0; i < series->point_count; ++i)
            {
                TpScalePoint const* point = run->points + series->first_point + i;
                double wall, low, high, cpu, rss;
                tp_scale_point_stats(point, &wall, &low, &high, &cpu, &rss);
                double speedup = reference_wall / wall;
                double speedup_low = reference_low / high, speedup_high = reference_high / low;
                double efficiency = speedup / (double)point->expected_workers;
                fprintf(json, "%s{\"workers\":%u,\"placement\":\"%s\",\"observed_workers\":%u,\"samples\":%u,\"wall_median\":",
                        i ? "," : "", point->workers, point->smt ? "smt" : "core", point->observed_workers, point->samples);
                tp_json_number(json, wall);
                fputs(",\"wall_interval\":[", json); tp_json_number(json, low); fputc(',', json); tp_json_number(json, high);
                fputs("],\"speedup\":", json); tp_json_number(json, speedup);
                fputs(",\"speedup_interval\":[", json); tp_json_number(json, speedup_low); fputc(',', json); tp_json_number(json, speedup_high);
                fputs("],\"efficiency\":", json); tp_json_number(json, efficiency);
                fputs(",\"cpu_seconds_median\":", json); tp_json_number(json, cpu);
                fputs(",\"cpu_inflation\":", json); tp_json_number(json, cpu / reference_cpu);
                fputs(",\"peak_rss_median\":", json); tp_json_number(json, rss);
                fputs(",\"rss_inflation\":", json); tp_json_number(json, rss / reference_rss);
                fputc('}', json);
                if (markdown)
                {
                    char workers[32];
                    if (point->workers) snprintf(workers, sizeof(workers), "%u", point->workers);
                    else snprintf(workers, sizeof(workers), "default");
                    fprintf(markdown, "| %s | %s | %u | %.6f | [%.6f, %.6f] | %.3f [%.3f, %.3f] | %.3f | %.6f | %.3f | %.1f | %.3f |\n",
                            workers, point->smt ? "smt" : "core", point->observed_workers, wall, low, high, speedup, speedup_low,
                            speedup_high, efficiency, cpu, cpu / reference_cpu, rss / 1048576.0, rss / reference_rss);
                }
            }
            fputs("]}", json);
        }
        fputs("]}\n", json);
        if (markdown)
        {
            fputs("\nAll artifacts are byte-identical across worker counts within each series, and rejected source produced "
                  "identical ordered diagnostics. This is not a performance gate, a generated-program result, or evidence "
                  "about concurrent independent compiler processes.\n", markdown);
            ok = !ferror(markdown) && ok;
        }
        ok = !ferror(json) && ok;
    }
    if (json && fclose(json) != 0) ok = 0;
    if (markdown && fclose(markdown) != 0) ok = 0;
    return ok;
}

/* Rotate the starting point every iteration and reverse odd iterations so no
 * worker count always follows the same predecessor. */
static int tp_scale_measure(TpScaleRun* run, TpScaleSeries const* series)
{
    int ok = 1;
    unsigned count = series->point_count;
    for (unsigned warmup = 0; warmup < run->config->warmups && ok; ++warmup)
        for (unsigned i = 0; i < count && ok; ++i) ok = tp_scale_sample(run, run->points + series->first_point + i, warmup, i, 0, 0);
    for (unsigned iteration = 0; iteration < run->config->scale_repeats && ok; ++iteration)
    {
        for (unsigned order = 0; order < count && ok; ++order)
        {
            unsigned step = iteration & 1 ? count - 1 - order : order;
            unsigned index = (step + iteration) % count;
            ok = tp_scale_sample(run, run->points + series->first_point + index, iteration, order, 1, 0);
        }
    }
    return ok;
}

static int tp_scale(TpConfig const* config)
{
    Arena* arena = arena_create((ArenaCreation){.flags = {.no_pool = 1}});
    TpScaleRun* run = arena_allocate_zeroed(arena, TpScaleRun, 1);
    int ok = run != NULL;
    int error = 0;
    if (ok)
    {
        run->config = config;
        run->topology = arena_allocate_zeroed(arena, TpTopology, 1);
        ok = run->topology && tp_absolute(config->output, run->root) && tp_absolute(config->compiler, run->compiler) &&
             tp_path(run->samples, run->root, "samples");
    }
    if (ok)
    {
        struct stat exists;
        char path[TP_PATH_CAP];
        ok = tp_path(path, run->root, "scaling-metadata.json") && stat(path, &exists) != 0 && errno == ENOENT;
        if (!ok) tp_error("result directory already contains a scaling run; choose a new --output directory");
    }
    if (ok)
    {
        error = !strcmp(config->cpu_set, "auto") ? tp_cpu_set_allowed(&run->requested) :
                tp_cpu_set_parse(config->cpu_set, &run->requested) ? 0 : EINVAL;
        /* A housekeeping core leaves the set whole, siblings included, before
         * any check: it must be in the requested set, and the rest must not
         * become empty. */
        if (!error && config->scale_exclude_core >= 0)
        {
            TpCpuSet excluded;
            unsigned cpu = (unsigned)config->scale_exclude_core;
            error = tp_cpu_set_has(&run->requested, cpu) ? 0 : EINVAL;
            if (!error) error = tp_topology_siblings(config->topology_root ? config->topology_root : TP_TOPOLOGY_ROOT, cpu, &excluded);
            for (unsigned c = 0; c < TP_MAX_CPUS && !error; ++c)
                if (tp_cpu_set_has(&excluded, c)) tp_cpu_set_remove(&run->requested, c);
            if (!error && (!run->requested.count || !tp_cpu_set_format(&excluded, run->excluded_text, sizeof(run->excluded_text))))
                error = EINVAL;
            if (error) tp_error("--exclude-core %u is not in --cpu-set %s, has no readable siblings, or leaves no CPU",
                                cpu, config->cpu_set);
        }
        if (!error) error = tp_cpu_set_permitted(&run->requested);
        ok = !error && tp_cpu_set_format(&run->requested, run->cpu_text, sizeof(run->cpu_text));
        if (!ok) tp_error("--cpu-set %s is unsupported here or not entirely inside the permitted affinity mask; it is never narrowed: %s",
                          config->cpu_set, strerror(error ? error : EINVAL));
    }
    if (ok)
    {
        error = tp_topology_read(config->topology_root ? config->topology_root : TP_TOPOLOGY_ROOT, &run->requested, run->topology);
        ok = !error;
        if (!ok) tp_error("CPU topology unavailable for --cpu-set %s: %s", run->cpu_text, strerror(error));
    }
    for (unsigned w = 1; w <= TP_SCALE_MAX_WORKERS && ok; ++w)
        if (tp_cpu_set_has(&config->scale_workers, w)) run->max_workers = w;
    if (ok && run->max_workers > run->topology->physical_cores)
    {
        tp_error("%u workers requested but --cpu-set %s holds %u physical cores; workers are never doubled onto SMT siblings or one CPU",
                 run->max_workers, run->cpu_text, run->topology->physical_cores);
        ok = 0;
    }
    if (ok && config->scale_allow_smt && run->topology->count == run->topology->physical_cores)
    {
        tp_error("--allow-smt requested but --cpu-set %s contains no SMT siblings", run->cpu_text);
        ok = 0;
    }
    if (ok && config->scale_allow_smt && run->requested.count > TP_SCALE_MAX_WORKERS)
    {
        tp_error("--allow-smt would request %u workers; the limit is %u", run->requested.count, TP_SCALE_MAX_WORKERS);
        ok = 0;
    }
    uint64_t compiler_bytes = 0, compiler_lines = 0;
    ok = ok && tp_hash_file(run->compiler, run->compiler_hash, &compiler_bytes, &compiler_lines) && tp_mkdirs(run->samples);
    int generated = ok && tp_scale_generate(run, arena);
    ok = generated && tp_scale_metadata(run);
    char path[TP_PATH_CAP];
    if (ok)
    {
        run->csv = tp_path(path, run->root, "scaling.csv") ? fopen(path, "wb") : NULL;
        run->commands = tp_path(path, run->root, "commands.jsonl") ? fopen(path, "wb") : NULL;
        ok = run->csv && run->commands && fputs(TP_SCALE_CSV_HEADER, run->csv) >= 0;
    }
    for (unsigned s = 0; s < run->series_count && ok; ++s)
    {
        TpScaleSeries const* series = run->series + s;
        ok = series->shape == TP_SCALE_DIAGNOSTIC ? tp_scale_diagnostics(run, series) : tp_scale_measure(run, series);
    }
    if (run && run->csv && fclose(run->csv) != 0) ok = 0;
    if (run && run->commands && fclose(run->commands) != 0) ok = 0;
    /* Generation succeeded means a result directory exists: always record
     * whether it is valid, so a partial run never looks like a missing one. */
    if (generated)
    {
        if (!ok) tp_scale_invalid(run, "incomplete scaling run");
        ok = tp_scale_summary(run, ok) && ok;
    }
    printf("THROUGHPUT_SCALE status=%s series=%u points=%u output=%s\n", ok ? "valid" : "invalid",
           run ? run->series_count : 0, run ? run->point_count : 0, run ? run->root : "");
    arena_destroy(arena, 1);
    return ok ? 0 : 2;
}

#endif
