// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// The same nested integer/misc builtin expression crosses every source context. Clang supplies an independent
// accept/reject and diagnostic-location oracle; check_test_source compares the two checker paths' typed streams.

#include "buster/compiler/test_paths.h"
#ifndef BUSTER_C_CHECK_CONTEXT_SOURCE_TEST_INCLUDED
#define BUSTER_C_CHECK_CONTEXT_SOURCE_TEST_INCLUDED

static __attribute__((noinline)) void check_test_semantic_context_sources(CheckTest* test, const char* clang)
{
    static const char* const declarations[] = {
        "static const int k=7;\n",
        "static int k=7;\n",
        "static const volatile int k=7;\n",
    };
    static const char* const uses[] = {
        "int bound[V];\n",
        "enum { E=V };\n",
        "int f(int n) { switch(n) { case V: return 1; } return 0; }\n",
        "struct S { unsigned field:V; };\n",
        "_Static_assert(V==3, \"nested fold\");\n",
        "static int initialized=V;\n",
        "enum { B=__builtin_popcount(V) };\n",
        "enum { U=sizeof(V) };\n",
        "int f(void) { return V; }\n",
    };
    const char* temporary = oracle_temporary_directory();
    char directory[256];
    const int directory_length = snprintf(directory, sizeof(directory), "%s/buster-semantic-context-XXXXXX", temporary);
    const bool created = directory_length > 0 && (size_t)directory_length < sizeof(directory) && mkdtemp(directory) != NULL;
    check_test_expect(test, created, "semantic context source directory");
    const char* arguments[] = { "-std=gnu17" };
    for (uint32_t declaration = 0; created && declaration < BUSTER_ARRAY_LENGTH(declarations); declaration += 1)
    {
        for (uint32_t use = 0; use < BUSTER_ARRAY_LENGTH(uses); use += 1)
        {
            char source[1024];
            char name[96];
            char path[256];
            const int length = snprintf(source, sizeof(source), "%s#define V __builtin_expect(__builtin_popcount(k),0)\n%s",
                                        declarations[declaration], uses[use]);
            snprintf(name, sizeof(name), "semantic context: object %u, use %u", declaration, use);
            const int path_length = snprintf(path, sizeof(path), "%s/%u-%u.c", directory, declaration, use);
            char* paths[] = { path };
            ParseTestClang outcome = { 0 };
            const bool rejected = declaration != 0 && use < 7;
            const bool ready = length > 0 && (size_t)length < sizeof(source) && path_length > 0 && (size_t)path_length < sizeof(path) &&
                               parse_test_write(path, source, (uint64_t)length) &&
                               parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome);
            check_test_expect(test, ready, "nested builtin context Clang oracle runs");
            if (ready)
            {
                check_test_expect(test, outcome.rejected == rejected, name);
                const CheckSourceFixture fixture = { .name = name, .source = source, .rejected = rejected };
                check_test_source(test, &fixture, path, &outcome);
            }
            unlink(path);
        }
    }
    CheckTestFixtures catalog;
    const uint32_t file_count = check_test_private_catalog(test, &catalog, "checker/context-selections", NULL, 0, 0);
    const CheckSourceFixture* selections = catalog.cases;
    for (uint32_t i = 0; created && i < file_count; i += 1)
    {
        char path[256];
        const int path_length = snprintf(path, sizeof(path), "%s/selection-%u.c", directory, i);
        char* paths[] = { path };
        ParseTestClang outcome = { 0 };
        const bool ready = path_length > 0 && (size_t)path_length < sizeof(path) &&
                           parse_test_write(path, selections[i].source, strlen(selections[i].source)) &&
                           parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome);
        check_test_expect(test, ready, "selected/discarded context Clang oracle runs");
        if (ready)
        {
            check_test_expect(test, outcome.rejected == selections[i].rejected, selections[i].name);
            check_test_source(test, &selections[i], path, &outcome);
        }
        unlink(path);
    }
    if (created)
    {
        rmdir(directory);
    }
    arena_release(&catalog.storage);
    check_test_program_suite(test, "checker/programs/semantic-context-sources", clang);
}

#endif
