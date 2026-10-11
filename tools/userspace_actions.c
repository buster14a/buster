// Compiler-action evidence for opt-in userspace qualification (#3179).
// userspace_inventory reads immutable per-invocation receipts, publishes
// role/status counts, and checks candidate coverage against the complete
// independent reference build. Failed optional probes remain visible.

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
        bool relocatable = false;
        String8 output = {0};
        String8 source = {0};
        for (u64 i = 7; i < arguments.length; i += 1)
        {
            String8 arg = arguments.pointer[i];
            compile = compile || string_equal(arg, S8("-c"));
            preprocess = preprocess || string_equal(arg, S8("-E"));
            relocatable = relocatable || string_equal(arg, S8("-r")) || string_equal(arg, S8("-Wl,-r"));
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
        bool link = !compile && !preprocess && !query && !relocatable;
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
                                       host ? S8("external-host-generator") : target_c ? S8("target-c") : preprocess || query ? S8("external-generator") : link ? S8("external-link") : relocatable ? S8("external-relocatable-link") : S8("compiler-probe"),
                                       buster ? S8("buster") : S8("clang"), source, source_hash, output, object_hash,
                                       ran.result == PROCESS_RESULT_SUCCESS && valid ? S8("pass") : S8("fail"));
        valid = clang_suite_write(arena, path_join(arena, action, S8("record.txt")), record) && valid;
        os_file_write(os_get_standard_stream(STANDARD_STREAM_OUTPUT), BUSTER_SLICE_TO_BYTE_SLICE(ran.output));
        os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(ran.error));
        result = valid ? ran.result : PROCESS_RESULT_FAILED;
    }
    return result;
}

typedef struct UserspaceAction UserspaceAction;
struct UserspaceAction
{
    String8 source;
    String8 output;
    String8 hash;
    bool passed;
};

BUSTER_GLOBAL_LOCAL String8 userspace_record_value(String8 record, String8 key)
{
    String8 result = {0};
    u64 start = 0;
    for (u64 i = 0; i <= record.length; i += 1)
    {
        if (i == record.length || record.pointer[i] == '\n')
        {
            String8 line = string_slice(record, start, i);
            if (string_starts_with_sequence(line, key))
            {
                result = string_slice(line, key.length, line.length);
            }
            start = i + 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool userspace_inventory(Arena* arena, String8 directory, bool compare)
{
    String8 sides[] = {S8("reference"), S8("buster")};
    UserspaceAction* actions[2] = {0};
    u64 counts[2] = {0};
    bool valid = true;
    for (u64 side = 0; side < (compare ? 2 : 1); side += 1)
    {
        String8 root = path_join(arena, directory, string_format(arena, S8("{S8}-actions"), sides[side]));
        MuslDirectoryEntry* entries = 0;
        u64 count = 0;
        bool listed = musl_list_directory(arena, root, &entries, &count);
        valid = listed && valid;
        actions[side] = arena_allocate(arena, UserspaceAction, count ? count : 1);
        u64 passed = 0;
        u64 failed = 0;
        u64 other = 0;
        String8List inventory = {0};
        for (u64 i = 0; listed && i < count; i += 1)
        {
            MuslDirectoryEntry entry = entries[i];
            if (entry.is_directory && string_starts_with_sequence(entry.name, S8("action-")))
            {
                String8 record = clang_suite_read(arena, path_join(arena, path_join(arena, root, entry.name), S8("record.txt")));
                String8 role = userspace_record_value(record, S8("role="));
                String8 status = userspace_record_value(record, S8("status="));
                bool pass = string_equal(status, S8("pass"));
                valid = record.length && role.length && (pass || string_equal(status, S8("fail"))) && valid;
                string8_list_push(arena, &inventory, string_format(arena, S8("action={S8}\n{S8}\n"), entry.name, record));
                if (string_equal(role, S8("target-c")))
                {
                    UserspaceAction action = {
                        .source = userspace_record_value(record, S8("source=")),
                        .output = userspace_record_value(record, S8("output=")),
                        .hash = userspace_record_value(record, S8("object_sha256=")),
                        .passed = pass,
                    };
                    valid = action.source.length && action.output.length && (!pass || action.hash.length == 64) && valid;
                    actions[side][counts[side]++] = action;
                    passed += pass;
                    failed += !pass;
                }
                else
                {
                    other += 1;
                }
            }
        }
        valid = counts[side] && valid;
        String8 summary = string_format(arena, S8("compiler={S8}\n"
                                                 "target_c_attempted={u64}\ntarget_c_pass={u64}\ntarget_c_fail={u64}\n"
                                                 "other_attempted={u64}\nupstream_tests=incomplete\n"),
                                        sides[side], counts[side], passed, failed, other);
        string8_list_push(arena, &inventory, summary);
        valid = clang_suite_write(arena, path_join(arena, root, S8("inventory.txt")),
                                  string_join_arena(arena, string8_list_to_slice(arena, inventory), false)) && valid;
        string_print(S8("USERSPACE_ACTIONS compiler={S8} attempted={u64} pass={u64} fail={u64} other={u64}\n"),
                     sides[side], counts[side], passed, failed, other);
    }
    if (compare)
    {
        u64 missing = 0;
        u64 unmatched = 0;
        u64 passed = 0;
        String8List coverage = {0};
        // Output names are configuration-stable even when generated source
        // arguments contain the side's absolute build directory.
        bool* used = arena_allocate(arena, bool, counts[1] ? counts[1] : 1);
        for (u64 i = 0; i < counts[1]; i += 1)
        {
            used[i] = false;
        }
        for (u64 i = 0; i < counts[0]; i += 1)
        {
            UserspaceAction required = actions[0][i];
            bool found = false;
            for (u64 j = 0; !found && j < counts[1]; j += 1)
            {
                if (!used[j] && string_equal(required.output, actions[1][j].output))
                {
                    used[j] = true;
                    found = true;
                    passed += actions[1][j].passed;
                    string8_list_push(arena, &coverage, string_format(arena, S8("{S8} {S8}\n"), required.output,
                                                                     actions[1][j].passed ? S8("pass") : S8("fail")));
                }
            }
            if (!found)
            {
                missing += 1;
                string8_list_push(arena, &coverage, string_format(arena, S8("{S8} not-attempted\n"), required.output));
            }
        }
        for (u64 i = 0; i < counts[1]; i += 1)
        {
            unmatched += !used[i];
        }
        String8 summary = string_format(arena, S8("required={u64}\nattempted={u64}\npass={u64}\nfail={u64}\n"
                                                 "not_attempted={u64}\nunmatched={u64}\n"),
                                        counts[0], counts[0] - missing, passed, counts[0] - missing - passed, missing, unmatched);
        string8_list_push(arena, &coverage, summary);
        valid = clang_suite_write(arena, path_join(arena, directory, S8("coverage.txt")),
                                  string_join_arena(arena, string8_list_to_slice(arena, coverage), false)) && valid;
        string_print(S8("USERSPACE_COVERAGE required={u64} attempted={u64} pass={u64} fail={u64} not_attempted={u64} unmatched={u64}\n"),
                     counts[0], counts[0] - missing, passed, counts[0] - missing - passed, missing, unmatched);
        valid = !missing && !unmatched && passed == counts[0] && valid;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool userspace_pack(Arena* arena, String8 directory, String8 musl_output)
{
    // upload-artifact treats paths case-insensitively. Preserve the exact
    // Linux UAPI and musl object names inside tar streams instead.
    String8 roots[] = {path_join(arena, directory, S8("uapi")), musl_output};
    String8 names[] = {S8("uapi.tar.gz"), S8("musl.tar.gz")};
    bool valid = true;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(roots); i += 1)
    {
        if (roots[i].length && path_exists(arena, roots[i]))
        {
            String8 arguments[] = {S8("tar"), S8("-czf"), path_join(arena, directory, names[i]), S8("-C"),
                                   path_parent(arena, roots[i]), quickjs_basename(roots[i])};
            ClangSuiteCommand ran = userspace_command(arena, S8("."), (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), USERSPACE_COMMAND_SECONDS);
            valid = ran.result == PROCESS_RESULT_SUCCESS && valid;
            os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(ran.error));
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool userspace_headers(Arena* arena, String8 directory)
{
    enum { MAX_DIRECTORIES = 4096 };
    String8* pending = arena_allocate(arena, String8, MAX_DIRECTORIES);
    u64 count = 1;
    u64 cursor = 0;
    u64 headers = 0;
    pending[0] = S8("uapi");
    String8List inventory = {0};
    bool valid = true;
    while (valid && cursor < count)
    {
        String8 relative = pending[cursor++];
        MuslDirectoryEntry* entries = 0;
        u64 entry_count = 0;
        valid = musl_list_directory(arena, path_join(arena, directory, relative), &entries, &entry_count);
        for (u64 i = 0; valid && i < entry_count; i += 1)
        {
            String8 path = path_join(arena, relative, entries[i].name);
            if (entries[i].is_directory)
            {
                valid = count < MAX_DIRECTORIES;
                if (valid)
                {
                    pending[count++] = path;
                }
            }
            else
            {
                ByteSlice bytes = file_read(arena, path_join(arena, directory, path), (FileReadOptions){0});
                valid = bytes.pointer && bytes.length;
                if (valid)
                {
                    String8 text = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
                    String8 license = S8("unverified: inspect retained header and package copyright");
                    u64 start = string_first_sequence(text, S8("SPDX-License-Identifier:"));
                    if (start != BUSTER_STRING_NO_MATCH)
                    {
                        u64 end = start;
                        while (end < text.length && text.pointer[end] != '\n')
                        {
                            end += 1;
                        }
                        license = string_slice(text, start, end);
                    }
                    string8_list_push(arena, &inventory, string_format(arena, S8("path={S8}\nsha256={S8}\nlicense_evidence={S8}\n\n"),
                                                                     path, stage_object_sha256_bytes(arena, bytes.pointer, bytes.length), license));
                    headers += 1;
                }
            }
        }
    }
    valid = clang_suite_write(arena, path_join(arena, directory, S8("uapi-inventory.txt")),
                              string_join_arena(arena, string8_list_to_slice(arena, inventory), false)) && valid;
    String8 kernel_arguments[] = {S8("uname"), S8("-srmo")};
    ClangSuiteCommand kernel = userspace_command(arena, S8("."), (SliceString8)BUSTER_ARRAY_TO_SLICE(kernel_arguments), 60);
    valid = clang_suite_write(arena, path_join(arena, directory, S8("kernel.txt")), kernel.output) &&
            kernel.result == PROCESS_RESULT_SUCCESS && valid;
    String8 package_arguments[] = {S8("dpkg-query"), S8("-W"), S8("linux-libc-dev")};
    ClangSuiteCommand package = userspace_command(arena, S8("."), (SliceString8)BUSTER_ARRAY_TO_SLICE(package_arguments), 60);
    String8 package_directory = path_join(arena, directory, S8("uapi-package"));
    make_directory_recursive(arena, package_directory);
    valid = userspace_save_command(arena, package_directory, package) && valid;
    String8 copyright = clang_suite_read(arena, S8("/usr/share/doc/linux-libc-dev/copyright"));
    if (copyright.length)
    {
        valid = clang_suite_write(arena, path_join(arena, package_directory, S8("copyright.txt")), copyright) && valid;
    }
    // Non-Debian hosts retain the failed package query as unverified metadata.
    string_print(S8("USERSPACE_HEADERS files={u64} kernel={S8} package_status={S8} status={S8}\n"),
                 headers, musl_first_line(kernel.output),
                 package.result == PROCESS_RESULT_SUCCESS ? S8("recorded") : S8("unverified"), valid ? S8("recorded") : S8("fail"));
    return valid;
}

BUSTER_GLOBAL_LOCAL bool userspace_seed(Arena* arena, String8 build, String8 seed)
{
    enum { MAX_SETTINGS = 128 };
    String8 settings[MAX_SETTINGS];
    u64 setting_count = 0;
    u64 start = 0;
    bool valid = true;
    for (u64 i = 0; valid && i <= seed.length; i += 1)
    {
        if (i == seed.length || seed.pointer[i] == '\n')
        {
            String8 line = string_slice(seed, start, i);
            if (line.length)
            {
                valid = setting_count < MAX_SETTINGS && string_first_sequence(line, S8("=")) != BUSTER_STRING_NO_MATCH;
                if (valid)
                {
                    settings[setting_count++] = line;
                }
            }
            start = i + 1;
        }
    }
    String8 base = clang_suite_read(arena, path_join(arena, build, S8(".config")));
    valid = valid && base.length && setting_count;
    String8List lines = {0};
    start = 0;
    for (u64 i = 0; valid && i <= base.length; i += 1)
    {
        if (i == base.length || base.pointer[i] == '\n')
        {
            String8 line = string_slice(base, start, i);
            bool replaced = false;
            for (u64 j = 0; !replaced && j < setting_count; j += 1)
            {
                u64 equal = string_first_sequence(settings[j], S8("="));
                String8 key = string_slice(settings[j], 0, equal);
                replaced = string_starts_with_sequence(line, string_format(arena, S8("{S8}="), key)) ||
                           string_equal(line, string_format(arena, S8("# {S8} is not set"), key));
            }
            if (!replaced && i < base.length)
            {
                string8_list_push(arena, &lines, string_format(arena, S8("{S8}\n"), line));
            }
            start = i + 1;
        }
    }
    string8_list_push(arena, &lines, seed);
    valid = clang_suite_write(arena, path_join(arena, build, S8("seed.config")),
                              string_join_arena(arena, string8_list_to_slice(arena, lines), false)) && valid;
    return valid;
}
