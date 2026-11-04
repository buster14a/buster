// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1238's codec, collisions, 32-bit IDs, protected tails and transactional allocation failures. The ordinary graph
// comparison helper additionally checks dense versus compact output across both validators and every scheduler.

static CheckNodeConversions check_test_conversion_record(uint32_t index)
{
    const CheckNodeConversions result = {
        .operands = {
            { .type = index | BUSTER_CHECK_CONVERSION_INLINE, .flags = index ^ UINT32_C(0x51f7a392) },
            { .type = index + 1, .flags = index * 3 },
            { .type = index * 5, .flags = index * 7 },
        },
        .store = { .type = index * 11, .flags = index * 13 },
    };
    return result;
}

static void check_test_conversion_ids(CheckTest* test)
{
    const CheckNodeConversions fixtures[] = {
        { 0 },
        { .operands = { { .type = BUSTER_TYPE_INT } } },
        { .operands = { { .type = BUSTER_CHECK_CONVERSION_INLINE - 1 } } },
        { .operands = { { .type = BUSTER_CHECK_CONVERSION_INLINE } } },
        { .operands = { { .type = UINT32_MAX } } },
        { .operands = { { .flags = 1 } } },
        { .operands = { { 0 }, { .type = 1 } } },
        { .operands = { { 0 }, { 0 }, { .flags = 1 } } },
        { .store = { .type = UINT32_MAX, .flags = UINT32_MAX } },
        // Original exact collision: changing operand 1 from 1 to 2 is cancelled by these store flags before the
        // final multiply. Hash equality must never merge unequal records, in either insertion order.
        { .operands = { { 0 }, { .type = 2 } }, .store = { .flags = UINT32_C(0x66717a45) } },
    };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        Arena output = arena_reserve(UINT64_C(1) << 25);
        check_test_expect(test, output.base != NULL, "conversion-ID fixture arena");
        if (output.base != NULL)
        {
            uint32_t ids[BUSTER_ARRAY_LENGTH(fixtures)];
            CheckStream stream = {
                .conversion_ids = ids,
                .conversion_table = { .arena = &output, .scalar = path == 0 },
            };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
            {
                check_stream_set_conversions(&stream, i, fixtures + i);
                const uint32_t id = ids[i];
                check_stream_set_conversions(&stream, i, fixtures + i);
                const CheckNodeConversions decoded = check_stream_conversions(&stream, i);
                check_test_expect(test, id == ids[i] && memcmp(&decoded, fixtures + i, sizeof(decoded)) == 0,
                                  "conversion-ID round trip and duplicate retain all eight fields");
            }
            check_test_expect(test, !stream.conversion_table.failed && ids[0] == 0 &&
                                    ids[1] == (BUSTER_CHECK_CONVERSION_INLINE | BUSTER_TYPE_INT) &&
                                    ids[2] == UINT32_MAX && ids[3] != 0 && ids[3] < BUSTER_CHECK_CONVERSION_INLINE,
                              "inline types and full-width table types occupy disjoint ID spaces");
            check_test_expect(test, check_conversion_id_hash(fixtures + 6) == check_conversion_id_hash(fixtures + 9) && ids[6] != ids[9],
                              "equal hashes do not merge different conversion records");
            // Decode every old ID after every growth has moved the record array. More than 65,535 distinct records
            // ensures the per-node IDs really are 32 bits, including for a stream which rewrites the same node.
            const uint32_t first = stream.conversion_table.count;
            for (uint32_t i = 0; i < 70000; i += 1)
            {
                const CheckNodeConversions record = check_test_conversion_record(i);
                check_stream_set_conversions(&stream, 0, &record);
                check_test_expect(test, !stream.conversion_table.failed && ids[0] == first + i + 1,
                                  "conversion dictionary grows without truncating a 32-bit ID");
            }
            const uint32_t retained = stream.conversion_table.count;
            const uint64_t length = output.length;
            for (uint32_t i = 70000; i != 0; i -= 1)
            {
                const CheckNodeConversions record = check_test_conversion_record(i - 1);
                check_stream_set_conversions(&stream, 0, &record);
                const CheckNodeConversions decoded = check_stream_conversions(&stream, 0);
                check_test_expect(test, ids[0] == first + i && memcmp(&record, &decoded, sizeof(record)) == 0,
                                  "old conversion IDs survive growth and reverse-order reuse");
            }
            check_test_expect(test, retained == stream.conversion_table.count && length == output.length,
                              "duplicate conversions allocate no new table or records");
            arena_release(&output);
        }
    }
    // A hand-built dense stream remains writable by its producer and readable through the same accessor.
    CheckNodeConversions dense_value = check_test_conversion_record(UINT32_MAX);
    const CheckStream dense = { .conversions = &dense_value };
    const CheckNodeConversions dense_read = check_stream_conversions(&dense, 0);
    check_test_expect(test, memcmp(&dense_value, &dense_read, sizeof(dense_value)) == 0, "dense conversion accessor remains exact");

    _Alignas(16) uint8_t bytes[1024];
    for (uint32_t limit = 7; limit < 775; limit += 1)
    {
        memset(bytes, 0xa5, sizeof(bytes));
        Arena output = { .base = bytes, .length = 7, .capacity = limit };
        uint32_t id = UINT32_MAX;
        CheckStream stream = {
            .conversion_ids = &id,
            .conversion_table = { .arena = &output, .scalar = true },
        };
        const CheckNodeConversions value = check_test_conversion_record(42);
        check_stream_set_conversions(&stream, 0, &value);
        check_test_expect(test, stream.conversion_table.failed && stream.conversion_table.count == 0 && id == 0 && output.length == 7,
                          "partial dictionary growth rolls back and records failure");
        check_stream_set_conversions(&stream, 0, &value);
        check_test_expect(test, stream.conversion_table.failed && output.length == 7, "conversion allocation failure is sticky");
        bool guarded = true;
        for (uint32_t i = 0; i < 7; i += 1)
        {
            guarded &= bytes[i] == 0xa5;
        }
        for (uint32_t i = limit; i < sizeof(bytes); i += 1)
        {
            guarded &= bytes[i] == 0xa5;
        }
        check_test_expect(test, guarded, "dictionary allocation preserves the arena prefix and capacity tail");
    }

    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "conversion-ID protected tail mapping");
    if (ready)
    {
        uint32_t* hashes = (uint32_t*)(mapping + page) - BUSTER_CHECK_CONVERSION_GROUP;
        for (uint32_t i = 0; i < BUSTER_CHECK_CONVERSION_GROUP; i += 1)
        {
            hashes[i] = i % 2 == 0 ? 19 : 0;
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            uint32_t empty = 0;
            const uint32_t found = check_conversion_id_candidates(hashes, 19, path == 0, &empty);
            check_test_expect(test, found == UINT32_C(0x5555) && empty == UINT32_C(0xaaaa), "protected hash group has exact scalar/vector masks");
        }
        CheckNodeConversions* record = (CheckNodeConversions*)(mapping + page) - 1;
        const CheckNodeConversions expected = check_test_conversion_record(123);
        *record = expected;
        check_test_expect(test, check_conversion_id_equal(record, &expected, false) && check_conversion_id_equal(record, &expected, true),
                          "record equality never reads beyond its 32 bytes");
        uint32_t* id = (uint32_t*)(mapping + page) - 1;
        *id = BUSTER_CHECK_CONVERSION_INLINE | BUSTER_TYPE_LONG;
        const CheckStream stream = { .conversion_ids = id };
        const CheckNodeConversions decoded = check_stream_conversions(&stream, 0);
        check_test_expect(test, decoded.operands[0].type == BUSTER_TYPE_LONG && decoded.store.type == 0,
                          "inline decoding reads only the final four-byte ID");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 2 * page);
    }
}
