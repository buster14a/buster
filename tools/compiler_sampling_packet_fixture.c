// Hosted-only producer/ZIP-consumer diagnostic. Include after the sampling
// controller and its private compiler_sampling_run_internal implementation.
// The sole caller of the fixture-host path has fixed inputs and refuses an
// actual approved benchmark host. It never authenticates physical evidence.
// Public entry: compiler_sampling_packet_fixture_main(arena, arguments).
// arguments are OUTPUT, or the private --owned-fixture OUTPUT worker form.

typedef struct CompilerSamplingPacketFixture CompilerSamplingPacketFixture;
struct CompilerSamplingPacketFixture
{
    CompilerSamplingOptions options;
    String8 root;
    bool valid;
};

BUSTER_GLOBAL_LOCAL bool compiler_sampling_packet_fixture_host(Arena* arena)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 cpu = BYTE_SLICE_TO_STRING(8, file_read(arena, S8("/proc/cpuinfo"), (FileReadOptions){.map_required = 0}));
    result = cpu.length && !string_contains(cpu, S8("AMD Ryzen 7 9700X"));
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_packet_fixture_directory(String8 path)
{
    OsDirectoryCreateResult made = os_make_directory_exclusive(path);
    bool result = !made.error.v && made.created;
    return result;
}

BUSTER_GLOBAL_LOCAL FileStats compiler_sampling_packet_fixture_stats(String8 path)
{
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){0}, (OsFileAccess){.read = 1},
        (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1});
    FileStats result = os_file_get_stats(file, (FileStatsOptions){.size = 1, .identity = 1});
    if (file) os_file_close(file);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_sampling_packet_fixture_json(Arena* arena, String8 value)
{
    u64 start = arena->position;
    arena_append_json_string(arena, value);
    String8 result = {.pointer = (char8*)arena_get_byte_pointer_at_position(arena, start), .length = arena->position - start};
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingPacketFixture compiler_sampling_packet_fixture_paths(Arena* arena, String8 root)
{
    String8 a = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    String8 b = S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    String8 c = S8("cccccccccccccccccccccccccccccccccccccccc");
    String8 d = S8("dddddddddddddddddddddddddddddddddddddddd");
    String8 input = path_join(arena, root, S8("input"));
    String8 fake = os_path_absolute(arena, S8("tools/tests/compiler_sampling_fake_lab.py"), true);
    String8 python = os_path_absolute(arena, S8("/usr/bin/python3"), true);
    CompilerSamplingPacketFixture result = {.root = root, .options = {
        .phase = S8("pilot"), .packet_text = S8("0"), .packet = 0,
        .ledger_root = path_join(arena, root, S8("ledger")),
        .freeze = path_join(arena, input, S8("freeze.tsv")),
        .python = python, .lab = fake, .driver = fake,
        .baseline = path_join(arena, input, S8("bin/baseline")),
        .candidate = path_join(arena, input, S8("bin/baseline")),
        .source = path_join(arena, input, S8("source")),
        .output = path_join(arena, root, S8("export")),
        .base = a, .base_tree = b, .head = c, .candidate_revision = a, .trusted_revision = d,
        .campaign_parent_revision = b,
        .protocol = path_join(arena, input, S8("protocol.json")),
        .closure = path_join(arena, input, S8("closure.json")),
        .prepared = path_join(arena, input, S8("prepared.json")),
        // The diagnostic has no production preparation phase. The independent
        // owner clock accounts for fixture construction as well as execution.
        .prep_text = S8("0"), .prep_us = 0, .valid = true}};
    result.valid = root.length && fake.length && python.length && compiler_sampling_packet_fixture_host(arena);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_packet_fixture_expected(Arena* arena, CompilerSamplingPacketFixture fixture,
    CompilerSamplingFreeze frozen, String8 freeze_digest)
{
    CompilerSamplingOptions o = fixture.options;
    String8 source_json = compiler_sampling_packet_fixture_json(arena, o.source);
    String8 identity = string_format(arena,
        S8("{{\"schema\":\"buster-main-sampling-packet-v1\",\"phase\":\"pilot\",\"packet\":\"0\","
           "\"campaign\":\"{S8}\",\"reservation_seconds\":\"3000\",\"family\":\"aa\",\"trials\":\"3\","
           "\"base\":\"{S8}\",\"base_tree\":\"{S8}\",\"request_head\":\"{S8}\","
           "\"baseline_revision\":\"{S8}\",\"candidate_revision\":\"{S8}\","
           "\"baseline_sha256\":\"{S8}\",\"candidate_sha256\":\"{S8}\","
           "\"lab_sha256\":\"{S8}\",\"protocol_sha256\":\"{S8}\",\"python_sha256\":\"{S8}\","
           "\"driver_sha256\":\"{S8}\",\"closure_sha256\":\"{S8}\",\"freeze_sha256\":\"{S8}\","
           "\"trusted_revision\":\"{S8}\",\"prepared_sha256\":\"{S8}\","
           "\"baseline_bytes\":\"{S8}\",\"candidate_bytes\":\"{S8}\","
           "\"cpu\":\"2\",\"warmups\":\"1\",\"seed\":\"20261003\",\"floor_percent\":\"0.5\","
           "\"fresh_copy\":\"true\",\"routine_enabled\":\"false\",\"evidence_class\":\"unqualified-sampling-research\"}}"),
        freeze_digest, o.base, o.base_tree, o.head, o.base, o.base,
        frozen.baseline_sha256, frozen.baseline_sha256, frozen.lab_sha256, frozen.protocol_sha256,
        frozen.python_sha256, frozen.driver_sha256, frozen.closure_sha256, freeze_digest,
        o.trusted_revision, frozen.prepared_sha256, frozen.baseline_bytes, frozen.baseline_bytes);
    String8 expected = string_format(arena,
        S8("{{\"diagnostic_fixture\":true,\"actual_approved_host\":false,\"trusted\":{{"
           "\"authenticated\":true,\"request\":{{\"repository\":\"buster14a/buster\",\"actor\":\"davidgmbb\",\"owner\":\"davidgmbb\","
           "\"selector\":\"profile: compiler-main-sampling-pilot-v1\",\"request_run_id\":\"998\","
           "\"request_head\":\"{S8}\",\"freeze_revision\":\"{S8}\",\"phase\":\"pilot\",\"packet\":0,"
           "\"campaign\":\"{S8}\",\"acquisition_campaign\":\"{S8}\",\"acquisition_revision\":\"{S8}\","
           "\"pilot_campaign\":\"{S8}\",\"pilot_revision\":\"{S8}\"}},"
           "\"executor\":{{\"repository\":\"buster14a/buster\",\"request_run_id\":\"998\",\"run_id\":\"1001\","
           "\"run_attempt\":\"1\",\"cpu_model\":\"AMD Ryzen 7 9700X 8-Core Processor\","
           "\"physical_packet_wall_us\":null,\"actions_job_occupancy_us\":null,\"queue_delay_seconds\":null}},"
           "\"identity\":{S8},\"binaries\":{{"
           "\"baseline\":{{\"sha256\":\"{S8}\",\"revision\":\"{S8}\",\"size_bytes\":{S8}}},"
           "\"candidate\":{{\"sha256\":\"{S8}\",\"revision\":\"{S8}\",\"size_bytes\":{S8}}}}},"
           "\"workload_config\":{{\"command\":\"IDE cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o OUT\","
           "\"repo_root\":{S8},\"perf\":\"perf\",\"extra\":[],\"extra_by_variant\":{{\"a\":[],\"b\":[]}}}},"
           "\"attempts\":[{{\"phase\":\"acquire\",\"packet\":0,\"run_id\":\"999\",\"run_attempt\":\"1\",\"state\":\"complete\","
           "\"reservation_seconds\":1800,\"actions_job_occupancy_us\":3000000,\"campaign\":\"{S8}\",\"freeze_revision\":\"{S8}\"}},"
           "{{\"phase\":\"pilot\",\"packet\":0,\"run_id\":\"1001\",\"run_attempt\":\"1\",\"state\":\"complete\","
           "\"reservation_seconds\":3000,\"physical_packet_wall_us\":null,\"actions_job_occupancy_us\":null,"
           "\"campaign\":\"{S8}\",\"freeze_revision\":\"{S8}\"}}]}}}}\n"),
        o.head, o.trusted_revision, freeze_digest, frozen.campaign_parent, frozen.campaign_parent_revision,
        freeze_digest, o.trusted_revision, identity,
        frozen.baseline_sha256, o.base, frozen.baseline_bytes, frozen.baseline_sha256, o.base, frozen.baseline_bytes,
        source_json, frozen.campaign_parent, frozen.campaign_parent_revision, freeze_digest, o.trusted_revision);
    bool result = file_write(path_join(arena, o.output, S8("fixture-expected.json")), BUSTER_SLICE_TO_BYTE_SLICE(expected));
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingPacketFixture compiler_sampling_packet_fixture_create(Arena* arena, String8 root)
{
    CompilerSamplingPacketFixture result = compiler_sampling_packet_fixture_paths(arena, root);
    CompilerSamplingOptions* o = &result.options;
    String8 input = path_join(arena, root, S8("input"));
    bool valid = result.valid && compiler_sampling_packet_fixture_directory(root) &&
        compiler_sampling_packet_fixture_directory(input) &&
        compiler_sampling_packet_fixture_directory(o->source) &&
        compiler_sampling_packet_fixture_directory(path_join(arena, input, S8("bin"))) &&
        compiler_sampling_packet_fixture_directory(o->output) &&
        compiler_sampling_packet_fixture_directory(o->ledger_root);
    u64 completed_stage = valid ? 1 : 0;
    String8 source_metadata = string_format(arena,
        S8("{{\"diagnostic_fixture\":true,\"actual_approved_host\":false,\"base\":\"{S8}\",\"base_tree\":\"{S8}\","
           "\"request_head\":\"{S8}\",\"trusted_revision\":\"{S8}\",\"freeze_revision\":\"{S8}\"}}\n"),
        o->base, o->base_tree, o->head, o->trusted_revision, o->trusted_revision);
    String8 source_path = path_join(arena, o->source, S8("fixture-metadata.json"));
    String8 ab1 = path_join(arena, input, S8("bin/ab1"));
    String8 ab2 = path_join(arena, input, S8("bin/ab2"));
    String8 acquisition = path_join(arena, input, S8("acquisition.json"));
    valid = valid &&
        file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source_metadata)) &&
        file_write(path_join(arena, o->source, S8("fixture.c")), BUSTER_SLICE_TO_BYTE_SLICE(S8("/* diagnostic; never compiled or measured */\n"))) &&
        file_write(o->baseline, BUSTER_SLICE_TO_BYTE_SLICE(S8("diagnostic-baseline-bytes\n"))) &&
        file_write(ab1, BUSTER_SLICE_TO_BYTE_SLICE(S8("diagnostic-ab1-bytes-only\n"))) &&
        file_write(ab2, BUSTER_SLICE_TO_BYTE_SLICE(S8("diagnostic-ab2-bytes-only\n"))) &&
        file_write(o->protocol, BUSTER_SLICE_TO_BYTE_SLICE(S8("{\"diagnostic_fixture\":true,\"routine_enabled\":false,\"schedule\":\"pilot-0-aa-v1-40-80\"}\n"))) &&
        file_write(o->closure, BUSTER_SLICE_TO_BYTE_SLICE(source_metadata)) &&
        file_write(o->prepared, BUSTER_SLICE_TO_BYTE_SLICE(S8("{\"diagnostic_fixture\":true,\"actual_approved_host\":false,\"binaries_executed\":false}\n"))) &&
        file_write(acquisition, BUSTER_SLICE_TO_BYTE_SLICE(S8("{\"diagnostic_fixture\":true,\"physical_acquisition\":false,\"synthetic_lineage\":\"acquire-0\"}\n")));
    if (valid) completed_stage = 2;
    String8 digests[10] = {0};
    String8 paths[] = {o->baseline, ab1, ab2, o->lab, o->python, o->driver, o->protocol, o->closure, o->prepared, acquisition};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(paths); i += 1)
        valid = stage_object_sha256_file(arena, paths[i], &digests[i]);
    if (valid) completed_stage = 3;
    FileStats sizes[] = {compiler_sampling_packet_fixture_stats(o->baseline),
        compiler_sampling_packet_fixture_stats(ab1), compiler_sampling_packet_fixture_stats(ab2)};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
        valid = sizes[i].valid && sizes[i].kind == OS_FILE_KIND_REGULAR && sizes[i].size;
    if (valid) completed_stage = 4;
    CompilerSamplingFreeze frozen = {.schema = S8("buster-main-sampling-freeze-v1"), .phase = S8("pilot"),
        .campaign_parent = digests[9], .campaign_parent_revision = o->campaign_parent_revision,
        .base = o->base, .base_tree = o->base_tree, .request_head = o->head, .trusted_revision = o->trusted_revision,
        .baseline_revision = o->base, .aa_candidate_revision = o->base,
        .ab1_revision = S8("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        .ab2_revision = S8("ffffffffffffffffffffffffffffffffffffffff"),
        .baseline_sha256 = digests[0], .aa_candidate_sha256 = digests[0],
        .ab1_candidate_sha256 = digests[1], .ab2_candidate_sha256 = digests[2],
        .lab_sha256 = digests[3], .python_sha256 = digests[4], .driver_sha256 = digests[5],
        .protocol_sha256 = digests[6], .closure_sha256 = digests[7], .prepared_sha256 = digests[8],
        .baseline_bytes = string_format(arena, S8("{u64}"), sizes[0].size),
        .ab1_candidate_bytes = string_format(arena, S8("{u64}"), sizes[1].size),
        .ab2_candidate_bytes = string_format(arena, S8("{u64}"), sizes[2].size),
        .candidate_pairs = S8("0"), .selected_candidate = S8("exploratory"),
        .calibration_ab1_low_percent = S8("-"), .calibration_ab1_high_percent = S8("-"),
        .calibration_ab2_low_percent = S8("-"), .calibration_ab2_high_percent = S8("-")};
    String8 freeze_text = compiler_sampling_freeze_fixture(arena, frozen);
    valid = valid && compiler_sampling_freeze_parse(freeze_text).valid &&
        file_write(o->freeze, BUSTER_SLICE_TO_BYTE_SLICE(freeze_text)) &&
        stage_object_sha256_file(arena, o->freeze, &o->freeze_sha256);
    if (valid) completed_stage = 5;
    o->campaign_parent = frozen.campaign_parent;
    o->closure_sha256 = frozen.closure_sha256;
    String8 campaign = path_join(arena, o->ledger_root, o->freeze_sha256);
    String8 persistent = path_join(arena, campaign, S8("pilot-0"));
    String8 claim = compiler_sampling_claim_record(arena, *o);
    valid = valid && compiler_sampling_packet_fixture_directory(campaign) &&
        compiler_sampling_packet_fixture_directory(persistent) &&
        file_write(path_join(arena, persistent, S8("reservation.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(claim)) &&
        file_write(path_join(arena, o->output, S8("claim.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(claim)) &&
        compiler_sampling_packet_fixture_expected(arena, result, frozen, o->freeze_sha256);
    if (valid) completed_stage = 6;
    else string_print(S8("HOSTED_PACKET_FIXTURE_CREATE completed_stage={u64} frozen_valid={S8}\n"),
        completed_stage, compiler_sampling_freeze_parse(freeze_text).valid ? S8("true") : S8("false"));
    result.valid = valid;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingPacketFixture compiler_sampling_packet_fixture_load(Arena* arena, String8 root)
{
    CompilerSamplingPacketFixture result = compiler_sampling_packet_fixture_paths(arena, root);
    CompilerSamplingOptions* o = &result.options;
    String8 text = BYTE_SLICE_TO_STRING(8, file_read(arena, o->freeze, (FileReadOptions){.map_required = 0}));
    CompilerSamplingFreeze frozen = compiler_sampling_freeze_parse(text);
    o->campaign_parent = frozen.campaign_parent;
    o->closure_sha256 = frozen.closure_sha256;
    result.valid = result.valid && frozen.valid && string_equal(frozen.phase, o->phase) &&
        string_equal(frozen.base, o->base) && string_equal(frozen.base_tree, o->base_tree) &&
        string_equal(frozen.request_head, o->head) && string_equal(frozen.trusted_revision, o->trusted_revision) &&
        string_equal(frozen.campaign_parent_revision, o->campaign_parent_revision) &&
        stage_object_sha256_file(arena, o->freeze, &o->freeze_sha256);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_packet_fixture_owner(Arena* arena, String8 requested)
{
    u64 started = os_now_microseconds();
    String8 root = os_path_absolute_lexical(arena, requested, true);
    String8 parent = path_parent(arena, root);
    String8 resolved_parent = os_path_absolute(arena, parent, true);
    String8 self = build_running_driver(arena);
    bool path_valid = root.length && parent.length && self.length && string_equal(parent, resolved_parent) &&
        generate_path_kind(arena, root) == GENERATE_PATH_MISSING && compiler_sampling_packet_fixture_host(arena);
    CompilerSamplingPacketFixture fixture = {0};
    if (path_valid) fixture = compiler_sampling_packet_fixture_create(arena, root);
    CompilerExperimentSupervisor supervisor = {0};
    bool contained = fixture.valid && compiler_experiment_supervisor_begin(arena, &supervisor);
    CompilerSamplingSignalScope signals = {0};
    bool deferred = contained && compiler_sampling_signals_begin(&signals);
    ProcessGroupControlState control = {0};
#if BUSTER_LINUX && !BUSTER_ANDROID
    control.cancellation_signal = &compiler_sampling_cancel_signal;
    control.cancellation_escalated = &compiler_sampling_cancel_escalated;
#endif
    ProcessSpawnResult spawn = {0};
    ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
    u64 worker_started = os_now_microseconds();
    if (deferred)
    {
        String8 command[] = {self, S8("compiler_profile_qualification"), S8("--self-test-packet-export"),
            S8("--owned-fixture"), root};
        spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.use_process_environment = 1, .new_process_group = 1, .observe_resources = 1});
        if (spawn.handle)
        {
            spawn.process_group_control = &control;
            // This is a 45-second hosted diagnostic, never a physical reservation.
            wait = os_process_wait_deadline(arena, spawn, 45ull * 1000000ull);
        }
    }
    u64 worker_wall = os_now_microseconds() - worker_started;
    bool cleanup = contained && compiler_experiment_supervisor_end(arena, &supervisor) &&
        !wait.process_tree_cleanup_failed && !wait.process_group_reservation_retained && !wait.process_group_ownership_lost;
    bool cancelled = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    cancelled = process_control_atomic_load(&compiler_sampling_cancel_signal) != 0;
#endif
    bool restored = deferred && compiler_sampling_signals_end(&signals);
    bool complete = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out && cleanup &&
        !cancelled && !supervisor.signalled && !supervisor.reaped && restored;
    u64 job_wall = os_now_microseconds() - started;
    bool written = false;
    if (fixture.valid)
    {
        bool proof = compiler_sampling_supervision_receipt(arena,
            path_join(arena, fixture.options.output, S8("owner-supervision.tsv")), supervisor, cleanup, job_wall);
        String8 owner = string_format(arena,
            S8("schema\tbuster-hosted-packet-fixture-owner-v1\nprocess_state\t{S8}\n"
               "owned_worker\t{S8}\nactual_approved_host\tfalse\ncleanup_failed\t{u64}\ntimed_out\t{u64}\n"),
            complete ? S8("complete") : S8("failed"), spawn.handle ? S8("true") : S8("false"),
            (u64)!cleanup, (u64)wait.timed_out);
        proof = proof && file_write(path_join(arena, fixture.options.output, S8("owner.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(owner));
        job_wall = os_now_microseconds() - started;
        complete = complete && proof && job_wall <= 60ull * 1000000ull;
        String8 manager = string_format(arena,
            S8("schema\tbuster-hosted-packet-fixture-manager-v1\nstate\t{S8}\nactual_approved_host\tfalse\n"
               "owned_worker\t{S8}\njob_wall_us\t{u64}\nworker_wall_us\t{u64}\ncleanup_failed\t{u64}\n"),
            complete ? S8("complete") : S8("failed"), spawn.handle ? S8("true") : S8("false"),
            job_wall, worker_wall, (u64)!cleanup);
        written = file_write(path_join(arena, root, S8("fixture-manager.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(manager));
    }
    ProcessResult result = complete && written && os_now_microseconds() - started <= 60ull * 1000000ull ?
        PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    if (result != PROCESS_RESULT_SUCCESS)
        string_print(S8("HOSTED_PACKET_FIXTURE_OWNER path_valid={S8} fixture_valid={S8} contained={S8} signals={S8} "
            "spawned={S8} wait_result={u64} timed_out={u64} cleanup={S8} restored={S8} "
            "adopted_signalled={u64} adopted_reaped={u64} job_wall_us={u64} written={S8}\n"),
            path_valid ? S8("true") : S8("false"), fixture.valid ? S8("true") : S8("false"),
            contained ? S8("true") : S8("false"), deferred ? S8("true") : S8("false"),
            spawn.handle ? S8("true") : S8("false"), (u64)wait.result, (u64)wait.timed_out,
            cleanup ? S8("true") : S8("false"), restored ? S8("true") : S8("false"),
            supervisor.signalled, supervisor.reaped, job_wall, written ? S8("true") : S8("false"));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_packet_fixture_main(Arena* arena, SliceString8 arguments)
{
    bool worker = arguments.length == 2 && string_equal(arguments.pointer[0], S8("--owned-fixture"));
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length == 1)
        result = compiler_sampling_packet_fixture_owner(arena, arguments.pointer[0]);
    else if (worker && compiler_sampling_packet_fixture_host(arena))
    {
        SliceString8 keys = {0}, values = {0};
        String8 root = os_path_absolute(arena, arguments.pointer[1], true);
        bool owned = compiler_sampling_owned_environment(arena, &keys, &values);
        CompilerSamplingPacketFixture fixture = compiler_sampling_packet_fixture_load(arena, root);
        if (owned && fixture.valid)
            result = compiler_sampling_run_internal(arena, fixture.options, true);
        if (result != PROCESS_RESULT_SUCCESS)
            string_print(S8("HOSTED_PACKET_FIXTURE_WORKER owned={S8} fixture_valid={S8} result={u64}\n"),
                owned ? S8("true") : S8("false"), fixture.valid ? S8("true") : S8("false"), (u64)result);
    }
    return result;
}
