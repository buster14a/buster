// Native Clang-corpus parser/checker negative controls, included after the
// production helpers by clang_suite.c. No external compiler work in self-tests.

BUSTER_GLOBAL_LOCAL bool clang_suite_smoke_self_test(Arena* arena)
{
    typedef struct ClangSuiteSmokeCheckTest ClangSuiteSmokeCheckTest;
    struct ClangSuiteSmokeCheckTest
    {
        String8 name;
        String8 source;
        String8 output;
        bool expected;
    };
    ClangSuiteSmokeCheckTest tests[] = {
        {S8("ordered"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha beta\n// CHECK: gamma\n"), S8("alpha\t  beta\r\ngamma\n"), true},
        {S8("same-line"), S8("// RUN: %clang_cc1 %s -E | FileCheck %s\n// CHECK: alpha\n// CHECK: beta\n"), S8("alpha beta"), true},
        {S8("substring"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha\n"), S8("before alpha after"), true},
        {S8("wrong-order"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha\n// CHECK: beta\n"), S8("beta alpha"), false},
        {S8("nonoverlap"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha\n// CHECK: alpha\n"), S8("alpha"), false},
        {S8("missing"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha\n"), S8("beta"), false},
        {S8("case"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha\n"), S8("Alpha"), false},
        {S8("line-boundary"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha beta\n"), S8("alpha\nbeta"), false},
        {S8("unknown-check"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha\n// CHECK-UNKNOWN: beta\n"), S8("alpha beta"), false},
        {S8("regex"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: {{alpha}}\n"), S8("alpha"), false},
        {S8("variable"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: [[NAME]]\n"), S8("alpha"), false},
        {S8("empty-check"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: \n"), S8("alpha"), false},
        {S8("missing-run"), S8("// CHECK: alpha\n"), S8("alpha"), false},
        {S8("extra-run"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha\n"), S8("alpha"), false},
        {S8("unsupported-run"), S8("// RUN: %clang_cc1 -E %s ; touch %t\n// CHECK: alpha\n"), S8("alpha"), false},
        {S8("requires"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// REQUIRES: feature\n// CHECK: alpha\n"), S8("alpha"), false},
        {S8("binary-source"), S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n\0// CHECK: alpha\n"), S8("alpha"), false},
        {S8("strict-spaces"), S8("// RUN: %clang_cc1 -E %s | FileCheck --strict-whitespace %s\n// CHECK: alpha  beta\n"), S8("alpha  beta\r\n"), true},
        {S8("strict-collapse-refused"), S8("// RUN: %clang_cc1 -E %s | FileCheck --strict-whitespace %s\n// CHECK: alpha  beta\n"), S8("alpha beta"), false},
        {S8("strict-tab-refused"), S8("// RUN: %clang_cc1 %s -E | FileCheck -strict-whitespace %s\n// CHECK: alpha beta\n"), S8("alpha\tbeta"), false},
        {S8("strict-tab-preserved"), S8("// RUN: %clang_cc1 %s -E | FileCheck -strict-whitespace %s\n// CHECK: alpha\tbeta\n"), S8("alpha\tbeta"), true},
        {S8("strict-late-run"), S8("// CHECK: alpha  beta\n// RUN: %clang_cc1 -E %s | FileCheck --strict-whitespace %s\n"), S8("alpha beta"), false},
        {S8("strict-line-boundary"), S8("// RUN: %clang_cc1 -E %s | FileCheck --strict-whitespace %s\n// CHECK: alpha beta\n"), S8("alpha\nbeta"), false},
        {S8("strict-order"), S8("// RUN: %clang_cc1 -E %s | FileCheck --strict-whitespace %s\n// CHECK: alpha\n// CHECK: beta\n"), S8("beta alpha"), false},
        {S8("strict-nonoverlap"), S8("// RUN: %clang_cc1 -E %s | FileCheck --strict-whitespace %s\n// CHECK: alpha\n// CHECK: alpha\n"), S8("alpha"), false},
        {S8("strict-regex-refused"), S8("// RUN: %clang_cc1 -E %s | FileCheck --strict-whitespace %s\n// CHECK: {{alpha}}\n"), S8("alpha"), false},
    };
    bool result = true;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(tests); i += 1)
    {
        ClangSuiteSmokeCheckTest test = tests[i];
        bool passed = clang_suite_smoke_check(arena, test.source, test.output) == test.expected;
        if (!passed)
        {
            string_print(S8("error: Clang suite smoke checker self-test {S8} failed\n"), test.name);
        }
        result = passed && result;
    }
    String8 source = S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n// CHECK: alpha\n");
    ClangSuiteCommand commands[] = {
        {.result = PROCESS_RESULT_SUCCESS, .output = S8("alpha")},
        {.result = PROCESS_RESULT_SUCCESS, .output = S8("beta")},
        {.result = PROCESS_RESULT_FAILED, .output = S8("alpha")},
        {.result = PROCESS_RESULT_CRASH, .output = S8("alpha")},
        {.result = PROCESS_RESULT_SUCCESS, .output = S8("alpha"), .launch_failed = true},
        {.result = PROCESS_RESULT_SUCCESS, .output = S8("alpha"), .timed_out = true},
        {.result = PROCESS_RESULT_SUCCESS, .output = S8("alpha"), .capture_failed = true},
        {.result = PROCESS_RESULT_SUCCESS, .output = S8("alpha"), .output_truncated = true},
        {.result = PROCESS_RESULT_SUCCESS, .output = S8("alpha"), .cleanup_failed = true},
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(commands); i += 1)
    {
        bool passed = clang_suite_smoke_command_passed(arena, source, commands[i]) == (i == 0);
        if (!passed)
        {
            string_print(S8("error: Clang suite smoke process self-test {u64} failed\n"), i);
        }
        result = passed && result;
    }
    String8 oversized = S8("// RUN: %clang_cc1 -E %s | FileCheck %s\n");
    for (u64 i = 0; i < 65; i += 1)
    {
        oversized = string_format(arena, S8("{S8}// CHECK: alpha\n"), oversized);
    }
    SliceString8 oversized_checks = {0};
    bool strict_whitespace = false;
    bool capacity_passed = !clang_suite_smoke_plan(arena, oversized, &oversized_checks, &strict_whitespace);
    if (!capacity_passed)
    {
        string_print(S8("error: Clang suite smoke check-capacity self-test failed\n"));
    }
    result = capacity_passed && result;
    string_print(S8("CLANG_SUITE_SMOKE_SELF_TEST cases={u64} status={S8}\n"),
                 (u64)(BUSTER_ARRAY_LENGTH(tests) + BUSTER_ARRAY_LENGTH(commands) + 1), result ? S8("pass") : S8("fail"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_self_test(Arena* arena)
{
    String8 sha = S8("0123456789012345678901234567890123456789");
    String8 paths[] = {S8("clang/LICENSE.TXT"), S8("clang/bindings/python/tests/test.py"), S8("clang/test/Inputs/header.h"),
                      S8("clang/test/lit.cfg.py"), S8("clang/tools/scan-build-py/tests/test.py"), S8("clang/unittests/test.cpp"), S8("llvm/LICENSE.TXT")};
    String8List records = {0};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(paths); i += 1)
    {
        string8_list_push(arena, &records, string_format(arena, S8("100644 blob {S8}\t{S8}"), sha, paths[i]));
        string8_list_push(arena, &records, (String8){.pointer = "\0", .length = 1});
    }
    String8 raw = string_join_arena(arena, string8_list_to_slice(arena, records), true);
    ClangSuiteInventory inventory;
    bool valid = clang_suite_inventory(arena, raw, &inventory) && inventory.files == 7 && inventory.counts[0] == 2 &&
                 inventory.counts[1] == 1 && inventory.counts[2] == 1 && inventory.counts[3] == 1 && inventory.counts[4] == 2;
    // Mutate the otherwise complete valid ledger so a missing-root failure
    // cannot mask an accidentally accepted malformed record.
    u64 offsets[] = {0, 7, 12, 52};
    char8 replacements[] = {'2', 'x', 'z', '\n'};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(offsets); i += 1)
    {
        String8 broken = string_duplicate_arena(arena, raw, true);
        broken.pointer[offsets[i]] = replacements[i];
        valid = !clang_suite_inventory(arena, broken, &inventory) && valid;
    }
    valid = !clang_suite_inventory(arena, S8(""), &inventory) &&
            !clang_suite_inventory(arena, string_slice(raw, 0, raw.length - 1), &inventory) && valid;
    String8 unsafe[] = {S8("clang/test/Inputs/../header.h"), S8("clang/test/Inputs/heade\tr.h"), S8("clang/test/Inputs/heade\nr.h"),
                       S8("clanf/test/Inputs/header.h"), S8("clang/bindings/python/tests/test.py")};
    for (u64 mutation_i = 0; mutation_i < BUSTER_ARRAY_LENGTH(unsafe); mutation_i += 1)
    {
        String8List changed = {0};
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(paths); i += 1)
        {
            String8 path = i == 2 ? unsafe[mutation_i] : paths[i];
            string8_list_push(arena, &changed, string_format(arena, S8("100644 blob {S8}\t{S8}"), sha, path));
            string8_list_push(arena, &changed, (String8){.pointer = "\0", .length = 1});
        }
        String8 broken = string_join_arena(arena, string8_list_to_slice(arena, changed), true);
        valid = !clang_suite_inventory(arena, broken, &inventory) && valid;
    }
    String8 modes[] = {S8("100755"), S8("120000")};
    for (u64 mode_i = 0; mode_i < BUSTER_ARRAY_LENGTH(modes); mode_i += 1)
    {
        String8 changed = string_duplicate_arena(arena, raw, true);
        memcpy(changed.pointer, modes[mode_i].pointer, 6);
        valid = clang_suite_inventory(arena, changed, &inventory) && inventory.files == 7 && valid;
    }
    String8 duplicated = string_format(arena, S8("{S8}{S8}"), raw, raw);
    valid = !clang_suite_inventory(arena, duplicated, &inventory) && clang_suite_smoke_self_test(arena) && valid;
    string_print(S8("CLANG_SUITE_SELF_TEST status={S8}\n"), valid ? S8("pass") : S8("fail"));
    return valid;
}
