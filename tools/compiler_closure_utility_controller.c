// Once-only native ordinary-path closure utility. Include after shared sampling/
// preparation primitives and pure compiler_closure_utility_admission.c.
// Entry: compiler_closure_utility_main. *_resolve observes canonical host/tools;
// *_claim and *_claim_worker consume this attempt before any child; *_worker
// runs exactly legacy then snapshot; *_export retains bounded ordinary/raw data.
// Native leg clocks include reset/bootstrap through export/hash finalization.
// Only the authenticated publisher may charge whole-job residual or assess net
// utility. Process success never activates snapshot or qualifies performance.
#ifndef BUSTER_COMPILER_CLOSURE_UTILITY_CONTROLLER_INCLUDED
#define BUSTER_COMPILER_CLOSURE_UTILITY_CONTROLLER_INCLUDED

typedef struct CompilerClosureUtilityControllerOptions CompilerClosureUtilityControllerOptions;
struct CompilerClosureUtilityControllerOptions
{
    String8 trusted_root, cleanup_root, evidence;
    bool owned_worker, self_test, valid;
};

typedef struct CompilerClosureUtilityControllerTransport CompilerClosureUtilityControllerTransport;
struct CompilerClosureUtilityControllerTransport { String8 bytes[5]; bool valid; };

typedef struct CompilerClosureUtilityControllerResolved CompilerClosureUtilityControllerResolved;
struct CompilerClosureUtilityControllerResolved
{
    CompilerClosureUtilityControllerOptions options;
    CompilerClosureUtilityControllerTransport transport;
    CompilerClosureUtilityAdmission admitted;
    CompilerSamplingControllerHost host;
    CompilerExperimentJobClock job_clock;
    String8 workspace, driver, lab, python, protocol, comparator, receipt_adapter, owned_phase;
    String8 claim, claim_record, pull, bootstrap_marker_sha256;
    bool diagnostic, valid;
};

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_fixture_allowed(Arena* arena);

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_claim_ready(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    bool result=resolved.valid && compiler_experiment_cleanup_guard(arena);
#if BUSTER_LINUX && !BUSTER_ANDROID
    result=result && (resolved.diagnostic ? compiler_closure_utility_fixture_allowed(arena) :
        compiler_experiment_job_clock_remaining_us(resolved.job_clock,5400000000ull,5280000000ull)!=0);
#else
    result=false;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerClosureUtilityControllerOptions compiler_closure_utility_controller_parse(SliceString8 arguments)
{
    CompilerClosureUtilityControllerOptions result = {0};
    if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test-utility-controller")))
    {
        result.self_test = true;
        result.valid = true;
    }
    else if ((arguments.length == 7 || arguments.length == 8) &&
        string_equal(arguments.pointer[0], S8("--execute-utility")) &&
        string_equal(arguments.pointer[1], S8("--trusted-root")) &&
        string_equal(arguments.pointer[3], S8("--cleanup-root")) &&
        string_equal(arguments.pointer[5], S8("--evidence")) &&
        (arguments.length == 7 || string_equal(arguments.pointer[7], S8("--owned-utility-worker"))))
    {
        result.trusted_root = arguments.pointer[2];
        result.cleanup_root = arguments.pointer[4];
        result.evidence = arguments.pointer[6];
        result.owned_worker = arguments.length == 8;
        result.valid = result.trusted_root.length && result.cleanup_root.length && result.evidence.length;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerClosureUtilityControllerTransport compiler_closure_utility_controller_transport(Arena* arena)
{
    CompilerClosureUtilityControllerTransport result = {.valid = true};
    String8 names[] = {S8("BQ_UTILITY_REQUEST_DATA"), S8("BQ_UTILITY_PLAN_DATA"),
        S8("BQ_UTILITY_ALLOWLIST_DATA"), S8("BQ_UTILITY_FACTS_DATA"), S8("BQ_UTILITY_HISTORY_DATA")};
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

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_attempt(Arena* arena,
    CompilerClosureUtilityControllerTransport transport, CompilerClosureUtilityAdmission admitted)
{
    String8 facts = transport.bytes[3];
    String8 names[] = {S8("GITHUB_RUN_ID"), S8("GITHUB_RUN_ATTEMPT"), S8("GITHUB_REPOSITORY"), S8("GITHUB_SHA"),
        S8("BQ_REQUEST_RUN_ID"), S8("BQ_REQUEST_ATTEMPT"), S8("BQ_HEAD_COMMIT"),
        S8("BQ_UTILITY_PHASE"), S8("BQ_UTILITY_PACKET"), S8("BQ_UTILITY_FAMILY"),
        S8("BQ_UTILITY_PLAN_REVISION"), S8("BQ_UTILITY_PLAN_SHA256"), S8("BQ_UTILITY_PROTOCOL_SHA256"),
        S8("BQ_UTILITY_BASE"), S8("BQ_UTILITY_BASE_TREE"),
        S8("BQ_UTILITY_CANDIDATE_REVISION"), S8("BQ_UTILITY_CANDIDATE_TREE"),
        S8("BQ_UTILITY_PULL_HEAD"), S8("BQ_UTILITY_TRUSTED_REVISION"), S8("BQ_UTILITY_RESERVATION_SECONDS"),
        S8("BQ_UTILITY_WORKER_SECONDS"), S8("BQ_UTILITY_TIMEOUT_MINUTES")};
    String8 values[] = {compiler_sampling_controller_fact(facts, S8("executor_run_id")), S8("1"),
        S8("buster14a/buster"), admitted.policy_trusted_revision,
        compiler_sampling_controller_fact(facts, S8("request_run_id")), S8("1"),
        compiler_sampling_controller_fact(facts, S8("request_head")), S8("utility"), S8("0"), S8("utility"),
        admitted.freeze_revision, admitted.freeze_sha256, admitted.protocol_sha256,
        admitted.plan.baseline_revision, admitted.plan.baseline_tree,
        admitted.plan.candidate_revision, admitted.plan.candidate_tree, admitted.plan.pull_head, admitted.trusted_revision,
        S8("5400"), S8("5280"), S8("90")};
    bool result = admitted.valid && transport.valid;
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(names); i += 1)
        result = values[i].length && compiler_sampling_controller_environment_matches(names[i], values[i], true);
    BUSTER_UNUSED(arena);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_closure_utility_controller_claim_record(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    CompilerClosureUtilityAdmission admitted = resolved.admitted;
    String8 facts = resolved.transport.bytes[3];
    String8 hashes[5] = {0};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(hashes); i += 1)
        hashes[i] = stage_object_sha256_bytes(arena, (u8*)resolved.transport.bytes[i].pointer, resolved.transport.bytes[i].length);
    String8 result = string_format(arena,
        S8("schema\tbuster-compiler-closure-utility-claim-v1\nprofile\tcompiler-baseline-closure-utility-v1\n"
           "phase\tutility\npacket\t0\nplan_revision\t{S8}\nplan_sha256\t{S8}\n"
           "request_run_id\t{S8}\nrequest_run_attempt\t1\nexecutor_run_id\t{S8}\nexecutor_run_attempt\t1\n"
           "request_head\t{S8}\npolicy_trusted_revision\t{S8}\nmeasurement_trusted_revision\t{S8}\n"
           "source_root\t{S8}\noutput_root\t{S8}\nevidence\t{S8}\ndriver\t{S8}\n"
           "request_sha256\t{S8}\nplan_transport_sha256\t{S8}\nallowlist_sha256\t{S8}\nfacts_sha256\t{S8}\nhistory_sha256\t{S8}\n"
           "pull\t{S8}\npull_head\t{S8}\nbootstrap_marker_sha256\t{S8}\n"
           "physical_job_clock_sha256\t{S8}\nreservation_seconds\t5400\nworker_seconds\t5280\ntail_seconds\t120\nstate\tclaimed\n"),
        admitted.freeze_revision, admitted.freeze_sha256,
        compiler_sampling_controller_fact(facts, S8("request_run_id")),
        compiler_sampling_controller_fact(facts, S8("executor_run_id")),
        compiler_sampling_controller_fact(facts, S8("request_head")), admitted.policy_trusted_revision, admitted.trusted_revision,
        admitted.plan.source_root, admitted.plan.output_root, resolved.options.evidence, resolved.driver,
        hashes[0], hashes[1], hashes[2], hashes[3], hashes[4], resolved.pull, admitted.plan.pull_head, resolved.bootstrap_marker_sha256,
        stage_object_sha256_bytes(arena, (u8*)resolved.job_clock.record.pointer, resolved.job_clock.record.length));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_paths(Arena* arena,
    CompilerClosureUtilityControllerOptions options, CompilerClosureUtilityPlan plan,
    CompilerClosureUtilityControllerResolved* resolved)
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
        compiler_closure_utility_plan_paths_outside(plan, cleanup, workspace) &&
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
    resolved->protocol = path_join(arena, trusted, S8("docs/compiler-closure-utility-v1.md"));
    resolved->python = plan.python_path;
    resolved->comparator = path_join(arena, trusted, S8("tools/bench_direct/compiler_compare.py"));
    resolved->receipt_adapter = path_join(arena, trusted, S8("tools/bench_direct/compiler_receipt.py"));
    resolved->owned_phase = path_join(arena, trusted, S8("tools/bench_direct/compiler_owned_phase.py"));
    bool pull_present = false;
    resolved->pull = compiler_sampling_controller_environment(S8("BQ_PULL"), &pull_present);
    u64 pull = 0;
    result = result && pull_present && compiler_sampling_admission_decimal(resolved->pull, &pull) && pull;
    resolved->claim = claim;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_tools(Arena* arena,
    CompilerClosureUtilityControllerResolved* resolved, bool initial)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 paths[] = {resolved->lab, resolved->python, resolved->driver, resolved->protocol,
        resolved->comparator, resolved->receipt_adapter, resolved->owned_phase};
    String8 hashes[] = {resolved->admitted.plan.lab_sha256, resolved->admitted.plan.python_sha256,
        resolved->admitted.plan.native_driver_sha256, resolved->admitted.plan.protocol_sha256,
        resolved->admitted.plan.comparator_sha256, resolved->admitted.plan.receipt_sha256,
        resolved->admitted.plan.owned_phase_sha256};
    result = true;
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(paths); i += 1)
    {
        String8 digest = {0};
        struct stat status = {0};
        result = paths[i].length && string_equal(os_path_absolute(arena, paths[i], true), paths[i]) &&
            compiler_sampling_controller_hash(arena, paths[i], &digest, &status) &&
            string_equal(digest, hashes[i]) && (i != 1 && i != 2 ? true : (status.st_mode & 0111) != 0);
    }
    String8 head = compiler_sampling_controller_read(arena,
        path_join(arena, resolved->options.trusted_root, S8(".git/HEAD")), 64);
    result = result && string_equal(production_profile_trim(head), resolved->admitted.trusted_revision);
    CompilerClosureBootstrapIdentity bootstrap = {0};
    String8 cache = path_join(arena, resolved->options.trusted_root, S8(".cache/bootstrap-driver"));
    result = result && compiler_closure_bootstrap_identity(arena, resolved->options.trusted_root,
        resolved->options.trusted_root, cache, &bootstrap) &&
        string_equal(path_join(arena, cache, bootstrap.artifact), resolved->driver) &&
        string_equal(bootstrap.artifact_sha256, resolved->admitted.plan.native_driver_sha256);
    if (result && initial) resolved->bootstrap_marker_sha256 = bootstrap.marker_sha256;
    result = result && string_equal(bootstrap.marker_sha256, resolved->bootstrap_marker_sha256);
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(resolved); BUSTER_UNUSED(initial);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_resolve(Arena* arena,
    CompilerClosureUtilityControllerOptions options, CompilerClosureUtilityControllerResolved* output)
{
    CompilerClosureUtilityControllerResolved resolved = {0};
    resolved.transport = compiler_closure_utility_controller_transport(arena);
    bool temp_present = false, workspace_present = false;
    String8 temp = compiler_sampling_controller_environment(S8("RUNNER_TEMP"), &temp_present);
    String8 workspace = compiler_sampling_controller_environment(S8("GITHUB_WORKSPACE"), &workspace_present);
    String8 cleanup = temp_present ? os_path_absolute(arena, temp, true) : (String8){0};
    workspace = workspace_present ? os_path_absolute(arena, workspace, true) : (String8){0};
    resolved.admitted = compiler_closure_utility_admission_validate(arena,
        resolved.transport.bytes[2], resolved.transport.bytes[0], resolved.transport.bytes[3],
        resolved.transport.bytes[4], resolved.transport.bytes[1], cleanup, workspace);
    bool valid = options.valid && resolved.transport.valid && resolved.admitted.valid &&
        compiler_closure_utility_controller_attempt(arena, resolved.transport, resolved.admitted) &&
        compiler_closure_utility_controller_paths(arena, options, resolved.admitted.plan, &resolved);
    resolved.host = compiler_sampling_controller_observed_host(arena);
    valid = valid && resolved.host.valid && compiler_experiment_cleanup_guard(arena) &&
        compiler_experiment_job_clock_resolve(arena, S8("utility"), resolved.admitted.policy_trusted_revision, &resolved.job_clock) &&
        compiler_experiment_job_clock_remaining_us(resolved.job_clock, 5400000000ull, 5280000000ull) &&
        compiler_closure_utility_controller_tools(arena, &resolved, true);
    resolved.claim_record = valid ? compiler_closure_utility_controller_claim_record(arena, resolved) : (String8){0};
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

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_claim(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    OsDirectoryCreateResult claimed = compiler_closure_utility_controller_claim_ready(arena,resolved) ?
        os_make_directory_exclusive(resolved.claim) : (OsDirectoryCreateResult){0};
    bool result = claimed.created && !claimed.error.v;
    if (result)
    {
        result = file_write(path_join(arena, resolved.claim, S8("claim.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(resolved.claim_record));
        OsDirectoryCreateResult evidence = result ? os_make_directory_exclusive(resolved.options.evidence) : (OsDirectoryCreateResult){0};
        result = result && evidence.created && !evidence.error.v &&
            file_write(path_join(arena, resolved.options.evidence, S8("claim.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(resolved.claim_record));
        if (result && !resolved.diagnostic) result = file_write(path_join(arena, resolved.options.evidence, S8("physical-job-clock.tsv")),
            BUSTER_SLICE_TO_BYTE_SLICE(resolved.job_clock.record));
        String8 names[] = {S8("request.txt"), S8("plan.tsv"), S8("allowlist.tsv"), S8("facts.tsv"), S8("history.tsv")};
        for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(names); i += 1)
            result = file_write(path_join(arena, resolved.options.evidence, names[i]), BUSTER_SLICE_TO_BYTE_SLICE(resolved.transport.bytes[i]));
    }
    // Once claimed, every failure retains the claim and partial evidence.
    return result;
}

// The parent's retained claim permits exactly one private worker, including
// failures before clone creates ROOT. An identical worker cannot replay it.
BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_claim_worker(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    String8 path = path_join(arena, resolved.claim, S8("execution"));
    OsDirectoryCreateResult created = resolved.valid && resolved.claim_record.length && compiler_closure_utility_controller_claim_ready(arena,resolved) ?
        os_make_directory_exclusive(path) : (OsDirectoryCreateResult){0};
    bool result = resolved.valid && created.created && !created.error.v &&
        file_write(path_join(arena, path, S8("claim.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(resolved.claim_record));
    return result; // partial directory remains consumed after publication failure
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_host_receipt(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    CompilerClosureUtilityAdmission admitted = resolved.admitted;
    String8 facts = resolved.transport.bytes[3];
    String8 text = string_format(arena,
        S8("{{\"schema\":\"buster-compiler-closure-utility-host-v1\",\"state\":\"complete\","
           "\"cpu_model\":\"{S8}\",\"logical_processor_records\":{u64},\"observed_from\":\"/proc/cpuinfo\","
           "\"request_head\":\"{S8}\",\"run_id\":\"{S8}\",\"run_attempt\":\"1\",\"request_run_id\":\"{S8}\","
           "\"measurement_trusted_revision\":\"{S8}\",\"policy_trusted_revision\":\"{S8}\","
           "\"plan_revision\":\"{S8}\",\"plan_sha256\":\"{S8}\",\"protocol_sha256\":\"{S8}\","
           "\"trusted_lab\":\"{S8}\",\"trusted_lab_sha256\":\"{S8}\",\"python\":\"{S8}\",\"python_sha256\":\"{S8}\","
           "\"native_driver\":\"{S8}\",\"native_driver_sha256\":\"{S8}\",\"bootstrap_marker_sha256\":\"{S8}\","
           "\"comparator\":\"{S8}\",\"comparator_sha256\":\"{S8}\",\"receipt_adapter\":\"{S8}\",\"receipt_sha256\":\"{S8}\","
           "\"owned_phase\":\"{S8}\",\"owned_phase_sha256\":\"{S8}\"\n}\n"),
        resolved.host.model, resolved.host.records, compiler_sampling_controller_fact(facts, S8("request_head")),
        compiler_sampling_controller_fact(facts, S8("executor_run_id")),
        compiler_sampling_controller_fact(facts, S8("request_run_id")), admitted.trusted_revision, admitted.policy_trusted_revision,
        admitted.freeze_revision, admitted.freeze_sha256, admitted.protocol_sha256,
        resolved.lab, admitted.plan.lab_sha256, resolved.python, admitted.plan.python_sha256,
        resolved.driver, admitted.plan.native_driver_sha256, resolved.bootstrap_marker_sha256,
        resolved.comparator, admitted.plan.comparator_sha256, resolved.receipt_adapter, admitted.plan.receipt_sha256,
        resolved.owned_phase, admitted.plan.owned_phase_sha256);
    return (resolved.host.valid || resolved.diagnostic) && file_write(path_join(arena, resolved.options.evidence, S8("host.json")), BUSTER_SLICE_TO_BYTE_SLICE(text));
}

// The shared metadata reader reserves its declared limit. Full raw evidence
// contains thousands of small files, so allocate only the observed bounded
// regular-file size and release each copy's scratch bytes immediately.

#if BUSTER_LINUX && !BUSTER_ANDROID
#define BUSTER_UTILITY_EXPORT_FILES 65536ull
#define BUSTER_UTILITY_EXPORT_BYTES (2ull << 30)
#define BUSTER_UTILITY_EXPORT_DEPTH 8ull

typedef struct CompilerClosureUtilityExportTotals CompilerClosureUtilityExportTotals;
struct CompilerClosureUtilityExportTotals { u64 files, bytes, deadline; bool diagnostic; };
typedef struct CompilerClosureUtilityExportDirectory CompilerClosureUtilityExportDirectory;
struct CompilerClosureUtilityExportDirectory { String8 source, destination, relative; u64 depth; };

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_named(String8 name, SliceString8 names)
{
    bool result = false;
    for (u64 i = 0; i < names.length; i += 1) result = result || string_equal(name, names.pointer[i]);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_member(String8 relative, bool directory, bool* skip)
{
    *skip = false;
    bool result = false;
    String8 local = relative;
    if (string_starts_with_sequence(local, S8("ordinary/"))) local = string_slice(local, 9, local.length);
    if (directory && (string_equal(relative, S8("ordinary")) || string_equal(local, S8("lab")) ||
        string_equal(local, S8("throughput")) || string_equal(local, S8("owned-phases")))) result = true;
    else if (string_starts_with_sequence(local, S8("lab/")))
    {
        String8 member = string_slice(local, 4, local.length);
        if (directory)
        {
            String8 allowed[] = {S8("a"), S8("b"), S8("pairs"), S8("a/env"), S8("b/env")};
            result = compiler_closure_utility_controller_named(member, (SliceString8)BUSTER_ARRAY_TO_SLICE(allowed));
            *skip = string_equal(member, S8("a/instances")) || string_equal(member, S8("b/instances"));
        }
        else if (string_starts_with_sequence(member, S8("pairs/")))
        {
            String8 name = string_slice(member, 6, member.length);
            bool numbered = name.length >= 10 && name.pointer[4] == '-' && (name.pointer[5] == 'a' || name.pointer[5] == 'b');
            for (u64 i = 0; numbered && i < 4; i += 1) numbered = name.pointer[i] >= '0' && name.pointer[i] <= '9';
            result = numbered && (string_ends_with_sequence(name, S8(".csv")) || string_ends_with_sequence(name, S8(".ccmetrics")) ||
                string_ends_with_sequence(name, S8(".err")) || string_ends_with_sequence(name, S8(".log")));
        }
        else if (string_starts_with_sequence(member, S8("a/")) || string_starts_with_sequence(member, S8("b/")))
        {
            String8 name = string_slice(member, 2, member.length);
            String8 allowed[] = {S8("lab.json"), S8("commands.log"), S8("perf-probe.csv"), S8("probe.ccmetrics"),
                S8("metrics-probe.log"), S8("warmup.ccmetrics"), S8("probe.c"), S8("wrapper.csv"), S8("wrapper-rss.log"),
                S8("source.metrics"), S8("source-run.log"), S8("plain-run.log"), S8("env/env.json"),
                S8("env/lscpu.txt"), S8("env/metricgroups.txt"), S8("env/cpuinfo.txt"), S8("stdout.log"), S8("stderr.log")};
            result = compiler_closure_utility_controller_named(name, (SliceString8)BUSTER_ARRAY_TO_SLICE(allowed)) ||
                (string_starts_with_sequence(name, S8("warmup-")) && string_ends_with_sequence(name, S8(".log")));
            *skip = string_equal(name, S8("out.exe")) || string_equal(name, S8("reference.exe"));
        }
        else
        {
            String8 allowed[] = {S8("compare.json"), S8("pairs.json"), S8("summary.json"), S8("report.md"), S8("report.txt"),
                S8("report.json"), S8("summary.txt")};
            result = compiler_closure_utility_controller_named(member, (SliceString8)BUSTER_ARRAY_TO_SLICE(allowed));
        }
    }
    else if (string_starts_with_sequence(local, S8("throughput/")))
    {
        String8 member = string_slice(local, 11, local.length);
        if (directory) result = string_equal(member, S8("inputs")) || string_equal(member, S8("artifacts"));
        else if (string_starts_with_sequence(member, S8("inputs/"))) result = string_ends_with_sequence(member, S8(".c"));
        else if (string_starts_with_sequence(member, S8("artifacts/")))
        {
            result = string_ends_with_sequence(member, S8(".metrics")) || string_ends_with_sequence(member, S8(".log")) ||
                string_ends_with_sequence(member, S8(".err"));
            *skip = string_ends_with_sequence(member, S8(".exe")) || string_ends_with_sequence(member, S8(".o")) ||
                string_ends_with_sequence(member, S8(".obj")) || string_ends_with_sequence(member, S8(".s"));
        }
        else
        {
            String8 allowed[] = {S8("summary.json"), S8("metadata.json"), S8("samples.csv"), S8("telemetry.csv"), S8("jobs.tsv"),
                S8("commands.jsonl"), S8("capabilities.jsonl"), S8("complete.txt"), S8("summary.txt"), S8("summary.md"), S8("report.txt"),
                S8("cpuinfo.txt"), S8("kernel.txt"), S8("affinity-and-host-status.txt"), S8("perf_event_paranoid.txt"),
                S8("cpu-quota.txt"), S8("cpuset.txt"), S8("smt.txt"), S8("governor.txt"), S8("frequency-driver.txt"), S8("host.txt")};
            result = compiler_closure_utility_controller_named(member, (SliceString8)BUSTER_ARRAY_TO_SLICE(allowed));
        }
    }
    else if (string_starts_with_sequence(local, S8("owned-phases/")))
    {
        String8 name = string_slice(local, 13, local.length);
        bool numbered = name.length >= 9;
        for (u64 i = 0; numbered && i < 4; i += 1) numbered = name.pointer[i] >= '0' && name.pointer[i] <= '9';
        String8 suffix = numbered ? string_slice(name, 4, name.length) : (String8){0};
        String8 allowed[] = {S8(".json"), S8(".json.argv"), S8(".json.stdout"), S8(".json.stderr"), S8(".json.bootstrap.complete")};
        result = numbered && (directory ? string_equal(suffix, S8(".json.claim")) :
            compiler_closure_utility_controller_named(suffix, (SliceString8)BUSTER_ARRAY_TO_SLICE(allowed)));
    }
    else if (!directory)
    {
        String8 allowed[] = {S8("receipt.json"), S8(".buster-compiler-compare-scratch"), S8("build.log"), S8("lab.log"),
            S8("throughput.log"), S8("baseline.CMakeCache.txt"), S8("candidate.CMakeCache.txt"), S8("closure.log"),
            S8("closure-snapshot.json"), S8("closure-restore.json"), S8("closure-verify.json"),
            S8("closure-snapshot.json.manifest.tsv"), S8("closure-restore.json.manifest.tsv"), S8("closure-verify.json.manifest.tsv"),
            S8("inventory.json"), S8("inventory.manifest.tsv"), S8("cleanup-uncertain")};
        result = compiler_closure_utility_controller_named(local, (SliceString8)BUSTER_ARRAY_TO_SLICE(allowed));
    }
    return result || *skip;
}

// Explicit bounded worklist: no input-dependent recursion and no executable
// storage. Empty native claim directories are represented in the manifest.
BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_copy(Arena* arena, String8 source, String8 destination,
    String8 relative, CompilerClosureUtilityExportTotals* totals, String8List* manifest)
{
    CompilerClosureUtilityExportDirectory* queue = arena_allocate(arena, CompilerClosureUtilityExportDirectory, BUSTER_UTILITY_EXPORT_FILES);
    u64 queued = 1;
    queue[0] = (CompilerClosureUtilityExportDirectory){source, destination, relative, 0};
    bool valid = os_now_microseconds() < totals->deadline;
    String8 last_member = relative;
    for (u64 at = 0; valid && at < queued; at += 1)
    {
        CompilerClosureUtilityExportDirectory current = queue[at];
        last_member = current.relative;
        String8 terminated = string_duplicate_arena(arena, current.source, true);
        struct stat status = {0};
        valid = current.depth <= BUSTER_UTILITY_EXPORT_DEPTH && lstat((char*)terminated.pointer, &status) == 0 &&
            S_ISDIR(status.st_mode) && string_equal(os_path_absolute(arena, current.source, true), current.source);
        OsDirectoryCreateResult made = valid ? os_make_directory_exclusive(current.destination) : (OsDirectoryCreateResult){0};
        valid = valid && made.created && !made.error.v;
        if (valid) string8_list_push(arena, manifest, string_format(arena, S8("D\t{S8}\t-\t0\t{u64}\n"),
            current.relative, (u64)(status.st_mode & 07777)));
        MuslDirectoryEntry* entries = 0;
        u64 count = 0;
        valid = valid && compiler_closure_list(arena, current.source, BUSTER_UTILITY_EXPORT_FILES, &entries, &count);
        if (valid) compiler_closure_sort(entries, count);
        for (u64 i = 0; valid && i < count; i += 1)
        {
            String8 name = entries[i].name;
            String8 input = path_join(arena, current.source, name), output = path_join(arena, current.destination, name);
            String8 member = path_join(arena, current.relative, name);
            last_member = member;
            String8 named = string_duplicate_arena(arena, input, true);
            struct stat info = {0};
            valid = compiler_sampling_controller_path_safe(name) && member.length <= 1024 &&
                lstat((char*)named.pointer, &info) == 0;
            bool skip = false;
            valid = valid && compiler_closure_utility_controller_member(member, S_ISDIR(info.st_mode), &skip);
            if (valid && !skip && S_ISDIR(info.st_mode))
            {
                valid = current.depth < BUSTER_UTILITY_EXPORT_DEPTH && queued < BUSTER_UTILITY_EXPORT_FILES;
                if (valid) queue[queued++] = (CompilerClosureUtilityExportDirectory){input, output, member, current.depth + 1};
            }
            else if (valid && !skip && S_ISREG(info.st_mode))
            {
                valid = !(info.st_mode & 0111) && info.st_size >= 0 &&
                    (u64)info.st_size <= BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT && totals->files < BUSTER_UTILITY_EXPORT_FILES &&
                    (u64)info.st_size <= BUSTER_UTILITY_EXPORT_BYTES - totals->bytes;
                TemporalArena data = scratch_begin(&arena, 1);
                String8 content = valid ? compiler_preparation_controller_read_data(data.arena, input) : (String8){0};
                struct stat after = {0};
                valid = valid && content.pointer && content.length == (u64)info.st_size &&
                    lstat((char*)named.pointer, &after) == 0 && S_ISREG(after.st_mode) && !(after.st_mode & 0111) &&
                    info.st_dev == after.st_dev && info.st_ino == after.st_ino && info.st_mode == after.st_mode &&
                    info.st_size == after.st_size && info.st_mtim.tv_sec == after.st_mtim.tv_sec &&
                    info.st_mtim.tv_nsec == after.st_mtim.tv_nsec && info.st_ctim.tv_sec == after.st_ctim.tv_sec &&
                    info.st_ctim.tv_nsec == after.st_ctim.tv_nsec && file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(content));
                if (valid)
                {
                    String8 digest = stage_object_sha256_bytes(arena, (u8*)content.pointer, content.length);
                    string8_list_push(arena, manifest, string_format(arena, S8("F\t{S8}\t{S8}\t{u64}\t{u64}\n"),
                        member, digest, content.length, (u64)(info.st_mode & 07777)));
                    totals->files += 1; totals->bytes += content.length;
                }
                scratch_end(data);
            }
            else if (valid && !skip) valid = false;
            // Retain stable partial data after cancellation without admitting a child.
            valid = valid && os_now_microseconds() < totals->deadline;
        }
    }
    if (!valid && totals->diagnostic)
        string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_EXPORT_FAILED source={S8} member={S8} "
            "copied_files={u64} copied_bytes={u64} within_deadline={u64} physical_qualification=false\n"),
            source, last_member, totals->files, totals->bytes, (u64)(os_now_microseconds() < totals->deadline));
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_export(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved, String8 leg, CompilerClosureUtilityExportTotals* totals,
    String8* export_sha256, String8* receipt_sha256)
{
    String8 store = resolved.admitted.plan.output_root;
    String8 target = path_join(arena, path_join(arena, resolved.options.evidence, S8("utility")), leg);
    OsDirectoryCreateResult made = os_make_directory_exclusive(target);
    bool result = made.created && !made.error.v;
    String8List lines = {0};
    string8_list_push(arena, &lines, S8("BUSTER_COMPILER_CLOSURE_UTILITY_EXPORT_V1\n"));
    String8 ordinary = path_join(arena, store, string_format(arena, S8("{S8}-evidence"), leg));
    String8 work = path_join(arena, store, string_format(arena, S8("{S8}-work"), leg));
    // Each available tree is attempted independently after a failed leg. A
    // missing/unsafe tree leaves an explicitly incomplete export, not a skip.
    String8 sources[] = {ordinary, path_join(arena, work, S8("lab")), path_join(arena, work, S8("throughput"))};
    String8 components[] = {S8("ordinary"), S8("lab"), S8("throughput")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        bool copied = made.created && !made.error.v && os_now_microseconds() < totals->deadline &&
            compiler_closure_utility_controller_copy(arena, sources[i], path_join(arena, target, components[i]),
                components[i], totals, &lines);
        result = result && copied;
        if (resolved.diagnostic)
            string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_EXPORT_COMPONENT leg={S8} component={S8} copied={u64}\n"),
                leg, components[i], (u64)copied);
    }
    String8 names[] = {S8("inventory.json"), S8("inventory.manifest.tsv")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        String8 input = path_join(arena, store, string_format(arena, S8("{S8}-{S8}"), leg, names[i]));
        TemporalArena data = scratch_begin(&arena, 1);
        String8 content = compiler_preparation_controller_read_data(data.arena, input);
        String8 digest = {0};
        struct stat status = {0};
        bool copied = made.created && !made.error.v && os_now_microseconds() < totals->deadline &&
            content.pointer && content.length && compiler_sampling_controller_hash(arena, input, &digest, &status) &&
            !(status.st_mode & 0111) && string_equal(digest, stage_object_sha256_bytes(arena, (u8*)content.pointer, content.length)) &&
            content.length <= BUSTER_UTILITY_EXPORT_BYTES - totals->bytes && totals->files < BUSTER_UTILITY_EXPORT_FILES &&
            file_write(path_join(arena, target, names[i]), BUSTER_SLICE_TO_BYTE_SLICE(content));
        if (copied)
        {
            string8_list_push(arena, &lines, string_format(arena, S8("F\t{S8}\t{S8}\t{u64}\t{u64}\n"),
                names[i], digest, content.length, (u64)(status.st_mode & 07777)));
            totals->files += 1; totals->bytes += content.length;
        }
        result = result && copied;
        if (resolved.diagnostic)
            string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_EXPORT_COMPONENT leg={S8} component={S8} copied={u64}\n"),
                leg, names[i], (u64)copied);
        scratch_end(data);
    }
    String8 manifest = string_join_arena(arena, string8_list_to_slice(arena, lines), false);
    String8 manifest_path = path_join(arena, resolved.options.evidence, string_format(arena, S8("utility-{S8}-export.tsv"), leg));
    bool written = made.created && !made.error.v && manifest.length <= BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT &&
        file_write(manifest_path, BUSTER_SLICE_TO_BYTE_SLICE(manifest));
    result = result && written;
    if (result)
    {
        struct stat status = {0};
        result = compiler_sampling_controller_hash(arena, manifest_path, export_sha256, &status) &&
            compiler_sampling_controller_hash(arena, path_join(arena, ordinary, S8("receipt.json")), receipt_sha256, &status);
    }
    return result;
}


#endif

BUSTER_GLOBAL_LOCAL u64 compiler_closure_utility_controller_budget(CompilerClosureUtilityControllerResolved resolved,
    u64 started, u64 worker_budget)
{
    u64 result;
    if (!resolved.diagnostic)
        result=compiler_experiment_job_clock_remaining_us(resolved.job_clock,5400000000ull,worker_budget);
    else
    {
        // Private hosted mode has no platform occupancy authority.
        u64 spent=os_now_microseconds()-started;
        result=spent<worker_budget ? worker_budget-spent : 0;
    }
    return result;
}

#if BUSTER_LINUX && !BUSTER_ANDROID
BUSTER_GLOBAL_LOCAL void compiler_closure_utility_controller_cancel(int signal)
{
    compiler_sampling_cancel_handler(signal);
    compiler_closure_cancel(signal);
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_signals_begin(CompilerSamplingSignalScope* scope)
{
    // Both owner APIs read their existing flags; never reset a stopped attempt.
    bool result = !compiler_sampling_controller_cancelled() && compiler_closure_admitting() &&
        !compiler_closure_signals_owned && compiler_sampling_signals_begin(scope);
    if (result)
    {
        struct sigaction action = {.sa_handler = compiler_closure_utility_controller_cancel};
        bool installed = sigemptyset(&action.sa_mask) == 0 && sigaction(SIGTERM, &action, 0) == 0 &&
            sigaction(SIGINT, &action, 0) == 0;
        if (!installed)
        {
            bool term = sigaction(SIGTERM, &scope->old_term, 0) == 0;
            bool interrupt = sigaction(SIGINT, &scope->old_int, 0) == 0;
            if (term && interrupt) scope->active = false;
        }
        result = installed;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_signals_end(CompilerSamplingSignalScope* scope)
{
    bool result=false;
    if (scope->active)
    {
        // Attempt both restorations even if the first sigaction fails.
        bool term=sigaction(SIGTERM,&scope->old_term,0)==0;
        bool interrupt=sigaction(SIGINT,&scope->old_int,0)==0;
        result=term && interrupt;
        if (result) scope->active=false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_unknown(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    String8 root = resolved.admitted.plan.output_root;
    String8 paths[] = {path_join(arena, resolved.options.evidence, S8("cleanup-uncertain")),
        path_join(arena, resolved.claim, S8("cleanup-uncertain")),
        path_join(arena, path_join(arena, root, S8("legacy-evidence")), S8("cleanup-uncertain")),
        path_join(arena, path_join(arena, root, S8("snapshot-evidence")), S8("cleanup-uncertain"))};
    bool unknown = compiler_closure_cleanup_failed || !compiler_experiment_cleanup_guard(arena);
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(paths); i += 1)
        unknown = unknown || generate_path_kind(arena, paths[i]) != GENERATE_PATH_MISSING;
    return unknown;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_latch(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved, String8 reason)
{
    // A quiet outer wait cannot turn a missing inner ownership proof into a
    // recovered attempt. Preserve a host-wide guard and both permanent claims.
    compiler_closure_cleanup_failed = true;
    bool guarded = compiler_experiment_cleanup_latch(arena, reason);
    bool evidence = file_write(path_join(arena, resolved.options.evidence, S8("cleanup-uncertain")),
        BUSTER_SLICE_TO_BYTE_SLICE(reason));
    bool claimed = file_write(path_join(arena, resolved.claim, S8("cleanup-uncertain")),
        BUSTER_SLICE_TO_BYTE_SLICE(reason));
    return guarded && evidence && claimed;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_ready(CompilerSamplingController* controller,
    CompilerClosureUtilityControllerResolved* resolved)
{
    bool unknown = controller->cleanup_failed || compiler_closure_utility_controller_unknown(controller->arena, *resolved);
    if (unknown)
    {
        controller->cleanup_failed = true;
        compiler_closure_utility_controller_latch(controller->arena, *resolved,
            S8("Native or ordinary bridge cleanup unproven; retain all utility roots and admit no next child.\n"));
    }
    String8 source = resolved->admitted.plan.source_root, output = resolved->admitted.plan.output_root;
    u64 source_kind = generate_path_kind(controller->arena, source);
    bool roots = (source_kind == GENERATE_PATH_MISSING || (source_kind == GENERATE_PATH_DIRECTORY &&
        string_equal(os_path_absolute(controller->arena, source, true), source))) &&
        generate_path_kind(controller->arena, output) == GENERATE_PATH_DIRECTORY &&
        string_equal(os_path_absolute(controller->arena, output, true), output);
    bool result = controller->success && roots && !unknown && compiler_closure_admitting() &&
        (resolved->diagnostic || compiler_experiment_job_clock_remaining_us(resolved->job_clock,5400000000ull,5280000000ull)) &&
        !compiler_sampling_controller_cancelled() && os_now_microseconds() < controller->deadline &&
        compiler_closure_utility_controller_tools(controller->arena, resolved, false);
    controller->success = controller->success && result;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_phase(CompilerSamplingController* controller,
    CompilerClosureUtilityControllerResolved* resolved, String8 name, SliceString8 arguments, u64 cap_us)
{
    bool ready = compiler_closure_utility_controller_ready(controller, resolved);
    bool result = ready && compiler_preparation_controller_phase(controller, name, arguments, cap_us);
    bool unknown = controller->cleanup_failed || compiler_closure_utility_controller_unknown(controller->arena, *resolved);
    if (unknown)
    {
        controller->cleanup_failed = true;
        compiler_closure_utility_controller_latch(controller->arena, *resolved,
            S8("Utility owned child or bridge cleanup unproven; permanent host guard, roots and attempt retained.\n"));
    }
    bool intact = compiler_closure_utility_controller_tools(controller->arena, resolved, false);
    controller->success = controller->success && result && intact && !unknown &&
        compiler_closure_admitting() && !compiler_sampling_controller_cancelled();
    if (resolved->diagnostic)
    {
        string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_PHASE stage={u64} phase={S8} ready={u64} "
            "child_complete={u64} tools_intact={u64} cleanup_unknown={u64} success={u64}\n"),
            controller->stage, name, (u64)ready, (u64)result, (u64)intact, (u64)unknown, (u64)controller->success);
        if (ready && !controller->success)
        {
            String8 path = path_join(controller->arena, controller->evidence,
                string_format(controller->arena, S8("controller-{u64}-{S8}.stderr.log"), controller->stage, name));
            String8 error = compiler_sampling_controller_read(controller->arena, path, 8192);
            string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_PHASE_STDERR bytes={u64} phase={S8}\n{S8}\n"),
                error.length, name, error);
            String8 output_path = path_join(controller->arena, controller->evidence,
                string_format(controller->arena, S8("controller-{u64}-{S8}.stdout.log"), controller->stage, name));
            String8 output = compiler_sampling_controller_read(controller->arena, output_path, 8192);
            string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_PHASE_STDOUT bytes={u64} phase={S8}\n{S8}\n"),
                output.length, name, output);
        }
    }
    return controller->success;
}

BUSTER_GLOBAL_LOCAL String8 compiler_closure_utility_controller_revision(CompilerSamplingController* controller,
    CompilerClosureUtilityControllerResolved* resolved, String8 expression, String8 phase)
{
    String8 tail[] = {S8("-C"), resolved->admitted.plan.source_root, S8("rev-parse"), expression};
    bool complete = compiler_closure_utility_controller_phase(controller, resolved, phase,
        compiler_sampling_controller_git(controller->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(tail)), 120000000ull);
    String8 result = complete ? production_profile_trim(controller->last_output) : (String8){0};
    controller->success = controller->success && compiler_sampling_hex(result, 40);
    return result;
}

BUSTER_GLOBAL_LOCAL SliceString8 compiler_closure_utility_controller_compare(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved, String8 leg, bool snapshot)
{
    OsArgumentBuilder builder;
    CompilerClosureUtilityPlan plan = resolved.admitted.plan;
    String8 facts = resolved.transport.bytes[3];
    String8 common[] = {resolved.python, S8("-B"), resolved.comparator, S8("--candidate"), plan.source_root,
        S8("--lab"), resolved.lab, S8("--work"), path_join(arena, plan.output_root, string_format(arena, S8("{S8}-work"), leg)),
        S8("--evidence"), path_join(arena, plan.output_root, string_format(arena, S8("{S8}-evidence"), leg)),
        S8("--summary"), path_join(arena, plan.output_root, string_format(arena, S8("{S8}.md"), leg)),
        S8("--closure-policy"), snapshot ? S8("snapshot-v1") : S8("legacy-rebuild")};
    // Allocate all arguments before constructing the contiguous builder.
    String8 identity[] = {S8("--mode"), S8("main"), S8("--repository"), S8("buster14a/buster"), S8("--ref"), S8("refs/heads/main"),
        S8("--pull"), resolved.pull, S8("--pull-head"), plan.pull_head, S8("--base"), plan.baseline_revision,
        S8("--base-tree"), plan.baseline_tree, S8("--head"), plan.candidate_revision, S8("--head-tree"), plan.candidate_tree,
        S8("--trusted-revision"), plan.trusted_revision, S8("--request-run-id"),
        compiler_sampling_controller_fact(facts, S8("request_run_id")), S8("--run-id"),
        compiler_sampling_controller_fact(facts, S8("executor_run_id")), S8("--run-attempt"), S8("1")};
    builder = os_argument_builder_start(arena);
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(common); i += 1) os_argument_builder_append(&builder, common[i]);
    if (snapshot)
    {
        os_argument_builder_append(&builder, S8("--closure-driver"));
        os_argument_builder_append(&builder, resolved.driver);
    }
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(identity); i += 1) os_argument_builder_append(&builder, identity[i]);
    return os_argument_builder_flush(&builder);
}

// Final closure inventory uses the actual production native routine and its
// four supervised metadata/resource probes. It is an in-process call, never a
// fabricated subprocess argv. Its complete wall and cleanup belong to the leg.
BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_inventory(CompilerSamplingController* controller,
    CompilerClosureUtilityControllerResolved* resolved, String8 leg, String8List* ledger, String8* inventory_sha256)
{
    bool ready = compiler_closure_utility_controller_ready(controller, resolved);
    u64 started = os_now_microseconds(), cleanup = compiler_closure_cleanup_us, waves = compiler_closure_cleanup_waves,
        signalled = compiler_closure_cleanup_signalled, reaped = compiler_closure_cleanup_reaped;
    CompilerClosureUtilityPlan plan = resolved->admitted.plan;
    String8 manifest = ready ? compiler_closure_inventory(controller->arena, plan.source_root, plan.source_root,
        path_join(controller->arena, plan.source_root, S8("build")),
        path_join(controller->arena, plan.source_root, S8(".cache/bootstrap-driver")),
        plan.baseline_revision, plan.baseline_tree) : (String8){0};
    bool unknown = compiler_closure_utility_controller_unknown(controller->arena, *resolved);
    if (unknown)
    {
        controller->cleanup_failed = true;
        compiler_closure_utility_controller_latch(controller->arena, *resolved,
            S8("Utility native inventory owner cleanup unproven; permanent guard and all roots retained.\n"));
    }
    String8 output = path_join(controller->arena, plan.output_root, string_format(controller->arena, S8("{S8}-inventory.manifest.tsv"), leg));
    struct stat status = {0};
    bool result = ready && manifest.length && !unknown && compiler_closure_admitting() &&
        !compiler_sampling_controller_cancelled() && file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(manifest)) &&
        compiler_sampling_controller_hash(controller->arena, output, inventory_sha256, &status) &&
        compiler_closure_utility_controller_tools(controller->arena, resolved, false);
    u64 wall = os_now_microseconds() - started;
    String8 text = string_format(controller->arena,
        S8("{{\"schema\":\"buster-compiler-closure-utility-inventory-v1\",\"state\":\"{S8}\","
           "\"leg\":\"{S8}\",\"call\":\"compiler_closure_inventory\",\"root\":\"{S8}\","
           "\"base\":\"{S8}\",\"base_tree\":\"{S8}\",\"manifest_sha256\":\"{S8}\","
           "\"wall_us\":{u64},\"cleanup_us\":{u64},\"adoption_waves\":{u64},"
           "\"adopted_signalled\":{u64},\"adopted_reaped\":{u64},\"cleanup_proven\":{S8},"
           "\"qualification_state\":\"unvalidated\",\"default_activated\":false\n}\n"),
        result ? S8("complete") : S8("failed"), leg, plan.source_root, plan.baseline_revision, plan.baseline_tree,
        *inventory_sha256, wall, compiler_closure_cleanup_us-cleanup, compiler_closure_cleanup_waves-waves,
        compiler_closure_cleanup_signalled-signalled, compiler_closure_cleanup_reaped-reaped, unknown ? S8("false") : S8("true"));
    bool published = file_write(path_join(controller->arena, plan.output_root,
        string_format(controller->arena, S8("{S8}-inventory.json"), leg)), BUSTER_SLICE_TO_BYTE_SLICE(text));
    string8_list_push(controller->arena, ledger, string_format(controller->arena,
        S8("{S8}\tcompiler_closure_inventory\t{S8}\t{S8}\t{S8}\t{u64}\t{S8}\t{u64}\t{u64}\t{u64}\t{u64}\t{S8}\n"),
        leg, plan.source_root, plan.baseline_revision, plan.baseline_tree, os_now_microseconds()-started, *inventory_sha256,
        compiler_closure_cleanup_us-cleanup, compiler_closure_cleanup_waves-waves,
        compiler_closure_cleanup_signalled-signalled, compiler_closure_cleanup_reaped-reaped,
        result && published ? S8("complete") : S8("failed")));
    controller->success = controller->success && result && published && os_now_microseconds() < controller->deadline;
    if (resolved->diagnostic)
        string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_INVENTORY leg={S8} ready={u64} "
            "manifest_bytes={u64} owner_unknown={u64} validated={u64} published={u64} success={u64}\n"),
            leg, (u64)ready, manifest.length, (u64)unknown, (u64)result, (u64)published, (u64)controller->success);
    return controller->success;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_leg(CompilerSamplingController* controller,
    CompilerClosureUtilityControllerResolved* resolved, String8 leg, bool snapshot,
    CompilerClosureUtilityExportTotals* totals, String8List* legs, String8List* inventory)
{
    bool ready=compiler_closure_utility_controller_ready(controller,resolved);
    if (ready)
    {
        Arena* arena = controller->arena;
        CompilerClosureUtilityPlan plan = resolved->admitted.plan;
        // Includes ALL reset, actual trusted bootstrap, ordinary main measurement,
        // native final inventory, complete raw copying and finalized raw hashes.
        u64 started = os_now_microseconds();
        String8 checkout[] = {S8("-C"), plan.source_root, S8("checkout"), S8("--quiet"), S8("--detach"), plan.candidate_revision};
        compiler_closure_utility_controller_phase(controller, resolved, string_format(arena, S8("{S8}-reset-checkout"), leg),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(checkout)), 120000000ull);
        String8 reset[] = {S8("-C"), plan.source_root, S8("reset"), S8("--hard"), S8("--quiet"), plan.candidate_revision};
        compiler_closure_utility_controller_phase(controller, resolved, string_format(arena, S8("{S8}-reset-tracked-source"), leg),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(reset)), 120000000ull);
        String8 clean[] = {S8("-C"), plan.source_root, S8("clean"), S8("-fdx")};
        compiler_closure_utility_controller_phase(controller, resolved, string_format(arena, S8("{S8}-reset-build-cache"), leg),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clean)), 120000000ull);
        String8 bootstrap[] = {path_join(arena, resolved->options.trusted_root, S8("build.sh")),
            S8("compiler_profile_qualification"), S8("--plan")};
        compiler_closure_utility_controller_phase(controller, resolved, string_format(arena, S8("{S8}-trusted-bootstrap"), leg),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(bootstrap), 300000000ull);
        SliceString8 compare = compiler_closure_utility_controller_compare(arena, *resolved, leg, snapshot);
        compiler_closure_utility_controller_phase(controller, resolved, string_format(arena, S8("{S8}-ordinary-compare"), leg),
            compare, BUSTER_CLOSURE_UTILITY_WORKER_SECONDS * 1000000ull);
        String8 inventory_sha = {0}, export_sha = {0}, receipt_sha = {0};
        if (controller->success) compiler_closure_utility_controller_inventory(controller, resolved, leg, inventory, &inventory_sha);
        bool intact = compiler_closure_utility_controller_tools(arena, resolved, false);
        // After failure only stable data copies remain permitted. Every child and
        // destructive reset above requires a proved, unstopped ownership state.
        bool exported = os_now_microseconds() < controller->deadline &&
            compiler_closure_utility_controller_export(arena, *resolved, leg, totals, &export_sha, &receipt_sha);
        controller->success = controller->success && intact && exported &&
            !compiler_closure_utility_controller_unknown(arena, *resolved) && compiler_closure_admitting() &&
            !compiler_sampling_controller_cancelled();
        u64 finished = os_now_microseconds();
        controller->success = controller->success && finished <= controller->deadline;
        if (resolved->diagnostic)
            string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_LEG leg={S8} exported={u64} tools_intact={u64} "
                "success={u64} wall_us={u64} copied_files={u64} copied_bytes={u64}\n"),
                leg, (u64)exported, (u64)intact, (u64)controller->success, finished-started, totals->files, totals->bytes);
        string8_list_push(arena, legs, string_format(arena,
            S8("{S8}\t{S8}\t{u64}\t{u64}\t{u64}\tbootstrap-through-export-hashfinalization\t{S8}\t{S8}\t{S8}\t{S8}\t{S8}\n"),
            leg, snapshot ? S8("snapshot-v1") : S8("legacy-rebuild"), started, finished, finished-started,
            export_sha, receipt_sha, resolved->admitted.plan.native_driver_sha256, inventory_sha,
            controller->success ? S8("complete") : S8("failed")));
    }
    return ready && controller->success;
}
#endif

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_utility_controller_worker_claimed(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved, bool claimed)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && !BUSTER_ANDROID
    // A duplicate or concurrent loser returns before ALL later evidence writes.
    if (claimed)
    {
        CompilerSamplingController controller = {.arena = arena, .evidence = resolved.options.evidence,
            .started = os_now_microseconds(), .success = resolved.valid};
        controller.plan.source_root = resolved.admitted.plan.source_root;
        controller.deadline = controller.started + compiler_closure_utility_controller_budget(resolved, controller.started,
            BUSTER_CLOSURE_UTILITY_WORKER_SECONDS * 1000000ull);
        string8_list_push(arena, &controller.phases, S8("stage\tphase\twall_us\texit_status\ttimed_out\tcleanup_failed\tcancelled\tstate\n"));
        String8List legs = {0}, inventory = {0};
        string8_list_push(arena, &legs, S8("BUSTER_COMPILER_CLOSURE_UTILITY_LEGS_V1\n"
            "leg\tpreparation_policy\tstart_us\tfinish_us\twall_us\tclock_scope\texport_sha256\treceipt_sha256\tnative_driver_sha256\tinventory_sha256\tprocess_state\n"));
        string8_list_push(arena, &inventory, S8("BUSTER_COMPILER_CLOSURE_UTILITY_INVENTORY_V1\n"
            "leg\tcall\troot\tbase\tbase_tree\twall_us\tmanifest_sha256\tcleanup_us\tadoption_waves\tadopted_signalled\tadopted_reaped\tprocess_state\n"));
        CompilerSamplingSignalScope signals = {0};
        bool deferred = compiler_closure_utility_controller_signals_begin(&signals);
        OsDirectoryCreateResult output = deferred ? os_make_directory_exclusive(resolved.admitted.plan.output_root) : (OsDirectoryCreateResult){0};
        OsDirectoryCreateResult storage = output.created && !output.error.v ?
            os_make_directory_exclusive(path_join(arena, resolved.options.evidence, S8("utility"))) : (OsDirectoryCreateResult){0};
        controller.success = controller.success && deferred && output.created && !output.error.v &&
            storage.created && !storage.error.v && compiler_closure_utility_controller_host_receipt(arena, resolved);
        String8 trusted_pin[] = {S8("-C"), resolved.options.trusted_root, S8("rev-parse"), S8("HEAD")};
        compiler_closure_utility_controller_phase(&controller, &resolved, S8("trusted-harness-pin"),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(trusted_pin)), 120000000ull);
        controller.success = controller.success &&
            string_equal(production_profile_trim(controller.last_output), resolved.admitted.trusted_revision);
        String8 trusted_clean[] = {S8("-C"), resolved.options.trusted_root, S8("diff"), S8("--quiet"), S8("--exit-code"), S8("HEAD"), S8("--")};
        compiler_closure_utility_controller_phase(&controller, &resolved, S8("trusted-harness-clean"),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(trusted_clean)), 120000000ull);
        if (!resolved.diagnostic)
        {
        String8 clone[] = {S8("clone"), S8("--no-checkout"), S8("--no-tags"), S8("https://github.com/buster14a/buster.git"), resolved.admitted.plan.source_root};
        compiler_closure_utility_controller_phase(&controller, &resolved, S8("clone-utility-source"),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clone)), 120000000ull);
        String8 fetch[] = {S8("-C"), resolved.admitted.plan.source_root, S8("fetch"), S8("--no-tags"), S8("origin"),
            resolved.admitted.plan.baseline_revision, resolved.admitted.plan.candidate_revision, resolved.admitted.plan.pull_head};
        compiler_closure_utility_controller_phase(&controller, &resolved, S8("fetch-utility-pins"),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fetch)), 120000000ull);
        }
        String8 base_tree = compiler_closure_utility_controller_revision(&controller, &resolved,
            string_format(arena, S8("{S8}^{{tree}"), resolved.admitted.plan.baseline_revision), S8("baseline-tree"));
        String8 head_tree = compiler_closure_utility_controller_revision(&controller, &resolved,
            string_format(arena, S8("{S8}^{{tree}"), resolved.admitted.plan.candidate_revision), S8("candidate-tree"));
        String8 first = compiler_closure_utility_controller_revision(&controller, &resolved,
            string_format(arena, S8("{S8}^1"), resolved.admitted.plan.candidate_revision), S8("candidate-first-parent"));
        String8 second = resolved.diagnostic ? resolved.admitted.plan.pull_head : compiler_closure_utility_controller_revision(&controller, &resolved,
            string_format(arena, S8("{S8}^2"), resolved.admitted.plan.candidate_revision), S8("candidate-second-parent"));
        controller.success = controller.success && string_equal(base_tree, resolved.admitted.plan.baseline_tree) &&
            string_equal(head_tree, resolved.admitted.plan.candidate_tree) &&
            string_equal(first, resolved.admitted.plan.baseline_revision) && string_equal(second, resolved.admitted.plan.pull_head);
        bool tools_before = controller.success && compiler_closure_utility_controller_tools(arena, &resolved, false);
        controller.success = controller.success && tools_before;
        CompilerClosureUtilityExportTotals totals = {.deadline = controller.deadline, .diagnostic = resolved.diagnostic};
        bool legacy = compiler_closure_utility_controller_leg(&controller, &resolved, S8("legacy"), false, &totals, &legs, &inventory);
        bool snapshot = legacy && compiler_closure_utility_controller_leg(&controller, &resolved, S8("snapshot"), true, &totals, &legs, &inventory);
        bool tools_after = compiler_closure_utility_controller_tools(arena, &resolved, false);
        bool unknown = controller.cleanup_failed || compiler_closure_utility_controller_unknown(arena, resolved);
        if (unknown)
        {
            controller.cleanup_failed = true;
            compiler_closure_utility_controller_latch(arena, resolved, S8("Utility terminal cleanup unproven; retain roots and block future host admission.\n"));
        }
        bool restored = deferred && compiler_closure_utility_controller_signals_end(&signals);
        controller.success = controller.success && legacy && snapshot && tools_after && restored && !unknown &&
            compiler_closure_admitting() && !compiler_sampling_controller_cancelled() && os_now_microseconds() < controller.deadline;
        String8 legs_text = string_join_arena(arena, string8_list_to_slice(arena, legs), false);
        String8 inventory_text = string_join_arena(arena, string8_list_to_slice(arena, inventory), false);
        bool tables = file_write(path_join(arena, resolved.options.evidence, S8("utility-legs.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(legs_text)) &&
            file_write(path_join(arena, resolved.options.evidence, S8("utility-inventory.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(inventory_text));
        controller.success = controller.success && tables;
        String8 terminal = string_format(arena,
            S8("schema\tbuster-compiler-closure-utility-controller-v1\nphase\tutility\npacket\t0\nplan_sha256\t{S8}\n"
               "process_state\t{S8}\nqualification_state\tunvalidated\ndefault_activated\tfalse\n"
               "duration_us\t{u64}\ncleanup_proven\t{S8}\nsource_root\t{S8}\noutput_root\t{S8}\n"
               "tools_before\t{S8}\ntools_after\t{S8}\nexported\t{S8}\ncomplete_legs\t{u64}\n"
               "clock_scope\tbootstrap-through-export-hashfinalization\nutility_charge_policy\tall-physical-residual-to-snapshot\n"
               "net_utility\tunavailable\nterminal_publication_us\tunavailable\n"),
            resolved.admitted.freeze_sha256, controller.success ? S8("complete") : S8("failed"), os_now_microseconds()-controller.started,
            unknown ? S8("false") : S8("true"), resolved.admitted.plan.source_root, resolved.admitted.plan.output_root,
            tools_before ? S8("true") : S8("false"), tools_after ? S8("true") : S8("false"),
            legacy && snapshot ? S8("true") : S8("false"), (u64)legacy+(u64)snapshot);
        bool recorded = compiler_sampling_controller_flush(&controller) &&
            file_write(path_join(arena, resolved.options.evidence, S8("utility.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(terminal)) &&
            file_write(path_join(arena, resolved.claim, S8("utility.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(terminal));
        bool within_after_publication = os_now_microseconds() <= controller.deadline;
        if (recorded && !within_after_publication)
        {
            String8 failure = S8("schema\tbuster-compiler-closure-utility-failure-v1\nreason\tworker-publication-overrun\n"
                "process_state\tfailed\nqualification_state\tunvalidated\ndefault_activated\tfalse\n");
            file_write(path_join(arena, resolved.options.evidence, S8("utility-overrun.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(failure));
            file_write(path_join(arena, resolved.claim, S8("utility-overrun.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(failure));
        }
        result = controller.success && recorded && within_after_publication ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        if (resolved.diagnostic)
            string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_WORKER legacy_complete={u64} snapshot_complete={u64} "
                "tools_after={u64} terminal_recorded={u64} within_deadline={u64} success={u64} phases={u64}\n"),
                (u64)legacy, (u64)snapshot, (u64)tools_after, (u64)recorded,
                (u64)within_after_publication, (u64)(result==PROCESS_RESULT_SUCCESS), controller.stage);
    }
    else string_print(S8("error: utility worker execution claim consumed; attempt cannot retry\n"));
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(resolved);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_utility_controller_worker(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    return compiler_closure_utility_controller_worker_claimed(arena, resolved,
        compiler_closure_utility_controller_claim_worker(arena, resolved));
}

BUSTER_GLOBAL_LOCAL SliceString8 compiler_closure_utility_controller_owner_arguments(Arena* arena,
    String8 driver, SliceString8 arguments)
{
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    os_argument_builder_append(&builder, driver);
    os_argument_builder_append(&builder, S8("compiler_profile_qualification"));
    for (u64 i = 0; i < arguments.length; i += 1) os_argument_builder_append(&builder, arguments.pointer[i]);
    os_argument_builder_append(&builder, arguments.length && string_equal(arguments.pointer[0], S8("--self-test-utility-export")) ?
        S8("--owned-utility-fixture-worker") : S8("--owned-utility-worker"));
    return os_argument_builder_flush(&builder);
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_utility_controller_owned(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved, SliceString8 arguments, u64 started)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && !BUSTER_ANDROID
    bool claimed = compiler_closure_utility_controller_claim(arena, resolved);
    CompilerExperimentSupervisor supervisor = {0};
    bool contained = claimed && compiler_experiment_supervisor_begin(arena, &supervisor);
    CompilerSamplingSignalScope signals = {0};
    bool deferred = contained && compiler_closure_utility_controller_signals_begin(&signals);
    ProcessSpawnResult spawn = {0};
    ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
    ProcessGroupControlState control = {.cancellation_signal = &compiler_sampling_cancel_signal,
        .cancellation_escalated = &compiler_sampling_cancel_escalated};
    SliceString8 command = compiler_closure_utility_controller_owner_arguments(arena, resolved.driver, arguments);
    u64 owner_limit = BUSTER_CLOSURE_UTILITY_WORKER_SECONDS * 1000000ull;
    bool launch_attempted=false;
    u64 before_spawn = os_now_microseconds() - started;
    u64 remaining = compiler_closure_utility_controller_budget(resolved, started, owner_limit);
    if (deferred && !compiler_sampling_controller_cancelled() && compiler_closure_admitting() &&
        compiler_experiment_cleanup_guard(arena) && before_spawn < owner_limit && remaining)
    {
        launch_attempted=true;
        spawn = os_process_spawn(command, (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.use_process_environment = 1, .new_process_group = 1, .observe_resources = 1});
        if (spawn.handle)
        {
            spawn.process_group_control = &control;
            u64 spent = os_now_microseconds() - started;
            u64 limit = BUSTER_CLOSURE_UTILITY_WORKER_SECONDS * 1000000ull;
            u64 remaining_before_wait = compiler_closure_utility_controller_budget(resolved, started, limit);
            wait = os_process_wait_deadline(arena, spawn, remaining_before_wait ? remaining_before_wait : 1);
            BUSTER_UNUSED(spent);
        }
    }
    bool wait_observed=spawn.handle && wait.result!=PROCESS_RESULT_UNKNOWN;
    bool manager_proven=(!launch_attempted || wait_observed) &&
        !wait.process_tree_cleanup_failed && !wait.process_group_reservation_retained && !wait.process_group_ownership_lost;
    bool released=manager_proven;
    bool quiet = contained && compiler_experiment_supervisor_end_known(arena, &supervisor, released);
    bool inner_unknown = compiler_closure_utility_controller_unknown(arena, resolved);
    bool cleanup = quiet && !wait.process_tree_cleanup_failed && !inner_unknown;
    if (claimed && !cleanup) compiler_closure_utility_controller_latch(arena, resolved,
        S8("Utility outer owner or inner bridge cleanup unproven; host guard and all roots retained.\n"));
    bool cancelled = compiler_sampling_controller_cancelled() || process_control_atomic_load(&compiler_closure_cancel_signal);
    bool complete = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.platform_status && !wait.timed_out &&
        cleanup && !cancelled && !supervisor.signalled && !supervisor.reaped;
    bool restored = deferred && compiler_closure_utility_controller_signals_end(&signals);
    complete = complete && restored;
    u64 publication_started = os_now_microseconds();
    u64 native_wall=publication_started-started;
    u64 clock_elapsed=resolved.diagnostic ? 0 : resolved.job_clock.entry_elapsed_us;
    u64 resolution_wall=!resolved.diagnostic && resolved.job_clock.entry_monotonic_us>=started ?
        resolved.job_clock.entry_monotonic_us-started : 0;
    bool entry_bound=resolved.diagnostic || (resolved.job_clock.entry_monotonic_us>=started && clock_elapsed>=resolution_wall);
    u64 preentry=resolved.diagnostic ? 0 : entry_bound ? clock_elapsed-resolution_wall : 0;
    u64 wall=native_wall+preentry;
    String8 wall_scope=resolved.diagnostic ? S8("native-diagnostic-entry-through-child-cleanup-before-terminal-publication") :
        S8("public-platform-job-start-lower-through-child-cleanup-before-terminal-publication");
    bool within = entry_bound && compiler_closure_utility_controller_budget(resolved, started, 5400000000ull) != 0;
    String8 owner = string_format(arena,
        S8("schema\tbuster-compiler-closure-utility-owner-v1\nphase\tutility\npacket\t0\nplan_sha256\t{S8}\n"
           "physical_packet_wall_us\t{u64}\nnative_entry_wall_us\t{u64}\njob_elapsed_at_native_entry_us\t{u64}\n"
           "physical_job_clock_sha256\t{S8}\nwall_scope\t{S8}\n"
           "process_state\t{S8}\ntimed_out\t{u64}\ncleanup_failed\t{u64}\n"
           "manager_launch_attempted\t{u64}\nmanager_wait_observed\t{u64}\nmanager_cleanup_proven\t{u64}\n"
           "within_reservation\t{S8}\ncancelled\t{u64}\nqualification_state\tunvalidated\ndefault_activated\tfalse\n"),
        resolved.admitted.freeze_sha256, wall, native_wall, preentry,
        resolved.diagnostic ? S8("unavailable") : stage_object_sha256_bytes(arena,(u8*)resolved.job_clock.record.pointer,resolved.job_clock.record.length),
        wall_scope, complete ? S8("complete") : S8("failed"),
        (u64)wait.timed_out, (u64)!cleanup, (u64)launch_attempted, (u64)wait_observed, (u64)manager_proven, within ? S8("true") : S8("false"), (u64)cancelled);
    bool recorded = claimed && compiler_sampling_supervision_receipt(arena,
        path_join(arena, resolved.options.evidence, S8("owner-supervision.tsv")), supervisor, cleanup, native_wall) &&
        file_write(path_join(arena, resolved.options.evidence, S8("owner.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(owner)) &&
        file_write(path_join(arena, resolved.claim, S8("owner.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(owner));
    u64 observed_at=os_now_microseconds();
    u64 observed_wall=observed_at-started+preentry;
    String8 publication = string_format(arena,
        S8("schema\tbuster-compiler-closure-utility-owner-publication-v1\nowner_sha256\t{S8}\n"
           "scope\t{S8}\ninitial_scope_us\t{u64}\npublication_us\t{u64}\n"
           "observed_wall_us\t{u64}\nobservation_publication_us\tunavailable\nwithin_reservation\t{S8}\n"),
        stage_object_sha256_bytes(arena, (u8*)owner.pointer, owner.length),
        resolved.diagnostic ? S8("native-diagnostic-entry-through-owner-publication") : S8("public-platform-job-start-lower-through-owner-publication"), wall, observed_wall - wall, observed_wall,
        compiler_closure_utility_controller_budget(resolved, started, 5400000000ull) ?  S8("true") : S8("false"));
    bool publication_recorded = recorded &&
        file_write(path_join(arena, resolved.options.evidence, S8("owner-publication.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(publication)) &&
        file_write(path_join(arena, resolved.claim, S8("owner-publication.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(publication));
    // No successful return may exclude either receipt's publication from the
    // actual hard clock guard. The last observation receipt cannot self-time;
    // its tail remains explicitly unavailable, never an invented zero.
    bool within_after_publication = entry_bound && compiler_closure_utility_controller_budget(resolved, started, 5400000000ull) != 0;
    if (claimed && !within_after_publication)
    {
        String8 failed = string_format(arena,
            S8("schema\tbuster-compiler-closure-utility-owner-v1\nphase\tutility\npacket\t0\nplan_sha256\t{S8}\n"
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
    if (resolved.diagnostic)
        string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_OWNER claimed={u64} contained={u64} "
            "launch_attempted={u64} wait_observed={u64} wait_result={u64} platform_status={u64} "
            "manager_proven={u64} quiet={u64} inner_unknown={u64} cleanup={u64} restored={u64} "
            "within={u64} recorded={u64} publication_recorded={u64} success={u64}\n"),
            (u64)claimed, (u64)contained, (u64)launch_attempted, (u64)wait_observed,
            (u64)wait.result, (u64)wait.platform_status, (u64)manager_proven, (u64)quiet,
            (u64)inner_unknown, (u64)cleanup, (u64)restored, (u64)within, (u64)recorded,
            (u64)publication_recorded, (u64)(result==PROCESS_RESULT_SUCCESS));
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(resolved); BUSTER_UNUSED(arguments); BUSTER_UNUSED(started);
#endif
    return result;
}



BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_utility_controller_self_test(Arena* arena);
BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_utility_fixture_execute(Arena* arena, SliceString8 arguments);

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_utility_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result;
    if (arguments.length && string_equal(arguments.pointer[0],S8("--self-test-utility-export")))
        result=compiler_closure_utility_fixture_execute(arena,arguments);
    else
    {
        u64 started=os_now_microseconds();
        CompilerClosureUtilityControllerOptions options=compiler_closure_utility_controller_parse(arguments);
        if (options.self_test) result=compiler_closure_utility_controller_self_test(arena);
        else
        {
            CompilerClosureUtilityControllerResolved resolved={0};
            if (options.valid && compiler_closure_utility_controller_resolve(arena,options,&resolved))
                result=options.owned_worker ? compiler_closure_utility_controller_worker(arena,resolved) :
                    compiler_closure_utility_controller_owned(arena,resolved,arguments,started);
            else
            {
                string_print(S8("error: native utility refuses host, authority, paths, runtime bindings or immutable tools\n"));
                result=PROCESS_RESULT_FAILED;
            }
        }
    }
    return result;
}
#include "compiler_closure_utility_test.c"
#include "compiler_closure_utility_fixture.c"
#endif
