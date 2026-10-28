// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original driver fixtures. Clang runs externally; its diagnostic text is not incorporated into the fixtures.

typedef struct CheckDriverDiagnosticCounts CheckDriverDiagnosticCounts;
struct CheckDriverDiagnosticCounts
{
    uint32_t warnings;
    uint32_t errors;
    uint32_t fatal;
    uint32_t notes;
    uint32_t last_error_line;
    bool after_fatal;
    char summary[128];
};

// Ignore Clang's source excerpts and carets, but independently compare published severities, the last original
// error's source line, attached notes and the complete summary. Do not infer any count from the summary itself.
static CheckDriverDiagnosticCounts check_test_driver_diagnostic_counts(const char* text)
{
    CheckDriverDiagnosticCounts counts = { 0 };
    for (const char* at = text; at != NULL && *at != 0;)
    {
        const char* end = strchr(at, '\n');
        const size_t length = end != NULL ? (size_t)(end - at) : strlen(at);
        const bool warned = memmem(at, length, ": warning: ", 11) != NULL;
        const bool failed = memmem(at, length, ": error: ", 9) != NULL;
        const bool stopped = memmem(at, length, "fatal error: ", 13) != NULL;
        const bool noted = memmem(at, length, ": note: ", 8) != NULL;
        counts.after_fatal |= counts.fatal != 0 && (warned || failed || stopped || noted);
        counts.warnings += warned;
        counts.errors += failed || stopped;
        counts.fatal += stopped;
        counts.notes += noted;
        if (failed)
        {
            uint32_t column = 0;
            sscanf(at, "%*[^:]:%u:%u:", &counts.last_error_line, &column);
        }
        if (memmem(at, length, " generated.", 11) != NULL && length < sizeof(counts.summary))
        {
            memcpy(counts.summary, at, length);
            counts.summary[length] = 0;
        }
        at = end != NULL ? end + 1 : NULL;
    }
    return counts;
}

static void check_test_driver_error_limits(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    const uint32_t sizes[] = { 0, 1, 19, 20, 21, 2000 };
    const char* limits[] = { NULL, "-ferror-limit=0", "-ferror-limit=1", "-ferror-limit=20", "-ferror-limit=0x3", "-ferror-limit=0b10", "-ferror-limit=0o7", "-ferror-limit=020", "-ferror-limit=4294967295" };
    char source[256];
    char object[256];
    snprintf(source, sizeof(source), "%s/error-limit.c", directory);
    snprintf(object, sizeof(object), "%s/error-limit.o", directory);
    for (uint32_t shape = 0; shape < 6; shape += 1)
    {
        for (uint32_t size = 0; size < BUSTER_ARRAY_LENGTH(sizes); size += 1)
        {
            FILE* file = fopen(source, "w");
            bool made = file != NULL;
            if (made)
            {
                fputs(shape == 4 ? "#define BAD missing\n" : shape == 5 ? "#pragma clang diagnostic error \"-Wshift-count-overflow\"\n" : "int valid;\n", file);
                for (uint32_t i = 0; i < sizes[size]; i += 1)
                {
                    if (shape == 1)
                    {
                        fprintf(file, "int invalid_%u = ;\n", i);
                    }
                    else if (shape == 2)
                    {
                        fprintf(file, "#error problem_%u\n", i);
                    }
                    else if (shape == 4)
                    {
                        fprintf(file, "int f_%u(void) { return BAD; }\n", i);
                    }
                    else if (shape == 5)
                    {
                        fprintf(file, "int f_%u(void) { return 1 << 32; }\n", i);
                    }
                    else if (shape == 3)
                    {
                        // Reader errors retain only an earlier safe graph prefix (#699). These independent typed
                        // errors leave the graph complete, so warnings between errors exercise the report cutoff.
                        fprintf(file, "int f_%u(void) { return *1; }\n", i);
                    }
                    else
                    {
                        fprintf(file, "int f_%u(void) { return missing_%u; }\n", i, i);
                    }
                    if (shape == 3)
                    {
                        fprintf(file, "int w_%u(void) { return 1 << 32; }\n", i);
                    }
                }
                if (shape == 3 && sizes[size] == 0)
                {
                    fputs("int warning_only(void) { return 1 << 32; }\n", file);
                }
                made = fclose(file) == 0;
            }
            check_test_expect(test, made, "error-limit original fixture can be written");
            for (uint32_t limit = 0; made && limit < BUSTER_ARRAY_LENGTH(limits); limit += 1)
            {
                // The extra modes distinguish suppressed warnings from quiet diagnostic counting, and preserve
                // Clang's explicit no-caret option, which suppresses its summary even while errors remain visible.
                const uint32_t policies = shape == 3 || shape == 5 ? 3 : 1;
                for (uint32_t policy = 0; policy < policies; policy += 1)
                {
                    char* arguments[16] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics" };
                    uint32_t count = 4;
                    if (limits[limit] != NULL)
                    {
                        arguments[count++] = (char*)limits[limit];
                    }
                    if (policy != 0)
                    {
                        arguments[count++] = policy == 1 ? "-w" : "-fno-caret-diagnostics";
                    }
                    arguments[count++] = source;
                    char* text = NULL;
                    const int oracle = check_test_driver_spawn(arguments, &text);
                    const CheckDriverDiagnosticCounts expected = check_test_driver_diagnostic_counts(text);
                    free(text);
                    char description[256];
                    snprintf(description, sizeof(description), "Clang error-limit oracle shape=%u errors=%u limit=%u policy=%u", shape, sizes[size], limit, policy);
                    check_test_expect(test, oracle == (sizes[size] != 0 && !(shape == 5 && policy == 1)), description);
                    for (uint32_t route = 0; route < 4; route += 1)
                    {
                        char* checked[16] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17" };
                        uint32_t used = 2;
                        if ((route & 1) != 0)
                        {
                            checked[used++] = "--scalar";
                        }
                        if (route < 2)
                        {
                            checked[used++] = "-fsyntax-only";
                        }
                        else
                        {
                            checked[used++] = "-c";
                            checked[used++] = "-o";
                            checked[used++] = object;
                        }
                        if (limits[limit] != NULL)
                        {
                            checked[used++] = (char*)limits[limit];
                        }
                        if (policy != 0)
                        {
                            checked[used++] = policy == 1 ? "-w" : "-fno-caret-diagnostics";
                        }
                        checked[used++] = source;
                        text = NULL;
                        const int status = check_test_driver_spawn(checked, &text);
                        const CheckDriverDiagnosticCounts actual = check_test_driver_diagnostic_counts(text);
                        snprintf(description, sizeof(description), "error-limit shape=%u errors=%u limit=%u policy=%u route=%u: warning/error/note %u/%u/%u vs Clang %u/%u/%u", shape, sizes[size], limit, policy, route,
                                 actual.warnings, actual.errors, actual.notes, expected.warnings, expected.errors, expected.notes);
                        check_test_expect(test, status == oracle && actual.warnings == expected.warnings && actual.errors == expected.errors &&
                                          actual.fatal == expected.fatal && actual.notes == expected.notes && actual.last_error_line == expected.last_error_line &&
                                          !actual.after_fatal && strcmp(actual.summary, expected.summary) == 0, description);
                        free(text);
                        unlink(object);
                    }
                }
            }
        }
    }
    // #684's explicit notes use a third diagnostic entry point. Conditional invocations keep these fixtures wholly
    // in preprocessing, avoiding the independently documented cross-stage recovery boundary (#698).
    const char* noted_sources[] =
    {
        "#define ONE(x) x\n#if ONE(1,2) + 0\n#endif\n#if ONE(1,2) + 0\n#endif\n#if ONE(1,2) + 0\n#endif\n",
        "#define BAD0(...) __VA_OPT__(\n#define BAD1(...) __VA_OPT__(\n#define BAD2(...) __VA_OPT__(\n",
    };
    const char* noted_limits[] = { NULL, "-ferror-limit=1", "-ferror-limit=0" };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(noted_sources); fixture += 1)
    {
        const bool made = parse_test_write(source, noted_sources[fixture], strlen(noted_sources[fixture]));
        check_test_expect(test, made, "attached-note limit fixture can be written");
        for (uint32_t limit = 0; made && limit < BUSTER_ARRAY_LENGTH(noted_limits); limit += 1)
        {
            for (uint32_t route = 0; route < 6; route += 1)
            {
                char* arguments[12] = { (char*)clang, "-std=gnu17", route < 4 ? "-fsyntax-only" : "-E", "-fno-color-diagnostics" };
                uint32_t count = 4;
                if (noted_limits[limit] != NULL)
                {
                    arguments[count++] = (char*)noted_limits[limit];
                }
                arguments[count++] = source;
                char* text = NULL;
                const int oracle = check_test_driver_spawn(arguments, &text);
                const CheckDriverDiagnosticCounts expected = check_test_driver_diagnostic_counts(text);
                free(text);
                char* checked[16] = { (char*)(executable != NULL ? executable : "/proc/self/exe") };
                uint32_t used = 1;
                // The standalone preprocessor command is selected by its leading -E.
                if (route >= 4)
                {
                    checked[used++] = "-E";
                }
                checked[used++] = "-std=gnu17";
                if ((route & 1) != 0)
                {
                    checked[used++] = "--scalar";
                }
                if (route < 2)
                {
                    checked[used++] = "-fsyntax-only";
                }
                else if (route < 4)
                {
                    checked[used++] = "-c";
                    checked[used++] = "-o";
                    checked[used++] = object;
                }
                if (noted_limits[limit] != NULL)
                {
                    checked[used++] = (char*)noted_limits[limit];
                }
                checked[used++] = source;
                text = NULL;
                const int status = check_test_driver_spawn(checked, &text);
                const CheckDriverDiagnosticCounts actual = check_test_driver_diagnostic_counts(text);
                char description[160];
                snprintf(description, sizeof(description), "attached-note cutoff, totals and group order fixture=%u limit=%u route=%u", fixture, limit, route);
                check_test_expect(test, oracle == 1 && status == oracle && actual.errors == expected.errors && actual.fatal == expected.fatal &&
                                  actual.warnings == expected.warnings && actual.notes == expected.notes && actual.last_error_line == expected.last_error_line &&
                                  !actual.after_fatal && strcmp(actual.summary, expected.summary) == 0, description);
                free(text);
                unlink(object);
            }
        }
    }
    const char* values[] = { "", "-1", "+1", "word", "1x", "4294967296", "08", "0x", "0b", "0o" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(values); i += 1)
    {
        char option[64];
        snprintf(option, sizeof(option), "-ferror-limit=%s", values[i]);
        char* arguments[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-fsyntax-only", option, source, NULL };
        char* text = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, "invalid integral value") != NULL,
                          "error limit rejects malformed and overflowing values");
        free(text);
        char* reference[] = { (char*)clang, "-fsyntax-only", option, source, NULL };
        text = NULL;
        const int oracle = check_test_driver_spawn(reference, &text);
        check_test_expect(test, oracle == 1 && text != NULL && strstr(text, "invalid integral value") != NULL,
                          "Clang independently rejects the same malformed and overflowing integral values");
        free(text);
    }
    const char* warnings[] =
    {
        "#define VALUE 1\n#define VALUE 2\nint value = VALUE;\n",
        "#pragma clang diagnostic ignored \"-Wshift-count-overflow\"\nint f(void) { return 1 << 32; }\n",
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(warnings); fixture += 1)
    {
        const bool made = parse_test_write(source, warnings[fixture], strlen(warnings[fixture]));
        check_test_expect(test, made, "warning-note summary fixture can be written");
        for (uint32_t quiet = 0; made && quiet < 2; quiet += 1)
        {
            char* arguments[] = { (char*)clang, "-fsyntax-only", "-fno-color-diagnostics", quiet != 0 ? "-w" : "-std=gnu17", source, NULL };
            char* text = NULL;
            const int oracle = check_test_driver_spawn(arguments, &text);
            const CheckDriverDiagnosticCounts expected = check_test_driver_diagnostic_counts(text);
            free(text);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-fsyntax-only", scalar != 0 ? "--scalar" : "-std=gnu17",
                                    quiet != 0 ? "-w" : "-std=gnu17", source, NULL };
                text = NULL;
                const int status = check_test_driver_spawn(checked, &text);
                const CheckDriverDiagnosticCounts actual = check_test_driver_diagnostic_counts(text);
                check_test_expect(test, status == 0 && oracle == 0 && actual.warnings == expected.warnings && actual.errors == 0 &&
                                  actual.notes == expected.notes && strcmp(actual.summary, expected.summary) == 0,
                                  "suppressed macro-redefinition notes and ignored semantic warnings do not enter the summary");
                free(text);
            }
        }
    }
    const char* repeated[][2] =
    {
        { "-ferror-limit=word", "-ferror-limit=1" },
        { "-ferror-limit=1", "-ferror-limit=0" },
        { "-ferror-limit=0", "-ferror-limit=2" },
        { "-ferror-limit=1", "-ferror-limit=word" },
    };
    const char repeated_source[] = "int f(void) { return *1; }\nint g(void) { return *2; }\nint h(void) { return *3; }\n";
    const bool repeated_made = parse_test_write(source, repeated_source, sizeof(repeated_source) - 1);
    check_test_expect(test, repeated_made, "last error-limit option fixture can be written");
    for (uint32_t fixture = 0; repeated_made && fixture < BUSTER_ARRAY_LENGTH(repeated); fixture += 1)
    {
        char* arguments[] = { (char*)clang, "-fsyntax-only", "-fno-color-diagnostics", (char*)repeated[fixture][0], (char*)repeated[fixture][1], source, NULL };
        char* text = NULL;
        const int oracle = check_test_driver_spawn(arguments, &text);
        const CheckDriverDiagnosticCounts expected = check_test_driver_diagnostic_counts(text);
        free(text);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-fsyntax-only", scalar != 0 ? "--scalar" : "-std=gnu17",
                                (char*)repeated[fixture][0], (char*)repeated[fixture][1], source, NULL };
            text = NULL;
            const int status = check_test_driver_spawn(checked, &text);
            const CheckDriverDiagnosticCounts actual = check_test_driver_diagnostic_counts(text);
            check_test_expect(test, status == oracle && actual.errors == expected.errors && actual.fatal == expected.fatal &&
                              actual.last_error_line == expected.last_error_line && strcmp(actual.summary, expected.summary) == 0,
                              "only the last error-limit option is interpreted, including after a malformed earlier value");
            free(text);
        }
    }
    unlink(source);
    unlink(object);
}
