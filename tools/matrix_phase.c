// Desktop matrix observation only. Included by build.c after coverage/scheduler
// definitions. matrix_phase_begin/tree/plan bind policy; matrix_phase_run owns
// child exit/timing authority. No observation changes graph edges or quotas.
#define MATRIX_PHASE_MAX_TASKS 96

typedef struct MatrixPhaseTree MatrixPhaseTree;
struct MatrixPhaseTree
{
    String8 id, directory, rows;
};
typedef struct MatrixPhaseState MatrixPhaseState;
struct MatrixPhaseState
{
    String8 root, driver, identity, scheduler;
    String8List trees, tasks;
    MatrixPhaseTree tree[MATRIX_COVERAGE_MAX_TREES];
    u32 tree_count, task_count, outer_jobs;
    u64 epoch;
    bool enabled, valid;
};
BUSTER_GLOBAL_LOCAL MatrixPhaseState matrix_phase = {0};

BUSTER_GLOBAL_LOCAL String8 matrix_phase_array(Arena* arena, SliceString8 values)
{
    String8List parts = {0};
    for (u64 i = 0; i < values.length; i += 1)
    {
        if (i) { string8_list_push(arena, &parts, S8(",")); }
        string8_list_push(arena, &parts, matrix_coverage_json_escape(arena, values.pointer[i]));
    }
    return string_format(arena, S8("[{S8}]"), string_join_arena(arena, string8_list_to_slice(arena, parts), false));
}

BUSTER_GLOBAL_LOCAL bool matrix_phase_write(Arena* arena, String8 root, String8 name, String8 value)
{
    String8 path = path_join(arena, root, name);
    bool result = file_publish(path, BUSTER_SLICE_TO_BYTE_SLICE(value));
    if (!result) { string_print(S8("error: matrix phase evidence publication failed: {S8}\n"), path); }
    return result;
}

BUSTER_GLOBAL_LOCAL bool matrix_phase_begin(Arena* arena, MatrixCoverageManifest* coverage, bool direct)
{
    String8 root = os_get_environment_variable(S8("BUSTER_MATRIX_PHASE_OUTPUT"));
    bool result = true;
    if (root.length)
    {
        make_directory_recursive(arena, root);
        matrix_phase = (MatrixPhaseState){.root = os_path_absolute(arena, root, true), .driver = build_running_driver(arena),
            .epoch = os_now_microseconds(), .enabled = true, .valid = true, .scheduler = direct ? S8("direct") : S8("pooled"), .outer_jobs = direct ? 1u : 0u};
        result = matrix_phase.root.length && !path_exists(arena, path_join(arena, root, S8("plan.json")));
        MatrixCoverageLane lane = coverage->lane;
        // This contract requires a Git tree, never the stage object's commit
        // fallback. Search PATH explicitly, including on Windows.
        String8 source_tree = {0};
        String8 git_arguments[] = {S8("git"), S8("rev-parse"), S8("HEAD^{tree}")};
        ProcessSpawnResult git = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(git_arguments), (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR), .use_process_environment = 1, .search_path = 1});
        if (git.handle)
        {
            ProcessWaitResult wait = os_process_wait_deadline(arena, git, 30 * 1000000);
            if (wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out)
            {
                source_tree = build_compiler_output_trim(BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]));
            }
        }
        result = result && source_tree.length == 40;
        for (u64 i = 0; result && i < source_tree.length; i += 1)
        {
            char8 c = source_tree.pointer[i];
            result = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }
        matrix_phase.identity = string_format(arena,
            S8("{{\"lane_id\":{S8},\"source_revision\":{S8},\"source_tree\":{S8},\"source_hash\":{S8},\"driver_hash\":{S8},\"repository\":{S8},\"run_id\":{S8},\"run_attempt\":{S8},\"workflow\":{S8},\"job\":{S8},\"platform\":{S8},\"architecture\":{S8},\"shard\":{S8}}}"),
            matrix_coverage_json_escape(arena, lane.lane_id), matrix_coverage_json_escape(arena, lane.source_revision),
            matrix_coverage_json_escape(arena, source_tree), matrix_coverage_json_escape(arena, lane.source_hash),
            matrix_coverage_json_escape(arena, lane.driver_hash), matrix_coverage_json_escape(arena, lane.repository),
            matrix_coverage_json_escape(arena, lane.run_id), matrix_coverage_json_escape(arena, lane.run_attempt),
            matrix_coverage_json_escape(arena, matrix_coverage_environment_value(S8("GITHUB_WORKFLOW"))),
            matrix_coverage_json_escape(arena, matrix_coverage_environment_value(S8("GITHUB_JOB"))),
            matrix_coverage_json_escape(arena, lane.platform), matrix_coverage_json_escape(arena, lane.architecture),
            matrix_coverage_json_escape(arena, lane.shard));
        matrix_phase.valid = result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 matrix_phase_task(Arena* arena, String8 tree, String8 phase, String8 config,
                                             String8 pool, String8 dependency, u32 jobs, SliceString8 argv)
{
    String8 id = string_format(arena, S8("{S8}-{S8}-{S8}"), tree, phase, config.length ? config : S8("all"));
    if (matrix_phase.enabled)
    {
        if (matrix_phase.task_count++) { string8_list_push(arena, &matrix_phase.tasks, S8(",\n")); }
        matrix_phase.valid = matrix_phase.valid && matrix_phase.task_count <= MATRIX_PHASE_MAX_TASKS;
        string8_list_push(arena, &matrix_phase.tasks, string_format(arena,
            S8("{{\"id\":{S8},\"tree\":{S8},\"phase\":{S8},\"configuration\":{S8},\"pool_edge\":{S8},\"dependency\":{S8},\"inner_jobs\":{S8},\"argv\":{S8}}}"),
            matrix_coverage_json_escape(arena, id), matrix_coverage_json_escape(arena, tree), matrix_coverage_json_escape(arena, phase),
            matrix_coverage_json_escape(arena, config), matrix_coverage_json_escape(arena, pool), matrix_coverage_json_escape(arena, dependency),
            jobs ? string_format(arena, S8("{u32}"), jobs) : S8("\"unknown\""), matrix_phase_array(arena, argv)));
    }
    return id;
}

BUSTER_GLOBAL_LOCAL SliceString8 matrix_phase_prefix(Arena* arena, String8 id, u64 timeout_seconds)
{
    String8* values = arena_allocate(arena, String8, 7);
    values[0] = matrix_phase.driver;
    values[1] = S8("matrix_phase_run");
    values[2] = matrix_phase.root;
    values[3] = id;
    values[4] = string_format(arena, S8("{u64}"), matrix_phase.epoch);
    values[5] = string_format(arena, S8("{u64}"), timeout_seconds);
    values[6] = S8("--");
    return (SliceString8){.pointer = values, .length = 7};
}

BUSTER_GLOBAL_LOCAL void matrix_phase_wrap(Arena* arena, ProcessRun* run, String8 tree, String8 phase, String8 config, u32 jobs)
{
    if (matrix_phase.enabled)
    {
        String8 id = matrix_phase_task(arena, tree, phase, config, S8(""), S8("ready"), jobs, run->arguments);
        SliceString8 prefix = matrix_phase_prefix(arena, id, run->timeout_seconds);
        OsArgumentBuilder builder = os_argument_builder_start(arena);
        for (u64 i = 0; i < prefix.length; i += 1) { os_argument_builder_append(&builder, prefix.pointer[i]); }
        for (u64 i = 0; i < run->arguments.length; i += 1) { os_argument_builder_append(&builder, run->arguments.pointer[i]); }
        run->arguments = os_argument_builder_flush(&builder);
        run->phase_task = id;
        // The observer waits immediately for this particular child. The parent
        // may reap concurrent observers in launch order without distorting ends.
        run->timeout_seconds = 0;
    }
}

BUSTER_GLOBAL_LOCAL Generate matrix_phase_tree(Arena* arena, Generate generate, MatrixCoverageManifest* coverage, MatrixCoverageTreePlan tree)
{
    if (matrix_phase.enabled)
    {
        u32 index = matrix_phase.tree_count++;
        MatrixPhaseTree* record = &matrix_phase.tree[index];
        record->id = string_format(arena, S8("tree{u32}"), index);
        record->directory = generate.build_directory;
        String8 rows[2] = {0};
        for (u32 i = 0; i < tree.row_count; i += 1) { rows[i] = coverage->plan.rows[tree.row_indices[i]].id; }
        record->rows = matrix_phase_array(arena, (SliceString8){.pointer = rows, .length = tree.row_count});
        MatrixCoverageCapability compiler = coverage->capabilities[tree.compiler];
        if (index) { string8_list_push(arena, &matrix_phase.trees, S8(",\n")); }
        string8_list_push(arena, &matrix_phase.trees, string_format(arena,
            S8("{{\"id\":{S8},\"build_directory\":{S8},\"rows\":{S8},\"compiler\":{S8},\"compiler_path\":{S8},\"compiler_sha256\":{S8},\"compiler_identity\":{S8},\"compiler_version\":{S8},\"target\":{S8},\"configurations\":{S8},\"sanitize\":{u32},\"fuzz\":{u32},\"lto\":false,\"generator\":\"Ninja Multi-Config\",\"linker\":{S8}}}"),
            matrix_coverage_json_escape(arena, record->id), matrix_coverage_json_escape(arena, record->directory), record->rows,
            matrix_coverage_json_escape(arena, build_compilers[tree.compiler]), matrix_coverage_json_escape(arena, compiler.path),
            matrix_coverage_json_escape(arena, compiler.executable_hash), matrix_coverage_json_escape(arena, compiler.identity),
            matrix_coverage_json_escape(arena, compiler.version), matrix_coverage_json_escape(arena, compiler.target),
            matrix_coverage_json_escape(arena, generate.configuration_types), generate.sanitize, generate.fuzz_available,
            matrix_coverage_json_escape(arena, generate_linker(generate, compiler.path))));
        // OsArgumentBuilder is a contiguous arena-backed String8 array.
        // Materialize strings first; allocations during append corrupt it.
        String8 observer_arguments[] = {
            cmake_string(arena, S8("BUSTER_MATRIX_PHASE_DRIVER"), matrix_phase.driver),
            cmake_string(arena, S8("BUSTER_MATRIX_PHASE_ROOT"), matrix_phase.root),
            cmake_string(arena, S8("BUSTER_MATRIX_PHASE_TREE"), record->id),
            cmake_string(arena, S8("BUSTER_MATRIX_PHASE_EPOCH"), string_format(arena, S8("{u64}"), matrix_phase.epoch)),
        };
        OsArgumentBuilder args = os_argument_builder_start(arena);
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(observer_arguments); i += 1) { os_argument_builder_append(&args, observer_arguments[i]); }
        generate.phase_arguments = os_argument_builder_flush(&args);
        for (u32 i = 0; i < tree.row_count; i += 1)
        {
            MatrixCoverageRow row = coverage->plan.rows[tree.row_indices[i]];
            if (tree.compiler == BUILD_COMPILER_CLANG)
            {
                matrix_phase_task(arena, record->id, S8("test"), row.configuration, S8(""), S8("nested"), 0, (SliceString8){0});
            }
        }
    }
    return generate;
}

BUSTER_GLOBAL_LOCAL String8 matrix_phase_find_tree(String8 directory)
{
    String8 result = {0};
    for (u32 i = 0; i < matrix_phase.tree_count; i += 1)
    {
        if (string_equal(directory, matrix_phase.tree[i].directory)) { result = matrix_phase.tree[i].id; }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void matrix_phase_cmake(Arena* arena, String8List* lines, String8 variable, String8 tree,
                                           String8 phase, String8 config, String8 pool, String8 dependency, u32 jobs)
{
    string8_list_push(arena, lines, string_format(arena, S8("set({S8}"), variable));
    if (matrix_phase.enabled)
    {
        String8 id = matrix_phase_task(arena, tree, phase, config, pool, dependency, jobs, (SliceString8){0});
        SliceString8 prefix = matrix_phase_prefix(arena, id, 0);
        for (u64 i = 0; i < prefix.length; i += 1)
        {
            string8_list_push(arena, lines, string_format(arena, S8(" [==[{S8}]==]"), prefix.pointer[i]));
        }
    }
    string8_list_push(arena, lines, S8(")\n"));
}

BUSTER_GLOBAL_LOCAL bool matrix_phase_callback(Arena* arena, ProcessRun* run, bool completed, ProcessResult status)
{
    bool result = true;
    if (matrix_phase.enabled && run->phase_task.length)
    {
        u64 now = os_now_microseconds(), pid = os_get_current_process_id();
        if (!completed) { run->start_us = now; }
        String8 common = string_format(arena,
            S8("\"id\":{S8},\"epoch_us\":{u64},\"pid\":{u64},\"start_us\":{u64},\"argv\":{S8},\"authority\":\"driver_callback\""),
            matrix_coverage_json_escape(arena, run->phase_task), matrix_phase.epoch, pid, run->start_us, matrix_phase_array(arena, run->arguments));
        String8 value = completed ? string_format(arena,
            S8("{{{S8},\"state\":{S8},\"child_start_us\":{u64},\"end_us\":{u64},\"publication_start_us\":{u64},\"result\":{u32},\"cpu_time\":\"unknown\",\"peak_rss\":\"unknown\"}}\n"),
            common, matrix_coverage_json_escape(arena, status == PROCESS_RESULT_SUCCESS ? S8("success") : S8("failure")),
            run->start_us, now, os_now_microseconds(), (u32)status) : string_format(arena, S8("{{{S8},\"state\":\"running\"}}\n"), common);
        String8 name = string_format(arena, S8("{S8}.{u64}.{S8}.json"), run->phase_task, pid, completed ? S8("end") : S8("start"));
        result = matrix_phase_write(arena, matrix_phase.root, name, value);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool matrix_phase_plan(Arena* arena, bool direct)
{
    bool result = true;
    if (matrix_phase.enabled)
    {
        for (BuildStep* step = program.build_graph.first_step; step; step = step->next)
        {
            for (ProcessRun* run = step->first_process; run; run = run->next)
            {
                String8 directory = {0}, config = {0}, phase = S8("build");
                if (run->callback)
                {
                    String8 label = {0}, tree = S8("matrix");
                    if (run->callback == matrix_coverage_complete_action) { label = S8("coverage"); }
                    else if (run->callback == build_artifact_fanout_capture_action || run->callback == build_artifact_fanout_clean_action)
                    {
                        BuildArtifactFanout* fanout = run->callback_data;
                        tree = matrix_phase_find_tree(fanout->build_directory);
                        label = run->callback == build_artifact_fanout_capture_action ? S8("capture") : S8("clean");
                    }
                    else if (run->callback == x86_completion_census_existing_validate_action || run->callback == x86_completion_census_prepare_output_action)
                    {
                        X86CompletionCensusPlan* census = run->callback_data;
                        tree = matrix_phase_find_tree(census->fanout.build_directory);
                        label = run->callback == x86_completion_census_existing_validate_action ? S8("census_validate") : S8("census_prepare");
                    }
                    if (label.length && tree.length)
                    {
                        String8* identity = arena_allocate(arena, String8, 1);
                        identity[0] = label;
                        run->arguments = (SliceString8){.pointer = identity, .length = 1};
                        run->phase_task = matrix_phase_task(arena, tree, S8("evidence"), label, S8(""), S8("ready"), 0, run->arguments);
                    }
                }
                if (!run->phase_task.length && !run->callback)
                {
                    for (u64 i = 0; i + 1 < run->arguments.length; i += 1)
                    {
                        String8 arg = run->arguments.pointer[i];
                        if (string_equal(arg, S8("--build"))) { directory = run->arguments.pointer[i + 1]; }
                        if (string_equal(arg, S8("--config"))) { config = run->arguments.pointer[i + 1]; }
                        if (string_equal(arg, S8("--target")) && string_equal(run->arguments.pointer[i + 1], S8("test_all"))) { phase = S8("validation"); }
                        if (string_equal(arg, S8("--target")) && string_equal(run->arguments.pointer[i + 1], S8("clean"))) { phase = S8("clean"); }
                    }
                    if (run->arguments.length > 1 && string_equal(run->arguments.pointer[1], S8("x86_64_completion_census")))
                    {
                        directory = path_parent(arena, path_parent(arena, run->arguments.pointer[0]));
                        config = S8("Release");
                        phase = S8("census");
                    }
                    if (direct && !directory.length)
                    {
                        for (u64 i = 0; i < run->arguments.length; i += 1)
                        {
                            for (u32 j = 0; j < matrix_phase.tree_count; j += 1)
                            {
                                String8 database = path_join(arena, matrix_phase.tree[j].directory, S8("compile_commands.json"));
                                if (string_equal(run->arguments.pointer[i], database) ||
                                    (run->arguments.length > 1 && string_equal(run->arguments.pointer[1], S8("clang_analyze")) &&
                                     string_equal(run->arguments.pointer[i], matrix_phase.tree[j].directory)))
                                {
                                    directory = matrix_phase.tree[j].directory;
                                    phase = S8("post_test");
                                }
                            }
                        }
                    }
                    String8 tree = matrix_phase_find_tree(directory);
                    if (tree.length && (direct || string_equal(phase, S8("clean")) || string_equal(phase, S8("census"))))
                    { matrix_phase_wrap(arena, run, tree, phase, config, 0); }
                    else if (directory.length && !tree.length && !direct)
                    {
                        matrix_phase_wrap(arena, run, S8("matrix"), S8("scheduler"), S8(""), matrix_phase.outer_jobs);
                    }
                }
            }
        }
        String8 json = string_format(arena,
            S8("{{\"schema\":\"buster-desktop-phases-v1\",\"epoch_us\":{u64},\"identity\":{S8},\"scheduler\":{S8},\"outer_jobs\":{u32},\"logical_cpus\":{u32},\"cpu_budget\":{u64},\"cpu_time\":\"unknown\",\"peak_rss\":\"unknown\",\"trees\":[{S8}],\"tasks\":[{S8}]}}\n"),
            matrix_phase.epoch, matrix_phase.identity, matrix_coverage_json_escape(arena, matrix_phase.scheduler),
            matrix_phase.outer_jobs, os_get_logical_thread_count(),
            environment_positive_u64_or(S8("BUSTER_MATRIX_THREADS"), os_get_logical_thread_count()),
            string_join_arena(arena, string8_list_to_slice(arena, matrix_phase.trees), false),
            string_join_arena(arena, string8_list_to_slice(arena, matrix_phase.tasks), false));
        result = matrix_phase.valid && matrix_phase_write(arena, matrix_phase.root, S8("plan.json"), json);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool matrix_phase_ready(Arena* arena, BuildStep* step)
{
    bool result = true;
    u64 now = matrix_phase.enabled ? os_now_microseconds() : 0;
    for (ProcessRun* run = step->first_process; run; run = run->next)
    {
        if (run->phase_task.length)
        {
            String8 value = string_format(arena, S8("{{\"epoch_us\":{u64},\"ready_us\":{u64}}}\n"), matrix_phase.epoch, now);
            result = matrix_phase_write(arena, matrix_phase.root, string_format(arena, S8("{S8}.ready.json"), run->phase_task), value) && result;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult matrix_phase_finish(Arena* arena, ProcessResult status)
{
    ProcessResult result = status;
    if (matrix_phase.enabled)
    {
        String8 value = string_format(arena, S8("{{\"epoch_us\":{u64},\"terminal_us\":{u64},\"result\":{u32}}}\n"),
                                     matrix_phase.epoch, os_now_microseconds(), (u32)status);
        if (!matrix_phase_write(arena, matrix_phase.root, S8("terminal.json"), value)) { result = PROCESS_RESULT_FAILED; }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult matrix_phase_run(Arena* arena, SliceString8 args)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    u64 epoch = 0, timeout = 0;
    bool valid = args.length >= 6 && text_parse_u64(args.pointer[2], &epoch) && text_parse_u64(args.pointer[3], &timeout) &&
                 timeout <= 86400 && string_equal(args.pointer[4], S8("--"));
    for (u64 i = 0; valid && i < args.pointer[1].length; i += 1)
    {
        u8 c = (u8)args.pointer[1].pointer[i];
        valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    }
    if (valid && args.pointer[1].length && path_exists(arena, path_join(arena, args.pointer[0], S8("plan.json"))))
    {
        String8 root = args.pointer[0], id = args.pointer[1];
        SliceString8 argv = {.pointer = args.pointer + 5, .length = args.length - 5};
        u64 pid = os_get_current_process_id(), start = os_now_microseconds();
        String8 stem = string_format(arena, S8("{S8}.{u64}"), id, pid);
        String8 common = string_format(arena, S8("\"id\":{S8},\"epoch_us\":{u64},\"pid\":{u64},\"start_us\":{u64},\"argv\":{S8}"),
                                      matrix_coverage_json_escape(arena, id), epoch, pid, start, matrix_phase_array(arena, argv));
        String8 started = string_format(arena, S8("{{{S8},\"state\":\"running\"}}\n"), common);
        String8 start_name = string_format(arena, S8("{S8}.start.json"), stem);
        if (!path_exists(arena, path_join(arena, root, start_name)) && matrix_phase_write(arena, root, start_name, started))
        {
            u64 child_start = os_now_microseconds();
            ProcessSpawnResult spawn = os_process_spawn(argv, (SliceString8){0}, (SliceString8){0},
                (ProcessSpawnOptions){.use_process_environment = 1, .new_process_group = timeout != 0, .search_path = 1});
            ProcessWaitResult wait = {.result = PROCESS_RESULT_NOT_EXISTENT};
            if (spawn.handle) { wait = os_process_wait_deadline(arena, spawn, timeout * 1000000); }
            u64 end = os_now_microseconds();
            String8 status = wait.timed_out ? S8("timeout") : wait.result == PROCESS_RESULT_SUCCESS ? S8("success") : S8("failure");
            String8 completed = string_format(arena,
                S8("{{{S8},\"state\":{S8},\"child_start_us\":{u64},\"end_us\":{u64},\"publication_start_us\":{u64},\"result\":{u32},\"platform_status\":{u32},\"spawned\":{u32},\"timed_out\":{u32},\"termination_requested\":{u32},\"forcibly_terminated\":{u32},\"cpu_time\":\"unknown\",\"peak_rss\":\"unknown\",\"test_jobs\":{S8},\"ctest_jobs\":\"not-applicable\"}}\n"),
                common, matrix_coverage_json_escape(arena, status), child_start, end, os_now_microseconds(), (u32)wait.result,
                wait.platform_status, spawn.handle ? 1u : 0u, (u32)wait.timed_out, (u32)wait.termination_requested, (u32)wait.forcibly_terminated,
                matrix_coverage_json_escape(arena, os_get_environment_variable(S8("BUSTER_TEST_JOBS")).length ?
                    os_get_environment_variable(S8("BUSTER_TEST_JOBS")) : S8("unknown")));
            bool published = matrix_phase_write(arena, root, string_format(arena, S8("{S8}.end.json"), stem), completed);
            result = published && !wait.timed_out ? wait.result : PROCESS_RESULT_FAILED;
        }
    }
    if (result != PROCESS_RESULT_SUCCESS) { string_print(S8("error: matrix phase worker or evidence failed\n")); }
    return result;
}
