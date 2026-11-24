// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #928 declaration-region regressions, checked against external Clang.
// Include after the source-fixture and native-program helpers.

static void check_test_typeof_compounds(CheckTest* test, const char* clang)
{
    CheckTestFixtures catalog;
    const uint32_t file_count = check_test_private_catalog(test, &catalog, "checker/typeof-compound", NULL, 0, CHECK_TEST_CATALOG_GNU23);
    const CheckSourceFixture* fixtures = catalog.cases;
    CheckTestOracleBatch batch;
    const bool allocated = check_test_oracle_batch(test, &batch, &catalog.storage, file_count);
    char directory[] = "/tmp/buster-checker-typeof-compound-XXXXXX";
    const bool created = allocated && mkdtemp(directory) != NULL;
    bool ready = created;
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(batch.paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    check_test_expect(test, ready, "typeof compound fixture files are available");
    for (uint32_t dialect = 0; ready && dialect < 2; dialect += 1)
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < file_count; i += 1)
        {
            if ((fixtures[i].standard == 2023) == (dialect != 0))
            {
                batch.pointers[count] = batch.paths[i];
                batch.indices[count++] = i;
            }
        }
        const char* arguments[] = { dialect != 0 ? "-std=gnu23" : "-std=gnu17" };
        const bool oracle = count == 0 || parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), batch.pointers, count, batch.outcomes);
        check_test_expect(test, oracle, "typeof compound Clang oracle is available");
        for (uint32_t i = 0; oracle && i < count; i += 1)
        {
            const CheckSourceFixture* fixture = fixtures + batch.indices[i];
            check_test_expect(test, batch.outcomes[i].rejected == fixture->rejected, fixture->name);
            check_test_source(test, fixture, batch.pointers[i], batch.outcomes + i);
        }
    }
    for (uint32_t i = 0; created && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        unlink(batch.paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
    arena_release(&catalog.storage);
    // Missing has no definition: retaining its call in the emitted unit must fail the native comparison.
    check_test_program_suite(test, "checker/programs/typeof-compounds", clang);
}
