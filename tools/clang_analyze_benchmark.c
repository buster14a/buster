// Fixed native wrapper for the trusted 9700X full analyzer comparison.
// The Python compare-pull harness selects only this profile and the baseline
// build driver owns its phase order, arguments, deadlines and wait4 counters.

#define BUSTER_ANALYZE_BENCHMARK_RUNS 4
#define BUSTER_ANALYZE_BENCHMARK_SHARDS 8
#define BUSTER_ANALYZE_BENCHMARK_JOBS 2
#define BUSTER_ANALYZE_BENCHMARK_TU_TIMEOUT 600
#define BUSTER_ANALYZE_BENCHMARK_BUDGET_US (75ull * 60ull * 1000000ull)
#define BUSTER_ANALYZE_BENCHMARK_OTHER_LIMIT_US (3ull * 60ull * 1000000ull)
#define BUSTER_ANALYZE_BENCHMARK_CAPTURE_BYTES BUSTER_MB(16)

typedef struct ClangAnalyzeBenchmarkOptions ClangAnalyzeBenchmarkOptions;
struct ClangAnalyzeBenchmarkOptions
{
    String8 baseline_driver;
    String8 candidate_driver;
    String8 clang;
    String8 database;
    String8 output;
    bool print_executable;
    bool valid;
};

typedef struct ClangAnalyzeBenchmarkMeasurement ClangAnalyzeBenchmarkMeasurement;
struct ClangAnalyzeBenchmarkMeasurement
{
    String8 phase;
    String8 role;
    u64 trial;
    u64 wall_us;
    u64 user_cpu_us;
    u64 system_cpu_us;
    u64 peak_memory_bytes;
    u64 cpu_status;
    u64 memory_status;
    u64 exit_status;
    u64 timed_out;
    u64 cleanup_failed;
    u64 capture_failed;
    u64 stdout_bytes;
    u64 stderr_bytes;
    bool attempted;
    bool complete;
};

BUSTER_GLOBAL_LOCAL ClangAnalyzeBenchmarkOptions clang_analyze_benchmark_parse(SliceString8 arguments)
{
    ClangAnalyzeBenchmarkOptions result = {.valid = true};
    for (u64 i = 0; result.valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        String8 inline_value = {0};
        bool has_value = build_argument_split_value(argument, &argument, &inline_value);
        if (string_equal(argument, S8("--print-executable")) && !has_value && !result.print_executable)
        {
            result.print_executable = true;
        }
        else if (string_equal(argument, S8("--baseline-driver")) && !result.baseline_driver.length && (has_value || i + 1 < arguments.length))
        {
            result.baseline_driver = has_value ? inline_value : arguments.pointer[++i];
        }
        else if (string_equal(argument, S8("--candidate-driver")) && !result.candidate_driver.length && (has_value || i + 1 < arguments.length))
        {
            result.candidate_driver = has_value ? inline_value : arguments.pointer[++i];
        }
        else if (string_equal(argument, S8("--clang")) && !result.clang.length && (has_value || i + 1 < arguments.length))
        {
            result.clang = has_value ? inline_value : arguments.pointer[++i];
        }
        else if (string_equal(argument, S8("--database")) && !result.database.length && (has_value || i + 1 < arguments.length))
        {
            result.database = has_value ? inline_value : arguments.pointer[++i];
        }
        else if (string_equal(argument, S8("--output")) && !result.output.length && (has_value || i + 1 < arguments.length))
        {
            result.output = has_value ? inline_value : arguments.pointer[++i];
        }
        else
        {
            result.valid = false;
        }
    }
    result.valid = result.valid && (result.print_executable ? arguments.length == 1 :
        result.baseline_driver.length && result.candidate_driver.length && result.clang.length &&
        result.database.length && result.output.length);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_benchmark_resource_status(u64 status)
{
    String8 result = S8("unknown");
    if (status == PROCESS_RESOURCE_OBSERVED) result = S8("observed");
    else if (status == PROCESS_RESOURCE_ERROR) result = S8("error");
    else if (status == PROCESS_RESOURCE_UNSUPPORTED) result = S8("unsupported");
    return result;
}

BUSTER_GLOBAL_LOCAL ClangAnalyzeBenchmarkMeasurement clang_analyze_benchmark_spawn(
    Arena* arena, String8 phase, String8 role, u64 trial, String8 driver, String8 database, String8 results,
    String8 clang, bool prepare, bool aggregate, u64 deadline_us, String8 output)
{
    ClangAnalyzeBenchmarkMeasurement measurement = {.phase = phase, .role = role, .trial = trial};
    String8 shards = string_format(arena, S8("{u64}"), BUSTER_ANALYZE_BENCHMARK_SHARDS);
    String8 jobs = string_format(arena, S8("{u64}"), BUSTER_ANALYZE_BENCHMARK_JOBS);
    String8 tu_timeout = string_format(arena, S8("{u64}"), BUSTER_ANALYZE_BENCHMARK_TU_TIMEOUT);
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    os_argument_builder_append(&builder, driver);
    os_argument_builder_append(&builder, S8("clang_analyze"));
    os_argument_builder_append(&builder, database);
    os_argument_builder_append(&builder, S8("--config"));
    os_argument_builder_append(&builder, S8("Release"));
    os_argument_builder_append(&builder, S8("--shards"));
    os_argument_builder_append(&builder, shards);
    os_argument_builder_append(&builder, S8("--jobs"));
    os_argument_builder_append(&builder, jobs);
    os_argument_builder_append(&builder, S8("--timeout"));
    os_argument_builder_append(&builder, tu_timeout);
    os_argument_builder_append(&builder, S8("--quiet"));
    os_argument_builder_append(&builder, S8("--clang"));
    os_argument_builder_append(&builder, clang);
    os_argument_builder_append(&builder, S8("--results"));
    os_argument_builder_append(&builder, results);
    if (prepare) os_argument_builder_append(&builder, S8("--prepare"));
    if (aggregate) os_argument_builder_append(&builder, S8("--aggregate"));
    SliceString8 command = os_argument_builder_flush(&builder);
    u64 start = os_now_microseconds();
    u64 remaining = deadline_us > start ? deadline_us - start : 1;
    // Full analyzer phases share the whole campaign deadline. A 16-minute
    // per-run cap would reject a valid inventory just over that duration;
    // the outer profile deadline still bounds the complete B,C,C,B sequence.
    u64 limit = prepare || aggregate ? BUSTER_ANALYZE_BENCHMARK_OTHER_LIMIT_US : remaining;
    if (remaining < limit) limit = remaining;
    ProcessSpawnResult spawn = os_process_spawn(command, (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){
        .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
        .use_process_environment = 1,
        .new_process_group = 1,
        .observe_resources = 1,
        .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = BUSTER_ANALYZE_BENCHMARK_CAPTURE_BYTES,
                                           [STANDARD_STREAM_ERROR] = BUSTER_ANALYZE_BENCHMARK_CAPTURE_BYTES},
                           .total = 2 * BUSTER_ANALYZE_BENCHMARK_CAPTURE_BYTES},
        .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
    });
    measurement.attempted = true;
    ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
    if (spawn.handle) wait = os_process_wait_deadline(arena, spawn, limit);
    measurement.wall_us = os_now_microseconds() - start;
    measurement.user_cpu_us = wait.resources.user_cpu_us;
    measurement.system_cpu_us = wait.resources.system_cpu_us;
    measurement.peak_memory_bytes = wait.resources.peak_memory_bytes;
    measurement.cpu_status = wait.resources.cpu_status;
    measurement.memory_status = wait.resources.memory_status;
    measurement.exit_status = wait.platform_status;
    measurement.timed_out = wait.timed_out;
    measurement.cleanup_failed = wait.process_tree_cleanup_failed || wait.process_group_reservation_retained || wait.process_group_ownership_lost;
    measurement.capture_failed = wait.capture_failed || wait.capture_limit_exceeded || wait.output_truncated;
    measurement.stdout_bytes = wait.observed_bytes[STANDARD_STREAM_OUTPUT];
    measurement.stderr_bytes = wait.observed_bytes[STANDARD_STREAM_ERROR];
    String8 stdout_path = string_format(arena, S8("{S8}/{S8}.stdout.log"), output, phase);
    String8 stderr_path = string_format(arena, S8("{S8}/{S8}.stderr.log"), output, phase);
    bool logs_written = file_write(stdout_path, BUSTER_SLICE_TO_BYTE_SLICE(wait.streams[STANDARD_STREAM_OUTPUT])) &&
                        file_write(stderr_path, BUSTER_SLICE_TO_BYTE_SLICE(wait.streams[STANDARD_STREAM_ERROR]));
    measurement.complete = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out && !measurement.cleanup_failed &&
        !measurement.capture_failed && logs_written && measurement.cpu_status == PROCESS_RESOURCE_OBSERVED &&
        measurement.memory_status == PROCESS_RESOURCE_OBSERVED;
    return measurement;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_benchmark_append_measurement(Arena* arena, String8List* rows,
                                                                     ClangAnalyzeBenchmarkMeasurement measurement)
{
    String8 row = string_format(arena,
        S8("{S8}\t{S8}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{S8}\t{S8}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{S8}\n"),
        measurement.phase, measurement.role, measurement.trial, measurement.wall_us, measurement.user_cpu_us,
        measurement.system_cpu_us, measurement.peak_memory_bytes,
        clang_analyze_benchmark_resource_status(measurement.cpu_status),
        clang_analyze_benchmark_resource_status(measurement.memory_status), measurement.exit_status,
        measurement.timed_out, measurement.cleanup_failed, measurement.capture_failed, measurement.stdout_bytes,
        measurement.stderr_bytes, measurement.complete ? S8("complete") : measurement.attempted ? S8("failed") : S8("not_run"));
    string8_list_push(arena, rows, row);
}

BUSTER_GLOBAL_LOCAL ClangAnalyzeBenchmarkMeasurement clang_analyze_benchmark_not_run(String8 phase, String8 role, u64 trial)
{
    ClangAnalyzeBenchmarkMeasurement measurement = {.phase = phase, .role = role, .trial = trial};
    return measurement;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_benchmark_write_profile(Arena* arena, String8List rows, String8 output)
{
    String8 table = string_join_arena(arena, string8_list_to_slice(arena, rows), true);
    String8 profile_path = string_format(arena, S8("{S8}/profile.tsv"), output);
    return file_write(profile_path, BUSTER_SLICE_TO_BYTE_SLICE(table));
}

BUSTER_GLOBAL_LOCAL ProcessResult clang_analyze_benchmark_main(Arena* arena, SliceString8 arguments)
{
    ClangAnalyzeBenchmarkOptions options = clang_analyze_benchmark_parse(arguments);
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (options.valid && options.print_executable)
    {
        String8 executable = os_path_absolute_lexical(arena, program_state->input.arguments.pointer[0], true);
        if (executable.length)
        {
            string_print(S8("BUSTER_ANALYZER_DRIVER {S8}\n"), executable);
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    else if (options.valid)
    {
        String8 baseline = os_path_absolute_lexical(arena, options.baseline_driver, true);
        String8 candidate = os_path_absolute_lexical(arena, options.candidate_driver, true);
        String8 clang = os_path_absolute_lexical(arena, options.clang, true);
        String8 database = os_path_absolute_lexical(arena, options.database, true);
        String8 output = os_path_absolute_lexical(arena, options.output, true);
        String8 order_roles[] = {S8("baseline"), S8("candidate"), S8("candidate"), S8("baseline")};
        String8 order_names[] = {S8("baseline-0"), S8("candidate-0"), S8("candidate-1"), S8("baseline-1")};
        String8List rows = {0};
        string8_list_push(arena, &rows, S8("phase\trole\ttrial\twall_us\tuser_cpu_us\tsystem_cpu_us\tpeak_rss_bytes\tcpu_status\tmemory_status\texit_status\ttimed_out\tcleanup_failed\tcapture_failed\tstdout_bytes\tstderr_bytes\tstate\n"));
        u64 budget_start = os_now_microseconds();
        u64 deadline = budget_start + BUSTER_ANALYZE_BENCHMARK_BUDGET_US;
        String8 prepare_roles[] = {S8("baseline"), S8("candidate")};
        bool success = true;
        bool preflight_complete = true;
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(prepare_roles); i += 1)
        {
            String8 role = prepare_roles[i];
            String8 driver = i ? candidate : baseline;
            String8 phase = string_format(arena, S8("prepare-{S8}"), role);
            String8 results = string_format(arena, S8("{S8}/{S8}"), output, phase);
            ClangAnalyzeBenchmarkMeasurement measurement = os_now_microseconds() < deadline ?
                clang_analyze_benchmark_spawn(arena, phase, role, 0, driver, database, results, clang, true, false, deadline, output) :
                clang_analyze_benchmark_not_run(phase, role, 0);
            clang_analyze_benchmark_append_measurement(arena, &rows, measurement);
            bool written = clang_analyze_benchmark_write_profile(arena, rows, output);
            preflight_complete = preflight_complete && measurement.complete;
            success = success && measurement.complete && written;
        }
        for (u64 trial = 0; trial < BUSTER_ANALYZE_BENCHMARK_RUNS; trial += 1)
        {
            String8 role = order_roles[trial];
            String8 driver = string_equal(role, S8("baseline")) ? baseline : candidate;
            String8 phase = order_names[trial];
            String8 results = string_format(arena, S8("{S8}/{S8}"), output, phase);
            String8 analysis_phase = string_format(arena, S8("analysis-{S8}"), phase);
            ClangAnalyzeBenchmarkMeasurement analysis = preflight_complete && os_now_microseconds() < deadline ?
                clang_analyze_benchmark_spawn(arena, analysis_phase, role, trial, driver, database, results,
                                              clang, false, false, deadline, output) :
                clang_analyze_benchmark_not_run(analysis_phase, role, trial);
            clang_analyze_benchmark_append_measurement(arena, &rows, analysis);
            bool analysis_written = clang_analyze_benchmark_write_profile(arena, rows, output);
            success = success && analysis.complete && analysis_written;
            String8 aggregate_phase = string_format(arena, S8("aggregate-{S8}"), phase);
            ClangAnalyzeBenchmarkMeasurement aggregate = preflight_complete && os_now_microseconds() < deadline ?
                clang_analyze_benchmark_spawn(arena, aggregate_phase, role, trial, driver, database, results,
                                              clang, false, true, deadline, output) :
                clang_analyze_benchmark_not_run(aggregate_phase, role, trial);
            clang_analyze_benchmark_append_measurement(arena, &rows, aggregate);
            bool aggregate_written = clang_analyze_benchmark_write_profile(arena, rows, output);
            success = success && aggregate.complete && aggregate_written;
        }
        bool written = clang_analyze_benchmark_write_profile(arena, rows, output);
        u64 elapsed = os_now_microseconds() - budget_start;
        string_print(S8("ANALYZE_BENCHMARK_PROFILE name=clang-analyze-full-v1 elapsed_us={u64} phases={u64} status={S8}\n"),
                     elapsed, rows.count - 1, success && written ? S8("pass") : S8("fail"));
        result = success && written ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    if (!options.valid)
    {
        string_print(S8("error: clang_analyze_benchmark --print-executable; or --baseline-driver path --candidate-driver path --clang path --database build-dir --output fresh-directory\n"));
    }
    return result;
}
