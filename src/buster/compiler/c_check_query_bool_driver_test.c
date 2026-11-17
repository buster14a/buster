// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #2899 sources stay under tests/driver. Clang 23.1.4 supplies the external diagnostic oracle; no LLVM
// implementation or test input is incorporated. The shared sweep compares complete diagnostics and status across
// both readers, one/three requested lanes, warning policies, syntax/object modes and rejected-output preservation.

typedef struct CheckDriverQueryBoolFile CheckDriverQueryBoolFile;
struct CheckDriverQueryBoolFile
{
    const char* path;
    CheckDriverDefaultWarningFixture fixture;
};

// These originals also expose a pre-existing primary assertion spelling gap: Buster prints the source type query,
// whereas Clang prints its canonical type. Keep the original inputs and freeze those exact primary texts only;
// warnings, errors' locations, notes, order and status still compare against Clang without normalization. This suite
// neither imports #2915's assertion pretty-printer work nor claims canonical requirement spelling for #2899.
static const CheckDriverWarningSpelling check_driver_query_type_spellings[] =
{
    {
        .clang = ": error: static assertion failed due to requirement 'sizeof(int[3]) == 0': array bound fails",
        .baseline = ": error: static assertion failed due to requirement 'sizeof(int[(1 << 2) ? 3 : 4]) == 0': array bound fails",
    },
    {
        .clang = ": error: static assertion failed due to requirement 'sizeof(int) == 0': typeof fails",
        .baseline = ": error: static assertion failed due to requirement 'sizeof(__typeof__((1 << 2) ? 3 : 4)) == 0': typeof fails",
    },
    {
        .clang = ": error: static assertion failed due to requirement '__builtin_types_compatible_p(int, int) == 0': types fails",
        .baseline = ": error: static assertion failed due to requirement '__builtin_types_compatible_p(__typeof__((1 << 2) ? 3 : 4), int) == 0': types fails",
    },
    { 0 },
};

static const CheckDriverWarningSpelling check_driver_query_failed_spellings[] =
{
    {
        .clang = ": error: static assertion failed due to requirement 'sizeof(int) == 0': body typeof fails",
        .baseline = ": error: static assertion failed due to requirement 'sizeof(__typeof__((1 << 2) ? 3 : 4)) == 0': body typeof fails",
    },
    {
        .clang = ": error: static assertion failed due to requirement 'sizeof(int) == 0': floating type fails",
        .baseline = ": error: static assertion failed due to requirement 'sizeof(__typeof__(0.5 ? 3 : 4)) == 0': floating type fails",
    },
    { 0 },
};

static void check_test_driver_query_bool_execution(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char object[PATH_MAX];
    char program[PATH_MAX];
    snprintf(object, sizeof(object), "%s/query-bool.o", directory);
    snprintf(program, sizeof(program), "%s/query-bool", directory);
    const char* source = "tests/driver/query_bool_execution.c";
    for (uint32_t language = 0; language < 2; language += 1)
    {
        for (uint32_t route = 0; route < 5; route += 1)
        {
            const char* compiler = route == 0 ? clang : executable != NULL ? executable : "/proc/self/exe";
            const char* standard = language == 0 ? "-std=gnu17" : "-std=c23";
            char* arguments[] =
            {
                (char*)compiler,
                (char*)standard,
                "-w",
                "-c",
                (char*)source,
                "-o",
                object,
                route == 0 ? NULL : "--lanes",
                route <= 2 ? "1" : "3",
                route == 2 || route == 4 ? "--scalar" : NULL,
                NULL,
            };
            char* output = NULL;
            const bool compiled = check_test_driver_spawn(arguments, &output) == 0;
            char description[256];
            snprintf(description, sizeof(description), "unevaluated query execution control %s route=%u: object compiles", standard, route);
            check_test_expect(test, compiled, description);
            free(output);
            output = NULL;
            char* const link[] = { (char*)clang, "-o", program, object, NULL };
            const bool linked = compiled && check_test_driver_spawn(link, &output) == 0;
            snprintf(description, sizeof(description), "unevaluated query execution control %s route=%u: object links", standard, route);
            check_test_expect(test, linked, description);
            free(output);
            output = NULL;
            char* const run[] = { program, NULL };
            const bool ran = linked && check_test_driver_spawn(run, &output) == 0;
            snprintf(description, sizeof(description), "unevaluated query execution control %s route=%u: erased operands have no effects", standard, route);
            check_test_expect(test, ran, description);
            free(output);
            unlink(object);
            unlink(program);
        }
    }
}

static void check_test_driver_query_bool(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const CheckDriverQueryBoolFile cases[] =
    {
        {
            .path = "tests/driver/query_bool_original.c",
            .fixture =
            {
                .name = "successful queries and assertions retain boolean conversions",
                .warnings = 5,
            },
        },
        {
            .path = "tests/driver/query_bool_types.c",
            .fixture =
            {
                .name = "array bounds and type queries retain boolean conversions",
                .warnings = 8,
                .errors = 3,
                .notes = 3,
                .baseline_spellings = check_driver_query_type_spellings,
            },
        },
        {
            .path = "tests/driver/query_bool_floating.c",
            .fixture =
            {
                .name = "erased queries retain floating literal conversions",
                // The file-scope floating array bound also keeps its GNU folding warning.
                .warnings = 13,
            },
        },
        {
            .path = "tests/driver/query_bool_controls.c",
            .fixture =
            {
                .name = "omitted middle expressions and explicit casts remain quiet",
            },
        },
        {
            .path = "tests/driver/query_bool_logical.c",
            .fixture =
            {
                .name = "logical conversions retain independent unevaluated suppression",
                .options = { "-Wno-constant-logical-operand" },
                // The floating array typedef also keeps its GNU folding warning.
                .warnings = 14,
            },
        },
        {
            .path = "tests/driver/query_bool_nested.c",
            .fixture =
            {
                .name = "nested query conversions keep construction order",
                .warnings = 20,
            },
        },
        {
            .path = "tests/driver/query_bool_selections.c",
            .fixture =
            {
                .name = "boolean conditions peel static selections without passing explicit casts",
                .warnings = 11,
            },
        },
        {
            .path = "tests/driver/query_bool_signed_selections.c",
            .fixture =
            {
                .name = "floating conditions keep one sign through selections and extension wrappers",
                .warnings = 7,
            },
        },
        {
            .path = "tests/driver/query_bool_selection_controls.c",
            .fixture =
            {
                .name = "logical selection and builtin constant query controls keep their warnings",
                .options = { "-Wno-constant-logical-operand" },
                .warnings = 8,
            },
        },
        {
            .path = "tests/driver/query_bool_shared_conversions.c",
            .fixture =
            {
                .name = "shared selection peeling keeps ordinary conversion locations",
                .warnings = 15,
            },
        },
        {
            .path = "tests/driver/query_bool_macro.c",
            .fixture =
            {
                .name = "query conversions retain macro and forwarded argument notes",
                .warnings = 10,
                .notes = 10,
            },
        },
        {
            .path = "tests/driver/query_bool_policy.c",
            .fixture =
            {
                .name = "query conversion policy follows the condition and macro expansion",
                .warnings = 5,
                .notes = 1,
            },
        },
        {
            .path = "tests/driver/query_bool_promoted.c",
            .fixture =
            {
                .name = "source promotion and category suppression remain independent",
                .options = { "-Wno-error" },
                .warnings = 1,
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/query_bool_signed.c",
            .fixture =
            {
                .name = "query signed shift conversions remain ignored by default",
                .warnings = 1,
            },
        },
        {
            .path = "tests/driver/query_bool_signed.c",
            .fixture =
            {
                .name = "query signed shift conversions can be enabled",
                .options = { "-Wint-in-bool-context" },
                .warnings = 5,
            },
        },
        {
            .path = "tests/driver/query_bool_scopes.c",
            .fixture =
            {
                .name = "query conversions retain member and deferred body ownership",
                .warnings = 10,
            },
        },
        {
            .path = "tests/driver/query_bool_failed.c",
            .fixture =
            {
                .name = "query conversions precede failed assertion diagnostics",
                .warnings = 5,
                .errors = 4,
                .notes = 3,
                .baseline_spellings = check_driver_query_failed_spellings,
            },
        },
        {
            .path = "tests/driver/query_bool_failed.c",
            .fixture =
            {
                .name = "query conversion recovery obeys the error limit",
                .options = { "-ferror-limit=1" },
                .warnings = 3,
                .errors = 2,
                .notes = 1,
                .baseline_spellings = check_driver_query_failed_spellings,
            },
        },
        {
            .path = "tests/driver/query_bool_failed.c",
            .fixture =
            {
                .name = "query conversion recovery obeys fatal errors",
                .options = { "-Wfatal-errors" },
                .warnings = 1,
                .errors = 1,
                .notes = 1,
                .baseline_spellings = check_driver_query_failed_spellings,
            },
        },
        {
            .path = "tests/driver/query_bool_recovery_brace.c",
            .fixture =
            {
                .name = "query conversions survive recovered names before a missing brace",
                .warnings = 1,
                .errors = 2,
                .notes = 1,
            },
        },
        {
            .path = "tests/driver/query_bool_recovery_brace.c",
            .fixture =
            {
                .name = "query recovery respects the error limit before a missing brace",
                .options = { "-ferror-limit=1" },
                .warnings = 1,
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/query_bool_recovery_brace.c",
            .fixture =
            {
                .name = "fatal name errors suppress later query conversion recovery",
                .options = { "-Wfatal-errors" },
                .errors = 1,
            },
        },
        {
            .path = "tests/driver/query_bool_recovery_label.c",
            .fixture =
            {
                .name = "query conversions remain before a missing goto label",
                .warnings = 1,
                .errors = 1,
            },
        },
        {
            .path = "tests/driver/query_bool_recovery_label.c",
            .fixture =
            {
                .name = "query conversion recovery precedes a fatal missing-label error",
                .options = { "-Wfatal-errors" },
                .warnings = 1,
                .errors = 1,
            },
        },
        {
            .path = "tests/driver/query_bool_original.c",
            .fixture =
            {
                .name = "query tautological conversions obey command-line suppression",
                .options = { "-Wno-tautological-constant-compare" },
            },
        },
        {
            .path = "tests/driver/query_bool_floating.c",
            .fixture =
            {
                .name = "query literal conversion suppression keeps unrelated folding warnings",
                .options = { "-Wno-literal-conversion" },
                .warnings = 1,
            },
        },
        {
            .path = "tests/driver/query_bool_execution.c",
            .fixture =
            {
                .name = "query diagnostics preserve accepted program evaluation",
                .warnings = 1,
            },
        },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(cases); f += 1)
    {
        const int file = open(cases[f].path, O_RDONLY | O_CLOEXEC);
        char* source = NULL;
        const bool read = file >= 0 && check_test_driver_read_file(file, &source);
        check_test_expect(test, read, "query boolean runtime fixture is read (run from the repository root)");
        if (read)
        {
            for (uint32_t language = 0; language < 2; language += 1)
            {
                CheckDriverDefaultWarningFixture fixture = cases[f].fixture;
                fixture.source = source;
                fixture.c23 = language != 0;
                check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
            }
        }
        if (file >= 0)
        {
            close(file);
        }
        free(source);
    }
    check_test_driver_query_bool_execution(test, clang, executable, directory);
}
