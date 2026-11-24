// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wgnu-empty-struct sources (#2409, c_check_input.c's CHECK_WARNING_EVENT_GNU_EMPTY_STRUCT). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static void check_test_driver_gnu_empty_struct(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/gnu-empty-struct", false);
}

// Array and record extension regressions are runtime data, including their option/diagnostic-count inventory.
static void check_test_driver_record_extensions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    CheckTestFixtures files = { .storage = arena_reserve(UINT64_C(2) * CHECK_TEST_FIXTURE_BYTES + 4096) };
    uint64_t length = 0;
    char* catalog = files.storage.base != NULL ? check_test_fixture_read(&files, "tests/driver/record-extensions/cases.txt", &length) : NULL;
    check_test_expect(test, catalog != NULL, "record extension runtime case list is read");
    const uint64_t mark = files.storage.length;
    char* cursor = NULL;
    char* line = catalog != NULL ? strtok_r(catalog, "\n", &cursor) : NULL;
    uint32_t count = 0;
    bool valid = catalog != NULL;
    for (; valid && line != NULL; line = strtok_r(NULL, "\n", &cursor))
    {
        if (line[0] == '#')
        {
            continue;
        }
        char name[128];
        char options[4][128];
        char extra;
        CheckDriverDefaultWarningFixture fixture = { 0 };
        valid = sscanf(line, "%127s %u %u %u %127s %127s %127s %127s %c", name, &fixture.warnings, &fixture.errors, &fixture.notes,
                       options[0], options[1], options[2], options[3], &extra) == 8 && check_test_fixture_word(name, sizeof(name) - 1);
        check_test_expect(test, valid, "record extension runtime case metadata is valid");
        if (valid)
        {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "tests/driver/record-extensions/%s.c", name);
            fixture.name = name;
            fixture.source = check_test_fixture_read(&files, path, &length);
            for (uint32_t option = 0; option < BUSTER_ARRAY_LENGTH(fixture.options); option += 1)
            {
                fixture.options[option] = strcmp(options[option], "-") != 0 ? options[option] : NULL;
            }
            check_test_expect(test, fixture.source != NULL, path);
            for (uint32_t standard = 0; fixture.source != NULL && standard < 2; standard += 1)
            {
                fixture.c23 = standard != 0;
                check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
            }
            count += 1;
            arena_pop_to(&files.storage, mark);
        }
    }
    check_test_expect(test, count != 0, "record extension runtime case list is nonempty");
    arena_release(&files.storage);
}
