// Bounded pristine Clang Driver slice, included by clang_suite.c.
// clang_suite_driver owns the explicit catalogue: each upstream RUN line maps
// to one argv for the reference Clang and one for `ide cc`, plus a Buster
// expectation (pass, a named refusal, or a CHECK mismatch) and owning issue.
// clang_suite_driver_plan verifies the original RUN/CHECK contract literally;
// clang_suite_driver_verdict separates expected failures from unexpected
// passes and failures; clang_suite_driver_self_test guards both.

typedef enum ClangSuiteDriverExpectation
{
    CLANG_SUITE_DRIVER_EXPECT_PASS,
    CLANG_SUITE_DRIVER_EXPECT_REFUSAL,
    CLANG_SUITE_DRIVER_EXPECT_MISMATCH,
} ClangSuiteDriverExpectation;

typedef enum ClangSuiteDriverVerdict
{
    CLANG_SUITE_DRIVER_VERDICT_PASS,
    CLANG_SUITE_DRIVER_VERDICT_EXPECTED_FAIL,
    CLANG_SUITE_DRIVER_VERDICT_UNEXPECTED_PASS,
    CLANG_SUITE_DRIVER_VERDICT_FAIL,
    CLANG_SUITE_DRIVER_VERDICT_COUNT,
} ClangSuiteDriverVerdict;

typedef struct ClangSuiteDriverRun ClangSuiteDriverRun;
struct ClangSuiteDriverRun
{
    String8 run;
    String8 prefix;
    SliceString8 arguments;
    bool merge_error;
    ClangSuiteDriverExpectation expectation;
    String8 refusal;
    u32 issue;
};

typedef struct ClangSuiteDriverCase ClangSuiteDriverCase;
struct ClangSuiteDriverCase
{
    String8 name;
    String8 path;
    String8 blob;
    String8 unsupported;
    u64 run_offset;
    u64 run_count;
};

#define CLANG_SUITE_DRIVER_ARGUMENTS(...) {.pointer = (String8[]){__VA_ARGS__}, .length = sizeof((String8[]){__VA_ARGS__}) / sizeof(String8)}

BUSTER_GLOBAL_LOCAL String8 clang_suite_driver_verdict_name(ClangSuiteDriverVerdict verdict)
{
    String8 names[] = {S8("pass"), S8("expected-fail"), S8("unexpected-pass"), S8("fail")};
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == CLANG_SUITE_DRIVER_VERDICT_COUNT);
    String8 result = names[verdict];
    return result;
}

// FileCheck recognizes PREFIX: only after a non-word character; PREFIX- at
// that boundary would be a directive modifier this adapter does not implement.
BUSTER_GLOBAL_LOCAL u64 clang_suite_driver_directive(String8 line, String8 prefix, bool* modifier)
{
    u64 result = BUSTER_STRING_NO_MATCH;
    for (u64 i = 0; result == BUSTER_STRING_NO_MATCH && i + prefix.length < line.length; i += 1)
    {
        char8 before = i ? line.pointer[i - 1] : ' ';
        bool boundary = !((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') || (before >= '0' && before <= '9') ||
                          before == '_' || before == '-');
        if (boundary && string_equal(string_slice(line, i, i + prefix.length), prefix))
        {
            char8 next = line.pointer[i + prefix.length];
            if (next == ':')
            {
                result = i + prefix.length + 1;
            }
            else if (next == '-')
            {
                *modifier = true;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_driver_plan(Arena* arena, String8 source, ClangSuiteDriverCase test, ClangSuiteDriverRun const* runs, SliceString8* checks_out)
{
    enum
    {
        CLANG_SUITE_DRIVER_CHECK_CAPACITY = 64,
        CLANG_SUITE_DRIVER_PREFIX_CAPACITY = 8,
    };
    String8* patterns = arena_allocate(arena, String8, CLANG_SUITE_DRIVER_CHECK_CAPACITY);
    u64* owners = arena_allocate(arena, u64, CLANG_SUITE_DRIVER_CHECK_CAPACITY);
    String8 prefixes[CLANG_SUITE_DRIVER_PREFIX_CAPACITY] = {S8("CHECK")};
    u64 prefix_count = 1;
    bool result = source.length != 0 && test.run_count != 0 && test.run_count < CLANG_SUITE_DRIVER_PREFIX_CAPACITY;
    for (u64 run_i = 0; result && run_i < test.run_count; run_i += 1)
    {
        String8 prefix = runs[run_i].prefix;
        bool known = !prefix.length;
        for (u64 i = 0; !known && i < prefix_count; i += 1)
        {
            known = string_equal(prefixes[i], prefix);
        }
        if (!known)
        {
            prefixes[prefix_count++] = prefix;
        }
    }
    u64 pattern_count = 0;
    u64 run_count = 0;
    u64 unsupported_count = 0;
    bool continuing = false;
    String8 pending = {0};
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
            String8 part = clang_suite_smoke_trim(string_slice(line, 7, line.length));
            bool continues = part.length && part.pointer[part.length - 1] == '\\';
            if (continues)
            {
                part = clang_suite_smoke_trim(string_slice(part, 0, part.length - 1));
            }
            pending = continuing ? string_format(arena, S8("{S8} {S8}"), pending, part) : part;
            continuing = continues;
            if (!continuing)
            {
                result = run_count < test.run_count && string_equal(pending, runs[run_count].run);
                run_count += 1;
            }
        }
        else if (result && clang_suite_smoke_prefix(line, S8("// UNSUPPORTED:")))
        {
            unsupported_count += 1;
            result = !continuing && unsupported_count == 1 && test.unsupported.length &&
                     string_equal(clang_suite_smoke_trim(string_slice(line, 15, line.length)), test.unsupported);
        }
        else if (result)
        {
            String8 keywords[] = {S8("RUN:"), S8("UNSUPPORTED:"), S8("REQUIRES:"), S8("XFAIL:"), S8("DEFINE:"), S8("REDEFINE:"), S8("ALLOW_RETRIES:"), S8("END.")};
            result = !continuing;
            for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(keywords); i += 1)
            {
                result = !clang_suite_smoke_contains(line, keywords[i]);
            }
        }
        for (u64 prefix_i = 0; result && prefix_i < prefix_count; prefix_i += 1)
        {
            bool modifier = false;
            u64 at = clang_suite_driver_directive(line, prefixes[prefix_i], &modifier);
            result = !modifier;
            if (result && at != BUSTER_STRING_NO_MATCH)
            {
                String8 pattern = clang_suite_smoke_trim(string_slice(line, at, line.length));
                result = pattern.length != 0 && pattern_count < CLANG_SUITE_DRIVER_CHECK_CAPACITY &&
                         !clang_suite_smoke_contains(pattern, S8("{{")) && !clang_suite_smoke_contains(pattern, S8("[["));
                if (result)
                {
                    patterns[pattern_count] = pattern;
                    owners[pattern_count] = prefix_i;
                    pattern_count += 1;
                }
            }
        }
    }
    result = result && !continuing && run_count == test.run_count && unsupported_count == (u64)(test.unsupported.length != 0);
    for (u64 prefix_i = 0; result && prefix_i < prefix_count; prefix_i += 1)
    {
        bool used = false;
        for (u64 run_i = 0; run_i < test.run_count; run_i += 1)
        {
            used = used || string_equal(runs[run_i].prefix, prefixes[prefix_i]);
        }
        for (u64 i = 0; result && !used && i < pattern_count; i += 1)
        {
            result = owners[i] != prefix_i;
        }
    }
    for (u64 run_i = 0; result && run_i < test.run_count; run_i += 1)
    {
        String8* checks = arena_allocate(arena, String8, pattern_count + 1);
        u64 check_count = 0;
        for (u64 i = 0; i < pattern_count; i += 1)
        {
            if (runs[run_i].prefix.length && string_equal(prefixes[owners[i]], runs[run_i].prefix))
            {
                checks[check_count++] = patterns[i];
            }
        }
        result = (check_count != 0) == (runs[run_i].prefix.length != 0);
        checks_out[run_i] = (SliceString8){.pointer = checks, .length = check_count};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_driver_match(Arena* arena, SliceString8 checks, String8 output)
{
    String8 canonical = clang_suite_smoke_canonicalize(arena, output, false);
    u64 cursor = 0;
    bool result = true;
    for (u64 check_i = 0; result && check_i < checks.length; check_i += 1)
    {
        String8 pattern = clang_suite_smoke_canonicalize(arena, checks.pointer[check_i], false);
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
    return result;
}

BUSTER_GLOBAL_LOCAL ClangSuiteDriverVerdict clang_suite_driver_verdict(Arena* arena, ClangSuiteDriverRun run, SliceString8 checks, ClangSuiteCommand command, bool reference)
{
    bool complete = !command.launch_failed && !command.timed_out && !command.capture_failed && !command.output_truncated && !command.cleanup_failed;
    String8 checked = run.merge_error ? string_format(arena, S8("{S8}{S8}"), command.output, command.error) : command.output;
    bool assertions = complete && command.result == PROCESS_RESULT_SUCCESS && clang_suite_driver_match(arena, checks, checked);
    ClangSuiteDriverVerdict result;
    if (reference || run.expectation == CLANG_SUITE_DRIVER_EXPECT_PASS)
    {
        result = assertions ? CLANG_SUITE_DRIVER_VERDICT_PASS : CLANG_SUITE_DRIVER_VERDICT_FAIL;
    }
    else if (assertions)
    {
        result = CLANG_SUITE_DRIVER_VERDICT_UNEXPECTED_PASS;
    }
    else if (run.expectation == CLANG_SUITE_DRIVER_EXPECT_REFUSAL)
    {
        bool refused = complete && command.result == PROCESS_RESULT_FAILED && clang_suite_smoke_contains(command.error, run.refusal);
        result = refused ? CLANG_SUITE_DRIVER_VERDICT_EXPECTED_FAIL : CLANG_SUITE_DRIVER_VERDICT_FAIL;
    }
    else
    {
        bool mismatched = complete && command.result == PROCESS_RESULT_SUCCESS;
        result = mismatched ? CLANG_SUITE_DRIVER_VERDICT_EXPECTED_FAIL : CLANG_SUITE_DRIVER_VERDICT_FAIL;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_suite_driver_expand(Arena* arena, String8 token, String8 source, String8 directory)
{
    String8List parts = {0};
    u64 start = 0;
    for (u64 i = 0; i + 1 < token.length; i += 1)
    {
        if (token.pointer[i] == '%' && (token.pointer[i + 1] == 's' || token.pointer[i + 1] == 'S'))
        {
            string8_list_push(arena, &parts, string_slice(token, start, i));
            string8_list_push(arena, &parts, token.pointer[i + 1] == 's' ? source : directory);
            start = i + 2;
            i += 1;
        }
    }
    string8_list_push(arena, &parts, string_slice(token, start, token.length));
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, parts), true);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_suite_driver_expectation_name(ClangSuiteDriverRun run)
{
    String8 result = run.expectation == CLANG_SUITE_DRIVER_EXPECT_PASS ? S8("pass") :
                     run.expectation == CLANG_SUITE_DRIVER_EXPECT_REFUSAL ? S8("refusal") : S8("check-mismatch");
    return result;
}

BUSTER_GLOBAL_LOCAL ClangSuiteDriverVerdict clang_suite_driver_observe(Arena* arena, String8 results, ClangSuiteDriverCase test, u64 run_i, ClangSuiteDriverRun run,
                                                                      SliceString8 checks, String8 compiler_name, String8 compiler, String8 path, bool reference, bool* written)
{
    String8 directory = path_parent(arena, path);
    u64 prefix_count = reference ? 1 : 2;
    String8* arguments = arena_allocate(arena, String8, run.arguments.length + prefix_count);
    arguments[0] = compiler;
    if (!reference)
    {
        arguments[1] = S8("cc");
    }
    for (u64 i = 0; i < run.arguments.length; i += 1)
    {
        arguments[prefix_count + i] = clang_suite_driver_expand(arena, run.arguments.pointer[i], path, directory);
    }
    SliceString8 argv = {.pointer = arguments, .length = run.arguments.length + prefix_count};
    ClangSuiteCommand command = clang_suite_command(arena, results, argv);
    ClangSuiteDriverVerdict verdict = clang_suite_driver_verdict(arena, run, checks, command, reference);
    String8 stem = path_join(arena, results, string_format(arena, S8("{S8}.run{u64}.{S8}"), test.name, run_i + 1, compiler_name));
    String8 receipt = string_format(arena,
        S8("CLANG_SUITE_DRIVER_RUN version=1 upstream={S8} path={S8} blob={S8} run={u64} compiler={S8} executable={S8} "
           "process_status={S8} process_result={u32} platform_status={u32} timed_out={u32} launch_failed={u32} "
           "capture_failed={u32} output_truncated={u32} cleanup_failed={u32} prefix={S8} literal_checks={u64} merged_stderr={u32} "
           "expectation={S8} issue={u32} verdict={S8}\n"),
        S8(BUSTER_CLANG_SUITE_COMMIT), test.path, test.blob, run_i + 1, compiler_name, compiler, clang_suite_smoke_status(command),
        (u32)command.result, command.platform_status, (u32)command.timed_out, (u32)command.launch_failed,
        (u32)command.capture_failed, (u32)command.output_truncated, (u32)command.cleanup_failed,
        run.prefix.length ? run.prefix : S8("status-only"), checks.length, (u32)run.merge_error,
        reference ? S8("pass") : clang_suite_driver_expectation_name(run), reference ? 0 : run.issue, clang_suite_driver_verdict_name(verdict));
    String8List argv_lines = {0};
    for (u64 i = 0; i < argv.length; i += 1)
    {
        string8_list_push(arena, &argv_lines, string_format(arena, S8("{S8}\n"), argv.pointer[i]));
    }
    bool argv_written = clang_suite_write(arena, string_format(arena, S8("{S8}.argv"), stem), string_join_arena(arena, string8_list_to_slice(arena, argv_lines), true));
    bool output_written = clang_suite_write(arena, string_format(arena, S8("{S8}.stdout"), stem), command.output);
    bool error_written = clang_suite_write(arena, string_format(arena, S8("{S8}.stderr"), stem), command.error);
    bool receipt_written = clang_suite_write(arena, string_format(arena, S8("{S8}.receipt"), stem), receipt);
    string_print(S8("{S8}"), receipt);
    *written = argv_written && output_written && error_written && receipt_written;
    return verdict;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_driver(Arena* arena, String8 checkout, String8 results, String8 ide, String8 clang)
{
    ClangSuiteDriverRun runs[] = {
        {S8("%clang -E %s %s | FileCheck %s"), S8("CHECK"), CLANG_SUITE_DRIVER_ARGUMENTS(S8("-E"), S8("%s"), S8("%s")), false, CLANG_SUITE_DRIVER_EXPECT_PASS, {0}, 0},
        {S8("%clang -w -std=c99 -trigraphs -std=gnu99 %s -E -o - | FileCheck -check-prefix=OVERRIDE %s"), S8("OVERRIDE"),
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("-w"), S8("-std=c99"), S8("-trigraphs"), S8("-std=gnu99"), S8("%s"), S8("-E"), S8("-o"), S8("-")),
         false, CLANG_SUITE_DRIVER_EXPECT_PASS, {0}, 0},
        {S8("%clang -w -std=c99 -ftrigraphs -std=gnu99 %s -E -o - | FileCheck -check-prefix=FOVERRIDE %s"), S8("FOVERRIDE"),
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("-w"), S8("-std=c99"), S8("-ftrigraphs"), S8("-std=gnu99"), S8("%s"), S8("-E"), S8("-o"), S8("-")),
         false, CLANG_SUITE_DRIVER_EXPECT_PASS, {0}, 0},
        {S8("%clang -target x86_64-unknown-unknown -nostdlibinc -ffreestanding -fsyntax-only %s"), {0},
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("-target"), S8("x86_64-unknown-unknown"), S8("-nostdlibinc"), S8("-ffreestanding"), S8("-fsyntax-only"), S8("%s")),
         false, CLANG_SUITE_DRIVER_EXPECT_REFUSAL, S8("unsupported target: x86_64-unknown-unknown"), 3421},
        {S8("%clang -target x86_64-apple-darwin11 -Werror -x c -x c -fsyntax-only %s"), {0},
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("-target"), S8("x86_64-apple-darwin11"), S8("-Werror"), S8("-x"), S8("c"), S8("-x"), S8("c"), S8("-fsyntax-only"), S8("%s")),
         false, CLANG_SUITE_DRIVER_EXPECT_REFUSAL, S8("unsupported target: x86_64-apple-darwin11"), 3421},
        {S8("%clang -working-directory=%S %S/working-directory-and-abs.c -fsyntax-only"), {0},
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("-working-directory=%S"), S8("%S/working-directory-and-abs.c"), S8("-fsyntax-only")),
         false, CLANG_SUITE_DRIVER_EXPECT_PASS, {0}, 0},
        {S8("%clang -print-target-triple --target=x86_64-linux-gnu 2>&1 | FileCheck %s"), S8("CHECK"),
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("-print-target-triple"), S8("--target=x86_64-linux-gnu")),
         true, CLANG_SUITE_DRIVER_EXPECT_PASS, {0}, 0},
        {S8("%clang --target=x86_64-linux-gnu -dumpmachine | FileCheck %s --check-prefix=X86_64"), S8("X86_64"),
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("--target=x86_64-linux-gnu"), S8("-dumpmachine")), false, CLANG_SUITE_DRIVER_EXPECT_MISMATCH, {0}, 3421},
        {S8("%clang --target=x86_64-redhat-linux -m32 -dumpmachine | FileCheck %s --check-prefix=X86_64_M32"), S8("X86_64_M32"),
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("--target=x86_64-redhat-linux"), S8("-m32"), S8("-dumpmachine")),
         false, CLANG_SUITE_DRIVER_EXPECT_MISMATCH, {0}, 3421},
        {S8("%clang --target=xxx-pc-freebsd -dumpmachine | FileCheck %s --check-prefix=FREEBSD"), S8("FREEBSD"),
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("--target=xxx-pc-freebsd"), S8("-dumpmachine")),
         false, CLANG_SUITE_DRIVER_EXPECT_REFUSAL, S8("unsupported target: xxx-pc-freebsd"), 3421},
        {S8("%clang -M -MG -include nonexistent-preinclude.h -std=c23 %s | FileCheck %s"), S8("CHECK"),
         CLANG_SUITE_DRIVER_ARGUMENTS(S8("-M"), S8("-MG"), S8("-include"), S8("nonexistent-preinclude.h"), S8("-std=c23"), S8("%s")),
         false, CLANG_SUITE_DRIVER_EXPECT_REFUSAL, S8("unsupported option: -M"), 2293},
    };
    ClangSuiteDriverCase tests[] = {
        {S8("preprocess-multiple"), S8("clang/test/Driver/preprocess-multiple.c"), S8("9f87d3d74fd3d1dc438d98c2a4db9f24bf1780df"), {0}, 0, 1},
        {S8("std-trigraph-override"), S8("clang/test/Driver/std-trigraph-override.c"), S8("e4b83ffcf823d9444d59e11a7cd5646bba9eded0"), S8("target={{.*-zos.*}}"), 1, 2},
        {S8("nostdlibinc"), S8("clang/test/Driver/nostdlibinc.c"), S8("5b6fab6a870b0be5c68bc1510c83053abea90292"), {0}, 3, 1},
        {S8("redundant-args"), S8("clang/test/Driver/redundant-args.c"), S8("7d00699c446dfb7e62148423f8c28dcd7d0f065e"), {0}, 4, 1},
        {S8("working-directory-and-abs"), S8("clang/test/Driver/working-directory-and-abs.c"), S8("6738c776d40cf772a2bedd2fd0139238627b870b"), {0}, 5, 1},
        {S8("print-target-triple"), S8("clang/test/Driver/print-target-triple.c"), S8("3b15f44d4d9c45107ee3719149e9558d08fcf59e"), {0}, 6, 1},
        {S8("dumpmachine"), S8("clang/test/Driver/dumpmachine.c"), S8("41dda5590eb8297bbc40ac7f87f4ca6061e81a74"), {0}, 7, 3},
        {S8("mg"), S8("clang/test/Driver/mg.c"), S8("b7458a08698d311aa996d3229dc55e18975eef09"), {0}, 10, 1},
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(runs) == 11);
    bool result = true;
    u64 attempted = 0;
    u64 reference_passed = 0;
    u64 verdicts[CLANG_SUITE_DRIVER_VERDICT_COUNT] = {0};
    for (u64 test_i = 0; test_i < BUSTER_ARRAY_LENGTH(tests); test_i += 1)
    {
        ClangSuiteDriverCase test = tests[test_i];
        String8 path = path_join(arena, checkout, test.path);
        bool regular = clang_suite_smoke_regular_file(arena, path);
        String8 git_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("hash-object"), S8("--no-filters"), S8("--"), test.path};
        ClangSuiteCommand hash = {.result = PROCESS_RESULT_FAILED};
        if (regular)
        {
            hash = clang_suite_command(arena, checkout, (SliceString8)BUSTER_ARRAY_TO_SLICE(git_arguments));
        }
        String8 source = regular ? clang_suite_read(arena, path) : (String8){0};
        SliceString8* checks = arena_allocate(arena, SliceString8, test.run_count);
        bool pristine = regular && hash.result == PROCESS_RESULT_SUCCESS && !hash.launch_failed && !hash.timed_out &&
                        !hash.capture_failed && !hash.output_truncated && !hash.cleanup_failed &&
                        string_equal(clang_suite_smoke_trim(hash.output), test.blob) &&
                        clang_suite_driver_plan(arena, source, test, runs + test.run_offset, checks);
        // Every catalogued UNSUPPORTED expression names z/OS, which a POSIX
        // Linux/macOS host running this harness never matches.
        String8 hash_stem = path_join(arena, results, string_format(arena, S8("{S8}.source-hash"), test.name));
        String8 hash_receipt = string_format(arena,
            S8("CLANG_SUITE_DRIVER_SOURCE version=1 path={S8} expected_blob={S8} regular_file={u32} process_status={S8} "
               "process_result={u32} platform_status={u32} runs={u64} unsupported={S8} unsupported_matches_host=0 verified={u32}\n"),
            test.path, test.blob, (u32)regular, regular ? clang_suite_smoke_status(hash) : S8("not-run"),
            (u32)hash.result, hash.platform_status, test.run_count, test.unsupported.length ? test.unsupported : S8("none"), (u32)pristine);
        bool hash_output_written = clang_suite_write(arena, string_format(arena, S8("{S8}.stdout"), hash_stem), hash.output);
        bool hash_error_written = clang_suite_write(arena, string_format(arena, S8("{S8}.stderr"), hash_stem), hash.error);
        bool hash_receipt_written = clang_suite_write(arena, string_format(arena, S8("{S8}.receipt"), hash_stem), hash_receipt);
        pristine = pristine && hash_output_written && hash_error_written && hash_receipt_written;
        if (!pristine)
        {
            string_print(S8("error: Clang suite driver requires regular pristine fixture {S8} blob={S8} and its catalogued original RUN/CHECK contract\n"), test.path, test.blob);
            result = false;
        }
        for (u64 run_i = 0; pristine && run_i < test.run_count; run_i += 1)
        {
            ClangSuiteDriverRun run = runs[test.run_offset + run_i];
            bool reference_written = false;
            bool buster_written = false;
            ClangSuiteDriverVerdict reference = clang_suite_driver_observe(arena, results, test, run_i, run, checks[run_i], S8("clang"), clang, path, true, &reference_written);
            ClangSuiteDriverVerdict buster = clang_suite_driver_observe(arena, results, test, run_i, run, checks[run_i], S8("buster"), ide, path, false, &buster_written);
            attempted += 1;
            reference_passed += (u64)(reference == CLANG_SUITE_DRIVER_VERDICT_PASS);
            verdicts[buster] += 1;
            result = reference_written && buster_written && reference == CLANG_SUITE_DRIVER_VERDICT_PASS &&
                     (buster == CLANG_SUITE_DRIVER_VERDICT_PASS || buster == CLANG_SUITE_DRIVER_VERDICT_EXPECTED_FAIL) && result;
        }
    }
    String8 receipt = string_format(arena,
        S8("CLANG_SUITE_DRIVER version=1 upstream={S8} selected_cases={u64} selected_runs={u64} attempted_runs={u64} clang_passed={u64} "
           "buster_passed={u64} buster_expected_fail={u64} buster_unexpected_pass={u64} buster_failed={u64} status={S8} scope=pristine-clang-driver-literal-tests\n"),
        S8(BUSTER_CLANG_SUITE_COMMIT), (u64)BUSTER_ARRAY_LENGTH(tests), (u64)BUSTER_ARRAY_LENGTH(runs), attempted, reference_passed,
        verdicts[CLANG_SUITE_DRIVER_VERDICT_PASS], verdicts[CLANG_SUITE_DRIVER_VERDICT_EXPECTED_FAIL],
        verdicts[CLANG_SUITE_DRIVER_VERDICT_UNEXPECTED_PASS], verdicts[CLANG_SUITE_DRIVER_VERDICT_FAIL], result ? S8("pass") : S8("fail"));
    bool summary_written = clang_suite_write(arena, path_join(arena, results, S8("driver-summary.txt")), receipt);
    string_print(S8("{S8}"), receipt);
    result = summary_written && result;
    return result;
}
