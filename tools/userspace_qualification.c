// Opt-in Linux userspace qualification (#3179), included by build.c.
// userspace_main owns pristine-input checks, reference/candidate builds and
// receipts. userspace_cc adapts upstream CC calls; every action retains argv,
// diagnostics and object hashes. Existing test_musl remains the libc gate.
// This is cloud correctness evidence, never qualified performance evidence.

#define USERSPACE_BUSYBOX_COMMIT "1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4"
#define USERSPACE_COMMAND_SECONDS 600
#define USERSPACE_CC_SECONDS 120

BUSTER_GLOBAL_LOCAL ClangSuiteCommand userspace_command(Arena* arena, String8 directory, SliceString8 arguments, u64 seconds)
{
    ProcessRun run = {
        .arguments = arguments,
        .working_directory = directory,
        .spawn_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = 1,
            .new_process_group = 1,
            .search_path = 1,
            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
        },
    };
    command_print(arguments);
    run.spawn = process_run_spawn(arena, &run);
    ClangSuiteCommand result = {.result = PROCESS_RESULT_FAILED, .launch_failed = !run.spawn.handle};
    if (run.spawn.handle)
    {
        ProcessWaitResult wait = os_process_wait_deadline(arena, run.spawn, seconds * 1000000);
        result = (ClangSuiteCommand){
            .result = wait.result,
            .output = {.pointer = (char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = wait.streams[STANDARD_STREAM_OUTPUT].length},
            .error = {.pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length},
            .platform_status = wait.platform_status,
            .timed_out = wait.timed_out != 0,
            .output_truncated = wait.output_truncated != 0,
            .capture_failed = wait.capture_failed != 0 || wait.capture_limit_exceeded != 0,
            .cleanup_failed = wait.process_tree_cleanup_failed != 0 || wait.process_group_reservation_retained != 0 ||
                              wait.process_group_ownership_lost != 0,
        };
        if (result.timed_out || result.output_truncated || result.capture_failed || result.cleanup_failed)
        {
            result.result = PROCESS_RESULT_FAILED;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool userspace_save_command(Arena* arena, String8 directory, ClangSuiteCommand command)
{
    bool valid = clang_suite_write(arena, path_join(arena, directory, S8("stdout.txt")), command.output);
    valid = clang_suite_write(arena, path_join(arena, directory, S8("stderr.txt")), command.error) && valid;
    String8 status = string_format(arena, S8("result={u32}\nplatform_status={u32}\ntimed_out={u32}\nlaunch_failed={u32}\n"
                                            "output_truncated={u32}\ncapture_failed={u32}\ncleanup_failed={u32}\n"),
                                  (u32)command.result, command.platform_status, (u32)command.timed_out, (u32)command.launch_failed,
                                  (u32)command.output_truncated, (u32)command.capture_failed, (u32)command.cleanup_failed);
    valid = clang_suite_write(arena, path_join(arena, directory, S8("status.txt")), status) && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool userspace_checkout(Arena* arena, String8 root, String8 commit, String8 evidence)
{
    String8 head_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("rev-parse"), S8("HEAD"), S8("HEAD^{tree}")};
    ClangSuiteCommand head = userspace_command(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(head_arguments), 60);
    String8 status_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("status"), S8("--porcelain=v1"), S8("--untracked-files=all"), S8("--ignored")};
    ClangSuiteCommand status = userspace_command(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(status_arguments), 60);
    bool valid = head.result == PROCESS_RESULT_SUCCESS && string_equal(musl_first_line(head.output), commit) &&
                 status.result == PROCESS_RESULT_SUCCESS && !status.output.length;
    if (evidence.length)
    {
        valid = clang_suite_write(arena, evidence, head.output) && valid;
    }
    if (!valid)
    {
        string_print(S8("error: userspace requires exact pristine checkout {S8} at {S8}, including ignored files\n"), root, commit);
    }
    return valid;
}

// Never call a command assembled from an upstream shell recipe. The native
// adapter receives the actual argv from upstream make and retains it verbatim.
// Preprocessor/query calls use Clang and are labeled external generators;
// required target-C actions use the selected compiler with no source patch,
// unsupported-flag filtering, replacement object or successful-result stub.
BUSTER_GLOBAL_LOCAL ProcessResult userspace_cc(Arena* arena, SliceString8 arguments)
{
    bool valid = arguments.length >= 8 && string_equal(arguments.pointer[6], S8("--")) &&
                 (string_equal(arguments.pointer[1], S8("reference")) || string_equal(arguments.pointer[1], S8("buster")) ||
                  string_equal(arguments.pointer[1], S8("host")));
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (valid)
    {
        bool candidate = string_equal(arguments.pointer[1], S8("buster"));
        bool host = string_equal(arguments.pointer[1], S8("host"));
        String8 ide = arguments.pointer[2];
        String8 musl_root = arguments.pointer[3];
        String8 musl_output = arguments.pointer[4];
        String8 evidence = arguments.pointer[5];
        bool compile = false;
        bool preprocess = false;
        bool query = false;
        String8 output = {0};
        String8 source = {0};
        for (u64 i = 7; i < arguments.length; i += 1)
        {
            String8 arg = arguments.pointer[i];
            compile = compile || string_equal(arg, S8("-c"));
            preprocess = preprocess || string_equal(arg, S8("-E"));
            query = query || string_equal(arg, S8("--version")) || string_equal(arg, S8("-dumpmachine")) ||
                    string_equal(arg, S8("-dumpversion")) || string_starts_with_sequence(arg, S8("-print-"));
            if (string_equal(arg, S8("-o")) && i + 1 < arguments.length)
            {
                output = arguments.pointer[i + 1];
            }
            if (string_ends_with_sequence(arg, S8(".c")))
            {
                source = arg;
            }
        }
        bool target_c = !host && compile && source.length && output.length && string_ends_with_sequence(output, S8(".o")) &&
                        !string_starts_with_sequence(source, S8("tmp.")) && !string_equal(source, S8("bb_libtest.c"));
        bool buster = candidate && compile;
        bool link = !compile && !preprocess && !query;
        String8 action = path_join(arena, evidence, string_format(arena, S8("action-{u64}"), os_get_current_process_id()));
        valid = summary_self_test_directory_claim(action) == SUMMARY_DIRECTORY_CLAIMED;
        String8List command = {0};
        string8_list_push(arena, &command, buster ? ide : S8("clang"));
        if (buster)
        {
            string8_list_push(arena, &command, S8("cc"));
            string8_list_push(arena, &command, S8("-fregister-allocator=fast"));
            string8_list_push(arena, &command, S8("-fno-machine-fallback"));
        }
        if (!query && !host)
        {
            string8_list_push(arena, &command, S8("-nostdinc"));
            String8 includes[] = {
                path_join(arena, musl_output, S8("obj/include")),
                path_join(arena, musl_root, S8("arch/x86_64")),
                path_join(arena, musl_root, S8("arch/generic")),
                path_join(arena, musl_root, S8("include")),
                path_join(arena, path_parent(arena, evidence), S8("uapi")),
            };
            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(includes); i += 1)
            {
                string8_list_push(arena, &command, S8("-isystem"));
                string8_list_push(arena, &command, includes[i]);
            }
        }
        if (link && !host)
        {
            // musl's install provides empty libm/libpthread/libdl/libcrypt/librt
            // aliases; all their symbols live in libc.a. Startup is explicit.
            string8_list_push(arena, &command, S8("-nostdlib"));
            string8_list_push(arena, &command, S8("-no-pie"));
            String8 side = S8("clang");
            string8_list_push(arena, &command, path_join(arena, path_join(arena, musl_output, side), S8("crt/crt1.o")));
            string8_list_push(arena, &command, path_join(arena, path_join(arena, musl_output, side), S8("crt/crti.o")));
        }
        String8List original = {0};
        for (u64 i = 7; i < arguments.length; i += 1)
        {
            String8 arg = arguments.pointer[i];
            string8_list_push(arena, &original, arg);
            bool musl_alias = link && !host && (string_equal(arg, S8("-lm")) || string_equal(arg, S8("-lpthread")) ||
                                      string_equal(arg, S8("-ldl")) || string_equal(arg, S8("-lcrypt")) || string_equal(arg, S8("-lrt")));
            if (!musl_alias)
            {
                string8_list_push(arena, &command, arg);
            }
        }
        if (link)
        {
            string8_list_push(arena, &command, S8("-Wl,--start-group"));
            string8_list_push(arena, &command, path_join(arena, musl_output, S8("libc-clang.a")));
            string8_list_push(arena, &command, S8("-Wl,--end-group"));
            string8_list_push(arena, &command, path_join(arena, path_join(arena, musl_output, S8("clang")), S8("crt/crtn.o")));
        }
        SliceString8 argv = string8_list_to_slice(arena, command);
        String8List original_raw = {0};
        SliceString8 original_argv = string8_list_to_slice(arena, original);
        for (u64 i = 0; i < original_argv.length; i += 1)
        {
            string8_list_push(arena, &original_raw, string_format(arena, S8("{u64}:{S8}\n"), original_argv.pointer[i].length, original_argv.pointer[i]));
        }
        valid = clang_suite_write(arena, path_join(arena, action, S8("upstream-argv.txt")),
                                  string_join_arena(arena, string8_list_to_slice(arena, original_raw), false)) && valid;
        // Length-prefixed argv cannot confuse whitespace inside a define/path.
        String8List raw = {0};
        for (u64 i = 0; i < argv.length; i += 1)
        {
            string8_list_push(arena, &raw, string_format(arena, S8("{u64}:{S8}\n"), argv.pointer[i].length, argv.pointer[i]));
        }
        valid = clang_suite_write(arena, path_join(arena, action, S8("argv.txt")),
                                  string_join_arena(arena, string8_list_to_slice(arena, raw), false)) && valid;
        if (link && !host && string_equal(output, S8("busybox_unstripped")))
        {
            valid = clang_suite_write(arena, path_join(arena, evidence, S8("final-link.argv")),
                                      string_join_arena(arena, string8_list_to_slice(arena, raw), false)) && valid;
        }
        ClangSuiteCommand ran = {.result = PROCESS_RESULT_FAILED};
        if (valid)
        {
            // Keep compiler stdout clean: upstream's macro/version recipes read it.
            ProcessRun run = {
                .arguments = argv,
                .spawn_options = {
                    .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                    .use_process_environment = 1, .new_process_group = 1, .search_path = 1,
                    .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
                },
            };
            run.spawn = process_run_spawn(arena, &run);
            ran.launch_failed = !run.spawn.handle;
            if (run.spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_deadline(arena, run.spawn, USERSPACE_CC_SECONDS * 1000000);
                ran = (ClangSuiteCommand){
                    .result = wait.result,
                    .output = {.pointer = (char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = wait.streams[STANDARD_STREAM_OUTPUT].length},
                    .error = {.pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length},
                    .platform_status = wait.platform_status, .timed_out = wait.timed_out != 0,
                    .output_truncated = wait.output_truncated != 0,
                    .capture_failed = wait.capture_failed != 0 || wait.capture_limit_exceeded != 0,
                    .cleanup_failed = wait.process_tree_cleanup_failed != 0 || wait.process_group_reservation_retained != 0 ||
                                      wait.process_group_ownership_lost != 0,
                };
                if (ran.timed_out || ran.output_truncated || ran.capture_failed || ran.cleanup_failed)
                {
                    ran.result = PROCESS_RESULT_FAILED;
                }
            }
        }
        valid = userspace_save_command(arena, action, ran) && valid;
        String8 source_hash = {0};
        if (source.length)
        {
            ByteSlice bytes = file_read(arena, source, (FileReadOptions){0});
            valid = bytes.pointer && bytes.length && valid;
            if (bytes.pointer && bytes.length)
            {
                source_hash = stage_object_sha256_bytes(arena, bytes.pointer, bytes.length);
            }
        }
        String8 object_hash = {0};
        if (ran.result == PROCESS_RESULT_SUCCESS && output.length && !string_equal(output, S8("/dev/null")))
        {
            ByteSlice bytes = file_read(arena, output, (FileReadOptions){0});
            valid = bytes.length && bytes.pointer && valid;
            if (bytes.pointer && bytes.length)
            {
                object_hash = stage_object_sha256_bytes(arena, bytes.pointer, bytes.length);
            }
        }
        String8 record = string_format(arena, S8("role={S8}\ncompiler={S8}\nsource={S8}\nsource_sha256={S8}\noutput={S8}\nobject_sha256={S8}\nstatus={S8}\n"),
                                       host ? S8("external-host-generator") : target_c ? S8("target-c") : preprocess || query ? S8("external-generator") : link ? S8("external-link") : S8("compiler-probe"),
                                       buster ? S8("buster") : S8("clang"), source, source_hash, output, object_hash,
                                       ran.result == PROCESS_RESULT_SUCCESS && valid ? S8("pass") : S8("fail"));
        valid = clang_suite_write(arena, path_join(arena, action, S8("record.txt")), record) && valid;
        os_file_write(os_get_standard_stream(STANDARD_STREAM_OUTPUT), BUSTER_SLICE_TO_BYTE_SLICE(ran.output));
        os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(ran.error));
        result = valid ? ran.result : PROCESS_RESULT_FAILED;
    }
    return result;
}

#include "userspace_actions.c"

BUSTER_GLOBAL_LOCAL bool userspace_make(Arena* arena, String8 source, String8 directory, String8 cc, String8 host_cc, String8 target, String8 evidence)
{
    String8 args[] = {S8("make"), S8("-C"), source, string_format(arena, S8("O={S8}"), directory),
                     string_format(arena, S8("CC={S8}"), cc), string_format(arena, S8("KCONFIG_ALLCONFIG={S8}"), path_join(arena, directory, S8("seed.config"))),
                     string_format(arena, S8("HOSTCC={S8}"), host_cc), S8("SKIP_STRIP=y"), S8("V=1"), S8("-j2"), S8("-k"), target};
    ClangSuiteCommand ran = userspace_command(arena, S8("."), (SliceString8)BUSTER_ARRAY_TO_SLICE(args), USERSPACE_COMMAND_SECONDS);
    bool valid = userspace_save_command(arena, evidence, ran) && ran.result == PROCESS_RESULT_SUCCESS;
    os_file_write(os_get_standard_stream(STANDARD_STREAM_OUTPUT), BUSTER_SLICE_TO_BYTE_SLICE(ran.output));
    os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(ran.error));
    return valid;
}


BUSTER_GLOBAL_LOCAL bool userspace_reference_archive(Arena* arena, String8 root, String8 output)
{
    MuslManifest manifest = {0};
    bool valid = musl_collect_manifest(arena, root, output, &manifest);
    String8* objects = arena_allocate(arena, String8, manifest.unit_count);
    u64 count = 0;
    for (u64 i = 0; valid && i < manifest.unit_count; i += 1)
    {
        MuslUnit unit = manifest.units[i];
        // Every reference action completed before test_musl's inventory gate.
        // Check the whole reference object set, including startup/loader.
        valid = path_exists(arena, unit.clang_object);
        if (string_starts_with_sequence(unit.relative, S8("src/")))
        {
            objects[count++] = unit.clang_object;
        }
    }
    String8 ar = executable_resolve_in_path(arena, S8("ar"));
    if (valid)
    {
        valid = ar.length && musl_archive(arena, ar, path_join(arena, output, S8("libc-clang.a")), objects, count,
                                         path_join(arena, output, S8("userspace-reference.rsp")));
    }
    string_print(S8("USERSPACE_REFERENCE_LIBC units={u64} archive_members={u64} status={S8}\n"),
                 manifest.unit_count, count, valid ? S8("complete-reference") : S8("fail"));
    return valid;
}

BUSTER_GLOBAL_LOCAL bool userspace_workload(Arena* arena, String8 program_path, String8 root)
{
    make_directory_recursive(arena, path_join(arena, root, S8("bin")));
    make_directory_recursive(arena, path_join(arena, root, S8("tmp")));
    bool valid = musl_copy_object(arena, program_path, path_join(arena, root, S8("bin/busybox"))) &&
                 musl_copy_object(arena, S8("tests/userspace/workload.sh"), path_join(arena, root, S8("workload.sh")));
#if BUSTER_LINUX
    valid = chmod(path_join(arena, root, S8("bin/busybox")).pointer, 0755) == 0 && valid;
#endif
    String8 run_args[] = {S8("sudo"), S8("-n"), S8("env"), S8("-i"), S8("PATH=/bin"), S8("LC_ALL=C"), S8("TZ=UTC"),
                          S8("/usr/sbin/chroot"), root, S8("/bin/busybox"), S8("ash"), S8("/workload.sh")};
    ClangSuiteCommand ran = {.result = PROCESS_RESULT_FAILED};
    if (valid)
    {
        ran = userspace_command(arena, S8("."), (SliceString8)BUSTER_ARRAY_TO_SLICE(run_args), 60);
    }
    String8 expected = clang_suite_read(arena, S8("tests/userspace/expected.txt"));
    valid = userspace_save_command(arena, root, ran) && valid;
    valid = ran.result == PROCESS_RESULT_SUCCESS && expected.length && string_equal(ran.output, expected) && !ran.error.length && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool userspace_joint_link(Arena* arena, String8 directory, String8 musl_output)
{
    String8 raw = clang_suite_read(arena, path_join(arena, directory, S8("buster-actions/final-link.argv")));
    String8List arguments = {0};
    bool valid = raw.length != 0;
    u64 cursor = 0;
    u64 replacements = 0;
    String8 output = path_join(arena, directory, S8("joint-busybox"));
    while (valid && cursor < raw.length)
    {
        u64 length = 0;
        bool digits = false;
        while (cursor < raw.length && raw.pointer[cursor] >= '0' && raw.pointer[cursor] <= '9')
        {
            u64 digit = (u64)(raw.pointer[cursor++] - '0');
            valid = length <= (UINT64_MAX - digit) / 10;
            if (valid)
            {
                length = length * 10 + digit;
            }
            digits = true;
        }
        valid = valid && digits && cursor < raw.length && raw.pointer[cursor] == ':';
        if (valid)
        {
            cursor += 1;
            valid = length < raw.length - cursor && raw.pointer[cursor + length] == '\n';
        }
        if (valid)
        {
            String8 argument = string_slice(raw, cursor, cursor + length);
            cursor += length + 1;
            String8 reference_paths[] = {
                path_join(arena, musl_output, S8("clang/crt/crt1.o")),
                path_join(arena, musl_output, S8("clang/crt/crti.o")),
                path_join(arena, musl_output, S8("libc-clang.a")),
                path_join(arena, musl_output, S8("clang/crt/crtn.o")),
            };
            String8 candidate_paths[] = {
                path_join(arena, musl_output, S8("buster/crt/crt1.o")),
                path_join(arena, musl_output, S8("buster/crt/crti.o")),
                path_join(arena, musl_output, S8("libc-buster.a")),
                path_join(arena, musl_output, S8("buster/crt/crtn.o")),
            };
            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(reference_paths); i += 1)
            {
                if (string_equal(argument, reference_paths[i]))
                {
                    argument = candidate_paths[i];
                    replacements += 1;
                }
            }
            if (string_equal(argument, S8("busybox_unstripped")))
            {
                argument = output;
                replacements += 1;
            }
            string8_list_push(arena, &arguments, argument);
        }
    }
    valid = replacements == 5 && valid;
    ClangSuiteCommand ran = {.result = PROCESS_RESULT_FAILED};
    if (valid)
    {
        ran = userspace_command(arena, path_join(arena, directory, S8("buster")),
                                string8_list_to_slice(arena, arguments), USERSPACE_CC_SECONDS);
    }
    String8 evidence = path_join(arena, directory, S8("joint-link"));
    make_directory_recursive(arena, evidence);
    valid = userspace_save_command(arena, evidence, ran) && ran.result == PROCESS_RESULT_SUCCESS && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL ProcessResult userspace_main(Arena* arena, SliceString8 arguments, String8 driver)
{
    bool cc = arguments.length && string_equal(arguments.pointer[0], S8("--cc"));
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (cc)
    {
        result = userspace_cc(arena, arguments);
    }
    else
    {
        bool valid = arguments.length == 4;
#if !BUSTER_LINUX || !BUSTER_CPU_ARCH_X86_64
        valid = false;
#endif
        if (valid)
        {
            String8 source = os_path_absolute(arena, arguments.pointer[0], true);
            String8 musl = os_path_absolute(arena, arguments.pointer[1], true);
            String8 libc_test = os_path_absolute(arena, arguments.pointer[2], true);
            String8 musl_output = {0};
            String8 requested = os_path_absolute_lexical(arena, arguments.pointer[3], true);
            String8 parent = os_path_absolute(arena, path_parent(arena, requested), true);
            String8 directory = path_join(arena, parent, quickjs_basename(requested));
            String8 ide = os_path_absolute(arena, musl_ide_path(arena, S8("Release")), true);
            driver = os_path_absolute(arena, driver, true);
            valid = source.length && musl.length && libc_test.length && parent.length && ide.length && driver.length &&
                    !string_equal(directory, source) && !string_starts_with_sequence(directory, path_join(arena, source, S8(""))) &&
                    !string_equal(directory, musl) && !string_starts_with_sequence(directory, path_join(arena, musl, S8(""))) &&
                    !string_equal(directory, libc_test) && !string_starts_with_sequence(directory, path_join(arena, libc_test, S8("")));
            if (valid)
            {
                valid = summary_self_test_directory_claim(directory) == SUMMARY_DIRECTORY_CLAIMED;
            }
            bool claimed = valid;
            if (valid)
            {
                valid = userspace_checkout(arena, source, S8(USERSPACE_BUSYBOX_COMMIT), path_join(arena, directory, S8("busybox.identity"))) &&
                        userspace_checkout(arena, musl, S8(MUSL_COMPATIBILITY_COMMIT), path_join(arena, directory, S8("musl.identity"))) &&
                        userspace_checkout(arena, libc_test, S8(LIBC_TEST_COMMIT), path_join(arena, directory, S8("libc-test.identity")));
            }
            // Reuse the existing gate directly in this process. If it fails,
            // the complete independent reference can still expose BusyBox
            // compiler gaps; only full musl success authorizes the joint link.
            bool musl_passed = false;
            if (valid)
            {
                TestMuslOptions options = {.source_directory = musl, .libc_test_directory = libc_test, .config = S8("Release")};
                musl_passed = test_musl_action(arena, &options) == PROCESS_RESULT_SUCCESS;
                musl_output = os_path_absolute(arena, string_format(arena, S8("build/musl-{S8}-{u64}"),
                                                  S8(MUSL_COMPATIBILITY_TAG), os_get_current_process_id()), true);
                valid = musl_output.length && userspace_reference_archive(arena, musl, musl_output);
            }
            String8 config = clang_suite_read(arena, S8("tests/userspace/busybox.config"));
            valid = config.length && valid;
            String8 uapi = path_join(arena, directory, S8("uapi"));
            if (valid)
            {
                make_directory_recursive(arena, uapi);
                String8 cp_args[] = {S8("cp"), S8("-a"), S8("/usr/include/linux"), S8("/usr/include/asm-generic"),
                                    S8("/usr/include/x86_64-linux-gnu/asm"), uapi};
                ClangSuiteCommand copied = userspace_command(arena, S8("."), (SliceString8)BUSTER_ARRAY_TO_SLICE(cp_args), 60);
                valid = copied.result == PROCESS_RESULT_SUCCESS;
            }
            String8 reference_config = {0};
            bool stages[2] = {0};
            for (u64 side = 0; valid && side < 2; side += 1)
            {
                String8 name = side ? S8("buster") : S8("reference");
                String8 build = path_join(arena, directory, name);
                String8 actions = path_join(arena, directory, side ? S8("buster-actions") : S8("reference-actions"));
                make_directory_recursive(arena, build);
                make_directory_recursive(arena, actions);
                valid = clang_suite_write(arena, path_join(arena, build, S8(".config")), side ? reference_config : config) &&
                        clang_suite_write(arena, path_join(arena, build, S8("seed.config")), config);
                String8 host_compiler = string_format(arena, S8("{S8} test_userspace --cc host {S8} {S8} {S8} {S8} --"),
                                                     driver, ide, musl, musl_output, actions);
                String8 compiler = string_format(arena, S8("{S8} test_userspace --cc {S8} {S8} {S8} {S8} {S8} --"),
                                                driver, name, ide, musl, musl_output, actions);
                // Upstream expands CC through its shell. Reject unsafe paths
                // instead of treating them as shell code.
                String8 paths[] = {driver, ide, musl, musl_output, actions};
                for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(paths); i += 1)
                {
                    for (u64 j = 0; valid && j < paths[i].length; j += 1)
                    {
                        char8 ch = paths[i].pointer[j];
                        valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                                ch == '/' || ch == '-' || ch == '_' || ch == '.';
                    }
                }
                if (valid)
                {
                    String8 configure = path_join(arena, build, S8("configure"));
                    make_directory_recursive(arena, configure);
                    valid = valid && userspace_make(arena, source, build, compiler, host_compiler, side ? S8("silentoldconfig") : S8("allnoconfig"), configure);
                }
                String8 actual_config = clang_suite_read(arena, path_join(arena, build, S8(".config")));
                if (!side)
                {
                    reference_config = actual_config;
                }
                valid = actual_config.length && string_equal(reference_config, actual_config) && valid;
                // Every requested boolean must survive upstream dependency resolution.
                u64 start = 0;
                for (u64 i = 0; i <= config.length; i += 1)
                {
                    if (i == config.length || config.pointer[i] == '\n')
                    {
                        String8 line = string_slice(config, start, i);
                        if (line.length)
                        {
                            valid = string_contains(actual_config, string_format(arena, S8("{S8}\n"), line)) && valid;
                        }
                        start = i + 1;
                    }
                }
                if (valid)
                {
                    stages[side] = userspace_make(arena, source, build, compiler, S8("busybox"), build);
                    bool inventory = userspace_inventory(arena, directory, side != 0);
                    stages[side] = stages[side] && inventory;
                    // Keep attempting the independent candidate after a
                    // reference failure only in a later run with a valid reference.
                    valid = stages[side];
                }
                if (valid)
                {
                    valid = userspace_workload(arena, path_join(arena, build, S8("busybox")), path_join(arena, build, S8("root")));
                    string_print(S8("USERSPACE_WORKLOAD compiler={S8} libc=clang status={S8}\n"), name, valid ? S8("pass") : S8("fail"));
                }
            }
            bool joint = false;
            if (valid && musl_passed)
            {
                joint = userspace_joint_link(arena, directory, musl_output) &&
                        userspace_workload(arena, path_join(arena, directory, S8("joint-busybox")), path_join(arena, directory, S8("joint-root")));
                valid = joint;
                string_print(S8("USERSPACE_WORKLOAD compiler=buster libc=buster status={S8}\n"), joint ? S8("pass") : S8("fail"));
            }
            if (claimed)
            {
                bool unchanged = userspace_checkout(arena, source, S8(USERSPACE_BUSYBOX_COMMIT), (String8){0}) &&
                                 userspace_checkout(arena, musl, S8(MUSL_COMPATIBILITY_COMMIT), (String8){0}) &&
                                 userspace_checkout(arena, libc_test, S8(LIBC_TEST_COMMIT), (String8){0});
                valid = unchanged && musl_passed && joint && valid;
                String8 text = string_format(arena, S8("BUSTER_USERSPACE_RECEIPT_V1\nbusybox_commit={S8}\n"
                                                      "musl_full_gate={u32}\nreference_build={u32}\nbuster_build={u32}\njoint_workload={u32}\ninputs_pristine={u32}\n"
                                                      "linker=external-clang-and-ld\nconfiguration_sha256={S8}\n"
                                                      "performance_validation=incomplete\nupstream_tests=incomplete\n"
                                                      "busybox_dynamic=incomplete\nnative_buster_link=incomplete\nstatus={S8}\n"),
                                             S8(USERSPACE_BUSYBOX_COMMIT), (u32)musl_passed, (u32)stages[0], (u32)stages[1], (u32)joint, (u32)unchanged,
                                             stage_object_sha256_bytes(arena, (u8*)reference_config.pointer, reference_config.length),
                                             valid ? S8("workload-pass") : S8("fail"));
                valid = clang_suite_write(arena, path_join(arena, directory, S8("receipt.txt")), text) && valid;
            }
            result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
        else
        {
            string_print(S8("usage (Linux x86-64): test_userspace BUSYBOX_CHECKOUT MUSL_CHECKOUT LIBC_TEST_CHECKOUT FRESH_RESULTS\n"));
        }
    }
    return result;
}
