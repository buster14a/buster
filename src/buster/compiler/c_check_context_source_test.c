// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The same nested integer/misc builtin expression crosses every source context. Clang supplies an independent
// accept/reject and diagnostic-location oracle; check_test_source compares the two checker paths' typed streams.
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
    char directory[] = "/tmp/buster-semantic-context-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    check_test_expect(test, created, "semantic context source directory");
    const char* arguments[] = { "-std=gnu17" };
    for (uint32_t declaration = 0; created && declaration < BUSTER_ARRAY_LENGTH(declarations); declaration += 1)
    {
        for (uint32_t use = 0; use < BUSTER_ARRAY_LENGTH(uses); use += 1)
        {
            char source[1024];
            char name[96];
            char path[160];
            const int length = snprintf(source, sizeof(source), "%s#define V __builtin_expect(__builtin_popcount(k),0)\n%s", declarations[declaration], uses[use]);
            snprintf(name, sizeof(name), "semantic context: object %u, use %u", declaration, use);
            snprintf(path, sizeof(path), "%s/%u-%u.c", directory, declaration, use);
            char* paths[] = { path };
            ParseTestClang outcome = { 0 };
            const bool rejected = declaration != 0 && use < 7;
            const bool ready = length > 0 && (size_t)length < sizeof(source) && parse_test_write(path, source, (uint64_t)length) &&
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
    const CheckSourceFixture selections[] = {
        {
            .name = "context: unselected discarded read is not evaluated",
            .source = "int m; static const int k=7; enum { E=1 ? __builtin_popcount(k) : (m,3) }; _Static_assert(E==3, \"\");\n",
        },
        {
            .name = "context: selected discarded read is unevaluable",
            .source = "int m; static int x=(m,3);\n",
            .rejected = true,
        },
        {
            .name = "context: unselected arithmetic fault is deferred",
            .source = "static const int k=7; enum { E=1 ? __builtin_popcount(k) : 1/0 }; _Static_assert(E==3, \"\");\n",
        },
        {
            .name = "context: selected arithmetic fault is diagnosed",
            .source = "enum { E=0 ? 3 : 1/0 };\n",
            .rejected = true,
        },
        {
            .name = "context: inactive union address is not a read",
            .source = "union U { int i; unsigned u; }; static const union U v={.i=7}; static const unsigned *p=&v.u;\n",
        },
        {
            .name = "context: inactive union member cannot supply a receipt",
            .source = "union U { int i; unsigned u; }; static const union U v={.i=7}; enum { E=__builtin_popcount(v.u) };\n",
            .rejected = true,
        },
        {
            .name = "context: qualifier-only pointer path retains receipt evidence",
            .source = "static const int k=7; enum { E=__builtin_popcount(*(const int *)&k) }; _Static_assert(E==3, \"\");\n",
        },
        {
            .name = "context: void round trip cannot recreate receipt evidence",
            .source = "static const int k=7; enum { E=__builtin_popcount(*(const int *)(const void *)&k) };\n",
            .rejected = true,
        },
    };
    for (uint32_t i = 0; created && i < BUSTER_ARRAY_LENGTH(selections); i += 1)
    {
        char path[160];
        snprintf(path, sizeof(path), "%s/selection-%u.c", directory, i);
        char* paths[] = { path };
        ParseTestClang outcome = { 0 };
        const bool ready = parse_test_write(path, selections[i].source, strlen(selections[i].source)) &&
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
    check_test_program_source(test,
        "static int left(void) { const int k=7; return __builtin_expect(__builtin_popcount(k),0); }"
        "static int right(void) { const int k=8; return __builtin_expect(__builtin_popcount(k),0); }"
        "int tested(int n) { volatile unsigned v=(unsigned)n; _Atomic(unsigned) a=(unsigned)n;"
        "unsigned x=v; v=x+1; __c11_atomic_store(&a,x+2,5);"
        "return left()*100+right()*10+(int)__builtin_popcount(v)+(int)__builtin_popcount(__c11_atomic_load(&a,5)); }",
        "semantic-context-distinct-bodies-runtime-volatile-atomic", clang);
}

#endif
