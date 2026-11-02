// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1125: requests for unimplemented hardening must fail before any output is published. The independent CLI
// fixtures name the reported options rather than deriving expectations from the driver's refusal table. Of the six
// families #1125 reported, -fsanitize-coverage= is instrumentation that #1112 refuses only when code is emitted
// (c_check_instrumentation_driver_test.c); its auxiliary -fsanitize-coverage-* options and the bare spelling stay here.

static int check_test_driver_hardening_command(const char* executable, const char* source, const char* output, uint32_t action, bool scalar, bool tier1, const char* const* options, uint32_t option_count, char** text)
{
    static const char* const actions[][2] = {
        { "-c", NULL },
        { "check", "-c" },
        { NULL, NULL },
        { "-fsyntax-only", NULL },
        { "check", NULL },
        { "--dump-typed", NULL },
        { "--dump-static", NULL },
        { "-E", NULL },
    };
    assert(action < BUSTER_ARRAY_LENGTH(actions) && option_count <= 8);
    char* arguments[24] = { 0 };
    uint32_t count = 0;
    arguments[count++] = (char*)executable;
    // A subcommand must precede --scalar and --tier1.
    for (uint32_t i = 0; i < 2 && actions[action][i] != NULL; i += 1)
    {
        arguments[count++] = (char*)actions[action][i];
    }
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    if (tier1)
    {
        arguments[count++] = "--tier1";
    }
    arguments[count++] = "-std=gnu17";
    for (uint32_t i = 0; i < option_count; i += 1)
    {
        arguments[count++] = (char*)options[i];
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    return check_test_driver_spawn(arguments, text);
}

static void check_test_driver_hardening(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "int helper(int x) { return x + 7; }\nint main(void) { return helper(3) != 10; }\n";
    static const char sentinel[] = "existing output must survive a rejected hardening request\n";
    static const char* const reported[] = {
        "-ftrivial-auto-var-init=zero",
        "-ftrapv",
        "-fzero-call-used-regs=all",
        "-fsplit-stack",
        "-mcmodel=large",
    };
    static const char* const variants[] = {
        "-ftrivial-auto-var-init=pattern",
        "-ftrivial-auto-var-init=",
        "-ftrivial-auto-var-init",
        "-ftrivial-auto-var-init=uninitialized-bogus",
        "-ftrivial-auto-var-init-stop-after=1",
        "-ftrivial-auto-var-init-max-size=1024",
        "-ftrapv-handler=handler",
        "-fzero-call-used-regs=used-gpr",
        "-fzero-call-used-regs=skip,all",
        "-fzero-call-used-regs=",
        "-fzero-call-used-regs",
        "-fsanitize-coverage",
        "-fsanitize-coverage-allowlist=coverage.txt",
        "-fsanitize-coverage-ignorelist=coverage.txt",
        "-fsanitize-coverage-stack-depth-callback-min=1",
        "-fsanitize-coverage-type=1",
        "-fsplit-stack=bogus",
        "-mcmodel=medium",
        "-mcmodel=kernel",
        "-mcmodel=small-bogus",
        "-mcmodel=",
        "-mcmodel",
    };
    static const char* const inactive[] = {
        "-ftrivial-auto-var-init=uninitialized",
        "-fzero-call-used-regs=skip",
        "-mcmodel=small",
        "-fno-split-stack",
        "-fno-strict-aliasing",
        "-fwrapv",
        "-march=x86-64",
    };
    char source[256];
    char output[256];
    char saved[256];
    char reference[256];
    char header[256];
    char valued[256];
    char missing[256];
    char label[256];
    snprintf(source, sizeof(source), "%s/hardening-driver.c", directory);
    snprintf(output, sizeof(output), "%s/hardening-driver.out", directory);
    snprintf(saved, sizeof(saved), "%s/hardening-driver.saved", directory);
    snprintf(reference, sizeof(reference), "%s/hardening-driver.reference", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1) && parse_test_write(saved, sentinel, sizeof(sentinel) - 1);
    check_test_expect(test, written, "hardening source and output sentinel are written");
    // The refusal happens before the source is read: a missing source does not replace the hardening diagnostic.
    snprintf(missing, sizeof(missing), "%s/hardening-driver-missing.c", directory);
    unlink(missing);
    unlink(output);
    char* missing_text = NULL;
    char* missing_arguments[] = { (char*)executable, "-c", "-o", output, missing, (char*)"-ftrapv", NULL };
    const int missing_status = check_test_driver_spawn(missing_arguments, &missing_text);
    check_test_expect(test, missing_status == 1 && missing_text != NULL && strstr(missing_text, "unsupported code generation option '-ftrapv'") != NULL && access(output, F_OK) != 0,
                      "a hardening request is refused before the source is read");
    free(missing_text);
    for (uint32_t flag = 0; written && flag < BUSTER_ARRAY_LENGTH(reported); flag += 1)
    {
        const char* options[] = { reported[flag] };
        char expected[160];
        snprintf(expected, sizeof(expected), "error: unsupported code generation option '%s'", reported[flag]);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            for (uint32_t tier1 = 0; tier1 < 2; tier1 += 1)
            {
                for (uint32_t action = 0; action < 3; action += 1)
                {
                    for (uint32_t existing = 0; existing < 2; existing += 1)
                    {
                        unlink(output);
                        const bool prepared = existing == 0 || parse_test_write(output, sentinel, sizeof(sentinel) - 1);
                        char* text = NULL;
                        const int status = check_test_driver_hardening_command(executable, source, output, action, scalar != 0, tier1 != 0, options, 1, &text);
                        const bool preserved = existing != 0 ? check_test_driver_same_files(saved, output) : access(output, F_OK) != 0 && errno == ENOENT;
                        snprintf(label, sizeof(label), "hardening %s scalar=%u tier1=%u action=%u existing=%u", reported[flag], scalar, tier1, action, existing);
                        check_test_expect(test, prepared && status == 1 && text != NULL && strstr(text, expected) != NULL && preserved, label);
                        free(text);
                    }
                }
            }
        }
        // The semantic command rejects these even without emission; -E is still the preprocessor's own action.
        for (uint32_t action = 3; action < 8; action += 1)
        {
            unlink(output);
            char* text = NULL;
            const int status = check_test_driver_hardening_command(executable, source, output, action, false, false, options, 1, &text);
            snprintf(label, sizeof(label), "hardening %s non-emitting action=%u", reported[flag], action);
            check_test_expect(test, action == 7 ? status == 0 : status == 1 && text != NULL && strstr(text, expected) != NULL && access(output, F_OK) != 0, label);
            free(text);
        }
        // A flag after the source is still an option; another refusal must not replace the first one's name.
        char* late[] = { (char*)executable, "-c", "-o", output, source, (char*)reported[flag], "-pg", NULL };
        unlink(output);
        char* text = NULL;
        const int status = check_test_driver_spawn(late, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, expected) != NULL && access(output, F_OK) != 0, "hardening after the source retains the first unsupported spelling");
        free(text);
        // An output name that spells one of these options is a value, not a request.
        snprintf(valued, sizeof(valued), "%s/%s", directory, reported[flag]);
        unlink(valued);
        char* value_arguments[] = { (char*)executable, "-c", "-o", valued, "-std=gnu17", source, NULL };
        text = NULL;
        const int valued_status = check_test_driver_spawn(value_arguments, &text);
        snprintf(label, sizeof(label), "hardening %s as an output name is a value", reported[flag]);
        check_test_expect(test, valued_status == 0 && access(valued, F_OK) == 0, label);
        free(text);
        unlink(valued);
        // A consumed option value is a filename, even when it spells one of these options.
        snprintf(header, sizeof(header), "%s/%s", directory, reported[flag]);
        const bool header_written = parse_test_write(header, "\n", 1);
        const char* include[] = { "-I", directory, "-include", reported[flag] };
        unlink(output);
        text = NULL;
        const int included = check_test_driver_hardening_command(executable, source, output, 0, false, false, include, BUSTER_ARRAY_LENGTH(include), &text);
        check_test_expect(test, header_written && included == 0 && access(output, F_OK) == 0, "hardening spelling consumed as an include filename is not refused");
        free(text);
        unlink(header);
    }
    for (uint32_t flag = 0; written && flag < BUSTER_ARRAY_LENGTH(variants); flag += 1)
    {
        const char* options[] = { variants[flag] };
        char expected[160];
        snprintf(expected, sizeof(expected), "error: unsupported code generation option '%s'", variants[flag]);
        unlink(output);
        char* text = NULL;
        const int status = check_test_driver_hardening_command(executable, source, output, 0, false, false, options, 1, &text);
        snprintf(label, sizeof(label), "hardening family variant %s", variants[flag]);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, expected) != NULL && access(output, F_OK) != 0, label);
        free(text);
    }
    for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
    {
        for (uint32_t tier1 = 0; tier1 < 2; tier1 += 1)
        {
            unlink(reference);
            unlink(output);
            char* text = NULL;
            const int baseline = check_test_driver_hardening_command(executable, source, reference, 0, scalar != 0, tier1 != 0, NULL, 0, &text);
            free(text);
            text = NULL;
            const int status = check_test_driver_hardening_command(executable, source, output, 0, scalar != 0, tier1 != 0, inactive, BUSTER_ARRAY_LENGTH(inactive), &text);
            snprintf(label, sizeof(label), "hardening inactive/default options preserve object bytes scalar=%u tier1=%u", scalar, tier1);
            check_test_expect(test, baseline == 0 && status == 0 && check_test_driver_same_files(reference, output), label);
            free(text);
        }
    }
    // The refusal table is deliberately conservative, as it is for -pg: a later no-op does not cancel a request.
    for (uint32_t reverse = 0; written && reverse < 2; reverse += 1)
    {
        const char* options[] = { reverse != 0 ? inactive[0] : reported[0], reverse != 0 ? reported[0] : inactive[0] };
        unlink(output);
        char* text = NULL;
        const int status = check_test_driver_hardening_command(executable, source, output, 0, false, false, options, 2, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, "unsupported code generation option '-ftrivial-auto-var-init=zero'") != NULL && access(output, F_OK) != 0,
                          "an inactive spelling cannot silently cancel a hardening refusal");
        free(text);
    }
    unlink(source);
    unlink(output);
    unlink(saved);
    unlink(reference);
}
