// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 member-recovery sources. Compare all diagnostic lines with Clang, including later declarations,
// invalid-member uses, valid sibling errors and forward notes. Rejected object commands must preserve existing output.

static void check_test_driver_invalid_records(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* file;
        uint32_t errors;
        uint32_t notes;
    } fixtures[] =
    {
        { "declarators.c", 7, 1 },
        { "bit_fields.c", 12, 0 },
        { "types.c", 9, 3 },
        { "uses.c", 8, 0 },
        { "nested.c", 8, 0 },
        { "propagation.c", 10, 0 },
        { "valid.c", 0, 0 },
    };
    static const char sentinel[] = "preserved invalid-record output\n";
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/invalid-record.o", directory);
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        for (uint32_t language = 0; language < 2; language += 1)
        {
            if (!check_test_driver_takes())
            {
                continue;
            }
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "tests/driver/invalid_records/%s", fixtures[f].file);
            char expected[32768] = { 0 };
            int oracle = -1;
            bool expected_ready = false;
            for (uint32_t route = 0; route < (fixtures[f].errors != 0 ? 4u : 3u); route += 1)
            {
                const bool emission = route == 3;
                const bool written = !emission || parse_test_write(object, sentinel, sizeof(sentinel) - 1);
                char* arguments[16] =
                {
                    (char*)(route == 0 ? clang : executable != NULL ? executable : "/proc/self/exe"),
                    language != 0 ? "-std=c23" : "-std=gnu17",
                    emission ? "-c" : "-fsyntax-only",
                    "-fno-caret-diagnostics",
                    "-fno-color-diagnostics",
                    "-ferror-limit=0",
                    path,
                };
                uint32_t used = 7;
                if (route != 0)
                {
                    arguments[used++] = "--lanes";
                    arguments[used++] = route == 1 ? "1" : "3";
                }
                if (route == 2)
                {
                    arguments[used++] = "--deferred-bodies";
                }
                if (emission)
                {
                    arguments[used++] = "-o";
                    arguments[used++] = object;
                }
                arguments[used] = NULL;
                char* text = NULL;
                const int status = written ? check_test_driver_spawn(arguments, &text) : -1;
                char actual[32768] = { 0 };
                const bool fits = text != NULL && check_test_driver_common_output(text, actual, sizeof(actual));
                if (route == 0)
                {
                    oracle = status;
                    expected_ready = fits;
                    memcpy(expected, actual, strlen(actual) + 1);
                }
                const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(text);
                bool preserved = true;
                if (emission)
                {
                    const int file = open(object, O_RDONLY | O_CLOEXEC);
                    char bytes[sizeof(sentinel)] = { 0 };
                    const ssize_t length = file >= 0 ? read(file, bytes, sizeof(bytes)) : -1;
                    preserved = length == (ssize_t)(sizeof(sentinel) - 1) && memcmp(bytes, sentinel, sizeof(sentinel) - 1) == 0;
                    if (file >= 0)
                    {
                        close(file);
                    }
                }
                const bool same = fits && expected_ready && strcmp(actual, expected) == 0;
                const bool good = written && status == (fixtures[f].errors != 0) && status == oracle && same && preserved &&
                                  counts.errors == fixtures[f].errors && counts.notes == fixtures[f].notes && counts.warnings == 0;
                char description[256];
                snprintf(description, sizeof(description), "invalid record %s language=%u route=%u: complete Clang diagnostics, continuation and output preservation",
                         fixtures[f].file, language, route);
                check_test_expect(test, good, description);
                if (!good)
                {
                    fprintf(stderr, "  status=%d Clang=%d errors=%u/%u notes=%u/%u warnings=%u preserved=%u\nClang:\n%s\nActual:\n%s\n",
                            status, oracle, counts.errors, fixtures[f].errors, counts.notes, fixtures[f].notes, counts.warnings, (unsigned)preserved,
                            expected, text != NULL ? text : "(missing)");
                }
                free(text);
            }
        }
    }
    unlink(object);
}
