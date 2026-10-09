// Hosted diagnostics only: real C claims, owner waits, cancellation flags and
// stable raw exporter controls. This entry refuses the physical CPU and every
// utility authority environment; it cannot qualify or activate a result.
#ifndef BUSTER_COMPILER_CLOSURE_UTILITY_TEST_INCLUDED
#define BUSTER_COMPILER_CLOSURE_UTILITY_TEST_INCLUDED

#if BUSTER_LINUX && !BUSTER_ANDROID
// Fixed native fixture: two workers of the same retained attempt race before
// source/output exist. The owned exact PID is reaped before another fixture.
BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_controller_worker_once_fixture(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    String8 record = resolved.claim_record;
    bool serial = compiler_closure_utility_controller_claim_worker(arena, resolved) &&
        !compiler_closure_utility_controller_claim_worker(arena, resolved) &&
        string_equal(compiler_sampling_controller_read(arena,
            path_join(arena, path_join(arena, resolved.claim, S8("execution")), S8("claim.tsv")), 16384), record);
    CompilerClosureUtilityControllerResolved raced = resolved;
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
        u8 won = valid && compiler_closure_utility_controller_claim_worker(arena, raced) ? 1 : 0;
        bool sent = valid && write(report[1], &won, 1) == 1;
        close(report[1]); close(release[0]);
        _exit(sent ? 0 : 7);
    }
    bool parent_won = false;
    u8 ready = 0, child_won = 2;
    if (child > 1)
    {
        close(report[1]); report[1] = -1;
        close(release[0]); release[0] = -1;
        struct pollfd event = {.fd = report[0], .events = POLLIN};
        bool ready_read = poll(&event, 1, 5000) > 0 && read(report[0], &ready, 1) == 1 && ready == 1;
        u8 go = 1;
        bool released = ready_read && write(release[1], &go, 1) == 1;
        parent_won = released && compiler_closure_utility_controller_claim_worker(arena, raced);
        bool got = released && poll(&event, 1, 5000) > 0 && read(report[0], &child_won, 1) == 1 && child_won <= 1;
        bool reaped = compiler_experiment_supervisor_fixture_reap(child, os_now_microseconds() + 5000000ull);
        result = result && got && reaped && ((parent_won ? 1u : 0u) + child_won == 1) &&
            !compiler_closure_utility_controller_claim_worker(arena, raced) &&
            string_equal(compiler_sampling_controller_read(arena,
                path_join(arena, path_join(arena, raced.claim, S8("execution")), S8("claim.tsv")), 16384), record);
    }
    else result = false;
    for (u64 i = 0; i < 2; i += 1)
    {
        if (report[i] >= 0) close(report[i]);
        if (release[i] >= 0) close(release[i]);
    }
    bool quiet = began && compiler_experiment_supervisor_end(arena, &supervisor) &&
        !supervisor.signalled && !supervisor.reaped;
    return result && quiet;
}
#endif

#if BUSTER_LINUX && !BUSTER_ANDROID
BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_fixture_allowed(Arena* arena)
{
    CompilerSamplingControllerHost host = compiler_sampling_controller_observed_host(arena);
    bool physical = host.valid;
    for (u64 i = 0; i + 5 <= host.model.length; i += 1)
        physical = physical || string_equal(string_slice(host.model, i, i + 5), S8("9700X"));
    bool result = !physical && compiler_experiment_cleanup_guard(arena);
    for (u64 i = 0; i < program_state->input.environment_keys.length; i += 1)
        result = result && !string_starts_with_sequence(program_state->input.environment_keys.pointer[i], S8("BQ_UTILITY_")) &&
            !string_starts_with_sequence(program_state->input.environment_keys.pointer[i], S8("BQ_PREPARATION_"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_fixture_signals(Arena* arena, String8 mode)
{
    CompilerExperimentSupervisor owner = {0};
    bool began = compiler_experiment_supervisor_begin(arena, &owner);
    pid_t child = began ? fork() : -1;
    if (!child)
    {
        struct sigaction prior_term = {0}, prior_int = {0}, after_term = {0}, after_int = {0};
        bool observed = sigaction(SIGTERM, 0, &prior_term) == 0 && sigaction(SIGINT, 0, &prior_int) == 0;
        CompilerSamplingSignalScope scope = {0};
        bool deferred = observed && compiler_closure_utility_controller_signals_begin(&scope);
        bool tested = deferred && compiler_closure_phase_self_test(arena, mode) == PROCESS_RESULT_SUCCESS;
        bool timeout = string_equal(mode, S8("timeout"));
        u64 signal = string_equal(mode, S8("SIGINT")) ? (u64)SIGINT : (u64)SIGTERM;
        bool flags = timeout || (process_control_atomic_load(&compiler_sampling_cancel_signal) == signal &&
            process_control_atomic_load(&compiler_closure_cancel_signal) == signal);
        bool restored = deferred && compiler_closure_utility_controller_signals_end(&scope) &&
            sigaction(SIGTERM, 0, &after_term) == 0 && sigaction(SIGINT, 0, &after_int) == 0 &&
            prior_term.sa_handler == after_term.sa_handler && prior_int.sa_handler == after_int.sa_handler;
        _exit(tested && flags && restored ? 0 : 9);
    }
    bool reaped = child > 1 && compiler_experiment_supervisor_fixture_reap(child, os_now_microseconds()+15000000ull);
    bool quiet = began && compiler_experiment_supervisor_end(arena, &owner) && !owner.signalled && !owner.reaped;
    return reaped && quiet;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_fixture_raw(Arena* arena, String8 directory)
{
    String8 source = path_join(arena, directory, S8("raw"));
    String8 destination = path_join(arena, directory, S8("raw-export"));
    String8 dirs[] = {S8(""), S8("ordinary"), S8("ordinary/owned-phases"), S8("ordinary/owned-phases/0001.json.claim"),
        S8("lab"), S8("lab/a"), S8("lab/a/env"), S8("lab/b"), S8("lab/pairs"), S8("throughput"),
        S8("throughput/inputs"), S8("throughput/artifacts")};
    bool result = true;
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(dirs); i += 1)
    {
        String8 path = dirs[i].length ? path_join(arena, source, dirs[i]) : source;
        OsDirectoryCreateResult made = os_make_directory_exclusive(path);
        result = made.created && !made.error.v;
    }
    String8 files[] = {S8("ordinary/receipt.json"), S8("ordinary/owned-phases/0001.json"), S8("ordinary/owned-phases/0001.json.argv"),
        S8("ordinary/owned-phases/0001.json.stdout"), S8("ordinary/owned-phases/0001.json.stderr"),
        S8("ordinary/owned-phases/0001.json.bootstrap.complete"), S8("lab/compare.json"), S8("lab/pairs.json"),
        S8("lab/summary.json"), S8("lab/a/lab.json"), S8("lab/a/env/env.json"), S8("lab/a/commands.log"),
        S8("lab/b/lab.json"), S8("lab/pairs/0001-a.csv"), S8("lab/pairs/0001-b.csv"), S8("lab/a/wrapper.csv"),
        S8("lab/a/wrapper-rss.log"), S8("lab/a/source.metrics"), S8("lab/a/source-run.log"),
        S8("throughput/summary.json"), S8("throughput/metadata.json"), S8("throughput/samples.csv"),
        S8("throughput/telemetry.csv"), S8("throughput/jobs.tsv"), S8("throughput/commands.jsonl"),
        S8("throughput/capabilities.jsonl"), S8("throughput/complete.txt"), S8("throughput/inputs/probe.c"),
        S8("throughput/artifacts/probe.log"), S8("throughput/artifacts/probe.metrics")};
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(files); i += 1)
    {
        // Deliberately opaque test data: not a fabricated production receipt.
        String8 bytes = string_ends_with_sequence(files[i], S8(".stdout")) ||
            string_ends_with_sequence(files[i], S8(".stderr")) ? S8("") : S8("diagnostic-unqualified-export-control\n");
        result = file_write(path_join(arena, source, files[i]), BUSTER_SLICE_TO_BYTE_SLICE(bytes));
    }
    String8 binary = path_join(arena, source, S8("lab/a/out.exe"));
    result = result && file_write(binary, BUSTER_SLICE_TO_BYTE_SLICE(S8("excluded compiler product\n")));
    String8List manifest = {0};
    CompilerClosureUtilityExportTotals totals = {.deadline = os_now_microseconds()+10000000ull};
    result = result && compiler_closure_utility_controller_copy(arena, source, destination, S8(""), &totals, &manifest) &&
        totals.files == BUSTER_ARRAY_LENGTH(files) &&
        generate_path_kind(arena, path_join(arena, destination, S8("lab/a/out.exe"))) == GENERATE_PATH_MISSING &&
        generate_path_kind(arena, path_join(arena, destination, S8("ordinary/owned-phases/0001.json.claim"))) == GENERATE_PATH_DIRECTORY;
    String8 joined = string_join_arena(arena, string8_list_to_slice(arena, manifest), false);
    result = result && joined.length && file_write(path_join(arena, directory, S8("diagnostic-export.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(joined));
    String8 unknown = path_join(arena, source, S8("unexpected.data"));
    result = result && file_write(unknown, BUSTER_SLICE_TO_BYTE_SLICE(S8("must fail closed\n")));
    String8List failed = {0};
    CompilerClosureUtilityExportTotals negative = {.deadline = os_now_microseconds()+10000000ull};
    bool rejected = !compiler_closure_utility_controller_copy(arena, source, path_join(arena, directory, S8("unknown-export")),
        S8(""), &negative, &failed);
    return result && rejected;
}
#endif

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_utility_controller_self_test(Arena* arena)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    if (!compiler_closure_utility_fixture_allowed(arena)) return PROCESS_RESULT_FAILED;
    String8 exact[] = {S8("--execute-utility"), S8("--trusted-root"), S8("/fixture/trusted"),
        S8("--cleanup-root"), S8("/fixture/temp"), S8("--evidence"), S8("/fixture/temp/evidence")};
    CompilerClosureUtilityControllerOptions parsed = compiler_closure_utility_controller_parse((SliceString8)BUSTER_ARRAY_TO_SLICE(exact));
    SliceString8 args = compiler_closure_utility_controller_owner_arguments(arena, S8("/fixture/driver"),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(exact));
    result = parsed.valid && !parsed.owned_worker && args.length == 10 &&
        string_equal(args.pointer[9], S8("--owned-utility-worker")) &&
        BUSTER_CLOSURE_UTILITY_PHYSICAL_SECONDS == 5400 && BUSTER_CLOSURE_UTILITY_WORKER_SECONDS == 5280 &&
        BUSTER_CLOSURE_UTILITY_TAIL_SECONDS == 120;
    CompilerClosureUtilityControllerResolved rejected = {0};
    // Public physical resolution does not accept this hosted environment.
    bool public_refused = !compiler_closure_utility_controller_resolve(arena, parsed, &rejected);
    String8 directory = {0};
    bool owned = result && public_refused && summary_self_test_claim_directory(arena, S8("closure-utility"), &directory);
    if (owned)
    {
        CompilerClosureUtilityControllerResolved fixture = {0};
        fixture.valid = true;
        fixture.claim = path_join(arena, directory, S8("claim"));
        fixture.options.evidence = path_join(arena, directory, S8("evidence"));
        fixture.claim_record = S8("schema\tbuster-compiler-closure-utility-diagnostic-claim-v1\ndiagnostic_fixture\ttrue\nqualification_state\tunqualified\n");
        for (u64 i = 0; i < 5; i += 1) fixture.transport.bytes[i] = S8("diagnostic-unqualified\n");
        bool once = compiler_closure_utility_controller_claim(arena, fixture) &&
            !compiler_closure_utility_controller_claim(arena, fixture) &&
            compiler_closure_utility_controller_worker_once_fixture(arena, fixture);
        bool raw = compiler_closure_utility_fixture_raw(arena, directory);
        CompilerSamplingController phase = {.arena=arena,.evidence=directory,.started=os_now_microseconds(),
            .deadline=os_now_microseconds()+5000000ull,.success=true};
        string8_list_push(arena, &phase.phases, S8("stage\tphase\twall_us\texit_status\ttimed_out\tcleanup_failed\tcancelled\tstate\n"));
        String8 okay[] = {S8("/bin/sh"), S8("-c"), S8("printf native-utility-diagnostic")};
        String8 failure[] = {S8("/bin/sh"), S8("-c"), S8("exit 7")};
        bool first = compiler_preparation_controller_phase(&phase, S8("utility-diagnostic"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(okay), 2000000ull);
        bool failed = !compiler_preparation_controller_phase(&phase, S8("utility-diagnostic-failure"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(failure), 2000000ull);
        bool stopped = !compiler_preparation_controller_phase(&phase, S8("utility-diagnostic-no-next"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(okay), 2000000ull) && !phase.cleanup_failed;
        bool signals = compiler_closure_utility_fixture_signals(arena, S8("timeout")) &&
            compiler_closure_utility_fixture_signals(arena, S8("SIGINT")) &&
            compiler_closure_utility_fixture_signals(arena, S8("SIGTERM"));
        result = once && raw && first && failed && stopped && signals;
        String8 status = S8("{\"schema\":\"buster-compiler-closure-utility-diagnostic-v1\",\"diagnostic_fixture\":true,"
            "\"qualification_state\":\"unqualified\",\"physical_qualification\":false}\n");
        result = file_write(path_join(arena, directory, S8("fixture-status.json")), BUSTER_SLICE_TO_BYTE_SLICE(status)) && result;
    }
    else result = false;
#else
    BUSTER_UNUSED(arena);
#endif
    string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC success={u64} physical_qualification=false\n"), result ? 1ull : 0ull);
    return result ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
#endif
