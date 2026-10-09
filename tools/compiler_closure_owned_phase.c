// Ordinary snapshot comparisons call this fixed trusted native executable.
// compiler_closure_owned_phase owns one exact argv, OS manager lease and Linux
// subreaper interval. Receipt command/root/driver hashes bind the Python bridge;
// no later child is safe unless the manager and adopted descendants are quiet.
// Terminal receipt publication is explicitly outside its native partial clock.
#if BUSTER_LINUX
#define BUSTER_COMPILER_OWNED_PHASE_SCHEMA "buster-compiler-owned-phase-v1"
#define BUSTER_COMPILER_OWNED_PHASE_ARGUMENT_LIMIT 128ull
#define BUSTER_COMPILER_OWNED_PHASE_COMMAND_LIMIT (256ull << 10)
#define BUSTER_COMPILER_OWNED_PHASE_LOG_LIMIT (8ull << 20)
#define BUSTER_COMPILER_OWNED_PHASE_TIMEOUT_LIMIT 10800ull

BUSTER_GLOBAL_LOCAL bool compiler_closure_owned_timeout(String8 text, u64* seconds)
{
    u64 value = 0;
    bool result = text.length > 0 && text.length <= 5 && text.pointer[0] != '0';
    for (u64 i = 0; result && i < text.length; i += 1)
    {
        result = text.pointer[i] >= '0' && text.pointer[i] <= '9';
        if (result) { value = value * 10 + (u64)(text.pointer[i] - '0'); }
    }
    result = result && value <= BUSTER_COMPILER_OWNED_PHASE_TIMEOUT_LIMIT;
    if (result) { *seconds = value; }
    return result;
}


BUSTER_GLOBAL_LOCAL bool compiler_closure_owned_bootstrap(Arena* arena, String8 driver,
    String8* trusted, String8* marker, CompilerClosureBootstrapIdentity* producer)
{
    String8 directory = path_parent(arena, driver);
    String8 posix = path_parent(arena, directory);
    String8 cache = path_parent(arena, posix);
    String8 hidden = path_parent(arena, cache);
    String8 root = path_parent(arena, hidden);
    bool result = root.length && string_equal(cache, path_join(arena, root, S8(".cache/bootstrap-driver"))) &&
        string_equal(posix, path_join(arena, cache, S8("posix"))) && directory.length > posix.length + 1;
    String8 configuration = result ? (String8){.pointer = directory.pointer + posix.length + 1,
        .length = directory.length - posix.length - 1} : (String8){0};
    String8 entry = path_join(arena, S8("posix"), configuration);
    String8 named = string_format(arena, S8("{S8}.complete"), driver);
    String8 relative = named.length > cache.length + 1 ? (String8){.pointer = named.pointer + cache.length + 1,
        .length = named.length - cache.length - 1} : (String8){0};
    result = result && compiler_closure_bootstrap_marker(arena, root, root, cache, entry, relative, configuration, producer) &&
        string_equal(driver, path_join(arena, cache, producer->artifact));
    if (result) { *trusted = root; *marker = compiler_closure_read(arena, named, BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT); }
    return result && marker->length;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_owned_phase(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    u64 started = os_now_microseconds();
    bool owned = false;
    String8 receipt_path = {0}, cwd = {0}, driver = {0}, driver_sha256 = {0}, command = {0};
    String8 trusted = {0}, marker = {0};
    CompilerClosureBootstrapIdentity producer = {0};
    u64 timeout_seconds = 0;
    CompilerClosurePhaseResult phase = {.wait = {.result = PROCESS_RESULT_FAILED}};
    if (arguments.length >= 6 && arguments.length <= BUSTER_COMPILER_OWNED_PHASE_ARGUMENT_LIMIT + 5 &&
        string_equal(arguments.pointer[0], S8("owned-phase")) && string_equal(arguments.pointer[4], S8("--")))
    {
        receipt_path = os_path_absolute_lexical(arena, arguments.pointer[1], true);
        cwd = os_path_absolute(arena, arguments.pointer[2], true);
        driver = program_state->input.arguments.length ?
            os_path_absolute(arena, program_state->input.arguments.pointer[0], true) : (String8){0};
        SliceString8 child = {.pointer = arguments.pointer + 5, .length = arguments.length - 5};
        command = production_profile_argv_text(arena, child);
        struct stat driver_status = {0};
        bool safe = compiler_closure_admitting() && receipt_path.length && cwd.length && driver.length &&
            compiler_closure_path_safe(receipt_path) && compiler_closure_path_safe(cwd) &&
            string_equal(path_parent(arena, receipt_path), os_path_absolute(arena, path_parent(arena, receipt_path), true)) &&
            !path_exists(arena, receipt_path) && command.length && command.length <= BUSTER_COMPILER_OWNED_PHASE_COMMAND_LIMIT &&
            compiler_closure_owned_timeout(arguments.pointer[3], &timeout_seconds) &&
            compiler_closure_hash(arena, driver, &driver_sha256, &driver_status) && (driver_status.st_mode & 0111) &&
            compiler_closure_owned_bootstrap(arena, driver, &trusted, &marker, &producer) &&
            string_equal(producer.artifact_sha256, driver_sha256);
        for (u64 i = 0; safe && i < child.length; i += 1)
        {
            safe = child.pointer[i].length && compiler_closure_path_safe(child.pointer[i]);
        }
        if (safe)
        {
            // Consumed before any child. A duplicate wrapper cannot reuse this
            // receipt basename, even if the first failed before spawning.
            OsDirectoryCreateResult claimed = os_make_directory_exclusive(string_format(arena, S8("{S8}.claim"), receipt_path));
            owned = claimed.created && !claimed.error.v;
        }
        if (owned)
        {
            bool recorded = file_publish(string_format(arena, S8("{S8}.argv"), receipt_path), BUSTER_SLICE_TO_BYTE_SLICE(command)) &&
                file_publish(string_format(arena, S8("{S8}.bootstrap.complete"), receipt_path), BUSTER_SLICE_TO_BYTE_SLICE(marker));
            if (recorded)
            {
                phase = compiler_closure_phase_run_bounded(arena, child, cwd, timeout_seconds * 1000000ull,
                    false, BUSTER_COMPILER_OWNED_PHASE_LOG_LIMIT);
            }
            bool logs = file_publish(string_format(arena, S8("{S8}.stdout"), receipt_path),
                phase.wait.streams[STANDARD_STREAM_OUTPUT]);
            logs = file_publish(string_format(arena, S8("{S8}.stderr"), receipt_path),
                phase.wait.streams[STANDARD_STREAM_ERROR]) && logs;
            u64 observed = os_now_microseconds() - started;
            String8 receipt = string_format(arena, S8("{{\"schema\":\"" BUSTER_COMPILER_OWNED_PHASE_SCHEMA "\","
                "\"ownership_schema\":\"buster-native-qualification-supervisor-v1\",\"state\":\"{S8}\","
                "\"command_sha256\":\"{S8}\",\"cwd_sha256\":\"{S8}\",\"driver_sha256\":\"{S8}\","
                "\"receipt_path_sha256\":\"{S8}\","
                "\"trusted_root_sha256\":\"{S8}\",\"bootstrap_config_sha256\":\"{S8}\","
                "\"bootstrap_marker_sha256\":\"{S8}\",\"bootstrap_dependency_count\":{u64},"
                "\"timeout_us\":{u64},\"duration_us\":{u64},"
                "\"duration_scope\":\"entry-through-log-publication-before-terminal-receipt\",\"receipt_publication_us\":null,"
                "\"stdout_sha256\":\"{S8}\",\"stderr_sha256\":\"{S8}\",\"exit_status\":{u64},"
                "\"timed_out\":{u64},\"cancelled\":{u64},\"capture_failed\":{u64},\"output_truncated\":{u64},"
                "\"cleanup_proven\":{S8},\"cleanup_us\":{u64},\"cleanup_waves\":{u64},"
                "\"cleanup_signalled\":{u64},\"cleanup_reaped\":{u64},"
                "\"reservation_retained\":{u64},\"ownership_lost\":{u64},\"tree_cleanup_failed\":{u64}\n}\n"),
                recorded && logs && phase.success ? S8("complete") : S8("failed"),
                production_profile_sha256_text(arena, command), production_profile_sha256_text(arena, cwd), driver_sha256, production_profile_sha256_text(arena, receipt_path),
                production_profile_sha256_text(arena, trusted), producer.configuration, producer.marker_sha256, producer.dependency_count,
                timeout_seconds * 1000000ull, observed,
                production_profile_sha256_text(arena, ((String8){.pointer = (char8*)phase.wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = phase.wait.streams[STANDARD_STREAM_OUTPUT].length})),
                production_profile_sha256_text(arena, ((String8){.pointer = (char8*)phase.wait.streams[STANDARD_STREAM_ERROR].pointer, .length = phase.wait.streams[STANDARD_STREAM_ERROR].length})),
                (u64)phase.wait.platform_status, (u64)phase.wait.timed_out, process_control_atomic_load(&compiler_closure_cancel_signal),
                (u64)phase.wait.capture_failed, (u64)phase.wait.output_truncated,
                phase.cleanup_proven ? S8("true") : S8("false"), phase.cleanup_us, phase.waves, phase.signalled, phase.reaped,
                (u64)phase.wait.process_group_reservation_retained, (u64)phase.wait.process_group_ownership_lost,
                (u64)phase.wait.process_tree_cleanup_failed);
            bool written = file_publish(receipt_path, BUSTER_SLICE_TO_BYTE_SLICE(receipt));
            if (recorded && logs && phase.success && written) { result = PROCESS_RESULT_SUCCESS; }
        }
    }
    return result;
}
#endif
