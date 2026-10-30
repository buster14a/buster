// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original lookup and bounded-slice fixtures; include after c_builtin_intrinsic_catalog.c. The installed Clang
// metadata is supplied by the separately reviewed generator, never reconstructed by this test. Inventory order,
// exact row identity, opaque custom-checking records, mixed search directions, legal batch partitions and guarded
// name/input/output tails exercise the lookup contract. Metadata presence establishes no executable capability.
// The separate frozen own-main inventory attests exact workload membership and preserves parser/type gaps;
// its x86 subset is joined by recorded shard, never inferred from a builtin name prefix or full-table presence.

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

static bool builtin_intrinsic_catalog_test_record_equal(const BuiltinIntrinsicInfo* a, const BuiltinIntrinsicInfo* b)
{
    const bool result = a != NULL && b != NULL && strcmp(a->name, b->name) == 0 && strcmp(a->encoding, b->encoding) == 0 &&
        strcmp(a->attributes, b->attributes) == 0 && strcmp(a->features, b->features) == 0 && strcmp(a->header_token, b->header_token) == 0 &&
        strcmp(a->language_token, b->language_token) == 0 && strcmp(a->shard, b->shard) == 0 && strcmp(a->record_kind, b->record_kind) == 0 &&
        a->name_length == b->name_length && a->encoding_length == b->encoding_length && a->attributes_length == b->attributes_length &&
        a->features_length == b->features_length && a->source_line == b->source_line && a->source_ordinal == b->source_ordinal &&
        a->custom_typechecking == b->custom_typechecking && a->constant_evaluable_attribute == b->constant_evaluable_attribute;
    return result;
}

static void builtin_intrinsic_catalog_test_own_main(BuiltinIntrinsicCatalogTest* test)
{
    builtin_intrinsic_catalog_test_expect(test, BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT == 1543, "frozen own-main scope contains exactly 1,543 requested names");
    uint32_t x86 = 0;
    uint32_t generic = 0;
    uint32_t gaps = 0;
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_own_main_infos + i;
        builtin_intrinsic_catalog_test_expect(test, i == 0 || strcmp(builtin_intrinsic_own_main_infos[i - 1].name, info->name) < 0,
            "frozen scope rows have strictly increasing names without duplicates");
        builtin_intrinsic_catalog_test_expect(test, info->name_length != 0 && strlen(info->name) == info->name_length &&
            strlen(info->encoding) == info->encoding_length && strlen(info->attributes) == info->attributes_length &&
            strlen(info->features) == info->features_length && builtin_intrinsic_own_main_lookup(info->name, info->name_length) == info,
            "each exact bounded workload name resolves to its immutable scope row with complete field lengths");
        const bool target = strcmp(info->shard, "x86") == 0 || strcmp(info->shard, "x86_64") == 0;
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_own_main_x86_in_scope(info->name, info->name_length) == target,
            "scope classification follows exact membership and the recorded target shard");
        x86 += target;
        const BuiltinIntrinsicInfo* full = builtin_intrinsic_lookup(info->name, info->name_length);
        if (strcmp(info->record_kind, "table-record") == 0)
        {
            generic += strcmp(info->shard, "generic") == 0;
            builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_catalog_test_record_equal(info, full),
                "every ordinary scope row joins the full catalogue without altering any metadata or provenance field");
        }
        else
        {
            gaps += 1;
            const bool bit_cast = strcmp(info->name, "__builtin_bit_cast") == 0;
            const bool va_list = strcmp(info->name, "__builtin_va_list") == 0;
            builtin_intrinsic_catalog_test_expect(test, (bit_cast || va_list) &&
                strcmp(info->record_kind, bit_cast ? "parser-special-form-gap" : "predefined-type-gap") == 0 &&
                strcmp(info->shard, "special") == 0 && strcmp(info->header_token, "NO_HEADER") == 0 && strcmp(info->language_token, "UNMODELED") == 0 &&
                info->encoding_length == 0 && info->attributes_length == 0 && info->features_length == 0 && info->source_line == 0 &&
                info->source_ordinal == 0 && !info->custom_typechecking && !info->constant_evaluable_attribute && full == NULL && !target,
                "only bit-cast syntax and va-list type gaps remain explicit non-prototypes outside the x86 scope");
        }
    }
    builtin_intrinsic_catalog_test_expect(test, x86 == 1492 && generic == 49 && gaps == 2,
        "own-main attestation is exactly 1,492 target records, 49 generic records and two explicit gaps");

    // A merge join supplies independent membership expectations for every full-catalog row, including rows that
    // share the ia32 spelling prefix but never appeared in the frozen workload's requested-name inventory.
    uint32_t scope = 0;
    uint32_t joined_targets = 0;
    uint32_t outside_targets = 0;
    uint32_t full_ia32 = 0;
    const char ia32_prefix[] = "__builtin_ia32_";
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* full = builtin_intrinsic_at(i);
        while (scope < BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT && strcmp(builtin_intrinsic_own_main_infos[scope].name, full->name) < 0)
        {
            scope += 1;
        }
        const BuiltinIntrinsicInfo* expected = scope < BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT &&
            strcmp(builtin_intrinsic_own_main_infos[scope].name, full->name) == 0 ? builtin_intrinsic_own_main_infos + scope : NULL;
        const bool target = strcmp(full->shard, "x86") == 0 || strcmp(full->shard, "x86_64") == 0;
        joined_targets += expected != NULL && target;
        outside_targets += expected == NULL && target;
        full_ia32 += full->name_length >= sizeof(ia32_prefix) - 1 && memcmp(full->name, ia32_prefix, sizeof(ia32_prefix) - 1) == 0;
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_own_main_lookup(full->name, full->name_length) == expected &&
            builtin_intrinsic_own_main_x86_in_scope(full->name, full->name_length) == (expected != NULL && target),
            "full-catalog lookup never admits a target name absent from the exact frozen scope join");
    }
    builtin_intrinsic_catalog_test_expect(test, joined_targets == 1492 && outside_targets != 0 && full_ia32 == 2029,
        "1,492 attested target names remain narrower than the full catalogue's 2,029 ia32 spellings");

    const char positive[] = "__builtin_ia32_enqcmd";
    const char positive64[] = "__builtin_ia32_prefetchi";
    const char generic_name[] = "__builtin_bswap32";
    const char bit_cast_name[] = "__builtin_bit_cast";
    const char va_list_name[] = "__builtin_va_list";
    const char outside[] = "__builtin_ia32_lfence";
    const char suffix[] = "__builtin_ia32_enqcmdtail";
    const char prefix[] = "x__builtin_ia32_enqcmd";
    const BuiltinIntrinsicInfo* positive_info = builtin_intrinsic_own_main_lookup(positive, sizeof(positive) - 1);
    const BuiltinIntrinsicInfo* generic_info = builtin_intrinsic_own_main_lookup(generic_name, sizeof(generic_name) - 1);
    const BuiltinIntrinsicInfo* bit_cast_info = builtin_intrinsic_own_main_lookup(bit_cast_name, sizeof(bit_cast_name) - 1);
    const BuiltinIntrinsicInfo* va_list_info = builtin_intrinsic_own_main_lookup(va_list_name, sizeof(va_list_name) - 1);
    const BuiltinIntrinsicInfo* outside_info = builtin_intrinsic_lookup(outside, sizeof(outside) - 1);
    builtin_intrinsic_catalog_test_expect(test, positive_info != NULL && builtin_intrinsic_own_main_x86_in_scope(positive, sizeof(positive) - 1) &&
        builtin_intrinsic_own_main_x86_in_scope(positive64, sizeof(positive64) - 1), "frozen membership includes both x86 shards without claiming feature eligibility");
    builtin_intrinsic_catalog_test_expect(test, generic_info != NULL && !builtin_intrinsic_own_main_x86_in_scope(generic_name, sizeof(generic_name) - 1) &&
        bit_cast_info != NULL && va_list_info != NULL && !builtin_intrinsic_own_main_x86_in_scope(bit_cast_name, sizeof(bit_cast_name) - 1) &&
        !builtin_intrinsic_own_main_x86_in_scope(va_list_name, sizeof(va_list_name) - 1), "generic membership and both explicit gaps never attest x86 call support");
    // The prototype scope: the x86 records and the ten generic names no checker module owns, never another generic one.
    uint32_t prototypes = 0;
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_own_main_infos + i;
        const bool in_scope = builtin_intrinsic_own_main_prototype_in_scope(info->name, info->name_length);
        const bool target = builtin_intrinsic_own_main_x86_in_scope(info->name, info->name_length);
        prototypes += in_scope && !target;
        builtin_intrinsic_catalog_test_expect(test, !in_scope || target || cbuiltin_resolve(info->name, info->name_length) == CBUILTIN_NONE,
            "a generic prototype-scope name is no builtin a checker module owns");
    }
    const char rotate[] = "__builtin_rotateleft8";
    const char unpredictable[] = "__builtin_unpredictable";
    builtin_intrinsic_catalog_test_expect(test, prototypes == 10 && builtin_intrinsic_own_main_prototype_in_scope(rotate, sizeof(rotate) - 1) &&
        builtin_intrinsic_own_main_prototype_in_scope(unpredictable, sizeof(unpredictable) - 1) && !builtin_intrinsic_own_main_prototype_in_scope(generic_name, sizeof(generic_name) - 1) &&
        !builtin_intrinsic_own_main_prototype_in_scope(bit_cast_name, sizeof(bit_cast_name) - 1) && !builtin_intrinsic_own_main_prototype_in_scope(va_list_name, sizeof(va_list_name) - 1) &&
        !builtin_intrinsic_own_main_prototype_in_scope(rotate, sizeof(rotate) - 2) && builtin_intrinsic_own_main_prototype_in_scope(positive, sizeof(positive) - 1),
        "the prototype scope is the x86 records and exactly the ten generic prototype-only names");
    builtin_intrinsic_catalog_test_expect(test, outside_info != NULL && strcmp(outside_info->shard, "x86") == 0 &&
        builtin_intrinsic_own_main_lookup(outside, sizeof(outside) - 1) == NULL && !builtin_intrinsic_own_main_x86_in_scope(outside, sizeof(outside) - 1),
        "valid full-catalog lfence target is rejected because it is absent from the frozen workload");
    static const char* const immediate_names[] = { "__builtin_ia32_palignr256", "__builtin_ia32_pblendw128", "__builtin_ia32_permdi256",
                                                  "__builtin_ia32_pshufd256", "__builtin_ia32_shufpd" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(immediate_names); i += 1)
    {
        const char* name = immediate_names[i];
        const uint32_t length = strlen(name);
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_lookup(name, length) != NULL &&
            builtin_intrinsic_immediate_in_scope(name, length) && builtin_intrinsic_own_main_prototype_in_scope(name, length) &&
            !builtin_intrinsic_immediate_in_scope(name, length - 1), "extra immediate scope admits only each complete catalogued name");
    }
    builtin_intrinsic_catalog_test_expect(test, !builtin_intrinsic_immediate_in_scope(NULL, 1) &&
        !builtin_intrinsic_immediate_in_scope(outside, sizeof(outside) - 1), "extra immediate scope rejects unrelated and null names");
    builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_own_main_lookup(suffix, sizeof(positive) - 1) == positive_info &&
        builtin_intrinsic_own_main_x86_in_scope(suffix, sizeof(positive) - 1) &&
        builtin_intrinsic_own_main_lookup(prefix + 1, sizeof(positive) - 1) == positive_info,
        "scope lookup compares only the exact supplied name slice");
    char embedded[sizeof(positive) - 1];
    memcpy(embedded, positive, sizeof(embedded));
    embedded[sizeof(embedded) / 2] = 0;
    const BuiltinIntrinsicName false_names[] =
    {
        { suffix, sizeof(suffix) - 1 },
        { prefix, sizeof(prefix) - 1 },
        { positive, sizeof(positive) },
        { positive, sizeof("__builtin_ia32_") - 1 },
        { "__builtin_ms_va_list", sizeof("__builtin_ms_va_list") - 1 },
        { embedded, sizeof(embedded) },
        { NULL, 0 },
        { NULL, UINT32_MAX },
        { positive, 0 },
        { "", 0 },
    };
    for (uint32_t i = 0; i < sizeof(false_names) / sizeof(false_names[0]); i += 1)
    {
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_own_main_lookup(false_names[i].name, false_names[i].length) == NULL &&
            !builtin_intrinsic_own_main_x86_in_scope(false_names[i].name, false_names[i].length),
            "scope rejects suffix/prefix, embedded or included NUL, null and empty names at their exact logical lengths");
    }
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
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_own_main_lookup((const char*)name_end, 0) == NULL &&
            !builtin_intrinsic_own_main_x86_in_scope((const char*)name_end, 0), "empty scope name at an inaccessible page is never dereferenced or admitted");
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
        for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT; i += 1)
        {
            const BuiltinIntrinsicInfo* info = builtin_intrinsic_own_main_infos + i;
            const bool bounded = info->name_length != 0 && info->name_length <= page;
            builtin_intrinsic_catalog_test_expect(test, bounded, "frozen scope name fits the guarded logical slice");
            if (bounded)
            {
                char* name = (char*)name_end - info->name_length;
                memcpy(name, info->name, info->name_length);
                const bool target = strcmp(info->shard, "x86") == 0 || strcmp(info->shard, "x86_64") == 0;
                builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_own_main_lookup(name, info->name_length) == info &&
                    builtin_intrinsic_own_main_x86_in_scope(name, info->name_length) == target,
                    "every unterminated scope name, including explicit gaps, ends exactly at a guard page");
            }
        }
        const char scope_positive[] = "__builtin_ia32_enqcmd";
        const char scope_outside[] = "__builtin_ia32_lfence";
        const char scope_prefix[] = "__builtin_ia32_";
        char* scope_payload = (char*)name_end - sizeof(scope_positive);
        memcpy(scope_payload, scope_positive, sizeof(scope_positive));
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_own_main_lookup(scope_payload, sizeof(scope_positive)) == NULL &&
            !builtin_intrinsic_own_main_x86_in_scope(scope_payload, sizeof(scope_positive)), "scope rejects an included NUL at the guarded logical boundary");
        scope_payload = (char*)name_end - sizeof(scope_positive) + 1;
        memcpy(scope_payload, scope_positive, sizeof(scope_positive) - 1);
        scope_payload[(sizeof(scope_positive) - 1) / 2] = 0;
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_own_main_lookup(scope_payload, sizeof(scope_positive) - 1) == NULL &&
            !builtin_intrinsic_own_main_x86_in_scope(scope_payload, sizeof(scope_positive) - 1), "scope rejects an embedded NUL within an exact guarded name slice");
        scope_payload = (char*)name_end - sizeof(scope_outside) + 1;
        memcpy(scope_payload, scope_outside, sizeof(scope_outside) - 1);
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_lookup(scope_payload, sizeof(scope_outside) - 1) != NULL &&
            builtin_intrinsic_own_main_lookup(scope_payload, sizeof(scope_outside) - 1) == NULL &&
            !builtin_intrinsic_own_main_x86_in_scope(scope_payload, sizeof(scope_outside) - 1), "guarded full-catalog target remains outside the exact frozen scope");
        scope_payload = (char*)name_end - sizeof(scope_prefix) + 1;
        memcpy(scope_payload, scope_prefix, sizeof(scope_prefix) - 1);
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_own_main_lookup(scope_payload, sizeof(scope_prefix) - 1) == NULL &&
            !builtin_intrinsic_own_main_x86_in_scope(scope_payload, sizeof(scope_prefix) - 1), "guarded ia32 prefix alone never establishes frozen membership");
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
        builtin_intrinsic_catalog_test_own_main(&test);
        builtin_intrinsic_catalog_test_names(&test);
        builtin_intrinsic_catalog_test_batches(&test);
        builtin_intrinsic_catalog_test_pages(&test);
    }
    printf("test-builtin-intrinsic-catalog: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    return result;
}
