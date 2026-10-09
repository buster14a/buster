// Bounded sampling research runner; included by build.c, never an automatic
// main profile. Entry: compiler_profile_qualification_main. Map: *_parse
// rejects undeclared controls; *_run records every attempted/not-run series;
// *_self_test covers the parser and immutable candidate profiles.
// This runner does not infer qualification from successful process exits.
#include "compiler_profile_qualification_ledger.c"

#define BUSTER_SAMPLING_PACKET_LIMIT_US (60ull * 60ull * 1000000ull)
#define BUSTER_SAMPLING_PACKET_TRIALS 4
#define BUSTER_SAMPLING_CAPTURE_BYTES BUSTER_MB(16)

typedef struct CompilerSamplingProfile CompilerSamplingProfile;
struct CompilerSamplingProfile
{
    String8 name;
    u64 pairs;
    u64 timeout_seconds;
};

typedef struct CompilerSamplingOptions CompilerSamplingOptions;
struct CompilerSamplingOptions
{
    String8 profile;
    String8 family;
    String8 python;
    String8 lab;
    String8 baseline;
    String8 candidate;
    String8 source;
    String8 output;
    String8 base;
    String8 head;
    String8 protocol;
    u64 trials;
    bool plan;
    bool self_test;
    bool valid;
};

BUSTER_GLOBAL_LOCAL CompilerSamplingProfile compiler_sampling_profile(String8 name)
{
    CompilerSamplingProfile result = {0};
    if (string_equal(name, S8("compiler-main-40pairs-candidate-v1")))
    {
        result = (CompilerSamplingProfile){.name = name, .pairs = 40, .timeout_seconds = 300};
    }
    else if (string_equal(name, S8("compiler-main-80pairs-candidate-v1")))
    {
        result = (CompilerSamplingProfile){.name = name, .pairs = 80, .timeout_seconds = 540};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_revision_valid(String8 value)
{
    bool result = value.length == 40;
    for (u64 i = 0; result && i < value.length; i += 1)
    {
        u8 byte = value.pointer[i];
        result = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingOptions compiler_sampling_parse(SliceString8 arguments)
{
    CompilerSamplingOptions result = {.valid = true};
    String8 names[] = {S8("--profile"), S8("--family"), S8("--python"), S8("--lab"),
        S8("--baseline"), S8("--candidate"), S8("--repo-root"), S8("--output"),
        S8("--base"), S8("--head"), S8("--protocol")};
    String8* values[] = {&result.profile, &result.family, &result.python, &result.lab,
        &result.baseline, &result.candidate, &result.source, &result.output,
        &result.base, &result.head, &result.protocol};
    for (u64 i = 0; result.valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        if (string_equal(argument, S8("--plan")) && !result.plan)
        {
            result.plan = true;
        }
        else if (string_equal(argument, S8("--self-test")) && !result.self_test)
        {
            result.self_test = true;
        }
        else if (string_equal(argument, S8("--trials")) && !result.trials && i + 1 < arguments.length)
        {
            String8 count = arguments.pointer[++i];
            result.valid = count.length == 1 && count.pointer[0] >= '1' &&
                count.pointer[0] <= '0' + BUSTER_SAMPLING_PACKET_TRIALS;
            if (result.valid) result.trials = (u64)(count.pointer[0] - '0');
        }
        else
        {
            bool found = false;
            for (u64 j = 0; !found && j < BUSTER_ARRAY_LENGTH(names); j += 1)
            {
                if (string_equal(argument, names[j]) && !values[j]->length && i + 1 < arguments.length)
                {
                    *values[j] = arguments.pointer[++i];
                    found = values[j]->length != 0;
                }
            }
            result.valid = found;
        }
    }
    if (result.plan || result.self_test)
    {
        result.valid = result.valid && arguments.length == 1;
    }
    else
    {
        CompilerSamplingProfile profile = compiler_sampling_profile(result.profile);
        bool family = string_equal(result.family, S8("aa")) || string_equal(result.family, S8("ab1")) ||
            string_equal(result.family, S8("ab2"));
        result.valid = result.valid && profile.pairs && family && result.trials &&
            result.python.length && result.lab.length && result.baseline.length && result.candidate.length &&
            result.source.length && result.output.length && result.protocol.length &&
            compiler_sampling_revision_valid(result.base) && compiler_sampling_revision_valid(result.head);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_path_overlap(String8 left, String8 right)
{
    bool result = generate_path_within(left, right) || generate_path_within(right, left);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_output_valid(Arena* arena, String8 output, SliceString8 inputs)
{
    bool result = generate_path_kind(arena, output) == GENERATE_PATH_MISSING;
    for (u64 i = 0; result && i < inputs.length; i += 1)
    {
        result = !compiler_sampling_path_overlap(output, inputs.pointer[i]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_self_test(Arena* arena)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    CompilerSamplingProfile small = compiler_sampling_profile(S8("compiler-main-40pairs-candidate-v1"));
    CompilerSamplingProfile large = compiler_sampling_profile(S8("compiler-main-80pairs-candidate-v1"));
    bool good = small.pairs == 40 && small.timeout_seconds == 300 &&
        large.pairs == 80 && large.timeout_seconds == 540 &&
        !compiler_sampling_profile(S8("compiler-compare-v1")).pairs &&
        !compiler_sampling_profile(S8("main")).pairs;
    String8 plan[] = {S8("--plan")};
    String8 mixed[] = {S8("--plan"), S8("--trials"), S8("4")};
    String8 overflow[] = {S8("--trials"), S8("5")};
    String8 duplicate[] = {S8("--self-test"), S8("--self-test")};
    good = good && compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(plan)).valid &&
        !compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(mixed)).valid &&
        !compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(overflow)).valid &&
        !compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(duplicate)).valid &&
        compiler_sampling_revision_valid(S8("0123456789abcdef0123456789abcdef01234567")) &&
        !compiler_sampling_revision_valid(S8("0123456789abcdef0123456789abcdef0123456g")) &&
        compiler_sampling_path_overlap(S8("/tmp/output"), S8("/tmp/output/child")) &&
        !compiler_sampling_path_overlap(S8("/tmp/output"), S8("/tmp/output-other"));
    good = good && compiler_sampling_schedule_self_test(arena) == PROCESS_RESULT_SUCCESS;
    if (!good) result = PROCESS_RESULT_FAILED;
    string_print(S8("COMPILER_SAMPLING_SELF_TEST status={S8} routine_enabled=false\n"),
        good ? S8("pass") : S8("fail"));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_run(Arena* arena, CompilerSamplingOptions options)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    CompilerSamplingProfile profile = compiler_sampling_profile(options.profile);
    String8 baseline = os_path_absolute(arena, options.baseline, true);
    String8 candidate = os_path_absolute(arena, options.candidate, true);
    String8 source = os_path_absolute(arena, options.source, true);
    String8 lab = os_path_absolute(arena, options.lab, true);
    String8 python = os_path_absolute(arena, options.python, true);
    String8 protocol = os_path_absolute(arena, options.protocol, true);
    String8 output = os_path_absolute(arena, options.output, true);
    bool aa = string_equal(options.family, S8("aa"));
    if (aa) candidate = baseline;
    String8 cpu_info = BYTE_SLICE_TO_STRING(8, file_read(arena, S8("/proc/cpuinfo"), (FileReadOptions){.map_required = 0}));
    bool approved_host = string_contains(cpu_info, S8("AMD Ryzen 7 9700X"));
    String8 inputs[] = {baseline, candidate, source, lab, python, protocol};
    String8 baseline_digest = {0}, candidate_digest = {0}, lab_digest = {0}, protocol_digest = {0};
    bool paths_valid = baseline.length && candidate.length && source.length && lab.length && python.length &&
        protocol.length && output.length && compiler_sampling_output_valid(arena, output, (SliceString8)BUSTER_ARRAY_TO_SLICE(inputs));
    bool identities_valid = paths_valid && stage_object_sha256_file(arena, baseline, &baseline_digest) &&
        stage_object_sha256_file(arena, candidate, &candidate_digest) && stage_object_sha256_file(arena, lab, &lab_digest) &&
        stage_object_sha256_file(arena, protocol, &protocol_digest);
    if (approved_host && identities_valid)
    {
        OsDirectoryCreateResult directory = os_make_directory(output);
        bool success = !directory.error.v && directory.created && !directory.existing_directory;
        String8List rows = {0};
        String8 metadata = string_format(arena,
            S8("schema\tbuster-main-sampling-packet-v1\nprofile\t{S8}\nfamily\t{S8}\n"
               "pairs\t{u64}\ntrials\t{u64}\nbase\t{S8}\nhead\t{S8}\n"
               "baseline_sha256\t{S8}\ncandidate_sha256\t{S8}\nlab_sha256\t{S8}\n"
               "protocol_sha256\t{S8}\ncpu\t2\nwarmups\t1\nseed\t20261003\n"
               "floor_percent\t0.5\nfresh_copy\ttrue\nroutine_enabled\tfalse\n"
               "evidence_class\tunqualified-sampling-research\n"),
            profile.name, options.family, profile.pairs, options.trials, options.base, options.head,
            baseline_digest, candidate_digest, lab_digest, protocol_digest);
        String8 metadata_path = path_join(arena, output, S8("identity.tsv"));
        success = success && file_write(metadata_path, BUSTER_SLICE_TO_BYTE_SLICE(metadata));
        string8_list_push(arena, &rows,
            S8("trial\twall_us\tuser_cpu_us\tsystem_cpu_us\tpeak_rss_bytes\texit_status\ttimed_out\tcleanup_failed\tcapture_failed\tstate\n"));
        u64 started = os_now_microseconds();
        u64 deadline = started + BUSTER_SAMPLING_PACKET_LIMIT_US;
        bool continue_run = success;
        for (u64 trial = 0; trial < options.trials; trial += 1)
        {
            bool attempted = continue_run && os_now_microseconds() < deadline;
            bool complete = false;
            u64 elapsed = 0;
            ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
            if (attempted)
            {
                String8 trial_path = path_join(arena, output, string_format(arena, S8("trial-{u64}"), trial));
                OsArgumentBuilder builder = os_argument_builder_start(arena);
                String8 command[] = {python, S8("-B"), lab, S8("compare"), S8("--baseline"), baseline,
                    S8("--candidate"), candidate, S8("--repo-root"), source, S8("--cpu"), S8("2"),
                    S8("--output"), trial_path, S8("--pairs"), string_format(arena, S8("{u64}"), profile.pairs),
                    S8("--target-minutes"), S8("10"), S8("--warmups"), S8("1"), S8("--seed"), S8("20261003"),
                    S8("--min-effect"), S8("0.5")};
                for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(command); i += 1) os_argument_builder_append(&builder, command[i]);
                u64 trial_started = os_now_microseconds();
                u64 remaining = deadline > trial_started ? deadline - trial_started : 1;
                u64 timeout = profile.timeout_seconds * 1000000ull;
                if (remaining < timeout) timeout = remaining;
                ProcessSpawnResult spawn = os_process_spawn(os_argument_builder_flush(&builder), (SliceString8){0},
                    (SliceString8){0}, (ProcessSpawnOptions){
                        .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
                        .use_process_environment = 1, .new_process_group = 1, .observe_resources = 1,
                        .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = BUSTER_SAMPLING_CAPTURE_BYTES,
                            [STANDARD_STREAM_ERROR] = BUSTER_SAMPLING_CAPTURE_BYTES}, .total = 2 * BUSTER_SAMPLING_CAPTURE_BYTES},
                        .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
                    });
                if (spawn.handle) wait = os_process_wait_deadline(arena, spawn, timeout);
                elapsed = os_now_microseconds() - trial_started;
                bool logs = file_write(path_join(arena, output, string_format(arena, S8("trial-{u64}.stdout.log"), trial)),
                    BUSTER_SLICE_TO_BYTE_SLICE(wait.streams[STANDARD_STREAM_OUTPUT])) &&
                    file_write(path_join(arena, output, string_format(arena, S8("trial-{u64}.stderr.log"), trial)),
                    BUSTER_SLICE_TO_BYTE_SLICE(wait.streams[STANDARD_STREAM_ERROR]));
                complete = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out &&
                    !wait.process_tree_cleanup_failed && !wait.process_group_reservation_retained &&
                    !wait.process_group_ownership_lost && !wait.capture_failed && !wait.capture_limit_exceeded &&
                    !wait.output_truncated && logs;
            }
            bool cleanup_failed = wait.process_tree_cleanup_failed || wait.process_group_reservation_retained || wait.process_group_ownership_lost;
            bool capture_failed = wait.capture_failed || wait.capture_limit_exceeded || wait.output_truncated;
            string8_list_push(arena, &rows, string_format(arena,
                S8("{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{S8}\n"),
                trial, elapsed, wait.resources.user_cpu_us, wait.resources.system_cpu_us, wait.resources.peak_memory_bytes,
                (u64)wait.platform_status, (u64)wait.timed_out, (u64)cleanup_failed, (u64)capture_failed,
                complete ? S8("process-complete-unvalidated") : attempted ? S8("failed") : S8("not_run")));
            String8 table = string_join_arena(arena, string8_list_to_slice(arena, rows), true);
            bool written = file_write(path_join(arena, output, S8("attempts.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(table));
            success = success && complete && written;
            // An ordinary failed series is retained and does not stop unrelated
            // scheduled trials; lost cleanup ownership prevents another child.
            continue_run = written && !cleanup_failed && os_now_microseconds() < deadline;
        }
        String8 after_baseline = {0}, after_candidate = {0}, after_lab = {0}, after_protocol = {0};
        bool unchanged = stage_object_sha256_file(arena, baseline, &after_baseline) &&
            stage_object_sha256_file(arena, candidate, &after_candidate) && stage_object_sha256_file(arena, lab, &after_lab) &&
            stage_object_sha256_file(arena, protocol, &after_protocol) &&
            string_equal(baseline_digest, after_baseline) && string_equal(candidate_digest, after_candidate) &&
            string_equal(lab_digest, after_lab) && string_equal(protocol_digest, after_protocol);
        u64 wall_us = os_now_microseconds() - started;
        String8 packet = string_format(arena,
            S8("physical_packet_wall_us\t{u64}\nidentity_unchanged\t{S8}\n"
               "process_state\t{S8}\nqualification_state\tunvalidated\nqueue_delay\tunavailable\n"),
            wall_us, unchanged ? S8("true") : S8("false"), success && unchanged ? S8("complete") : S8("failed"));
        bool terminal = file_write(path_join(arena, output, S8("packet.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(packet));
        if (success && unchanged && terminal) result = PROCESS_RESULT_SUCCESS;
        string_print(S8("COMPILER_SAMPLING_PACKET profile={S8} trials={u64} wall_us={u64} state={S8} qualification=unvalidated\n"),
            profile.name, options.trials, wall_us, result == PROCESS_RESULT_SUCCESS ? S8("complete") : S8("failed"));
    }
    else
    {
        string_print(S8("error: sampling research requires observed AMD Ryzen 7 9700X, immutable readable inputs, and a new non-overlapping output directory\n"));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_profile_qualification_main(Arena* arena, SliceString8 arguments)
{
    CompilerSamplingOptions options = compiler_sampling_parse(arguments);
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (options.valid && options.self_test)
    {
        result = compiler_sampling_self_test(arena);
    }
    else if (options.valid && options.plan)
    {
        string_print(S8("COMPILER_SAMPLING_PLAN protocol=buster-compiler-main-sampling-qualification-v1 routine_enabled=false\n"
            "candidate=compiler-main-40pairs-candidate-v1 pairs=40 warmups=1 cpu=2 fresh_copy=true floor_percent=0.5\n"
            "candidate=compiler-main-80pairs-candidate-v1 pairs=80 warmups=1 cpu=2 fresh_copy=true floor_percent=0.5\n"
            "max_trials=4 packet_limit_seconds=3600 evidence_class=unqualified-sampling-research\n"));
        result = PROCESS_RESULT_SUCCESS;
    }
    else if (options.valid)
    {
        result = compiler_sampling_run(arena, options);
    }
    else
    {
        string_print(S8("error: compiler_profile_qualification --plan | --self-test | --profile NAME --family aa|ab1|ab2 "
            "--trials 1..4 --python PATH --lab PATH --baseline PATH --candidate PATH --repo-root PATH "
            "--output NEW_DIRECTORY --base SHA --head SHA --protocol PATH\n"));
    }
    return result;
}
