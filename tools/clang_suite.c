// Native build-driver integration for the external Clang corpus (#2280).
// clang_suite_inventory parses the complete immutable Git source ledger;
// clang_suite_main verifies source identity and owns fresh evidence publication.
// clang_suite_smoke.c owns the deliberately narrow preprocessing assertions.
// Source rows are not lit/GoogleTest case discovery or execution coverage.

#define BUSTER_CLANG_SUITE_COMMIT "85ac560262434c9ccfc0c183ec22d4138ed647fb"
#define BUSTER_CLANG_SUITE_VERSION "llvmorg-23.1.2"
#define BUSTER_CLANG_SUITE_FILES 31192
#define BUSTER_CLANG_SUITE_TIMEOUT_SECONDS 60

typedef struct ClangSuiteCommand ClangSuiteCommand;
struct ClangSuiteCommand
{
    ProcessResult result;
    String8 output;
    String8 error;
    u32 platform_status;
    bool timed_out;
    bool launch_failed;
    bool output_truncated;
    bool capture_failed;
    bool cleanup_failed;
};

typedef struct ClangSuiteInventory ClangSuiteInventory;
struct ClangSuiteInventory
{
    String8 manifest;
    u64 counts[5];
    u64 files;
};

BUSTER_GLOBAL_LOCAL ClangSuiteCommand clang_suite_command(Arena* arena, String8 directory, SliceString8 arguments)
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
    ClangSuiteCommand result = {0};
    run.spawn = process_run_spawn(arena, &run);
    result.launch_failed = !run.spawn.handle;
    if (result.launch_failed)
    {
        result.result = PROCESS_RESULT_NOT_EXISTENT;
    }
    else
    {
        ProcessWaitResult wait = os_process_wait_deadline(arena, run.spawn, BUSTER_CLANG_SUITE_TIMEOUT_SECONDS * 1000000);
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

BUSTER_GLOBAL_LOCAL String8 clang_suite_read(Arena* arena, String8 path)
{
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){.end_padding = 1});
    String8 result = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_write(Arena* arena, String8 path, String8 text)
{
    bool result = file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text));
    if (result)
    {
        result = string_equal(text, clang_suite_read(arena, path));
    }
    if (!result)
    {
        string_print(S8("error: Clang suite evidence write failed: {S8}\n"), path);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_sha_valid(String8 sha)
{
    bool valid = sha.length == 40;
    for (u64 i = 0; valid && i < sha.length; i += 1)
    {
        char8 c = sha.pointer[i];
        valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u64 clang_suite_scope(String8 path)
{
    String8 prefixes[] = {S8("clang/test/"), S8("clang/unittests/"), S8("clang/tools/scan-build-py/tests/"), S8("clang/bindings/python/tests/")};
    u64 result = BUSTER_ARRAY_LENGTH(prefixes) + 1;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(prefixes); i += 1)
    {
        if (string_starts_with_sequence(path, prefixes[i]))
        {
            result = i;
        }
    }
    if (string_equal(path, S8("clang/LICENSE.TXT")) || string_equal(path, S8("llvm/LICENSE.TXT")))
    {
        result = BUSTER_ARRAY_LENGTH(prefixes);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_safe_path(String8 path)
{
    bool valid = path.length != 0;
    u64 component = 0;
    for (u64 i = 0; valid && i <= path.length; i += 1)
    {
        if (i == path.length || path.pointer[i] == '/')
        {
            String8 part = string_slice(path, component, i);
            valid = part.length && !string_equal(part, S8(".")) && !string_equal(part, S8(".."));
            component = i + 1;
        }
        else
        {
            char8 c = path.pointer[i];
            valid = c != '\t' && c != '\n' && c != '\r' && c != '\0' && c != '\\';
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_inventory(Arena* arena, String8 raw, ClangSuiteInventory* inventory)
{
    *inventory = (ClangSuiteInventory){0};
    String8List rows = {0};
    String8 previous = {0};
    u64 cursor = 0;
    bool valid = raw.length != 0;
    string8_list_push(arena, &rows, S8("BUSTER_CLANG_SUITE_SOURCES_V1\nmode\tblob\tpath\trole\tstate\n"));
    while (valid && cursor < raw.length)
    {
        u64 end = cursor;
        while (end < raw.length && raw.pointer[end] != '\0')
        {
            end += 1;
        }
        String8 record = string_slice(raw, cursor, end);
        valid = end < raw.length && record.length > 53 && record.pointer[6] == ' ' &&
                string_equal(string_slice(record, 7, 11), S8("blob")) && record.pointer[11] == ' ' && record.pointer[52] == '\t';
        if (valid)
        {
            String8 mode = string_slice(record, 0, 6);
            String8 sha = string_slice(record, 12, 52);
            String8 path = string_slice(record, 53, record.length);
            u64 scope = clang_suite_scope(path);
            bool symlink = string_equal(mode, S8("120000"));
            valid = (symlink || string_equal(mode, S8("100644")) || string_equal(mode, S8("100755"))) &&
                    clang_suite_sha_valid(sha) && clang_suite_safe_path(path) && scope < BUSTER_ARRAY_LENGTH(inventory->counts);
            if (valid && previous.length)
            {
                u64 length = previous.length < path.length ? previous.length : path.length;
                int order = memcmp(previous.pointer, path.pointer, (size_t)length);
                valid = order < 0 || (order == 0 && previous.length < path.length);
            }
            if (valid)
            {
                String8 role = scope == 4 ? S8("license") : symlink ? S8("symlink") :
                               string_first_sequence(path, S8("/Inputs/")) != BUSTER_STRING_NO_MATCH ? S8("support-input") :
                               string_ends_with_sequence(path, S8("lit.cfg.py")) || string_ends_with_sequence(path, S8("lit.local.cfg")) ||
                               string_ends_with_sequence(path, S8("lit.local.cfg.py")) ? S8("configuration") : S8("suite-source");
                string8_list_push(arena, &rows, string_format(arena, S8("{S8}\t{S8}\t{S8}\t{S8}\tunadapted\n"), mode, sha, path, role));
                inventory->counts[scope] += 1;
                inventory->files += 1;
                previous = path;
            }
        }
        cursor = end + 1;
    }
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(inventory->counts); i += 1)
    {
        valid = inventory->counts[i] != 0;
    }
    if (valid)
    {
        inventory->manifest = string_join_arena(arena, string8_list_to_slice(arena, rows), true);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_verify_checkout(Arena* arena, String8 checkout)
{
    String8 head_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("rev-parse"), S8("HEAD")};
    ClangSuiteCommand head = clang_suite_command(arena, checkout, (SliceString8)BUSTER_ARRAY_TO_SLICE(head_arguments));
    bool valid = head.result == PROCESS_RESULT_SUCCESS && string_equal(quickjs_trim_ascii_space(head.output), S8(BUSTER_CLANG_SUITE_COMMIT));
    if (valid)
    {
        String8 status_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("status"), S8("--porcelain=v1"), S8("--untracked-files=all"), S8("--ignored")};
        ClangSuiteCommand status = clang_suite_command(arena, checkout, (SliceString8)BUSTER_ARRAY_TO_SLICE(status_arguments));
        valid = status.result == PROCESS_RESULT_SUCCESS && !status.output.length;
    }
    if (!valid)
    {
        string_print(S8("error: Clang suite requires exact pin {S8} and a clean external checkout, including ignored/untracked files\n"), S8(BUSTER_CLANG_SUITE_COMMIT));
    }
    return valid;
}

// Existing POSIX realpath resolves aliases before the output/source boundary
// is checked. Windows GetFullPathName does not prove the reparse-point boundary;
// that platform's native suite execution stays explicitly pending (#2291).
BUSTER_GLOBAL_LOCAL String8 clang_suite_directory(Arena* arena, String8 path)
{
    String8 result = {0};
#if BUSTER_LINUX || BUSTER_MACOS
    result = os_path_absolute(arena, path, true);
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(path);
#endif
    return result;
}

#include "clang_suite_smoke.c"

#include "clang_suite_test.c"

BUSTER_GLOBAL_LOCAL ProcessResult clang_suite_main(Arena* arena, SliceString8 arguments)
{
    bool self_test = arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test"));
    bool smoke = arguments.length == 5 && string_equal(arguments.pointer[0], S8("--smoke"));
    bool inventory_only = arguments.length == 3 && string_equal(arguments.pointer[0], S8("--inventory"));
    bool valid = self_test || smoke || inventory_only;
    if (self_test)
    {
        valid = clang_suite_self_test(arena);
    }
    else if (valid)
    {
        String8 checkout = clang_suite_directory(arena, arguments.pointer[1]);
        String8 requested = os_path_absolute_lexical(arena, arguments.pointer[2], true);
        String8 parent = clang_suite_directory(arena, path_parent(arena, requested));
        String8 name = quickjs_basename(requested);
        String8 results = path_join(arena, parent, name);
        valid = checkout.length && parent.length && clang_suite_safe_path(name) && !string_equal(parent, checkout) &&
                !string_starts_with_sequence(parent, path_join(arena, checkout, S8("")));
        if (!valid)
        {
            string_print(S8("error: Clang suite needs an existing canonical POSIX checkout/parent and fresh output outside the checkout; Windows boundary validation is pending (#2291)\n"));
        }
        if (valid)
        {
            valid = clang_suite_verify_checkout(arena, checkout);
        }
        if (valid)
        {
            valid = summary_self_test_directory_claim(results) == SUMMARY_DIRECTORY_CLAIMED;
            if (!valid)
            {
                string_print(S8("error: Clang suite output must be a fresh directory: {S8}\n"), results);
            }
        }
        bool claimed = valid;
        ClangSuiteInventory inventory = {0};
        String8 raw_digest = {0};
        if (valid)
        {
            String8 git_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("ls-tree"), S8("-r"), S8("-z"), S8("--full-tree"),
                                      S8(BUSTER_CLANG_SUITE_COMMIT), S8("--"), S8("clang/test"), S8("clang/unittests"),
                                      S8("clang/tools/scan-build-py/tests"), S8("clang/bindings/python/tests"), S8("clang/LICENSE.TXT"), S8("llvm/LICENSE.TXT")};
            ClangSuiteCommand tree = clang_suite_command(arena, checkout, (SliceString8)BUSTER_ARRAY_TO_SLICE(git_arguments));
            valid = tree.result == PROCESS_RESULT_SUCCESS && clang_suite_inventory(arena, tree.output, &inventory);
            u64 counts[] = {30712, 410, 30, 38, 2};
            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(counts); i += 1)
            {
                valid = inventory.counts[i] == counts[i] && valid;
            }
            valid = inventory.files == BUSTER_CLANG_SUITE_FILES && valid;
            if (valid)
            {
                raw_digest = stage_object_sha256_bytes(arena, (u8*)tree.output.pointer, tree.output.length);
                valid = clang_suite_write(arena, path_join(arena, results, S8("git-ls-tree.bin")), tree.output) &&
                        clang_suite_write(arena, path_join(arena, results, S8("sources.tsv")), inventory.manifest);
            }
            else
            {
                string_print(S8("error: Clang suite source ledger is malformed or differs from its complete pinned census\n"));
            }
        }
        if (valid && smoke)
        {
            String8 ide = os_path_absolute_lexical(arena, arguments.pointer[3], true);
            String8 clang = os_path_absolute_lexical(arena, arguments.pointer[4], true);
            valid = ide.length && clang.length && clang_suite_smoke(arena, checkout, results, ide, clang);
        }
        if (claimed)
        {
            bool unchanged = clang_suite_verify_checkout(arena, checkout);
            valid = unchanged && valid;
            String8 receipt = string_format(arena, S8("BUSTER_CLANG_SUITE_RECEIPT_V1\nversion={S8}\ncommit={S8}\n"
                                                     "source_files={u64}\nsource_manifest_sha256={S8}\nraw_git_inventory_sha256={S8}\n"
                                                     "source_ledger_executed=0\nsmoke_requested={u32}\ncheckout_status_clean={u32}\nstatus={S8}\n"),
                                            S8(BUSTER_CLANG_SUITE_VERSION), S8(BUSTER_CLANG_SUITE_COMMIT), inventory.files,
                                            stage_object_sha256_bytes(arena, (u8*)inventory.manifest.pointer, inventory.manifest.length), raw_digest,
                                            (u32)smoke, (u32)unchanged, valid ? S8("pass") : S8("fail"));
            valid = clang_suite_write(arena, path_join(arena, results, S8("receipt.txt")), receipt) && valid;
        }
    }
    else
    {
        string_print(S8("usage: test_clang_suite --self-test | --inventory CHECKOUT FRESH_RESULTS | --smoke CHECKOUT FRESH_RESULTS ABSOLUTE_IDE ABSOLUTE_CLANG\n"));
    }
    ProcessResult result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}
