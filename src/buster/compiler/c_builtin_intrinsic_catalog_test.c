// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original lookup and bounded-slice fixtures; include after c_builtin_intrinsic_catalog.c. The installed Clang
// metadata is supplied by the separately reviewed generator, never reconstructed by this test. Inventory order,
// exact row identity, opaque custom-checking records, mixed search directions, legal batch partitions and guarded
// name/input/output tails exercise the lookup contract. Metadata presence establishes no executable capability.

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH 17u

typedef struct BuiltinIntrinsicCatalogTest BuiltinIntrinsicCatalogTest;
struct BuiltinIntrinsicCatalogTest
{
    uint64_t checks;
    uint64_t failures;
};

static void builtin_intrinsic_catalog_test_expect(BuiltinIntrinsicCatalogTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-builtin-intrinsic-catalog: %s\n", description);
        }
    }
}

static void builtin_intrinsic_catalog_test_inventory(BuiltinIntrinsicCatalogTest* test)
{
    builtin_intrinsic_catalog_test_expect(test, BUSTER_BUILTIN_INTRINSIC_COUNT == 3775, "exact installed Clang 23.1.1 generic/x86/x86-64 inventory count");
    builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_at(BUSTER_BUILTIN_INTRINSIC_COUNT) == NULL &&
        builtin_intrinsic_at(UINT32_MAX) == NULL, "out-of-range inventory indices miss");
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(i);
        const bool fields = info != NULL && info->name != NULL && info->encoding != NULL && info->attributes != NULL &&
            info->features != NULL && info->header_token != NULL && info->language_token != NULL && info->shard != NULL && info->record_kind != NULL;
        builtin_intrinsic_catalog_test_expect(test, fields, "inventory rows expose every immutable metadata field");
        if (fields)
        {
            builtin_intrinsic_catalog_test_expect(test, info->name_length != 0 && strlen(info->name) == info->name_length &&
                strlen(info->encoding) == info->encoding_length && strlen(info->attributes) == info->attributes_length &&
                strlen(info->features) == info->features_length, "stored lengths equal complete metadata byte strings");
            builtin_intrinsic_catalog_test_expect(test, strcmp(info->record_kind, "table-record") == 0 && info->source_line != 0 &&
                info->custom_typechecking == (strchr(info->attributes, 't') != NULL) &&
                info->constant_evaluable_attribute == (strchr(info->attributes, 'E') != NULL),
                "table provenance and attribute facts remain metadata rather than inferred prototypes");
            builtin_intrinsic_catalog_test_expect(test, i == 0 || strcmp(builtin_intrinsic_at(i - 1)->name, info->name) < 0,
                "inventory has strictly increasing byte names without duplicates");
            builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_lookup(info->name, info->name_length) == info,
                "scalar search returns the exact inventory row for every name");
        }
    }
    const char bswap_name[] = "__builtin_bswap32";
    const BuiltinIntrinsicInfo* bswap = builtin_intrinsic_lookup(bswap_name, sizeof(bswap_name) - 1);
    builtin_intrinsic_catalog_test_expect(test, bswap != NULL && strcmp(bswap->encoding, "UZiUZi") == 0 &&
        strcmp(bswap->attributes, "ncE") == 0 && bswap->features_length == 0 && strcmp(bswap->header_token, "NO_HEADER") == 0 &&
        strcmp(bswap->language_token, "ALL_LANGUAGES") == 0 && strcmp(bswap->shard, "generic") == 0 &&
        !bswap->custom_typechecking && bswap->constant_evaluable_attribute, "known byte-swap descriptor retains exact installed encodings and tokens");
    const char shuffle_name[] = "__builtin_shufflevector";
    const BuiltinIntrinsicInfo* shuffle = builtin_intrinsic_lookup(shuffle_name, sizeof(shuffle_name) - 1);
    builtin_intrinsic_catalog_test_expect(test, shuffle != NULL && strcmp(shuffle->encoding, "v.") == 0 &&
        strcmp(shuffle->attributes, "nctE") == 0 && shuffle->custom_typechecking && shuffle->constant_evaluable_attribute,
        "shuffle custom checking and opaque descriptor are preserved without inventing an ordinary prototype");
}

static void builtin_intrinsic_catalog_test_names(BuiltinIntrinsicCatalogTest* test)
{
    const char known[] = "__builtin_bswap32";
    const uint32_t known_length = sizeof(known) - 1;
    const BuiltinIntrinsicInfo* expected = builtin_intrinsic_lookup(known, known_length);
    const char suffix[] = "__builtin_bswap32tail";
    const char prefix[] = "x__builtin_bswap32";
    const char missing[] = "__buster_unregistered_intrinsic_148";
    const char shorter[] = "__builtin_bswap3";
    const char longer[] = "__builtin_bswap320";
    const char changed_case[] = "__builtin_Bswap32";
    const unsigned char high_first[] = { 0x80, '_', '_', 'b' };
    const unsigned char high_last[] = { '_', '_', 'b', 0xff };
    char embedded[sizeof(known) - 1];
    memcpy(embedded, known, sizeof(embedded));
    embedded[sizeof(embedded) / 2] = 0;
    const BuiltinIntrinsicName names[] =
    {
        { known, known_length },
        { suffix, known_length },
        { prefix + 1, known_length },
        { suffix, sizeof(suffix) - 1 },
        { prefix, sizeof(prefix) - 1 },
        { known, sizeof(known) },
        { embedded, sizeof(embedded) },
        { missing, sizeof(missing) - 1 },
        { shorter, sizeof(shorter) - 1 },
        { longer, sizeof(longer) - 1 },
        { changed_case, sizeof(changed_case) - 1 },
        { (const char*)high_first, sizeof(high_first) },
        { (const char*)high_last, sizeof(high_last) },
        { NULL, 0 },
        { NULL, UINT32_MAX },
        { known, 0 },
        { "", 0 },
    };
    const BuiltinIntrinsicInfo* wanted[BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH] = {0};
    wanted[0] = expected;
    wanted[1] = expected;
    wanted[2] = expected;
    builtin_intrinsic_catalog_test_expect(test, expected != NULL, "bounded-name fixture resolves its exact positive control");
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        for (uint32_t count = 0; count <= BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH; count += 1)
        {
            for (uint32_t split = 0; split <= count; split += 1)
            {
                const BuiltinIntrinsicInfo* outputs[BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH];
                memset(outputs, 0xa5, sizeof(outputs));
                builtin_intrinsic_lookup_batch(names, outputs, split, scalar != 0);
                builtin_intrinsic_lookup_batch(names + split, outputs + split, count - split, scalar != 0);
                bool equal = true;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    equal = equal && outputs[i] == wanted[i] && builtin_intrinsic_lookup(names[i].name, names[i].length) == wanted[i];
                }
                builtin_intrinsic_catalog_test_expect(test, equal, "exact slices, prefix/suffix bytes, embedded NUL, unsigned bytes and misses survive every batch partition");
                bool untouched = true;
                const unsigned char* bytes = (const unsigned char*)(outputs + count);
                for (size_t i = 0; i < (BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH - count) * sizeof(*outputs); i += 1)
                {
                    untouched = untouched && bytes[i] == 0xa5;
                }
                builtin_intrinsic_catalog_test_expect(test, untouched, "batch writes no inactive output elements");
            }
        }
        builtin_intrinsic_lookup_batch(NULL, NULL, 0, scalar != 0);
    }
    builtin_intrinsic_catalog_test_expect(test, true, "both empty batch paths permit null slices");
}

static void builtin_intrinsic_catalog_test_batches(BuiltinIntrinsicCatalogTest* test)
{
    const uint32_t count = BUSTER_BUILTIN_INTRINSIC_COUNT;
    BuiltinIntrinsicName* inputs = calloc(count, sizeof(*inputs));
    const BuiltinIntrinsicInfo** scalar = calloc(count, sizeof(*scalar));
    const BuiltinIntrinsicInfo** vector = calloc(count, sizeof(*vector));
    const bool ready = inputs != NULL && scalar != NULL && vector != NULL;
    builtin_intrinsic_catalog_test_expect(test, ready, "full inventory batch fixture allocation");
    if (ready)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            // A nonmonotone permutation sends adjacent lanes toward different search intervals.
            const uint32_t index = (uint32_t)(((uint64_t)i * 37) % count);
            const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(index);
            inputs[i] = (BuiltinIntrinsicName)
            {
                .name = info->name,
                .length = info->name_length,
            };
        }
        builtin_intrinsic_lookup_batch(inputs, scalar, count, true);
        builtin_intrinsic_lookup_batch(inputs, vector, count, false);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t index = (uint32_t)(((uint64_t)i * 37) % count);
            builtin_intrinsic_catalog_test_expect(test, scalar[i] == builtin_intrinsic_at(index) && vector[i] == scalar[i],
                "whole-table scalar and SIMD lookup return every original row in requested order");
        }
        const uint32_t partitions[] =
        {
            1,
            7,
            8,
            9,
            17,
            33,
        };
        for (uint32_t p = 0; p < sizeof(partitions) / sizeof(partitions[0]); p += 1)
        {
            memset(vector, 0xa5, count * sizeof(*vector));
            for (uint32_t base = 0; base < count;)
            {
                const uint32_t lanes = count - base < partitions[p] ? count - base : partitions[p];
                builtin_intrinsic_lookup_batch(inputs + base, vector + base, lanes, false);
                base += lanes;
            }
            bool equal = true;
            for (uint32_t i = 0; i < count; i += 1)
            {
                equal = equal && vector[i] == scalar[i];
            }
            builtin_intrinsic_catalog_test_expect(test, equal, "whole-table lookup is invariant under tiny, vector-boundary and partial batch partitions");
        }
    }
    free(vector);
    free(scalar);
    free(inputs);
}

static void builtin_intrinsic_catalog_test_pages(BuiltinIntrinsicCatalogTest* test)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    const size_t page = page_size > 0 ? (size_t)page_size : 0;
    unsigned char* mapping = page != 0 ? mmap(NULL, 6 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED && BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH * sizeof(BuiltinIntrinsicName) + 32 <= page &&
        BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH * sizeof(const BuiltinIntrinsicInfo*) + 32 <= page;
    if (ready)
    {
        ready = mprotect(mapping + page, page, PROT_NONE) == 0 && mprotect(mapping + 3 * page, page, PROT_NONE) == 0 &&
            mprotect(mapping + 5 * page, page, PROT_NONE) == 0;
    }
    builtin_intrinsic_catalog_test_expect(test, ready, "guarded name/input/output slice allocation");
    if (ready)
    {
        unsigned char* name_end = mapping + page;
        unsigned char* input_end = mapping + 3 * page;
        unsigned char* output_end = mapping + 5 * page;
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_lookup((const char*)name_end, 0) == NULL,
            "empty name at an inaccessible page is never dereferenced");
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            builtin_intrinsic_lookup_batch((const BuiltinIntrinsicName*)input_end, (const BuiltinIntrinsicInfo**)output_end, 0, scalar != 0);
        }
        for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
        {
            const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(i);
            const bool bounded = info->name_length != 0 && info->name_length <= page;
            builtin_intrinsic_catalog_test_expect(test, bounded, "inventory name fits the guarded logical slice");
            if (bounded)
            {
                char* name = (char*)name_end - info->name_length;
                memcpy(name, info->name, info->name_length);
                const BuiltinIntrinsicName input =
                {
                    .name = name,
                    .length = info->name_length,
                };
                const BuiltinIntrinsicInfo* output = NULL;
                builtin_intrinsic_lookup_batch(&input, &output, 1, false);
                builtin_intrinsic_catalog_test_expect(test, output == info && builtin_intrinsic_lookup(name, info->name_length) == info,
                    "every unterminated table name ends exactly at a guard page for scalar and SIMD search");
            }
        }
        const char known[] = "__builtin_bswap32";
        const uint32_t length = sizeof(known) - 1;
        const BuiltinIntrinsicInfo* expected = builtin_intrinsic_lookup(known, length);
        char* payload = (char*)name_end - length;
        memcpy(payload, known, length);
        const char missing[] = "__buster_unregistered_intrinsic_148";
        for (uint32_t count = 1; count <= BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH; count += 1)
        {
            BuiltinIntrinsicName* inputs = (BuiltinIntrinsicName*)(input_end - count * sizeof(*inputs));
            const BuiltinIntrinsicInfo** outputs = (const BuiltinIntrinsicInfo**)(output_end - count * sizeof(*outputs));
            const BuiltinIntrinsicInfo* wanted[BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH] = {0};
            memset(mapping + 2 * page, 0xa5, page);
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t mode = i % 8;
                const BuiltinIntrinsicInfo* edge = mode == 0 ? builtin_intrinsic_at(0) : builtin_intrinsic_at(BUSTER_BUILTIN_INTRINSIC_COUNT - 1);
                inputs[i] = (BuiltinIntrinsicName)
                {
                    .name = mode < 2 ? edge->name : mode == 2 ? payload : mode == 3 ? NULL : mode == 4 ? (const char*)name_end : missing,
                    .length = mode < 2 ? edge->name_length : mode == 2 ? length : mode == 3 ? UINT32_MAX : mode == 4 ? 0 : sizeof(missing) - 1,
                };
                wanted[i] = mode < 2 ? edge : mode == 2 ? expected : NULL;
            }
            BuiltinIntrinsicName before[BUSTER_BUILTIN_INTRINSIC_CATALOG_TEST_BATCH];
            memcpy(before, inputs, count * sizeof(*inputs));
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                for (uint32_t split = 0; split <= count; split += 1)
                {
                    memset(mapping + 4 * page, 0xa5, page);
                    builtin_intrinsic_lookup_batch(inputs, outputs, split, scalar != 0);
                    builtin_intrinsic_lookup_batch(inputs + split, outputs + split, count - split, scalar != 0);
                    bool equal = true;
                    for (uint32_t i = 0; i < count; i += 1)
                    {
                        equal = equal && outputs[i] == wanted[i];
                    }
                    builtin_intrinsic_catalog_test_expect(test, equal, "guarded mixed-lane searches and every two-part partition match independent expected row identities");
                    bool canaries = true;
                    for (uint32_t i = 1; i <= 32; i += 1)
                    {
                        canaries = canaries && ((const unsigned char*)inputs)[-(ptrdiff_t)i] == 0xa5 &&
                            ((const unsigned char*)outputs)[-(ptrdiff_t)i] == 0xa5;
                    }
                    builtin_intrinsic_catalog_test_expect(test, canaries && memcmp(inputs, before, count * sizeof(*inputs)) == 0,
                        "guarded batches preserve prefix canaries and immutable input records");
                }
            }
        }
        payload = (char*)name_end - length - 1;
        memcpy(payload, known, length + 1);
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_lookup(payload, length + 1) == NULL,
            "including a terminating NUL in a guarded logical name misses");
        payload = (char*)name_end - length;
        memcpy(payload, known, length);
        payload[length / 2] = 0;
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_lookup(payload, length) == NULL,
            "an embedded NUL in a guarded logical name cannot alias its original spelling");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 6 * page);
    }
}

static int test_builtin_intrinsic_catalog(int argc, char** argv)
{
    (void)argv;
    BuiltinIntrinsicCatalogTest test = {0};
    const bool options = argc == 0;
    builtin_intrinsic_catalog_test_expect(&test, options, "usage: test-builtin-intrinsic-catalog");
    if (options)
    {
        builtin_intrinsic_catalog_test_inventory(&test);
        builtin_intrinsic_catalog_test_names(&test);
        builtin_intrinsic_catalog_test_batches(&test);
        builtin_intrinsic_catalog_test_pages(&test);
    }
    printf("test-builtin-intrinsic-catalog: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    return result;
}
