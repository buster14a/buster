// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original __auto_type qualifier sources (#1844). The shared diagnostic harness compares Clang's full output and
// object preservation through both kernels, one/three requested lanes and the default, -Werror and -w policies.

static void check_test_driver_auto_qualifiers(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    CheckTestFixtures files = { .storage = arena_reserve(UINT64_C(2) * CHECK_TEST_FIXTURE_BYTES + 4096) };
    uint64_t length = 0;
    char* catalog = files.storage.base != NULL ? check_test_fixture_read(&files, "tests/driver/auto-qualifiers/cases.txt", &length) : NULL;
    check_test_expect(test, catalog != NULL, "auto qualifier runtime case list is read");
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
        char extra;
        uint32_t c23 = 0;
        CheckDriverDefaultWarningFixture fixture = { 0 };
        valid = sscanf(line, "%127s %u %u %u %u %c", name, &fixture.warnings, &fixture.errors, &fixture.notes, &c23, &extra) == 5 &&
                check_test_fixture_word(name, sizeof(name) - 1) && c23 <= 1;
        check_test_expect(test, valid, "auto qualifier runtime case metadata is valid");
        if (valid)
        {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "tests/driver/auto-qualifiers/%s.c", name);
            fixture.name = name;
            fixture.c23 = c23 != 0;
            fixture.source = check_test_fixture_read(&files, path, &length);
            check_test_expect(test, fixture.source != NULL, path);
            if (fixture.source != NULL)
            {
                check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
                if (!fixture.c23)
                {
                    fixture.c23 = true;
                    check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
                }
            }
            count += 1;
            arena_pop_to(&files.storage, mark);
        }
    }
    check_test_expect(test, count != 0, "auto qualifier runtime case list is nonempty");
    arena_release(&files.storage);
}
