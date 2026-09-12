/* Offline replay only. Keep zero CPU observations in raw data, and report an
 * unavailable CPU assessment where the unchanged statistics reject zeros. */
#define main throughput_cli_main
#include <throughput.c>
#undef main

int main(int argc, char** argv)
{
    enum { jobs = 6, pairs = 16 };
    static TpRow rows[jobs * TP_ROUNDS * 2 * pairs];
    char const* names[jobs] = {"tiny_startup", "many_functions", "symbols_8", "symbols_64", "symbols_512", "symbols_2048"};
    int ok = argc == 2 && tp_load_samples(argv[1], "raw.csv", jobs, pairs, 1, rows);
    if (ok) puts("round,job,metric,assessment_available,base_median,head_median,ratio_median,ratio_low_99,ratio_high_99,base_relative_mad,head_relative_mad");
    for (unsigned j = 0; ok && j < jobs; ++j)
    {
        TpRow const* reference = rows + tp_row_index(j, 0, 0, 0, pairs);
        for (unsigned r = 0; ok && r < TP_ROUNDS; ++r)
        {
            for (unsigned metric = 0; ok && metric < 3; ++metric)
            {
                double a[pairs], b[pairs];
                for (unsigned p = 0; p < pairs; ++p)
                {
                    TpRow const* x = rows + tp_row_index(j, r, 0, p, pairs);
                    TpRow const* y = rows + tp_row_index(j, r, 1, p, pairs);
                    ok = ok && x->present && y->present && strcmp(reference->output_hash, x->output_hash) == 0 && strcmp(reference->output_hash, y->output_hash) == 0;
                    ok = ok && reference->output_bytes == x->output_bytes && reference->output_bytes == y->output_bytes;
                    ok = ok && reference->source_bytes == x->source_bytes && reference->source_bytes == y->source_bytes;
                    ok = ok && reference->source_lines == x->source_lines && reference->source_lines == y->source_lines;
                    ok = ok && reference->source_functions == x->source_functions && reference->source_functions == y->source_functions;
                    a[p] = metric == 0 ? x->process.wall_seconds : metric == 1 ? x->process.user_seconds + x->process.system_seconds : x->process.peak_rss_bytes;
                    b[p] = metric == 0 ? y->process.wall_seconds : metric == 1 ? y->process.user_seconds + y->process.system_seconds : y->process.peak_rss_bytes;
                }
                TpAssessment result = tp_assess(a, b, pairs, 0.05, 0.0, 0.01);
                char const* label = metric == 0 ? "wall_seconds" : metric == 1 ? "cpu_seconds" : "peak_rss_bytes";
                if (result.valid)
                    printf("%u,%s,%s,1,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n", r, names[j], label,
                        result.baseline_median, result.candidate_median, result.ratio_median, result.ratio_low, result.ratio_high,
                        result.baseline_relative_mad, result.candidate_relative_mad);
                else
                {
                    printf("%u,%s,%s,0,NA,NA,NA,NA,NA,NA,NA\n", r, names[j], label);
                    if (metric != 1) ok = 0;
                }
            }
        }
    }
    if (!ok) fprintf(stderr, "invalid or incomplete raw observations\n");
    return !ok;
}
