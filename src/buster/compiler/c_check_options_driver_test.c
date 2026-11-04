// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent CLI fixtures for #1125's default-deny boundary, not generated from the admission tables. Included by
// c_check_hardening_driver_test.c after its command helper; its source and output sentinels belong to that caller.

static void check_test_driver_option_admission(CheckTest* test, const char* executable, const char* directory, const char* source, const char* output, const char* saved, const char* reference, const char* sentinel)
{
    static const char* const refused[] = {
        "-fbuster-unknown",
        "-fno-buster-unknown",
        "-mbuster-unknown",
        "-mretpoline",
        "-mspeculative-load-hardening",
        "-mlvi-hardening",
        "-mharden-sls=all",
        "-mfunction-return=thunk-extern",
        "-mstackrealign",
        "-mms-bitfields",
        "-mno-sse2-bogus",
        "-fsanitize-coverage-allowlist=missing.txt",
        "-fsanitize-coverage-ignorelist=missing.txt",
        "-fsanitize-coverage-type=3",
        "-fno-sanitize-coverage-allowlist=missing.txt",
        "-flto",
        "-flto=thin",
        "-fopenacc",
        "-fno-builtin-",
        "-fno-split-stack-bogus",
        "-fdiagnostics-color=bogus",
        "-fdiagnostics-color=never-bogus",
        "-fdiagnostics-buster-unknown",
        "-fsyntax-only-bogus",
        "-fwrapv=bogus",
        "-mpascal-strings",
        "-municode-bogus",
    };
    char label[256];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
    {
        const char* options[] = { refused[i] };
        char expected[256];
        snprintf(expected, sizeof(expected), "error: unsupported code generation option '%s'", refused[i]);
        // The first four exercise every semantic action, both tiers, with/without an existing destination. The
        // remaining spellings check the independent admission boundary once rather than multiplying subprocesses.
        const uint32_t actions = i < 4 ? 7 : 1;
        const uint32_t tiers = i < 4 ? 2 : 1;
        for (uint32_t tier = 0; tier < tiers; tier += 1)
        {
            for (uint32_t action = 0; action < actions; action += 1)
            {
                for (uint32_t existing = 0; existing < 2; existing += 1)
                {
                    unlink(output);
                    const bool prepared = existing == 0 || parse_test_write(output, sentinel, strlen(sentinel));
                    char* text = NULL;
                    const int status = check_test_driver_hardening_command(executable, source, output, action, tier != 0, tier != 0, options, 1, &text);
                    const bool preserved = existing != 0 ? check_test_driver_same_files(saved, output) : access(output, F_OK) != 0;
                    snprintf(label, sizeof(label), "option admission %s tier=%u action=%u existing=%u", refused[i], tier, action, existing);
                    check_test_expect(test, prepared && status == 1 && text != NULL && strstr(text, expected) != NULL && preserved, label);
                    free(text);
                }
            }
        }
    }
    // Unknown options still cannot turn a missing source into the first error, and the first unknown option wins.
    char missing[256];
    snprintf(missing, sizeof(missing), "%s/option-admission-missing.c", directory);
    unlink(missing);
    unlink(output);
    char* missing_arguments[] = { (char*)executable, "-c", missing, "-fbuster-unknown", "-mbuster-unknown", "-o", (char*)output, NULL };
    char* text = NULL;
    const int missing_status = check_test_driver_spawn(missing_arguments, &text);
    check_test_expect(test, missing_status == 1 && text != NULL && strstr(text, "unsupported code generation option '-fbuster-unknown'") != NULL && access(output, F_OK) != 0,
                      "an unknown codegen option precedes source IO and retains its spelling");
    free(text);
    // An unsupported option cannot be hidden by a later compatibility option. No generic negative-prefix escape.
    const char* mixed[] = { "-mretpoline", "-m64", "-fno-lto" };
    text = NULL;
    const int mixed_status = check_test_driver_hardening_command(executable, source, output, 0, false, false, mixed, BUSTER_ARRAY_LENGTH(mixed), &text);
    check_test_expect(test, mixed_status == 1 && text != NULL && strstr(text, "unsupported code generation option '-mretpoline'") != NULL && access(output, F_OK) != 0,
                      "later admitted options do not erase a codegen refusal");
    free(text);
    // The include's next argument is a value even when the same word would be an unknown option on its own.
    char header[256];
    snprintf(header, sizeof(header), "%s/-fbuster-unknown", directory);
    const bool header_written = parse_test_write(header, "\n", 1);
    const char* include[] = { "-I", directory, "-include", "-fbuster-unknown" };
    text = NULL;
    const int included = check_test_driver_hardening_command(executable, source, output, 0, false, false, include, BUSTER_ARRAY_LENGTH(include), &text);
    check_test_expect(test, header_written && included == 0 && access(output, F_OK) == 0, "unknown codegen spelling consumed as an include filename is a value");
    free(text);
    unlink(header);
    // Every documented compatibility spelling is exact. These ask for default output or permit, but do not require,
    // optimizations. Compare bytes with a fresh baseline in each tier; the fixture has no option-sensitive macros.
    static const char* const compatible[] = {
        "-fno-lto",
        "-fno-split-stack",
        "-fno-trapv",
        "-fno-strict-aliasing",
        "-fno-semantic-interposition",
        "-fdiagnostics-color=never",
        "-m64",
        "-mred-zone",
        "-mno-ms-bitfields",
    };
    for (uint32_t tier = 0; tier < 2; tier += 1)
    {
        unlink(reference);
        unlink(output);
        text = NULL;
        const int baseline = check_test_driver_hardening_command(executable, source, reference, 0, tier != 0, tier != 0, NULL, 0, &text);
        free(text);
        text = NULL;
        const int status = check_test_driver_hardening_command(executable, source, output, 0, tier != 0, tier != 0, compatible, BUSTER_ARRAY_LENGTH(compatible), &text);
        check_test_expect(test, baseline == 0 && status == 0 && check_test_driver_same_files(reference, output), "admitted compatibility options preserve object bytes");
        free(text);
    }
    // Preprocessing deliberately models flags without promising code generation; it does not use this scanner.
    const char* preprocessed[] = { "-mretpoline", "-fbuster-unknown", "-fsanitize-coverage-allowlist=missing.txt" };
    unlink(output);
    text = NULL;
    const int preprocessing = check_test_driver_hardening_command(executable, source, output, 7, false, false, preprocessed, BUSTER_ARRAY_LENGTH(preprocessed), &text);
    check_test_expect(test, preprocessing == 0 && access(output, F_OK) == 0, "pure preprocessing retains its own option contract");
    free(text);
}
