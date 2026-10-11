// Hosted-only full native preparation diagnostic, included after the shared
// preparation controller. Entry: compiler_preparation_fixture_main.
// The private checkout uses the historical TCC bootstrap and real Clang tiny
// compilers/harness. Production execution/receipt/export functions own all
// preparation, five pair phases, cleanup and cost records. The fixed provider
// only writes marked diagnostic data. No admitted or 9700X run may enter here.
#if BUSTER_LINUX && !BUSTER_ANDROID
typedef struct CompilerPreparationFixture CompilerPreparationFixture;
struct CompilerPreparationFixture
{
    String8 directory, root, base, base_tree, head, head_tree;
    bool valid;
};

BUSTER_GLOBAL_LOCAL String8 compiler_preparation_fixture_host(Arena* arena)
{
    int descriptor = open("/proc/cpuinfo", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    u64 limit = 64 * 1024, used = 0;
    char8* bytes = arena_allocate(arena, char8, limit + 1);
    bool valid = descriptor >= 0, eof = false;
    while (valid && !eof && used < limit)
    {
        ssize_t count = read(descriptor, bytes + used, (size_t)(limit - used));
        if (count > 0) used += (u64)count;
        else if (!count) eof = true;
        else valid = errno == EINTR;
    }
    if (valid && !eof)
    {
        char extra = 0;
        valid = read(descriptor, &extra, 1) == 0;
    }
    if (descriptor >= 0) valid = close(descriptor) == 0 && valid;
    String8 result = {.pointer = bytes, .length = used};
    valid = valid && used && production_profile_contains(result, S8("model name")) &&
        !production_profile_contains(result, S8("AMD Ryzen 7 9700X"));
    for (u64 i = 0; valid && i < program_state->input.environment_keys.length; i += 1)
    {
        String8 key = program_state->input.environment_keys.pointer[i];
        valid = !string_starts_with_sequence(key, S8("BQ_")) || (string_equal(key, S8("BQ_REQUIRE_DISTINCT_GROUP")) &&
            getenv("BQ_REQUIRE_DISTINCT_GROUP") && strcmp(getenv("BQ_REQUIRE_DISTINCT_GROUP"), "1") == 0);
    }
    if (!valid) result = (String8){0};
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerPreparationFixture compiler_preparation_fixture_setup(Arena* arena, String8 python, String8 provider)
{
    CompilerPreparationFixture result = {0};
    String8 directory = string_format(arena, S8("build/compiler-preparation-fixture-{u64}"), os_now_microseconds());
    OsDirectoryCreateResult claimed = os_make_directory_exclusive(directory);
    if (claimed.created && !claimed.error.v)
    {
        directory = os_path_absolute(arena, directory, true);
        String8 root = path_join(arena, directory, S8("checkout with spaces"));
        make_directory_recursive(arena, path_join(arena, root, S8("src/generated")));
        make_directory_recursive(arena, path_join(arena, root, S8("build/generated")));
        make_directory_recursive(arena, path_join(arena, root, S8("build/Release")));
        make_directory_recursive(arena, path_join(arena, root, S8("build/throughput-tools")));
        make_directory_recursive(arena, path_join(arena, root, S8(".cache/bootstrap-driver")));
        make_directory_recursive(arena, path_join(arena, root, S8("tools/throughput")));
        String8 clang = os_path_absolute(arena, executable_resolve_in_path(arena, S8("clang")), true);
        String8 linker = os_path_absolute(arena, executable_resolve_in_path(arena, S8("ld")), true);
        String8 ninja = os_path_absolute(arena, executable_resolve_in_path(arena, S8("ninja")), true);
        String8 build = path_join(arena, root, S8("build"));
        String8 header = path_join(arena, root, S8("src/generated/ignored.h"));
        String8 generated = path_join(arena, build, S8("generated/value.h"));
        String8 bootstrap = path_join(arena, root, S8(".cache/bootstrap-driver/driver"));
        String8 script = path_join(arena, root, S8("build.sh"));
        String8 ide = path_join(arena, build, S8("Release/ide"));
        bool passed = clang.length && linker.length && ninja.length &&
            production_profile_write(path_join(arena, root, S8(".compiler-preparation-fixture")), S8("BUSTER_COMPILER_PREPARATION_DIAGNOSTIC_ONLY_V1\n")) &&
            production_profile_write(path_join(arena, root, S8(".gitignore")), S8("build/\n.cache/\nsrc/generated/\n")) &&
            production_profile_write(path_join(arena, root, S8("build.c")), S8("#include \"fixture-dependency.h\"\n"
                "#include <errno.h>\n"
                "#include <stdio.h>\n"
                "#include <string.h>\n"
                "#include <sys/stat.h>\n"
                "#include <sys/types.h>\n"
                "#include <sys/wait.h>\n"
                "#include <unistd.h>\n"
                "int main(int argc, char** argv)\n"
                "{\n"
                "    int result = 1;\n"
                "    if (argc >= 2 && strcmp(argv[1], \"generate\") == 0)\n"
                "    {\n"
                "        char directory[4096];\n"
                "        if (!getcwd(directory, sizeof(directory))) return 1;\n"
                "        if (mkdir(\"build\",0755) != 0 && errno != EEXIST) return 1;\n"
                "        if (mkdir(\"build/generated\",0755) != 0 && errno != EEXIST) return 1;\n"
                "        if (mkdir(\"build/Release\",0755) != 0 && errno != EEXIST) return 1;\n"
                "        if (mkdir(\"src\",0755) != 0 && errno != EEXIST) return 1;\n"
                "        if (mkdir(\"src/generated\",0755) != 0 && errno != EEXIST) return 1;\n"
                "        FILE* cache = fopen(\"build/CMakeCache.txt\",\"w\");\n"
                "        if (!cache) return 1;\n"
                "        int printed = fprintf(cache,\"BUSTER_INCLUDE_TESTS:BOOL=OFF\\nCMAKE_HOME_DIRECTORY:INTERNAL=%s\\n\"\n"
                "            \"CMAKE_C_COMPILER:FILEPATH=%s\\nCMAKE_LINKER:FILEPATH=%s\\nCMAKE_MAKE_PROGRAM:FILEPATH=%s\\n\",\n"
                "            directory,FIXTURE_CLANG,FIXTURE_LINKER,FIXTURE_NINJA);\n"
                "        int closed = fclose(cache);\n"
                "        FILE* ignored = fopen(\"src/generated/ignored.h\",\"w\");\n"
                "        if (!ignored) return 1;\n"
                "        int generated = fputs(\"#define FIXTURE_MESSAGE \\\"baseline corpus consumer\\\"\\n\",ignored);\n"
                "        int ignored_closed = fclose(ignored);\n"
                "        FILE* generated_build = fopen(\"build/generated/value.h\",\"w\");\n"
                "        if (!generated_build) return 1;\n"
                "        int build_written = fputs(\"#define FIXTURE_BUILD 1\\n\",generated_build);\n"
                "        int build_closed = fclose(generated_build);\n"
                "        return printed > 0 && !closed && generated >= 0 && !ignored_closed && build_written >= 0 && !build_closed ? 0 : 1;\n"
                "    }\n"
                "    if ((argc == 3 && strcmp(argv[1], \"bench_throughput\") == 0 && strcmp(argv[2], \"help\") == 0) ||\n"
                "        (argc >= 2 && strcmp(argv[1], \"build\") == 0))\n"
                "    {\n"
                "        int directories = (mkdir(\"build\", 0755) == 0 || errno == EEXIST);\n"
                "        directories = directories && (mkdir(\"build/throughput-tools\", 0755) == 0 || errno == EEXIST);\n"
                "        pid_t child = directories ? fork() : -1;\n"
                "        if (child == 0)\n"
                "        {\n"
                "            execlp(\"clang\", \"clang\", \"-std=c11\", \"-O2\", \"-Wall\", \"-Wextra\", \"-Werror\",\n"
                "                \"-fwrapv\", \"-fno-strict-aliasing\", \"-funsigned-char\",\n"
                "                \"tools/throughput/fixture.c\", \"-o\", strcmp(argv[1],\"build\") == 0 ?\n"
                "                    \"build/Release/ide\" : \"build/throughput-tools/throughput\", (char*)0);\n"
                "            _exit(127);\n"
                "        }\n"
                "        if (child > 0)\n"
                "        {\n"
                "            int status = 0;\n"
                "            pid_t waited;\n"
                "            do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);\n"
                "            result = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;\n"
                "        }\n"
                "    }\n"
                "    return result;\n"
                "}\n")) &&
            production_profile_write(path_join(arena, root, S8("fixture-dependency.h")), string_format(arena, S8("#define FIXTURE_BASELINE 1\n#define FIXTURE_CLANG \"{S8}\"\n"
                    "#define FIXTURE_LINKER \"{S8}\"\n#define FIXTURE_NINJA \"{S8}\"\n"
                    "#define FIXTURE_PYTHON \"{S8}\"\n#define FIXTURE_PROVIDER \"{S8}\"\n"), clang, linker, ninja, python, provider)) &&
            production_profile_write(path_join(arena, root, S8("tools/throughput/fixture.c")),
                S8("#include <stdio.h>\n#include <string.h>\n#include <unistd.h>\n#include \"../../fixture-dependency.h\"\n"
                    "#include \"../../src/generated/ignored.h\"\n#include \"../../build/generated/value.h\"\n"
                    "#if FIXTURE_BUILD != 1\n#error invalid generated input\n#endif\n"
                    "int main(int argc, char** argv)\n{\n"
                    "    int result = 0;\n"
                    "    if (argc > 1 && strcmp(argv[1], \"run\") == 0)\n"
                    "    {\n"
                    "        char* arguments[64];\n"
                    "        result = 127;\n"
                    "        if (argc < 60)\n"
                    "        {\n"
                    "            arguments[0] = FIXTURE_PYTHON; arguments[1] = \"-B\"; arguments[2] = FIXTURE_PROVIDER;\n"
                    "            for (int index = 1; index < argc; index += 1) arguments[index + 2] = argv[index];\n"
                    "            arguments[argc + 2] = (char*)0;\n"
                    "            execv(FIXTURE_PYTHON, arguments);\n"
                    "        }\n"
                    "    }\n"
                    "    else { puts(FIXTURE_MESSAGE); }\n"
                    "    return result;\n}\n")) &&
            file_copy((CopyFileArguments){.original_path = S8("tools/bootstrap_driver.sh"),
                .new_path = path_join(arena, root, S8("tools/bootstrap_driver.sh"))}) &&
            file_copy((CopyFileArguments){.original_path = S8("build.sh"), .new_path = script}) &&
            chmod((char*)script.pointer, 0755) == 0 &&
            production_profile_write(header, S8("#define FIXTURE_MESSAGE \"baseline corpus consumer\"\n")) &&
            production_profile_write(generated, S8("#define FIXTURE_BUILD 1\n")) &&
            production_profile_write(bootstrap, S8("baseline immutable driver\n")) &&
            production_profile_write(ide, S8("baseline compiler bytes\n")) && chmod((char*)ide.pointer, 0755) == 0 &&
            production_profile_write(path_join(arena, root, S8("empty.file")), S8("")) &&
            production_profile_write(path_join(arena, build, S8("CMakeCache.txt")),
                string_format(arena, S8("BUSTER_INCLUDE_TESTS:BOOL=OFF\nCMAKE_HOME_DIRECTORY:INTERNAL={S8}\n"
                    "CMAKE_C_COMPILER:FILEPATH={S8}\nCMAKE_LINKER:FILEPATH={S8}\nCMAKE_MAKE_PROGRAM:FILEPATH={S8}\n"),
                    root, clang, linker, ninja));

        String8 init[] = {S8("git"), S8("-c"), S8("gc.auto=0"), S8("-c"), S8("maintenance.auto=false"), S8("-c"), S8("core.hooksPath=/dev/null"), S8("-C"), root, S8("init"), S8("--quiet")};
        String8 add[] = {S8("git"), S8("-c"), S8("gc.auto=0"), S8("-c"), S8("maintenance.auto=false"), S8("-c"), S8("core.hooksPath=/dev/null"), S8("-C"), root, S8("add"), S8(".")};
        String8 commit[] = {S8("git"), S8("-c"), S8("gc.auto=0"), S8("-c"), S8("maintenance.auto=false"), S8("-c"), S8("core.hooksPath=/dev/null"), S8("-C"), root, S8("-c"), S8("user.name=Closure fixture"), S8("-c"),
            S8("user.email=closure@example.invalid"), S8("-c"), S8("gc.auto=0"), S8("-c"), S8("maintenance.auto=false"), S8("commit"), S8("--quiet"), S8("-m"), S8("baseline")};
        passed = passed && compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(init)).success &&
            compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(add)).success &&
            compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit)).success;
        ProductionProfileCommandResult revision = compiler_closure_git(arena, root, S8("HEAD"));
        ProductionProfileCommandResult tree = compiler_closure_git(arena, root, S8("HEAD^{tree}"));
        String8 base = production_profile_trim(revision.output);
        String8 base_tree = production_profile_trim(tree.output);

        passed = passed && revision.success && tree.success && compiler_closure_commit_valid(base) && compiler_closure_commit_valid(base_tree);
        // A second real Git tree exercises candidate checkout and removal of
        // candidate-only source without changing the tiny fixture compiler.
        passed = passed && production_profile_write(path_join(arena, root, S8("candidate-only.txt")), S8("candidate fixture source\n")) &&
            compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(add)).success &&
            compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit)).success;
        ProductionProfileCommandResult candidate_revision = compiler_closure_git(arena, root, S8("HEAD"));
        ProductionProfileCommandResult candidate_tree = compiler_closure_git(arena, root, S8("HEAD^{tree}"));
        String8 head = production_profile_trim(candidate_revision.output), head_tree = production_profile_trim(candidate_tree.output);
        passed = passed && candidate_revision.success && candidate_tree.success &&
            compiler_closure_commit_valid(head) && compiler_closure_commit_valid(head_tree) &&
            !string_equal(base, head) && !string_equal(base_tree, head_tree);
        result = (CompilerPreparationFixture){directory, root, base, base_tree, head, head_tree, passed};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_preparation_fixture_main(Arena* arena, String8 export_argument)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    String8 cpuinfo = compiler_preparation_fixture_host(arena);
    String8 export = os_path_absolute_lexical(arena, export_argument, true);
    String8 export_parent = path_parent(arena, export);
    String8 provider = os_path_absolute(arena, S8("tools/bench_direct/compiler_preparation_diagnostic_provider.py"), true);
    String8 python = os_path_absolute(arena, executable_resolve_in_path(arena, S8("python3")), true);
    String8 provider_sha256 = {0}, python_sha256 = {0};
    struct stat provider_status = {0}, python_status = {0};
    char const* case_bytes = getenv("BUSTER_PREPARATION_DIAGNOSTIC_CORPUS_CASE");
    String8 diagnostic_case = case_bytes ? (String8){(char8*)case_bytes, (u64)strlen(case_bytes)} : S8("regression");
    bool case_valid = string_equal(diagnostic_case, S8("regression")) || string_equal(diagnostic_case, S8("invalid")) ||
        string_equal(diagnostic_case, S8("missing")) || string_equal(diagnostic_case, S8("bad-exit")) ||
        string_equal(diagnostic_case, S8("partial-numeric")) || string_equal(diagnostic_case, S8("inconsistent-regression"));
    bool valid = case_valid && cpuinfo.length && export.length && export_parent.length &&
        string_equal(export_parent, os_path_absolute(arena, export_parent, true)) &&
        compiler_closure_path_safe(export) && production_profile_path_components_safe(export) &&
        !path_exists(arena, export) && provider.length && python.length &&
        compiler_closure_path_safe(provider) && compiler_closure_path_safe(python) &&
        compiler_closure_hash(arena, provider, &provider_sha256, &provider_status) &&
        compiler_closure_hash(arena, python, &python_sha256, &python_status) && (python_status.st_mode & 0111);
    bool signals = valid && compiler_closure_signals_begin();
    valid = valid && signals;
    CompilerPreparationFixture fixture = {0};
    if (valid) fixture = compiler_preparation_fixture_setup(arena, python, provider);
    valid = valid && fixture.valid;
    String8 output = valid ? path_join(arena, fixture.directory, S8("qualification")) : (String8){0};
    OsDirectoryCreateResult created = valid ? os_make_directory_exclusive(output) : (OsDirectoryCreateResult){0};
    valid = valid && created.created && !created.error.v;
    CompilerClosurePreparation legacy = {0}, snapshot = {0};
    u64 started = os_now_microseconds();
    if (valid)
    {
        u64 initialization_started = os_now_microseconds();
        bool legacy_pins = compiler_closure_preparation_initialize(&legacy, arena, fixture.root, path_join(arena, output, S8("legacy")),
            S8("legacy-rebuild"), fixture.base, fixture.base_tree, fixture.head, fixture.head_tree);
        legacy.cost_initialization_us = os_now_microseconds() - initialization_started;
        legacy.cost_initialization_complete = legacy_pins;
        initialization_started = os_now_microseconds();
        bool snapshot_pins = compiler_closure_preparation_initialize(&snapshot, arena, fixture.root, path_join(arena, output, S8("snapshot")),
            S8("snapshot-v1"), fixture.base, fixture.base_tree, fixture.head, fixture.head_tree);
        snapshot.cost_initialization_us = os_now_microseconds() - initialization_started;
        snapshot.cost_initialization_complete = snapshot_pins;
        legacy.started_us = started;
        snapshot.started_us = started;
        valid = legacy_pins && snapshot_pins;
        valid = valid && compiler_closure_qualification_execute(&legacy, &snapshot, provider, python);
        bool written = compiler_closure_qualification_write(arena, output, fixture.root,
            fixture.base, fixture.base_tree, fixture.head, fixture.head_tree, provider_sha256, python_sha256,
            &legacy, &snapshot, started, valid);
        valid = valid && written && legacy.stage == 35 && snapshot.stage == 42 &&
            !path_exists(arena, path_join(arena, fixture.root, S8("candidate-only.txt")));
    }
    if (signals) valid = compiler_closure_signals_end() && valid;
    if (output.length && path_exists(arena, output) && !compiler_closure_cleanup_failed)
    {
        OsDirectoryCreateResult export_created = os_make_directory_exclusive(export);
        bool exported = export_created.created && !export_created.error.v;
        u64 files = 0, bytes = 0;
        exported = exported && compiler_preparation_controller_copy_directory(arena, output, path_join(arena, export, S8("qualification")),
            0, &files, &bytes);
        String8 status = string_format(arena, S8("{{\"schema\":\"buster-compiler-preparation-fixture-v1\",\"diagnostic_fixture\":true,"
            "\"qualification_state\":\"unqualified\",\"qualification_status\":\"unqualified\",\"physical_qualification\":false,"
            "\"operation_state\":\"{S8}\",\"diagnostic_case\":\"{S8}\"}}\n"),
            valid ? S8("complete") : S8("failed"), diagnostic_case);
        String8 plan = string_format(arena, S8("{{\"schema\":\"buster-compiler-preparation-fixture-v1\","
            "\"diagnostic_fixture\":true,\"qualification_state\":\"unqualified\",\"diagnostic_case\":\"{S8}\",\"expected\":{{"
            "\"base\":\"{S8}\",\"base_tree\":\"{S8}\",\"head\":\"{S8}\",\"head_tree\":\"{S8}\","
            "\"root\":\"{S8}\",\"output\":\"{S8}\",\"trusted_lab\":\"{S8}\",\"python\":\"{S8}\","
            "\"trusted_lab_sha256\":\"{S8}\",\"python_sha256\":\"{S8}\"}}}}\n"),
            diagnostic_case, fixture.base, fixture.base_tree, fixture.head, fixture.head_tree, fixture.root, output,
            provider, python, provider_sha256, python_sha256);
        exported = exported && production_profile_write(path_join(arena, export, S8("fixture-plan.json")), plan) &&
            production_profile_write(path_join(arena, export, S8("fixture-status.json")), status) &&
            production_profile_write(path_join(arena, export, S8("fixture-cpuinfo.txt")), cpuinfo);
        valid = valid && exported;
    }
    // Failure keeps the checkout and any partial evidence. The data export
    // never needs its executables or saved source/build trees.
    bool cleaned = valid ? os_directory_delete(fixture.directory) : false;
    valid = valid && cleaned;
    string_print(S8("COMPILER_PREPARATION_NATIVE_FIXTURE state={S8} diagnostic_fixture=true qualification_state=unqualified "
        "actual_native_preparer=1 labs=5 corpora=5 export={S8} retained_source={S8}\n"),
        valid ? S8("complete") : S8("failed"), export, cleaned ? S8("-") : fixture.directory);
    if (valid) result = PROCESS_RESULT_SUCCESS;
    return result;
}
#endif
