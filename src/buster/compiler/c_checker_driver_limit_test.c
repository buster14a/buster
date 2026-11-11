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
    uint32_t first_error_line;
    uint32_t last_error_line;
    bool after_fatal;
    char summary[128];
};

// Ignore Clang's source excerpts and carets, but independently compare published severities, the first and last
// original error's source line, attached notes and the complete summary. Do not infer any count from the summary.
// Each line is found and parsed within its own bytes: strchr and sscanf on the rest of the text read all of it per
// line (AddressSanitizer's strict string checks measure it, and the C library's sscanf too), which made this walk
// quadratic in the output, 2,000 s of a 16th of test-checker-driver under ASan against 18 s plain (#2030).
static CheckDriverDiagnosticCounts check_test_driver_diagnostic_counts(const char* text)
{
    CheckDriverDiagnosticCounts counts = { 0 };
    const char* stop = text != NULL ? text + strlen(text) : NULL;
    for (const char* at = text; at != NULL && at < stop;)
    {
        const char* end = memchr(at, '\n', (size_t)(stop - at));
        const size_t length = end != NULL ? (size_t)(end - at) : (size_t)(stop - at);
        const bool warned = memmem(at, length, ": warning: ", 11) != NULL;
        const bool failed = memmem(at, length, ": error: ", 9) != NULL;
        const bool stopped = memmem(at, length, "fatal error: ", 13) != NULL;
        const bool noted = memmem(at, length, ": note: ", 8) != NULL;
        // Notes attached to the fatal diagnostic follow it; only a later primary diagnostic is post-fatal.
        counts.after_fatal |= counts.fatal != 0 && (warned || failed || stopped);
        counts.warnings += warned;
        counts.errors += failed || stopped;
        counts.fatal += stopped;
        counts.notes += noted;
        if (failed || stopped)
        {
            // A locationless fatal (the -ferror-limit cutoff) keeps the last located error's line. The pattern stops
            // within the line, whose first colon a space follows, so a copy of the line parses as the text does.
            uint32_t column = 0;
            uint32_t line = 0;
            char copy[256];
            const char* parsed = at;
            if (length < sizeof(copy))
            {
                memcpy(copy, at, length);
                copy[length] = 0;
                parsed = copy;
            }
            if (sscanf(parsed, "%*[^:]:%u:%u:", &line, &column) == 2 && line != 0)
            {
                counts.first_error_line = counts.first_error_line != 0 ? counts.first_error_line : line;
                counts.last_error_line = line;
            }
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

// Compose system visibility with the publication budget and attached-note entry point. The second fixture keeps
// an invocation open across a system-header exit, forcing canonical replay before the final counts are published.
static void check_test_driver_system_error_limits(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[PATH_MAX];
    char header[PATH_MAX];
    char object[PATH_MAX];
    snprintf(source, sizeof(source), "%s/system-error-limit.c", directory);
    snprintf(header, sizeof(header), "%s/error-limit-system.h", directory);
    snprintf(object, sizeof(object), "%s/system-error-limit.o", directory);
    const char* sources[] =
    {
        "#include <error-limit-system.h>\nint untouched;\n",
        "#include <error-limit-system.h>\nb)\n#error first\n#error second\n",
    };
    const char* headers[] =
    {
        "#define VALUE 1\n#define VALUE 2\n#pragma message(\"message\")\n#warning kept\n#error first\n#pragma GCC warning \"after-first\"\n#error second\n",
        "#define F(x) _Pragma(\"message(\\\"cross\\\")\")\n#warning kept\nF(a\n",
    };
    const char* policies[][2] =
    {
        { NULL, NULL },
        { "-Wsystem-headers", NULL },
        { "-Wno-system-headers", NULL },
        { "-Wsystem-headers", "-Wno-system-headers" },
        { "-Wno-system-headers", "-Wsystem-headers" },
        { "-Wsystem-headers", "-w" },
        { "-w", "-Wsystem-headers" },
        { "-Wsystem-headers", "-fno-caret-diagnostics" },
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(sources); fixture += 1)
    {
        const bool made = parse_test_write(source, sources[fixture], strlen(sources[fixture])) &&
                          parse_test_write(header, headers[fixture], strlen(headers[fixture]));
        check_test_expect(test, made, "system visibility and error-limit fixtures can be written");
        for (uint32_t policy = 0; made && policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
        {
            for (uint32_t route = 0; route < 6; route += 1)
            {
                char* arguments[16] = { (char*)clang, "-std=gnu17", route < 4 ? "-fsyntax-only" : "-E", "-fno-color-diagnostics", "-ferror-limit=1", "-isystem", (char*)directory };
                uint32_t count = 7;
                char* checked[16] = { (char*)(executable != NULL ? executable : "/proc/self/exe") };
                uint32_t used = 1;
                if (route >= 4)
                {
                    checked[used++] = "-E";
                }
                checked[used++] = "-std=gnu17";
                checked[used++] = "-ferror-limit=1";
                checked[used++] = "-isystem";
                checked[used++] = (char*)directory;
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
                for (uint32_t flag = 0; flag < BUSTER_ARRAY_LENGTH(policies[policy]); flag += 1)
                {
                    if (policies[policy][flag] != NULL)
                    {
                        arguments[count++] = (char*)policies[policy][flag];
                        checked[used++] = (char*)policies[policy][flag];
                    }
                }
                arguments[count++] = source;
                checked[used++] = source;
                char* text = NULL;
                const int oracle = check_test_driver_spawn(arguments, &text);
                const CheckDriverDiagnosticCounts expected = check_test_driver_diagnostic_counts(text);
                free(text);
                text = NULL;
                const int status = check_test_driver_spawn(checked, &text);
                const CheckDriverDiagnosticCounts actual = check_test_driver_diagnostic_counts(text);
                const bool show = policy == 1 || policy == 4 || policy == 7;
                const bool quiet = policy == 5 || policy == 6;
                const uint32_t warnings = quiet ? 0 : show ? (fixture == 0 ? 4u : 2u) : 1u;
                char description[192];
                snprintf(description, sizeof(description), "system visibility before fatal publication fixture=%u policy=%u route=%u", fixture, policy, route);
                check_test_expect(test, oracle == 1 && status == oracle && expected.errors == 2 && expected.fatal == 1 && expected.warnings == warnings &&
                                  (expected.notes != 0) == show && actual.warnings == expected.warnings && actual.errors == expected.errors &&
                                  actual.fatal == expected.fatal && actual.notes == expected.notes && actual.last_error_line == expected.last_error_line &&
                                  !actual.after_fatal && strcmp(actual.summary, expected.summary) == 0 && access(object, F_OK) != 0, description);
                free(text);
                unlink(object);
            }
        }
    }
    unlink(source);
    unlink(header);
    unlink(object);
}

// -Wfatal-errors makes the first error, a warning mapped to one included, clang's fatal error and stops publication
// there; -Wno-fatal-errors restores the ordinary error budget. Clang is the oracle for each stage's counts and summary,
// and the last option on the line decides, as it does for Clang.
static void check_test_driver_fatal_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(source, sizeof(source), "%s/fatal-errors.c", directory);
    snprintf(object, sizeof(object), "%s/fatal-errors.o", directory);
    static const struct
    {
        const char* source;
        bool promoted;
        // Clang's code generation also checks an x86 builtin's target features, which the range fixture needs; its
        // default-error policy is compared by -fsyntax-only alone.
        bool codegen;
        // A fixture whose nonfatal form has other pre-existing diagnostic differences is compared only where the
        // fatal cutoff applies.
        bool fatal_only;
    } fixtures[] =
    {
        { "int f(void){return x;}\nint g(void){return y;}\n", false, true },
        { "int f(void){ return 1 }\nint g(void){ return 2 }\n", false, true },
        { "#error one\n#error two\n", false, true },
        { "int f(a) int a; { return a; }\nint g(b) int b; { return b; }\n", true, true },
        { "int f(void){return 1 << 32;}\n", false, true },
        { "#pragma clang diagnostic error \"-Wshift-count-overflow\"\n"
          "int f(void){return 1 << 32;}\nint g(void){return 1 << 33;}\n", false, true },
        { "typedef char V __attribute__((vector_size(16)));\n"
          "unsigned f(V a){return __builtin_ia32_cmpb128_mask(a,a,8,65535);}\n"
          "unsigned g(V a){return __builtin_ia32_cmpb128_mask(a,a,9,65535);}\n", false, false },
        { "int f(void){ return 1 }\n#error later\n", false, true },
        { "int f(void){ return 1 }\n#warning later\n", false, true },
        { "#warning before\nint f(void){return x;}\n", false, true },
        { "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint f(void)\n{\nreturn 0\n#pragma STDC FENV_ACCESS MAYBE\n}\n", false, true },
        { "typedef char V __attribute__((vector_size(16)));\n"
          "#pragma clang diagnostic error \"-Wargument-outside-range\"\n"
          "unsigned f(V a){return __builtin_ia32_cmpb128_mask(a,a,8,65535);}\n"
          "unsigned g(V a){return __builtin_ia32_cmpb128_mask(a,a,9,65535);}\n", false, false },
        { "_Complex x;\n", true, true },
        { "#define F(x) x\nint a = F(1;\n", false, true, true },
    };
    const char* policies[][2] =
    {
        { "-Wfatal-errors", NULL },
        { "-Wno-fatal-errors", NULL },
        { "-Wfatal-errors", "-Wno-fatal-errors" },
        { "-Wno-fatal-errors", "-Wfatal-errors" },
        { "-Wfatal-errors", "-ferror-limit=1" },
        { "-Wno-fatal-errors", "-ferror-limit=1" },
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        const bool made = parse_test_write(source, fixtures[fixture].source, strlen(fixtures[fixture].source));
        check_test_expect(test, made, "fatal-errors fixture can be written");
        for (uint32_t policy = 0; made && policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
        {
            bool policy_fatal = false;
            for (uint32_t flag = 0; flag < BUSTER_ARRAY_LENGTH(policies[policy]); flag += 1)
            {
                if (policies[policy][flag] != NULL && strcmp(policies[policy][flag], "-Wfatal-errors") == 0)
                {
                    policy_fatal = true;
                }
                else if (policies[policy][flag] != NULL && strcmp(policies[policy][flag], "-Wno-fatal-errors") == 0)
                {
                    policy_fatal = false;
                }
            }
            if (fixtures[fixture].fatal_only && !policy_fatal)
            {
                continue;
            }
            for (uint32_t route = 0; route < 6; route += 1)
            {
                if (!fixtures[fixture].codegen && route >= 2 && route < 4)
                {
                    continue;
                }
                char* arguments[20] = { (char*)clang, "-std=gnu17", "-fno-color-diagnostics" };
                uint32_t count = 3;
                if (route < 2)
                {
                    arguments[count++] = "-fsyntax-only";
                }
                else if (route < 4)
                {
                    arguments[count++] = "-c";
                    arguments[count++] = "-o";
                    arguments[count++] = object;
                }
                else
                {
                    arguments[count++] = "-E";
                }
                if (fixtures[fixture].promoted)
                {
                    arguments[count++] = "-Werror";
                }
                for (uint32_t flag = 0; flag < BUSTER_ARRAY_LENGTH(policies[policy]); flag += 1)
                {
                    if (policies[policy][flag] != NULL)
                    {
                        arguments[count++] = (char*)policies[policy][flag];
                    }
                }
                arguments[count++] = source;
                char* text = NULL;
                const int oracle = check_test_driver_spawn(arguments, &text);
                const CheckDriverDiagnosticCounts expected = check_test_driver_diagnostic_counts(text);
                free(text);
                char* checked[20] = { (char*)(executable != NULL ? executable : "/proc/self/exe") };
                uint32_t used = 1;
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
                if (fixtures[fixture].promoted)
                {
                    checked[used++] = "-Werror";
                }
                for (uint32_t flag = 0; flag < BUSTER_ARRAY_LENGTH(policies[policy]); flag += 1)
                {
                    if (policies[policy][flag] != NULL)
                    {
                        checked[used++] = (char*)policies[policy][flag];
                    }
                }
                checked[used++] = source;
                text = NULL;
                const int status = check_test_driver_spawn(checked, &text);
                const CheckDriverDiagnosticCounts actual = check_test_driver_diagnostic_counts(text);
                char description[192];
                snprintf(description, sizeof(description), "fatal-errors fixture=%u policy=%u route=%u: warning/error/fatal/note %u/%u/%u/%u vs Clang %u/%u/%u/%u",
                         fixture, policy, route, actual.warnings, actual.errors, actual.fatal, actual.notes,
                         expected.warnings, expected.errors, expected.fatal, expected.notes);
                check_test_expect(test, status == oracle && actual.warnings == expected.warnings && actual.errors == expected.errors &&
                                  actual.fatal == expected.fatal && actual.notes == expected.notes &&
                                  actual.first_error_line == expected.first_error_line && actual.last_error_line == expected.last_error_line &&
                                  !actual.after_fatal && strcmp(actual.summary, expected.summary) == 0, description);
                free(text);
                unlink(object);
            }
        }
    }
    // The flag suffix follows the published severity: a promoted warning names its flag while it is an error and
    // omits -Werror once it is fatal. Clang is the oracle for both.
    {
        static const char complex[] = "_Complex x;\n";
        const bool written = parse_test_write(source, complex, sizeof(complex) - 1);
        check_test_expect(test, written, "fatal-errors suffix fixture can be written");
        char* fatal_arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", "-Werror", "-Wfatal-errors", source, NULL };
        char* error_arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", "-Werror", source, NULL };
        char* checked_fatal[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", "-Werror", "-Wfatal-errors", source, NULL };
        char* checked_error[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", "-Werror", source, NULL };
        char* text = NULL;
        const int oracle_fatal = check_test_driver_spawn(fatal_arguments, &text);
        const bool oracle_fatal_text = oracle_fatal == 1 && text != NULL && strstr(text, "fatal error: plain '_Complex'") != NULL &&
                                       strstr(text, "[-Werror]") == NULL;
        free(text);
        text = NULL;
        const int status_fatal = check_test_driver_spawn(checked_fatal, &text);
        const bool actual_fatal_text = status_fatal == 1 && text != NULL && strstr(text, "fatal error: plain '_Complex'") != NULL &&
                                       strstr(text, "[-Werror]") == NULL;
        free(text);
        text = NULL;
        const int oracle_error = check_test_driver_spawn(error_arguments, &text);
        const bool oracle_error_text = oracle_error == 1 && text != NULL && strstr(text, "error: plain '_Complex'") != NULL &&
                                       strstr(text, "[-Werror]") != NULL;
        free(text);
        text = NULL;
        const int status_error = check_test_driver_spawn(checked_error, &text);
        const bool actual_error_text = status_error == 1 && text != NULL && strstr(text, "error: plain '_Complex'") != NULL &&
                                       strstr(text, "[-Werror]") != NULL;
        check_test_expect(test, oracle_fatal_text && actual_fatal_text && oracle_error_text && actual_error_text,
                          "a promoted event warning keeps [-Werror] while it is an error and omits it once fatal");
        free(text);
    }
    {
        static const char range[] = "typedef char V __attribute__((vector_size(16)));\n"
                                    "unsigned f(V a){return __builtin_ia32_cmpb128_mask(a,a,8,65535);}\n";
        const bool written = parse_test_write(source, range, sizeof(range) - 1);
        check_test_expect(test, written, "fatal-errors range suffix fixture can be written");
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", "-Wfatal-errors", source, NULL };
        char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", "-Wfatal-errors", source, NULL };
        char* text = NULL;
        const int oracle = check_test_driver_spawn(arguments, &text);
        const bool oracle_text = oracle == 1 && text != NULL &&
                                 strstr(text, "fatal error: argument value 8 is outside the valid range [0, 7] [-Wargument-outside-range]") != NULL;
        free(text);
        text = NULL;
        const int status = check_test_driver_spawn(checked, &text);
        const bool actual_text = status == 1 && text != NULL &&
                                 strstr(text, "fatal error: argument value 8 is outside the valid range [0, 7] [-Wargument-outside-range]") != NULL;
        check_test_expect(test, oracle_text && actual_text, "a fatal default-error warning keeps its flag suffix");
        free(text);
    }
    unlink(source);
    unlink(object);
}

// The diagnostic lines of a driver's output (#1970): its warnings, errors and notes, and the include stacks before them,
// into `lines`; false where they don't fit.
static bool check_test_driver_preprocessor_lines(const char* text, char* lines, size_t capacity)
{
    size_t length = 0;
    bool fits = capacity != 0;
    lines[0] = 0;
    const char* stop = text != NULL ? text + strlen(text) : NULL;
    for (const char* line = text; fits && line != NULL && line < stop;)
    {
        const char* end = memchr(line, '\n', (size_t)(stop - line));
        const size_t size = end != NULL ? (size_t)(end - line) : (size_t)(stop - line);
        const bool kept = memmem(line, size, ": error: ", 9) != NULL || memmem(line, size, ": warning: ", 11) != NULL ||
                          memmem(line, size, ": note: ", 8) != NULL || (size > 22 && memcmp(line, "In file included from ", 22) == 0);
        fits = !kept || size + 1 < capacity - length;
        if (kept && fits)
        {
            memcpy(lines + length, line, size);
            length += size;
            lines[length++] = '\n';
            lines[length] = 0;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return fits;
}

// The preprocessor's warnings with clang's flags, options and pragmas (#1790, #1970), and the include stack after an
// attached note in another header (#1970): the whole diagnostic output, clang's and ours, of each fixture under each
// option set, with real includes and with line markers.
static void check_test_driver_preprocessor_flags(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char include[512];
    char path[640];
    snprintf(include, sizeof(include), "%s/pp-flags", directory);
    bool written = (mkdir(include, 0700) == 0 || errno == EEXIST);
    snprintf(path, sizeof(path), "%s/a.h", include);
    written = written && parse_test_write(path, "#define M 1\n", 12);
    snprintf(path, sizeof(path), "%s/b.h", include);
    static const char b[] = "#define M 2\nint m(void) { return 1 << 36; }\n";
    written = written && parse_test_write(path, b, sizeof(b) - 1);
    snprintf(path, sizeof(path), "%s/s.h", include);
    static const char s[] = "#warning in a system header\n#pragma message(\"hidden there\")\n#define S 1\n#define S 2\n";
    written = written && parse_test_write(path, s, sizeof(s) - 1);
    static const char* const sources[] =
    {
        "#include \"pp-flags/a.h\"\n#include \"pp-flags/b.h\"\n",
        "# 1 \"x.c\"\n# 1 \"./pp-flags/a.h\" 1\n#define M 1\n# 2 \"x.c\" 2\n# 1 \"./pp-flags/b.h\" 1\n#define M 2\n"
        "int m(void) { return 1 << 36; }\n# 3 \"x.c\" 2\n",
        "#include <s.h>\n#warning text\n#pragma message(\"message\")\n#pragma GCC warning \"gcc\"\n#define A 1\n#define A 2\n"
        "#ifdef A junk\n#endif junk\n#pragma clang diagnostic push\n#pragma clang diagnostic error \"-W#pragma-messages\"\n"
        "#pragma message(\"promoted\")\n#pragma clang diagnostic pop\n#pragma clang diagnostic ignored \"-W#warnings\"\n#warning ignored\n"
        "#pragma clang diagnostic warning \"-W#warnings\"\n#pragma clang diagnostic pop\n#define B@\nint x = 'ab';\n",
    };
    static const char* const options[][3] =
    {
        { NULL },
        { "-Werror", NULL },
        { "-w", NULL },
        { "-Wno-#warnings", "-Wno-macro-redefined", NULL },
        { "-Werror=#pragma-messages", "-Wno-error=extra-tokens", NULL },
        { "-Werror", "-Wno-error=#warnings", NULL },
        { "-pedantic-errors", NULL },
        { "-Wno-all", "-Wsystem-headers", NULL },
        { "-std=c23", "-pedantic", NULL },
        { "-Wno-everything", NULL },
    };
    char source[640];
    snprintf(source, sizeof(source), "%s/pp-flags.c", directory);
    for (uint32_t f = 0; written && f < BUSTER_ARRAY_LENGTH(sources); f += 1)
    {
        const bool fixture = parse_test_write(source, sources[f], strlen(sources[f]));
        for (uint32_t o = 0; fixture && o < BUSTER_ARRAY_LENGTH(options); o += 1)
        {
            // The line markers' fixture leaves out -pedantic's sets: clang's -Wgnu-line-marker isn't given yet (#2222).
            const bool compared = !(f == 1 && (o == 6 || o == 8));
            char* theirs_arguments[16] = { (char*)clang, "-fsyntax-only", "-std=gnu17", "-fno-color-diagnostics", "-isystem", include };
            char* ours_arguments[16] = { (char*)executable, "-fsyntax-only", "-std=gnu17", "-isystem", include };
            uint32_t theirs_count = 6;
            uint32_t ours_count = 5;
            for (uint32_t k = 0; k < 3 && options[o][k] != NULL; k += 1)
            {
                theirs_arguments[theirs_count++] = (char*)options[o][k];
                ours_arguments[ours_count++] = (char*)options[o][k];
            }
            theirs_arguments[theirs_count] = source;
            ours_arguments[ours_count] = source;
            char* theirs = NULL;
            char* ours = NULL;
            const int theirs_status = compared ? check_test_driver_spawn(theirs_arguments, &theirs) : 0;
            const int ours_status = compared ? check_test_driver_spawn(ours_arguments, &ours) : 0;
            char expected[8192];
            char actual[8192];
            const bool read = check_test_driver_preprocessor_lines(theirs, expected, sizeof(expected)) && check_test_driver_preprocessor_lines(ours, actual, sizeof(actual));
            char description[20000];
            snprintf(description, sizeof(description), "preprocessor flags: fixture %u, options %u: status %d (clang %d), diagnostics\n%s(clang's\n%s)", f, o,
                     ours_status, theirs_status, actual, expected);
            check_test_expect(test, !compared || (read && theirs != NULL && ours_status == theirs_status && strcmp(actual, expected) == 0), description);
            free(theirs);
            free(ours);
        }
    }
    check_test_expect(test, written, "preprocessor flags: the fixtures are written");
    unlink(source);
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
                if (check_test_driver_takes())
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
            if (check_test_driver_takes())
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
    }
    const char* values[] = { "", "-1", "+1", "word", "1x", "4294967296", "08", "0x", "0b", "0o" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(values); i += 1)
    {
        if (check_test_driver_takes())
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
            if (check_test_driver_takes())
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
        if (check_test_driver_takes())
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
    }
    unlink(source);
    unlink(object);
    if (check_test_driver_takes())
    {
        check_test_driver_fatal_errors(test, clang, executable, directory);
    }
    if (check_test_driver_takes())
    {
        check_test_driver_preprocessor_flags(test, clang, executable, directory);
    }
    if (check_test_driver_takes())
    {
        check_test_driver_system_error_limits(test, clang, executable, directory);
    }
}
