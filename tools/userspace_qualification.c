// Opt-in Linux userspace qualification (#3179), included by build.c.
// userspace_main owns immutable-input checks, reference/candidate builds and
// receipts. userspace_cc adapts upstream CC calls; every action retains argv,
// diagnostics and object hashes. Existing test_musl remains the libc gate.
// userspace_input_objects proves raw worktree identities in bounded Git batches.
// userspace_input_self_test owns Git-only refusal controls, without a compiler.
// This is cloud correctness evidence, never qualified performance evidence.

#define USERSPACE_BUSYBOX_COMMIT "1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4"
#define USERSPACE_COMMAND_SECONDS 600
#define USERSPACE_CC_SECONDS 120

enum
{
    USERSPACE_GIT_CAPTURE_BYTES = 32 * 1024 * 1024,
    USERSPACE_GIT_BATCH_FILES = 128,
    USERSPACE_INPUT_PATH_BYTES = 4096,
    USERSPACE_INPUT_LINK_BYTES = 65536,
};

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
    if (arguments.length && string_equal(arguments.pointer[0], S8("git")))
    {
        run.spawn_options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = USERSPACE_GIT_CAPTURE_BYTES;
        run.spawn_options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = USERSPACE_GIT_CAPTURE_BYTES;
        run.spawn_options.capture_limits.total = 2 * USERSPACE_GIT_CAPTURE_BYTES;
    }
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

BUSTER_GLOBAL_LOCAL bool userspace_git_environment_allowed(SliceString8 keys)
{
    bool valid = !keys.length || keys.pointer != 0;
    for (u64 i = 0; valid && i < keys.length; i += 1)
    {
        String8 key = keys.pointer[i];
        bool git = key.length >= 4 && (key.pointer[0] == 'G' || key.pointer[0] == 'g') &&
                   (key.pointer[1] == 'I' || key.pointer[1] == 'i') &&
                   (key.pointer[2] == 'T' || key.pointer[2] == 't') && key.pointer[3] == '_';
        valid = !git;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL ClangSuiteCommand userspace_git(Arena* arena, String8 root, SliceString8 command)
{
    String8 prefix[] = {
        S8("git"), S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"),
        S8("-c"), S8("core.untrackedCache=false"), S8("-c"), S8("core.filemode=true"),
        S8("-c"), S8("core.autocrlf=false"), S8("-c"), S8("core.hooksPath=/dev/null"),
    };
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(prefix); i += 1)
    {
        os_argument_builder_append(&builder, prefix[i]);
    }
    for (u64 i = 0; i < command.length; i += 1)
    {
        os_argument_builder_append(&builder, command.pointer[i]);
    }
    ClangSuiteCommand result = userspace_command(arena, root, os_argument_builder_flush(&builder), 60);
    return result;
}

BUSTER_GLOBAL_LOCAL bool userspace_input_relative_path(String8 path)
{
    bool valid = path.length && path.length < USERSPACE_INPUT_PATH_BYTES && path.pointer[0] != '/';
    u64 start = 0;
    for (u64 i = 0; valid && i <= path.length; i += 1)
    {
        if (i == path.length || path.pointer[i] == '/')
        {
            String8 component = string_slice(path, start, i);
            valid = component.length && !string_equal(component, S8(".")) && !string_equal(component, S8("..")) &&
                    !string_equal(component, S8(".git"));
            start = i + 1;
        }
    }
    return valid;
}

// The NUL-delimited immutable tree, not the index, supplies expected identities.
// Hash regular files in bounded batches with filters disabled. Symlink targets
// are compared as bytes; a link cannot redirect a regular pinned source file.
BUSTER_GLOBAL_LOCAL bool userspace_input_objects(Arena* arena, String8 root, String8 commit)
{
    bool valid = false;
#if BUSTER_LINUX
    String8 tree_arguments[] = {S8("ls-tree"), S8("-rz"), S8("--full-tree"), commit};
    ClangSuiteCommand tree = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(tree_arguments));
    valid = tree.result == PROCESS_RESULT_SUCCESS && tree.output.length;
    u64 cursor = 0;
    while (valid && cursor < tree.output.length)
    {
        String8 hash_arguments[USERSPACE_GIT_BATCH_FILES + 3] = {S8("hash-object"), S8("--no-filters"), S8("--")};
        String8 hashes[USERSPACE_GIT_BATCH_FILES] = {0};
        u64 count = 0;
        while (valid && cursor < tree.output.length && count < USERSPACE_GIT_BATCH_FILES)
        {
            u64 end = cursor;
            while (end < tree.output.length && tree.output.pointer[end])
            {
                end += 1;
            }
            String8 entry = string_slice(tree.output, cursor, end);
            valid = end < tree.output.length && entry.length > 53 && entry.pointer[6] == ' ' &&
                    string_equal(string_slice(entry, 7, 11), S8("blob")) && entry.pointer[11] == ' ' &&
                    entry.pointer[52] == '\t';
            String8 mode = valid ? string_slice(entry, 0, 6) : (String8){0};
            String8 object = valid ? string_slice(entry, 12, 52) : (String8){0};
            String8 relative = valid ? string_slice(entry, 53, entry.length) : (String8){0};
            valid = valid && userspace_input_relative_path(relative);
            if (valid)
            {
                String8 path = string_duplicate_arena(arena, path_join(arena, root, relative), true);
                String8 parent = path_parent(arena, path);
                String8 actual_parent = os_path_absolute(arena, parent, true);
                struct stat status;
                valid = actual_parent.length && string_equal(parent, actual_parent) &&
                        lstat((const char*)path.pointer, &status) == 0;
                if (valid && (string_equal(mode, S8("100644")) || string_equal(mode, S8("100755"))))
                {
                    valid = S_ISREG(status.st_mode) &&
                            ((status.st_mode & 0111) != 0) == string_equal(mode, S8("100755"));
                    if (valid)
                    {
                        hash_arguments[count + 3] = relative;
                        hashes[count++] = object;
                    }
                }
                else if (valid && string_equal(mode, S8("120000")))
                {
                    valid = S_ISLNK(status.st_mode) && status.st_size > 0 && status.st_size < USERSPACE_INPUT_LINK_BYTES;
                    if (valid)
                    {
                        char* target = arena_allocate(arena, char, (u64)status.st_size + 1);
                        ssize_t size = readlink((const char*)path.pointer, target, (size_t)status.st_size + 1);
                        String8 blob_arguments[] = {S8("cat-file"), S8("blob"), object};
                        ClangSuiteCommand blob = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(blob_arguments));
                        valid = size == status.st_size && blob.result == PROCESS_RESULT_SUCCESS &&
                                string_equal(blob.output, (String8){.pointer = (char8*)target, .length = size > 0 ? (u64)size : 0});
                    }
                }
                else
                {
                    valid = false;
                }
            }
            cursor = end + 1;
        }
        if (valid && count)
        {
            ClangSuiteCommand actual = userspace_git(arena, root, (SliceString8){.pointer = hash_arguments, .length = count + 3});
            valid = actual.result == PROCESS_RESULT_SUCCESS && actual.output.length == count * 41;
            for (u64 i = 0; valid && i < count; i += 1)
            {
                valid = actual.output.pointer[i * 41 + 40] == '\n' &&
                        string_equal(string_slice(actual.output, i * 41, i * 41 + 40), hashes[i]);
            }
        }
    }
    valid = valid && cursor == tree.output.length;
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(root);
    BUSTER_UNUSED(commit);
#endif
    return valid;
}

BUSTER_GLOBAL_LOCAL bool userspace_checkout_with_environment(Arena* arena, String8 root, String8 commit, String8 evidence,
                                                           SliceString8 environment_keys)
{
    bool valid = userspace_git_environment_allowed(environment_keys) && commit.length == 40;
    for (u64 i = 0; valid && i < commit.length; i += 1)
    {
        char8 ch = commit.pointer[i];
        valid = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    }
    String8 identity = {0};
    if (valid)
    {
        String8 root_arguments[] = {S8("rev-parse"), S8("--show-toplevel")};
        ClangSuiteCommand directory = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(root_arguments));
        String8 canonical = os_path_absolute(arena, root, true);
        valid = directory.result == PROCESS_RESULT_SUCCESS && canonical.length &&
                string_equal(compat_trim_ascii_space(directory.output), canonical);
    }
    if (valid)
    {
        String8 head_arguments[] = {S8("rev-parse"), S8("HEAD"), S8("HEAD^{tree}")};
        ClangSuiteCommand head = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(head_arguments));
        identity = head.output;
        valid = head.result == PROCESS_RESULT_SUCCESS && head.output.length == 82 && string_equal(musl_first_line(head.output), commit);
    }
    if (valid)
    {
        String8 replace_arguments[] = {S8("for-each-ref"), S8("refs/replace")};
        ClangSuiteCommand replacements = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(replace_arguments));
        valid = replacements.result == PROCESS_RESULT_SUCCESS && !replacements.output.length;
    }
    if (valid)
    {
        String8 sparse_arguments[] = {S8("config"), S8("--bool"), S8("--default=false"), S8("--get"), S8("core.sparseCheckout")};
        ClangSuiteCommand sparse = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(sparse_arguments));
        valid = sparse.result == PROCESS_RESULT_SUCCESS && string_equal(compat_trim_ascii_space(sparse.output), S8("false"));
    }
    if (valid)
    {
        String8 index_arguments[] = {S8("ls-files"), S8("-v"), S8("--stage"), S8("-z")};
        ClangSuiteCommand index = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(index_arguments));
        valid = index.result == PROCESS_RESULT_SUCCESS && index.output.length;
        for (u64 cursor = 0; valid && cursor < index.output.length;)
        {
            u64 end = cursor;
            while (end < index.output.length && index.output.pointer[end])
            {
                end += 1;
            }
            String8 entry = string_slice(index.output, cursor, end);
            valid = end < index.output.length && entry.length > 52 && entry.pointer[0] == 'H' && entry.pointer[1] == ' ' &&
                    entry.pointer[50] == '0' && entry.pointer[51] == '\t';
            cursor = end + 1;
        }
    }
    if (valid)
    {
        String8 status_arguments[] = {S8("status"), S8("--porcelain=v1"), S8("--untracked-files=all"), S8("--ignored=matching")};
        ClangSuiteCommand status = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(status_arguments));
        valid = status.result == PROCESS_RESULT_SUCCESS && !status.output.length;
    }
    if (valid)
    {
        valid = userspace_input_objects(arena, root, commit);
    }
    if (evidence.length)
    {
        valid = clang_suite_write(arena, evidence, identity) && valid;
    }
    if (!valid)
    {
        string_print(S8("USERSPACE_INPUT_REFUSED expected={S8} path={S8}\n"), commit, root);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool userspace_checkout(Arena* arena, String8 root, String8 commit, String8 evidence)
{
    bool valid = userspace_checkout_with_environment(arena, root, commit, evidence, program_state->input.environment_keys);
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


BUSTER_GLOBAL_LOCAL bool userspace_input_control(Arena* arena, String8 root, String8 pin, String8 name, u64* refused)
{
    bool rejected = !userspace_checkout(arena, root, pin, (String8){0});
    *refused += rejected ? 1 : 0;
    string_print(S8("USERSPACE_INPUT_CONTROL name={S8} refused={u32} compiler_invocations=0\n"), name, (u32)rejected);
    return rejected;
}

BUSTER_GLOBAL_LOCAL bool userspace_input_git_control(Arena* arena, String8 root, SliceString8 command)
{
    ClangSuiteCommand result = userspace_git(arena, root, command);
    bool valid = result.result == PROCESS_RESULT_SUCCESS;
    return valid;
}

// Synthetic repositories exercise admission only: no root, chroot, external
// project, reference compiler or candidate compiler is invoked in this mode.
BUSTER_GLOBAL_LOCAL ProcessResult userspace_input_self_test(Arena* arena)
{
    bool valid = false;
    u64 refused = 0;
#if BUSTER_LINUX
    String8 relative = {0};
    valid = summary_self_test_claim_directory(arena, S8("userspace-input"), &relative);
    String8 root = valid ? os_path_absolute(arena, relative, true) : (String8){0};
    valid = valid && root.length;
    String8 fixture = path_join(arena, root, S8("fixture.c"));
    String8 pristine = S8("int pristine;\n");
    String8 altered = S8("int altered;\n");
    String8 init[] = {S8("init"), S8("-q")};
    String8 add[] = {S8("add"), S8("--"), S8("fixture.c")};
    String8 commit[] = {S8("-c"), S8("user.name=Buster input controls"), S8("-c"),
                       S8("user.email=controls@example.invalid"), S8("-c"), S8("commit.gpgsign=false"),
                       S8("commit"), S8("-q"), S8("-m"), S8("input control")};
    String8 head[] = {S8("rev-parse"), S8("HEAD")};
    valid = valid && userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(init)) &&
            clang_suite_write(arena, fixture, pristine) &&
            userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(add)) &&
            userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit));
    ClangSuiteCommand identity = {.result = PROCESS_RESULT_FAILED};
    if (valid)
    {
        identity = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(head));
    }
    String8 pin = compat_trim_ascii_space(identity.output);
    valid = valid && identity.result == PROCESS_RESULT_SUCCESS && pin.length == 40 &&
            userspace_checkout(arena, root, pin, (String8){0});
    if (valid)
    {
        valid = userspace_input_control(arena, root, S8("0000000000000000000000000000000000000000"), S8("wrong-pin"), &refused);
    }
    if (valid)
    {
        valid = clang_suite_write(arena, fixture, altered) &&
                userspace_input_control(arena, root, pin, S8("tracked-drift"), &refused) &&
                !userspace_input_objects(arena, root, pin) && clang_suite_write(arena, fixture, pristine);
    }
    if (valid)
    {
        String8 assume[] = {S8("update-index"), S8("--assume-unchanged"), S8("--"), S8("fixture.c")};
        String8 clear[] = {S8("update-index"), S8("--no-assume-unchanged"), S8("--"), S8("fixture.c")};
        valid = userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(assume)) &&
                clang_suite_write(arena, fixture, altered) &&
                userspace_input_control(arena, root, pin, S8("assume-unchanged"), &refused) &&
                !userspace_input_objects(arena, root, pin) && clang_suite_write(arena, fixture, pristine) &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(clear));
    }
    if (valid)
    {
        String8 skip[] = {S8("update-index"), S8("--skip-worktree"), S8("--"), S8("fixture.c")};
        String8 clear[] = {S8("update-index"), S8("--no-skip-worktree"), S8("--"), S8("fixture.c")};
        valid = userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(skip)) &&
                clang_suite_write(arena, fixture, altered) &&
                userspace_input_control(arena, root, pin, S8("skip-worktree"), &refused) &&
                !userspace_input_objects(arena, root, pin) && clang_suite_write(arena, fixture, pristine) &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(clear));
    }
    if (valid)
    {
        String8 injected = path_join(arena, root, S8("injected.h"));
        String8 exclude = path_join(arena, root, S8(".git/info/exclude"));
        valid = clang_suite_write(arena, exclude, S8("injected.h\n")) && clang_suite_write(arena, injected, altered) &&
                userspace_input_control(arena, root, pin, S8("ignored-header"), &refused) &&
                os_file_delete(injected) && clang_suite_write(arena, exclude, (String8){0});
        valid = valid && clang_suite_write(arena, injected, altered) &&
                userspace_input_control(arena, root, pin, S8("untracked-header"), &refused) && os_file_delete(injected);
    }
    if (valid)
    {
        String8 sparse[] = {S8("config"), S8("core.sparseCheckout"), S8("true")};
        String8 clear[] = {S8("config"), S8("core.sparseCheckout"), S8("false")};
        valid = userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(sparse)) &&
                userspace_input_control(arena, root, pin, S8("sparse"), &refused) &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(clear));
    }
    if (valid)
    {
        String8 keys[] = {S8("GIT_INDEX_FILE")};
        bool rejected = !userspace_checkout_with_environment(arena, root, pin, (String8){0}, (SliceString8)BUSTER_ARRAY_TO_SLICE(keys));
        refused += rejected ? 1 : 0;
        valid = rejected;
        string_print(S8("USERSPACE_INPUT_CONTROL name=git-environment refused={u32} compiler_invocations=0\n"), (u32)rejected);
    }
    if (valid)
    {
        String8 hidden_mode[] = {S8("config"), S8("core.filemode"), S8("false")};
        String8 clear[] = {S8("config"), S8("core.filemode"), S8("true")};
        valid = userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(hidden_mode)) &&
                chmod((const char*)fixture.pointer, 0755) == 0 &&
                userspace_input_control(arena, root, pin, S8("executable-mode"), &refused) &&
                !userspace_input_objects(arena, root, pin) && chmod((const char*)fixture.pointer, 0644) == 0 &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(clear));
    }
    if (valid)
    {
        String8 altered_head = {0};
        valid = clang_suite_write(arena, fixture, altered) &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(add)) &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit));
        if (valid)
        {
            ClangSuiteCommand changed = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(head));
            altered_head = compat_trim_ascii_space(changed.output);
            valid = changed.result == PROCESS_RESULT_SUCCESS && altered_head.length == 40;
        }
        String8 checkout[] = {S8("checkout"), S8("-q"), S8("--detach"), pin};
        String8 replace[] = {S8("replace"), pin, altered_head};
        String8 clear[] = {S8("replace"), S8("-d"), pin};
        valid = valid && userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(checkout)) &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(replace)) &&
                userspace_input_control(arena, root, pin, S8("replacement-ref"), &refused) &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(clear));
    }
    if (valid)
    {
        String8 target = path_join(arena, root, S8("target.c"));
        valid = clang_suite_write(arena, target, pristine) && os_file_delete(fixture) &&
                symlink("target.c", (const char*)fixture.pointer) == 0 &&
                userspace_input_control(arena, root, pin, S8("regular-file-link"), &refused) &&
                !userspace_input_objects(arena, root, pin) && os_file_delete(fixture) &&
                clang_suite_write(arena, fixture, pristine) && os_file_delete(target);
    }
    if (valid)
    {
        String8 link = path_join(arena, root, S8("input-link"));
        String8 add_link[] = {S8("add"), S8("--"), S8("input-link")};
        valid = symlink("fixture.c", (const char*)link.pointer) == 0 &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(add_link)) &&
                userspace_input_git_control(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit));
        if (valid)
        {
            ClangSuiteCommand linked = userspace_git(arena, root, (SliceString8)BUSTER_ARRAY_TO_SLICE(head));
            String8 linked_pin = compat_trim_ascii_space(linked.output);
            valid = linked.result == PROCESS_RESULT_SUCCESS && userspace_checkout(arena, root, linked_pin, (String8){0});
        }
    }
    valid = valid && refused == 11;
#else
    BUSTER_UNUSED(arena);
#endif
    string_print(S8("USERSPACE_INPUT_CONTROLS result={S8} refused={u64} compiler_invocations=0\n"),
                 valid ? S8("pass") : S8("fail"), refused);
    ProcessResult result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult userspace_main(Arena* arena, SliceString8 arguments, String8 driver)
{
    bool cc = arguments.length && string_equal(arguments.pointer[0], S8("--cc"));
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (cc)
    {
        result = userspace_cc(arena, arguments);
    }
    else if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test-inputs")))
    {
        result = userspace_input_self_test(arena);
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
            String8 ide = os_path_absolute(arena, compat_ide_resolve(arena, S8("test_userspace"), (String8){0}, S8("Release")), true);
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
