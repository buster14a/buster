// Bounded sampling research runner; included by build.c, never an automatic
// main profile. Entry: compiler_profile_qualification_main. Map: *_parse
// rejects undeclared controls; *_run records every attempted/not-run series;
// *_self_test covers the parser and immutable candidate profiles.
// This runner does not infer qualification from successful process exits.
#include "compiler_profile_qualification_ledger.c"
#include "compiler_profile_qualification_freeze.c"
#include "compiler_profile_qualification_admission.c"

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
    String8 phase;
    String8 packet_text;
    String8 ledger_root;
    String8 freeze;
    String8 freeze_sha256;
    String8 python;
    String8 lab;
    String8 baseline;
    String8 candidate;
    String8 source;
    String8 output;
    String8 base;
    String8 base_tree;
    String8 head;
    String8 candidate_revision;
    String8 campaign_parent;
    String8 allowlist;
    String8 facts;
    String8 history;
    String8 request;
    bool admit;
    bool owned_worker;
    String8 protocol;
    String8 driver;
    String8 closure;
    String8 closure_sha256;
    String8 prep_text;
    u64 packet;
    u64 prep_us;
    bool claim;
    bool plan;
    bool self_test;
    bool valid;
};

BUSTER_GLOBAL_LOCAL CompilerSamplingProfile compiler_sampling_profile(String8 name)
{
    CompilerSamplingProfile result = {0};
    if (string_equal(name, S8("compiler-compare-v1")))
    {
        result = (CompilerSamplingProfile){.name = name, .timeout_seconds = 720};
    }
    else if (string_equal(name, S8("compiler-main-40pairs-candidate-v1")))
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

BUSTER_GLOBAL_LOCAL bool compiler_sampling_unsigned(String8 text, u64* output)
{
    bool result = text.length && text.length <= 16 && (text.length == 1 || text.pointer[0] != '0');
    u64 value = 0;
    for (u64 i = 0; result && i < text.length; i += 1)
    {
        u8 byte = text.pointer[i];
        result = byte >= '0' && byte <= '9';
        if (result) value = value * 10 + (u64)(byte - '0');
    }
    if (result) *output = value;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingOptions compiler_sampling_parse(SliceString8 arguments)
{
    CompilerSamplingOptions result = {.valid = true};
    String8 names[] = {S8("--phase"), S8("--packet"), S8("--ledger-root"), S8("--freeze"), S8("--freeze-sha256"),
        S8("--python"), S8("--lab"), S8("--baseline"), S8("--candidate"), S8("--repo-root"), S8("--output"),
        S8("--base"), S8("--base-tree"), S8("--head"), S8("--protocol"), S8("--driver"), S8("--closure"),
        S8("--closure-sha256"), S8("--prep-us"), S8("--candidate-revision"), S8("--campaign-parent"), S8("--allowlist"), S8("--facts"), S8("--history"), S8("--request")};
    String8* values[] = {&result.phase, &result.packet_text, &result.ledger_root, &result.freeze, &result.freeze_sha256,
        &result.python, &result.lab, &result.baseline, &result.candidate, &result.source, &result.output,
        &result.base, &result.base_tree, &result.head, &result.protocol, &result.driver, &result.closure,
        &result.closure_sha256, &result.prep_text, &result.candidate_revision, &result.campaign_parent, &result.allowlist, &result.facts, &result.history, &result.request};
    for (u64 i = 0; result.valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        if (string_equal(argument, S8("--plan")) && !result.plan) result.plan = true;
        else if (string_equal(argument, S8("--self-test")) && !result.self_test) result.self_test = true;
        else if (string_equal(argument, S8("--claim")) && !result.claim) result.claim = true;
        else if (string_equal(argument, S8("--admit")) && !result.admit) result.admit = true;
        else if (string_equal(argument, S8("--owned-worker")) && !result.owned_worker) result.owned_worker = true;
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
    else if (result.admit)
    {
        result.valid = result.valid && !result.claim && !result.owned_worker && arguments.length == 13 &&
            result.allowlist.length && result.request.length && result.facts.length && result.history.length && result.freeze.length && result.output.length;
    }
    else
    {
        result.valid = result.valid && compiler_sampling_unsigned(result.packet_text, &result.packet) &&
            compiler_sampling_schedule(result.phase, result.packet).valid && result.ledger_root.length &&
            result.freeze.length && compiler_sampling_hex(result.freeze_sha256, 64) && result.output.length && result.source.length;
        if (!result.claim)
        {
            result.valid = result.valid && result.python.length && result.lab.length && result.baseline.length &&
                result.candidate.length && result.source.length && result.protocol.length && result.driver.length &&
                result.closure.length && compiler_sampling_hex(result.closure_sha256, 64) &&
                compiler_sampling_revision_valid(result.base) && compiler_sampling_revision_valid(result.base_tree) &&
                compiler_sampling_revision_valid(result.head) && compiler_sampling_revision_valid(result.candidate_revision) &&
                (string_equal(result.campaign_parent, S8("-")) || compiler_sampling_hex(result.campaign_parent, 64)) && compiler_sampling_unsigned(result.prep_text, &result.prep_us);
        }
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
        compiler_sampling_profile(S8("compiler-compare-v1")).timeout_seconds == 720 &&
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
    good = good && compiler_sampling_freeze_self_test(arena) && compiler_sampling_admission_self_test(arena);
    good = good && compiler_sampling_schedule_self_test(arena) == PROCESS_RESULT_SUCCESS;
    if (!good) result = PROCESS_RESULT_FAILED;
    string_print(S8("COMPILER_SAMPLING_SELF_TEST status={S8} routine_enabled=false\n"),
        good ? S8("pass") : S8("fail"));
    return result;
}


BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_admit(Arena* arena, CompilerSamplingOptions options)
{
    String8 allowlist = BYTE_SLICE_TO_STRING(8, file_read(arena, options.allowlist, (FileReadOptions){.map_required = 0}));
    String8 request = BYTE_SLICE_TO_STRING(8, file_read(arena, options.request, (FileReadOptions){.map_required = 0}));
    String8 facts = BYTE_SLICE_TO_STRING(8, file_read(arena, options.facts, (FileReadOptions){.map_required = 0}));
    String8 history = BYTE_SLICE_TO_STRING(8, file_read(arena, options.history, (FileReadOptions){.map_required = 0}));
    String8 freeze = BYTE_SLICE_TO_STRING(8, file_read(arena, options.freeze, (FileReadOptions){.map_required = 0}));
    CompilerSamplingAdmission admitted = compiler_sampling_admission_validate(arena, allowlist, request, facts, history, freeze);
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (admitted.valid)
    {
        CompilerSamplingFreeze frozen = compiler_sampling_freeze_parse(freeze);
        String8 candidate_revision = string_equal(admitted.family, S8("aa")) ? frozen.base :
            string_equal(admitted.family, S8("ab1")) ? frozen.ab1_revision : frozen.ab2_revision;
        String8 output = string_format(arena,
            S8("sampling_admitted=true\nsampling_phase={S8}\nsampling_packet={u64}\nsampling_family={S8}\n"
               "sampling_reservation_seconds={u64}\nsampling_timeout_minutes={u64}\nsampling_freeze_revision={S8}\n"
               "sampling_freeze_sha256={S8}\nsampling_campaign_parent={S8}\nsampling_protocol_sha256={S8}\n"
               "sampling_base={S8}\nsampling_base_tree={S8}\nsampling_candidate_revision={S8}\n"),
            admitted.phase, admitted.packet, admitted.family, admitted.reservation_seconds,
            admitted.reservation_seconds / 60, admitted.freeze_revision, admitted.freeze_sha256,
            admitted.campaign_parent, admitted.protocol_sha256, frozen.base, frozen.base_tree, candidate_revision);
        if (file_write(options.output, BUSTER_SLICE_TO_BYTE_SLICE(output))) result = PROCESS_RESULT_SUCCESS;
    }
    string_print(S8("COMPILER_SAMPLING_ADMISSION state={S8} reason={S8} qualification=unqualified routine_enabled=false\n"),
        result == PROCESS_RESULT_SUCCESS ? S8("admitted") : S8("refused"), admitted.reason);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_sampling_claim_record(Arena* arena, CompilerSamplingOptions options)
{
    String8 result = compiler_sampling_reservation(arena, options.freeze_sha256, options.phase, options.packet);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_observed_host(Arena* arena)
{
    String8 cpu_info = BYTE_SLICE_TO_STRING(8, file_read(arena, S8("/proc/cpuinfo"), (FileReadOptions){.map_required = 0}));
    bool result = string_contains(cpu_info, S8("AMD Ryzen 7 9700X"));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_claim(Arena* arena, CompilerSamplingOptions options)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    String8 output = os_path_absolute(arena, options.output, true);
    String8 root = os_path_absolute(arena, options.ledger_root, true);
    String8 freeze = os_path_absolute(arena, options.freeze, true);
    String8 actual = {0}, claim = {0};
    String8 source = os_path_absolute(arena, options.source, true);
    bool valid = output.length && root.length && freeze.length && source.length &&
        generate_path_kind(arena, source) == GENERATE_PATH_DIRECTORY && !compiler_sampling_path_overlap(root, source) &&
        !compiler_sampling_path_overlap(output, source) && compiler_sampling_observed_host(arena) &&
        stage_object_sha256_file(arena, freeze, &actual) && string_equal(actual, options.freeze_sha256) &&
        generate_path_kind(arena, output) == GENERATE_PATH_MISSING && !compiler_sampling_path_overlap(root, output) &&
        !compiler_sampling_path_overlap(root, freeze) &&
        compiler_sampling_ledger_claim(arena, root, options.freeze_sha256, options.phase, options.packet, &claim);
    if (valid)
    {
        OsDirectoryCreateResult created = os_make_directory_exclusive(output);
        String8 record = compiler_sampling_claim_record(arena, options);
        valid = !created.error.v && created.created && file_write(path_join(arena, output, S8("claim.tsv")),
            BUSTER_SLICE_TO_BYTE_SLICE(record));
        if (valid)
        {
            CompilerSamplingPacket packet = compiler_sampling_schedule(options.phase, options.packet);
            string_print(S8("COMPILER_SAMPLING_CLAIM phase={S8} packet={u64} reservation_seconds={u64} state=claimed-unvalidated\n"),
                options.phase, options.packet, packet.reservation_seconds);
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    if (result != PROCESS_RESULT_SUCCESS)
    {
        string_print(S8("error: immutable sampling packet claim refused; retain this attempt, do not retry the packet\n"));
    }
    return result;
}

typedef struct CompilerSamplingVerification CompilerSamplingVerification;
struct CompilerSamplingVerification { bool valid; bool cleanup_failed; };

BUSTER_GLOBAL_LOCAL CompilerSamplingVerification compiler_sampling_closure_verify(Arena* arena, CompilerSamplingOptions options,
                                                         String8 output, u64 trial, bool after, u64 deadline)
{
    String8 receipt = path_join(arena, output, string_format(arena, S8("closure-{u64}-{S8}.json"),
        trial, after ? S8("after") : S8("before")));
    String8 command[] = {options.driver, S8("compiler_closure"), S8("verify"), options.source, options.closure,
        options.base, options.base_tree, receipt, options.closure_sha256};
    u64 now = os_now_microseconds();
    CompilerSamplingVerification result = {0};
    if (now < deadline)
    {
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command),
            (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = 1, .new_process_group = 1});
        ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
        if (spawn.handle) wait = os_process_wait_deadline(arena, spawn, deadline - now);
        result.cleanup_failed = wait.process_tree_cleanup_failed || wait.process_group_reservation_retained || wait.process_group_ownership_lost;
        result.valid = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out && !result.cleanup_failed;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_run(Arena* arena, CompilerSamplingOptions options)
{
    u64 entry = os_now_microseconds();
    ProcessResult result = PROCESS_RESULT_FAILED;
    CompilerSamplingPacket schedule = compiler_sampling_schedule(options.phase, options.packet);
    String8 baseline = os_path_absolute(arena, options.baseline, true);
    String8 candidate = os_path_absolute(arena, options.candidate, true);
    String8 source = os_path_absolute(arena, options.source, true);
    String8 lab = os_path_absolute(arena, options.lab, true);
    String8 python = os_path_absolute(arena, options.python, true);
    String8 protocol = os_path_absolute(arena, options.protocol, true);
    String8 output = os_path_absolute(arena, options.output, true);
    String8 root = os_path_absolute(arena, options.ledger_root, true);
    String8 freeze = os_path_absolute(arena, options.freeze, true);
    String8 driver = os_path_absolute(arena, options.driver, true);
    String8 closure = os_path_absolute(arena, options.closure, true);
    bool aa = string_equal(schedule.family, S8("aa"));
    if (aa) candidate = baseline;
    options.source = source;
    options.driver = driver;
    options.closure = closure;
    String8 inputs[] = {baseline, candidate, source, lab, python, protocol, root, freeze, driver, closure};
    bool paths_valid = source.length && generate_path_kind(arena, source) == GENERATE_PATH_DIRECTORY &&
        output.length && generate_path_kind(arena, output) == GENERATE_PATH_DIRECTORY;
    for (u64 i = 0; paths_valid && i < BUSTER_ARRAY_LENGTH(inputs); i += 1)
    {
        paths_valid = inputs[i].length && !compiler_sampling_path_overlap(output, inputs[i]);
    }
    String8 baseline_digest = {0}, candidate_digest = {0}, lab_digest = {0}, protocol_digest = {0};
    String8 python_digest = {0}, driver_digest = {0}, freeze_digest = {0};
    String8 freeze_text = BYTE_SLICE_TO_STRING(8, file_read(arena, freeze, (FileReadOptions){.map_required = 0}));
    CompilerSamplingFreeze frozen = compiler_sampling_freeze_parse(freeze_text);
    bool identities_valid = paths_valid && stage_object_sha256_file(arena, baseline, &baseline_digest) &&
        stage_object_sha256_file(arena, candidate, &candidate_digest) && stage_object_sha256_file(arena, lab, &lab_digest) &&
        stage_object_sha256_file(arena, protocol, &protocol_digest) && stage_object_sha256_file(arena, python, &python_digest) &&
        stage_object_sha256_file(arena, driver, &driver_digest) && stage_object_sha256_file(arena, freeze, &freeze_digest) &&
        string_equal(freeze_digest, options.freeze_sha256);
    CompilerSamplingFreezeActual actual = {.phase = options.phase, .campaign_parent = options.campaign_parent,
        .base = options.base, .base_tree = options.base_tree, .request_head = frozen.request_head,
        .baseline_revision = options.base, .candidate_revision = aa ? options.base : options.candidate_revision,
        .protocol_sha256 = protocol_digest, .lab_sha256 = lab_digest, .python_sha256 = python_digest,
        .driver_sha256 = driver_digest, .closure_sha256 = options.closure_sha256,
        .baseline_sha256 = baseline_digest, .candidate_sha256 = candidate_digest};
    identities_valid = identities_valid && compiler_sampling_freeze_matches_family(frozen, actual, schedule.family);
    String8 expected_claim = compiler_sampling_claim_record(arena, options);
    String8 actual_claim = BYTE_SLICE_TO_STRING(8, file_read(arena, path_join(arena, output, S8("claim.tsv")),
        (FileReadOptions){.map_required = 0}));
    String8 persistent = path_join(arena, path_join(arena, root, options.freeze_sha256),
        string_format(arena, S8("{S8}-{u64}"), options.phase, options.packet));
    String8 actual_reservation = BYTE_SLICE_TO_STRING(8, file_read(arena, path_join(arena, persistent, S8("reservation.tsv")),
        (FileReadOptions){.map_required = 0}));
    bool claimed = string_equal(expected_claim, actual_claim) && string_equal(expected_claim, actual_reservation);
    if (compiler_sampling_observed_host(arena) && identities_valid && claimed)
    {
        OsDirectoryCreateResult execution = os_make_directory_exclusive(path_join(arena, persistent, S8("execution")));
        bool success = !execution.error.v && execution.created;
        if (success)
        {
        String8List rows = {0};
        String8 metadata = string_format(arena,
            S8("schema\tbuster-main-sampling-packet-v1\nphase\t{S8}\npacket\t{u64}\ncampaign\t{S8}\n"
               "reservation_seconds\t{u64}\nfamily\t{S8}\ntrials\t{u64}\nbase\t{S8}\nbase_tree\t{S8}\nrequest_head\t{S8}\n"
               "baseline_revision\t{S8}\ncandidate_revision\t{S8}\nbaseline_sha256\t{S8}\ncandidate_sha256\t{S8}\n"
               "lab_sha256\t{S8}\nprotocol_sha256\t{S8}\npython_sha256\t{S8}\ndriver_sha256\t{S8}\nclosure_sha256\t{S8}\n"
               "freeze_sha256\t{S8}\ncpu\t2\nwarmups\t1\nseed\t20261003\nfloor_percent\t0.5\nfresh_copy\ttrue\n"
               "routine_enabled\tfalse\nevidence_class\tunqualified-sampling-research\n"),
            options.phase, options.packet, options.freeze_sha256, schedule.reservation_seconds, schedule.family, schedule.count,
            options.base, options.base_tree, options.head, options.base, aa ? options.base : options.candidate_revision,
            baseline_digest, candidate_digest, lab_digest, protocol_digest, python_digest, driver_digest,
            options.closure_sha256, options.freeze_sha256);
        success = success && file_write(path_join(arena, output, S8("identity.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(metadata));
        string8_list_push(arena, &rows, S8("trial\tprofile\tfamily\tordinal\tpairs\twall_us\tuser_cpu_us\tsystem_cpu_us\tpeak_rss_bytes\t"
            "cpu_status\tmemory_status\texit_status\ttimed_out\tcleanup_failed\tcapture_failed\tclosure_before\tclosure_after\tstate\n"));
        u64 allocation = schedule.reservation_seconds * 1000000ull;
        u64 remaining = allocation > options.prep_us ? allocation - options.prep_us : 0;
        // Keep two minutes for bounded process-group cleanup, final inventories and export.
        u64 finish_reserve = 120ull * 1000000ull;
        u64 deadline = entry + (remaining > finish_reserve ? remaining - finish_reserve : 0);
        bool continue_run = success && remaining;
        bool cleanup_uncertain = false;
        for (u64 trial = 0; trial < schedule.count; trial += 1)
        {
            CompilerSamplingSlot slot = schedule.slots[trial];
            CompilerSamplingProfile profile = compiler_sampling_profile(slot.profile);
            bool attempted = continue_run && os_now_microseconds() < deadline;
            bool complete = false, closure_before = false, closure_after = false, verifier_cleanup_failed = false;
            u64 elapsed = 0;
            ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
            if (attempted)
            {
                CompilerSamplingVerification before = compiler_sampling_closure_verify(arena, options, output, trial, false, deadline);
                closure_before = before.valid;
                verifier_cleanup_failed = before.cleanup_failed;
                String8 trial_path = path_join(arena, output, string_format(arena, S8("trial-{u64}"), trial));
                OsArgumentBuilder builder = os_argument_builder_start(arena);
                String8 command[] = {python, S8("-B"), lab, S8("compare"), S8("--baseline"), baseline,
                    S8("--candidate"), candidate, S8("--repo-root"), source, S8("--cpu"), S8("2"),
                    S8("--output"), trial_path, S8("--target-minutes"), S8("10"),
                    S8("--warmups"), S8("1"), S8("--seed"), S8("20261003"), S8("--min-effect"), S8("0.5")};
                for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(command); i += 1) os_argument_builder_append(&builder, command[i]);
                if (profile.pairs)
                {
                    os_argument_builder_append(&builder, S8("--pairs"));
                    os_argument_builder_append(&builder, string_format(arena, S8("{u64}"), profile.pairs));
                }
                u64 trial_started = os_now_microseconds();
                u64 left = deadline > trial_started ? deadline - trial_started : 1;
                u64 timeout = profile.timeout_seconds * 1000000ull;
                if (left < timeout) timeout = left;
                ProcessSpawnResult spawn = {0};
                if (closure_before)
                {
                    spawn = os_process_spawn(os_argument_builder_flush(&builder), (SliceString8){0}, (SliceString8){0},
                        (ProcessSpawnOptions){.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
                            .use_process_environment = 1, .new_process_group = 1, .observe_resources = 1,
                            .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = BUSTER_SAMPLING_CAPTURE_BYTES,
                                [STANDARD_STREAM_ERROR] = BUSTER_SAMPLING_CAPTURE_BYTES}, .total = 2 * BUSTER_SAMPLING_CAPTURE_BYTES},
                            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL});
                }
                if (spawn.handle) wait = os_process_wait_deadline(arena, spawn, timeout);
                elapsed = os_now_microseconds() - trial_started;
                bool cleanup = !wait.process_tree_cleanup_failed && !wait.process_group_reservation_retained &&
                    !wait.process_group_ownership_lost;
                if (cleanup && !verifier_cleanup_failed)
                {
                    CompilerSamplingVerification after = compiler_sampling_closure_verify(arena, options, output, trial, true, deadline);
                    closure_after = after.valid;
                    verifier_cleanup_failed = after.cleanup_failed;
                }
                bool logs = file_write(path_join(arena, output, string_format(arena, S8("trial-{u64}.stdout.log"), trial)),
                    BUSTER_SLICE_TO_BYTE_SLICE(wait.streams[STANDARD_STREAM_OUTPUT])) &&
                    file_write(path_join(arena, output, string_format(arena, S8("trial-{u64}.stderr.log"), trial)),
                    BUSTER_SLICE_TO_BYTE_SLICE(wait.streams[STANDARD_STREAM_ERROR]));
                complete = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out && cleanup &&
                    !wait.capture_failed && !wait.capture_limit_exceeded && !wait.output_truncated && logs &&
                    closure_before && closure_after && wait.resources.cpu_status == PROCESS_RESOURCE_OBSERVED &&
                    wait.resources.memory_status == PROCESS_RESOURCE_OBSERVED;
            }
            bool cleanup_failed = verifier_cleanup_failed || wait.process_tree_cleanup_failed || wait.process_group_reservation_retained || wait.process_group_ownership_lost;
            cleanup_uncertain = cleanup_uncertain || cleanup_failed;
            bool capture_failed = wait.capture_failed || wait.capture_limit_exceeded || wait.output_truncated;
            string8_list_push(arena, &rows, string_format(arena,
                S8("{u64}\t{S8}\t{S8}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t"
                   "{u64}\t{u64}\t{u64}\t{u64}\t{S8}\t{S8}\t{S8}\n"),
                trial, slot.profile, schedule.family, slot.ordinal, profile.pairs, elapsed,
                wait.resources.user_cpu_us, wait.resources.system_cpu_us, wait.resources.peak_memory_bytes,
                (u64)wait.resources.cpu_status, (u64)wait.resources.memory_status, (u64)wait.platform_status,
                (u64)wait.timed_out, (u64)cleanup_failed, (u64)capture_failed, closure_before ? options.closure_sha256 : S8("-"),
                closure_after ? options.closure_sha256 : S8("-"),
                complete ? S8("process-complete-unvalidated") : attempted ? S8("failed") : S8("not_run")));
            String8 table = string_join_arena(arena, string8_list_to_slice(arena, rows), true);
            bool written = file_write(path_join(arena, output, S8("attempts.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(table));
            success = success && complete && written;
            continue_run = written && !cleanup_failed && os_now_microseconds() < deadline;
        }
        String8 after_baseline = {0}, after_candidate = {0}, after_lab = {0}, after_protocol = {0};
        String8 after_python = {0}, after_driver = {0}, after_freeze = {0};
        bool unchanged = stage_object_sha256_file(arena, baseline, &after_baseline) &&
            stage_object_sha256_file(arena, candidate, &after_candidate) && stage_object_sha256_file(arena, lab, &after_lab) &&
            stage_object_sha256_file(arena, protocol, &after_protocol) && stage_object_sha256_file(arena, python, &after_python) &&
            stage_object_sha256_file(arena, driver, &after_driver) && stage_object_sha256_file(arena, freeze, &after_freeze) &&
            string_equal(baseline_digest, after_baseline) && string_equal(candidate_digest, after_candidate) &&
            string_equal(lab_digest, after_lab) && string_equal(protocol_digest, after_protocol) &&
            string_equal(python_digest, after_python) && string_equal(driver_digest, after_driver) && string_equal(freeze_digest, after_freeze);
        u64 wall_us = options.prep_us + os_now_microseconds() - entry;
        bool within_budget = wall_us <= allocation;
        // A physical overrun or lost cleanup ownership permanently exhausts this campaign.
        // Missing/tampered terminal accounting also refuses subsequent claims.
        if (!within_budget || cleanup_uncertain)
        {
            file_write(path_join(arena, path_join(arena, root, options.freeze_sha256), S8("exhausted.tsv")),
                BUSTER_SLICE_TO_BYTE_SLICE(S8("state\texhausted\nreason\tpacket-overrun-or-cleanup-ownership\n")));
        }
        String8 packet = string_format(arena,
            S8("physical_packet_wall_us\t{u64}\nprep_us\t{u64}\ncaptured_input_files_unchanged\t{S8}\n"
               "within_reservation\t{S8}\nprocess_state\t{S8}\nqualification_state\tunvalidated\nqueue_delay\tunavailable\n"),
            wall_us, options.prep_us, unchanged ? S8("true") : S8("false"), within_budget ? S8("true") : S8("false"),
            success && unchanged && within_budget ? S8("complete") : S8("failed"));
        bool terminal = file_write(path_join(arena, output, S8("packet.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(packet)) &&
            file_write(path_join(arena, persistent, S8("terminal.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(packet));
        if (success && unchanged && within_budget && terminal) result = PROCESS_RESULT_SUCCESS;
        string_print(S8("COMPILER_SAMPLING_PACKET phase={S8} packet={u64} streams={u64} wall_us={u64} state={S8} qualification=unvalidated\n"),
            options.phase, options.packet, schedule.count, wall_us, result == PROCESS_RESULT_SUCCESS ? S8("complete") : S8("failed"));
        }
        else
        {
            string_print(S8("error: sampling packet already executed or execution claim failed; old evidence preserved\n"));
        }
    }
    else
    {
        string_print(S8("error: sampling packet requires approved host, matching immutable claim/freeze, full closure and readable pinned inputs\n"));
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
        string_print(S8("COMPILER_SAMPLING_PLAN protocol=buster-compiler-main-sampling-qualification-v1 routine_enabled=false\n"));
        String8 phases[] = {S8("pilot"), S8("confirm")};
        for (u64 phase = 0; phase < BUSTER_ARRAY_LENGTH(phases); phase += 1)
        {
            u64 count = phase ? 40 : 3;
            for (u64 packet = 0; packet < count; packet += 1)
            {
                CompilerSamplingPacket schedule = compiler_sampling_schedule(phases[phase], packet);
                for (u64 slot = 0; slot < schedule.count; slot += 1)
                {
                    string_print(S8("{S8}\t{u64}\t{S8}\t{u64}\t{S8}\t{u64}\t{u64}\n"),
                        phases[phase], packet, schedule.family, slot, schedule.slots[slot].profile,
                        schedule.slots[slot].ordinal, schedule.reservation_seconds);
                }
            }
        }
        result = PROCESS_RESULT_SUCCESS;
    }
    else if (options.valid && options.admit)
    {
        result = compiler_sampling_admit(arena, options);
    }
    else if (options.valid && options.claim)
    {
        result = compiler_sampling_claim(arena, options);
    }
    else if (options.valid)
    {
        result = compiler_sampling_run(arena, options);
    }
    else
    {
        string_print(S8("error: compiler_profile_qualification --plan | --self-test | [--claim] --phase pilot|confirm --packet N "
            "--ledger-root PATH --freeze PATH --freeze-sha256 SHA256 --output PATH; execution also requires "
            "--python PATH --lab PATH --baseline PATH --candidate PATH --repo-root PATH --base SHA --base-tree SHA "
            "--head SHA --protocol PATH --driver PATH --closure PATH --closure-sha256 SHA256 --prep-us N\n"));
    }
    return result;
}
