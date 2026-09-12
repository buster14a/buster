/* Audit supplement: reuse the repository's process collector and statistics.
 * The unmodified standard corpus has four inputs above eBPF's frame limit.
 * This wrapper supplies only previously qualified bounded eBPF jobs; it does
 * not change the standard runner or claim its dedicated-host CI verdict. */
#define main throughput_cli_main
#include <throughput.c>
#undef main

int main(int argc, char** argv)
{
    enum { jobs_count = 6, rounds = 2, pairs = 16 };
    int ok = argc == 6;
    if (!ok) fprintf(stderr, "usage: measure_objects base head inputs output cwd\n");
    char const* names[jobs_count] = {"tiny_startup", "many_functions", "symbols_8", "symbols_64", "symbols_512", "symbols_2048"};
    char const* files[jobs_count] = {"corpus/tiny_startup.c", "corpus/many_functions.c", "symbols_8.c", "symbols_64.c", "symbols_512.c", "symbols_2048.c"};
    uint64_t functions[jobs_count] = {1, 512, 16, 128, 1024, 4096};
    TpJob jobs[jobs_count] = {0};
    TpConfig config = {.cpu = -1, .timeout = 60, .flags = {"-target", "bpfel-unknown-linux"}, .flag_count = 2};
    static TpRow rows[rounds][jobs_count][pairs][2];
    char hashes[jobs_count][65] = {{0}};
    FILE* raw = NULL;
    FILE* commands = NULL;
    FILE* capabilities = NULL;
    FILE* summary = NULL;
    char path[TP_PATH_CAP];
    if (ok)
    {
        ok = tp_path(path, argv[4], "raw.csv") && (raw = fopen(path, "wb")) != NULL;
        ok = ok && tp_path(path, argv[4], "commands.jsonl") && (commands = fopen(path, "wb")) != NULL;
        ok = ok && tp_path(path, argv[4], "capabilities.jsonl") && (capabilities = fopen(path, "wb")) != NULL;
        ok = ok && tp_path(path, argv[4], "summary.csv") && (summary = fopen(path, "wb")) != NULL;
    }
    if (ok) fputs(TP_RAW_HEADER, raw);
    for (unsigned j = 0; ok && j < jobs_count; ++j)
    {
        snprintf(jobs[j].workload.name, sizeof(jobs[j].workload.name), "%s", names[j]);
        jobs[j].mode = 2;
        jobs[j].workload.functions = functions[j];
        ok = tp_path(jobs[j].workload.path, argv[3], files[j]);
        ok = ok && tp_hash_file(jobs[j].workload.path, jobs[j].workload.hash, &jobs[j].workload.bytes, &jobs[j].workload.lines);
        printf("job=%u name=%s source_hash=%s\n", j, names[j], jobs[j].workload.hash);
        for (unsigned v = 0; ok && v < 2; ++v)
        {
            char id[64];
            snprintf(id, sizeof(id), "warm-j%u-v%u", j, v);
            TpRow row;
            ok = tp_measure(&config, jobs + j, argv[v + 1], argv[5], argv[4], v, id, 0, &row, commands, capabilities);
            if (v == 0) memcpy(hashes[j], row.output_hash, sizeof(row.output_hash));
            else ok = ok && strcmp(hashes[j], row.output_hash) == 0;
        }
    }
    for (unsigned r = 0; ok && r < rounds; ++r)
    {
        for (unsigned p = 0; ok && p < pairs; ++p)
        {
            for (unsigned j = 0; ok && j < jobs_count; ++j)
            {
                for (unsigned o = 0; ok && o < 2; ++o)
                {
                    unsigned v = o ^ ((p + r) & 1);
                    char id[64];
                    snprintf(id, sizeof(id), "r%u-p%02u-j%u-v%u", r, p, j, v);
                    TpRow* row = &rows[r][j][p][v];
                    ok = tp_measure(&config, jobs + j, argv[v + 1], argv[5], argv[4], v, id, 0, row, commands, capabilities);
                    ok = ok && strcmp(hashes[j], row->output_hash) == 0 && row->source_bytes == jobs[j].workload.bytes;
                    ok = ok && tp_sample_csv(raw, r, p, o, v, j, row);
                }
            }
        }
    }
    if (ok) fputs("round,job,metric,base_median,head_median,ratio_median,ratio_low_99,ratio_high_99,base_relative_mad,head_relative_mad\n", summary);
    for (unsigned r = 0; ok && r < rounds; ++r)
    {
        for (unsigned j = 0; j < jobs_count; ++j)
        {
            for (unsigned metric = 0; metric < 3; ++metric)
            {
                double a[pairs], b[pairs];
                for (unsigned p = 0; p < pairs; ++p)
                {
                    TpProcess const* x = &rows[r][j][p][0].process;
                    TpProcess const* y = &rows[r][j][p][1].process;
                    a[p] = metric == 0 ? x->wall_seconds : metric == 1 ? x->user_seconds + x->system_seconds : x->peak_rss_bytes;
                    b[p] = metric == 0 ? y->wall_seconds : metric == 1 ? y->user_seconds + y->system_seconds : y->peak_rss_bytes;
                }
                TpAssessment assessment = tp_assess(a, b, pairs, 0.05, 0.0, 0.01);
                ok = ok && assessment.valid;
                fprintf(summary, "%u,%s,%s,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n", r, names[j],
                    metric == 0 ? "wall_seconds" : metric == 1 ? "cpu_seconds" : "peak_rss_bytes",
                    assessment.baseline_median, assessment.candidate_median, assessment.ratio_median,
                    assessment.ratio_low, assessment.ratio_high, assessment.baseline_relative_mad, assessment.candidate_relative_mad);
            }
        }
    }
    if (raw && fclose(raw) != 0) ok = 0;
    if (commands && fclose(commands) != 0) ok = 0;
    if (capabilities && fclose(capabilities) != 0) ok = 0;
    if (summary && fclose(summary) != 0) ok = 0;
    return !ok;
}
