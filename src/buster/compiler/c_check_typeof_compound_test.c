// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #928 declaration-region regressions, checked against external Clang.
// Include after the source-fixture and native-program helpers.

static void check_test_typeof_compounds(CheckTest* test, const char* clang)
{
    static const CheckSourceFixture fixtures[] =
    {
        {
            .name = "scalar compound literal in a typeof declaration",
            .source = "int f(void) { __typeof__((int){0}) x = 1; return x; }\n",
        },
        {
            .name = "file-scope scalar compound literal in typeof",
            .source = "__typeof__((int){1}) x; _Static_assert(sizeof x == sizeof(int), \"scalar\");\n",
        },
        {
            .name = "record compound literal in a typeof declaration",
            .source = "struct S { int x; }; int f(void) { __typeof__((struct S){0}) s = {1}; return s.x; }\n",
        },
        {
            .name = "array compound literal keeps its array type",
            .source = "int f(void) { __typeof__((int[2]){0}) a = {1}; _Static_assert(sizeof a == 2 * sizeof(int), \"array\"); return a[0]; }\n",
        },
        {
            .name = "inferred compound array bound is complete before typeof resumes",
            .source = "int f(void) { __typeof__((int[]){1,2,3}) a = {1}; _Static_assert(sizeof a == 3 * sizeof(int), \"bound\"); return a[0]; }\n",
        },
        {
            .name = "designated compound array bound is complete before typeof resumes",
            .source = "int f(void) { __typeof__((int[]){[3]=1}) a = {1}; _Static_assert(sizeof a == 4 * sizeof(int), \"designated bound\"); return a[0]; }\n",
        },
        {
            .name = "vector compound literal preserves its typedef type",
            .source = "typedef float V __attribute__((vector_size(16))); __typeof__((V){0}) v; _Static_assert(__builtin_types_compatible_p(__typeof__(v), V), \"vector\");\n",
        },
        {
            .name = "member of a compound literal supplies its own type",
            .source = "struct S { long x; }; int f(void) { __typeof__(((struct S){0}).x) x = 0; _Static_assert(sizeof x == sizeof(long), \"member\"); return x; }\n",
        },
        {
            .name = "multiple compound literals participate in operand conversions",
            .source = "int f(void) { __typeof__((int){0} + (long){1}) x = 1; _Static_assert(sizeof x == sizeof(long), \"conversion\"); return x; }\n",
        },
        {
            .name = "typeof retains compound literal const qualification",
            .source = "int f(void) { __typeof__((const int){0}) x = 1;\n x = 2; return x; }\n",
            .rejected = true,
        },
        {
            .name = "typeof does not decay a compound array into an assignable pointer",
            .source = "int f(void) { __typeof__((int[2]){0}) a = {1}; int *p = 0;\n a = p; return 0; }\n",
            .rejected = true,
        },
        {
            .name = "typeof still checks scalar compound designators",
            .source = "int f(void) {\n __typeof__((int){.x=1}) x = 0; return x; }\n",
            .rejected = true,
        },
        {
            .name = "typeof still checks out-of-bounds compound designators",
            .source = "int f(void) {\n __typeof__((int[2]){[3]=1}) a = {0}; return a[0]; }\n",
            .rejected = true,
        },
        {
            .name = "typeof still checks record compound designators",
            .source = "struct S { int x; }; int f(void) {\n __typeof__((struct S){.y=1}) s = {0}; return s.x; }\n",
            .rejected = true,
        },
        {
            .name = "typeof still binds compound initializer operands",
            .source = "int f(void) {\n __typeof__((int){missing}) x = 0; return x; }\n",
            .rejected = true,
        },
        {
            .name = "typeof rejects a compound literal bit-field operand",
            .source = "struct S { int x:1; }; int f(void) {\n __typeof__(((struct S){0}).x) x = 0; return x; }\n",
            .rejected = true,
        },
        {
            .name = "C23 typeof accepts compound arrays",
            .source = "int f(void) { typeof((int[2]){0}) a = {1}; _Static_assert(sizeof a == 2 * sizeof(int), \"C23 array\"); return a[0]; }\n",
            .standard = 2023,
        },
        {
            .name = "C23 typeof_unqual removes compound scalar const",
            .source = "int f(void) { typeof_unqual((const int){0}) x = 1; x = 2; return x; }\n",
            .standard = 2023,
        },
        {
            .name = "C23 typeof retains compound scalar const",
            .source = "int f(void) { typeof((const int){0}) x = 1;\n x = 2; return x; }\n",
            .rejected = true,
            .standard = 2023,
        },
        {
            .name = "C23 typeof_unqual also rejects a compound bit-field operand",
            .source = "struct S { int x:1; }; int f(void) {\n typeof_unqual(((struct S){0}).x) x = 0; return x; }\n",
            .rejected = true,
            .standard = 2023,
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-typeof-compound-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    bool ready = created;
    char paths[FILE_COUNT][160];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    check_test_expect(test, ready, "typeof compound fixture files are available");
    for (uint32_t dialect = 0; ready && dialect < 2; dialect += 1)
    {
        char* pointers[FILE_COUNT];
        uint32_t indices[FILE_COUNT];
        ParseTestClang outcomes[FILE_COUNT];
        uint32_t count = 0;
        for (uint32_t i = 0; i < FILE_COUNT; i += 1)
        {
            if ((fixtures[i].standard == 2023) == (dialect != 0))
            {
                pointers[count] = paths[i];
                indices[count++] = i;
            }
        }
        const char* arguments[] = { dialect != 0 ? "-std=gnu23" : "-std=gnu17" };
        const bool oracle = parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, count, outcomes);
        check_test_expect(test, oracle, "typeof compound Clang oracle is available");
        for (uint32_t i = 0; oracle && i < count; i += 1)
        {
            const CheckSourceFixture* fixture = fixtures + indices[i];
            check_test_expect(test, outcomes[i].rejected == fixture->rejected, fixture->name);
            check_test_source(test, fixture, pointers[i], outcomes + i);
        }
    }
    for (uint32_t i = 0; created && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
    // Missing has no definition: retaining its call in the emitted unit must fail the native comparison.
    check_test_program_source(test,
        "int missing(void); int tested(int n) { int x=n; __typeof__((int){missing() + ++x}) a=3; return x+a; }",
        "typeof-compound-unevaluated-call-and-update", clang);
    check_test_program_source(test,
        "struct S { int x; }; int tested(int n) { int x=n; __typeof__((struct S){++x}) s={3};"
        "__typeof__((int[]){++x,++x,++x}) a={1,2,4}; return x+s.x+a[2]+sizeof(a); }",
        "typeof-compound-unevaluated-record-and-inferred-array", clang);
    // A variably modified typeof operand is evaluated, so its compound initializer's effects run. The same operand
    // with a fixed type is unevaluated; both forms must agree with Clang's code on every input.
    check_test_program_source(test,
        "static void *bump(int *p) { ++*p; return 0; }"
        "int tested(int n) { int x = 0; int m = n > 0 ? n : 1;"
        "__typeof__((int (*)[m]){bump(&x)}) p = 0; return x * 100 + (p != 0); }",
        "typeof-variably-modified-compound-operand-is-evaluated", clang);
    check_test_program_source(test,
        "static void *bump(int *p) { ++*p; return 0; }"
        "int tested(int n) { int x = n; __typeof__((int (*)[3]){bump(&x)}) p = 0;"
        "return x * 100 + (p != 0); }",
        "typeof-fixed-compound-operand-is-unevaluated", clang);
    // The operand is evaluated before the declared object's initializer, as Clang orders a declaration's effects.
    check_test_program_source(test,
        "static void *bump(int *p, int v) { *p = *p * 10 + v; return 0; }"
        "int tested(int n) { int x = 0; int m = n > 0 ? n : 1;"
        "__typeof__((int (*)[m]){bump(&x, 1)}) p = bump(&x, 2); (void)p; return x; }",
        "typeof-variably-modified-operand-precedes-initializer", clang);
    check_test_program_source(test,
        "static void *bump(int *p, int v) { *p = *p * 10 + v; return 0; }"
        "int tested(int n) { int x = 0; (void)n;"
        "__typeof__((int (*)[3]){bump(&x, 1)}) p = bump(&x, 2); (void)p; return x; }",
        "typeof-fixed-operand-does-not-precede-initializer", clang);
}
