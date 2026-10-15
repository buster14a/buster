// Ordinary post-merge external-project routing (#3501).
// Included by build.c after compatibility harnesses. external_projects_main
// owns fixed project/configuration assignments and durable start/end records.
// Harnesses retain their source/oracle/watchdog contracts; no failure is waived.
typedef enum ExternalProject
{
    EXTERNAL_PROJECT_CJSON,
    EXTERNAL_PROJECT_ZLIB,
    EXTERNAL_PROJECT_STB,
    EXTERNAL_PROJECT_LUA,
    EXTERNAL_PROJECT_QUICKJS,
    EXTERNAL_PROJECT_SQLITE,
    EXTERNAL_PROJECT_GIT,
    EXTERNAL_PROJECT_RAD,
    EXTERNAL_PROJECT_FFMPEG,
    EXTERNAL_PROJECT_COUNT,
} ExternalProject;

// The userspace verifier is defined later in build.c's unity include order.
BUSTER_GLOBAL_LOCAL ProcessResult userspace_input_self_test(Arena* arena);
BUSTER_GLOBAL_LOCAL ProcessResult ffmpeg_main(Arena* arena, SliceString8 arguments, String8 driver_argument);

BUSTER_GLOBAL_LOCAL String8 external_project_names[] = {
    S8_INITIALIZER("cjson"), S8_INITIALIZER("zlib"), S8_INITIALIZER("stb"),
    S8_INITIALIZER("lua"), S8_INITIALIZER("quickjs"), S8_INITIALIZER("sqlite"),
    S8_INITIALIZER("git"), S8_INITIALIZER("rad"), S8_INITIALIZER("ffmpeg"),
};

BUSTER_GLOBAL_LOCAL bool external_projects_sha_valid(String8 sha)
{
    bool valid = sha.length == 40;
    for (u64 i = 0; valid && i < sha.length; i += 1)
    {
        valid = (sha.pointer[i] >= '0' && sha.pointer[i] <= '9') ||
                (sha.pointer[i] >= 'a' && sha.pointer[i] <= 'f');
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool external_projects_event_valid(String8 event, String8 ref, String8 submitted, String8 checkout)
{
    bool valid = string_equal(event, S8("push")) && string_equal(ref, S8("refs/heads/main")) &&
                 external_projects_sha_valid(submitted) && string_equal(submitted, checkout);
    return valid;
}

BUSTER_GLOBAL_LOCAL u32 external_projects_find(String8 name)
{
    u32 result = EXTERNAL_PROJECT_COUNT;
    for (u32 i = 0; i < EXTERNAL_PROJECT_COUNT; i += 1)
    {
        if (string_equal(name, external_project_names[i]))
        {
            result = i;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool external_projects_record(Arena* arena, u32 project, String8 status, u64 elapsed_us)
{
    String8 path = string_format(arena, S8("evidence/projects/{S8}.tsv"), external_project_names[project]);
    String8 text = string_format(arena,
        S8("project\tstatus\tdiagnostic_wall_us\tsha\trun\tattempt\tconfiguration\n"
           "{S8}\t{S8}\t{u64}\t{S8}\t{S8}\t{S8}\t{S8}\n"),
        external_project_names[project], status, elapsed_us, os_get_environment_variable(S8("GITHUB_SHA")),
        os_get_environment_variable(S8("GITHUB_RUN_ID")), os_get_environment_variable(S8("GITHUB_RUN_ATTEMPT")),
        project == EXTERNAL_PROJECT_RAD ? S8("Release;allocations-on;debug-info-off") :
        project == EXTERNAL_PROJECT_FFMPEG ? S8("Release;FFmpeg-7.1.3;portable-c17;portable-gnu17;optimized-gnu17;external-NASM") : S8("Release;default-harness"));
    bool result = file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text));
    string_print(S8("EXTERNAL_PROJECT_V1 {S8}"), text);
    return result;
}

BUSTER_GLOBAL_LOCAL bool external_projects_ffmpeg_record(Arena* arena, String8* status)
{
    String8 profiles = string_format(arena, S8("profile\tresult\nportable-c17\t{S8}\nportable-gnu17\t{S8}\noptimized-gnu17\t{S8}\n"),
        status[0], status[1], status[2]);
    bool result = file_write(S8("build/ffmpeg-profiles.tsv"), BUSTER_SLICE_TO_BYTE_SLICE(profiles));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult external_projects_execute(Arena* arena, u32 project)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    switch (project)
    {
        case EXTERNAL_PROJECT_CJSON:
        {
            TestCjsonOptions options = {.build_directory = S8("build"), .config = S8("Release"), .source_directory = S8("external/cjson")};
            result = test_cjson_action(arena, &options);
        }
        break;
        case EXTERNAL_PROJECT_ZLIB:
        {
            TestZlibOptions options = {.build_directory = S8("build"), .config = S8("Release"), .source_directory = S8("external/zlib")};
            result = test_zlib_action(arena, &options);
        }
        break;
        case EXTERNAL_PROJECT_STB:
        {
            TestStbOptions options = {.build_directory = S8("build"), .config = S8("Release"), .source_directory = S8("external/stb")};
            result = test_stb_action(arena, &options);
        }
        break;
        case EXTERNAL_PROJECT_LUA:
        {
            TestLuaOptions options = {.build_directory = S8("build"), .config = S8("Release"),
                .source_directory = S8("external/lua-5.4.8/src"), .compatibility_directory = S8("external/lua-git")};
            result = test_lua_action(arena, &options);
        }
        break;
        case EXTERNAL_PROJECT_QUICKJS:
        {
            TestQuickjsOptions options = {.build_directory = S8("build"), .config = S8("Release"), .source_directory = S8("external/quickjs")};
            result = test_quickjs_action(arena, &options);
        }
        break;
        case EXTERNAL_PROJECT_SQLITE:
        {
            TestSqliteOptions options = {.build_directory = S8("build"), .config = S8("Release"),
                .amalgamation_directory = S8("external/sqlite-amalgamation-3530400"), .source_directory = S8("external/sqlite-src-3530400")};
            result = test_sqlite_action(arena, &options);
        }
        break;
        case EXTERNAL_PROJECT_GIT:
        {
            String8 args[] = {S8(".ci-tools/git-qualification"), S8("--run"), S8("external/git"), S8("build/Release/ide"),
                executable_resolve_in_path(arena, S8("clang")), S8("evidence/git")};
            CpythonCommandResult run = cpython_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(args), S8("."), false,
                (SliceString8){0}, (SliceString8){0}, 0);
            result = run.result;
        }
        break;
        case EXTERNAL_PROJECT_RAD:
        {
            TestRaddebuggerOptions options = {.build_directory = S8("build/rad-compiler"), .config = S8("Release"), .source_directory = S8("external/raddebugger")};
            result = test_raddebugger_action(arena, &options);
        }
        break;
        case EXTERNAL_PROJECT_FFMPEG:
        {
            String8 args[] = {S8("--portable"), S8("external/ffmpeg"), S8("build/ffmpeg-portable"), S8("build/Release/ide")};
            String8 portable_args[] = {S8("--portable-gnu17"), S8("external/ffmpeg"), S8("build/ffmpeg-portable-gnu17"), S8("build/Release/ide")};
            String8 optimized_args[] = {S8("--optimized-gnu17"), S8("external/ffmpeg"), S8("build/ffmpeg-optimized-gnu17"), S8("build/Release/ide"), executable_resolve_in_path(arena, S8("nasm"))};
            String8 driver = build_running_driver(arena);
            SliceString8 profiles[] = {BUSTER_ARRAY_TO_SLICE(args), BUSTER_ARRAY_TO_SLICE(portable_args), BUSTER_ARRAY_TO_SLICE(optimized_args)};
            String8 status[] = {S8("not-run"), S8("not-run"), S8("not-run")};
            bool recorded = external_projects_ffmpeg_record(arena, status);
            bool passed = true;
            // Keep ISO acceptance independent and attempt both GNU controls
            // even when an earlier census fails. Publish incomplete state
            // before each profile so job interruption cannot look complete.
            for (u32 i = 0; recorded && i < BUSTER_ARRAY_LENGTH(profiles); i += 1)
            {
                status[i] = S8("started;incomplete");
                recorded = external_projects_ffmpeg_record(arena, status);
                if (recorded)
                {
                    ProcessResult profile = ffmpeg_main(arena, profiles[i], driver);
                    status[i] = profile == PROCESS_RESULT_SUCCESS ? S8("pass") : S8("fail");
                    recorded = external_projects_ffmpeg_record(arena, status);
                    passed = profile == PROCESS_RESULT_SUCCESS && passed;
                }
            }
            result = recorded && passed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
        break;
        default: break;
    }
#else
    (void)arena;
    (void)project;
    string_print(S8("external project assignment is pending on this native platform; no qualification executed\n"));
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult external_projects_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test")))
    {
        String8 sha = S8("0123456789abcdef0123456789abcdef01234567");
        String8 later = S8("1123456789abcdef0123456789abcdef01234567");
        bool passed = external_projects_event_valid(S8("push"), S8("refs/heads/main"), sha, sha) &&
            !external_projects_event_valid(S8("push"), S8("refs/heads/main"), sha, later) &&
            !external_projects_event_valid(S8("push"), S8("refs/tags/v1"), sha, sha) &&
            !external_projects_event_valid(S8("push"), S8("refs/heads/candidate"), sha, sha) &&
            !external_projects_event_valid(S8("pull_request"), S8("refs/heads/main"), sha, sha) &&
            !external_projects_event_valid(S8("pull_request_target"), S8("refs/heads/main"), sha, sha) &&
            !external_projects_event_valid(S8("merge_group"), S8("refs/heads/main"), sha, sha) &&
            !external_projects_event_valid(S8("workflow_dispatch"), S8("refs/heads/main"), sha, sha) &&
            !external_projects_event_valid(S8("workflow_call"), S8("refs/heads/main"), sha, sha) &&
            !external_projects_event_valid(S8("push"), S8("refs/heads/main"), S8("bad"), S8("bad")) &&
            external_projects_find(S8("unknown")) == EXTERNAL_PROJECT_COUNT &&
            BUSTER_ARRAY_LENGTH(external_project_names) == EXTERNAL_PROJECT_COUNT;
        for (u32 i = 0; i < EXTERNAL_PROJECT_COUNT; i += 1)
        {
            passed = passed && external_projects_find(external_project_names[i]) == i;
        }
#if BUSTER_LINUX
        // Keep the private userspace immutability controls inside the one
        // policy-owned generic external-project self-test route.
        passed = userspace_input_self_test(arena) == PROCESS_RESULT_SUCCESS && passed;
#endif
        string_print(S8("EXTERNAL_PROJECT_SELF_TEST {S8}\n"), passed ? S8("pass") : S8("failed"));
        result = passed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (arguments.length == 1)
    {
        u32 project = external_projects_find(arguments.pointer[0]);
        String8 git = executable_resolve_in_path(arena, S8("git"));
        String8 args[] = {git, S8("--no-replace-objects"), S8("rev-parse"), S8("HEAD")};
        CpythonCommandResult identity = cpython_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(args), S8("."), true,
            (SliceString8){0}, (SliceString8){0}, 10000000);
        bool admitted = identity.result == PROCESS_RESULT_SUCCESS && !identity.error.length && project < EXTERNAL_PROJECT_COUNT &&
            string_equal(os_get_environment_variable(S8("GITHUB_REPOSITORY")), S8("buster14a/buster")) &&
            external_projects_event_valid(os_get_environment_variable(S8("GITHUB_EVENT_NAME")), os_get_environment_variable(S8("GITHUB_REF")),
                os_get_environment_variable(S8("GITHUB_SHA")), cpython_trim_ascii_space(identity.output));
        if (admitted && external_projects_record(arena, project, S8("started;incomplete"), 0))
        {
            u64 begin = os_now_microseconds();
            result = external_projects_execute(arena, project);
            bool recorded = external_projects_record(arena, project,
                result == PROCESS_RESULT_SUCCESS ? S8("pass") : S8("failed;see-harness-stage-logs"), os_now_microseconds() - begin);
            if (!recorded)
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
        else
        {
            string_print(S8("error: external_projects requires an inventoried project and the original main-push checkout SHA\n"));
        }
    }
    else
    {
        string_print(S8("usage: build external_projects --self-test|cjson|zlib|stb|lua|quickjs|sqlite|git|rad|ffmpeg\n"));
    }
    return result;
}
