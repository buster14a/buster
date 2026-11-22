// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The diagnostic-only type reader deliberately leaves complex forms in their source spelling. Unlike the canonical
// spelling fixtures, these check rejection, diagnostic counts and output preservation against Clang: they must never
// enter a partial reader with unavailable string or declaration state (#2409).

static void check_test_driver_assert_requirement_fallback(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] = "tests/driver/assert_requirement_fallback.c";
    static const char sentinel[] = "preserved-requirement-output";
    static const char* const lanes[] = { "1", "3" };
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/requirement-fallback.o", directory);
    for (uint32_t language = 0; language < 2; language += 1)
    {
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            char* arguments[] =
            {
                (char*)clang,
                language != 0 ? "-std=c23" : "-std=gnu17",
                "-fno-caret-diagnostics",
                "-fno-color-diagnostics",
                mode != 0 ? "-c" : "-fsyntax-only",
                (char*)source,
                "-o",
                object,
                NULL,
                NULL,
                NULL,
                NULL,
            };
            unlink(object);
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(expected);
            const bool ready = oracle == 1 && counts.warnings == 0 && counts.errors == 3 && counts.notes == 1;
            check_test_expect(test, ready, "complex requirement fallback has three independent Clang assertion errors");
            for (uint32_t lane = 0; ready && lane < BUSTER_ARRAY_LENGTH(lanes); lane += 1)
            {
                arguments[0] = (char*)(executable != NULL ? executable : "/proc/self/exe");
                arguments[8] = "--lanes";
                arguments[9] = (char*)lanes[lane];
                arguments[10] = NULL;
                unlink(object);
                const bool prior = mode == 0 || parse_test_write(object, sentinel, sizeof(sentinel));
                char* actual = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                const CheckDriverDiagnosticCounts observed = check_test_driver_diagnostic_counts(actual);
                const bool same = status == oracle && actual != NULL && observed.warnings == counts.warnings &&
                                  observed.errors == counts.errors && observed.notes == counts.notes &&
                                  strstr(actual, ": active string") != NULL && strstr(actual, ": pointer string") != NULL &&
                                  strstr(actual, ": complex declarator") != NULL;
                check_test_expect(test, same, "complex requirement fallback preserves assertion diagnostics without crashing");
                if (!same)
                {
                    fprintf(stderr, "requirement fallback language=%u mode=%u lanes=%s: status %d (clang %d)\n%s\n",
                            language, mode, lanes[lane], status, oracle, actual != NULL ? actual : "<no diagnostics>");
                }
                if (mode != 0)
                {
                    FILE* retained = fopen(object, "rb");
                    char bytes[sizeof(sentinel)] = { 0 };
                    const bool unchanged = retained != NULL && fread(bytes, 1, sizeof(bytes), retained) == sizeof(bytes) &&
                                           memcmp(bytes, sentinel, sizeof(bytes)) == 0 && fgetc(retained) == EOF;
                    check_test_expect(test, prior && unchanged, "complex requirement fallback preserves an existing output file");
                    if (retained != NULL)
                    {
                        fclose(retained);
                    }
                }
                else
                {
                    check_test_expect(test, access(object, F_OK) != 0, "complex requirement fallback emits no syntax-only output");
                }
                free(actual);
            }
            free(expected);
            unlink(object);
        }
    }
}
