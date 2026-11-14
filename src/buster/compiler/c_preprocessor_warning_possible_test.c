// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original regressions for #2547: -Wreserved-macro-identifier's check, off by default, reads neither a #define's name
// nor the command line's options once a unit knows the warning is ignored (Preprocessor.warnings_possible), and gives
// the warning where the command line or a pragma does. Included by c_preprocessor_test.c. The driver's fixtures
// (c_check_reserved_macro_driver_test.c) compare the diagnostics' text with Clang's; these count them and check the
// state that keeps a default compile off the warning's path.

typedef struct PPWarningPossibleCase PPWarningPossibleCase;
struct PPWarningPossibleCase
{
    const char* name;
    const char* source;
    const char* options[2];
    bool suppress;
    // The unit's warnings, and whether -Wreserved-macro-identifier may still be given at its end.
    uint32_t warnings;
    bool possible;
};

static bool pp_test_warning_possible(const char* directory)
{
    static const PPWarningPossibleCase cases[] =
    {
        {
            .name = "off by default",
            .source = "#define _A 1\n#undef _A\n#define __b 2\n#define _GNU_SOURCE\nint z;\n",
            .warnings = 0,
            .possible = false,
        },
        {
            .name = "given by the command line",
            .source = "#define _A 1\n#undef _A\n#define __b 2\n#define lower 3\nint z;\n",
            .options = { "-Wreserved-identifier" },
            .warnings = 3,
            .possible = true,
        },
        {
            .name = "given and then taken away by the command line",
            .source = "#define _A 1\nint z;\n",
            .options = { "-Wreserved-macro-identifier", "-Wno-reserved-identifier" },
            .warnings = 0,
            .possible = false,
        },
        {
            .name = "-w",
            .source = "#define _A 1\nint z;\n",
            .options = { "-Wreserved-identifier" },
            .suppress = true,
            .warnings = 0,
            .possible = false,
        },
        {
            // The bit cleared at _A, set again by each pragma, and cleared again at _C.
            .name = "a pragma gives it and another ignores it",
            .source = "#define _A 1\n"
                      "#pragma clang diagnostic warning \"-Wreserved-macro-identifier\"\n"
                      "#define _B 1\n"
                      "#undef _B\n"
                      "#pragma clang diagnostic ignored \"-Wreserved-identifier\"\n"
                      "#define _C 1\n"
                      "int z;\n",
            .warnings = 2,
            .possible = false,
        },
        {
            // The pop restores the command line's mapping.
            .name = "push, ignored and pop",
            .source = "#pragma clang diagnostic push\n"
                      "#pragma clang diagnostic ignored \"-Wreserved-identifier\"\n"
                      "#define _A 1\n"
                      "#pragma clang diagnostic pop\n"
                      "#define _B 1\n"
                      "int z;\n",
            .options = { "-Wreserved-macro-identifier" },
            .warnings = 1,
            .possible = true,
        },
        {
            // The feature-test macros' table (pp_feature_macros): its longest name, a name that extends one of them, and
            // an #undef of one, which gives the warning.
            .name = "feature-test macros",
            .source = "#define _GNU_SOURCE\n"
                      "#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1\n"
                      "#define _GNU_SOURCEX\n"
                      "#define _GNU_SOURC\n"
                      "#undef _GNU_SOURCE\n"
                      "int z;\n",
            .options = { "-Wreserved-identifier" },
            .warnings = 3,
            .possible = true,
        },
        {
            // The command line's definitions are a virtual file, as clang's <built-in>.
            .name = "the command line's definitions",
            .source = "int z;\n",
            .options = { "-Wreserved-identifier" },
            .warnings = 0,
            .possible = true,
        },
    };
    char path[4096];
    snprintf(path, sizeof(path), "%s/warning-possible.c", directory);
    const char* macros[] = { "D_COMMAND=1", "U_COMMAND" };
    uint32_t failures = 0;
    uint32_t runs = 0;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const PPWarningPossibleCase* test = cases + c;
        const bool written = pp_test_write_file(path, test->source);
        failures += !written;
        uint32_t option_count = 0;
        while (option_count < BUSTER_ARRAY_LENGTH(test->options) && test->options[option_count] != NULL)
        {
            option_count += 1;
        }
        for (uint32_t mode = 0; written && mode < 4; mode += 1)
        {
            static InternTable table;
            static Preprocessor pp;
            const PPOptions options =
            {
                .language = { .standard = 2017, .gnu = true },
                .scalar = (mode & 1) != 0,
                .defer_history = (mode & 2) != 0,
                .quiet = true,
                .no_standard_includes = true,
                .suppress_warnings = test->suppress,
                .semantic_warning_options = test->options,
                .semantic_warning_option_count = option_count,
                .macros = macros,
                .macro_count = BUSTER_ARRAY_LENGTH(macros),
            };
            const bool ready = intern_initialize(&table) && pp_initialize(&pp, &options, &table);
            const bool accepted = ready && pp_run(&pp, path);
            const bool possible = ((pp.warnings_possible >> PP_WARNING_RESERVED_MACRO_IDENTIFIER) & 1u) != 0;
            // A default unit reads the warning's options once, and no other warning's: none of these reaches one.
            const bool lazy = test->options[0] != NULL || test->suppress ||
                              (pp.warning_commands[PP_WARNING_RESERVED_MACRO_IDENTIFIER] == BUSTER_PP_IGNORED &&
                               pp.warning_commands[PP_WARNING_EXTRA_TOKENS] == BUSTER_PP_COMMAND_UNREAD &&
                               pp.warning_commands[PP_WARNING_MACRO_REDEFINED] == BUSTER_PP_COMMAND_UNREAD);
            const bool passed = accepted && pp.errors == 0 && pp.warnings == test->warnings && possible == test->possible && lazy;
            if (!passed)
            {
                printf("warning possible %s mode=%u: accepted=%u errors=%u warnings=%u (expected %u) possible=%u (expected %u) lazy=%u\n", test->name,
                       mode, accepted, pp.errors, pp.warnings, test->warnings, possible, test->possible, lazy);
            }
            failures += !passed;
            runs += 1;
            pp_release(&pp);
            intern_release(&table);
        }
    }
    unlink(path);
    printf("reserved macro names' possible warning: %u failures in %u runs\n", failures, runs);
    return failures == 0;
}
