// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original negative-filter fixtures, included after the catalog test's expectation helper. The unfiltered
// scalar lookup and direct inventory identities remain independent acceptance oracles. Reconstruct the
// complete generated bitmap, exercise every real name and deliberate collisions, and protect name tails.
static void builtin_intrinsic_catalog_test_filter(BuiltinIntrinsicCatalogTest* test)
{
    uint64_t expected[BUSTER_BUILTIN_INTRINSIC_FILTER_WORDS] = { 0 };
    uint32_t collisions = 0;
    for (uint32_t base = 0; base < BUSTER_BUILTIN_INTRINSIC_COUNT; base += 8)
    {
        const uint32_t count = BUSTER_BUILTIN_INTRINSIC_COUNT - base < 8 ? BUSTER_BUILTIN_INTRINSIC_COUNT - base : 8;
        BuiltinIntrinsicName names[8];
        const BuiltinIntrinsicInfo* outputs[8];
        for (uint32_t i = 0; i < count; i += 1)
        {
            const BuiltinIntrinsicInfo* info = builtin_intrinsic_infos + base + i;
            names[i] = (BuiltinIntrinsicName){ info->name, info->name_length };
            const uint32_t hash = builtin_intrinsic_filter_hash(builtin_intrinsic_filter_key(info->name, info->name_length));
            expected[hash / 64] |= UINT64_C(1) << (hash % 64);
        }
        const uint32_t mask = (1u << count) - 1u;
        builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_filter_candidates(names, count, true) == mask &&
            builtin_intrinsic_filter_candidates(names, count, false) == mask, "every catalog name survives both filter twins, including plain libc names");
        builtin_intrinsic_lookup_batch(names, outputs, count, false);
        for (uint32_t i = 0; i < count; i += 1)
        {
            builtin_intrinsic_catalog_test_expect(test, outputs[i] == builtin_intrinsic_infos + base + i,
                "filtered lookup returns the original exact row for every catalog hit");
        }
    }
    builtin_intrinsic_catalog_test_expect(test, memcmp(expected, builtin_intrinsic_filter_bits, sizeof(expected)) == 0,
        "checked-in filter is the exact deterministic projection of the current catalog");
    builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_filter_candidates(NULL, 0, true) == 0 &&
        builtin_intrinsic_filter_candidates(NULL, 0, false) == 0, "zero-sized filter input permits null");
    builtin_intrinsic_lookup_batch(NULL, NULL, 0, false);
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_infos + i;
        char changed[128];
        if (info->name_length > 8 && info->name_length <= sizeof(changed))
        {
            memcpy(changed, info->name, info->name_length);
            changed[4] = (char)((uint8_t)changed[4] ^ 0x80u);
            const BuiltinIntrinsicName name = { changed, info->name_length };
            const BuiltinIntrinsicInfo* result = info;
            const BuiltinIntrinsicInfo* oracle = builtin_intrinsic_lookup(name.name, name.length);
            builtin_intrinsic_lookup_batch(&name, &result, 1, false);
            builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_filter_candidates(&name, 1, false) == 1 && result == oracle,
                "middle-byte fingerprint collisions still undergo exact bounded-name lookup");
            collisions += oracle == NULL;
        }
    }
    builtin_intrinsic_catalog_test_expect(test, collisions != 0, "deliberate false-positive inputs reach exact rejection");

    uint32_t rejections = 0;
    for (uint32_t base = 0; base < 4096; base += 8)
    {
        char spellings[8][48];
        BuiltinIntrinsicName names[8];
        const BuiltinIntrinsicInfo* outputs[8];
        for (uint32_t i = 0; i < 8; i += 1)
        {
            const int length = snprintf(spellings[i], sizeof(spellings[i]), "buster_filter_miss_%08x", base + i);
            names[i] = (BuiltinIntrinsicName){ spellings[i], (uint32_t)length };
        }
        for (uint32_t count = 1; count <= 8; count += 1)
        {
            const uint32_t scalar = builtin_intrinsic_filter_candidates(names, count, true);
            const uint32_t vector = builtin_intrinsic_filter_candidates(names, count, false);
            builtin_intrinsic_catalog_test_expect(test, scalar == vector && (vector >> count) == 0,
                "filter masks agree for every partial batch with no inactive bits");
            builtin_intrinsic_lookup_batch(names, outputs, count, false);
            for (uint32_t i = 0; i < count; i += 1)
            {
                builtin_intrinsic_catalog_test_expect(test, outputs[i] == builtin_intrinsic_lookup(names[i].name, names[i].length),
                    "ordinary misses retain exact scalar/vector lookup identity");
                rejections += (vector & (1u << i)) == 0;
            }
        }
    }
    builtin_intrinsic_catalog_test_expect(test, rejections != 0, "ordinary names exercise negative rejection rather than an all-positive filter");

    const long page_size = sysconf(_SC_PAGESIZE);
    builtin_intrinsic_catalog_test_expect(test, page_size > 0, "query page size for guarded filter names");
    if (page_size > 0)
    {
        const size_t page = (size_t)page_size;
        uint8_t* memory = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const bool mapped = memory != MAP_FAILED;
        const bool guarded = mapped && mprotect(memory + page, page, PROT_NONE) == 0;
        builtin_intrinsic_catalog_test_expect(test, guarded, "map protected logical tails for filter names");
        if (guarded)
        {
            for (uint32_t length = 0; length <= 32; length += 1)
            {
                char* name = (char*)memory + page - length;
                if (length != 0)
                {
                    memset(name, 'x', length);
                }
                const BuiltinIntrinsicName inputs[8] = {
                    { name, length },
                    { NULL, UINT32_MAX },
                    { (const char*)memory + page, 0 },
                    { "memcpy", sizeof("memcpy") - 1 },
                    { "__builtin_bswap32", sizeof("__builtin_bswap32") - 1 },
                };
                for (uint32_t count = 1; count <= 8; count += 1)
                {
                    const BuiltinIntrinsicInfo* outputs[8];
                    builtin_intrinsic_catalog_test_expect(test, builtin_intrinsic_filter_candidates(inputs, count, true) ==
                        builtin_intrinsic_filter_candidates(inputs, count, false), "bounded short names, empty protected slices and null names agree");
                    builtin_intrinsic_lookup_batch(inputs, outputs, count, false);
                    for (uint32_t i = 0; i < count; i += 1)
                    {
                        builtin_intrinsic_catalog_test_expect(test, outputs[i] == builtin_intrinsic_lookup(inputs[i].name, inputs[i].length),
                            "guarded mixed hit/miss batch equals the unfiltered oracle");
                    }
                }
            }
        }
        if (mapped)
        {
            munmap(memory, page * 2);
        }
    }
}
