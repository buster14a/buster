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

#include "userspace_actions.c"

BUSTER_GLOBAL_LOCAL bool userspace_make(Arena* arena, String8 source, String8 directory, String8 cc, String8 host_cc, String8 target, String8 evidence)
{
    String8 args[] = {S8("make"), S8("-C"), source, string_format(arena, S8("O={S8}"), directory),
                     string_format(arena, S8("CC={S8}"), cc), string_format(arena, S8("KBUILD_DEFCONFIG={S8}"), path_join(arena, directory, S8("seed.config"))),
                     S8("KCONFIG_NOTIMESTAMP=1"), S8("SOURCE_DATE_EPOCH=0"),
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
    make_directory_recursive(arena, path_join(arena, root, S8("dev")));
    bool valid = musl_copy_object(arena, program_path, path_join(arena, root, S8("bin/busybox"))) &&
                 musl_copy_object(arena, S8("tools/userspace/fixtures/workload.sh"), path_join(arena, root, S8("workload.sh")));
#if BUSTER_LINUX
    valid = chmod(path_join(arena, root, S8("bin/busybox")).pointer, 0755) == 0 && valid;
#endif
    if (valid)
    {
        // Noninteractive ash opens /dev/null for a background child.
        String8 device_args[] = {S8("mknod"), S8("-m"), S8("666"),
                                 path_join(arena, root, S8("dev/null")), S8("c"), S8("1"), S8("3")};
        ClangSuiteCommand device = userspace_command(arena, S8("."), (SliceString8)BUSTER_ARRAY_TO_SLICE(device_args), 60);
        String8 device_evidence = path_join(arena, root, S8("device-node"));
        make_directory_recursive(arena, device_evidence);
        valid = userspace_save_command(arena, device_evidence, device) && device.result == PROCESS_RESULT_SUCCESS;
        if (!valid)
        {
            string_print(S8("USERSPACE_DEVICE result={u32} status={u32} timeout={u32} capture={u32} cleanup={u32}\n"),
                         (u32)device.result, device.platform_status, (u32)device.timed_out, (u32)device.capture_failed, (u32)device.cleanup_failed);
            os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(device.error));
        }
    }
    String8 chroot = executable_resolve_in_path(arena, S8("chroot"));
    valid = chroot.length && valid;
    if (!chroot.length)
    {
        string_print(S8("error: userspace requires the external chroot executable in PATH\n"));
    }
    String8 run_args[] = {S8("env"), S8("-i"), S8("PATH=/bin"), S8("LC_ALL=C"), S8("TZ=UTC"),
                          chroot, root, S8("/bin/busybox"), S8("ash"), S8("/workload.sh")};
    ClangSuiteCommand ran = {.result = PROCESS_RESULT_FAILED};
    if (valid)
    {
        ran = userspace_command(arena, S8("."), (SliceString8)BUSTER_ARRAY_TO_SLICE(run_args), 60);
    }
    String8 expected = clang_suite_read(arena, S8("tools/userspace/fixtures/expected.txt"));
    valid = userspace_save_command(arena, root, ran) && valid;
    valid = ran.result == PROCESS_RESULT_SUCCESS && expected.length && string_equal(ran.output, expected) && !ran.error.length && valid;
    if (!valid)
    {
        os_file_write(os_get_standard_stream(STANDARD_STREAM_OUTPUT), BUSTER_SLICE_TO_BYTE_SLICE(ran.output));
        os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(ran.error));
    }
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
#else
        if (geteuid() != 0)
        {
            string_print(S8("error: userspace chroot supervision requires root on the isolated cloud runner; use sudo -n for the native driver\n"));
            valid = false;
        }
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
            String8 config = clang_suite_read(arena, S8("tools/userspace/fixtures/busybox.config"));
            valid = config.length && valid;
            String8 uapi = path_join(arena, directory, S8("uapi"));
            if (valid)
            {
                make_directory_recursive(arena, uapi);
                String8 cp_args[] = {S8("cp"), S8("-a"), S8("/usr/include/linux"), S8("/usr/include/asm-generic"),
                                    S8("/usr/include/x86_64-linux-gnu/asm"), uapi};
                ClangSuiteCommand copied = userspace_command(arena, S8("."), (SliceString8)BUSTER_ARRAY_TO_SLICE(cp_args), 60);
                valid = copied.result == PROCESS_RESULT_SUCCESS && userspace_headers(arena, directory);
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
                String8 paths[] = {driver, ide, source, musl, musl_output, actions};
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
                    if (valid && !side)
                    {
                        String8 resolve = path_join(arena, build, S8("resolve"));
                        make_directory_recursive(arena, resolve);
                        valid = userspace_seed(arena, build, config) &&
                                userspace_make(arena, source, build, compiler, host_compiler, S8("defconfig"), resolve);
                    }
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
                            String8 expected = string_ends_with_sequence(line, S8("=n"))
                                               ? string_format(arena, S8("# {S8} is not set\n"), string_slice(line, 0, line.length - 2))
                                               : string_format(arena, S8("{S8}\n"), line);
                            bool kept = string_contains(actual_config, expected);
                            if (!kept)
                            {
                                string_print(S8("USERSPACE_CONFIGURATION missing={S8}\n"), line);
                            }
                            valid = kept && valid;
                        }
                        start = i + 1;
                    }
                }
                if (valid)
                {
                    stages[side] = userspace_make(arena, source, build, compiler, host_compiler, S8("busybox"), build);
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
                valid = userspace_pack(arena, directory, musl_output) && valid;
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
