// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent checks for foreign_strings.c. The model walks bytes and compares each entry with every earlier
// source entry; it shares neither the terminator scanner, hash functions nor probe table. Both production paths
// must reproduce its complete pool, zero padding, entry maps and every interior-byte lookup. Guarded input slices
// are read-only, and failures must preserve the caller's result and arena mark.

typedef struct ForeignStringsTest ForeignStringsTest;
struct ForeignStringsTest
{
    Arena model;
    Arena paths[2];
    uint64_t checked;
    uint64_t failed;
};

static void foreign_strings_test_expect(ForeignStringsTest* test, bool condition, const char* what, uint64_t detail)
{
    test->checked += 1;
    if (!condition)
    {
        if (test->failed < 12)
        {
            printf("  string merging: %s (%llu)\n", what, (unsigned long long)detail);
        }
        test->failed += 1;
    }
}

static uint64_t foreign_strings_test_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static bool foreign_strings_test_model(const ForeignStringInput* inputs, uint32_t input_count, Arena* arena, ForeignStrings* expected)
{
    uint64_t maximum = 0;
    for (uint32_t section = 0; section < input_count; section += 1)
    {
        maximum += inputs[section].size;
    }
    ForeignStrings model = {
        .input_count = input_count,
        .section_begin = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)input_count + 1),
        .input_offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, maximum + 1),
        .output_offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, maximum + 1),
        .lengths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, maximum + 1),
    };
    const uint8_t** pointers = BUSTER_ARENA_PUSH_ARRAY(arena, const uint8_t*, maximum + 1);
    uint32_t* groups = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, maximum + 1);
    uint8_t* units = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, maximum + 1);
    bool valid = maximum < UINT32_MAX && model.section_begin != 0 && model.input_offset != 0 && model.output_offset != 0 && model.lengths != 0 &&
                 pointers != 0 && groups != 0 && units != 0;
    uint64_t pool_size = 0;
    for (uint32_t section = 0; valid && section < input_count; section += 1)
    {
        model.section_begin[section] = model.count;
        uint32_t begin = 0;
        while (valid && begin < inputs[section].size)
        {
            const uint32_t unit = inputs[section].unit_log2 <= 2 ? 1u << inputs[section].unit_log2 : 1;
            uint32_t end = begin;
            bool terminated = false;
            while (end <= inputs[section].size && unit <= inputs[section].size - end && !terminated)
            {
                uint8_t nonzero = 0;
                for (uint32_t byte = 0; byte < unit; byte += 1)
                {
                    nonzero |= inputs[section].bytes[end + byte];
                }
                terminated = nonzero == 0;
                if (!terminated)
                {
                    end += unit;
                }
            }
            valid = terminated && inputs[section].alignment_log2 < 32 && inputs[section].unit_log2 <= 2;
            if (valid)
            {
                const uint32_t length = end - begin + unit;
                uint64_t alignment = 1ull << inputs[section].alignment_log2;
                while ((uint64_t)begin % alignment != 0)
                {
                    alignment /= 2;
                }
                uint32_t winner = UINT32_MAX;
                for (uint32_t prior = 0; winner == UINT32_MAX && prior < model.count; prior += 1)
                {
                    if (groups[prior] == inputs[section].group && units[prior] == unit && model.lengths[prior] == length && (uint64_t)model.output_offset[prior] % alignment == 0 &&
                        memcmp(pointers[prior], inputs[section].bytes + begin, length) == 0)
                    {
                        winner = prior;
                    }
                }
                const uint64_t offset = winner != UINT32_MAX ? model.output_offset[winner] : ((pool_size + alignment - 1) / alignment) * alignment;
                valid = offset + length <= UINT32_MAX;
                if (valid)
                {
                    const uint32_t entry = model.count;
                    model.input_offset[entry] = begin;
                    model.output_offset[entry] = (uint32_t)offset;
                    model.lengths[entry] = length;
                    pointers[entry] = inputs[section].bytes + begin;
                    groups[entry] = inputs[section].group;
                    units[entry] = (uint8_t)unit;
                    model.count += 1;
                    pool_size = winner == UINT32_MAX ? offset + length : pool_size;
                    begin = end + unit;
                }
            }
        }
    }
    if (valid)
    {
        model.section_begin[input_count] = model.count;
        model.size = (uint32_t)pool_size;
        model.bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, pool_size + 1);
        valid = model.bytes != 0;
    }
    if (valid)
    {
        memset(model.bytes, 0, model.size);
        for (uint32_t entry = 0; entry < model.count; entry += 1)
        {
            memcpy(model.bytes + model.output_offset[entry], pointers[entry], model.lengths[entry]);
        }
        *expected = model;
    }
    return valid;
}

static bool foreign_strings_test_same(const ForeignStrings* a, const ForeignStrings* b)
{
    bool same = a->size == b->size && a->count == b->count && a->input_count == b->input_count && a->bytes != 0 && a->section_begin != 0 &&
                a->input_offset != 0 && a->output_offset != 0 && a->lengths != 0;
    same = same && memcmp(a->bytes, b->bytes, b->size) == 0 && memcmp(a->section_begin, b->section_begin, ((uint64_t)b->input_count + 1) * sizeof(uint32_t)) == 0 &&
           memcmp(a->input_offset, b->input_offset, (uint64_t)b->count * sizeof(uint32_t)) == 0 &&
           memcmp(a->output_offset, b->output_offset, (uint64_t)b->count * sizeof(uint32_t)) == 0 && memcmp(a->lengths, b->lengths, (uint64_t)b->count * sizeof(uint32_t)) == 0;
    return same;
}

static bool foreign_strings_test_mappings(const ForeignStringInput* inputs, const ForeignStrings* expected, const ForeignStrings* actual)
{
    bool valid = true;
    for (uint32_t section = 0; valid && section < expected->input_count; section += 1)
    {
        uint32_t entry = expected->section_begin[section];
        for (uint32_t byte = 0; valid && byte < inputs[section].size; byte += 1)
        {
            while (byte >= expected->input_offset[entry] + expected->lengths[entry])
            {
                entry += 1;
            }
            const uint32_t wanted = expected->output_offset[entry] + byte - expected->input_offset[entry];
            uint32_t mapped = UINT32_MAX;
            valid = foreign_strings_offset(actual, section, byte, &mapped) && mapped == wanted && mapped < actual->size && actual->bytes[mapped] == inputs[section].bytes[byte];
        }
        uint32_t untouched = UINT32_MAX;
        valid = valid && !foreign_strings_offset(actual, section, inputs[section].size, &untouched) && untouched == UINT32_MAX &&
                !foreign_strings_offset(actual, section, UINT64_MAX, &untouched) && untouched == UINT32_MAX;
    }
    uint32_t untouched = UINT32_MAX;
    valid = valid && !foreign_strings_offset(actual, expected->input_count, 0, &untouched) && untouched == UINT32_MAX &&
            !foreign_strings_offset(actual, UINT32_MAX, 0, &untouched) && !foreign_strings_offset(actual, 0, 0, 0) && !foreign_strings_offset(0, 0, 0, &untouched);
    return valid;
}

static void foreign_strings_test_case(ForeignStringsTest* test, const ForeignStringInput* inputs, uint32_t input_count, const char* what, uint64_t detail)
{
    const uint64_t mark = test->model.length;
    ForeignStrings expected = { 0 };
    bool ready = foreign_strings_test_model(inputs, input_count, &test->model, &expected);
    ForeignStringInput* descriptors = BUSTER_ARENA_PUSH_ARRAY(&test->model, ForeignStringInput, (uint64_t)input_count + 1);
    uint64_t bytes = 0;
    for (uint32_t section = 0; section < input_count; section += 1)
    {
        bytes += inputs[section].size;
    }
    uint8_t* saved = BUSTER_ARENA_PUSH_ARRAY(&test->model, uint8_t, bytes + 1);
    ready = ready && descriptors != 0 && saved != 0;
    if (ready && input_count != 0)
    {
        memcpy(descriptors, inputs, (uint64_t)input_count * sizeof(*inputs));
    }
    uint64_t at = 0;
    for (uint32_t section = 0; ready && section < input_count; section += 1)
    {
        if (inputs[section].size != 0)
        {
            memcpy(saved + at, inputs[section].bytes, inputs[section].size);
        }
        at += inputs[section].size;
    }
    foreign_strings_test_expect(test, ready, "independent model allocation", detail);
    ForeignStrings results[2] = { 0 };
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const uint64_t poisoned = (uint64_t)expected.size + ((uint64_t)input_count + 1 + 3 * ((uint64_t)expected.count + 1)) * sizeof(uint32_t) + 64;
        if (poisoned <= test->paths[path].capacity)
        {
            assert(arena_commit(&test->paths[path], test->paths[path].capacity));
            memset(test->paths[path].base, path == 0 ? 0xa5 : 0x5a, poisoned);
        }
        const ForeignStringsError error = foreign_strings_build(inputs, input_count, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, &test->paths[path], &results[path]);
        const bool same = error == FOREIGN_STRINGS_ERROR_NONE && foreign_strings_test_same(&results[path], &expected);
        foreign_strings_test_expect(test, same, what, detail * 2 + path);
        foreign_strings_test_expect(test, same && foreign_strings_test_mappings(inputs, &expected, &results[path]), "every interior byte, NUL and outside offset", detail * 2 + path);
        bool unchanged = input_count == 0 || memcmp(inputs, descriptors, (uint64_t)input_count * sizeof(*inputs)) == 0;
        at = 0;
        for (uint32_t section = 0; unchanged && section < input_count; section += 1)
        {
            unchanged = inputs[section].size == 0 || memcmp(inputs[section].bytes, saved + at, inputs[section].size) == 0;
            at += inputs[section].size;
        }
        foreign_strings_test_expect(test, unchanged, "input bytes and descriptors unchanged", detail * 2 + path);
    }
    if (ready)
    {
        foreign_strings_test_expect(test, results[0].bytes != 0 && results[1].bytes != 0 && foreign_strings_test_same(&results[0], &results[1]), "exact/vector output identity", detail);
    }
    arena_pop_to(&test->paths[0], 0);
    arena_pop_to(&test->paths[1], 0);
    arena_pop_to(&test->model, mark);
}

static void foreign_strings_test_examples(ForeignStringsTest* test)
{
    static const uint8_t first[] = { 'a', 'b', 0, 'x', 0, 0, 'a', 'b', 0 };
    static const uint8_t second[] = { 'a', 'b', 0, 'x', 0, 0 };
    static const uint8_t z[] = { 'z', 0 };
    static const uint8_t suffix[] = { 'f', 'o', 'o', 'b', 'a', 'r', 0, 'b', 'a', 'r', 0 };
    const ForeignStringInput inputs[] = {
        {
            .bytes = first,
            .size = sizeof(first),
            .group = 1,
            .alignment_log2 = 3,
        },
        {
            .bytes = second,
            .size = sizeof(second),
            .group = 1,
            .alignment_log2 = 4,
        },
        {
            .bytes = second,
            .size = sizeof(second),
            .group = 2,
        },
        {
            .bytes = z,
            .size = sizeof(z),
            .group = 3,
        },
        {
            .bytes = z,
            .size = sizeof(z),
            .group = 3,
            .alignment_log2 = 4,
        },
        {
            .bytes = z,
            .size = sizeof(z),
            .group = 3,
            .alignment_log2 = 3,
        },
        {
            .bytes = suffix,
            .size = sizeof(suffix),
            .group = UINT32_MAX,
        },
        {
            .alignment_log2 = 31,
        },
    };
    foreign_strings_test_case(test, 0, 0, "no input sections", 0);
    foreign_strings_test_case(test, inputs, BUSTER_ARRAY_LENGTH(inputs), "groups, stable winners, effective alignment, padding and no suffix merging", 1);
    const ForeignStringInput high[] = {
        {
            .bytes = z,
            .size = sizeof(z),
        },
        {
            .bytes = z,
            .size = sizeof(z),
            .alignment_log2 = 31,
        },
    };
    foreign_strings_test_case(test, high, BUSTER_ARRAY_LENGTH(high), "actual offset zero satisfies a later stronger alignment", 2);
    static const uint8_t collision[] = { 'c', 'o', 'l', 'l', 'i', 's', 'i', 'o', 'n', 0 };
    ForeignStringInput colliding[128] = { 0 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(colliding); i += 1)
    {
        // The current table's group mix preserves these zero low bits. Distinct groups therefore start probing
        // the same bucket, while the second half must still find each group's earliest matching copy.
        colliding[i] = (ForeignStringInput){
            .bytes = collision,
            .size = sizeof(collision),
            .group = (i % 64) * 1024,
            .alignment_log2 = i % 6,
        };
    }
    foreign_strings_test_case(test, colliding, BUSTER_ARRAY_LENGTH(colliding), "colliding probe buckets retain group identity and first suitable copies", 3);
    static const uint8_t wide16[] = { 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 };
    static const uint8_t wide32[] = { 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
    const ForeignStringInput wide[] = {
        {
            .bytes = wide16,
            .size = sizeof(wide16),
            .group = 11,
            .alignment_log2 = 1,
            .unit_log2 = 1,
        },
        {
            .bytes = wide32,
            .size = sizeof(wide32),
            .group = 11,
            .alignment_log2 = 2,
            .unit_log2 = 2,
        },
        {
            .bytes = wide16,
            .size = sizeof(wide16),
            .group = 11,
            .alignment_log2 = 4,
            .unit_log2 = 1,
        },
        {
            .bytes = wide32,
            .size = sizeof(wide32),
            .group = 11,
            .unit_log2 = 1,
        },
        {
            .bytes = wide16,
            .size = sizeof(wide16),
            .group = 11,
        },
        {
            .bytes = wide16,
            .size = sizeof(wide16),
            .group = 12,
            .unit_log2 = 1,
        },
    };
    foreign_strings_test_case(test, wide, BUSTER_ARRAY_LENGTH(wide), "mixed character widths, embedded zero bytes, groups and byte-interior maps", 4);
}

static void foreign_strings_test_random_cases(ForeignStringsTest* test, uint64_t seed)
{
    uint64_t state = seed != 0 ? seed : 0x3141592653589793ull;
    uint8_t dictionary[32][97];
    uint32_t lengths[32];
    for (uint32_t word = 0; word < 32; word += 1)
    {
        lengths[word] = word == 0 ? 1 : 1 + (word * 13) % 96;
        for (uint32_t byte = 0; byte + 1 < lengths[word]; byte += 1)
        {
            dictionary[word][byte] = byte < 12 ? 'q' : (uint8_t)(1 + (word * 17 + byte * 7) % 255);
        }
        dictionary[word][lengths[word] - 1] = 0;
    }
    for (uint32_t trial = 0; trial < 192; trial += 1)
    {
        uint8_t bytes[17][1024];
        ForeignStringInput inputs[17] = { 0 };
        const uint32_t input_count = (uint32_t)(foreign_strings_test_random(&state) % 18);
        for (uint32_t section = 0; section < input_count; section += 1)
        {
            const uint32_t words = (uint32_t)(foreign_strings_test_random(&state) % 11);
            uint32_t size = 0;
            for (uint32_t word = 0; word < words; word += 1)
            {
                const uint32_t which = (uint32_t)(foreign_strings_test_random(&state) % 32);
                memcpy(bytes[section] + size, dictionary[which], lengths[which]);
                size += lengths[which];
            }
            inputs[section] = (ForeignStringInput){
                .bytes = bytes[section],
                .size = size,
                .group = (uint32_t)(foreign_strings_test_random(&state) % 5),
                .alignment_log2 = (uint8_t)(foreign_strings_test_random(&state) % 7),
            };
        }
        foreign_strings_test_case(test, inputs, input_count, "deterministic mixed lengths, groups and alignments", trial + 4);
    }
}

static void foreign_strings_test_guards(ForeignStringsTest* test)
{
    static const uint32_t lengths[] = { 0, 1, 2, 7, 8, 15, 16, 31, 32, 63, 64, 65, 127, 128, 129, 255, 256, 257 };
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = page > 4 * 257 ? mmap(0, 3 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    foreign_strings_test_expect(test, ready, "guard mapping", 0);
    for (uint32_t side = 0; ready && side < 2; side += 1)
    {
        for (uint8_t unit_log2 = 0; ready && unit_log2 < 3; unit_log2 += 1)
        {
            const uint32_t unit = 1u << unit_log2;
            for (uint32_t length = 0; ready && length < BUSTER_ARRAY_LENGTH(lengths); length += 1)
            {
                const uint32_t size = lengths[length] * unit;
                for (uint32_t pattern = 0; ready && pattern < 3; pattern += 1)
                {
                    ready = mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0;
                    uint8_t* bytes = mapping + (side == 0 ? page : 2 * page - size);
                    for (uint32_t index = 0; ready && index < lengths[length]; index += 1)
                    {
                        const bool zero = pattern == 0 || index + 1 == lengths[length] || (pattern == 2 && index % 17 == 0);
                        for (uint32_t byte = 0; byte < unit; byte += 1)
                        {
                            // Nonzero wide units deliberately contain zero bytes, including their low byte.
                            bytes[index * unit + byte] = !zero && byte == index % unit ? (uint8_t)(1 + index % 255) : 0;
                        }
                    }
                    ready = ready && mprotect(mapping + page, page, PROT_READ) == 0;
                    if (ready)
                    {
                        const ForeignStringInput inputs[] = {
                            {
                                .bytes = bytes,
                                .size = size,
                                .group = 7,
                                .alignment_log2 = 6,
                                .unit_log2 = unit_log2,
                            },
                            {
                                .bytes = bytes,
                                .size = size,
                                .group = 7,
                                .unit_log2 = unit_log2,
                            },
                        };
                        foreign_strings_test_case(test, inputs, 2, "read-only guarded 1/2/4-byte scanner/hash tails", side * 100000 + unit_log2 * 10000 + size * 3 + pattern);
                    }
                }
            }
        }
    }
    foreign_strings_test_expect(test, ready, "guard protection changes", 0);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 3 * page);
    }
}

static void foreign_strings_test_failure(ForeignStringsTest* test, const ForeignStringInput* inputs, uint32_t input_count, ForeignPath path,
                                         Arena* arena, ForeignStringsError wanted, const char* what, uint64_t detail)
{
    ForeignStrings output;
    ForeignStrings saved;
    memset(&output, 0xa5, sizeof(output));
    memcpy(&saved, &output, sizeof(output));
    const uint64_t mark = arena != 0 ? arena->length : 0;
    const ForeignStringsError error = foreign_strings_build(inputs, input_count, path, arena, &output);
    foreign_strings_test_expect(test, error == wanted && memcmp(&output, &saved, sizeof(output)) == 0 && (arena == 0 || arena->length == mark), what, detail);
}

static void foreign_strings_test_failures(ForeignStringsTest* test)
{
    static const uint8_t zero[] = { 0 };
    static const uint8_t missing[] = { 'n', 'o' };
    static const uint8_t wide_missing[] = { 0, 0, 0, 1 };
    static const uint8_t partial[] = { 0, 0, 0 };
    static const uint8_t many[] = { 'a', 'b', 0, 0, 'c', 0, 'a', 'b', 0 };
    const ForeignStringInput valid = {
        .bytes = many,
        .size = sizeof(many),
        .alignment_log2 = 3,
    };
    const ForeignStringInput malformed[] = {
        {
            .size = 1,
        },
        {
            .bytes = missing,
            .size = sizeof(missing),
        },
        {
            .bytes = zero,
            .size = 1,
            .alignment_log2 = 32,
        },
        {
            .bytes = partial,
            .size = sizeof(partial),
            .unit_log2 = 1,
        },
        {
            .bytes = partial,
            .size = sizeof(partial),
            .unit_log2 = 2,
        },
        {
            .bytes = wide_missing + 2,
            .size = 2,
            .unit_log2 = 1,
        },
        {
            .bytes = wide_missing,
            .size = sizeof(wide_missing),
            .unit_log2 = 2,
        },
        {
            .unit_log2 = 3,
        },
    };
    const ForeignStringInput excessive[] = {
        {
            .bytes = zero,
            .size = 1,
            .group = 0,
            .alignment_log2 = 31,
        },
        {
            .bytes = zero,
            .size = 1,
            .group = 1,
            .alignment_log2 = 31,
        },
        {
            .bytes = zero,
            .size = 1,
            .group = 2,
            .alignment_log2 = 31,
        },
    };
    for (uint32_t way = 0; way < 2; way += 1)
    {
        Arena* arena = &test->paths[way];
        const ForeignPath path = way == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR;
        uint8_t* prefix = arena_push(arena, 17, 1);
        foreign_strings_test_expect(test, prefix != 0, "failure prefix allocation", way);
        if (prefix != 0)
        {
            memset(prefix, 0x3c, 17);
            for (uint32_t bad = 0; bad < BUSTER_ARRAY_LENGTH(malformed); bad += 1)
            {
                foreign_strings_test_failure(test, &malformed[bad], 1, path, arena, FOREIGN_STRINGS_ERROR_INPUT, "malformed input rolls back", way * 10 + bad);
            }
            foreign_strings_test_failure(test, 0, 1, path, arena, FOREIGN_STRINGS_ERROR_INPUT, "missing input descriptors", way);
            foreign_strings_test_failure(test, &valid, 1, (ForeignPath)255, arena, FOREIGN_STRINGS_ERROR_INPUT, "invalid path", way);
            foreign_strings_test_failure(test, &valid, 1, path, 0, FOREIGN_STRINGS_ERROR_INPUT, "missing arena", way);
            foreign_strings_test_failure(test, excessive, 3, path, arena, FOREIGN_STRINGS_ERROR_CAPACITY, "aligned pool exceeds 32-bit offsets", way);
            const uint64_t mark = arena->length;
            foreign_strings_test_expect(test, foreign_strings_build(&valid, 1, path, arena, 0) == FOREIGN_STRINGS_ERROR_INPUT && arena->length == mark,
                                        "missing result rolls back", way);
            ForeignStrings successful = { 0 };
            const bool built = foreign_strings_build(&valid, 1, path, arena, &successful) == FOREIGN_STRINGS_ERROR_NONE;
            const uint64_t needed = arena->length - mark;
            foreign_strings_test_expect(test, built && needed != 0, "measure output arena requirement", way);
            arena_pop_to(arena, mark);
            const uint64_t capacity = arena->capacity;
            const uint64_t rooms[] = { 0, 1, 7, 31, 63, 64, 127, 511, needed != 0 ? needed - 1 : 0 };
            for (uint32_t room = 0; built && room < BUSTER_ARRAY_LENGTH(rooms); room += 1)
            {
                if (rooms[room] < needed)
                {
                    arena->capacity = mark + rooms[room];
                    foreign_strings_test_failure(test, &valid, 1, path, arena, FOREIGN_STRINGS_ERROR_MEMORY, "starved arena preserves result and mark", way * 100 + room);
                    arena->capacity = capacity;
                }
            }
            bool unchanged = true;
            for (uint32_t byte = 0; byte < 17; byte += 1)
            {
                unchanged = unchanged && prefix[byte] == 0x3c;
            }
            foreign_strings_test_expect(test, unchanged, "arena prefix survives all failures", way);
        }
        arena_pop_to(arena, 0);
    }
}

static void foreign_strings_test_link(ForeignStringsTest* test)
{
    static const uint8_t first_source[] = { 'p', 'r', 'e', 'f', 'i', 'x', 0, 's', 'a', 'm', 'e', 0, 't', 'a', 'i', 'l', 0 };
    static const uint8_t second_source[] = { 's', 'a', 'm', 'e', 0, 'x', 0, 's', 'a', 'm', 'e', 0 };
    static const uint8_t wanted_pool[] = { 'p', 'r', 'e', 'f', 'i', 'x', 0, 's', 'a', 'm', 'e', 0, 't', 'a', 'i', 'l', 0, 'x', 0 };
    static const uint32_t second_map[] = { 7, 8, 9, 10, 11, 17, 18, 7, 8, 9, 10, 11 };
    const LinkCapacity capacity = {
        .names = 8,
        .atoms = 4,
        .symbols = 8,
        .fixups = 16,
        .wide_addends = 4,
    };
    for (uint32_t way = 0; way < 2; way += 1)
    {
        const uint64_t mark = test->model.length;
        Link link = { 0 };
        uint8_t first[sizeof(first_source)];
        uint8_t second[sizeof(second_source)];
        uint8_t data[2][128] = { 0 };
        memcpy(first, first_source, sizeof(first));
        memcpy(second, second_source, sizeof(second));
        const ForeignStringInput inputs[] = {
            {
                .bytes = first,
                .size = sizeof(first),
                .group = 7,
                .alignment_log2 = 2,
            },
            {
                .bytes = second,
                .size = sizeof(second),
                .group = 7,
            },
        };
        ForeignStrings strings = { 0 };
        const ForeignPath path = way == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR;
        bool ready = link_initialize(&link, &test->model, &test->model, capacity) &&
                     foreign_strings_build(inputs, 2, path, &test->paths[way], &strings) == FOREIGN_STRINGS_ERROR_NONE;
        AtomId atoms[4] = { 0 };
        SymbolId symbols[7] = { 0 };
        FixupId fixups[12] = { 0 };
        uint32_t atom_inputs[5] = { 0 };
        uint8_t section_symbols[9] = { 0 };
        if (ready)
        {
            atoms[0] = link_add_atom(&link, BUSTER_LINK_CLASS_STRINGS, 2, sizeof(first), first, 1);
            atoms[1] = link_add_atom(&link, BUSTER_LINK_CLASS_STRINGS, 0, sizeof(second), second, 2);
            atoms[2] = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, sizeof(data[0]), data[0], 1);
            atoms[3] = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, sizeof(data[1]), data[1], 2);
            static const char* const names[] = { "first_same", "second_same", "interior", "first_section", "second_section", "section_alias", "same_alias" };
            static const uint32_t source_atom[] = { 0, 1, 1, 0, 1, 1, 1 };
            static const uint32_t values[] = { 7, 0, 1, 0, 0, 1, 7 };
            static const uint32_t sizes[] = { 5, 5, 2, 0, 0, 0, 5 };
            for (uint32_t symbol = 0; symbol < BUSTER_ARRAY_LENGTH(symbols); symbol += 1)
            {
                const NameId name = link_add_name(&link, (const uint8_t*)names[symbol], (uint32_t)strlen(names[symbol]));
                const uint8_t flags = symbol >= 3 && symbol <= 5 ? BUSTER_LINK_SYMBOL_NOTYPE : BUSTER_LINK_SYMBOL_OBJECT;
                symbols[symbol] = link_add_symbol(&link, name, atoms[source_atom[symbol]], values[symbol], sizes[symbol], flags, BUSTER_LINK_SOURCE_FOREIGN_OBJECT);
            }
            atom_inputs[atoms[0]] = 1;
            atom_inputs[atoms[1]] = 2;
            section_symbols[symbols[3]] = 1;
            section_symbols[symbols[4]] = 1;
            section_symbols[symbols[5]] = 1;
            fixups[0] = link_add_fixup(&link, atoms[2], 0, BUSTER_LINK_FIXUP_ABS64, symbols[0], 0);
            fixups[1] = link_add_fixup(&link, atoms[3], 0, BUSTER_LINK_FIXUP_ABS64, symbols[4], 0);
            fixups[2] = link_add_fixup(&link, atoms[3], 8, BUSTER_LINK_FIXUP_ABS32, symbols[4], 5);
            fixups[3] = link_add_fixup(&link, atoms[3], 16, BUSTER_LINK_FIXUP_ABS32S, symbols[4], 7);
            fixups[4] = link_add_fixup(&link, atoms[3], 24, BUSTER_LINK_FIXUP_PC64, symbols[2], -1);
            fixups[5] = link_add_fixup(&link, atoms[3], 32, BUSTER_LINK_FIXUP_PC64, symbols[1], 2);
            fixups[6] = link_add_fixup(&link, atoms[3], 40, BUSTER_LINK_FIXUP_ABS64, symbols[1], 2);
            fixups[7] = link_add_fixup(&link, atoms[3], 48, BUSTER_LINK_FIXUP_SIZE32, symbols[1], 11);
            fixups[8] = link_add_fixup(&link, atoms[3], 56, BUSTER_LINK_FIXUP_SIZE64, symbols[4], 1ll << 40);
            fixups[9] = link_add_fixup(&link, atoms[3], 64, BUSTER_LINK_FIXUP_GOTPCREL, symbols[4], -4);
            fixups[10] = link_add_fixup(&link, atoms[3], 72, BUSTER_LINK_FIXUP_ABS64, symbols[5], -1);
            fixups[11] = link_add_fixup(&link, atoms[3], 80, BUSTER_LINK_FIXUP_ABS64, symbols[4], 11);
            ready = link.error == LINK_ERROR_NONE && link_check(&link) == LINK_ERROR_NONE && strings.size == sizeof(wanted_pool) &&
                    memcmp(strings.bytes, wanted_pool, sizeof(wanted_pool)) == 0;
        }
        foreign_strings_test_expect(test, ready, "manual Link fixture and known pool", way);
        const uint64_t columns_size = test->model.length - mark;
        uint8_t* baseline = ready ? arena_push(&test->paths[1 - way], columns_size, 1) : 0;
        uint8_t* before = ready ? arena_push(&test->paths[1 - way], columns_size, 1) : 0;
        uint8_t* pool_before = ready ? arena_push(&test->paths[1 - way], test->paths[way].length, 1) : 0;
        ready = ready && baseline != 0 && before != 0 && pool_before != 0;
        foreign_strings_test_expect(test, ready, "Link and immutable map snapshots", way);
        if (ready)
        {
            memcpy(baseline, test->model.base + mark, columns_size);
            memcpy(pool_before, test->paths[way].base, test->paths[way].length);
            Link original;
            memcpy(&original, &link, sizeof(link));
            static const uint8_t direct_kinds[] = {
                BUSTER_LINK_FIXUP_ABS32,
                BUSTER_LINK_FIXUP_ABS64,
                BUSTER_LINK_FIXUP_ABS32S,
                BUSTER_LINK_FIXUP_PC64,
            };
            for (uint32_t bad = 0; bad < 20; bad += 1)
            {
                ForeignStringsError wanted = FOREIGN_STRINGS_ERROR_INPUT;
                if (bad == 0)
                {
                    // A legal Link object alias that includes the following independently merged string.
                    link.symbols.size[symbols[1]] = 6;
                    wanted = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
                }
                else if (bad == 1 || bad == 2)
                {
                    // This is the final fixup, after several earlier section expressions have translated.
                    link.fixups.addend[fixups[11]] = bad == 1 ? (int32_t)sizeof(second) : -1;
                    wanted = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
                    foreign_strings_test_expect(test, link_check(&link) == LINK_ERROR_NONE,
                                                "one-past and before-section absolute expressions remain valid unmerged Link inputs", way * 100 + bad);
                }
                else if (bad == 3 || bad == 4)
                {
                    link.fixups.kind[fixups[11]] = bad == 3 ? BUSTER_LINK_FIXUP_PC32 : BUSTER_LINK_FIXUP_PC64;
                    wanted = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
                }
                else if (bad == 5)
                {
                    atom_inputs[atoms[1]] = 3;
                }
                else if (bad == 6)
                {
                    // One-past symbols are valid Link records, but there is no input byte to translate.
                    link.symbols.value[symbols[6]] = sizeof(second);
                    link.symbols.size[symbols[6]] = 0;
                    wanted = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
                }
                else if (bad < 10)
                {
                    // The next entry moves independently: remapping S and retaining this A would read "tail".
                    link.fixups.target[fixups[11]] = symbols[1];
                    link.fixups.kind[fixups[11]] = direct_kinds[bad - 7];
                    link.fixups.addend[fixups[11]] = 5;
                    wanted = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
                }
                else if (bad < 16)
                {
                    // Without instruction-end provenance no sign of a PC32/CALL addend makes pooling safe.
                    static const int32_t addends[] = { -4, 0, 1 };
                    link.fixups.target[fixups[11]] = symbols[1];
                    link.fixups.kind[fixups[11]] = bad < 13 ? BUSTER_LINK_FIXUP_PC32 : BUSTER_LINK_FIXUP_CALL;
                    link.fixups.addend[fixups[11]] = addends[(bad - 10) % 3];
                    wanted = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
                }
                else if (bad < 19)
                {
                    link.fixups.target[fixups[11]] = bad == 18 ? symbols[6] : symbols[1];
                    link.fixups.kind[fixups[11]] = BUSTER_LINK_FIXUP_PC64;
                    link.fixups.addend[fixups[11]] = bad == 16 ? 5 : -1;
                    wanted = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
                }
                else
                {
                    link.fixups.kind[fixups[11]] = BUSTER_LINK_FIXUP_CALL;
                    link.fixups.addend[fixups[11]] = 0;
                    wanted = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
                }
                memcpy(before, test->model.base + mark, columns_size);
                const ForeignStringsError error = foreign_strings_apply(&link, &strings, atom_inputs, section_symbols);
                const bool unchanged = memcmp(&link, &original, sizeof(link)) == 0 && memcmp(before, test->model.base + mark, columns_size) == 0 &&
                                       memcmp(pool_before, test->paths[way].base, test->paths[way].length) == 0 && test->model.length == mark + columns_size;
                foreign_strings_test_expect(test, error == wanted && unchanged, "consumer failure preserves every Link column and pool/map byte", way * 100 + bad);
                memcpy(test->model.base + mark, baseline, columns_size);
                memcpy(&link, &original, sizeof(link));
                atom_inputs[atoms[1]] = 2;
            }
            Scratch starved = scratch_begin(0);
            const uint64_t scratch_capacity = starved.arena->capacity;
            starved.arena->capacity = starved.arena->length;
            const ForeignStringsError memory_error = foreign_strings_apply(&link, &strings, atom_inputs, section_symbols);
            const bool scratch_unchanged = starved.arena->length == starved.length;
            starved.arena->capacity = scratch_capacity;
            scratch_end(starved);
            foreign_strings_test_expect(test, memory_error == FOREIGN_STRINGS_ERROR_MEMORY && scratch_unchanged && memcmp(&link, &original, sizeof(link)) == 0 &&
                                              memcmp(baseline, test->model.base + mark, columns_size) == 0, "consumer scratch exhaustion is atomic", way);
            for (uint32_t kind = 0; kind < BUSTER_ARRAY_LENGTH(direct_kinds); kind += 1)
            {
                link.fixups.kind[fixups[6]] = direct_kinds[kind];
                const ForeignStringsError interior = foreign_strings_apply(&link, &strings, atom_inputs, section_symbols);
                foreign_strings_test_expect(test, interior == FOREIGN_STRINGS_ERROR_NONE && link.symbols.value[symbols[1]] == 7 &&
                                                  link_fixup_addend(&link, fixups[6]) == 2 && link_check(&link) == LINK_ERROR_NONE,
                                            "named same-entry absolute/PC64 nonzero addend remains unchanged", way * BUSTER_ARRAY_LENGTH(direct_kinds) + kind);
                memcpy(test->model.base + mark, baseline, columns_size);
                memcpy(&link, &original, sizeof(link));
            }
            const ForeignStringsError applied = foreign_strings_apply(&link, &strings, atom_inputs, section_symbols);
            foreign_strings_test_expect(test, applied == FOREIGN_STRINGS_ERROR_NONE && link_check(&link) == LINK_ERROR_NONE,
                                        "merged cross-unit local targets remain valid Link records", way);
            bool values_match = applied == FOREIGN_STRINGS_ERROR_NONE && link.atoms.size[atoms[0]] == sizeof(wanted_pool) && link.atoms.bytes[atoms[0]] == strings.bytes &&
                                link.atoms.unit[atoms[0]] == 0 && link.atoms.alignment_log2[atoms[0]] == 2 && link.atoms.size[atoms[1]] == 0 &&
                                link.atoms.bytes[atoms[1]] == 0 && link.atoms.unit[atoms[1]] == 0 && link.atoms.count == original.atoms.count;
            static const uint32_t wanted_values[] = { 7, 7, 8, 0, 7, 8, 7 };
            for (uint32_t symbol = 0; symbol < BUSTER_ARRAY_LENGTH(symbols); symbol += 1)
            {
                values_match = values_match && link.symbols.atom[symbols[symbol]] == atoms[0] && link.symbols.value[symbols[symbol]] == wanted_values[symbol];
            }
            foreign_strings_test_expect(test, values_match, "named aliases, interior symbols and section bases translate to shared storage", way);
            static const int64_t wanted_addends[] = { 0, 0, 10, 0, -1, 2, 2, 11, 1ll << 40, -4, -1, 4 };
            bool expressions_match = true;
            for (uint32_t fixup = 0; fixup < BUSTER_ARRAY_LENGTH(fixups); fixup += 1)
            {
                expressions_match = expressions_match && link_fixup_addend(&link, fixups[fixup]) == wanted_addends[fixup];
            }
            foreign_strings_test_expect(test, expressions_match && link.fixups.wide_count == original.fixups.wide_count &&
                                              (link.fixups.kind[fixups[8]] & BUSTER_LINK_FIXUP_WIDE) != 0,
                                        "section absolute S+A, named same-entry PC64, size and GOT expressions", way);
            bool map_matches = memcmp(pool_before, test->paths[way].base, test->paths[way].length) == 0 && memcmp(first, first_source, sizeof(first)) == 0 &&
                               memcmp(second, second_source, sizeof(second)) == 0 && atom_inputs[atoms[0]] == 1 && atom_inputs[atoms[1]] == 2;
            for (uint32_t offset = 0; offset < sizeof(second); offset += 1)
            {
                uint32_t mapped = UINT32_MAX;
                map_matches = map_matches && foreign_strings_offset(&strings, atom_inputs[atoms[1]] - 1, offset, &mapped) && mapped == second_map[offset];
            }
            foreign_strings_test_expect(test, map_matches, "original atom ids and maps still translate every old source byte", way);
        }
        arena_pop_to(&test->paths[0], 0);
        arena_pop_to(&test->paths[1], 0);
        arena_pop_to(&test->model, mark);
    }
}

static bool foreign_strings_test(uint64_t seed)
{
    ForeignStringsTest test = {
        .model = arena_reserve(16ull << 20),
        .paths = {
            arena_reserve(16ull << 20),
            arena_reserve(16ull << 20),
        },
    };
    const bool ready = test.model.base != 0 && test.paths[0].base != 0 && test.paths[1].base != 0;
    foreign_strings_test_expect(&test, ready, "test arenas", 0);
    if (ready)
    {
        foreign_strings_test_examples(&test);
        foreign_strings_test_random_cases(&test, seed);
        foreign_strings_test_guards(&test);
        foreign_strings_test_failures(&test);
        foreign_strings_test_link(&test);
    }
    arena_release(&test.model);
    arena_release(&test.paths[0]);
    arena_release(&test.paths[1]);
    printf("string merging: %llu checks, %llu failed (independent model, every-byte maps, collision groups, alignment, read-only guards and rollback)\n",
           (unsigned long long)test.checked, (unsigned long long)test.failed);
    return test.failed == 0;
}
