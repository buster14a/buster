// Native producer/consumer controls for the attempt-local closure.
#if BUSTER_LINUX
BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_self_test(Arena* arena)
{
    String8 directory = string_format(arena, S8("build/compiler-closure-self-test-{u64}"), os_now_microseconds());
    make_directory_recursive(arena, directory);
    directory = os_path_absolute(arena, directory, true);
    String8 root = path_join(arena, directory, S8("checkout with spaces"));
    String8 snapshot = path_join(arena, directory, S8("snapshot"));
    String8 report = path_join(arena, directory, S8("native.json"));
    make_directory_recursive(arena, path_join(arena, root, S8("src/generated")));
    make_directory_recursive(arena, path_join(arena, root, S8("build/generated")));
    make_directory_recursive(arena, path_join(arena, root, S8("build/Release")));
    make_directory_recursive(arena, path_join(arena, root, S8("build/throughput-tools")));
    make_directory_recursive(arena, path_join(arena, root, S8(".cache/bootstrap-driver")));
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
        production_profile_write(path_join(arena, root, S8("build.c")), S8("baseline native driver source\n")) &&
        production_profile_write(script, S8("#!/bin/sh\n[ \"$1\" = bench_throughput ] && [ \"$2\" = help ]\n")) &&
        chmod((char*)script.pointer, 0755) == 0 &&
        production_profile_write(header, S8("baseline ignored generated source\n")) &&
        production_profile_write(generated, S8("baseline build generated input\n")) &&
        production_profile_write(bootstrap, S8("baseline immutable driver\n")) &&
        production_profile_write(ide, S8("baseline compiler bytes\n")) && chmod((char*)ide.pointer, 0755) == 0 &&
        production_profile_write(harness, S8("#!/bin/sh\nprintf 'baseline corpus consumer\\n'\n")) &&
        chmod((char*)harness.pointer, 0755) == 0 &&
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
        compiler_closure_transfer(arena, S8("snapshot"), root, snapshot, base, base_tree, report, S8("-"));
    String8 digest = compiler_closure_read(arena, path_join(arena, snapshot, S8(".complete")), SHA256_HEX_CAPACITY - 1);
    if (passed)
    {
        // Real restore must recover ignored and build-generated inputs and the
        // saved baseline driver, and remove candidate-only source/cache files.
        make_directory_recursive(arena, path_join(arena, root, S8("build/generated")));
        make_directory_recursive(arena, path_join(arena, root, S8(".cache/bootstrap-driver")));
        passed = production_profile_write(header, S8("candidate generated source\n")) &&
            production_profile_write(generated, S8("candidate build generated input\n")) &&
            production_profile_write(bootstrap, S8("candidate driver\n")) &&
            production_profile_write(path_join(arena, root, S8("src/generated/candidate-only.h")), S8("candidate only\n")) &&
            compiler_closure_transfer(arena, S8("restore"), root, snapshot, base, base_tree, report, digest) &&
            compiler_closure_transfer(arena, S8("verify"), root, snapshot, base, base_tree, report, digest);
    }
    if (passed)
    {
        String8 command[] = {harness};
        ProductionProfileCommandResult consumer = compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        passed = consumer.success && string_equal(consumer.output, S8("baseline corpus consumer\n")) &&
            !path_exists(arena, path_join(arena, root, S8("src/generated/candidate-only.h")));
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
        "corpus_consumer=1 candidate_only_removed=1 empty_file=1 spaces=1 wrong_identity=1 missing_marker=1 tamper=1 timestamp=1 cleanup={u32}\n"),
        passed ? S8("pass") : S8("fail"), (u32)cleaned);
    return passed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
#endif

