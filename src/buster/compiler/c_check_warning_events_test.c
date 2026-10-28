// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse carrier ownership and failure tests. These assert independent record/note identities, not rendered text.

static void check_test_warning_events(CheckTest* test)
{
    Arena arena = arena_reserve(UINT64_C(1) << 20);
    Arena suffix_arena = arena_reserve(UINT64_C(1) << 20);
    CheckWarningBuffer buffer = { .arena = &arena };
    CheckWarningBuffer suffix = { .arena = &suffix_arena };
    const CheckWarningEvents empty = check_warning_view(&buffer);
    check_test_expect(test,
                      empty.count == 0 && empty.events == NULL && empty.note_count == 0 && empty.notes == NULL,
                      "an empty warning view needs no event or note allocation");
    CheckWarningEvents prefix = { 0 };
    for (uint32_t i = 0; i < 65; i += 1)
    {
        const CheckWarningEvent event = {
            .id = CHECK_WARNING_ID_GNU_CONSTANT,
            .kind = i % 2 == 0 ? CHECK_WARNING_EVENT_GNU_ARRAY_FOLD : CHECK_WARNING_EVENT_GNU_ICE_FOLD,
            .token = i * 3,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .flags = i % 2 == 0 ? 0 : BUSTER_CHECK_WARNING_DEFAULT_IGNORED,
            .completion_kind = i % 2 == 0 ? CHECK_WARNING_COMPLETE_AT_TOKEN : CHECK_WARNING_COMPLETE_AFTER_DELIMITER,
            .completion = i * 3 + 1,
            .payload = {
                .first_type = i + 1,
                .name = i + 7,
                .token = i * 3 + 1,
            },
        };
        const CheckWarningNote note = {
            .kind = CHECK_WARNING_NOTE_USED_HERE,
            .token = i * 3 + 2,
            .source = i * 5,
        };
        const uint32_t notes = i % 3 != 0;
        const bool appended = check_warning_append(&buffer, &event, &note, notes);
        check_test_expect(test, appended && buffer.count == i + 1, "sparse warning append grows complete primaries and attached-note spans");
        if (i == 7)
        {
            prefix = check_warning_view(&buffer);
        }
    }
    check_test_expect(test, buffer.count == 65 && prefix.count == 8, "warning carrier fixture has its required complete prefixes");
    if (buffer.count == 65 && prefix.count == 8)
    {
        const CheckWarningEvents view = check_warning_view(&buffer);
        uint32_t notes = 0;
        bool exact = view.count == 65 && prefix.count == 8;
        for (uint32_t i = 0; exact && i < view.count; i += 1)
        {
            const CheckWarningEvent* event = view.events + i;
            const uint32_t count = i % 3 != 0;
            exact = event->id == CHECK_WARNING_ID_GNU_CONSTANT && event->token == i * 3 && event->completion == i * 3 + 1 &&
                    event->completion_kind == (i % 2 == 0 ? CHECK_WARNING_COMPLETE_AT_TOKEN : CHECK_WARNING_COMPLETE_AFTER_DELIMITER) &&
                    event->payload.first_type == i + 1 && event->payload.name == i + 7 &&
                    event->note_first == notes && event->note_count == count && event->flags == (i % 2 == 0 ? 0 : BUSTER_CHECK_WARNING_DEFAULT_IGNORED);
            if (count != 0)
            {
                const CheckWarningNote* note = view.notes + notes;
                exact = exact && note->kind == CHECK_WARNING_NOTE_USED_HERE && note->token == i * 3 + 2 && note->source == i * 5;
            }
            if (i < prefix.count)
            {
                exact = exact && memcmp(prefix.events + i, event, sizeof(*event)) == 0;
            }
            notes += count;
        }
        check_test_expect(test, exact && notes == view.note_count, "growth preserves all independently stated event payloads and prior immutable views");
        const CheckWarningEvent original = view.events[2];
        const bool private_append = check_warning_append(&suffix, &original, view.notes + original.note_first, original.note_count);
        check_test_expect(test,
                          private_append && suffix.count == 1 && suffix.note_count == original.note_count && suffix.events[0].note_first == 0 &&
                              memcmp(&original, view.events + 2, sizeof(original)) == 0 && prefix.events[2].note_first == original.note_first,
                          "a private deferred suffix rebases attached notes without modifying the borrowed prefix");
        const CheckWarningEvent late = {
            .id = CHECK_WARNING_ID_UNDEFINED_INTERNAL,
            .kind = CHECK_WARNING_EVENT_UNDEFINED_INTERNAL,
            .token = 4,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_TU_END,
            .completion = BUSTER_CHECK_WARNING_NO_SOURCE,
        };
        check_test_expect(test, check_warning_append(&suffix, &late, NULL, 0) && suffix.events[1].id == CHECK_WARNING_ID_UNDEFINED_INTERNAL &&
                               suffix.events[1].completion_kind == CHECK_WARNING_COMPLETE_TU_END && suffix.events[1].completion == UINT32_MAX,
                          "high policy IDs and TU completion are retained independently of diagnostic location");
        CheckWarningEvent invalid_completion = late;
        invalid_completion.completion = 4;
        check_test_expect(test, !check_warning_append(&suffix, &invalid_completion, NULL, 0) && suffix.count == 2,
                          "a TU completion cannot silently reuse a parser-location boundary");
        // Force the next append to grow, then deny that allocation through a copied arena limit. The live arena and
        // the old event/note views remain valid regardless of which of the two arrays would otherwise grow first.
        bool ready = true;
        while (ready && buffer.count < buffer.capacity)
        {
            const bool appended = check_warning_append(&buffer, &original, NULL, 0);
            check_test_expect(test, appended, "carrier reaches a capacity boundary without changing its frozen prefix");
            ready = appended;
        }
        Arena limited = arena;
        limited.capacity = limited.length;
        CheckWarningBuffer rejected = buffer;
        rejected.arena = &limited;
        const CheckWarningBuffer before = rejected;
        const uint64_t mark = limited.length;
        const bool accepted = check_warning_append(&rejected, &original, NULL, 0);
        check_test_expect(test,
                          !accepted && limited.length == mark && memcmp(&before, &rejected, sizeof(before)) == 0 &&
                              memcmp(view.events + 2, &original, sizeof(original)) == 0,
                          "failed carrier growth rolls back allocation and publishes no partial event or note state");
        CheckWarningEvent invalid = original;
        invalid.id = CHECK_WARNING_ID_COUNT;
        check_test_expect(test,
                          !check_warning_append(&buffer, &invalid, NULL, 0) && arena.length == mark,
                          "invalid warning IDs cannot index a policy descriptor or change output");
        Arena notes_arena = arena_reserve(UINT64_C(1) << 16);
        CheckWarningBuffer with_notes = { .arena = &notes_arena };
        const CheckWarningNote note = {
            .kind = CHECK_WARNING_NOTE_USED_HERE,
            .token = 2,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        };
        bool populated = true;
        for (uint32_t i = 0; populated && i < 16; i += 1)
        {
            populated = check_warning_append(&with_notes, &original, &note, 1);
        }
        check_test_expect(test, populated, "two-array failure fixture has complete old event and note prefixes");
        if (populated)
        {
            Arena note_limit = notes_arena;
            note_limit.capacity = note_limit.length + 32 * sizeof(CheckWarningEvent);
            CheckWarningBuffer partial = with_notes;
            partial.arena = &note_limit;
            const CheckWarningBuffer old = partial;
            const uint64_t old_length = note_limit.length;
            const bool published = check_warning_append(&partial, &original, &note, 1);
            check_test_expect(test, !published && note_limit.length == old_length && memcmp(&partial, &old, sizeof(old)) == 0 &&
                                   old.events[0].token == original.token && old.notes[0].token == note.token,
                              "a failed note allocation also rolls back a successful event-array growth");
        }
        arena_release(&notes_arena);
    }
    arena_release(&suffix_arena);
    arena_release(&arena);
}
