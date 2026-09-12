// Reuse the repository's native process collector for the -E action.
// Build from the repository root with -D_GNU_SOURCE -O2 -I. and -lm,
// passing this file's path; the audit records the complete command.
#include "tools/throughput/platform.h"

int main(int argc, char** argv)
{
    int result;
    if (argc < 4)
    {
        fprintf(stderr, "usage: measure_preprocess LOG CWD COMPILER [ARGS...]\n");
        result = 2;
    }
    else
    {
        int cpu = tp_first_allowed_cpu();
        TpProcess process = tp_process(argv + 3, argv[2], argv[1], 60, cpu, 0);
        printf("{\"wall_seconds\":%.9f,\"user_seconds\":%.9f,\"system_seconds\":%.9f,"
               "\"peak_rss_bytes\":%.0f,\"exit_code\":%d,\"signal\":%d,\"timeout\":%d,\"launch_error\":%d,\"cpu\":%d}\n",
               process.wall_seconds, process.user_seconds, process.system_seconds, process.peak_rss_bytes,
               process.exit_code, process.signal_number, process.timed_out, process.launch_error, cpu);
        result = process.exit_code || process.signal_number || process.timed_out || process.launch_error ? 1 : 0;
    }
    return result;
}
