// Native producer/consumer controls for the attempt-local closure.
#if BUSTER_LINUX
BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_self_test(Arena* arena, String8 export)
{
    String8 directory = string_format(arena, S8("build/compiler-closure-self-test-{u64}"), os_now_microseconds());
    make_directory_recursive(arena, directory);
    directory = os_path_absolute(arena, directory, true);
    String8 root = path_join(arena, directory, S8("checkout with spaces"));
    String8 snapshot = path_join(arena, directory, S8("snapshot"));
    String8 report = path_join(arena, directory, S8("native.json"));
    String8 snapshot_report = path_join(arena, directory, S8("snapshot.json"));
    String8 restore_report = path_join(arena, directory, S8("restore.json"));
    String8 verify_report = path_join(arena, directory, S8("verify.json"));
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
    String8 harness = path_join(arena, build, S8("throughput-tools/throughput"));
    bool passed = clang.length && linker.length && ninja.length &&
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
            "    if (argc == 3 && strcmp(argv[1], \"bench_throughput\") == 0 && strcmp(argv[2], \"help\") == 0)\n"
            "    {\n"
            "        int directories = (mkdir(\"build\", 0755) == 0 || errno == EEXIST);\n"
            "        directories = directories && (mkdir(\"build/throughput-tools\", 0755) == 0 || errno == EEXIST);\n"
            "        pid_t child = directories ? fork() : -1;\n"
            "        if (child == 0)\n"
            "        {\n"
            "            execlp(\"clang\", \"clang\", \"-std=c11\", \"-O2\", \"-Wall\", \"-Wextra\", \"-Werror\",\n"
            "                \"-fwrapv\", \"-fno-strict-aliasing\", \"-funsigned-char\",\n"
            "                \"tools/throughput/fixture.c\", \"-o\", \"build/throughput-tools/throughput\", (char*)0);\n"
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
        production_profile_write(path_join(arena, root, S8("fixture-dependency.h")), S8("#define FIXTURE_BASELINE 1\n")) &&
        production_profile_write(path_join(arena, root, S8("tools/throughput/fixture.c")),
            S8("#include <stdio.h>\nint main(void) { puts(\"baseline corpus consumer\"); return 0; }\n")) &&
        file_copy((CopyFileArguments){.original_path = S8("tools/bootstrap_driver.sh"),
            .new_path = path_join(arena, root, S8("tools/bootstrap_driver.sh"))}) &&
        file_copy((CopyFileArguments){.original_path = S8("build.sh"), .new_path = script}) &&
        chmod((char*)script.pointer, 0755) == 0 &&
        production_profile_write(header, S8("baseline ignored generated source\n")) &&
        production_profile_write(generated, S8("baseline build generated input\n")) &&
        production_profile_write(bootstrap, S8("baseline immutable driver\n")) &&
        production_profile_write(ide, S8("baseline compiler bytes\n")) && chmod((char*)ide.pointer, 0755) == 0 &&
        production_profile_write(path_join(arena, root, S8("empty.file")), S8("")) &&
        production_profile_write(path_join(arena, build, S8("CMakeCache.txt")),
            string_format(arena, S8("BUSTER_INCLUDE_TESTS:BOOL=OFF\nCMAKE_HOME_DIRECTORY:INTERNAL={S8}\n"
                "CMAKE_C_COMPILER:FILEPATH={S8}\nCMAKE_LINKER:FILEPATH={S8}\nCMAKE_MAKE_PROGRAM:FILEPATH={S8}\n"),
                root, clang, linker, ninja));
    String8 init[] = {S8("git"), S8("-C"), root, S8("init"), S8("--quiet")};
    String8 add[] = {S8("git"), S8("-C"), root, S8("add"), S8(".")};
    String8 commit[] = {S8("git"), S8("-C"), root, S8("-c"), S8("user.name=Closure fixture"), S8("-c"),
        S8("user.email=closure@example.invalid"), S8("commit"), S8("--quiet"), S8("-m"), S8("baseline")};
    passed = passed && compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(init)).success &&
        compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(add)).success &&
        compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit)).success;
    ProductionProfileCommandResult revision = compiler_closure_git(arena, root, S8("HEAD"));
    ProductionProfileCommandResult tree = compiler_closure_git(arena, root, S8("HEAD^{tree}"));
    String8 base = production_profile_trim(revision.output);
    String8 base_tree = production_profile_trim(tree.output);
    passed = passed && revision.success && tree.success &&
        compiler_closure_transfer(arena, S8("snapshot"), root, snapshot, base, base_tree, snapshot_report, S8("-"));
    String8 digest = compiler_closure_read(arena, path_join(arena, snapshot, S8(".complete")), SHA256_HEX_CAPACITY - 1);
    if (passed)
    {
        // Real restore must recover ignored and build-generated inputs and the
        // saved baseline driver, and remove candidate-only source/cache files.
        make_directory_recursive(arena, path_join(arena, root, S8("build/generated")));
        make_directory_recursive(arena, path_join(arena, root, S8("build/throughput-tools")));
        make_directory_recursive(arena, path_join(arena, root, S8(".cache/bootstrap-driver")));
        passed = production_profile_write(header, S8("candidate generated source\n")) &&
            production_profile_write(generated, S8("candidate build generated input\n")) &&
            production_profile_write(bootstrap, S8("candidate driver\n")) &&
            production_profile_write(harness, S8("#!/bin/sh\nprintf 'candidate corpus consumer\\n'\n")) &&
            production_profile_write(path_join(arena, root, S8("src/generated/candidate-only.h")), S8("candidate only\n")) &&
            compiler_closure_transfer(arena, S8("restore"), root, snapshot, base, base_tree, restore_report, digest) &&
            compiler_closure_transfer(arena, S8("verify"), root, snapshot, base, base_tree, verify_report, digest);
    }
    if (passed)
    {
        String8 command[] = {harness};
        ProductionProfileCommandResult consumer = compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        passed = consumer.success && string_equal(consumer.output, S8("baseline corpus consumer\n")) &&
            !path_exists(arena, path_join(arena, root, S8("src/generated/candidate-only.h")));
    }


    if (passed && export.length)
    {
        // Retain successful producer/consumer bytes for the hosted Python
        // publisher replay before subsequent refusal controls alter state.
        passed = os_make_directory_exclusive(export).created;
        String8 paths[] = {snapshot_report, restore_report, verify_report};
        String8 names[] = {S8("snapshot.json"), S8("restore.json"), S8("verify.json")};
        for (u64 index = 0; passed && index < BUSTER_ARRAY_LENGTH(paths); index += 1)
        {
            passed = file_copy((CopyFileArguments){.original_path = paths[index],
                .new_path = path_join(arena, export, names[index])}) &&
                file_copy((CopyFileArguments){.original_path = string_format(arena, S8("{S8}.manifest.tsv"), paths[index]),
                    .new_path = path_join(arena, export, string_format(arena, S8("{S8}.manifest.tsv"), names[index]))});
        }
    }
    if (passed)
    {
        String8 cache = path_join(arena, root, S8(".cache/bootstrap-driver"));
        CompilerClosureBootstrapIdentity producer = {0};
        passed = compiler_closure_bootstrap_identity(arena, root, root, cache, &producer);
        String8 artifact = path_join(arena, cache, producer.artifact);
        String8 marker = path_join(arena, cache, producer.marker);
        String8 entry = path_parent(arena, producer.marker);
        String8 original = compiler_closure_read(arena, marker, BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT);
        CompilerClosureBootstrapIdentity rejected = {0};
        // Current valid marker must identify the real TCC-created executable;
        // a wrong configuration, non-executable or missing driver is rejected.
        passed = passed && original.length && !compiler_closure_bootstrap_marker(arena, root, root, cache, entry,
            producer.marker, S8("0000000000000000000000000000000000000000000000000000000000000000"), &rejected);
        passed = chmod((char*)artifact.pointer, 0644) == 0 && passed;
        passed = !compiler_closure_bootstrap_identity(arena, root, root, cache, &rejected) && passed;
        passed = chmod((char*)artifact.pointer, 0755) == 0 && passed;
        String8 moved = path_join(arena, directory, S8("missing-driver"));
        passed = os_file_replace(artifact, moved).v == 0 && passed;
        passed = !compiler_closure_bootstrap_identity(arena, root, root, cache, &rejected) && passed;
        passed = os_file_replace(moved, artifact).v == 0 && passed;
        passed = production_profile_write(marker, S8("BUSTER_BOOTSTRAP_CACHE_V1\n")) && passed;
        passed = !compiler_closure_bootstrap_identity(arena, root, root, cache, &rejected) && passed;
        passed = production_profile_write(marker, original) && passed;
        passed = compiler_closure_bootstrap_identity(arena, root, root, cache, &rejected) &&
            string_equal(producer.artifact_sha256, rejected.artifact_sha256) && passed;
        String8 dependency = path_join(arena, root, S8("fixture-dependency.h"));
        passed = production_profile_write(dependency, S8("#define FIXTURE_BASELINE 2\n")) && passed;
        passed = !compiler_closure_bootstrap_identity(arena, root, root, cache, &rejected) && passed;
        passed = production_profile_write(dependency, S8("#define FIXTURE_BASELINE 1\n")) && passed;
        passed = compiler_closure_bootstrap_identity(arena, root, root, cache, &rejected) && passed;
        String8 old = path_join(arena, cache, S8("posix/0000000000000000000000000000000000000000000000000000000000000000"));
        make_directory_recursive(arena, old);
        passed = production_profile_write(path_join(arena, old, S8("stale.complete")), original) && passed;
        passed = compiler_closure_bootstrap_identity(arena, root, root, cache, &rejected) &&
            string_equal(producer.artifact, rejected.artifact) && passed;
    }
    // Wrong identity, missing marker, changed generated bytes and unfinished
    // snapshot are rejected through the production consumer, without repair.
    if (passed)
    {
        passed = !compiler_closure_transfer(arena, S8("verify"), root, snapshot, S8("0000000000000000000000000000000000000000"),
            base_tree, report, digest);
        String8 marker = path_join(arena, snapshot, S8(".complete"));
        passed = os_file_delete(marker) && passed;
        passed = !compiler_closure_transfer(arena, S8("verify"), root, snapshot, base, base_tree, report, digest) && passed;
        passed = production_profile_write(marker, digest) && passed;
        passed = production_profile_write(generated, S8("tampered generated input\n")) && passed;
        passed = !compiler_closure_transfer(arena, S8("verify"), root, snapshot, base, base_tree, report, digest) && passed;
        passed = production_profile_write(generated, S8("baseline build generated input\n")) && passed;
        // Even same bytes with changed timestamps are refused until restored;
        // the successful round trip above proves the original mtimes survive.
        passed = !compiler_closure_transfer(arena, S8("verify"), root, snapshot, base, base_tree, report, digest) && passed;
    }
    bool cleaned = os_directory_delete(directory);
    passed = passed && cleaned;
    string_print(S8("COMPILER_CLOSURE_SELF_TEST status={S8} source_generated=1 build_generated=1 baseline_driver=1 "
        "corpus_consumer=1 actual_bootstrap_producer=1 marker_artifact_pair=1 changed_dependency=1 stale_configuration=1 "
        "non_executable_driver=1 missing_driver=1 truncated_marker=1 candidate_only_removed=1 empty_file=1 spaces=1 "
        "wrong_identity=1 missing_marker=1 tamper=1 timestamp=1 cleanup={u32}\n"),
        passed ? S8("pass") : S8("fail"), (u32)cleaned);
    return passed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
#endif

