// Compiler-action evidence for opt-in userspace qualification (#3179).
// userspace_inventory reads immutable per-invocation receipts, publishes
// role/status counts, and checks candidate coverage against the complete
// independent reference build. Failed optional probes remain visible.

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
