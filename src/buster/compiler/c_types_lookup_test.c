// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-types' check of the member lookup (#591, c_types.c's TypeLookup). type_lookup_member, which takes each record's
// scan or index, must give what its scalar twin (type_lookup_member_scalar) gives, what the walk (type_lookup_walk) gives,
// and what an oracle of the test's own gives, which lists a record's named members depth first through its anonymous
// members, every occurrence in order, and takes the first with the name: the member, its offset and the qualifiers its
// access inherits. On:
//   - random worlds of structs and unions of 0 to 459 members (named ones, bit-fields named and not, members of record
//     types, and anonymous members of earlier records, qualified, aligned, and one record in several of them), whose
//     names repeat as the table allows, looked up by every name and by absent ones, through the records themselves,
//     qualified and aligned; once with the table's default room for indices, in which none may walk, and once with room
//     for 48 buckets, in which some must;
//   - fixtures: the scan's limit (16 members, then 17) with and without an anonymous member, a name that an anonymous
//     member before a named one also finds, and 24 names that hash to one bucket of eight, whose probes go on to the
//     buckets after it and wrap around;
//   - copies (type_copy), a view taken back (type_view_reset) and a tag withdrawn and restored, each answering as the
//     table it came from;
//   - unions that hold the one before them twice, 64 levels deep: a tree of 2^64 leaves, which neither path may expand;
//   - one record of 100,000 members and three anonymous ones, every name found, with an index no larger than its names
//     ask for;
//   - the walk with every amount of room to 16 KB left in the scratch arena, which answers or says it couldn't;
//   - the scan's load at every count to 16 of names ending at a PROT_NONE page.
//
// Include in c_types_test.c, before run_type_tests.

#define BUSTER_TYPE_LOOKUP_TEST_NAMES 96
// The most named members a record's listing may have, which keeps the oracle's listings small though records hold one
// another.
#define BUSTER_TYPE_LOOKUP_TEST_FLAT 2048
#define BUSTER_TYPE_LOOKUP_TEST_MEMBERS 460
#define BUSTER_TYPE_LOOKUP_TEST_LEVELS 64
#define BUSTER_TYPE_LOOKUP_TEST_LARGE 100000

// A named member as the oracle lists it.
typedef struct TypeLookupTestEntry TypeLookupTestEntry;
struct TypeLookupTestEntry
{
    uint32_t name;
    uint32_t member;
    uint32_t qualifiers;
    uint64_t offset;
};

typedef struct TypeLookupTestWorld TypeLookupTestWorld;
struct TypeLookupTestWorld
{
    TypeTestTable test;
    TypeId* records;
    // Each record's named members, in its listing.
    uint32_t* flat;
    uint32_t count;
    uint32_t capacity;
};

// How a world's records look their members up, and how its lookups went.
typedef struct TypeLookupTestCounts TypeLookupTestCounts;
struct TypeLookupTestCounts
{
    uint64_t scanned;
    uint64_t indexed;
    uint64_t walked;
    uint64_t found;
    uint64_t absent;
};

static bool type_lookup_test_open(TypeTestTable* test, uint32_t types, uint32_t members, uint32_t lookups)
{
    const TypeCapacity capacity = {
        .types = types,
        .parameters = types * 4,
        .tags = types,
        .members = members,
        .lookups = lookups,
    };
    test->arena = arena_reserve(type_capacity_bytes(capacity));
    test->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    return type_initialize(&test->table, &test->arena, &test->index_arena, capacity);
}

// The oracle's listing of `record`'s named members, depth first through anonymous members, into `entries` (room for
// BUSTER_TYPE_LOOKUP_TEST_FLAT); false if it has more.
static bool type_lookup_test_list(const TypeTable* table, TypeId record, TypeLookupTestEntry* entries, uint32_t* count)
{
    typedef struct TypeLookupTestFrame TypeLookupTestFrame;
    struct TypeLookupTestFrame
    {
        uint32_t next;
        uint32_t end;
        uint32_t qualifiers;
        uint64_t offset;
    };
    TypeLookupTestFrame stack[BUSTER_TYPE_LOOKUP_TEST_FLAT];
    const TypeId root = type_strip_aligned(table, record);
    const uint32_t root_tag = table->types.index[type_index(root)];
    uint32_t depth = 1;
    uint32_t listed = 0;
    bool fits = true;
    stack[0] = (TypeLookupTestFrame){
        .next = table->tags.member_begin[root_tag],
        .end = table->tags.member_begin[root_tag] + table->tags.member_count[root_tag],
        .qualifiers = type_qualifiers(root),
    };
    while (fits && depth != 0)
    {
        TypeLookupTestFrame* top = &stack[depth - 1];
        if (top->next == top->end)
        {
            depth -= 1;
        }
        else
        {
            const uint32_t member = top->next++;
            const uint64_t offset = top->offset + table->members.offset[member];
            const TypeId inner = type_strip_aligned(table, table->members.type[member]);
            const uint8_t kind = type_kind(table, inner);
            if (table->members.name[member] != 0)
            {
                fits = listed < BUSTER_TYPE_LOOKUP_TEST_FLAT;
                if (fits)
                {
                    entries[listed++] = (TypeLookupTestEntry){
                        .name = table->members.name[member],
                        .member = member,
                        .qualifiers = top->qualifiers,
                        .offset = offset,
                    };
                }
            }
            else if ((table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0 && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
            {
                fits = depth < BUSTER_TYPE_LOOKUP_TEST_FLAT;
                if (fits)
                {
                    const uint32_t tag = table->types.index[type_index(inner)];
                    stack[depth++] = (TypeLookupTestFrame){
                        .next = table->tags.member_begin[tag],
                        .end = table->tags.member_begin[tag] + table->tags.member_count[tag],
                        .qualifiers = top->qualifiers | type_qualifiers(inner),
                        .offset = offset,
                    };
                }
            }
        }
    }
    *count = listed;
    return fits;
}

// Looks `name` up in `record` by the lookup, its twin, the walk and type_find_member, and in the oracle's listing (of
// `count` entries), which must agree.
static bool type_lookup_test_name(TypeTestTally* tally, const TypeTable* table, TypeId record, uint32_t name, const TypeLookupTestEntry* entries,
                                  uint32_t count, TypeLookupTestCounts* counts, const char* check)
{
    uint32_t expected = 0;
    uint64_t expected_offset = 0;
    uint32_t expected_qualifiers = 0;
    for (uint32_t i = 0; expected == 0 && name != 0 && i < count; i += 1)
    {
        if (entries[i].name == name)
        {
            expected = entries[i].member;
            expected_offset = entries[i].offset;
            expected_qualifiers = entries[i].qualifiers;
        }
    }
    uint64_t offsets[4] = { UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX };
    uint32_t qualifiers[3] = { UINT32_MAX, UINT32_MAX, UINT32_MAX };
    const uint32_t fast = type_lookup_member(table, record, name, &offsets[0], &qualifiers[0]);
    const uint32_t twin = type_lookup_member_scalar(table, record, name, &offsets[1], &qualifiers[1]);
    const uint32_t walked = type_lookup_walk(table, record, name, &offsets[2], &qualifiers[2]);
    const uint32_t found = type_find_member(table, record, name, &offsets[3]);
    bool same = fast == expected && twin == expected && walked == expected && found == expected;
    for (uint32_t path = 0; path < 4; path += 1)
    {
        same = same && offsets[path] == expected_offset && (path == 3 || qualifiers[path] == expected_qualifiers);
    }
    counts->found += expected != 0;
    counts->absent += expected == 0;
    char detail[320];
    snprintf(detail, sizeof(detail), "record %#x, name %u: member %u, offset %" PRIu64 ", qualifiers %u expected; lookup %u (%" PRIu64 ", %u), twin %u (%" PRIu64 ", %u), walk %u (%" PRIu64 ", %u), find %u (%" PRIu64 ")",
             record, name, expected, expected_offset, expected_qualifiers, fast, offsets[0], qualifiers[0], twin, offsets[1], qualifiers[1], walked, offsets[2],
             qualifiers[2], found, offsets[3]);
    return type_test_expect(tally, same, check, detail);
}

// Every name, and two that no record has, looked up in `record` and in it qualified and aligned.
static void type_lookup_test_record_names(TypeTestTally* tally, TypeTable* table, TypeId record, TypeLookupTestEntry* entries, TypeLookupTestCounts* counts,
                                          uint64_t* random, const char* check)
{
    uint32_t count = 0;
    const bool listed = type_lookup_test_list(table, record, entries, &count);
    type_test_expect(tally, listed, check, "a record's listing is larger than the oracle's room");
    const TypeId aligned = type_aligned(table, record, (uint32_t)type_test_below(random, 6));
    const TypeId views[3] = {
        record,
        record | (uint32_t)(1 + type_test_below(random, BUSTER_TYPE_QUALIFIERS)),
        aligned != 0 ? aligned | (uint32_t)type_test_below(random, BUSTER_TYPE_QUALIFIERS + 1) : record,
    };
    for (uint32_t view = 0; listed && view < 3; view += 1)
    {
        // The listing's qualifiers start from the record's own: the view adds its own.
        TypeLookupTestEntry viewed[BUSTER_TYPE_LOOKUP_TEST_FLAT];
        const uint32_t added = type_qualifiers(type_strip_aligned(table, views[view]));
        for (uint32_t i = 0; i < count; i += 1)
        {
            viewed[i] = entries[i];
            viewed[i].qualifiers |= added;
        }
        bool same = true;
        for (uint32_t name = 0; same && name <= BUSTER_TYPE_LOOKUP_TEST_NAMES + 1; name += 1)
        {
            same = type_lookup_test_name(tally, table, views[view], name, viewed, count, counts, check);
        }
    }
}

// A random member for a record in `table` after the world's: an anonymous member of one of the world's records (whose
// listing leaves `flat` room for a record's members in the oracle's), a bit-field, named or not, or a named member of a
// scalar or record type.
static TypeMember type_lookup_test_member(TypeTable* table, const TypeLookupTestWorld* world, uint64_t* random, uint64_t* flat)
{
    const uint64_t kind = type_test_below(random, 100);
    TypeMember member = type_test_member((uint32_t)(1 + type_test_below(random, BUSTER_TYPE_LOOKUP_TEST_NAMES)), BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
    const uint32_t pick = world->count != 0 ? (uint32_t)type_test_below(random, world->count) : 0;
    if (kind < 12 && world->count != 0 && *flat + world->flat[pick] <= BUSTER_TYPE_LOOKUP_TEST_FLAT - BUSTER_TYPE_LOOKUP_TEST_MEMBERS)
    {
        TypeId inner = world->records[pick] | (uint32_t)(type_test_below(random, 4) == 0 ? BUSTER_TYPE_CONST : 0) |
                       (uint32_t)(type_test_below(random, 4) == 0 ? BUSTER_TYPE_VOLATILE : 0);
        const TypeId aligned = type_test_below(random, 5) == 0 ? type_aligned(table, inner, (uint32_t)type_test_below(random, 5)) : 0;
        member.name = 0;
        member.type = aligned != 0 ? aligned : inner;
        *flat += world->flat[pick];
    }
    else if (kind < 18)
    {
        member.name = 0;
        member.bit_width = (uint32_t)type_test_below(random, 17);
    }
    else if (kind < 22)
    {
        member.bit_width = (uint32_t)(1 + type_test_below(random, 31));
        *flat += 1;
    }
    else
    {
        static const TypeId scalars[] = {
            BUSTER_TYPE_CHAR,
            BUSTER_TYPE_INT,
            BUSTER_TYPE_DOUBLE,
            BUSTER_TYPE_LONG,
        };
        member.type = kind < 30 && world->count != 0 ? world->records[pick] : scalars[type_test_below(random, BUSTER_ARRAY_LENGTH(scalars))];
        *flat += 1;
    }
    return member;
}

// A world of `count` random records, some of them anonymous members of later ones.
static bool type_lookup_test_world(TypeLookupTestWorld* world, uint64_t seed, uint32_t count, uint32_t lookups)
{
    *world = (TypeLookupTestWorld){ .capacity = count };
    bool valid = type_lookup_test_open(&world->test, count * 4 + 64, count * 64 + 1024, lookups);
    world->records = valid ? calloc(count, sizeof(TypeId)) : 0;
    world->flat = valid ? calloc(count, sizeof(uint32_t)) : 0;
    valid = valid && world->records != 0 && world->flat != 0;
    uint64_t random = seed;
    TypeMember members[BUSTER_TYPE_LOOKUP_TEST_MEMBERS];
    while (valid && world->count < count)
    {
        TypeTable* table = &world->test.table;
        const uint64_t shape = type_test_below(&random, 100);
        const uint32_t size = (uint32_t)(shape < 60 ? type_test_below(&random, 21) : shape < 88 ? 15 + type_test_below(&random, 50) : 60 + type_test_below(&random, 400));
        uint64_t flat = 0;
        for (uint32_t i = 0; i < size; i += 1)
        {
            members[i] = type_lookup_test_member(&world->test.table, world, &random, &flat);
        }
        const uint8_t kind = type_test_below(&random, 3) == 0 ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT;
        const TypeId record = type_tag(table, kind, 1);
        valid = record != 0 && type_complete_record(table, record, members, size, (TypeRecordAttributes){ 0 });
        if (valid)
        {
            world->records[world->count] = record;
            world->flat[world->count] = (uint32_t)flat;
            world->count += 1;
        }
    }
    return valid;
}

static void type_lookup_test_world_close(TypeLookupTestWorld* world)
{
    free(world->records);
    free(world->flat);
    type_test_table_close(&world->test);
}

// How each of the world's records looks its members up.
static void type_lookup_test_count(const TypeTable* table, const TypeLookupTestWorld* world, TypeLookupTestCounts* counts)
{
    for (uint32_t i = 0; i < world->count; i += 1)
    {
        const uint32_t lookup = table->tags.lookup[table->types.index[type_index(world->records[i])]];
        counts->scanned += lookup == 0;
        counts->indexed += lookup != 0 && lookup != BUSTER_TYPE_LOOKUP_WALK;
        counts->walked += lookup == BUSTER_TYPE_LOOKUP_WALK;
    }
}

static void type_lookup_test_print(const char* check, const TypeLookupTestCounts* counts)
{
    printf("%-34s %10" PRIu64 " records scanned, %" PRIu64 " indexed, %" PRIu64 " walked; %" PRIu64 " lookups found, %" PRIu64 " absent\n", check,
           counts->scanned, counts->indexed, counts->walked, counts->found, counts->absent);
}

// Random worlds, with the default room for indices (every record scanned or indexed) and with room for 48 buckets
// (some records walked), and a copy of the first: every record by every name.
static void type_lookup_test_worlds(TypeTestTally* tally, uint64_t seed, uint32_t count)
{
    TypeLookupTestEntry* entries = calloc(BUSTER_TYPE_LOOKUP_TEST_FLAT, sizeof(TypeLookupTestEntry));
    type_test_expect(tally, entries != 0, "member lookup worlds", "no memory for the oracle");
    for (uint32_t room = 0; entries != 0 && room < 2; room += 1)
    {
        const char* check = room == 0 ? "member lookup worlds" : "member lookup fallback";
        TypeLookupTestWorld world;
        const bool built = type_lookup_test_world(&world, seed + room, count, room == 0 ? 0 : 48);
        if (type_test_expect(tally, built, check, "the world can't be built"))
        {
            TypeTable* table = &world.test.table;
            TypeLookupTestCounts counts = { 0 };
            type_lookup_test_count(table, &world, &counts);
            uint64_t random = seed ^ 0x591;
            for (uint32_t i = 0; i < world.count; i += 1)
            {
                type_lookup_test_record_names(tally, table, world.records[i], entries, &counts, &random, check);
            }
            type_test_expect_number(tally, room == 0 ? counts.walked == 0 : counts.walked != 0 && counts.indexed != 0, check,
                                    room == 0 ? "records walked with the default room" : "records walked and indexed with room for 48 buckets", counts.walked,
                                    counts.indexed);
            type_test_expect(tally, counts.scanned != 0 && counts.indexed != 0, check, "a world without scanned or indexed records");
            type_lookup_test_print(check, &counts);
            if (room == 0)
            {
                // A copy answers as its source; a record defined in it, the view taken back, and a tag withdrawn and
                // restored, too.
                TypeTestTable copy = {
                    .arena = arena_reserve(UINT64_C(1) << 32),
                    .index_arena = arena_reserve(UINT64_C(1) << 30),
                };
                // Room for an aligned type a record, which its lookups make, and for the record defined in the copy.
                const TypeCapacity extra = {
                    .types = world.count * 2 + 64,
                    .parameters = 64,
                    .tags = 64,
                    .members = 4096,
                };
                const bool copied = type_copy(&copy.table, table, &copy.arena, &copy.index_arena, extra);
                if (type_test_expect(tally, copied && copy.table.lookup.count == table->lookup.count, "member lookup copies", "the copy failed or lost buckets"))
                {
                    TypeLookupTestCounts copied_counts = { 0 };
                    for (uint32_t i = 0; i < world.count; i += 1)
                    {
                        type_lookup_test_record_names(tally, &copy.table, world.records[i], entries, &copied_counts, &random, "member lookup copies");
                    }
                    const TypeViewMark mark = type_view_mark(&copy.table);
                    TypeMember members[BUSTER_TYPE_LOOKUP_TEST_MEMBERS];
                    uint64_t flat = 0;
                    for (uint32_t i = 0; i < 40; i += 1)
                    {
                        members[i] = type_lookup_test_member(&copy.table, &world, &random, &flat);
                    }
                    const TypeId added = type_tag(&copy.table, BUSTER_TYPE_KIND_STRUCT, 1);
                    const bool defined = added != 0 && type_complete_record(&copy.table, added, members, 40, (TypeRecordAttributes){ 0 });
                    type_test_expect(tally, defined && copy.table.lookup.count > mark.lookups, "member lookup copies", "a record in the copy got no index");
                    if (defined)
                    {
                        type_lookup_test_record_names(tally, &copy.table, added, entries, &copied_counts, &random, "member lookup copies");
                    }
                    const bool reset = type_view_reset(&copy.table, mark);
                    type_test_expect(tally, reset && copy.table.lookup.count == mark.lookups, "member lookup copies", "the view's reset kept the record's buckets");
                    const TypeId record = world.records[world.count - 1];
                    const uint32_t tag = copy.table.types.index[type_index(record)];
                    type_tag_withdraw(&copy.table, tag, false);
                    uint64_t offset = 0;
                    uint32_t qualifiers = 0;
                    bool gone = true;
                    for (uint32_t name = 1; name <= BUSTER_TYPE_LOOKUP_TEST_NAMES; name += 1)
                    {
                        gone = gone && type_lookup_member(&copy.table, record, name, &offset, &qualifiers) == 0;
                    }
                    type_test_expect(tally, gone, "member lookup copies", "a withdrawn record still finds members");
                    type_tag_restore(&copy.table, table, tag);
                    type_lookup_test_record_names(tally, &copy.table, record, entries, &copied_counts, &random, "member lookup copies");
                    type_lookup_test_print("member lookup copies", &copied_counts);
                }
                type_test_table_close(&copy);
            }
        }
        type_lookup_test_world_close(&world);
    }
    free(entries);
}

// The fixtures: the scan's limit, a name an anonymous member finds before a named member has it, and names that all
// hash to one bucket.
static void type_lookup_test_fixtures(TypeTestTally* tally)
{
    const char* check = "member lookup fixtures";
    TypeTestTable test;
    TypeLookupTestEntry* entries = calloc(BUSTER_TYPE_LOOKUP_TEST_FLAT, sizeof(TypeLookupTestEntry));
    if (type_test_expect(tally, entries != 0 && type_lookup_test_open(&test, 1024, 8192, 0), check, "the table can't be made"))
    {
        TypeTable* table = &test.table;
        TypeLookupTestCounts counts = { 0 };
        uint64_t random = 591;
        const TypeMember small = type_test_member(70, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
        const TypeId inner = type_test_record(table, BUSTER_TYPE_KIND_STRUCT, &small, 1, (TypeRecordAttributes){ 0 });
        // 16 members and 17, with and without an anonymous one among them: 16 without it is the largest scan.
        for (uint32_t size = 16; size <= 17; size += 1)
        {
            for (uint32_t anonymous = 0; anonymous < 2; anonymous += 1)
            {
                TypeMember members[17];
                for (uint32_t i = 0; i < size; i += 1)
                {
                    members[i] = type_test_member(i + 1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
                }
                members[size / 2] = anonymous != 0 ? type_test_member(0, inner, BUSTER_TYPE_NOT_BIT_FIELD) : members[size / 2];
                const TypeId record = type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, size, (TypeRecordAttributes){ 0 });
                const uint32_t lookup = record != 0 ? table->tags.lookup[table->types.index[type_index(record)]] : BUSTER_TYPE_LOOKUP_WALK;
                char detail[96];
                snprintf(detail, sizeof(detail), "%u members, %s anonymous one: lookup %u", size, anonymous != 0 ? "an" : "no", lookup);
                type_test_expect(tally, record != 0 && (lookup == 0) == (size <= BUSTER_TYPE_LOOKUP_SCAN && anonymous == 0) && lookup != BUSTER_TYPE_LOOKUP_WALK,
                                 check, detail);
                if (record != 0)
                {
                    type_lookup_test_record_names(tally, table, record, entries, &counts, &random, check);
                }
            }
        }
        // struct { int a; struct { int x; }; int x; }: the anonymous member's x, the first in declaration order (the
        // table takes a repeated name; the checker's records never have one).
        const TypeMember shadowed[3] = {
            type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
            type_test_member(0, inner, BUSTER_TYPE_NOT_BIT_FIELD),
            type_test_member(70, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
        };
        const TypeId shadow = type_test_record(table, BUSTER_TYPE_KIND_STRUCT, shadowed, 3, (TypeRecordAttributes){ 0 });
        uint64_t offset = 0;
        uint32_t qualifiers = 0;
        const uint32_t found = shadow != 0 ? type_lookup_member(table, shadow, 70, &offset, &qualifiers) : 0;
        type_test_expect(tally, found == table->tags.member_begin[table->types.index[type_index(inner)]] && offset == 32, check,
                         "the anonymous member's x, before the named one");
        // 24 names in one bucket of the eight a record of 24 names gets: their probes fill it and go on, around the end.
        TypeMember colliding[24];
        uint32_t chosen = 0;
        for (uint32_t name = 1; chosen < 24; name += 1)
        {
            if (type_lookup_bucket(name, 3) == 7)
            {
                colliding[chosen++] = type_test_member(name, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
            }
        }
        const TypeId crowded = type_test_record(table, BUSTER_TYPE_KIND_STRUCT, colliding, 24, (TypeRecordAttributes){ 0 });
        const uint32_t crowded_tag = crowded != 0 ? table->types.index[type_index(crowded)] : 0;
        type_test_expect(tally, crowded != 0 && table->tags.lookup_log2[crowded_tag] == 3, check, "24 names don't take eight buckets");
        bool all = crowded != 0;
        for (uint32_t i = 0; all && i < 24; i += 1)
        {
            all = type_lookup_member(table, crowded, colliding[i].name, &offset, &qualifiers) == table->tags.member_begin[crowded_tag] + i && offset == 32 * i;
        }
        type_test_expect(tally, all, check, "a name of a crowded bucket isn't found where it is");
        uint32_t absent = colliding[23].name + 1;
        while (type_lookup_bucket(absent, 3) != 7)
        {
            absent += 1;
        }
        type_test_expect(tally, crowded != 0 && type_lookup_member(table, crowded, absent, &offset, &qualifiers) == 0, check,
                         "an absent name of a crowded bucket is found");
        type_test_table_close(&test);
        type_lookup_test_print(check, &counts);
    }
    free(entries);
}

// Unions that hold the one before them twice as anonymous members, and a name of their own: a tree of 2^64 leaves. The
// lookup and its twin go down one path, and the walk enters each union once.
static void type_lookup_test_shared(TypeTestTally* tally)
{
    const char* check = "member lookup shared records";
    TypeTestTable test;
    if (type_test_expect(tally, type_lookup_test_open(&test, 1024, 8192, 0), check, "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const TypeMember leaves[2] = {
            type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
            type_test_member(2, BUSTER_TYPE_LONG, BUSTER_TYPE_NOT_BIT_FIELD),
        };
        TypeId levels[BUSTER_TYPE_LOOKUP_TEST_LEVELS + 1] = { type_test_record(table, BUSTER_TYPE_KIND_UNION, leaves, 2, (TypeRecordAttributes){ 0 }) };
        for (uint32_t level = 1; level <= BUSTER_TYPE_LOOKUP_TEST_LEVELS && levels[level - 1] != 0; level += 1)
        {
            const TypeMember members[3] = {
                type_test_member(0, levels[level - 1], BUSTER_TYPE_NOT_BIT_FIELD),
                type_test_member(0, levels[level - 1] | BUSTER_TYPE_CONST, BUSTER_TYPE_NOT_BIT_FIELD),
                type_test_member(100 + level, BUSTER_TYPE_SHORT, BUSTER_TYPE_NOT_BIT_FIELD),
            };
            levels[level] = type_test_record(table, BUSTER_TYPE_KIND_UNION, members, 3, (TypeRecordAttributes){ 0 });
        }
        const TypeId top = levels[BUSTER_TYPE_LOOKUP_TEST_LEVELS];
        bool same = top != 0;
        // Both leaves, every level's own name, and names no union has.
        for (uint32_t name = 1; same && name <= 100 + BUSTER_TYPE_LOOKUP_TEST_LEVELS + 2; name += name == 3 ? 98 : 1)
        {
            uint64_t offsets[3] = { 0 };
            uint32_t qualifiers[3] = { 0 };
            const uint32_t fast = type_lookup_member(table, top, name, &offsets[0], &qualifiers[0]);
            const uint32_t twin = type_lookup_member_scalar(table, top, name, &offsets[1], &qualifiers[1]);
            const uint32_t walked = type_lookup_walk(table, top, name, &offsets[2], &qualifiers[2]);
            const bool present = (name >= 1 && name <= 2) || (name > 100 && name <= 100 + BUSTER_TYPE_LOOKUP_TEST_LEVELS);
            same = fast == twin && fast == walked && (offsets[0] | offsets[1] | offsets[2]) == 0 && (qualifiers[0] | qualifiers[1] | qualifiers[2]) == 0 &&
                   (fast != 0) == present && (fast == 0 || table->members.name[fast] == name);
            char detail[96];
            snprintf(detail, sizeof(detail), "name %u: lookup %u, twin %u, walk %u", name, fast, twin, walked);
            type_test_expect(tally, same, check, detail);
        }
        type_test_expect_number(tally, table->lookup.count < 4096, check, "buckets for 64 levels, against 4096", table->lookup.count, 4096);
        type_test_table_close(&test);
    }
}

// One struct of BUSTER_TYPE_LOOKUP_TEST_LARGE ints and three anonymous members: every name found where it is, through an
// index of no more buckets than twice its names fill half of.
static void type_lookup_test_large(TypeTestTally* tally)
{
    const char* check = "member lookup large record";
    TypeTestTable test;
    TypeMember* members = calloc(BUSTER_TYPE_LOOKUP_TEST_LARGE + 3, sizeof(TypeMember));
    if (type_test_expect(tally, members != 0 && type_lookup_test_open(&test, 1024, BUSTER_TYPE_LOOKUP_TEST_LARGE + 64, 0), check, "the table can't be made"))
    {
        TypeTable* table = &test.table;
        TypeMember inner_members[4];
        for (uint32_t i = 0; i < 4; i += 1)
        {
            inner_members[i] = type_test_member(BUSTER_TYPE_LOOKUP_TEST_LARGE + 1 + i, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
        }
        const TypeId inner = type_test_record(table, BUSTER_TYPE_KIND_UNION, inner_members, 4, (TypeRecordAttributes){ 0 });
        for (uint32_t i = 0; i < BUSTER_TYPE_LOOKUP_TEST_LARGE; i += 1)
        {
            members[i] = type_test_member(i + 1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
        }
        for (uint32_t i = 0; i < 3; i += 1)
        {
            members[BUSTER_TYPE_LOOKUP_TEST_LARGE + i] = type_test_member(0, inner, BUSTER_TYPE_NOT_BIT_FIELD);
        }
        const uint32_t before = table->lookup.count;
        const TypeId record = inner != 0 ? type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, BUSTER_TYPE_LOOKUP_TEST_LARGE + 3, (TypeRecordAttributes){ 0 }) : 0;
        const uint32_t tag = record != 0 ? table->types.index[type_index(record)] : 0;
        // Its names are counted with the anonymous members' every time: 100,012, so 2^15 buckets of 8 slots.
        type_test_expect_number(tally, record != 0 && table->lookup.count - before == UINT32_C(1) << 15 && table->tags.lookup_log2[tag] == 15, check,
                                "the index's buckets", table->lookup.count - before, UINT32_C(1) << 15);
        bool all = record != 0;
        for (uint32_t name = 1; all && name <= BUSTER_TYPE_LOOKUP_TEST_LARGE + 4; name += 1)
        {
            uint64_t offset = 0;
            uint32_t qualifiers = 0;
            const uint32_t member = type_lookup_member(table, record, name, &offset, &qualifiers);
            const uint32_t expected = name <= BUSTER_TYPE_LOOKUP_TEST_LARGE ? table->tags.member_begin[tag] + name - 1 :
                                      table->tags.member_begin[table->types.index[type_index(inner)]] + name - BUSTER_TYPE_LOOKUP_TEST_LARGE - 1;
            const uint64_t expected_offset = name <= BUSTER_TYPE_LOOKUP_TEST_LARGE ? UINT64_C(32) * (name - 1) : UINT64_C(32) * BUSTER_TYPE_LOOKUP_TEST_LARGE;
            all = member == expected && offset == expected_offset && qualifiers == 0;
        }
        type_test_expect(tally, all, check, "a name isn't found where it is");
        uint64_t offset = 0;
        uint32_t qualifiers = 0;
        type_test_expect(tally, record != 0 && type_lookup_member(table, record, BUSTER_TYPE_LOOKUP_TEST_LARGE + 5, &offset, &qualifiers) == 0, check, "an absent name is found");
        type_test_table_close(&test);
    }
    free(members);
}

// The walk with every amount of room to BUSTER_TYPE_TEST_SCRATCH_ROOM left in the scratch arena it takes: the right
// member, or BUSTER_TYPE_MEMBER_UNKNOWN, which type_find_member gives as 0; the lookup of an indexed record takes none.
static void type_lookup_test_scratch(TypeTestTally* tally)
{
    const char* check = "member lookup scratch";
    TypeTestTable test;
    if (type_test_expect(tally, type_lookup_test_open(&test, 1024, 8192, 0), check, "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const TypeMember leaf = type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
        TypeId record = type_test_record(table, BUSTER_TYPE_KIND_STRUCT, &leaf, 1, (TypeRecordAttributes){ 0 });
        for (uint32_t level = 0; record != 0 && level < 100; level += 1)
        {
            const TypeMember members[2] = {
                type_test_member(0, record, BUSTER_TYPE_NOT_BIT_FIELD),
                type_test_member(2 + level, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
            };
            record = type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 2, (TypeRecordAttributes){ 0 });
        }
        uint64_t expected_offset = UINT64_MAX;
        uint32_t qualifiers = 0;
        const uint32_t expected = record != 0 ? type_lookup_member(table, record, 1, &expected_offset, &qualifiers) : 0;
        type_test_expect(tally, expected != 0 && expected_offset == 0, check, "the leaf under 100 anonymous members isn't found");
        Scratch scratch = scratch_begin(0);
        const uint64_t capacity = scratch.arena->capacity;
        uint32_t answered = 0;
        uint32_t unknown = 0;
        for (uint64_t room = 0; expected != 0 && room <= BUSTER_TYPE_TEST_SCRATCH_ROOM; room += 16)
        {
            const uint64_t length = scratch.arena->length;
            // `room` bytes left for the walk, by the arena's bound rather than by pushing the rest of its reservation
            // (a commit that large may refuse where the reservation's address space did not).
            scratch.arena->capacity = room < capacity - length ? length + room : capacity;
            uint64_t offsets[2] = { UINT64_MAX, UINT64_MAX };
            const uint32_t walked = type_lookup_walk(table, record, 1, &offsets[0], &qualifiers);
            const uint32_t found = type_find_member(table, record, 1, &offsets[1]);
            const uint32_t indexed = type_lookup_member(table, record, 1, &offsets[1], &qualifiers);
            char detail[96];
            snprintf(detail, sizeof(detail), "%" PRIu64 " bytes left: walk %u, find %u, lookup %u", room, walked, found, indexed);
            type_test_expect(tally, (walked == expected || walked == BUSTER_TYPE_MEMBER_UNKNOWN) && found == expected && indexed == expected, check, detail);
            answered += walked == expected;
            unknown += walked == BUSTER_TYPE_MEMBER_UNKNOWN;
            arena_pop_to(scratch.arena, length);
        }
        scratch.arena->capacity = capacity;
        type_test_expect(tally, answered != 0 && unknown != 0, check, "the room never made the walk fail, or never let it through");
        scratch_end(scratch);
        type_test_table_close(&test);
    }
}

// The scan's masked load at every count to BUSTER_TYPE_LOOKUP_SCAN of names that end at a PROT_NONE page, for every name
// they hold and absent ones, against a loop.
static void type_lookup_test_guarded(TypeTestTally* tally)
{
    const char* check = "member lookup guarded scans";
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    if (type_test_expect(tally, guarded, check, "no guard page"))
    {
        for (uint32_t count = 0; count <= BUSTER_TYPE_LOOKUP_SCAN; count += 1)
        {
            // From 1: the scan's answer 0 is no member, as member 0 is none in a table.
            for (uint32_t begin = 1; begin < 4; begin += 1)
            {
                uint32_t* names = (uint32_t*)(mapping + page) - count - begin;
                for (uint32_t i = 0; i < count + begin; i += 1)
                {
                    names[i] = 1 + (i * 7) % (count + 2);
                }
                bool same = true;
                for (uint32_t name = 1; same && name <= count + 3; name += 1)
                {
                    uint32_t expected = 0;
                    for (uint32_t i = begin; expected == 0 && i < begin + count; i += 1)
                    {
                        expected = names[i] == name ? i : 0;
                    }

                    same = type_lookup_scan(names, begin, count, name) == expected;
                }
                char detail[64];
                snprintf(detail, sizeof(detail), "%u names from %u", count, begin);
                type_test_expect(tally, same, check, detail);
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}

static bool type_test_lookups(uint64_t seed, uint32_t count)
{
    TypeTestTally tally = { 0 };
    type_lookup_test_fixtures(&tally);
    type_lookup_test_worlds(&tally, seed, count);
    type_lookup_test_shared(&tally);
    type_lookup_test_large(&tally);
    type_lookup_test_scratch(&tally);
    type_lookup_test_guarded(&tally);
    type_test_report("member lookup index", tally);
    return tally.failures == 0;
}
