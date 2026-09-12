/* #116 supplement: generated C populations with one call and one function
 * pointer per definition. Reuses the unchanged native process/PMU collector,
 * output hashing and paired statistics. This is not a dedicated-host gate.
 * Build: clang -O2 -Itools/throughput measure_assembly.c -lm -o measure_assembly
 * Run: measure_assembly BASE CANDIDATE OUTPUT_DIRECTORY REPOSITORY_DIRECTORY
 * The output directory must already exist. A separate larger qualification
 * failed its baseline 8K timeout; that result is retained beside this source. */
#define main throughput_cli_main
#include <throughput.c>
#undef main

int main(int argc, char** argv)
{
    enum { populations = 6, job_count = populations * 2, pairs = 8 };
    unsigned counts[populations] = {1, 8, 256, 512, 1024, 2048};
    TpJob jobs[job_count] = {0};
    TpConfig config = {.cpu = -1, .timeout = 60};
    static TpRow rows[job_count][pairs][2];
    char hashes[job_count][65] = {{0}};
    int ok = argc == 5;
    if (!ok) fprintf(stderr, "usage: measure_assembly BASE CANDIDATE OUTPUT_DIRECTORY REPOSITORY_DIRECTORY\n");
    FILE* raw = NULL;
    FILE* commands = NULL;
    FILE* capabilities = NULL;
    FILE* summary = NULL;
    char path[TP_PATH_CAP];
    if (ok)
    {
        ok = tp_path(path, argv[3], "raw.csv") && (raw = fopen(path, "wb")) != NULL;
        ok = ok && tp_path(path, argv[3], "commands.jsonl") && (commands = fopen(path, "wb")) != NULL;
        ok = ok && tp_path(path, argv[3], "capabilities.jsonl") && (capabilities = fopen(path, "wb")) != NULL;
        ok = ok && tp_path(path, argv[3], "summary.csv") && (summary = fopen(path, "wb")) != NULL;
    }
    if (ok) fputs(TP_RAW_HEADER, raw);
    for (unsigned population = 0; ok && population < populations; population += 1)
    {
        unsigned count = counts[population];
        char leaf[64];
        snprintf(leaf, sizeof(leaf), "functions_%u.c", count);
        ok = tp_path(path, argv[3], leaf);
        FILE* file = ok ? fopen(path, "wb") : NULL;
        ok = file != NULL;
        if (ok)
        {
            fputs("extern unsigned external(unsigned);\n", file);
            for (unsigned index = 0; index < count; index += 1)
            {
                fprintf(file, "unsigned function_%u(unsigned x) { return external(x + %uu); }\n", index, index);
                fprintf(file, "unsigned (*pointer_%u)(unsigned) = function_%u;\n", index, index);
            }
            ok = fclose(file) == 0;
        }
        for (unsigned kind = 0; ok && kind < 2; kind += 1)
        {
            unsigned j = population * 2 + kind;
            TpJob* job = jobs + j;
            snprintf(job->workload.name, sizeof(job->workload.name), "functions_%u_%s", count, kind ? "assembly" : "object");
            job->mode = 2;
            job->assembly = (int)kind;
            job->workload.functions = count;
            ok = tp_path(job->workload.path, argv[3], leaf);
            ok = ok && tp_hash_file(job->workload.path, job->workload.hash, &job->workload.bytes, &job->workload.lines);
            printf("job=%u name=%s source_sha256=%s bytes=%" PRIu64 "\n", j, job->workload.name, job->workload.hash, job->workload.bytes);
            for (unsigned variant = 0; ok && variant < 2; variant += 1)
            {
                char id[64];
                snprintf(id, sizeof(id), "warm-j%u-v%u", j, variant);
                TpRow row;
                ok = tp_measure(&config, job, argv[variant + 1], argv[4], argv[3], variant, id, 0, &row, commands, capabilities);
                if (variant == 0) memcpy(hashes[j], row.output_hash, sizeof(row.output_hash));
                else ok = ok && strcmp(hashes[j], row.output_hash) == 0;
            }
        }
    }
    for (unsigned pair = 0; ok && pair < pairs; pair += 1)
    {
        for (unsigned j = 0; ok && j < job_count; j += 1)
        {
            for (unsigned order = 0; ok && order < 2; order += 1)
            {
                unsigned variant = order ^ (pair & 1);
                char id[64];
                snprintf(id, sizeof(id), "pair%u-j%u-v%u", pair, j, variant);
                TpRow* row = &rows[j][pair][variant];
                ok = tp_measure(&config, jobs + j, argv[variant + 1], argv[4], argv[3], variant, id, 0, row, commands, capabilities);
                ok = ok && strcmp(hashes[j], row->output_hash) == 0;
                ok = ok && tp_sample_csv(raw, 0, pair, order, variant, j, row);
            }
        }
    }
    if (ok) fputs("job,metric,base_median,candidate_median,ratio_median,ratio_low_99,ratio_high_99,base_relative_mad,candidate_relative_mad\n", summary);
    for (unsigned j = 0; ok && j < job_count; j += 1)
    {
        // CPU time can round to zero for tiny processes; all raw CPU samples
        // are retained, without manufacturing a valid ratio for that series.
        for (unsigned metric = 0; metric < 2; metric += 1)
        {
            double baseline[pairs], candidate[pairs];
            for (unsigned pair = 0; pair < pairs; pair += 1)
            {
                TpProcess const* a = &rows[j][pair][0].process;
                TpProcess const* b = &rows[j][pair][1].process;
                baseline[pair] = metric ? a->peak_rss_bytes : a->wall_seconds;
                candidate[pair] = metric ? b->peak_rss_bytes : b->wall_seconds;
            }
            TpAssessment assessment = tp_assess(baseline, candidate, pairs, 0.05, 0.0, 0.01);
            ok = ok && assessment.valid;
            fprintf(summary, "%s,%s,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n", jobs[j].workload.name,
                    metric ? "peak_rss_bytes" : "wall_seconds", assessment.baseline_median, assessment.candidate_median,
                    assessment.ratio_median, assessment.ratio_low, assessment.ratio_high,
                    assessment.baseline_relative_mad, assessment.candidate_relative_mad);
        }
    }
    // Separate hardware-counter probes do not enter the wall-time series.
    for (unsigned variant = 0; ok && variant < 2; variant += 1)
    {
        TpRow row;
        char id[32];
        snprintf(id, sizeof(id), "pmu-v%u", variant);
        ok = tp_measure(&config, jobs + job_count - 1, argv[variant + 1], argv[4], argv[3], variant, id, 1, &row, commands, capabilities);
        ok = ok && strcmp(hashes[job_count - 1], row.output_hash) == 0;
        printf("pmu variant=%u instructions=%.17g cycles=%.17g\n", variant, row.process.counters[1], row.process.counters[0]);
    }
    if (raw && fclose(raw) != 0) ok = 0;
    if (commands && fclose(commands) != 0) ok = 0;
    if (capabilities && fclose(capabilities) != 0) ok = 0;
    if (summary && fclose(summary) != 0) ok = 0;
    return !ok;
}
