// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent truth tables for the feature grammar, all branch outcomes, malformed/unknown metadata and bounded
// scratch. Guard pages end expression, feature-name, table, output and scratch slices at their exact logical bounds.
// Include after c_builtin_intrinsic_features.c. No CPU detection, compiler invocation or target execution is used.

#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_INTRINSIC_FEATURE_TEST_DEPTH 64u

typedef struct CBuiltinIntrinsicFeatureTest CBuiltinIntrinsicFeatureTest;
struct CBuiltinIntrinsicFeatureTest
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CBuiltinIntrinsicFeatureTestPage CBuiltinIntrinsicFeatureTestPage;
struct CBuiltinIntrinsicFeatureTestPage
{
    uint8_t* mapping;
    uint64_t page_size;
};

static void cbuiltin_intrinsic_feature_test_expect(CBuiltinIntrinsicFeatureTest* test, bool condition, const char* name)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 24)
        {
            fprintf(stderr, "test-builtin-intrinsic-features: %s\n", name);
        }
        test->failures += 1;
    }
}

static void cbuiltin_intrinsic_feature_test_status(CBuiltinIntrinsicFeatureTest* test, const char* expression, uint32_t length,
                                                 const CBuiltinIntrinsicFeature* features, uint32_t count, CBuiltinIntrinsicFeatureStatus expected)
{
    CBuiltinIntrinsicFeatureFrame frames[BUSTER_INTRINSIC_FEATURE_TEST_DEPTH];
    const CBuiltinIntrinsicFeatureStatus actual = c_builtin_intrinsic_features_evaluate(expression, length, features, count, frames, BUSTER_INTRINSIC_FEATURE_TEST_DEPTH);
    cbuiltin_intrinsic_feature_test_expect(test, actual == expected, "expression status");
}

static void cbuiltin_intrinsic_feature_test_truth(CBuiltinIntrinsicFeatureTest* test)
{
    // Bit n describes assignment a=(n&1), b=(n&2), c=(n&4), d=(n&8). Masks are independent truth tables.
    static const struct
    {
        const char* expression;
        uint16_t mask;
    } cases[] = {
        { "", 0xffff },
        { "a", 0xaaaa },
        { "b", 0xcccc },
        { "(a)", 0xaaaa },
        { "((a))", 0xaaaa },
        { "a,b", 0x8888 },
        { "a|b", 0xeeee },
        { "a,b|c", 0xf8f8 },
        { "a,(b|c)", 0xa8a8 },
        { "a|b,c", 0xeaea },
        { "(a|b),c", 0xe0e0 },
        { "(a|b),(c|d)", 0xeee0 },
        { "((a,b)|(c,d))", 0xf888 },
        { "a,(b|c,d)", 0xa888 },
        { "(a|b),(c|d),a", 0xaaa0 },
        { "a|b|c|d", 0xfffe },
        { "a,b,c,d", 0x8000 },
        { "a,a|b,b", 0xeeee },
    };
    for (uint32_t assignment = 0; assignment < 16; assignment += 1)
    {
        CBuiltinIntrinsicFeature features[] = {
            { "a", 1, (assignment & 1) != 0 },
            { "b", 1, (assignment & 2) != 0 },
            { "c", 1, (assignment & 4) != 0 },
            { "d", 1, (assignment & 8) != 0 },
        };
        for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
        {
            const bool available = (cases[i].mask & (1u << assignment)) != 0;
            const CBuiltinIntrinsicFeatureStatus expected = available ? CBUILTIN_INTRINSIC_FEATURE_AVAILABLE : CBUILTIN_INTRINSIC_FEATURE_MISSING;
            cbuiltin_intrinsic_feature_test_status(test, cases[i].expression, (uint32_t)strlen(cases[i].expression), features, 4, expected);
        }
    }
}

static void cbuiltin_intrinsic_feature_test_constraints(CBuiltinIntrinsicFeatureTest* test)
{
    const CBuiltinIntrinsicFeature features[] = {
        { "a", 1, true },
        { "b", 1, false },
        { "avx512vl", 8, true },
        { "avx512vnni", 10, false },
        { "avxvnni", 7, true },
        { "fma4", 4, true },
        { "sse4.1", 6, true },
        { "amx-tile", 8, true },
    };
    const uint32_t feature_count = sizeof(features) / sizeof(features[0]);
    const char* invalid[] = {
        ",", "|", "(", ")", "()", "a,", "a|", ",a", "|a", "a,,b", "a||b", "a,|b", "a|,b",
        "(a", "a)", "(a)(b)", "a(b)", "(a)b", "a b", "a\tb", "a\n", "a\r", "a\va", "a\fa",
        "a|(b,)", "a|(b|)", "a|(z,)", "z|a,", "z,a)", "z a",
    };
    for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i += 1)
    {
        cbuiltin_intrinsic_feature_test_status(test, invalid[i], (uint32_t)strlen(invalid[i]), features, feature_count, CBUILTIN_INTRINSIC_FEATURE_INVALID);
    }
    const char* unknown[] = { "z", "a|z", "b,z", "z|a", "a|(b|z)", "A", "sse4", "fma", "!a", "a&b" };
    for (uint32_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i += 1)
    {
        cbuiltin_intrinsic_feature_test_status(test, unknown[i], (uint32_t)strlen(unknown[i]), features, feature_count, CBUILTIN_INTRINSIC_FEATURE_UNSUPPORTED);
    }
    const char mixed[] = "avx512vl,avx512vnni|avxvnni";
    cbuiltin_intrinsic_feature_test_status(test, mixed, sizeof(mixed) - 1, features, feature_count, CBUILTIN_INTRINSIC_FEATURE_AVAILABLE);
    const char punctuated[] = "fma4,sse4.1,amx-tile";
    cbuiltin_intrinsic_feature_test_status(test, punctuated, sizeof(punctuated) - 1, features, feature_count, CBUILTIN_INTRINSIC_FEATURE_AVAILABLE);
    const char embedded[] = { 'a', 0, '|', 'b' };
    cbuiltin_intrinsic_feature_test_status(test, embedded, sizeof(embedded), features, feature_count, CBUILTIN_INTRINSIC_FEATURE_INVALID);
    cbuiltin_intrinsic_feature_test_status(test, 0, 0, 0, 0, CBUILTIN_INTRINSIC_FEATURE_AVAILABLE);
    cbuiltin_intrinsic_feature_test_status(test, 0, 1, features, feature_count, CBUILTIN_INTRINSIC_FEATURE_INVALID);
    cbuiltin_intrinsic_feature_test_status(test, "a", 1, 0, 1, CBUILTIN_INTRINSIC_FEATURE_INVALID);
    cbuiltin_intrinsic_feature_test_status(test, "a", 1, 0, 0, CBUILTIN_INTRINSIC_FEATURE_UNSUPPORTED);
    cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate("a", 1, features, feature_count, 0, 1) == CBUILTIN_INTRINSIC_FEATURE_INVALID,
                                         "null scratch with positive capacity");
    cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate("a|b", 3, features, feature_count, 0, 0) == CBUILTIN_INTRINSIC_FEATURE_AVAILABLE,
                                         "root expression needs no scratch");
    cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate("(a)", 3, features, feature_count, 0, 0) == CBUILTIN_INTRINSIC_FEATURE_CAPACITY,
                                         "nested expression needs scratch");
    cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate("a|(b)", 5, features, feature_count, 0, 0) == CBUILTIN_INTRINSIC_FEATURE_CAPACITY,
                                         "available alternative does not skip nested scratch requirements");
    cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate("b,(a)", 5, features, feature_count, 0, 0) == CBUILTIN_INTRINSIC_FEATURE_CAPACITY,
                                         "missing conjunction does not skip nested scratch requirements");
    const CBuiltinIntrinsicFeature invalid_tables[][2] = {
        { { "a", 1, true }, { "a", 1, false } },
        { { "a", 1, true }, { "a", 1, true } },
        { { "a", 1, true }, { 0, 1, false } },
        { { "a", 1, true }, { "", 0, false } },
        { { "a", 1, true }, { "a b", 3, false } },
        { { "a", 1, true }, { "a,b", 3, false } },
        { { "a", 1, true }, { "a|b", 3, false } },
        { { "a", 1, true }, { "(a)", 3, false } },
        { { "a", 1, true }, { embedded, sizeof(embedded), false } },
    };
    for (uint32_t i = 0; i < sizeof(invalid_tables) / sizeof(invalid_tables[0]); i += 1)
    {
        cbuiltin_intrinsic_feature_test_status(test, "a", 1, invalid_tables[i], 2, CBUILTIN_INTRINSIC_FEATURE_INVALID);
        cbuiltin_intrinsic_feature_test_status(test, 0, 0, invalid_tables[i], 2, CBUILTIN_INTRINSIC_FEATURE_INVALID);
    }
    const CBuiltinIntrinsicFeature case_distinct[] = {
        { "a", 1, true },
        { "A", 1, false },
    };
    cbuiltin_intrinsic_feature_test_status(test, "A", 1, case_distinct, 2, CBUILTIN_INTRINSIC_FEATURE_MISSING);
}

static void cbuiltin_intrinsic_feature_test_batch(CBuiltinIntrinsicFeatureTest* test)
{
    const CBuiltinIntrinsicFeature features[] = {
        { "a", 1, true },
        { "b", 1, false },
    };
    const CBuiltinIntrinsicFeatureExpression expressions[] = {
        { 0, 0 },
        { "a", 1 },
        { "b", 1 },
        { "a,b", 3 },
        { "a|(b)", 5 },
        { "a|z", 3 },
        { "a|z,", 4 },
        { "((a))", 5 },
        { "a,", 2 },
    };
    const CBuiltinIntrinsicFeatureStatus expected[] = {
        CBUILTIN_INTRINSIC_FEATURE_AVAILABLE,
        CBUILTIN_INTRINSIC_FEATURE_AVAILABLE,
        CBUILTIN_INTRINSIC_FEATURE_MISSING,
        CBUILTIN_INTRINSIC_FEATURE_MISSING,
        CBUILTIN_INTRINSIC_FEATURE_AVAILABLE,
        CBUILTIN_INTRINSIC_FEATURE_UNSUPPORTED,
        CBUILTIN_INTRINSIC_FEATURE_INVALID,
        CBUILTIN_INTRINSIC_FEATURE_AVAILABLE,
        CBUILTIN_INTRINSIC_FEATURE_INVALID,
    };
    CBuiltinIntrinsicFeatureFrame frames[2];
    CBuiltinIntrinsicFeatureStatus statuses[sizeof(expected) / sizeof(expected[0])];
    const uint32_t count = sizeof(expected) / sizeof(expected[0]);
    cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate_batch(expressions, count, features, 2, frames, 2, statuses) ==
                                         CBUILTIN_INTRINSIC_FEATURE_AVAILABLE, "valid batch envelope");
    cbuiltin_intrinsic_feature_test_expect(test, memcmp(statuses, expected, sizeof(expected)) == 0, "batch statuses");
    for (uint32_t partition = 1; partition <= count; partition += 1)
    {
        memset(statuses, 0xff, sizeof(statuses));
        for (uint32_t first = 0; first < count;)
        {
            const uint32_t remaining = count - first;
            const uint32_t batch = remaining < partition ? remaining : partition;
            cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate_batch(expressions + first, batch, features, 2, frames, 2,
                                                  statuses + first) == CBUILTIN_INTRINSIC_FEATURE_AVAILABLE, "partition envelope");
            first += batch;
        }
        cbuiltin_intrinsic_feature_test_expect(test, memcmp(statuses, expected, sizeof(expected)) == 0, "partition statuses");
    }
    cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate_batch(0, 0, 0, 0, 0, 0, 0) == CBUILTIN_INTRINSIC_FEATURE_AVAILABLE,
                                         "empty batch");
    memset(statuses, 0x5a, sizeof(statuses));
    CBuiltinIntrinsicFeatureStatus before[sizeof(expected) / sizeof(expected[0])];
    memcpy(before, statuses, sizeof(before));
    cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate_batch(0, 1, features, 2, frames, 2, statuses) ==
                                         CBUILTIN_INTRINSIC_FEATURE_INVALID, "invalid batch input");
    cbuiltin_intrinsic_feature_test_expect(test, memcmp(statuses, before, sizeof(before)) == 0, "invalid batch preserves output");
    cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate_batch(expressions, 1, features, 2, frames, 2, 0) ==
                                         CBUILTIN_INTRINSIC_FEATURE_INVALID, "invalid batch output");
}

static bool cbuiltin_intrinsic_feature_test_page_open(CBuiltinIntrinsicFeatureTestPage* page)
{
    const long size = sysconf(_SC_PAGESIZE);
    bool ready = size > 0;
    *page = (CBuiltinIntrinsicFeatureTestPage){ 0 };
    if (ready)
    {
        void* mapping = mmap(0, (uint64_t)size * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mapping != MAP_FAILED;
        if (ready)
        {
            page->mapping = mapping;
            page->page_size = (uint64_t)size;
            ready = mprotect(page->mapping + page->page_size, page->page_size, PROT_READ | PROT_WRITE) == 0;
        }
    }
    return ready;
}

static void cbuiltin_intrinsic_feature_test_page_close(CBuiltinIntrinsicFeatureTestPage* page)
{
    if (page->mapping != 0)
    {
        munmap(page->mapping, page->page_size * 3);
    }
    *page = (CBuiltinIntrinsicFeatureTestPage){ 0 };
}

static void cbuiltin_intrinsic_feature_test_pages(CBuiltinIntrinsicFeatureTest* test)
{
    CBuiltinIntrinsicFeatureTestPage pages[5] = { 0 };
    bool ready = true;
    for (uint32_t i = 0; i < 5; i += 1)
    {
        const bool opened = cbuiltin_intrinsic_feature_test_page_open(pages + i);
        ready = ready && opened;
    }
    cbuiltin_intrinsic_feature_test_expect(test, ready, "guard page allocation");
    if (ready)
    {
        uint8_t* expression_end = pages[0].mapping + pages[0].page_size * 2;
        uint8_t* name_end = pages[1].mapping + pages[1].page_size * 2;
        CBuiltinIntrinsicFeature* features = (CBuiltinIntrinsicFeature*)(pages[2].mapping + pages[2].page_size * 2 - sizeof(CBuiltinIntrinsicFeature));
        CBuiltinIntrinsicFeatureFrame* frame_end = (CBuiltinIntrinsicFeatureFrame*)(pages[3].mapping + pages[3].page_size * 2);
        CBuiltinIntrinsicFeatureStatus* status_end = (CBuiltinIntrinsicFeatureStatus*)(pages[4].mapping + pages[4].page_size * 2);
        name_end[-1] = 'a';
        *features = (CBuiltinIntrinsicFeature){
            .name = (const char*)name_end - 1,
            .name_length = 1,
            .available = true,
        };
        uint8_t saved[sizeof(*features)];
        memcpy(saved, features, sizeof(saved));
        expression_end[-1] = 'a';
        cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate((const char*)expression_end - 1, 1, features, 1, frame_end, 0) ==
                                             CBUILTIN_INTRINSIC_FEATURE_AVAILABLE, "guarded atom/name/table and unused scratch");
        cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate((const char*)expression_end, 0, features, 1, frame_end, 0) ==
                                             CBUILTIN_INTRINSIC_FEATURE_AVAILABLE, "guarded empty expression");
        const struct
        {
            const char* expression;
            CBuiltinIntrinsicFeatureStatus status;
        } cases[] = {
            { "a|z", CBUILTIN_INTRINSIC_FEATURE_UNSUPPORTED },
            { "a|", CBUILTIN_INTRINSIC_FEATURE_INVALID },
            { "a)", CBUILTIN_INTRINSIC_FEATURE_INVALID },
            { "a|()", CBUILTIN_INTRINSIC_FEATURE_INVALID },
            { "a|z,", CBUILTIN_INTRINSIC_FEATURE_INVALID },
            { "a,(", CBUILTIN_INTRINSIC_FEATURE_INVALID },
        };
        for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
        {
            const uint32_t length = (uint32_t)strlen(cases[i].expression);
            char* expression = (char*)expression_end - length;
            memcpy(expression, cases[i].expression, length);
            cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate(expression, length, features, 1, frame_end - 4, 4) ==
                                                 cases[i].status, "guarded unknown/invalid expression tail");
        }
        cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate((const char*)expression_end, 0,
                                             (const CBuiltinIntrinsicFeature*)name_end, 0, frame_end, 0) == CBUILTIN_INTRINSIC_FEATURE_AVAILABLE,
                                             "guarded zero feature count");
        for (uint32_t depth = 1; depth <= BUSTER_INTRINSIC_FEATURE_TEST_DEPTH; depth += 1)
        {
            const uint32_t length = depth * 2 + 1;
            char* expression = (char*)expression_end - length;
            memset(expression, '(', depth);
            expression[depth] = 'a';
            memset(expression + depth + 1, ')', depth);
            cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate(expression, length, features, 1, frame_end - depth, depth) ==
                                                 CBUILTIN_INTRINSIC_FEATURE_AVAILABLE, "exact nested scratch capacity");
            cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate(expression, length, features, 1, frame_end - (depth - 1), depth - 1) ==
                                                 CBUILTIN_INTRINSIC_FEATURE_CAPACITY, "one-short nested scratch capacity");
            const CBuiltinIntrinsicFeatureExpression batch = {
                .characters = expression,
                .length = length,
            };
            cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate_batch(&batch, 1, features, 1, frame_end - depth, depth, status_end - 1) ==
                                                 CBUILTIN_INTRINSIC_FEATURE_AVAILABLE && status_end[-1] == CBUILTIN_INTRINSIC_FEATURE_AVAILABLE,
                                                 "guarded status publication");
            cbuiltin_intrinsic_feature_test_expect(test, expression[depth] == 'a' && expression[0] == '(' && expression[length - 1] == ')',
                                                 "expression preserved");
        }
        cbuiltin_intrinsic_feature_test_expect(test, memcmp(features, saved, sizeof(saved)) == 0 && name_end[-1] == 'a', "feature inputs preserved");
        const CBuiltinIntrinsicFeatureExpression* inaccessible_inputs = (const CBuiltinIntrinsicFeatureExpression*)expression_end;
        cbuiltin_intrinsic_feature_test_expect(test, c_builtin_intrinsic_features_evaluate_batch(inaccessible_inputs, 0, features, 1, frame_end, 0, status_end) ==
                                             CBUILTIN_INTRINSIC_FEATURE_AVAILABLE, "guarded zero-count batch");
    }
    for (uint32_t i = 0; i < 5; i += 1)
    {
        cbuiltin_intrinsic_feature_test_page_close(pages + i);
    }
}

static int run_builtin_intrinsic_features_tests(int argc, char** argv)
{
    (void)argv;
    CBuiltinIntrinsicFeatureTest test = { 0 };
    const bool options = argc == 0;
    cbuiltin_intrinsic_feature_test_expect(&test, options, "usage: test-builtin-intrinsic-features");
    if (options)
    {
        cbuiltin_intrinsic_feature_test_truth(&test);
        cbuiltin_intrinsic_feature_test_constraints(&test);
        cbuiltin_intrinsic_feature_test_batch(&test);
        cbuiltin_intrinsic_feature_test_pages(&test);
    }
    printf("test-builtin-intrinsic-features: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    return result;
}
