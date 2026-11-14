// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2406's GNU-attribute-led calls before C23: classify the callee, keep its implicit declaration, reject the
// declarator, skip arguments that clang never reads, and continue. The whole diagnostic stream is the oracle,
// with independent later-error counts, ordinary prototypes and noncallable controls, repeated macro expansions,
// nested unread arguments, prior recovery rounds, builtins and the distinct C23 declaration grammar.
static void check_test_driver_attribute_calls(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* path;
        uint32_t later_errors;
        bool c23;
        bool accepted;
    } fixtures[] =
    {
        { "tests/checker/parse-recovery/attributed-calls.c", 2, false, false },
        { "tests/checker/parse-recovery/attributed-call-attributes.c", 1, false, false },
        { "tests/checker/parse-recovery/attributed-call-macros.c", 2, false, false },
        { "tests/checker/parse-recovery/attributed-call-rounds.c", 3, false, false },
        { "tests/checker/parse-recovery/attributed-call-nesting.c", 1, false, false },
        { "tests/checker/parse-recovery/attributed-call-views.c", 2, false, false },
        { "tests/checker/parse-recovery/attribute-recovery-integration.c", 2, true, false },
        { "tests/checker/parse-recovery/attributed-call-builtin-control.c", 0, false, false },
        { "tests/checker/parse-recovery/attributed-call-c23-control.c", 0, true, false },
        { "tests/checker/parse-recovery/attributed-call-valid-control.c", 0, true, true },
    };
    static const char* const standards[] = { "-std=gnu89", "-std=gnu17", "-std=gnu23" };
    static const char* const policies[] = { "-Wall", "-Werror", "-w", "-Wno-implicit-function-declaration", "-ferror-limit=2" };
    enum { LINES = 32768 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char object[PATH_MAX];
    char saved[PATH_MAX];
    snprintf(object, sizeof(object), "%s/attribute-call.o", directory);
    snprintf(saved, sizeof(saved), "%s/attribute-call.saved", directory);
    static const char sentinel[] = "preserved attribute-call output\n";
    const bool prepared = parse_test_write(saved, sentinel, sizeof(sentinel) - 1);
    check_test_expect(test, prepared && expected_lines != NULL && actual_lines != NULL, "attribute calls: output and diagnostic buffers available");
    for (uint32_t f = 0; prepared && expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        const bool present = access(fixtures[f].path, R_OK) == 0;
        check_test_expect(test, present, "attribute calls: runtime fixture available");
        const uint32_t languages = fixtures[f].c23 ? 3u : 2u;
        for (uint32_t s = 0; present && s < languages; s += 1)
        {
            for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(policies); p += 1)
            {
                char* reference[] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", (char*)policies[p], (char*)fixtures[f].path, NULL };
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(reference, &expected);
                const bool read = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                uint32_t later = 0;
                const char* found = read ? expected_lines : "";
                while ((found = strstr(found, "undeclared identifier 'after_")) != NULL)
                {
                    later += 1;
                    found += 1;
                }
                check_test_expect(test, read && oracle == (fixtures[f].accepted ? 0 : 1) &&
                                        (p + 1 == BUSTER_ARRAY_LENGTH(policies) || later == fixtures[f].later_errors),
                                  "attribute calls: clang status and independent later-error count");
                for (uint32_t mode = 0; read && mode < 6; mode += 1)
                {
                    const bool compile = mode >= 2;
                    char* arguments[20] = { (char*)executable, (char*)standards[s], compile ? "-c" : "-fsyntax-only",
                                            "-fno-caret-diagnostics", (char*)policies[p], (char*)fixtures[f].path };
                    uint32_t count = 6;
                    if ((mode & 1) != 0)
                    {
                        arguments[count++] = "--scalar";
                    }
                    bool output_ready = true;
                    if (compile)
                    {
                        arguments[count++] = "-o";
                        arguments[count++] = object;
                        arguments[count++] = "--lanes";
                        arguments[count++] = mode >= 4 ? "3" : "1";
                        if (mode >= 4)
                        {
                            arguments[count++] = "--deferred-bodies";
                        }
                        unlink(object);
                        output_ready = parse_test_write(object, sentinel, sizeof(sentinel) - 1);
                    }
                    arguments[count] = NULL;
                    char* actual = NULL;
                    const int status = output_ready ? check_test_driver_spawn(arguments, &actual) : -1;
                    actual_lines[0] = 0;
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(actual_lines, expected_lines) == 0;
                    bool output = !compile || check_test_driver_same_files(saved, object);
                    if (compile && fixtures[f].accepted)
                    {
                        uint64_t length = 0;
                        uint8_t* bytes = check_test_driver_read_aligned(object, &length);
                        const Elf64_Ehdr* header = (const Elf64_Ehdr*)bytes;
                        output = bytes != NULL && length >= sizeof(*header) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 &&
                                 header->e_ident[EI_CLASS] == ELFCLASS64 && header->e_type == ET_REL && header->e_machine == EM_X86_64;
                        free(bytes);
                    }
                    char description[8192];
                    snprintf(description, sizeof(description), "attribute calls %s, %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)",
                             fixtures[f].path, standards[s], policies[p], mode, status, oracle, actual_lines, expected_lines);
                    check_test_expect(test, same && status == oracle && output, description);
                    free(actual);
                }
                free(expected);
            }
        }
    }
    unlink(object);
    unlink(saved);
    free(expected_lines);
    free(actual_lines);
}
