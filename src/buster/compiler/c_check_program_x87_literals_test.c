// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Exact literal-pool checks, owned by test-checker's program tests. Raw integer fields avoid host NaN operations.

static void check_test_program_x87_literals(CheckTest* test)
{
    static const uint64_t patterns[][2] = {
        { 0, 0 },
        { 0, 0x8000 },
        { UINT64_C(0x8000000000000000), 0x3fff },
        { UINT64_C(0x8000000000000000), 0x3ffe },
        { UINT64_C(0x8000000000000000), 0x7fff },
        { UINT64_C(0x8000000000000000), 0xffff },
        { UINT64_C(0xc000000000000001), 0x7fff },
        { UINT64_C(0xc000000000000002), 0x7fff },
        { UINT64_C(0x8000000000000001), 0x7fff },
        { 1, 0 },
        { UINT64_MAX, 0x7ffe },
        { UINT64_C(0x8000000000000000), 0 },
    };
    CheckTestTypes types = { 0 };
    Arena output[2] = { arena_reserve(UINT64_C(1) << 20), arena_reserve(UINT64_C(1) << 20) };
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* guarded = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool ready = check_test_types_open(&types) && output[0].base != NULL && output[1].base != NULL && scratch.base != NULL &&
                 guarded != MAP_FAILED && mprotect(guarded + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "x87 literal pool arenas and protected type-column tail");
    CheckValue values[35];
    for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(patterns); p += 1)
    {
        const bool finite = p < 4 || p == 9 || p == 10;
        check_test_expect(test, check_lower_x87_literal_load_safe(patterns[p][0], patterns[p][1]) == finite,
                          "only canonical finite encodings may move a constant's x87 transfer to its definition");
    }
    for (uint32_t count = 0; ready && count <= BUSTER_ARRAY_LENGTH(values); count += 1)
    {
        TypeId* ids = (TypeId*)(guarded + page - (uint64_t)count * sizeof(TypeId));
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t pattern = (i * 7 + 3) % BUSTER_ARRAY_LENGTH(patterns);
            values[i] = (CheckValue){
                .type = BUSTER_TYPE_LONG_DOUBLE,
                .category = CHECK_RVALUE,
                .constant_kind = CHECK_CONSTANT_FLOATING,
                .floating_storage = { patterns[pattern][0], patterns[pattern][1] | UINT64_C(0x123456789abc0000) },
            };
            ids[i] = values[i].type;
        }
        if (count > 1)
        {
            values[1].type = BUSTER_TYPE_COMPLEX_LONG_DOUBLE;
            values[1].constant_kind = CHECK_CONSTANT_COMPLEX;
            values[1].imaginary_storage[0] = UINT64_C(0xc000000000000123);
            values[1].imaginary_storage[1] = UINT64_C(0xfeedbeef0000ffff);
            ids[1] = values[1].type;
        }
        for (uint32_t dense = 0; dense < 2; dense += 1)
        {
            const CheckStream stream = {
                .dense_values = dense ? values : NULL,
                .value_types = dense ? NULL : ids,
                .payloads = dense ? NULL : values,
                .payload_count = dense ? 0 : count,
                .count = count,
            };
            const CheckProgram program = { .types = &types.table, .stream = &stream };
            CheckLowerInitializers pools[2] = { { .symbol_base = 73 }, { .symbol_base = 73 } };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                arena_pop_to(output + path, 0);
                const uint64_t scratch_mark = scratch.length;
                const bool made = check_lower_x87_literal_pools(pools + path, &program, output + path, &scratch, path == 0);
                check_test_expect(test, made && scratch.length == scratch_mark && (count != 0 || pools[path].count == 0), "literal pooling preserves scratch and leaves an empty unit empty");
                for (uint32_t p = 0; made && p < pools[path].x87_count; p += 1)
                {
                    const CheckLowerInitPool* item = pools[path].items + pools[path].x87_first + p;
                    const uint64_t low = pools[path].x87_keys[2 * p];
                    const uint64_t high = pools[path].x87_keys[2 * p + 1];
                    bool zero_padding = true;
                    for (uint32_t b = 10; b < 16; b += 1)
                    {
                        zero_padding &= item->bytes[b] == 0;
                    }
                    const bool sorted = p == 0 || pools[path].x87_keys[2 * (p - 1) + 1] < high ||
                                        (pools[path].x87_keys[2 * (p - 1) + 1] == high && pools[path].x87_keys[2 * (p - 1)] < low);
                    check_test_expect(test, item->size == 16 && item->alignment_log2 == 4 && (uintptr_t)item->bytes % 16 == 0 && zero_padding && high <= UINT16_MAX && sorted &&
                                           check_lower_x87_literal_symbol(pools + path, low, high) == 73 + p, "F80 literal keys are distinct, ordered, 16-aligned and zero padded");
                }
                for (uint32_t i = 0; made && i < count; i += 1)
                {
                    const SymbolId real = check_lower_x87_literal_symbol(pools + path, values[i].floating_storage[0], values[i].floating_storage[1]);
                    const SymbolId imaginary = values[i].constant_kind == CHECK_CONSTANT_COMPLEX ? check_lower_x87_literal_symbol(pools + path, values[i].imaginary_storage[0], values[i].imaginary_storage[1]) : real;
                    check_test_expect(test, real >= 73 && imaginary >= 73, "each original ten-byte value, including NaN payloads and noncanonical bits, resolves exactly");
                }
                check_test_expect(test, check_lower_x87_literal_symbol(pools + path, UINT64_C(0x923456789abcdef0), 0x4567) == 0, "an absent synthesized literal requests conservative fallback");
            }
            check_test_expect(test, pools[0].x87_count == pools[1].x87_count &&
                                   (pools[0].x87_count == 0 || memcmp(pools[0].x87_keys, pools[1].x87_keys, (uint64_t)pools[0].x87_count * 16) == 0),
                              "scalar and vector literal pools have identical ordering and bytes at every vector tail");
            if (count != 0)
            {
                const uint64_t mark = output[0].length;
                const uint64_t capacity = output[0].capacity;
                output[0].capacity = mark;
                CheckLowerInitializers refused = { .symbol_base = 73 };
                const bool rejected = !check_lower_x87_literal_pools(&refused, &program, output, &scratch, true);
                check_test_expect(test, rejected && output[0].length == mark && refused.count == 0 && refused.items == NULL && refused.x87_keys == NULL,
                                  "literal allocation failure rolls back without publishing partial metadata");
                output[0].capacity = capacity;
            }
        }
    }
    if (ready)
    {
        static const uint8_t prior_bytes[] = { 3, 1, 4 };
        CheckLowerInitPool prior = { .bytes = prior_bytes, .size = sizeof(prior_bytes), .part = 17 };
        CheckInitPart part = {
            .kind = CHECK_INIT_VALUE,
            .type = BUSTER_TYPE_LONG_DOUBLE,
            .value = {
                .type = BUSTER_TYPE_LONG_DOUBLE,
                .constant_kind = CHECK_CONSTANT_FLOATING,
                .floating_storage = { UINT64_C(0x923456789abcdef0), UINT64_C(0xfedcba9876544567) },
            },
        };
        CheckInitializer initializer = { .parts = &part, .count = 1 };
        const CheckInitializers initializers = { .plans = &initializer, .count = 1 };
        const CheckProgram program = { .types = &types.table, .initializers = &initializers };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            arena_pop_to(output + path, 0);
            CheckLowerInitializers pools = { .items = &prior, .count = 1, .symbol_base = 73 };
            const bool made = check_lower_x87_literal_pools(&pools, &program, output + path, &scratch, path == 0);
            const SymbolId symbol = made ? check_lower_x87_literal_symbol(&pools, part.value.floating_storage[0], part.value.floating_storage[1]) : 0;
            check_test_expect(test, made && pools.x87_first == 1 && pools.count == pools.x87_count + 1 && symbol >= 74 &&
                                   pools.items[0].bytes == prior.bytes && pools.items[0].size == prior.size && pools.items[0].part == prior.part,
                              "converted initializer literals append without changing preceding byte-pool indices");
            const uint64_t output_mark = output[path].length;
            const uint64_t scratch_mark = scratch.length;
            const uint64_t capacity = scratch.capacity;
            scratch.capacity = scratch_mark;
            CheckLowerInitializers refused = { .items = &prior, .count = 1, .symbol_base = 73 };
            const bool rejected = !check_lower_x87_literal_pools(&refused, &program, output + path, &scratch, path == 0);
            check_test_expect(test, rejected && output[path].length == output_mark && scratch.length == scratch_mark &&
                                   refused.items == &prior && refused.count == 1 && refused.x87_count == 0 && refused.x87_keys == NULL,
                              "scratch refusal preserves both arenas and existing pool metadata");
            scratch.capacity = capacity;
        }
    }
    if (guarded != MAP_FAILED)
    {
        munmap(guarded, 2 * page);
    }
    arena_release(&scratch);
    arena_release(output + 1);
    arena_release(output);
    check_test_types_close(&types);
}
