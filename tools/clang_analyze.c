// Build-driver-only Clang analysis. Included by build.c after its compile-command
// parser; this is not a compiler module. clang_analyze_plan freezes the selected
// database rows and their exact analyzer argv, clang_analyze_worker owns a shard,
// and clang_analyze_aggregate requires one terminal result for every selected TU.
// clang_analyze_main also exposes preparation, independent workers and replay of
// aggregation. Results are evidence for one fresh run, never an incremental cache.

#define BUSTER_ANALYZE_DEFAULT_SHARDS 8
#define BUSTER_ANALYZE_DEFAULT_JOBS 2
#define BUSTER_ANALYZE_MAX_SHARDS 256
#define BUSTER_ANALYZE_TIMEOUT_SECONDS 600
#define BUSTER_ANALYZE_RESULT_VERSION "BUSTER_CLANG_ANALYZE_RESULT_V1\n"

typedef struct ClangAnalyzeOptions ClangAnalyzeOptions;
struct ClangAnalyzeOptions
{
    String8 database;
    String8 config;
    String8 clang;
    String8 results;
    String8 baseline_driver;
    u64 shards;
    u64 jobs;
    u64 shard;
    u64 timeout;
    bool quiet;
    bool prepare;
    bool aggregate;
    bool worker;
    bool self_test;
};

typedef struct ClangAnalyzeUnit ClangAnalyzeUnit;
struct ClangAnalyzeUnit
{
    CompileCommandEntry entry;
    SliceString8 command;
    String8 module;
    u64 shard;
};

typedef struct ClangAnalyzePlan ClangAnalyzePlan;
struct ClangAnalyzePlan
{
    ClangAnalyzeUnit* units;
    u64 count;
    u64 excluded;
    String8 manifest;
    String8 fingerprint;
};

typedef enum ClangAnalyzeStatus
{
    CLANG_ANALYZE_PASS,
    CLANG_ANALYZE_WARNING,
    CLANG_ANALYZE_FAILURE,
    CLANG_ANALYZE_CRASH,
    CLANG_ANALYZE_TIMEOUT,
    CLANG_ANALYZE_LAUNCH,
    CLANG_ANALYZE_LOG_FAILURE,
    CLANG_ANALYZE_STATUS_COUNT,
} ClangAnalyzeStatus;

BUSTER_GLOBAL_LOCAL String8 clang_analyze_status_name(u64 status)
{
    String8 names[] = {S8("pass"), S8("warning"), S8("failure"), S8("crash"), S8("timeout"), S8("launch-failure"), S8("log-failure")};
    String8 result = status < BUSTER_ARRAY_LENGTH(names) ? names[status] : S8("invalid-result");
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_read(Arena* arena, String8 path)
{
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){.end_padding = 1});
    String8 result = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_write(Arena* arena, String8 path, String8 text)
{
    // Read back evidence before publishing success, including short writes/close
    // failures not distinguished by older bootstrap file helpers.
    bool result = file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text));
    if (result)
    {
        result = string_equal(text, clang_analyze_read(arena, path));
    }
    if (!result)
    {
        string_print(S8("error: analyzer evidence write failed: {S8}\n"), path);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_new_directory(Arena* arena, String8 path)
{
    bool result;
#if BUSTER_WINDOWS
    String16 wide = string16_from_string8(arena, path, true);
    result = CreateDirectoryW(wide.pointer, 0) != 0;
#else
    String8 terminated = string_duplicate_arena(arena, path, true);
    result = mkdir(terminated.pointer, 0700) == 0;
#endif
    if (!result)
    {
        string_print(S8("error: analyzer requires a fresh directory (or shard already claimed): {S8}\n"), path);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 clang_analyze_child_peak_rss(void)
{
    u64 result = 0;
#if BUSTER_LINUX || BUSTER_APPLE
    struct rusage usage = {0};
    if (getrusage(RUSAGE_CHILDREN, &usage) == 0)
    {
        result = (u64)usage.ru_maxrss;
#if BUSTER_LINUX
        result *= 1024;
#endif
    }
#endif
    // Zero means unavailable (Windows), not zero memory consumption. POSIX
    // reports the largest child's high water, not simultaneous process-tree RSS.
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_sha256(Arena* arena, String8 text)
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, text.pointer, text.length);
    char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
    sha256_finish_hex(&hash, digits);
    String8 result = {.pointer = digits, .length = SHA256_HEX_CAPACITY - 1};
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_module(String8 file)
{
    // A source basename is the stable module key; its paired *_test TU follows
    // the same owner. No filesystem discovery or reconstructed compile flags.
    u64 start = 0;
    for (u64 i = 0; i < file.length; i += 1)
    {
        if (file.pointer[i] == '/' || file.pointer[i] == '\\')
        {
            start = i + 1;
        }
    }
    String8 result = string_slice(file, start, file.length - 2);
    if (string_ends_with_sequence(result, S8("_test")))
    {
        result.length -= 5;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 clang_analyze_module_shard(String8 module, u64 shards)
{
    // Fixed byte-order-independent FNV-1a, unaffected by database enumeration.
    u64 hash = 14695981039346656037ull;
    for (u64 i = 0; i < module.length; i += 1)
    {
        hash = (hash ^ (u8)module.pointer[i]) * 1099511628211ull;
    }
    return hash % shards;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_command_accounts_for_source(Arena* arena, CompileCommandEntry entry, SliceString8 arguments)
{
    bool valid = true;
    bool found = false;
    String8 source = build_relative_path(arena, entry.directory, entry.file);
    for (u64 i = 0; valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        for (u64 c = 0; valid && c < argument.length; c += 1) valid = argument.pointer[c] != 0;
        // Response files could reintroduce -c/-o after the analyzer projection.
        // Refuse them explicitly instead of accepting an unaccounted action.
        valid = valid && !(argument.length && argument.pointer[0] == '@');
        if (argument.length && argument.pointer[0] != '-' && clang_analyze_is_c_source(argument))
        {
            String8 candidate = build_relative_path(arena, entry.directory, argument);
            found = found || build_artifact_fanout_path_equal(source, candidate);
        }
    }
    valid = valid && found;
    if (!valid)
    {
        string_print(S8("error: analyzer command must explicitly name its source and contain no response files or NUL bytes: {S8}\n"), entry.file);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_plan(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzePlan* plan)
{
    String8 database = clang_analyze_read(arena, options.database);
    JsonParser parser = {.text = database};
    bool valid = database.pointer && json_consume(&parser, '[');
    bool done = valid && json_consume(&parser, ']');
    u64 capacity = 0;
    *plan = (ClangAnalyzePlan){0};
    while (valid && !done)
    {
        CompileCommandEntry entry = json_parse_compile_command_entry(arena, &parser, &valid);
        valid = valid && entry.file.length && entry.directory.length;
        SliceString8 arguments = entry.arguments;
        if (valid && !arguments.length && entry.command.length)
        {
            arguments = shell_split(arena, entry.command, &valid);
        }
        valid = valid && arguments.length && arguments.pointer[0].length;
        if (valid && clang_analyze_is_c_source(entry.file) && clang_analyze_entry_matches_config(arena, entry, arguments, options.config))
        {
            valid = clang_analyze_command_accounts_for_source(arena, entry, arguments);
            if (!entry.output.length)
            {
                for (u64 i = 1; i + 1 < arguments.length; i += 1)
                {
                    if (string_equal(arguments.pointer[i], S8("-o"))) entry.output = arguments.pointer[i + 1];
                }
            }
            // A TU identity is its directory/file/output in this configuration.
            // Conflicting commands for the same output fail rather than silently
            // choosing one. Distinct outputs for the same source remain eligible.
            for (u64 i = 0; valid && i < plan->count; i += 1)
            {
                CompileCommandEntry previous = plan->units[i].entry;
                if (string_equal(previous.directory, entry.directory) && string_equal(previous.file, entry.file) &&
                    string_equal(previous.output, entry.output))
                {
                    string_print(S8("error: duplicate analyzer TU: {S8}\n"), entry.file);
                    valid = false;
                }
            }
            if (valid)
            {
                if (plan->count == capacity)
                {
                    u64 new_capacity = capacity ? capacity * 2 : 64;
                    ClangAnalyzeUnit* units = arena_allocate(arena, ClangAnalyzeUnit, new_capacity);
                    if (plan->count)
                    {
                        memcpy(units, plan->units, plan->count * sizeof(*units));
                    }
                    plan->units = units;
                    capacity = new_capacity;
                }
                entry.arguments = arguments;
                String8 module = clang_analyze_module(entry.file);
                plan->units[plan->count++] = (ClangAnalyzeUnit){.entry = entry, .module = module,
                    .shard = clang_analyze_module_shard(module, options.shards),
                    .command = clang_analyzer_command(arena, arguments, options.clang)};
            }
        }
        else if (valid)
        {
            plan->excluded += 1;
        }
        if (valid)
        {
            if (json_consume(&parser, ']'))
            {
                done = true;
            }
            else
            {
                valid = json_consume(&parser, ',');
                // A trailing comma cannot turn a truncated database into success.
                json_skip_whitespace(&parser);
                valid = valid && parser.index < parser.text.length && parser.text.pointer[parser.index] != ']';
            }
        }
    }
    json_skip_whitespace(&parser);
    valid = valid && done && parser.index == database.length && plan->count;
    if (valid)
    {
        String8List parts = {0};
        string8_list_push(arena, &parts, S8("BUSTER_CLANG_ANALYZE_PLAN_V1\n"));
        build_artifact_fanout_provenance_record_append_string(arena, &parts, os_path_absolute_lexical(arena, options.results, true));
        build_artifact_fanout_provenance_record_append_string(arena, &parts, options.config);
        build_artifact_fanout_provenance_record_append_string(arena, &parts, options.clang);
        build_artifact_fanout_provenance_record_append_u64(arena, &parts, options.shards);
        build_artifact_fanout_provenance_record_append_u64(arena, &parts, options.timeout);
        build_artifact_fanout_provenance_record_append_u64(arena, &parts, plan->count);
        build_artifact_fanout_provenance_record_append_u64(arena, &parts, plan->excluded);
        // Retain the exact authority, including rows excluded by config/language.
        build_artifact_fanout_provenance_record_append_string(arena, &parts, database);
        for (u64 i = 0; i < plan->count; i += 1)
        {
            ClangAnalyzeUnit unit = plan->units[i];
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, i);
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, unit.shard);
            build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.module);
            build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.entry.file);
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, unit.command.length);
            for (u64 a = 0; a < unit.command.length; a += 1)
            {
                build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.command.pointer[a]);
            }
        }
        plan->manifest = string_join_arena(arena, string8_list_to_slice(arena, parts), true);
        plan->fingerprint = clang_analyze_sha256(arena, plan->manifest);
    }
    else
    {
        string_print(S8("error: invalid, empty or duplicate analyzer inventory: {S8} config={S8} offset={u64}\n"),
                     options.database, options.config, parser.index);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_shard_directory(Arena* arena, ClangAnalyzeOptions options, u64 shard)
{
    String8 result = path_join(arena, options.results, string_format(arena, S8("shard-{u64}"), shard));
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_same_plan(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzePlan plan)
{
    bool result = string_equal(plan.manifest, clang_analyze_read(arena, path_join(arena, options.results, S8("manifest.txt"))));
    if (!result)
    {
        string_print(S8("error: analyzer manifest differs from the selected database/options: {S8}\n"), options.results);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessSpawnResult clang_analyze_spawn(Arena* arena, SliceString8 command, String8 directory)
{
    bool changed;
#if BUSTER_WINDOWS
    char16 previous[BUSTER_KB(32) / sizeof(char16)];
    DWORD length = GetCurrentDirectoryW(BUSTER_ARRAY_LENGTH(previous), previous);
    String16 wide = string16_from_string8(arena, directory, true);
    changed = length && length < BUSTER_ARRAY_LENGTH(previous) && SetCurrentDirectoryW(wide.pointer);
#else
    char previous[BUSTER_KB(32)];
    String8 terminated = string_duplicate_arena(arena, directory, true);
    changed = getcwd(previous, sizeof(previous)) && chdir(terminated.pointer) == 0;
#endif
    ProcessSpawnResult result = {0};
    if (changed)
    {
        result = os_process_spawn(command, (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1,
                                  .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR)});
#if BUSTER_WINDOWS
        BUSTER_CHECK(SetCurrentDirectoryW(previous));
#else
        BUSTER_CHECK(chdir(previous) == 0);
#endif
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_worker(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzePlan plan)
{
    String8 directory = clang_analyze_shard_directory(arena, options, options.shard);
    bool ready = clang_analyze_same_plan(arena, options, plan) && clang_analyze_new_directory(arena, directory);
    bool success = ready;
    String8List rows = {0};
    u64 count = 0;
    u64 start = os_now_microseconds();
    for (u64 i = 0; ready && i < plan.count; i += 1)
    {
        ClangAnalyzeUnit unit = plan.units[i];
        if (unit.shard == options.shard)
        {
            TemporalArena temporary = scratch_begin(&arena, 1);
            Arena* scratch = temporary.arena;
            u64 unit_start = os_now_microseconds();
            // Only the dedicated shard process changes its working directory.
            ProcessSpawnResult spawn = clang_analyze_spawn(scratch, unit.command, unit.entry.directory);
            ProcessWaitResult wait = os_process_wait_deadline(scratch, spawn, options.timeout * 1000000);
            String8 out = {.pointer = (char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = wait.streams[STANDARD_STREAM_OUTPUT].length};
            String8 err = {.pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length};
            bool warning = clang_analyze_output_has_warning(out) || clang_analyze_output_has_warning(err);
            u64 status = !spawn.handle ? CLANG_ANALYZE_LAUNCH : wait.timed_out ? CLANG_ANALYZE_TIMEOUT :
                         wait.result == PROCESS_RESULT_CRASH ? CLANG_ANALYZE_CRASH : wait.result != PROCESS_RESULT_SUCCESS ? CLANG_ANALYZE_FAILURE : warning ? CLANG_ANALYZE_WARNING : CLANG_ANALYZE_PASS;
            String8 pieces[] = {out, err};
            String8 log = string_join_arena(scratch, (SliceString8)BUSTER_ARRAY_TO_SLICE(pieces), true);
            String8 log_path = path_join(scratch, directory, string_format(scratch, S8("unit-{u64}.log"), i));
            if (!clang_analyze_write(scratch, log_path, log))
            {
                status = CLANG_ANALYZE_LOG_FAILURE;
            }
            u64 elapsed = os_now_microseconds() - unit_start;
            string8_list_push(arena, &rows, string_format(arena, S8("{u64}\n{u64}\n{u64}\n{S8}\n"), i, status, elapsed, clang_analyze_sha256(scratch, log)));
            count += 1;
            success = success && status == CLANG_ANALYZE_PASS;
            if (!options.quiet || status != CLANG_ANALYZE_PASS)
            {
                string_print(S8("ANALYZE_UNIT shard={u64} unit={u64} status={S8} elapsed_us={u64} file={S8}\n"),
                             options.shard, i, clang_analyze_status_name(status), elapsed, unit.entry.file);
            }
            if (status != CLANG_ANALYZE_PASS)
            {
                command_print(unit.command);
                string_print(S8("{S8}"), log);
            }
            scratch_end(temporary);
        }
    }
    if (ready)
    {
        u64 elapsed = os_now_microseconds() - start;
        u64 rss = clang_analyze_child_peak_rss();
        String8 header = string_format(arena, S8(BUSTER_ANALYZE_RESULT_VERSION "{S8}\n{u64}\n{u64}\n{u64}\n{u64}\n"),
                                       plan.fingerprint, options.shard, count, elapsed, rss);
        String8 pieces[] = {header, string_join_arena(arena, string8_list_to_slice(arena, rows), true)};
        String8 report = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pieces), true);
        // The terminal record is written last. A killed worker has no complete
        // record, even if every earlier analyzer process happened to succeed.
        bool written = clang_analyze_write(arena, path_join(arena, directory, S8("result.txt")), report);
        success = success && written;
        string_print(S8("ANALYZE_SHARD shard={u64} units={u64} elapsed_us={u64} peak_child_rss_bytes={u64} status={S8}\n"),
                     options.shard, count, elapsed, rss, success ? S8("pass") : S8("fail"));
    }
    return success;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_aggregate(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzePlan plan)
{
    bool success = clang_analyze_same_plan(arena, options, plan);
    bool* seen = arena_allocate(arena, bool, plan.count);
    memset(seen, 0, plan.count * sizeof(*seen));
    u64 checked = 0;
    u64 failures = 0;
    u64 max_rss = 0;
    for (u64 shard = 0; shard < options.shards; shard += 1)
    {
        String8 path = path_join(arena, clang_analyze_shard_directory(arena, options, shard), S8("result.txt"));
        String8 text = clang_analyze_read(arena, path);
        u64 cursor = 0;
        String8 magic = {0};
        String8 fingerprint = {0};
        u64 recorded_shard = 0;
        u64 count = 0;
        u64 elapsed = 0;
        u64 rss = 0;
        bool valid = build_artifact_fanout_provenance_record_read_line(text, &cursor, &magic) &&
                     string_equal(magic, S8("BUSTER_CLANG_ANALYZE_RESULT_V1")) &&
                     build_artifact_fanout_provenance_record_read_line(text, &cursor, &fingerprint) && string_equal(fingerprint, plan.fingerprint) &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &recorded_shard) && recorded_shard == shard &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &count) && count <= plan.count &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &elapsed) &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &rss);
        for (u64 row = 0; valid && row < count; row += 1)
        {
            u64 index = 0;
            u64 status = 0;
            u64 duration = 0;
            String8 log_fingerprint = {0};
            valid = build_artifact_fanout_provenance_record_read_u64(text, &cursor, &index) && index < plan.count && !seen[index] &&
                    plan.units[index].shard == shard && build_artifact_fanout_provenance_record_read_u64(text, &cursor, &status) &&
                    status < CLANG_ANALYZE_STATUS_COUNT && build_artifact_fanout_provenance_record_read_u64(text, &cursor, &duration) &&
                    build_artifact_fanout_provenance_record_read_line(text, &cursor, &log_fingerprint);
            if (valid)
            {
                String8 log_path = path_join(arena, clang_analyze_shard_directory(arena, options, shard), string_format(arena, S8("unit-{u64}.log"), index));
                String8 log = clang_analyze_read(arena, log_path);
                valid = log.pointer && string_equal(log_fingerprint, clang_analyze_sha256(arena, log));
                if (!valid) string_print(S8("error: missing or changed analyzer log: shard={u64} file={S8}\n"), shard, plan.units[index].entry.file);
            }
            if (valid)
            {
                seen[index] = true;
                checked += 1;
                if (status != CLANG_ANALYZE_PASS)
                {
                    failures += 1;
                    string_print(S8("error: analyzer shard={u64} status={S8} file={S8}\n"), shard, clang_analyze_status_name(status), plan.units[index].entry.file);
                }
            }
        }
        valid = valid && cursor == text.length;
        if (!valid)
        {
            string_print(S8("error: missing, duplicate, stale or malformed analyzer result: shard={u64} path={S8}\n"), shard, path);
        }
        if (valid && rss > max_rss)
        {
            max_rss = rss;
        }
        success = success && valid;
    }
    for (u64 i = 0; i < plan.count; i += 1)
    {
        if (!seen[i])
        {
            string_print(S8("error: missing analyzer TU: shard={u64} file={S8}\n"), plan.units[i].shard, plan.units[i].entry.file);
            success = false;
        }
    }
    success = success && !failures && checked == plan.count;
    string_print(S8("ANALYZE_AGGREGATE eligible={u64} checked={u64} excluded_config_or_language={u64} failures={u64} shards={u64} peak_child_rss_bytes={u64} status={S8}\n"),
                 plan.count, checked, plan.excluded, failures, options.shards, max_rss, success ? S8("pass") : S8("fail"));
    return success;
}

BUSTER_GLOBAL_LOCAL SliceString8 clang_analyze_worker_command(Arena* arena, ClangAnalyzeOptions options, u64 shard)
{
    // The argument builder stores a contiguous array in this arena. Allocate
    // all string payloads before starting it, never between append operations.
    String8 self = os_path_absolute(arena, program_state->input.arguments.pointer[0], true);
    String8 shards_text = string_format(arena, S8("{u64}"), options.shards);
    String8 shard_text = string_format(arena, S8("{u64}"), shard);
    String8 timeout_text = string_format(arena, S8("{u64}"), options.timeout);
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    os_argument_builder_append(&builder, self);
    os_argument_builder_append(&builder, S8("clang_analyze"));
    os_argument_builder_append(&builder, options.database);
    os_argument_builder_append(&builder, S8("--results"));
    os_argument_builder_append(&builder, options.results);
    os_argument_builder_append(&builder, S8("--shards"));
    os_argument_builder_append(&builder, shards_text);
    os_argument_builder_append(&builder, S8("--shard"));
    os_argument_builder_append(&builder, shard_text);
    os_argument_builder_append(&builder, S8("--timeout"));
    os_argument_builder_append(&builder, timeout_text);
    if (options.config.length)
    {
        os_argument_builder_append(&builder, S8("--config"));
        os_argument_builder_append(&builder, options.config);
    }
    if (options.clang.length)
    {
        os_argument_builder_append(&builder, S8("--clang"));
        os_argument_builder_append(&builder, options.clang);
    }
    if (options.quiet)
    {
        os_argument_builder_append(&builder, S8("--quiet"));
    }
    SliceString8 result = os_argument_builder_flush(&builder);
    return result;
}

typedef struct ClangAnalyzeResources ClangAnalyzeResources;
struct ClangAnalyzeResources
{
    u64 samples;
    u64 peak_processes;
    u64 peak_tree_rss;
};

BUSTER_GLOBAL_LOCAL void clang_analyze_sample_resources(ClangAnalyzeResources* resources)
{
#if BUSTER_LINUX
    // Sample the coordinator and descendants, including the reference driver's
    // original fan-out. Sum RSS (shared pages count in each process), not PSS.
    // /proc races with ordinary child exit, so this is a sampled lower bound.
    u32 pids[4096];
    u64 count = 1;
    u64 rss = 0;
    u64 processes = 0;
    pids[0] = (u32)getpid();
    for (u64 i = 0; i < count; i += 1)
    {
        char path[128];
        snprintf(path, sizeof(path), "/proc/%u/statm", pids[i]);
        FILE* file = fopen(path, "r");
        if (file)
        {
            unsigned long long size = 0;
            unsigned long long resident = 0;
            if (fscanf(file, "%llu %llu", &size, &resident) == 2 && resident)
            {
                rss += (u64)resident * os_get_page_size();
                processes += 1;
            }
            fclose(file);
        }
        snprintf(path, sizeof(path), "/proc/%u/task/%u/children", pids[i], pids[i]);
        file = fopen(path, "r");
        if (file)
        {
            unsigned long child = 0;
            while (count < BUSTER_ARRAY_LENGTH(pids) && fscanf(file, "%lu", &child) == 1)
            {
                bool seen = child > UINT32_MAX;
                for (u64 previous = 0; !seen && previous < count; previous += 1)
                {
                    seen = pids[previous] == (u32)child;
                }
                if (!seen) pids[count++] = (u32)child;
            }
            fclose(file);
        }
    }
    if (processes)
    {
        resources->samples += 1;
        if (processes > resources->peak_processes) resources->peak_processes = processes;
        if (rss > resources->peak_tree_rss) resources->peak_tree_rss = rss;
    }
#else
    BUSTER_UNUSED(resources);
#endif
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_finished(ProcessSpawnResult spawn)
{
    bool result = !spawn.handle;
    if (spawn.handle)
    {
#if BUSTER_WINDOWS
        result = WaitForSingleObject(spawn.handle, 0) != WAIT_TIMEOUT;
#else
        siginfo_t information = {0};
        int status = waitid(P_PID, (id_t)(uintptr_t)spawn.handle, &information, WEXITED | WNOHANG | WNOWAIT);
        result = information.si_pid != 0 || (status < 0 && errno != EINTR);
#endif
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_sample_pause(void)
{
#if BUSTER_WINDOWS
    Sleep(25);
#else
    poll(0, 0, 25);
#endif
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_baseline(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzePlan plan)
{
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    os_argument_builder_append(&builder, options.baseline_driver);
    os_argument_builder_append(&builder, S8("clang_analyze"));
    os_argument_builder_append(&builder, options.database);
    os_argument_builder_append(&builder, S8("--quiet"));
    if (options.config.length)
    {
        os_argument_builder_append(&builder, S8("--config"));
        os_argument_builder_append(&builder, options.config);
    }
    if (options.clang.length)
    {
        os_argument_builder_append(&builder, S8("--clang"));
        os_argument_builder_append(&builder, options.clang);
    }
    u64 start = os_now_microseconds();
    ProcessSpawnResult spawn = os_process_spawn(os_argument_builder_flush(&builder), (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
    ClangAnalyzeResources resources = {0};
    while (!clang_analyze_finished(spawn) && os_now_microseconds() - start < 3600ull * 1000000)
    {
        clang_analyze_sample_resources(&resources);
        clang_analyze_sample_pause();
    }
    ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 1);
    u64 elapsed = os_now_microseconds() - start;
    u64 rss = clang_analyze_child_peak_rss();
    String8 out = {.pointer = (char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = wait.streams[STANDARD_STREAM_OUTPUT].length};
    String8 err = {.pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length};
    // A later reference may itself use shard workers. Host CPU capacity is
    // known here; the external driver's actual scheduling limit is not.
    String8 metric = string_format(arena, S8("ANALYZE_BASELINE eligible={u64} elapsed_us={u64} host_logical_cpus={u32} peak_child_rss_bytes={u64} samples={u64} peak_live_processes={u64} sampled_peak_tree_rss_bytes={u64} status={S8}\n"),
        plan.count, elapsed, os_get_logical_thread_count(), rss, resources.samples, resources.peak_processes, resources.peak_tree_rss, wait.result == PROCESS_RESULT_SUCCESS ? S8("pass") : S8("fail"));
    String8 pieces[] = {metric, out, err};
    bool written = clang_analyze_write(arena, path_join(arena, options.results, S8("baseline.log")),
                                      string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pieces), true));
    string_print(S8("{S8}"), metric);
    bool result = wait.result == PROCESS_RESULT_SUCCESS && written;
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_run(Arena* arena, ClangAnalyzeOptions options)
{
    u64 setup_start = os_now_microseconds();
    ClangAnalyzePlan plan;
    bool success = clang_analyze_plan(arena, options, &plan);
    if (success && options.worker)
    {
        success = clang_analyze_worker(arena, options, plan);
    }
    else if (success && options.aggregate)
    {
        success = clang_analyze_aggregate(arena, options, plan);
    }
    else if (success)
    {
        success = clang_analyze_new_directory(arena, options.results) &&
                  clang_analyze_write(arena, path_join(arena, options.results, S8("manifest.txt")), plan.manifest);
        if (success && !options.prepare)
        {
            u64 setup_us = os_now_microseconds() - setup_start;
            bool baseline = !options.baseline_driver.length || clang_analyze_baseline(arena, options, plan);
            u64 start = os_now_microseconds();
            u64 peak_pending = 0;
            u64 next_shard = 0;
            u64 completed = 0;
            u64 pending = 0;
            ClangAnalyzeResources resources = {0};
            ProcessSpawnResult* spawns = arena_allocate(arena, ProcessSpawnResult, options.shards);
            bool* active = arena_allocate(arena, bool, options.shards);
            memset(active, 0, options.shards * sizeof(*active));
            while (completed < options.shards)
            {
                while (next_shard < options.shards && pending < options.jobs)
                {
                    SliceString8 command = clang_analyze_worker_command(arena, options, next_shard);
                    spawns[next_shard] = os_process_spawn(command, (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
                    active[next_shard++] = true;
                    pending += 1;
                }
                if (pending > peak_pending) peak_pending = pending;
                clang_analyze_sample_resources(&resources);
                for (u64 shard = 0; shard < next_shard; shard += 1)
                {
                    if (active[shard] && clang_analyze_finished(spawns[shard]))
                    {
                        // No captured coordinator pipes: a finished worker can
                        // be reaped immediately. Its own TU waits drain both streams.
                        ProcessWaitResult wait = os_process_wait_sync(arena, spawns[shard]);
                        if (wait.result != PROCESS_RESULT_SUCCESS)
                        {
                            string_print(S8("error: analyzer worker failed: shard={u64}\n"), shard);
                            success = false;
                        }
                        active[shard] = false;
                        pending -= 1;
                        completed += 1;
                    }
                }
                if (pending) clang_analyze_sample_pause();
            }
            // Always aggregate, even when a worker failed or never launched.
            bool aggregate = clang_analyze_aggregate(arena, options, plan);
            success = success && aggregate && baseline;
            string_print(S8("ANALYZE_RUN elapsed_us={u64} peak_pending_workers={u64} jobs={u64} samples={u64} peak_live_processes={u64} sampled_peak_tree_rss_bytes={u64} results={S8} status={S8}\n"),
                         os_now_microseconds() - start + setup_us, peak_pending, options.jobs, resources.samples, resources.peak_processes, resources.peak_tree_rss,
                         options.results, success ? S8("pass") : S8("fail"));
        }
    }
    return success;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_self_test(Arena* arena);

BUSTER_GLOBAL_LOCAL ProcessResult clang_analyze_main(Arena* arena, SliceString8 arguments)
{
    ClangAnalyzeOptions options = {.database = S8("build"), .shards = BUSTER_ANALYZE_DEFAULT_SHARDS,
        .jobs = BUSTER_ANALYZE_DEFAULT_JOBS, .timeout = BUSTER_ANALYZE_TIMEOUT_SECONDS};
    bool valid = true;
    bool database_set = false;
    for (u64 i = 0; valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        String8 inline_value = {0};
        bool has_value = build_argument_split_value(argument, &argument, &inline_value);
        if (string_equal(argument, S8("--quiet")) && !has_value) options.quiet = true;
        else if (string_equal(argument, S8("--prepare")) && !has_value) options.prepare = true;
        else if (string_equal(argument, S8("--aggregate")) && !has_value) options.aggregate = true;
        else if (string_equal(argument, S8("--self-test")) && !has_value) options.self_test = true;
        else if (!string_starts_with_sequence(argument, S8("--")) && !database_set)
        {
            options.database = argument;
            database_set = true;
        }
        else if (has_value || i + 1 < arguments.length)
        {
            String8 value = has_value ? inline_value : arguments.pointer[++i];
            if (string_equal(argument, S8("--config"))) options.config = value;
            else if (string_equal(argument, S8("--clang"))) options.clang = value;
            else if (string_equal(argument, S8("--results"))) options.results = value;
            else if (string_equal(argument, S8("--build-directory"))) options.database = value;
            else if (string_equal(argument, S8("--baseline-driver"))) options.baseline_driver = value;
            else
            {
                u64 number = 0;
                valid = text_parse_u64(value, &number);
                if (string_equal(argument, S8("--shards"))) options.shards = number;
                else if (string_equal(argument, S8("--jobs"))) options.jobs = number;
                else if (string_equal(argument, S8("--timeout"))) options.timeout = number;
                else if (string_equal(argument, S8("--shard")))
                {
                    options.shard = number;
                    options.worker = true;
                }
                else valid = false;
            }
        }
        else valid = false;
    }
    valid = valid && options.shards && options.shards <= BUSTER_ANALYZE_MAX_SHARDS && options.jobs && options.jobs <= BUSTER_ANALYZE_MAX_SHARDS &&
            options.timeout && options.timeout <= 86400 && (!options.worker || options.shard < options.shards) &&
            ((u32)options.prepare + (u32)options.aggregate + (u32)options.worker <= 1) &&
            (!(options.prepare || options.aggregate || options.worker) || options.results.length) &&
            (!options.baseline_driver.length || !(options.prepare || options.aggregate || options.worker));
    if (valid && options.self_test)
    {
        valid = arguments.length == 1 && clang_analyze_self_test(arena);
    }
    else if (valid)
    {
        options.database = os_path_absolute(arena, clang_analyze_compile_commands_path(arena, options.database), true);
        if (!options.results.length)
        {
            options.results = path_join(arena, path_parent(arena, options.database), string_format(arena, S8("analyze-{u64}"), os_now_microseconds()));
        }
        options.results = os_path_absolute_lexical(arena, options.results, true);
        if (options.jobs > options.shards) options.jobs = options.shards;
        u64 cpus = os_get_logical_thread_count();
        if (cpus && options.jobs > cpus) options.jobs = cpus;
        valid = clang_analyze_run(arena, options);
    }
    else
    {
        string_print(S8("error: clang_analyze [database] [--config Release] [--shards N] [--jobs N] [--timeout seconds] [--results fresh-directory] "
                        "[--prepare | --shard zero-based-index | --aggregate] [--clang path] [--quiet]; or --self-test\n"));
    }
    return valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_test_json(Arena* arena, String8 value)
{
    u64 start = arena->position;
    arena_append_json_string(arena, value);
    String8 result = {.pointer = (char8*)arena_get_byte_pointer_at_position(arena, start), .length = arena->position - start};
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_test_database(Arena* arena, String8 directory, String8 compiler, String8 mode)
{
    String8 files[] = {S8("alpha.c"), S8("alpha_test.c"), S8("beta.c"), S8("gamma.c")};
    String8List rows = {0};
    string8_list_push(arena, &rows, S8("["));
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(files); i += 1)
    {
        String8 output = string_format(arena, S8("obj/Release/{S8}.o"), files[i]);
        String8 row = string_format(arena, S8("{S8}{{\"directory\":{S8},\"file\":{S8},\"output\":{S8},\"arguments\":[{S8},\"-c\",{S8},\"-fwrapv\",\"-fno-strict-aliasing\",\"-funsigned-char\",{S8},\"-o\",{S8}]}"),
            i ? S8(",") : S8(""), clang_analyze_test_json(arena, directory), clang_analyze_test_json(arena, files[i]),
            clang_analyze_test_json(arena, output), clang_analyze_test_json(arena, compiler),
            clang_analyze_test_json(arena, i ? S8("-DFIXTURE_OK") : mode), clang_analyze_test_json(arena, files[i]), clang_analyze_test_json(arena, output));
        string8_list_push(arena, &rows, row);
    }
    // Excluded rows are still inventoried; they must never become shard work.
    string8_list_push(arena, &rows, string_format(arena, S8(",{{\"directory\":{S8},\"file\":\"debug.c\",\"output\":\"obj/Debug/debug.o\",\"arguments\":[{S8},\"-c\",\"debug.c\"]},"
        "{{\"directory\":{S8},\"file\":\"excluded.cpp\",\"arguments\":[{S8},\"-c\",\"excluded.cpp\"]}]"),
        clang_analyze_test_json(arena, directory), clang_analyze_test_json(arena, compiler),
        clang_analyze_test_json(arena, directory), clang_analyze_test_json(arena, compiler)));
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, rows), true);
    return result;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_test_check(bool condition, String8 name, u64* failures)
{
    string_print(S8("ANALYZE_SELF_TEST name={S8} status={S8}\n"), name, condition ? S8("pass") : S8("fail"));
    *failures += !condition;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_self_test(Arena* arena)
{
    u64 failures = 0;
    bool split_valid = true;
    SliceString8 split = shell_split(arena, S8("\"clang tool\" \"-DBUSTER_HOST_C_COMPILER=\\\"C:/Program Files/clang.exe\\\"\" -I\"dir with spaces\" -c \"source file.c\" -o output.o"), &split_valid);
    String8 expected_macro = S8("-DBUSTER_HOST_C_COMPILER=\"C:/Program Files/clang.exe\"");
    bool quoted = split_valid && split.length == 7 && string_equal(split.pointer[0], S8("clang tool")) &&
                  string_equal(split.pointer[1], expected_macro) && string_equal(split.pointer[2], S8("-Idir with spaces")) &&
                  string_equal(split.pointer[4], S8("source file.c"));
    clang_analyze_test_check(quoted, S8("compile-command-quoting"), &failures);
    SliceString8 projected = quoted ? clang_analyzer_command(arena, split, (String8){0}) : (SliceString8){0};
    clang_analyze_test_check(projected.length == 9 && string_equal(projected.pointer[6], expected_macro) &&
                             string_equal(projected.pointer[7], S8("-Idir with spaces")) && string_equal(projected.pointer[8], S8("source file.c")),
                             S8("preserve-semantic-compile-arguments"), &failures);
    String8 root = os_path_absolute_lexical(arena, string_format(arena, S8("build/analyzer-self-test-{u64}"), os_now_microseconds()), true);
    bool ready = clang_analyze_new_directory(arena, root);
    String8 fixture = path_join(arena, root, S8("fixture.exe"));
    String8 clang = get_resolved_path(arena, &clang_path, S8("clang"));
    String8 compile[] = {clang, S8("-fwrapv"), S8("-fno-strict-aliasing"), S8("-funsigned-char"), S8("tools/clang_analyze_fixture.c"), S8("-o"), fixture};
    ProcessSpawnResult spawn = {0};
    if (ready)
    {
        spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(compile), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
    }
    ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 60000000);
    ready = ready && wait.result == PROCESS_RESULT_SUCCESS;
    clang_analyze_test_check(ready, S8("native-process-oracle"), &failures);
    String8 database = path_join(arena, root, S8("compile_commands.json"));
    String8 modes[] = {S8("-DFIXTURE_OK"), S8("-DFIXTURE_WARNING"), S8("-DFIXTURE_STDOUT"), S8("-DFIXTURE_FAILURE"),
                      S8("-DFIXTURE_CRASH"), S8("-DFIXTURE_TIMEOUT"), S8("-DFIXTURE_LARGE_OUTPUT")};
    for (u64 mode = 0; ready && mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
    {
        String8 contents = clang_analyze_test_database(arena, root, fixture, modes[mode]);
        bool written = clang_analyze_write(arena, database, contents);
        // Only the sleeping oracle needs the short deadline. Draining both
        // pipes can take several seconds on hosted Windows AArch64 runners.
        ClangAnalyzeOptions options = {.database = database, .config = S8("Release"), .shards = 4, .jobs = 2, .timeout = mode == 5 ? 1 : 30, .quiet = true,
            .results = path_join(arena, root, string_format(arena, S8("case-{u64}"), mode))};
        bool passed = written && clang_analyze_run(arena, options);
        clang_analyze_test_check(passed == (mode == 0 || mode == 6), modes[mode], &failures);
        ClangAnalyzePlan plan;
        bool planned = clang_analyze_plan(arena, options, &plan);
        clang_analyze_test_check(planned && plan.count == 4 && plan.excluded == 2 && plan.units[0].shard == plan.units[1].shard,
                                 S8("complete-inventory-and-module-pair"), &failures);
        // Every case, including an early failure, must publish all TU logs.
        for (u64 i = 0; planned && i < plan.count; i += 1)
        {
            String8 log = path_join(arena, clang_analyze_shard_directory(arena, options, plan.units[i].shard), string_format(arena, S8("unit-{u64}.log"), i));
            clang_analyze_test_check(path_exists(arena, log), S8("continue-after-failure"), &failures);
        }
        if (mode == 0 && planned && passed)
        {
            options.aggregate = true;
            clang_analyze_test_check(clang_analyze_run(arena, options), S8("independent-aggregate"), &failures);
            u64 shard = plan.units[0].shard;
            String8 path = path_join(arena, clang_analyze_shard_directory(arena, options, shard), S8("result.txt"));
            String8 original = clang_analyze_read(arena, path);
            remove_path_recursive(arena, path);
            clang_analyze_test_check(!clang_analyze_aggregate(arena, options, plan), S8("missing-shard"), &failures);
            String8 truncated = string_slice(original, 0, original.length - 1);
            clang_analyze_test_check(clang_analyze_write(arena, path, truncated) && !clang_analyze_aggregate(arena, options, plan), S8("truncated-result"), &failures);
            String8 extra = string_format(arena, S8("{S8}0\n0\n0\n"), original);
            clang_analyze_test_check(clang_analyze_write(arena, path, extra) && !clang_analyze_aggregate(arena, options, plan), S8("unaccounted-result"), &failures);
            // Keep a valid header but duplicate one row, then omit a row. These
            // are structurally valid reports whose coverage must still fail.
            String8 empty_hash = clang_analyze_sha256(arena, S8(""));
            String8 duplicate = string_format(arena, S8(BUSTER_ANALYZE_RESULT_VERSION "{S8}\n{u64}\n2\n1\n1\n0\n0\n1\n{S8}\n0\n0\n1\n{S8}\n"), plan.fingerprint, shard, empty_hash, empty_hash);
            clang_analyze_test_check(clang_analyze_write(arena, path, duplicate) && !clang_analyze_aggregate(arena, options, plan), S8("duplicate-TU"), &failures);
            String8 omitted = string_format(arena, S8(BUSTER_ANALYZE_RESULT_VERSION "{S8}\n{u64}\n0\n1\n1\n"), plan.fingerprint, shard);
            clang_analyze_test_check(clang_analyze_write(arena, path, omitted) && !clang_analyze_aggregate(arena, options, plan), S8("omitted-TU"), &failures);
            String8 wrong_shard = string_format(arena, S8(BUSTER_ANALYZE_RESULT_VERSION "{S8}\n{u64}\n0\n1\n1\n"), plan.fingerprint, options.shards);
            clang_analyze_test_check(clang_analyze_write(arena, path, wrong_shard) && !clang_analyze_aggregate(arena, options, plan), S8("wrong-shard"), &failures);
            String8 stale = string_duplicate_arena(arena, original, true);
            stale.pointer[sizeof(BUSTER_ANALYZE_RESULT_VERSION) - 1] = 'z';
            clang_analyze_test_check(clang_analyze_write(arena, path, stale) && !clang_analyze_aggregate(arena, options, plan), S8("stale-result"), &failures);
            clang_analyze_test_check(clang_analyze_write(arena, path, original) && clang_analyze_aggregate(arena, options, plan), S8("restored-coverage"), &failures);
            String8 log_path = path_join(arena, clang_analyze_shard_directory(arena, options, shard), S8("unit-0.log"));
            remove_path_recursive(arena, log_path);
            clang_analyze_test_check(!clang_analyze_aggregate(arena, options, plan), S8("missing-log"), &failures);
            clang_analyze_test_check(clang_analyze_write(arena, log_path, S8("changed")) && !clang_analyze_aggregate(arena, options, plan), S8("changed-log"), &failures);
            clang_analyze_test_check(clang_analyze_write(arena, log_path, S8("")) && clang_analyze_aggregate(arena, options, plan), S8("restored-log"), &failures);
            options.shard = shard;
            clang_analyze_test_check(!clang_analyze_worker(arena, options, plan), S8("duplicate-worker-refused"), &failures);
            options.config = S8("Debug");
            clang_analyze_test_check(!clang_analyze_run(arena, options), S8("changed-config-refused"), &failures);
        }
    }
    if (ready)
    {
        ClangAnalyzeOptions options = {.database = database, .config = S8("Release"), .shards = 1, .jobs = 1, .timeout = 1, .quiet = true,
            .results = path_join(arena, root, S8("single"))};
        String8 contents = clang_analyze_test_database(arena, root, fixture, modes[0]);
        clang_analyze_test_check(clang_analyze_write(arena, database, contents) && clang_analyze_run(arena, options), S8("one-shard-one-job"), &failures);
        clang_analyze_test_check(!clang_analyze_run(arena, options), S8("no-result-cache"), &failures);
        options.results = path_join(arena, root, S8("independent"));
        options.prepare = true;
        bool independent = clang_analyze_run(arena, options);
        options.prepare = false;
        options.worker = true;
        independent = independent && clang_analyze_run(arena, options);
        options.worker = false;
        options.aggregate = true;
        independent = independent && clang_analyze_run(arena, options);
        clang_analyze_test_check(independent, S8("prepare-worker-aggregate"), &failures);
        options.aggregate = false;
        options.results = path_join(arena, root, S8("missing-executable"));
        options.clang = path_join(arena, root, S8("not-a-compiler"));
        clang_analyze_test_check(!clang_analyze_run(arena, options), S8("launch-failure"), &failures);
        options.clang = (String8){0};
        String8 invalid[] = {S8("[]"), S8("[{}]"), S8("["), S8("[{\"file\":\"missing.c\"}]"),
                            string_format(arena, S8("{S8}garbage"), contents),
                            string_format(arena, S8("{S8},]"), string_slice(contents, 0, contents.length - 1))};
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
        {
            ClangAnalyzePlan plan;
            clang_analyze_test_check(clang_analyze_write(arena, database, invalid[i]) && !clang_analyze_plan(arena, options, &plan),
                                     S8("malformed-or-empty-database"), &failures);
        }
        // Repeat the complete array contents to prove duplicate inventory is an
        // error independently of result coverage checks.
        String8 entries = string_slice(contents, 1, contents.length - 1);
        String8 duplicate = string_format(arena, S8("[{S8},{S8}]"), entries, entries);
        ClangAnalyzePlan duplicate_plan;
        clang_analyze_test_check(clang_analyze_write(arena, database, duplicate) && !clang_analyze_plan(arena, options, &duplicate_plan),
                                 S8("duplicate-database-entry"), &failures);
        // Real Clang validates the compile-command projection and cwd handling.
        String8 source = S8("#include \"input.h\"\nint value(void) { return VALUE; }\n");
        String8 header = S8("#define VALUE 42\n");
        bool files = clang_analyze_write(arena, path_join(arena, root, S8("real.c")), source) &&
                     clang_analyze_write(arena, path_join(arena, root, S8("input.h")), header);
        String8 real = string_format(arena, S8("[{{\"directory\":{S8},\"file\":\"real.c\",\"output\":\"Release/real.o\",\"arguments\":[{S8},\"-c\",\"-fwrapv\",\"-fno-strict-aliasing\",\"-funsigned-char\",\"real.c\",\"-o\",\"Release/real.o\"]}]"),
            clang_analyze_test_json(arena, root), clang_analyze_test_json(arena, clang));
        options.results = path_join(arena, root, S8("real-clang"));
        options.timeout = 30;
        clang_analyze_test_check(files && clang_analyze_write(arena, database, real) && clang_analyze_run(arena, options), S8("real-clang-command"), &failures);
        header = S8("#define VALUE (*(int*)0)\n");
        options.results = path_join(arena, root, S8("changed-header"));
        clang_analyze_test_check(clang_analyze_write(arena, path_join(arena, root, S8("input.h")), header) && !clang_analyze_run(arena, options),
                                 S8("transitive-header-is-reanalyzed"), &failures);
    }
    string_print(S8("ANALYZE_SELF_TEST failures={u64} evidence={S8}\n"), failures, root);
    return failures == 0;
}
