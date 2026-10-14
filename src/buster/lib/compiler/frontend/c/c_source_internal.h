#pragma once

// Private source-map and once-file table contracts. Test hooks are absent
// from tests-disabled builds; file identity remains separate from spelling.
#include <buster/lib/compiler/frontend/c/c.h>

// Lowering sees source bytes, never token grammar. The resolved length keeps
// oversized spellings exact without exposing the token's sentinel encoding.
typedef struct CSourceAnchor CSourceAnchor;
struct CSourceAnchor
{
    u32 offset;
    u32 length;
};

BUSTER_F_DECL CSourceAnchor c_source_anchor_at(CPreprocessResult const* preprocess, u32 index);
BUSTER_F_DECL CSourceAnchor c_source_anchor_from_name(CPreprocessResult const* preprocess, String8 name);
BUSTER_F_DECL String8 c_source_anchor_spelling(CPreprocessResult const* preprocess, CSourceAnchor anchor);
BUSTER_F_DECL u32 c_source_anchor_source_cursor(CPreprocessResult const* preprocess, CSourceAnchor anchor, IrSourceMapCursor* cursor);
BUSTER_F_DECL CSourceSite c_source_anchor_site_cursor(CPreprocessResult const* preprocess, CSourceAnchor anchor, IrSourceMapCursor* cursor);
BUSTER_F_DECL CSourceLocation c_source_anchor_location_cursor(CPreprocessResult const* preprocess, CSourceAnchor anchor, IrSourceMapCursor* cursor);

// Required frontend reservations have one failure contract. The calling-thread
// test seam selects a phase-local creation, then arms the allocator's existing
// one-shot reserve failure immediately before it (bypassing pool reuse).
typedef enum CFrontendReservationPhase
{
    C_FRONTEND_RESERVATION_PREPROCESS,
    C_FRONTEND_RESERVATION_ANALYSIS,
    C_FRONTEND_RESERVATION_LOWERING,
} CFrontendReservationPhase;

BUSTER_F_DECL Arena* c_frontend_arena_create(ArenaCreation creation, CFrontendReservationPhase phase);
#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL void c_test_fail_frontend_reservation(CFrontendReservationPhase phase, u32 ordinal);
BUSTER_F_DECL bool c_test_frontend_reservation_pending(void);
// Shrink only the ordinary function arena so a small fixture reaches growth.
BUSTER_F_DECL void c_test_lowering_initial_reservation(u64 size);
#endif

// Translation keeps offsets and checkpoint counts in u32. Reserve one byte
// for the terminator and two checkpoint slots without narrowing the counts.
#define C_SOURCE_MAXIMUM_LENGTH ((u64)UINT32_MAX - 2)

typedef struct CSourceAllocationPlan CSourceAllocationPlan;
struct CSourceAllocationPlan
{
    u64 translated_capacity;
    u64 checkpoint_capacity;
};

typedef struct CIncludeFileIdentity CIncludeFileIdentity;
struct CIncludeFileIdentity
{
    // Path key for builtins and other non-filesystem namespaces. Filesystem
    // bytes use the identity captured from their supplying descriptor instead;
    // physical identities compare their device/index pair and ignore this path.
    String8 path;
    u64 device;
    u64 index;
    bool physical;
};

// One identity record drives all three ways a file can suppress a later
// inclusion: #import, #pragma once, and a proven whole-file include guard.
// `spelling` remains the first resolved path for diagnostics/source maps while
// `identity` is the comparison key. Keeping those roles separate prevents a
// canonical identity from degrading a useful diagnostic spelling.
typedef struct CIncludeFileEntry CIncludeFileEntry;
struct CIncludeFileEntry
{
    // With a path key this is both the key and diagnostic spelling. With a
    // physical key it remains only the spelling users should see.
    String8 spelling;
    u64 hash;
    u64 device;
    u64 index;
    u32 guard_symbol;
    // Zero means unrecorded; otherwise this is the dependency index plus one.
    u32 dependency_slot;
    bool physical;
    bool once;
    bool included;
};

typedef struct CIncludeFileTable CIncludeFileTable;
struct CIncludeFileTable
{
    Arena* arena;
    CIncludeFileEntry* entries;
    u32 count;
    u32 capacity;
#if BUSTER_INCLUDE_TESTS
    // Actual slot examinations, including reinsertion while growing.
    u64 probe_count;
#endif
};

typedef enum CIncludeFileStatus
{
    C_INCLUDE_FILE_OK,
    C_INCLUDE_FILE_INVALID_IDENTITY,
    C_INCLUDE_FILE_ALLOCATION_FAILED,
} CIncludeFileStatus;

#define C_INCLUDE_FILE_INITIAL_CAPACITY 64

#if BUSTER_INCLUDE_TESTS
// Exercise the production expanded-line commit with an empty foreign token.
BUSTER_F_DECL bool c_test_expanded_empty_foreign_token_source_map(Arena* arena);
BUSTER_F_DECL bool c_test_source_allocation_plan(u64 length, CSourceAllocationPlan* plan);
// Includes lex into a shared spelling space; exercise that production entry
// without creating or mapping a multi-gigabyte file.
BUSTER_F_DECL CLexResult c_test_lex_include_source(Arena* arena, String8 source);
// Preserve public GNU17 lexing while comparing each dialect's phase-one path.
BUSTER_F_DECL CLexResult c_test_lex_dialect(Arena* arena, String8 source, CPreprocessDialect dialect, bool force_scalar);
BUSTER_F_DECL CIncludeFileStatus c_test_include_file_entry(CIncludeFileTable* table, CIncludeFileIdentity identity, String8 spelling, CIncludeFileEntry** entry_out);
BUSTER_F_DECL bool c_test_include_file_table_grow(CIncludeFileTable* table);
BUSTER_F_DECL void c_test_source_map_sort(Arena* arena, IrSourceRegion* regions, u32 count);
// A test calls c_preprocess directly on a rewindable arena and never owns the
// result's private arenas past the fixture. Every c_preprocess records them
// here until c_preprocess_release returns them; this releases whatever the
// calling thread's fixture left, so a module of hundreds of fixtures holds a
// bounded address space. Only for use once nothing reads the fixture's results.
BUSTER_F_DECL void c_test_release_preprocess_arenas(void);
// Per-thread log of the address ranges c_preprocess_release returned since
// the last reset; `contains` says whether a non-empty object starts inside one,
// that is, whether a result still reads released memory.
BUSTER_F_DECL void c_test_released_log_reset(void);
BUSTER_F_DECL bool c_test_released_log_contains(void const* pointer, u64 length);
// Releases the units whose results live in `owner` at or above `position`:
// what a rewind of that arena to `position` is about to discard. A null owner
// releases every unit of the thread.
BUSTER_F_DECL void c_test_release_preprocess_arenas_from(Arena* owner, u64 position);
// The calling thread's next registration serial. Releasing `since` a mark
// returns every unit the thread preprocessed after taking it and has not
// released, whatever arena its result lives in. The test harness takes a mark
// when each fixture and module body begins and releases since it when the
// scope ends, so no unit outlives the scope that created it (GitHub #3431).
BUSTER_F_DECL u64 c_test_preprocess_unit_mark(void);
BUSTER_F_DECL void c_test_release_preprocess_arenas_since(u64 mark);
// scratch_end for a test that preprocessed into the scratch arena: releases
// the units the rewind discards, then rewinds.
BUSTER_F_DECL void c_test_scratch_end(TemporalArena temporary);
// BUSTER_TEST_FIXTURE, then release what the fixture preprocessed.
#define C_TEST_FIXTURE(arguments, function) \
    do \
    { \
        BUSTER_TEST_FIXTURE(arguments, function); \
        c_test_release_preprocess_arenas(); \
    } while (0)
// The thread's unit registry reserves its own arena at its first unit. A
// module whose fixtures check live reservations creates it before the first.
BUSTER_F_DECL void c_test_unit_registry_prewarm(void);
// C_TEST_FIXTURE that also requires the fixture to return every reservation
// it made. The reading is taken inside the fixture's scope, before the scope's
// end releases the units it left, so a unit kept alive across the fixture
// fails here instead of being released silently. On failure it reports both
// readings (GitHub #3306, #3431). Like BUSTER_TEST_FIXTURE, it calls through a
// volatile pointer to keep the fixture out of line (#2816).
#define C_TEST_BOUNDED_FIXTURE(arguments, function) \
    do \
    { \
        TestArenaScope fixture_scope_ = buster_test_arena_begin((arguments), (arguments)->arena, S8(#function), false); \
        TestFunction* volatile fixture_function_ = function; \
        u64 live_ = arena_test_live_reserved_bytes(); \
        UnitTestResult fixture_result_ = fixture_function_(arguments); \
        u64 left_ = arena_test_live_reserved_bytes(); \
        result.test_count += fixture_result_.test_count; \
        result.succeeded_test_count += fixture_result_.succeeded_test_count; \
        buster_test_arena_end((arguments), fixture_scope_, true); \
        BUSTER_TEST_RAW(arguments, left_ == live_, \
                        string_format((arguments)->arena, S8("{S8}: {u64} reserved bytes live before, {u64} after"), S8(#function), live_, left_)); \
        c_test_release_preprocess_arenas(); \
    } while (0)
// Test the private append-only finalization boundary, not arbitrary map edits.
BUSTER_F_DECL void c_test_source_map_publish_appended(Arena* arena, CSourceMapRecovery* recovery, IrSourceRegion* regions, u32 count, u32 capacity);
// Ownership check for the preprocessing phase boundary: the number of
// pointers anywhere in the result graph (symbol table internals included)
// that lie in [start, end). Written field by field, independently of the
// seal, so a released phase range must report zero.
BUSTER_F_DECL u64 c_test_preprocess_references_range(CPreprocessResult const* result, void const* start, void const* end);
// The identifier table's production entry points: `intern` inserts, `find`
// answers the id a spelling already has and never inserts, `count` is the
// number of ids the table has handed out.
BUSTER_F_DECL u32 c_test_symbol_intern(CSymbolTable* table, String8 name);
BUSTER_F_DECL u32 c_test_symbol_find(CSymbolTable const* table, String8 name);
BUSTER_F_DECL u32 c_test_symbol_count(CSymbolTable const* table);
// The spelling id `symbol` names, or empty for 0 and ids past the count.
BUSTER_F_DECL String8 c_test_symbol_name(CSymbolTable const* table, u32 symbol);
// Predefined object-like macro texts take a direct tokenizer instead of the
// lexer when their bytes allow it (c_predefined_text_tokens). `compare` lexes
// one text into a fresh space and says whether the direct tokens agree with
// the lexer's, or that the text is left to the lexer.
typedef enum CTestPredefinedTextResult
{
    C_TEST_PREDEFINED_TEXT_LEXED,
    C_TEST_PREDEFINED_TEXT_AGREES,
    C_TEST_PREDEFINED_TEXT_DISAGREES,
} CTestPredefinedTextResult;
BUSTER_F_DECL CTestPredefinedTextResult c_test_predefined_text_compare(Arena* arena, String8 text);
// The calling thread's prelude: `force_lexer` lexes every text; `verify`
// also compares each text as above and counts it until taken.
typedef struct CTestPredefinedTextCounts CTestPredefinedTextCounts;
struct CTestPredefinedTextCounts
{
    u64 direct;
    u64 lexed;
    // Lexed texts that are not string literals.
    u64 lexed_unquoted;
    u64 disagreements;
};
BUSTER_F_DECL void c_test_predefined_text_mode(bool force_lexer, bool verify);
BUSTER_F_DECL CTestPredefinedTextCounts c_test_predefined_text_take(void);
#endif
