// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original real-driver regressions for unknown warning options (#1467). Every diagnostic has an independent
// expected name, severity and count, and Clang checks the complete text except its optional spelling suggestion.
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

static void cc_compat_warning_options(CCCompatibilityTest* test)
{
    static const char source[] = "int warning_options(void);\nint warning_options(void) { return 0; }\n";
    const bool written = cc_compat_write(test, "warning-options.c", source);
    cc_compat_expect(test, written, "write warning option fixture");
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
        // Known-but-unimplemented warnings (#1365), numerical driver spellings and compatibility aliases remain
        // distinct from unknown names. All are clean on this source, including with -Werror.
        const char* known[] =
        {
            "-Werror",
            "-Wshadow",
            "-Wconversion",
            "-Wvla",
            "-Wimplicit-fallthrough",
            "-Werror=shadow",
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
