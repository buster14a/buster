// Bounded sampling research runner; included by build.c, never an automatic
// main profile. Entry: compiler_profile_qualification_main. Map: *_parse
// rejects undeclared controls; *_run records every attempted/not-run series;
// *_self_test covers the parser and immutable candidate profiles.
// This runner does not infer qualification from successful process exits.
#include "compiler_profile_qualification_ledger.c"
#include "compiler_profile_qualification_freeze.c"
#include "compiler_profile_qualification_admission.c"
#include "compiler_preparation_qualification_admission.c"
#include "compiler_closure_utility_admission.c"
#include "compiler_experiment_supervisor.c"

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
    String8 parent_freeze;
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
    String8 trusted_revision;
    String8 campaign_parent;
    String8 campaign_parent_revision;
    String8 allowlist;
    String8 facts;
    String8 history;
    String8 request;
    bool admit;
    bool execute;
    bool owned_worker;
    String8 acquisition_plan;
    String8 cleanup_root;
    String8 trusted_root;
    String8 protocol;
    String8 prepared;
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
        S8("--closure-sha256"), S8("--prep-us"), S8("--candidate-revision"), S8("--campaign-parent"), S8("--trusted-revision"), S8("--allowlist"), S8("--facts"), S8("--history"), S8("--request"), S8("--parent-freeze"), S8("--campaign-parent-revision"), S8("--prepared"), S8("--acquisition-plan"), S8("--cleanup-root"), S8("--trusted-root"), S8("--evidence")};
    String8* values[] = {&result.phase, &result.packet_text, &result.ledger_root, &result.freeze, &result.freeze_sha256,
        &result.python, &result.lab, &result.baseline, &result.candidate, &result.source, &result.output,
        &result.base, &result.base_tree, &result.head, &result.protocol, &result.driver, &result.closure,
        &result.closure_sha256, &result.prep_text, &result.candidate_revision, &result.campaign_parent, &result.trusted_revision, &result.allowlist, &result.facts, &result.history, &result.request, &result.parent_freeze, &result.campaign_parent_revision, &result.prepared, &result.acquisition_plan, &result.cleanup_root, &result.trusted_root, &result.output};
    for (u64 i = 0; result.valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        if (string_equal(argument, S8("--plan")) && !result.plan) result.plan = true;
        else if (string_equal(argument, S8("--self-test")) && !result.self_test) result.self_test = true;
        else if (string_equal(argument, S8("--claim")) && !result.claim) result.claim = true;
        else if (string_equal(argument, S8("--admit")) && !result.admit) result.admit = true;
        else if (string_equal(argument, S8("--execute")) && !result.execute) result.execute = true;
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
    else if (result.execute)
    {
        result.valid = result.valid && !result.claim && !result.admit &&
            arguments.length == (result.owned_worker ? 12 : 11) &&
            compiler_sampling_unsigned(result.packet_text, &result.packet) &&
            compiler_sampling_schedule(result.phase, result.packet).valid &&
            result.trusted_root.length && result.cleanup_root.length && result.output.length;
    }
    else if (result.admit)
    {
        result.valid = result.valid && !result.claim && !result.owned_worker && arguments.length == 15 &&
            result.parent_freeze.length && result.allowlist.length && result.request.length && result.facts.length && result.history.length && result.freeze.length && result.output.length;
    }
    else
    {
        result.valid = result.valid && compiler_sampling_unsigned(result.packet_text, &result.packet) &&
            compiler_sampling_schedule(result.phase, result.packet).valid && result.ledger_root.length &&
            result.freeze.length && compiler_sampling_hex(result.freeze_sha256, 64) && result.output.length && result.source.length;
        if (!result.claim)
        {
            result.valid = result.valid && result.python.length && result.lab.length && result.baseline.length &&
                result.candidate.length && result.source.length && result.protocol.length && result.prepared.length && result.driver.length &&
                result.closure.length && compiler_sampling_hex(result.closure_sha256, 64) &&
                compiler_sampling_revision_valid(result.base) && compiler_sampling_revision_valid(result.base_tree) &&
                compiler_sampling_revision_valid(result.head) && compiler_sampling_revision_valid(result.candidate_revision) && compiler_sampling_revision_valid(result.trusted_revision) &&
                compiler_sampling_hex(result.campaign_parent, 64) && compiler_sampling_hex(result.campaign_parent_revision, 40) && compiler_sampling_unsigned(result.prep_text, &result.prep_us);
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

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_self_test(Arena* arena);
BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_self_test(Arena* arena);

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
    String8 execute[] = {S8("--execute"), S8("--phase"), S8("acquire"), S8("--packet"), S8("0"),
        S8("--trusted-root"), S8("/trusted"), S8("--cleanup-root"), S8("/tmp"), S8("--evidence"), S8("/tmp/evidence")};
    String8 execute_duplicate[] = {S8("--execute"), S8("--phase"), S8("acquire"), S8("--packet"), S8("0"),
        S8("--trusted-root"), S8("/trusted"), S8("--cleanup-root"), S8("/tmp"), S8("--evidence"), S8("/tmp/evidence"),
        S8("--output"), S8("/tmp/other")};
    good = good && compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(plan)).valid &&
        !compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(mixed)).valid &&
        !compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(overflow)).valid &&
        !compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(duplicate)).valid &&
        compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(execute)).valid &&
        !compiler_sampling_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(execute_duplicate)).valid &&
        compiler_sampling_revision_valid(S8("0123456789abcdef0123456789abcdef01234567")) &&
        !compiler_sampling_revision_valid(S8("0123456789abcdef0123456789abcdef0123456g")) &&
        compiler_sampling_path_overlap(S8("/tmp/output"), S8("/tmp/output/child")) &&
        !compiler_sampling_path_overlap(S8("/tmp/output"), S8("/tmp/output-other"));
    good = good && compiler_experiment_supervisor_self_test(arena);
    good = good && compiler_sampling_freeze_self_test(arena) && compiler_sampling_admission_self_test(arena);
    good = good && compiler_preparation_admission_self_test(arena);
    good = good && compiler_sampling_schedule_self_test(arena) == PROCESS_RESULT_SUCCESS;
    good = good && compiler_sampling_controller_self_test(arena);
    good = good && compiler_preparation_controller_self_test(arena);
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
    String8 parent_freeze = BYTE_SLICE_TO_STRING(8, file_read(arena, options.parent_freeze, (FileReadOptions){.map_required = 0}));
    CompilerSamplingAdmission admitted = compiler_sampling_admission_validate(arena, allowlist, request, facts, history, freeze, parent_freeze);
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (admitted.valid)
    {
        CompilerSamplingFreeze frozen = compiler_sampling_freeze_parse(freeze);
        CompilerSamplingAcquisitionPlan acquisition = compiler_sampling_acquisition_plan_parse(freeze);
        String8 base = acquisition.valid ? acquisition.base : frozen.base;
        String8 base_tree = acquisition.valid ? acquisition.base_tree : frozen.base_tree;
        String8 candidate_revision = acquisition.valid ? acquisition.ab1_revision : string_equal(admitted.family, S8("aa")) ? frozen.base :
            string_equal(admitted.family, S8("ab1")) ? frozen.ab1_revision : frozen.ab2_revision;
        String8 output = string_format(arena,
            S8("sampling_admitted=true\nsampling_phase={S8}\nsampling_packet={u64}\nsampling_family={S8}\n"
               "sampling_reservation_seconds={u64}\nsampling_timeout_minutes={u64}\nsampling_freeze_revision={S8}\n"
               "sampling_freeze_sha256={S8}\nsampling_campaign_parent={S8}\nsampling_parent_freeze_revision={S8}\nsampling_protocol_sha256={S8}\n"
               "sampling_base={S8}\nsampling_base_tree={S8}\nsampling_candidate_revision={S8}\nsampling_trusted_revision={S8}\n"),
            admitted.phase, admitted.packet, admitted.family, admitted.reservation_seconds,
            admitted.reservation_seconds / 60, admitted.freeze_revision, admitted.freeze_sha256,
            admitted.campaign_parent, admitted.parent_freeze_revision, admitted.protocol_sha256, base, base_tree, candidate_revision, admitted.trusted_revision);
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


BUSTER_GLOBAL_LOCAL bool compiler_sampling_owned_environment(Arena* arena, SliceString8* keys, SliceString8* values)
{
#if BUSTER_LINUX
    pid_t group = getpgrp();
    *keys = program_state->input.environment_keys;
    *values = program_state->input.environment_values;
    return group > 1 && group == getpid() && group != getsid(0);
#else
    return false;
#endif
}

typedef struct CompilerSamplingVerification CompilerSamplingVerification;
struct CompilerSamplingVerification { bool valid; bool cleanup_failed; };


#if BUSTER_LINUX && !BUSTER_ANDROID
BUSTER_GLOBAL_LOCAL ProcessControlAtomic compiler_sampling_cancel_signal;
BUSTER_GLOBAL_LOCAL ProcessControlAtomic compiler_sampling_cancel_escalated;
BUSTER_GLOBAL_LOCAL void compiler_sampling_cancel_handler(int signal)
{
    if (!process_control_atomic_set_if_zero(&compiler_sampling_cancel_signal, (u64)signal))
    {
        process_control_atomic_store(&compiler_sampling_cancel_escalated, 1);
    }
}
#endif

typedef struct CompilerSamplingSignalScope CompilerSamplingSignalScope;
struct CompilerSamplingSignalScope
{
#if BUSTER_LINUX && !BUSTER_ANDROID
    struct sigaction old_term;
    struct sigaction old_int;
#endif
    bool active;
};

BUSTER_GLOBAL_LOCAL bool compiler_sampling_signals_begin(CompilerSamplingSignalScope* scope)
{
#if BUSTER_LINUX && !BUSTER_ANDROID
    struct sigaction handler = {0};
    handler.sa_handler = compiler_sampling_cancel_handler;
    sigemptyset(&handler.sa_mask);
    process_control_atomic_store(&compiler_sampling_cancel_signal, 0);
    process_control_atomic_store(&compiler_sampling_cancel_escalated, 0);
    bool term = sigaction(SIGTERM, &handler, &scope->old_term) == 0;
    bool result = term && sigaction(SIGINT, &handler, &scope->old_int) == 0;
    if (!result && term) sigaction(SIGTERM, &scope->old_term, 0);
    scope->active = result;
    return result;
#else
    return false;
#endif
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_signals_end(CompilerSamplingSignalScope* scope)
{
#if BUSTER_LINUX && !BUSTER_ANDROID
    bool result = scope->active && sigaction(SIGTERM, &scope->old_term, 0) == 0 &&
        sigaction(SIGINT, &scope->old_int, 0) == 0;
    if (result) scope->active = false;
    return result;
#else
    return false;
#endif
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_supervision_receipt(Arena* arena, String8 path,
    CompilerExperimentSupervisor supervisor, bool proven, u64 wall_us)
{
    String8 receipt = string_format(arena,
        S8("schema\tbuster-native-qualification-supervisor-v1\ncleanup_proven\t{S8}\nwall_us\t{u64}\n"
           "adoption_waves\t{u64}\nadopted_signalled\t{u64}\nadopted_reaped\t{u64}\n"),
        proven ? S8("true") : S8("false"), wall_us, supervisor.waves, supervisor.signalled, supervisor.reaped);
    return file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(receipt));
}

BUSTER_GLOBAL_LOCAL CompilerSamplingVerification compiler_sampling_closure_verify(Arena* arena, CompilerSamplingOptions options,
                                                         String8 output, u64 trial, bool after, u64 deadline)
{
    String8 receipt = path_join(arena, output, string_format(arena, S8("closure-{u64}-{S8}.json"),
        trial, after ? S8("after") : S8("before")));
    String8 command[] = {options.driver, S8("compiler_closure"), S8("verify"), options.source, options.closure,
        options.base, options.base_tree, receipt, options.closure_sha256};
    u64 now = os_now_microseconds();
    CompilerSamplingVerification result = {0};
    SliceString8 keys = {0}, values = {0};
    bool owned = compiler_sampling_owned_environment(arena, &keys, &values);
    CompilerExperimentSupervisor supervisor = {0};
    bool contained = owned && compiler_experiment_supervisor_begin(arena, &supervisor);
    result.cleanup_failed = !contained;
    if (contained && now < deadline)
    {
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command),
            keys, values, (ProcessSpawnOptions){.search_path = 1, .new_process_group = 1});
        ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
        if (spawn.handle) wait = os_process_wait_deadline(arena, spawn, deadline - now);
        result.cleanup_failed = wait.process_tree_cleanup_failed || wait.process_group_reservation_retained || wait.process_group_ownership_lost;
        result.valid = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out && !result.cleanup_failed;
    }
    if (contained)
    {
        bool ended = compiler_experiment_supervisor_end(arena, &supervisor);
        result.cleanup_failed = result.cleanup_failed || !ended;
        bool recorded = compiler_sampling_supervision_receipt(arena,
            path_join(arena, output, string_format(arena, S8("closure-{u64}-{S8}-supervision.tsv"), trial, after ? S8("after") : S8("before"))),
            supervisor, ended, os_now_microseconds() - now);
        result.valid = result.valid && !result.cleanup_failed && recorded && !supervisor.signalled && !supervisor.reaped;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL FileStats compiler_sampling_binary_stats(String8 path)
{
    FileStats result = {0};
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){0}, (OsFileAccess){.read = 1},
        (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1});
    if (file)
    {
        result = os_file_get_stats(file, (FileStatsOptions){.size = 1, .identity = 1});
        os_file_close(file);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_run_internal(Arena* arena, CompilerSamplingOptions options, bool fixture_host)
{
    u64 entry = os_now_microseconds();
    SliceString8 child_keys = {0}, child_values = {0};
    bool owned = compiler_sampling_owned_environment(arena, &child_keys, &child_values);
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
    String8 prepared = os_path_absolute(arena, options.prepared, true);
    bool aa = string_equal(schedule.family, S8("aa"));
    if (aa) candidate = baseline;
    options.source = source;
    options.driver = driver;
    options.closure = closure;
    String8 inputs[] = {baseline, candidate, source, lab, python, protocol, prepared, root, freeze, driver, closure};
    bool paths_valid = source.length && generate_path_kind(arena, source) == GENERATE_PATH_DIRECTORY &&
        output.length && generate_path_kind(arena, output) == GENERATE_PATH_DIRECTORY;
    for (u64 i = 0; paths_valid && i < BUSTER_ARRAY_LENGTH(inputs); i += 1)
    {
        paths_valid = inputs[i].length && !compiler_sampling_path_overlap(output, inputs[i]);
    }
    String8 baseline_digest = {0}, candidate_digest = {0}, lab_digest = {0}, protocol_digest = {0};
    String8 python_digest = {0}, driver_digest = {0}, freeze_digest = {0}, prepared_digest = {0};
    FileStats baseline_stats = compiler_sampling_binary_stats(baseline), candidate_stats = compiler_sampling_binary_stats(candidate);
    String8 freeze_text = BYTE_SLICE_TO_STRING(8, file_read(arena, freeze, (FileReadOptions){.map_required = 0}));
    CompilerSamplingFreeze frozen = compiler_sampling_freeze_parse(freeze_text);
    bool identities_valid = paths_valid && stage_object_sha256_file(arena, baseline, &baseline_digest) &&
        stage_object_sha256_file(arena, candidate, &candidate_digest) && stage_object_sha256_file(arena, lab, &lab_digest) &&
        stage_object_sha256_file(arena, protocol, &protocol_digest) && stage_object_sha256_file(arena, python, &python_digest) &&
        stage_object_sha256_file(arena, driver, &driver_digest) && stage_object_sha256_file(arena, freeze, &freeze_digest) &&
        stage_object_sha256_file(arena, prepared, &prepared_digest) && baseline_stats.valid && candidate_stats.valid &&
        baseline_stats.kind == OS_FILE_KIND_REGULAR && candidate_stats.kind == OS_FILE_KIND_REGULAR && baseline_stats.size && candidate_stats.size &&
        string_equal(freeze_digest, options.freeze_sha256);
    CompilerSamplingFreezeActual actual = {.phase = options.phase, .campaign_parent = options.campaign_parent,
        .campaign_parent_revision = options.campaign_parent_revision,
        .base = options.base, .base_tree = options.base_tree, .request_head = frozen.request_head,
        .trusted_revision = options.trusted_revision, .baseline_revision = options.base, .candidate_revision = aa ? options.base : options.candidate_revision,
        .protocol_sha256 = protocol_digest, .lab_sha256 = lab_digest, .python_sha256 = python_digest,
        .driver_sha256 = driver_digest, .closure_sha256 = options.closure_sha256, .prepared_sha256 = prepared_digest,
        .baseline_bytes = string_format(arena, S8("{u64}"), baseline_stats.size),
        .candidate_bytes = string_format(arena, S8("{u64}"), candidate_stats.size),
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
    if (owned && (fixture_host || compiler_sampling_observed_host(arena)) && identities_valid && claimed)
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
                   "freeze_sha256\t{S8}\ntrusted_revision\t{S8}\nprepared_sha256\t{S8}\nbaseline_bytes\t{u64}\ncandidate_bytes\t{u64}\ncpu\t2\nwarmups\t1\nseed\t20261003\nfloor_percent\t0.5\nfresh_copy\ttrue\n"
                   "routine_enabled\tfalse\nevidence_class\tunqualified-sampling-research\n"),
                options.phase, options.packet, options.freeze_sha256, schedule.reservation_seconds, schedule.family, schedule.count,
                options.base, options.base_tree, options.head, options.base, aa ? options.base : options.candidate_revision,
                baseline_digest, candidate_digest, lab_digest, protocol_digest, python_digest, driver_digest,
                options.closure_sha256, options.freeze_sha256, options.trusted_revision, prepared_digest, baseline_stats.size, candidate_stats.size);
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
                    String8 pair_count = profile.pairs ? string_format(arena, S8("{u64}"), profile.pairs) : (String8){0};
                    String8 command[] = {python, S8("-B"), lab, S8("compare"), S8("--baseline"), baseline,
                        S8("--candidate"), candidate, S8("--repo-root"), source, S8("--cpu"), S8("2"),
                        S8("--output"), trial_path, S8("--target-minutes"), S8("10"),
                        S8("--warmups"), S8("1"), S8("--seed"), S8("20261003"), S8("--min-effect"), S8("0.5")};
                    OsArgumentBuilder builder = os_argument_builder_start(arena);
                    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(command); i += 1) os_argument_builder_append(&builder, command[i]);
                    if (profile.pairs)
                    {
                        os_argument_builder_append(&builder, S8("--pairs"));
                        os_argument_builder_append(&builder, pair_count);
                    }
                    SliceString8 trial_command = os_argument_builder_flush(&builder);
                    u64 trial_started = os_now_microseconds();
                    u64 left = deadline > trial_started ? deadline - trial_started : 1;
                    u64 timeout = profile.timeout_seconds * 1000000ull;
                    if (left < timeout) timeout = left;
                    ProcessSpawnResult spawn = {0};
                    CompilerExperimentSupervisor measured = {0};
                    bool contained = closure_before && compiler_experiment_supervisor_begin(arena, &measured);
                    if (closure_before && !contained) verifier_cleanup_failed = true;
                    if (contained)
                    {
                        spawn = os_process_spawn(trial_command, child_keys, child_values,
                            (ProcessSpawnOptions){.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
                                .search_path = 1, .new_process_group = 1, .observe_resources = 1,
                                .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = BUSTER_SAMPLING_CAPTURE_BYTES,
                                    [STANDARD_STREAM_ERROR] = BUSTER_SAMPLING_CAPTURE_BYTES}, .total = 2 * BUSTER_SAMPLING_CAPTURE_BYTES},
                                .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL});
                    }
                    if (spawn.handle) wait = os_process_wait_deadline(arena, spawn, timeout);
                    bool ended = contained && compiler_experiment_supervisor_end(arena, &measured);
                    if (contained && !ended) verifier_cleanup_failed = true;
                    bool supervision_recorded = compiler_sampling_supervision_receipt(arena,
                        path_join(arena, output, string_format(arena, S8("trial-{u64}-supervision.tsv"), trial)),
                        measured, ended, os_now_microseconds() - trial_started);
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
                        closure_before && closure_after && supervision_recorded && !measured.signalled && !measured.reaped && wait.resources.cpu_status == PROCESS_RESOURCE_OBSERVED &&
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
            String8 after_python = {0}, after_driver = {0}, after_freeze = {0}, after_prepared = {0};
            bool unchanged = stage_object_sha256_file(arena, baseline, &after_baseline) &&
                stage_object_sha256_file(arena, candidate, &after_candidate) && stage_object_sha256_file(arena, lab, &after_lab) &&
                stage_object_sha256_file(arena, protocol, &after_protocol) && stage_object_sha256_file(arena, python, &after_python) &&
                stage_object_sha256_file(arena, driver, &after_driver) && stage_object_sha256_file(arena, freeze, &after_freeze) &&
                stage_object_sha256_file(arena, prepared, &after_prepared) && string_equal(prepared_digest, after_prepared) &&
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


BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_run(Arena* arena, CompilerSamplingOptions options)
{
    // The public/controller runner always requires the actual approved CPU.
    // Only fixed private hosted fixtures may exercise the writer with synthetic data.
    return compiler_sampling_run_internal(arena, options, false);
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_run_owned(Arena* arena, CompilerSamplingOptions options, SliceString8 arguments)
{
    CompilerSamplingPacket planned = compiler_sampling_schedule(options.phase, options.packet);
    u64 started = os_now_microseconds();
    u64 allocation = planned.reservation_seconds * 1000000ull;
    u64 remaining = allocation > options.prep_us ? allocation - options.prep_us : 0;
    // The wrapper owns a fresh group for the entire worker and all lab compilers.
    // No worker launches into the Actions runner's process group.
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    os_argument_builder_append(&builder, options.driver);
    os_argument_builder_append(&builder, S8("compiler_profile_qualification"));
    for (u64 i = 0; i < arguments.length; i += 1) os_argument_builder_append(&builder, arguments.pointer[i]);
    os_argument_builder_append(&builder, S8("--owned-worker"));
    SliceString8 worker_command = os_argument_builder_flush(&builder);
    ProcessSpawnResult spawn = {0};
    ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
    CompilerExperimentSupervisor supervisor = {0};
    bool contained = compiler_experiment_supervisor_begin(arena, &supervisor);
    CompilerSamplingSignalScope signals = {0};
    bool deferred = contained && compiler_sampling_signals_begin(&signals);
    ProcessGroupControlState control = {0};
#if BUSTER_LINUX && !BUSTER_ANDROID
    control.cancellation_signal = &compiler_sampling_cancel_signal;
    control.cancellation_escalated = &compiler_sampling_cancel_escalated;
#endif
    if (deferred && remaining > 120ull * 1000000ull)
    {
        spawn = os_process_spawn(worker_command, (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.use_process_environment = 1, .new_process_group = 1, .observe_resources = 1});
        if (spawn.handle)
        {
            spawn.process_group_control = &control;
            wait = os_process_wait_deadline(arena, spawn, remaining - 120ull * 1000000ull);
        }
    }
    bool cleanup = contained && compiler_experiment_supervisor_end(arena, &supervisor) &&
        !wait.process_tree_cleanup_failed && !wait.process_group_reservation_retained && !wait.process_group_ownership_lost;
    bool cancelled = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    cancelled = process_control_atomic_load(&compiler_sampling_cancel_signal) != 0;
#endif
    bool complete = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out && cleanup &&
        !cancelled && !supervisor.signalled && !supervisor.reaped;
    bool restored = deferred && compiler_sampling_signals_end(&signals);
    complete = complete && restored;
    u64 wall = options.prep_us + os_now_microseconds() - started;
    String8 owner = string_format(arena,
        S8("schema\tbuster-main-sampling-owner-v1\nphysical_packet_wall_us\t{u64}\nprocess_state\t{S8}\ntimed_out\t{u64}\n"
           "cleanup_failed\t{u64}\nwithin_reservation\t{S8}\ncancelled\t{u64}\n"),
        wall, complete ? S8("complete") : S8("failed"), (u64)wait.timed_out, (u64)!cleanup, wall <= allocation ? S8("true") : S8("false"), (u64)cancelled);
    String8 persistent = path_join(arena, path_join(arena, options.ledger_root, options.freeze_sha256),
        string_format(arena, S8("{S8}-{u64}"), options.phase, options.packet));
    bool supervisor_written = compiler_sampling_supervision_receipt(arena,
        path_join(arena, options.output, S8("owner-supervision.tsv")), supervisor, cleanup, wall);
    bool written = supervisor_written && file_write(path_join(arena, options.output, S8("owner.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(owner)) &&
        file_write(path_join(arena, persistent, S8("owner.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(owner));
    if (!complete || wall > allocation || !written)
    {
        String8 campaign = path_join(arena, options.ledger_root, options.freeze_sha256);
        file_write(path_join(arena, campaign, S8("exhausted.tsv")),
            BUSTER_SLICE_TO_BYTE_SLICE(S8("state\texhausted\nreason\towned-worker-failed-or-overrun\n")));
    }
    return complete && wall <= allocation && written && restored ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}

#include "compiler_profile_qualification_controller.c"
#include "compiler_preparation_qualification_controller.c"
#include "compiler_sampling_packet_fixture.c"
#include "compiler_preparation_fixture_test.c"


BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_preparation_admit(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    CompilerPreparationAdmission admitted = {0};
    bool cleanup_present = false, workspace_present = false;
    String8 observed_cleanup = compiler_sampling_controller_environment(S8("RUNNER_TEMP"), &cleanup_present);
    String8 observed_workspace = compiler_sampling_controller_environment(S8("GITHUB_WORKSPACE"), &workspace_present);
    bool valid = arguments.length == 9 && cleanup_present && workspace_present;
    String8 data[5] = {0};
    u64 limits[] = {16384, 512, 16384, BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES, BUSTER_SAMPLING_FREEZE_MAX_BYTES};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(data); i += 1)
    {
        data[i] = compiler_sampling_controller_read(arena, arguments.pointer[i + 1], limits[i]);
        valid = data[i].length != 0;
    }
    String8 cleanup = valid ? os_path_absolute(arena, observed_cleanup, true) : (String8){0};
    String8 workspace = valid ? os_path_absolute(arena, observed_workspace, true) : (String8){0};
    valid = valid && cleanup.length && workspace.length &&
        string_equal(cleanup, os_path_absolute(arena, arguments.pointer[6], true)) &&
        string_equal(workspace, os_path_absolute(arena, arguments.pointer[7], true));
    if (valid) admitted = compiler_preparation_admission_validate(arena, data[0], data[1], data[2], data[3], data[4], cleanup, workspace);
    if (admitted.valid)
    {
        CompilerPreparationPlan plan = admitted.plan;
        String8 outputs = string_format(arena,
            S8("preparation_admitted=true\npreparation_phase=qualify\npreparation_packet=0\npreparation_family=preparation\npreparation_reservation_seconds={u64}\n"
               "preparation_worker_seconds={u64}\npreparation_timeout_minutes={u64}\npreparation_plan_revision={S8}\npreparation_plan_sha256={S8}\n"
               "preparation_protocol_sha256={S8}\npreparation_base={S8}\npreparation_base_tree={S8}\n"
               "preparation_candidate_revision={S8}\npreparation_candidate_tree={S8}\npreparation_trusted_revision={S8}\n"),
            admitted.reservation_seconds, admitted.worker_seconds, admitted.timeout_minutes, admitted.freeze_revision, admitted.freeze_sha256,
            admitted.protocol_sha256, plan.baseline_revision, plan.baseline_tree, plan.candidate_revision, plan.candidate_tree, admitted.trusted_revision);
        if (file_write(arguments.pointer[8], BUSTER_SLICE_TO_BYTE_SLICE(outputs))) result = PROCESS_RESULT_SUCCESS;
    }
    if (result != PROCESS_RESULT_SUCCESS) string_print(S8("error: distinct preparation qualification admission is disabled or invalid\n"));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_utility_admit(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    CompilerClosureUtilityAdmission admitted = {0};
    bool cleanup_present = false, workspace_present = false;
    String8 observed_cleanup = compiler_sampling_controller_environment(S8("RUNNER_TEMP"), &cleanup_present);
    String8 observed_workspace = compiler_sampling_controller_environment(S8("GITHUB_WORKSPACE"), &workspace_present);
    bool valid = arguments.length == 9 && cleanup_present && workspace_present;
    String8 data[5] = {0};
    u64 limits[] = {16384, 512, 16384, BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES, BUSTER_SAMPLING_FREEZE_MAX_BYTES};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(data); i += 1)
    {
        data[i] = compiler_sampling_controller_read(arena, arguments.pointer[i + 1], limits[i]);
        valid = data[i].length != 0;
    }
    String8 cleanup = valid ? os_path_absolute(arena, observed_cleanup, true) : (String8){0};
    String8 workspace = valid ? os_path_absolute(arena, observed_workspace, true) : (String8){0};
    valid = valid && cleanup.length && workspace.length &&
        string_equal(cleanup, os_path_absolute(arena, arguments.pointer[6], true)) &&
        string_equal(workspace, os_path_absolute(arena, arguments.pointer[7], true));
    if (valid) admitted = compiler_closure_utility_admission_validate(arena, data[0], data[1], data[2], data[3], data[4], cleanup, workspace);
    if (admitted.valid)
    {
        CompilerClosureUtilityPlan plan = admitted.plan;
        String8 outputs = string_format(arena,
            S8("utility_admitted=true\nutility_phase=utility\nutility_packet=0\nutility_family=utility\nutility_reservation_seconds={u64}\n"
               "utility_worker_seconds={u64}\nutility_timeout_minutes={u64}\nutility_plan_revision={S8}\nutility_plan_sha256={S8}\n"
               "utility_protocol_sha256={S8}\nutility_base={S8}\nutility_base_tree={S8}\n"
               "utility_candidate_revision={S8}\nutility_candidate_tree={S8}\nutility_pull_head={S8}\nutility_trusted_revision={S8}\n"),
            admitted.reservation_seconds, admitted.worker_seconds, admitted.timeout_minutes, admitted.freeze_revision, admitted.freeze_sha256,
            admitted.protocol_sha256, plan.baseline_revision, plan.baseline_tree, plan.candidate_revision, plan.candidate_tree, plan.pull_head, admitted.trusted_revision);
        if (file_write(arguments.pointer[8], BUSTER_SLICE_TO_BYTE_SLICE(outputs))) result = PROCESS_RESULT_SUCCESS;
    }
    if (result != PROCESS_RESULT_SUCCESS) string_print(S8("error: distinct closure utility admission is disabled or invalid\n"));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_profile_qualification_main(Arena* arena, SliceString8 arguments)
{
    CompilerSamplingOptions options = compiler_sampling_parse(arguments);
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length && string_equal(arguments.pointer[0], S8("--self-test-preparation-native-export")))
    {
#if BUSTER_LINUX && !BUSTER_ANDROID
        if (arguments.length == 2) result = compiler_preparation_fixture_main(arena, arguments.pointer[1]);
#else
        string_print(S8("error: hosted preparation export fixture requires Linux\n"));
#endif
    }
    else if (arguments.length && string_equal(arguments.pointer[0], S8("--self-test-packet-export")))
    {
        SliceString8 fixture_arguments = {.pointer = arguments.pointer + 1, .length = arguments.length - 1};
        result = compiler_sampling_packet_fixture_main(arena, fixture_arguments);
    }
    else if (arguments.length && (string_equal(arguments.pointer[0], S8("--execute-preparation")) ||
        string_equal(arguments.pointer[0], S8("--self-test-preparation-controller"))))
    {
        result = compiler_preparation_qualification_main(arena, arguments);
    }
    else if (arguments.length && string_equal(arguments.pointer[0], S8("--self-test-utility-admission")))
    {
        if (arguments.length == 1 && compiler_closure_utility_admission_self_test(arena)) result = PROCESS_RESULT_SUCCESS;
    }
    else if (arguments.length && string_equal(arguments.pointer[0], S8("--admit-utility")))
    {
        result = compiler_sampling_utility_admit(arena, arguments);
    }
    else if (arguments.length && string_equal(arguments.pointer[0], S8("--admit-preparation")))
    {
        result = compiler_sampling_preparation_admit(arena, arguments);
    }
    else if (options.valid && options.self_test)
    {
        result = compiler_sampling_self_test(arena);
    }
    else if (options.valid && options.plan)
    {
        string_print(S8("COMPILER_SAMPLING_PLAN protocol=buster-compiler-main-sampling-qualification-v1 routine_enabled=false\n"));
        String8 phases[] = {S8("acquire"), S8("pilot"), S8("confirm")};
        for (u64 phase = 0; phase < BUSTER_ARRAY_LENGTH(phases); phase += 1)
        {
            u64 count = phase == 0 ? 1 : phase == 1 ? 3 : 40;
            for (u64 packet = 0; packet < count; packet += 1)
            {
                CompilerSamplingPacket schedule = compiler_sampling_schedule(phases[phase], packet);
                if (!schedule.count)
                    string_print(S8("{S8}\t{u64}\t{S8}\t-\tpre-outcome-acquisition\t-\t{u64}\n"),
                        phases[phase], packet, schedule.family, schedule.reservation_seconds);
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
    else if (options.valid && options.execute)
    {
        CompilerSamplingControllerOptions resolved = {0};
        if (compiler_sampling_controller_resolve(arena, options, &resolved))
        {
            result = options.owned_worker ? compiler_sampling_controller_execute(arena, resolved) :
                compiler_sampling_run_owned(arena, resolved.packet, arguments);
        }
        else string_print(S8("error: trusted sampling controller rejected immutable admission data or platform identity before execution\n"));
    }
    else if (options.valid && options.claim)
    {
        result = compiler_sampling_claim(arena, options);
    }
    else if (options.valid)
    {
        result = options.owned_worker ? compiler_sampling_run(arena, options) : compiler_sampling_run_owned(arena, options, arguments);
    }
    else
    {
        string_print(S8("error: compiler_profile_qualification --plan | --self-test | --execute --phase acquire|pilot|confirm --packet N --trusted-root PATH --cleanup-root PATH --evidence PATH | [--claim] --phase pilot|confirm --packet N "
            "--ledger-root PATH --freeze PATH --freeze-sha256 SHA256 --output PATH; execution also requires "
            "--python PATH --lab PATH --baseline PATH --candidate PATH --repo-root PATH --base SHA --base-tree SHA "
            "--head SHA --protocol PATH --driver PATH --closure PATH --closure-sha256 SHA256 --prep-us N\n"));
    }
    return result;
}
