// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse reader adapter and actual deferred remapper on independent payload/type maps and protected prefixes.

static void check_test_warning_deferred(CheckTest* test)
{
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena private_output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    CheckTestTypes types = { 0 };
    const bool typed = check_test_types_open(&types);
    static Parser parser;
    static Preprocessor pp;
    parser = (Parser){ 0 };
    pp = (Preprocessor){ 0 };
    parser.tokens.count = 32;
    pp.output.count = 64;
    CheckInput input = { 0 };
    CheckInputReader reader = {
        .output = &input,
        .parser = &parser,
        .pp = &pp,
        .types = &types.table,
        .arena = &output,
        .warning_events = { .arena = &output },
    };
    const TypeId old_pointer = typed ? type_pointer(&types.table, BUSTER_TYPE_INT) : 0;
    const TypeId old_array = old_pointer != 0 ? type_array(&types.table, BUSTER_TYPE_INT, 3) : 0;
    const TypeId new_pointer = old_array != 0 ? type_pointer(&types.table, BUSTER_TYPE_DOUBLE) : 0;
    const TypeId new_array = new_pointer != 0 ? type_array(&types.table, BUSTER_TYPE_DOUBLE, 4) : 0;
    const TypeId first = old_pointer | BUSTER_TYPE_CONST;
    const TypeId second = old_array | BUSTER_TYPE_VOLATILE;
    const CheckWarningEvent event = {
        .id = CHECK_WARNING_ID_NON_LITERAL_NULL,
        .kind = CHECK_WARNING_EVENT_NON_LITERAL_NULL,
        .token = 3,
        .source = 7,
        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = 14,
        .payload = {
            .first_type = first,
            .name = 17,
            .token = 5,
        },
    };
    const CheckWarningNote note = {
        .kind = CHECK_WARNING_NOTE_USED_HERE,
        .token = 9,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .payload = { .second_type = second },
    };
    const bool retained = new_array != 0 && type_is_valid(&types.table, first) && type_is_valid(&types.table, second) &&
                          type_index(old_array) == type_index(old_pointer) + 1 && check_input_warning_event(&reader, &event, &note, 1);
    check_test_expect(test, retained && !input.resource && input.report_count == 0 && input.warning_events.count == 1 &&
                           input.warning_events.note_count == 1 && input.warning_events.events[0].completion == 14,
                      "reader warning adapter retains source/completion and notes without creating a constraint report");
    if (retained)
    {
        const CheckWarningEvents prefix = input.warning_events;
        CheckInput lane_input = input;
        CheckInputReader lane_reader = reader;
        lane_reader.output = &lane_input;
        lane_reader.arena = &private_output;
        lane_reader.warning_events = (CheckWarningBuffer){ .arena = &private_output };
        lane_input.warning_events = (CheckWarningEvents){ 0 };
        const bool added = check_input_warning_event(&lane_reader, &event, &note, 1);
        check_test_expect(test, added && lane_input.warning_events.count == 1 && reader.warning_events.count == 1 &&
                               lane_reader.warning_events.events != reader.warning_events.events && prefix.events[0].note_first == 0 &&
                               prefix.events[0].payload.first_type == first && prefix.notes[0].payload.second_type == second,
                          "a copied reader owns a private warning suffix and cannot append into the borrowed top-level prefix");
        uint32_t mapped[] = { type_index(new_pointer), type_index(new_array) };
        const TypeReplayPart part = { .base = type_index(old_pointer), .types = mapped };
        CheckWarningBuffer merged = { .arena = &private_output };
        uint8_t* guard = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, 64);
        if (guard != NULL)
        {
            memset(guard, 0xa5, 64);
        }
        const uint64_t mark = scratch.length;
        const bool remapped = guard != NULL && check_deferred_warnings(&merged, &prefix, &part, &scratch);
        check_test_expect(test, remapped && merged.count == 1 && merged.note_count == 1 && scratch.length == mark &&
                               merged.events[0].payload.first_type == (new_pointer | BUSTER_TYPE_CONST) &&
                               merged.notes[0].payload.second_type == (new_array | BUSTER_TYPE_VOLATILE) &&
                               merged.events[0].token == event.token && merged.events[0].source == event.source &&
                               merged.events[0].completion == event.completion && merged.events[0].payload.name == event.payload.name &&
                               prefix.events[0].payload.first_type == first && prefix.notes[0].payload.second_type == second,
                          "actual deferred warning merge maps only typed fields and preserves original source identities and immutable payloads");
        bool guarded = guard != NULL;
        for (uint32_t i = 0; guarded && i < 64; i += 1)
        {
            guarded = guard[i] == 0xa5;
        }
        check_test_expect(test, guarded, "deferred note scratch leaves the existing arena prefix untouched");
        CheckWarningEvent bad = event;
        bad.note_first = 1;
        bad.note_count = 1;
        const CheckWarningEvents invalid = { .events = &bad, .count = 1, .notes = &note, .note_count = 1 };
        const uint32_t before = merged.count;
        check_test_expect(test, !check_deferred_warnings(&merged, &invalid, &part, &scratch) && merged.count == before && scratch.length == mark,
                          "invalid attached-note spans cannot reach a remap or publish a partial primary");
        CheckWarningEvent outside = event;
        outside.payload.token = parser.tokens.count;
        check_test_expect(test, !check_input_warning_event(&lane_reader, &outside, NULL, 0) && lane_input.warning_events.count == 1,
                          "a related payload location is bounded independently of its valid primary location");
        outside = event;
        outside.completion = parser.tokens.count + 1;
        check_test_expect(test, !check_input_warning_event(&lane_reader, &outside, NULL, 0) && lane_input.resource &&
                               lane_input.warning_events.count == 1 && input.warning_events.count == 1,
                          "an out-of-range completion fails the owning reader without corrupting either frozen view");
    }
    arena_release(&scratch);
    check_test_types_close(&types);
    arena_release(&private_output);
    arena_release(&output);
}

static void check_test_warning_set(CheckTest* test)
{
    Arena arena = arena_reserve(UINT64_C(1) << 20);
    Arena private_arena = arena_reserve(UINT64_C(1) << 20);
    CheckWarningSet set = { .arena = &arena };
    check_test_expect(test, !check_warning_set_contains(&set, 1) && arena.length == 0, "an empty exceptional fact set allocates nothing");
    bool valid = true;
    for (uint32_t i = 1; valid && i <= 257; i += 1)
    {
        valid = check_warning_set_add(&set, i * 17);
    }
    check_test_expect(test, valid && set.count == 257, "exceptional fact hashes retain independent keys across capacity boundaries");
    CheckWarningSet lane = set;
    lane.arena = &private_arena;
    lane.borrowed = true;
    const uint32_t* prefix = set.keys;
    valid = valid && check_warning_set_contains(&lane, 17) && check_warning_set_add(&lane, 7) && check_warning_set_add(&lane, 7);
    check_test_expect(test, valid && lane.count == 258 && set.count == 257 && lane.keys != prefix && set.keys == prefix &&
                           !check_warning_set_contains(&set, 7) && check_warning_set_contains(&lane, 7),
                      "a lane's first insertion copies its frozen table and repeated source facts are idempotent");
    Arena limited = private_arena;
    limited.capacity = limited.length;
    CheckWarningSet failed = set;
    failed.arena = &limited;
    failed.borrowed = true;
    const CheckWarningSet before = failed;
    check_test_expect(test, !check_warning_set_add(&failed, 9) && memcmp(&failed, &before, sizeof(before)) == 0 &&
                           !check_warning_set_contains(&set, 9), "a failed private fact-table allocation preserves the borrowed prefix and state");
    arena_release(&private_arena);
    arena_release(&arena);
}
