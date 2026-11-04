// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// Included by c_preprocessor_test.c. The header oracle accepts only a multiset subset of the reference's header
// warnings (#1131, #1228). Keep the complete location, message and warning group: matching only a warning group,
// basename or total count could hide a new warning in our headers. Include stacks and notes are presentation context.
// Both clang runs use -fno-caret-diagnostics -fno-color-diagnostics; no source excerpt can masquerade as a diagnostic.

typedef struct PPHeaderWarning PPHeaderWarning;
struct PPHeaderWarning
{
    const char* text;
    size_t length;
};

static int pp_test_header_warning_compare(const void* left, const void* right)
{
    const PPHeaderWarning a = *(const PPHeaderWarning*)left;
    const PPHeaderWarning b = *(const PPHeaderWarning*)right;
    const size_t length = a.length < b.length ? a.length : b.length;
    const int order = memcmp(a.text, b.text, length);
    return order != 0 ? order : (a.length > b.length) - (a.length < b.length);
}

// Collect immutable line slices, without truncation. The first severity on a line wins, so a note quoting
// "warning:" or "error:" cannot become a warning/error. Errors and unlocated driver warnings fail closed.
static bool pp_test_header_warning_lines(const char* text, const char* path, PPHeaderWarning** warnings, size_t* count)
{
    static const char* const severities[] =
    {
        ": warning: ",
        ": error: ",
        ": fatal error: ",
        ": note: ",
    };
    const char* const begin = text != NULL ? text : "";
    const size_t path_length = strlen(path);
    size_t capacity = 1;
    for (const char* at = begin; *at != 0; at += 1)
    {
        capacity += *at == '\n';
    }
    *count = 0;
    *warnings = capacity <= SIZE_MAX / sizeof(PPHeaderWarning) ? malloc(capacity * sizeof(PPHeaderWarning)) : NULL;
    bool result = *warnings != NULL;
    for (const char* line = begin; result && *line != 0;)
    {
        const size_t length = strcspn(line, "\n");
        const char* const end = line + length;
        const char* marker = end;
        size_t severity = 0;
        for (const char* at = line; marker == end && at < end;)
        {
            const char* found = memchr(at, ':', (size_t)(end - at));
            if (found == NULL)
            {
                break;
            }
            for (size_t s = 0; s < sizeof(severities) / sizeof(severities[0]); s += 1)
            {
                const size_t prefix = strlen(severities[s]);
                if ((size_t)(end - found) >= prefix && memcmp(found, severities[s], prefix) == 0)
                {
                    marker = found;
                    severity = s;
                    break;
                }
            }
            at = found + 1;
        }
        if (marker != end && severity != 3)
        {
            // Walk back over :line:column, allowing colons in the preceding filename.
            const char* name_end = marker;
            bool located = true;
            for (uint32_t field = 0; field < 2; field += 1)
            {
                const char* const digits_end = name_end;
                while (name_end > line && name_end[-1] >= '0' && name_end[-1] <= '9')
                {
                    name_end -= 1;
                }
                located = located && name_end < digits_end && name_end > line && name_end[-1] == ':';
                if (located)
                {
                    name_end -= 1;
                }
            }
            result = severity == 0 && located && name_end > line;
            const bool primary = result && (size_t)(name_end - line) == path_length && memcmp(line, path, path_length) == 0;
            if (result && !primary)
            {
                (*warnings)[*count] = (PPHeaderWarning){ .text = line, .length = length };
                *count += 1;
            }
        }
        line = end + (*end == '\n');
    }
    if (result && *count > 1)
    {
        qsort(*warnings, *count, sizeof(PPHeaderWarning), pp_test_header_warning_compare);
    }
    return result;
}

// Sorted multiset subtraction: reference-only warnings may disappear, but each candidate occurrence consumes one
// distinct matching reference occurrence. The inputs remain intact for the failure report and the primary check.
static bool pp_test_header_warnings(const char* reference, const char* candidate, const char* path)
{
    PPHeaderWarning* theirs = NULL;
    PPHeaderWarning* ours = NULL;
    size_t their_count = 0;
    size_t our_count = 0;
    const bool reference_ok = pp_test_header_warning_lines(reference, path, &theirs, &their_count);
    const bool candidate_ok = pp_test_header_warning_lines(candidate, path, &ours, &our_count);
    bool result = reference_ok && candidate_ok;
    size_t t = 0;
    for (size_t o = 0; result && o < our_count; o += 1)
    {
        while (t < their_count && pp_test_header_warning_compare(&theirs[t], &ours[o]) < 0)
        {
            t += 1;
        }
        result = t < their_count && pp_test_header_warning_compare(&theirs[t], &ours[o]) == 0;
        t += result;
    }
    free(theirs);
    free(ours);
    return result;
}

static bool pp_test_header_warning_fixtures(void)
{
    static const char warning[] = "/system/shared.h:9:2: warning: inherited [-Wexample]\n";
    static const char other[] = "/system/shared.h:9:2: warning: different [-Wexample]\n";
    static const char twice[] = "/system/shared.h:9:2: warning: inherited [-Wexample]\n/system/shared.h:9:2: warning: inherited [-Wexample]\n";
    static const struct
    {
        const char* reference;
        const char* candidate;
        bool expected;
    } cases[] =
    {
        { NULL, NULL, true },
        { warning, warning, true },
        { warning, "", true },
        { "", warning, false },
        { warning, other, false },
        { warning, twice, false },
        { twice, warning, true },
        { twice, twice, true },
        { warning, "/ours/shared.h:9:2: warning: inherited [-Wexample]\n", false },
        { warning, "/system/shared.h:10:2: warning: inherited [-Wexample]\n", false },
        { warning, "/system/shared.h:9:3: warning: inherited [-Wexample]\n", false },
        { warning, "/system/shared.h:9:2: warning: inherited [-Wother]\n", false },
        { warning, "/system/shared.h:9:2: warning: inherited [-Wexample]", true },
        { "", "/tmp/unit.c:1:1: warning: primary [-Wexample]\n", true },
        { "/tmp/unit.c:1:1: warning: primary [-Wexample]\n", warning, false },
        { "", "/tmp/unit.c.h:1:1: warning: primary [-Wexample]\n", false },
        { "", "/system/shared.h:1:1: note: says: warning: not a warning\n", true },
        { "", "/system/shared.h:1:1: note: says: error: not an error\n", true },
        { warning, "/system/shared.h:9:2: error: inherited [-Wexample]\n", false },
        { "/system/shared.h:9:2: error: broken\n", "", false },
        { "", "/tmp/unit.c:1:1: fatal error: broken\n", false },
        { "clang: warning: driver problem\n", "clang: warning: driver problem\n", false },
        { "", "clang: error: driver problem\n", false },
        { "", "/system/shared.h:x:2: warning: malformed location\n", false },
        { "", "/system/shared.h:2:x: warning: malformed location\n", false },
        { warning, "In file included from /ours/stdint.h:16:\n/system/shared.h:9:2: warning: inherited [-Wexample]\n<built-in>:213:9: note: previous definition is here\n", true },
        { "/a.h:1:1: warning: A [-Wa]\n/b.h:2:2: warning: B [-Wb]\n", "/b.h:2:2: warning: B [-Wb]\n/a.h:1:1: warning: A [-Wa]\n", true },
        { "/dir:colon/shared.h:1:2: warning: A [-Wa]\n", "/dir:colon/shared.h:1:2: warning: A [-Wa]\n", true },
    };
    bool result = true;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        const bool passed = pp_test_header_warnings(cases[i].reference, cases[i].candidate, "/tmp/unit.c") == cases[i].expected;
        if (!passed)
        {
            printf("header warning subtraction: fixture %zu failed\n", i);
        }
        result = result && passed;
    }
    // A difference beyond the old primary diagnostic buffer's 64 KiB must not be truncated into equality.
    const size_t length = 70000;
    char* long_warning = malloc(length + 1);
    char* changed = malloc(length + 1);
    bool long_ok = long_warning != NULL && changed != NULL;
    if (long_ok)
    {
        memset(long_warning, 'x', length);
        memcpy(long_warning, "/long.h:1:1: warning: ", 22);
        long_warning[length] = 0;
        memcpy(changed, long_warning, length + 1);
        changed[length - 1] = 'y';
        long_ok = pp_test_header_warnings(long_warning, long_warning, "/tmp/unit.c") && !pp_test_header_warnings(long_warning, changed, "/tmp/unit.c");
    }
    free(long_warning);
    free(changed);
    result = result && long_ok;
    printf("header warning subtraction: %s (locations, messages, multiplicity, notes, errors and untruncated lines)\n", result ? "pass" : "FAIL");
    return result;
}

// Real clang diagnostics, independent of the host libc: shared system-header warnings must subtract even though
// their include stacks differ. A local addition, an extra occurrence and a rejected preprocessing run must not pass.
static bool pp_test_header_warning_oracle(const char* directory)
{
    static const char* const variants[] =
    {
        "#include <warning-shared.h>\n",
        "",
        "#include <warning-shared.h>\n#warning additional header diagnostic\n",
        "#include <warning-shared.h>\n#include <warning-shared.h>\n",
        "#include <warning-shared.h>\n#error rejected header\n",
    };
    char reference_directory[4200];
    char candidate_directory[4200];
    char reference_header[4300];
    char candidate_header[4300];
    char shared[4200];
    char unit[4200];
    snprintf(reference_directory, sizeof(reference_directory), "%s/warning-reference", directory);
    snprintf(candidate_directory, sizeof(candidate_directory), "%s/warning-candidate", directory);
    snprintf(reference_header, sizeof(reference_header), "%s/fixture.h", reference_directory);
    snprintf(candidate_header, sizeof(candidate_header), "%s/fixture.h", candidate_directory);
    snprintf(shared, sizeof(shared), "%s/warning-shared.h", directory);
    snprintf(unit, sizeof(unit), "%s/warning-unit.c", directory);
    const bool reference_made = mkdir(reference_directory, 0700) == 0;
    const bool candidate_made = mkdir(candidate_directory, 0700) == 0;
    bool result = reference_made && candidate_made && pp_test_write_file(reference_header, variants[0]) &&
                  pp_test_write_file(shared, "#warning inherited header diagnostic\n") && pp_test_write_file(unit, "#include <fixture.h>\n");
    for (uint32_t configuration = 0; result && configuration < 4; configuration += 1)
    {
        char* arguments[] =
        {
            configuration < 2 ? "-std=c89" : "-std=gnu17",
            "-isystem",
            reference_directory,
            "-isystem",
            (char*)directory,
            "-Wsystem-headers",
            "-fno-caret-diagnostics",
            "-fno-color-diagnostics",
            "-P",
            "-ffreestanding",
        };
        const int count = configuration % 2 != 0 ? 10 : 9;
        PPClangOutput reference = pp_test_clang_process(pp_test_clang_path, arguments, count, unit, false);
        // Require an actual baseline warning; otherwise a broken fixture could pass every positive case vacuously.
        result = reference.status == PP_CLANG_ACCEPTED && !pp_test_header_warnings("", reference.diagnostics, unit);
        arguments[2] = candidate_directory;
        for (uint32_t variant = 0; result && variant < sizeof(variants) / sizeof(variants[0]); variant += 1)
        {
            const bool written = pp_test_write_file(candidate_header, variants[variant]);
            PPClangOutput candidate = written ? pp_test_clang_process(pp_test_clang_path, arguments, count, unit, false) : (PPClangOutput){ 0 };
            const bool accepted = candidate.status == PP_CLANG_ACCEPTED && pp_test_header_warnings(reference.diagnostics, candidate.diagnostics, unit);
            const bool expected_status = candidate.status == (variant == 4 ? PP_CLANG_REJECTED : PP_CLANG_ACCEPTED);
            const bool passed = written && expected_status && accepted == (variant < 2);
            if (!passed)
            {
                printf("header warning oracle: configuration %u, variant %u failed\nreference:\n%s\ncandidate:\n%s\n", configuration, variant,
                       reference.diagnostics != NULL ? reference.diagnostics : "", candidate.diagnostics != NULL ? candidate.diagnostics : "");
            }
            result = result && passed;
            free(candidate.text);
            free(candidate.diagnostics);
        }
        free(reference.text);
        free(reference.diagnostics);
    }
    unlink(reference_header);
    unlink(candidate_header);
    unlink(shared);
    unlink(unit);
    if (reference_made)
    {
        result = rmdir(reference_directory) == 0 && result;
    }
    if (candidate_made)
    {
        result = rmdir(candidate_directory) == 0 && result;
    }
    printf("header warning oracle: %s (inherited, removed, new, duplicate and rejected; C89/GNU17, hosted/freestanding)\n", result ? "pass" : "FAIL");
    return result;
}
