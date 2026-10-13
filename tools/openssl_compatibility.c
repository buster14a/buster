// OpenSSL qualification intake (#3184), included only by the native build driver.
// openssl_main owns pristine admission, independent cells and receipt publication.
// openssl_proxy attributes C, assembly and external links without changing flags.
// Existing bounded child capture and Git controls are shared with RAD's harness.
// This intake is build/test evidence, not TLS/ABI/security/performance qualification.

#define BUSTER_OPENSSL_COMMIT "45e844fa2a14ec92d146bd8f5778ac130b6625fb"

BUSTER_GLOBAL_LOCAL RaddebuggerCommandResult openssl_command(Arena* arena, SliceString8 arguments, String8 working_directory, String8 prefix, RaddebuggerEnvironment environment, bool* stopped, u64 timeout_us)
{
    RaddebuggerCommandResult result = {.wait = {.result = PROCESS_RESULT_FAILED}, .recorded = true};
    if (prefix.length)
    {
        String8List command = {0};
        for (u64 index = 0; index < arguments.length; index += 1)
        {
            string8_list_push(arena, &command, string_format(arena, S8("{u64}:{S8}\n"), arguments.pointer[index].length, arguments.pointer[index]));
        }
        String8 text = string_join_arena(arena, string8_list_to_slice(arena, command), false);
        result.recorded = file_write(string_format_z(arena, S8("{S8}.command"), prefix), BUSTER_SLICE_TO_BYTE_SLICE(text));
    }
    ProcessRun run = {
        .arguments = arguments,
        .working_directory = working_directory,
        .spawn_options = {
            .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
            .use_process_environment = 1,
            .new_process_group = 1,
            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_TRUNCATE,
        },
    };
    if (environment != RADDEBUGGER_ENVIRONMENT_INHERIT)
    {
        // Use explicit snapshots: Git provenance probes omit repository/index
        // overrides, and runtime smokes omit display-server variables. Neither
        // policy changes this driver's environment or compiler invocations.
        SliceString8 inherited_keys = program_state->input.environment_keys;
        SliceString8 inherited_values = program_state->input.environment_values;
        bool valid = inherited_keys.length == inherited_values.length &&
                     (!inherited_keys.length || (inherited_keys.pointer && inherited_values.pointer));
        if (valid)
        {
            String8* keys = arena_allocate(arena, String8, inherited_keys.length);
            String8* values = arena_allocate(arena, String8, inherited_keys.length);
            u64 count = 0;
            for (u64 index = 0; index < inherited_keys.length; index += 1)
            {
                String8 key = inherited_keys.pointer[index];
                bool git_override = key.length >= 4 && (key.pointer[0] == 'G' || key.pointer[0] == 'g') &&
                                    (key.pointer[1] == 'I' || key.pointer[1] == 'i') && (key.pointer[2] == 'T' || key.pointer[2] == 't') && key.pointer[3] == '_';
                bool display_override = string_equal(key, S8("DISPLAY")) || string_equal(key, S8("WAYLAND_DISPLAY"));
                bool omitted = environment == RADDEBUGGER_ENVIRONMENT_GIT ? git_override : display_override;
                if (!omitted)
                {
                    keys[count] = key;
                    values[count++] = inherited_values.pointer[index];
                }
            }
            run.environment_keys = (SliceString8){.pointer = keys, .length = count};
            run.environment_values = (SliceString8){.pointer = values, .length = count};
            run.spawn_options.use_process_environment = 0;
        }
        result.recorded = valid && result.recorded;
    }
    run.spawn_options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = RADDEBUGGER_CAPTURE_LIMIT_BYTES;
    run.spawn_options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = RADDEBUGGER_CAPTURE_LIMIT_BYTES;
    run.spawn_options.capture_limits.total = 2 * RADDEBUGGER_CAPTURE_LIMIT_BYTES;
    u64 started = os_now_microseconds();
    if (result.recorded && !*stopped)
    {
        run.spawn = process_run_spawn(arena, &run);
        if (run.spawn.handle)
        {
            result.wait = os_process_wait_deadline(arena, run.spawn, timeout_us);
        }
    }
    result.output = BYTE_SLICE_TO_STRING(8, result.wait.streams[STANDARD_STREAM_OUTPUT]);
    *stopped = build_compiler_query_cleanup_failed(result.wait) || *stopped;
    if (prefix.length)
    {
        String8 status = string_format(arena, S8("result={u32}\nplatform_status={u32}\nelapsed_us={u64}\ntimed_out={u32}\ncapture_failed={u32}\n"
                                                "output_truncated={u32}\ncleanup_failed={u32}\nspawn_failure={u32}\nspawn_error={u32}\nlaunched={u32}\nadmission_stopped={u32}\n"),
                                       (u32)result.wait.result, result.wait.platform_status, os_now_microseconds() - started, (u32)result.wait.timed_out,
                                       (u32)result.wait.capture_failed, (u32)result.wait.output_truncated, (u32)result.wait.process_tree_cleanup_failed,
                                       (u32)run.spawn.failure, run.spawn.error.v, (u32)(run.spawn.handle != 0), (u32)*stopped);
        bool output_written = file_write(string_format_z(arena, S8("{S8}.stdout"), prefix), result.wait.streams[STANDARD_STREAM_OUTPUT]);
        bool error_written = file_write(string_format_z(arena, S8("{S8}.stderr"), prefix), result.wait.streams[STANDARD_STREAM_ERROR]);
        bool status_written = file_write(string_format_z(arena, S8("{S8}.status"), prefix), BUSTER_SLICE_TO_BYTE_SLICE(status));
        result.recorded = output_written && error_written && status_written && result.recorded;
    }
    if (result.wait.result != PROCESS_RESULT_SUCCESS || result.wait.timed_out || result.wait.output_truncated || !result.recorded)
    {
        string_print_error(S8("OPENSSL_CHILD result={u32} timed_out={u32} truncated={u32} recorded={u32} logs={S8}\n{S8}\n"),
                     (u32)result.wait.result, (u32)result.wait.timed_out, (u32)result.wait.output_truncated, (u32)result.recorded, prefix,
                     BYTE_SLICE_TO_STRING(8, result.wait.streams[STANDARD_STREAM_ERROR]));
    }
    return result;
}

// The upstream pin declares optional test integrations as gitlinks. This
// intake accepts their exact pinned entries only while their directories are
// absent or empty; populated integrations require a separate input inventory.
BUSTER_GLOBAL_LOCAL bool openssl_gitlink_admitted(Arena* arena, String8 root, String8 entry)
{
    String8 paths[] = {S8("cloudflare-quiche"), S8("fuzz/corpora"), S8("gost-engine"), S8("krb5"), S8("oqs-provider"), S8("pkcs11-provider"), S8("pyca-cryptography"), S8("python-ecdsa"), S8("tlsfuzzer"), S8("tlslite-ng"), S8("wycheproof")};
    String8 commits[] = {S8("7ab6a55cfe471267d61e4d28ba43d41defcd87e0"), S8("ce771805c094d098c25a218bc8e9f7344eccbc5a"), S8("74b1f4fddbc2d6de969815b1992ddc1ae7c643fe"), S8("784c38f50e70a739400cdd3f2620bac2e2788e6c"), S8("7bc597c04b534ddea9b6654481deb31ded8e1bbc"), S8("64fc325ac0f91d03d76b3546df2998d3a38c525b"), S8("7e33b0e7739d633c77b8c478620167f693ed13f4"), S8("4096fa01715929e08b97e73f3173aee9d57f2a3f"), S8("61f45d9701294fd87ef92d2a7e3dfb076653a562"), S8("77ef321dde1a9e6bcf94d73c80f8789a770d8031"), S8("2196000605e45d91097147c9c71f26b72af58003")};
    bool valid = false;
    if (entry.length > 52 && entry.pointer[49] == ' ' && entry.pointer[50] == '0' && entry.pointer[51] == '\t')
    {
        String8 path = string_slice(entry, 52, entry.length);
        String8 commit = string_slice(entry, 9, 49);
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(paths); i += 1)
        {
            if (string_equal(path, paths[i]) && string_equal(commit, commits[i]))
            {
                String8 directory = path_join(arena, root, path);
                valid = !path_exists(arena, directory);
                if (!valid)
                {
                    String8 canonical = os_path_absolute(arena, directory, true);
                    MuslDirectoryEntry* entries = 0;
                    u64 count = 0;
                    valid = string_equal(canonical, directory) && musl_list_directory(arena, directory, &entries, &count) && count == 0;
                }
                break;
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool openssl_git_file_batch(Arena* arena, String8 git, String8 root, String8* paths, String8* hashes, u64 count, bool* stopped)
{
    bool valid = true;
    if (count)
    {
        OsArgumentBuilder builder = os_argument_builder_start(arena);
        os_argument_builder_append(&builder, git);
        os_argument_builder_append(&builder, S8("--no-replace-objects"));
        os_argument_builder_append(&builder, S8("hash-object"));
        os_argument_builder_append(&builder, S8("--no-filters"));
        os_argument_builder_append(&builder, S8("--"));
        for (u64 i = 0; i < count; i += 1)
        {
            os_argument_builder_append(&builder, paths[i]);
        }
        RaddebuggerCommandResult command = openssl_command(arena, os_argument_builder_flush(&builder), root, (String8){0},
                                                          RADDEBUGGER_ENVIRONMENT_GIT, stopped, RADDEBUGGER_PROCESS_TIMEOUT_US);
        valid = raddebugger_command_ok(command);
        u64 offset = 0;
        for (u64 i = 0; valid && i < count; i += 1)
        {
            u64 end = offset;
            while (end < command.output.length && command.output.pointer[end] != '\n') { end += 1; }
            valid = string_equal(compat_trim_ascii_space(string_slice(command.output, offset, end)), hashes[i]);
            offset = end + (end < command.output.length);
        }
        valid = valid && offset == command.output.length;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool openssl_git_verify(Arena* arena, String8 git, String8 source_directory, String8 expected_commit, bool* stopped)
{
    String8 root_arguments[] = {git, S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"), S8("rev-parse"), S8("--show-toplevel")};
    RaddebuggerCommandResult root = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(root_arguments), source_directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
    String8 reported_root = os_path_absolute(arena, compat_trim_ascii_space(root.output), true);
    bool valid = raddebugger_command_ok(root) && reported_root.length && string_equal(reported_root, source_directory);
    String8 commit = {0};
    if (valid)
    {
        String8 head_arguments[] = {git, S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"), S8("rev-parse"), S8("HEAD")};
        RaddebuggerCommandResult head = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(head_arguments), source_directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
        commit = compat_trim_ascii_space(head.output);
        valid = raddebugger_command_ok(head) && string_equal(commit, expected_commit);
    }
    if (valid)
    {
        // Ignored local headers can override upstream includes. Index flags
        // can conceal edited tracked files from status, so neither is accepted.
        String8 status_arguments[] = {git, S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"), S8("status"), S8("--porcelain"), S8("--untracked-files=all"), S8("--ignored")};
        RaddebuggerCommandResult status = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(status_arguments), source_directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
        valid = raddebugger_command_ok(status) && !status.output.length;
    }
    if (valid)
    {
        String8 index_arguments[] = {git, S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"), S8("ls-files"), S8("-v"), S8("--stage"), S8("-z")};
        RaddebuggerCommandResult index = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(index_arguments), source_directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
        valid = raddebugger_command_ok(index) && index.output.length;
        String8 paths[128];
        String8 hashes[128];
        u64 count = 0;
        for (u64 offset = 0; valid && offset < index.output.length;)
        {
            u64 end = offset;
            while (end < index.output.length && index.output.pointer[end])
            {
                end += 1;
            }
            String8 entry = string_slice(index.output, offset, end);
            valid = end < index.output.length && entry.length > 9 && entry.pointer[0] == 'H' && entry.pointer[1] == ' ' &&
                    (string_equal(string_slice(entry, 2, 8), S8("100644")) || string_equal(string_slice(entry, 2, 8), S8("100755")) ||
                     (string_equal(string_slice(entry, 2, 8), S8("160000")) && string_equal(expected_commit, S8(BUSTER_OPENSSL_COMMIT)) &&
                      openssl_gitlink_admitted(arena, source_directory, entry)));
            if (valid && !string_equal(string_slice(entry, 2, 8), S8("160000")))
            {
                valid = entry.length > 52 && entry.pointer[49] == ' ' && entry.pointer[50] == '0' && entry.pointer[51] == '\t';
                if (valid)
                {
                    String8 path = path_join(arena, source_directory, string_slice(entry, 52, entry.length));
                    valid = string_equal(path, os_path_absolute(arena, path, true));
                    paths[count] = path;
                    hashes[count++] = string_slice(entry, 9, 49);
                    if (valid && count == BUSTER_ARRAY_LENGTH(paths))
                    {
                        valid = openssl_git_file_batch(arena, git, source_directory, paths, hashes, count, stopped);
                        count = 0;
                    }
                }
            }
            offset = end + 1;
        }
        if (valid) { valid = openssl_git_file_batch(arena, git, source_directory, paths, hashes, count, stopped); }
    }
    if (valid)
    {
        String8 sparse_arguments[] = {git, S8("--no-replace-objects"), S8("config"), S8("--bool"), S8("--default=false"), S8("--get"), S8("core.sparseCheckout")};
        RaddebuggerCommandResult sparse = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(sparse_arguments), source_directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
        valid = raddebugger_command_ok(sparse) && string_equal(compat_trim_ascii_space(sparse.output), S8("false"));
    }
    string_print(S8("OPENSSL_SOURCE commit={S8} expected={S8} pristine={u32} path={S8}\n"), commit, expected_commit, (u32)valid, source_directory);
    return valid;
}

typedef enum OpensslRoute
{
    OPENSSL_ROUTE_C,
    OPENSSL_ROUTE_ASSEMBLY,
    OPENSSL_ROUTE_LINK,
    OPENSSL_ROUTE_PROBE,
    OPENSSL_ROUTE_INVALID,
} OpensslRoute;

BUSTER_GLOBAL_LOCAL OpensslRoute openssl_route(SliceString8 arguments)
{
    bool c = false;
    bool assembly = false;
    bool object = false;
    bool compile = false;
    for (u64 i = 0; i < arguments.length; i += 1)
    {
        String8 arg = arguments.pointer[i];
        if (string_equal(arg, S8("-o")) || string_equal(arg, S8("-I")) || string_equal(arg, S8("-include")) ||
            string_equal(arg, S8("-isystem")) || string_equal(arg, S8("-MF")) || string_equal(arg, S8("-MT")))
        {
            i += 1;
        }
        else if (!string_starts_with_sequence(arg, S8("-")))
        {
            c = string_ends_with_sequence(arg, S8(".c")) || c;
            assembly = string_ends_with_sequence(arg, S8(".s")) || string_ends_with_sequence(arg, S8(".S")) || assembly;
            object = string_ends_with_sequence(arg, S8(".o")) || string_ends_with_sequence(arg, S8(".a")) || object;
        }
        compile = string_equal(arg, S8("-c")) || string_equal(arg, S8("-E")) || string_equal(arg, S8("-S")) || compile;
    }
    OpensslRoute result = c && assembly ? OPENSSL_ROUTE_INVALID : c ? OPENSSL_ROUTE_C : assembly ? OPENSSL_ROUTE_ASSEMBLY :
                          object && !compile ? OPENSSL_ROUTE_LINK : OPENSSL_ROUTE_PROBE;
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult openssl_proxy(Arena* arena, SliceString8 arguments)
{
    bool valid = arguments.length >= 7;
    bool reference = valid && string_equal(arguments.pointer[0], S8("reference"));
    valid = valid && (reference || string_equal(arguments.pointer[0], S8("buster")));
    bool stopped = false;
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (valid)
    {
        String8 ide = arguments.pointer[1];
        String8 clang = arguments.pointer[2];
        String8 logs = arguments.pointer[3];
        SliceString8 forwarded = {.pointer = arguments.pointer + 6, .length = arguments.length - 6};
        OpensslRoute route = openssl_route(forwarded);
        valid = route != OPENSSL_ROUTE_INVALID;
        // Queries go to the actual C compiler too; a trusted --version response
        // must never manufacture a successful Buster compiler probe.
        bool external = reference || route == OPENSSL_ROUTE_ASSEMBLY || route == OPENSSL_ROUTE_LINK;
        String8 compiler = external ? clang : ide;
        String8 prefix = path_join(arena, logs, string_format(arena, S8("action-{u64}"), os_get_current_process_id()));
        String8 kinds[] = {S8("c"), S8("external-assembly"), S8("external-link"), S8("compiler-probe"), S8("invalid")};
        String8 kind = kinds[route];
        // openssl_main hashes both compilers once and again after the cells.
        String8 compiler_digest = arguments.pointer[external ? 5 : 4];
        String8 attribution = string_format(arena, S8("mode={S8}\nkind={S8}\ncompiler={S8}\ncompiler_sha256={S8}\n"),
                                            reference ? S8("reference") : S8("buster"), kind, compiler, compiler_digest);
        valid = clang_suite_write(arena, string_format(arena, S8("{S8}.provenance"), prefix), attribution) && valid;
        if (valid)
        {
            OsArgumentBuilder builder = os_argument_builder_start(arena);
            os_argument_builder_append(&builder, compiler);
            if (!external) { os_argument_builder_append(&builder, S8("cc")); }
            os_argument_builder_append(&builder, S8("-target"));
            os_argument_builder_append(&builder, external ? S8("x86_64-linux-gnu") : S8("x86_64-linux"));
            os_argument_builder_append(&builder, external ? S8("-march=x86-64") : S8("-march=baseline"));
            for (u64 i = 0; i < forwarded.length; i += 1)
            {
                os_argument_builder_append(&builder, forwarded.pointer[i]);
            }
            RaddebuggerCommandResult command = openssl_command(arena, os_argument_builder_flush(&builder), (String8){0}, prefix,
                                                                  RADDEBUGGER_ENVIRONMENT_INHERIT, &stopped, RADDEBUGGER_PROCESS_TIMEOUT_US);
            // Configure's preprocessor queries need their real output. Commands
            // printed by the harness belong in the retained build log, not here.
            string_print(S8("{S8}"), command.output);
            string_print_error(S8("{S8}"), BYTE_SLICE_TO_STRING(8, command.wait.streams[STANDARD_STREAM_ERROR]));
            valid = raddebugger_command_ok(command);
            for (u64 i = 0; valid && i + 1 < forwarded.length; i += 1)
            {
                if (string_equal(forwarded.pointer[i], S8("-o")))
                {
                    String8 digest = {0};
                    valid = stage_object_sha256_file(arena, forwarded.pointer[i + 1], &digest);
                    if (valid)
                    {
                        valid = clang_suite_write(arena, string_format(arena, S8("{S8}.artifact-sha256"), prefix), digest);
                    }
                    break;
                }
            }
            result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 openssl_shell_word(Arena* arena, String8 word)
{
    String8List list = {0};
    string8_list_push(arena, &list, S8("'"));
    u64 start = 0;
    for (u64 i = 0; i < word.length; i += 1)
    {
        if (word.pointer[i] == '\'')
        {
            string8_list_push(arena, &list, string_slice(word, start, i));
            string8_list_push(arena, &list, S8("'\\''"));
            start = i + 1;
        }
    }
    string8_list_push(arena, &list, string_slice(word, start, word.length));
    string8_list_push(arena, &list, S8("'"));
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, list), false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool openssl_step(Arena* arena, String8 directory, String8 name, SliceString8 arguments, bool* stopped)
{
    String8 prefix = path_join(arena, directory, name);
    bool tests = string_equal(name, S8("tests"));
    command_print(arguments);
    RaddebuggerCommandResult command = openssl_command(arena, arguments, directory, prefix, RADDEBUGGER_ENVIRONMENT_INHERIT, stopped,
                                                       RADDEBUGGER_PROCESS_TIMEOUT_US);
    if (tests)
    {
        // Retain the entire bounded TAP log in the evidence directory and print
        // its final progress/summary even when the upstream suite fails.
        u64 start = command.output.length > 8192 ? command.output.length - 8192 : 0;
        string_print(S8("{S8}\n"), string_slice(command.output, start, command.output.length));
    }
    bool result = raddebugger_command_ok(command);
    return result;
}

BUSTER_GLOBAL_LOCAL bool openssl_archive(Arena* arena, String8 directory, bool* stopped)
{
    String8 tar = executable_resolve_in_path(arena, S8("tar"));
    String8 archive = path_join(arena, directory, S8("evidence.tar.gz"));
    String8 tar_digest = {0};
    bool valid = tar.length && !path_exists(arena, archive) && stage_object_sha256_file(arena, tar, &tar_digest);
    if (valid)
    {
        String8 version[] = {tar, S8("--version")};
        valid = openssl_step(arena, directory, S8("archiver"), (SliceString8)BUSTER_ARRAY_TO_SLICE(version), stopped);
        // Create the excluded output before tar snapshots directory metadata.
        // Creating it during traversal would make the input directory change.
        valid = clang_suite_write(arena, archive, (String8){0}) && valid;
        String8 arguments[] = {tar, S8("--exclude=./evidence.tar.gz"), S8("-czf"), archive, S8("-C"), directory, S8(".")};
        valid = openssl_step(arena, directory, S8("archive"), (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), stopped) && valid;
    }
    String8 digest = {0};
    valid = valid && stage_object_sha256_file(arena, archive, &digest);
    if (valid)
    {
        String8 identity = string_format(arena, S8("archiver={S8}\narchiver_sha256={S8}\narchive_sha256={S8}\n"),
                                         tar, tar_digest, digest);
        valid = clang_suite_write(arena, path_join(arena, directory, S8("archive-identity.txt")), identity);
        string_print(S8("OPENSSL_ARCHIVE sha256={S8} path={S8}\n"), digest, archive);
    }
    return valid;
}

#include "openssl_compatibility_test.c"

BUSTER_GLOBAL_LOCAL ProcessResult openssl_main(Arena* arena, SliceString8 arguments, String8 driver)
{
    bool proxy = arguments.length && string_equal(arguments.pointer[0], S8("--cc-proxy"));
    bool self_test = arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test"));
    bool valid = false;
    if (proxy)
    {
        SliceString8 rest = {.pointer = arguments.pointer + 1, .length = arguments.length - 1};
        valid = openssl_proxy(arena, rest) == PROCESS_RESULT_SUCCESS;
    }
    else if (self_test)
    {
        valid = openssl_self_test(arena);
    }
    else if (arguments.length == 4)
    {
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        bool stopped = false;
        String8 source = os_path_absolute(arena, arguments.pointer[0], true);
        String8 output = os_path_absolute_lexical(arena, arguments.pointer[1], true);
        String8 parent = os_path_absolute(arena, path_parent(arena, output), true);
        String8 ide = os_path_absolute(arena, arguments.pointer[2], true);
        String8 clang = os_path_absolute(arena, arguments.pointer[3], true);
        driver = os_path_absolute(arena, driver, true);
        String8 git = executable_resolve_in_path(arena, S8("git"));
        String8 perl = executable_resolve_in_path(arena, S8("perl"));
        String8 make = executable_resolve_in_path(arena, S8("make"));
        valid = source.length && parent.length && ide.length && clang.length && driver.length && git.length && perl.length && make.length;
        if (valid)
        {
            output = path_join(arena, parent, quickjs_basename(output));
            valid = !string_equal(parent, source) && !string_starts_with_sequence(parent, string_format(arena, S8("{S8}/"), source)) &&
                    openssl_git_verify(arena, git, source, S8(BUSTER_OPENSSL_COMMIT), &stopped);
        }
        bool claimed = valid && summary_self_test_directory_claim(output) == SUMMARY_DIRECTORY_CLAIMED;
        valid = claimed && valid;
        String8List summary = {0};
        string8_list_push(arena, &summary, S8("cell\tconfigure\tbuild\ttests\n"));
        if (claimed)
        {
            String8 inventory_args[] = {git, S8("--no-replace-objects"), S8("ls-tree"), S8("-r"), S8("--full-tree"), S8(BUSTER_OPENSSL_COMMIT)};
            valid = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(inventory_args), source,
                                                              path_join(arena, output, S8("source-inventory")), RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) && valid;
            String8 versions[] = {clang, S8("--version")};
            valid = openssl_step(arena, output, S8("reference-compiler"), (SliceString8)BUSTER_ARRAY_TO_SLICE(versions), &stopped) && valid;
            String8 digests[4] = {0};
            valid = stage_object_sha256_file(arena, ide, &digests[0]) && stage_object_sha256_file(arena, clang, &digests[1]) && valid;
            // Both trusted cells complete before any Buster attempt. Build
            // failures are retained independently; no failing feature is removed.
            for (u32 cell = 0; cell < 4 && !stopped; cell += 1)
            {
                bool reference = cell < 2;
                bool portable = (cell % 2) == 0;
                String8 name = cell == 0 ? S8("reference-portable") : cell == 1 ? S8("reference-optimized") :
                               cell == 2 ? S8("buster-portable") : S8("buster-optimized");
                String8 directory = path_join(arena, output, name);
                make_directory_recursive(arena, directory);
                String8 logs = path_join(arena, directory, S8("actions"));
                make_directory_recursive(arena, logs);
                String8 cc = string_format(arena, S8("CC={S8} test_openssl --cc-proxy {S8} {S8} {S8} {S8} {S8} {S8}"),
                                           openssl_shell_word(arena, driver), reference ? S8("reference") : S8("buster"),
                                           openssl_shell_word(arena, ide), openssl_shell_word(arena, clang), openssl_shell_word(arena, logs),
                                           digests[0], digests[1]);
                String8 configure_args[] = {perl, path_join(arena, source, S8("Configure")), S8("linux-x86_64"), S8("--debug"), S8("shared"),
                                            string_format(arena, S8("--prefix={S8}/prefix"), directory),
                                            string_format(arena, S8("--openssldir={S8}/prefix/ssl"), directory), cc, S8("AR=ar"), S8("RANLIB=ranlib"), S8("no-asm")};
                SliceString8 configure = {.pointer = configure_args, .length = BUSTER_ARRAY_LENGTH(configure_args) - (portable ? 0 : 1)};
                bool configured = openssl_step(arena, directory, S8("configure"), configure, &stopped);
                bool built = false;
                bool tested = false;
                if (configured)
                {
                    String8 dump[] = {perl, S8("configdata.pm"), S8("--dump")};
                    configured = openssl_step(arena, directory, S8("configuration"), (SliceString8)BUSTER_ARRAY_TO_SLICE(dump), &stopped);
                }
                if (configured)
                {
                    String8 build_args[] = {make, S8("-j2")};
                    built = openssl_step(arena, directory, S8("build"), (SliceString8)BUSTER_ARRAY_TO_SLICE(build_args), &stopped);
                }
                if (built)
                {
                    String8 list[] = {make, S8("list-tests")};
                    bool listed = openssl_step(arena, directory, S8("test-inventory"), (SliceString8)BUSTER_ARRAY_TO_SLICE(list), &stopped);
                    // V=1 would make run_tests.pl ignore HARNESS_JOBS.
                    String8 test[] = {make, S8("test"), S8("HARNESS_JOBS=4"), S8("HARNESS_TIMER=1")};
                    tested = openssl_step(arena, directory, S8("tests"), (SliceString8)BUSTER_ARRAY_TO_SLICE(test), &stopped) && listed;
                }
                valid = configured && built && tested && valid;
                string8_list_push(arena, &summary, string_format(arena, S8("{S8}\t{S8}\t{S8}\t{S8}\n"), name,
                                  configured ? S8("pass") : S8("fail"), built ? S8("pass") : configured ? S8("fail") : S8("not-run"),
                                  tested ? S8("pass") : built ? S8("fail") : S8("not-run")));
                string_print(S8("OPENSSL_CELL name={S8} configure={u32} build={u32} tests={u32}\n"), name, (u32)configured, (u32)built, (u32)tested);
            }
            bool pristine = openssl_git_verify(arena, git, source, S8(BUSTER_OPENSSL_COMMIT), &stopped);
            bool compilers = stage_object_sha256_file(arena, ide, &digests[2]) && stage_object_sha256_file(arena, clang, &digests[3]) &&
                             string_equal(digests[0], digests[2]) && string_equal(digests[1], digests[3]);
            valid = compilers && pristine && valid;
            String8 receipt = string_format(arena, S8("BUSTER_OPENSSL_INTAKE_V1\nupstream={S8}\npristine={u32}\ncompilers_unchanged={u32}\nbuild_test_cells={S8}\n"
                                                     "target=x86_64-linux\nconfiguration=debug-shared\nlinker=external-clang\n"
                                                     "tls_interoperability=not-run\nmixed_abi=not-run\nproviders=not-qualified\n"
                                                     "performance=incomplete\nsecurity=not-qualified\nissue_complete=0\n"),
                                            S8(BUSTER_OPENSSL_COMMIT), (u32)pristine, (u32)compilers, valid ? S8("pass") : S8("fail"));
            valid = clang_suite_write(arena, path_join(arena, output, S8("summary.tsv")),
                                      string_join_arena(arena, string8_list_to_slice(arena, summary), false)) && valid;
            valid = clang_suite_write(arena, path_join(arena, output, S8("receipt.txt")), receipt) && valid;
            bool archived = openssl_archive(arena, output, &stopped);
            valid = archived && valid;
        }
#else
        BUSTER_UNUSED(driver);
        string_print_error(S8("error: OpenSSL intake is supported only on Linux x86-64\n"));
#endif
    }
    else
    {
        string_print_error(S8("usage: test_openssl --self-test | CHECKOUT FRESH_OUTPUT IDE CLANG\n"));
    }
    ProcessResult result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}
