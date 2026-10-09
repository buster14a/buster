// Private hosted ordinary bridge producer/reader fixture. DIAGNOSTIC-UNQUALIFIED.
// Tiny builds and synthetic data exercise containment and contracts; no timing or activation claims.
#if BUSTER_LINUX
#include <string.h>
BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_fixture_nonphysical_cpu(String8 text)
{
    bool result=text.pointer && text.length;
    String8 needle=S8("9700x");
    for (u64 i=0; result && i+needle.length<=text.length; i+=1)
    {
        bool matched=true;
        for (u64 j=0; matched && j<needle.length; j+=1)
        {
            u8 byte=text.pointer[i+j];
            if (byte>='A' && byte<='Z') byte=(u8)(byte-'A'+'a');
            matched=byte==needle.pointer[j];
        }
        if (matched) result=false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_source_fixture_host(Arena* arena)
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
    result = result && used && compiler_closure_utility_fixture_nonphysical_cpu(observed);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_source_fixture_literal(String8 value)
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

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_utility_source_fixture_initialize(Arena* arena,
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
    String8 provider = path_join(arena, trusted, S8("tools/bench_direct/compiler_closure_utility_diagnostic.py"));
    bool safe = compiler_closure_utility_source_fixture_host(arena) &&
        string_equal(root, requested_root) && string_equal(output, requested_output) &&
        compiler_closure_utility_source_fixture_literal(root) && compiler_closure_utility_source_fixture_literal(output) &&
        compiler_closure_utility_source_fixture_literal(clang) && compiler_closure_utility_source_fixture_literal(linker) &&
        compiler_closure_utility_source_fixture_literal(ninja) && compiler_closure_utility_source_fixture_literal(python) &&
        compiler_closure_utility_source_fixture_literal(provider) &&
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
            production_profile_write(path_join(arena, root, S8(".compiler-utility-fixture")), S8("BUSTER_COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_ONLY_V1\n")) &&
            production_profile_write(path_join(arena, root, S8("build.c")), S8("/* Private diagnostic driver: tiny real Clang build, no qualification. */\n#include \"fixture-dependency.h\"\n#include <errno.h>\n#include <stdio.h>\n#include <string.h>\n#include <sys/stat.h>\n#include <sys/types.h>\n#include <sys/wait.h>\n#include <unistd.h>\nstatic int directory(const char* path)\n{\n    return mkdir(path,0755)==0 || errno==EEXIST;\n}\nint main(int argc,char** argv)\n{\n    int result=1;\n    if (argc>=2 && strcmp(argv[1],\"generate\")==0)\n    {\n        char root[4096];\n        int ready=getcwd(root,sizeof(root)) && directory(\"build\") && directory(\"build/generated\") &&\n            directory(\"build/Release\") && directory(\"src\") && directory(\"src/generated\");\n        if (ready)\n        {\n            FILE* cache=fopen(\"build/CMakeCache.txt\",\"w\");\n            int printed=0,closed=1,generated=-1,ignored_closed=1,written=-1,build_closed=1;\n            if (cache)\n            {\n                printed=fprintf(cache,\"BUSTER_INCLUDE_TESTS:BOOL=OFF\\nCMAKE_HOME_DIRECTORY:INTERNAL=%s\\n\"\n                    \"CMAKE_C_COMPILER:FILEPATH=%s\\nCMAKE_LINKER:FILEPATH=%s\\nCMAKE_MAKE_PROGRAM:FILEPATH=%s\\n\",\n                    root,FIXTURE_CLANG,FIXTURE_LINKER,FIXTURE_NINJA);\n                closed=fclose(cache);\n            }\n            FILE* ignored=fopen(\"src/generated/ignored.h\",\"w\");\n            if (ignored)\n            {\n                generated=fprintf(ignored,\"#define FIXTURE_GENERATED %d\\n\",FIXTURE_VERSION);\n                ignored_closed=fclose(ignored);\n            }\n            FILE* build=fopen(\"build/generated/value.h\",\"w\");\n            if (build)\n            {\n                written=fputs(\"#define FIXTURE_BUILD 1\\n\",build);\n                build_closed=fclose(build);\n            }\n            result=printed>0 && !closed && generated>0 && !ignored_closed && written>=0 && !build_closed ? 0:1;\n        }\n    }\n    else if ((argc>=2 && strcmp(argv[1],\"build\")==0) ||\n        (argc>=3 && argc<=64 && strcmp(argv[1],\"bench_throughput\")==0 &&\n        ((argc==3 && strcmp(argv[2],\"help\")==0) || strcmp(argv[2],\"run\")==0)))\n    {\n        int compiler=strcmp(argv[1],\"build\")==0;\n        int ready=directory(\"build\") && directory(\"build/Release\") && directory(\"build/throughput-tools\");\n        pid_t child=ready ? fork():-1;\n        if (child==0)\n        {\n            execl(FIXTURE_CLANG,FIXTURE_CLANG,\"-std=c11\",\"-O2\",\"-Wall\",\"-Wextra\",\"-Werror\",\n                \"-fwrapv\",\"-fno-strict-aliasing\",\"-funsigned-char\",\n                compiler ? \"src/fixture-ide.c\":\"tools/throughput/fixture-corpus.c\",\n                \"-o\",compiler ? \"build/Release/ide\":\"build/throughput-tools/throughput\",(char*)0);\n            _exit(127);\n        }\n        if (child>0)\n        {\n            int status=0;\n            pid_t waited;\n            do { waited=waitpid(child,&status,0); } while (waited<0 && errno==EINTR);\n            result=waited==child && WIFEXITED(status) && WEXITSTATUS(status)==0 ? 0:1;\n        }\n        if (result==0 && !compiler && strcmp(argv[2],\"run\")==0)\n        {\n            /* Original legacy run options remain argv[2:] with the same NULL terminator. */\n            const char* harness=\"build/throughput-tools/throughput\";\n            argv[1]=(char*)harness;\n            result=1;\n            if (fputs(\"COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_LEGACY_CORPUS_RUN argv_forwarded=true\\n\",stdout)>=0 &&\n                fflush(stdout)==0) execv(harness,argv+1);\n        }\n    }\n    return result;\n}\n")) &&
            production_profile_write(path_join(arena, root, S8("src/fixture-ide.c")), S8("/* Private diagnostic executable, never a Buster performance compiler. */\n#include <stdio.h>\n#include \"../fixture-dependency.h\"\n#include \"generated/ignored.h\"\n#include \"../build/generated/value.h\"\n#if FIXTURE_GENERATED != FIXTURE_VERSION || FIXTURE_BUILD != 1\n#error incorrect generated diagnostic input\n#endif\nint main(void) { printf(\"DIAGNOSTIC-UNQUALIFIED ordinary fixture %d\\n\",FIXTURE_VERSION); return 0; }\n")) &&
            production_profile_write(path_join(arena, root, S8("tools/throughput/fixture-corpus.c")), S8("/* Private compiled diagnostic corpus adapter, never statistical measurements. */\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\n#include \"../../fixture-dependency.h\"\n#include \"../../src/generated/ignored.h\"\n#include \"../../build/generated/value.h\"\n#if FIXTURE_GENERATED != FIXTURE_VERSION || FIXTURE_BUILD != 1\n#error incorrect frozen diagnostic input\n#endif\nint main(int argc,char** argv)\n{\n    int result=1;\n    if (argc>1 && argc<=64 && strcmp(argv[1],\"run\")==0)\n    {\n        char** forwarded=calloc((size_t)argc+5,sizeof(char*));\n        if (forwarded)\n        {\n            forwarded[0]=FIXTURE_PYTHON;\n            forwarded[1]=\"-B\";\n            forwarded[2]=FIXTURE_PROVIDER;\n            forwarded[3]=\"--emit-corpus\";\n            for (int i=1;i<argc;i+=1) forwarded[i+3]=argv[i];\n            execv(FIXTURE_PYTHON,forwarded);\n            free(forwarded);\n        }\n    }\n    return result;\n}\n")) &&
            file_copy((CopyFileArguments){.original_path = path_join(arena, trusted, S8("tools/bootstrap_driver.sh")),
                .new_path = path_join(arena, root, S8("tools/bootstrap_driver.sh"))}) &&
            file_copy((CopyFileArguments){.original_path = path_join(arena, trusted, S8("build.sh")),
                .new_path = path_join(arena, root, S8("build.sh"))});
        String8 script = string_duplicate_arena(arena, path_join(arena, root, S8("build.sh")), true);
        passed = passed && chmod((char*)script.pointer, 0755) == 0;
    }
    String8 escape_source=path_join(arena,root,S8("tools/throughput/fixture-lab125.c"));
    String8 escape_driver=path_join(arena,output,S8("lab125"));
    if (passed)
    {
        passed=file_copy((CopyFileArguments){.original_path=path_join(arena,trusted,
            S8("tools/tests/compiler_closure_utility_lab125_fixture.c")),.new_path=escape_source});
        String8 compile[]={clang,S8("-std=c11"),S8("-O2"),S8("-Wall"),S8("-Wextra"),S8("-Werror"),
            S8("-fwrapv"),S8("-fno-strict-aliasing"),S8("-funsigned-char"),escape_source,S8("-o"),escape_driver};
        passed=passed && compiler_closure_capture(arena,(SliceString8)BUSTER_ARRAY_TO_SLICE(compile)).success;
        String8 digest={0}; struct stat status={0};
        passed=passed && compiler_sampling_controller_hash(arena,escape_driver,&digest,&status) &&
            (status.st_mode & 0111) && status.st_size>0 && status.st_size<=(16<<20);
        String8 descriptor=passed ? string_format(arena,
            S8("{{\"schema\":\"buster-compiler-utility-lab125-adapter-v1\",\"diagnostic_fixture\":true,"
               "\"qualification_state\":\"unqualified\",\"path\":\"{S8}\",\"sha256\":\"{S8}\","
               "\"bytes\":{u64}}}\n"),escape_driver,digest,(u64)status.st_size) : (String8){0};
        passed=passed && file_write(path_join(arena,root,S8(".compiler-utility-lab125.json")),BUSTER_SLICE_TO_BYTE_SLICE(descriptor));
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
        String8 plan = string_format(arena, S8("{{\"schema\":\"buster-compiler-utility-diagnostic-source-v1\","
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

#if BUSTER_LINUX && !BUSTER_ANDROID

// This private reader records the actual hosted CPU, with no approved-host
// substitution. The unchanged public parser remains exact-9700X and 64 KiB.
BUSTER_GLOBAL_LOCAL CompilerSamplingControllerHost compiler_closure_utility_fixture_cpu_parse(String8 text)
{
    CompilerSamplingControllerHost result = {0};
    bool valid = text.pointer && text.length && text.length <= (1ull << 20);
    u64 processors[4096] = {0}, processor = 0;
    bool have_processor = false, have_model = false, have_record = false;
    for (u64 begin = 0; valid && begin <= text.length;)
    {
        u64 end = begin;
        while (valid && end < text.length && text.pointer[end] != '\n')
        {
            u8 byte = text.pointer[end];
            valid = byte == '\t' || (byte >= 32 && byte <= 126);
            end += 1;
        }
        String8 line = string_slice(text, begin, end);
        if (!line.length || begin == text.length)
        {
            if (have_record)
            {
                valid = valid && have_processor && have_model && result.records < BUSTER_ARRAY_LENGTH(processors);
                for (u64 i = 0; valid && i < result.records; i += 1) valid = processors[i] != processor;
                if (valid) processors[result.records++] = processor;
            }
            have_processor = false; have_model = false; have_record = false;
        }
        else if (valid)
        {
            have_record = true;
            u64 colon = 0;
            while (colon < line.length && line.pointer[colon] != ':') colon += 1;
            valid = colon < line.length;
            if (valid)
            {
                String8 key = production_profile_trim(string_slice(line, 0, colon));
                String8 value = production_profile_trim(string_slice(line, colon + 1, line.length));
                if (string_equal(key, S8("processor")))
                {
                    valid = !have_processor && compiler_sampling_admission_decimal(value, &processor) && processor <= 65535;
                    have_processor = true;
                }
                else if (string_equal(key, S8("model name")))
                {
                    valid = !have_model && value.length && value.length <= 128;
                    for (u64 i = 0; valid && i < value.length; i += 1)
                    {
                        valid = value.pointer[i] >= 32 && value.pointer[i] <= 126 && value.pointer[i] != '"' && value.pointer[i] != '\\';
                    }
                    valid = valid && compiler_closure_utility_fixture_nonphysical_cpu(value) &&
                        (!result.model.length || string_equal(result.model, value));
                    if (valid) result.model = value;
                    have_model = true;
                }
            }
        }
        if (end == text.length)
        {
            if (have_record)
            {
                valid = valid && have_processor && have_model && result.records < BUSTER_ARRAY_LENGTH(processors);
                for (u64 i = 0; valid && i < result.records; i += 1) valid = processors[i] != processor;
                if (valid) processors[result.records++] = processor;
            }
            begin = text.length + 1;
        }
        else begin = end + 1;
    }
    // Actual observations are diagnostic facts only. They never admit a host.
    if (!valid || !result.records || !result.model.length) result=(CompilerSamplingControllerHost){0};
    result.valid=false;
    return result;
}



BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_fixture_cpu_self_test(void)
{
    // Parser controls are never host observations or qualification facts.
    String8 model=S8("Hosted diagnostic parser control");
    CompilerSamplingControllerHost positive=compiler_closure_utility_fixture_cpu_parse(
        S8("processor: 0\nmodel name: Hosted diagnostic parser control\n\n"
           "processor: 1\nmodel name: Hosted diagnostic parser control\n\n"));
    bool result=positive.records==2 && string_equal(positive.model,model) && !positive.valid;
    String8 refused[]={
        S8("processor: 0\nmodel name: Hosted control\n\nprocessor: 0\nmodel name: Hosted control\n\n"),
        S8("processor: 0\nmodel name: Hosted alpha\n\nprocessor: 1\nmodel name: Hosted beta\n\n"),
        S8("processor: 0\n\n"),
        S8("processor: 0\nmodel name: Hosted \"unsafe\" control\n\n"),
        S8("processor: 0\nmodel name: Hosted \\unsafe control\n\n"),
        S8("processor: 0\nmodel name: AMD Ryzen 7 9700X 8-Core Processor\n\n"),
        S8("processor: 0\nmodel name: amd ryzen 7 9700x 8-core processor\n\n"),
        S8("processor: 0\nmodel name: aMd rYzEn 7 9700x 8-cOrE pRoCeSsOr\n\n"),
    };
    for (u64 i=0; result && i<BUSTER_ARRAY_LENGTH(refused); i+=1)
    {
        CompilerSamplingControllerHost host=compiler_closure_utility_fixture_cpu_parse(refused[i]);
        result=!host.records && !host.model.length && !host.valid;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingControllerHost compiler_closure_utility_fixture_observed_host(Arena* arena)
{
    CompilerSamplingControllerHost result={0};
    bool valid=compiler_closure_utility_source_fixture_host(arena);
    u64 limit=1ull << 20, used=0;
    char8* bytes=arena_allocate(arena,char8,limit+1);
    int descriptor=valid ? open("/proc/cpuinfo",O_RDONLY|O_CLOEXEC|O_NOFOLLOW) : -1;
    valid=valid && descriptor>=0;
    bool eof=false;
    while (valid && !eof)
    {
        ssize_t count=read(descriptor,bytes+used,(size_t)(limit+1-used));
        if (count>0) { used+=(u64)count; valid=used<=limit; }
        else if (count==0) eof=true;
        else valid=errno==EINTR;
    }
    if (descriptor>=0) valid=close(descriptor)==0 && valid;
    if (valid && eof) result=compiler_closure_utility_fixture_cpu_parse((String8){bytes,used});
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_fixture_lab_case(String8* selection)
{
    bool present=false;
    String8 value=compiler_sampling_controller_environment(S8("BUSTER_UTILITY_DIAGNOSTIC_LAB_CASE"),&present);
    bool result=!present || string_equal(value,S8("legacy")) || string_equal(value,S8("snapshot"));
    if (result) *selection=present ? value : S8("none");
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_fixture_context(Arena* arena, String8 master,
    CompilerClosureUtilityControllerResolved* result)
{
    CompilerClosureUtilityControllerResolved resolved = {0};
    String8 trusted=os_path_absolute(arena,S8("."),true);
    resolved.diagnostic=true;
    resolved.options.trusted_root=trusted;
    resolved.options.evidence=path_join(arena,master,S8("evidence"));
    resolved.options.cleanup_root=master;
    resolved.workspace=trusted;
    resolved.driver=program_state->input.arguments.length ?
        os_path_absolute(arena,program_state->input.arguments.pointer[0],true) : (String8){0};
    resolved.lab=path_join(arena,trusted,S8("tools/bench_direct/compiler_closure_utility_diagnostic.py"));
    resolved.comparator=resolved.lab; // Private fixed entry to production ordinary main.
    resolved.receipt_adapter=path_join(arena,trusted,S8("tools/bench_direct/compiler_receipt.py"));
    resolved.owned_phase=path_join(arena,trusted,S8("tools/bench_direct/compiler_owned_phase.py"));
    resolved.protocol=path_join(arena,trusted,S8("docs/compiler-closure-utility-v1.md"));
    resolved.python=os_path_absolute(arena,executable_resolve_in_path(arena,S8("python3")),true);
    resolved.claim=path_join(arena,master,S8("output.claim"));
    resolved.pull=S8("1");
    resolved.host=compiler_closure_utility_fixture_observed_host(arena);
    CompilerClosureUtilityPlan* plan=&resolved.admitted.plan;
    plan->source_root=path_join(arena,master,S8("source"));
    plan->output_root=path_join(arena,master,S8("output"));
    plan->trusted_root=trusted;
    plan->python_path=resolved.python;
    plan->trusted_revision=production_profile_trim(compiler_sampling_controller_read(arena,path_join(arena,trusted,S8(".git/HEAD")),64));
    resolved.admitted.trusted_revision=plan->trusted_revision;
    resolved.admitted.policy_trusted_revision=plan->trusted_revision;
    resolved.admitted.freeze_revision=plan->trusted_revision;
    String8 paths[]={resolved.lab,resolved.python,resolved.driver,resolved.protocol,resolved.comparator,resolved.receipt_adapter,resolved.owned_phase};
    String8* hashes[]={&plan->lab_sha256,&plan->python_sha256,&plan->native_driver_sha256,&plan->protocol_sha256,
        &plan->comparator_sha256,&plan->receipt_sha256,&plan->owned_phase_sha256};
    String8 lab_case={0};
    bool valid=compiler_closure_utility_fixture_lab_case(&lab_case) &&
        compiler_closure_utility_fixture_allowed(arena) && resolved.host.records && resolved.host.model.length &&
        !resolved.host.valid && compiler_closure_utility_fixture_cpu_self_test() && trusted.length && resolved.driver.length &&
        compiler_sampling_hex(plan->trusted_revision,40) && compiler_sampling_acquisition_path(master) &&
        string_equal(path_parent(arena,master),os_path_absolute(arena,path_parent(arena,master),true)) &&
        !compiler_sampling_path_overlap(master,trusted);
    for (u64 i=0;valid && i<BUSTER_ARRAY_LENGTH(paths);i+=1)
    {
        struct stat status={0};
        valid=string_equal(os_path_absolute(arena,paths[i],true),paths[i]) &&
            compiler_sampling_controller_hash(arena,paths[i],hashes[i],&status);
    }
    resolved.admitted.protocol_sha256=plan->protocol_sha256;
    plan->valid=valid;
    valid=valid && compiler_closure_utility_controller_tools(arena,&resolved,true);
    resolved.claim_record=string_format(arena,S8("schema\tbuster-compiler-closure-utility-diagnostic-claim-v1\n"
        "diagnostic_fixture\ttrue\nqualification_state\tunqualified\nphysical_qualification\tfalse\n"
        "source_root\t{S8}\noutput_root\t{S8}\nevidence\t{S8}\ntrusted_root\t{S8}\n"
        "trusted_revision\t{S8}\nnative_driver_sha256\t{S8}\nbootstrap_marker_sha256\t{S8}\n"
        "clock_scope\tnative-diagnostic-only\nphysical_job_cost\tunavailable\n"),
        plan->source_root,plan->output_root,resolved.options.evidence,trusted,plan->trusted_revision,
        plan->native_driver_sha256,resolved.bootstrap_marker_sha256);
    resolved.admitted.freeze_sha256=stage_object_sha256_bytes(arena,(u8*)resolved.claim_record.pointer,resolved.claim_record.length);
    resolved.transport.bytes[3]=S8("request_run_id\t1\nexecutor_run_id\t1\nrequest_head\tdiagnostic-unqualified\n");
    resolved.valid=valid;
    if (valid) *result=resolved;
    return valid;
}
#endif

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_utility_fixture_execute(Arena* arena, SliceString8 arguments)
{
    ProcessResult result=PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && !BUSTER_ANDROID
    u64 started=os_now_microseconds();
    bool worker=arguments.length==3 && string_equal(arguments.pointer[2],S8("--owned-utility-fixture-worker"));
    bool valid=(arguments.length==2 || worker) && string_equal(arguments.pointer[0],S8("--self-test-utility-export")) &&
        compiler_closure_utility_fixture_allowed(arena);
    String8 master=valid ? os_path_absolute_lexical(arena,arguments.pointer[1],true) : (String8){0};
    CompilerClosureUtilityControllerResolved resolved={0};
    valid=valid && string_equal(master,arguments.pointer[1]) &&
        compiler_closure_utility_fixture_context(arena,master,&resolved);
    if (worker)
    {
        valid=valid && getpgrp()==getpid() && getpgrp()!=getsid(0) &&
            string_equal(os_path_absolute(arena,master,true),master) &&
            string_equal(compiler_sampling_controller_read(arena,path_join(arena,resolved.claim,S8("claim.tsv")),16384),resolved.claim_record) &&
            string_equal(compiler_sampling_controller_read(arena,path_join(arena,resolved.options.evidence,S8("claim.tsv")),16384),resolved.claim_record);
        bool claimed=valid && compiler_closure_utility_controller_claim_worker(arena,resolved);
        if (claimed)
        {
            string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_HOST logical_processor_records={u64} "
                "host_admitted=0 parser_controls=9 physical_qualification=false\n"),resolved.host.records);
            String8 init=path_join(arena,master,S8("initialize"));
            // Exclusive private worker claim precedes constructor's real Git
            // children. Constructor uses native containment, not a Python build.
            bool initialized=compiler_closure_utility_source_fixture_initialize(arena,resolved.admitted.plan.source_root,init)==PROCESS_RESULT_SUCCESS;
            ProductionProfileCommandResult head=initialized ? compiler_closure_git(arena,resolved.admitted.plan.source_root,S8("HEAD")) : (ProductionProfileCommandResult){0};
            ProductionProfileCommandResult base=initialized ? compiler_closure_git(arena,resolved.admitted.plan.source_root,S8("HEAD^1")) : (ProductionProfileCommandResult){0};
            ProductionProfileCommandResult head_tree=initialized ? compiler_closure_git(arena,resolved.admitted.plan.source_root,S8("HEAD^{tree}")) : (ProductionProfileCommandResult){0};
            ProductionProfileCommandResult base_tree=initialized ? compiler_closure_git(arena,resolved.admitted.plan.source_root,S8("HEAD^1^{tree}")) : (ProductionProfileCommandResult){0};
            CompilerClosureUtilityPlan* plan=&resolved.admitted.plan;
            plan->baseline_revision=production_profile_trim(base.output);
            plan->baseline_tree=production_profile_trim(base_tree.output);
            plan->candidate_revision=production_profile_trim(head.output);
            plan->candidate_tree=production_profile_trim(head_tree.output);
            plan->pull_head=plan->candidate_revision;
            bool pins=initialized && head.success && base.success && head_tree.success && base_tree.success &&
                compiler_sampling_hex(plan->baseline_revision,40) && compiler_sampling_hex(plan->baseline_tree,40) &&
                compiler_sampling_hex(plan->candidate_revision,40) && compiler_sampling_hex(plan->candidate_tree,40);
            String8 diagnostic_lab_case={0};
            pins=pins && compiler_closure_utility_fixture_lab_case(&diagnostic_lab_case);
            String8 fixture=string_format(arena,S8("{{\"schema\":\"buster-compiler-closure-utility-fixture-v1\","
                "\"diagnostic_fixture\":true,\"qualification_state\":\"unqualified\",\"physical_qualification\":false,"
                "\"diagnostic_lab_case\":\"{S8}\","
                "\"expected\":{{\"base\":\"{S8}\",\"base_tree\":\"{S8}\",\"head\":\"{S8}\",\"head_tree\":\"{S8}\","
                "\"pull_head\":\"{S8}\",\"trusted_revision\":\"{S8}\",\"root\":\"{S8}\",\"output\":\"{S8}\","
                "\"trusted_root\":\"{S8}\",\"trusted_lab\":\"{S8}\",\"python\":\"{S8}\","
                "\"native_driver\":\"{S8}\",\"native_driver_sha256\":\"{S8}\",\"bootstrap_marker_sha256\":\"{S8}\"}}}}\n"),
                diagnostic_lab_case,plan->baseline_revision,plan->baseline_tree,plan->candidate_revision,plan->candidate_tree,plan->pull_head,
                plan->trusted_revision,plan->source_root,plan->output_root,plan->trusted_root,resolved.lab,resolved.python,
                resolved.driver,plan->native_driver_sha256,resolved.bootstrap_marker_sha256);
            bool marked=pins && file_write(path_join(arena,resolved.options.evidence,S8("fixture-plan.json")),BUSTER_SLICE_TO_BYTE_SLICE(fixture));
            if (marked)
            {
                result=compiler_closure_utility_controller_worker_claimed(arena,resolved,true);
                if (result==PROCESS_RESULT_SUCCESS)
                {
                    String8 marker=S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_LEGACY_CORPUS_RUN argv_forwarded=true\n");
                    String8 legacy_log=compiler_sampling_controller_read(arena,
                        path_join(arena,resolved.options.evidence,S8("utility/legacy/ordinary/throughput.log")),8192);
                    String8 snapshot_log=compiler_sampling_controller_read(arena,
                        path_join(arena,resolved.options.evidence,S8("utility/snapshot/ordinary/throughput.log")),8192);
                    bool legacy_run=production_profile_contains(legacy_log,marker);
                    bool frozen_snapshot=!production_profile_contains(snapshot_log,marker);
                    string_print(S8("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_DISPATCH legacy_run_forwarded={u64} "
                        "snapshot_frozen_harness={u64} physical_qualification=false\n"),
                        (u64)legacy_run,(u64)frozen_snapshot);
                    result=legacy_run && frozen_snapshot ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
                }
            }
        }
    }
    else if (valid && generate_path_kind(arena,master)==GENERATE_PATH_MISSING)
    {
        OsDirectoryCreateResult made=os_make_directory_exclusive(master);
        if (made.created && !made.error.v)
            result=compiler_closure_utility_controller_owned(arena,resolved,arguments,started);
    }
    string_print(S8("COMPILER_CLOSURE_UTILITY_EXPORT_DIAGNOSTIC success={u64} physical_qualification=false\n"),
        result==PROCESS_RESULT_SUCCESS ? 1ull : 0ull);
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(arguments);
#endif
    return result;
}
