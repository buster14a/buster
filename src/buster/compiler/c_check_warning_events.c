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
    CHECK_WARNING_ID_LIBRARY_REDECLARATION = 39,
    CHECK_WARNING_ID_BUILTIN_HEADER = 40,
    CHECK_WARNING_ID_UNKNOWN_ATTRIBUTE = 41,
    CHECK_WARNING_ID_GCC_COMPAT = 42,
    CHECK_WARNING_ID_GNU_ALIGNOF_EXPRESSION = 43,
    CHECK_WARNING_ID_DEPRECATED_DECLARATIONS = 44,
    CHECK_WARNING_ID_CFSTRING_LITERAL = 45,
    CHECK_WARNING_ID_UNSUPPORTED_AVAILABILITY_GUARD = 46,
    CHECK_WARNING_ID_AVAILABILITY = 47,
    CHECK_WARNING_ID_COUNTED_BY_ELEMENT = 48,
    CHECK_WARNING_ID_EXTERN_INITIALIZER = 49,
    CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES = 50,
    CHECK_WARNING_ID_UNKNOWN_SANITIZERS = 51,
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
    CHECK_WARNING_EVENT_TYPEDEF_NAMELESS,
    CHECK_WARNING_EVENT_STANDALONE_SPECIFIER,
    CHECK_WARNING_EVENT_PLAIN_COMPLEX,
    CHECK_WARNING_EVENT_VARIABLE_SIZED_MEMBER,
    CHECK_WARNING_EVENT_STATIC_LOCAL_INLINE,
    CHECK_WARNING_EVENT_MANY_SCALAR_BRACES,
    CHECK_WARNING_EVENT_INITIALIZER_OVERRIDES,
    CHECK_WARNING_EVENT_UNDEFINED_INTERNAL,
    CHECK_WARNING_EVENT_C23_PARAMETER,
    // A retained directive is lexical, including at parser EOF; source and payload.token are PP coordinates.
    CHECK_WARNING_EVENT_PRAGMA,
    CHECK_WARNING_EVENT_LIBRARY_REDECLARATION,
    CHECK_WARNING_EVENT_BUILTIN_HEADER,
    // An attribute clang doesn't know, which it ignores (#147): payload.name its name's identifier, payload.data its scope's
    // (0 for none) with BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE where it is a C23 attribute ([[...]]) rather than GNU's.
    CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE,
    // `__clang__`'s expansion where a C23 attribute's scope or name goes, which clang reads as `_Clang` (#147, c_parser.c's
    // parse_clang_scope_numbers): -Wignored-attributes' warning at it.
    CHECK_WARNING_EVENT_CLANG_MACRO_SCOPE,
    // An attribute on a declaration of none of its subjects (#147, check_input_attribute_subjects): its payload as
    // CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's, -Wignored-attributes' warning.
    CHECK_WARNING_EVENT_ATTRIBUTE_SUBJECT,
    // A function type's attribute on a declarator of no function type (#147, check_input_attribute_function_types):
    // payload.name the attribute's name, payload.first_type the declarator's type, payload.data its typedef spelling.
    CHECK_WARNING_EVENT_FUNCTION_TYPE_ATTRIBUTE,
    CHECK_WARNING_EVENT_NONSTRING_TYPE,
    CHECK_WARNING_EVENT_TRANSPARENT_UNION,
    // A GNU attribute GCC knows after a function definition's declarator (clang's parser's warning, #147): payload.name
    // its name as written.
    CHECK_WARNING_EVENT_GCC_ATTRIBUTE_POSITION,
    // nonnull's warnings (clang's handleNonNullAttr, #147): an index of a parameter that is no pointer, a function of no
    // pointer parameter, and arguments on a parameter's. payload.name the attribute's name, payload.data its scope's
    // (0 for none) with BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE.
    CHECK_WARNING_EVENT_NONNULL_POINTERS,
    CHECK_WARNING_EVENT_NONNULL_NO_POINTERS,
    CHECK_WARNING_EVENT_NONNULL_PARAMETER_ARGUMENTS,
    // A C23 declaration attribute after a declarator chunk (#147, check_input_standard_place); a gnu:: type attribute
    // there (-Wgcc-compat); a calling convention x86-64 hasn't on a function; an attribute clang ignores in that syntax
    // or place (noderef, vector_size). Payload as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's.
    CHECK_WARNING_EVENT_TYPE_POSITION,
    CHECK_WARNING_EVENT_GCC_TYPE_ATTRIBUTE,
    CHECK_WARNING_EVENT_CONVENTION_UNSUPPORTED,
    CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED,
    // A tag declaration's attribute (#147, check_input_tag_attribute_warning): after a bare declaration's name (its tag's
    // keyword in data: 0 struct, 1 union, 2 enum), or after the tag's definition (data as above, a note at it).
    CHECK_WARNING_EVENT_TAG_ATTRIBUTE_PLACE,
    CHECK_WARNING_EVENT_TAG_ATTRIBUTE_DEFINED,
    // A GNU declaration attribute in a type name (#147, check_input_attribute_in_type_name), named as written.
    CHECK_WARNING_EVENT_TYPE_NAME_ATTRIBUTE,
    // _Alignof or alignof of an expression (#147, check_input_alignof_expression): its keyword in the payload's name.
    CHECK_WARNING_EVENT_ALIGNOF_EXPRESSION,
    // A use of what a deprecated attribute marks (#147, check_input_deprecated_uses): the used name, and the attribute's
    // message's string token in the payload's token (or none).
    CHECK_WARNING_EVENT_DEPRECATED_USE,
    // A CFString builtin's literal that isn't UTF-8 (#148, check_input_cfstring_literal), at the literal.
    CHECK_WARNING_EVENT_CFSTRING_TRUNCATED,
    // A __builtin_available that guards nothing (#148, check_input_availability_prepare), at its keyword, and a version
    // whose separators differ (`1.2_3`), at its number.
    CHECK_WARNING_EVENT_UNGUARDED_AVAILABILITY,
    CHECK_WARNING_EVENT_VERSION_SEPARATORS,
    // A counted_by family attribute on a flexible array member of structs with flexible array members (#148,
    // check_input_counted_by_member), which clang drops: payload.first_type the element's type, payload.name the
    // attribute's kind less one.
    CHECK_WARNING_EVENT_COUNTED_BY_ELEMENT,
    // -Wignored-attributes' warnings of attributes clang's handlers ignore (#147, check_input_attribute_subjects): weak_import
    // on a definition, and internal_linkage on a parameter (named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are), at the
    // attribute; internal_linkage on a variable of local storage, at the variable's name; nocf_check without -fcf-protection,
    // at the attribute (check_input_attribute_read_item).
    CHECK_WARNING_EVENT_ATTRIBUTE_DEFINITION,
    CHECK_WARNING_EVENT_ATTRIBUTE_VARIABLES_FUNCTIONS,
    CHECK_WARNING_EVENT_INTERNAL_LINKAGE_LOCAL,
    CHECK_WARNING_EVENT_NOCF_CHECK_IGNORED,
    // A file-scope extern variable's initializer (clang's warn_extern_init), at the variable's name.
    CHECK_WARNING_EVENT_EXTERN_INITIALIZER,
    // mode's vector mode (#147, check_input_attribute_mode: -Wdeprecated-attributes), at the attribute.
    CHECK_WARNING_EVENT_VECTOR_MODE,
    // alloc_size's and alloc_align's function that returns no pointer (#147, -Wignored-attributes), at the attribute, named
    // as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are.
    CHECK_WARNING_EVENT_RETURN_POINTERS,
    CHECK_WARNING_EVENT_RETURN_POINTERS_REFERENCES,
    // An argument clang's handler doesn't know (#147, -Wignored-attributes): visibility's string, at it, and format's kind,
    // at the attribute; named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are, the argument's token in the payload's token
    // (a string literal's, with those after it, or an identifier, without a keyword's underscores).
    CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED,
    // format's first argument to check on a function that isn't variadic (#147, -Wgcc-compat), at the declaration's name,
    // named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are.
    CHECK_WARNING_EVENT_GCC_VARIADIC,
    // target's string clang's checkTargetAttr doesn't take (#147, check_input_target_warning), at the string: the string's
    // first token in the payload's token, the warning's case and its piece of the string in its data.
    CHECK_WARNING_EVENT_TARGET_UNSUPPORTED,
    // no_sanitize's sanitizer clang doesn't know (-Wunknown-sanitizers), at its string, and one a variable of static
    // storage can't take, at the variable's name (-Wignored-attributes): the string's first token in the payload's token,
    // named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are.
    CHECK_WARNING_EVENT_UNKNOWN_SANITIZER,
    CHECK_WARNING_EVENT_SANITIZER_GLOBAL,
    CHECK_WARNING_EVENT_KIND_COUNT,
} CheckWarningEventKind;

// CHECK_WARNING_EVENT_TRANSPARENT_UNION's case, in its data's low byte (its bits above it), and its note's.
#define BUSTER_CHECK_TRANSPARENT_INCOMPLETE 1u
#define BUSTER_CHECK_TRANSPARENT_EMPTY 2u
#define BUSTER_CHECK_TRANSPARENT_FLOAT 3u
#define BUSTER_CHECK_TRANSPARENT_VECTOR 4u
#define BUSTER_CHECK_TRANSPARENT_SIZE 5u
#define BUSTER_CHECK_TRANSPARENT_ALIGNMENT 6u

typedef enum CheckWarningNoteKind
{
    CHECK_WARNING_NOTE_PRIOR_INITIALIZATION,
    CHECK_WARNING_NOTE_INLINE_LINKAGE,
    CHECK_WARNING_NOTE_USED_HERE,
    CHECK_WARNING_NOTE_PREVIOUS_DEFINITION,
    CHECK_WARNING_NOTE_TRANSPARENT_FIRST_FIELD,
    CHECK_WARNING_NOTE_CONFLICTING_ATTRIBUTE,
    CHECK_WARNING_NOTE_DEPRECATED_HERE,
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
// CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's payload.data bit of a C23 attribute's spelling, past its scope's identifier.
#define BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE 0x80000000u

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
