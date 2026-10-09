// Native execution of the distinct preparation qualification. Include after
// sampling controller primitives and the pure preparation admission policy.
// Admission bytes are authenticated by the existing trusted GitHub adapter;
// this controller replays them, binds the runtime and claims before any fetch.
// No process success activates snapshot preparation or qualifies statistics.
#ifndef BUSTER_COMPILER_PREPARATION_CONTROLLER_INCLUDED
#define BUSTER_COMPILER_PREPARATION_CONTROLLER_INCLUDED

typedef struct CompilerPreparationControllerOptions CompilerPreparationControllerOptions;
struct CompilerPreparationControllerOptions
{
    String8 trusted_root, cleanup_root, evidence;
    bool owned_worker, self_test, valid;
};

typedef struct CompilerPreparationControllerTransport CompilerPreparationControllerTransport;
struct CompilerPreparationControllerTransport { String8 bytes[5]; bool valid; };

typedef struct CompilerPreparationControllerResolved CompilerPreparationControllerResolved;
struct CompilerPreparationControllerResolved
{
    CompilerPreparationControllerOptions options;
    CompilerPreparationControllerTransport transport;
    CompilerPreparationAdmission admitted;
    CompilerSamplingControllerHost host;
    CompilerExperimentJobClock job_clock;
    String8 workspace, driver, lab, python, protocol, claim, claim_record;
    bool valid;
};

BUSTER_GLOBAL_LOCAL CompilerPreparationControllerOptions compiler_preparation_controller_parse(SliceString8 arguments)
{
    CompilerPreparationControllerOptions result = {0};
    if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test-preparation-controller")))
    {
        result.self_test = true;
        result.valid = true;
    }
    else if ((arguments.length == 7 || arguments.length == 8) &&
        string_equal(arguments.pointer[0], S8("--execute-preparation")) &&
        string_equal(arguments.pointer[1], S8("--trusted-root")) &&
        string_equal(arguments.pointer[3], S8("--cleanup-root")) &&
        string_equal(arguments.pointer[5], S8("--evidence")) &&
        (arguments.length == 7 || string_equal(arguments.pointer[7], S8("--owned-worker"))))
    {
        result.trusted_root = arguments.pointer[2];
        result.cleanup_root = arguments.pointer[4];
        result.evidence = arguments.pointer[6];
        result.owned_worker = arguments.length == 8;
        result.valid = result.trusted_root.length && result.cleanup_root.length && result.evidence.length;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerPreparationControllerTransport compiler_preparation_controller_transport(Arena* arena)
{
    CompilerPreparationControllerTransport result = {.valid = true};
    String8 names[] = {S8("BQ_PREPARATION_REQUEST_DATA"), S8("BQ_PREPARATION_PLAN_DATA"),
        S8("BQ_PREPARATION_ALLOWLIST_DATA"), S8("BQ_PREPARATION_FACTS_DATA"), S8("BQ_PREPARATION_HISTORY_DATA")};
    u64 present_count = 0, observed_count = 0, total = 0;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        bool present = false;
        String8 encoded = compiler_sampling_controller_environment(names[i], &present);
        present_count += present ? 1 : 0;
        total += encoded.length;
        bool decoded = present && encoded.length <= 65536 && total <= 262144 &&
            compiler_sampling_controller_base64(arena, encoded, false, &result.bytes[i]);
        result.valid = result.valid && decoded;
    }
    for (u64 key = 0; key < program_state->input.environment_keys.length; key += 1)
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
            observed_count += string_equal(program_state->input.environment_keys.pointer[key], names[i]) ? 1 : 0;
    result.valid = result.valid && present_count == 5 && observed_count == 5;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_attempt(Arena* arena,
    CompilerPreparationControllerTransport transport, CompilerPreparationAdmission admitted)
{
    String8 facts = transport.bytes[3];
    String8 names[] = {S8("GITHUB_RUN_ID"), S8("GITHUB_RUN_ATTEMPT"), S8("GITHUB_REPOSITORY"), S8("GITHUB_SHA"),
        S8("BQ_REQUEST_RUN_ID"), S8("BQ_REQUEST_ATTEMPT"), S8("BQ_HEAD_COMMIT"),
        S8("BQ_PREPARATION_PHASE"), S8("BQ_PREPARATION_PACKET"), S8("BQ_PREPARATION_FAMILY"),
        S8("BQ_PREPARATION_PLAN_REVISION"), S8("BQ_PREPARATION_PLAN_SHA256"), S8("BQ_PREPARATION_PROTOCOL_SHA256"),
        S8("BQ_PREPARATION_BASE"), S8("BQ_PREPARATION_BASE_TREE"),
        S8("BQ_PREPARATION_CANDIDATE_REVISION"), S8("BQ_PREPARATION_CANDIDATE_TREE"),
        S8("BQ_PREPARATION_TRUSTED_REVISION"), S8("BQ_PREPARATION_RESERVATION_SECONDS"),
        S8("BQ_PREPARATION_WORKER_SECONDS"), S8("BQ_PREPARATION_TIMEOUT_MINUTES")};
    String8 values[] = {compiler_sampling_controller_fact(facts, S8("executor_run_id")), S8("1"),
        S8("buster14a/buster"), admitted.policy_trusted_revision,
        compiler_sampling_controller_fact(facts, S8("request_run_id")), S8("1"),
        compiler_sampling_controller_fact(facts, S8("request_head")), S8("qualify"), S8("0"), S8("preparation"),
        admitted.freeze_revision, admitted.freeze_sha256, admitted.protocol_sha256,
        admitted.plan.baseline_revision, admitted.plan.baseline_tree,
        admitted.plan.candidate_revision, admitted.plan.candidate_tree, admitted.trusted_revision,
        S8("5400"), S8("5280"), S8("90")};
    bool result = admitted.valid && transport.valid;
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(names); i += 1)
        result = values[i].length && compiler_sampling_controller_environment_matches(names[i], values[i], true);
    BUSTER_UNUSED(arena);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_preparation_controller_claim_record(Arena* arena,
    CompilerPreparationControllerResolved resolved)
{
    CompilerPreparationAdmission admitted = resolved.admitted;
    String8 facts = resolved.transport.bytes[3];
    String8 hashes[5] = {0};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(hashes); i += 1)
        hashes[i] = stage_object_sha256_bytes(arena, (u8*)resolved.transport.bytes[i].pointer, resolved.transport.bytes[i].length);
    String8 result = string_format(arena,
        S8("schema\tbuster-compiler-preparation-claim-v1\nprofile\tcompiler-baseline-closure-qualification-v1\n"
           "phase\tqualify\npacket\t0\nplan_revision\t{S8}\nplan_sha256\t{S8}\n"
           "request_run_id\t{S8}\nrequest_run_attempt\t1\nexecutor_run_id\t{S8}\nexecutor_run_attempt\t1\n"
           "request_head\t{S8}\npolicy_trusted_revision\t{S8}\nmeasurement_trusted_revision\t{S8}\n"
           "source_root\t{S8}\noutput_root\t{S8}\nevidence\t{S8}\ndriver\t{S8}\n"
           "request_sha256\t{S8}\nplan_transport_sha256\t{S8}\nallowlist_sha256\t{S8}\nfacts_sha256\t{S8}\nhistory_sha256\t{S8}\n"
           "physical_job_clock_sha256\t{S8}\nreservation_seconds\t5400\nworker_seconds\t5280\ntail_seconds\t120\nstate\tclaimed\n"),
        admitted.freeze_revision, admitted.freeze_sha256,
        compiler_sampling_controller_fact(facts, S8("request_run_id")),
        compiler_sampling_controller_fact(facts, S8("executor_run_id")),
        compiler_sampling_controller_fact(facts, S8("request_head")), admitted.policy_trusted_revision, admitted.trusted_revision,
        admitted.plan.source_root, admitted.plan.output_root, resolved.options.evidence, resolved.driver,
        hashes[0], hashes[1], hashes[2], hashes[3], hashes[4],
        stage_object_sha256_bytes(arena, (u8*)resolved.job_clock.record.pointer, resolved.job_clock.record.length));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_paths(Arena* arena,
    CompilerPreparationControllerOptions options, CompilerPreparationPlan plan,
    CompilerPreparationControllerResolved* resolved)
{
    bool temp_present = false, workspace_present = false;
    String8 temp = compiler_sampling_controller_environment(S8("RUNNER_TEMP"), &temp_present);
    String8 workspace = compiler_sampling_controller_environment(S8("GITHUB_WORKSPACE"), &workspace_present);
    String8 cleanup = temp_present ? os_path_absolute(arena, temp, true) : (String8){0};
    workspace = workspace_present ? os_path_absolute(arena, workspace, true) : (String8){0};
    String8 trusted = os_path_absolute(arena, options.trusted_root, true);
    String8 evidence = os_path_absolute_lexical(arena, options.evidence, true);
    String8 driver = program_state->input.arguments.length ?
        os_path_absolute(arena, program_state->input.arguments.pointer[0], true) : (String8){0};
    String8 source_parent = path_parent(arena, plan.source_root), output_parent = path_parent(arena, plan.output_root);
    String8 claim = string_format(arena, S8("{S8}.claim"), plan.output_root);
    bool result = plan.valid && cleanup.length && workspace.length && trusted.length && driver.length && evidence.length &&
        string_equal(trusted, plan.trusted_root) &&
        string_equal(cleanup, os_path_absolute(arena, options.cleanup_root, true)) &&
        compiler_preparation_plan_paths_outside(plan, cleanup, workspace) &&
        compiler_sampling_acquisition_path(evidence) && compiler_sampling_acquisition_path(claim) &&
        string_equal(source_parent, os_path_absolute(arena, source_parent, true)) &&
        string_equal(output_parent, os_path_absolute(arena, output_parent, true)) &&
        string_equal(path_parent(arena, evidence), os_path_absolute(arena, path_parent(arena, evidence), true)) &&
        compiler_sampling_acquisition_path_within(evidence, cleanup) && !string_equal(evidence, cleanup) &&
        !compiler_sampling_path_overlap(plan.source_root, trusted) &&
        !compiler_sampling_path_overlap(plan.output_root, trusted) &&
        !compiler_sampling_path_overlap(claim, trusted) &&
        !compiler_sampling_path_overlap(claim, cleanup) && !compiler_sampling_path_overlap(claim, workspace) &&
        !compiler_sampling_path_overlap(plan.source_root, evidence) &&
        !compiler_sampling_path_overlap(plan.output_root, evidence) &&
        !compiler_sampling_path_overlap(claim, evidence) &&
        !compiler_sampling_path_overlap(evidence, trusted) &&
        compiler_sampling_acquisition_path_within(driver, trusted) &&
        generate_path_kind(arena, plan.source_root) == GENERATE_PATH_MISSING &&
        generate_path_kind(arena, plan.output_root) == GENERATE_PATH_MISSING;
    resolved->options = options;
    resolved->options.trusted_root = trusted;
    resolved->options.cleanup_root = cleanup;
    resolved->options.evidence = evidence;
    resolved->workspace = workspace;
    resolved->driver = driver;
    resolved->lab = path_join(arena, trusted, S8("tools/uarch_lab.py"));
    resolved->protocol = path_join(arena, trusted, S8("docs/compiler-preparation-qualification-v1.md"));
    resolved->python = plan.python_path;
    resolved->claim = claim;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_tools(Arena* arena,
    CompilerPreparationControllerResolved resolved)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 paths[] = {resolved.lab, resolved.python, resolved.driver, resolved.protocol};
    String8 hashes[] = {resolved.admitted.plan.lab_sha256, resolved.admitted.plan.python_sha256,
        resolved.admitted.plan.native_driver_sha256, resolved.admitted.plan.protocol_sha256};
    result = true;
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(paths); i += 1)
    {
        String8 digest = {0};
        struct stat status = {0};
        result = paths[i].length && string_equal(os_path_absolute(arena, paths[i], true), paths[i]) &&
            compiler_sampling_controller_hash(arena, paths[i], &digest, &status) &&
            string_equal(digest, hashes[i]) && (i == 0 || i == 3 || (status.st_mode & 0111));
    }
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(resolved);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_resolve(Arena* arena,
    CompilerPreparationControllerOptions options, CompilerPreparationControllerResolved* output)
{
    CompilerPreparationControllerResolved resolved = {0};
    resolved.transport = compiler_preparation_controller_transport(arena);
    bool temp_present = false, workspace_present = false;
    String8 temp = compiler_sampling_controller_environment(S8("RUNNER_TEMP"), &temp_present);
    String8 workspace = compiler_sampling_controller_environment(S8("GITHUB_WORKSPACE"), &workspace_present);
    String8 cleanup = temp_present ? os_path_absolute(arena, temp, true) : (String8){0};
    workspace = workspace_present ? os_path_absolute(arena, workspace, true) : (String8){0};
    resolved.admitted = compiler_preparation_admission_validate(arena,
        resolved.transport.bytes[2], resolved.transport.bytes[0], resolved.transport.bytes[3],
        resolved.transport.bytes[4], resolved.transport.bytes[1], cleanup, workspace);
    bool valid = options.valid && resolved.transport.valid && resolved.admitted.valid &&
        compiler_preparation_controller_attempt(arena, resolved.transport, resolved.admitted) &&
        compiler_preparation_controller_paths(arena, options, resolved.admitted.plan, &resolved) &&
        compiler_experiment_job_clock_resolve(arena, S8("preparation"), resolved.admitted.policy_trusted_revision, &resolved.job_clock) &&
        compiler_experiment_job_clock_remaining_us(resolved.job_clock, 5400000000ull, 5280000000ull);
    resolved.claim_record = valid ? compiler_preparation_controller_claim_record(arena, resolved) : (String8){0};
    resolved.host = compiler_sampling_controller_observed_host(arena);
    valid = valid && resolved.host.valid && compiler_preparation_controller_tools(arena, resolved);
    if (valid && options.owned_worker)
    {
#if BUSTER_LINUX && !BUSTER_ANDROID
        valid = getpgrp() > 1 && getpgrp() == getpid() && getpgrp() != getsid(0) &&
            string_equal(os_path_absolute(arena, resolved.claim, true), resolved.claim) &&
            string_equal(os_path_absolute(arena, resolved.options.evidence, true), resolved.options.evidence) &&
            string_equal(compiler_sampling_controller_read(arena,
                path_join(arena, resolved.claim, S8("claim.tsv")), 16384), resolved.claim_record) &&
            string_equal(compiler_sampling_controller_read(arena,
                path_join(arena, resolved.options.evidence, S8("claim.tsv")), 16384), resolved.claim_record) &&
            generate_path_kind(arena, path_join(arena, resolved.claim, S8("execution"))) == GENERATE_PATH_MISSING;
#else
        valid = false;
#endif
    }
    else if (valid)
        valid = generate_path_kind(arena, resolved.claim) == GENERATE_PATH_MISSING &&
            generate_path_kind(arena, resolved.options.evidence) == GENERATE_PATH_MISSING;
    resolved.valid = valid;
    if (valid) *output = resolved;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_claim(Arena* arena,
    CompilerPreparationControllerResolved resolved)
{
    OsDirectoryCreateResult claimed = os_make_directory_exclusive(resolved.claim);
    bool result = claimed.created && !claimed.error.v;
    if (result)
    {
        result = file_write(path_join(arena, resolved.claim, S8("claim.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(resolved.claim_record));
        OsDirectoryCreateResult evidence = result ? os_make_directory_exclusive(resolved.options.evidence) : (OsDirectoryCreateResult){0};
        result = result && evidence.created && !evidence.error.v &&
            file_write(path_join(arena, resolved.options.evidence, S8("claim.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(resolved.claim_record)) &&
            file_write(path_join(arena, resolved.options.evidence, S8("physical-job-clock.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(resolved.job_clock.record));
        String8 names[] = {S8("request.txt"), S8("plan.tsv"), S8("allowlist.tsv"), S8("facts.tsv"), S8("history.tsv")};
        for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(names); i += 1)
            result = file_write(path_join(arena, resolved.options.evidence, names[i]), BUSTER_SLICE_TO_BYTE_SLICE(resolved.transport.bytes[i]));
    }
    // Once claimed, every failure retains the claim and partial evidence.
    return result;
}

// The parent's retained claim permits exactly one private worker, including
// failures before clone creates ROOT. An identical worker cannot replay it.
BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_claim_worker(Arena* arena,
    CompilerPreparationControllerResolved resolved)
{
    String8 path = path_join(arena, resolved.claim, S8("execution"));
    OsDirectoryCreateResult created = resolved.valid && resolved.claim_record.length ?
        os_make_directory_exclusive(path) : (OsDirectoryCreateResult){0};
    bool result = resolved.valid && created.created && !created.error.v &&
        file_write(path_join(arena, path, S8("claim.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(resolved.claim_record));
    return result; // partial directory remains consumed after publication failure
}

BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_host_receipt(Arena* arena,
    CompilerPreparationControllerResolved resolved)
{
    CompilerPreparationAdmission admitted = resolved.admitted;
    String8 facts = resolved.transport.bytes[3];
    String8 text = string_format(arena,
        S8("{{\"schema\":\"buster-compiler-preparation-host-v1\",\"state\":\"complete\","
           "\"cpu_model\":\"{S8}\",\"logical_processor_records\":{u64},\"observed_from\":\"/proc/cpuinfo\","
           "\"request_head\":\"{S8}\",\"run_id\":\"{S8}\",\"run_attempt\":\"1\",\"request_run_id\":\"{S8}\","
           "\"measurement_trusted_revision\":\"{S8}\",\"policy_trusted_revision\":\"{S8}\","
           "\"plan_revision\":\"{S8}\",\"plan_sha256\":\"{S8}\",\"protocol_sha256\":\"{S8}\","
           "\"trusted_lab\":\"{S8}\",\"trusted_lab_sha256\":\"{S8}\",\"python\":\"{S8}\",\"python_sha256\":\"{S8}\","
           "\"native_driver\":\"{S8}\",\"native_driver_sha256\":\"{S8}\"\n}\n"),
        resolved.host.model, resolved.host.records, compiler_sampling_controller_fact(facts, S8("request_head")),
        compiler_sampling_controller_fact(facts, S8("executor_run_id")),
        compiler_sampling_controller_fact(facts, S8("request_run_id")), admitted.trusted_revision, admitted.policy_trusted_revision,
        admitted.freeze_revision, admitted.freeze_sha256, admitted.protocol_sha256,
        resolved.lab, admitted.plan.lab_sha256, resolved.python, admitted.plan.python_sha256,
        resolved.driver, admitted.plan.native_driver_sha256);
    return resolved.host.valid && file_write(path_join(arena, resolved.options.evidence, S8("host.json")), BUSTER_SLICE_TO_BYTE_SLICE(text));
}

// The shared metadata reader reserves its declared limit. Full raw evidence
// contains thousands of small files, so allocate only the observed bounded
// regular-file size and release each copy's scratch bytes immediately.
BUSTER_GLOBAL_LOCAL String8 compiler_preparation_controller_read_data(Arena* arena, String8 path)
{
    String8 result = {0};
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 named = string_duplicate_arena(arena, path, true);
    struct stat before_name = {0}, before = {0}, after = {0}, after_name = {0};
    bool valid = lstat((char*)named.pointer, &before_name) == 0 && S_ISREG(before_name.st_mode);
    int descriptor = valid ? open((char*)named.pointer, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    valid = valid && descriptor >= 0 && fstat(descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
        before.st_size >= 0 && (u64)before.st_size <= BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT &&
        before_name.st_dev == before.st_dev && before_name.st_ino == before.st_ino &&
        before_name.st_size == before.st_size && before_name.st_mode == before.st_mode;
    u64 capacity = valid ? (u64)before.st_size : 0, used = 0, started = os_now_microseconds();
    char8* content = valid ? arena_allocate(arena, char8, capacity + 1) : 0;
    bool eof = false;
    while (valid && !eof)
    {
        valid = os_now_microseconds() - started < 30000000ull;
        ssize_t count = valid ? read(descriptor, content + used, capacity + 1 - used) : -1;
        if (count > 0) { used += (u64)count; valid = used <= capacity; }
        else if (!count) eof = true;
        else valid = valid && errno == EINTR;
    }
    valid = valid && eof && used == capacity && fstat(descriptor, &after) == 0 &&
        lstat((char*)named.pointer, &after_name) == 0 &&
        before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_mode == after.st_mode &&
        before.st_size == after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
        before.st_mtim.tv_nsec == after.st_mtim.tv_nsec && before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
        before.st_ctim.tv_nsec == after.st_ctim.tv_nsec && after.st_dev == after_name.st_dev &&
        after.st_ino == after_name.st_ino && after.st_mode == after_name.st_mode && after.st_size == after_name.st_size;
    if (descriptor >= 0) valid = close(descriptor) == 0 && valid;
    if (valid) result = (String8){content, used};
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(path);
#endif
    return result;
}

// Copy bounded data members only, retaining partial logs on failure. Executable
// binaries and saved source/build trees stay in the persistent native output.
// Five labs may each select up to 1000 pairs: preserve pairs CSV/ccmetrics,
// variant metadata/env/logs and corpus inputs/artifact logs/metrics/jsonl.
// Export bounds are 65536 regular data files, 8MiB/member and 2GiB total;
// exceeding a bound is an invalid export while the original output is retained.
BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_copy_directory(Arena* arena,
    String8 source, String8 destination, u64 depth, u64* files, u64* bytes)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 terminated = string_duplicate_arena(arena, source, true);
    struct stat status = {0};
    bool valid = depth <= 4 && lstat((char*)terminated.pointer, &status) == 0 && S_ISDIR(status.st_mode);
    OsDirectoryCreateResult created = valid ? os_make_directory_exclusive(destination) : (OsDirectoryCreateResult){0};
    valid = valid && created.created && !created.error.v;
    MuslDirectoryEntry* entries = 0;
    u64 count = 0;
    valid = valid && musl_list_directory(arena, source, &entries, &count) && count <= 65536;
    for (u64 i = 0; valid && i < count; i += 1)
    {
        String8 name = {(char8*)entries[i].name.pointer, entries[i].name.length};
        if (string_equal(name, S8(".")) || string_equal(name, S8(".."))) continue;
        String8 input = path_join(arena, source, name), output = path_join(arena, destination, name);
        String8 input_terminated = string_duplicate_arena(arena, input, true);
        struct stat item = {0};
        valid = compiler_sampling_controller_path_safe(name) && lstat((char*)input_terminated.pointer, &item) == 0;
        if (valid && S_ISDIR(item.st_mode))
        {
            bool lab = string_ends_with_sequence(source, S8("-lab"));
            bool corpus = string_ends_with_sequence(source, S8("-throughput"));
            bool variant = string_ends_with_sequence(source, S8("-lab/a")) || string_ends_with_sequence(source, S8("-lab/b"));
            bool allowed = depth == 0 ? string_equal(name, S8("legacy")) || string_equal(name, S8("snapshot")) :
                depth == 1 ? string_equal(name, S8("ab-lab")) || string_equal(name, S8("immutable-aa-lab")) ||
                    string_equal(name, S8("cross-build-aa-lab")) || string_equal(name, S8("ab-throughput")) ||
                    string_equal(name, S8("immutable-aa-throughput")) || string_equal(name, S8("cross-build-aa-throughput")) :
                depth == 2 ? (lab && (string_equal(name, S8("a")) || string_equal(name, S8("b")) || string_equal(name, S8("pairs")))) ||
                    (corpus && (string_equal(name, S8("artifacts")) || string_equal(name, S8("inputs")))) :
                depth == 3 && variant && string_equal(name, S8("env"));
            bool executable_storage = (depth == 1 && (string_equal(name, S8("bin")) || string_equal(name, S8("frozen-baseline")))) ||
                (depth == 3 && variant && string_equal(name, S8("instances")));
            valid = allowed ? compiler_preparation_controller_copy_directory(arena, input, output, depth + 1, files, bytes) :
                executable_storage;
        }
        else if (valid && S_ISREG(item.st_mode))
        {
            bool allowed = string_ends_with_sequence(name, S8(".json")) || string_ends_with_sequence(name, S8(".tsv")) ||
                string_ends_with_sequence(name, S8(".txt")) || string_ends_with_sequence(name, S8(".argv")) ||
                string_ends_with_sequence(name, S8(".stdout")) || string_ends_with_sequence(name, S8(".stderr")) ||
                string_ends_with_sequence(name, S8(".csv")) ||
                string_ends_with_sequence(name, S8(".jsonl")) || string_ends_with_sequence(name, S8(".log")) ||
                string_ends_with_sequence(name, S8(".err")) || string_ends_with_sequence(name, S8(".ccmetrics")) ||
                string_ends_with_sequence(name, S8(".metrics")) || string_ends_with_sequence(name, S8(".md")) ||
                (string_ends_with_sequence(source, S8("-throughput/inputs")) && string_ends_with_sequence(name, S8(".c")));
            if (allowed)
            {
                valid = item.st_size >= 0 && (u64)item.st_size <= BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT &&
                    !(item.st_mode & 0111) && *files < 65536 && *bytes + (u64)item.st_size <= (2ull << 30);
                TemporalArena data = scratch_begin(&arena, 1);
                String8 content = valid ? compiler_preparation_controller_read_data(data.arena, input) : (String8){0};
                valid = valid && content.pointer && content.length == (u64)item.st_size &&
                    file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(content));
                *files += valid ? 1 : 0;
                *bytes += valid ? content.length : 0;
                scratch_end(data);
            }
            else
            {
                // These compiler products are intentionally not executable
                // artifact inputs to the hosted consumer. Every raw data type
                // or unexpected member must be retained or reject the export.
                bool compiler_product = string_ends_with_sequence(name, S8(".exe")) ||
                    string_ends_with_sequence(name, S8(".o")) || string_ends_with_sequence(name, S8(".obj")) ||
                    string_ends_with_sequence(name, S8(".s"));
                valid = depth >= 2 && compiler_product;
            }
        }
        else if (valid) valid = false; // no symlinks, devices or sockets
    }
    result = valid;
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(source); BUSTER_UNUSED(destination); BUSTER_UNUSED(depth);
    BUSTER_UNUSED(files); BUSTER_UNUSED(bytes);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_export(Arena* arena,
    CompilerPreparationControllerResolved resolved)
{
    u64 files = 0, bytes = 0;
    return compiler_preparation_controller_copy_directory(arena, resolved.admitted.plan.output_root,
        path_join(arena, resolved.options.evidence, S8("qualification")), 0, &files, &bytes);
}

// Persist the actual argv before taking the manager lease. The reused phase
// helper owns cleanup and cannot start another phase after uncertain ownership.
BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_phase(CompilerSamplingController* controller,
    String8 name, SliceString8 arguments, u64 cap_us)
{
    String8 path = path_join(controller->arena, controller->evidence,
        string_format(controller->arena, S8("controller-{u64}-{S8}.argv"), controller->stage + 1, name));
    bool recorded = file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(production_profile_argv_text(controller->arena, arguments)));
    controller->success = controller->success && recorded;
    return compiler_sampling_controller_phase(controller, name, arguments, cap_us);
}

BUSTER_GLOBAL_LOCAL String8 compiler_preparation_controller_tree(CompilerSamplingController* controller,
    String8 revision, String8 name)
{
    String8 tail[] = {S8("-C"), controller->plan.source_root, S8("rev-parse"),
        string_format(controller->arena, S8("{S8}{S8}"), revision, S8("^{tree}"))};
    bool complete = compiler_preparation_controller_phase(controller, name,
        compiler_sampling_controller_git(controller->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(tail)), 120000000ull);
    String8 result = complete ? production_profile_trim(controller->last_output) : (String8){0};
    controller->success = controller->success && compiler_sampling_hex(result, 40);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_preparation_controller_worker(Arena* arena,
    CompilerPreparationControllerResolved resolved)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && !BUSTER_ANDROID
    bool claimed = compiler_preparation_controller_claim_worker(arena, resolved);
    if (claimed)
    {
        CompilerSamplingController controller = {.arena = arena, .evidence = resolved.options.evidence,
            .started = os_now_microseconds(), .success = resolved.valid};
        controller.plan.source_root = resolved.admitted.plan.source_root;
        controller.deadline = controller.started + compiler_experiment_job_clock_remaining_us(resolved.job_clock,
            BUSTER_PREPARATION_PHYSICAL_SECONDS * 1000000ull, BUSTER_PREPARATION_WORKER_SECONDS * 1000000ull);
        string8_list_push(arena, &controller.phases, S8("stage\tphase\twall_us\texit_status\ttimed_out\tcleanup_failed\tcancelled\tstate\n"));
        CompilerSamplingSignalScope signals = {0};
        bool deferred = compiler_sampling_signals_begin(&signals);
        controller.success = controller.success && deferred && compiler_preparation_controller_host_receipt(arena, resolved);
        String8 trusted_pin[] = {S8("-C"), resolved.options.trusted_root, S8("rev-parse"), S8("HEAD")};
        compiler_preparation_controller_phase(&controller, S8("trusted-harness-pin"),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(trusted_pin)), 120000000ull);
        controller.success = controller.success &&
            string_equal(production_profile_trim(controller.last_output), resolved.admitted.trusted_revision);
        String8 clone[] = {S8("clone"), S8("--no-checkout"), S8("--no-tags"),
            S8("https://github.com/buster14a/buster.git"), resolved.admitted.plan.source_root};
        compiler_preparation_controller_phase(&controller, S8("clone-preparation-source"),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clone)), 120000000ull);
        String8 fetch[] = {S8("-C"), resolved.admitted.plan.source_root, S8("fetch"), S8("--no-tags"), S8("origin"),
            resolved.admitted.plan.baseline_revision, resolved.admitted.plan.candidate_revision};
        compiler_preparation_controller_phase(&controller, S8("fetch-preparation-pins"),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fetch)), 120000000ull);
        String8 base_tree = compiler_preparation_controller_tree(&controller, resolved.admitted.plan.baseline_revision, S8("baseline-tree"));
        String8 head_tree = compiler_preparation_controller_tree(&controller, resolved.admitted.plan.candidate_revision, S8("candidate-tree"));
        controller.success = controller.success && string_equal(base_tree, resolved.admitted.plan.baseline_tree) &&
            string_equal(head_tree, resolved.admitted.plan.candidate_tree);
        String8 checkout[] = {S8("-C"), resolved.admitted.plan.source_root, S8("checkout"), S8("--detach"),
            resolved.admitted.plan.candidate_revision};
        compiler_preparation_controller_phase(&controller, S8("candidate-checkout"),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(checkout)), 120000000ull);
        bool tools_before = controller.success && compiler_preparation_controller_tools(arena, resolved);
        controller.success = controller.success && tools_before;
        String8 qualify[] = {resolved.driver, S8("compiler_closure"), S8("qualify"),
            resolved.admitted.plan.source_root, resolved.admitted.plan.output_root,
            resolved.admitted.plan.baseline_revision, resolved.admitted.plan.baseline_tree,
            resolved.admitted.plan.candidate_revision, resolved.admitted.plan.candidate_tree, resolved.lab, resolved.python};
        compiler_preparation_controller_phase(&controller, S8("legacy-snapshot-five-long-controls"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(qualify), BUSTER_PREPARATION_WORKER_SECONDS * 1000000ull);
        bool tools_after = compiler_preparation_controller_tools(arena, resolved);
        controller.success = controller.success && tools_after;
        // Export partial proof only after the owned manager lease and all adopted
        // descendants are quiet. Unknown cleanup retains the persistent output.
        bool exported = !controller.cleanup_failed &&
            generate_path_kind(arena, resolved.admitted.plan.output_root) != GENERATE_PATH_MISSING &&
            compiler_preparation_controller_export(arena, resolved);
        controller.success = controller.success && exported;
        bool restored = deferred && compiler_sampling_signals_end(&signals);
        controller.success = controller.success && restored && !compiler_sampling_controller_cancelled() &&
            os_now_microseconds() <= controller.deadline;
        String8 terminal = string_format(arena,
            S8("schema\tbuster-compiler-preparation-controller-v1\nphase\tqualify\npacket\t0\nplan_sha256\t{S8}\n"
               "process_state\t{S8}\nqualification_state\tunvalidated\ndefault_activated\tfalse\n"
               "duration_us\t{u64}\ncleanup_proven\t{S8}\nsource_root\t{S8}\noutput_root\t{S8}\n"
               "tools_before\t{S8}\ntools_after\t{S8}\nexported\t{S8}\n"),
            resolved.admitted.freeze_sha256, controller.success ? S8("complete") : S8("failed"),
            os_now_microseconds() - controller.started, controller.cleanup_failed ? S8("false") : S8("true"),
            resolved.admitted.plan.source_root, resolved.admitted.plan.output_root,
            tools_before ? S8("true") : S8("false"), tools_after ? S8("true") : S8("false"), exported ? S8("true") : S8("false"));
        bool recorded = compiler_sampling_controller_flush(&controller) &&
            file_write(path_join(arena, resolved.options.evidence, S8("preparation.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(terminal)) &&
            file_write(path_join(arena, resolved.claim, S8("preparation.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(terminal));
        // The outer manager also charges this publication, but the worker's own
        // deadline must not be represented by a pre-publication success check.
        bool within_after_publication = os_now_microseconds() <= controller.deadline;
        if (recorded && !within_after_publication)
        {
            String8 failed = string_format(arena,
                S8("schema\tbuster-compiler-preparation-controller-v1\nphase\tqualify\npacket\t0\n"
                   "plan_sha256\t{S8}\nprocess_state\tfailed\nqualification_state\tunvalidated\n"
                   "default_activated\tfalse\nreason\tworker-publication-overrun\nduration_us\t{u64}\n"
                   "cleanup_proven\t{S8}\nsource_root\t{S8}\noutput_root\t{S8}\n"),
                resolved.admitted.freeze_sha256, os_now_microseconds() - controller.started,
                controller.cleanup_failed ? S8("false") : S8("true"),
                resolved.admitted.plan.source_root, resolved.admitted.plan.output_root);
            file_write(path_join(arena, resolved.options.evidence, S8("preparation.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(failed));
            file_write(path_join(arena, resolved.claim, S8("preparation.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(failed));
        }
        result = controller.success && recorded && within_after_publication ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else string_print(S8("error: preparation private worker claim already consumed or publication failed; retained attempt cannot retry\n"));
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(resolved);
#endif
    return result;
}

// Freeze the contiguous builder before any supervisor, signal or other arena
// operation can be introduced by callers.
BUSTER_GLOBAL_LOCAL SliceString8 compiler_preparation_controller_owner_arguments(Arena* arena,
    String8 driver, SliceString8 arguments)
{
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    os_argument_builder_append(&builder, driver);
    os_argument_builder_append(&builder, S8("compiler_profile_qualification"));
    for (u64 i = 0; i < arguments.length; i += 1) os_argument_builder_append(&builder, arguments.pointer[i]);
    os_argument_builder_append(&builder, S8("--owned-worker"));
    return os_argument_builder_flush(&builder);
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_preparation_controller_owned(Arena* arena,
    CompilerPreparationControllerResolved resolved, SliceString8 arguments, u64 started)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && !BUSTER_ANDROID
    bool claimed = compiler_preparation_controller_claim(arena, resolved);
    CompilerExperimentSupervisor supervisor = {0};
    bool contained = claimed && compiler_experiment_supervisor_begin(arena, &supervisor);
    CompilerSamplingSignalScope signals = {0};
    bool deferred = contained && compiler_sampling_signals_begin(&signals);
    ProcessSpawnResult spawn = {0};
    bool launch_attempted = false;
    ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
    ProcessGroupControlState control = {.cancellation_signal = &compiler_sampling_cancel_signal,
        .cancellation_escalated = &compiler_sampling_cancel_escalated};
    SliceString8 command = compiler_preparation_controller_owner_arguments(arena, resolved.driver, arguments);
    if (deferred && !compiler_sampling_controller_cancelled())
    {
        launch_attempted = true;
        spawn = os_process_spawn(command, (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.use_process_environment = 1, .new_process_group = 1, .observe_resources = 1});
        if (spawn.handle)
        {
            spawn.process_group_control = &control;
            u64 remaining = compiler_experiment_job_clock_remaining_us(resolved.job_clock,
                BUSTER_PREPARATION_PHYSICAL_SECONDS * 1000000ull, BUSTER_PREPARATION_WORKER_SECONDS * 1000000ull);
            wait = os_process_wait_deadline(arena, spawn, remaining ? remaining : 1);
        }
    }
    bool released = (!launch_attempted || (spawn.handle && wait.result != PROCESS_RESULT_UNKNOWN)) &&
        !wait.process_tree_cleanup_failed && !wait.process_group_reservation_retained && !wait.process_group_ownership_lost;
    bool cleanup = contained && compiler_experiment_supervisor_end_known(arena, &supervisor, released);
    bool cancelled = compiler_sampling_controller_cancelled();
    bool complete = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.platform_status && !wait.timed_out &&
        cleanup && !cancelled && !supervisor.signalled && !supervisor.reaped;
    bool restored = deferred && compiler_sampling_signals_end(&signals);
    complete = complete && restored;
    u64 publication_started = os_now_microseconds();
    u64 wall = publication_started - started;
    bool within = compiler_experiment_job_clock_remaining_us(resolved.job_clock,
        BUSTER_PREPARATION_PHYSICAL_SECONDS * 1000000ull, BUSTER_PREPARATION_PHYSICAL_SECONDS * 1000000ull) != 0;
    String8 owner = string_format(arena,
        S8("schema\tbuster-compiler-preparation-owner-v1\nphase\tqualify\npacket\t0\nplan_sha256\t{S8}\n"
           "physical_packet_wall_us\t{u64}\nwall_scope\tentry-through-child-cleanup-before-terminal-publication\n"
           "process_state\t{S8}\ntimed_out\t{u64}\ncleanup_failed\t{u64}\n"
           "within_reservation\t{S8}\ncancelled\t{u64}\nqualification_state\tunvalidated\ndefault_activated\tfalse\n"),
        resolved.admitted.freeze_sha256, wall, complete ? S8("complete") : S8("failed"),
        (u64)wait.timed_out, (u64)!cleanup, within ? S8("true") : S8("false"), (u64)cancelled);
    bool recorded = claimed && compiler_sampling_supervision_receipt(arena,
        path_join(arena, resolved.options.evidence, S8("owner-supervision.tsv")), supervisor, cleanup, wall) &&
        file_write(path_join(arena, resolved.options.evidence, S8("owner.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(owner)) &&
        file_write(path_join(arena, resolved.claim, S8("owner.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(owner));
    u64 observed_wall = os_now_microseconds() - started;
    String8 publication = string_format(arena,
        S8("schema\tbuster-compiler-preparation-owner-publication-v1\nowner_sha256\t{S8}\n"
           "scope\tentry-through-owner-publication\ninitial_scope_us\t{u64}\npublication_us\t{u64}\n"
           "observed_wall_us\t{u64}\nobservation_publication_us\tunavailable\nwithin_reservation\t{S8}\n"),
        stage_object_sha256_bytes(arena, (u8*)owner.pointer, owner.length), wall, observed_wall - wall, observed_wall,
        compiler_experiment_job_clock_remaining_us(resolved.job_clock, BUSTER_PREPARATION_PHYSICAL_SECONDS * 1000000ull,
            BUSTER_PREPARATION_PHYSICAL_SECONDS * 1000000ull) ? S8("true") : S8("false"));
    bool publication_recorded = recorded &&
        file_write(path_join(arena, resolved.options.evidence, S8("owner-publication.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(publication)) &&
        file_write(path_join(arena, resolved.claim, S8("owner-publication.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(publication));
    // No successful return may exclude either receipt's publication from the
    // actual hard clock guard. The last observation receipt cannot self-time;
    // its tail remains explicitly unavailable, never an invented zero.
    bool within_after_publication = compiler_experiment_job_clock_remaining_us(resolved.job_clock,
        BUSTER_PREPARATION_PHYSICAL_SECONDS * 1000000ull, BUSTER_PREPARATION_PHYSICAL_SECONDS * 1000000ull) != 0;
    if (claimed && !within_after_publication)
    {
        String8 failed = string_format(arena,
            S8("schema\tbuster-compiler-preparation-owner-v1\nphase\tqualify\npacket\t0\nplan_sha256\t{S8}\n"
               "physical_packet_wall_us\t{u64}\nwall_scope\tobserved-failed-publication-overrun\n"
               "process_state\tfailed\ntimed_out\t{u64}\ncleanup_failed\t{u64}\nwithin_reservation\tfalse\n"
               "cancelled\t{u64}\nqualification_state\tunvalidated\ndefault_activated\tfalse\n"),
            resolved.admitted.freeze_sha256, os_now_microseconds() - started,
            (u64)wait.timed_out, (u64)!cleanup, (u64)cancelled);
        file_write(path_join(arena, resolved.options.evidence, S8("owner.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(failed));
        file_write(path_join(arena, resolved.claim, S8("owner.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(failed));
    }
    result = complete && within && recorded && publication_recorded && within_after_publication ?
        PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(resolved); BUSTER_UNUSED(arguments); BUSTER_UNUSED(started);
#endif
    return result;
}

#if BUSTER_LINUX && !BUSTER_ANDROID
// Fixed native fixture: two workers of the same retained attempt race before
// source/output exist. The owned exact PID is reaped before another fixture.
BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_worker_once_fixture(Arena* arena,
    CompilerPreparationControllerResolved resolved)
{
    String8 record = resolved.claim_record;
    bool serial = compiler_preparation_controller_claim_worker(arena, resolved) &&
        !compiler_preparation_controller_claim_worker(arena, resolved) &&
        string_equal(compiler_sampling_controller_read(arena,
            path_join(arena, path_join(arena, resolved.claim, S8("execution")), S8("claim.tsv")), 16384), record);
    CompilerPreparationControllerResolved raced = resolved;
    raced.claim = string_format(arena, S8("{S8}-concurrent"), resolved.claim);
    OsDirectoryCreateResult made = os_make_directory_exclusive(raced.claim);
    bool result = serial && made.created && !made.error.v;
    int report[2] = {-1, -1}, release[2] = {-1, -1};
    result = result && pipe2(report, O_CLOEXEC) == 0 && pipe2(release, O_CLOEXEC) == 0;
    CompilerExperimentSupervisor supervisor = {0};
    bool began = result && compiler_experiment_supervisor_begin(arena, &supervisor);
    pid_t child = began ? fork() : -1;
    if (!child)
    {
        close(report[0]); close(release[1]);
        u8 ready = 1, go = 0;
        bool valid = write(report[1], &ready, 1) == 1 && read(release[0], &go, 1) == 1 && go == 1;
        u8 won = valid && compiler_preparation_controller_claim_worker(arena, raced) ? 1 : 0;
        bool sent = valid && write(report[1], &won, 1) == 1;
        close(report[1]); close(release[0]);
        _exit(sent ? 0 : 7);
    }
    bool parent_won = false;
    bool direct_child_quiet = child < 0;
    u8 ready = 0, child_won = 2;
    if (child > 1)
    {
        close(report[1]); report[1] = -1;
        close(release[0]); release[0] = -1;
        struct pollfd event = {.fd = report[0], .events = POLLIN};
        bool ready_read = poll(&event, 1, 5000) > 0 && read(report[0], &ready, 1) == 1 && ready == 1;
        u8 go = 1;
        bool released = ready_read && write(release[1], &go, 1) == 1;
        parent_won = released && compiler_preparation_controller_claim_worker(arena, raced);
        bool got = released && poll(&event, 1, 5000) > 0 && read(report[0], &child_won, 1) == 1 && child_won <= 1;
        bool reaped = compiler_experiment_supervisor_fixture_reap(child, os_now_microseconds() + 5000000ull);
        direct_child_quiet = reaped;
        result = result && got && reaped && ((parent_won ? 1u : 0u) + child_won == 1) &&
            !compiler_preparation_controller_claim_worker(arena, raced) &&
            string_equal(compiler_sampling_controller_read(arena,
                path_join(arena, path_join(arena, raced.claim, S8("execution")), S8("claim.tsv")), 16384), record);
    }
    else result = false;
    for (u64 i = 0; i < 2; i += 1)
    {
        if (report[i] >= 0) close(report[i]);
        if (release[i] >= 0) close(release[i]);
    }
    bool quiet = began && compiler_experiment_supervisor_end_known(arena, &supervisor, direct_child_quiet) &&
        !supervisor.signalled && !supervisor.reaped;
    return result && quiet;
}
#endif

BUSTER_GLOBAL_LOCAL bool compiler_preparation_controller_self_test(Arena* arena)
{
    String8 exact[] = {S8("--execute-preparation"), S8("--trusted-root"), S8("/trusted"),
        S8("--cleanup-root"), S8("/cleanup"), S8("--evidence"), S8("/cleanup/evidence")};
    CompilerPreparationControllerOptions parsed = compiler_preparation_controller_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(exact));
    SliceString8 owner_args = compiler_preparation_controller_owner_arguments(arena, S8("/fixture/driver"),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(exact));
    bool owner_argv = owner_args.length == 10 && string_equal(owner_args.pointer[0], S8("/fixture/driver")) &&
        string_equal(owner_args.pointer[1], S8("compiler_profile_qualification")) &&
        string_equal(owner_args.pointer[2], S8("--execute-preparation")) &&
        string_equal(owner_args.pointer[9], S8("--owned-worker"));
    bool result = owner_argv && parsed.valid && !parsed.owned_worker &&
        string_equal(parsed.trusted_root, S8("/trusted")) &&
        BUSTER_PREPARATION_PHYSICAL_SECONDS == 5400 && BUSTER_PREPARATION_WORKER_SECONDS == 5280 &&
        BUSTER_PREPARATION_TAIL_SECONDS == 120;
    String8 worker[] = {S8("--execute-preparation"), S8("--trusted-root"), S8("/trusted"),
        S8("--cleanup-root"), S8("/cleanup"), S8("--evidence"), S8("/cleanup/evidence"), S8("--owned-worker")};
    result = result && compiler_preparation_controller_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(worker)).owned_worker;
    worker[7] = S8("--unknown");
    result = result && !compiler_preparation_controller_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(worker)).valid;
    exact[3] = S8("--trusted-root");
    result = result && !compiler_preparation_controller_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(exact)).valid;
    String8 decoded = {0};
    result = result && compiler_sampling_controller_base64(arena, S8("eAo="), false, &decoded) &&
        string_equal(decoded, S8("x\n")) && !compiler_sampling_controller_base64(arena, S8("eAp="), false, &decoded);
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 directory = {0};
    bool owned = summary_self_test_claim_directory(arena, S8("preparation-controller"), &directory);
    result = result && owned;
    if (owned)
    {
        CompilerSamplingController phase = {.arena = arena, .evidence = directory,
            .started = os_now_microseconds(), .deadline = os_now_microseconds() + 5000000ull, .success = true};
        string8_list_push(arena, &phase.phases, S8("stage\tphase\twall_us\texit_status\ttimed_out\tcleanup_failed\tcancelled\tstate\n"));
        String8 okay[] = {S8("/bin/sh"), S8("-c"), S8("printf preparation-phase")};
        bool first = compiler_preparation_controller_phase(&phase, S8("preparation-fixture"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(okay), 2000000ull);
        String8 failing[] = {S8("/bin/sh"), S8("-c"), S8("exit 7")};
        bool failed = !compiler_preparation_controller_phase(&phase, S8("failed-preparation-fixture"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(failing), 2000000ull);
        bool stopped = !compiler_preparation_controller_phase(&phase, S8("no-next-preparation"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(okay), 2000000ull);
        result = result && first && failed && stopped && !phase.success && !phase.cleanup_failed;
        CompilerPreparationControllerResolved claim_fixture = {0};
        claim_fixture.options.evidence = path_join(arena, directory, S8("claimed-evidence"));
        claim_fixture.claim = path_join(arena, directory, S8("persistent-claim"));
        claim_fixture.driver = S8("/fixture/trusted-driver");
        claim_fixture.valid = true;
        claim_fixture.admitted.freeze_revision = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        claim_fixture.admitted.freeze_sha256 = S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        claim_fixture.admitted.trusted_revision = S8("cccccccccccccccccccccccccccccccccccccccc");
        claim_fixture.admitted.policy_trusted_revision = S8("dddddddddddddddddddddddddddddddddddddddd");
        claim_fixture.admitted.plan.source_root = S8("/fixture/source");
        claim_fixture.admitted.plan.output_root = S8("/fixture/output");
        for (u64 i = 0; i < 5; i += 1) claim_fixture.transport.bytes[i] = S8("fixture\n");
        claim_fixture.claim_record = compiler_preparation_controller_claim_record(arena, claim_fixture);
        bool claimed = compiler_preparation_controller_claim(arena, claim_fixture);
        bool once = claimed && !compiler_preparation_controller_claim(arena, claim_fixture) &&
            string_equal(compiler_sampling_controller_read(arena,
                path_join(arena, claim_fixture.claim, S8("claim.tsv")), 16384), claim_fixture.claim_record);
        bool worker_once = once && compiler_preparation_controller_worker_once_fixture(arena, claim_fixture);
        String8 source = path_join(arena, directory, S8("raw"));
        String8 directories[] = {S8(""), S8("legacy"), S8("legacy/ab-lab"), S8("legacy/ab-lab/a"),
            S8("legacy/ab-lab/a/env"), S8("legacy/ab-lab/b"), S8("legacy/ab-lab/pairs"),
            S8("legacy/ab-throughput"), S8("legacy/ab-throughput/artifacts"), S8("legacy/ab-throughput/inputs"),
            S8("legacy/bin"), S8("legacy/frozen-baseline"), S8("snapshot")};
        bool raw = true;
        for (u64 i = 0; raw && i < BUSTER_ARRAY_LENGTH(directories); i += 1)
        {
            String8 path = directories[i].length ? path_join(arena, source, directories[i]) : source;
            OsDirectoryCreateResult made = os_make_directory_exclusive(path);
            raw = made.created && !made.error.v;
        }
        String8 members[] = {S8("qualification.json"), S8("legacy/prepared.json"), S8("legacy/preparation-cost.json"),
            S8("legacy/1-pins.stderr"), S8("legacy/ab-lab/compare.json"), S8("legacy/ab-lab/a/lab.json"),
            S8("legacy/ab-lab/a/commands.log"), S8("legacy/ab-lab/a/warmup-0.log"),
            S8("legacy/ab-lab/a/env/lscpu.txt"), S8("legacy/ab-lab/pairs/0001-a.csv"),
            S8("legacy/ab-lab/pairs/0001-a.ccmetrics"), S8("legacy/ab-lab/pairs/0001-a.err"),
            S8("legacy/ab-throughput/commands.jsonl"), S8("legacy/ab-throughput/capabilities.jsonl"),
            S8("legacy/ab-throughput/complete.txt"), S8("legacy/ab-throughput/artifacts/sample.log"),
            S8("legacy/ab-throughput/artifacts/sample.metrics"), S8("legacy/ab-throughput/inputs/generated.c")};
        String8 data = S8("retained raw fixture\n");
        for (u64 i = 0; raw && i < BUSTER_ARRAY_LENGTH(members); i += 1)
        {
            String8 value = i == 3 ? (String8){0} : data;
            raw = file_write(path_join(arena, source, members[i]), BUSTER_SLICE_TO_BYTE_SLICE(value));
        }
        String8 product = path_join(arena, source, S8("legacy/ab-throughput/artifacts/generated.exe"));
        raw = raw && file_write(product, BUSTER_SLICE_TO_BYTE_SLICE(data)) && chmod((char*)product.pointer, 0755) == 0;
        u64 files = 0, bytes = 0;
        String8 copied = path_join(arena, directory, S8("export"));
        bool exported = raw && compiler_preparation_controller_copy_directory(arena, source, copied, 0, &files, &bytes) &&
            files == BUSTER_ARRAY_LENGTH(members);
        for (u64 i = 0; exported && i < BUSTER_ARRAY_LENGTH(members); i += 1)
        {
            String8 actual = compiler_preparation_controller_read_data(arena, path_join(arena, copied, members[i]));
            exported = actual.pointer && string_equal(actual, i == 3 ? (String8){0} : data);
        }
        bool products_excluded = exported &&
            generate_path_kind(arena, path_join(arena, copied, S8("legacy/ab-throughput/artifacts/generated.exe"))) == GENERATE_PATH_MISSING &&
            generate_path_kind(arena, path_join(arena, copied, S8("legacy/bin"))) == GENERATE_PATH_MISSING;
        raw = raw && file_write(path_join(arena, source, S8("legacy/ab-lab/pairs/unexpected.raw")), BUSTER_SLICE_TO_BYTE_SLICE(data));
        files = 0; bytes = 0;
        bool rejected = raw && !compiler_preparation_controller_copy_directory(arena, source,
            path_join(arena, directory, S8("reject-export")), 0, &files, &bytes);
        result = result && once && worker_once && exported && products_excluded && rejected && os_directory_delete(directory);
        string_print(S8("COMPILER_PREPARATION_CONTROLLER_SELF_TEST claim_once={u64} worker_once_serial_concurrent={u64} full_raw_export={u64} "
            "empty_log={u64} executable_excluded={u64} unexpected_raw_rejected={u64} no_next_phase={u64}\n"),
            (u64)once, (u64)worker_once, (u64)exported, (u64)exported, (u64)products_excluded, (u64)rejected, (u64)stopped);
    }
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_preparation_qualification_main(Arena* arena, SliceString8 arguments)
{
    u64 started = os_now_microseconds();
    CompilerPreparationControllerOptions options = compiler_preparation_controller_parse(arguments);
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (options.valid && options.self_test)
        result = compiler_preparation_controller_self_test(arena) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    else if (options.valid)
    {
        CompilerPreparationControllerResolved resolved = {0};
        if (compiler_preparation_controller_resolve(arena, options, &resolved))
            result = options.owned_worker ? compiler_preparation_controller_worker(arena, resolved) :
                compiler_preparation_controller_owned(arena, resolved, arguments, started);
        else string_print(S8("error: preparation qualification rejected admission, platform or immutable identity before fetching\n"));
    }
    return result;
}
#endif // BUSTER_COMPILER_PREPARATION_CONTROLLER_INCLUDED
