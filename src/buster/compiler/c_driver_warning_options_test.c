// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original real-driver regressions for unknown warning names (#1467) and missing capabilities (#1365). Unknown-name
// diagnostics have independent names, severities and counts and a Clang text oracle, excluding its optional hint.
// Capability errors have Buster's explicit refusal contract rather than an invented Clang counterpart.
// This file is included after the CC compatibility process helpers; no production option classifier is its oracle.

typedef struct CCWarningOptionCase CCWarningOptionCase;
struct CCWarningOptionCase
{
    const char* arguments[8];
    const char* message;
    uint32_t warnings;
    uint32_t errors;
    bool fatal;
};

typedef struct CCWarningCapabilityCase CCWarningCapabilityCase;
struct CCWarningCapabilityCase
{
    const char* arguments[6];
    const char* refused;
    const char* family;
};

// Only Clang's optional hint on an unknown-option line is omitted. Keep every other byte, including the diagnosed
// option, severity, order and summary. Buster's frozen __has_warning catalog cannot distinguish ignored groups,
// which Clang excludes when choosing its suggestions.
static void cc_warning_option_hint(char* text)
{
    char* line = text;
    while (line != NULL && *line != 0)
    {
        char* end = strchr(line, '\n');
        char* diagnostic = strstr(line, "unknown warning option '");
        char* hint = strstr(line, "; did you mean '");
        if (end != NULL && diagnostic != NULL && diagnostic < end && hint != NULL && diagnostic < hint && hint < end &&
            end - hint >= 2 && end[-2] == '\'' && end[-1] == '?')
        {
            memmove(hint, end, strlen(end) + 1);
            end = hint;
        }
        line = end != NULL ? end + 1 : NULL;
    }
}

static void cc_warning_option_run(CCCompatibilityTest* test, const char* name, const char* const* options, const char* message, uint32_t warnings, uint32_t errors, bool fatal)
{
    static const char* const actions[] =
    {
        "-E",
        "-fsyntax-only",
    };
    char expected[4096] = { 0 };
    size_t length = 0;
    for (uint32_t i = 0; i < warnings + errors; i += 1)
    {
        length += (size_t)snprintf(expected + length, sizeof(expected) - length, "%s: %s\n", fatal ? "fatal error" : errors != 0 ? "error" : "warning", message);
    }
    if (warnings + errors != 0)
    {
        const uint32_t count = warnings != 0 ? warnings : errors;
        snprintf(expected + length, sizeof(expected) - length, "%u %s%s generated.\n", count, warnings != 0 ? "warning" : "error", count == 1 ? "" : "s");
    }
    for (uint32_t action = 0; action < sizeof(actions) / sizeof(actions[0]); action += 1)
    {
        const char* arguments[48] =
        {
            "-fno-diagnostics-show-option",
            "-fno-color-diagnostics",
            actions[action],
        };
        uint32_t count = 3;
        for (uint32_t i = 0; options[i] != NULL; i += 1)
        {
            arguments[count++] = options[i];
        }
        arguments[count++] = "warning-options.c";
        arguments[count] = NULL;
        CCCompatibilityRun oracle = cc_compat_run(test, test->clang, NULL, arguments);
        cc_warning_option_hint(oracle.errors);
        const bool oracle_good = oracle.status == (errors != 0) && oracle.errors != NULL && strcmp(oracle.errors, expected) == 0;
        char description[256];
        snprintf(description, sizeof(description), "warning option %s, %s: independent Clang diagnostic", name, actions[action]);
        cc_compat_expect(test, oracle_good, description);
        if (!oracle_good)
        {
            fprintf(stderr, "  Clang status %d; expected:\n%s  actual:\n%s", oracle.status, expected, oracle.errors != NULL ? oracle.errors : "(missing)\n");
        }
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            arguments[count] = scalar != 0 ? "--scalar" : NULL;
            arguments[count + 1] = NULL;
            // Only Clang needs the first two rendering switches. Buster already emits plain diagnostics without
            // option suffixes, and its C driver deliberately rejects unimplemented -f options.
            CCCompatibilityRun ours = cc_compat_run(test, test->compiler, NULL, arguments + 2);
            const bool good = ours.status == (errors != 0) && ours.errors != NULL && strcmp(ours.errors, expected) == 0;
            snprintf(description, sizeof(description), "warning option %s, %s, %s: complete diagnostic and status", name, actions[action], scalar != 0 ? "scalar" : "default");
            cc_compat_expect(test, good, description);
            if (!good)
            {
                fprintf(stderr, "  Buster status %d; expected:\n%s  actual:\n%s", ours.status, expected, ours.errors != NULL ? ours.errors : "(missing)\n");
            }
            cc_compat_release(&ours);
        }
        cc_compat_release(&oracle);
    }
}

// Capability errors are Buster's explicit contract, not invented Clang diagnostics. Independent spellings and
// expected families below keep recognition and implementation separate; even a clean input must refuse a request.
static void cc_warning_capability_run(CCCompatibilityTest* test, const char* const* options, const char* refused, const char* family)
{
    static const char* const actions[] =
    {
        "-E",
        "-fsyntax-only",
        "-c",
    };
    char expected[512] = { 0 };
    if (refused != NULL)
    {
        snprintf(expected, sizeof(expected), "error: unsupported warning option '%s' (warning family '%s' is not fully implemented)\n", refused, family);
    }
    const size_t length = strlen(expected);
    for (uint32_t action = 0; action < sizeof(actions) / sizeof(actions[0]); action += 1)
    {
        const bool kept = cc_compat_write(test, "warning-capability.out", "previous output\n") &&
                          cc_compat_write(test, "warning-capability.d", "previous dependencies\n");
        const char* arguments[32] =
        {
            actions[action],
            "-MD",
            "-MF",
            "warning-capability.d",
            "-o",
            "warning-capability.out",
            "-pipe",
        };
        uint32_t count = 7;
        for (uint32_t i = 0; options[i] != NULL; i += 1)
        {
            arguments[count++] = options[i];
        }
        arguments[count++] = "warning-capabilities.c";
        arguments[count] = NULL;
        CCCompatibilityRun run = cc_compat_run(test, test->compiler, NULL, arguments);
        bool good = kept && run.errors != NULL && run.status == (refused != NULL);
        if (refused != NULL)
        {
            char* output = cc_compat_read(test, "warning-capability.out");
            char* dependencies = cc_compat_read(test, "warning-capability.d");
            good = good && strncmp(run.errors, expected, length) == 0 && strncmp(run.errors + length, "usage: compiler ", 16) == 0 &&
                   run.output != NULL && run.output[0] == 0 && output != NULL && strcmp(output, "previous output\n") == 0 &&
                   dependencies != NULL && strcmp(dependencies, "previous dependencies\n") == 0;
            free(output);
            free(dependencies);
        }
        else
        {
            good = good && run.errors[0] == 0;
        }
        char description[256];
        snprintf(description, sizeof(description), "warning capability %s, %s: status, diagnostic and preserved outputs", options[0], actions[action]);
        cc_compat_expect(test, good, description);
        if (!good)
        {
            fprintf(stderr, "  Buster status %d; expected prefix: %s  actual:\n%s", run.status, refused != NULL ? expected : "(silent success)\n", run.errors != NULL ? run.errors : "(missing)\n");
        }
        cc_compat_release(&run);
    }
}

static void cc_compat_warning_capabilities(CCCompatibilityTest* test)
{
    static const char* const families[] =
    {
        "shadow",
        "conversion",
        "sign-conversion",
        "missing-prototypes",
        "missing-variable-declarations",
        "strict-prototypes",
        "undef",
        "cast-qual",
        "pointer-arith",
        "switch-enum",
        "vla",
        "implicit-fallthrough",
        "comma",
        "float-equal",
        "redundant-decls",
        "nested-externs",
    };
    const bool written = cc_compat_write(test, "warning-capabilities.c", "int warning_capabilities(void);\nint warning_capabilities(void) { return 0; }\n");
    cc_compat_expect(test, written, "write warning capability source");
    if (written)
    {
        for (uint32_t f = 0; f < sizeof(families) / sizeof(families[0]); f += 1)
        {
            char enable[128];
            char disable[128];
            char error[128];
            char demote[128];
            snprintf(enable, sizeof(enable), "-W%s", families[f]);
            snprintf(disable, sizeof(disable), "-Wno-%s", families[f]);
            snprintf(error, sizeof(error), "-Werror=%s", families[f]);
            snprintf(demote, sizeof(demote), "-Wno-error=%s", families[f]);
            const char* positive[] = { enable, NULL };
            const char* promoted[] = { error, NULL };
            const char* negative[] = { disable, NULL };
            const char* cancelled[] = { enable, disable, "-Werror", NULL };
            const char* demoted[] = { demote, "-Werror", NULL };
            cc_warning_capability_run(test, positive, enable, families[f]);
            cc_warning_capability_run(test, promoted, error, families[f]);
            cc_warning_capability_run(test, negative, NULL, NULL);
            cc_warning_capability_run(test, cancelled, NULL, NULL);
            cc_warning_capability_run(test, demoted, NULL, NULL);
        }
        static const CCWarningCapabilityCase policies[] =
        {
            { { "-Wshadow", "-Werror" }, "-Wshadow", "shadow" },
            { { "-Wshadow", "-w" }, "-Wshadow", "shadow" },
            { { "-w", "-Wshadow" }, "-Wshadow", "shadow" },
            { { "-Wshadow", "-Wno-unknown-warning-option" }, "-Wshadow", "shadow" },
            { { "-Wshadow", "-Wno-error=shadow" }, "-Wshadow", "shadow" },
            { { "-Wno-shadow", "-Werror=shadow" }, "-Werror=shadow", "shadow" },
            { { "-Werror=shadow", "-Wno-shadow" }, NULL, NULL },
            { { "-Wfatal-errors=shadow" }, "-Wfatal-errors=shadow", "shadow" },
            { { "-Wno-fatal-errors=shadow" }, NULL, NULL },
            { { "-Wshadow", "-Wshadow" }, "-Wshadow", "shadow" },
            { { "-Wshadow", "-Wno-shadow", "-Wshadow" }, "-Wshadow", "shadow" },
            { { "-Wshadow", "-Wno-shadow-all" }, NULL, NULL },
            { { "-Wsign-conversion", "-Wno-conversion" }, NULL, NULL },
            { { "-Wconversion", "-Wno-sign-conversion" }, "-Wconversion", "conversion" },
            { { "-Wshadow", "-Wconversion", "-Wno-shadow" }, "-Wconversion", "conversion" },
            { { "-Wvla", "-Wshadow" }, "-Wvla", "vla" },
            { { "-Wshadow", "-Wno-everything", "-Wall", "-Wextra" }, NULL, NULL },
            { { "-Wno-everything", "-Wshadow" }, "-Wshadow", "shadow" },
            { { "-Wall", "-Wextra", "-Werror" }, NULL, NULL },
            { { "-I", "-Wshadow", "-Werror" }, NULL, NULL },
            { { "-Wp,-Wshadow" }, NULL, NULL },
            { { "-Wshadow", "-M" }, "-Wshadow", "shadow" },
        };
        for (uint32_t p = 0; p < sizeof(policies) / sizeof(policies[0]); p += 1)
        {
            cc_warning_capability_run(test, policies[p].arguments, policies[p].refused, policies[p].family);
        }
        // Recognition remains a separate feature query. Source mappings cannot satisfy a missing CLI capability.
        const bool pragma = cc_compat_write(test, "warning-capabilities.c", "#pragma clang diagnostic ignored \"-Wshadow\"\n#if !__has_warning(\"-Wshadow\")\n#error lost warning recognition\n#endif\nint warning_capabilities(void);\nint warning_capabilities(void) { return 0; }\n");
        cc_compat_expect(test, pragma, "write capability pragma and recognition source");
        const char* ignored[] = { "-Wshadow", NULL };
        const char* queried[] = { "-Wno-shadow", NULL };
        cc_warning_capability_run(test, ignored, "-Wshadow", "shadow");
        cc_warning_capability_run(test, queried, NULL, NULL);
        const bool dash = cc_compat_write(test, "-Wshadow", "int after_dash(void);\nint after_dash(void) { return 0; }\n");
        const char* path[] = { "-fsyntax-only", "-Werror", "-x", "c", "--", "-Wshadow", NULL };
        CCCompatibilityRun run = cc_compat_run(test, test->compiler, NULL, path);
        cc_compat_expect(test, dash && run.status == 0 && run.errors != NULL && run.errors[0] == 0, "a post-double-dash warning spelling remains an input path");
        cc_compat_release(&run);
        const char* stdin_arguments[] = { "-fsyntax-only", "-Wshadow", "-x", "c", "-", NULL };
        const bool stdin_written = cc_compat_write(test, "warning-capability-stdin.c", "#error source must not be checked\n");
        run = cc_compat_run(test, test->compiler, "warning-capability-stdin.c", stdin_arguments);
        cc_compat_expect(test, stdin_written && run.status != 0 && run.errors != NULL && strstr(run.errors, "unsupported warning option '-Wshadow'") != NULL &&
                         strstr(run.errors, "source must not be checked") == NULL, "an unsupported warning request refuses stdin before source checking");
        cc_compat_release(&run);
        const char* deferred[] = { "-fsyntax-only", "--lanes", "2", "--deferred-bodies", "-Wshadow", "warning-capabilities.c", NULL };
        run = cc_compat_run(test, test->compiler, NULL, deferred);
        cc_compat_expect(test, run.status != 0 && run.errors != NULL && strstr(run.errors, "unsupported warning option '-Wshadow'") != NULL,
                         "a deferred multi-lane command refuses an unsupported warning before scheduling");
        cc_compat_release(&run);
        const char* missing[] = { "-c", "-Wvla", "warning-capability-missing.c", "-o", "warning-capability-new.o", "-MD", NULL };
        run = cc_compat_run(test, test->compiler, NULL, missing);
        char object_path[PATH_MAX];
        char dependency_path[PATH_MAX];
        snprintf(object_path, sizeof(object_path), "%s/warning-capability-new.o", test->directory);
        snprintf(dependency_path, sizeof(dependency_path), "%s/warning-capability-new.d", test->directory);
        cc_compat_expect(test, run.status != 0 && run.errors != NULL && strstr(run.errors, "unsupported warning option '-Wvla'") != NULL &&
                         access(object_path, F_OK) != 0 && access(dependency_path, F_OK) != 0,
                         "a capability refusal precedes input access and publishes no new object or dependencies");
        cc_compat_release(&run);
    }
}

static void cc_compat_warning_options(CCCompatibilityTest* test)
{
    static const char source[] = "int warning_options(void);\nint warning_options(void) { return 0; }\n";
    const bool written = cc_compat_write(test, "warning-options.c", source);
    cc_compat_expect(test, written, "write warning option fixture");
    cc_compat_warning_capabilities(test);
    if (written)
    {
        static const CCWarningOptionCase unknown[] =
        {
            {
                .arguments = { "-Wbuster-unknown-1467", NULL },
                .message = "unknown warning option '-Wbuster-unknown-1467'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wno-buster-unknown-1467", NULL },
                .message = "unknown warning option '-Wno-buster-unknown-1467'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wbuster-unknown-1467=3", NULL },
                .message = "unknown warning option '-Wbuster-unknown-1467=3'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wno-buster-unknown-1467=3", NULL },
                .message = "unknown warning option '-Wno-buster-unknown-1467=3'",
                .warnings = 1,
            },
            {
                .arguments = { "-Werror=buster-unknown-1467", NULL },
                .message = "unknown warning option '-Werror=buster-unknown-1467'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wno-error=buster-unknown-1467", NULL },
                .message = "unknown warning option '-Werror=buster-unknown-1467'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wfatal-errors=buster-unknown-1467", NULL },
                .message = "unknown warning option '-Wfatal-errors=buster-unknown-1467'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wfatal-errors-buster-unknown-1467", NULL },
                .message = "unknown warning option '-Wfatal-errors=buster-unknown-1467'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wno-fatal-errors=buster-unknown-1467", NULL },
                .message = "unknown warning option '-Wfatal-errors=buster-unknown-1467'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wno-frame-larger-than=64", NULL },
                .message = "unknown warning option '-Wno-frame-larger-than=64'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wframe-larger-thanx=64", NULL },
                .message = "unknown warning option '-Wframe-larger-thanx=64'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wlarger-thanx=64", NULL },
                .message = "unknown warning option '-Wlarger-thanx=64'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wlarge-by-value-copyx=64", NULL },
                .message = "unknown warning option '-Wlarge-by-value-copyx=64'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wundef-prefixx=FOO", NULL },
                .message = "unknown warning option '-Wundef-prefixx=FOO'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wsystem-headers-in-modulex=example", NULL },
                .message = "unknown warning option '-Wsystem-headers-in-modulex=example'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wformat=3", NULL },
                .message = "unknown warning option '-Wformat=3'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wno-format=0", NULL },
                .message = "unknown warning option '-Wno-format=0'",
                .warnings = 1,
            },
            {
                .arguments = { "-Werror=everything", NULL },
                .message = "unknown warning option '-Werror=everything'",
                .warnings = 1,
            },
            {
                .arguments = { "-Werror=system-headers", NULL },
                .message = "unknown warning option '-Werror=system-headers'",
                .warnings = 1,
            },
            {
                .arguments = { "-Werror-no-such", NULL },
                .message = "unknown -Werror warning specifier: '-Werror-no-such'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wfatal-errors=", NULL },
                .message = "unknown -Wfatal-errors warning specifier: '-Wfatal-errors='",
                .warnings = 1,
            },
            {
                .arguments = { "-Wno-fatal-errors-", NULL },
                .message = "unknown -Wfatal-errors warning specifier: '-Wno-fatal-errors-'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wnonportable-cfstringsbuster-unknown-1467", NULL },
                .message = "unknown warning option '-Wbuster-unknown-1467'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wno-nonportable-cfstringsbuster-unknown-1467", NULL },
                .message = "unknown warning option '-Wbuster-unknown-1467'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wnonportable-cfstringserrorfoo", NULL },
                .message = "unknown -Werror warning specifier: '-Werrorfoo'",
                .warnings = 1,
            },
            {
                .arguments = { "-Wno-nonportable-cfstringsfatal-errors=", NULL },
                .message = "unknown -Wfatal-errors warning specifier: '-Wfatal-errors='",
                .warnings = 1,
            },
        };
        for (uint32_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i += 1)
        {
            const CCWarningOptionCase* entry = unknown + i;
            cc_warning_option_run(test, entry->arguments[0], entry->arguments, entry->message, entry->warnings, entry->errors, entry->fatal);
        }
        static const CCWarningOptionCase policies[] =
        {
            {
                .arguments = { NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-w", NULL },
                .warnings = 0,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Werror", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = false,
            },
            {
                .arguments = { "-Werror", "-w", NULL },
                .warnings = 0,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-w", "-Werror=unknown-warning-option", NULL },
                .warnings = 0,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wno-unknown-warning-option", NULL },
                .warnings = 0,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wno-unknown-warning-option", "-Wunknown-warning-option", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wunknown-warning-option", "-Wno-unknown-warning-option", NULL },
                .warnings = 0,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wno-everything", "-Wunknown-warning-option", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wno-everything", "-Weverything", NULL },
                .warnings = 0,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Weverything", "-Wno-everything", NULL },
                .warnings = 0,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wno-everything", "-Weverything", "-Wunknown-warning-option", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Werror", "-Wno-error=unknown-warning-option", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wno-error=unknown-warning-option", "-Werror", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Werror=unknown-warning-option", "-Wno-error", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = false,
            },
            {
                .arguments = { "-Werror=unknown-warning-option", "-Wunknown-warning-option", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = false,
            },
            {
                .arguments = { "-Werror=unknown-warning-option", "-Wno-error=unknown-warning-option", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wno-error=unknown-warning-option", "-Werror=unknown-warning-option", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = false,
            },
            {
                .arguments = { "-Werror=unknown-warning-option", "-Wno-unknown-warning-option", "-Wunknown-warning-option", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wno-error=unknown-warning-option", "-Werror=unknown-warning-option", "-Wno-unknown-warning-option", "-Wunknown-warning-option", "-Werror", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Werror", "-Wno-error", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Wno-error", "-Werror", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = false,
            },
            {
                .arguments = { "-Werror=", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = false,
            },
            {
                .arguments = { "-Werror", "-Wno-error=", NULL },
                .warnings = 1,
                .errors = 0,
                .fatal = false,
            },
            {
                .arguments = { "-Werror", "-Wfatal-errors", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = true,
            },
            {
                .arguments = { "-Wfatal-errors=unknown-warning-option", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = true,
            },
            {
                .arguments = { "-Wfatal-errors=unknown-warning-option", "-Wno-fatal-errors=unknown-warning-option", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = false,
            },
            {
                .arguments = { "-Werror", "-Wfatal-errors", "-Wno-fatal-errors=unknown-warning-option", NULL },
                .warnings = 0,
                .errors = 1,
                .fatal = false,
            },
            {
                .arguments = { "-Wfatal-errors=unknown-warning-option", "-w", NULL },
                .warnings = 0,
                .errors = 0,
                .fatal = false,
            },
        };
        // The misspelling comes first and last in separate runs: a policy is resolved over the complete command,
        // not just over options that precede the diagnostic. A genuine semantic warning is not needed to trigger it.
        for (uint32_t i = 0; i < sizeof(policies) / sizeof(policies[0]); i += 1)
        {
            const CCWarningOptionCase* entry = policies + i;
            for (uint32_t first = 0; first < 2; first += 1)
            {
                const char* arguments[10];
                uint32_t count = 0;
                if (first != 0)
                {
                    arguments[count++] = "-Wbuster-unknown-1467";
                }
                for (uint32_t option = 0; entry->arguments[option] != NULL; option += 1)
                {
                    arguments[count++] = entry->arguments[option];
                }
                if (first == 0)
                {
                    arguments[count++] = "-Wbuster-unknown-1467";
                }
                arguments[count] = NULL;
                char name[64];
                snprintf(name, sizeof(name), "ordered policy %u, typo %s", i, first != 0 ? "first" : "last");
                cc_warning_option_run(test, name, arguments, "unknown warning option '-Wbuster-unknown-1467'", entry->warnings, entry->errors, entry->fatal);
            }
        }
        // Disabling/demotion controls for recognized-only families remain distinct from unknown names (#1365).
        // The capability tests separately require explicit refusal for their positive requests.
        const char* known[] =
        {
            "-Werror",
            "-Wno-shadow",
            "-Wno-conversion",
            "-Wno-vla",
            "-Wno-implicit-fallthrough",
            "-Wno-error=shadow",
            "-Wno-error=conversion",
            "-Wlarger-than=42",
            "-Wlarger-than-42",
            "-Wframe-larger-than=4096",
            "-Wlarge-by-value-copy=4096",
            "-Wundef-prefix=BUSTER_,TEST_",
            "-Wsystem-headers-in-module=example",
            "-Wformat=0",
            "-Wformat=2",
            "-Wstrict-aliasing=2",
            "-Wstrict-overflow=5",
            "-Werror-implicit-function-declaration",
            "-Wno-error-implicit-function-declaration",
            "-W",
            "-Wno-",
            "-Wnonportable-cfstrings",
            "-Wno-nonportable-cfstrings",
            NULL,
        };
        cc_warning_option_run(test, "known vocabulary", known, NULL, 0, 0, false);
        const char* transports[] =
        {
            "-Werror",
            "-Wno-unused-command-line-argument",
            "-Wa,--noexecstack",
            "-Wp,-DBUSTER_TRANSPORT_1467=1",
            "-Wl,--as-needed",
            NULL,
        };
        cc_warning_option_run(test, "driver transports are not warning names", transports, NULL, 0, 0, false);
        const char* duplicate[] =
        {
            "-Wbuster-unknown-1467",
            "-Wbuster-unknown-1467",
            NULL,
        };
        cc_warning_option_run(test, "duplicate unknown options", duplicate, "unknown warning option '-Wbuster-unknown-1467'", 2, 0, false);
        char long_option[512];
        memset(long_option, 'z', sizeof(long_option) - 1);
        memcpy(long_option, "-W", 2);
        long_option[sizeof(long_option) - 1] = 0;
        char long_message[560];
        snprintf(long_message, sizeof(long_message), "unknown warning option '%s'", long_option);
        const char* long_arguments[] = { long_option, NULL };
        cc_warning_option_run(test, "long unknown spelling", long_arguments, long_message, 1, 0, false);
        const char* limited[] =
        {
            "-Werror",
            "-Wfatal-errors",
            "-Wbuster-unknown-1467",
            "-Wbuster-unknown-1467",
            NULL,
        };
        cc_warning_option_run(test, "fatal stops duplicate diagnostics", limited, "unknown warning option '-Wbuster-unknown-1467'", 0, 1, true);
        const bool pragma = cc_compat_write(test, "warning-options.c", "#pragma clang diagnostic ignored \"-Wunknown-warning-option\"\nint warning_options(void);\nint warning_options(void) { return 0; }\n");
        cc_compat_expect(test, pragma, "write source suppression control");
        const char* pragma_arguments[] = { "-Wbuster-unknown-1467", NULL };
        cc_warning_option_run(test, "source pragma cannot suppress the command line", pragma_arguments, "unknown warning option '-Wbuster-unknown-1467'", 1, 0, false);
        cc_compat_expect(test, cc_compat_write(test, "warning-options.c", source), "restore warning option fixture");
        // Option operands and input paths are not warning-policy records.
        const bool dash_file = cc_compat_write(test, "-Wbuster-unknown-1467", source);
        const char* operand[] =
        {
            "-Werror",
            "-I",
            "-Wbuster-unknown-1467",
            NULL,
        };
        cc_warning_option_run(test, "option operand", operand, NULL, 0, 0, false);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const char* after_dash[] = { "-fsyntax-only", "-Werror", "-x", "c", scalar != 0 ? "--scalar" : "-pipe", "--", "-Wbuster-unknown-1467", NULL };
            CCCompatibilityRun path = cc_compat_run(test, test->compiler, NULL, after_dash);
            cc_compat_expect(test, dash_file && path.status == 0 && path.errors != NULL && path.errors[0] == 0,
                             "a post-double-dash input is not an unknown warning option");
            cc_compat_release(&path);
            // An unknown-warning error cannot overwrite a previously published object or dependency file.
            const bool kept = cc_compat_write(test, "warning-options.o", "previous object\n") && cc_compat_write(test, "warning-options.d", "previous dependencies\n");
            const char* compile[] = { "-c", "-MD", "-Werror=unknown-warning-option", "-Wbuster-unknown-1467", "warning-options.c",
                                      "-o", "warning-options.o", scalar != 0 ? "--scalar" : "-pipe", NULL };
            CCCompatibilityRun failed = cc_compat_run(test, test->compiler, NULL, compile);
            char* object = cc_compat_read(test, "warning-options.o");
            char* dependency = cc_compat_read(test, "warning-options.d");
            cc_compat_expect(test, kept && failed.status != 0 && failed.errors != NULL && strstr(failed.errors, "error: unknown warning option") != NULL &&
                             object != NULL && strcmp(object, "previous object\n") == 0 &&
                             dependency != NULL && strcmp(dependency, "previous dependencies\n") == 0,
                             "promoted unknown warning preserves existing object and dependencies");
            free(object);
            free(dependency);
            cc_compat_release(&failed);
        }
    }
}
