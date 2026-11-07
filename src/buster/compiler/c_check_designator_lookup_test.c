// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1498 controls: the designation cursor must receive one immediate member at a time, including through
// anonymous records. Compare both lookup kernels with explicit member identities and independently stated ELF bytes.
static void check_init_test_member_lookup(CheckTest* test)
{
    static const uint32_t counts[] = { 0, 1, 15, 16, 17, 31, 32, 1024 };
    TypeMember members[1024];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(members); i += 1)
    {
        members[i] = (TypeMember){
            .name = i + 1,
            .type = BUSTER_TYPE_INT,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        };
    }
    for (uint32_t fallback = 0; fallback < 2; fallback += 1)
    {
        const TypeCapacity capacity = {
            .types = 4096,
            .parameters = 8192,
            .tags = 512,
            .members = 4096,
            .lookups = fallback != 0 ? 1 : 4096,
        };
        CheckTestTypes types = { 0 };
        bool valid = check_test_types_open_with(&types, capacity);
        for (uint32_t fixture = 0; valid && fixture < BUSTER_ARRAY_LENGTH(counts); fixture += 1)
        {
            TypeTable* table = &types.table;
            const uint32_t count = counts[fixture];
            const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 2048 + fixture);
            valid = record != 0 && type_complete_record(table, record, members, count, (TypeRecordAttributes){ 0 });
            const uint32_t tag = valid ? table->types.index[type_index(record)] : 0;
            const uint32_t begin = valid ? table->tags.member_begin[tag] : 0;
            valid = valid && (count <= BUSTER_TYPE_LOOKUP_SCAN ? table->tags.lookup[tag] == 0 :
                             fallback != 0 ? table->tags.lookup[tag] == BUSTER_TYPE_LOOKUP_WALK : table->tags.lookup[tag] != BUSTER_TYPE_LOOKUP_WALK);
            for (uint32_t scalar = 0; valid && scalar < 2; scalar += 1)
            {
                for (uint32_t name = 1; valid && name <= count + 1; name += 1)
                {
                    const uint32_t expected = name <= count ? begin + name - 1 : 0;
                    valid = check_init_member(table, tag, name, scalar != 0) == expected;
                }
                valid = valid && check_init_member(table, tag, UINT32_MAX, scalar != 0) == 0;
            }
            if (valid && count != 0)
            {
                const TypeMember outer_members[] = {
                    {
                        .type = record,
                        .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
                    },
                    {
                        .name = 4096,
                        .type = BUSTER_TYPE_UINT,
                        .bit_width = 5,
                    },
                };
                const TypeId outer = type_tag(table, BUSTER_TYPE_KIND_UNION, 3072 + fixture);
                valid = outer != 0 && type_complete_record(table, outer, outer_members, BUSTER_ARRAY_LENGTH(outer_members), (TypeRecordAttributes){ 0 });
                const uint32_t outer_tag = valid ? table->types.index[type_index(outer)] : 0;
                const uint32_t outer_begin = valid ? table->tags.member_begin[outer_tag] : 0;
                for (uint32_t scalar = 0; valid && scalar < 2; scalar += 1)
                {
                    // The leaf's own member index belongs to the inner record. Returning it here would skip the
                    // anonymous union frame and corrupt positional continuation and union-history selection.
                    valid = check_init_member(table, outer_tag, count, scalar != 0) == outer_begin &&
                            check_init_member(table, outer_tag, 4096, scalar != 0) == outer_begin + 1 &&
                            check_init_member(table, outer_tag, UINT32_MAX, scalar != 0) == 0;
                }
            }
        }
        check_test_expect(test, valid, fallback != 0 ? "designation member identity through exhausted lookup capacity and anonymous unions" :
                                                     "designation scalar/vector member identity at scan/index boundaries and 1024 members");
        check_test_types_close(&types);
    }
}

static void check_init_test_designator_lookup(CheckTest* test, const char* clang)
{
    check_init_test_member_lookup(test);
    static const char source[] =
        "struct Wide {unsigned char a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15;"
        "unsigned char a16,a17,a18,a19,a20,a21,a22,a23,a24,a25,a26,a27,a28,a29,a30,a31;};\n"
        "struct Wide wide={.a31=32,.a0=1,2,.a16=17,18,.a15=16};\n"
        "struct Nested {char prefix; union {struct {char first; union {struct {char x,y;}; char pair[2];}; char last;}; char raw[4];}; char suffix;};\n"
        "struct Nested nested={.y=7,8,9};\n";
    static const CheckInitExpected expected[] = {
        {
            .name = "wide",
            .bytes = {
                [0] = 1,
                [1] = 2,
                [15] = 16,
                [16] = 17,
                [17] = 18,
                [31] = 32,
            },
            .size = 32,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "nested",
            .bytes = {
                [3] = 7,
                [4] = 8,
                [5] = 9,
            },
            .size = 6,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    check_init_test_source(test, "struct S {struct {int x;};int y;} value={.missing=1};", NULL, 0, false, false, NULL);
    check_init_test_source(test, "struct S {struct {int x;};int x;} value={.x=1};", NULL, 0, false, false, NULL);
    check_init_test_source(test, "struct S {struct {int x;};union {int x;};} value={.x=1};", NULL, 0, false, false, NULL);
}
