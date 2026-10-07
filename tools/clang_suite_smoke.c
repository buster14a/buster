// Bounded pristine Clang preprocessor slice, included by clang_suite.c.
// clang_suite_smoke owns two explicit upstream RUN adapters and preserves each
// compiler's independent output and status. clang_suite_smoke_plan accepts only
// their literal CHECK contract; clang_suite_smoke_self_test guards rejection.

#if BUSTER_LINUX || BUSTER_MACOS
#include <sys/stat.h>
#endif

typedef struct ClangSuiteSmokeCase ClangSuiteSmokeCase;
struct ClangSuiteSmokeCase
{
    String8 name;
    String8 path;
    String8 blob;
};

BUSTER_GLOBAL_LOCAL String8 clang_suite_smoke_trim(String8 text)
{
    while (text.length && (text.pointer[0] == ' ' || text.pointer[0] == '\t'))
    {
        text = string_slice(text, 1, text.length);
    }
    while (text.length && (text.pointer[text.length - 1] == ' ' || text.pointer[text.length - 1] == '\t' || text.pointer[text.length - 1] == '\r' || text.pointer[text.length - 1] == '\n'))
    {
        text.length -= 1;
    }
    return text;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_smoke_prefix(String8 text, String8 prefix)
{
    bool result = text.length >= prefix.length && string_equal(string_slice(text, 0, prefix.length), prefix);
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_smoke_contains(String8 text, String8 pattern)
{
    bool result = false;
    if (pattern.length && pattern.length <= text.length)
    {
        for (u64 i = 0; !result && i <= text.length - pattern.length; i += 1)
        {
            result = string_equal(string_slice(text, i, i + pattern.length), pattern);
        }
    }
    return result;
}

// FileCheck's default literal matching folds horizontal whitespace. Preserve
// newlines so a literal cannot silently match tokens on different lines.
BUSTER_GLOBAL_LOCAL String8 clang_suite_smoke_canonicalize(Arena* arena, String8 text)
{
    char8* bytes = arena_allocate(arena, char8, text.length + 1);
    u64 length = 0;
    bool horizontal_space = false;
    for (u64 i = 0; i < text.length; i += 1)
    {
        char8 byte = text.pointer[i];
        if (byte == ' ' || byte == '\t')
        {
            if (!horizontal_space)
            {
                bytes[length++] = ' ';
            }
            horizontal_space = true;
        }
        else
        {
            horizontal_space = false;
            if (!(byte == '\r' && i + 1 < text.length && text.pointer[i + 1] == '\n'))
            {
                bytes[length++] = byte;
            }
        }
    }
    bytes[length] = 0;
    String8 result = {.pointer = bytes, .length = length};
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_smoke_plan(Arena* arena, String8 source, SliceString8* checks_out)
{
    enum
    {
        CLANG_SUITE_SMOKE_CHECK_CAPACITY = 64,
    };
    String8* checks = arena_allocate(arena, String8, CLANG_SUITE_SMOKE_CHECK_CAPACITY);
    u64 check_count = 0;
    u64 run_count = 0;
    bool result = source.length != 0;
    String8 remaining = source;
    String8 line = {0};
    while (result && text_next_line(&remaining, &line))
    {
        for (u64 i = 0; result && i < line.length; i += 1)
        {
            result = line.pointer[i] != 0;
        }
        line = clang_suite_smoke_trim(line);
        if (result && clang_suite_smoke_prefix(line, S8("// RUN:")))
        {
            run_count += 1;
            result = run_count == 1 &&
                     (string_equal(line, S8("// RUN: %clang_cc1 -E %s | FileCheck %s")) ||
                      string_equal(line, S8("// RUN: %clang_cc1 %s -E | FileCheck %s")));
        }
        else if (result && clang_suite_smoke_prefix(line, S8("// CHECK:")))
        {
            String8 pattern = clang_suite_smoke_trim(string_slice(line, 9, line.length));
            result = pattern.length != 0 && check_count < CLANG_SUITE_SMOKE_CHECK_CAPACITY &&
                     !clang_suite_smoke_contains(pattern, S8("{{")) && !clang_suite_smoke_contains(pattern, S8("[["));
            if (result)
            {
                checks[check_count++] = clang_suite_smoke_canonicalize(arena, pattern);
            }
        }
        else if (result &&
                 (clang_suite_smoke_prefix(line, S8("// CHECK-")) || clang_suite_smoke_prefix(line, S8("// REQUIRES:")) ||
                  clang_suite_smoke_prefix(line, S8("// UNSUPPORTED:")) || clang_suite_smoke_prefix(line, S8("// XFAIL:")) ||
                  clang_suite_smoke_prefix(line, S8("// DEFINE:")) || clang_suite_smoke_prefix(line, S8("// REDEFINE:"))))
        {
            result = false;
        }
    }
    result = result && run_count == 1 && check_count != 0;
    if (result)
    {
        *checks_out = (SliceString8){.pointer = checks, .length = check_count};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_smoke_check(Arena* arena, String8 source, String8 output)
{
    SliceString8 checks = {0};
    bool result = clang_suite_smoke_plan(arena, source, &checks);
    if (result)
    {
        String8 canonical = clang_suite_smoke_canonicalize(arena, output);
        u64 cursor = 0;
        for (u64 check_i = 0; result && check_i < checks.length; check_i += 1)
        {
            String8 pattern = checks.pointer[check_i];
            bool found = false;
            if (pattern.length <= canonical.length - cursor)
            {
                for (u64 i = cursor; !found && i <= canonical.length - pattern.length; i += 1)
                {
                    if (string_equal(string_slice(canonical, i, i + pattern.length), pattern))
                    {
                        cursor = i + pattern.length;
                        found = true;
                    }
                }
            }
            result = found;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_smoke_command_passed(Arena* arena, String8 source, ClangSuiteCommand command)
{
    bool result = command.result == PROCESS_RESULT_SUCCESS && !command.launch_failed && !command.timed_out &&
                  !command.capture_failed && !command.output_truncated && !command.cleanup_failed &&
                  clang_suite_smoke_check(arena, source, command.output);
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_smoke_regular_file(Arena* arena, String8 path)
{
    String8 terminated = string_format(arena, S8("{S8}"), path);
    bool result;
#if BUSTER_WINDOWS
    String16 wide = string16_from_string8(arena, terminated, true);
    DWORD attributes = GetFileAttributesW(wide.pointer);
    result = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
#elif BUSTER_LINUX || BUSTER_MACOS
    struct stat status;
    result = lstat((const char*)terminated.pointer, &status) == 0 && S_ISREG(status.st_mode);
#else
    BUSTER_UNUSED(terminated);
    result = false;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_suite_smoke_status(ClangSuiteCommand command)
{
    String8 result;
    if (command.launch_failed)
    {
        result = S8("launch-failed");
    }
    else if (command.timed_out)
    {
        result = S8("timeout");
    }
    else if (command.capture_failed || command.output_truncated || command.cleanup_failed)
    {
        result = S8("incomplete");
    }
    else if (command.result == PROCESS_RESULT_CRASH)
    {
        result = S8("crash");
    }
    else
    {
        result = command.result == PROCESS_RESULT_SUCCESS ? S8("success") : S8("failure");
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_smoke_observe(Arena* arena, String8 results, ClangSuiteSmokeCase test, String8 source,
                                                  String8 compiler_name, String8 compiler, SliceString8 arguments)
{
    ClangSuiteCommand command = clang_suite_command(arena, S8("."), arguments);
    bool checked = clang_suite_smoke_command_passed(arena, source, command);
    String8 stem = path_join(arena, results, string_format(arena, S8("{S8}.{S8}"), test.name, compiler_name));
    String8 receipt = string_format(arena,
        S8("CLANG_SUITE_SMOKE_CASE version=1 upstream={S8} path={S8} blob={S8} compiler={S8} executable={S8} "
           "process_status={S8} process_result={u32} platform_status={u32} timed_out={u32} launch_failed={u32} "
           "capture_failed={u32} output_truncated={u32} cleanup_failed={u32} literal_checks={S8}\n"),
        S8(BUSTER_CLANG_SUITE_COMMIT), test.path, test.blob, compiler_name, compiler, clang_suite_smoke_status(command),
        (u32)command.result, command.platform_status, (u32)command.timed_out, (u32)command.launch_failed,
        (u32)command.capture_failed, (u32)command.output_truncated, (u32)command.cleanup_failed, checked ? S8("pass") : S8("fail"));
    bool output_written = clang_suite_write(arena, string_format(arena, S8("{S8}.stdout"), stem), command.output);
    bool error_written = clang_suite_write(arena, string_format(arena, S8("{S8}.stderr"), stem), command.error);
    bool receipt_written = clang_suite_write(arena, string_format(arena, S8("{S8}.receipt"), stem), receipt);
    string_print(S8("{S8}"), receipt);
    bool result = checked && output_written && error_written && receipt_written;
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_smoke(Arena* arena, String8 checkout, String8 results, String8 ide, String8 clang)
{
    ClangSuiteSmokeCase tests[] = {
        {S8("macro-paste-simple"), S8("clang/test/Preprocessor/macro_paste_simple.c"), S8("0e62ba46dc96d8336b1eeac102147a26c48382cf")},
        {S8("macro-paste-hashhash"), S8("clang/test/Preprocessor/macro_paste_hashhash.c"), S8("f4b03bef2e16703d32cbacfdc482ecb490d1a3cf")},
    };
    bool result = true;
    u64 attempted = 0;
    u64 passed = 0;
    for (u64 test_i = 0; test_i < BUSTER_ARRAY_LENGTH(tests); test_i += 1)
    {
        ClangSuiteSmokeCase test = tests[test_i];
        String8 path = path_join(arena, checkout, test.path);
        bool regular = clang_suite_smoke_regular_file(arena, path);
        String8 git_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("hash-object"), S8("--no-filters"), S8("--"), test.path};
        ClangSuiteCommand hash = {.result = PROCESS_RESULT_FAILED};
        if (regular)
        {
            hash = clang_suite_command(arena, checkout, (SliceString8)BUSTER_ARRAY_TO_SLICE(git_arguments));
        }
        String8 source = regular ? clang_suite_read(arena, path) : (String8){0};
        SliceString8 checks = {0};
        bool pristine = regular && hash.result == PROCESS_RESULT_SUCCESS && !hash.launch_failed && !hash.timed_out &&
                        !hash.capture_failed && !hash.output_truncated && !hash.cleanup_failed &&
                        string_equal(clang_suite_smoke_trim(hash.output), test.blob) && clang_suite_smoke_plan(arena, source, &checks);
        String8 hash_stem = path_join(arena, results, string_format(arena, S8("{S8}.source-hash"), test.name));
        String8 hash_receipt = string_format(arena,
            S8("CLANG_SUITE_SMOKE_SOURCE version=1 path={S8} expected_blob={S8} regular_file={u32} process_status={S8} "
               "process_result={u32} platform_status={u32} verified={u32}\n"),
            test.path, test.blob, (u32)regular, regular ? clang_suite_smoke_status(hash) : S8("not-run"),
            (u32)hash.result, hash.platform_status, (u32)pristine);
        bool hash_output_written = clang_suite_write(arena, string_format(arena, S8("{S8}.stdout"), hash_stem), hash.output);
        bool hash_error_written = clang_suite_write(arena, string_format(arena, S8("{S8}.stderr"), hash_stem), hash.error);
        bool hash_receipt_written = clang_suite_write(arena, string_format(arena, S8("{S8}.receipt"), hash_stem), hash_receipt);
        pristine = pristine && hash_output_written && hash_error_written && hash_receipt_written;
        if (!pristine)
        {
            string_print(S8("error: Clang suite smoke requires regular pristine fixture {S8} blob={S8} and its supported original RUN/CHECK contract\n"), test.path, test.blob);
            result = false;
        }
        else
        {
            String8 clang_arguments[] = {clang, S8("-cc1"), S8("-E"), path};
            String8 ide_arguments[] = {ide, S8("cc"), S8("-E"), path};
            bool reference_passed = clang_suite_smoke_observe(arena, results, test, source, S8("clang"), clang, (SliceString8)BUSTER_ARRAY_TO_SLICE(clang_arguments));
            bool buster_passed = clang_suite_smoke_observe(arena, results, test, source, S8("buster"), ide, (SliceString8)BUSTER_ARRAY_TO_SLICE(ide_arguments));
            attempted += 1;
            bool case_passed = reference_passed && buster_passed;
            passed += (u64)case_passed;
            result = case_passed && result;
        }
    }
    String8 receipt = string_format(arena, S8("CLANG_SUITE_SMOKE version=1 upstream={S8} selected={u64} attempted={u64} passed={u64} status={S8} scope=two-pristine-C-preprocessor-tests\n"),
                                    S8(BUSTER_CLANG_SUITE_COMMIT), (u64)BUSTER_ARRAY_LENGTH(tests), attempted, passed, result ? S8("pass") : S8("fail"));
    bool summary_written = clang_suite_write(arena, path_join(arena, results, S8("smoke-summary.txt")), receipt);
    string_print(S8("{S8}"), receipt);
    result = summary_written && result;
    return result;
}
