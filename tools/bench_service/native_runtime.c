/* Runtime measurement for immutable static programs (#2648).
 * Map: bq_native_runtime_records validates fixed original sample rows;
 * bq_native_sample verifies/prepares once, keeps trusted rows in memory, then
 * emits them over the inherited pipe. Uploaded stdout is never that pipe.
 * bq_native_sampler fixes the candidate account and production CPU. The
 * shared throughput collector owns launch/wait4/timeout/group cleanup; this
 * recipe is diagnostic process latency, not an admission or a kernel timer. */
#include <math.h>
#include <inttypes.h>
#include <limits.h>
#define TP_MEDIAN_ONLY 1
#include "../throughput/stats.h"
#undef TP_MEDIAN_ONLY

BUSTER_GLOBAL_LOCAL bool bq_native_runtime_records(char const* bytes, u32 length, char const* identity,
                                                    int cpu, bool require_success, BqRuntimeSummary* summary)
{
    if (summary) *summary = (BqRuntimeSummary){0};
    double wall[BQ_RUNTIME_SAMPLES] = {0}, process_cpu[BQ_RUNTIME_SAMPLES] = {0};
    u64 maximum_rss = 0;
    char header[512];
    int count = snprintf(header, sizeof(header),
        "BQ-RUNTIME-V1\nprogram-manifest-sha256=%s\nmeasurement=process-launch-through-wait4\n"
        "warmups=2\nsamples=9\ntimeout-seconds=10\ncpu=%d\noracle=exit-zero-unchecked-transcript\n"
        "qualification=diagnostic-only\npmu=unavailable\nallocations=unavailable\n", identity, cpu);
    bool ok = count > 0 && (u32)count < sizeof(header) && length < BQ_RUNTIME_RECORD_CAP &&
              length >= (u32)count && !memchr(bytes, 0, length) && !memcmp(bytes, header, (u32)count);
    u32 cursor = ok ? (u32)count : 0;
    for (u32 row = 0; ok && row < BQ_RUNTIME_WARMUPS + BQ_RUNTIME_SAMPLES; row += 1)
    {
        char prefix[64];
        int prefix_length = snprintf(prefix, sizeof(prefix), "row=%s,%u,",
            row < BQ_RUNTIME_WARMUPS ? "warmup" : "sample", row < BQ_RUNTIME_WARMUPS ? row : row - BQ_RUNTIME_WARMUPS);
        ok = prefix_length > 0 && cursor + (u32)prefix_length <= length &&
             !memcmp(bytes + cursor, prefix, (u32)prefix_length);
        if (ok) cursor += (u32)prefix_length;
        u64 values[14] = {0};
        for (u32 field = 0; ok && field < BUSTER_ARRAY_LENGTH(values); field += 1)
        {
            u32 start = cursor;
            for (; cursor < length && bytes[cursor] >= '0' && bytes[cursor] <= '9'; cursor += 1) { }
            IntegerParsingU64 value = string8_parse_u64_decimal((String8){(char8*)bytes + start, cursor - start});
            ok = cursor > start && value.status == INTEGER_PARSING_SUCCESS && value.length == cursor - start &&
                 cursor < length && bytes[cursor] == (field == BUSTER_ARRAY_LENGTH(values) - 1 ? '\n' : ',');
            if (ok) { values[field] = value.value; cursor += 1; }
        }
        ok = ok && values[0] <= UINT64_C(3600000000000) &&
             values[1] <= UINT64_C(3600000000000) && values[2] <= UINT64_C(3600000000000) && values[8] <= 15 && values[9] <= 256 && values[10] <= 64 && values[11] <= 1 &&
             values[12] <= INT_MAX && values[13] <= 10 && values[3] <= UINT64_C(8589934592);
        if (ok && require_success)
            ok = values[0] > 0 && values[8] == 15 && values[9] == 1 && !values[10] && !values[11] &&
                 !values[12] && !values[13];
        if (ok && row >= BQ_RUNTIME_WARMUPS)
        {
            wall[row - BQ_RUNTIME_WARMUPS] = (double)values[0];
            process_cpu[row - BQ_RUNTIME_WARMUPS] = (double)(values[1] + values[2]);
            if (values[3] > maximum_rss) maximum_rss = values[3];
        }
    }
    ok = ok && cursor == length;
    if (ok && summary)
    {
        tp_sort(wall, BQ_RUNTIME_SAMPLES);
        tp_sort(process_cpu, BQ_RUNTIME_SAMPLES);
        summary->median_wall_ns = (u64)tp_median_sorted(wall, BQ_RUNTIME_SAMPLES);
        summary->median_cpu_ns = (u64)tp_median_sorted(process_cpu, BQ_RUNTIME_SAMPLES);
        summary->minimum_wall_ns = (u64)wall[0];
        summary->maximum_wall_ns = (u64)wall[BQ_RUNTIME_SAMPLES - 1];
        summary->maximum_rss_bytes = maximum_rss;
    }
    return ok;
}

#ifdef __linux__
#define TP_PROCESS_DESCRIPTOR_ONLY 1
#include "../throughput/platform.h"
#undef TP_PROCESS_DESCRIPTOR_ONLY

BUSTER_GLOBAL_LOCAL u64 bq_native_ns(double seconds)
{
    u64 value = isfinite(seconds) && seconds >= 0 && seconds < (double)UINT64_MAX * 1e-9 ?
                (u64)(seconds * 1e9) : 0;
    return value;
}

BUSTER_GLOBAL_LOCAL BqError bq_native_sample(char const* source, char const* identity, uid_t owner, int cpu,
                                             FILE* output)
{
    int executable = bq_native_executable(source, identity, owner);
    /* Same-UID payloads cannot inspect sampler memory or its trusted pipe via
     * ptrace/proc-fd access. Child descriptor marking closes all trusted FDs on
     * exec; stdout/stderr are first replaced by one untrusted bounded file. */
    bool ok = executable >= 0 && prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) == 0 &&
              prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) == 0;
    char records[BQ_RUNTIME_RECORD_CAP] = {0};
    int written = ok ? snprintf(records, sizeof(records),
        "BQ-RUNTIME-V1\nprogram-manifest-sha256=%s\nmeasurement=process-launch-through-wait4\n"
        "warmups=2\nsamples=9\ntimeout-seconds=10\ncpu=%d\noracle=exit-zero-unchecked-transcript\n"
        "qualification=diagnostic-only\npmu=unavailable\nallocations=unavailable\n", identity, cpu) : -1;
    ok = ok && written > 0 && (u32)written < sizeof(records);
    u32 used = ok ? (u32)written : 0;
    bool successful = ok;
    for (u32 row = 0; ok && row < BQ_RUNTIME_WARMUPS + BQ_RUNTIME_SAMPLES; row += 1)
    {
        char log_name[64];
        int name_length = snprintf(log_name, sizeof(log_name), "native-run-%u.log", row);
        int log = name_length > 0 && (u32)name_length < sizeof(log_name) ?
                  open(log_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
        ok = log >= 0;
        TpProcess process = {0};
        if (ok)
        {
            TpDescriptorLaunch launch = {.executable = executable, .log = log, .file_limit = BQ_NATIVE_LOG_CAP};
            char* const arguments[] = {"program", NULL};
            process = tp_process_internal(arguments, NULL, NULL, BQ_RUNTIME_TIMEOUT, cpu, 0, &launch);
            close(log);
            int length = snprintf(records + used, sizeof(records) - used,
                "row=%s,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,%u,%u,%u,%u,%u\n",
                row < BQ_RUNTIME_WARMUPS ? "warmup" : "sample", row < BQ_RUNTIME_WARMUPS ? row : row - BQ_RUNTIME_WARMUPS,
                (uint64_t)bq_native_ns(process.wall_seconds), (uint64_t)bq_native_ns(process.user_seconds),
                (uint64_t)bq_native_ns(process.system_seconds),
                (uint64_t)(isfinite(process.peak_rss_bytes) && process.peak_rss_bytes >= 0 ? process.peak_rss_bytes : 0),
                (uint64_t)process.diagnostics[0], (uint64_t)process.diagnostics[1],
                (uint64_t)process.diagnostics[2], (uint64_t)process.diagnostics[3], process.diagnostics_available,
                (unsigned)(process.exit_code + 1), (unsigned)process.signal_number, (unsigned)process.timed_out,
                (unsigned)process.launch_error, (unsigned)process.launch_stage);
            ok = length > 0 && (u32)length < sizeof(records) - used;
            if (ok) used += (u32)length;
            successful = successful && !process.launch_error && !process.signal_number && !process.timed_out &&
                         process.exit_code == 0 && process.wall_seconds > 0 && isfinite(process.peak_rss_bytes) &&
                         process.diagnostics_available == 15;
        }
    }
    int after = ok ? bq_native_executable(source, identity, owner) : -1;
    ok = ok && after >= 0;
    if (after >= 0) close(after);
    if (executable >= 0) close(executable);
    ok = ok && bq_native_runtime_records(records, used, identity, cpu, false, NULL) &&
         fwrite(records, 1, used, output) == used && fflush(output) == 0;
    BqError error = ok ? successful ? BQ_OK : BQ_WORKER_FAILED : BQ_CONFIGURATION_MISMATCH;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_native_sampler(char const* source, char const* identity)
{
    struct passwd* service = getpwnam("buster-bench");
    uid_t owner = service ? service->pw_uid : (uid_t)-1;
    struct passwd* candidate = getpwnam("buster-bench-candidate");
    bool allowed = candidate && candidate->pw_uid != 0 && candidate->pw_uid != owner &&
                   getuid() == candidate->pw_uid && geteuid() == candidate->pw_uid &&
                   getgid() == candidate->pw_gid && getegid() == candidate->pw_gid;
    BqError error = allowed ? bq_native_sample(source, identity, owner, 2, stdout) : BQ_CONFIGURATION_MISMATCH;
    return error;
}
#else
BUSTER_GLOBAL_LOCAL BqError bq_native_sampler(char const* source, char const* identity)
{
    (void)source; (void)identity;
    return BQ_UNSUPPORTED;
}
#endif
