// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1391 control-flow regressions, read as runtime data. Quiet and definitely-uninitialized cases compare
// complete Clang diagnostics; unsafe joins check the existing -Wuninitialized contract independently, since Clang
// can classify them as -Wsometimes-uninitialized instead. The marked read pins the unsafe use, not just a count.

typedef struct CheckDriverUninitializedFixture CheckDriverUninitializedFixture;
struct CheckDriverUninitializedFixture
{
    const char* name;
    // Null means quiet; otherwise one diagnostic and its initialization note, at the fixture's marked read.
    const char* variable;
    bool clang_lines;
};

// Each unsafe fixture has one explicit expected-use marker directly before its variable. Derive the source position
// from the fixture bytes, independently of either compiler's graph or control-flow analysis.
static bool check_test_uninitialized_location(const char* source, const char* path, const char* variable, const char* text, bool error)
{
    static const char marker[] = "/* uninitialized */";
    const char* at = strstr(source, marker);
    const char* use = at != NULL ? at + sizeof(marker) - 1 : NULL;
    bool found = use != NULL && strstr(use, marker) == NULL && strncmp(use, variable, strlen(variable)) == 0;
    if (found)
    {
        uint32_t line = 1;
        uint32_t column = 1;
        for (const char* p = source; p < use; p += 1)
        {
            line += *p == '\n';
            column = *p == '\n' ? 1 : column + 1;
        }
        char diagnostic[PATH_MAX + 256];
        const int length = snprintf(diagnostic, sizeof(diagnostic), "%s:%u:%u: %s: variable '%s' is uninitialized when used here",
                                    path, line, column, error ? "error" : "warning", variable);
        found = length >= 0 && (size_t)length < sizeof(diagnostic) && text != NULL && strstr(text, diagnostic) != NULL;
    }
    return found;
}

static void check_test_driver_uninitialized(CheckTest* test, const char* clang, const char* executable, const char* directory, bool require_deferred)
{
    static const CheckDriverUninitializedFixture cases[] =
    {
        {
            .name = "reported-path",
            .clang_lines = true,
        },
        {
            .name = "reported-components",
            .clang_lines = true,
        },
        {
            .name = "body-write",
            .clang_lines = true,
        },
        {
            .name = "continue-after",
            .clang_lines = true,
        },
        {
            .name = "break-before",
            .clang_lines = true,
        },
        {
            .name = "return-before",
            .clang_lines = true,
        },
        {
            .name = "branch-writes",
            .clang_lines = true,
        },
        {
            .name = "nested",
            .clang_lines = true,
        },
        {
            .name = "shadowed",
            .clang_lines = true,
        },
        {
            .name = "comma-write",
            .clang_lines = true,
        },
        {
            .name = "and-left-write",
            .clang_lines = true,
        },
        {
            .name = "or-left-write",
            .clang_lines = true,
        },
        {
            .name = "conditional-writes",
            .clang_lines = true,
        },
        {
            .name = "goto-write",
            .clang_lines = true,
        },
        {
            .name = "goto-exit",
            .clang_lines = true,
        },
        {
            .name = "switch-writes",
            .clang_lines = true,
        },
        {
            .name = "inner-continue",
            .clang_lines = true,
        },
        {
            .name = "continue-before",
            .variable = "step",
        },
        {
            .name = "conditional-write",
            .variable = "step",
        },
        {
            .name = "and-right-write",
            .variable = "step",
        },
        {
            .name = "or-right-write",
            .variable = "step",
        },
        {
            .name = "conditional-one-write",
            .variable = "step",
        },
        {
            .name = "goto-bypass",
            .variable = "step",
        },
        {
            .name = "switch-continue",
            .variable = "step",
        },
        {
            .name = "after-loop",
            .variable = "step",
        },
        {
            .name = "break-after-loop",
            .variable = "step",
        },
        {
            .name = "body-read",
            .variable = "step",
        },
        {
            .name = "condition-first",
            .variable = "step",
        },
        {
            .name = "never-written",
            .variable = "step",
            .clang_lines = true,
        },
        {
            .name = "shadow-unwritten",
            .variable = "step",
            .clang_lines = true,
        },
        {
            .name = "known-zero",
            .variable = "step",
            .clang_lines = true,
        },
        {
            .name = "before-loop",
            .variable = "step",
            .clang_lines = true,
        },
        {
            .name = "increment-readwrite",
            .variable = "step",
            .clang_lines = true,
        },
        {
            .name = "wide-safe",
            .clang_lines = true,
        },
        {
            .name = "wide-bit63",
            .variable = "v63",
        },
        {
            .name = "wide-bit64",
            .variable = "v64",
        },
    };
    static const char* const policies[] = { NULL, "-Werror=uninitialized", "-Wno-uninitialized", "-w" };
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(source, sizeof(source), "%s/uninitialized-for.c", directory);
    snprintf(object, sizeof(object), "%s/uninitialized-for.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        bool selected[BUSTER_ARRAY_LENGTH(policies)];
        bool any = false;
        for (uint32_t policy = 0; policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
        {
            selected[policy] = check_test_driver_takes();
            any |= selected[policy];
        }
        if (!any)
        {
            continue;
        }
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "tests/driver/uninitialized-for/%s.c", cases[c].name);
        const int file = open(path, O_RDONLY | O_CLOEXEC);
        char* fixture = NULL;
        const bool read = file >= 0 && check_test_driver_read_file(file, &fixture);
        if (file >= 0)
        {
            close(file);
        }
        char unit[16384];
        // A separate body permits real deferred admission; one source body with the option can silently fall back.
        const int length = read ? snprintf(unit, sizeof(unit), "%s\nint uninitialized_anchor(void) { return 0; }\n", fixture) : -1;
        const bool written = length >= 0 && (size_t)length < sizeof(unit) && parse_test_write(source, unit, (size_t)length);
        char description[256];
        snprintf(description, sizeof(description), "uninitialized %s runtime fixture is available", cases[c].name);
        check_test_expect(test, written, description);
        for (uint32_t policy = 0; written && policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
        {
            if (!selected[policy])
            {
                continue;
            }
            const uint32_t expected = policy < 2 && cases[c].variable != NULL ? 1u : 0u;
            const uint32_t warnings = policy == 0 ? expected : 0;
            const uint32_t errors = policy == 1 ? expected : 0;
            const int expected_status = errors != 0 ? 1 : 0;
            char reference[4096] = { 0 };
            bool reference_fits = false;
            // Standard driver parts have one CPU. Their default/scalar routes are supplemented by the focused
            // --uninitialized-only run under the correctness inventory's existing two-CPU diagnostic grant.
            const uint32_t routes = require_deferred ? 4u : 3u;
            for (uint32_t route = cases[c].clang_lines ? 0u : 1u; route < routes; route += 1)
            {
                char* arguments[24] =
                {
                    (char*)(route == 0 ? clang : executable != NULL ? executable : "/proc/self/exe"),
                    "-std=gnu17",
                    "-c",
                    "-fno-caret-diagnostics",
                    "-fno-color-diagnostics",
                    "-ferror-limit=0",
                    "-Wall",
                    "-Wextra",
                    "-Wuninitialized",
                    source,
                    "-o",
                    object,
                };
                uint32_t used = 12;
                if (policies[policy] != NULL)
                {
                    arguments[used++] = (char*)policies[policy];
                }
                if (route == 2)
                {
                    arguments[used++] = "--scalar";
                }
                else if (route == 3)
                {
                    arguments[used++] = "--deferred-bodies";
                    arguments[used++] = "--lanes";
                    arguments[used++] = "2";
                    arguments[used++] = "--worker-budget";
                    arguments[used++] = "2";
                    arguments[used++] = "--time-stages";
                }
                arguments[used] = NULL;
                unlink(object);
                char* text = NULL;
                const int status = check_test_driver_spawn(arguments, &text);
                char lines[4096] = { 0 };
                const bool fit = check_test_driver_common_output(text != NULL ? text : "", lines, sizeof(lines));
                const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(text);
                if (route == 0)
                {
                    reference_fits = fit;
                    memcpy(reference, lines, strlen(lines) + 1);
                }
                const bool same = !cases[c].clang_lines || route == 0 || (reference_fits && strcmp(reference, lines) == 0);
                const bool located = expected == 0 || check_test_uninitialized_location(unit, source, cases[c].variable, text, policy == 1);
                const bool deferred = route != 3 || (text != NULL && strstr(text, "time-check\tdeferred-lanes\t2\n") != NULL &&
                                                     strstr(text, "time-check\tdeferred-merged\tyes\n") != NULL);
                const bool good = fit && text != NULL && status == expected_status && counts.warnings == warnings &&
                                  counts.errors == errors && counts.notes == expected && counts.fatal == 0 && same && located && deferred &&
                                  (access(object, F_OK) == 0) == (status == 0);
                snprintf(description, sizeof(description), "uninitialized %s policy=%u route=%u: counts, use, status%s%s", cases[c].name, policy, route,
                         cases[c].clang_lines ? ", Clang lines" : ", independent unsafe-path control", route == 3 ? ", admitted lanes" : "");
                check_test_expect(test, good, description);
                if (!good)
                {
                    fprintf(stderr, "  %s\n  status %d expected %d; warnings %u expected %u; errors %u expected %u; notes %u expected %u; use %u; deferred %u\nClang lines:\n%s\nActual output:\n%s\n",
                            description, status, expected_status, counts.warnings, warnings, counts.errors, errors, counts.notes, expected, located, deferred,
                            reference, text != NULL ? text : "(missing)");
                }
                free(text);
            }
        }
        free(fixture);
    }
    unlink(object);
    unlink(source);
}
