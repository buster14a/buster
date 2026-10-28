// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original namespace fixtures exercise anonymous expansion, growth and every scratch byte limit. Source fixtures
// in c_check_source_test.c independently compare layout and first-error locations against external Clang.

static void check_test_record_name_limits(CheckTest* test, TypeTable* types, const TypeMember* members, uint32_t count, uint32_t limit)
{
    _Alignas(16) uint8_t bytes[8192];
    uint32_t accepted = 0;
    uint32_t exhausted = 0;
    for (uint32_t room = 0; room <= limit; room += 1)
    {
        memset(bytes, 0xa5, sizeof(bytes));
        Arena scratch = { .base = bytes, .length = 13, .capacity = 13 + room };
        CheckInput input = { 0 };
        CheckInputReader reader = { .output = &input, .types = types, .scratch = &scratch };
        uint32_t failed = 0;
        const bool valid = check_input_record_names(&reader, members, count, UINT32_MAX, &failed);
        accepted += valid;
        exhausted += input.resource;
        bool intact = scratch.length == 13;
        for (uint32_t i = 0; i < sizeof(bytes); i += 1)
        {
            intact &= (i >= 13 && i < scratch.capacity) || bytes[i] == 0xa5;
        }
        check_test_expect(test, intact && valid != input.resource && (!valid || failed == BUSTER_CHECK_NONE),
                          "record names restore scratch and preserve canaries at every capacity, including table and stack growth");
    }
    check_test_expect(test, accepted != 0 && exhausted != 0, "record name scratch sweep exercises both success and explicit exhaustion");
}

static void check_test_record_names(CheckTest* test)
{
    CheckTestTypes types = { 0 };
    Arena scratch = arena_reserve(16384);
    bool ready = check_test_types_open(&types) && scratch.base != 0;
    TypeMember leaves[65];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(leaves); i += 1)
    {
        leaves[i] = (TypeMember){ .name = 100 + i * 256, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    }
    TypeId record = ready ? type_tag(&types.table, BUSTER_TYPE_KIND_STRUCT, 1) : 0;
    const TypeRecordAttributes attributes = { 0 };
    ready = ready && record != 0 && type_complete_record(&types.table, record, leaves, BUSTER_ARRAY_LENGTH(leaves), attributes);
    // Alternate structs and unions, with aligned wrappers on anonymous members. The leaf IDs collide in the small
    // tables, so a successful expansion also checks that every resize rehashes the complete previous namespace.
    for (uint32_t level = 0; ready && level < 12; level += 1)
    {
        const TypeMember member = { .type = type_aligned(&types.table, record, 3), .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
        record = type_tag(&types.table, level % 2 == 0 ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT, level + 2);
        ready = record != 0 && type_complete_record(&types.table, record, &member, 1, attributes);
    }
    check_test_expect(test, ready, "record namespace fixture completes its nested anonymous types");
    if (ready)
    {
        CheckInput input = { 0 };
        CheckInputReader reader = { .output = &input, .types = &types.table, .scratch = &scratch };
        uint32_t failed = 0;
        TypeMember members[] = {
            { .type = record, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
            { .name = 3, .type = record, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
            { .type = BUSTER_TYPE_INT, .bit_width = 0 },
        };
        check_test_expect(test, check_input_record_names(&reader, 0, 0, 0, &failed) && failed == BUSTER_CHECK_NONE && !input.resource,
                          "empty record has an empty namespace");
        check_test_expect(test, check_input_record_names(&reader, members, BUSTER_ARRAY_LENGTH(members), 100, &failed) &&
                               failed == BUSTER_CHECK_NONE && !input.resource,
                          "anonymous expansion grows across hash and stack boundaries, ignoring named aggregates and unnamed bit-fields");
        members[1].name = leaves[0].name;
        check_test_expect(test, !check_input_record_names(&reader, members, BUSTER_ARRAY_LENGTH(members), 100, &failed) && failed == 1 && !input.resource,
                          "duplicate after anonymous expansion and rehashing reports its later direct member");
        members[0].name = leaves[0].name;
        members[1].name = 0;
        check_test_expect(test, !check_input_record_names(&reader, members, BUSTER_ARRAY_LENGTH(members), 100, &failed) && failed == 1 && !input.resource,
                          "duplicate inside anonymous expansion reports its containing direct member");
        members[0].name = 0;
        check_test_expect(test, !check_input_record_names(&reader, members, BUSTER_ARRAY_LENGTH(members), 100, &failed) && failed == 1 && !input.resource,
                          "two anonymous paths exposing the same namespace report the later path");
        check_test_expect(test, !check_input_record_names(&reader, members, 1, 0, &failed) && failed == 0 && !input.resource,
                          "anonymous depth remains bounded independently of hash capacity");
        const TypeId empty = type_tag(&types.table, BUSTER_TYPE_KIND_STRUCT, 1001);
        const TypeMember empty_member = { .type = empty, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
        check_test_expect(test, empty != 0 && type_complete_record(&types.table, empty, 0, 0, attributes) &&
                               check_input_record_names(&reader, &empty_member, 1, 1, &failed) && failed == BUSTER_CHECK_NONE && !input.resource,
                          "an empty anonymous record introduces no names");
        const TypeMember invalid = { .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
        check_test_expect(test, !check_input_record_names(&reader, &invalid, 1, 100, &failed) && failed == 0 && !input.resource,
                          "an anonymous nonrecord is a semantic failure");
        const TypeMember incomplete = { .type = type_tag(&types.table, BUSTER_TYPE_KIND_STRUCT, 1000), .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
        check_test_expect(test, !check_input_record_names(&reader, &incomplete, 1, 100, &failed) && failed == 0 && !input.resource,
                          "an incomplete anonymous record is a semantic failure");
        members[0].name = 3;
        check_test_record_name_limits(test, &types.table, members, 1, 32);
        members[0].name = 0;
        check_test_record_name_limits(test, &types.table, members, 1, 4096);
    }
    arena_release(&scratch);
    check_test_types_close(&types);
}
