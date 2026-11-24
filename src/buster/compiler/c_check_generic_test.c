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
                // The definition: a lane's bit where its key is `key`, and where it's empty in the high half.
                uint32_t expected = 0;
                for (uint32_t lane = 0; lane < BUSTER_CHECK_GENERIC_GROUP; lane += 1)
                {
                    expected |= (uint32_t)(keys[lane] == key) << lane | (uint32_t)(keys[lane] == 0) << (BUSTER_CHECK_GENERIC_GROUP + lane);
                }
                const uint32_t masks = check_generic_masks(keys, key);
                check_test_expect(test, masks == expected && (masks >> BUSTER_CHECK_GENERIC_GROUP) == (UINT16_MAX & (UINT32_C(0xffff) << length)),
                                  "generic masks are each lane's match and emptiness through the last owned group");
            }
        }
        // Key 1 starts in the last group of a 32-slot directory. Both a hit and a miss must wrap
        // to its first group without probing into the guard page.
        uint32_t* slots = keys - BUSTER_CHECK_GENERIC_GROUP;
        CheckGenericSet directory = {
            .keys = slots,
            .capacity = 2 * BUSTER_CHECK_GENERIC_GROUP,
        };
        for (uint32_t i = 0; i < directory.capacity; i += 1)
        {
            slots[i] = i < 3 || i >= BUSTER_CHECK_GENERIC_GROUP ? 9 : 0;
        }
        slots[2] = 1;
        check_test_expect(test, check_generic_slot(&directory, 1) == 2, "generic directory finds a wrapped key");
        slots[2] = 9;
        check_test_expect(test, check_generic_slot(&directory, 1) == 3, "generic directory inserts beyond a wrapped collision");
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
    const TypeCapacity capacity = {
        .types = 8192,
        .parameters = 8192,
        .tags = 1024,
        .members = 4096,
    };
    bool ready = check_test_types_open_with(&storage, capacity) && scratch.base != NULL;
    TypeId records[257] = { 0 };
    TypeId pointers[257] = { 0 };
    TypeId returns[257] = { 0 };
    TypeId vectors[257] = { 0 };
    TypeId bitints[257] = { 0 };
    TypeId enums[257] = { 0 };
    TypeId prototypes[257] = { 0 };
    TypeId integer_pointers[257] = { 0 };
    const TypeMember member = {
        .name = 1,
        .type = BUSTER_TYPE_INT,
        .bit_width = BUSTER_TYPE_NOT_BIT_FIELD
    };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(records); i += 1)
    {
        records[i] = type_tag(&storage.table, i % 2 == 0 ? BUSTER_TYPE_KIND_STRUCT : BUSTER_TYPE_KIND_UNION, i + 1);
        ready = records[i] != 0 && type_complete_record(&storage.table, records[i], &member, 1, (TypeRecordAttributes){ 0 });
        pointers[i] = ready ? type_pointer(&storage.table, records[i]) : 0;
        ready &= pointers[i] != 0;
        returns[i] = ready ? type_pointer(&storage.table, type_function(&storage.table, records[i], NULL, 0, BUSTER_TYPE_FLAG_PROTOTYPE)) : 0;
        vectors[i] = ready ? type_vector(&storage.table, BUSTER_TYPE_INT, i + 1, true) : 0;
        bitints[i] = ready ? type_bit_int(&storage.table, i + 2, false) : 0;
        enums[i] = type_tag(&storage.table, BUSTER_TYPE_KIND_ENUM, 1000 + i);
        ready = ready && enums[i] != 0 && type_enum_complete(&storage.table, enums[i], BUSTER_TYPE_UINT, 0);
        prototypes[i] = type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, pointers + i, 1, BUSTER_TYPE_FLAG_PROTOTYPE));
        integer_pointers[i] = type_pointer(&storage.table, i == 0 ? BUSTER_TYPE_INT : integer_pointers[i - 1]);
        ready &= returns[i] != 0 && vectors[i] != 0 && bitints[i] != 0 && prototypes[i] != 0 && integer_pointers[i] != 0;
    }
    check_test_expect(test, ready, "generic nominal type family is constructed");
    const uint32_t counts[] = {
        0,
        1,
        7,
        8,
        9,
        15,
        16,
        17,
        31,
        32,
        33,
        127,
        257
    };
    const TypeId* families[] = {
        records,
        pointers,
        returns,
        vectors,
        bitints,
        enums,
        prototypes,
        integer_pointers,
    };
    if (ready)
    {
        for (uint32_t family = 0; family < BUSTER_ARRAY_LENGTH(families); family += 1)
        {
            const TypeId* types = families[family];
            for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(counts); c += 1)
            {
                const uint32_t count = counts[c];
                const uint32_t selected = count / 2;
                const uint64_t mark = scratch.length;
                CheckGenericSet set;
                bool valid = check_generic_initialize(&reader, &set, count, types[selected], 1);
                uint32_t matched = 0;
                for (uint32_t i = 0; valid && i < count; i += 1)
                {
                    bool matches = false;
                    valid = check_generic_association(&reader, &set, types[i], &matches);
                    check_test_expect(test, matches == (i == selected), "generic nominal family selects exactly its control");
                    matched += matches;
                }
                // Tiny selections use the single inline vector. Enums and equal-return prototypes
                // deliberately share their coarse key there. Larger selections use the relation index.
                const bool shared = count <= 16 && (types == enums || types == prototypes);
                const uint32_t prior = shared && count != 0 ? count * (count - 1) / 2 : 0;
                const uint32_t controls = shared ? count : (count != 0);
                check_test_expect(test, valid && !output.resource && matched == (count != 0) && set.duplicate_queries == prior &&
                                       set.control_queries == controls, "distinct generic families avoid pairwise exact queries beyond the inline vector");
                if (valid && count != 0)
                {
                    bool matches = false;
                    check_test_expect(test, !check_generic_association(&reader, &set, types[count - 1], &matches) &&
                                           set.duplicate_queries == prior + (shared ? count : 1),
                                      "generic duplicate at the end of a nominal family is rejected");
                }
                arena_pop_to(&scratch, mark);
            }
        }
        // Distinct array bounds can both match an incomplete array. Their common list crosses a vector's
        // worth of rows. With the duplicate last, its exact match follows every preceding, unequal bound.
        CheckGenericSet collided;
        bool valid = check_generic_initialize(&reader, &collided, 21, BUSTER_TYPE_INT, 1);
        TypeId nested = 0;
        for (uint32_t i = 0; valid && i < 20; i += 1)
        {
            nested = type_pointer(&storage.table, type_array(&storage.table, BUSTER_TYPE_INT, i + 1));
            bool matches = false;
            valid = nested != 0 && check_generic_association(&reader, &collided, nested, &matches) && !matches;
        }
        bool matches = false;
        check_test_expect(test, valid && !check_generic_association(&reader, &collided, nested, &matches) &&
                               collided.duplicate_queries == 210 && collided.control_queries == 0,
                          "generic compatible-array candidates retain source order");
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
            {
                .name = 1,
                .type = record_pointer,
                .bit_width = BUSTER_TYPE_NOT_BIT_FIELD
            },
            {
                .name = 2,
                .type = pointers[1],
                .bit_width = BUSTER_TYPE_NOT_BIT_FIELD
            },
        };
        ready = ready && type_complete_record(&storage.table, union_type, union_members, 2, (TypeRecordAttributes){ 0 });
        const TypeId tail_union[] = { BUSTER_TYPE_INT, union_type };
        const TypeId tail_member[] = { BUSTER_TYPE_INT, record_pointer };
        const TypeId union_fn = type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, tail_union, 2, BUSTER_TYPE_FLAG_PROTOTYPE));
        const TypeId member_fn = type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, tail_member, 2, BUSTER_TYPE_FLAG_PROTOTYPE));
        CheckGenericSet marked;
        bool selected = false;
        ready = ready && union_fn != 0 && member_fn != 0 && check_generic_initialize(&reader, &marked, 17, member_fn, 10) &&
                check_generic_association(&reader, &marked, union_fn, &selected) && !selected &&
                type_mark_transparent_union(&storage.table, union_type).status == TYPE_TRANSPARENT_UNION_APPLIED;
        check_test_expect(test, ready && !check_generic_association(&reader, &marked, member_fn, &selected),
                          "a union marked transparent after insertion still reaches its compatible parameter");
        arena_pop_to(&scratch, 0);
        const TypeId family[] = {
            BUSTER_TYPE_INT,
            BUSTER_TYPE_UINT,
            enumeration,
            enums[0],
            BUSTER_TYPE_FLOAT,
            BUSTER_TYPE_DOUBLE,
            BUSTER_TYPE_COMPLEX_FLOAT,
            BUSTER_TYPE_COMPLEX_DOUBLE,
            bitints[29],
            bitints[30],
            type_bit_int(&storage.table, 32, true),
            type_qualify(&storage.table, BUSTER_TYPE_UINT, BUSTER_TYPE_CONST),
            records[0],
            records[1],
            type_aligned(&storage.table, records[0], 4),
            record_pointer,
            type_pointer(&storage.table, type_qualify(&storage.table, records[0], BUSTER_TYPE_CONST)),
            type_pointer(&storage.table, type_array(&storage.table, records[0], 2)),
            type_pointer(&storage.table, type_incomplete_array(&storage.table, records[0])),
            type_atomic(&storage.table, records[0]),
            union_fn,
            member_fn,
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, &parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC)),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, &enumeration, 1, BUSTER_TYPE_FLAG_PROTOTYPE)),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, NULL, 0, 0)),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, &parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE)),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, &union_type, 1, BUSTER_TYPE_FLAG_PROTOTYPE)),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_INT, &record_pointer, 1, BUSTER_TYPE_FLAG_PROTOTYPE)),
            type_vector(&storage.table, BUSTER_TYPE_INT, 4, false),
            type_vector(&storage.table, BUSTER_TYPE_INT, 4, true),
            type_vector(&storage.table, BUSTER_TYPE_FLOAT, 4, false),
            type_vector(&storage.table, BUSTER_TYPE_INT, 8, true),
            type_pointer(&storage.table, type_function(&storage.table, enumeration, NULL, 0, BUSTER_TYPE_FLAG_PROTOTYPE)),
            type_pointer(&storage.table, type_function(&storage.table, BUSTER_TYPE_UINT, NULL, 0, BUSTER_TYPE_FLAG_PROTOTYPE)),
            type_pointer(&storage.table, type_incomplete_array(&storage.table, BUSTER_TYPE_INT)),
            type_pointer(&storage.table, type_array(&storage.table, BUSTER_TYPE_INT, 8)),
        };
        uint32_t activations[1024] = { 0 };
        activations[storage.table.types.index[type_index(union_type)]] = 10;
        reader.transparent_unions = activations;
        for (uint32_t size = 2; ready && size <= 17; size += 15)
        {
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
                        bool valid = a != 0 && b != 0 && expected != TYPE_ANSWER_FULL && check_generic_initialize(&reader, &set, size, b, position);
                        valid = valid && check_generic_association(&reader, &set, a, &matches);
                        check_test_expect(test, valid && matches == (expected == TYPE_ANSWER_YES), "generic control filtering agrees with independent exact compatibility");
                        check_test_expect(test, valid && check_generic_association(&reader, &set, b, &matches) == (expected == TYPE_ANSWER_NO),
                                          "generic duplicate filtering preserves enums, qualifiers, arrays, functions and source-time transparent unions");
                        arena_pop_to(&scratch, 0);
                    }
                }
            }
        }
        // Completing a tag changes enum/integer compatibility. Keys stored before completion must still admit the
        // later exact query, both for an earlier association and for a control keyed while the enum was incomplete.
        const TypeId later_enum = type_tag(&storage.table, BUSTER_TYPE_KIND_ENUM, 902);
        const TypeId later_pointer = type_pointer(&storage.table, later_enum);
        const TypeId integer_pointer = type_pointer(&storage.table, BUSTER_TYPE_UINT);
        CheckGenericSet later;
        CheckGenericSet control_later;
        bool matches = false;
        bool completion = later_enum != 0 && later_pointer != 0 && integer_pointer != 0 &&
                          check_generic_initialize(&reader, &later, 17, later_pointer, 10) &&
                          check_generic_initialize(&reader, &control_later, 17, later_pointer, 10);
        completion = completion && check_generic_association(&reader, &later, later_pointer, &matches) && matches &&
                     type_enum_complete(&storage.table, later_enum, BUSTER_TYPE_UINT, 0);
        check_test_expect(test, completion && !check_generic_association(&reader, &later, integer_pointer, &matches),
                          "generic enum completion does not stale an earlier association key");
        check_test_expect(test, completion && check_generic_association(&reader, &control_later, integer_pointer, &matches) && matches,
                          "generic enum completion does not stale an earlier control key");
        // An incompatible int before the repeated enum costs an exact comparison. The same int
        // after the enum must never be visited: the first duplicate ends the merged walk.
        for (uint32_t order = 0; order < 2; order += 1)
        {
            const TypeId first = order == 0 ? BUSTER_TYPE_INT : enums[0];
            const TypeId second = order == 0 ? enums[0] : BUSTER_TYPE_INT;
            CheckGenericSet mixed;
            bool ok = check_generic_initialize(&reader, &mixed, 19, BUSTER_TYPE_DOUBLE, 1) &&
                      check_generic_association(&reader, &mixed, first, &matches) &&
                      check_generic_association(&reader, &mixed, second, &matches);
            const uint64_t before = mixed.duplicate_queries;
            ok = ok && !check_generic_association(&reader, &mixed, enums[0], &matches);
            check_test_expect(test, ok && mixed.duplicate_queries == before + (order == 0 ? 2 : 1),
                              "interleaved integer and enum postings stop at the first duplicate in source order");
        }
        arena_pop_to(&scratch, 0);
        const uint64_t saved_capacity = scratch.capacity;
        // Enough room for both packed columns, too little for the directory.
        scratch.capacity = 256;
        CheckGenericSet fallback;
        bool ok = check_generic_initialize(&reader, &fallback, 17, enums[16], 1);
        for (uint32_t i = 0; ok && i < 17; i += 1)
        {
            ok = check_generic_association(&reader, &fallback, enums[i], &matches) && matches == (i == 16);
        }
        check_test_expect(test, ok && fallback.scan_types != NULL && !output.resource &&
                               !check_generic_association(&reader, &fallback, BUSTER_TYPE_UINT, &matches),
                          "a full optional directory falls back to exact source-ordered scanning");
        arena_pop_to(&scratch, 0);
        scratch.capacity = 31;
        CheckGenericSet full;
        check_test_expect(test, !check_generic_initialize(&reader, &full, 4096, BUSTER_TYPE_INT, 1) && output.resource,
                          "generic scratch exhaustion is explicit before publishing any association");
        scratch.capacity = saved_capacity;
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
    for (uint32_t scenario = 0; ready && scenario < 10; scenario += 1)
    {
        const bool constant = (scenario & 1) != 0;
        const uint32_t family = scenario / 2;
        uint32_t length = 0;
        for (uint32_t i = 0; i < 257; i += 1)
        {
            if (family == 0)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "typedef int G%u __attribute__((ext_vector_type(%u)));\n", i, i + 1);
            }
            else if (family == 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "typedef signed _BitInt(%u) G%u;\n", i + 2, i);
            }
            else if (family == 2)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "struct R%u{int x;};typedef struct R%u(*G%u)(void);\n", i, i, i);
            }
            else if (family == 3)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "typedef enum E%u{V%u} G%u;\n", i, i, i);
            }
            else
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "struct P%u;typedef int(*G%u)(struct P%u*);\n", i, i, i);
            }
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%s_Generic(%s,", constant ? "enum{N=" : "int f(void){return ",
                                     family == 0 ? "(G0){0}" : "(G0)0");
        for (uint32_t i = 0; i < 257; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "G%u:%u,", i, i);
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "default:1/0)%s\n",
                                     constant ? "};_Static_assert(N==0,\"selected\");" : ";}");
        ready = length < sizeof(source);
        snprintf(path, sizeof(path), "%s/partition-%u.c", directory, scenario);
        ready = ready && parse_test_write(path, source, length);
        char* paths[] = { path };
        ParseTestClang outcome = { 0 };
        ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome);
        check_test_expect(test, ready && !outcome.rejected, "generated generic stable-leaf families agree with Clang");
        if (ready)
        {
            const CheckSourceFixture fixture = {
                .name = "generated 257-association generic stable-leaf family",
                .source = source,
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

