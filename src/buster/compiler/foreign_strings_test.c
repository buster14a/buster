// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent checks for foreign_strings.c. The model walks bytes and compares each entry with every earlier
// entry that owns storage, then, for one-byte units, finds its block head by comparing it with the ends of every
// earlier entry (#3044); it shares neither the terminator scanner, hash functions, probe table, prefilter nor sort.
// Both production paths must reproduce its complete pool, zero padding, entry maps and every interior-byte lookup, and
// the suffix cases' pools and offsets worked out by hand. The earlier suffix rule's walk stays as a second model: at
// alignment 1 the two must agree. Direct calls of the prefilter and the head pass pin how many entries they sort and
// how many heads they write. Guarded input slices are read-only, and failures must preserve the caller's result and
// arena mark.

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

// The earlier suffix rule, kept for the alignment-1 trials alone: the end of the first earlier, longer one-byte owner
// of the group that ends with the entry, where that offset meets its alignment.
static uint32_t foreign_strings_test_walk(const ForeignStrings* model, const uint8_t* const* pointers, const uint32_t* groups, const uint8_t* units, const uint8_t* owners,
                                          uint32_t group, const uint8_t* bytes, uint32_t length, uint64_t alignment, uint64_t* offset)
{
    uint32_t winner = UINT32_MAX;
    for (uint32_t prior = 0; winner == UINT32_MAX && prior < model->count; prior += 1)
    {
        if (owners[prior] != 0 && groups[prior] == group && units[prior] == 1 && model->lengths[prior] > length)
        {
            const uint32_t start = model->lengths[prior] - length;
            const uint64_t shared = (uint64_t)model->output_offset[prior] + start;
            if (shared % alignment == 0 && memcmp(pointers[prior] + start, bytes, length) == 0)
            {
                winner = prior;
                *offset = shared;
            }
        }
    }
    return winner;
}

// The block head rule (#3044): the head is the first earlier entry of the group, owner or not, with one-byte units, at
// least as long, whose content ends with the entry's. It must be an owner; its end is taken where that offset meets
// the alignment.
static uint32_t foreign_strings_test_head(ForeignStringsTest* test, const ForeignStrings* model, const uint8_t* const* pointers, const uint32_t* groups, const uint8_t* units,
                                          const uint8_t* owners, uint32_t group, const uint8_t* bytes, uint32_t length, uint64_t alignment, uint64_t* offset)
{
    uint32_t head = UINT32_MAX;
    for (uint32_t prior = 0; head == UINT32_MAX && prior < model->count; prior += 1)
    {
        if (groups[prior] == group && units[prior] == 1 && model->lengths[prior] >= length &&
            memcmp(pointers[prior] + model->lengths[prior] - length, bytes, length) == 0)
        {
            head = prior;
        }
    }
    uint32_t winner = UINT32_MAX;
    if (head != UINT32_MAX)
    {
        foreign_strings_test_expect(test, owners[head] != 0, "a block head owns its bytes", head);
        const uint64_t shared = (uint64_t)model->output_offset[head] + model->lengths[head] - length;
        if (shared % alignment == 0)
        {
            winner = head;
            *offset = shared;
        }
    }
    return winner;
}

static bool foreign_strings_test_model(ForeignStringsTest* test, const ForeignStringInput* inputs, uint32_t input_count, bool walk, Arena* arena, ForeignStrings* expected)
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
    uint8_t* owners = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, maximum + 1);
    bool valid = maximum < UINT32_MAX && model.section_begin != 0 && model.input_offset != 0 && model.output_offset != 0 && model.lengths != 0 &&
                 pointers != 0 && groups != 0 && units != 0 && owners != 0;
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
                uint64_t offset = 0;
                for (uint32_t prior = 0; winner == UINT32_MAX && prior < model.count; prior += 1)
                {
                    if (owners[prior] != 0 && groups[prior] == inputs[section].group && units[prior] == unit && model.lengths[prior] == length &&
                        (uint64_t)model.output_offset[prior] % alignment == 0 && memcmp(pointers[prior], inputs[section].bytes + begin, length) == 0)
                    {
                        winner = prior;
                        offset = model.output_offset[prior];
                    }
                }
                // A one-byte entry no suitable owner equals takes its block head's end where that offset meets its
                // alignment (#3044), or, in the walk model, the first earlier longer owner's that does.
                if (winner == UINT32_MAX && unit == 1 && walk)
                {
                    winner = foreign_strings_test_walk(&model, pointers, groups, units, owners, inputs[section].group, inputs[section].bytes + begin, length, alignment, &offset);
                }
                else if (winner == UINT32_MAX && unit == 1)
                {
                    winner = foreign_strings_test_head(test, &model, pointers, groups, units, owners, inputs[section].group, inputs[section].bytes + begin, length, alignment,
                                                       &offset);
                }
                offset = winner != UINT32_MAX ? offset : ((pool_size + alignment - 1) / alignment) * alignment;
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
                    owners[entry] = winner == UINT32_MAX ? 1 : 0;
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
    bool ready = foreign_strings_test_model(test, inputs, input_count, false, &test->model, &expected);
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
    foreign_strings_test_case(test, inputs, BUSTER_ARRAY_LENGTH(inputs), "groups, stable winners, effective alignment, padding and a suffix in the last group", 1);
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

// Both paths' pool size and every entry's output offset against values worked out by hand (#3044), beside the
// model's comparison, so that a rule the model and the producer would share in error still fails.
static void foreign_strings_test_known(ForeignStringsTest* test, const ForeignStringInput* inputs, uint32_t input_count, uint32_t size, const uint32_t* offsets,
                                       uint32_t count, const char* what, uint64_t detail)
{
    foreign_strings_test_case(test, inputs, input_count, what, detail);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        ForeignStrings result = { 0 };
        const ForeignStringsError error = foreign_strings_build(inputs, input_count, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, &test->paths[path], &result);
        bool same = error == FOREIGN_STRINGS_ERROR_NONE && result.size == size && result.count == count;
        for (uint32_t entry = 0; same && entry < count; entry += 1)
        {
            same = result.output_offset[entry] == offsets[entry];
        }
        foreign_strings_test_expect(test, same, what, detail * 2 + path);
        arena_pop_to(&test->paths[path], 0);
    }
}

// One-byte suffixes (#3044): a later entry takes the end of its block head, the first earlier entry of its group that
// ends with it, where that offset meets its alignment; never a later owner's bytes, another group's or a wider unit's.
static void foreign_strings_test_suffixes(ForeignStringsTest* test)
{
    static const uint8_t both[] = "compiler\0piler";
    static const uint8_t compiler[] = "compiler";
    static const uint8_t piler[] = "piler";
    static const uint8_t xpiler[] = "xpiler";
    static const uint8_t reversed[] = "piler\0compiler\0piler";
    static const uint8_t repeated[] = "compiler\0piler\0piler";
    static const uint8_t empty[] = "compiler\0";
    static const uint8_t wide_owner[] = { 'a', 0, 'b', 0, 0, 0 };
    static const uint8_t wide_suffix[] = { 'b', 0, 0, 0 };
    static const uint8_t zero[] = { 0 };
    const ForeignStringInput shared[] = {
        {
            .bytes = both,
            .size = sizeof(both),
            .group = 1,
        },
    };
    static const uint32_t shared_offsets[] = { 0, 3 };
    foreign_strings_test_known(test, shared, BUSTER_ARRAY_LENGTH(shared), 9, shared_offsets, 2, "piler shares compiler's last six bytes", 1000);
    const ForeignStringInput groups[] = {
        {
            .bytes = compiler,
            .size = sizeof(compiler),
            .group = 1,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 2,
        },
    };
    static const uint32_t groups_offsets[] = { 0, 9 };
    foreign_strings_test_known(test, groups, BUSTER_ARRAY_LENGTH(groups), 15, groups_offsets, 2, "a suffix in another group takes its own bytes", 1001);
    const ForeignStringInput widths[] = {
        {
            .bytes = wide_owner,
            .size = sizeof(wide_owner),
            .group = 3,
            .alignment_log2 = 1,
            .unit_log2 = 1,
        },
        {
            .bytes = wide_suffix,
            .size = sizeof(wide_suffix),
            .group = 3,
            .alignment_log2 = 1,
            .unit_log2 = 1,
        },
        {
            .bytes = zero,
            .size = sizeof(zero),
            .group = 3,
        },
        {
            .bytes = compiler,
            .size = sizeof(compiler),
            .group = 3,
        },
        {
            .bytes = zero,
            .size = sizeof(zero),
            .group = 3,
        },
    };
    // u"b" ends u"ab" and "\0" ends both, but only one-byte owners share their ends. The last "\0" equals the first
    // one's own copy, and an equal owner wins over compiler's end.
    static const uint32_t widths_offsets[] = { 0, 6, 10, 11, 10 };
    foreign_strings_test_known(test, widths, BUSTER_ARRAY_LENGTH(widths), 20, widths_offsets, 5, "two-byte units and two-byte owners share no suffix", 1002);
    const ForeignStringInput aligned[] = {
        {
            .bytes = both,
            .size = 9,
            .group = 4,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 4,
            .alignment_log2 = 1,
        },
    };
    static const uint32_t aligned_offsets[] = { 0, 10 };
    foreign_strings_test_known(test, aligned, BUSTER_ARRAY_LENGTH(aligned), 16, aligned_offsets, 2, "an alignment the shared offset misses takes new bytes", 1003);
    const ForeignStringInput later[] = {
        {
            .bytes = both,
            .size = 9,
            .group = 5,
        },
        {
            .bytes = xpiler,
            .size = sizeof(xpiler),
            .group = 5,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 5,
            .alignment_log2 = 1,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 5,
        },
    };
    // Only the block head is tried (#3044's block rule; the earlier rule walked on to xpiler, whose end at 10 meets the
    // alignment, for a pool of 16 and offsets { 0, 9, 10, 3 }). piler's head is compiler, whose end at 3 two-byte
    // alignment misses, so the aligned piler owns bytes at 16, and the last piler takes those by the equal probe.
    static const uint32_t later_offsets[] = { 0, 9, 16, 16 };
    foreign_strings_test_known(test, later, BUSTER_ARRAY_LENGTH(later), 22, later_offsets, 4, "only the block head's end is tried, not a later owner's", 1004);
    const ForeignStringInput order[] = {
        {
            .bytes = reversed,
            .size = sizeof(reversed),
            .group = 6,
        },
    };
    // The later, longer compiler never moves piler; the third entry equals piler's own copy.
    static const uint32_t order_offsets[] = { 0, 6, 0 };
    foreign_strings_test_known(test, order, BUSTER_ARRAY_LENGTH(order), 15, order_offsets, 3, "a later longer string moves no earlier one", 1005);
    const ForeignStringInput duplicates[] = {
        {
            .bytes = repeated,
            .size = sizeof(repeated),
            .group = 7,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 7,
            .alignment_log2 = 1,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 7,
        },
    };
    // A second piler resolves to the first's shared bytes; one that needs two-byte alignment takes its own copy, and a
    // later equal one takes that owner rather than compiler's end.
    static const uint32_t duplicates_offsets[] = { 0, 3, 3, 10, 10 };
    foreign_strings_test_known(test, duplicates, BUSTER_ARRAY_LENGTH(duplicates), 16, duplicates_offsets, 5, "duplicates of a suffix and of its aligned copy", 1006);
    const ForeignStringInput terminator[] = {
        {
            .bytes = empty,
            .size = sizeof(empty),
            .group = 0,
        },
    };
    static const uint32_t terminator_offsets[] = { 0, 8 };
    foreign_strings_test_known(test, terminator, BUSTER_ARRAY_LENGTH(terminator), 9, terminator_offsets, 2, "an empty string shares the terminator in group zero", 1007);
    // Eleven three-byte owners end at 2, 5, ..., 32. A later "\0" aligned to 32 tries only its block head, the
    // group's first one-byte entry "aa", whose end at 2 misses, so it owns a byte at 64 (#3044's block rule; the earlier
    // rule walked on to the end at 32, for a pool of 33).
    static const uint8_t owners[] = "aa\0ab\0ac\0ad\0ae\0af\0ag\0ah\0ai\0aj\0ak";
    const ForeignStringInput growth[] = {
        {
            .bytes = owners,
            .size = sizeof(owners),
            .group = 8,
        },
        {
            .bytes = zero,
            .size = sizeof(zero),
            .group = 8,
            .alignment_log2 = 5,
        },
    };
    static const uint32_t growth_offsets[] = { 0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 64 };
    foreign_strings_test_known(test, growth, BUSTER_ARRAY_LENGTH(growth), 65, growth_offsets, 12, "an empty string's head is its group's first entry, whose end misses",
                               1008);
}

// The producer's stages over a fixture's entries (#3044): the group and flag stages called directly, for how many
// entries the prefilter flags, then the build's own block-head call, for how many groups are sorted, how many one-byte
// entries they hold, how many heads the head pass and the empty strings write and whether heads were allocated. A
// flag or an allocation the rule does not need changes no pool, so only these counts show one.
typedef struct ForeignStringsTestCounts ForeignStringsTestCounts;
struct ForeignStringsTestCounts
{
    uint32_t sorted_groups;
    uint32_t considered;
    uint32_t flagged;
    uint32_t written;
    bool heads;
    bool valid;
};

static ForeignStringsTestCounts foreign_strings_test_counts(const ForeignStringInput* inputs, uint32_t input_count, Arena* arena)
{
    const uint64_t mark = arena->length;
    ForeignStrings strings = {
        .input_count = input_count,
    };
    bool valid = foreign_strings_count(inputs, input_count, FOREIGN_PATH_VECTOR, &strings.count) == FOREIGN_STRINGS_ERROR_NONE;
    strings.section_begin = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)input_count + 1);
    strings.input_offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)strings.count + 1);
    strings.lengths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)strings.count + 1);
    const uint8_t** pointers = BUSTER_ARENA_PUSH_ARRAY(arena, const uint8_t*, (uint64_t)strings.count + 1);
    uint32_t* sections = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)strings.count + 1);
    valid = valid && strings.section_begin != 0 && strings.input_offset != 0 && strings.lengths != 0 && pointers != 0 && sections != 0;
    ForeignStringsBlocks direct = {
        .arena = arena,
        .count = strings.count,
    };
    if (valid)
    {
        foreign_strings_entries(inputs, input_count, FOREIGN_PATH_VECTOR, &strings, pointers, sections);
        valid = foreign_strings_groups(inputs, input_count, &strings, &direct);
    }
    if (valid && direct.sorted_groups != 0)
    {
        valid = foreign_strings_flag(&strings, pointers, sections, &direct);
    }
    ForeignStringsBlocks blocks = {
        .arena = arena,
        .count = strings.count,
    };
    valid = valid && foreign_strings_block_heads(inputs, &strings, pointers, sections, &blocks);
    const ForeignStringsTestCounts counts = {
        .sorted_groups = blocks.sorted_groups,
        .considered = blocks.considered,
        .flagged = direct.flagged_count,
        .written = blocks.heads_written,
        .heads = blocks.heads != 0,
        .valid = valid && blocks.flagged_count == direct.flagged_count,
    };
    arena_pop_to(arena, mark);
    return counts;
}

static void foreign_strings_test_pin(ForeignStringsTest* test, const ForeignStringInput* inputs, uint32_t input_count, uint32_t sorted_groups, uint32_t considered,
                                     uint32_t flagged, uint32_t written, const char* what, uint64_t detail)
{
    const ForeignStringsTestCounts counts = foreign_strings_test_counts(inputs, input_count, &test->paths[0]);
    const bool same = counts.valid && counts.sorted_groups == sorted_groups && counts.considered == considered && counts.flagged == flagged &&
                      counts.written == written && counts.heads == (written != 0);
    if (!same && test->failed < 12)
    {
        printf("  string merging: %s: sorted groups %u, considered %u, flagged %u, heads written %u%s\n", what, counts.sorted_groups, counts.considered, counts.flagged,
               counts.written, counts.heads ? "" : " (none allocated)");
    }
    foreign_strings_test_expect(test, same, what, detail);
}

// Appends `count` content bytes and a terminator at `size`; returns the new size.
static uint32_t foreign_strings_test_put(uint8_t* bytes, uint32_t size, const uint8_t* content, uint32_t count)
{
    memcpy(bytes + size, content, count);
    bytes[size + count] = 0;
    return size + count + 1;
}

// A content of `count` bytes sharing `shared` trailing bytes with two longer ones that differ in the byte before:
// the shorter first shares nothing; the shorter last takes the first longer one's end, a byte in. The shared tails
// straddle the 8-byte keys and the 64-byte comparisons.
static void foreign_strings_test_tails(ForeignStringsTest* test)
{
    static const uint32_t shared[] = { 7, 8, 9, 15, 16, 17, 63, 64, 65 };
    uint8_t bytes[2 * BUSTER_ARRAY_LENGTH(shared)][3 * 68];
    ForeignStringInput inputs[2 * BUSTER_ARRAY_LENGTH(shared)];
    uint32_t offsets[6 * BUSTER_ARRAY_LENGTH(shared)];
    uint32_t size = 0;
    for (uint32_t index = 0; index < BUSTER_ARRAY_LENGTH(shared); index += 1)
    {
        const uint32_t count = shared[index];
        uint8_t longer[2][66];
        longer[0][0] = 'A';
        longer[1][0] = 'B';
        for (uint32_t byte = 0; byte < count; byte += 1)
        {
            longer[0][byte + 1] = (uint8_t)('a' + (byte * 7) % 26);
            longer[1][byte + 1] = longer[0][byte + 1];
        }
        uint32_t at = foreign_strings_test_put(bytes[2 * index], 0, longer[0] + 1, count);
        at = foreign_strings_test_put(bytes[2 * index], at, longer[0], count + 1);
        at = foreign_strings_test_put(bytes[2 * index], at, longer[1], count + 1);
        inputs[2 * index] = (ForeignStringInput){
            .bytes = bytes[2 * index],
            .size = at,
            .group = 2 * count,
        };
        at = foreign_strings_test_put(bytes[2 * index + 1], 0, longer[0], count + 1);
        at = foreign_strings_test_put(bytes[2 * index + 1], at, longer[1], count + 1);
        at = foreign_strings_test_put(bytes[2 * index + 1], at, longer[0] + 1, count);
        inputs[2 * index + 1] = (ForeignStringInput){
            .bytes = bytes[2 * index + 1],
            .size = at,
            .group = 2 * count + 1,
        };
        offsets[6 * index] = size;
        offsets[6 * index + 1] = size + count + 1;
        offsets[6 * index + 2] = size + 2 * count + 3;
        size += 3 * count + 5;
        offsets[6 * index + 3] = size;
        offsets[6 * index + 4] = size + count + 2;
        offsets[6 * index + 5] = size + 1;
        size += 2 * count + 4;
    }
    foreign_strings_test_known(test, inputs, BUSTER_ARRAY_LENGTH(inputs), size, offsets, BUSTER_ARRAY_LENGTH(offsets), "tails of 7 to 65 shared bytes, the shorter first and last",
                               1009);
    foreign_strings_test_pin(test, inputs, BUSTER_ARRAY_LENGTH(inputs), 18, 54, 54, 9, "eighteen sorted groups flag every tail and write a head for each shorter last", 1009);
}

// The bytes of a tie of 43 (1014): forty 13-byte strings m\x80 to m\xf5 ending abcdefghijk, zzabcdefghijk before the
// 21st, then zabcdefghijk and abcdefghijk; `bytes` holds 44 * 16.
static uint32_t foreign_strings_test_tie(uint8_t* bytes)
{
    static const uint8_t tail[] = "abcdefghijk";
    uint32_t size = 0;
    for (uint32_t index = 0; index < 40; index += 1)
    {
        uint8_t word[2 + sizeof(tail)] = {
            'z',
            'z',
        };
        memcpy(word + 2, tail, sizeof(tail) - 1);
        if (index == 20)
        {
            size = foreign_strings_test_put(bytes, size, word, sizeof(tail) + 1);
        }
        word[0] = 'm';
        word[1] = (uint8_t)(0x80 + 3 * index);
        size = foreign_strings_test_put(bytes, size, word, sizeof(tail) + 1);
    }
    size = foreign_strings_test_put(bytes, size, (const uint8_t*)"zabcdefghijk", sizeof(tail));
    size = foreign_strings_test_put(bytes, size, tail, sizeof(tail) - 1);
    return size;
}

// Ties past the first key bytes (#3044), each of more than BUSTER_FOREIGN_STRINGS_INSERTION records: one re-keyed
// once (one string ending another), one of a single content, one whose common tail is past two keys, one with a
// member at the known depth, one with a 300-byte common tail, a staircase that splits a few strings a level and one
// key that alone differs; and a tie past 64 bytes.
static void foreign_strings_test_ties(ForeignStringsTest* test)
{
    uint8_t rekeyed[44 * 16];
    uint32_t size = foreign_strings_test_tie(rekeyed);
    const ForeignStringInput tie[] = {
        {
            .bytes = rekeyed,
            .size = size,
            .group = 10,
        },
    };
    // Forty-one 14-byte owners, zzabcdefghijk the 21st; zabcdefghijk ends it, and abcdefghijk ends the first.
    uint32_t tie_offsets[43];
    for (uint32_t entry = 0; entry < 41; entry += 1)
    {
        tie_offsets[entry] = 14 * entry;
    }
    tie_offsets[41] = 281;
    tie_offsets[42] = 2;
    foreign_strings_test_known(test, tie, BUSTER_ARRAY_LENGTH(tie), 574, tie_offsets, BUSTER_ARRAY_LENGTH(tie_offsets), "a tie of 43 re-keyed once", 1014);
    foreign_strings_test_pin(test, tie, BUSTER_ARRAY_LENGTH(tie), 1, 43, 43, 2, "a tie of 43 writes the two extended classes' heads", 1014);
    uint8_t copies[41 * 18];
    size = foreign_strings_test_put(copies, 0, (const uint8_t*)"duplicate-literal", 17);
    size = foreign_strings_test_put(copies, size, (const uint8_t*)"literal", 7);
    for (uint32_t index = 1; index < 40; index += 1)
    {
        size = foreign_strings_test_put(copies, size, (const uint8_t*)"duplicate-literal", 17);
    }
    const ForeignStringInput equal[] = {
        {
            .bytes = copies,
            .size = size,
            .group = 11,
        },
    };
    // Forty copies tie as one content; literal, the second entry, ends the first copy.
    uint32_t equal_offsets[41] = { 0 };
    equal_offsets[1] = 10;
    foreign_strings_test_known(test, equal, BUSTER_ARRAY_LENGTH(equal), 18, equal_offsets, BUSTER_ARRAY_LENGTH(equal_offsets), "a tie of 40 equal copies", 1015);
    foreign_strings_test_pin(test, equal, BUSTER_ARRAY_LENGTH(equal), 1, 41, 41, 1, "a tie of 40 equal copies writes only the suffix's head", 1015);
    static const uint8_t ending[] = "UVWXYZabcdefghijklmn";
    uint8_t twice[42 * 24];
    uint8_t word[2 + sizeof(ending)] = {
        'R',
        0x8f,
    };
    memcpy(word + 2, ending, sizeof(ending) - 1);
    size = foreign_strings_test_put(twice, 0, word, sizeof(ending) + 1);
    for (uint32_t index = 0; index < 40; index += 1)
    {
        word[1] = (uint8_t)(0x80 + 3 * index);
        size = foreign_strings_test_put(twice, size, word + 1, sizeof(ending));
    }
    size = foreign_strings_test_put(twice, size, ending, sizeof(ending) - 1);
    const ForeignStringInput deeper[] = {
        {
            .bytes = twice,
            .size = size,
            .group = 12,
        },
    };
    // Forty 21-byte strings differ only in their first byte, past two keys: the tie's common tail is the 20 bytes they
    // share. R\x8f... comes first, so the sixth (\x8f...) takes its end at 1, and the 20-byte tail its end at 2.
    uint32_t deeper_offsets[42];
    deeper_offsets[0] = 0;
    for (uint32_t index = 0; index < 40; index += 1)
    {
        deeper_offsets[index + 1] = index < 5 ? 23 + 22 * index : 23 + 22 * (index - 1);
    }
    deeper_offsets[6] = 1;
    deeper_offsets[41] = 2;
    foreign_strings_test_known(test, deeper, BUSTER_ARRAY_LENGTH(deeper), 881, deeper_offsets, BUSTER_ARRAY_LENGTH(deeper_offsets), "a tie of 42 resolved past two keys", 1016);
    foreign_strings_test_pin(test, deeper, BUSTER_ARRAY_LENGTH(deeper), 1, 42, 42, 2, "a tie of 42 resolved past two keys writes two heads", 1016);
    uint8_t long_strings[4 * 141];
    uint8_t text[140];
    for (uint32_t byte = 0; byte < sizeof(text); byte += 1)
    {
        text[byte] = (uint8_t)('a' + (byte * 11) % 26);
    }
    size = foreign_strings_test_put(long_strings, 0, text, sizeof(text));
    text[5] = '#';
    size = foreign_strings_test_put(long_strings, size, text, sizeof(text));
    text[5] = (uint8_t)('a' + (5 * 11) % 26);
    size = foreign_strings_test_put(long_strings, size, text, sizeof(text));
    size = foreign_strings_test_put(long_strings, size, text + 70, 70);
    const ForeignStringInput longer[] = {
        {
            .bytes = long_strings,
            .size = size,
            .group = 13,
        },
    };
    // Two 140-byte strings share their last 134 bytes, past two 64-byte comparisons; the first's copy equals it, and its
    // last 70 bytes take its end at 70.
    static const uint32_t longer_offsets[] = { 0, 141, 0, 70 };
    foreign_strings_test_known(test, longer, BUSTER_ARRAY_LENGTH(longer), 282, longer_offsets, BUSTER_ARRAY_LENGTH(longer_offsets), "equal and nearly equal strings past 64 bytes",
                               1017);
    foreign_strings_test_pin(test, longer, BUSTER_ARRAY_LENGTH(longer), 1, 4, 4, 1, "strings past 64 bytes write one head", 1017);
    uint8_t exact[41 * 11];
    uint8_t ten[10] = {
        'm',
        0x80,
        'a',
        'b',
        'c',
        'd',
        'e',
        'f',
        'g',
        'h',
    };
    size = foreign_strings_test_put(exact, 0, ten, sizeof(ten));
    size = foreign_strings_test_put(exact, size, ten + 2, 8);
    for (uint32_t index = 1; index < 40; index += 1)
    {
        ten[1] = (uint8_t)(0x80 + 3 * index);
        size = foreign_strings_test_put(exact, size, ten, sizeof(ten));
    }
    const ForeignStringInput known[] = {
        {
            .bytes = exact,
            .size = size,
            .group = 22,
        },
    };
    // A tie of 41 re-keyed past 8 bytes, where abcdefgh has none left: its key is zero, so it sorts first and its head
    // is the first entry, whose end at 2 it takes.
    uint32_t known_offsets[41];
    known_offsets[0] = 0;
    known_offsets[1] = 2;
    for (uint32_t index = 1; index < 40; index += 1)
    {
        known_offsets[index + 1] = 11 * index;
    }
    foreign_strings_test_known(test, known, BUSTER_ARRAY_LENGTH(known), 440, known_offsets, BUSTER_ARRAY_LENGTH(known_offsets), "a re-keyed tie with a member at the known depth",
                               1023);
    foreign_strings_test_pin(test, known, BUSTER_ARRAY_LENGTH(known), 1, 41, 41, 1, "a re-keyed tie with a member at the known depth writes one head", 1023);
    // A deep tie: forty 301-byte strings share a 300-byte tail and differ in their first byte; an earlier 302-byte one
    // extends the sixth, and so does a last one that shares 301 bytes with the first. Its first level compares the 256
    // bytes past its first key, all equal, and its second finds the 300-byte common tail, the least of its members'
    // tails, which only members that differ there bound.
    uint8_t tail[300];
    for (uint32_t byte = 0; byte < sizeof(tail); byte += 1)
    {
        tail[byte] = (uint8_t)('a' + (byte * 7) % 26);
    }
    uint8_t deep_strings[303 + 40 * 302 + 303];
    uint8_t member[302];
    member[0] = 'z';
    member[1] = 0x80 + 3 * 5;
    memcpy(member + 2, tail, sizeof(tail));
    size = foreign_strings_test_put(deep_strings, 0, member, 302);
    for (uint32_t index = 0; index < 40; index += 1)
    {
        member[1] = (uint8_t)(0x80 + 3 * index);
        size = foreign_strings_test_put(deep_strings, size, member + 1, 301);
    }
    member[0] = 'q';
    member[1] = 0x80 + 3 * 5;
    size = foreign_strings_test_put(deep_strings, size, member, 302);
    const ForeignStringInput deep[] = {
        {
            .bytes = deep_strings,
            .size = size,
            .group = 24,
        },
    };
    // The sixth string takes the first's end at 1; the other 39 own 302 bytes each, and the last 303 after them.
    uint32_t deep_offsets[42];
    deep_offsets[0] = 0;
    for (uint32_t index = 0; index < 40; index += 1)
    {
        deep_offsets[index + 1] = 303 + 302 * (index < 5 ? index : index - 1);
    }
    deep_offsets[6] = 1;
    deep_offsets[41] = 303 + 302 * 39;
    foreign_strings_test_known(test, deep, BUSTER_ARRAY_LENGTH(deep), 303 + 302 * 39 + 303, deep_offsets, BUSTER_ARRAY_LENGTH(deep_offsets), "a tie with a 300-byte common tail",
                               1024);
    foreign_strings_test_pin(test, deep, BUSTER_ARRAY_LENGTH(deep), 1, 42, 42, 1, "a tie with a 300-byte common tail writes one head", 1024);
    // A staircase: the 8-byte tail abcdefgh first, then b, a^i and the tail for i = 0 to 79, each diverging from the
    // next 1 byte deeper, then a^20 and a^50 before the tail. The tail, first in its tie, is its shortest member; each
    // level splits off at most 8 strings and the next starts 8 bytes deeper, at its new first member's divergence.
    uint8_t stairs[9 + 800 + 3160 + 29 + 59];
    uint8_t step[90];
    memset(step, 'a', sizeof(step));
    memcpy(step + 82, "abcdefgh", 8);
    size = foreign_strings_test_put(stairs, 0, step + 82, 8);
    for (uint32_t index = 0; index < 80; index += 1)
    {
        step[81 - index] = 'b';
        size = foreign_strings_test_put(stairs, size, step + 81 - index, 9 + index);
        step[81 - index] = 'a';
    }
    size = foreign_strings_test_put(stairs, size, step + 62, 28);
    size = foreign_strings_test_put(stairs, size, step + 32, 58);
    const ForeignStringInput staircase[] = {
        {
            .bytes = stairs,
            .size = size,
            .group = 25,
        },
    };
    // Every b, a^i and the tail owns 10 + i bytes after the tail's 9; a^20 and a^50 before the tail take the ends of the
    // 21st and 51st, a byte in.
    uint32_t staircase_offsets[83];
    staircase_offsets[0] = 0;
    for (uint32_t index = 0; index < 80; index += 1)
    {
        staircase_offsets[index + 1] = 9 + 10 * index + index * (index - 1) / 2;
    }
    staircase_offsets[81] = staircase_offsets[21] + 1;
    staircase_offsets[82] = staircase_offsets[51] + 1;
    foreign_strings_test_known(test, staircase, BUSTER_ARRAY_LENGTH(staircase), 9 + 800 + 3160, staircase_offsets, BUSTER_ARRAY_LENGTH(staircase_offsets),
                               "a staircase tie headed by its shortest member", 1025);
    foreign_strings_test_pin(test, staircase, BUSTER_ARRAY_LENGTH(staircase), 1, 83, 83, 2, "a staircase tie writes the two inner strings' heads", 1025);
    // Forty 10-byte strings end with abcdefgh, and the sixth entry is fgh: its key is the only one that differs, in the
    // sixth record of the radix sort's first 8-record step, so it sorts first only if that step's upper half counts.
    uint8_t lane_strings[41 * 11];
    uint8_t lane_word[10] = {
        'P',
        0x80,
        'a',
        'b',
        'c',
        'd',
        'e',
        'f',
        'g',
        'h',
    };
    size = 0;
    for (uint32_t index = 0; index < 40; index += 1)
    {
        lane_word[1] = (uint8_t)(0x80 + 3 * index);
        size = foreign_strings_test_put(lane_strings, size, lane_word, sizeof(lane_word));
        if (index == 4)
        {
            size = foreign_strings_test_put(lane_strings, size, lane_word + 7, 3);
        }
    }
    const ForeignStringInput lane[] = {
        {
            .bytes = lane_strings,
            .size = size,
            .group = 26,
        },
    };
    // fgh takes the first string's end at 7; the forty own 11 bytes each.
    uint32_t lane_offsets[41];
    for (uint32_t index = 0; index < 40; index += 1)
    {
        lane_offsets[index < 5 ? index : index + 1] = 11 * index;
    }
    lane_offsets[5] = 7;
    foreign_strings_test_known(test, lane, BUSTER_ARRAY_LENGTH(lane), 440, lane_offsets, BUSTER_ARRAY_LENGTH(lane_offsets), "a key that differs only in a step's upper half",
                               1026);
    foreign_strings_test_pin(test, lane, BUSTER_ARRAY_LENGTH(lane), 1, 41, 41, 1, "a key that differs only in a step's upper half writes one head", 1026);
}

// Block heads worked out by hand (#3044), each also against the model, with the prefilter's and the head pass's counts
// pinned.
static void foreign_strings_test_heads(ForeignStringsTest* test)
{
    static const uint8_t later[] = "xpiler\0compiler\0piler";
    const ForeignStringInput unsorted[] = {
        {
            .bytes = later,
            .size = sizeof(later),
            .group = 9,
        },
    };
    // piler sorts first, but its head is xpiler, the last in sorted order and the first in input order.
    static const uint32_t unsorted_offsets[] = { 0, 7, 1 };
    foreign_strings_test_known(test, unsorted, BUSTER_ARRAY_LENGTH(unsorted), 16, unsorted_offsets, 3, "a head that is not first in sorted order", 1010);
    foreign_strings_test_pin(test, unsorted, BUSTER_ARRAY_LENGTH(unsorted), 1, 3, 3, 1, "a head that is not first in sorted order is written once", 1010);
    static const uint8_t piler[] = "piler";
    static const uint8_t compiler[] = "compiler";
    const ForeignStringInput joined[] = {
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 9,
        },
        {
            .bytes = compiler,
            .size = sizeof(compiler),
            .group = 9,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 9,
        },
    };
    // The second piler joins the first's class, whose head is its own first entry: the equal probe places it.
    static const uint32_t joined_offsets[] = { 0, 6, 0 };
    foreign_strings_test_known(test, joined, BUSTER_ARRAY_LENGTH(joined), 15, joined_offsets, 3, "a class's later member joins it", 1011);
    foreign_strings_test_pin(test, joined, BUSTER_ARRAY_LENGTH(joined), 1, 3, 3, 0, "a class headed by its own first entry writes no head", 1011);
    static const uint8_t abc[] = "abc";
    static const uint8_t xy[] = "xy\0";
    static const uint8_t empty_first[] = "\0ab\0";
    const ForeignStringInput empties[] = {
        {
            .bytes = abc,
            .size = sizeof(abc),
            .group = 1,
        },
        {
            .bytes = xy,
            .size = sizeof(xy),
            .group = 2,
        },
        {
            .bytes = empty_first,
            .size = sizeof(empty_first),
            .group = 3,
        },
    };
    // Group 2's empty string ends xy, its group's first entry, not group 1's abc; group 3's first entry is empty and
    // owns its byte, which its later empty string takes.
    static const uint32_t empties_offsets[] = { 0, 4, 6, 7, 8, 7 };
    foreign_strings_test_known(test, empties, BUSTER_ARRAY_LENGTH(empties), 11, empties_offsets, 6, "empty strings beside another group and first in theirs", 1012);
    foreign_strings_test_pin(test, empties, BUSTER_ARRAY_LENGTH(empties), 2, 5, 0, 2, "empty strings take their group's first entry unsorted", 1012);
    static const uint8_t xpiler[] = "xpiler";
    static const uint8_t iler[] = "iler\0compiler";
    static const uint8_t ler[] = "ler";
    static const uint8_t wide[] = { 'a', 0, 'b', 0, 0, 0 };
    static const uint8_t narrow[] = "xab\0ab\0b";
    const ForeignStringInput interleaved[] = {
        {
            .bytes = compiler,
            .size = sizeof(compiler),
            .group = 1,
        },
        {
            .bytes = xpiler,
            .size = sizeof(xpiler),
            .group = 0x100,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = UINT32_MAX,
        },
        {
            .bytes = iler,
            .size = sizeof(iler),
            .group = 0x10000,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 1,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 0x100,
        },
        {
            .bytes = compiler,
            .size = sizeof(compiler),
            .group = UINT32_MAX,
        },
        {
            .bytes = ler,
            .size = sizeof(ler),
            .group = 0x10000,
        },
        {
            .bytes = wide,
            .size = sizeof(wide),
            .group = 9,
            .alignment_log2 = 1,
            .unit_log2 = 1,
        },
        {
            .bytes = narrow,
            .size = sizeof(narrow),
            .group = 9,
        },
    };
    // Each group's suffix takes its own group's head: compiler's in 1, xpiler's in 0x100, none in UINT32_MAX (piler comes
    // first), iler's in 0x10000; group 9's two-byte u"ab" shares nothing, and its one-byte ab and b end xab.
    static const uint32_t interleaved_offsets[] = { 0, 9, 16, 22, 27, 3, 10, 36, 23, 46, 52, 53, 54 };
    foreign_strings_test_known(test, interleaved, BUSTER_ARRAY_LENGTH(interleaved), 56, interleaved_offsets, 13, "interleaved groups 1, 0x100, 0x10000, UINT32_MAX and mixed units",
                               1013);
    foreign_strings_test_pin(test, interleaved, BUSTER_ARRAY_LENGTH(interleaved), 5, 12, 12, 5, "five interleaved sorted groups", 1013);
    static const uint8_t chain[] = "abcdefghijkl\0bcdefghijkl\0cdefghijkl\0defghijkl\0efghijkl\0fghijkl\0ghijkl\0hijkl\0ijkl\0jkl\0kl\0l";
    const ForeignStringInput chained[] = {
        {
            .bytes = chain,
            .size = sizeof(chain),
            .group = 14,
        },
    };
    static const uint32_t chained_offsets[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    foreign_strings_test_known(test, chained, BUSTER_ARRAY_LENGTH(chained), 13, chained_offsets, 12, "a suffix chain of twelve, longest first", 1018);
    foreign_strings_test_pin(test, chained, BUSTER_ARRAY_LENGTH(chained), 1, 12, 12, 11, "a suffix chain writes every later entry's head", 1018);
    static const uint8_t high[] = { 0xff, 0x80, 'z', 0x80, 0xff, 0, 0x7f, 0x80, 0xff, 0, 0x80, 0xff, 0, 'z', 0x80, 0xff, 0 };
    const ForeignStringInput unsigned_bytes[] = {
        {
            .bytes = high,
            .size = sizeof(high),
            .group = 15,
        },
    };
    // Keys order 0xff above 0x80 above 0x7f: \x80\xff and z\x80\xff end the first string, not the second.
    static const uint32_t high_offsets[] = { 0, 6, 3, 2 };
    foreign_strings_test_known(test, unsigned_bytes, BUSTER_ARRAY_LENGTH(unsigned_bytes), 10, high_offsets, 4, "bytes 0x80 and 0xff order unsigned", 1019);
    foreign_strings_test_pin(test, unsigned_bytes, BUSTER_ARRAY_LENGTH(unsigned_bytes), 1, 4, 4, 2, "bytes 0x80 and 0xff write two heads", 1019);
    // The prefilter's boundaries: a 7-byte receiver whose only donor has 8 bytes, an 8-byte one whose donor has 9, a
    // short receiver whose only donor has 20, a long-level duplicate that writes no head, and receivers without donors
    // beside a long string seen once, which the long level must not flag.
    static const uint8_t seven[] = "abcdefgh\0bcdefgh";
    static const uint8_t eight[] = "abcdefghi\0bcdefghi";
    static const uint8_t twenty[] = "0123456789abcdefgxyz\0xyz";
    static const uint8_t duplicate[] = "duplicate-text\0duplicate-text\0x";
    static const uint8_t donorless[] = "ab\0cd\0efghijklm";
    static const uint8_t* const boundaries[] = {
        seven,
        eight,
        twenty,
        duplicate,
        donorless,
    };
    static const uint32_t boundary_sizes[] = {
        sizeof(seven),
        sizeof(eight),
        sizeof(twenty),
        sizeof(duplicate),
        sizeof(donorless),
    };
    static const uint32_t boundary_offsets[5][3] = {
        { 0, 1 },
        { 0, 1 },
        { 0, 17 },
        { 0, 0, 15 },
        { 0, 3, 6 },
    };
    static const uint32_t boundary_pools[] = { 9, 10, 21, 17, 16 };
    static const uint32_t boundary_counts[] = { 2, 2, 2, 3, 3 };
    static const uint32_t boundary_flagged[] = { 2, 2, 2, 2, 0 };
    static const uint32_t boundary_written[] = { 1, 1, 1, 0, 0 };
    for (uint32_t index = 0; index < BUSTER_ARRAY_LENGTH(boundaries); index += 1)
    {
        const ForeignStringInput boundary[] = {
            {
                .bytes = boundaries[index],
                .size = boundary_sizes[index],
                .group = 16,
            },
        };
        foreign_strings_test_known(test, boundary, 1, boundary_pools[index], boundary_offsets[index], boundary_counts[index], "a prefilter boundary", 1020 * 10 + index);
        foreign_strings_test_pin(test, boundary, 1, 1, boundary_counts[index], boundary_flagged[index], boundary_written[index], "a prefilter boundary's counts",
                                 1020 * 10 + index);
    }
    static const uint8_t first[] = "compiler\0piler";
    static const uint8_t repeated[] = "aaaa\0bbbb\0aaaa";
    static const uint8_t third[] = "xpiler\0ler";
    static const uint8_t last[] = "zz";
    const ForeignStringInput skipped[] = {
        {
            .bytes = first,
            .size = sizeof(first),
            .group = 1,
        },
        {
            .bytes = repeated,
            .size = sizeof(repeated),
            .group = 2,
        },
        {
            .bytes = third,
            .size = sizeof(third),
            .group = 3,
        },
        {
            .bytes = last,
            .size = sizeof(last),
            .group = 4,
        },
    };
    // Groups 2 and 4 have one length each, so only groups 1 and 3 are sorted.
    static const uint32_t skipped_offsets[] = { 0, 3, 9, 14, 9, 19, 22, 26 };
    foreign_strings_test_known(test, skipped, BUSTER_ARRAY_LENGTH(skipped), 29, skipped_offsets, 8, "two sorted groups beside one-length groups", 1021);
    foreign_strings_test_pin(test, skipped, BUSTER_ARRAY_LENGTH(skipped), 2, 4, 4, 2, "one-length groups are not considered", 1021);
    static const uint8_t lone[] = "";
    static const uint8_t pair[] = "piler\0compiler";
    const ForeignStringInput misaligned[] = {
        {
            .bytes = lone,
            .size = sizeof(lone),
            .group = 21,
        },
        {
            .bytes = pair,
            .size = sizeof(pair),
            .group = 20,
        },
        {
            .bytes = piler,
            .size = sizeof(piler),
            .group = 20,
            .alignment_log2 = 1,
        },
    };
    // The first piler lands at 1, which the later two-byte-aligned piler's equal probe misses; its head is that same
    // piler, not compiler, whose end at 10 the alignment would meet, so it owns bytes at 16.
    static const uint32_t misaligned_offsets[] = { 0, 1, 7, 16 };
    foreign_strings_test_known(test, misaligned, BUSTER_ARRAY_LENGTH(misaligned), 22, misaligned_offsets, 4, "an equal class's head is its first member, not a later extension",
                               1022);
    foreign_strings_test_pin(test, misaligned, BUSTER_ARRAY_LENGTH(misaligned), 1, 3, 3, 0, "an equal class headed by its first member writes no head", 1022);
    foreign_strings_test_tails(test);
    foreign_strings_test_ties(test);
}

// At effective alignment 1 the block head is the earlier rule's first longer owner (#3044's fact b): random suffix
// chains with every alignment 1 give the two models one pool, and both paths that pool.
static void foreign_strings_test_alignment_one(ForeignStringsTest* test, uint64_t seed)
{
    uint64_t state = (seed != 0 ? seed : 0x3141592653589793ull) ^ 0x2545f4914f6cdd1dull;
    uint8_t bases[3][24];
    for (uint32_t base = 0; base < 3; base += 1)
    {
        for (uint32_t byte = 0; byte + 1 < sizeof(bases[base]); byte += 1)
        {
            bases[base][byte] = (foreign_strings_test_random(&state) & 1) != 0 ? 'a' : 'b';
        }
        bases[base][sizeof(bases[base]) - 1] = 0;
    }
    for (uint32_t trial = 0; trial < 128; trial += 1)
    {
        uint8_t bytes[8][12 * sizeof(bases[0])];
        ForeignStringInput inputs[8] = { 0 };
        const uint32_t input_count = (uint32_t)(foreign_strings_test_random(&state) % 9);
        for (uint32_t section = 0; section < input_count; section += 1)
        {
            const uint32_t words = (uint32_t)(foreign_strings_test_random(&state) % 13);
            uint32_t size = 0;
            for (uint32_t word = 0; word < words; word += 1)
            {
                const uint32_t base = (uint32_t)(foreign_strings_test_random(&state) % 3);
                const uint32_t start = (uint32_t)(foreign_strings_test_random(&state) % sizeof(bases[base]));
                memcpy(bytes[section] + size, bases[base] + start, sizeof(bases[base]) - start);
                size += sizeof(bases[base]) - start;
            }
            inputs[section] = (ForeignStringInput){
                .bytes = bytes[section],
                .size = size,
                .group = (uint32_t)(foreign_strings_test_random(&state) % 3),
            };
        }
        const uint64_t mark = test->model.length;
        ForeignStrings block = { 0 };
        ForeignStrings walked = { 0 };
        const bool modelled = foreign_strings_test_model(test, inputs, input_count, false, &test->model, &block) &&
                              foreign_strings_test_model(test, inputs, input_count, true, &test->model, &walked);
        foreign_strings_test_expect(test, modelled && foreign_strings_test_same(&block, &walked), "at alignment 1 the block head is the first longer owner that fits", trial);
        arena_pop_to(&test->model, mark);
        foreign_strings_test_case(test, inputs, input_count, "alignment-1 suffix chains", trial + 7000);
    }
}

// A PROT_NONE page directly before the slice (#3044): a string of 0 to 9, 15, 16 or 17 content bytes starts at the
// slice's first byte, before or after a string one byte longer that ends with it, so the prefilter's and the sort's keys
// and the comparisons start their loads there.
static void foreign_strings_test_head_guards(ForeignStringsTest* test)
{
    static const uint32_t contents[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17 };
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = page >= 64 ? mmap(0, 2 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    foreign_strings_test_expect(test, ready, "head guard mapping", 0);
    for (uint32_t index = 0; ready && index < BUSTER_ARRAY_LENGTH(contents); index += 1)
    {
        const uint32_t count = contents[index];
        uint8_t word[18];
        word[0] = 'Q';
        for (uint32_t byte = 0; byte < count; byte += 1)
        {
            word[byte + 1] = (uint8_t)('a' + byte % 26);
        }
        for (uint32_t order = 0; ready && order < 2; order += 1)
        {
            uint8_t* bytes = mapping + page;
            ready = mprotect(bytes, page, PROT_READ | PROT_WRITE) == 0;
            uint32_t size = 0;
            if (ready && order == 0)
            {
                size = foreign_strings_test_put(bytes, 0, word + 1, count);
                size = foreign_strings_test_put(bytes, size, word, count + 1);
            }
            else if (ready)
            {
                size = foreign_strings_test_put(bytes, 0, word, count + 1);
                size = foreign_strings_test_put(bytes, size, word + 1, count);
            }
            ready = ready && mprotect(bytes, page, PROT_READ) == 0;
            if (ready)
            {
                const ForeignStringInput inputs[] = {
                    {
                        .bytes = bytes,
                        .size = size,
                        .group = 7,
                    },
                };
                foreign_strings_test_case(test, inputs, 1, "strings at a slice whose preceding page is unreadable", 8000 + count * 2 + order);
            }
        }
    }
    foreign_strings_test_expect(test, ready, "head guard protection changes", 0);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 2 * page);
    }
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
    // Every word ends one of three bases of two letters, so most words end earlier ones, at random groups and
    // alignments (#3044).
    uint8_t bases[3][24];
    for (uint32_t base = 0; base < 3; base += 1)
    {
        for (uint32_t byte = 0; byte + 1 < sizeof(bases[base]); byte += 1)
        {
            bases[base][byte] = (foreign_strings_test_random(&state) & 1) != 0 ? 'a' : 'b';
        }
        bases[base][sizeof(bases[base]) - 1] = 0;
    }
    for (uint32_t trial = 0; trial < 128; trial += 1)
    {
        uint8_t bytes[8][12 * sizeof(bases[0])];
        ForeignStringInput inputs[8] = { 0 };
        const uint32_t input_count = (uint32_t)(foreign_strings_test_random(&state) % 9);
        for (uint32_t section = 0; section < input_count; section += 1)
        {
            const uint32_t words = (uint32_t)(foreign_strings_test_random(&state) % 13);
            uint32_t size = 0;
            for (uint32_t word = 0; word < words; word += 1)
            {
                const uint32_t base = (uint32_t)(foreign_strings_test_random(&state) % 3);
                const uint32_t start = (uint32_t)(foreign_strings_test_random(&state) % sizeof(bases[base]));
                memcpy(bytes[section] + size, bases[base] + start, sizeof(bases[base]) - start);
                size += sizeof(bases[base]) - start;
            }
            inputs[section] = (ForeignStringInput){
                .bytes = bytes[section],
                .size = size,
                .group = (uint32_t)(foreign_strings_test_random(&state) % 3),
                .alignment_log2 = (uint8_t)(foreign_strings_test_random(&state) % 4),
            };
        }
        foreign_strings_test_case(test, inputs, input_count, "deterministic suffix chains, groups and alignments", trial + 5000);
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

// The producer's scratch starved at every room up to the first that suffices (#3044), each room poisoned first, so a
// column read before it is written reads poison rather than an earlier build's values. Each smaller room must fail
// with MEMORY and leave the result, the output arena's mark and the scratch length as they were; the first room that
// suffices must build the unstarved pool.
static void foreign_strings_test_starve(ForeignStringsTest* test, const ForeignStringInput* inputs, uint32_t input_count, uint32_t size, uint32_t count, uint32_t last,
                                        uint64_t limit, uint64_t detail)
{
    for (uint32_t way = 0; way < 2; way += 1)
    {
        Arena* arena = &test->paths[way];
        const ForeignPath path = way == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR;
        const uint64_t base = (detail * 2 + way) * 100000;
        ForeignStrings unstarved = { 0 };
        const bool built = foreign_strings_build(inputs, input_count, path, arena, &unstarved) == FOREIGN_STRINGS_ERROR_NONE;
        foreign_strings_test_expect(test, built && unstarved.size == size && unstarved.count == count && unstarved.output_offset[count - 1] == last,
                                    "unstarved scratch builds the fixture's pool", base);
        const Scratch scratch = scratch_begin(arena);
        Arena* starved = scratch.arena;
        const uint64_t capacity = starved->capacity;
        const uint64_t mark = arena->length;
        uint64_t room = 0;
        bool sufficed = false;
        while (built && !sufficed && room <= limit)
        {
            uint8_t* poison = arena_push(starved, room + 1, 1);
            foreign_strings_test_expect(test, poison != 0, "poison the starved scratch room", base + room);
            if (poison != 0)
            {
                memset(poison, 0x40, room + 1);
            }
            arena_pop_to(starved, scratch.length);
            ForeignStrings output;
            ForeignStrings saved;
            memset(&output, 0xa5, sizeof(output));
            memcpy(&saved, &output, sizeof(output));
            starved->capacity = scratch.length + room;
            const ForeignStringsError error = foreign_strings_build(inputs, input_count, path, arena, &output);
            starved->capacity = capacity;
            sufficed = error == FOREIGN_STRINGS_ERROR_NONE;
            if (sufficed)
            {
                foreign_strings_test_expect(test, foreign_strings_test_same(&output, &unstarved) && starved->length == scratch.length,
                                            "the first sufficient scratch builds the unstarved pool", base + room);
            }
            else
            {
                foreign_strings_test_expect(test, error == FOREIGN_STRINGS_ERROR_MEMORY && memcmp(&output, &saved, sizeof(output)) == 0 && arena->length == mark &&
                                                  starved->length == scratch.length,
                                            "starved scratch fails with MEMORY and preserves result, mark and scratch", base + room);
                room += 1;
            }
        }
        foreign_strings_test_expect(test, sufficed && room != 0, "a bounded scratch room builds the fixture", base);
        scratch_end(scratch);
        arena_pop_to(arena, 0);
    }
}

// Two fixtures starved (#3044). The 1008 fixture flags nothing: it reaches the entry columns, the probe table, the
// groups, the short bitmaps, the flags, the flagged list and its empty string's head, within 4096 bytes. The 1014 tie
// with a short receiver, "jk", which ends every string, reaches the long and short bitmaps, the flags, the flagged list,
// the records, the common suffixes, the work list (a tie of 44, more than 32) and the head pass's heads, within 8192.
static void foreign_strings_test_scratch(ForeignStringsTest* test)
{
    static const uint8_t owners[] = "aa\0ab\0ac\0ad\0ae\0af\0ag\0ah\0ai\0aj\0ak";
    static const uint8_t zero[] = { 0 };
    const ForeignStringInput growth[] = {
        {
            .bytes = owners,
            .size = sizeof(owners),
            .group = 8,
        },
        {
            .bytes = zero,
            .size = sizeof(zero),
            .group = 8,
            .alignment_log2 = 5,
        },
    };
    foreign_strings_test_starve(test, growth, BUSTER_ARRAY_LENGTH(growth), 65, 12, 64, 4096, 0);
    uint8_t rekeyed[44 * 16];
    const uint32_t size = foreign_strings_test_tie(rekeyed);
    static const uint8_t receiver[] = "jk";
    const ForeignStringInput tie[] = {
        {
            .bytes = rekeyed,
            .size = size,
            .group = 10,
        },
        {
            .bytes = receiver,
            .size = sizeof(receiver),
            .group = 10,
        },
    };
    foreign_strings_test_pin(test, tie, BUSTER_ARRAY_LENGTH(tie), 1, 44, 44, 3, "the starved tie's counts", 1);
    foreign_strings_test_starve(test, tie, BUSTER_ARRAY_LENGTH(tie), 574, 44, 11, 8192, 1);
}

static void foreign_strings_test_link(ForeignStringsTest* test)
{
    static const uint8_t first_source[] = { 'p', 'r', 'e', 'f', 'i', 'x', 0, 's', 'a', 'm', 'e', 0, 't', 'a', 'i', 'l', 0 };
    static const uint8_t second_source[] = { 's', 'a', 'm', 'e', 0, 'x', 0, 's', 'a', 'm', 'e', 0 };
    // second's "x" shares the end of first's "prefix" (#3044).
    static const uint8_t wanted_pool[] = { 'p', 'r', 'e', 'f', 'i', 'x', 0, 's', 'a', 'm', 'e', 0, 't', 'a', 'i', 'l', 0 };
    static const uint32_t second_map[] = { 7, 8, 9, 10, 11, 5, 6, 7, 8, 9, 10, 11 };
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
            static const int64_t wanted_addends[] = { 0, 0, -2, 0, -1, 2, 2, 11, 1ll << 40, -4, -1, 4 };
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
        foreign_strings_test_suffixes(&test);
        foreign_strings_test_heads(&test);
        foreign_strings_test_random_cases(&test, seed);
        foreign_strings_test_alignment_one(&test, seed);
        foreign_strings_test_guards(&test);
        foreign_strings_test_head_guards(&test);
        foreign_strings_test_failures(&test);
        foreign_strings_test_scratch(&test);
        foreign_strings_test_link(&test);
    }
    arena_release(&test.model);
    arena_release(&test.paths[0]);
    arena_release(&test.paths[1]);
    printf("string merging: %llu checks, %llu failed (independent model, block heads, pinned counts, every-byte maps, collision groups, alignment, read-only guards and rollback)\n",
           (unsigned long long)test.checked, (unsigned long long)test.failed);
    return test.failed == 0;
}
