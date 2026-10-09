// Private hosted ordinary bridge producer/reader fixture. DIAGNOSTIC-UNQUALIFIED.
// Tiny builds and synthetic data exercise containment and contracts; no timing or activation claims.
#if BUSTER_LINUX
#include <string.h>
BUSTER_GLOBAL_LOCAL bool compiler_closure_ordinary_fixture_host(Arena* arena)
{
    extern char** environ;
    bool result = compiler_closure_admitting();
    for (char** entry = environ; result && entry && *entry; entry += 1)
    {
        const char* value = *entry;
        if (value[0] == 'B' && value[1] == 'Q' && value[2] == '_')
        {
            // Hosted containment tests alone use this documented diagnostic flag.
            result = strcmp(value, "BQ_REQUIRE_DISTINCT_GROUP=1") == 0;
        }
    }
    u64 limit = 1ull << 20;
    char8* bytes = arena_allocate(arena, char8, limit + 1);
    u64 used = 0;
    int descriptor = result ? open("/proc/cpuinfo", O_RDONLY | O_NOFOLLOW) : -1;
    result = result && descriptor >= 0;
    bool eof = false;
    while (result && !eof && used < limit)
    {
        ssize_t count = read(descriptor, bytes + used, (size_t)(limit - used));
        if (count > 0) { used += (u64)count; }
        else if (count == 0) { eof = true; }
        else if (errno != EINTR) { result = false; }
    }
    if (result && !eof)
    {
        char extra = 0;
        result = read(descriptor, &extra, 1) == 0;
    }
    if (descriptor >= 0 && close(descriptor) != 0) { result = false; }
    String8 observed = {.pointer = bytes, .length = used};
    result = result && used && !production_profile_contains(observed, S8("9700X")) &&
        !production_profile_contains(observed, S8("9700x"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_ordinary_fixture_literal(String8 value)
{
    // These fixed fixture strings enter C and JSON literals. Fail closed for other spellings.
    bool result = value.length > 1 && value.length <= 4096 && value.pointer[0] == '/';
    for (u64 i = 0; result && i < value.length; i += 1)
    {
        char8 byte = value.pointer[i];
        result = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '/' || byte == '_' || byte == '-' || byte == '.' || byte == ' ';
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_ordinary_fixture_initialize(Arena* arena,
    String8 requested_root, String8 requested_output)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    String8 root = os_path_absolute_lexical(arena, requested_root, true);
    String8 output = os_path_absolute_lexical(arena, requested_output, true);
    String8 trusted = os_path_absolute(arena, S8("."), true);
    String8 root_parent = path_parent(arena, root);
    String8 output_parent = path_parent(arena, output);
    String8 clang = os_path_absolute(arena, executable_resolve_in_path(arena, S8("clang")), true);
    String8 linker = os_path_absolute(arena, executable_resolve_in_path(arena, S8("ld")), true);
    String8 ninja = os_path_absolute(arena, executable_resolve_in_path(arena, S8("ninja")), true);
    String8 python = os_path_absolute(arena, executable_resolve_in_path(arena, S8("python3")), true);
    String8 provider = path_join(arena, trusted, S8("tools/bench_direct/compiler_ordinary_fixture_test.py"));
    bool safe = compiler_closure_ordinary_fixture_host(arena) &&
        string_equal(root, requested_root) && string_equal(output, requested_output) &&
        compiler_closure_ordinary_fixture_literal(root) && compiler_closure_ordinary_fixture_literal(output) &&
        compiler_closure_ordinary_fixture_literal(clang) && compiler_closure_ordinary_fixture_literal(linker) &&
        compiler_closure_ordinary_fixture_literal(ninja) && compiler_closure_ordinary_fixture_literal(python) &&
        compiler_closure_ordinary_fixture_literal(provider) &&
        production_profile_path_components_safe(root) && production_profile_path_components_safe(output) &&
        string_equal(root_parent, os_path_absolute(arena, root_parent, true)) &&
        string_equal(output_parent, os_path_absolute(arena, output_parent, true)) &&
        !string_equal(root, output) && !production_profile_path_is_child(root, output) &&
        !production_profile_path_is_child(output, root) &&
        !string_equal(root, trusted) && !production_profile_path_is_child(root, trusted) &&
        !production_profile_path_is_child(trusted, root) &&
        !string_equal(output, trusted) && !production_profile_path_is_child(output, trusted) &&
        !production_profile_path_is_child(trusted, output) &&
        !path_exists(arena, root) && !path_exists(arena, output) && path_exists(arena, provider);
    bool passed = safe && os_make_directory_exclusive(root).created && os_make_directory_exclusive(output).created;
    if (passed)
    {
        make_directory_recursive(arena, path_join(arena, root, S8("src")));
        make_directory_recursive(arena, path_join(arena, root, S8("tools/throughput")));
        passed = production_profile_write(path_join(arena, output, S8("DIAGNOSTIC-UNQUALIFIED")),
            S8("Private hosted ordinary bridge fixture. Synthetic lab/corpus data. No performance qualification, physical acquisition or activation.\n")) &&
            production_profile_write(path_join(arena, root, S8("DIAGNOSTIC-UNQUALIFIED")),
            S8("Private tiny Clang/Git/bootstrap fixture; never performance evidence.\n")) &&
            production_profile_write(path_join(arena, root, S8(".gitignore")), S8("build/\n.cache/\nsrc/generated/\n")) &&
            production_profile_write(path_join(arena, root, S8("build.c")), S8("/* Private diagnostic driver: tiny real Clang build, no performance qualification. */\n#include \"fixture-dependency.h\"\n#include <errno.h>\n#include <stdio.h>\n#include <string.h>\n#include <sys/stat.h>\n#include <sys/types.h>\n#include <sys/wait.h>\n#include <unistd.h>\nstatic int directory(const char* path) { return mkdir(path,0755)==0 || errno==EEXIST; }\nint main(int argc,char** argv)\n{\n    int result=1;\n    if (argc>=2 && strcmp(argv[1],\"generate\")==0)\n    {\n        char root[4096];\n        if (!getcwd(root,sizeof(root)) || !directory(\"build\") || !directory(\"build/generated\") ||\n            !directory(\"build/Release\") || !directory(\"src\") || !directory(\"src/generated\")) return 1;\n        FILE* cache=fopen(\"build/CMakeCache.txt\",\"w\");\n        if (!cache) return 1;\n        int printed=fprintf(cache,\"BUSTER_INCLUDE_TESTS:BOOL=OFF\\nCMAKE_HOME_DIRECTORY:INTERNAL=%s\\n\"\n            \"CMAKE_C_COMPILER:FILEPATH=%s\\nCMAKE_LINKER:FILEPATH=%s\\nCMAKE_MAKE_PROGRAM:FILEPATH=%s\\n\",\n            root,FIXTURE_CLANG,FIXTURE_LINKER,FIXTURE_NINJA);\n        int closed=fclose(cache);\n        FILE* ignored=fopen(\"src/generated/ignored.h\",\"w\");\n        if (!ignored) return 1;\n        int generated=fprintf(ignored,\"#define FIXTURE_GENERATED %d\\n\",FIXTURE_VERSION);\n        int ignored_closed=fclose(ignored);\n        FILE* build=fopen(\"build/generated/value.h\",\"w\");\n        if (!build) return 1;\n        int written=fputs(\"#define FIXTURE_BUILD 1\\n\",build);\n        int build_closed=fclose(build);\n        result=printed>0 && !closed && generated>0 && !ignored_closed && written>=0 && !build_closed ? 0:1;\n    }\n    else if ((argc>=2 && strcmp(argv[1],\"build\")==0) ||\n        (argc==3 && strcmp(argv[1],\"bench_throughput\")==0 && strcmp(argv[2],\"help\")==0))\n    {\n        int compiler=strcmp(argv[1],\"build\")==0;\n        int ready=directory(\"build\") && directory(\"build/Release\") && directory(\"build/throughput-tools\");\n        pid_t child=ready ? fork():-1;\n        if (child==0)\n        {\n            execl(FIXTURE_CLANG,FIXTURE_CLANG,\"-std=c11\",\"-O2\",\"-Wall\",\"-Wextra\",\"-Werror\",\n                \"-fwrapv\",\"-fno-strict-aliasing\",\"-funsigned-char\",\n                compiler ? \"src/fixture-ide.c\":\"tools/throughput/fixture-corpus.c\",\n                \"-o\",compiler ? \"build/Release/ide\":\"build/throughput-tools/throughput\",(char*)0);\n            _exit(127);\n        }\n        if (child>0)\n        {\n            int status=0;\n            pid_t waited;\n            do { waited=waitpid(child,&status,0); } while (waited<0 && errno==EINTR);\n            result=waited==child && WIFEXITED(status) && WEXITSTATUS(status)==0 ? 0:1;\n        }\n    }\n    return result;\n}\n")) &&
            production_profile_write(path_join(arena, root, S8("src/fixture-ide.c")), S8("/* Private diagnostic executable, never a Buster performance compiler. */\n#include <stdio.h>\n#include \"../fixture-dependency.h\"\n#include \"generated/ignored.h\"\n#include \"../build/generated/value.h\"\n#if FIXTURE_GENERATED != FIXTURE_VERSION || FIXTURE_BUILD != 1\n#error incorrect generated diagnostic input\n#endif\nint main(void) { printf(\"DIAGNOSTIC-UNQUALIFIED ordinary fixture %d\\n\",FIXTURE_VERSION); return 0; }\n")) &&
            production_profile_write(path_join(arena, root, S8("tools/throughput/fixture-corpus.c")), S8("/* Private compiled diagnostic corpus adapter, never statistical measurements. */\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\n#include \"../../fixture-dependency.h\"\n#include \"../../src/generated/ignored.h\"\n#include \"../../build/generated/value.h\"\n#if FIXTURE_GENERATED != FIXTURE_VERSION || FIXTURE_BUILD != 1\n#error incorrect frozen diagnostic input\n#endif\nint main(int argc,char** argv)\n{\n    int result=1;\n    if (argc>1 && argc<=64 && strcmp(argv[1],\"run\")==0)\n    {\n        char** forwarded=calloc((size_t)argc+5,sizeof(char*));\n        if (forwarded)\n        {\n            forwarded[0]=FIXTURE_PYTHON;\n            forwarded[1]=\"-B\";\n            forwarded[2]=FIXTURE_PROVIDER;\n            forwarded[3]=\"--emit-corpus\";\n            for (int i=1;i<argc;i+=1) forwarded[i+3]=argv[i];\n            execv(FIXTURE_PYTHON,forwarded);\n            free(forwarded);\n        }\n    }\n    return result;\n}\n")) &&
            file_copy((CopyFileArguments){.original_path = path_join(arena, trusted, S8("tools/bootstrap_driver.sh")),
                .new_path = path_join(arena, root, S8("tools/bootstrap_driver.sh"))}) &&
            file_copy((CopyFileArguments){.original_path = path_join(arena, trusted, S8("build.sh")),
                .new_path = path_join(arena, root, S8("build.sh"))});
        String8 script = string_duplicate_arena(arena, path_join(arena, root, S8("build.sh")), true);
        passed = passed && chmod((char*)script.pointer, 0755) == 0;
    }
    String8 base = {0}, base_tree = {0}, head = {0}, head_tree = {0};
    for (u64 version = 1; passed && version <= 2; version += 1)
    {
        String8 header = string_format(arena, S8("#define FIXTURE_VERSION {u64}\n#define FIXTURE_CLANG \"{S8}\"\n"
            "#define FIXTURE_LINKER \"{S8}\"\n#define FIXTURE_NINJA \"{S8}\"\n#define FIXTURE_PYTHON \"{S8}\"\n"
            "#define FIXTURE_PROVIDER \"{S8}\"\n"), version, clang, linker, ninja, python, provider);
        passed = production_profile_write(path_join(arena, root, S8("fixture-dependency.h")), header);
        if (version == 1)
        {
            String8 init[] = {S8("git"), S8("-c"), S8("gc.auto=0"), S8("-c"), S8("maintenance.auto=false"),
                S8("-c"), S8("core.hooksPath=/dev/null"), S8("-C"), root, S8("init"), S8("--quiet")};
            passed = passed && compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(init)).success;
        }
        String8 add[] = {S8("git"), S8("-c"), S8("gc.auto=0"), S8("-c"), S8("maintenance.auto=false"),
            S8("-c"), S8("core.hooksPath=/dev/null"), S8("-C"), root, S8("add"), S8(".")};
        String8 commit[] = {S8("git"), S8("-c"), S8("gc.auto=0"), S8("-c"), S8("maintenance.auto=false"),
            S8("-c"), S8("core.hooksPath=/dev/null"), S8("-C"), root, S8("-c"), S8("user.name=Private diagnostic fixture"),
            S8("-c"), S8("user.email=ordinary-fixture@example.invalid"), S8("commit"), S8("--quiet"), S8("-m"),
            version == 1 ? S8("DIAGNOSTIC-UNQUALIFIED baseline") : S8("DIAGNOSTIC-UNQUALIFIED candidate")};
        passed = passed && compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(add)).success &&
            compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit)).success;
        ProductionProfileCommandResult revision = compiler_closure_git(arena, root, S8("HEAD"));
        ProductionProfileCommandResult tree = compiler_closure_git(arena, root, S8("HEAD^{tree}"));
        passed = passed && revision.success && tree.success;
        if (version == 1) { base = production_profile_trim(revision.output); base_tree = production_profile_trim(tree.output); }
        else { head = production_profile_trim(revision.output); head_tree = production_profile_trim(tree.output); }
    }
    if (passed)
    {
        passed = base.length == 40 && base_tree.length == 40 && head.length == 40 && head_tree.length == 40 &&
            !string_equal(base, head) && !string_equal(base_tree, head_tree);
        String8 plan = string_format(arena, S8("{{\"schema\":\"buster-compiler-ordinary-diagnostic-fixture-v1\","
            "\"diagnostic_only\":true,\"performance_qualified\":false,\"activation_allowed\":false,\"state\":\"complete\","
            "\"root\":\"{S8}\",\"output\":\"{S8}\",\"base\":\"{S8}\",\"base_tree\":\"{S8}\","
            "\"head\":\"{S8}\",\"head_tree\":\"{S8}\",\"provider\":\"{S8}\"}\n"),
            root, output, base, base_tree, head, head_tree, provider);
        passed = passed && file_publish(path_join(arena, output, S8("fixture-plan.json")), BUSTER_SLICE_TO_BYTE_SLICE(plan));
    }
    if (passed) { result = PROCESS_RESULT_SUCCESS; }
    string_print(S8("COMPILER_ORDINARY_DIAGNOSTIC_FIXTURE status={S8} qualified=0 activation=0\n"),
        passed ? S8("pass") : S8("refused-or-failed"));
    return result;
}
#endif
