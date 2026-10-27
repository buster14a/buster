// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Independent pairwise compatibility oracle, exact-call scaling bounds, collisions, owned tails and exhaustion
// for the shared _Generic index. Source-reader fixtures live beside the other constant-expression selections.

static void check_test_generic_masks(CheckTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = memory != MAP_FAILED && mprotect(memory + page, (size_t)page, PROT_NONE) == 0;
    check_test_expect(test, ready, "generic candidate guard page is available");
    if (ready)
    {
        TypeId* keys = (TypeId*)(memory + page) - BUSTER_CHECK_GENERIC_GROUP;
        for (uint32_t length = 0; length <= BUSTER_CHECK_GENERIC_GROUP; length += 1)
        {
            for (uint32_t lane = 0; lane < BUSTER_CHECK_GENERIC_GROUP; lane += 1)
            {
                keys[lane] = lane < length ? (lane % 3 == 0 ? 7 : 9) : 0;
            }
            for (TypeId key = 7; key <= 10; key += 1)
            {
                const uint32_t scalar = check_generic_masks(keys, key, true);
                const uint32_t vector = check_generic_masks(keys, key, false);
                check_test_expect(test, scalar == vector && (scalar >> BUSTER_CHECK_GENERIC_GROUP) == (UINT16_MAX & (UINT32_C(0xffff) << length)),
                                  "generic scalar/vector masks agree through the last owned group");
            }
        }
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, (size_t)page * 2);
    }
}

static __attribute__((noinline)) void check_test_generic_index(CheckTest* test)
{
    CheckTestTypes storage = { 0 };
    Arena scratch = arena_reserve(UINT64_C(1) << 22);
    Parser parser = { 0 };
    CheckInput output = { 0 };
    CheckInputReader reader = {
        .types = &storage.table,
        .parser = &parser,
        .scratch = &scratch,
        .output = &output,
    };
    bool ready = check_test_types_open(&storage) && scratch.base != NULL;
    TypeId records[257] = { 0 };
    TypeId pointers[257] = { 0 };
    const TypeMember member = { .name = 1, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(records); i += 1)
    {
        records[i] = type_tag(&storage.table, i % 2 == 0 ? BUSTER_TYPE_KIND_STRUCT : BUSTER_TYPE_KIND_UNION, i + 1);
        ready = records[i] != 0 && type_complete_record(&storage.table, records[i], &member, 1, (TypeRecordAttributes){ 0 });
        pointers[i] = ready ? type_pointer(&storage.table, records[i]) : 0;
        ready &= pointers[i] != 0;
    }
    check_test_expect(test, ready, "generic nominal type family is constructed");
    const uint32_t counts[] = { 0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 127, 257 };
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        parser.options.scalar = scalar != 0;
        for (uint32_t family = 0; family < 2; family += 1)
        {
            const TypeId* types = family == 0 ? records : pointers;
            for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(counts); c += 1)
            {
                const uint32_t count = counts[c];
                const uint32_t selected = count / 2;
                const uint64_t mark = scratch.length;
                CheckGenericSet set;
                bool valid = check_generic_initialize(&reader, &set, count * 4, types[selected], 1);
                uint32_t matched = 0;
                for (uint32_t i = 0; valid && i < count; i += 1)
                {
                    bool matches = false;
                    valid = check_generic_association(&reader, &set, types[i], &matches);
                    check_test_expect(test, matches == (i == selected), "generic nominal family selects exactly its control");
                    matched += matches;
                }
                check_test_expect(test, valid && !output.resource && matched == (count != 0) && set.duplicate_queries == 0 &&
                                       set.control_queries == (count != 0), "generic distinct records and pointers perform no pairwise duplicate queries");
                if (valid && count != 0)
                {
                    bool matches = false;
                    check_test_expect(test, !check_generic_association(&reader, &set, types[count - 1], &matches) && set.duplicate_queries == 1,
                                      "generic duplicate at the end of a nominal family is rejected");
                }
                arena_pop_to(&scratch, mark);
            }
        }
        // Twenty incompatible pointer depths share a key. They fill the last group and wrap to the first group;
        // the repeated last depth must still visit all nineteen earlier candidates before its equal type.
        CheckGenericSet collided;
        bool valid = check_generic_initialize(&reader, &collided, 80, BUSTER_TYPE_INT, 1);
        TypeId nested = 0;
        for (uint32_t i = 0; nested == 0 && i < BUSTER_ARRAY_LENGTH(records); i += 1)
        {
            uint32_t hash = records[i] * UINT32_C(0x9e3779b9);
            hash ^= hash >> 16;
            if ((hash & 48) == 48)
            {
                nested = records[i];
            }
        }
        valid &= nested != 0 && collided.capacity == 64;
        for (uint32_t i = 0; valid && i < 20; i += 1)
        {
            nested = type_pointer(&storage.table, nested);
            bool matches = false;
            valid = nested != 0 && check_generic_association(&reader, &collided, nested, &matches) && !matches;
        }
        bool matches = false;
        check_test_expect(test, valid && !check_generic_association(&reader, &collided, nested, &matches) &&
                               collided.duplicate_queries == 210 && collided.control_queries == 0,
                          "generic collision chains retain candidate source order through wraparound");
        arena_pop_to(&scratch, 0);
    }
    if (ready)
    {
        const TypeId enumeration = type_tag(&storage.table, BUSTER_TYPE_KIND_ENUM, 900);
        ready = type_enum_complete(&storage.table, enumeration, BUSTER_TYPE_UINT, 0);
        const TypeId parameter = BUSTER_TYPE_DOUBLE;
        const TypeId record_pointer = pointers[0];
        const TypeId union_type = type_tag(&storage.table, BUSTER_TYPE_KIND_UNION, 901);
        const TypeMember union_members[] = {
            { .name = 1, .type = record_pointer, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
            { .name = 2, .type = pointers[1], .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        };
        ready = ready && type_complete_record(&storage.table, union_type, union_members, 2, (TypeRecordAttributes){ 0 }) &&
                type_mark_transparent_union(&storage.table, union_type).status == TYPE_TRANSPARENT_UNION_APPLIED;
        const TypeId family[] = {
            BUSTER_TYPE_INT,
            BUSTER_TYPE_UINT,
            enumeration,
            type_qualify(&storage.table, BUSTER_TYPE_UINT, BUSTER_TYPE_CONST),
            records[0],
            records[1],
            type_aligned(&storage.table, records[0], 4),
            record_pointer,
            type_pointer(&storage.table, type_qualify(&storage.table, records[0], BUSTER_TYPE_CONST)),
            type_pointer(&storage.table, type_array(&storage.table, records[0], 2)),
            type_pointer(&storage.table, type_incomplete_array(&storage.table, records[0])),
            type_atomic(&storage.table, records[0]),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, NULL, 0, 0)),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, &parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE)),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, &union_type, 1, BUSTER_TYPE_FLAG_PROTOTYPE)),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, &record_pointer, 1, BUSTER_TYPE_FLAG_PROTOTYPE)),
            type_vector(&storage.table, BUSTER_TYPE_INT, 4, false),
            type_vector(&storage.table, BUSTER_TYPE_INT, 4, true),
        };
        uint32_t activations[512] = { 0 };
        activations[storage.table.types.index[type_index(union_type)]] = 10;
        reader.transparent_unions = activations;
        for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
        {
            parser.options.scalar = scalar != 0;
            for (uint32_t position = 9; position <= 10; position += 1)
            {
                for (uint32_t left = 0; left < BUSTER_ARRAY_LENGTH(family); left += 1)
                {
                    for (uint32_t right = 0; right < BUSTER_ARRAY_LENGTH(family); right += 1)
                    {
                        const TypeId a = family[left];
                        const TypeId b = family[right];
                        const TypeAnswer expected = type_compatible_at(&storage.table, a, b, activations, storage.table.tags.count, position);
                        CheckGenericSet set;
                        bool matches = false;
                        bool valid = a != 0 && b != 0 && expected != TYPE_ANSWER_FULL && check_generic_initialize(&reader, &set, 8, b, position);
                        valid = valid && check_generic_association(&reader, &set, a, &matches);
                        check_test_expect(test, valid && matches == (expected == TYPE_ANSWER_YES), "generic control filtering agrees with independent exact compatibility");
                        check_test_expect(test, valid && check_generic_association(&reader, &set, b, &matches) == (expected == TYPE_ANSWER_NO),
                                          "generic duplicate filtering preserves enums, qualifiers, arrays, functions and source-time transparent unions");
                    }
                }
            }
        }
        const uint64_t capacity = scratch.capacity;
        scratch.capacity = 31;
        CheckGenericSet full;
        check_test_expect(test, !check_generic_initialize(&reader, &full, 4096, BUSTER_TYPE_INT, 1) && output.resource,
                          "generic scratch exhaustion is explicit before publishing any association");
        scratch.capacity = capacity;
        arena_pop_to(&scratch, 0);
    }
    check_test_expect(test, ready, "generic ambiguous compatibility fixtures are constructed");
    arena_release(&scratch);
    check_test_types_close(&storage);
    check_test_generic_masks(test);
}

static __attribute__((noinline)) void check_test_generic_sources(CheckTest* test, const char* clang)
{
    char directory[] = "/tmp/buster-generic-index-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char source[32768];
    char path[160];
    const char* arguments[] = { "-std=gnu17" };
    for (uint32_t scenario = 0; ready && scenario < 6; scenario += 1)
    {
        const bool constant = (scenario & 1) != 0;
        const bool pointer = scenario < 2 || scenario >= 4;
        const bool duplicate = scenario >= 4;
        uint32_t length = 0;
        for (uint32_t i = 0; i < 257; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "struct G%u%s;\n", i, pointer ? "" : "{int x;}");
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%s_Generic(%s,", constant ? "enum{N=" : "int f(struct G0*x){return ",
                                     pointer ? "(struct G0*)0" : "*(struct G0*)0");
        for (uint32_t i = 0; i < 257; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "struct G%u%s:%u,", i, pointer ? "*" : "", i);
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%sdefault:1/0)%s\n", duplicate ? "struct G0*:999," : "",
                                     constant ? "};_Static_assert(N==0,\"selected\");" : ";}");
        ready = length < sizeof(source);
        snprintf(path, sizeof(path), "%s/%u.c", directory, scenario);
        ready = ready && parse_test_write(path, source, length);
        char* paths[] = { path };
        ParseTestClang outcome = { 0 };
        ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome);
        check_test_expect(test, ready && outcome.rejected == duplicate, "generated generic nominal family agrees with Clang");
        if (ready)
        {
            const CheckSourceFixture fixture = {
                .name = "generated 257-association generic family",
                .source = source,
                .rejected = duplicate,
            };
            check_test_source(test, &fixture, path, &outcome);
        }
        unlink(path);
    }
    check_test_expect(test, ready, "generated generic source fixtures run without resource failures");
    if (directory[0] != 0)
    {
        rmdir(directory);
    }
}
