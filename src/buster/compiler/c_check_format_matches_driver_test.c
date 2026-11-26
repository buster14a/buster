// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 format contract regressions. Keep full ordered diagnostics, including source columns and notes,
// against the pinned Clang oracle; exercise ordinary and deferred body reading with different lane counts.

static void check_test_driver_format_matches(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* file;
        const char* option;
        uint32_t errors;
        uint32_t warnings;
        uint32_t notes;
    } fixtures[] =
    {
        { "attributes.c", NULL, 12, 3, 0 },
        { "archetypes.c", NULL, 0, 7, 5 },
        { "body.c", NULL, 0, 6, 4 },
        { "body.c", "-Wformat-nonliteral", 0, 8, 5 },
        { "body_formats.c", NULL, 0, 8, 7 },
        { "body_policy.c", NULL, 0, 2, 2 },
        { "closed_scopes.c", NULL, 0, 5, 5 },
        { "conversions.c", NULL, 0, 11, 10 },
        { "conversions.c", "-Wformat-signedness", 0, 13, 12 },
        { "literals.c", NULL, 0, 4, 4 },
        { "malformed.c", NULL, 0, 17, 7 },
        { "multiple.c", NULL, 0, 10, 8 },
        { "positions.c", NULL, 0, 8, 8 },
        { "sensitivity.c", NULL, 0, 8, 7 },
        { "suppression.c", NULL, 0, 2, 2 },
        { "suppression.c", "-Wno-format", 0, 0, 0 },
        { "suppression.c", "-w", 0, 0, 0 },
        { "suppression.c", "-Werror=format", 2, 0, 2 },
        { "valid.c", NULL, 0, 0, 0 },
        { "visibility.c", NULL, 0, 4, 4 },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        for (uint32_t language = 0; language < 2; language += 1)
        {
            if (!check_test_driver_takes())
            {
                continue;
            }
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "tests/driver/format_matches/%s", fixtures[f].file);
            char expected[32768] = { 0 };
            int oracle = -1;
            bool expected_ready = false;
            for (uint32_t route = 0; route < 3; route += 1)
            {
                char* arguments[16] =
                {
                    (char*)(route == 0 ? clang : executable != NULL ? executable : "/proc/self/exe"),
                    language != 0 ? "-std=c23" : "-std=gnu17",
                    "-fsyntax-only",
                    "-fno-caret-diagnostics",
                    "-fno-color-diagnostics",
                    "-ferror-limit=0",
                    path,
                };
                uint32_t used = 7;
                if (fixtures[f].option != NULL)
                {
                    arguments[used++] = (char*)fixtures[f].option;
                }
                if (route != 0)
                {
                    arguments[used++] = "--lanes";
                    arguments[used++] = route == 1 ? "1" : "3";
                }
                if (route == 2)
                {
                    arguments[used++] = "--deferred-bodies";
                }
                arguments[used] = NULL;
                char* text = NULL;
                const int status = check_test_driver_spawn(arguments, &text);
                char actual[32768] = { 0 };
                const bool fits = text != NULL && check_test_driver_common_output(text, actual, sizeof(actual));
                if (route == 0)
                {
                    oracle = status;
                    expected_ready = fits;
                    memcpy(expected, actual, strlen(actual) + 1);
                }
                const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(text);
                const bool same = fits && expected_ready && strcmp(actual, expected) == 0;
                const bool good = status == (fixtures[f].errors != 0) && status == oracle && same && counts.errors == fixtures[f].errors &&
                                  counts.warnings == fixtures[f].warnings && counts.notes == fixtures[f].notes;
                char description[256];
                snprintf(description, sizeof(description), "format_matches %s option=%s language=%u route=%u: complete Clang contract diagnostics",
                         fixtures[f].file, fixtures[f].option != NULL ? fixtures[f].option : "default", language, route);
                check_test_expect(test, good, description);
                if (!good)
                {
                    fprintf(stderr, "  status=%d Clang=%d errors=%u/%u warnings=%u/%u notes=%u/%u\nClang:\n%s\nActual:\n%s\n",
                            status, oracle, counts.errors, fixtures[f].errors, counts.warnings, fixtures[f].warnings, counts.notes, fixtures[f].notes,
                            expected, text != NULL ? text : "(missing)");
                }
                free(text);
            }
        }
    }
    (void)directory;
}
