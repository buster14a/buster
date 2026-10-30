// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse semantic warning facts, shared by readers, checked streams and initializer plans. IDs name policy, not
// bits in a token/node column. A published view is immutable: later appends never change its prefix. Deferred
// readers borrow a frozen prefix and own separate suffix buffers; the unit merge copies/remaps only those suffixes.
// Include after the arena and TypeId definitions. No function here chooses severity or renders text.
#ifndef BUSTER_C_CHECK_WARNING_EVENTS_INCLUDED
#define BUSTER_C_CHECK_WARNING_EVENTS_INCLUDED

#include "c_check_warning_ids.c"

_Static_assert(CHECK_WARNING_BASE_ID_COUNT == 25, "sparse warning IDs follow the stable shared prefix");

enum
{
    CHECK_WARNING_ID_NON_LITERAL_NULL = 25,
    CHECK_WARNING_ID_LITERAL_RANGE = 26,
    CHECK_WARNING_ID_ORDERED_POINTER_INTEGER = 27,
    CHECK_WARNING_ID_MISSING_DECLARATIONS = 28,
    CHECK_WARNING_ID_PLAIN_COMPLEX = 29,
    CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER = 30,
    CHECK_WARNING_ID_STATIC_LOCAL_INLINE = 31,
    CHECK_WARNING_ID_MANY_SCALAR_BRACES = 32,
    CHECK_WARNING_ID_INITIALIZER_OVERRIDES = 33,
    CHECK_WARNING_ID_UNDEFINED_INTERNAL = 34,
    CHECK_WARNING_ID_C23_PARAMETER = 35,
    CHECK_WARNING_ID_UNRECOGNIZED_PRAGMA = 36,
    CHECK_WARNING_ID_IGNORED_PRAGMA = 37,
    CHECK_WARNING_ID_SOURCE_OPENMP = 38,
    CHECK_WARNING_ID_COUNT,
};

typedef enum CheckWarningEventKind
{
    CHECK_WARNING_EVENT_MESSAGE,
    CHECK_WARNING_EVENT_LATE_ATTRIBUTE,
    CHECK_WARNING_EVENT_GNU_INLINE_WITHOUT_INLINE,
    CHECK_WARNING_EVENT_GNU_ARRAY_FOLD,
    CHECK_WARNING_EVENT_GNU_ICE_FOLD,
    CHECK_WARNING_EVENT_NON_LITERAL_NULL,
    CHECK_WARNING_EVENT_LITERAL_OVERFLOW,
    CHECK_WARNING_EVENT_LITERAL_UNDERFLOW,
    CHECK_WARNING_EVENT_ORDERED_POINTER_INTEGER,
    CHECK_WARNING_EVENT_EMPTY_DECLARATION,
    CHECK_WARNING_EVENT_PLAIN_COMPLEX,
    CHECK_WARNING_EVENT_VARIABLE_SIZED_MEMBER,
    CHECK_WARNING_EVENT_STATIC_LOCAL_INLINE,
    CHECK_WARNING_EVENT_MANY_SCALAR_BRACES,
    CHECK_WARNING_EVENT_INITIALIZER_OVERRIDES,
    CHECK_WARNING_EVENT_UNDEFINED_INTERNAL,
    CHECK_WARNING_EVENT_C23_PARAMETER,
    // A retained directive is lexical, including at parser EOF; source and payload.token are PP coordinates.
    CHECK_WARNING_EVENT_PRAGMA,
    CHECK_WARNING_EVENT_KIND_COUNT,
} CheckWarningEventKind;

typedef enum CheckWarningNoteKind
{
    CHECK_WARNING_NOTE_PRIOR_INITIALIZATION,
    CHECK_WARNING_NOTE_INLINE_LINKAGE,
    CHECK_WARNING_NOTE_USED_HERE,
    CHECK_WARNING_NOTE_PREVIOUS_DEFINITION,
    CHECK_WARNING_NOTE_KIND_COUNT,
} CheckWarningNoteKind;

typedef enum CheckWarningCompletion
{
    // Complete with this parser token in lookahead, before consuming it.
    CHECK_WARNING_COMPLETE_AT_TOKEN,
    // Complete after consuming this delimiter and lexing the following parser token (or EOF).
    CHECK_WARNING_COMPLETE_AFTER_DELIMITER,
    // Complete after all EOF preprocessing events; the located declaration still governs source policy.
    CHECK_WARNING_COMPLETE_TU_END,
    CHECK_WARNING_COMPLETION_COUNT,
} CheckWarningCompletion;

// Per-event source policy is independent of the ID: GNU array-bound folds warn by default, whereas an accepted
// nonstandard ICE fold can start ignored under the same -Wgnu-folding-constant policy. Completion/lookahead is also
// independent of the diagnostic location: a later preprocessing diagnostic can precede an earlier source warning.
#define BUSTER_CHECK_WARNING_DEFAULT_IGNORED 0x01u
// A semantic proof prevents publication, independently of every requested warning policy (for example sizeof).
#define BUSTER_CHECK_WARNING_SUPPRESSED 0x02u
#define BUSTER_CHECK_WARNING_NO_SOURCE UINT32_MAX

typedef struct CheckWarningPayload CheckWarningPayload;
struct CheckWarningPayload
{
    // Only these two fields contain TypeIds and need type remapping when a deferred body's facts are merged.
    // A nonzero ID must be valid in the producing reader's type table; zero means that the kind needs no type.
    TypeId first_type;
    TypeId second_type;
    // An IdentId in source consumers; standalone graph clients need not include the interner.
    uint32_t name;
    // Related TU parser token when the kind uses one, or NO_SOURCE. Kinds without a related location ignore it;
    // their zero-initialized field is also a valid token whenever a primary can be published.
    uint32_t token;
    uint32_t data;
};

typedef struct CheckWarningEvent CheckWarningEvent;
struct CheckWarningEvent
{
    uint32_t id;
    uint32_t kind;
    uint32_t token;
    // PP output source index for an exact boundary, or NO_SOURCE for the ordinary source of token.
    uint32_t source;
    uint32_t flags;
    uint32_t completion_kind;
    // Parser token for AT_TOKEN/AFTER_DELIMITER; NO_SOURCE for TU_END. Attached notes inherit this completion.
    uint32_t completion;
    CheckWarningPayload payload;
    uint32_t note_first;
    uint32_t note_count;
};

typedef struct CheckWarningNote CheckWarningNote;
struct CheckWarningNote
{
    uint32_t kind;
    uint32_t token;
    uint32_t source;
    CheckWarningPayload payload;
};

typedef struct CheckWarningEvents CheckWarningEvents;
struct CheckWarningEvents
{
    const CheckWarningEvent* events;
    uint32_t count;
    const CheckWarningNote* notes;
    uint32_t note_count;
};

typedef struct CheckWarningBuffer CheckWarningBuffer;
struct CheckWarningBuffer
{
    Arena* arena;
    CheckWarningEvent* events;
    uint32_t count;
    uint32_t capacity;
    CheckWarningNote* notes;
    uint32_t note_count;
    uint32_t note_capacity;
};

static __attribute__((unused)) CheckWarningEvents check_warning_view(const CheckWarningBuffer* buffer)
{
    const CheckWarningEvents result = {
        .events = buffer->count != 0 ? buffer->events : NULL,
        .count = buffer->count,
        .notes = buffer->note_count != 0 ? buffer->notes : NULL,
        .note_count = buffer->note_count,
    };
    return result;
}

static uint32_t check_warning_capacity(uint32_t capacity, uint32_t needed)
{
    uint32_t result = capacity != 0 ? capacity : 16;
    while (result < needed)
    {
        result = result <= UINT32_MAX / 2 ? result * 2 : needed;
    }
    return result;
}

// Append one primary and its attached notes atomically. Existing prefix records are never rewritten, including
// when either allocation fails. Arena suffix rollback preserves other owners' earlier allocations and views.
static __attribute__((unused)) bool check_warning_append(CheckWarningBuffer* buffer, const CheckWarningEvent* event,
                                                        const CheckWarningNote* notes, uint32_t note_count)
{
    bool valid = buffer != NULL && buffer->arena != NULL && event != NULL && event->id < CHECK_WARNING_ID_COUNT &&
                 event->kind < CHECK_WARNING_EVENT_KIND_COUNT && event->token != UINT32_MAX &&
                 (event->flags & ~(BUSTER_CHECK_WARNING_DEFAULT_IGNORED | BUSTER_CHECK_WARNING_SUPPRESSED)) == 0 && event->completion_kind < CHECK_WARNING_COMPLETION_COUNT &&
                 ((event->completion_kind == CHECK_WARNING_COMPLETE_TU_END) == (event->completion == UINT32_MAX)) &&
                 buffer->count <= buffer->capacity && (buffer->count == 0 || buffer->events != NULL) &&
                 buffer->note_count <= buffer->note_capacity && (buffer->note_count == 0 || buffer->notes != NULL) &&
                 buffer->count < UINT32_MAX && note_count <= UINT32_MAX - buffer->note_count &&
                 (note_count == 0 || notes != NULL);
    for (uint32_t i = 0; valid && i < note_count; i += 1)
    {
        valid = notes[i].kind < CHECK_WARNING_NOTE_KIND_COUNT && notes[i].token != UINT32_MAX;
    }
    if (valid)
    {
        Arena* arena = buffer->arena;
        const uint64_t mark = arena->length;
        const uint32_t count = buffer->count + 1;
        const uint32_t note_end = buffer->note_count + note_count;
        const uint32_t capacity = check_warning_capacity(buffer->capacity, count);
        const uint32_t note_capacity = note_end != 0 ? check_warning_capacity(buffer->note_capacity, note_end) : buffer->note_capacity;
        CheckWarningEvent* events = buffer->events;
        CheckWarningNote* written_notes = buffer->notes;
        if (capacity != buffer->capacity)
        {
            events = BUSTER_ARENA_PUSH_ARRAY(arena, CheckWarningEvent, capacity);
            if (events != NULL && buffer->count != 0)
            {
                memcpy(events, buffer->events, (uint64_t)buffer->count * sizeof(*events));
            }
        }
        if (note_capacity != buffer->note_capacity)
        {
            written_notes = BUSTER_ARENA_PUSH_ARRAY(arena, CheckWarningNote, note_capacity);
            if (written_notes != NULL && buffer->note_count != 0)
            {
                memcpy(written_notes, buffer->notes, (uint64_t)buffer->note_count * sizeof(*written_notes));
            }
        }
        valid = events != NULL && (note_end == 0 || written_notes != NULL);
        if (valid)
        {
            // Save the input before publishing: it may itself be in an earlier immutable event view.
            CheckWarningEvent added = *event;
            added.note_first = buffer->note_count;
            added.note_count = note_count;
            if (note_count != 0)
            {
                memmove(written_notes + buffer->note_count, notes, (uint64_t)note_count * sizeof(*notes));
            }
            events[buffer->count] = added;
            buffer->events = events;
            buffer->count = count;
            buffer->capacity = capacity;
            buffer->notes = written_notes;
            buffer->note_count = note_end;
            buffer->note_capacity = note_capacity;
        }
        else
        {
            arena_pop_to(arena, mark);
        }
    }
    return valid;
}

#endif
