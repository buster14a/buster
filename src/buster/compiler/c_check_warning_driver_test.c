// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original default-warning sources. Clang supplies the external diagnostic oracle, including source policy.
// Include after the checker driver's spawn, count and file-comparison helpers. Initializer-only families have
// separate fixtures. These cases compare complete no-caret diagnostics through both kernels and lane requests.

typedef struct CheckDriverWarningSpelling CheckDriverWarningSpelling;
struct CheckDriverWarningSpelling
{
    const char* clang;
    const char* baseline;
};

typedef struct CheckDriverDefaultWarningFixture CheckDriverDefaultWarningFixture;
struct CheckDriverDefaultWarningFixture
{
    const char* name;
    const char* source;
    const char* options[4];
    uint32_t warnings;
    uint32_t errors;
    uint32_t notes;
    bool c23;
    bool compare_warning_text;
    bool compare_node_window;
    bool syntax_only;
    // An opt-in, NULL-terminated list of exact primary diagnostic spellings retained by a separate existing gap.
    // Every location, warning, note and diagnostic order still compares byte for byte. No ordinary fixture uses it.
    const CheckDriverWarningSpelling* baseline_spellings;
    uint64_t source_length; // Runtime sources can include NUL bytes.
};

// Exact output comparison with a fixture's explicitly frozen primary error spellings. A map includes the complete
// ": error: ..." primary text through its unique assertion message, so no source spelling or note is normalized.
static bool check_test_driver_warning_text_matches(const char* actual, const char* expected, const CheckDriverWarningSpelling* spellings)
{
    bool match = actual != NULL && expected != NULL && (spellings != NULL || strcmp(actual, expected) == 0);
    while (spellings != NULL && match && (*actual != 0 || *expected != 0))
    {
        bool replaced = false;
        for (uint32_t i = 0; spellings != NULL && spellings[i].clang != NULL && !replaced; i += 1)
        {
            const size_t length = strlen(spellings[i].clang);
            if (length != 0 && strncmp(expected, spellings[i].clang, length) == 0)
            {
                const size_t baseline_length = strlen(spellings[i].baseline);
                match = strncmp(actual, spellings[i].baseline, baseline_length) == 0;
                if (match)
                {
                    actual += baseline_length;
                    expected += length;
                }
                replaced = true;
            }
        }
        if (!replaced)
        {
            match = *actual == *expected;
            actual += match;
            expected += match;
        }
    }
    return match;
}

// A warning sweep of `fixtures`. With `deal`, it is a long sweep (test-checker-driver --part, check_test_driver_takes):
// each fixture is a unit of its own, which the caller doesn't deal; without it, the sweep is its caller's unit.
static void check_test_driver_warning_fixtures_dealt(CheckTest* test, const char* clang, const char* executable, const char* directory,
                                                     const CheckDriverDefaultWarningFixture* fixtures, uint32_t fixture_count, bool deal)
{
    static const char* const policies[] = { NULL, "-Werror", "-w" };
    static const char* const lanes[] = { "1", "3" };
    // test-checker-driver reports this sweep's time with each run mode (#2071, check_driver_run_modes).
    const uint64_t began = check_test_driver_nanoseconds();
    char source[PATH_MAX];
    char objects[4][PATH_MAX];
    snprintf(source, sizeof(source), "%s/default-warning.c", directory);
    for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
    {
        snprintf(objects[route], sizeof(objects[route]), "%s/default-warning-%u.o", directory, route);
    }
    uint64_t swept = 0;
    for (uint32_t f = 0; f < fixture_count; f += 1)
    {
        if (deal && !check_test_driver_takes())
        {
            continue;
        }
        swept += 1;
        const CheckDriverDefaultWarningFixture* fixture = fixtures + f;
        const bool written = parse_test_write(source, fixture->source, fixture->source_length != 0 ? fixture->source_length : strlen(fixture->source));
        bool mismatch_printed = false;
        check_test_expect(test, written, "original default-warning fixture is written");
        for (uint32_t policy = 0; written && policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
        {
            for (uint32_t mode = 0; mode < (fixture->syntax_only ? 1u : 2u); mode += 1)
            {
                char* arguments[20] = { (char*)clang, fixture->c23 ? "-std=c23" : "-std=gnu17", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                        mode == 0 ? "-fsyntax-only" : "-c", source, "-o", objects[2] };
                uint32_t count = 8;
                for (uint32_t option = 0; option < BUSTER_ARRAY_LENGTH(fixture->options); option += 1)
                {
                    if (fixture->options[option] != NULL)
                    {
                        arguments[count++] = (char*)fixture->options[option];
                    }
                }
                if (policies[policy] != NULL)
                {
                    arguments[count++] = (char*)policies[policy];
                }
                unlink(objects[2]);
                char* expected = NULL;
                unlink(objects[3]);
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(expected);
                const bool oracle_ready = (oracle == 0 || oracle == 1) && expected != NULL;
                char description[256];
                snprintf(description, sizeof(description), "%s policy=%u mode=%u: independent Clang oracle", fixture->name, policy, mode);
                const bool oracle_matches = oracle_ready && (policy != 0 || (oracle == (fixture->errors != 0) && counts.warnings == fixture->warnings &&
                                             counts.errors == fixture->errors && counts.notes == fixture->notes));
                if (policy == 0 && !oracle_matches)
                {
                    fprintf(stderr, "warning fixture oracle mismatch: %s mode=%u\nexpected status=%d warnings=%u errors=%u notes=%u; "
                                    "actual status=%d warnings=%u errors=%u notes=%u\n%s\n",
                            fixture->name, mode, fixture->errors != 0, fixture->warnings, fixture->errors, fixture->notes,
                            oracle, counts.warnings, counts.errors, counts.notes, expected != NULL ? expected : "(no diagnostic output)");
                }
                check_test_expect(test, oracle_matches, description);
                for (uint32_t lane = 0; oracle_ready && lane < BUSTER_ARRAY_LENGTH(lanes); lane += 1)
                {
                    const char* alternate = fixture->compare_node_window ? "--node-window-assignments" : "--scalar";
                    for (uint32_t route = 0; route < 2; route += 1)
                    {
                        arguments[0] = (char*)(executable != NULL ? executable : "/proc/self/exe");
                        arguments[7] = objects[route];
                        arguments[count] = "--lanes";
                        arguments[count + 1] = (char*)lanes[lane];
                        arguments[count + 2] = route != 0 ? (char*)alternate : NULL;
                        arguments[count + 3] = NULL;
                        unlink(objects[route]);
                        static const char sentinel[] = "preserved-warning-output";
                        const bool preserve = mode != 0 && oracle == 1;
                        const bool prior = !preserve || parse_test_write(objects[route], sentinel, sizeof(sentinel));
                        char* actual = NULL;
                        const int status = check_test_driver_spawn(arguments, &actual);
                        snprintf(description, sizeof(description), "%s policy=%u mode=%u lanes=%s %s: complete Clang diagnostics",
                                 fixture->name, policy, mode, lanes[lane], route != 0 ? alternate : "default");
                        const char* expected_text = fixture->compare_warning_text ? strstr(expected, "warning: ") : NULL;
                        expected_text = fixture->compare_warning_text && expected_text == NULL ? strstr(expected, "error: ") : expected_text;
                        const char* actual_text = fixture->compare_warning_text && expected_text != NULL ?
                                                  strstr(actual != NULL ? actual : "", policy == 1 ? "error: " : "warning: ") : NULL;
                        const bool diagnostics_match = check_test_driver_warning_text_matches(expected_text != NULL ? actual_text : actual,
                                                                                               expected_text != NULL ? expected_text : expected,
                                                                                               fixture->baseline_spellings);
                        const bool same = status == oracle && diagnostics_match;
                        check_test_expect(test, same, description);
                        if (!same && !mismatch_printed)
                        {
                            fprintf(stderr, "%s: status %d (clang %d)\nBuster:\n%s\nClang:\n%s\n", description,
                                    status, oracle, actual != NULL ? actual : "<no output>\n", expected);
                            mismatch_printed = true;
                        }
                        if (policy == 0 && lane == 0 && route == 0)
                        {
                            // The reach filter's audit (#1340, BUSTER_TEST_COMMON_REACH_AUDIT) on the fixture, under its own
                            // options: the unit fails where a node the filter took out has a bit of its own reach.
                            char* audited = NULL;
                            arguments[7] = objects[2];
                            unlink(objects[2]);
                            const int audit = setenv("BUSTER_TEST_COMMON_REACH_AUDIT", "1", 1) == 0 ? check_test_driver_spawn(arguments, &audited) : -1;
                            unsetenv("BUSTER_TEST_COMMON_REACH_AUDIT");
                            unlink(objects[2]);
                            arguments[7] = objects[route];
                            snprintf(description, sizeof(description), "%s mode=%u: the reach filter takes out no node with a bit", fixture->name, mode);
                            check_test_expect(test, audit == status && audited != NULL && actual != NULL && strcmp(audited, actual) == 0, description);
                            free(audited);
                        }
                        if (preserve)
                        {
                            FILE* retained = fopen(objects[route], "rb");
                            char bytes[sizeof(sentinel)] = { 0 };
                            const bool unchanged = retained != NULL && fread(bytes, 1, sizeof(bytes), retained) == sizeof(bytes) &&
                                                   memcmp(bytes, sentinel, sizeof(bytes)) == 0 && fgetc(retained) == EOF;
                            check_test_expect(test, prior && unchanged, "promoted default warnings preserve an existing output file");
                            if (retained != NULL)
                            {
                                fclose(retained);
                            }
                        }
                        else
                        {
                            check_test_expect(test, (access(objects[route], F_OK) == 0) == (mode != 0 && status == 0),
                                              "default warning policy controls object publication");
                        }
                        free(actual);
                    }
                    if (mode != 0 && oracle == 0)
                    {
                        check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]),
                                          "default warning source has identical objects through both routes at each lane request");
                        if (lane == 0)
                        {
                            check_test_expect(test, link(objects[0], objects[3]) == 0, "the first lane request retains a frozen object twin");
                        }
                        else
                        {
                            check_test_expect(test, check_test_driver_same_files(objects[0], objects[3]),
                                              "default warning source has identical objects across requested lane counts");
                        }
                    }
                }
                free(expected);
                for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
                {
                    unlink(objects[route]);
                }
            }
        }
    }
    unlink(source);
    check_driver_run_modes.sweep_fixtures += swept;
    check_driver_run_modes.sweep_nanoseconds += check_test_driver_nanoseconds() - began;
}

static void check_test_driver_warning_fixtures(CheckTest* test, const char* clang, const char* executable, const char* directory,
                                               const CheckDriverDefaultWarningFixture* fixtures, uint32_t fixture_count)
{
    check_test_driver_warning_fixtures_dealt(test, clang, executable, directory, fixtures, fixture_count, false);
}

// Options are individual argv entries. Catalogs may change warning and language policy. All input,
// output and plugin paths remain under the driver's control.
static bool check_test_driver_warning_option(const char* option)
{
    static const char* const switches[] = {
        "-w", "-pedantic", "-pedantic-errors", "-fbuiltin", "-fno-builtin", "-ffreestanding",
        "-fwrapv", "-fno-wrapv", "-fno-strict-overflow", "-funsigned-char", "-fsigned-char",
    };
    bool valid = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(switches); i += 1)
    {
        valid |= strcmp(option, switches[i]) == 0;
    }
    if (strncmp(option, "-W", 2) == 0 || strncmp(option, "-std=", 5) == 0 ||
        strncmp(option, "-fno-builtin-", 13) == 0 || strncmp(option, "-ferror-limit=", 14) == 0)
    {
        valid = strlen(option) < 128 && option[strlen(option) - 1] != '=' &&
                strspn(option, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-=+_") == strlen(option);
    }
    return valid;
}

static bool check_test_driver_warning_policy(CheckDriverDefaultWarningFixture* fixture, char* flags, char* options)
{
    bool valid = true;
    if (strcmp(flags, "-") != 0)
    {
        while (valid && flags != NULL)
        {
            const char* flag = strsep(&flags, ",");
            bool* field = NULL;
            if (strcmp(flag, "c23") == 0)
            {
                field = &fixture->c23;
            }
            else if (strcmp(flag, "warning-text") == 0)
            {
                field = &fixture->compare_warning_text;
            }
            else if (strcmp(flag, "node-window") == 0)
            {
                field = &fixture->compare_node_window;
            }
            else if (strcmp(flag, "syntax-only") == 0)
            {
                field = &fixture->syntax_only;
            }
            valid = field != NULL && !*field;
            if (valid)
            {
                *field = true;
            }
        }
    }
    if (strcmp(options, "-") != 0)
    {
        uint32_t count = 0;
        while (valid && options != NULL)
        {
            const char* option = strsep(&options, " ");
            valid = count < BUSTER_ARRAY_LENGTH(fixture->options) && check_test_driver_warning_option(option);
            if (valid)
            {
                fixture->options[count++] = option;
            }
        }
    }
    return valid;
}

static bool check_test_driver_warning_count(const char* text, uint32_t* count)
{
    char* end = NULL;
    const unsigned long value = strtoul(text, &end, 10);
    const bool valid = *text >= '0' && *text <= '9' && *end == 0 && value <= UINT32_MAX;
    if (valid)
    {
        *count = (uint32_t)value;
    }
    return valid;
}

// The v1 suites use their filename as ID and their caller's dialect. A v2 row has its own ID,
// allowing several option or expectation variants to share a source. Load the whole inventory
// before dealing the sweep so every part validates it, including the files its peers will run.
static void check_test_driver_warning_catalog_run(CheckTest* test, const char* clang, const char* executable, const char* directory,
                                                  const char* suite, bool deal, bool c23)
{
    CheckTestFixtures storage = { .storage = arena_reserve(UINT64_C(1) << 27) };
    char suite_path[PATH_MAX];
    const int path_length = snprintf(suite_path, sizeof(suite_path), "%s/%s", check_test_fixture_root, suite);
    char path[PATH_MAX + 132];
    snprintf(path, sizeof(path), "%s/catalog.tsv", suite_path);
    uint64_t length = 0;
    char* catalog = check_test_fixture_read(&storage, path, &length);
    CheckDriverDefaultWarningFixture* cases = arena_push(&storage.storage, sizeof(*cases) * CHECK_TEST_FIXTURE_CASES, 8);
    const char** ids = arena_push(&storage.storage, sizeof(*ids) * CHECK_TEST_FIXTURE_CASES, 8);
    storage.files = arena_push(&storage.storage, sizeof(*storage.files) * CHECK_TEST_FIXTURE_CASES, 8);
    bool valid = path_length >= 0 && (size_t)path_length < sizeof(suite_path) && cases != NULL && ids != NULL && storage.files != NULL &&
                 catalog != NULL && length != 0 && catalog[length - 1] == '\n';
    uint32_t line = 1;
    const char* header = valid ? strsep(&catalog, "\n") : "";
    const bool v2 = strcmp(header, "buster-fixtures v2 checker-warnings") == 0;
    valid = valid && (v2 || strcmp(header, "buster-fixtures v1 checker-warnings") == 0);
    while (valid && *catalog != 0)
    {
        char* row = strsep(&catalog, "\n");
        line += 1;
        if (row[0] == '#')
        {
            continue;
        }
        char* id = strsep(&row, "\t");
        char* file = v2 ? strsep(&row, "\t") : id;
        char* warnings = strsep(&row, "\t");
        char* errors = strsep(&row, "\t");
        char* notes = strsep(&row, "\t");
        char* flags = v2 ? strsep(&row, "\t") : NULL;
        char* options = v2 ? strsep(&row, "\t") : NULL;
        const uint32_t index = storage.count;
        valid = row != NULL && *row != 0 && strchr(row, '\t') == NULL && index < CHECK_TEST_FIXTURE_CASES &&
                check_test_fixture_word(id, v2 ? 64 : 128) && check_test_fixture_word(file, 128) &&
                strlen(file) > 2 && strcmp(file + strlen(file) - 2, ".c") == 0;
        if (valid)
        {
            CheckDriverDefaultWarningFixture* fixture = &cases[index];
            *fixture = (CheckDriverDefaultWarningFixture){ .name = row, .c23 = !v2 && c23 };
            valid = check_test_driver_warning_count(warnings, &fixture->warnings) && check_test_driver_warning_count(errors, &fixture->errors) &&
                    check_test_driver_warning_count(notes, &fixture->notes) && (!v2 || check_test_driver_warning_policy(fixture, flags, options));
            for (uint32_t i = 0; valid && i < index; i += 1)
            {
                valid = strcmp(id, ids[i]) != 0;
                if (!valid)
                {
                    snprintf(storage.problem, sizeof(storage.problem), "duplicate case ID '%s'", id);
                }
                if (strcmp(file, storage.files[i]) == 0)
                {
                    fixture->source = cases[i].source;
                    fixture->source_length = cases[i].source_length;
                }
            }
            if (valid && fixture->source == NULL)
            {
                snprintf(path, sizeof(path), "%s/%s", suite_path, file);
                fixture->source = check_test_fixture_read_bytes(&storage, path, true, &fixture->source_length);
                valid = fixture->source != NULL;
            }
            ids[index] = id;
            storage.files[index] = file;
            storage.count += 1;
        }
    }
    valid = valid && storage.count != 0 && check_test_fixture_orphans(&storage, suite_path);
    if (!valid)
    {
        fprintf(stderr, "%s/catalog.tsv line %u: %s\n", suite_path, line, storage.problem[0] ? storage.problem : "invalid warning catalog");
    }
    check_test_expect(test, valid, "runtime warning catalog is complete and well formed");
    if (valid)
    {
        check_test_driver_warning_fixtures_dealt(test, clang, executable, directory, cases, storage.count, deal);
    }
    arena_release(&storage.storage);
}

static void check_test_driver_warning_suite(CheckTest* test, const char* clang, const char* executable, const char* directory, const char* suite, bool deal)
{
    check_test_driver_warning_catalog_run(test, clang, executable, directory, suite, deal, false);
}

static void check_test_driver_warning_catalog(CheckTest* test, const char* clang, const char* executable, const char* directory,
                                              const char* suite)
{
    check_test_driver_warning_suite(test, clang, executable, directory, suite, false);
}

// The default warnings' sweep, the driver's longest: a long sweep, one unit per fixture, which its caller doesn't deal.
static void check_test_driver_default_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/default-warning", true);
    if (check_test_driver_takes())
    {
        check_test_driver_warning_catalog(test, clang, executable, directory, "checker/division-by-zero");
    }
}

// -flazy-system-headers defers only system-header bodies. A unit must still publish its translation-unit-final
// warnings, exactly as a run without the flag does: one with no deferred body, and one whose system header has a body
// the planner defers (an unused static function's), whose user code's internal function is used and never defined.
static void check_test_driver_lazy_final_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char header_text[] = "#pragma GCC system_header\nstatic int unused(int x){return x+1;}\n";
    static const char* const source_texts[2] = {
        "static int missing(void);int f(void){return missing();}\n",
        "#include \"lazy-final-warning.h\"\nstatic int missing(void);int f(void){return missing();}\n",
    };
    char header[PATH_MAX];
    char source[PATH_MAX];
    snprintf(header, sizeof(header), "%s/lazy-final-warning.h", directory);
    snprintf(source, sizeof(source), "%s/lazy-final-warning.c", directory);
    for (uint32_t deferring = 0; deferring < 2; deferring += 1)
    {
        const bool written = parse_test_write(header, header_text, sizeof(header_text) - 1) &&
                             parse_test_write(source, source_texts[deferring], strlen(source_texts[deferring]));
        char* expected = NULL;
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", "-fno-color-diagnostics", "-fsyntax-only", source, NULL };
        const int oracle = written ? check_test_driver_spawn(arguments, &expected) : -1;
        const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(expected);
        check_test_expect(test, oracle == 0 && expected != NULL && counts.warnings == 1 && counts.notes == 1,
                          deferring != 0 ? "a lazy-system-header run with a deferred body has Clang's final warning oracle" :
                                           "a lazy-system-header run without a deferred body has Clang's final warning oracle");
        for (uint32_t route = 0; written && route < 2; route += 1)
        {
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* run[] = { (char*)(executable != NULL && route != 0 ? executable : "/proc/self/exe"), "-std=gnu17",
                                "-fno-caret-diagnostics", "-fno-color-diagnostics", "-fsyntax-only", "-flazy-system-headers", source,
                                scalar != 0 ? "--scalar" : NULL, NULL };
                char* actual = NULL;
                const int status = check_test_driver_spawn(run, &actual);
                check_test_expect(test, status == oracle && actual != NULL && expected != NULL && strcmp(actual, expected) == 0,
                                  deferring != 0 ? (scalar != 0 ? "lazy-system-headers scalar run with a deferred body publishes the same final warning" :
                                                                  "lazy-system-headers vector run with a deferred body publishes the same final warning") :
                                                   (scalar != 0 ? "lazy-system-headers scalar run publishes the same final warning" :
                                                                  "lazy-system-headers vector run publishes the same final warning"));
                free(actual);
            }
        }
        free(expected);
    }
    unlink(source);
    unlink(header);
}
