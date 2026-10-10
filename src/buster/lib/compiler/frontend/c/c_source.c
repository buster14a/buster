// Source loading, lexing, and preprocessing — the first stage of the
// frontend, entered through c_preprocess near the bottom of the file. It
// turns a root file into the flat CToken stream every later stage walks:
// translation (dialect trigraphs, line splices and carriage returns), lexing,
// directive handling, include resolution (real files and the builtin
// resource headers), macro expansion, and the source metrics behind the
// SOURCE table. c_prewarm at the bottom fills the lazily built tables on
// one thread before any parallel phase reads them (AGENTS.md).
//
// Layout, in file order; each anchor is a definition to search for:
//   CCensusState, c_census_record ..           the source-fact census
//   c_census_phase_name                        (c_census.h), compiled only
//                                              into BUSTER_BENCH_ALLOCATIONS
//                                              builds
//   c_space_local .. c_space_retoken           spelling spaces: append-only
//                                              storage token spellings point
//                                              into
//   c_source_map_append, c_source_map_sort,    location checkpoints (see
//   c_lex_token_location                       CTranslatedSource below)
//   c_preprocess_token_site,                   record sites and their on-demand
//   c_preprocess_site_location                 recovery (CSourceSite in c.h)
//   c_identifier_start ..                      character classes and the
//   c_literal_plain_run_end                    prewarmed run tables
//   c_source_allocation_plan, c_translate_source checked source bounds and
//                                              phase-1/2 translation with
//                                              SWAR/AVX-512/scalar variants
//                                              kept in differential agreement
//   c_source_metrics_add,                      the SOURCE table counters and
//   c_source_metrics_file_row                  the per-file attribution rows
//   c_ucn_decode, c_ucn_identifier_allowed      dialect-owned identifier escapes
//   c_lex_validate_word_utf8                   bounded word encoding checks
//   c_punctuator_canonical                     digraph meaning at emission,
//                                              preserving physical spellings
//   c_lex_scan_one, c_lex_scalar               the scalar lexer
//   c_lex_compact_tables_build,                the SIMD lexer (Validark
//   c_lex_compact                              method, AGENTS.md): one
//                                              window-wide class lookup
//                                              (c_lex_byte_classes, authored
//                                              from the scalar predicates
//                                              above), the punctuator NFA and
//                                              spelling tables, then the
//                                              interleaved 16-row emitter;
//                                              c_lex dispatches and
//                                              c_lex_reference is the
//                                              differential baseline
//   CSourceCache, c_source_cache_lex            bounded raw lex templates before
//                                              fresh symbol interning
//   c_token_preceded_by_space,                 source spacing: the `#`/output white-
//   c_token_requires_separator                 space test and the lexical-
//                                              join guard -E and quoting
//                                              diagnostics print through
//   CTokenStream, c_token_stream_reserve       final token rows plus the
//                                              one-byte kind|punctuator
//                                              sidecar consumed by parser
//                                              shape walks
//   c_macro_name_hash .. c_symbol_intern       macro and symbol tables
//   c_semantic_integer_transform_builtin,     fixed unsigned builtin signatures
//   c_integer_transform_bits                  and bounded bit-transform folding
//   c_macro_expansion_tasks_reserve ..         shared LIFO task batches,
//   c_preprocess_expand                        arguments, stringify, paste,
//                                              direct plain production
//                                              (c_macro_produce_plain_tasks),
//                                              rescan, generation ENABLE
//                                              ownership and context floors
//   CPpClassMasks, c_pp_class_masks_build,     the per-64-token class
//   c_pp_line_end_masked,                      projection of a lexed run's
//   c_pp_parenthesis_depth                     shape sidecar, and what the
//                                              driver's per-line scans read
//                                              instead of walking token rows
//   c_preprocess_directive_line_end             comment-aware directive ends
//   c_frame_wrap_conditional_tokens             conditional whitespace and sites
//   c_integer_operation,                       C integer operators over the
//   c_integer_constant_binary/unary            shared ir_integer_* semantics:
//                                              one operation table for runtime
//                                              lowering and every constant
//                                              evaluator
//   c_conditional_* ,                          #if evaluation including
//   c_integer_expression_evaluate              __has_* feature tests
//   c_preprocess_pragma_*,                     pragmas: once, pack, GCC visibility, push/pop
//   c_preprocess_expansion_pragma              macro effects at the rescan cursor
//   COutputSpacingBlock,                      optional expanded-line text
//   c_preprocess_process_expanded_line         boundaries and output materialization
//   CIncludeProbeTable, c_include_read ..      include resolution, its
//   c_include_name                             per-TU probe cache, and the
//                                              builtin resource headers,
//                                              including stddef request guards
//   CIncludeGuardState, CIncludeFileTable,     shared #import, #pragma once,
//   c_include_suppressed                       and #ifndef guard identity
//   c_preprocess_command_operations,           ordered command-line macro
//   c_preprocess_define_directive              operations and shared #define
//                                              parsing
//   c_preprocess_respell_token,                final-stream rewrites: C23 and
//   c_preprocess_respell_identifiers,          UCN respellings, GNU `member:`
//   c_preprocess_rewrite_obsolete_designators, as `.member =`, and block-
//   c_preprocess_rename_local_labels           unique GNU `__label__` names
//   c_preprocess_seal, c_phase_arena_retire    the phase boundary: the result
//                                              copied out of the phase arena
//                                              before its release
//                                              (docs/compiler-lifetime.md)
//   CPreprocessSourceFrame,
//   c_preprocess_lex_diagnostics_release       per-line lexer diagnostic
//                                              release as the frame walk
//                                              enters live and skipped lines
//   c_preprocess                               the stage driver
//   c_prewarm                                  serial table prewarm

#include "c_internal.h"
#include <buster/lib/compiler/frontend/c/c_source_internal.h>
#include <buster/lib/compiler/frontend/c/c_source_metrics_internal.h>
#include "c_vendor_builtin.c"

#if BUSTER_BENCH_ALLOCATIONS
// The source-fact census (c_census.h). The fact map holds one u16 per
// spelling-space offset of the registered space: bits [0, C_CENSUS_PHASE_COUNT)
// record which phases read the spelling at that offset, the bits above them
// which conversion kinds already ran there. It is its own reservation,
// committed in steps as offsets are touched, so it neither perturbs the arena
// allocation counters nor commits memory for untouched offsets.
typedef struct CCensusState CCensusState;
struct CCensusState
{
    CCensusCounters counters;
    u16* facts;
    u64 fact_reserved;
    u64 fact_committed;
    u64 fact_touched;
    char8 const* space_base;
    u64 space_reserved;
    CCensusPhase phase;
    // Library traffic at the last phase boundary: the delta since then
    // belongs to the phase active over that interval.
    StringEqualCensus string_equal_mark;
    BusterHashCensus hash_mark;
    ArenaBenchmarkCounters arena_mark;
};

BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL CCensusState c_census_state;

enum
{
    C_CENSUS_FACT_SHIFT = C_CENSUS_PHASE_COUNT,
    C_CENSUS_FACT_COMMIT_STEP = 1u << 24,
};
BUSTER_CT_CHECK(C_CENSUS_FACT_SHIFT + C_CENSUS_FACT_COUNT <= 16);

BUSTER_GLOBAL_LOCAL void c_census_add(u64* value, u64 amount)
{
    if (amount > UINT64_MAX - *value)
    {
        *value = UINT64_MAX;
        c_census_state.counters.overflowed = true;
    }
    else
    {
        *value += amount;
    }
}

void c_census_record(CCensusCounter counter, u64 amount)
{
    if ((u32)counter < C_CENSUS_COUNT)
    {
        c_census_add(c_census_state.counters.values + counter, amount);
    }
    else
    {
        c_census_state.counters.overflowed = true;
    }
}

void c_census_phase_record(CCensusPhaseCounter counter, u64 amount)
{
    if ((u32)counter < C_CENSUS_PHASE_COUNTER_COUNT)
    {
        c_census_add(c_census_state.counters.phase_values[c_census_state.phase] + counter, amount);
    }
    else
    {
        c_census_state.counters.overflowed = true;
    }
}

// Charges the library traffic since the last boundary to the active phase.
BUSTER_GLOBAL_LOCAL void c_census_flush(void)
{
    StringEqualCensus string_equal = string_equal_census();
    BusterHashCensus hash = buster_hash_census();
    ArenaBenchmarkCounters arena = arena_benchmark_counters();
    u64* phase = c_census_state.counters.phase_values[c_census_state.phase];
    c_census_add(phase + C_CENSUS_PHASE_STRING_EQUAL_CALLS, string_equal.calls - c_census_state.string_equal_mark.calls);
    c_census_add(phase + C_CENSUS_PHASE_STRING_EQUAL_BYTES, string_equal.compared_bytes - c_census_state.string_equal_mark.compared_bytes);
    c_census_add(phase + C_CENSUS_PHASE_HASH_CALLS, hash.calls - c_census_state.hash_mark.calls);
    c_census_add(phase + C_CENSUS_PHASE_HASH_BYTES, hash.bytes - c_census_state.hash_mark.bytes);
    c_census_add(phase + C_CENSUS_PHASE_ARENA_CALLS, arena.calls - c_census_state.arena_mark.calls);
    c_census_add(phase + C_CENSUS_PHASE_ARENA_BYTES, arena.requested_bytes - c_census_state.arena_mark.requested_bytes);
    c_census_state.string_equal_mark = string_equal;
    c_census_state.hash_mark = hash;
    c_census_state.arena_mark = arena;
}

CCensusPhase c_census_phase_enter(CCensusPhase phase)
{
    CCensusPhase previous = c_census_state.phase;
    c_census_flush();
    c_census_state.phase = (u32)phase < C_CENSUS_PHASE_COUNT ? phase : C_CENSUS_PHASE_OTHER;
    return previous;
}

void c_census_phase_exit(CCensusPhase previous)
{
    c_census_flush();
    c_census_state.phase = previous;
}

void c_census_space_begin(char8 const* base, u64 reserved_size)
{
    if (c_census_state.facts && reserved_size * sizeof(*c_census_state.facts) > c_census_state.fact_reserved)
    {
        // The map was reserved for a smaller space; the next fact reserves
        // one that covers this space.
        os_unreserve(c_census_state.facts, c_census_state.fact_reserved);
        c_census_state.facts = 0;
        c_census_state.fact_reserved = 0;
        c_census_state.fact_committed = 0;
    }
    else if (c_census_state.facts && c_census_state.fact_touched)
    {
        memset(c_census_state.facts, 0, c_census_state.fact_touched * sizeof(*c_census_state.facts));
    }
    c_census_state.fact_touched = 0;
    c_census_state.space_base = base;
    c_census_state.space_reserved = reserved_size;
}

// The fact word of `pointer`, or null when it lies outside the registered
// space or the map cannot grow to reach it.
BUSTER_GLOBAL_LOCAL u16* c_census_fact_word(char8 const* pointer)
{
    u16* result = 0;
    char8 const* base = c_census_state.space_base;
    if (base && pointer >= base && (u64)(pointer - base) < c_census_state.space_reserved)
    {
        u64 key = (u64)(pointer - base);
        if (!c_census_state.facts)
        {
            u64 reserved = c_census_state.space_reserved * sizeof(u16);
            c_census_state.facts = (u16*)os_reserve(0, reserved, (ProtectionFlags){.read = 1, .write = 1},
                                                    (MapFlags){.priv = 1, .anonymous = 1, .no_reserve = 1});
            c_census_state.fact_reserved = c_census_state.facts ? reserved : 0;
        }
        u64 needed = (key + 1) * sizeof(u16);
        while (c_census_state.facts && needed > c_census_state.fact_committed && c_census_state.fact_committed < c_census_state.fact_reserved)
        {
            u64 step = BUSTER_MIN((u64)C_CENSUS_FACT_COMMIT_STEP, c_census_state.fact_reserved - c_census_state.fact_committed);
            if (!os_commit((u8*)c_census_state.facts + c_census_state.fact_committed, step, (ProtectionFlags){.read = 1, .write = 1}, false))
            {
                break;
            }
            c_census_state.fact_committed += step;
        }
        if (c_census_state.facts && needed <= c_census_state.fact_committed)
        {
            c_census_state.fact_touched = BUSTER_MAX(c_census_state.fact_touched, key + 1);
            result = c_census_state.facts + key;
        }
    }
    return result;
}

void c_census_spelling_read(char8 const* pointer, u64 length)
{
    u64* phase = c_census_state.counters.phase_values[c_census_state.phase];
    c_census_add(phase + C_CENSUS_PHASE_SPELLING_READS, 1);
    c_census_add(phase + C_CENSUS_PHASE_SPELLING_READ_BYTES, length);
    u16* word = c_census_fact_word(pointer);
    if (!word)
    {
        c_census_add(phase + C_CENSUS_PHASE_SPELLING_READ_UNTRACKED, 1);
    }
    else if (!(*word & (1u << c_census_state.phase)))
    {
        *word |= (u16)(1u << c_census_state.phase);
        c_census_add(phase + C_CENSUS_PHASE_SPELLING_READ_DISTINCT, 1);
    }
}

void c_census_fact(CCensusFact fact, char8 const* pointer, u64 length)
{
    // Each kind owns four consecutive phase counters: total, bytes, distinct,
    // untracked (characters, which have no byte counter, own three).
    CCensusPhaseCounter total = C_CENSUS_PHASE_COUNTER_COUNT;
    CCensusPhaseCounter bytes = C_CENSUS_PHASE_COUNTER_COUNT;
    CCensusPhaseCounter distinct = C_CENSUS_PHASE_COUNTER_COUNT;
    CCensusPhaseCounter untracked = C_CENSUS_PHASE_COUNTER_COUNT;
    switch (fact)
    {
    case C_CENSUS_FACT_INTEGER:
        total = C_CENSUS_PHASE_INTEGER_CONVERSIONS;
        bytes = C_CENSUS_PHASE_INTEGER_CONVERSION_BYTES;
        distinct = C_CENSUS_PHASE_INTEGER_CONVERSION_DISTINCT;
        untracked = C_CENSUS_PHASE_INTEGER_CONVERSION_UNTRACKED;
        break;
    case C_CENSUS_FACT_FLOAT:
        total = C_CENSUS_PHASE_FLOAT_CONVERSIONS;
        bytes = C_CENSUS_PHASE_FLOAT_CONVERSION_BYTES;
        distinct = C_CENSUS_PHASE_FLOAT_CONVERSION_DISTINCT;
        untracked = C_CENSUS_PHASE_FLOAT_CONVERSION_UNTRACKED;
        break;
    case C_CENSUS_FACT_CHARACTER:
        total = C_CENSUS_PHASE_CHARACTER_DECODES;
        distinct = C_CENSUS_PHASE_CHARACTER_DECODE_DISTINCT;
        untracked = C_CENSUS_PHASE_CHARACTER_DECODE_UNTRACKED;
        break;
    case C_CENSUS_FACT_STRING_DECODE:
        total = C_CENSUS_PHASE_STRING_DECODES;
        bytes = C_CENSUS_PHASE_STRING_DECODE_BYTES;
        distinct = C_CENSUS_PHASE_STRING_DECODE_DISTINCT;
        untracked = C_CENSUS_PHASE_STRING_DECODE_UNTRACKED;
        break;
    case C_CENSUS_FACT_STRING_COUNT:
        total = C_CENSUS_PHASE_STRING_COUNTS;
        bytes = C_CENSUS_PHASE_STRING_COUNT_BYTES;
        distinct = C_CENSUS_PHASE_STRING_COUNT_DISTINCT;
        untracked = C_CENSUS_PHASE_STRING_COUNT_UNTRACKED;
        break;
    case C_CENSUS_FACT_COUNT:
    default:
        c_census_state.counters.overflowed = true;
        break;
    }
    if (total != C_CENSUS_PHASE_COUNTER_COUNT)
    {
        u64* phase = c_census_state.counters.phase_values[c_census_state.phase];
        c_census_add(phase + total, 1);
        if (bytes != C_CENSUS_PHASE_COUNTER_COUNT)
        {
            c_census_add(phase + bytes, length);
        }
        u16* word = c_census_fact_word(pointer);
        u16 bit = (u16)(1u << (C_CENSUS_FACT_SHIFT + fact));
        if (!word)
        {
            c_census_add(phase + untracked, 1);
        }
        else if (!(*word & bit))
        {
            *word |= bit;
            c_census_add(phase + distinct, 1);
        }
    }
}

CCensusCounters c_census_counters(void)
{
    c_census_flush();
    return c_census_state.counters;
}

String8 c_census_counter_name(CCensusCounter counter)
{
    String8 result = {0};
    switch (counter)
    {
#define C_CENSUS_NAME(id, name) case C_CENSUS_##id: result = S8(#name); break;
        C_CENSUS_COUNTERS(C_CENSUS_NAME)
#undef C_CENSUS_NAME
        default: break;
    }
    return result;
}

String8 c_census_phase_counter_name(CCensusPhaseCounter counter)
{
    String8 result = {0};
    switch (counter)
    {
#define C_CENSUS_PHASE_NAME(id, name) case C_CENSUS_PHASE_##id: result = S8(#name); break;
        C_CENSUS_PHASE_COUNTERS(C_CENSUS_PHASE_NAME)
#undef C_CENSUS_PHASE_NAME
        default: break;
    }
    return result;
}

String8 c_census_phase_name(CCensusPhase phase)
{
    String8 result = {0};
    switch (phase)
    {
    case C_CENSUS_PHASE_OTHER: result = S8("other"); break;
    case C_CENSUS_PHASE_PREPROCESS: result = S8("preprocess"); break;
    case C_CENSUS_PHASE_PARSE: result = S8("parse"); break;
    case C_CENSUS_PHASE_SEMANTIC: result = S8("semantic"); break;
    case C_CENSUS_PHASE_LOWER: result = S8("lower"); break;
    case C_CENSUS_PHASE_COUNT: default: break;
    }
    return result;
}
#endif

// Locations are recorded as checkpoints instead of one entry per translated
// byte: within a run the original offset and the column both advance one per
// output byte and the line is constant, so a checkpoint is needed only where
// that linearity breaks — the byte after a newline and the byte after a line
// splice. This is what keeps the lexer from writing a location for every
// byte of every include it translates, and what lets tokens drop their eager
// location entirely: a token's line/column recover on demand from its offset
// through these checkpoints.
typedef struct CTranslatedSource CTranslatedSource;
struct CTranslatedSource
{
    String8 source;
    IrSourceCheckpoint* checkpoints;
    u32* checkpoint_offsets;
    u32* checkpoint_pages;
    u32 checkpoint_page_count;
    u32 checkpoint_count;
    // Spelling-space offset of source.pointer (0 without a space).
    u32 translated_offset;
    // Physical lines of the untranslated file, spliced ones included. The
    // line counter the checkpoints already need makes this free.
    u32 raw_lines;
};

// The single contiguous byte space every token offset of one preprocess run
// points into. All spelling bytes — the prelude, every translated include,
// and every synthesized spelling (stringify, paste, builtins, expansion
// copies) — are bump-allocated here so a spelling is always
// `base + token.offset`, one add on the hot paths. The block is carved from
// the caller's arena once; untouched tail pages never commit, so oversizing
// costs address space only.
BUSTER_C_INTERNAL char8* c_space_allocate(CSpellingSpace* space, u64 size)
{
    if (space->arena)
    {
        char8* result = (char8*)arena_allocate_bytes(space->arena, size, 1);
        space->used = (u64)(result - space->base) + size;
        BUSTER_VALIDATE(space->used <= UINT32_MAX);
        return result;
    }
    BUSTER_VALIDATE(space->used + size <= space->capacity);
    char8* result = space->base + space->used;
    space->used += size;
    return result;
}
// Hand back the tail of the space's most recent allocation.
BUSTER_C_INTERNAL void c_space_shrink(CSpellingSpace* space, u64 size)
{
    space->used -= size;
    if (space->arena)
    {
        arena_set_position(space->arena, space->arena->position - size);
    }
}

BUSTER_C_INTERNAL u32 c_space_offset(CSpellingSpace const* space, char8 const* pointer)
{
    return (u32)(pointer - space->base);
}

// Exact byte length of the literal spelling starting at `spelling`:
// optional encoding prefix, opening delimiter, escape-aware body, closing
// delimiter. The body loop mirrors the lexer's literal scan byte for byte —
// escape pairs skip two — so a spelling the lexer closed re-measures to the
// same length. `limit` bounds the scan for creation-time validation of a
// possibly unterminated spelling; a validated spelling passes UINT64_MAX
// because its closing delimiter, the spelling's own last byte, stops the
// scan. Returns 0 when no delimiter opens at the start.
BUSTER_C_INTERNAL u64 c_token_literal_scan_length(char8 const* spelling, u64 limit)
{
    u64 cursor = 0;
    char8 delimiter = 0;
    if (limit && (spelling[0] == '"' || spelling[0] == '\''))
    {
        delimiter = spelling[0];
        cursor = 1;
    }
    else if (limit >= 3 && spelling[0] == 'u' && spelling[1] == '8' && (spelling[2] == '"' || spelling[2] == '\''))
    {
        delimiter = spelling[2];
        cursor = 3;
    }
    else if (limit >= 2 && (spelling[0] == 'u' || spelling[0] == 'U' || spelling[0] == 'L') && (spelling[1] == '"' || spelling[1] == '\''))
    {
        delimiter = spelling[1];
        cursor = 2;
    }
    else
    {
        return 0;
    }
    while (cursor < limit)
    {
        char8 character = spelling[cursor];
        if (character == delimiter)
        {
            return cursor + 1;
        }
        cursor += character == '\\' && cursor + 1 < limit ? 2 : 1;
    }
    return 0;
}

// The cold half of c_token_length: the field carries the sentinel, so the
// exact length is re-derived from the spelling. Creation only ever stores
// the sentinel for terminated string and character literals (see CToken).
u64 c_token_length_oversized(char8 const* spelling_base, CToken token)
{
    BUSTER_CHECK(token.kind == C_TOKEN_STRING_LITERAL || token.kind == C_TOKEN_CHARACTER_LITERAL);
    u64 length = c_token_literal_scan_length(spelling_base + token.offset, UINT64_MAX);
    BUSTER_CHECK(length >= C_TOKEN_LENGTH_OVERSIZED);
    return length;
}

// The u16 the length field stores for a spelling of `length` bytes. The
// caller owns the sentinel invariant: a spelling at or past the sentinel
// must be a terminated literal, or must be diagnosed and clamped instead.
BUSTER_C_SHARED u16 c_token_length_field(u64 length)
{
    return length < C_TOKEN_LENGTH_OVERSIZED ? (u16)length : C_TOKEN_LENGTH_OVERSIZED;
}

// A token whose spelling is a fresh copy of `text` in the space; used for
// the preprocessor's built-in macro definitions, whose replacement spellings
// must resolve through the shared base like any lexed token.
BUSTER_C_SHARED CToken c_space_token(CSpellingSpace* space, String8 text, CTokenKind kind, CPunctuator punctuator)
{
    // The sentinel is only valid on terminated literals; no other caller
    // synthesizes a spelling anywhere near it.
    BUSTER_CHECK(text.length < C_TOKEN_LENGTH_OVERSIZED || kind == C_TOKEN_STRING_LITERAL || kind == C_TOKEN_CHARACTER_LITERAL);
#if BUSTER_BENCH_ALLOCATIONS
    if (space->arena)
    {
        C_CENSUS_RECORD(SPACE_SYNTHESIZED_COPIES, 1);
        C_CENSUS_RECORD(SPACE_SYNTHESIZED_BYTES, text.length);
    }
    else
    {
        C_CENSUS_PHASE_RECORD(TEMP_TOKENS, 1);
        C_CENSUS_PHASE_RECORD(TEMP_SPELLING_BYTES, text.length);
    }
#endif
    char8* copy = c_space_allocate(space, text.length);
    if (text.length)
    {
        memcpy(copy, text.pointer, text.length);
    }
    return (CToken){
        .offset = c_space_offset(space, copy),
        .length = c_token_length_field(text.length),
        .kind = (u8)kind,
        .punctuator = (u8)punctuator,
    };
}

// Copy a token's spelling from another base into this space, keeping every
// other field; parse-side evaluators that mix real tokens with synthesized
// ones rebase everything into one local space this way.
BUSTER_C_SHARED CToken c_space_retoken(CSpellingSpace* space, char8 const* from_base, CToken token)
{
    String8 spelling = c_token_spelling(from_base, token);
#if BUSTER_BENCH_ALLOCATIONS
    if (space->arena)
    {
        C_CENSUS_RECORD(SPACE_SYNTHESIZED_COPIES, 1);
        C_CENSUS_RECORD(SPACE_SYNTHESIZED_BYTES, spelling.length);
    }
    else
    {
        C_CENSUS_PHASE_RECORD(TEMP_TOKENS, 1);
        C_CENSUS_PHASE_RECORD(TEMP_SPELLING_BYTES, spelling.length);
    }
#endif
    char8* copy = c_space_allocate(space, spelling.length);
    if (spelling.length)
    {
        memcpy(copy, spelling.pointer, spelling.length);
    }
    token.offset = c_space_offset(space, copy);
    return token;
}

// A transient prelude-seeded spelling space for parse-side expression
// evaluation over synthesized tokens.
BUSTER_C_SHARED CSpellingSpace c_space_local(Arena* arena, u64 capacity)
{
    CSpellingSpace space = {
        .capacity = capacity + C_SPELLING_PRELUDE_LENGTH,
    };
    C_CENSUS_PHASE_RECORD(TEMP_SPACES, 1);
    C_CENSUS_PHASE_RECORD(TEMP_SPACE_BYTES, space.capacity);
    space.base = arena_allocate(arena, char8, space.capacity);
    memcpy(c_space_allocate(&space, C_SPELLING_PRELUDE_LENGTH), C_SPELLING_PRELUDE_TEXT, C_SPELLING_PRELUDE_LENGTH);
    return space;
}

// The source map under construction: append-only while preprocessing runs
// (nothing queries it until the result is assembled), sorted once at the
// end. #line splits may append below any number of previously emitted
// regions; finalization must preserve ties without depending on displacement.
typedef struct CSourceMap CSourceMap;
typedef struct CSourceRegionNames CSourceRegionNames;
struct CSourceRegionNames
{
    String8 physical;
    String8 logical;
};

struct CSourceMap
{
    Arena* arena;
    IrSourceRegion* regions;
    u32 count;
    u32 capacity;
    CSourceRegionNames* names;
    u32 name_count;
    u32 name_capacity;
};

BUSTER_C_INTERNAL void c_source_map_append(CSourceMap* map, IrSourceRegion region)
{
    if (map->count == map->capacity)
    {
        u32 capacity = map->capacity ? map->capacity * 2 : 256;
        IrSourceRegion* regions = arena_allocate(map->arena, IrSourceRegion, capacity);
        if (map->count)
        {
            memcpy(regions, map->regions, sizeof(*regions) * map->count);
        }
        map->regions = regions;
        map->capacity = capacity;
    }
    map->regions[map->count++] = region;
}

// Stable LSD radix sorting bounds finalization independently of append order.
// The ordered path only reads keys. Unordered maps use one temporary row
// buffer; four byte passes finish in the original array before scratch rewind.
BUSTER_C_INTERNAL void c_source_map_sort(Arena* arena, IrSourceRegion* regions, u32 count)
{
    u32 ordered = 1;
    while (ordered < count && regions[ordered - 1].start <= regions[ordered].start)
    {
        ordered += 1;
    }
    if (ordered < count)
    {
        TemporalArena temporary = arena_begin_temporal(arena);
        IrSourceRegion* source = regions;
        IrSourceRegion* destination = arena_allocate(arena, IrSourceRegion, count);
        enum { C_SOURCE_MAP_RADIX_BITS = 8, C_SOURCE_MAP_RADIX_BUCKETS = 1 << C_SOURCE_MAP_RADIX_BITS };
        for (u32 byte_index = 0; byte_index < sizeof(regions->start); byte_index += 1)
        {
            u32 offsets[C_SOURCE_MAP_RADIX_BUCKETS] = {0};
            u32 shift = byte_index * C_SOURCE_MAP_RADIX_BITS;
            for (u32 index = 0; index < count; index += 1)
            {
                u32 bucket = (source[index].start >> shift) & (C_SOURCE_MAP_RADIX_BUCKETS - 1);
                offsets[bucket] += 1;
            }
            u32 offset = 0;
            for (u32 bucket = 0; bucket < C_SOURCE_MAP_RADIX_BUCKETS; bucket += 1)
            {
                u32 population = offsets[bucket];
                offsets[bucket] = offset;
                offset += population;
            }
            // Forward scatter retains append order among equal start keys.
            for (u32 index = 0; index < count; index += 1)
            {
                u32 bucket = (source[index].start >> shift) & (C_SOURCE_MAP_RADIX_BUCKETS - 1);
                destination[offsets[bucket]++] = source[index];
            }
            IrSourceRegion* swap = source;
            source = destination;
            destination = swap;
        }
        scratch_end(temporary);
    }
}

#if BUSTER_INCLUDE_TESTS
void c_test_source_map_sort(Arena* arena, IrSourceRegion* regions, u32 count)
{
    c_source_map_sort(arena, regions, count);
}
#endif

// TEXT origins temporarily index these names. Finalization registers physical
// source IDs only for token-bearing or diagnosed regions, preserving the
// self-host rule that unused host-resource headers never enter the file table.
BUSTER_C_INTERNAL void c_source_map_name(CSourceMap* map, String8 physical, String8 logical)
{
    if (map->name_count == map->name_capacity)
    {
        u32 capacity = map->name_capacity ? map->name_capacity * 2 : 8;
        CSourceRegionNames* names = arena_allocate(map->arena, CSourceRegionNames, capacity);
        if (map->name_count)
        {
            memcpy(names, map->names, sizeof(*names) * map->name_count);
        }
        map->names = names;
        map->name_capacity = capacity;
    }
    map->names[map->name_count++] = (CSourceRegionNames){.physical = physical, .logical = logical};
    map->regions[map->count - 1].origin_plus_one = map->name_count;
}

// Hand the finished regions to the map every lookup reads, deriving the key
// array the region search and the source query run on. A trailing sentinel
// key ends the space, so a containment check needs no bounds test.
BUSTER_C_INTERNAL void c_source_map_publish(Arena* arena, CSourceMapRecovery* recovery, CSourceMap const* map)
{
    if (!map->count)
    {
        // A published key array always has a region to answer with, so the
        // lookups test the array alone and never a count as well.
        return;
    }
    IrSourceRegionKey* keys = arena_allocate(arena, IrSourceRegionKey, map->count + 1);
    for (u32 index = 0; index < map->count; index += 1)
    {
        keys[index] = (IrSourceRegionKey){
            .start = map->regions[index].start,
            .source = map->regions[index].source,
        };
    }
    keys[map->count] = (IrSourceRegionKey){.start = UINT32_MAX};
    recovery->map.keys = keys;
    recovery->map.regions = map->regions;
    recovery->map.count = map->count;
    recovery->capacity = map->capacity;
}

// Only the append-only C23 respell phase may use this boundary: equal counts
// there mean every existing key and its sentinel are still final. This is not
// a general mutation cache. Keep published storage alive in the TU arena;
// canonical lowering copies its pointers for later diagnostics and debug info.
BUSTER_C_INTERNAL void c_source_map_publish_appended(Arena* arena, CSourceMapRecovery* recovery, CSourceMap const* map)
{
    if (map->count != recovery->map.count)
    {
        c_source_map_publish(arena, recovery, map);
    }
}

#if BUSTER_INCLUDE_TESTS
void c_test_source_map_publish_appended(Arena* arena, CSourceMapRecovery* recovery, IrSourceRegion* regions, u32 count, u32 capacity)
{
    CSourceMap map = {.arena = arena, .regions = regions, .count = count, .capacity = capacity};
    c_source_map_publish_appended(arena, recovery, &map);
}
#endif

BUSTER_C_SHARED bool c_ir_decode_character_value(Arena* arena, char8 const* spelling_base, CToken token, Target target, u64* value_out, CTypeKind* kind_out);

BUSTER_C_SHARED bool c_preprocess_dialect_is_c23(CPreprocessDialect dialect);

BUSTER_C_INTERNAL bool c_ascii_alpha(char8 character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

BUSTER_C_INTERNAL bool c_ascii_digit(char8 character)
{
    return character >= '0' && character <= '9';
}

BUSTER_C_INTERNAL bool c_identifier_start(char8 character)
{
    return c_ascii_alpha(character) || character == '_' || character == '$' || character >= 0x80;
}

BUSTER_C_INTERNAL bool c_identifier_continue(char8 character)
{
    return c_identifier_start(character) || c_ascii_digit(character);
}

// Identifier UCNs have a dialect-owned Annex D policy. Raw UTF-8 remains
// encoding-validated without imposing these source-escape range restrictions.
// C99 uses N1256 Annex D; C11/C17 use N1570 Annex D. These are authored
// inclusive ranges, not a modern Unicode category or normalization database.
typedef struct CUcnRange CUcnRange;
struct CUcnRange
{
    u32 first;
    u32 last;
};

BUSTER_C_INTERNAL CUcnRange const c_ucn_c99_ranges[] = {
    {0x00AA, 0x00AA},
    {0x00B5, 0x00B5},
    {0x00B7, 0x00B7},
    {0x00BA, 0x00BA},
    {0x00C0, 0x00D6},
    {0x00D8, 0x00F6},
    {0x00F8, 0x01F5},
    {0x01FA, 0x0217},
    {0x0250, 0x02A8},
    {0x02B0, 0x02B8},
    {0x02BB, 0x02BB},
    {0x02BD, 0x02C1},
    {0x02D0, 0x02D1},
    {0x02E0, 0x02E4},
    {0x037A, 0x037A},
    {0x0386, 0x0386},
    {0x0388, 0x038A},
    {0x038C, 0x038C},
    {0x038E, 0x03A1},
    {0x03A3, 0x03CE},
    {0x03D0, 0x03D6},
    {0x03DA, 0x03DA},
    {0x03DC, 0x03DC},
    {0x03DE, 0x03DE},
    {0x03E0, 0x03E0},
    {0x03E2, 0x03F3},
    {0x0401, 0x040C},
    {0x040E, 0x044F},
    {0x0451, 0x045C},
    {0x045E, 0x0481},
    {0x0490, 0x04C4},
    {0x04C7, 0x04C8},
    {0x04CB, 0x04CC},
    {0x04D0, 0x04EB},
    {0x04EE, 0x04F5},
    {0x04F8, 0x04F9},
    {0x0531, 0x0556},
    {0x0559, 0x0559},
    {0x0561, 0x0587},
    {0x05B0, 0x05B9},
    {0x05BB, 0x05BD},
    {0x05BF, 0x05BF},
    {0x05C1, 0x05C2},
    {0x05D0, 0x05EA},
    {0x05F0, 0x05F2},
    {0x0621, 0x063A},
    {0x0640, 0x0652},
    {0x0660, 0x0669},
    {0x0670, 0x06B7},
    {0x06BA, 0x06BE},
    {0x06C0, 0x06CE},
    {0x06D0, 0x06DC},
    {0x06E5, 0x06E8},
    {0x06EA, 0x06ED},
    {0x06F0, 0x06F9},
    {0x0901, 0x0903},
    {0x0905, 0x0939},
    {0x093D, 0x094D},
    {0x0950, 0x0952},
    {0x0958, 0x0963},
    {0x0966, 0x096F},
    {0x0981, 0x0983},
    {0x0985, 0x098C},
    {0x098F, 0x0990},
    {0x0993, 0x09A8},
    {0x09AA, 0x09B0},
    {0x09B2, 0x09B2},
    {0x09B6, 0x09B9},
    {0x09BE, 0x09C4},
    {0x09C7, 0x09C8},
    {0x09CB, 0x09CD},
    {0x09DC, 0x09DD},
    {0x09DF, 0x09E3},
    {0x09E6, 0x09F1},
    {0x0A02, 0x0A02},
    {0x0A05, 0x0A0A},
    {0x0A0F, 0x0A10},
    {0x0A13, 0x0A28},
    {0x0A2A, 0x0A30},
    {0x0A32, 0x0A33},
    {0x0A35, 0x0A36},
    {0x0A38, 0x0A39},
    {0x0A3E, 0x0A42},
    {0x0A47, 0x0A48},
    {0x0A4B, 0x0A4D},
    {0x0A59, 0x0A5C},
    {0x0A5E, 0x0A5E},
    {0x0A66, 0x0A6F},
    {0x0A74, 0x0A74},
    {0x0A81, 0x0A83},
    {0x0A85, 0x0A8B},
    {0x0A8D, 0x0A8D},
    {0x0A8F, 0x0A91},
    {0x0A93, 0x0AA8},
    {0x0AAA, 0x0AB0},
    {0x0AB2, 0x0AB3},
    {0x0AB5, 0x0AB9},
    {0x0ABD, 0x0AC5},
    {0x0AC7, 0x0AC9},
    {0x0ACB, 0x0ACD},
    {0x0AD0, 0x0AD0},
    {0x0AE0, 0x0AE0},
    {0x0AE6, 0x0AEF},
    {0x0B01, 0x0B03},
    {0x0B05, 0x0B0C},
    {0x0B0F, 0x0B10},
    {0x0B13, 0x0B28},
    {0x0B2A, 0x0B30},
    {0x0B32, 0x0B33},
    {0x0B36, 0x0B39},
    {0x0B3D, 0x0B43},
    {0x0B47, 0x0B48},
    {0x0B4B, 0x0B4D},
    {0x0B5C, 0x0B5D},
    {0x0B5F, 0x0B61},
    {0x0B66, 0x0B6F},
    {0x0B82, 0x0B83},
    {0x0B85, 0x0B8A},
    {0x0B8E, 0x0B90},
    {0x0B92, 0x0B95},
    {0x0B99, 0x0B9A},
    {0x0B9C, 0x0B9C},
    {0x0B9E, 0x0B9F},
    {0x0BA3, 0x0BA4},
    {0x0BA8, 0x0BAA},
    {0x0BAE, 0x0BB5},
    {0x0BB7, 0x0BB9},
    {0x0BBE, 0x0BC2},
    {0x0BC6, 0x0BC8},
    {0x0BCA, 0x0BCD},
    {0x0BE7, 0x0BEF},
    {0x0C01, 0x0C03},
    {0x0C05, 0x0C0C},
    {0x0C0E, 0x0C10},
    {0x0C12, 0x0C28},
    {0x0C2A, 0x0C33},
    {0x0C35, 0x0C39},
    {0x0C3E, 0x0C44},
    {0x0C46, 0x0C48},
    {0x0C4A, 0x0C4D},
    {0x0C60, 0x0C61},
    {0x0C66, 0x0C6F},
    {0x0C82, 0x0C83},
    {0x0C85, 0x0C8C},
    {0x0C8E, 0x0C90},
    {0x0C92, 0x0CA8},
    {0x0CAA, 0x0CB3},
    {0x0CB5, 0x0CB9},
    {0x0CBE, 0x0CC4},
    {0x0CC6, 0x0CC8},
    {0x0CCA, 0x0CCD},
    {0x0CDE, 0x0CDE},
    {0x0CE0, 0x0CE1},
    {0x0CE6, 0x0CEF},
    {0x0D02, 0x0D03},
    {0x0D05, 0x0D0C},
    {0x0D0E, 0x0D10},
    {0x0D12, 0x0D28},
    {0x0D2A, 0x0D39},
    {0x0D3E, 0x0D43},
    {0x0D46, 0x0D48},
    {0x0D4A, 0x0D4D},
    {0x0D60, 0x0D61},
    {0x0D66, 0x0D6F},
    {0x0E01, 0x0E3A},
    {0x0E40, 0x0E5B},
    {0x0E81, 0x0E82},
    {0x0E84, 0x0E84},
    {0x0E87, 0x0E88},
    {0x0E8A, 0x0E8A},
    {0x0E8D, 0x0E8D},
    {0x0E94, 0x0E97},
    {0x0E99, 0x0E9F},
    {0x0EA1, 0x0EA3},
    {0x0EA5, 0x0EA5},
    {0x0EA7, 0x0EA7},
    {0x0EAA, 0x0EAB},
    {0x0EAD, 0x0EAE},
    {0x0EB0, 0x0EB9},
    {0x0EBB, 0x0EBD},
    {0x0EC0, 0x0EC4},
    {0x0EC6, 0x0EC6},
    {0x0EC8, 0x0ECD},
    {0x0ED0, 0x0ED9},
    {0x0EDC, 0x0EDD},
    {0x0F00, 0x0F00},
    {0x0F18, 0x0F19},
    {0x0F20, 0x0F33},
    {0x0F35, 0x0F35},
    {0x0F37, 0x0F37},
    {0x0F39, 0x0F39},
    {0x0F3E, 0x0F47},
    {0x0F49, 0x0F69},
    {0x0F71, 0x0F84},
    {0x0F86, 0x0F8B},
    {0x0F90, 0x0F95},
    {0x0F97, 0x0F97},
    {0x0F99, 0x0FAD},
    {0x0FB1, 0x0FB7},
    {0x0FB9, 0x0FB9},
    {0x10A0, 0x10C5},
    {0x10D0, 0x10F6},
    {0x1E00, 0x1E9B},
    {0x1EA0, 0x1EF9},
    {0x1F00, 0x1F15},
    {0x1F18, 0x1F1D},
    {0x1F20, 0x1F45},
    {0x1F48, 0x1F4D},
    {0x1F50, 0x1F57},
    {0x1F59, 0x1F59},
    {0x1F5B, 0x1F5B},
    {0x1F5D, 0x1F5D},
    {0x1F5F, 0x1F7D},
    {0x1F80, 0x1FB4},
    {0x1FB6, 0x1FBC},
    {0x1FBE, 0x1FBE},
    {0x1FC2, 0x1FC4},
    {0x1FC6, 0x1FCC},
    {0x1FD0, 0x1FD3},
    {0x1FD6, 0x1FDB},
    {0x1FE0, 0x1FEC},
    {0x1FF2, 0x1FF4},
    {0x1FF6, 0x1FFC},
    {0x203F, 0x2040},
    {0x207F, 0x207F},
    {0x2102, 0x2102},
    {0x2107, 0x2107},
    {0x210A, 0x2113},
    {0x2115, 0x2115},
    {0x2118, 0x211D},
    {0x2124, 0x2124},
    {0x2126, 0x2126},
    {0x2128, 0x2128},
    {0x212A, 0x2131},
    {0x2133, 0x2138},
    {0x2160, 0x2182},
    {0x3005, 0x3007},
    {0x3021, 0x3029},
    {0x3041, 0x3093},
    {0x309B, 0x309C},
    {0x30A1, 0x30F6},
    {0x30FB, 0x30FC},
    {0x3105, 0x312C},
    {0x4E00, 0x9FA5},
    {0xAC00, 0xD7A3},
};

BUSTER_C_INTERNAL CUcnRange const c_ucn_c99_digits[] = {
    {0x0660, 0x0669},
    {0x06F0, 0x06F9},
    {0x0966, 0x096F},
    {0x09E6, 0x09EF},
    {0x0A66, 0x0A6F},
    {0x0AE6, 0x0AEF},
    {0x0B66, 0x0B6F},
    {0x0BE7, 0x0BEF},
    {0x0C66, 0x0C6F},
    {0x0CE6, 0x0CEF},
    {0x0D66, 0x0D6F},
    {0x0E50, 0x0E59},
    {0x0ED0, 0x0ED9},
    {0x0F20, 0x0F33},
};

BUSTER_C_INTERNAL CUcnRange const c_ucn_c11_ranges[] = {
    {0x00A8, 0x00A8},
    {0x00AA, 0x00AA},
    {0x00AD, 0x00AD},
    {0x00AF, 0x00AF},
    {0x00B2, 0x00B5},
    {0x00B7, 0x00BA},
    {0x00BC, 0x00BE},
    {0x00C0, 0x00D6},
    {0x00D8, 0x00F6},
    {0x00F8, 0x167F},
    {0x1681, 0x180D},
    {0x180F, 0x1FFF},
    {0x200B, 0x200D},
    {0x202A, 0x202E},
    {0x203F, 0x2040},
    {0x2054, 0x2054},
    {0x2060, 0x218F},
    {0x2460, 0x24FF},
    {0x2776, 0x2793},
    {0x2C00, 0x2DFF},
    {0x2E80, 0x2FFF},
    {0x3004, 0x3007},
    {0x3021, 0x302F},
    {0x3031, 0xD7FF},
    {0xF900, 0xFD3D},
    {0xFD40, 0xFDCF},
    {0xFDF0, 0xFE44},
    {0xFE47, 0xFFFD},
};

BUSTER_C_INTERNAL bool c_ucn_in_ranges(u32 value, CUcnRange const* ranges, u64 count)
{
    u64 low = 0;
    u64 high = count;
    while (low < high)
    {
        u64 middle = low + (high - low) / 2;
        if (ranges[middle].last < value)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < count && ranges[low].first <= value;
}

BUSTER_C_INTERNAL bool c_ucn_enabled(CPreprocessDialect dialect)
{
    return dialect == C_PREPROCESS_DIALECT_C99 || dialect == C_PREPROCESS_DIALECT_GNU99 ||
           dialect == C_PREPROCESS_DIALECT_C11 || dialect == C_PREPROCESS_DIALECT_GNU11 ||
           dialect == C_PREPROCESS_DIALECT_C17 || dialect == C_PREPROCESS_DIALECT_GNU17;
}

BUSTER_C_INTERNAL bool c_ucn_identifier_allowed(u32 value, CPreprocessDialect dialect, bool initial)
{
    bool allowed;
    if (dialect == C_PREPROCESS_DIALECT_C99 || dialect == C_PREPROCESS_DIALECT_GNU99)
    {
        allowed = c_ucn_in_ranges(value, c_ucn_c99_ranges, BUSTER_ARRAY_LENGTH(c_ucn_c99_ranges)) &&
                  (!initial || !c_ucn_in_ranges(value, c_ucn_c99_digits, BUSTER_ARRAY_LENGTH(c_ucn_c99_digits)));
    }
    else
    {
        allowed = c_ucn_in_ranges(value, c_ucn_c11_ranges, BUSTER_ARRAY_LENGTH(c_ucn_c11_ranges)) ||
                  (value >= 0x10000 && value <= 0xEFFFD && (value & 0xFFFF) <= 0xFFFD);
        if (initial)
        {
            allowed = allowed && !(value >= 0x0300 && value <= 0x036F) && !(value >= 0x1DC0 && value <= 0x1DFF) &&
                      !(value >= 0x20D0 && value <= 0x20FF) && !(value >= 0xFE20 && value <= 0xFE2F);
        }
    }
    return allowed;
}

typedef enum CUcnStatus
{
    C_UCN_VALID,
    C_UCN_MALFORMED,
    C_UCN_SURROGATE,
    C_UCN_OUT_OF_RANGE,
    C_UCN_FORBIDDEN,
} CUcnStatus;

typedef struct CUcn CUcn;
struct CUcn
{
    u32 value;
    u32 length;
    CUcnStatus status;
};

BUSTER_C_INTERNAL bool c_ucn_prefix(String8 source, u64 offset)
{
    return offset < source.length && source.pointer[offset] == '\\' && source.length - offset >= 2 &&
           (source.pointer[offset + 1] == 'u' || source.pointer[offset + 1] == 'U');
}

// The caller has established the prefix. Consume only the available hex
// prefix on malformed input, so the following newline or token retains its
// own source position and every diagnostic path makes progress.
BUSTER_C_INTERNAL CUcn c_ucn_decode(String8 source, u64 offset)
{
    CUcn result = {.length = 2};
    u32 digits = source.pointer[offset + 1] == 'u' ? 4 : 8;
    while (result.length < digits + 2 && result.length < source.length - offset)
    {
        u32 character = source.pointer[offset + result.length];
        u32 digit = character >= '0' && character <= '9' ? character - '0' :
                    character >= 'a' && character <= 'f' ? character - 'a' + 10 :
                    character >= 'A' && character <= 'F' ? character - 'A' + 10 : 16;
        if (digit == 16)
        {
            break;
        }
        result.value = (result.value << 4) | digit;
        result.length += 1;
    }
    if (result.length != digits + 2)
    {
        result.status = C_UCN_MALFORMED;
    }
    else if (result.value > 0x10FFFF)
    {
        result.status = C_UCN_OUT_OF_RANGE;
    }
    else if (result.value >= 0xD800 && result.value <= 0xDFFF)
    {
        result.status = C_UCN_SURROGATE;
    }
    else if (result.value < 0xA0 && result.value != '$' && result.value != '@' && result.value != 0x60)
    {
        result.status = C_UCN_FORBIDDEN;
    }
    return result;
}

// One byte per character value: nonzero for an ASCII identifier continuation.
// High bytes stop the fast run for bounded UTF-8 validation in the lexer;
// the identifier run scan ANDs eight entries per step
// instead of branching per byte. c_prewarm() fills it ahead of any gang.
BUSTER_C_INTERNAL u8 c_identifier_continue_table[256];
BUSTER_C_INTERNAL bool c_identifier_continue_table_built;

BUSTER_C_INTERNAL void c_identifier_continue_table_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    for (u32 character = 0; character < 256; character += 1)
    {
        c_identifier_continue_table[character] = character < 0x80 && c_identifier_continue((char8)character) ? 1 : 0;
    }
    c_identifier_continue_table_built = true;
}

BUSTER_C_INTERNAL u64 c_identifier_run_end(String8 source, u64 offset)
{
    if (!c_identifier_continue_table_built)
    {
        c_identifier_continue_table_build();
    }
    const u8* bytes = (const u8*)source.pointer;
    while (offset + 8 <= source.length)
    {
        u8 all = c_identifier_continue_table[bytes[offset]] & c_identifier_continue_table[bytes[offset + 1]] &
                 c_identifier_continue_table[bytes[offset + 2]] & c_identifier_continue_table[bytes[offset + 3]] &
                 c_identifier_continue_table[bytes[offset + 4]] & c_identifier_continue_table[bytes[offset + 5]] &
                 c_identifier_continue_table[bytes[offset + 6]] & c_identifier_continue_table[bytes[offset + 7]];
        if (!all)
        {
            break;
        }
        offset += 8;
    }
    while (offset < source.length && c_identifier_continue_table[bytes[offset]])
    {
        offset += 1;
    }
    return offset;
}

// One byte per character value: 1 when the byte cannot end or escape a
// character/string literal (either quote kind, backslash, newline), so the
// literal scan ANDs eight entries per step like the identifier run.
BUSTER_C_INTERNAL u8 c_literal_plain_table[256];
BUSTER_C_INTERNAL bool c_literal_plain_table_built;

BUSTER_C_INTERNAL void c_literal_plain_table_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    for (u32 character = 0; character < 256; character += 1)
    {
        bool special = character == '"' || character == '\'' || character == '\\' || character == '\n';
        c_literal_plain_table[character] = special ? 0 : 1;
    }
    c_literal_plain_table_built = true;
}

BUSTER_C_INTERNAL u64 c_literal_plain_run_end(String8 source, u64 offset)
{
    if (!c_literal_plain_table_built)
    {
        c_literal_plain_table_build();
    }
    const u8* bytes = (const u8*)source.pointer;
    while (offset + 8 <= source.length)
    {
        u8 all = c_literal_plain_table[bytes[offset]] & c_literal_plain_table[bytes[offset + 1]] &
                 c_literal_plain_table[bytes[offset + 2]] & c_literal_plain_table[bytes[offset + 3]] &
                 c_literal_plain_table[bytes[offset + 4]] & c_literal_plain_table[bytes[offset + 5]] &
                 c_literal_plain_table[bytes[offset + 6]] & c_literal_plain_table[bytes[offset + 7]];
        if (!all)
        {
            break;
        }
        offset += 8;
    }
    while (offset < source.length && c_literal_plain_table[bytes[offset]])
    {
        offset += 1;
    }
    return offset;
}

BUSTER_C_INTERNAL bool c_horizontal_whitespace(char8 character)
{
    switch (character)
    {
    case ' ':
    case '\t':
    case '\v':
    case '\f':
    {
        return true;
    }
    default:
    {
        return false;
    }
    }
}

BUSTER_C_INLINE BUSTER_ALWAYS_INLINE u64 c_translate_plain_run_end_swar(String8 source, u64 offset, bool trigraphs)
{
    while (offset + 8 <= source.length)
    {
        u64 word;
        memcpy(&word, source.pointer + offset, sizeof(word));
        u64 carriage = word ^ UINT64_C(0x0D0D0D0D0D0D0D0D);
        u64 newline = word ^ UINT64_C(0x0A0A0A0A0A0A0A0A);
        u64 backslash = word ^ UINT64_C(0x5C5C5C5C5C5C5C5C);
        u64 low = UINT64_C(0x0101010101010101);
        u64 high = UINT64_C(0x8080808080808080);
        u64 found = ((carriage - low) & ~carriage) | ((newline - low) & ~newline) | ((backslash - low) & ~backslash);
        if (trigraphs)
        {
            u64 question = word ^ UINT64_C(0x3F3F3F3F3F3F3F3F);
            found |= (question - low) & ~question;
        }
        found &= high;
        if (found)
        {
            break;
        }
        offset += 8;
    }
    while (offset < source.length)
    {
        char8 probe = source.pointer[offset];
        if (probe == '\r' || probe == '\n' || probe == '\\' || (trigraphs && probe == '?'))
        {
            break;
        }
        offset += 1;
    }
    return offset;
}

#if BUSTER_C_TRANSLATE_AVX512
BUSTER_C_INTERNAL u64 c_translate_plain_run_end_avx512(String8 source, u64 offset, bool trigraphs)
{
    Simd512 carriage_return = simd512_splat('\r');
    Simd512 line_feed = simd512_splat('\n');
    Simd512 backslash = simd512_splat('\\');
    bool stopped = false;
    while (!stopped && source.length - offset >= 64)
    {
        Simd512 chunk = simd512_load(source.pointer + offset);
        Mask64 stop_mask = simd512_equal_u8(chunk, carriage_return) | simd512_equal_u8(chunk, line_feed) |
                           simd512_equal_u8(chunk, backslash);
        if (trigraphs)
        {
            stop_mask |= simd512_equal_u8(chunk, simd512_splat('?'));
        }
        if (stop_mask)
        {
            offset += mask64_first_set(stop_mask);
            stopped = true;
        }
        else
        {
            offset += 64;
        }
    }
    if (!stopped)
    {
        offset = c_translate_plain_run_end_swar(source, offset, trigraphs);
    }
    return offset;
}
#endif

#if BUSTER_INCLUDE_TESTS
bool c_test_space_null_empty_tokens(Arena* arena)
{
    CSpellingSpace space = c_space_local(arena, 1);
    CToken token = c_space_token(&space, (String8){0}, C_TOKEN_END_OF_FILE, (CPunctuator)0);
    CToken copied = c_space_retoken(&space, space.base, token);
    return !token.length && !copied.length && token.offset == copied.offset;
}

BUSTER_C_INTERNAL u64 c_translate_plain_run_end_scalar(String8 source, u64 offset, bool trigraphs)
{
    while (offset < source.length)
    {
        char8 probe = source.pointer[offset];
        if (probe == '\r' || probe == '\n' || probe == '\\' || (trigraphs && probe == '?'))
        {
            break;
        }
        offset += 1;
    }
    return offset;
}

bool c_test_translate_plain_run_paths_agree(String8 source)
{
    bool result = !source.length || source.pointer;
    for (u32 mode = 0; result && mode < 2; mode += 1)
    {
        bool trigraphs = mode != 0;
        for (u64 offset = 0; result; offset += 1)
        {
            u64 scalar_end = c_translate_plain_run_end_scalar(source, offset, trigraphs);
            result = c_translate_plain_run_end_swar(source, offset, trigraphs) == scalar_end;
#if BUSTER_C_TRANSLATE_AVX512
            result = result && c_translate_plain_run_end_avx512(source, offset, trigraphs) == scalar_end;
#endif
            if (offset == source.length)
            {
                break;
            }
        }
    }
    return result;
}
#endif

// Validate the source size before constructing any source-derived allocation
// count or reading the source pointer. Rejected plans leave the output alone.
BUSTER_C_INTERNAL bool c_source_allocation_plan(u64 length, CSourceAllocationPlan* plan)
{
    bool valid = length <= C_SOURCE_MAXIMUM_LENGTH;
    if (valid)
    {
        // All additions follow the u32 source bound. These compile-time
        // checks also keep the corresponding arena byte products in u64.
        BUSTER_CT_CHECK(C_SOURCE_MAXIMUM_LENGTH + 2 <= UINT64_MAX / sizeof(IrSourceCheckpoint));
        BUSTER_CT_CHECK(C_SOURCE_MAXIMUM_LENGTH + 2 <= UINT64_MAX / sizeof(u32));
        BUSTER_CT_CHECK(C_SOURCE_MAXIMUM_LENGTH + 17 <= UINT64_MAX / sizeof(CToken));
        BUSTER_CT_CHECK(C_SOURCE_MAXIMUM_LENGTH + 1 <= (UINT64_MAX - BUSTER_MB(1)) / (2 * sizeof(CDiagnostic)));
        *plan = (CSourceAllocationPlan){
            .translated_capacity = length + 1,
            .checkpoint_capacity = length + 2,
        };
    }
    return valid;
}

#if BUSTER_INCLUDE_TESTS
bool c_test_source_allocation_plan(u64 length, CSourceAllocationPlan* plan)
{
    return c_source_allocation_plan(length, plan);
}
#endif

// GNU modes preserve literal question-mark sequences. C23 removed trigraphs;
// the supported earlier strict C modes require their phase-one replacement.
BUSTER_C_INTERNAL bool c_translate_trigraphs_enabled(CPreprocessDialect dialect)
{
    return dialect == C_PREPROCESS_DIALECT_C99 || dialect == C_PREPROCESS_DIALECT_C11 || dialect == C_PREPROCESS_DIALECT_C17;
}

BUSTER_C_INTERNAL char8 c_translate_trigraph_character(char8 third)
{
    char8 result = 0;
    switch (third)
    {
    case '=': result = '#'; break;
    case '/': result = '\\'; break;
    case '\'': result = '^'; break;
    case '(': result = '['; break;
    case ')': result = ']'; break;
    case '!': result = '|'; break;
    case '<': result = '{'; break;
    case '>': result = '}'; break;
    case '-': result = '~'; break;
    default: break;
    }
    return result;
}

// force_scalar keeps the byte-at-a-time run loop as the whole implementation,
// which is what c_lex_reference lexes through so the differential gate
// compares the chunk fast path against it.
BUSTER_C_INTERNAL CTranslatedSource c_translate_source(Arena* arena, CSpellingSpace* space, String8 source, bool force_scalar,
                                                      bool trigraphs, CSourceAllocationPlan const* plan)
{
    CTranslatedSource result = {0};
    if (plan)
    {
        char8* translated = space ? c_space_allocate(space, plan->translated_capacity) : arena_allocate(arena, char8, plan->translated_capacity);
        result.translated_offset = space ? c_space_offset(space, translated) : 0;
        IrSourceCheckpoint* checkpoints = arena_allocate(arena, IrSourceCheckpoint, plan->checkpoint_capacity);
        u32* checkpoint_offsets = arena_allocate(arena, u32, plan->checkpoint_capacity);
        u32 checkpoint_count = 0;
        u64 input = 0;
        u64 output = 0;
        u32 line = 1;
        u32 column = 1;
        // The next output byte starts a new linear run; initially true so the
        // first byte records the first checkpoint.
        bool run_broken = true;
        while (input < source.length)
        {
#if BUSTER_C_TRANSLATE_AVX512
            // Chunk fast path: a plain newline copies through unchanged, so a
            // 64-byte chunk whose only stop bytes are newlines needs no
            // per-run rescan -- the chunk stores whole, the newline mask
            // yields one checkpoint per line start, and line/column advance
            // by popcount and top-bit arithmetic.  Only the bytes that edit
            // the stream fall to the exact scalar handling below, at their
            // own position: every '\r' (folds to '\n'), a backslash whose
            // next byte opens a splice, and a backslash on the last lane,
            // whose next byte the chunk cannot see.
            if (!force_scalar)
            {
                while (source.length - input >= 64)
                {
                    Simd512 chunk = simd512_load(source.pointer + input);
                    Mask64 carriage = simd512_equal_u8(chunk, simd512_splat('\r'));
                    Mask64 line_feed = simd512_equal_u8(chunk, simd512_splat('\n'));
                    Mask64 backslash = simd512_equal_u8(chunk, simd512_splat('\\'));
                    u64 stops = carriage | (backslash & ((line_feed | carriage) >> 1)) | (backslash & (UINT64_C(1) << 63));
                    if (trigraphs)
                    {
                        // Stop at the first question mark, including the last
                        // lanes whose raw triple belongs to the next chunk.
                        stops |= simd512_equal_u8(chunk, simd512_splat('?'));
                    }
                    u64 limit = stops ? (u64)__builtin_ctzll(stops) : 64;
                    if (!limit)
                    {
                        break;
                    }
                    u64 newlines = line_feed & (limit >= 64 ? ~UINT64_C(0) : ((UINT64_C(1) << limit) - 1));
                    simd512_store(translated + output, chunk);
                    if (run_broken)
                    {
                        checkpoints[checkpoint_count] = (IrSourceCheckpoint){
                            .offset = (u32)input,
                            .line = line,
                            .column = column,
                        };
                        checkpoint_offsets[checkpoint_count] = (u32)output;
                        checkpoint_count += 1;
                        run_broken = false;
                    }
                    if (newlines)
                    {
                        column = (u32)(limit - (u64)(63 - __builtin_clzll(newlines)));
                        for (u64 rest = newlines; rest; rest &= rest - 1)
                        {
                            u64 position = (u64)__builtin_ctzll(rest);
                            line += 1;
                            // The byte after this newline starts a line; when
                            // it sits past the limit, the next iteration or
                            // the stop byte's own handling records it, which
                            // is what keeps a splice's skipped backslash from
                            // acquiring a checkpoint here.
                            if (position + 1 < limit)
                            {
                                checkpoints[checkpoint_count] = (IrSourceCheckpoint){
                                    .offset = (u32)(input + position + 1),
                                    .line = line,
                                    .column = 1,
                                };
                                checkpoint_offsets[checkpoint_count] = (u32)(output + position + 1);
                                checkpoint_count += 1;
                            }
                            else
                            {
                                run_broken = true;
                            }
                        }
                    }
                    else
                    {
                        column += (u32)limit;
                    }
                    input += limit;
                    output += limit;
                    if (limit < 64)
                    {
                        break;
                    }
                }
                if (input >= source.length)
                {
                    break;
                }
            }
#endif
            // A run containing no newline, backslash or enabled trigraph
            // candidate keeps line/column linear; editing bytes reach the exact
            // scalar handling below. Native AVX-512 hosts classify 64 bytes at a
            // time; every fallback retains the previous eight-byte SWAR scan.
            u64 plain_end = input;
            if (force_scalar)
            {
                // The reference must not share the vector plain-run scanner with
                // the dispatched path: disabling only the fused loop was weaker.
                while (plain_end < source.length)
                {
                    char8 character = source.pointer[plain_end];
                    if (character == '\r' || character == '\n' || character == '\\' || (trigraphs && character == '?'))
                    {
                        break;
                    }
                    plain_end += 1;
                }
            }
            else
            {
#if BUSTER_C_TRANSLATE_AVX512
                plain_end = c_translate_plain_run_end_avx512(source, input, trigraphs);
#else
                plain_end = c_translate_plain_run_end_swar(source, input, trigraphs);
#endif
            }
            if (plain_end > input)
            {
                if (run_broken)
                {
                    checkpoints[checkpoint_count] = (IrSourceCheckpoint){
                        .offset = (u32)input,
                        .line = line,
                        .column = column,
                    };
                    checkpoint_offsets[checkpoint_count] = (u32)output;
                    checkpoint_count += 1;
                    run_broken = false;
                }
                u64 plain_length = plain_end - input;
                memcpy(translated + output, source.pointer + input, plain_length);
                output += plain_length;
                column += (u32)plain_length;
                input = plain_end;
                continue;
            }
            char8 character = source.pointer[input];
            u64 character_length = 1;
            if (trigraphs && character == '?' && source.length - input >= 3 && source.pointer[input + 1] == '?')
            {
                char8 replacement = c_translate_trigraph_character(source.pointer[input + 2]);
                if (replacement)
                {
                    // Consume raw input only: phase-two splicing must not
                    // create another phase-one replacement opportunity.
                    character = replacement;
                    character_length = 3;
                    run_broken = true;
                }
            }
            u64 newline_length = 0;
            if (character == '\r')
            {
                newline_length = input + 1 < source.length && source.pointer[input + 1] == '\n' ? 2 : 1;
            }
            else if (character == '\n')
            {
                newline_length = 1;
            }
            if (character == '\\' && character_length < source.length - input)
            {
                u64 splice_length = 0;
                if (source.pointer[input + character_length] == '\n')
                {
                    splice_length = character_length + 1;
                }
                else if (source.pointer[input + character_length] == '\r')
                {
                    splice_length = character_length + 1 < source.length - input && source.pointer[input + character_length + 1] == '\n'
                        ? character_length + 2 : character_length + 1;
                }
                if (splice_length)
                {
                    input += splice_length;
                    line += 1;
                    column = 1;
                    run_broken = true;
                    continue;
                }
            }
            if (run_broken)
            {
                checkpoints[checkpoint_count] = (IrSourceCheckpoint){
                    .offset = (u32)input,
                    .line = line,
                    .column = column,
                };
                checkpoint_offsets[checkpoint_count] = (u32)output;
                checkpoint_count += 1;
            }
            if (newline_length)
            {
                translated[output++] = '\n';
                input += newline_length;
                line += 1;
                column = 1;
                run_broken = true;
            }
            else
            {
                translated[output++] = character;
                input += character_length;
                column += (u32)character_length;
                run_broken = character_length != 1;
            }
        }
        translated[output] = 0;
        checkpoints[checkpoint_count] = (IrSourceCheckpoint){
            .offset = (u32)source.length,
            .line = line,
            .column = column,
        };
        checkpoint_offsets[checkpoint_count] = (u32)output;
        checkpoint_count += 1;
        if (space)
        {
            // Translation only shrinks (trigraphs/splices delete bytes); hand the
            // unused tail back so the next spelling packs against it.
            c_space_shrink(space, source.length - output);
        }
        C_CENSUS_RECORD(TRANSLATE_CALLS, 1);
        C_CENSUS_RECORD(TRANSLATE_INPUT_BYTES, source.length);
        C_CENSUS_RECORD(TRANSLATE_COPIED_BYTES, output);
        C_CENSUS_RECORD(TRANSLATE_CHECKPOINTS, checkpoint_count);
        // Capacity, not use: both arrays are sized by the source length.
        C_CENSUS_RECORD(TRANSLATE_CHECKPOINT_BYTES, (source.length + 2) * (sizeof(*checkpoints) + sizeof(*checkpoint_offsets)));
        result.source = (String8){
            .pointer = translated,
            .length = output,
        };
        result.checkpoints = checkpoints;
        result.checkpoint_offsets = checkpoint_offsets;
        result.checkpoint_count = checkpoint_count;
        // The page bracket for the checkpoints, built in the one linear pass the
        // finished offsets allow. It is what keeps a line-table consumer's
        // per-line lookup from binary-searching the whole file.
        result.checkpoint_page_count = (u32)(output >> IR_SOURCE_CHECKPOINT_PAGE_SHIFT) + 1;
        result.checkpoint_pages = arena_allocate(arena, u32, result.checkpoint_page_count);
        u32 fill_checkpoint = 0;
        for (u32 page = 0; page < result.checkpoint_page_count; page += 1)
        {
            u32 page_start = page << IR_SOURCE_CHECKPOINT_PAGE_SHIFT;
            while (fill_checkpoint + 1 < checkpoint_count && checkpoint_offsets[fill_checkpoint + 1] <= page_start)
            {
                fill_checkpoint += 1;
            }
            result.checkpoint_pages[page] = fill_checkpoint;
        }
        // `line` counts breaks, so it is one past the lines that ended; a column
        // past the first means bytes followed the last break and opened one more.
        result.raw_lines = line - 1 + (column > 1);
    }

    return result;
}

// Line/column recovery for a translated-source-local offset through the
// retained checkpoints, amortized by the result's cursor: queries at
// non-decreasing offsets advance instead of searching.
BUSTER_C_INTERNAL CSourceLocation c_lex_local_location(CLexResult* result, u64 offset)
{
    C_CENSUS_PHASE_RECORD(LOCATION_RECOVERIES, 1);
    if (!result->checkpoint_count)
    {
        return (CSourceLocation){0};
    }
    u32 cursor = result->location_cursor;
    while (cursor + 1 < result->checkpoint_count && result->checkpoint_offsets[cursor + 1] <= offset)
    {
        cursor += 1;
    }
    while (cursor && result->checkpoint_offsets[cursor] > offset)
    {
        cursor -= 1;
    }
    result->location_cursor = cursor;
    IrSourceCheckpoint checkpoint = result->checkpoints[cursor];
    u32 delta = (u32)offset - result->checkpoint_offsets[cursor];
    return (CSourceLocation){
        .offset = checkpoint.offset + delta,
        .line = checkpoint.line,
        .column = checkpoint.column + delta,
        .map_offset = (u32)offset + result->translated_offset,
    };
}

CSourceLocation c_lex_token_location(CLexResult* lex, CToken token)
{
    return c_lex_local_location(lex, token.offset - lex->translated_offset);
}

// The stamp a macro invocation's output carries: one position every offset
// in the copied run resolves to.
BUSTER_C_INTERNAL IrSourcePosition c_position_from_source_location(CSourceLocation location)
{
    return (IrSourcePosition){
        .source = location.file,
        .offset = location.offset,
        .line = location.line,
        .column = location.column,
    };
}

// A position, in the shape this frontend's diagnostics take.
BUSTER_C_INTERNAL CSourceLocation c_source_location_from_position(u32 map_offset, IrSourcePosition position)
{
    return (CSourceLocation){
        .offset = position.offset,
        .line = position.line,
        .column = position.column,
        .file = position.source,
        .map_offset = map_offset,
    };
}

CSourceLocation c_preprocess_token_location(CPreprocessResult const* preprocess, CToken token)
{
    C_CENSUS_PHASE_RECORD(LOCATION_RECOVERIES, 1);
    CSourceMapRecovery const* recovery = preprocess->recovery;
    return c_source_location_from_position(token.offset, recovery ? ir_source_map_position(&recovery->map, token.offset, 0) : (IrSourcePosition){0});
}

BUSTER_C_SHARED CSourceSite c_preprocess_token_site_cursor(CPreprocessResult const* preprocess, CToken token, IrSourceMapCursor* cursor)
{
    IR_DIAGNOSTIC_CENSUS_RECORD(C_RECORD_SITES, 1);
    BUSTER_CHECK(token.offset < UINT32_MAX);
    CSourceSite result = {
        .map_offset_plus_one = token.offset + 1,
        .file = c_preprocess_token_source(preprocess, token, cursor),
    };
#if BUSTER_REFERENCE_CHECKS
    // The source a region's key names is the source every position in that
    // region resolves to (TEXT regions answer region->source, STAMP regions a
    // stamp built with the same source), so the site agrees with the eager
    // location it replaces.
    BUSTER_CHECK(result.file == c_preprocess_token_location(preprocess, token).file);
#endif
    return result;
}

CSourceSite c_preprocess_token_site(CPreprocessResult const* preprocess, CToken token)
{
    return c_preprocess_token_site_cursor(preprocess, token, 0);
}

CSourceLocation c_preprocess_site_location(CPreprocessResult const* preprocess, CSourceSite site)
{
    IR_DIAGNOSTIC_CENSUS_RECORD(C_SITE_RESOLUTIONS, 1);
    CSourceLocation result = {0};
    if (site.map_offset_plus_one)
    {
        CToken token = {
            .offset = site.map_offset_plus_one - 1,
        };
        result = c_preprocess_token_location(preprocess, token);
    }
    return result;
}

// The amortized variant for consumers whose queries mostly ascend (parsing
// walks tokens roughly in stream order).
BUSTER_C_SHARED CSourceLocation c_preprocess_token_location_cursor(CPreprocessResult const* preprocess, CToken token, IrSourceMapCursor* cursor)
{
    C_CENSUS_PHASE_RECORD(LOCATION_RECOVERIES, 1);
    CSourceMapRecovery const* recovery = preprocess->recovery;
    return c_source_location_from_position(token.offset,
                                           recovery ? ir_source_map_position(&recovery->map, token.offset, cursor) : (IrSourcePosition){0});
}

// Which source a token belongs to, without recovering its line and column:
// what lowering wants, since an IR source range is a source plus the offset
// the token already carries. Forced inline with ir_source_map_source: every
// lowered instruction asks, and record sites are a second caller.
BUSTER_C_SHARED BUSTER_SHARED_INLINE u32 c_preprocess_token_source(CPreprocessResult const* preprocess, CToken token, IrSourceMapCursor* cursor)
{
    CSourceMapRecovery const* recovery = preprocess->recovery;
    return recovery ? ir_source_map_source(&recovery->map, token.offset, cursor) : 0;
}

// One finished line, classified by what the lexer saw on it. Branchless: the
// flags are 0/1 and every bucket is an unconditional add, so a line of code,
// a line of comment and a blank line all cost the same four adds.
BUSTER_C_INTERNAL void c_source_metrics_line(CSourceMetrics* metrics, u32 has_code, u32 has_comment)
{
    metrics->translated_lines += 1;
    metrics->code_lines += has_code;
    metrics->comment_lines += has_comment;
    metrics->mixed_lines += has_code & has_comment;
    metrics->blank_lines += (has_code | has_comment) ^ 1;
}

void c_source_metrics_add(CSourceMetrics* total, CSourceMetrics const* part)
{
    total->files += part->files;
    total->bytes += part->bytes;
    total->translated_bytes += part->translated_bytes;
    total->lines += part->lines;
    total->translated_lines += part->translated_lines;
    total->spliced_lines += part->spliced_lines;
    total->code_lines += part->code_lines;
    total->comment_lines += part->comment_lines;
    total->mixed_lines += part->mixed_lines;
    total->blank_lines += part->blank_lines;
    total->comment_bytes += part->comment_bytes;
    total->blank_bytes += part->blank_bytes;
    total->literal_bytes += part->literal_bytes;
    total->comments += part->comments;
    total->tokens += part->tokens;
}

u64 c_source_metrics_code_bytes(CSourceMetrics metrics)
{
    return metrics.translated_bytes - metrics.comment_bytes - metrics.blank_bytes;
}

// The row index for `path`, appending a fresh zero-count row the first time
// the path is seen; a first sight is `rows[result].lex_count == 0`.
BUSTER_C_INTERNAL u32 c_source_metrics_file_row_hashed(Arena* arena, CSourceMetricsFileSet* set, String8 path, u64 hash)
{
    if (set->count * 2 >= set->capacity)
    {
        u32 capacity = set->capacity ? set->capacity * 2 : 256;
        u64* hashes = arena_allocate(arena, u64, capacity);
        u32* slot_rows = arena_allocate(arena, u32, capacity);
        memset(hashes, 0, capacity * sizeof(*hashes));
        for (u32 index = 0; index < set->capacity; index += 1)
        {
            u64 moved = set->hashes[index];
            if (moved)
            {
                u32 slot = (u32)moved & (capacity - 1);
                while (hashes[slot])
                {
                    slot = (slot + 1) & (capacity - 1);
                }
                hashes[slot] = moved;
                slot_rows[slot] = set->slot_rows[index];
            }
        }
        set->hashes = hashes;
        set->slot_rows = slot_rows;
        set->capacity = capacity;
    }
    // Zero marks an empty slot, so no path may hash to it.
    hash |= 1;
    u32 slot = (u32)hash & (set->capacity - 1);
    bool found = false;
    while (set->hashes[slot] && !found)
    {
        found = set->hashes[slot] == hash;
        if (found)
        {
            String8 stored_path = set->rows[set->slot_rows[slot]].path;
            found = stored_path.length == path.length &&
                    (!path.length || memcmp(stored_path.pointer, path.pointer, path.length) == 0);
        }
        slot = found ? slot : (slot + 1) & (set->capacity - 1);
    }
    u32 result;
    if (found)
    {
        result = set->slot_rows[slot];
    }
    else
    {
        if (set->count == set->row_capacity)
        {
            u32 row_capacity = set->row_capacity ? set->row_capacity * 2 : 128;
            CSourceFileMetrics* rows = arena_allocate(arena, CSourceFileMetrics, row_capacity);
            if (set->count)
            {
                memcpy(rows, set->rows, set->count * sizeof(*rows));
            }
            set->rows = rows;
            set->row_capacity = row_capacity;
        }
        result = set->count;
        set->rows[result] = (CSourceFileMetrics){.path = path};
        set->hashes[slot] = hash;
        set->slot_rows[slot] = result;
        set->count += 1;
    }

    return result;
}

BUSTER_C_INTERNAL u32 c_source_metrics_file_row(Arena* arena, CSourceMetricsFileSet* set, String8 path)
{
    return c_source_metrics_file_row_hashed(arena, set, path, buster_hash_64((u8*)path.pointer, path.length));
}

#if BUSTER_INCLUDE_TESTS
u32 c_test_source_metrics_file_row(Arena* arena, CSourceMetricsFileSet* set, String8 path, u64 hash)
{
    return c_source_metrics_file_row_hashed(arena, set, path, hash);
}
#endif

// The cold tail of c_token_push: the item is 0xFFFF bytes or longer. A
// terminated literal — the scan bounded by the item's extent reproduces the
// exact length, which certifies both termination and escape parity — stores
// the sentinel c_token_length re-derives from. Everything else (an
// unterminated literal, an identifier or number the call site diagnoses)
// clamps to the largest direct length; the stream already carries an error
// diagnostic in every clamping case, so the lie never reaches a compile
// that succeeds, and a clamped read stays inside the real spelling.
BUSTER_C_INTERNAL u16 c_token_push_long_length(CTranslatedSource translated, u64 start, u64 end, CTokenKind kind)
{
    u64 length = end - start;
    bool literal = kind == C_TOKEN_STRING_LITERAL || kind == C_TOKEN_CHARACTER_LITERAL;
    bool exact = literal && c_token_literal_scan_length(translated.source.pointer + start, length) == length;
    return exact ? C_TOKEN_LENGTH_OVERSIZED : C_TOKEN_LENGTH_OVERSIZED - 1;
}

// Spelling ids drive maximal munch; published token ids drive C semantics.
// Offsets and lengths retain the original spelling for #, ## and -E.
BUSTER_C_INTERNAL CPunctuator c_punctuator_canonical(CPunctuator punctuator)
{
    CPunctuator result;
    switch (punctuator)
    {
    case C_PUNCTUATOR_LEFT_BRACKET_DIGRAPH: result = C_PUNCTUATOR_LEFT_BRACKET; break;
    case C_PUNCTUATOR_RIGHT_BRACKET_DIGRAPH: result = C_PUNCTUATOR_RIGHT_BRACKET; break;
    case C_PUNCTUATOR_LEFT_BRACE_DIGRAPH: result = C_PUNCTUATOR_LEFT_BRACE; break;
    case C_PUNCTUATOR_RIGHT_BRACE_DIGRAPH: result = C_PUNCTUATOR_RIGHT_BRACE; break;
    case C_PUNCTUATOR_HASH_DIGRAPH: result = C_PUNCTUATOR_HASH; break;
    case C_PUNCTUATOR_HASH_HASH_DIGRAPH: result = C_PUNCTUATOR_HASH_HASH; break;
    default: result = punctuator; break;
    }
    return result;
}

BUSTER_C_INTERNAL void c_token_push(CLexResult* result, CTranslatedSource translated, u64 start, u64 end, CTokenKind kind, CPunctuator punctuator)
{
    u64 length = end - start;
    u16 stored = length < C_TOKEN_LENGTH_OVERSIZED ? (u16)length : c_token_push_long_length(translated, start, end, kind);
    u64 token_index = result->token_count;
    CToken token = {
        .offset = translated.translated_offset + (u32)start,
        .length = stored,
        .kind = (u8)kind,
        .punctuator = (u8)punctuator,
    };
    result->tokens[token_index] = token;
    if (result->token_shapes)
    {
        result->token_shapes[token_index] = c_token_shape_from_token(token);
    }
    result->token_count = token_index + 1;
}

// Indexed by CPunctuator, so the id and the spelling cannot drift apart.  The
// scan walks the table in enum order, which CPunctuator declares longest-first
// for maximal munch.
BUSTER_C_INTERNAL String8 const c_punctuator_spellings[C_PUNCTUATOR_COUNT] = {
    [C_PUNCTUATOR_HASH_HASH_DIGRAPH] = S8_INITIALIZER("%:%:"),
    [C_PUNCTUATOR_SHIFT_LEFT_ASSIGN] = S8_INITIALIZER("<<="),
    [C_PUNCTUATOR_SHIFT_RIGHT_ASSIGN] = S8_INITIALIZER(">>="),
    [C_PUNCTUATOR_ELLIPSIS] = S8_INITIALIZER("..."),
    [C_PUNCTUATOR_ARROW] = S8_INITIALIZER("->"),
    [C_PUNCTUATOR_PLUS_PLUS] = S8_INITIALIZER("++"),
    [C_PUNCTUATOR_MINUS_MINUS] = S8_INITIALIZER("--"),
    [C_PUNCTUATOR_SHIFT_LEFT] = S8_INITIALIZER("<<"),
    [C_PUNCTUATOR_SHIFT_RIGHT] = S8_INITIALIZER(">>"),
    [C_PUNCTUATOR_LESS_EQUAL] = S8_INITIALIZER("<="),
    [C_PUNCTUATOR_GREATER_EQUAL] = S8_INITIALIZER(">="),
    [C_PUNCTUATOR_EQUAL] = S8_INITIALIZER("=="),
    [C_PUNCTUATOR_NOT_EQUAL] = S8_INITIALIZER("!="),
    [C_PUNCTUATOR_AMPERSAND_AMPERSAND] = S8_INITIALIZER("&&"),
    [C_PUNCTUATOR_PIPE_PIPE] = S8_INITIALIZER("||"),
    [C_PUNCTUATOR_STAR_ASSIGN] = S8_INITIALIZER("*="),
    [C_PUNCTUATOR_SLASH_ASSIGN] = S8_INITIALIZER("/="),
    [C_PUNCTUATOR_PERCENT_ASSIGN] = S8_INITIALIZER("%="),
    [C_PUNCTUATOR_PLUS_ASSIGN] = S8_INITIALIZER("+="),
    [C_PUNCTUATOR_MINUS_ASSIGN] = S8_INITIALIZER("-="),
    [C_PUNCTUATOR_AMPERSAND_ASSIGN] = S8_INITIALIZER("&="),
    [C_PUNCTUATOR_CARET_ASSIGN] = S8_INITIALIZER("^="),
    [C_PUNCTUATOR_PIPE_ASSIGN] = S8_INITIALIZER("|="),
    [C_PUNCTUATOR_HASH_HASH] = S8_INITIALIZER("##"),
    [C_PUNCTUATOR_LEFT_BRACKET_DIGRAPH] = S8_INITIALIZER("<:"),
    [C_PUNCTUATOR_RIGHT_BRACKET_DIGRAPH] = S8_INITIALIZER(":>"),
    [C_PUNCTUATOR_LEFT_BRACE_DIGRAPH] = S8_INITIALIZER("<%"),
    [C_PUNCTUATOR_RIGHT_BRACE_DIGRAPH] = S8_INITIALIZER("%>"),
    [C_PUNCTUATOR_HASH_DIGRAPH] = S8_INITIALIZER("%:"),
    [C_PUNCTUATOR_LEFT_BRACKET] = S8_INITIALIZER("["),
    [C_PUNCTUATOR_RIGHT_BRACKET] = S8_INITIALIZER("]"),
    [C_PUNCTUATOR_LEFT_PARENTHESIS] = S8_INITIALIZER("("),
    [C_PUNCTUATOR_RIGHT_PARENTHESIS] = S8_INITIALIZER(")"),
    [C_PUNCTUATOR_LEFT_BRACE] = S8_INITIALIZER("{"),
    [C_PUNCTUATOR_RIGHT_BRACE] = S8_INITIALIZER("}"),
    [C_PUNCTUATOR_DOT] = S8_INITIALIZER("."),
    [C_PUNCTUATOR_AMPERSAND] = S8_INITIALIZER("&"),
    [C_PUNCTUATOR_STAR] = S8_INITIALIZER("*"),
    [C_PUNCTUATOR_PLUS] = S8_INITIALIZER("+"),
    [C_PUNCTUATOR_MINUS] = S8_INITIALIZER("-"),
    [C_PUNCTUATOR_TILDE] = S8_INITIALIZER("~"),
    [C_PUNCTUATOR_EXCLAMATION] = S8_INITIALIZER("!"),
    [C_PUNCTUATOR_SLASH] = S8_INITIALIZER("/"),
    [C_PUNCTUATOR_PERCENT] = S8_INITIALIZER("%"),
    [C_PUNCTUATOR_LESS] = S8_INITIALIZER("<"),
    [C_PUNCTUATOR_GREATER] = S8_INITIALIZER(">"),
    [C_PUNCTUATOR_CARET] = S8_INITIALIZER("^"),
    [C_PUNCTUATOR_PIPE] = S8_INITIALIZER("|"),
    [C_PUNCTUATOR_QUESTION] = S8_INITIALIZER("?"),
    [C_PUNCTUATOR_COLON] = S8_INITIALIZER(":"),
    [C_PUNCTUATOR_SEMICOLON] = S8_INITIALIZER(";"),
    [C_PUNCTUATOR_ASSIGN] = S8_INITIALIZER("="),
    [C_PUNCTUATOR_COMMA] = S8_INITIALIZER(","),
    [C_PUNCTUATOR_HASH] = S8_INITIALIZER("#"),
    [C_PUNCTUATOR_AT] = S8_INITIALIZER("@"),
    [C_PUNCTUATOR_BACKSLASH] = S8_INITIALIZER("\\"),
};

// First-byte dispatch over c_punctuator_spellings, derived from the table at
// first use so the spellings stay the single source of truth. Enum order is
// longest-first, so each per-byte group inherits maximal munch. Without it,
// every lexed punctuator paid a linear scan of the whole table — a `;` sat
// behind ~52 failed memcmp probes.
typedef struct CPunctuatorDispatch CPunctuatorDispatch;
struct CPunctuatorDispatch
{
    u8 start;
    u8 count;
};

BUSTER_C_INTERNAL u8 c_punctuator_dispatch_order[C_PUNCTUATOR_COUNT];
BUSTER_C_INTERNAL CPunctuatorDispatch c_punctuator_dispatch[256];
BUSTER_C_INTERNAL bool c_punctuator_dispatch_built;

BUSTER_C_INTERNAL void c_punctuator_dispatch_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    u32 cursor = 0;
    for (u32 first = 0; first < 256; first += 1)
    {
        u32 start = cursor;
        for (u32 punctuator_index = C_PUNCTUATOR_NONE + 1; punctuator_index < C_PUNCTUATOR_COUNT; punctuator_index += 1)
        {
            String8 spelling = c_punctuator_spellings[punctuator_index];
            if (spelling.length && (u8)spelling.pointer[0] == first)
            {
                c_punctuator_dispatch_order[cursor++] = (u8)punctuator_index;
            }
        }
        c_punctuator_dispatch[first] = (CPunctuatorDispatch){
            .start = (u8)start,
            .count = (u8)(cursor - start),
        };
    }
    c_punctuator_dispatch_built = true;
}

BUSTER_C_INTERNAL u64 c_punctuator_length(String8 source, u64 offset, CPunctuator* punctuator_out)
{
    if (!c_punctuator_dispatch_built)
    {
        c_punctuator_dispatch_build();
    }
    if (offset < source.length)
    {
        CPunctuatorDispatch dispatch = c_punctuator_dispatch[(u8)source.pointer[offset]];
        for (u32 order_index = 0; order_index < dispatch.count; order_index += 1)
        {
            u32 punctuator_index = c_punctuator_dispatch_order[dispatch.start + order_index];
            String8 punctuator = c_punctuator_spellings[punctuator_index];
            if (punctuator.length > source.length - offset)
            {
                continue;
            }
            bool match = true;
            for (u64 byte_index = 1; byte_index < punctuator.length; byte_index += 1)
            {
                if (source.pointer[offset + byte_index] != punctuator.pointer[byte_index])
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                *punctuator_out = c_punctuator_canonical((CPunctuator)punctuator_index);
                return punctuator.length;
            }
        }
    }
    *punctuator_out = C_PUNCTUATOR_NONE;
    return 0;
}

BUSTER_C_INTERNAL bool c_literal_prefix(String8 source, u64 offset, u64* prefix_length, char8* delimiter)
{
    *prefix_length = 0;
    *delimiter = 0;
    if (offset < source.length)
    {
        char8 character = source.pointer[offset];
        if (character == '\'' || character == '"')
        {
            *delimiter = character;
            return true;
        }
        if (character == 'u' && offset + 2 < source.length && source.pointer[offset + 1] == '8' &&
            (source.pointer[offset + 2] == '\'' || source.pointer[offset + 2] == '"'))
        {
            *prefix_length = 2;
            *delimiter = source.pointer[offset + 2];
            return true;
        }
        if ((character == 'u' || character == 'U' || character == 'L') && offset + 1 < source.length &&
            (source.pointer[offset + 1] == '\'' || source.pointer[offset + 1] == '"'))
        {
            *prefix_length = 1;
            *delimiter = source.pointer[offset + 1];
            return true;
        }
    }

    return false;
}

// Everything one lex mutates, so the scalar reference loop and the compaction
// emitter below drive tokens, diagnostics and measurement through exactly the
// same code.  The three line fields are the only state that crosses a
// compaction window: they describe the line under construction, never the
// tokenizer, so the window pipeline still starts every window from scratch.
typedef enum CLexDiagnosticStorage
{
    C_LEX_DIAGNOSTIC_STORAGE_NONE,
    C_LEX_DIAGNOSTIC_STORAGE_SCRATCH,
    C_LEX_DIAGNOSTIC_STORAGE_DEDICATED,
    C_LEX_DIAGNOSTIC_STORAGE_RESULT,
} CLexDiagnosticStorage;

typedef struct CLexState CLexState;
struct CLexState
{
    CLexResult* result;
    CTranslatedSource translated;
    CPreprocessDialect dialect;
    // The result's arena. Diagnostic rows grow in diagnostic_arena, which is
    // rewound or destroyed once lexing ends and the rows are copied out, so
    // anything a row points at -- a formatted message -- lives here instead.
    Arena* arena;
    // Taken at the first diagnostic (c_lex_diagnostic_storage_begin); null
    // and zero while the file lexes cleanly.
    Arena* diagnostic_arena;
    TemporalArena diagnostic_temporary;
    u64 diagnostic_reserve_size;
    u64 diagnostic_capacity;
    u64 maximum_diagnostic_count;
    // Offset just past the newline that ended the last line, so a file whose
    // last line has no terminating newline still ends a line.
    u64 line_start;
    u64 newline_tokens;
    u32 line_has_code;
    u32 line_has_comment;
    // Nonzero on the compaction path only: c_lex_scan_one then fast-forwards
    // its comment and literal scans in 64-byte strides.  The reference path
    // keeps the byte loops, so the differential gate compares the two.
    u32 simd_scan;
    CLexDiagnosticStorage diagnostic_storage;
};

// Where a lexer's diagnostic rows grow, chosen at its first diagnostic so a
// file that lexes cleanly takes none: a scratch arena when the file's worst
// case -- one diagnostic per byte, plus the growth copies -- fits it, else a
// dedicated arena. Should that arena not be available the rows grow in the
// result arena itself, which costs only the growth copies.
BUSTER_C_INTERNAL void c_lex_diagnostic_storage_begin(CLexState* state)
{
    Arena* conflicts[] = {
        state->arena,
    };
    state->diagnostic_temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    Arena* scratch = state->diagnostic_temporary.arena;
    if (state->diagnostic_reserve_size <= scratch->reserved_size - scratch->position)
    {
        state->diagnostic_arena = scratch;
        state->diagnostic_storage = C_LEX_DIAGNOSTIC_STORAGE_SCRATCH;
    }
    else
    {
        scratch_end(state->diagnostic_temporary);
        u64 reserved_size = BUSTER_MAX(BUSTER_MB(64), state->diagnostic_reserve_size);
        IR_DIAGNOSTIC_CENSUS_RECORD(C_LEX_DIAGNOSTIC_ARENAS, 1);
        IR_DIAGNOSTIC_CENSUS_RECORD(C_LEX_DIAGNOSTIC_ARENA_BYTES, reserved_size);
        state->diagnostic_arena = arena_create((ArenaCreation){
            .reserved_size = reserved_size,
        });
        state->diagnostic_storage = state->diagnostic_arena ? C_LEX_DIAGNOSTIC_STORAGE_DEDICATED : C_LEX_DIAGNOSTIC_STORAGE_RESULT;
        if (!state->diagnostic_arena)
        {
            state->diagnostic_arena = state->arena;
        }
    }
}

// Releases the storage c_lex_diagnostic_storage_begin took, once the rows are
// copied into the result arena.
BUSTER_C_INTERNAL void c_lex_diagnostic_storage_end(CLexState* state)
{
    if (state->diagnostic_storage == C_LEX_DIAGNOSTIC_STORAGE_SCRATCH)
    {
        scratch_end(state->diagnostic_temporary);
    }
    else if (state->diagnostic_storage == C_LEX_DIAGNOSTIC_STORAGE_DEDICATED)
    {
        arena_destroy(state->diagnostic_arena, 1);
    }
}

// The capacity sequence is the one the rows had when 64 of them were reserved
// before lexing: 64 (or one per byte for a shorter file), then doubling up to
// one per byte.
BUSTER_C_INTERNAL void c_diagnostic_push(CLexState* state, u64 offset, CDiagnosticKind kind, String8 message)
{
    IR_DIAGNOSTIC_CENSUS_RECORD(C_DIAGNOSTICS_RECORDED, 1);
    CLexResult* result = state->result;
    BUSTER_VALIDATE(result->diagnostic_count < state->maximum_diagnostic_count);
    if (state->diagnostic_storage == C_LEX_DIAGNOSTIC_STORAGE_NONE)
    {
        c_lex_diagnostic_storage_begin(state);
    }
    if (result->diagnostic_count == state->diagnostic_capacity)
    {
        u64 capacity = !state->diagnostic_capacity                                          ? BUSTER_MIN(state->maximum_diagnostic_count, UINT64_C(64))
                       : state->diagnostic_capacity > state->maximum_diagnostic_count / 2 ? state->maximum_diagnostic_count
                                                                                          : state->diagnostic_capacity * 2;
        if (!state->diagnostic_capacity)
        {
            C_DIAGNOSTIC_RESERVATION_CENSUS(LEX, capacity);
        }
        CDiagnostic* diagnostics = arena_allocate(state->diagnostic_arena, CDiagnostic, capacity);
        if (result->diagnostic_count)
        {
            memcpy(diagnostics, result->diagnostics, sizeof(*diagnostics) * result->diagnostic_count);
        }
        result->diagnostics = diagnostics;
        state->diagnostic_capacity = capacity;
    }
    result->diagnostics[result->diagnostic_count++] = (CDiagnostic){
        .message = message,
        .location = c_lex_local_location(result, offset),
        .kind = kind,
        .severity = C_DIAGNOSTIC_ERROR,
    };
}

#if BUSTER_C_LEX_COMPACT

// The reference spelling of "the lanes below `bit_index`", saturating at the
// window width: one compare, one shift and one conditional move.
BUSTER_C_INLINE BUSTER_UNUSED_DECL BUSTER_INLINE u64 c_lex_mask_below_reference(u64 bit_index)
{
    return bit_index >= 64 ? ~(u64)0 : (((u64)1 << bit_index) - 1);
}

// The same mask in one instruction.  BMI2's BZHI reads its lane count from the
// low eight bits of the index operand and clears nothing when that count is 64
// or more, which is exactly the reference's saturation, so the whole helper is
// `bzhi $-1, index` for every index this lexer forms.  The bound is what makes
// the low-eight-bits reading safe, so it is stated: every index here is a
// window position (0..64) or a window position plus a punctuator spelling
// (at most 67), and the check compiles to an optimizer assumption under
// BUSTER_OPTIMIZE rather than to code.  c_lex_lane_mask keeps the reference
// form because its callers pass whole-file byte counts.
#define C_LEX_MASK_BELOW_LIMIT 68

BUSTER_C_INLINE BUSTER_INLINE u64 c_lex_mask_below(u64 bit_index)
{
    BUSTER_CHECK(bit_index < C_LEX_MASK_BELOW_LIMIT);
#if defined(__BMI2__)
    u64 result = _bzhi_u64(~(u64)0, (u32)bit_index);
#else
    u64 result = c_lex_mask_below_reference(bit_index);
#endif
    return result;
}

BUSTER_C_INLINE BUSTER_INLINE __mmask64 c_lex_lane_mask(u64 count)
{
    return (__mmask64)c_lex_mask_below_reference(count);
}

// The bytes one window of the compact emitter reads: its own 64 lanes plus the
// two the punctuator NFA's second and third lookups and the comment delimiters
// see past the last of them.  A window with that many bytes left needs no lane
// mask on any of its three loads.
#define C_LEX_WINDOW_LOADED_BYTES 66

// Lanes of a lookahead load that hold a real byte.  The window bounds do not
// serve: a punctuator or comment delimiter near the window's end is classified
// from bytes the next window owns, and reading them as zero would mis-spell it.
BUSTER_C_INLINE BUSTER_INLINE __mmask64 c_lex_lookahead_mask(u64 remaining, u64 ahead)
{
    return c_lex_lane_mask(remaining > ahead ? remaining - ahead : 0);
}

// 64-byte-stride forward scans for the escape items the window masks do not
// model — comments and literals longer than a window (2026-08-10b's largest
// remaining slice: on this tree half of all lexed bytes are such items, most
// of them the generated string tables).  Each returns the offset of the first
// byte c_lex_scan_one's byte loop would have stopped at, or source.length, so
// the loops keep their exact stop-byte handling and only the plain runs
// between stops vectorize.  Invalid lanes load as zero, and no scanned-for
// byte is NUL, so the compares need no separate bounds mask.

BUSTER_C_INLINE BUSTER_INLINE u64 c_lex_byte_find(String8 source, u64 offset, char8 target)
{
    const __m512i needle = _mm512_set1_epi8((char)target);
    u64 result = source.length;
    while (offset < source.length)
    {
        __m512i chunk = _mm512_maskz_loadu_epi8(c_lex_lane_mask(source.length - offset), source.pointer + offset);
        u64 hits = (u64)_mm512_cmpeq_epi8_mask(chunk, needle);
        if (hits)
        {
            result = offset + (u64)__builtin_ctzll(hits);
            break;
        }
        offset += 64;
    }
    return result;
}

BUSTER_C_INLINE BUSTER_INLINE u64 c_lex_literal_special_find(String8 source, u64 offset, char8 delimiter)
{
    const __m512i delimiter_vector = _mm512_set1_epi8((char)delimiter);
    const __m512i backslash_vector = _mm512_set1_epi8('\\');
    const __m512i newline_vector = _mm512_set1_epi8('\n');
    u64 result = source.length;
    while (offset < source.length)
    {
        __m512i chunk = _mm512_maskz_loadu_epi8(c_lex_lane_mask(source.length - offset), source.pointer + offset);
        u64 hits = (u64)_mm512_cmpeq_epi8_mask(chunk, delimiter_vector) | (u64)_mm512_cmpeq_epi8_mask(chunk, backslash_vector) |
                   (u64)_mm512_cmpeq_epi8_mask(chunk, newline_vector);
        if (hits)
        {
            result = offset + (u64)__builtin_ctzll(hits);
            break;
        }
        offset += 64;
    }
    return result;
}

BUSTER_C_INLINE BUSTER_INLINE u64 c_lex_block_comment_stop_find(String8 source, u64 offset)
{
    const __m512i star_vector = _mm512_set1_epi8('*');
    const __m512i slash_vector = _mm512_set1_epi8('/');
    const __m512i newline_vector = _mm512_set1_epi8('\n');
    u64 result = source.length;
    while (offset < source.length)
    {
        u64 remaining = source.length - offset;
        __m512i chunk = _mm512_maskz_loadu_epi8(c_lex_lane_mask(remaining), source.pointer + offset);
        // A star in the last lane pairs with the next chunk's first byte, so
        // the lookahead load is masked by the file bounds, not the chunk's.
        __m512i next = _mm512_maskz_loadu_epi8(c_lex_lookahead_mask(remaining, 1), source.pointer + offset + 1);
        u64 close = (u64)_mm512_cmpeq_epi8_mask(chunk, star_vector) & (u64)_mm512_cmpeq_epi8_mask(next, slash_vector);
        u64 hits = close | (u64)_mm512_cmpeq_epi8_mask(chunk, newline_vector);
        if (hits)
        {
            result = offset + (u64)__builtin_ctzll(hits);
            break;
        }
        offset += 64;
    }
    return result;
}

#endif

// Only identifier/pp-number spellings use this source-input contract. Comments
// and literal payloads retain their byte policy; OS utf8_decode is unrelated.
// Report the first byte of the first ill-formed sequence, including a truncated
// leader at the end of this bounded token. Translation checkpoints recover its
// physical position even when a CRLF splice occurs inside the sequence.
BUSTER_C_INTERNAL void c_lex_validate_word_utf8(CLexState* state, u64 offset, u64 end)
{
    const u8* bytes = (const u8*)state->translated.source.pointer;
    while (offset < end)
    {
        u8 leader = bytes[offset];
        u32 length = leader < 0x80 ? 1 : leader >= 0xC2 && leader <= 0xDF ? 2 :
                     leader >= 0xE0 && leader <= 0xEF ? 3 : leader >= 0xF0 && leader <= 0xF4 ? 4 : 0;
        bool valid = length && length <= end - offset;
        for (u32 index = 1; valid && index < length; index += 1)
        {
            valid = (bytes[offset + index] & 0xC0) == 0x80;
        }
        if (valid && length >= 3)
        {
            u8 second = bytes[offset + 1];
            valid = (leader != 0xE0 || second >= 0xA0) && (leader != 0xED || second < 0xA0) &&
                    (leader != 0xF0 || second >= 0x90) && (leader != 0xF4 || second < 0x90);
        }
        if (!valid)
        {
            c_diagnostic_push(state, offset, C_DIAGNOSTIC_INVALID_UTF8, S8("invalid UTF-8 sequence in C source token"));
            break;
        }
        offset += length;
    }
}

// UCN syntax/range diagnostics belong to the original backslash. Translation
// checkpoints recover its physical column even after trigraphs or splicing.
BUSTER_C_INTERNAL u64 c_lex_ucn(CLexState* state, u64 offset, bool initial)
{
    CUcn name = c_ucn_decode(state->translated.source, offset);
    String8 message = {0};
    if (name.status == C_UCN_MALFORMED)
    {
        message = S8("malformed universal character name in identifier");
    }
    else if (name.status == C_UCN_SURROGATE)
    {
        message = S8("surrogate universal character name in identifier");
    }
    else if (name.status == C_UCN_OUT_OF_RANGE)
    {
        message = S8("universal character name exceeds Unicode range in identifier");
    }
    else if (name.status == C_UCN_FORBIDDEN || !c_ucn_identifier_allowed(name.value, state->dialect, initial))
    {
        message = S8("universal character name is not allowed in this identifier position");
    }
    if (message.length)
    {
        c_diagnostic_push(state, offset, C_DIAGNOSTIC_INVALID_CHARACTER, message);
    }
    return offset + name.length;
}

// One item of the scalar lexer: a whitespace byte, a newline, a comment, or a
// token, starting at `offset` and returning the offset just past it. The
// compaction emitter escapes here for shapes its masks do not model. Long
// comment/literal bodies fast-forward in 64-byte strides for state->simd_scan;
// the reference keeps byte loops so the differential gate compares the two.
BUSTER_C_INTERNAL u64 c_lex_scan_one(CLexState* state, u64 offset)
{
    CLexResult* result = state->result;
    CTranslatedSource translated = state->translated;
    char8 character = translated.source.pointer[offset];
    if (c_horizontal_whitespace(character))
    {
        result->metrics.blank_bytes += 1;
        return offset + 1;
    }
    if (character == '\n')
    {
        c_token_push(result, translated, offset, offset + 1, C_TOKEN_NEWLINE, C_PUNCTUATOR_NONE);
        c_source_metrics_line(&result->metrics, state->line_has_code, state->line_has_comment);
        result->metrics.blank_bytes += 1;
        state->newline_tokens += 1;
        state->line_has_code = 0;
        state->line_has_comment = 0;
        state->line_start = offset + 1;
        return offset + 1;
    }
    if (character == '/' && offset + 1 < translated.source.length && translated.source.pointer[offset + 1] == '/')
    {
        u64 comment_start = offset;
        offset += 2;
#if BUSTER_C_LEX_COMPACT
        if (state->simd_scan)
        {
            offset = c_lex_byte_find(translated.source, offset, '\n');
        }
#endif
        while (offset < translated.source.length && translated.source.pointer[offset] != '\n')
        {
            offset += 1;
        }
        result->metrics.comment_bytes += offset - comment_start;
        result->metrics.comments += 1;
        state->line_has_comment = 1;
        return offset;
    }
    if (character == '/' && offset + 1 < translated.source.length && translated.source.pointer[offset + 1] == '*')
    {
        u64 comment_start = offset;
        offset += 2;
        bool terminated = false;
        // Set before the scan so the lines a multi-line comment crosses are
        // all attributed to it as they end.
        state->line_has_comment = 1;
        while (offset < translated.source.length)
        {
#if BUSTER_C_LEX_COMPACT
            // Fast-forward to the next stop byte; the checks below then
            // consume it exactly as the byte loop would have.
            if (state->simd_scan)
            {
                offset = c_lex_block_comment_stop_find(translated.source, offset);
                if (offset >= translated.source.length)
                {
                    break;
                }
            }
#endif
            if (translated.source.pointer[offset] == '*' && offset + 1 < translated.source.length && translated.source.pointer[offset + 1] == '/')
            {
                offset += 2;
                terminated = true;
                break;
            }
            if (translated.source.pointer[offset] == '\n')
            {
                c_token_push(result, translated, offset, offset + 1, C_TOKEN_NEWLINE, C_PUNCTUATOR_NONE);
                c_source_metrics_line(&result->metrics, state->line_has_code, 1);
                state->newline_tokens += 1;
                state->line_has_code = 0;
                state->line_start = offset + 1;
            }
            offset += 1;
        }
        result->metrics.comment_bytes += offset - comment_start;
        result->metrics.comments += 1;
        if (!terminated)
        {
            c_diagnostic_push(state, comment_start, C_DIAGNOSTIC_UNTERMINATED_BLOCK_COMMENT, S8("unterminated block comment"));
        }
        return offset;
    }
    // Everything past this point produces a token, so one store here covers
    // every kind instead of one per lexing branch.
    state->line_has_code = 1;
    u64 literal_prefix_length = 0;
    char8 literal_delimiter = 0;
    if (c_literal_prefix(translated.source, offset, &literal_prefix_length, &literal_delimiter))
    {
        u64 start = offset;
        offset += literal_prefix_length + 1;
        bool terminated = false;
        while (offset < translated.source.length)
        {
            // The stride scan seeks only this literal's own delimiter where
            // the shared table also stops at the other quote kind; the loop
            // treats that byte as plain either way.
#if BUSTER_C_LEX_COMPACT
            offset = state->simd_scan ? c_lex_literal_special_find(translated.source, offset, literal_delimiter)
                                      : c_literal_plain_run_end(translated.source, offset);
#else
            offset = c_literal_plain_run_end(translated.source, offset);
#endif
            if (offset >= translated.source.length)
            {
                break;
            }
            character = translated.source.pointer[offset];
            if (character == literal_delimiter)
            {
                offset += 1;
                terminated = true;
                break;
            }
            if (character == '\n')
            {
                break;
            }
            if (character == '\\' && offset + 1 < translated.source.length)
            {
                offset += 2;
            }
            else
            {
                offset += 1;
            }
        }
        CTokenKind kind = literal_delimiter == '\'' ? C_TOKEN_CHARACTER_LITERAL : C_TOKEN_STRING_LITERAL;
        c_token_push(result, translated, start, offset, kind, C_PUNCTUATOR_NONE);
        result->metrics.literal_bytes += offset - start;
        if (!terminated)
        {
            c_diagnostic_push(state, start, literal_delimiter == '\'' ? C_DIAGNOSTIC_UNTERMINATED_CHARACTER_LITERAL : C_DIAGNOSTIC_UNTERMINATED_STRING_LITERAL,
                              literal_delimiter == '\'' ? S8("unterminated character literal") : S8("unterminated string literal"));
        }
        return offset;
    }
    bool ucn = c_ucn_enabled(state->dialect);
    if (c_identifier_start(character) || (ucn && c_ucn_prefix(translated.source, offset)))
    {
        u64 start = offset;
        offset = c_identifier_run_end(translated.source, offset + (character < 0x80 && character != '\\'));
        while (offset < translated.source.length)
        {
            if (ucn && c_ucn_prefix(translated.source, offset))
            {
                offset = c_lex_ucn(state, offset, offset == start);
                offset = c_identifier_run_end(translated.source, offset);
            }
            else if (translated.source.pointer[offset] >= 0x80)
            {
                u64 first_high = offset;
                do
                {
                    offset += 1;
                } while (offset < translated.source.length && c_identifier_continue(translated.source.pointer[offset]));
                c_lex_validate_word_utf8(state, first_high, offset);
            }
            else
            {
                break;
            }
        }
        c_token_push(result, translated, start, offset, C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
        if (BUSTER_UNLIKELY(offset - start >= C_TOKEN_LENGTH_OVERSIZED))
        {
            c_diagnostic_push(state, start, C_DIAGNOSTIC_TOKEN_TOO_LONG, S8("identifier exceeds 65534 bytes"));
        }
        return offset;
    }
    if (c_ascii_digit(character) || (character == '.' && offset + 1 < translated.source.length && c_ascii_digit(translated.source.pointer[offset + 1])))
    {
        u64 start = offset++;
        u8 high_bytes = 0;
        while (offset < translated.source.length)
        {
            character = translated.source.pointer[offset];
            bool exponent_sign = (character == '+' || character == '-') && offset > start &&
                                 (translated.source.pointer[offset - 1] == 'e' || translated.source.pointer[offset - 1] == 'E' ||
                                  translated.source.pointer[offset - 1] == 'p' || translated.source.pointer[offset - 1] == 'P');
            if (ucn && c_ucn_prefix(translated.source, offset))
            {
                offset = c_lex_ucn(state, offset, false);
                continue;
            }
            if (!c_identifier_continue(character) && character != '.' && character != '\'' && !exponent_sign)
            {
                break;
            }
            high_bytes |= (u8)character;
            offset += 1;
        }
        if (BUSTER_UNLIKELY(high_bytes & 0x80))
        {
            c_lex_validate_word_utf8(state, start, offset);
        }
        c_token_push(result, translated, start, offset, C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
        if (BUSTER_UNLIKELY(offset - start >= C_TOKEN_LENGTH_OVERSIZED))
        {
            c_diagnostic_push(state, start, C_DIAGNOSTIC_TOKEN_TOO_LONG, S8("preprocessing number exceeds 65534 bytes"));
        }
        return offset;
    }
    CPunctuator punctuator = C_PUNCTUATOR_NONE;
    u64 punctuator_length = c_punctuator_length(translated.source, offset, &punctuator);
    if (punctuator_length)
    {
        c_token_push(result, translated, offset, offset + punctuator_length, C_TOKEN_PUNCTUATOR, punctuator);
        return offset + punctuator_length;
    }
    c_token_push(result, translated, offset, offset + 1, C_TOKEN_INVALID, C_PUNCTUATOR_NONE);
    c_diagnostic_push(state, offset, C_DIAGNOSTIC_INVALID_CHARACTER,
                      string_format(state->arena, S8("invalid character byte {u32} in C source"), (u32)character));
    return offset + 1;
}

BUSTER_C_INTERNAL void c_lex_scalar(CLexState* state)
{
    u64 length = state->translated.source.length;
    u64 offset = 0;
    while (offset < length)
    {
        offset = c_lex_scan_one(state, offset);
    }
}

#if BUSTER_C_LEX_COMPACT

// Deus-Lex-Machina compaction emitter for the C lexer (see the AGENTS.md SIMD
// lexing/parsing method notes and validark.dev/posts/deus-lex-machina), the
// same architecture the buster tokenizer runs: the translated source is walked
// in token-aligned 64-byte windows; each window classifies once into per-class
// bitmasks; quote, comment and escape spans resolve by mask arithmetic with a
// forward-seeking cursor (escape parity per the simdjson backslash algorithm);
// multi-character punctuators legalize through a bit-channel vpermi2b NFA
// (three per-position tables AND-ed, one channel per punctuator family);
// preprocessing-number extents resolve by carry propagation over a
// continuation mask — all of a window's numbers at once when numbers are its
// only extent class, a count-trailing-ones cursor query otherwise; and every
// complete token then materializes at once — vpcompressb pulls start and end
// positions of an iota vector through the token-boundary masks, a byte
// subtract yields all lengths, and the kind and punctuator vectors compress
// by the same starts mask before masked widening interleaved stores write
// the 12-byte CToken rows sixteen at a time.
//
// Windows always begin at an item boundary, so no lexer state crosses a
// window: the item touching a window's last byte is deferred and rescanned by
// the next window.  The shapes the masks do not model — invalid bytes,
// unterminated literals, unterminated block comments, and any single item
// longer than a window (long comments, long identifiers, long literals) —
// escape to c_lex_scan_one, which is the code the scalar loop runs, so both
// paths agree on every hard case by construction.

BUSTER_CT_CHECK(C_PUNCTUATOR_NONE == 0);
BUSTER_CT_CHECK(C_TOKEN_INVALID == 0);

// Bit channels of the punctuator NFA.  A byte pair (or triple) spells a
// punctuator when the AND of the per-position table lookups is nonzero in some
// channel; the two channels marked "equal-byte" additionally require the first
// two bytes to be identical, which one vpcmpeqb supplies and which is what
// keeps the seven doubled spellings from needing a channel each.
enum
{
    C_LEX_NFA_ASSIGN = 1 << 0,
    C_LEX_NFA_DOUBLE = 1 << 1,
    C_LEX_NFA_ARROW = 1 << 2,
    C_LEX_NFA_LESS_PAIR = 1 << 3,
    C_LEX_NFA_PERCENT_PAIR = 1 << 4,
    C_LEX_NFA_SHIFT_ASSIGN = 1 << 5,
    C_LEX_NFA_ELLIPSIS = 1 << 6,
    C_LEX_NFA_PAIR_CHANNELS = C_LEX_NFA_ASSIGN | C_LEX_NFA_ARROW | C_LEX_NFA_LESS_PAIR | C_LEX_NFA_PERCENT_PAIR,
};

enum
{
    C_LEX_PAIR_TABLE_SIZE = 16,
};

// The four byte classes the window loop used to assemble out of thirteen
// compares and seven ors.  One vpermi2b over the table below answers all of
// them at once, and each falls out as a vptestmb against its own bit; the
// table is authored from the very predicates the scalar lexer calls, so the
// two spellings of "identifier byte" and "horizontal white space" cannot
// drift apart.  It covers 0..127 only: every byte above ASCII is an
// identifier byte by c_identifier_start and joins the word class through the
// sign mask the permute already needs, and no byte above ASCII is in any of
// the other three.
enum
{
    C_LEX_CLASS_WORD = 1 << 0,
    C_LEX_CLASS_WHITE = 1 << 1,
    C_LEX_CLASS_DIGIT = 1 << 2,
    C_LEX_CLASS_EXPONENT = 1 << 3,
};

BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_byte_classes[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_single_punctuators[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_nfa_first[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_nfa_second[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_nfa_third[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_iota[64];
// Two-character spellings, keyed by densely numbered first and second bytes so
// the whole cross product fits one cache line instead of a 64 KB table.  The
// emitter reads the keys and the cross product as vpermi2b tables, so all
// four arrays are vector-aligned.
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_pair_row[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_pair_column[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_pair_punctuators[C_LEX_PAIR_TABLE_SIZE][C_LEX_PAIR_TABLE_SIZE];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_triple_punctuators[128];
BUSTER_C_INTERNAL bool c_lex_compact_tables_built;

// Every spelling table below is derived from c_punctuator_spellings, so a new
// punctuator cannot drift out of the emitter's tables.  The NFA channels
// themselves are hand-assigned; c_test_frontend_lex_punctuator_nfa validates
// them exhaustively against c_punctuator_length.
BUSTER_C_INTERNAL void c_lex_compact_tables_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    for (u32 index = 0; index < 64; index += 1)
    {
        c_lex_iota[index] = (u8)index;
    }
    for (u32 value = 0; value < 128; value += 1)
    {
        char8 character = (char8)value;
        u8 classes = 0;
        if (c_identifier_continue(character))
        {
            classes |= (u8)C_LEX_CLASS_WORD;
        }
        if (c_horizontal_whitespace(character))
        {
            classes |= (u8)C_LEX_CLASS_WHITE;
        }
        if (c_ascii_digit(character))
        {
            classes |= (u8)C_LEX_CLASS_DIGIT;
        }
        // The letters that open a preprocessing number's exponent, in both
        // cases: the emitter used to fold case by oring 0x20 into the whole
        // window for this test and the alphabetic one alone.
        if (character == 'e' || character == 'E' || character == 'p' || character == 'P')
        {
            classes |= (u8)C_LEX_CLASS_EXPONENT;
        }
        c_lex_byte_classes[value] = classes;
    }
    memset(c_lex_pair_row, 0xFF, sizeof(c_lex_pair_row));
    memset(c_lex_pair_column, 0xFF, sizeof(c_lex_pair_column));
    u32 row_count = 0;
    u32 column_count = 0;
    for (u32 index = C_PUNCTUATOR_NONE + 1; index < C_PUNCTUATOR_COUNT; index += 1)
    {
        String8 spelling = c_punctuator_spellings[index];
        BUSTER_CHECK(spelling.length && (u8)spelling.pointer[0] < 128);
        u8 first = (u8)spelling.pointer[0];
        if (spelling.length == 1)
        {
            c_lex_single_punctuators[first] = (u8)index;
        }
        else if (spelling.length == 2)
        {
            u8 second = (u8)spelling.pointer[1];
            BUSTER_CHECK(second < 128);
            if (c_lex_pair_row[first] == 0xFF)
            {
                c_lex_pair_row[first] = (u8)row_count++;
            }
            if (c_lex_pair_column[second] == 0xFF)
            {
                c_lex_pair_column[second] = (u8)column_count++;
            }
            BUSTER_CHECK(row_count <= C_LEX_PAIR_TABLE_SIZE && column_count <= C_LEX_PAIR_TABLE_SIZE);
            c_lex_pair_punctuators[c_lex_pair_row[first]][c_lex_pair_column[second]] = (u8)c_punctuator_canonical((CPunctuator)index);
        }
        else if (spelling.length == 3)
        {
            BUSTER_CHECK(!c_lex_triple_punctuators[first]);
            c_lex_triple_punctuators[first] = (u8)index;
        }
        else
        {
            // The one four-character spelling, %:%:, which the emitter finds
            // as two adjacent %: pairs rather than through a fourth table.
            BUSTER_CHECK(spelling.length == 4 && index == C_PUNCTUATOR_HASH_HASH_DIGRAPH);
        }
    }

    for (const char8* it = "!%&*+-/<=>^|"; *it; it += 1)
    {
        c_lex_nfa_first[(u8)*it] |= C_LEX_NFA_ASSIGN;
    }
    c_lex_nfa_second['='] |= C_LEX_NFA_ASSIGN;

    for (const char8* it = "#&+-<>|"; *it; it += 1)
    {
        c_lex_nfa_first[(u8)*it] |= C_LEX_NFA_DOUBLE;
        c_lex_nfa_second[(u8)*it] |= C_LEX_NFA_DOUBLE;
    }

    c_lex_nfa_first['-'] |= C_LEX_NFA_ARROW;
    c_lex_nfa_first[':'] |= C_LEX_NFA_ARROW;
    c_lex_nfa_second['>'] |= C_LEX_NFA_ARROW;

    c_lex_nfa_first['<'] |= C_LEX_NFA_LESS_PAIR;
    c_lex_nfa_second[':'] |= C_LEX_NFA_LESS_PAIR;
    c_lex_nfa_second['%'] |= C_LEX_NFA_LESS_PAIR;

    c_lex_nfa_first['%'] |= C_LEX_NFA_PERCENT_PAIR;
    c_lex_nfa_second[':'] |= C_LEX_NFA_PERCENT_PAIR;
    c_lex_nfa_second['>'] |= C_LEX_NFA_PERCENT_PAIR;

    c_lex_nfa_first['<'] |= C_LEX_NFA_SHIFT_ASSIGN;
    c_lex_nfa_first['>'] |= C_LEX_NFA_SHIFT_ASSIGN;
    c_lex_nfa_second['<'] |= C_LEX_NFA_SHIFT_ASSIGN;
    c_lex_nfa_second['>'] |= C_LEX_NFA_SHIFT_ASSIGN;
    c_lex_nfa_third['='] |= C_LEX_NFA_SHIFT_ASSIGN;

    c_lex_nfa_first['.'] |= C_LEX_NFA_ELLIPSIS;
    c_lex_nfa_second['.'] |= C_LEX_NFA_ELLIPSIS;
    c_lex_nfa_third['.'] |= C_LEX_NFA_ELLIPSIS;

    c_lex_compact_tables_built = true;
}

BUSTER_C_INLINE BUSTER_INLINE u64 c_lex_mask_range(u64 low, u64 high)
{
    return c_lex_mask_below(high) & ~c_lex_mask_below(low);
}

// Sixteen finished rows: the compressed start, length, kind and punctuator
// bytes widen to u32 lanes, the metadata u32 assembles as
// length | kind<<16 | punctuator<<24, and three masked
// simd512_permute2_u32 (vpermt2d) interleave offsets and metadata into the
// 48 u32 lanes of sixteen 12-byte CToken rows — 192 stored bytes.  The zero
// symbol u32 lanes come from the permutes' own zeroing masks rather than
// from sacrificial zero lanes, which is what
// lets one pass cover sixteen rows where the earlier eight-row form spent
// the offset vector's masked-off upper lanes as its zero source.
BUSTER_C_INLINE BUSTER_INLINE void c_lex_store_rows(CToken* out, CTokenShape* shapes_out, u64 shape_mask, __m512i starts, __m512i lengths,
                                                        __m512i kinds, __m512i punctuators, __m512i shapes, __m512i base, __m512i index_0,
                                                        __m512i index_1, __m512i index_2)
{
    Simd512 start_wide = simd512_widen_u8((Simd512)starts, 0);
    Simd512 length_wide = simd512_widen_u8((Simd512)lengths, 0);
    Simd512 kind_wide = simd512_widen_u8((Simd512)kinds, 0);
    Simd512 punctuator_wide = simd512_widen_u8((Simd512)punctuators, 0);
    Simd512 offsets = simd512_add_u32(start_wide, (Simd512)base);
    Simd512 metadata = simd512_or(length_wide, simd512_or(simd512_shift_left_u32(kind_wide, 16), simd512_shift_left_u32(punctuator_wide, 24)));
    u32* row_lanes = (u32*)out;
    simd512_store(row_lanes, simd512_permute2_u32(0xDB6D, offsets, (Simd512)index_0, metadata));
    simd512_store(row_lanes + 16, simd512_permute2_u32(0x6DB6, offsets, (Simd512)index_1, metadata));
    simd512_store(row_lanes + 32, simd512_permute2_u32(0xB6DB, offsets, (Simd512)index_2, metadata));
    simd512_store_masked(shapes_out, shape_mask, (Simd512)shapes);
}

BUSTER_C_INTERNAL void c_lex_compact(CLexState* state)
{
    CLexResult* result = state->result;
    String8 source = state->translated.source;
    u32 translated_offset = state->translated.translated_offset;

    if (!c_lex_compact_tables_built)
    {
        c_lex_compact_tables_build();
    }

    const __m512i class_low = _mm512_load_si512((const __m512i*)c_lex_byte_classes);
    const __m512i class_high = _mm512_load_si512((const __m512i*)(c_lex_byte_classes + 64));
    const __m512i single_low = _mm512_load_si512((const __m512i*)c_lex_single_punctuators);
    const __m512i single_high = _mm512_load_si512((const __m512i*)(c_lex_single_punctuators + 64));
    const __m512i nfa_first_low = _mm512_load_si512((const __m512i*)c_lex_nfa_first);
    const __m512i nfa_first_high = _mm512_load_si512((const __m512i*)(c_lex_nfa_first + 64));
    const __m512i nfa_second_low = _mm512_load_si512((const __m512i*)c_lex_nfa_second);
    const __m512i nfa_second_high = _mm512_load_si512((const __m512i*)(c_lex_nfa_second + 64));
    const __m512i nfa_third_low = _mm512_load_si512((const __m512i*)c_lex_nfa_third);
    const __m512i nfa_third_high = _mm512_load_si512((const __m512i*)(c_lex_nfa_third + 64));
    const __m512i iota = _mm512_load_si512((const __m512i*)c_lex_iota);
    // The 16-row interleave: dword d of the 48 stored holds row d/3's offset
    // (d%3 == 0), zeroed symbol (1) or metadata (2); offsets sit in the
    // permute's first source, metadata in lanes 16..31 of its second, and
    // the symbol dwords take any index because the store masks zero them.
    const __m512i row_index_0 = _mm512_setr_epi32(0, 0, 16, 1, 0, 17, 2, 0, 18, 3, 0, 19, 4, 0, 20, 5);
    const __m512i row_index_1 = _mm512_setr_epi32(0, 21, 6, 0, 22, 7, 0, 23, 8, 0, 24, 9, 0, 25, 10, 0);
    const __m512i row_index_2 = _mm512_setr_epi32(26, 11, 0, 27, 12, 0, 28, 13, 0, 29, 14, 0, 30, 15, 0, 31);

    u64 offset = 0;
    while (offset < source.length)
    {
        const char8* it = source.pointer + offset;
        u64 remaining = source.length - offset;
        u64 window_length = remaining < 64 ? remaining : 64;
        bool at_end_of_file = window_length == remaining;
        u64 valid;
        __m512i chunk0;
        __m512i chunk1;
        __m512i chunk2;
        if (remaining >= C_LEX_WINDOW_LOADED_BYTES)
        {
            // Every lane of the window and of both lookaheads holds a byte the
            // file owns, so the three loads need no mask and the three lane
            // masks no arithmetic at all — nine instructions of shift and
            // conditional move that 92% of windows were paying to reconstruct
            // an all-ones mask.  The bound is the window plus the two bytes
            // the punctuator NFA and the comment delimiters look ahead by.
            valid = ~(u64)0;
            chunk0 = _mm512_loadu_si512((const void*)it);
            chunk1 = _mm512_loadu_si512((const void*)(it + 1));
            chunk2 = _mm512_loadu_si512((const void*)(it + 2));
        }
        else
        {
            __mmask64 valid_lanes = c_lex_lane_mask(window_length);
            valid = (u64)valid_lanes;
            chunk0 = _mm512_maskz_loadu_epi8(valid_lanes, it);
            chunk1 = _mm512_maskz_loadu_epi8(c_lex_lookahead_mask(remaining, 1), it + 1);
            chunk2 = _mm512_maskz_loadu_epi8(c_lex_lookahead_mask(remaining, 2), it + 2);
        }

        // One masked load, every byte class in lockstep.  The word, white,
        // digit and exponent classes come out of one table lookup and four
        // bit tests; the rest are single bytes, which a compare already
        // answers in one instruction.
        u64 high_byte = (u64)_mm512_movepi8_mask(chunk0);
        __m512i class_vector = _mm512_maskz_permutex2var_epi8((__mmask64)~high_byte, class_low, chunk0, class_high);
        u64 digit = (u64)_mm512_test_epi8_mask(class_vector, _mm512_set1_epi8((char)C_LEX_CLASS_DIGIT));
        u64 line_feed = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('\n'));
        u64 quote = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('"'));
        u64 apostrophe = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('\''));
        u64 backslash = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('\\'));
        u64 slash = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('/'));
        u64 star = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('*'));
        u64 dot = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('.'));
        u64 plus = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('+'));
        u64 minus = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('-'));
        u64 percent = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('%'));
        u64 exponent_letter = (u64)_mm512_test_epi8_mask(class_vector, _mm512_set1_epi8((char)C_LEX_CLASS_EXPONENT));
        u64 slash_next = (u64)_mm512_cmpeq_epi8_mask(chunk1, _mm512_set1_epi8('/'));
        u64 star_next = (u64)_mm512_cmpeq_epi8_mask(chunk1, _mm512_set1_epi8('*'));
        u64 colon_next = (u64)_mm512_cmpeq_epi8_mask(chunk1, _mm512_set1_epi8(':'));
        u64 repeated = (u64)_mm512_cmpeq_epi8_mask(chunk0, chunk1);
        // High bytes still join word extents, but their whole token escapes
        // below for UTF-8 validation before it can reach the batch emitter.
        u64 word = high_byte | (u64)_mm512_test_epi8_mask(class_vector, _mm512_set1_epi8((char)C_LEX_CLASS_WORD));
        u64 white = (u64)_mm512_test_epi8_mask(class_vector, _mm512_set1_epi8((char)C_LEX_CLASS_WHITE));

        // Single-character punctuator ids double as the "byte can start a
        // punctuator" class, since C_PUNCTUATOR_NONE is zero.
        __m512i single_vector = _mm512_maskz_permutex2var_epi8((__mmask64)~high_byte, single_low, chunk0, single_high);
        u64 punctuator_byte = (u64)_mm512_test_epi8_mask(single_vector, single_vector);

        // Bit-channel NFA: three per-position lookups AND together, one
        // channel per punctuator family, plus the equal-byte refinement.
        u64 high_next = (u64)_mm512_movepi8_mask(chunk1);
        u64 high_third = (u64)_mm512_movepi8_mask(chunk2);
        __m512i first_vector = _mm512_maskz_permutex2var_epi8((__mmask64)~high_byte, nfa_first_low, chunk0, nfa_first_high);
        __m512i second_vector = _mm512_maskz_permutex2var_epi8((__mmask64)~high_next, nfa_second_low, chunk1, nfa_second_high);
        __m512i third_vector = _mm512_maskz_permutex2var_epi8((__mmask64)~high_third, nfa_third_low, chunk2, nfa_third_high);
        __m512i pair_vector = _mm512_and_si512(first_vector, second_vector);
        __m512i triple_vector = _mm512_and_si512(pair_vector, third_vector);
        u64 punctuator2 = (u64)_mm512_test_epi8_mask(pair_vector, _mm512_set1_epi8((char)C_LEX_NFA_PAIR_CHANNELS)) |
                          ((u64)_mm512_test_epi8_mask(pair_vector, _mm512_set1_epi8((char)C_LEX_NFA_DOUBLE)) & repeated);
        u64 ellipsis = (u64)_mm512_test_epi8_mask(triple_vector, _mm512_set1_epi8((char)C_LEX_NFA_ELLIPSIS));
        u64 punctuator3 = ellipsis | ((u64)_mm512_test_epi8_mask(triple_vector, _mm512_set1_epi8((char)C_LEX_NFA_SHIFT_ASSIGN)) & repeated);
        u64 percent_colon = percent & colon_next;
        u64 punctuator4 = percent_colon & (percent_colon >> 2);

        // A preprocessing number swallows identifier bytes, dots, digit
        // separators, and the sign of an exponent — the one context-sensitive
        // rule, and one mask shift models it exactly.
        u64 exponent_sign = (plus | minus) & (exponent_letter << 1);
        u64 number_continue = word | dot | apostrophe | exponent_sign;
        u64 number_seed = (digit & ~(word << 1)) | (dot & (digit >> 1));
        u64 line_comment_open = slash & slash_next;
        u64 block_comment_open = slash & star_next;
        u64 block_comment_close = star & slash_next;
        // `...` is the one punctuator that can swallow another item's start:
        // its last dot would otherwise seed the number in `...5`.  Every other
        // spelling is built from bytes no item can begin with, so the cursor
        // below needs no other punctuator.
        u64 other_candidates = (line_comment_open | block_comment_open | quote | apostrophe | ellipsis) & valid;

        // Numbers, comments, literals and ellipses are the only items that can
        // swallow another item's bytes.  A window whose candidates are all
        // number seeds — the dominant class, 93% of cursor iterations in the
        // 2026-08-22T140940Z census — resolves every extent at once by carry
        // propagation: adding each continuation run's first seed to the
        // continuation mask carries through the run, and the flipped bits are
        // exactly that number's span.  The first seed of a run is itself one
        // addition — run starts ripple through the continuation bytes below
        // the first seed and land on it, while later seeds (the digit after
        // `5.` or `1e+`) sit on cleared sum bits.  Mixed windows keep the
        // left-to-right cursor, whose order is what resolves the precedence
        // among the four classes; identifiers, punctuators and whitespace
        // fall out of pure mask arithmetic below either way.
        u64 comment_span = 0;
        u64 comment_starts = 0;
        u64 literal_span = 0;
        u64 number_span = 0;
        u64 number_starts = 0;
        u64 string_starts = 0;
        u64 character_starts = 0;
        u64 defer_at = 64;
        u64 trigger_at = 64;
        u64 extent_candidates = 0;
        if (!other_candidates)
        {
            u64 seeds = number_seed & valid;
            u64 run_starts = number_continue & ~(number_continue << 1);
            number_starts = ((number_continue & ~seeds) + run_starts) & seeds;
            number_span = ((number_continue + number_starts) ^ number_continue) & number_continue;
            // A number reaching the window's last byte may continue into the
            // next window; defer it exactly as the cursor would have.
            if (!at_end_of_file && (number_span >> 63))
            {
                defer_at = (u64)(63 - __builtin_clzll(number_starts));
                number_starts &= ~((u64)1 << defer_at);
                number_span &= c_lex_mask_below(defer_at);
            }
        }
        else
        {
            extent_candidates = (number_seed & valid) | other_candidates;
        }
        u64 cursor = 0;
        for (u64 rest = extent_candidates; rest; rest = extent_candidates & ~c_lex_mask_below(cursor))
        {
            u64 start = (u64)__builtin_ctzll(rest);
            u64 end;
            if ((number_seed >> start) & 1)
            {
                u64 tail = ~(number_continue >> start);
                end = tail ? start + (u64)__builtin_ctzll(tail) : 64;
                if (end >= window_length)
                {
                    if (!at_end_of_file)
                    {
                        defer_at = start;
                        break;
                    }
                    end = window_length;
                }
                number_span |= c_lex_mask_range(start, end);
                number_starts |= (u64)1 << start;
            }
            else if (((line_comment_open | block_comment_open) >> start) & 1)
            {
                if ((line_comment_open >> start) & 1)
                {
                    u64 stop = line_feed & ~c_lex_mask_below(start);
                    if (stop)
                    {
                        // The newline that ends a line comment is its own
                        // token and is not part of the comment.
                        end = (u64)__builtin_ctzll(stop);
                    }
                    else if (at_end_of_file)
                    {
                        end = window_length;
                    }
                    else
                    {
                        defer_at = start;
                        break;
                    }
                }
                else
                {
                    u64 stop = block_comment_close & ~c_lex_mask_below(start + 2);
                    if (!stop)
                    {
                        // Unterminated at the end of the file is a diagnostic;
                        // otherwise the comment continues past this window.
                        if (at_end_of_file)
                        {
                            trigger_at = start;
                        }
                        else
                        {
                            defer_at = start;
                        }
                        break;
                    }
                    end = (u64)__builtin_ctzll(stop) + 2;
                    if (end >= window_length && !at_end_of_file)
                    {
                        defer_at = start;
                        break;
                    }
                }
                comment_span |= c_lex_mask_range(start, end);
                comment_starts |= (u64)1 << start;
            }
            else if ((ellipsis >> start) & 1)
            {
                // Nothing to record: the punctuator scan below finds it in
                // `available` like any other operator.  The cursor is the
                // whole point, so its third dot cannot also seed a number.
                end = start + 3;
            }
            else
            {
                // c_literal_prefix reads u8/u/U/L only at an item start, so a
                // quote preceded by word bytes is prefixed exactly when those
                // bytes are a one- or two-byte run of their own.
                u64 opener = start;
                if (start > cursor && ((word >> (start - 1)) & 1))
                {
                    u64 gaps = ~word & c_lex_mask_range(cursor, start);
                    u64 run_start = gaps ? (u64)(63 - __builtin_clzll(gaps)) + 1 : cursor;
                    u64 run_length = start - run_start;
                    u8 run_first = (u8)it[run_start];
                    if ((run_length == 1 && (run_first == 'u' || run_first == 'U' || run_first == 'L')) ||
                        (run_length == 2 && run_first == 'u' && (u8)it[run_start + 1] == '8'))
                    {
                        opener = run_start;
                    }
                }
                bool is_string = ((quote >> start) & 1) != 0;
                // The body begins after the delimiter, wherever the token did.
                u64 content = start + 1;
                // Escape-run parity, simdjson's backslash algorithm, scoped to
                // the literal body so a backslash before the opener cannot
                // escape the opening delimiter.
                u64 escaped = 0;
                u64 run = backslash & ~c_lex_mask_below(content);
                if (run)
                {
                    u64 run_starts = run & ~(run << 1);
                    u64 even_starts = run_starts & UINT64_C(0x5555555555555555);
                    u64 odd_starts = run_starts & UINT64_C(0xAAAAAAAAAAAAAAAA);
                    u64 even_ends = (run + even_starts) & ~run;
                    u64 odd_ends = (run + odd_starts) & ~run;
                    escaped = (even_ends & UINT64_C(0xAAAAAAAAAAAAAAAA)) | (odd_ends & UINT64_C(0x5555555555555555));
                }
                u64 terminator = (is_string ? quote : apostrophe) & ~escaped;
                u64 stop = (terminator | line_feed) & ~c_lex_mask_below(content);
                if (!stop)
                {
                    if (at_end_of_file)
                    {
                        trigger_at = opener;
                    }
                    else
                    {
                        defer_at = opener;
                    }
                    break;
                }
                u64 stop_at = (u64)__builtin_ctzll(stop);
                if (!((terminator >> stop_at) & 1))
                {
                    // A newline before the closing delimiter: unterminated,
                    // which the scalar path reports.
                    trigger_at = opener;
                    break;
                }
                end = stop_at + 1;
                if (end >= window_length && !at_end_of_file)
                {
                    defer_at = opener;
                    break;
                }
                literal_span |= c_lex_mask_range(opener, end);
                if (is_string)
                {
                    string_starts |= (u64)1 << opener;
                }
                else
                {
                    character_starts |= (u64)1 << opener;
                }
            }
            cursor = end;
        }

        u64 available = valid & ~comment_span & ~literal_span & ~number_span;
        u64 word_available = word & available;
        u64 identifier_starts = word_available & ~(word_available << 1);
        // Newlines are tokens wherever they appear, block comments included.
        u64 newline_starts = line_feed & valid;

        // Maximal munch over the punctuator runs.  A run holding no pair,
        // triple or quad candidate is nothing but single-byte tokens, so the
        // left-greedy walk runs only when a longer spelling opens somewhere
        // in the operator set — and it resolves starts alone: the ids blend
        // in registers below rather than patching an in-memory copy of the
        // singles vector and reloading it through the store queue.
        u64 operators = punctuator_byte & available;
        u64 multi = (punctuator2 | punctuator3 | punctuator4) & operators;
        u64 operator_starts = operators;
        __m512i punctuator_vector = single_vector;
        if (multi)
        {
            operator_starts = operators & ~(operators << 1) & ~(operators >> 1);
            for (u64 clustered = operators & ~operator_starts; clustered;)
            {
                u64 start = (u64)__builtin_ctzll(clustered);
                u64 bit = (u64)1 << start;
                u64 spelling_length = (punctuator4 & bit) ? 4 : (punctuator3 & bit) ? 3 : (punctuator2 & bit) ? 2 : 1;
                operator_starts |= bit;
                clustered &= ~c_lex_mask_below(start + spelling_length);
            }
            // The five blends (2026-08-10b's lead): pair ids come from the
            // same 16x16 cross-product table the scalar walk indexed,
            // addressed by the dense row and column keys of a spelling's two
            // bytes; triples index their first-byte table; the quad is the
            // one %:%: spelling.  Ids land on every candidate lane, start or
            // not, and the starts mask below selects the real ones, so the
            // rest may hold any table byte.  Every pair-candidate lane has
            // assigned keys — the NFA only legalizes spellings the tables
            // hold, which c_test_frontend_lex_differential proves
            // exhaustively — and the keys are at most 15, so the 16-bit
            // shift cannot carry a lane's row into its neighbour.
            const __m512i pair_row_low = _mm512_load_si512((const __m512i*)c_lex_pair_row);
            const __m512i pair_row_high = _mm512_load_si512((const __m512i*)(c_lex_pair_row + 64));
            const __m512i pair_column_low = _mm512_load_si512((const __m512i*)c_lex_pair_column);
            const __m512i pair_column_high = _mm512_load_si512((const __m512i*)(c_lex_pair_column + 64));
            __m512i pair_rows = _mm512_maskz_permutex2var_epi8((__mmask64)punctuator2, pair_row_low, chunk0, pair_row_high);
            __m512i pair_columns = _mm512_maskz_permutex2var_epi8((__mmask64)punctuator2, pair_column_low, chunk1, pair_column_high);
            __m512i pair_index = _mm512_or_si512(_mm512_slli_epi16(pair_rows, 4), pair_columns);
            const __m512i pair_table_0 = _mm512_load_si512((const __m512i*)&c_lex_pair_punctuators[0][0]);
            const __m512i pair_table_1 = _mm512_load_si512((const __m512i*)&c_lex_pair_punctuators[4][0]);
            const __m512i pair_table_2 = _mm512_load_si512((const __m512i*)&c_lex_pair_punctuators[8][0]);
            const __m512i pair_table_3 = _mm512_load_si512((const __m512i*)&c_lex_pair_punctuators[12][0]);
            __m512i pair_ids = _mm512_mask_mov_epi8(_mm512_permutex2var_epi8(pair_table_0, pair_index, pair_table_1),
                                                    _mm512_movepi8_mask(pair_index),
                                                    _mm512_permutex2var_epi8(pair_table_2, pair_index, pair_table_3));
            const __m512i triple_low = _mm512_load_si512((const __m512i*)c_lex_triple_punctuators);
            const __m512i triple_high = _mm512_load_si512((const __m512i*)(c_lex_triple_punctuators + 64));
            __m512i triple_ids = _mm512_maskz_permutex2var_epi8((__mmask64)punctuator3, triple_low, chunk0, triple_high);
            punctuator_vector = _mm512_mask_mov_epi8(punctuator_vector, (__mmask64)punctuator2, pair_ids);
            punctuator_vector = _mm512_mask_mov_epi8(punctuator_vector, (__mmask64)punctuator3, triple_ids);
            punctuator_vector = _mm512_mask_set1_epi8(punctuator_vector, (__mmask64)punctuator4, (char)C_PUNCTUATOR_HASH_HASH);
        }

        // Nothing left over may reach the emitter: a byte that is neither a
        // resolved span, an identifier byte, whitespace, a newline nor a
        // punctuator byte is C_TOKEN_INVALID, whose diagnostic the scalar path
        // formats.
        u64 unclassified = available & ~word & ~white & ~line_feed & ~punctuator_byte;
        // ASCII windows retain the existing uncommon-byte branch. High bytes
        // reuse the sign mask and escape at their whole word's start; only
        // resolved words before a deferred item matter. Comments and literals
        // keep their byte semantics.
        if (BUSTER_UNLIKELY(unclassified | high_byte))
        {
            if (unclassified)
            {
                u64 candidate = (u64)__builtin_ctzll(unclassified);
                trigger_at = BUSTER_MIN(trigger_at, candidate);
            }
            u64 word_high = high_byte & ~comment_span & ~literal_span & c_lex_mask_below(defer_at);
            if (word_high)
            {
                u64 first_high = (u64)__builtin_ctzll(word_high);
                u64 owners = (identifier_starts | number_starts) & c_lex_mask_below(first_high + 1);
                BUSTER_CHECK(owners);
                u64 candidate = (u64)(63 - __builtin_clzll(owners));
                trigger_at = BUSTER_MIN(trigger_at, candidate);
            }
        }

        // A UCN joins the word before its backslash. Escape at that word's
        // start rather than emitting its ASCII prefix as a separate token.
        // Comments and literal payloads retain their existing byte rules.
        if (BUSTER_UNLIKELY(backslash && c_ucn_enabled(state->dialect)))
        {
            u64 ucn_next = (u64)_mm512_cmpeq_epi8_mask(chunk1, _mm512_set1_epi8('u')) |
                           (u64)_mm512_cmpeq_epi8_mask(chunk1, _mm512_set1_epi8('U'));
            u64 names = backslash & ucn_next & ~comment_span & ~literal_span & c_lex_mask_below(defer_at);
            if (names)
            {
                u64 first = (u64)__builtin_ctzll(names);
                u64 candidate = first;
                if (first && ((word | number_span) & (UINT64_C(1) << (first - 1))))
                {
                    u64 owners = (identifier_starts | number_starts) & c_lex_mask_below(first);
                    BUSTER_CHECK(owners);
                    candidate = (u64)(63 - __builtin_clzll(owners));
                }
                trigger_at = BUSTER_MIN(trigger_at, candidate);
            }
        }

        u64 token_starts = identifier_starts | number_starts | string_starts | character_starts | operator_starts | newline_starts;
        // Bytes owned by no token: whitespace outside a literal, and comment
        // text other than the newlines a block comment contains.
        u64 comment_body = comment_span & ~line_feed;
        u64 non_token = (white & valid & ~literal_span) | comment_body;
        u64 token_span = valid & ~non_token;
        // Every position that begins something, so the byte before each one
        // ends the item in front of it.  Whitespace contributes per byte, as
        // the scalar loop consumes it, so a window of pure whitespace still
        // advances a whole window instead of one byte.
        u64 boundary = token_starts | (non_token & ~(non_token << 1)) | (white & valid & ~literal_span);

        u64 bound;
        bool escape_after = false;
        if (trigger_at < 64 && trigger_at <= defer_at)
        {
            bound = trigger_at;
            escape_after = true;
        }
        else if (defer_at < 64)
        {
            bound = defer_at;
        }
        else if (at_end_of_file)
        {
            bound = window_length;
        }
        else
        {
            BUSTER_CHECK(boundary);
            bound = (u64)(63 - __builtin_clzll(boundary));
        }

        u64 emitted = c_lex_mask_below(bound);
        // No span ever straddles the bound: a span's start is an item start,
        // and an incomplete one sets defer_at, so these popcounts are exact.
        result->metrics.blank_bytes += (u64)__builtin_popcountll((white | line_feed) & emitted & ~comment_span & ~literal_span);
        result->metrics.comment_bytes += (u64)__builtin_popcountll(comment_span & emitted);
        result->metrics.literal_bytes += (u64)__builtin_popcountll(literal_span & emitted);
        result->metrics.comments += (u64)__builtin_popcountll(comment_starts & emitted);

        // One finished line per newline in the window, classified by whether
        // any code or comment byte falls in the range since the last one.
        // All lines resolve at once: subtracting a segment's marks from the
        // newline mask borrows through exactly that segment -- marks between
        // two newlines sum below the higher newline's bit, so the borrow
        // clears it and stops -- and the cleared newlines are the lines
        // holding a mark.  Marks above the last newline are the next window's
        // carry and stay out of the subtraction.
        u64 code_bytes = token_span & ~line_feed & emitted;
        u64 comment_bytes = comment_span & emitted;
        u64 newlines = newline_starts & emitted;
        if (newlines)
        {
            u64 last = (u64)(63 - __builtin_clzll(newlines));
            u64 below_last = c_lex_mask_below(last);
            u64 first_line = newlines & (~newlines + 1);
            u64 lines_with_code = newlines & ~(newlines - (code_bytes & below_last));
            u64 lines_with_comment = newlines & ~(newlines - (comment_bytes & below_last & ~newlines));
            // A newline inside a block comment leaves the comment open, so
            // the next line is a comment line too; the carried state does the
            // same for the first line.
            u64 open_comment_newlines = comment_span & newlines & below_last;
            lines_with_comment |= newlines & ~(newlines - (open_comment_newlines << 1));
            if (state->line_has_code)
            {
                lines_with_code |= first_line;
            }
            if (state->line_has_comment)
            {
                lines_with_comment |= first_line;
            }
            u64 line_count = (u64)__builtin_popcountll(newlines);
            result->metrics.translated_lines += line_count;
            result->metrics.code_lines += (u64)__builtin_popcountll(lines_with_code);
            result->metrics.comment_lines += (u64)__builtin_popcountll(lines_with_comment);
            result->metrics.mixed_lines += (u64)__builtin_popcountll(lines_with_code & lines_with_comment);
            result->metrics.blank_lines += line_count - (u64)__builtin_popcountll(lines_with_code | lines_with_comment);
            u64 above_last = ~c_lex_mask_below(last + 1);
            state->line_has_code = (code_bytes & above_last) != 0;
            state->line_has_comment = (u32)((comment_span >> last) & 1) | ((comment_bytes & above_last) != 0);
            state->line_start = offset + last + 1;
        }
        else
        {
            state->line_has_code |= code_bytes != 0;
            state->line_has_comment |= comment_bytes != 0;
        }
        state->newline_tokens += (u64)__builtin_popcountll(newlines);

        if (bound)
        {
            // Each emission bound is a window position or its length (64 max).
            BUSTER_CHECK(bound <= 64);
            // Starts and ends compress through the token-boundary masks, one
            // byte subtract yields every length in the window, and the kind
            // and punctuator vectors ride the same starts mask.
            u64 start_mask = token_starts & emitted;
            // `emitted` is a contiguous low-bit mask. Its highest bit marks
            // the final lane without a variable shift at the 64-byte edge.
            u64 final_lane = emitted & ~(emitted >> 1);
            u64 end_mask = ((boundary >> 1) | final_lane) & token_span & emitted;
            u32 count = (u32)__builtin_popcountll(start_mask);
            BUSTER_CHECK(count == (u32)__builtin_popcountll(end_mask));

            __m512i kind_vector = _mm512_maskz_set1_epi8((__mmask64)newline_starts, (char)C_TOKEN_NEWLINE);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)identifier_starts, (char)C_TOKEN_IDENTIFIER);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)number_starts, (char)C_TOKEN_PREPROCESSING_NUMBER);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)operator_starts, (char)C_TOKEN_PUNCTUATOR);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)string_starts, (char)C_TOKEN_STRING_LITERAL);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)character_starts, (char)C_TOKEN_CHARACTER_LITERAL);

            __m512i start_positions = _mm512_maskz_compress_epi8((__mmask64)start_mask, iota);
            __m512i end_positions = _mm512_maskz_compress_epi8((__mmask64)end_mask, iota);
            __m512i lengths = _mm512_add_epi8(_mm512_sub_epi8(end_positions, start_positions), _mm512_set1_epi8(1));
            __m512i kinds = _mm512_maskz_compress_epi8((__mmask64)start_mask, kind_vector);
            // A non-punctuator start keeps whatever its first byte spells —
            // `.` opening a number, say — so the field is cleared first.
            __m512i punctuator_ids =
                _mm512_maskz_compress_epi8((__mmask64)start_mask, _mm512_maskz_mov_epi8((__mmask64)operator_starts, punctuator_vector));
            __m512i punctuator_shapes = _mm512_or_si512(punctuator_vector, _mm512_set1_epi8((char)C_TOKEN_SHAPE_PUNCTUATOR));
            __m512i shape_vector = _mm512_mask_mov_epi8(kind_vector, (__mmask64)operator_starts, punctuator_shapes);
            __m512i shapes = _mm512_maskz_compress_epi8((__mmask64)start_mask, shape_vector);

            CToken* out = result->tokens + result->token_count;
            CTokenShape* shapes_out = result->token_shapes + result->token_count;
            __m512i base = _mm512_set1_epi32((s32)(u32)((u64)translated_offset + offset));
            // Sixteen rows per pass, tail rows included: the token array
            // carries sixteen slots of slack and the next window overwrites
            // them.  76% of emitting windows carry 15 or fewer tokens (the
            // 2026-08-22T140940Z census), so the mean window is one pass
            // where the 8-row store was two plus a lane rotation.  The first
            // pass is unconditional on purpose — guarding it on a non-empty
            // window measured +2.0 M stage-1 instructions, because a window
            // with no token at all needs 64 bytes of whitespace or comment,
            // which escapes to the scalar scanner long before it reaches
            // here.
            for (u32 written = 0;;)
            {
                u32 shape_count = BUSTER_MIN(count - written, UINT32_C(16));
                c_lex_store_rows(out + written, shapes_out + written, c_lex_mask_below(shape_count), start_positions, lengths, kinds, punctuator_ids,
                                 shapes, base, row_index_0, row_index_1, row_index_2);
                written += 16;
                if (written >= count)
                {
                    break;
                }
                start_positions = _mm512_alignr_epi32(start_positions, start_positions, 4);
                lengths = _mm512_alignr_epi32(lengths, lengths, 4);
                kinds = _mm512_alignr_epi32(kinds, kinds, 4);
                punctuator_ids = _mm512_alignr_epi32(punctuator_ids, punctuator_ids, 4);
                shapes = _mm512_alignr_epi32(shapes, shapes, 4);
            }
            result->token_count += count;
            offset += bound;
            if (!escape_after)
            {
                continue;
            }
        }

        // Either the window holds one unfinished item or a scalar-escape
        // trigger sits at the emission bound: scan exactly that item with the
        // reference scanner and re-enter the window loop after it.
        offset = c_lex_scan_one(state, offset);
    }
}

#endif

#if BUSTER_INCLUDE_TESTS
bool c_test_lex_compact_tables_ready(void)
{
#if BUSTER_C_LEX_COMPACT
    return c_lex_compact_tables_built;
#else
    return true;
#endif
}

// Test seam: the one-instruction lane-prefix mask against its portable
// reference, over the whole domain the emitter forms indices in — every window
// position and every position a punctuator spelling carries past the last of
// them. Counting mismatches keeps the sweep to one test assertion.
u64 c_test_lex_mask_below_mismatches(void)
{
    u64 result = 0;
#if BUSTER_C_LEX_COMPACT
    for (u64 bit_index = 0; bit_index < C_LEX_MASK_BELOW_LIMIT; bit_index += 1)
    {
        result += c_lex_mask_below(bit_index) != c_lex_mask_below_reference(bit_index);
    }
#endif
    return result;
}

// Test seam: reconcile the emitter's hand-assigned NFA channels with the
// spelling table the scalar path scans. For every byte sequence the window
// pipeline can classify, the channels must legalize exactly the spellings
// c_punctuator_length finds, at exactly its length. Counting mismatches keeps
// the exhaustive sweep to one test assertion.
u64 c_test_lex_punctuator_nfa_mismatches(void)
{
#if BUSTER_C_LEX_COMPACT
    if (!c_lex_compact_tables_built)
    {
        c_lex_compact_tables_build();
    }
    if (!c_punctuator_dispatch_built)
    {
        c_punctuator_dispatch_build();
    }
    u64 mismatches = 0;
    char8 probe[4];
    for (u32 first = 0; first < 128; first += 1)
    {
        if (!c_lex_single_punctuators[first])
        {
            continue;
        }
        for (u32 second = 0; second < 256; second += 1)
        {
            for (u32 third = 0; third < 256; third += 1)
            {
                // The fourth byte only ever matters to %:%:, so it sweeps just
                // that prefix instead of multiplying the whole cross product.
                u32 fourth_low = 0;
                u32 fourth_high = 1;
                if (first == '%' && second == ':' && third == '%')
                {
                    fourth_high = 256;
                }
                for (u32 fourth = fourth_low; fourth < fourth_high; fourth += 1)
                {
                    probe[0] = (char8)first;
                    probe[1] = (char8)second;
                    probe[2] = (char8)third;
                    probe[3] = (char8)fourth;
                    // What the emitter's channels decide. Bytes above ASCII
                    // zero their lookups, exactly as the maskz permutes do.
                    u8 first_bits = c_lex_nfa_first[first];
                    u8 second_bits = second < 128 ? c_lex_nfa_second[second] : 0;
                    u8 third_bits = third < 128 ? c_lex_nfa_third[third] : 0;
                    u8 pair = first_bits & second_bits;
                    u8 triple = pair & third_bits;
                    bool repeated = first == second;
                    bool is_four = first == '%' && second == ':' && third == '%' && fourth == ':';
                    bool is_three = (triple & C_LEX_NFA_ELLIPSIS) != 0 || ((triple & C_LEX_NFA_SHIFT_ASSIGN) != 0 && repeated);
                    bool is_two = (pair & C_LEX_NFA_PAIR_CHANNELS) != 0 || ((pair & C_LEX_NFA_DOUBLE) != 0 && repeated);
                    u64 emitted_length = is_four ? 4 : is_three ? 3 : is_two ? 2 : 1;
                    u8 emitted_punctuator = (u8)C_PUNCTUATOR_HASH_HASH;
                    if (emitted_length == 3)
                    {
                        emitted_punctuator = c_lex_triple_punctuators[first];
                    }
                    else if (emitted_length == 2)
                    {
                        u8 row = c_lex_pair_row[first];
                        u8 column = second < 128 ? c_lex_pair_column[second] : 0xFF;
                        emitted_punctuator = row == 0xFF || column == 0xFF ? 0 : c_lex_pair_punctuators[row][column];
                    }
                    else if (emitted_length == 1)
                    {
                        emitted_punctuator = c_lex_single_punctuators[first];
                    }
                    CPunctuator reference = C_PUNCTUATOR_NONE;
                    u64 reference_length = c_punctuator_length((String8){probe, BUSTER_ARRAY_LENGTH(probe)}, 0, &reference);
                    mismatches += emitted_length != reference_length || emitted_punctuator != (u8)reference;
                }
            }
        }
    }
    return mismatches;
#else
    return 0;
#endif
}
#endif

BUSTER_C_INTERNAL CLexResult c_lex_dispatch(Arena* arena, CSpellingSpace* space, String8 source, bool force_scalar, bool trigraphs, CPreprocessDialect dialect)
{
    CLexResult result = {0};
    CSourceAllocationPlan plan;
    if (arena && (!source.length || source.pointer))
    {
        bool source_supported = c_source_allocation_plan(source.length, &plan);
        if (source_supported && space)
        {
            // Each include shares the same u32 spelling offsets. A source
            // may fit by itself while the preceding spellings leave too
            // little representable space for its copy and terminator.
            source_supported = space->used <= UINT32_MAX && plan.translated_capacity <= UINT32_MAX - space->used;
        }
        if (!source_supported)
        {
            // Preserve a small, well-formed EOF stream for preprocessing's
            // frame walk, but never let an unread source appear successful.
            // The empty spelling/checkpoint owns the include diagnostic's
            // map offset without inspecting the caller's source pointer.
            c_source_allocation_plan(0, &plan);
        }
        CTranslatedSource translated = c_translate_source(arena, source_supported ? space : 0, source_supported ? source : (String8){0}, force_scalar, trigraphs, &plan);
        if (!source_supported && space)
        {
            // Reuse the previous spelling's final byte as the empty source's
            // location anchor; a full spelling space cannot allocate even
            // another terminator. No token spelling reads this empty range.
            translated.translated_offset = space->used ? (u32)BUSTER_MIN(space->used - 1, (u64)UINT32_MAX - 1) : 0;
        }
        result.translated_source = translated.source;
        result.spelling_base = space ? space->base : translated.source.pointer;
        result.checkpoints = translated.checkpoints;
        result.checkpoint_offsets = translated.checkpoint_offsets;
        result.checkpoint_pages = translated.checkpoint_pages;
        result.checkpoint_page_count = translated.checkpoint_page_count;
        result.checkpoint_count = translated.checkpoint_count;
        result.translated_offset = translated.translated_offset;
        // One token per byte bounds the stream including the end marker exactly as
        // the scalar loop needs; sixteen more absorb the tail rows of the emitter's
        // full-width interleaved stores, which the next window overwrites.
        u64 token_capacity = source_supported ? translated.source.length + 17 : 1;
        result.tokens = arena_allocate(arena, CToken, token_capacity);
        result.token_shapes = arena_allocate(arena, CTokenShape, token_capacity);
        if (!source_supported)
        {
            c_token_push(&result, translated, 0, 0, C_TOKEN_END_OF_FILE, C_PUNCTUATOR_NONE);
            result.diagnostics = arena_allocate(arena, CDiagnostic, 1);
            result.diagnostic_count = 1;
            IR_DIAGNOSTIC_CENSUS_RECORD(C_DIAGNOSTICS_RECORDED, 1);
            C_DIAGNOSTIC_RESERVATION_CENSUS(LEX, 1);
            result.diagnostics[0] = (CDiagnostic){
                .message = source.length > C_SOURCE_MAXIMUM_LENGTH ? S8("C source exceeds the 4294967293-byte translation limit")
                                                                   : S8("C source exceeds the remaining 32-bit spelling-offset space"),
                .location = c_lex_local_location(&result, 0),
                .kind = C_DIAGNOSTIC_SOURCE_TOO_LARGE,
                .severity = C_DIAGNOSTIC_ERROR,
            };
        }
        else if (translated.source.length <= UINT64_MAX / sizeof(CDiagnostic) - 1)
        {
            u64 diagnostic_bytes = (translated.source.length + 1) * sizeof(CDiagnostic);
            if (diagnostic_bytes <= (UINT64_MAX - BUSTER_MB(1)) / 2)
            {
                // Diagnostic storage waits for the first diagnostic; see
                // c_lex_diagnostic_storage_begin.
                {
                    CLexState state = {
                        .result = &result,
                        .translated = translated,
                        .dialect = dialect,
                        .arena = arena,
                        .diagnostic_reserve_size = diagnostic_bytes * 2 + BUSTER_MB(1),
                        .maximum_diagnostic_count = translated.source.length + 1,
                    };
                    // Source measurement rides the branches the lexer already takes; see
                    // CSourceMetrics.
                    result.metrics.files = 1;
                    result.metrics.bytes = source.length;
                    result.metrics.translated_bytes = translated.source.length;
                    result.metrics.lines = translated.raw_lines;
#if BUSTER_C_LEX_COMPACT
                    if (force_scalar)
                    {
                        c_lex_scalar(&state);
                    }
                    else
                    {
                        state.simd_scan = 1;
                        c_lex_compact(&state);
                    }
#else
                    BUSTER_UNUSED(force_scalar);
                    c_lex_scalar(&state);
#endif
                    // A file whose last line has no terminating newline still ends a line.
                    if (translated.source.length > state.line_start)
                    {
                        c_source_metrics_line(&result.metrics, state.line_has_code, state.line_has_comment);
                    }
                    result.metrics.spliced_lines = result.metrics.lines - result.metrics.translated_lines;
                    result.metrics.tokens = result.token_count - state.newline_tokens;
                    c_token_push(&result, translated, translated.source.length, translated.source.length, C_TOKEN_END_OF_FILE, C_PUNCTUATOR_NONE);
                    C_CENSUS_RECORD(LEX_CALLS, 1);
                    C_CENSUS_RECORD(LEX_INPUT_BYTES, translated.source.length);
                    C_CENSUS_RECORD(LEX_TOKEN_ROWS, result.token_count);
                    C_CENSUS_RECORD(LEX_TOKEN_ROW_BYTES, result.token_count * (sizeof(CToken) + sizeof(CTokenShape)));
                    C_CENSUS_RECORD(LEX_RESERVED_ROW_BYTES, token_capacity * (sizeof(CToken) + sizeof(CTokenShape)));
                    CDiagnostic* diagnostics = arena_allocate(arena, CDiagnostic, result.diagnostic_count);
                    if (result.diagnostic_count)
                    {
                        memcpy(diagnostics, result.diagnostics, sizeof(*diagnostics) * result.diagnostic_count);
                    }
                    result.diagnostics = diagnostics;
                    c_lex_diagnostic_storage_end(&state);
                }
            }
        }
    }

    return result;
}

BUSTER_C_INTERNAL CLexResult c_lex_space(Arena* arena, CSpellingSpace* space, String8 source, bool trigraphs, CPreprocessDialect dialect)
{
    return c_lex_dispatch(arena, space, source, false, trigraphs, dialect);
}

// Bounded process-local raw lexical templates. Entries are captured before
// symbol interning; no path, macro, target, source-map or IR identity is retained.
// Every import owns its copies, so eviction cannot invalidate a compilation.
enum { C_SOURCE_CACHE_ENTRY_LIMIT = 64 };
typedef struct CSourceCacheEntry CSourceCacheEntry;
struct CSourceCacheEntry
{
    String8 source;
    // Phase-one replacement and dialect-owned identifier decoding change the
    // lexical template, so they are part of the key beside the raw bytes.
    bool trigraphs;
    CPreprocessDialect dialect;
    CLexResult lex;
};

struct CSourceCache
{
    Arena* storage;
    u64 start;
    CSourceCacheStats stats;
    CSourceCacheEntry entries[C_SOURCE_CACHE_ENTRY_LIMIT];
};

CSourceCache* c_source_cache_create(Arena* owner, u64 byte_limit)
{
    CSourceCache* result = 0;
    u64 metadata_position;
    bool fits = owner && owner->position >= arena_minimum_position && owner->position <= owner->reserved_size &&
                align_forward_checked(owner->position, BUSTER_ALIGN_OF(CSourceCache), &metadata_position) &&
                metadata_position <= owner->reserved_size && sizeof(CSourceCache) <= owner->reserved_size - metadata_position;
    if (fits && byte_limit && byte_limit <= BUSTER_MB(64))
    {
        u64 owner_start = owner->position;
        CSourceCache* metadata = arena_allocate_zeroed(owner, CSourceCache, 1);
        // Commit at creation: a failed optional reservation returns null;
        // subsequent admitted captures cannot require a failing growth commit.
        u64 reservation = byte_limit + BUSTER_KB(64);
        Arena* storage = arena_create((ArenaCreation){
            .reserved_size = reservation,
            .initial_size = reservation,
            .granularity = BUSTER_KB(64),
            .flags = {.no_pool = 1},
        });
        if (storage)
        {
            result = metadata;
            result->storage = storage;
            result->start = storage->position;
            result->stats.byte_limit = byte_limit;
        }
        else
        {
            arena_release_to_position(owner, owner_start);
        }
    }
    return result;
}

void c_source_cache_clear(CSourceCache* cache)
{
    if (cache && cache->storage)
    {
        cache->stats.entry_count = 0;
        cache->stats.retained_bytes = 0;
        cache->stats.resets += 1;
        arena_release_to_position(cache->storage, cache->start);
    }
}

void c_source_cache_destroy(CSourceCache* cache)
{
    if (cache && cache->storage)
    {
        c_source_cache_clear(cache);
        arena_destroy(cache->storage, 1);
        cache->storage = 0;
    }
}

CSourceCacheStats c_source_cache_stats(CSourceCache const* cache)
{
    CSourceCacheStats result = {0};
    if (cache)
    {
        result = cache->stats;
    }
    return result;
}

BUSTER_C_INTERNAL CLexResult c_source_cache_copy(Arena* arena, CSpellingSpace* space, CLexResult const* source)
{
    CLexResult result = *source;
    // Translation shrinks its raw-size reservation to this final compact size.
    u64 size = source->translated_source.length + 1;
    char8* text = space ? c_space_allocate(space, size) : arena_allocate(arena, char8, size);
    memcpy(text, source->translated_source.pointer, size);
    result.translated_source.pointer = text;
    result.translated_offset = space ? c_space_offset(space, text) : 0;
    result.spelling_base = space ? space->base : text;
    result.tokens = arena_allocate(arena, CToken, source->token_count);
    result.token_shapes = arena_allocate(arena, CTokenShape, source->token_count);
    for (u64 index = 0; index < source->token_count; index += 1)
    {
        CToken token = source->tokens[index];
        token.offset = token.offset - source->translated_offset + result.translated_offset;
        token.symbol = 0;
        result.tokens[index] = token;
    }
    memcpy(result.token_shapes, source->token_shapes, source->token_count * sizeof(*result.token_shapes));
    result.checkpoints = arena_allocate(arena, IrSourceCheckpoint, source->checkpoint_count);
    result.checkpoint_offsets = arena_allocate(arena, u32, source->checkpoint_count);
    result.checkpoint_pages = arena_allocate(arena, u32, source->checkpoint_page_count);
    memcpy(result.checkpoints, source->checkpoints, source->checkpoint_count * sizeof(*result.checkpoints));
    memcpy(result.checkpoint_offsets, source->checkpoint_offsets, source->checkpoint_count * sizeof(*result.checkpoint_offsets));
    memcpy(result.checkpoint_pages, source->checkpoint_pages, source->checkpoint_page_count * sizeof(*result.checkpoint_pages));
    result.diagnostics = 0;
    result.diagnostic_count = 0;
    result.location_cursor = 0;
    return result;
}

BUSTER_C_INTERNAL CLexResult c_source_cache_lex(Arena* arena, CSpellingSpace* space, String8 source, bool trigraphs, CPreprocessDialect dialect, CSourceCache* cache)
{
    CLexResult result;
    CSourceAllocationPlan plan;
    TemporalArena snapshot_scope = {0};
    bool eligible = cache && cache->storage && arena && space && (!source.length || source.pointer) &&
                    c_source_allocation_plan(source.length, &plan) &&
                    space->used <= UINT32_MAX && plan.translated_capacity <= UINT32_MAX - space->used &&
                    source.length <= cache->stats.byte_limit;
    if (eligible)
    {
        Arena* conflicts[] = {arena, cache->storage};
        snapshot_scope = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
        eligible = snapshot_scope.arena && snapshot_scope.arena->position <= snapshot_scope.arena->reserved_size &&
                   source.length <= snapshot_scope.arena->reserved_size - snapshot_scope.arena->position;
    }
    if (eligible)
    {
        // One owned byte snapshot feeds lookup, cold lexing and capture alike.
        // A mutable file mapping must never be compared and then lexed separately.
        char8* snapshot = arena_allocate(snapshot_scope.arena, char8, source.length);
        if (source.length)
        {
            memcpy(snapshot, source.pointer, source.length);
        }
        String8 captured = {.pointer = snapshot, .length = source.length};
        CSourceCacheEntry const* found = 0;
        for (u32 index = 0; index < cache->stats.entry_count && !found; index += 1)
        {
            CSourceCacheEntry const* entry = &cache->entries[index];
            // Sixty-four entries bound the scan. No hash collision or metadata
            // shortcut can admit a hit: equality always reads the captured bytes.
            if (entry->trigraphs == trigraphs && entry->dialect == dialect && string_equal(captured, entry->source))
            {
                found = entry;
            }
        }
        if (found)
        {
            result = c_source_cache_copy(arena, space, &found->lex);
            cache->stats.hits += 1;
            cache->stats.reused_bytes += source.length;
            cache->stats.reused_tokens += result.token_count;
        }
        else
        {
            cache->stats.misses += 1;
            result = c_lex_space(arena, space, captured, trigraphs, dialect);
            // Source bounds make every product and sum representable in u64.
            // Seven allocations below each need less than eight padding bytes.
            u64 needed = source.length + result.translated_source.length + 1 +
                         result.token_count * (sizeof(CToken) + sizeof(CTokenShape)) +
                         (u64)result.checkpoint_count * (sizeof(IrSourceCheckpoint) + sizeof(u32)) +
                         (u64)result.checkpoint_page_count * sizeof(u32) + 64;
            if (!result.diagnostic_count && result.token_count && needed <= cache->stats.byte_limit)
            {
                if (cache->stats.entry_count == C_SOURCE_CACHE_ENTRY_LIMIT ||
                    needed > cache->stats.byte_limit - cache->stats.retained_bytes)
                {
                    c_source_cache_clear(cache);
                }
                CSourceCacheEntry* entry = &cache->entries[cache->stats.entry_count];
                char8* raw = arena_allocate(cache->storage, char8, captured.length);
                if (captured.length)
                {
                    memcpy(raw, captured.pointer, captured.length);
                }
                *entry = (CSourceCacheEntry){
                    .source = {.pointer = raw, .length = captured.length},
                    .trigraphs = trigraphs,
                    .dialect = dialect,
                    .lex = c_source_cache_copy(cache->storage, 0, &result),
                };
                cache->stats.entry_count += 1;
                cache->stats.retained_bytes = cache->storage->position - cache->start;
            }
            else
            {
                cache->stats.bypasses += 1;
            }
        }
    }
    else
    {
        if (cache && cache->storage)
        {
            cache->stats.bypasses += 1;
        }
        result = c_lex_space(arena, space, source, trigraphs, dialect);
    }
    if (snapshot_scope.arena)
    {
        scratch_end(snapshot_scope);
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
CLexResult c_test_lex_dialect(Arena* arena, String8 source, CPreprocessDialect dialect, bool force_scalar)
{
    return c_lex_dispatch(arena, 0, source, force_scalar, c_translate_trigraphs_enabled(dialect), dialect);
}

CLexResult c_test_lex_include_source(Arena* arena, String8 source)
{
    CSpellingSpace space = c_space_local(arena, 1024);
    return c_lex_space(arena, &space, source, false, C_PREPROCESS_DIALECT_GNU17);
}
#endif

CLexResult c_lex(Arena* arena, String8 source)
{
    return c_lex_dispatch(arena, 0, source, false, false, C_PREPROCESS_DIALECT_GNU17);
}

// The scalar reference loop, whatever the host: the differential gate asserts
// the dispatched lexer agrees with it byte for byte.
CLexResult c_lex_reference(Arena* arena, String8 source)
{
    return c_lex_dispatch(arena, 0, source, true, false, C_PREPROCESS_DIALECT_GNU17);
}

typedef struct CMacro CMacro;
typedef struct CMacroDefinition CMacroDefinition;
struct CMacroDefinition
{
    CToken* replacement;
    // One byte per replacement token: was it written after white space? A
    // macro is expanded far more often than it is defined, and `#` needs
    // this for every argument substituted into the list, so it is derived
    // once here (c_macro_replacement_spaces) instead of on every expansion.
    // Null on the builtin marker definitions, whose spellings are
    // synthesized one at a time and read as spaced; index 0 is unused,
    // because the first replacement token takes the invocation's spacing.
    u8* replacement_space;
    String8* parameters;
    // One word per replacement token: the index of the parameter it names,
    // or C_MACRO_PARAMETER_NONE. Computed once at definition (c_macro_define)
    // so substitution reads a word per token instead of comparing the
    // token's spelling against every parameter name on every expansion.
    // Null when the replacement list is empty.
    u32* parameter_index;
    // Ordinary substitution uses per parameter, excluding stringization and
    // either side of a paste. Zero means argument prescan is unnecessary;
    // raw tokens remain available to # and ##. In a paste/stringize-free
    // definition every use is ordinary, so this also retains the exact
    // replacement-capacity dot product. Null when there are no parameters.
    u32* parameter_expand_count;
    // Variadic definitions only: a second parameter_count entries after
    // parameter_expand_count holding the ordinary uses outside `__VA_OPT__`
    // content. A parameter used only inside content is prescanned only once the
    // variable argument shows that the content stands (C23 6.10.5.2 skips an
    // absent content before substitution), so omitted content never demands
    // its fixed arguments.
    // Push/pop restores a definition generation, including its active rescan
    // ownership. A name can have several generations on the task stack.
    u32 generation;
    u32 replacement_count;
    // The replacement tokens that name no parameter: the fixed half of that
    // capacity.
    u32 plain_count;
    u32 parameter_count;
    bool defined;
    bool function_like;
    bool variadic;
    bool pragma_like;
    bool header_query;
    bool target_os_query;
    // Compiler-internal helper macros (__has_attribute and its siblings,
    // _Pragma) that `-dM` does not list, like the dynamic builtins.
    bool dump_hidden;
    // Does the list hold a `##`, and a `#` that stringifies a parameter?
    // 78,3% of the expansions of a unity build have neither and 99,97% have
    // no `#` at all, and that is the whole difference between substituting
    // the list in one pass into its final array and staging it in a wider
    // row for a paste pass with nothing to do.
    bool has_paste;
    bool has_stringify;
    // A variadic definition writing C23 `__VA_OPT__`: it stages like a paste
    // and prescans its variable argument to decide whether the content
    // stands.
    bool has_va_opt;
    // Dynamic builtins belong to the saved definition just like replacement
    // tokens, so push/pop restores their synthesized behavior too.
    u8 builtin;
};

#define C_MACRO_PARAMETER_NONE UINT32_MAX

typedef enum CMacroBuiltin
{
    C_MACRO_BUILTIN_NONE,
    C_MACRO_BUILTIN_LINE,
    C_MACRO_BUILTIN_FILE,
    C_MACRO_BUILTIN_COUNTER,
    C_MACRO_BUILTIN_INCLUDE_LEVEL,
    C_MACRO_BUILTIN_BASE_FILE,
    C_MACRO_BUILTIN_FILE_NAME,
} CMacroBuiltin;

struct CMacro
{
    CMacro* next;
    // Head-of-list state: the macro named by each interned symbol id, or
    // null. A symbol id is a dense small integer the identifier token
    // already carries, so the hot question of the line loop and the
    // expansion rescan — does this identifier name a macro at all? — is one
    // indexed load with no hashing and no chain, and the ~95% of identifiers
    // that name nothing never touch a CMacro record. Sized to the symbol
    // table's id capacity when built and regrown from the list on the rare
    // definition whose id lies past it (c_macro_index_rebuild).
    CMacro** by_symbol;
    String8 name;
    u32 symbol;
    CMacroDefinition definition;
    u32 by_symbol_capacity;
    u32 next_generation;
    bool disabled;
    // Head-of-list state for lazy builtin macros: the main preprocess loop
    // stores the current frame and token offset here each iteration (two
    // stores, no location recovery), and dynamic values are read on expansion.
    u32 builtin_line;
    struct CPreprocessSourceFrame* builtin_frame;
    u32 builtin_token_offset;
    String8 builtin_path;
    // Per-translation-unit state: __COUNTER__ advances once per expansion,
    // while __BASE_FILE__ retains the root path independently of #line.
    u32 builtin_counter;
    String8 builtin_base_path;
};

typedef struct CMacroPushMacro CMacroPushMacro;
struct CMacroPushMacro
{
    CMacroPushMacro* previous;
    CMacro* macro;
    String8 name;
    CMacroDefinition definition;
};

BUSTER_C_SHARED u64 c_macro_name_hash(String8 name)
{
    C_CENSUS_PHASE_RECORD(HASH_CALLS, 1);
    C_CENSUS_PHASE_RECORD(HASH_BYTES, name.length);
    u64 hash = 1469598103934665603ull;
    for (u64 index = 0; index < name.length; index += 1)
    {
        hash ^= name.pointer[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

// Rebuild the symbol index over every macro in the list at `capacity`
// entries; the caller guarantees every defined symbol id lies below it.
BUSTER_C_INTERNAL void c_macro_index_rebuild(Arena* arena, CMacro* first, u32 capacity)
{
    BUSTER_VALIDATE(first && capacity);
    CMacro** by_symbol = arena_allocate(arena, CMacro*, capacity);
    memset(by_symbol, 0, sizeof(*by_symbol) * capacity);
    for (CMacro* macro = first; macro; macro = macro->next)
    {
        BUSTER_VALIDATE(macro->symbol < capacity);
        by_symbol[macro->symbol] = macro;
    }
    first->by_symbol = by_symbol;
    first->by_symbol_capacity = capacity;
}

// Identifier interning: the preprocessor assigns every identifier token a
// u32 symbol id (stored in token.symbol) so downstream consumers compare
// integers instead of re-hashing and re-comparing spellings. The well-known
// names take the ids below C_SYMBOL_WELL_KNOWN_COUNT, and the names in the
// table below are interned next, in array order, so their ids are the
// compile-time-known range [C_SYMBOL_WELL_KNOWN_COUNT,
// C_SYMBOL_WELL_KNOWN_COUNT + C_SYMBOL_PREDEFINED_COUNT) and one dense byte
// table classifies them. Tokens that never pass through the intern pass
// (pasted, synthesized, or test-built tokens) keep symbol 0 and every
// consumer falls back to the spelling ladder, so a missed path costs speed,
// never correctness.
typedef struct CSymbolPredefined CSymbolPredefined;
struct CSymbolPredefined
{
    String8 name;
    u8 builtin;
};

BUSTER_C_INTERNAL CSymbolPredefined const c_symbol_predefined[] = {
    { S8_INITIALIZER("__builtin_expect"), C_SYMBOL_BUILTIN_EXPECT },
    { S8_INITIALIZER("__builtin_expect_with_probability"), C_SYMBOL_BUILTIN_EXPECT },
    { S8_INITIALIZER("__builtin_constant_p"), C_SYMBOL_BUILTIN_CONSTANT_P },
    { S8_INITIALIZER("__builtin_choose_expr"), C_SYMBOL_BUILTIN_CHOOSE_EXPR },
    { S8_INITIALIZER("__builtin_types_compatible_p"), C_SYMBOL_BUILTIN_TYPES_COMPATIBLE_P },
    { S8_INITIALIZER("__builtin_object_size"), C_SYMBOL_BUILTIN_OBJECT_SIZE },
    { S8_INITIALIZER("__builtin_assume_aligned"), C_SYMBOL_BUILTIN_ASSUME_ALIGNED },
    { S8_INITIALIZER("__builtin_debugtrap"), C_SYMBOL_BUILTIN_DEBUGTRAP },
    { S8_INITIALIZER("__builtin_trap"), C_SYMBOL_BUILTIN_DEBUGTRAP },
    // Clang's emmintrin.h carries only `void _mm_pause(void);` -- the
    // definition lives in the compiler -- so a dialect that consumes clang
    // headers must know the name or every SSE2 spin loop leaves an
    // unresolved symbol behind.
    { S8_INITIALIZER("_mm_pause"), C_SYMBOL_BUILTIN_SPIN_PAUSE },
    { S8_INITIALIZER("__builtin_ia32_pause"), C_SYMBOL_BUILTIN_SPIN_PAUSE },
    // Clang's SSE2 headers lower scalar-count vector shifts through these
    // compiler-owned spellings. Their implementation is canonical vector
    // IR and is therefore independent of the selected native backend.
    { S8_INITIALIZER("__builtin_ia32_pslldi128"), C_SYMBOL_BUILTIN_SSE2_IMMEDIATE_SHIFT },
    { S8_INITIALIZER("__builtin_ia32_psllqi128"), C_SYMBOL_BUILTIN_SSE2_IMMEDIATE_SHIFT },
    { S8_INITIALIZER("__builtin_ia32_psradi128"), C_SYMBOL_BUILTIN_SSE2_IMMEDIATE_SHIFT },
    { S8_INITIALIZER("__builtin_ia32_psrldi128"), C_SYMBOL_BUILTIN_SSE2_IMMEDIATE_SHIFT },
    { S8_INITIALIZER("__builtin_ia32_psrlqi128"), C_SYMBOL_BUILTIN_SSE2_IMMEDIATE_SHIFT },
    { S8_INITIALIZER("__builtin_unreachable"), C_SYMBOL_BUILTIN_UNREACHABLE },
    { S8_INITIALIZER("__builtin_frame_address"), C_SYMBOL_BUILTIN_FRAME_ADDRESS },
    { S8_INITIALIZER("__builtin_return_address"), C_SYMBOL_BUILTIN_RETURN_ADDRESS },
    { S8_INITIALIZER("__builtin_alloca"), C_SYMBOL_BUILTIN_ALLOCA },
    { S8_INITIALIZER("__builtin_complex"), C_SYMBOL_BUILTIN_COMPLEX },
    { S8_INITIALIZER("__builtin_strlen"), C_SYMBOL_BUILTIN_STRLEN },
    { S8_INITIALIZER("__builtin___clear_cache"), C_SYMBOL_BUILTIN_CLEAR_CACHE },
    { S8_INITIALIZER("__builtin_prefetch"), C_SYMBOL_BUILTIN_PREFETCH },
    { S8_INITIALIZER("__builtin_va_arg"), C_SYMBOL_BUILTIN_VA_ARG },
    { S8_INITIALIZER("__builtin_va_start"), C_SYMBOL_BUILTIN_VA_START },
    { S8_INITIALIZER("__va_start"), C_SYMBOL_BUILTIN_VA_START },
    { S8_INITIALIZER("__builtin_c23_va_start"), C_SYMBOL_BUILTIN_VA_START_C23 },
    { S8_INITIALIZER("__builtin_va_copy"), C_SYMBOL_BUILTIN_VA_COPY },
    { S8_INITIALIZER("__builtin_va_end"), C_SYMBOL_BUILTIN_VA_END },
    { S8_INITIALIZER("_Generic"), C_SYMBOL_BUILTIN_GENERIC },
    { S8_INITIALIZER("__c11_atomic_load"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_store"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_init"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_add"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_sub"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_and"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_or"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_xor"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_exchange"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_compare_exchange_strong"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_compare_exchange_weak"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_is_lock_free"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_thread_fence"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_signal_fence"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_synchronize"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_fetch_and_nand"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_nand_and_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_fetch_and_add"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_fetch_and_sub"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_fetch_and_or"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_fetch_and_and"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_fetch_and_xor"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_add_and_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_sub_and_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_or_and_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_and_and_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_xor_and_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_bool_compare_and_swap"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_val_compare_and_swap"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_lock_test_and_set"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__sync_lock_release"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_load"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_store"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_exchange"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_compare_exchange"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_load_n"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_store_n"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_exchange_n"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_fetch_add"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_fetch_sub"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_fetch_and"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_fetch_or"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_fetch_xor"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_fetch_nand"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_add_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_sub_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_and_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_or_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_xor_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_nand_fetch"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_compare_exchange_n"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_thread_fence"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_signal_fence"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_is_lock_free"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_always_lock_free"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_test_and_set"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__atomic_clear"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__builtin_floorf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_floor"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_ceilf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_ceil"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_sqrtf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_sqrt"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_powf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_pow"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmodf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmod"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_cosf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_cos"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_acosf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_acos"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fabsf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fabs"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fabsl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_copysignf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_copysign"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_copysignl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmaxf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmax"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmaxl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fminf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmin"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fminl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_powif"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_powi"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_powil"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_roundf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_round"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_truncf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_trunc"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_truncl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_rintf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_rint"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_rintl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_nearbyintf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_nearbyint"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_nearbyintl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmaf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fma"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmal"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_ldexpf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_ldexp"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_ldexpl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_lroundf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_lround"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_lroundl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_llroundf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_llround"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_llroundl"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_inf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_inff"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_nanf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_nan"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_huge_val"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isnanf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isnan"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isinf_sign"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isinf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isinff"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isfinite"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isnormal"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fpclassify"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isgreater"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isgreaterequal"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isless"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_islessequal"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_islessgreater"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_isunordered"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_signbit"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_signbitf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_signbitl"), C_SYMBOL_BUILTIN_MATH },
    // The block-memory family. Clang declares these without <string.h>, so
    // freestanding sources spell their copies this way once __clang__ is
    // defined; lowering routes them to the ordinary library call.
    { S8_INITIALIZER("__builtin_memcpy"), C_SYMBOL_BUILTIN_MEMORY },
    { S8_INITIALIZER("__builtin_memmove"), C_SYMBOL_BUILTIN_MEMORY },
    { S8_INITIALIZER("__builtin_memset"), C_SYMBOL_BUILTIN_MEMORY },
    { S8_INITIALIZER("__builtin_memcmp"), C_SYMBOL_BUILTIN_MEMORY },
    { S8_INITIALIZER("__builtin___memcpy_chk"), C_SYMBOL_BUILTIN_MEMORY },
    { S8_INITIALIZER("__builtin___memmove_chk"), C_SYMBOL_BUILTIN_MEMORY },
    { S8_INITIALIZER("__builtin___memset_chk"), C_SYMBOL_BUILTIN_MEMORY },
    // The string forms GCC documents among its library builtins; like the
    // memory family they lower to the ordinary libc call.
    { S8_INITIALIZER("__builtin_strcmp"), C_SYMBOL_BUILTIN_MEMORY },
    { S8_INITIALIZER("__builtin_strcpy"), C_SYMBOL_BUILTIN_MEMORY },
    { S8_INITIALIZER("__builtin_strchr"), C_SYMBOL_BUILTIN_MEMORY },
    { S8_INITIALIZER("__builtin_clz"), C_SYMBOL_BUILTIN_COUNT_LEADING_ZEROS },
    // The `l` spellings take unsigned long, which is target-dependent (LP64
    // versus LLP64). The shared signature policy below selects their operand
    // width before the count operation; all these spellings return int.
    { S8_INITIALIZER("__builtin_clzl"), C_SYMBOL_BUILTIN_COUNT_LEADING_ZEROS },
    { S8_INITIALIZER("__builtin_clzll"), C_SYMBOL_BUILTIN_COUNT_LEADING_ZEROS },
    // clrsb counts the bits after the sign bit that equal it; the operand is
    // signed and is rewritten so the count is a clz of a nonzero value.
    { S8_INITIALIZER("__builtin_clrsb"), C_SYMBOL_BUILTIN_COUNT_LEADING_REDUNDANT_SIGN_BITS },
    { S8_INITIALIZER("__builtin_clrsbl"), C_SYMBOL_BUILTIN_COUNT_LEADING_REDUNDANT_SIGN_BITS },
    { S8_INITIALIZER("__builtin_clrsbll"), C_SYMBOL_BUILTIN_COUNT_LEADING_REDUNDANT_SIGN_BITS },
    { S8_INITIALIZER("__builtin_ctz"), C_SYMBOL_BUILTIN_COUNT_TRAILING_ZEROS },
    { S8_INITIALIZER("__builtin_ctzl"), C_SYMBOL_BUILTIN_COUNT_TRAILING_ZEROS },
    { S8_INITIALIZER("__builtin_ctzll"), C_SYMBOL_BUILTIN_COUNT_TRAILING_ZEROS },
    // abs/labs/llabs take and return the signed type their suffix names, so
    // unlike the count family the C result type is not int.
    { S8_INITIALIZER("__builtin_abs"), C_SYMBOL_BUILTIN_ABSOLUTE_VALUE },
    { S8_INITIALIZER("__builtin_labs"), C_SYMBOL_BUILTIN_ABSOLUTE_VALUE },
    { S8_INITIALIZER("__builtin_llabs"), C_SYMBOL_BUILTIN_ABSOLUTE_VALUE },
    { S8_INITIALIZER("__builtin_ffs"), C_SYMBOL_BUILTIN_FIND_FIRST_SET },
    { S8_INITIALIZER("__builtin_ffsl"), C_SYMBOL_BUILTIN_FIND_FIRST_SET },
    { S8_INITIALIZER("__builtin_ffsll"), C_SYMBOL_BUILTIN_FIND_FIRST_SET },
    // The typed checked-arithmetic forms: the spelling fixes the operand
    // type, the third argument points at the wrapped result, and the call
    // answers whether the exact result did not fit.
    { S8_INITIALIZER("__builtin_sadd_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_saddl_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_saddll_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_uadd_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_uaddl_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_uaddll_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_ssub_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_ssubl_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_ssubll_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_usub_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_usubl_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_usubll_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_smul_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_smull_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_smulll_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_umul_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_umull_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_umulll_overflow"), C_SYMBOL_BUILTIN_OVERFLOW },
    { S8_INITIALIZER("__builtin_popcount"), C_SYMBOL_BUILTIN_POPULATION_COUNT },
    { S8_INITIALIZER("__builtin_popcountl"), C_SYMBOL_BUILTIN_POPULATION_COUNT },
    { S8_INITIALIZER("__builtin_popcountll"), C_SYMBOL_BUILTIN_POPULATION_COUNT },
    // parity is the population count's low bit. bswap reverses the bytes of
    // its fixed-width unsigned operand and result through the integer
    // transform path, which shares the rotate builtins' typing and folding.
    { S8_INITIALIZER("__builtin_parity"), C_SYMBOL_BUILTIN_PARITY },
    { S8_INITIALIZER("__builtin_parityl"), C_SYMBOL_BUILTIN_PARITY },
    { S8_INITIALIZER("__builtin_parityll"), C_SYMBOL_BUILTIN_PARITY },
    { S8_INITIALIZER("__builtin_bswap16"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_bswap32"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_bswap64"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_rotateleft8"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_rotateleft16"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_rotateleft32"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_rotateleft64"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_rotateright8"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_rotateright16"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_rotateright32"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    { S8_INITIALIZER("__builtin_rotateright64"), C_SYMBOL_BUILTIN_INTEGER_TRANSFORM },
    // The target-fixed 512-bit vocabulary. These names are a buster extension
    // and exist so `<buster/lib/simd.h>` can write one kernel that the host
    // compilers and the self-hosted stages both compile; see the SIMD section
    // of AGENTS.md before adding to the list.
    { S8_INITIALIZER("__builtin_buster_simd_load"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_load_masked"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_store"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_store_masked"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_splat_u8"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_equal_u8"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_less_u8"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_sign_u8"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_test_u8"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_permute2_u8"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_compress_u8"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_compress_store_u8"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_widen_u8"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_shift_left_u32"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_ternary_u32"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_equal_u32"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_splat_u32"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_less_u32"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_compress_u32"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_permute2_u32"), C_SYMBOL_BUILTIN_SIMD },
};

#define C_SYMBOL_PREDEFINED_COUNT BUSTER_ARRAY_LENGTH(c_symbol_predefined)

// Dense kind classification for interned symbols; every id at or below
// predefined_limit indexes it, and the ids that are not builtins — 0, the
// well-known names, the keywords and the classified extras — stay
// C_SYMBOL_BUILTIN_NONE.
// The cold path for symbol-less identifier tokens: the same classification
// by spelling, scanning the single source-of-truth table above.
BUSTER_C_SHARED CSymbolBuiltin c_symbol_builtin_from_spelling(String8 spelling)
{
    if (spelling.length && spelling.pointer[0] == '_')
    {
        for (u64 index = 0; index < C_SYMBOL_PREDEFINED_COUNT; index += 1)
        {
            if (string_equal(spelling, c_symbol_predefined[index].name))
            {
                return (CSymbolBuiltin)c_symbol_predefined[index].builtin;
            }
        }
        if (c_semantic_bfloat16_builtin_spelling(spelling)) return C_SYMBOL_BUILTIN_NONE;
        if (c_vendor_builtin_spelling(spelling)) return C_SYMBOL_BUILTIN_VENDOR_TARGET;
        if (c_vendor_generic_builtin(spelling).operation) return C_SYMBOL_BUILTIN_VENDOR_GENERIC;
    }

    return C_SYMBOL_BUILTIN_NONE;
}

// These fixed builtin classes have no declaration entity. Their semantic
// result is void even when emission uses an internal placeholder value.
BUSTER_C_SHARED bool c_semantic_builtin_returns_void(CSymbolBuiltin builtin)
{
    bool result = builtin == C_SYMBOL_BUILTIN_DEBUGTRAP || builtin == C_SYMBOL_BUILTIN_SPIN_PAUSE ||
                  builtin == C_SYMBOL_BUILTIN_UNREACHABLE || builtin == C_SYMBOL_BUILTIN_CLEAR_CACHE ||
                  builtin == C_SYMBOL_BUILTIN_PREFETCH || builtin == C_SYMBOL_BUILTIN_VA_START ||
                  builtin == C_SYMBOL_BUILTIN_VA_START_C23 || builtin == C_SYMBOL_BUILTIN_VA_COPY ||
                  builtin == C_SYMBOL_BUILTIN_VA_END;
    return result;
}

// Fixed GNU signatures for clz/ctz/popcount/clrsb. The operation kind is
// shared by three spellings; the suffix determines the parameter width, while
// the C result type is always int. clrsb alone takes a signed operand.
// CTypeKind retains the target's long data model.
CTypeKind c_semantic_integer_count_parameter_kind(CSymbolBuiltin builtin, String8 spelling)
{
    CTypeKind result = C_TYPE_INVALID;
    if (builtin == C_SYMBOL_BUILTIN_COUNT_LEADING_ZEROS || builtin == C_SYMBOL_BUILTIN_COUNT_TRAILING_ZEROS ||
        builtin == C_SYMBOL_BUILTIN_POPULATION_COUNT || builtin == C_SYMBOL_BUILTIN_PARITY)
    {
        result = string_ends_with_sequence(spelling, S8("ll")) ? C_TYPE_UNSIGNED_LONG_LONG :
                 string_ends_with_sequence(spelling, S8("l")) ? C_TYPE_UNSIGNED_LONG : C_TYPE_UNSIGNED_INT;
    }
    else if (builtin == C_SYMBOL_BUILTIN_COUNT_LEADING_REDUNDANT_SIGN_BITS)
    {
        result = string_ends_with_sequence(spelling, S8("ll")) ? C_TYPE_LONG_LONG :
                 string_ends_with_sequence(spelling, S8("l")) ? C_TYPE_LONG : C_TYPE_INT;
    }
    return result;
}

// The signed parameter and result type of __builtin_abs/labs/llabs, or
// C_TYPE_INVALID for any other builtin. CTypeKind retains the target's long
// data model.
CTypeKind c_semantic_absolute_value_kind(CSymbolBuiltin builtin, String8 spelling)
{
    CTypeKind result = C_TYPE_INVALID;
    if (builtin == C_SYMBOL_BUILTIN_ABSOLUTE_VALUE)
    {
        result = string_equal(spelling, S8("__builtin_llabs")) ? C_TYPE_LONG_LONG :
                 string_equal(spelling, S8("__builtin_labs")) ? C_TYPE_LONG : C_TYPE_INT;
    }
    return result;
}

// The C type of __UINT64_TYPE__: unsigned long where long is 64 bits, except
// Darwin and Wasm, which spell int64_t as long long. Clang and GCC type the
// 64-bit bswap and rotate builtins with it.
CTypeKind c_semantic_uint64_kind(Target target)
{
    bool apple_target = target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS;
    bool wasm_target = target.cpu_arch == CPU_ARCH_WASM32 || target.cpu_arch == CPU_ARCH_WASM64;
    bool int64_uses_long = target_data_layout(target).unsigned_long_integer.bit_width == 64 && !apple_target && !wasm_target;
    return int64_uses_long ? C_TYPE_UNSIGNED_LONG : C_TYPE_UNSIGNED_LONG_LONG;
}

// Fixed unsigned signatures match Clang's T(T) and T(T,T) builtins. The 64-bit
// C rank follows __UINT64_TYPE__ (c_semantic_uint64_kind).
typedef struct CIntegerTransformDefinition CIntegerTransformDefinition;
struct CIntegerTransformDefinition
{
    String8 name;
    u8 width;
    u8 operation;
};

CIntegerTransformBuiltin c_semantic_integer_transform_builtin(Target target, String8 name)
{
    static CIntegerTransformDefinition const entries[] = {
        {S8_INITIALIZER("__builtin_bswap16"), 16, C_INTEGER_TRANSFORM_BYTE_SWAP},
        {S8_INITIALIZER("__builtin_bswap32"), 32, C_INTEGER_TRANSFORM_BYTE_SWAP},
        {S8_INITIALIZER("__builtin_bswap64"), 64, C_INTEGER_TRANSFORM_BYTE_SWAP},
        {S8_INITIALIZER("__builtin_rotateleft8"), 8, C_INTEGER_TRANSFORM_ROTATE_LEFT},
        {S8_INITIALIZER("__builtin_rotateleft16"), 16, C_INTEGER_TRANSFORM_ROTATE_LEFT},
        {S8_INITIALIZER("__builtin_rotateleft32"), 32, C_INTEGER_TRANSFORM_ROTATE_LEFT},
        {S8_INITIALIZER("__builtin_rotateleft64"), 64, C_INTEGER_TRANSFORM_ROTATE_LEFT},
        {S8_INITIALIZER("__builtin_rotateright8"), 8, C_INTEGER_TRANSFORM_ROTATE_RIGHT},
        {S8_INITIALIZER("__builtin_rotateright16"), 16, C_INTEGER_TRANSFORM_ROTATE_RIGHT},
        {S8_INITIALIZER("__builtin_rotateright32"), 32, C_INTEGER_TRANSFORM_ROTATE_RIGHT},
        {S8_INITIALIZER("__builtin_rotateright64"), 64, C_INTEGER_TRANSFORM_ROTATE_RIGHT},
    };
    CIntegerTransformBuiltin result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(entries) && !result.operation; index += 1)
    {
        if (string_equal(name, entries[index].name))
        {
            result.width = entries[index].width;
            result.operation = entries[index].operation;
            result.argument_count = result.operation == C_INTEGER_TRANSFORM_BYTE_SWAP ? 1 : 2;
            result.type = result.width == 8 ? C_TYPE_UNSIGNED_CHAR : result.width == 16 ? C_TYPE_UNSIGNED_SHORT :
                          result.width == 32 ? C_TYPE_UNSIGNED_INT : c_semantic_uint64_kind(target);
        }
    }
    return result;
}

// Inputs have already undergone their fixed unsigned parameter conversion.
// Zero counts are handled explicitly so the host never shifts a u64 by 64.
u64 c_integer_transform_bits(CIntegerTransformBuiltin builtin, u64 value, u64 count)
{
    u64 result = 0;
    if (builtin.operation && builtin.width && builtin.width <= 64)
    {
        u64 width_mask = builtin.width == 64 ? UINT64_MAX : (UINT64_C(1) << builtin.width) - 1;
        result = value & width_mask;
        if (builtin.operation == C_INTEGER_TRANSFORM_BYTE_SWAP)
        {
            static u64 const masks[] = {UINT64_C(0x00ff00ff00ff00ff), UINT64_C(0x0000ffff0000ffff), UINT64_C(0x00000000ffffffff)};
            for (u32 stage = 0, shift = 8; shift < builtin.width; stage += 1, shift *= 2)
            {
                u64 mask = masks[stage] & width_mask;
                result = (((result & mask) << shift) | ((result >> shift) & mask)) & width_mask;
            }
        }
        else
        {
            u32 shift = (u32)(count & (u64)(builtin.width - 1));
            if (shift)
            {
                result = builtin.operation == C_INTEGER_TRANSFORM_ROTATE_LEFT
                    ? (result << shift) | (result >> (builtin.width - shift))
                    : (result >> shift) | (result << (builtin.width - shift));
                result &= width_mask;
            }
        }
    }
    return result;
}

// The fixed unsigned type of a __builtin_bswap16/32/64 operand and result, or
// C_TYPE_INVALID for any other builtin. The 64-bit form is __UINT64_TYPE__.
CTypeKind c_semantic_byte_swap_kind(Target target, CSymbolBuiltin builtin, String8 spelling)
{
    CTypeKind result = C_TYPE_INVALID;
    if (builtin == C_SYMBOL_BUILTIN_BYTE_SWAP)
    {
        result = string_ends_with_sequence(spelling, S8("64")) ? c_semantic_uint64_kind(target) :
                 string_ends_with_sequence(spelling, S8("32")) ? C_TYPE_UNSIGNED_INT : C_TYPE_UNSIGNED_SHORT;
    }
    return result;
}

// The operand kind a constant-folded clz/ctz/ffs/clrsb/popcount/parity/bswap
// call converts its argument to, or C_TYPE_INVALID for any other builtin. It
// is the declared parameter type, with ffs (which takes a signed int) added to
// the counting family and the fixed bswap widths. CTypeKind retains the
// target's long data model.
CTypeKind c_semantic_integer_builtin_fold_kind(Target target, CSymbolBuiltin builtin, String8 spelling)
{
    CTypeKind result = c_semantic_byte_swap_kind(target, builtin, spelling);
    if (result == C_TYPE_INVALID)
    {
        result = c_semantic_integer_count_parameter_kind(builtin, spelling);
    }
    if (result == C_TYPE_INVALID)
    {
        result = c_semantic_absolute_value_kind(builtin, spelling);
    }
    if (result == C_TYPE_INVALID && builtin == C_SYMBOL_BUILTIN_FIND_FIRST_SET)
    {
        result = string_ends_with_sequence(spelling, S8("ll")) ? C_TYPE_LONG_LONG :
                 string_ends_with_sequence(spelling, S8("l")) ? C_TYPE_LONG : C_TYPE_INT;
    }
    return result;
}

// Evaluate one of those builtins on `bits`, the operand already converted to
// its fold kind and `width` bits wide (8 to 64). The count builtins answer an
// int-valued count and bswap the swapped bits. Returns false where the
// builtin is undefined (clz/ctz of zero) or is not one of them, so no value
// is claimed.
bool c_semantic_integer_builtin_fold(CSymbolBuiltin builtin, u32 width, u64 bits, u64* answer_out)
{
    bool known = width >= 8 && width <= 64;
    u64 answer = 0;
    if (known)
    {
        u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        bits &= mask;
        u32 leading_zeros = width;
        u32 trailing_zeros = width;
        u32 population = 0;
        for (u32 bit = 0; bit < width; bit += 1)
        {
            if ((bits >> bit) & 1)
            {
                leading_zeros = width - 1 - bit;
                trailing_zeros = BUSTER_MIN(trailing_zeros, bit);
                population += 1;
            }
        }
        switch (builtin)
        {
        case C_SYMBOL_BUILTIN_COUNT_LEADING_ZEROS: known = bits != 0; answer = leading_zeros; break;
        case C_SYMBOL_BUILTIN_COUNT_TRAILING_ZEROS: known = bits != 0; answer = trailing_zeros; break;
        case C_SYMBOL_BUILTIN_FIND_FIRST_SET: answer = bits ? trailing_zeros + 1 : 0; break;
        case C_SYMBOL_BUILTIN_POPULATION_COUNT: answer = population; break;
        case C_SYMBOL_BUILTIN_PARITY: answer = population & 1; break;
        case C_SYMBOL_BUILTIN_ABSOLUTE_VALUE:
        {
            // The most negative value has no positive counterpart and wraps
            // to itself under -fwrapv, as the (x ^ s) - s lowering and GCC's
            // static-initializer fold both do.
            u64 sign = UINT64_C(1) << (width - 1);
            answer = bits & sign ? (0 - bits) & mask : bits;
            break;
        }
        case C_SYMBOL_BUILTIN_COUNT_LEADING_REDUNDANT_SIGN_BITS:
        {
            // Leading bits equal to the sign bit, not counting the sign bit.
            u64 magnitude = (bits >> (width - 1)) & 1 ? ~bits & mask : bits;
            u32 magnitude_zeros = width;
            for (u32 bit = 0; bit < width; bit += 1)
            {
                if ((magnitude >> bit) & 1) magnitude_zeros = width - 1 - bit;
            }
            answer = magnitude_zeros - 1;
            break;
        }
        case C_SYMBOL_BUILTIN_BYTE_SWAP:
        {
            for (u32 byte = 0; byte < width / 8; byte += 1) answer |= ((bits >> (byte * 8)) & 0xff) << (width - 8 - byte * 8);
            break;
        }
        default: known = false; break;
        }
    }
    *answer_out = known ? answer : 0;
    return known;
}

// The libm-backed rounding, fused-multiply-add and scaling builtins, one row
// per link name. `argument_kind` is the floating type of every floating
// parameter, `result_kind` the call's type, and `integer_second` marks the
// `int` second parameter of ldexp. The call lowers to a runtime libm import.
BUSTER_GLOBAL_LOCAL CMathLibmShape const c_math_libm_shapes[] = {
    {S8_INITIALIZER("truncf"), 1, C_TYPE_FLOAT, C_TYPE_FLOAT, false},
    {S8_INITIALIZER("trunc"), 1, C_TYPE_DOUBLE, C_TYPE_DOUBLE, false},
    {S8_INITIALIZER("truncl"), 1, C_TYPE_LONG_DOUBLE, C_TYPE_LONG_DOUBLE, false},
    {S8_INITIALIZER("rintf"), 1, C_TYPE_FLOAT, C_TYPE_FLOAT, false},
    {S8_INITIALIZER("rint"), 1, C_TYPE_DOUBLE, C_TYPE_DOUBLE, false},
    {S8_INITIALIZER("rintl"), 1, C_TYPE_LONG_DOUBLE, C_TYPE_LONG_DOUBLE, false},
    {S8_INITIALIZER("nearbyintf"), 1, C_TYPE_FLOAT, C_TYPE_FLOAT, false},
    {S8_INITIALIZER("nearbyint"), 1, C_TYPE_DOUBLE, C_TYPE_DOUBLE, false},
    {S8_INITIALIZER("nearbyintl"), 1, C_TYPE_LONG_DOUBLE, C_TYPE_LONG_DOUBLE, false},
    {S8_INITIALIZER("fmaf"), 3, C_TYPE_FLOAT, C_TYPE_FLOAT, false},
    {S8_INITIALIZER("fma"), 3, C_TYPE_DOUBLE, C_TYPE_DOUBLE, false},
    {S8_INITIALIZER("fmal"), 3, C_TYPE_LONG_DOUBLE, C_TYPE_LONG_DOUBLE, false},
    {S8_INITIALIZER("ldexpf"), 2, C_TYPE_FLOAT, C_TYPE_FLOAT, true},
    {S8_INITIALIZER("ldexp"), 2, C_TYPE_DOUBLE, C_TYPE_DOUBLE, true},
    {S8_INITIALIZER("ldexpl"), 2, C_TYPE_LONG_DOUBLE, C_TYPE_LONG_DOUBLE, true},
    {S8_INITIALIZER("lroundf"), 1, C_TYPE_FLOAT, C_TYPE_LONG, false},
    {S8_INITIALIZER("lround"), 1, C_TYPE_DOUBLE, C_TYPE_LONG, false},
    {S8_INITIALIZER("lroundl"), 1, C_TYPE_LONG_DOUBLE, C_TYPE_LONG, false},
    {S8_INITIALIZER("llroundf"), 1, C_TYPE_FLOAT, C_TYPE_LONG_LONG, false},
    {S8_INITIALIZER("llround"), 1, C_TYPE_DOUBLE, C_TYPE_LONG_LONG, false},
    {S8_INITIALIZER("llroundl"), 1, C_TYPE_LONG_DOUBLE, C_TYPE_LONG_LONG, false},
};

// The libm shape of a math builtin named by its link name or its
// `__builtin_` spelling; `arity` is zero when it is not one of those.
CMathLibmShape c_semantic_math_libm_shape(String8 name)
{
    String8 link_name = string_starts_with_sequence(name, S8("__builtin_")) ? string_slice(name, 10, name.length) : name;
    CMathLibmShape result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_math_libm_shapes) && !result.arity; index += 1)
    {
        if (string_equal(link_name, c_math_libm_shapes[index].link_name))
        {
            result = c_math_libm_shapes[index];
        }
    }
    return result;
}

// The math builtins whose floating operand is long double, by link name. A
// bare `l` suffix test is wrong: ceil and huge_val end in `l` but return double.
bool c_semantic_math_link_is_long_double(String8 link_name)
{
    CMathLibmShape libm = c_semantic_math_libm_shape(link_name);
    return string_equal(link_name, S8("fabsl")) || string_equal(link_name, S8("fmaxl")) || string_equal(link_name, S8("fminl")) ||
           string_equal(link_name, S8("powil")) || string_equal(link_name, S8("copysignl")) || string_equal(link_name, S8("signbitl")) ||
           (libm.arity && libm.argument_kind == C_TYPE_LONG_DOUBLE);
}

// One probe entry of the intern table. The identity of a name is its first
// and last 8 bytes plus its length: for names up to 16 bytes the two
// overlapped words cover every byte, so matching (low, high, length) is byte
// equality with no further loads; longer names use the triple as a 17-byte
// filter and verify only the middle bytes against the stored spelling.
// length_and_id packs (length << 32) | id, and 0 marks an empty slot (id 0
// is never assigned).
struct CSymbolSlot
{
    u64 low;
    u64 high;
    u64 length_and_id;
};

typedef struct CSymbolKey
{
    u64 low;
    u64 high;
} CSymbolKey;

BUSTER_C_INTERNAL CSymbolKey c_symbol_key(String8 name)
{
    CSymbolKey key = {0};
    if (name.length >= 8)
    {
        memcpy(&key.low, name.pointer, sizeof(key.low));
        memcpy(&key.high, name.pointer + name.length - 8, sizeof(key.high));
    }
    else
    {
        // The bytes of a name shorter than the low word, gathered by the set
        // bits of its own length. Every read stays inside [pointer, pointer +
        // length), so no padding is assumed of the spelling space or of the
        // string literals the table is seeded from, and the byte loop's
        // one-to-seven trip count — half of every intern on a unity build is
        // a name this short — becomes three tests whose outcomes the length
        // already decides.
        u64 offset = 0;
        if (name.length & 4)
        {
            u32 quarter;
            memcpy(&quarter, name.pointer, sizeof(quarter));
            key.low = quarter;
            offset = 4;
        }
        if (name.length & 2)
        {
            u16 half;
            memcpy(&half, name.pointer + offset, sizeof(half));
            key.low |= (u64)half << (8 * offset);
            offset += 2;
        }
        if (name.length & 1)
        {
            key.low |= (u64)(u8)name.pointer[offset] << (8 * offset);
        }
    }
    return key;
}

// Differences in the top bytes of a key word only propagate upward through a
// multiply, so the slot bits must come from a folded value: fold the first
// product's high word down, remultiply, and take the high half.
BUSTER_C_INTERNAL u32 c_symbol_slot_hash(CSymbolKey key, u64 length)
{
    u64 hash = key.low * UINT64_C(0x9E3779B97F4A7C15) ^ key.high * UINT64_C(0xC2B2AE3D27D4EB4F) ^ length;
    hash ^= hash >> 32;
    hash *= UINT64_C(0xD6E8FEB86659FD93);
    return (u32)(hash >> 32);
}

// Both spellings already match on their first 8 bytes, last 8 bytes, and
// length (> 16); confirm the middle [8, length - 8) in overlapped words.
BUSTER_C_INTERNAL bool c_symbol_middle_equal(String8 stored, String8 name)
{
    u64 end = name.length - 8;
    u64 offset = 8;
    for (; offset + 8 <= end; offset += 8)
    {
        u64 stored_word;
        u64 name_word;
        memcpy(&stored_word, stored.pointer + offset, sizeof(stored_word));
        memcpy(&name_word, name.pointer + offset, sizeof(name_word));
        if (stored_word != name_word)
        {
            return false;
        }
    }
    u64 stored_word;
    u64 name_word;
    memcpy(&stored_word, stored.pointer + end - 8, sizeof(stored_word));
    memcpy(&name_word, name.pointer + end - 8, sizeof(name_word));
    return stored_word == name_word;
}

// Identifier identity uses decoded UTF-8, while tokens keep their original
// spelling until macro # and ## have finished. Only escaped names allocate;
// encoded output never exceeds the length of its original UCN spelling.
BUSTER_C_INTERNAL String8 c_symbol_ucn_name(CSymbolTable* table, String8 name)
{
    String8 canonical = name;
    u64 first = string_first_code_unit(name, '\\');
    if (BUSTER_UNLIKELY(first < name.length))
    {
        char8* bytes = arena_allocate(table->arena, char8, name.length);
        memcpy(bytes, name.pointer, first);
        u64 output = first;
        u64 offset = first;
        bool decoded = false;
        bool valid = true;
        while (offset < name.length && valid)
        {
            if (c_ucn_prefix(name, offset))
            {
                CUcn ucn = c_ucn_decode(name, offset);
                valid = ucn.status == C_UCN_VALID;
                if (valid)
                {
                    u32 value = ucn.value;
                    if (value < 0x80)
                    {
                        bytes[output++] = (char8)value;
                    }
                    else if (value < 0x800)
                    {
                        bytes[output++] = (char8)(0xC0 | (value >> 6));
                        bytes[output++] = (char8)(0x80 | (value & 0x3F));
                    }
                    else if (value < 0x10000)
                    {
                        bytes[output++] = (char8)(0xE0 | (value >> 12));
                        bytes[output++] = (char8)(0x80 | ((value >> 6) & 0x3F));
                        bytes[output++] = (char8)(0x80 | (value & 0x3F));
                    }
                    else
                    {
                        bytes[output++] = (char8)(0xF0 | (value >> 18));
                        bytes[output++] = (char8)(0x80 | ((value >> 12) & 0x3F));
                        bytes[output++] = (char8)(0x80 | ((value >> 6) & 0x3F));
                        bytes[output++] = (char8)(0x80 | (value & 0x3F));
                    }
                    offset += ucn.length;
                    decoded = true;
                }
            }
            else
            {
                bytes[output++] = name.pointer[offset++];
            }
        }
        if (valid && decoded)
        {
            canonical = (String8){.pointer = bytes, .length = output};
            table->has_ucn_names = true;
        }
    }
    return canonical;
}

BUSTER_C_SHARED u32 c_symbol_intern(CSymbolTable* table, String8 name)
{
    name = c_symbol_ucn_name(table, name);
    WORK_LEDGER_RECORD(LOOKUP_SYMBOL_INTERNS, 1);
    WORK_LEDGER_RECORD(LOOKUP_SYMBOL_INTERN_BYTES_HASHED, name.length);
    CSymbolKey key = c_symbol_key(name);
    u64 length_word = (u64)name.length << 32;
    u32 mask = table->slot_capacity - 1;
    u32 slot = c_symbol_slot_hash(key, name.length) & mask;
    C_CENSUS_PHASE_RECORD(INTERN_CALLS, 1);
    C_CENSUS_PHASE_RECORD(INTERN_KEY_BYTES, BUSTER_MIN(name.length, (u64)16));
    for (;;)
    {
        WORK_LEDGER_RECORD(LOOKUP_SYMBOL_INTERN_PROBES, 1);
        CSymbolSlot* entry = &table->slots[slot];
        u64 length_and_id = entry->length_and_id;
        C_CENSUS_PHASE_RECORD(INTERN_PROBES, 1);
        if (!length_and_id)
        {
            break;
        }
        if (entry->low == key.low && entry->high == key.high && (length_and_id & UINT64_C(0xFFFFFFFF00000000)) == length_word)
        {
            u32 id = (u32)length_and_id;
#if BUSTER_BENCH_ALLOCATIONS
            if (name.length > 16)
            {
                C_CENSUS_PHASE_RECORD(INTERN_MIDDLE_COMPARES, 1);
                C_CENSUS_PHASE_RECORD(INTERN_MIDDLE_BYTES, name.length - 16);
            }
#endif
            if (name.length <= 16 || c_symbol_middle_equal(table->names[id], name))
            {
                return id;
            }
        }
        slot = (slot + 1) & mask;
    }
    C_CENSUS_PHASE_RECORD(INTERN_INSERTS, 1);
    if (table->count + 1 == table->name_capacity)
    {
        u32 name_capacity = table->name_capacity * 2;
        String8* names = arena_allocate(table->arena, String8, name_capacity);
        memcpy(names, table->names, sizeof(*names) * (table->count + 1));
        table->names = names;
        table->name_capacity = name_capacity;
    }
    // Keep the probe table at most half full so lookups stay short; the
    // rebuild re-places every entry at the doubled capacity from the key
    // words the entries already carry.
    if (table->count + 1 > table->slot_capacity / 2)
    {
        u32 slot_capacity = table->slot_capacity * 2;
        C_CENSUS_PHASE_RECORD(INTERN_REHASH_SLOTS, table->slot_capacity);
        CSymbolSlot* slots = arena_allocate(table->arena, CSymbolSlot, slot_capacity);
        memset(slots, 0, sizeof(*slots) * slot_capacity);
        for (u32 old_slot = 0; old_slot < table->slot_capacity; old_slot += 1)
        {
            CSymbolSlot entry = table->slots[old_slot];
            if (!entry.length_and_id)
            {
                continue;
            }
            u32 rebuilt_slot = c_symbol_slot_hash((CSymbolKey){.low = entry.low, .high = entry.high}, entry.length_and_id >> 32) & (slot_capacity - 1);
            while (slots[rebuilt_slot].length_and_id)
            {
                rebuilt_slot = (rebuilt_slot + 1) & (slot_capacity - 1);
            }
            slots[rebuilt_slot] = entry;
        }
        table->slots = slots;
        table->slot_capacity = slot_capacity;
        slot = c_symbol_slot_hash(key, name.length) & (slot_capacity - 1);
        while (table->slots[slot].length_and_id)
        {
            slot = (slot + 1) & (slot_capacity - 1);
        }
    }
    u32 id = table->count + 1;
    table->count = id;
    table->names[id] = name;
    table->slots[slot] = (CSymbolSlot){
        .low = key.low,
        .high = key.high,
        .length_and_id = length_word | id,
    };
    return id;
}

// The id `name` was interned under, or 0 when it never was. The probe is
// c_symbol_intern's without the insertion, so a consumer that holds a
// spelling from outside the token stream -- lowering's IrField names -- can
// ask for its id without growing the table. A 0 answer is exact: no token
// interned into this table spells `name`.
BUSTER_C_SHARED u32 c_symbol_find(const CSymbolTable* table, String8 name)
{
    CSymbolKey key = c_symbol_key(name);
    u64 length_word = (u64)name.length << 32;
    u32 mask = table->slot_capacity - 1;
    u32 slot = c_symbol_slot_hash(key, name.length) & mask;
    u32 result = 0;
    for (;;)
    {
        CSymbolSlot* entry = &table->slots[slot];
        u64 length_and_id = entry->length_and_id;
        if (!length_and_id)
        {
            break;
        }
        if (entry->low == key.low && entry->high == key.high && (length_and_id & UINT64_C(0xFFFFFFFF00000000)) == length_word)
        {
            u32 id = (u32)length_and_id;
            if (name.length <= 16 || c_symbol_middle_equal(table->names[id], name))
            {
                result = id;
                break;
            }
        }
        slot = (slot + 1) & mask;
    }
    return result;
}

BUSTER_C_SHARED String8 const c_declaration_keyword_spellings[] = {
    S8_INITIALIZER("auto"),          S8_INITIALIZER("break"),     S8_INITIALIZER("case"),           S8_INITIALIZER("char"),
    S8_INITIALIZER("const"),         S8_INITIALIZER("continue"),  S8_INITIALIZER("default"),        S8_INITIALIZER("do"),
    S8_INITIALIZER("double"),        S8_INITIALIZER("else"),      S8_INITIALIZER("enum"),           S8_INITIALIZER("extern"),
    S8_INITIALIZER("float"),         S8_INITIALIZER("for"),       S8_INITIALIZER("goto"),           S8_INITIALIZER("if"),
    S8_INITIALIZER("inline"),        S8_INITIALIZER("int"),       S8_INITIALIZER("long"),           S8_INITIALIZER("register"),
    S8_INITIALIZER("restrict"),      S8_INITIALIZER("return"),    S8_INITIALIZER("short"),          S8_INITIALIZER("signed"),
    S8_INITIALIZER("sizeof"),        S8_INITIALIZER("static"),    S8_INITIALIZER("struct"),         S8_INITIALIZER("switch"),
    S8_INITIALIZER("typedef"),       S8_INITIALIZER("union"),     S8_INITIALIZER("unsigned"),       S8_INITIALIZER("void"),
    S8_INITIALIZER("volatile"),      S8_INITIALIZER("while"),     S8_INITIALIZER("_Alignas"),       S8_INITIALIZER("_Alignof"),
    S8_INITIALIZER("_Atomic"),       S8_INITIALIZER("_Bool"),     S8_INITIALIZER("_Complex"),       S8_INITIALIZER("_Generic"),
    S8_INITIALIZER("_Imaginary"),    S8_INITIALIZER("_Noreturn"), S8_INITIALIZER("_Static_assert"), S8_INITIALIZER("_Thread_local"),
    S8_INITIALIZER("__attribute__"), S8_INITIALIZER("__attribute"), S8_INITIALIZER("__declspec"),    S8_INITIALIZER("__auto_type"),
    S8_INITIALIZER("__thread"),      S8_INITIALIZER("__typeof__"), S8_INITIALIZER("__typeof"),       S8_INITIALIZER("__extension__"),
    S8_INITIALIZER("__inline"),
    S8_INITIALIZER("__inline__"),
    S8_INITIALIZER("__const"),       S8_INITIALIZER("__const__"), S8_INITIALIZER("__volatile"),     S8_INITIALIZER("__volatile__"),
    S8_INITIALIZER("__restrict"),    S8_INITIALIZER("__restrict__"), S8_INITIALIZER("__signed"),    S8_INITIALIZER("__signed__"),
    S8_INITIALIZER("__asm"),         S8_INITIALIZER("__asm__"),   S8_INITIALIZER("__alignof"),      S8_INITIALIZER("__alignof__"),
    S8_INITIALIZER("_Nonnull"),      S8_INITIALIZER("_Nullable"), S8_INITIALIZER("_Null_unspecified"), S8_INITIALIZER("__int128"),
    S8_INITIALIZER("__complex"),     S8_INITIALIZER("__complex__"), S8_INITIALIZER("__builtin_va_list"),
    S8_INITIALIZER("_Float16"),
    S8_INITIALIZER("__bf16"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(c_declaration_keyword_spellings) < C_DECLARATION_KEYWORD_SLOT_COUNT / 2);

// Names whose parse-side token class is nonzero but that are not part of the
// builtin ladder; interned at create time so every classifiable name has a
// predefined id and `symbol > c_symbol_predefined_limit` is a constant-time
// "ordinary identifier" answer.
BUSTER_C_INTERNAL String8 const c_symbol_classified_extras[] = {
    S8_INITIALIZER("true"),          S8_INITIALIZER("false"),  S8_INITIALIZER("nullptr"),
    S8_INITIALIZER("alignof"),       S8_INITIALIZER("constexpr"), S8_INITIALIZER("typeof_unqual"),
    S8_INITIALIZER("typeof"),        S8_INITIALIZER("vector_size"), S8_INITIALIZER("__vector_size"),
    S8_INITIALIZER("__vector_size__"),
};

// The spellings behind CSymbolWellKnown: entry N is interned N-th and
// therefore receives id N, which is what lets a call site write the
// enumerator where the id belongs. Designated by enumerator so the pairing
// survives any reordering of the enum, and c_symbol_table_create asserts
// both halves of the contract — every entry present, every id as promised.
// C_SYMBOL_WELL_KNOWN_NONE keeps the empty spelling and is never interned.
BUSTER_C_SHARED String8 const c_symbol_well_known_spellings[C_SYMBOL_WELL_KNOWN_COUNT] = {
    [C_SYMBOL_WELL_KNOWN_NONE] = S8_INITIALIZER(""),
    [C_SYMBOL_WELL_KNOWN_CASE] = S8_INITIALIZER("case"),
    [C_SYMBOL_WELL_KNOWN_DEFAULT] = S8_INITIALIZER("default"),
    [C_SYMBOL_WELL_KNOWN_IF] = S8_INITIALIZER("if"),
    [C_SYMBOL_WELL_KNOWN_FOR] = S8_INITIALIZER("for"),
    [C_SYMBOL_WELL_KNOWN_WHILE] = S8_INITIALIZER("while"),
    [C_SYMBOL_WELL_KNOWN_SWITCH] = S8_INITIALIZER("switch"),
    [C_SYMBOL_WELL_KNOWN_DO] = S8_INITIALIZER("do"),
    [C_SYMBOL_WELL_KNOWN_ELSE] = S8_INITIALIZER("else"),
    [C_SYMBOL_WELL_KNOWN_OVERLOADABLE] = S8_INITIALIZER("__overloadable__"),
    [C_SYMBOL_WELL_KNOWN_STATIC_ASSERT] = S8_INITIALIZER("_Static_assert"),
    [C_SYMBOL_WELL_KNOWN_BREAK] = S8_INITIALIZER("break"),
    [C_SYMBOL_WELL_KNOWN_CONTINUE] = S8_INITIALIZER("continue"),
    [C_SYMBOL_WELL_KNOWN_GOTO] = S8_INITIALIZER("goto"),
    [C_SYMBOL_WELL_KNOWN_RETURN] = S8_INITIALIZER("return"),
    [C_SYMBOL_WELL_KNOWN_TYPEDEF] = S8_INITIALIZER("typedef"),
    [C_SYMBOL_WELL_KNOWN_STRUCT] = S8_INITIALIZER("struct"),
    [C_SYMBOL_WELL_KNOWN_UNION] = S8_INITIALIZER("union"),
    [C_SYMBOL_WELL_KNOWN_ENUM] = S8_INITIALIZER("enum"),
    [C_SYMBOL_WELL_KNOWN_STATIC] = S8_INITIALIZER("static"),
    [C_SYMBOL_WELL_KNOWN_EXTERN] = S8_INITIALIZER("extern"),
    [C_SYMBOL_WELL_KNOWN_THREAD_LOCAL] = S8_INITIALIZER("_Thread_local"),
    [C_SYMBOL_WELL_KNOWN_THREAD_GNU] = S8_INITIALIZER("__thread"),
    [C_SYMBOL_WELL_KNOWN_ASM] = S8_INITIALIZER("asm"),
    [C_SYMBOL_WELL_KNOWN_ASM_GNU] = S8_INITIALIZER("__asm"),
    [C_SYMBOL_WELL_KNOWN_ASM_GNU_ALT] = S8_INITIALIZER("__asm__"),
    [C_SYMBOL_WELL_KNOWN_ATTRIBUTE] = S8_INITIALIZER("__attribute__"),
    [C_SYMBOL_WELL_KNOWN_ATTRIBUTE_SHORT] = S8_INITIALIZER("__attribute"),
    [C_SYMBOL_WELL_KNOWN_SIZEOF] = S8_INITIALIZER("sizeof"),
    [C_SYMBOL_WELL_KNOWN_ALIGNOF] = S8_INITIALIZER("_Alignof"),
    [C_SYMBOL_WELL_KNOWN_ALIGNOF_GNU] = S8_INITIALIZER("__alignof"),
    [C_SYMBOL_WELL_KNOWN_ALIGNOF_GNU_ALT] = S8_INITIALIZER("__alignof__"),
    [C_SYMBOL_WELL_KNOWN_THREAD_LOCAL_C23] = S8_INITIALIZER("thread_local"),
    [C_SYMBOL_WELL_KNOWN_INLINE] = S8_INITIALIZER("inline"),
    [C_SYMBOL_WELL_KNOWN_INLINE_GNU] = S8_INITIALIZER("__inline"),
    [C_SYMBOL_WELL_KNOWN_INLINE_GNU_ALT] = S8_INITIALIZER("__inline__"),
    [C_SYMBOL_WELL_KNOWN_SECTION] = S8_INITIALIZER("section"),
    [C_SYMBOL_WELL_KNOWN_SECTION_GNU] = S8_INITIALIZER("__section__"),
    [C_SYMBOL_WELL_KNOWN_WEAK] = S8_INITIALIZER("weak"),
    [C_SYMBOL_WELL_KNOWN_WEAK_GNU] = S8_INITIALIZER("__weak__"),
    [C_SYMBOL_WELL_KNOWN_ALIAS] = S8_INITIALIZER("alias"),
    [C_SYMBOL_WELL_KNOWN_ALIAS_GNU] = S8_INITIALIZER("__alias__"),
    [C_SYMBOL_WELL_KNOWN_CONSTRUCTOR] = S8_INITIALIZER("constructor"),
    [C_SYMBOL_WELL_KNOWN_CONSTRUCTOR_GNU] = S8_INITIALIZER("__constructor__"),
    [C_SYMBOL_WELL_KNOWN_DESTRUCTOR] = S8_INITIALIZER("destructor"),
    [C_SYMBOL_WELL_KNOWN_DESTRUCTOR_GNU] = S8_INITIALIZER("__destructor__"),
    [C_SYMBOL_WELL_KNOWN_RETURNS_TWICE] = S8_INITIALIZER("returns_twice"),
    [C_SYMBOL_WELL_KNOWN_RETURNS_TWICE_GNU] = S8_INITIALIZER("__returns_twice__"),
    [C_SYMBOL_WELL_KNOWN_VISIBILITY] = S8_INITIALIZER("visibility"),
    [C_SYMBOL_WELL_KNOWN_VISIBILITY_GNU] = S8_INITIALIZER("__visibility__"),
    [C_SYMBOL_WELL_KNOWN_EXTENSION] = S8_INITIALIZER("__extension__"),
    [C_SYMBOL_WELL_KNOWN_DECLSPEC] = S8_INITIALIZER("__declspec"),
    [C_SYMBOL_WELL_KNOWN_REGISTER] = S8_INITIALIZER("register"),
    [C_SYMBOL_WELL_KNOWN_BUILTIN_OFFSETOF] = S8_INITIALIZER("__builtin_offsetof"),
    [C_SYMBOL_WELL_KNOWN_VOLATILE] = S8_INITIALIZER("volatile"),
    [C_SYMBOL_WELL_KNOWN_VOLATILE_GNU_ALT] = S8_INITIALIZER("__volatile__"),
    [C_SYMBOL_WELL_KNOWN_CONSTEXPR] = S8_INITIALIZER("constexpr"),
    [C_SYMBOL_WELL_KNOWN_CONST] = S8_INITIALIZER("const"),
    [C_SYMBOL_WELL_KNOWN_ATOMIC] = S8_INITIALIZER("_Atomic"),
    [C_SYMBOL_WELL_KNOWN_VA_OPT] = S8_INITIALIZER("__VA_OPT__"),
};

enum
{
    C_SYMBOL_PREDEFINED_LIMIT_CAPACITY = 512,
};

BUSTER_C_SHARED u8 c_parse_token_class_compute(String8 spelling);
BUSTER_C_SHARED u16 c_parse_word_bits_compute(String8 spelling);

BUSTER_C_INTERNAL CSymbolTable c_symbol_table_create(Arena* arena)
{
    CSymbolTable table = {
        .arena = arena,
        .slot_capacity = 1u << 14,
        .name_capacity = 1u << 12,
    };
    table.names = arena_allocate(arena, String8, table.name_capacity);
    table.slots = arena_allocate(arena, CSymbolSlot, table.slot_capacity);
    memset(table.slots, 0, sizeof(*table.slots) * table.slot_capacity);
    table.builtin_kinds = arena_allocate(arena, u8, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    table.class_bits = arena_allocate(arena, u8, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    table.word_bits = arena_allocate(arena, u16, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    memset(table.builtin_kinds, 0, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    memset(table.class_bits, 0, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    memset(table.word_bits, 0, sizeof(*table.word_bits) * C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    // The well-known names go in first and in enumerator order, because the
    // passes that compare against them spell the id as the enumerator itself
    // rather than reading it back out of the table.
    for (u32 well_known = 1; well_known < C_SYMBOL_WELL_KNOWN_COUNT; well_known += 1)
    {
        // An enumerator with no spelling would intern the empty name and
        // match nothing, so the gap is caught here rather than as a pass
        // that silently stops recognizing its keyword.
        BUSTER_CHECK(c_symbol_well_known_spellings[well_known].length);
        u32 id = c_symbol_intern(&table, c_symbol_well_known_spellings[well_known]);
        BUSTER_CHECK(id == well_known);
    }
    for (u64 index = 0; index < C_SYMBOL_PREDEFINED_COUNT; index += 1)
    {
        u32 id = c_symbol_intern(&table, c_symbol_predefined[index].name);
        BUSTER_CHECK(id == C_SYMBOL_WELL_KNOWN_COUNT + index);
        // Routed through the spelling scan instead of reading
        // c_symbol_predefined[index].builtin directly, so the two
        // classification paths cannot drift.
        table.builtin_kinds[id] = (u8)c_symbol_builtin_from_spelling(c_symbol_predefined[index].name);
    }
    // The declaration keywords and the classified extras join the predefined
    // range; their class bits come from the same compute the spelling
    // fallback uses, so the two classification paths cannot drift.
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(c_declaration_keyword_spellings); index += 1)
    {
        c_symbol_intern(&table, c_declaration_keyword_spellings[index]);
    }
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(c_symbol_classified_extras); index += 1)
    {
        c_symbol_intern(&table, c_symbol_classified_extras[index]);
    }
    BUSTER_VALIDATE(table.count < C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    table.predefined_limit = table.count;
    for (u32 id = 1; id <= table.count; id += 1)
    {
        table.class_bits[id] = c_parse_token_class_compute(table.names[id]);
        table.word_bits[id] = c_parse_word_bits_compute(table.names[id]);
    }
    return table;
}

// The intern pass over a lexed run. Identifiers are one token in five, and
// the one-byte shape sidecar says which: a 64-token window is one masked
// load and one compare, and only the identifier lanes are visited, where the
// row loop tested the kind of every 12-byte row. A run without a sidecar
// takes the row loop, which is also the definition the c frontend test
// module holds the mask path to (c_test_intern_scan_by_shape).
BUSTER_C_INTERNAL void c_symbols_intern_tokens(CSymbolTable* table, char8 const* spelling_base, CToken* tokens, CTokenShape const* shapes, u64 token_count)
{
    if (shapes)
    {
        Simd512 identifier_shape = simd512_splat((u8)C_TOKEN_IDENTIFIER);
        for (u64 window_base = 0; window_base < token_count; window_base += 64)
        {
            Mask64 window_mask = mask64_prefix(token_count - window_base);
            Mask64 identifiers = simd512_equal_u8(simd512_load_masked(shapes + window_base, window_mask), identifier_shape);
            while (identifiers)
            {
                u64 index = window_base + mask64_first_set(identifiers);
                C_CENSUS_RECORD(INTERN_PASS_TOKENS, 1);
                tokens[index].symbol = c_symbol_intern(table, c_token_spelling(spelling_base, tokens[index]));
                identifiers = mask64_and(identifiers, identifiers - 1);
            }
        }
    }
    else
    {
        for (u64 index = 0; index < token_count; index += 1)
        {
            if (tokens[index].kind == C_TOKEN_IDENTIFIER)
            {
                C_CENSUS_RECORD(INTERN_PASS_TOKENS, 1);
                tokens[index].symbol = c_symbol_intern(table, c_token_spelling(spelling_base, tokens[index]));
            }
        }
    }
}

// A token in flight through the preprocessor's expansion machinery, carrying
// the source location the final stream and diagnostics must preserve.
// `foreign` marks tokens whose location no longer derives from their
// spelling offset (macro replacement stamps the invocation location onto
// every replacement token); output materialization copies their spellings
// into the spelling space under an expansion source-map entry so on-demand
// recovery reproduces the stamped location exactly.
// `preceded_by_space` answers what `#` and text output ask: did white space stand
// before this token where it was written? C11 6.10.3.2p2 makes a
// stringified argument reproduce its own spacing — every run of white space
// becomes one space and tokens that were adjacent stay adjacent. It is
// ordinarily meaningful on a `foreign` token only: a token still spelled where it was
// written has its answer in its spelling offset, and recovering it at the
// two places that ask (argument collection and replacement materialization)
// keeps the per-token cost off the line staging path, which every token of
// every expanded line passes through. Substitution is what makes the offset
// stop answering — a replacement token stands where the invocation was, not
// where it was spelled — so from there the flag carries it. The flag is one
// bit of the word beside the token, so it costs no memory.
// Text-output requests also initialize the flag on raw text-line tokens
// before expansion; the final optional sidecar retains it after invocation
// stamping and output materialization erase the original offset relationship.
// `no_expand` is C11 6.10.3.4p2's blue paint. The expansion machinery keeps
// one `disabled` bit per macro rather than a hide set per token, which is
// enough while a replacement is being rescanned in place: the bit is set
// across the rescan and cleared after it. It is not enough for a token that
// leaves that rescan alive. A macro argument is fully expanded in a nested
// context and the resulting tokens are then substituted and rescanned again
// in the parent, by which time the bit has been cleared, so a self-naming
// macro reached through an argument expanded once more per nesting level
// (`si_pid` -> `a.b.si_pid` -> `a.b.a.b.si_pid`). Painting the name where the
// disabled bit stopped it makes the refusal permanent, which is what the
// standard requires and what the disabled bit alone cannot express. It is
// one bit of the same word, so it too costs no memory.
#define C_PP_STAMP_BITS 29
#define C_PP_STAMP_MASK ((1u << C_PP_STAMP_BITS) - 1)

typedef struct CPpToken CPpToken;
struct CPpToken
{
    CToken token;
    // 1 + the index of the token's location in the line's CPpStampTable, or
    // 0 for a text-line token that has needed none: only the token that
    // becomes a macro invocation ever needs a location (as the stamp its
    // replacement inherits and for its own diagnostics), and one invocation
    // is far rarer than one wrapped token, so c_preprocess_expand recovers it
    // from the offset at that moment through the line's frame and stamps the
    // table. A foreign token is stamped by construction and a directive-line
    // token is wrapped with its line head. An index instead of the 20-byte
    // location itself is what holds the record to one 16-byte row: a token
    // is copied eight to ten times on its way through the expansion
    // machinery, and the location is the same for a whole replacement.
    u32 stamp : C_PP_STAMP_BITS;
    u32 foreign : 1;
    u32 preceded_by_space : 1;
    u32 no_expand : 1;
};

BUSTER_CT_CHECK(sizeof(CPpToken) == 16);

// The distinct locations one line's expansion carries: one entry per
// invocation resolved on a text line, or the line head of a directive line.
// The wrap sites reset it per line, so it stays a handful of entries.
typedef struct CPpStampTable CPpStampTable;
struct CPpStampTable
{
    Arena* arena;
    CSourceLocation* entries;
    u32 count;
    u32 capacity;
};

// The stamp of `location`: 1 + its index.
BUSTER_C_INTERNAL u32 c_pp_stamp_push(CPpStampTable* stamps, CSourceLocation location)
{
    if (stamps->count == stamps->capacity)
    {
        u32 capacity = stamps->capacity ? stamps->capacity * 2 : 64;
        CSourceLocation* entries = arena_allocate(stamps->arena, CSourceLocation, capacity);
        if (stamps->count)
        {
            memcpy(entries, stamps->entries, sizeof(*entries) * stamps->count);
        }
        stamps->entries = entries;
        stamps->capacity = capacity;
    }
    BUSTER_VALIDATE(stamps->count < C_PP_STAMP_MASK);
    stamps->entries[stamps->count] = location;
    stamps->count += 1;
    return stamps->count;
}

// The location a stamped token carries; the zero location for an unstamped
// token, or when there is no table (the parse-side evaluator, which has no
// macros to invoke).
BUSTER_C_INTERNAL CSourceLocation c_pp_stamp_location(CPpStampTable const* stamps, u32 stamp)
{
    CSourceLocation result = {0};
    if (stamps && stamp)
    {
        result = stamps->entries[stamp - 1];
    }
    return result;
}

// White space between two tokens of one lexed run. The lexer keeps no white
// space, and does not have to: spellings are contiguous in translated
// source, so a gap between one token's end and the next token's offset is
// exactly where white space or a comment stood. The one gap that is not
// visible that way is a newline token, whose single byte abuts a token that
// starts in column one. Two tokens from different runs — one spelled in a
// `#define` line and one in the file that invokes it, say — are never
// contiguous, so they read as separated, which is what they are.
BUSTER_C_INTERNAL bool c_token_preceded_by_space(char8 const* spelling_base, CToken previous, CToken token)
{
    return previous.kind == C_TOKEN_NEWLINE || previous.offset + c_token_length(spelling_base, previous) != token.offset;
}

// The converse question, asked by a printer rather than by `#`: would these
// two spellings, written with nothing between them, lex back as these two
// tokens? Identifiers, numbers and literal prefixes run into one another, and
// a punctuator followed by a spelling that extends it (`-` `-`, `<` `:`)
// maximal-munches into another; every other pair may touch.
BUSTER_C_INTERNAL bool c_token_literal_has_prefix(String8 spelling)
{
    bool result = spelling.length && spelling.pointer[0] != '\'' && spelling.pointer[0] != '"';
    return result;
}

BUSTER_C_INTERNAL bool c_token_identifier_is_literal_prefix(String8 spelling)
{
    bool result = (spelling.length == 1 &&
                   (spelling.pointer[0] == 'u' || spelling.pointer[0] == 'U' || spelling.pointer[0] == 'L')) ||
                  (spelling.length == 2 && spelling.pointer[0] == 'u' && spelling.pointer[1] == '8');
    return result;
}

BUSTER_C_INTERNAL bool c_token_punctuators_join(CToken previous, String8 previous_spelling, String8 current)
{
    char8 first = current.length ? current.pointer[0] : 0;
    bool result = false;
    switch ((CPunctuator)previous.punctuator)
    {
    case C_PUNCTUATOR_PERCENT: result = first == ':' || first == '>' || first == '='; break;
    case C_PUNCTUATOR_LESS: result = first == '<' || first == '=' || first == ':' || first == '%'; break;
    case C_PUNCTUATOR_GREATER: result = first == '>' || first == '='; break;
    case C_PUNCTUATOR_ASSIGN:
    case C_PUNCTUATOR_EXCLAMATION:
    case C_PUNCTUATOR_STAR:
    case C_PUNCTUATOR_CARET: result = first == '='; break;
    case C_PUNCTUATOR_AMPERSAND: result = first == '&' || first == '='; break;
    case C_PUNCTUATOR_PIPE: result = first == '|' || first == '='; break;
    case C_PUNCTUATOR_SLASH: result = first == '/' || first == '*' || first == '='; break;
    case C_PUNCTUATOR_PLUS: result = first == '+' || first == '='; break;
    case C_PUNCTUATOR_MINUS: result = first == '-' || first == '>' || first == '='; break;
    case C_PUNCTUATOR_HASH: result = previous_spelling.length == 2 ? first == '%' : first == '#'; break;
    case C_PUNCTUATOR_COLON: result = first == '>'; break;
    case C_PUNCTUATOR_DOT: result = first == '.'; break;
    case C_PUNCTUATOR_SHIFT_LEFT:
    case C_PUNCTUATOR_SHIFT_RIGHT: result = first == '='; break;
    default: break;
    }
    return result;
}

bool c_token_requires_separator(CToken previous, String8 previous_spelling, CToken current, String8 current_spelling)
{
    bool result = false;
    if (previous.kind == C_TOKEN_IDENTIFIER)
    {
        result = current.kind == C_TOKEN_IDENTIFIER ||
                 (current.kind == C_TOKEN_PREPROCESSING_NUMBER && current_spelling.length && current_spelling.pointer[0] != '.');
        if (!result && (current.kind == C_TOKEN_CHARACTER_LITERAL || current.kind == C_TOKEN_STRING_LITERAL))
        {
            result = c_token_literal_has_prefix(current_spelling) || c_token_identifier_is_literal_prefix(previous_spelling);
        }
    }
    else if (previous.kind == C_TOKEN_PREPROCESSING_NUMBER)
    {
        result = current.kind == C_TOKEN_IDENTIFIER || current.kind == C_TOKEN_PREPROCESSING_NUMBER ||
                 current.kind == C_TOKEN_CHARACTER_LITERAL ||
                 (current.kind == C_TOKEN_STRING_LITERAL && c_token_literal_has_prefix(current_spelling));
        if (!result && current.kind == C_TOKEN_PUNCTUATOR && previous_spelling.length)
        {
            char8 last = previous_spelling.pointer[previous_spelling.length - 1];
            char8 first = current_spelling.length ? current_spelling.pointer[0] : 0;
            result = first == '.' ||
                     ((first == '+' || first == '-') &&
                      (last == 'e' || last == 'E' || last == 'p' || last == 'P'));
        }
    }
    else if (previous.kind == C_TOKEN_PUNCTUATOR)
    {
        result = (previous.punctuator == C_PUNCTUATOR_DOT && current.kind == C_TOKEN_PREPROCESSING_NUMBER && current_spelling.length &&
                  current_spelling.pointer[0] != '.') ||
                 (current.kind == C_TOKEN_PUNCTUATOR && c_token_punctuators_join(previous, previous_spelling, current_spelling));
    }
    return result;
}

typedef struct CPreprocessTokenNode CPreprocessTokenNode;
struct CPreprocessTokenNode
{
    CPreprocessTokenNode* next;
    CPpToken token;
};

// The preprocessed output stream. Tokens land in their final slots as lines
// resolve: the stream is the tail of a dedicated commit-on-demand arena
// (created beside the spelling arena in c_preprocess) that nothing else
// allocates from, so successive reservations are contiguous and `base` is
// the finished token array the moment the last line lands. A line reserves
// its upper bound — its lexed token count, or an expansion's node count —
// writes the tokens that survive the newline or pragma strip, and hands the
// surplus slots back. This replaces a staging array per line plus a range
// list copied into a final array at the end: the range after a line's
// staging array never coalesced with the next line's, because the range
// node itself was allocated between them, so the final copy was one
// line-sized memcpy per line (269 K on the unity compile) walking a node
// per line besides.
typedef struct CTokenStream CTokenStream;
struct CTokenStream
{
    Arena* arena;
    Arena* shape_arena;
    CToken* base;
    CTokenShape* shape_base;
};

BUSTER_C_INTERNAL CToken* c_token_stream_reserve(CTokenStream* stream, u64 token_count, CTokenShape** shapes_out)
{
    CToken* result = arena_allocate(stream->arena, CToken, token_count);
    *shapes_out = arena_allocate(stream->shape_arena, CTokenShape, token_count);
    return result;
}

BUSTER_C_INTERNAL void c_token_stream_shrink(CTokenStream* stream, u64 surplus_token_count)
{
    arena_set_position(stream->arena, stream->arena->position - surplus_token_count * sizeof(CToken));
    arena_set_position(stream->shape_arena, stream->shape_arena->position - surplus_token_count * sizeof(CTokenShape));
}

typedef enum CMacroExpansionTaskKind
{
    C_MACRO_EXPANSION_TOKEN,
    C_MACRO_EXPANSION_ENABLE,
} CMacroExpansionTaskKind;

typedef struct CMacroExpansionTask CMacroExpansionTask;
struct CMacroExpansionTask
{
    CMacro* macro;
    CPpToken token;
    CMacroExpansionTaskKind kind;
    u32 generation;
};

// Contexts suspend strictly LIFO. Pending parent tasks remain below a child's
// task_base; indices, unlike pointers into this array, survive growth.
enum
{
    C_MACRO_EXPANSION_LOCAL_TASK_CAPACITY = 64,
};

typedef struct CMacroExpansionTaskStack CMacroExpansionTaskStack;
struct CMacroExpansionTaskStack
{
    CMacroExpansionTask* data;
    u64 count;
    u64 capacity;
};

// Expose the row-storage bound to GCC before narrowing into the count bitfield.
#define C_MACRO_ARGUMENT_COUNT_MASK (UINT64_MAX >> 1)

typedef struct CMacroArgument CMacroArgument;
struct CMacroArgument
{
    CPpToken* tokens;
    CPpToken* expanded_tokens;
    u64 token_count;
    // A token row occupies 16 bytes, so its count cannot need the top bit.
    // Omission remains distinct from emptiness without growing this record.
    u64 expanded_token_count : 63;
    u64 omitted : 1;
};

typedef struct CMacroExpansionContinuation CMacroExpansionContinuation;

// Transient storage of macro invocations, strictly LIFO by invocation nesting
// and created on the first invocation that needs it. Everything an invocation
// allocates here is dead once its replacement tokens are on the task stack
// (they are copied by value), so c_macro_expansion_release rewinds both arenas
// to the positions taken before the invocation began. Nothing that outlives
// the invocation may live here: diagnostics, formatted messages, interned
// symbols, stamps and the root context's output nodes stay in the phase arena,
// and spellings in the spelling space. `argument_arena` holds only the raw
// argument token run of the innermost live invocations, so that it can give
// consumed arguments back from its top; `expansion_arena` holds the argument
// records, continuations, argument contexts and their output runs.
typedef struct CMacroExpansionStorage CMacroExpansionStorage;
struct CMacroExpansionStorage
{
    Arena* argument_arena;
    Arena* expansion_arena;
};

typedef struct CMacroExpansionMark CMacroExpansionMark;
struct CMacroExpansionMark
{
    u64 argument;
    u64 expansion;
};

typedef struct CMacroExpansionContext CMacroExpansionContext;
struct CMacroExpansionContext
{
    CMacroExpansionContext* parent;
    CMacroExpansionContinuation* continuation;
    u64 task_base;
    // The root context's rescan output is the line's node list, which the
    // caller consumes. An argument context (one with a continuation) writes
    // `output_tokens` instead: one contiguous run in the expansion arena that
    // becomes the argument's expanded tokens in place, so no node and no copy
    // is made per token. `output_count` counts either form.
    CPreprocessTokenNode* first_output;
    CPreprocessTokenNode* last_output;
    CPpToken* output_tokens;
    u64 output_count;
};

struct CMacroExpansionContinuation
{
    CMacroExpansionContext* parent;
    CMacro* macro;
    CMacroArgument* arguments;
    CPpToken invocation;
    // Arena positions taken before the invocation's first transient
    // allocation; c_macro_expansion_release rewinds to them once the
    // replacement has been handed to the task stack.
    CMacroExpansionMark mark;
    // The live part of the invocation's raw argument run (see
    // c_macro_continuation_release_raw).
    CPpToken* raw_tokens;
    u64 raw_count;
    u32 argument_count;
    u32 argument_index;
    // Second walk of a `__VA_OPT__` definition's arguments: the parameters
    // only the standing content uses, prescanned after the variable argument.
    bool content_pass;
};

typedef struct CPreprocessPragmaContext CPreprocessPragmaContext;
BUSTER_C_INTERNAL bool c_preprocess_expansion_pragma(CPreprocessPragmaContext* pragma_context,
                                                       char8 const* base, CPpToken marker, CPpStampTable const* stamps, CMacroExpansionTaskStack const* tasks);

typedef struct CMacroReplacementToken CMacroReplacementToken;
struct CMacroReplacementToken
{
    CPpToken token;
    bool placemarker;
    // This item is the `##` of GNU's `, ## __VA_ARGS__` comma-deletion
    // idiom: a paste written between a literal comma and the variadic
    // parameter. With omitted varargs the placemarker path deletes the comma;
    // with tokens present GNU performs no paste at all -- the comma stays
    // and the arguments follow -- where a real paste of `,` against the
    // first argument token cannot form one preprocessing token and would
    // refuse every non-empty call.
    bool comma_paste;
    // GNU omitted-varargs comma deletion discards the removed boundary.
    bool reset_space;
};
BUSTER_CT_CHECK(sizeof(CMacroReplacementToken) == 20);

BUSTER_C_EXTERN bool c_token_spelling_equal(char8 const* spelling_base, CToken token, String8 spelling)
{
    C_CENSUS_PHASE_RECORD(SPELLING_EQUAL_CALLS, 1);
    C_CENSUS_PHASE_RECORD(SPELLING_EQUAL_BYTES, spelling.length);
    return string_equal(c_token_spelling(spelling_base, token), spelling);
}

// Macros are found by interned symbol id: one indexed load. Symbol 0 (an
// uninterned token) matches nothing because every definition interns its
// name, so entry 0 is never written; an id past the index's capacity was
// interned after the last definition and so names no macro either.
BUSTER_C_INTERNAL CMacro* c_macro_find(CMacro* first, u32 symbol)
{
    CMacro* result = 0;
    if (first && symbol < first->by_symbol_capacity)
    {
        result = first->by_symbol[symbol];
    }
    return result;
}

// The lookup for tokens that may not have passed the intern pass (pasted or
// synthesized): intern on demand when a table is available so the fast
// symbol lookup stays authoritative.
BUSTER_C_INTERNAL CMacro* c_macro_find_token(CMacro* first, CSymbolTable* symbols, char8 const* spelling_base, CToken const* token)
{
    u32 symbol = token->symbol;
    if (!symbol && symbols)
    {
        symbol = c_symbol_intern(symbols, c_token_spelling(spelling_base, *token));
    }
    return c_macro_find(first, symbol);
}

// The white space a `#define` line was written with, recorded for the
// definition it produced. Only a list of two or more tokens has any interior
// spacing to record.
BUSTER_C_INTERNAL u8* c_macro_replacement_spaces(Arena* arena, char8 const* spelling_base, CToken* replacement, u32 replacement_count)
{
    u8* result = 0;
    if (replacement_count > 1)
    {
        result = arena_allocate(arena, u8, replacement_count);
        for (u32 index = 1; index < replacement_count; index += 1)
        {
            result[index] = c_token_preceded_by_space(spelling_base, replacement[index - 1], replacement[index]) ? 1 : 0;
        }
    }
    return result;
}

BUSTER_C_INTERNAL bool c_macro_is_paste(CToken token)
{
    return c_token_is_punctuator(&token, C_PUNCTUATOR_HASH_HASH);
}

BUSTER_C_INTERNAL bool c_macro_is_va_opt(CToken token)
{
    return token.kind == C_TOKEN_IDENTIFIER && token.symbol == C_SYMBOL_WELL_KNOWN_VA_OPT;
}

// The `)` closing the `__VA_OPT__` content whose `(` is at `open`, or
// `replacement_count` when the list ends first.
BUSTER_C_INTERNAL u32 c_macro_va_opt_close(CToken const* replacement, u32 replacement_count, u32 open)
{
    u32 depth = 0;
    u32 index = open;
    bool closed = false;
    while (index < replacement_count && !closed)
    {
        if (c_token_is_punctuator(&replacement[index], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&replacement[index], C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            depth -= 1;
            closed = depth == 0;
        }
        index += closed ? 0 : 1;
    }
    return index;
}

// C23 6.10.5.2's constraints on `__VA_OPT__` in a replacement list: only a
// variadic macro may write it, always as `__VA_OPT__ ( content )` with
// balanced parentheses, never nested, and with no `##` at either end of the
// content. The empty string means the list is valid; otherwise
// `violation_index` names the offending `__VA_OPT__`.
BUSTER_C_INTERNAL String8 c_macro_va_opt_violation(CToken const* replacement, u32 replacement_count, bool variadic, u32* violation_index)
{
    String8 message = {0};
    u32 index = 0;
    while (index < replacement_count && !message.length)
    {
        u32 next = index + 1;
        if (c_macro_is_va_opt(replacement[index]))
        {
            u32 open = index + 1;
            bool parenthesized = open < replacement_count && c_token_is_punctuator(&replacement[open], C_PUNCTUATOR_LEFT_PARENTHESIS);
            u32 close = parenthesized ? c_macro_va_opt_close(replacement, replacement_count, open) : replacement_count;
            if (!variadic)
            {
                message = S8("'__VA_OPT__' can only appear in the replacement list of a variadic macro");
            }
            else if (!parenthesized)
            {
                message = S8("'__VA_OPT__' must be followed by '('");
            }
            else if (close >= replacement_count)
            {
                message = S8("unterminated '__VA_OPT__' content");
            }
            else if (close > open + 1 && (c_macro_is_paste(replacement[open + 1]) || c_macro_is_paste(replacement[close - 1])))
            {
                message = S8("'##' cannot appear at either end of '__VA_OPT__' content");
            }
            else
            {
                for (u32 inner = open + 1; inner < close && !message.length; inner += 1)
                {
                    if (c_macro_is_va_opt(replacement[inner]))
                    {
                        message = S8("'__VA_OPT__' cannot be nested in '__VA_OPT__' content");
                    }
                }
            }
            *violation_index = index;
            next = close + 1;
        }
        index = next;
    }
    return message;
}

// Full-length name hash for the cold identity tables (file paths, macro
// parameters). The symbol-table key above samples only the first and last
// eight bytes, which would collide for long paths that differ in the middle.
BUSTER_C_INTERNAL u32 c_name_hash(String8 name)
{
    u64 hash = (name.length + 1) * UINT64_C(0x9E3779B97F4A7C15);
    u64 offset = 0;
    for (; offset + 8 <= name.length; offset += 8)
    {
        u64 word;
        memcpy(&word, name.pointer + offset, sizeof(word));
        hash = (hash ^ word) * UINT64_C(0xC2B2AE3D27D4EB4F);
        hash ^= hash >> 29;
    }
    u64 tail = 0;
    for (u64 shift = 0; offset < name.length; offset += 1, shift += 8)
    {
        tail |= (u64)(u8)name.pointer[offset] << shift;
    }
    hash = (hash ^ tail) * UINT64_C(0xD6E8FEB86659FD93);
    hash ^= hash >> 32;
    hash *= UINT64_C(0x9E3779B97F4A7C15);
    return (u32)(hash >> 32);
}

// Definition-time parameter index: which parameter, if any, a name spells.
// Up to C_MACRO_PARAMETER_LINEAR_LIMIT parameters are scanned directly; more
// get an open-addressing table (index + 1, zero marking empty) over the
// parameter names, sized for the parameter capacity up front, so a
// definition's lookups cost one hash and a short probe chain instead of a
// scan of every parameter. Duplicates are probed in insertion order, so the
// first parameter that spells a name still wins.
enum
{
    C_MACRO_PARAMETER_LINEAR_LIMIT = 8,
};

typedef struct CMacroParameterMap CMacroParameterMap;
struct CMacroParameterMap
{
    u32* slots;
    u32 mask;
#if BUSTER_INCLUDE_TESTS
    // Name comparisons performed, for scaling fixtures.
    u64 compare_count;
#endif
};

BUSTER_C_INTERNAL CMacroParameterMap c_macro_parameter_map_create(Arena* arena, u64 capacity)
{
    CMacroParameterMap map = {0};
    if (capacity > C_MACRO_PARAMETER_LINEAR_LIMIT)
    {
        u64 slot_count = 32;
        while (slot_count < capacity * 2)
        {
            slot_count *= 2;
        }
        map.slots = arena_allocate(arena, u32, slot_count);
        memset(map.slots, 0, slot_count * sizeof(*map.slots));
        map.mask = (u32)(slot_count - 1);
    }
    return map;
}

// Records parameters[index] after the caller has finished looking it up.
BUSTER_C_INTERNAL void c_macro_parameter_map_insert(CMacroParameterMap* map, String8* parameters, u32 index)
{
    if (map->slots)
    {
        u32 slot = c_name_hash(parameters[index]) & map->mask;
        while (map->slots[slot])
        {
            slot = (slot + 1) & map->mask;
        }
        map->slots[slot] = index + 1;
    }
}

BUSTER_C_INTERNAL s32 c_macro_parameter_map_find(CMacroParameterMap* map, String8* parameters, u32 parameter_count, String8 name)
{
    s32 result = -1;
    if (map->slots)
    {
        u32 slot = c_name_hash(name) & map->mask;
        while (map->slots[slot] && result < 0)
        {
#if BUSTER_INCLUDE_TESTS
            map->compare_count += 1;
#endif
            u32 candidate = map->slots[slot] - 1;
            if (string_equal(parameters[candidate], name))
            {
                result = (s32)candidate;
            }
            slot = (slot + 1) & map->mask;
        }
    }
    else
    {
        for (u32 parameter_index = 0; parameter_index < parameter_count && result < 0; parameter_index += 1)
        {
#if BUSTER_INCLUDE_TESTS
            map->compare_count += 1;
#endif
            if (string_equal(parameters[parameter_index], name))
            {
                result = (s32)parameter_index;
            }
        }
    }
    return result;
}

// `spelling_base` resolves the replacement tokens' spellings for the
// parameter index and may be null when the replacement list is empty.
// `parameter_map` is the directive's index over `parameters` when the caller
// already built one; null builds it here.
BUSTER_C_INTERNAL CMacro* c_macro_define_indexed(Arena* arena, char8 const* spelling_base, CSymbolTable* symbols, CMacro** first, CMacro** last,
                                                   String8 name, CToken* replacement, u32 replacement_count, String8* parameters, u32 parameter_count,
                                                   bool function_like, bool variadic, CMacroParameterMap* parameter_map)
{
    CMacroParameterMap local_map = {0};
    if (!parameter_map && function_like)
    {
        local_map = c_macro_parameter_map_create(arena, parameter_count);
        for (u32 index = 0; index < parameter_count; index += 1)
        {
            c_macro_parameter_map_insert(&local_map, parameters, index);
        }
        parameter_map = &local_map;
    }
    u32 symbol = c_symbol_intern(symbols, name);
    CMacro* macro = c_macro_find(*first, symbol);
    if (!macro)
    {
        macro = arena_allocate(arena, CMacro, 1);
        *macro = (CMacro){
            .name = name,
            .symbol = symbol,
        };
        if (*last)
        {
            (*last)->next = macro;
        }
        else
        {
            *first = macro;
        }
        *last = macro;
        // Every id the table has handed out lies below its name capacity,
        // so an index of that size covers this symbol and every earlier one;
        // the first definition builds it, and a definition whose id lies
        // past it (the table doubled since) regrows it from the list.
        BUSTER_CHECK(*first); // The first/last list endpoints are published together.
        if (symbol >= (*first)->by_symbol_capacity)
        {
            c_macro_index_rebuild(arena, *first, symbols->name_capacity);
        }
        else
        {
            (*first)->by_symbol[symbol] = macro;
        }
    }
    macro->next_generation += 1;
    macro->disabled = false;
    // A new definition also clears any previous dynamic builtin kind.
    macro->definition = (CMacroDefinition){
        .generation = macro->next_generation,
        .replacement = replacement,
        .replacement_count = replacement_count,
        .parameters = parameters,
        .parameter_count = parameter_count,
        .function_like = function_like,
        .variadic = variadic,
        .defined = true,
    };
    macro->definition.plain_count = replacement_count;
    // Allocated for every parameter list, empty replacement included: a
    // `#define F(x)` with no replacement tokens still reaches the capacity
    // walk below over its parameters.
    u32 parameter_expand_slots = variadic ? parameter_count * 2 : parameter_count;
    u32* parameter_expand_count = parameter_count ? arena_allocate(arena, u32, parameter_expand_slots) : 0;
    for (u32 index = 0; index < parameter_expand_slots; index += 1)
    {
        parameter_expand_count[index] = 0;
    }
    macro->definition.parameter_expand_count = parameter_expand_count;
    if (replacement_count)
    {
        u32* parameter_index = arena_allocate(arena, u32, replacement_count);
        u32 content_close = 0;
        for (u32 index = 0; index < replacement_count; index += 1)
        {
            CToken token = replacement[index];
            if (variadic && index + 1 < replacement_count && c_macro_is_va_opt(token))
            {
                content_close = c_macro_va_opt_close(replacement, replacement_count, index + 1);
            }
            s32 found = token.kind == C_TOKEN_IDENTIFIER && function_like ? c_macro_parameter_map_find(parameter_map, parameters, parameter_count, token.symbol ? symbols->names[token.symbol] : c_symbol_ucn_name(symbols, c_token_spelling(spelling_base, token))) : -1;
            parameter_index[index] = found >= 0 ? (u32)found : C_MACRO_PARAMETER_NONE;
            if (found >= 0)
            {
                bool stringized = index && c_token_is_punctuator(&replacement[index - 1], C_PUNCTUATOR_HASH);
                bool pasted = (index && c_macro_is_paste(replacement[index - 1])) ||
                              (index + 1 < replacement_count && c_macro_is_paste(replacement[index + 1]));
                bool ordinary = !stringized && !pasted;
                parameter_expand_count[found] += ordinary;
                if (variadic)
                {
                    parameter_expand_count[parameter_count + (u32)found] += ordinary && index >= content_close;
                }
                macro->definition.plain_count -= 1;
            }
        }
        macro->definition.parameter_index = parameter_index;
        // Recorded here for the same reason the parameter index is: which
        // operators a definition uses cannot change after it is written, and
        // every expansion of it would otherwise re-derive the answer.
        for (u32 index = 0; index < replacement_count; index += 1)
        {
            if (c_macro_is_paste(replacement[index]))
            {
                macro->definition.has_paste = true;
            }
            if (function_like && c_token_is_punctuator(&replacement[index], C_PUNCTUATOR_HASH) && index + 1 < replacement_count &&
                parameter_index[index + 1] != C_MACRO_PARAMETER_NONE)
            {
                macro->definition.has_stringify = true;
            }
            if (variadic && c_macro_is_va_opt(replacement[index]))
            {
                macro->definition.has_va_opt = true;
            }
        }
        if (macro->definition.has_va_opt)
        {
            // The variable argument decides whether the content stands, so
            // it is always prescanned.
            parameter_expand_count[parameter_count - 1] += 1;
            parameter_expand_count[parameter_count * 2 - 1] += 1;
        }
    }
    return macro;
}

BUSTER_C_INTERNAL CMacro* c_macro_define(Arena* arena, char8 const* spelling_base, CSymbolTable* symbols, CMacro** first, CMacro** last, String8 name,
                                           CToken* replacement, u32 replacement_count, String8* parameters, u32 parameter_count, bool function_like,
                                           bool variadic)
{
    return c_macro_define_indexed(arena, spelling_base, symbols, first, last, name, replacement, replacement_count, parameters, parameter_count,
                                  function_like, variadic, 0);
}

BUSTER_C_INTERNAL void c_macro_define_object_text(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro** first, CMacro** last, String8 name,
                                                    String8 replacement_text)
{
    CLexResult lex = c_lex_space(arena, space, replacement_text, false, C_PREPROCESS_DIALECT_GNU17);
    c_symbols_intern_tokens(symbols, lex.spelling_base, lex.tokens, lex.token_shapes, lex.token_count);
    u32 replacement_count = 0;
    while (replacement_count < lex.token_count && lex.tokens[replacement_count].kind != C_TOKEN_NEWLINE &&
           lex.tokens[replacement_count].kind != C_TOKEN_END_OF_FILE)
    {
        replacement_count += 1;
    }
    CMacro* macro = c_macro_define(arena, lex.spelling_base, symbols, first, last, name, lex.tokens, replacement_count, 0, 0, false, false);
    macro->definition.replacement_space = c_macro_replacement_spaces(arena, lex.spelling_base, lex.tokens, replacement_count);
}

BUSTER_C_INTERNAL void c_preprocess_diagnostic_reserve(Arena* arena, CPreprocessResult* result)
{
    if (result->diagnostic_count < result->diagnostic_capacity)
    {
        return;
    }
    u64 capacity = result->diagnostic_capacity ? result->diagnostic_capacity * 2 : 1;
    if (capacity <= result->diagnostic_count)
    {
        capacity = result->diagnostic_count + 1;
    }
    CDiagnostic* diagnostics = arena_allocate(arena, CDiagnostic, capacity);
    if (result->diagnostic_count)
    {
        memcpy(diagnostics, result->diagnostics, sizeof(*diagnostics) * result->diagnostic_count);
    }
    result->diagnostics = diagnostics;
    result->diagnostic_capacity = capacity;
}

BUSTER_C_INTERNAL void c_preprocess_diagnostic_push_severity(Arena* arena, CPreprocessResult* result, CSourceLocation location, CDiagnosticKind kind,
                                                               CDiagnosticSeverity severity, String8 message)
{
    IR_DIAGNOSTIC_CENSUS_RECORD(C_DIAGNOSTICS_RECORDED, 1);
    c_preprocess_diagnostic_reserve(arena, result);
    result->diagnostics[result->diagnostic_count++] = (CDiagnostic){
        .message = message,
        .location = location,
        .kind = kind,
        .severity = severity,
    };
    if (severity == C_DIAGNOSTIC_WARNING)
    {
        result->warning_count += 1;
    }
    else
    {
        result->error_count += 1;
    }
}

BUSTER_C_INTERNAL void c_preprocess_diagnostic_push(Arena* arena, CPreprocessResult* result, CSourceLocation location, CDiagnosticKind kind, String8 message)
{
    c_preprocess_diagnostic_push_severity(arena, result, location, kind, C_DIAGNOSTIC_ERROR, message);
}

BUSTER_C_INTERNAL void c_preprocess_diagnostic_copy(Arena* arena, CPreprocessResult* result, CDiagnostic diagnostic)
{
    c_preprocess_diagnostic_reserve(arena, result);
    result->diagnostics[result->diagnostic_count++] = diagnostic;
    if (diagnostic.severity == C_DIAGNOSTIC_WARNING)
    {
        result->warning_count += 1;
    }
    else
    {
        result->error_count += 1;
    }
}

BUSTER_C_INTERNAL void c_preprocess_output_push(Arena* arena, CPreprocessTokenNode** first, CPreprocessTokenNode** last, CPpToken token, u64* count)
{
    CPreprocessTokenNode* node = arena_allocate(arena, CPreprocessTokenNode, 1);
    node->next = 0;
    node->token = token;
    if (*last)
    {
        (*last)->next = node;
    }
    else
    {
        *first = node;
    }
    *last = node;
    *count += 1;
}

#define C_MACRO_EXPANSION_STORAGE_RESERVED_SIZE BUSTER_GB(8)

// Both arenas are created together and only when an invocation first needs
// them, so a unit that never expands a function-like macro reserves nothing.
BUSTER_C_INTERNAL void c_macro_expansion_storage_ensure(CMacroExpansionStorage* storage)
{
    if (!storage->expansion_arena)
    {
        storage->argument_arena = arena_create((ArenaCreation){
            .reserved_size = C_MACRO_EXPANSION_STORAGE_RESERVED_SIZE,
            .flags = {.pool_reuse = 1},
        });
        storage->expansion_arena = arena_create((ArenaCreation){
            .reserved_size = C_MACRO_EXPANSION_STORAGE_RESERVED_SIZE,
            .flags = {.pool_reuse = 1},
        });
        BUSTER_CHECK(storage->argument_arena && storage->expansion_arena);
    }
}

// Releases the arenas and, in a test build, records the largest peak of live
// bytes any storage reached in `detail`. A rewind folds the cursor it discards
// into `high_water`, and creation resets it, so the peak is a deterministic
// function of the input.
BUSTER_C_INTERNAL void c_macro_expansion_storage_destroy(CMacroExpansionStorage* storage, CPreprocessDetail* detail)
{
    if (storage->expansion_arena)
    {
#if BUSTER_INCLUDE_TESTS
        u64 peak = BUSTER_MAX(storage->argument_arena->high_water, storage->argument_arena->position) - arena_minimum_position +
                   BUSTER_MAX(storage->expansion_arena->high_water, storage->expansion_arena->position) - arena_minimum_position;
        detail->macro_expansion_peak_bytes = BUSTER_MAX(detail->macro_expansion_peak_bytes, peak);
#else
        (void)detail;
#endif
        arena_destroy(storage->expansion_arena, 1);
        arena_destroy(storage->argument_arena, 1);
        *storage = (CMacroExpansionStorage){0};
    }
}

BUSTER_C_INTERNAL CMacroExpansionMark c_macro_expansion_mark(CMacroExpansionStorage const* storage)
{
    return (CMacroExpansionMark){
        .argument = storage->argument_arena->position,
        .expansion = storage->expansion_arena->position,
    };
}

BUSTER_C_INTERNAL void c_macro_expansion_release(CMacroExpansionStorage const* storage, CMacroExpansionMark mark)
{
    arena_set_position(storage->argument_arena, mark.argument);
    arena_set_position(storage->expansion_arena, mark.expansion);
}

// One token onto an argument context's output run. Nothing else allocates from
// the expansion arena between two pushes to the same context: an invocation
// found in between is released before the context runs again.
BUSTER_C_INTERNAL void c_macro_context_output_push(Arena* arena, Arena* expansion_arena, CMacroExpansionContext* context, CPpToken token)
{
    if (context->continuation)
    {
        CPpToken* slot = arena_allocate(expansion_arena, CPpToken, 1);
        if (!context->output_count)
        {
            context->output_tokens = slot;
        }
        BUSTER_ASSERT(slot == context->output_tokens + context->output_count);
        *slot = token;
        context->output_count += 1;
    }
    else
    {
        c_preprocess_output_push(arena, &context->first_output, &context->last_output, token, &context->output_count);
    }
}

// Room for `token_count` tokens plus an optional ENABLE marker above the
// current top. Growth copies the stack, so `tasks->data` is re-read after
// this call and never retained across it; every context floor is a stable
// `task_base` index for the same reason.
BUSTER_C_INTERNAL void c_macro_expansion_tasks_reserve(Arena* arena, CMacroExpansionTaskStack* tasks, u64 token_count, bool enable)
{
    u64 maximum_count = UINT64_MAX / sizeof(CMacroExpansionTask);
    BUSTER_VALIDATE(tasks->count < maximum_count && token_count <= maximum_count - tasks->count - enable);
    u64 required = tasks->count + token_count + enable;
    if (required > tasks->capacity)
    {
        u64 doubled = tasks->capacity > maximum_count / 2 ? maximum_count : tasks->capacity * 2;
        u64 capacity = BUSTER_MAX(required, doubled);
        CMacroExpansionTask* data = arena_allocate(arena, CMacroExpansionTask, capacity);
        if (tasks->count)
        {
            memcpy(data, tasks->data, tasks->count * sizeof(*data));
        }
        tasks->data = data;
        tasks->capacity = capacity;
    }
}

// One capacity check for the whole batch. Reversing the stored tokens keeps
// the next token at the top; an optional ENABLE marker sits below the batch.
// No pointer into task storage escapes this helper or survives another push.
BUSTER_C_INTERNAL void c_macro_expansion_tasks_push(Arena* arena, CMacroExpansionTaskStack* tasks, CPpToken const* tokens, u64 token_count, CMacro* macro, u32 generation)
{
    c_macro_expansion_tasks_reserve(arena, tasks, token_count, macro != 0);
    u64 output = tasks->count;
    if (macro)
    {
        tasks->data[output++] = (CMacroExpansionTask){.macro = macro, .kind = C_MACRO_EXPANSION_ENABLE, .generation = generation};
    }
    for (u64 index = token_count; index; index -= 1)
    {
        tasks->data[output++] = (CMacroExpansionTask){.token = tokens[index - 1], .kind = C_MACRO_EXPANSION_TOKEN};
    }
    tasks->count = output;
}

BUSTER_C_INTERNAL void c_macro_enable_definition(CMacroExpansionTask task)
{
    if (task.macro->definition.generation == task.generation)
    {
        task.macro->disabled = false;
    }
}

// Collecting an invocation's arguments is also where an argument recovers the
// white space `#` has to reproduce: its tokens are the contiguous run the
// invocation was written with, so the answer is in their spelling offsets and
// nothing earlier has to carry it. Only tokens the expansion machinery
// synthesized or substituted arrive with an answer of their own, and those
// are already marked foreign.
//
// The argument records and the run of argument tokens are transient: the
// records go to the expansion arena and the tokens, collected as one
// contiguous run in call order (arguments never interleave), to the argument
// arena. Both are the caller's to release; diagnostics stay in `arena`.
BUSTER_C_INTERNAL bool c_macro_invocation_arguments(Arena* arena, CMacroExpansionStorage* storage, char8 const* spelling_base, CSymbolTable* symbols, CMacro* first_macro, CMacroExpansionTaskStack* tasks, u64 task_base,
                                                      CMacro* macro, CSourceLocation location, CMacroArgument** arguments_out, u32* argument_count_out,
                                                      CPreprocessResult* result)
{
    while (tasks->count > task_base && tasks->data[tasks->count - 1].kind == C_MACRO_EXPANSION_ENABLE)
    {
        c_macro_enable_definition(tasks->data[--tasks->count]);
    }
    bool invoked = tasks->count > task_base && tasks->data[tasks->count - 1].kind == C_MACRO_EXPANSION_TOKEN &&
                   c_token_is_punctuator(&tasks->data[tasks->count - 1].token.token, C_PUNCTUATOR_LEFT_PARENTHESIS);
    if (invoked)
    {
        tasks->count -= 1;
        u32 capacity = macro->definition.parameter_count + 1;
        CMacroArgument* arguments = arena_allocate(storage->expansion_arena, CMacroArgument, capacity);
        for (u32 argument_index = 0; argument_index < capacity; argument_index += 1)
        {
            arguments[argument_index] = (CMacroArgument){0};
        }
        CPpToken* run = 0;
        u64 run_count = 0;
        u32 argument_count = macro->definition.parameter_count ? 1 : 0;
        u32 current = 0;
        u32 depth = 0;
        bool closed = false;
        bool valid = true;
        while (tasks->count > task_base && valid)
        {
            CMacroExpansionTask task = tasks->data[--tasks->count];
            if (task.kind == C_MACRO_EXPANSION_ENABLE)
            {
                c_macro_enable_definition(task);
            }
            else
            {
                CPpToken token = task.token;
                if (token.token.kind == C_TOKEN_IDENTIFIER && !token.no_expand)
                {
                    CMacro* argument_macro = c_macro_find_token(first_macro, symbols, spelling_base, &token.token);
                    // Collection can cross an ENABLE before argument prescan.
                    // Keep a refusal from this token's original rescan context.
                    token.no_expand = argument_macro && argument_macro->definition.defined && argument_macro->disabled;
                }
                bool separator = false;
                if (c_token_is_punctuator(&token.token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&token.token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    if (!depth)
                    {
                        closed = true;
                        break;
                    }
                    depth -= 1;
                }
                else if (c_token_is_punctuator(&token.token, C_PUNCTUATOR_COMMA) && !depth)
                {
                    bool collect_variadic = macro->definition.variadic && current + 1 >= macro->definition.parameter_count;
                    if (!collect_variadic)
                    {
                        current += 1;
                        if (current >= capacity)
                        {
                            c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_MACRO_INVOCATION, S8("too many arguments in macro invocation"));
                            valid = false;
                        }
                        else
                        {
                            argument_count = BUSTER_MAX(argument_count, current + 1);
                        }
                        separator = true;
                    }
                }
                if (valid && !separator)
                {
                    if (!argument_count)
                    {
                        argument_count = 1;
                    }
                    // Nothing else allocates from the argument arena while an
                    // invocation collects, so the run stays contiguous.
                    CPpToken* slot = arena_allocate(storage->argument_arena, CPpToken, 1);
                    if (!run)
                    {
                        run = slot;
                    }
                    BUSTER_ASSERT(slot == run + run_count);
                    *slot = token;
                    run_count += 1;
                    arguments[current].token_count += 1;
                }
            }
        }
        if (valid && !closed)
        {
            c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_MACRO_INVOCATION,
                                         string_format(arena, S8("unterminated invocation of macro '{S8}'"), macro->name));
            valid = false;
        }
        if (valid)
        {
            if (macro->definition.variadic && argument_count + 1 == macro->definition.parameter_count)
            {
                arguments[argument_count].omitted = true;
                argument_count += 1;
            }
            else if (macro->definition.variadic && macro->definition.parameter_count == 1 && !arguments[0].token_count)
            {
                // With no fixed parameter, `M()` has no distinguishing comma.
                // GNU modes treat it as omitted; standard modes retain it as
                // an explicitly empty argument, matching GCC and Clang.
                arguments[0].omitted = c_preprocess_dialect_is_gnu(result->dialect);
            }
            if (argument_count != macro->definition.parameter_count)
            {
                c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_MACRO_INVOCATION, S8("macro argument count does not match its definition"));
                valid = false;
            }
        }
        if (valid)
        {
            // Arguments were collected in order, so each one's tokens start
            // where the previous one's end.
            CPpToken* cursor = run;
            for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
            {
                CMacroArgument* argument = arguments + argument_index;
                argument->tokens = cursor;
                for (u64 token_index = 1; token_index < argument->token_count; token_index += 1)
                {
                    if (!argument->tokens[token_index].foreign)
                    {
                        argument->tokens[token_index].preceded_by_space =
                            c_token_preceded_by_space(spelling_base, argument->tokens[token_index - 1].token, argument->tokens[token_index].token);
                    }
                }
                cursor = cursor ? cursor + argument->token_count : cursor;
            }
            *arguments_out = arguments;
            *argument_count_out = argument_count;
        }
    }
    return invoked;
}

BUSTER_C_INTERNAL CPpToken c_macro_stringify(CSpellingSpace* space, CMacroArgument argument, u32 stamp)
{
    char8 const* base = space->base;
    u64 length = 2;
    u64 trailing_backslashes = 0;
    for (u64 token_index = 0; token_index < argument.token_count; token_index += 1)
    {
        CToken token = argument.tokens[token_index].token;
        String8 spelling = c_token_spelling(base, token);
        bool literal = token.kind == C_TOKEN_STRING_LITERAL || token.kind == C_TOKEN_CHARACTER_LITERAL;
        length += spelling.length;
        bool separated = token_index != 0 && argument.tokens[token_index].preceded_by_space;
        length += separated;
        if (separated)
        {
            trailing_backslashes = 0;
        }
        for (u64 character_index = 0; character_index < spelling.length; character_index += 1)
        {
            char8 character = spelling.pointer[character_index];
            length += literal && (character == '\\' || character == '"');
            if (!literal && character == '\\')
            {
                trailing_backslashes += 1;
            }
            else
            {
                trailing_backslashes = 0;
            }
        }
    }
    bool escape_trailing_backslash = trailing_backslashes & 1;
    length += escape_trailing_backslash;
    char8* spelling = c_space_allocate(space, length + 1);
    u64 output = 0;
    spelling[output++] = '"';
    for (u64 token_index = 0; token_index < argument.token_count; token_index += 1)
    {
        CToken token = argument.tokens[token_index].token;
        String8 token_spelling = c_token_spelling(base, token);
        bool literal = token.kind == C_TOKEN_STRING_LITERAL || token.kind == C_TOKEN_CHARACTER_LITERAL;
        // One space for every run of white space the argument was written
        // with, and none at all between tokens that were written adjacent:
        // `#V` on `A.B.C` is "A.B.C", not "A . B . C".
        if (token_index && argument.tokens[token_index].preceded_by_space)
        {
            spelling[output++] = ' ';
        }
        for (u64 character_index = 0; character_index < token_spelling.length; character_index += 1)
        {
            char8 character = token_spelling.pointer[character_index];
            bool trailing_backslash = escape_trailing_backslash && token_index + 1 == argument.token_count &&
                                      character_index + 1 == token_spelling.length && character == '\\';
            if ((literal && (character == '\\' || character == '"')) || trailing_backslash)
            {
                spelling[output++] = '\\';
            }
            spelling[output++] = character;
        }
    }
    spelling[output++] = '"';
    spelling[output] = 0;
    return (CPpToken){
        .token =
            {
                .offset = c_space_offset(space, spelling),
                // Literal-token quotes and backslashes are escaped; an odd
                // trailing run from other tokens escapes its last backslash
                // so the closing quote terminates the literal exactly.
                .length = c_token_length_field(output),
                .kind = C_TOKEN_STRING_LITERAL,
            },
        .stamp = stamp & C_PP_STAMP_MASK,
        .foreign = true,
    };
}

BUSTER_C_INTERNAL u32 c_preprocess_builtin_line(CMacro* first);
BUSTER_C_INTERNAL u32 c_preprocess_builtin_include_level(CMacro* first);
BUSTER_C_INTERNAL CSourceLocation c_preprocess_recover_location(struct CPreprocessSourceFrame* frame, u32 file, CToken token);

BUSTER_C_INTERNAL CPpToken c_macro_builtin_token(CSpellingSpace* space, CMacro* first, u8 builtin, u32 stamp, u32 line)
{
    CPpToken result = {0};
    if (builtin == C_MACRO_BUILTIN_LINE || builtin == C_MACRO_BUILTIN_COUNTER || builtin == C_MACRO_BUILTIN_INCLUDE_LEVEL)
    {
        char8* digits = c_space_allocate(space, 11);
        u32 value = 0;
        if (builtin == C_MACRO_BUILTIN_LINE)
        {
            // The invocation stamp owns the line, including argument tokens
            // on another physical line within the same expansion batch.
            value = line ? line : c_preprocess_builtin_line(first);
        }
        else if (builtin == C_MACRO_BUILTIN_COUNTER)
        {
            value = first->builtin_counter++;
        }
        else
        {
            value = c_preprocess_builtin_include_level(first);
        }
        u32 length = 0;
        do
        {
            digits[9 - length] = (char8)('0' + value % 10);
            value /= 10;
            length += 1;
        } while (value);
        digits[10] = 0;
        result = (CPpToken){
            .token =
                {
                    .offset = c_space_offset(space, digits + 10 - length),
                    .length = (u16)length,
                    .kind = C_TOKEN_PREPROCESSING_NUMBER,
                },
            .stamp = stamp & C_PP_STAMP_MASK,
            .foreign = true,
        };
    }
    else
    {
        String8 path = builtin == C_MACRO_BUILTIN_BASE_FILE ? first->builtin_base_path : first->builtin_path;
        if (builtin == C_MACRO_BUILTIN_FILE_NAME)
        {
            u64 basename_start = 0;
            for (u64 index = 0; index < path.length; index += 1)
            {
                if (path.pointer[index] == '/' || path.pointer[index] == '\\')
                {
                    basename_start = index + 1;
                }
            }
            path = string_slice(path, basename_start, path.length);
        }
        u64 capacity = path.length * 4 + 3;
        char8* quoted = c_space_allocate(space, capacity);
        u64 output = 0;
        quoted[output++] = '"';
        for (u64 index = 0; index < path.length; index += 1)
        {
            char8 character = path.pointer[index];
            if (character < 32 || character == 127)
            {
                // Three octal digits preserve the byte without absorbing a
                // following digit, including names decoded from #line.
                quoted[output++] = '\\';
                quoted[output++] = (char8)('0' + ((character >> 6) & 7));
                quoted[output++] = (char8)('0' + ((character >> 3) & 7));
                quoted[output++] = (char8)('0' + (character & 7));
            }
            else
            {
                if (character == '\\' || character == '"')
                {
                    quoted[output++] = '\\';
                }
                quoted[output++] = character;
            }
        }
        quoted[output++] = '"';
        quoted[output] = 0;
        c_space_shrink(space, capacity - (output + 1));
        result = (CPpToken){
            .token =
                {
                    .offset = c_space_offset(space, quoted),
                    .length = c_token_length_field(output),
                    .kind = C_TOKEN_STRING_LITERAL,
                },
            .stamp = stamp & C_PP_STAMP_MASK,
            .foreign = true,
        };
    }
    return result;
}

// The `##` pass over `items[0..count)`, compacted in place into
// `*pasted_count_out` items. Placemarkers survive every paste in a chain and
// are removed only when the rescan tokens are emitted. `__VA_OPT__` content
// that `#` stringifies runs its own pass first, over just its tokens.
BUSTER_C_INTERNAL bool c_macro_paste_tokens(Arena* arena, CSpellingSpace* space, CMacro* macro, CMacroDefinition const* definition, CMacroArgument const* arguments,
                                             CSourceLocation location, u32 stamp, CPreprocessResult* result, CMacroReplacementToken* items, u32 count,
                                             u32* pasted_count_out)
{
    char8 const* base = space->base;
    bool ok = true;
    u32 pasted_count = 0;
    // Removing an empty left operand early would let `##` consume an
    // unrelated preceding token or appear to be at an edge.
    for (u32 index = 0; index < count && ok; index += 1)
    {
        CMacroReplacementToken item = items[index];
        if (!c_macro_is_paste(item.token.token))
        {
            items[pasted_count++] = item;
            continue;
        }
        if (!pasted_count || index + 1 >= count)
        {
            c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_TOKEN_PASTE,
                                         string_format(arena, S8("'##' appears at the edge of macro '{S8}'"), macro->name));
            ok = false;
            continue;
        }
        CMacroReplacementToken right = items[++index];
        CMacroReplacementToken* left = &items[pasted_count - 1];
        if (right.placemarker)
        {
            if (item.comma_paste && arguments[definition->parameter_count - 1].omitted)
            {
                left->placemarker = true;
                left->reset_space = true;
            }
            continue;
        }
        if (left->placemarker)
        {
            right.token.preceded_by_space = left->token.preceded_by_space;
            *left = right;
            continue;
        }
        if (item.comma_paste)
        {
            // Varargs present: GNU performs no paste here at all.  The comma
            // already stands in the buffer; the argument's first token
            // follows it as itself, and the rest of the argument flows
            // through the loop as ordinary tokens.
            items[pasted_count++] = right;
            continue;
        }
        String8 left_spelling = c_token_spelling(base, left->token.token);
        String8 right_spelling = c_token_spelling(base, right.token.token);
        u64 joined_length = left_spelling.length + right_spelling.length;
        // The joined text lives in the spelling space so the pasted token's
        // offset resolves like any other; pasting never crosses a newline or
        // splice, so relexing it cannot change its bytes and the relex is
        // validation plus kind classification only.
        char8* joined = c_space_allocate(space, joined_length + 1);
        if (left_spelling.length)
        {
            memcpy(joined, left_spelling.pointer, left_spelling.length);
        }
        if (right_spelling.length)
        {
            memcpy(joined + left_spelling.length, right_spelling.pointer, right_spelling.length);
        }
        joined[joined_length] = 0;
        TemporalArena paste_temporary = scratch_begin(&arena, 1);
        CLexResult lex = c_lex_dispatch(paste_temporary.arena, 0, (String8){
                                                                      .pointer = joined,
                                                                      .length = joined_length,
                                                                  }, false, false, result->dialect);
        // The oversized-token diagnostics count here: a pasted identifier or
        // number past the length field's reach fails as an invalid paste
        // instead of storing a sentinel only literals may carry.
        bool paste_valid = !lex.diagnostic_count && lex.token_count == 2 && lex.tokens[0].kind != C_TOKEN_END_OF_FILE &&
                           c_token_length(lex.spelling_base, lex.tokens[0]) == joined_length;
        CToken pasted_shape = paste_valid ? lex.tokens[0] : (CToken){0};
        scratch_end(paste_temporary);
        if (!paste_valid)
        {
            c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_TOKEN_PASTE,
                                         string_format(arena, S8("token paste '{S8}##{S8}' in macro '{S8}' does not form one preprocessing token"),
                                                       left_spelling, right_spelling, macro->name));
            ok = false;
            continue;
        }
        // A pasted identifier is interned where it is formed, exactly as
        // the token pass interns every lexed one, so the id -- not the
        // spelling -- is the name key every later phase reads. Left at 0,
        // each downstream lookup re-interned the joined spelling and
        // dropped the answer; interning here is once per paste. The
        // joined bytes live in the spelling space, which outlives the
        // table's borrowed name pointer.
        u32 pasted_symbol = pasted_shape.kind == C_TOKEN_IDENTIFIER && result->symbols
                                ? c_symbol_intern(result->symbols, (String8){.pointer = joined, .length = joined_length})
                                : 0;
        left->token = (CPpToken){
            .token =
                {
                    .offset = c_space_offset(space, joined),
                    .length = c_token_length_field(joined_length),
                    .kind = pasted_shape.kind,
                    .punctuator = pasted_shape.punctuator,
                    .symbol = pasted_symbol,
                },
            .stamp = stamp & C_PP_STAMP_MASK,
            .foreign = true,
            // The joined token starts where its left operand started, so it
            // inherits that operand's spacing; the pasted spelling itself
            // carries none.
            .preceded_by_space = left->token.preceded_by_space,
        };
    }
    *pasted_count_out = pasted_count;
    return ok;
}

// The invocation token is what a replacement token replaces, so it is what
// the replacement list's first token inherits its spacing from: `A+B` where
// `A` expands to `1` must stringify as "1+10", not "1 + 10". Every later
// replacement token was written in the `#define` line, whose tokens are
// contiguous in source like any other lexed run. The exception is the
// builtin `__has_*` marker lists below, whose spellings are synthesized one
// allocation at a time and so read as spaced — which is what stringifying
// one has always produced.
// Only builtins and definitions writing `#` or `##` stage a list here; a
// paste-free, stringify-free definition is produced into its task batch by
// c_macro_produce_plain_tasks without one.
BUSTER_C_INTERNAL bool c_macro_replacement_tokens(Arena* arena, Arena* scratch, CSpellingSpace* space, CMacro* first, CMacro* macro, CMacroDefinition const* definition, CMacroArgument* arguments,
                                                    CPpToken invocation, CPpStampTable const* stamps, CPreprocessResult* result, CPpToken** tokens_out,
                                                    u32* token_count_out, bool* trailing_space_out)
{
    u32 stamp = invocation.stamp;
    CSourceLocation location = c_pp_stamp_location(stamps, stamp);
    u8 const* definition_spaces = definition->replacement_space;
    u32 const* parameter_indices = definition->parameter_index;
    bool ok = true;
    *trailing_space_out = false;
    if (definition->builtin)
    {
        CPpToken* builtin_token = arena_allocate(scratch, CPpToken, 1);
        builtin_token[0] = c_macro_builtin_token(space, first, definition->builtin, stamp, location.line);
        builtin_token[0].preceded_by_space = invocation.preceded_by_space;
        *tokens_out = builtin_token;
        *token_count_out = 1;
    }
    else
    {
        u64 capacity = definition->replacement_count + 1;
        for (u32 replacement_index = 0; replacement_index < definition->replacement_count; replacement_index += 1)
        {
            u32 parameter_index = parameter_indices[replacement_index];
            if (parameter_index != C_MACRO_PARAMETER_NONE && arguments)
            {
                CMacroArgument argument = arguments[parameter_index];
                capacity += BUSTER_MAX(argument.token_count, argument.expanded_token_count);
            }
        }
        CMacroReplacementToken* materialized = arena_allocate(scratch, CMacroReplacementToken, capacity);
        u32 materialized_count = 0;
        // `__VA_OPT__ ( content )` stands for its content when the variable
        // argument has tokens after expansion and for a placemarker otherwise
        // (C23 6.10.5.2). Standing content is materialized in place, so `##`
        // on either side pastes against its edge tokens; `va_opt_close` is the
        // `)` that ends the open content and `va_opt_start` its first item.
        bool va_opt_present = definition->has_va_opt && arguments && arguments[definition->parameter_count - 1].expanded_token_count != 0;
        u32 va_opt_close = UINT32_MAX;
        u32 va_opt_start = 0;
        bool va_opt_stringify = false;
        bool va_opt_space = false;
        for (u32 replacement_index = 0; replacement_index < definition->replacement_count && ok; replacement_index += 1)
        {
            CToken replacement = definition->replacement[replacement_index];
            bool replacement_space = replacement_index ? !definition_spaces || definition_spaces[replacement_index] != 0 : invocation.preceded_by_space;
            if (replacement_index == va_opt_close)
            {
                va_opt_close = UINT32_MAX;
                if (va_opt_stringify)
                {
                    u32 content_count = 0;
                    ok = c_macro_paste_tokens(arena, space, macro, definition, arguments, location, stamp, result, materialized + va_opt_start,
                                              materialized_count - va_opt_start, &content_count);
                    CMacroArgument content = {.tokens = arena_allocate(scratch, CPpToken, content_count)};
                    for (u32 index = 0; index < content_count; index += 1)
                    {
                        if (!materialized[va_opt_start + index].placemarker)
                        {
                            content.tokens[content.token_count++] = materialized[va_opt_start + index].token;
                        }
                    }
                    CPpToken stringified = c_macro_stringify(space, content, stamp);
                    stringified.preceded_by_space = va_opt_space;
                    materialized[va_opt_start] = (CMacroReplacementToken){
                        .token = stringified,
                    };
                    materialized_count = va_opt_start + 1;
                }
                else if (materialized_count == va_opt_start)
                {
                    materialized[materialized_count++] = (CMacroReplacementToken){
                        .token = {.token = replacement, .stamp = stamp & C_PP_STAMP_MASK, .foreign = true, .preceded_by_space = va_opt_space},
                        .placemarker = true,
                    };
                }
                else
                {
                    materialized[va_opt_start].token.preceded_by_space = va_opt_space;
                }
                continue;
            }
            bool stringify_va_opt = definition->has_va_opt && c_token_is_punctuator(&replacement, C_PUNCTUATOR_HASH) &&
                                    replacement_index + 1 < definition->replacement_count &&
                                    c_macro_is_va_opt(definition->replacement[replacement_index + 1]);
            if (definition->has_va_opt && (stringify_va_opt || c_macro_is_va_opt(replacement)))
            {
                // c_macro_va_opt_violation admitted the definition, so the
                // `(` follows and its `)` closes inside the list.
                u32 open = replacement_index + (stringify_va_opt ? 2 : 1);
                u32 close = c_macro_va_opt_close(definition->replacement, definition->replacement_count, open);
                if (va_opt_present)
                {
                    va_opt_close = close;
                    va_opt_start = materialized_count;
                    va_opt_stringify = stringify_va_opt;
                    va_opt_space = replacement_space;
                    replacement_index = open;
                }
                else
                {
                    CMacroReplacementToken absent = {
                        .token = {.token = replacement, .stamp = stamp & C_PP_STAMP_MASK, .foreign = true, .preceded_by_space = replacement_space},
                        .placemarker = true,
                    };
                    if (stringify_va_opt)
                    {
                        absent.token = c_macro_stringify(space, (CMacroArgument){0}, stamp);
                        absent.token.preceded_by_space = replacement_space;
                        absent.placemarker = false;
                    }
                    materialized[materialized_count++] = absent;
                    replacement_index = close;
                }
                continue;
            }
            if (definition->function_like && c_token_is_punctuator(&replacement, C_PUNCTUATOR_HASH) &&
                replacement_index + 1 < definition->replacement_count)
            {
                u32 parameter_index = parameter_indices[replacement_index + 1];
                if (arguments && parameter_index != C_MACRO_PARAMETER_NONE)
                {
                    CPpToken stringified = c_macro_stringify(space, arguments[parameter_index], stamp);
                    stringified.preceded_by_space = replacement_space;
                    materialized[materialized_count++] = (CMacroReplacementToken){
                        .token = stringified,
                    };
                    replacement_index += 1;
                    continue;
                }
            }
            u32 parameter_index = parameter_indices[replacement_index];
            if (parameter_index == C_MACRO_PARAMETER_NONE || !arguments)
            {
                // GNU's comma-deletion idiom, recognized on the definition's own
                // spelling: a `##` written between a literal comma and the
                // variadic parameter.  The paste loop below reads the mark.
                bool comma_paste = false;
                if (definition->variadic && c_macro_is_paste(replacement) && replacement_index &&
                    replacement_index + 1 < definition->replacement_count &&
                    c_token_is_punctuator(&definition->replacement[replacement_index - 1], C_PUNCTUATOR_COMMA))
                {
                    u32 next_parameter = parameter_indices[replacement_index + 1];
                    comma_paste = next_parameter != C_MACRO_PARAMETER_NONE && next_parameter + 1 == definition->parameter_count;
                }
                materialized[materialized_count++] = (CMacroReplacementToken){
                    .token = {.token = replacement, .stamp = stamp & C_PP_STAMP_MASK, .foreign = true, .preceded_by_space = replacement_space},
                    .comma_paste = comma_paste,
                };
                continue;
            }
            CMacroArgument argument = arguments[parameter_index];
            bool raw_argument = (replacement_index && c_macro_is_paste(definition->replacement[replacement_index - 1])) ||
                                (replacement_index + 1 < definition->replacement_count && c_macro_is_paste(definition->replacement[replacement_index + 1]));
            CPpToken* argument_tokens = raw_argument ? argument.tokens : argument.expanded_tokens;
            u64 argument_token_count = raw_argument ? argument.token_count : argument.expanded_token_count;
            if (!argument_token_count)
            {
                materialized[materialized_count++] = (CMacroReplacementToken){
                    .token = {.token = replacement, .stamp = stamp & C_PP_STAMP_MASK, .foreign = true, .preceded_by_space = replacement_space},
                    .placemarker = true,
                };
                continue;
            }
            for (u64 argument_index = 0; argument_index < argument_token_count; argument_index += 1)
            {
                CPpToken argument_token = argument_tokens[argument_index];
                argument_token.stamp = stamp & C_PP_STAMP_MASK;
                argument_token.foreign = true;
                // The argument stands where the parameter was written, so its
                // first token takes the parameter's spacing; the rest keep the
                // spacing they were written or expanded with.
                if (!argument_index)
                {
                    argument_token.preceded_by_space = replacement_space;
                }
                materialized[materialized_count++] = (CMacroReplacementToken){
                    .token = argument_token,
                };
            }
        }
        u32 pasted_count = 0;
        ok = ok && c_macro_paste_tokens(arena, space, macro, definition, arguments, location, stamp, result, materialized, materialized_count, &pasted_count);
        if (ok)
        {
            CPpToken* output = arena_allocate(scratch, CPpToken, pasted_count);
            u32 output_count = 0;
            bool pending_space = false;
            for (u32 index = 0; index < pasted_count; index += 1)
            {
                CMacroReplacementToken item = materialized[index];
                if (item.placemarker)
                {
                    // Ordinary empty parameters preserve their boundary. GNU
                    // omitted-comma elision discards the pending boundary too.
                    pending_space = item.reset_space ? false : pending_space || item.token.preceded_by_space;
                }
                else
                {
                    item.token.preceded_by_space = output_count ? item.token.preceded_by_space || pending_space : invocation.preceded_by_space;
                    output[output_count++] = item.token;
                    pending_space = false;
                }
            }
            *tokens_out = output;
            *token_count_out = output_count;
            *trailing_space_out = pending_space;
        }
    }
    return ok;
}

BUSTER_C_INTERNAL CPpToken c_macro_pragma_token(CSpellingSpace* space, CMacro* macro, CMacroArgument argument, CPpToken invocation)
{
    char8 const* base = space->base;
    CPpToken result = invocation;
    result.token.kind = C_TOKEN_PRAGMA;
    result.token.punctuator = C_PUNCTUATOR_NONE;
    result.token.offset = 0;
    result.token.length = 0;
    result.token.symbol = 0;
    result.foreign = true;
    CPpToken* tokens = argument.expanded_tokens;
    u64 token_count = argument.expanded_token_count;
    if (string_equal(macro->name, S8("_Pragma")))
    {
        String8 first_spelling = token_count == 1 ? c_token_spelling(base, tokens[0].token) : (String8){0};
        u64 prefix_length = first_spelling.length >= 3 && first_spelling.pointer[0] == 'L' ? 1 : 0;
        // Text past the length field's reach stays length 0 like any other
        // malformed operand: the marker is dropped, and no real pragma body
        // approaches 64 KB.
        if (token_count == 1 && tokens[0].token.kind == C_TOKEN_STRING_LITERAL && first_spelling.length >= prefix_length + 2 &&
            first_spelling.pointer[prefix_length] == '"' && first_spelling.pointer[first_spelling.length - 1] == '"' &&
            first_spelling.length - prefix_length - 2 < C_TOKEN_LENGTH_OVERSIZED)
        {
            String8 inner = {
                .pointer = first_spelling.pointer + prefix_length + 1,
                .length = first_spelling.length - prefix_length - 2,
            };
            char8* spelling = c_space_allocate(space, inner.length + 1);
            u64 output = 0;
            for (u64 index = 0; index < inner.length; index += 1)
            {
                // C's destringization removes escapes only for a quote or
                // backslash. Other escape spellings remain pragma text.
                if (inner.pointer[index] == '\\' && index + 1 < inner.length &&
                    (inner.pointer[index + 1] == '\\' || inner.pointer[index + 1] == '"'))
                {
                    index += 1;
                }
                spelling[output++] = inner.pointer[index];
            }
            spelling[output] = 0;
            result.token.offset = c_space_offset(space, spelling);
            result.token.length = (u16)output;
        }
    }
    else
    {
        u64 length = 0;
        for (u64 token_index = 0; token_index < token_count; token_index += 1)
        {
            length += c_token_length(base, tokens[token_index].token) + (token_index != 0);
        }
        if (length && length < C_TOKEN_LENGTH_OVERSIZED)
        {
            char8* spelling = c_space_allocate(space, length + 1);
            u64 output = 0;
            for (u64 token_index = 0; token_index < token_count; token_index += 1)
            {
                String8 token_spelling = c_token_spelling(base, tokens[token_index].token);
                if (token_index)
                {
                    spelling[output++] = ' ';
                }
                if (token_spelling.length)
                {
                    memcpy(spelling + output, token_spelling.pointer, token_spelling.length);
                }
                output += token_spelling.length;
            }
            spelling[output] = 0;
            result.token.offset = c_space_offset(space, spelling);
            result.token.length = (u16)output;
        }
    }
    return result;
}

// Whether nothing reads an argument's raw tokens after its prescan: the
// substitution of a pragma-like or paste-free, stringify-free, `__VA_OPT__`-free
// definition uses only expanded tokens (c_macro_materialize).
BUSTER_C_INTERNAL bool c_macro_definition_raw_free(CMacroDefinition const* definition)
{
    return definition->pragma_like || (!definition->builtin && !definition->has_paste && !definition->has_stringify && !definition->has_va_opt);
}

// Called as the current argument's tokens are handed to a child context: the
// invocation's raw run is shrunk to the arguments after it, so a nested
// invocation's collection does not pile up on top of dead copies of its
// enclosing ones (arguments of `F(F(F(...)))` would otherwise all stay live).
// Earlier arguments that alias the run keep what they still use in a copy; the
// remaining ones move down to the front. Only a run at the top of the argument
// arena can be given back, which holds whenever nested invocations have been
// released, and any other state is left as it is.
BUSTER_C_INTERNAL void c_macro_continuation_release_raw(CMacroExpansionStorage* storage, CMacroExpansionContinuation* continuation)
{
    CMacroDefinition const* definition = &continuation->macro->definition;
    Arena* argument_arena = storage->argument_arena;
    CMacroArgument* arguments = continuation->arguments;
    CPpToken* base = continuation->raw_tokens;
    u64 run_end = base ? (u64)((u8*)(base + continuation->raw_count) - (u8*)argument_arena) : 0;
    if (base && argument_arena->position == run_end && c_macro_definition_raw_free(definition))
    {
        u32 index = continuation->argument_index;
        for (u32 earlier_index = 0; earlier_index < index; earlier_index += 1)
        {
            CMacroArgument* earlier = arguments + earlier_index;
            if (earlier->tokens && earlier->expanded_tokens == earlier->tokens && earlier->expanded_token_count)
            {
                if (definition->pragma_like || definition->parameter_expand_count[earlier_index] != 0)
                {
                    CPpToken* copy = arena_allocate(storage->expansion_arena, CPpToken, earlier->expanded_token_count);
                    memcpy(copy, earlier->tokens, earlier->expanded_token_count * sizeof(CPpToken));
                    earlier->expanded_tokens = copy;
                }
                else
                {
                    earlier->expanded_tokens = 0;
                    earlier->expanded_token_count = 0;
                }
            }
            earlier->tokens = 0;
        }
        arguments[index].tokens = 0;
        CPpToken* remaining = index + 1 < continuation->argument_count ? arguments[index + 1].tokens : base + continuation->raw_count;
        u64 remaining_count = (u64)((base + continuation->raw_count) - remaining);
        if (remaining_count)
        {
            memmove(base, remaining, remaining_count * sizeof(CPpToken));
        }
        for (u32 later_index = index + 1; later_index < continuation->argument_count; later_index += 1)
        {
            arguments[later_index].tokens = base + (arguments[later_index].tokens - remaining);
        }
        continuation->raw_count = remaining_count;
        arena_set_position(argument_arena, (u64)((u8*)(base + remaining_count) - (u8*)argument_arena));
    }
}

// The next argument that needs an expansion context of its own, or null
// once every remaining argument is resolved and the invocation is ready to
// materialize. Definition-owned demand is reused before inspecting any raw
// argument: unused, stringized-only and pasted-only parameters must not be
// prescanned. A mixed-use parameter still needs one expansion, shared by all
// ordinary uses. Pragma operands are expanded despite their empty replacement.
// An argument none of whose identifiers names a defined macro
// — disabled ones included, since a name the disabled bit refuses must
// still be painted no_expand — rescans to exactly itself, so its expansion
// aliases its tokens instead of paying a child context, a task node and an
// output node per token to copy them unchanged, which is most arguments.
// The prescan looks up each identifier in the order the child would have,
// so a symbol-0 token (pasted or synthesized) interns at the same point
// either way.
BUSTER_C_INTERNAL CMacroExpansionContext* c_macro_continuation_advance(Arena* arena, CMacroExpansionStorage* storage, CMacro* first_macro, CSymbolTable* symbols, char8 const* base,
                                                                        CMacroExpansionContinuation* continuation, CMacroExpansionTaskStack* tasks)
{
    CMacroExpansionContext* child = 0;
    bool more = true;
    while (!child && more)
    {
        CMacroDefinition const* definition = &continuation->macro->definition;
        if (continuation->argument_index >= continuation->argument_count)
        {
            // Every argument the definition always substitutes is resolved. If
            // the variable argument leaves `__VA_OPT__` content standing, a
            // second walk prescans the parameters only that content uses.
            u32 variable_index = definition->parameter_count - 1;
            more = definition->has_va_opt && !continuation->content_pass && variable_index < continuation->argument_count &&
                   (continuation->arguments[variable_index].expanded_token_count & C_MACRO_ARGUMENT_COUNT_MASK) != 0;
            continuation->content_pass = more;
            continuation->argument_index = 0;
            continue;
        }
        CMacroArgument* argument = continuation->arguments + continuation->argument_index;
        bool content_only = definition->has_va_opt && definition->parameter_expand_count[continuation->argument_index] != 0 &&
                            definition->parameter_expand_count[definition->parameter_count + continuation->argument_index] == 0;
        if (definition->has_va_opt && content_only != continuation->content_pass)
        {
            // Resolved by the other walk; until then the raw tokens stand in,
            // and an absent content never reads them.
            if (!continuation->content_pass)
            {
                argument->expanded_tokens = argument->tokens;
                argument->expanded_token_count = argument->token_count & C_MACRO_ARGUMENT_COUNT_MASK;
            }
            continuation->argument_index += 1;
            continue;
        }
        if (definition->header_query && argument->token_count >= 3 &&
            c_token_is_punctuator(&argument->tokens[0].token, C_PUNCTUATOR_LESS) &&
            c_token_is_punctuator(&argument->tokens[argument->token_count - 1].token, C_PUNCTUATOR_GREATER))
        {
            // Header names are recognized before ordinary argument prescan.
            // Keep this on the builtin's continuation so aliases encountered
            // during rescanning receive the same literal-operand policy.
            bool literal_header = !argument->tokens[0].foreign && !argument->tokens[argument->token_count - 1].foreign;
            for (u64 token_index = 0; token_index < argument->token_count; token_index += 1)
            {
                // Punctuators do not expand, so the endpoint bits also carry
                // literal provenance through the builtin's replacement. A
                // wrapper may already have removed an empty macro argument;
                // its surviving offsets cannot prove the original span.
                if (literal_header || (token_index && token_index + 1 < argument->token_count))
                {
                    argument->tokens[token_index].no_expand = true;
                }
            }
        }
        // Target-OS queries compare these spellings, not their GNU macro values.
        if (definition->target_os_query && (argument->token_count & C_MACRO_ARGUMENT_COUNT_MASK) == 1 &&
            argument->tokens[0].token.kind == C_TOKEN_IDENTIFIER &&
            (c_token_spelling_equal(base, argument->tokens[0].token, S8("linux")) ||
             c_token_spelling_equal(base, argument->tokens[0].token, S8("unix"))))
        {
            argument->tokens[0].no_expand = true;
        }
        bool used_expanded = definition->pragma_like || definition->parameter_expand_count[continuation->argument_index] != 0;
        bool needs_expansion = false;
        for (u64 token_index = 0; used_expanded && token_index < argument->token_count && !needs_expansion; token_index += 1)
        {
            CPpToken token = argument->tokens[token_index];
            if (token.token.kind == C_TOKEN_IDENTIFIER && !token.no_expand)
            {
                CMacro* macro = c_macro_find_token(first_macro, symbols, base, &token.token);
                needs_expansion = macro && macro->definition.defined;
            }
        }
        if (needs_expansion)
        {
            child = arena_allocate(storage->expansion_arena, CMacroExpansionContext, 1);
            *child = (CMacroExpansionContext){
                .parent = continuation->parent,
                .continuation = continuation,
                .task_base = tasks->count,
            };
            c_macro_expansion_tasks_push(arena, tasks, argument->tokens, argument->token_count, 0, 0);
            c_macro_continuation_release_raw(storage, continuation);
        }
        else
        {
            argument->expanded_tokens = argument->tokens;
            argument->expanded_token_count = argument->token_count & C_MACRO_ARGUMENT_COUNT_MASK;
            continuation->argument_index += 1;
        }
    }
    return child;
}

// The paste-free, stringify-free replacement of a non-builtin macro written
// straight into its task batch: the same rows the plain branch of
// c_macro_replacement_tokens stages, minus the staging array and its copy.
// The batch size is exact (plain_count plus each parameter's ordinary-use count
// times its expanded count), so one reservation precedes every write; the ENABLE
// marker takes the batch base and the substituted list is written downward
// from the batch end, the reverse order c_macro_expansion_tasks_push stores.
// Sources are the definition and argument arrays, never task storage, so
// nothing here reads rows the reservation may have moved.
BUSTER_C_INTERNAL void c_macro_produce_plain_tasks(Arena* arena, CMacroExpansionTaskStack* tasks, CMacro* macro, CMacroDefinition const* definition, CMacroArgument const* arguments,
                                                    CPpToken invocation, bool* trailing_space_out)
{
    u32 stamp = invocation.stamp;
    u8 const* definition_spaces = definition->replacement_space;
    u32 const* parameter_indices = definition->parameter_index;
    u32 parameter_count = arguments ? definition->parameter_count : 0;
    u64 token_count = definition->plain_count;
    for (u32 parameter_index = 0; parameter_index < parameter_count; parameter_index += 1)
    {
        token_count += (u64)definition->parameter_expand_count[parameter_index] * arguments[parameter_index].expanded_token_count;
    }
    c_macro_expansion_tasks_reserve(arena, tasks, token_count, true);
    CMacroExpansionTask* batch = tasks->data + tasks->count;
    batch[0] = (CMacroExpansionTask){.macro = macro, .kind = C_MACRO_EXPANSION_ENABLE, .generation = definition->generation};
    CMacroExpansionTask* cursor = batch + 1 + token_count;
    bool pending_space = false;
    bool first_token = true;
    for (u32 replacement_index = 0; replacement_index < definition->replacement_count; replacement_index += 1)
    {
        bool replacement_space = replacement_index ? !definition_spaces || definition_spaces[replacement_index] != 0 : invocation.preceded_by_space;
        u32 parameter_index = parameter_indices[replacement_index];
        if (parameter_index == C_MACRO_PARAMETER_NONE || !arguments)
        {
            replacement_space = first_token ? invocation.preceded_by_space : replacement_space || pending_space;
            pending_space = false;
            first_token = false;
            cursor -= 1;
            *cursor = (CMacroExpansionTask){
                .token = {
                    .token = definition->replacement[replacement_index],
                    .stamp = stamp & C_PP_STAMP_MASK,
                    .foreign = true,
                    .preceded_by_space = replacement_space,
                },
                .kind = C_MACRO_EXPANSION_TOKEN,
            };
        }
        else
        {
            CMacroArgument argument = arguments[parameter_index];
            if (!argument.expanded_token_count)
            {
                pending_space = pending_space || replacement_space;
            }
            for (u64 argument_index = 0; argument_index < argument.expanded_token_count; argument_index += 1)
            {
                CPpToken argument_token = argument.expanded_tokens[argument_index];
                argument_token.stamp = stamp & C_PP_STAMP_MASK;
                argument_token.foreign = true;
                if (!argument_index)
                {
                    argument_token.preceded_by_space = first_token ? invocation.preceded_by_space : replacement_space || pending_space;
                    pending_space = false;
                    first_token = false;
                }
                cursor -= 1;
                *cursor = (CMacroExpansionTask){.token = argument_token, .kind = C_MACRO_EXPANSION_TOKEN};
            }
        }
    }
    BUSTER_ASSERT(cursor == batch + 1);
    tasks->count += 1 + token_count;
    *trailing_space_out = pending_space;
}

// A removed token or trailing empty parameter has no output token to carry
// its whitespace. Forward it only within this rescan: an argument's trailing
// whitespace is discarded when its expanded tokens are substituted later.
BUSTER_C_INTERNAL void c_macro_forward_space(CMacroExpansionTaskStack* tasks, u64 pending_count, u64 task_base)
{
    for (u64 index = pending_count; index > task_base;)
    {
        index -= 1;
        if (tasks->data[index].kind == C_MACRO_EXPANSION_TOKEN)
        {
            tasks->data[index].token.preceded_by_space = true;
            break;
        }
    }
}

// Materialize a resolved invocation into `target`, the context its
// replacement rescans in: the replacement tokens are pushed under the
// macro's disabled bit, or, for a pragma-like macro, the operand becomes one
// pragma marker token in the output. False when the replacement list itself
// is invalid (an edge `##`, a paste that forms no token), diagnosed there.
// A pragma-like definition has no replacement list to build; a plain one is
// produced into its batch directly; builtins, `#` and `##` stage first.
BUSTER_C_INTERNAL bool c_macro_materialize(Arena* arena, Arena* scratch, CSpellingSpace* space, CMacro* first_macro, CMacro* macro, CMacroDefinition const* definition, CMacroArgument* arguments,
                                            u32 argument_count, CPpToken invocation, CPpStampTable const* stamps, CPreprocessResult* result,
                                            CMacroExpansionTaskStack* tasks, u64 task_base)
{
    bool ok = true;
    u64 pending_count = tasks->count;
    bool trailing_space = false;
    if (definition->pragma_like)
    {
        if (argument_count == 1)
        {
            CPpToken pragma = c_macro_pragma_token(space, macro, arguments[0], invocation);
            // Argument prescan retains the marker. The same task path executes
            // direct and substituted markers once they reach the outer rescan.
            macro->disabled = macro->disabled || definition->generation == macro->definition.generation;
            c_macro_expansion_tasks_push(arena, tasks, &pragma, 1, macro, definition->generation);
        }
    }
    else if (!definition->builtin && !definition->has_paste && !definition->has_stringify && !definition->has_va_opt)
    {
        macro->disabled = macro->disabled || definition->generation == macro->definition.generation;
        c_macro_produce_plain_tasks(arena, tasks, macro, definition, arguments, invocation, &trailing_space);
    }
    else
    {
        CPpToken* replacement_tokens = 0;
        u32 replacement_count = 0;
        ok = c_macro_replacement_tokens(arena, scratch, space, first_macro, macro, definition, arguments, invocation, stamps, result, &replacement_tokens, &replacement_count, &trailing_space);
        if (ok)
        {
            macro->disabled = macro->disabled || definition->generation == macro->definition.generation;
            c_macro_expansion_tasks_push(arena, tasks, replacement_tokens, replacement_count, macro, definition->generation);
        }
    }
    bool empty_replacement = tasks->count == pending_count + 1 && tasks->data[pending_count].kind == C_MACRO_EXPANSION_ENABLE;
    if (ok && (trailing_space || (empty_replacement && invocation.preceded_by_space)))
    {
        c_macro_forward_space(tasks, pending_count, task_base);
    }
    return ok;
}

// `frame` and `file` locate the input's unstamped tokens on demand into
// `stamps` (see CPpToken.stamp); null when every input token is stamped
// already.
BUSTER_C_INTERNAL bool c_preprocess_expand(Arena* arena, CMacroExpansionStorage* storage, CSpellingSpace* space, CSymbolTable* symbols, CMacro* first_macro,
                                             struct CPreprocessSourceFrame* frame, u32 file, CPpStampTable* stamps, CPpToken* input, u32 input_count,
                                             CPreprocessTokenNode** first_output, CPreprocessTokenNode** last_output, u64* output_count, u32 expansion_limit,
                                             CPreprocessResult* result, CPreprocessPragmaContext* pragma_context)
{
    CMacroExpansionContext* context = arena_allocate(arena, CMacroExpansionContext, 1);
    *context = (CMacroExpansionContext){
        .first_output = *first_output,
        .last_output = *last_output,
        .output_count = *output_count,
    };
    CMacroExpansionTask local_tasks[C_MACRO_EXPANSION_LOCAL_TASK_CAPACITY];
    CMacroExpansionTaskStack tasks = {.data = local_tasks, .capacity = BUSTER_ARRAY_LENGTH(local_tasks)};
    c_macro_expansion_tasks_push(arena, &tasks, input, input_count, 0, 0);
    u32 expansion_count = 0;
    bool ok = true;
    bool done = false;
    while (!done)
    {
        if (tasks.count == context->task_base)
        {
            CMacroExpansionContinuation* continuation = context->continuation;
            if (!continuation)
            {
                *first_output = context->first_output;
                *last_output = context->last_output;
                *output_count = context->output_count;
                done = true;
            }
            else
            {
                CMacroArgument* argument = continuation->arguments + continuation->argument_index;
                // The context's output run is the argument's expansion as it
                // stands; it stays live until the invocation is released.
                argument->expanded_tokens = context->output_tokens;
                argument->expanded_token_count = context->output_count & C_MACRO_ARGUMENT_COUNT_MASK;
                continuation->argument_index += 1;
                CMacroExpansionContext* child = c_macro_continuation_advance(arena, storage, first_macro, symbols, space->base, continuation, &tasks);
                if (child)
                {
                    context = child;
                }
                else
                {
                    context = continuation->parent;
                    CMacroExpansionMark mark = continuation->mark;
                    ok = c_macro_materialize(arena, storage->expansion_arena, space, first_macro, continuation->macro,
                                             &continuation->macro->definition,
                                             continuation->arguments, continuation->argument_count,
                                             continuation->invocation, stamps, result, &tasks, context->task_base);
                    // The replacement is on the task stack by value; the
                    // arguments, their expansions and this continuation are dead.
                    c_macro_expansion_release(storage, mark);
                    done = !ok;
                }
            }
        }
        else
        {
            CMacroExpansionTask task = tasks.data[--tasks.count];
            if (task.kind == C_MACRO_EXPANSION_ENABLE)
            {
                c_macro_enable_definition(task);
            }
            else
            {
                CPpToken token = task.token;
                if (token.token.kind == C_TOKEN_PRAGMA && !context->parent &&
                    c_preprocess_expansion_pragma(pragma_context, space->base, token, stamps, &tasks))
                {
                    if (token.preceded_by_space)
                    {
                        c_macro_forward_space(&tasks, tasks.count, context->task_base);
                    }
                    continue;
                }
                CMacro* macro = token.token.kind == C_TOKEN_IDENTIFIER && !token.no_expand
                                    ? c_macro_find_token(first_macro, symbols, space->base, &token.token)
                                    : 0;
                if (!macro || !macro->definition.defined || macro->disabled)
                {
                    // The disabled bit is cleared as soon as this rescan
                    // ends, so a name it refused here would expand on any
                    // later rescan the token survives into -- the one an
                    // argument's expanded tokens get in the parent context.
                    // Paint the refusal onto the token instead.
                    token.no_expand = token.no_expand || (macro && macro->definition.defined && macro->disabled);
                    if (c_macro_is_va_opt(token.token))
                    {
                        // Every admitted replacement list consumes its
                        // `__VA_OPT__`, so one reaching the output was written
                        // outside a variadic macro's replacement list.
                        if (!token.foreign && !token.stamp)
                        {
                            token.stamp = c_pp_stamp_push(stamps, c_preprocess_recover_location(frame, file, token.token)) & C_PP_STAMP_MASK;
                        }
                        c_preprocess_diagnostic_push(arena, result, c_pp_stamp_location(stamps, token.stamp), C_DIAGNOSTIC_INVALID_MACRO_INVOCATION,
                                                     S8("'__VA_OPT__' can only appear in the replacement list of a variadic macro"));
                    }
                    c_macro_context_output_push(arena, storage->expansion_arena, context, token);
                }
                else
                {
                    // The invocation's location: the stamp its replacement
                    // carries, and what its diagnostics point at.
                    if (!token.foreign && !token.stamp)
                    {
                        IR_DIAGNOSTIC_CENSUS_RECORD(C_STAMP_LOCATIONS, 1);
                        token.stamp = c_pp_stamp_push(stamps, c_preprocess_recover_location(frame, file, token.token)) & C_PP_STAMP_MASK;
                    }
                    CMacroArgument* arguments = 0;
                    u32 argument_count = 0;
                    bool invoked = true;
                    // Whether this invocation allocates transient storage: it
                    // collects arguments or stages a replacement list.
                    CMacroDefinition const* definition = &macro->definition;
                    bool scoped = definition->function_like || definition->builtin || definition->has_paste || definition->has_stringify || definition->has_va_opt;
                    bool deferred = false;
                    CMacroExpansionMark mark = {0};
                    if (scoped)
                    {
                        c_macro_expansion_storage_ensure(storage);
                        mark = c_macro_expansion_mark(storage);
                    }
                    if (macro->definition.function_like)
                    {
                        invoked = c_macro_invocation_arguments(arena, storage, space->base, symbols, first_macro, &tasks, context->task_base, macro, c_pp_stamp_location(stamps, token.stamp), &arguments,
                                                               &argument_count, result);
                        if (!invoked)
                        {
                            // Nothing was collected. The mark is dropped before
                            // the push so the token's slot is not rewound with it.
                            scoped = false;
                            c_macro_context_output_push(arena, storage->expansion_arena, context, token);
                        }
                        else if (argument_count != macro->definition.parameter_count)
                        {
                            // Diagnosed by the collection; the invocation is dropped.
                            invoked = false;
                        }
                    }
                    if (invoked)
                    {
                        expansion_count += 1;
                        result->detail->preprocessed.expansions += 1;
                        if (expansion_count > expansion_limit)
                        {
                            c_preprocess_diagnostic_push(arena, result, c_pp_stamp_location(stamps, token.stamp), C_DIAGNOSTIC_MACRO_EXPANSION_LIMIT,
                                                         S8("macro expansion limit exceeded"));
                            ok = false;
                            done = true;
                        }
                        else if (macro->definition.function_like && argument_count)
                        {
                            CPpToken* raw_tokens = arguments[0].tokens;
                            CMacroArgument const* last_argument = arguments + argument_count - 1;
                            CMacroExpansionContinuation* continuation = arena_allocate(storage->expansion_arena, CMacroExpansionContinuation, 1);
                            *continuation = (CMacroExpansionContinuation){
                                .parent = context,
                                .macro = macro,
                                .arguments = arguments,
                                .invocation = token,
                                .mark = mark,
                                .raw_tokens = raw_tokens,
                                .raw_count = raw_tokens ? (u64)((last_argument->tokens + last_argument->token_count) - raw_tokens) : 0,
                                .argument_count = argument_count,
                            };
                            CMacroExpansionContext* child = c_macro_continuation_advance(arena, storage, first_macro, symbols, space->base, continuation, &tasks);
                            if (child)
                            {
                                context = child;
                                deferred = true;
                            }
                            else
                            {
                                ok = c_macro_materialize(arena, storage->expansion_arena, space, first_macro, macro, &macro->definition, arguments, argument_count, token, stamps, result, &tasks, context->task_base);
                                done = !ok;
                            }
                        }
                        else
                        {
                            ok = c_macro_materialize(arena, storage->expansion_arena, space, first_macro, macro, &macro->definition, arguments, argument_count, token, stamps, result, &tasks, context->task_base);
                            done = !ok;
                        }
                    }
                    if (scoped && !deferred)
                    {
                        c_macro_expansion_release(storage, mark);
                    }
                }
            }
        }
    }
    return ok;
}

BUSTER_C_SHARED u32 c_conditional_precedence(CConditionalOperator operation)
{
    switch (operation)
    {
    case C_CONDITIONAL_COMMA:
        return 0;
    case C_CONDITIONAL_QUESTION:
    case C_CONDITIONAL_SELECT:
        return 1;
    case C_CONDITIONAL_LOGICAL_OR:
        return 2;
    case C_CONDITIONAL_LOGICAL_AND:
        return 3;
    case C_CONDITIONAL_BITWISE_OR:
        return 4;
    case C_CONDITIONAL_BITWISE_XOR:
        return 5;
    case C_CONDITIONAL_BITWISE_AND:
        return 6;
    case C_CONDITIONAL_EQUAL:
    case C_CONDITIONAL_NOT_EQUAL:
        return 7;
    case C_CONDITIONAL_LESS:
    case C_CONDITIONAL_LESS_EQUAL:
    case C_CONDITIONAL_GREATER:
    case C_CONDITIONAL_GREATER_EQUAL:
        return 8;
    case C_CONDITIONAL_SHIFT_LEFT:
    case C_CONDITIONAL_SHIFT_RIGHT:
        return 9;
    case C_CONDITIONAL_ADD:
    case C_CONDITIONAL_SUBTRACT:
        return 10;
    case C_CONDITIONAL_MULTIPLY:
    case C_CONDITIONAL_DIVIDE:
    case C_CONDITIONAL_REMAINDER:
        return 11;
    case C_CONDITIONAL_UNARY_PLUS:
    case C_CONDITIONAL_UNARY_MINUS:
    case C_CONDITIONAL_LOGICAL_NOT:
    case C_CONDITIONAL_BITWISE_NOT:
    case C_CONDITIONAL_REAL_PART:
    case C_CONDITIONAL_IMAGINARY_PART:
    case C_CONDITIONAL_ADDRESS_OF:
    case C_CONDITIONAL_DEREFERENCE:
    case C_CONDITIONAL_CAST:
        return 12;
    case C_CONDITIONAL_OPEN:
    case C_CONDITIONAL_INDEX_OPEN:
    case C_CONDITIONAL_OPERATOR_COUNT:
        return 0;
    }
    return 0;
}

BUSTER_C_SHARED bool c_conditional_is_unary(CConditionalOperator operation)
{
    return operation >= C_CONDITIONAL_UNARY_PLUS && operation <= C_CONDITIONAL_CAST;
}

BUSTER_C_SHARED bool c_conditional_operator(CToken token, bool unary, CConditionalOperator* operation)
{
    if (token.kind != C_TOKEN_PUNCTUATOR)
    {
        return false;
    }
    // Punctuator ids and spellings are one-to-one, so matching the id is the
    // spelling compare the previous ladder spelled out.
#define C_CONDITIONAL_MATCH(punctuator, binary_operation, unary_operation)                                                                                     \
    case punctuator:                                                                                                                                           \
    {                                                                                                                                                          \
        *operation = unary ? (unary_operation) : (binary_operation);                                                                                           \
        return *operation != C_CONDITIONAL_OPERATOR_COUNT;                                                                                                     \
    }
    switch (token.punctuator)
    {
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_PLUS, C_CONDITIONAL_ADD, C_CONDITIONAL_UNARY_PLUS)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_MINUS, C_CONDITIONAL_SUBTRACT, C_CONDITIONAL_UNARY_MINUS)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_EXCLAMATION, C_CONDITIONAL_OPERATOR_COUNT, C_CONDITIONAL_LOGICAL_NOT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_TILDE, C_CONDITIONAL_OPERATOR_COUNT, C_CONDITIONAL_BITWISE_NOT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_STAR, C_CONDITIONAL_MULTIPLY, C_CONDITIONAL_DEREFERENCE)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_SLASH, C_CONDITIONAL_DIVIDE, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_PERCENT, C_CONDITIONAL_REMAINDER, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_SHIFT_LEFT, C_CONDITIONAL_SHIFT_LEFT, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_SHIFT_RIGHT, C_CONDITIONAL_SHIFT_RIGHT, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_LESS, C_CONDITIONAL_LESS, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_LESS_EQUAL, C_CONDITIONAL_LESS_EQUAL, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_GREATER, C_CONDITIONAL_GREATER, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_GREATER_EQUAL, C_CONDITIONAL_GREATER_EQUAL, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_EQUAL, C_CONDITIONAL_EQUAL, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_NOT_EQUAL, C_CONDITIONAL_NOT_EQUAL, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_AMPERSAND, C_CONDITIONAL_BITWISE_AND, C_CONDITIONAL_ADDRESS_OF)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_CARET, C_CONDITIONAL_BITWISE_XOR, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_PIPE, C_CONDITIONAL_BITWISE_OR, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_COMMA, C_CONDITIONAL_COMMA, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_AMPERSAND_AMPERSAND, C_CONDITIONAL_LOGICAL_AND, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_PIPE_PIPE, C_CONDITIONAL_LOGICAL_OR, C_CONDITIONAL_OPERATOR_COUNT)
    default:
    {
        break;
    }
    }
#undef C_CONDITIONAL_MATCH
    return false;
}

// Keep integer admission independent of expression evaluation. Preprocessing,
// syntax validation, ordinary lowering and wide-float initialization share this
// bounded reader; type selection and dialect policy remain with their callers.
BUSTER_C_SHARED bool c_number_is_float(String8 spelling)
{
    bool floating = false;
    bool hexadecimal = spelling.length >= 2 && spelling.pointer[0] == '0' && (spelling.pointer[1] == 'x' || spelling.pointer[1] == 'X');
    for (u64 index = 0; !floating && index < spelling.length; index += 1)
    {
        u8 byte = spelling.pointer[index];
        floating = byte == '.' || byte == 'p' || byte == 'P' || (!hexadecimal && (byte == 'e' || byte == 'E'));
    }
    return floating;
}

// Microsoft SDK limits use i8/i16/i32 as well as i64. Keep admission and
// the lowerer's fixed-width type choice on one bounded suffix decoder.
BUSTER_C_SHARED u32 c_integer_msvc_suffix_width(String8 suffix)
{
    u32 width = 0;
    u64 index = suffix.length && (suffix.pointer[0] == 'u' || suffix.pointer[0] == 'U');
    if (index < suffix.length && (suffix.pointer[index] == 'i' || suffix.pointer[index] == 'I'))
    {
        index += 1;
        u64 digits = suffix.length - index;
        if (digits == 1 && suffix.pointer[index] == '8')
        {
            width = 8;
        }
        else if (digits == 2)
        {
            u8 first = suffix.pointer[index];
            u8 second = suffix.pointer[index + 1];
            if (first == '1' && second == '6')
            {
                width = 16;
            }
            else if (first == '3' && second == '2')
            {
                width = 32;
            }
            else if (first == '6' && second == '4')
            {
                width = 64;
            }
        }
    }
    return width;
}

BUSTER_C_SHARED u32 c_integer_msvc_literal_width(String8 spelling, bool* is_unsigned)
{
    u32 width = 0;
    for (u32 length = 2; !width && length <= 3 && length <= spelling.length; length += 1)
    {
        u64 start = spelling.length - length;
        width = c_integer_msvc_suffix_width((String8){.pointer = spelling.pointer + start, .length = length});
        if (width)
        {
            *is_unsigned = start && (spelling.pointer[start - 1] == 'u' || spelling.pointer[start - 1] == 'U');
        }
    }
    return width;
}

BUSTER_C_INTERNAL bool c_integer_suffix_valid(String8 suffix)
{
    bool valid = !suffix.length;
    if (suffix.length)
    {
        bool first_unsigned = suffix.pointer[0] == 'u' || suffix.pointer[0] == 'U';
        bool first_long = suffix.pointer[0] == 'l' || suffix.pointer[0] == 'L';
        if (c_integer_msvc_suffix_width(suffix))
        {
            valid = true;
        }
        else if (suffix.length == 1)
        {
            valid = first_unsigned || first_long;
        }
        else if (suffix.length == 2 || suffix.length == 3)
        {
            bool second_unsigned = suffix.pointer[1] == 'u' || suffix.pointer[1] == 'U';
            bool second_long = suffix.pointer[1] == 'l' || suffix.pointer[1] == 'L';
            bool long_pair = first_long && second_long && suffix.pointer[0] == suffix.pointer[1];
            if (suffix.length == 2)
            {
                valid = long_pair || (first_unsigned && second_long) || (first_long && second_unsigned);
            }
            else
            {
                bool third_unsigned = suffix.pointer[2] == 'u' || suffix.pointer[2] == 'U';
                bool third_long = suffix.pointer[2] == 'l' || suffix.pointer[2] == 'L';
                valid = (first_unsigned && second_long && third_long && suffix.pointer[1] == suffix.pointer[2]) ||
                        (long_pair && third_unsigned);
            }
        }
    }
    return valid;
}

BUSTER_C_INTERNAL u32 c_integer_digit(u8 byte)
{
    u32 digit = UINT32_MAX;
    if (byte >= '0' && byte <= '9')
    {
        digit = (u32)(byte - '0');
    }
    else if (byte >= 'a' && byte <= 'f')
    {
        digit = (u32)(byte - 'a') + 10;
    }
    else if (byte >= 'A' && byte <= 'F')
    {
        digit = (u32)(byte - 'A') + 10;
    }
    return digit;
}

BUSTER_C_SHARED bool c_conditional_number(String8 spelling, u64* value)
{
    C_CENSUS_FACT(INTEGER, spelling.pointer, spelling.length);
    WORK_LEDGER_RECORD(LITERAL_NUMBER_CONVERSIONS, 1);
    u32 base = 10;
    u64 index = 0;
    if (spelling.length >= 2 && spelling.pointer[0] == '0')
    {
        if (spelling.pointer[1] == 'x' || spelling.pointer[1] == 'X')
        {
            base = 16;
            index = 2;
        }
        else if (spelling.pointer[1] == 'b' || spelling.pointer[1] == 'B')
        {
            base = 2;
            index = 2;
        }
        else
        {
            base = 8;
        }
    }
    u64 parsed = 0;
    u64 cutoff = UINT64_MAX / base;
    u32 last_digit = (u32)(UINT64_MAX % base);
    bool any = false;
    bool valid = true;
    while (valid && index < spelling.length)
    {
        u8 byte = spelling.pointer[index];
        if (byte == '\'')
        {
            // A separator joins two digits of this base, never a prefix or
            // suffix. Checking the next byte also rejects consecutive quotes.
            valid = any && index + 1 < spelling.length && c_integer_digit(spelling.pointer[index + 1]) < base;
            index += 1;
        }
        else
        {
            u32 digit = c_integer_digit(byte);
            if (digit >= base)
            {
                break;
            }
            valid = parsed < cutoff || (parsed == cutoff && digit <= last_digit);
            if (valid)
            {
                parsed = parsed * base + digit;
                any = true;
                index += 1;
            }
        }
    }
    valid = valid && any && c_integer_suffix_valid((String8){.pointer = spelling.pointer + index, .length = spelling.length - index});
    if (valid)
    {
        *value = parsed;
    }
    return valid;
}

// C11 6.10.1p4: conditional-inclusion arithmetic uses intmax_t or
// uintmax_t. Keep signedness and deferred arithmetic faults in the existing
// one-byte-per-value sidecar. Reductions still validate every operand's syntax,
// but &&, || and ?: discard faults from operands they do not evaluate.
// A fault always follows an evaluated condition/left operand, even when its
// placeholder value makes a later logical expression appear to succeed.
enum
{
    C_CONDITIONAL_VALUE_UNSIGNED = 1,
    C_CONDITIONAL_VALUE_FAULT = 2,
};

BUSTER_C_SHARED bool c_integer_operation(CConditionalOperator operation, bool is_signed, IrUnaryOperation* unary_out, IrBinaryOperation* binary_out)
{
    IrUnaryOperation unary = IR_UNARY_COUNT;
    IrBinaryOperation binary = IR_BINARY_COUNT;
    switch (operation)
    {
    case C_CONDITIONAL_UNARY_MINUS: unary = IR_UNARY_INTEGER_NEGATE; break;
    case C_CONDITIONAL_BITWISE_NOT: unary = IR_UNARY_INTEGER_BITWISE_NOT; break;
    case C_CONDITIONAL_LOGICAL_NOT: unary = IR_UNARY_BOOLEAN_NOT; break;
    case C_CONDITIONAL_MULTIPLY: binary = IR_BINARY_INTEGER_MULTIPLY; break;
    case C_CONDITIONAL_DIVIDE: binary = is_signed ? IR_BINARY_SIGNED_DIVIDE : IR_BINARY_UNSIGNED_DIVIDE; break;
    case C_CONDITIONAL_REMAINDER: binary = is_signed ? IR_BINARY_SIGNED_REMAINDER : IR_BINARY_UNSIGNED_REMAINDER; break;
    case C_CONDITIONAL_ADD: binary = IR_BINARY_INTEGER_ADD; break;
    case C_CONDITIONAL_SUBTRACT: binary = IR_BINARY_INTEGER_SUBTRACT; break;
    case C_CONDITIONAL_SHIFT_LEFT: binary = IR_BINARY_SHIFT_LEFT; break;
    case C_CONDITIONAL_SHIFT_RIGHT: binary = is_signed ? IR_BINARY_SIGNED_SHIFT_RIGHT : IR_BINARY_UNSIGNED_SHIFT_RIGHT; break;
    case C_CONDITIONAL_BITWISE_AND: binary = IR_BINARY_INTEGER_BITWISE_AND; break;
    case C_CONDITIONAL_BITWISE_XOR: binary = IR_BINARY_INTEGER_BITWISE_XOR; break;
    case C_CONDITIONAL_BITWISE_OR: binary = IR_BINARY_INTEGER_BITWISE_OR; break;
    case C_CONDITIONAL_LESS: binary = is_signed ? IR_BINARY_SIGNED_LESS : IR_BINARY_UNSIGNED_LESS; break;
    case C_CONDITIONAL_LESS_EQUAL: binary = is_signed ? IR_BINARY_SIGNED_LESS_EQUAL : IR_BINARY_UNSIGNED_LESS_EQUAL; break;
    case C_CONDITIONAL_GREATER: binary = is_signed ? IR_BINARY_SIGNED_GREATER : IR_BINARY_UNSIGNED_GREATER; break;
    case C_CONDITIONAL_GREATER_EQUAL: binary = is_signed ? IR_BINARY_SIGNED_GREATER_EQUAL : IR_BINARY_UNSIGNED_GREATER_EQUAL; break;
    case C_CONDITIONAL_EQUAL: binary = IR_BINARY_INTEGER_EQUAL; break;
    case C_CONDITIONAL_NOT_EQUAL: binary = IR_BINARY_INTEGER_NOT_EQUAL; break;
    default: break;
    }
    *unary_out = unary;
    *binary_out = binary;
    return unary != IR_UNARY_COUNT || binary != IR_BINARY_COUNT;
}

BUSTER_C_SHARED CIntegerConstantResult c_integer_constant_binary(CConditionalOperator operation, IrInteger left, IrInteger right, u32 width,
                                                                bool is_signed, u32 count_width)
{
    CIntegerConstantResult result = {.faults = IR_INTEGER_FAULT_UNSUPPORTED};
    IrUnaryOperation unary = IR_UNARY_COUNT;
    IrBinaryOperation binary = IR_BINARY_COUNT;
    if (c_integer_operation(operation, is_signed, &unary, &binary) && binary != IR_BINARY_COUNT)
    {
        bool shift = binary == IR_BINARY_SHIFT_LEFT || binary == IR_BINARY_SIGNED_SHIFT_RIGHT || binary == IR_BINARY_UNSIGNED_SHIFT_RIGHT;
        IrIntegerResult computed = ir_integer_binary(binary, left, right, width, shift ? count_width : width);
        result.bits = computed.bits;
        result.faults = computed.faults;
        result.comparison = ir_integer_binary_is_comparison(binary);
    }
    result.constant = !(result.faults & (IR_INTEGER_FAULT_UNSUPPORTED | IR_INTEGER_FAULT_DIVIDE_BY_ZERO | IR_INTEGER_FAULT_SHIFT_COUNT));
    return result;
}

BUSTER_C_SHARED CIntegerConstantResult c_integer_constant_unary(CConditionalOperator operation, IrInteger operand, u32 width)
{
    CIntegerConstantResult result = {.bits = ir_integer_mask(operand, width)};
    IrUnaryOperation unary = IR_UNARY_COUNT;
    IrBinaryOperation binary = IR_BINARY_COUNT;
    if (c_integer_operation(operation, true, &unary, &binary) && unary != IR_UNARY_COUNT)
    {
        IrIntegerResult computed = ir_integer_unary(unary, operand, width);
        result.bits = computed.bits;
        result.faults = computed.faults;
        result.comparison = unary == IR_UNARY_BOOLEAN_NOT;
    }
    else if (operation != C_CONDITIONAL_UNARY_PLUS)
    {
        result.faults = IR_INTEGER_FAULT_UNSUPPORTED;
    }
    result.constant = !(result.faults & IR_INTEGER_FAULT_UNSUPPORTED);
    return result;
}

BUSTER_C_INTERNAL bool c_conditional_apply(CConditionalOperator operation, u64* values, u8* value_flags, u32* value_count)
{
    IR_SEMANTIC_RECORD(PREPROCESSOR_NODES, 1);
    bool valid = true;
    if (c_conditional_is_unary(operation))
    {
        if (!*value_count)
        {
            valid = false;
        }
        else
        {
            u32 index = *value_count - 1;
            u64* value = values + index;
            CIntegerConstantResult computed = c_integer_constant_unary(operation, (IrInteger){.low = *value}, 64);
            valid = computed.constant;
            *value = computed.bits.low;
            if (computed.comparison)
            {
                value_flags[index] &= C_CONDITIONAL_VALUE_FAULT;
            }
        }
    }
    else if (operation == C_CONDITIONAL_SELECT)
    {
        if (*value_count < 3)
        {
            valid = false;
        }
        else
        {
            u64 false_value = values[--*value_count];
            u8 false_flags = value_flags[*value_count];
            u64 true_value = values[--*value_count];
            u8 true_flags = value_flags[*value_count];
            u32 index = *value_count - 1;
            u64* condition = values + index;
            u8 selected_flags = *condition ? true_flags : false_flags;
            // Both arms determine the common type, but only the selected
            // arm and the always-evaluated condition contribute faults.
            value_flags[index] = (u8)(((true_flags | false_flags) & C_CONDITIONAL_VALUE_UNSIGNED) |
                                     ((value_flags[index] | selected_flags) & C_CONDITIONAL_VALUE_FAULT));
            *condition = *condition ? true_value : false_value;
        }
    }
    else if (*value_count < 2)
    {
        valid = false;
    }
    else
    {
        u64 right = values[--*value_count];
        u8 right_flags = value_flags[*value_count];
        u32 index = *value_count - 1;
        u64* left = values + index;
        u8 left_flags = value_flags[index];
        u8 flags = (u8)(left_flags | right_flags);
        bool mixed_unsigned = (flags & C_CONDITIONAL_VALUE_UNSIGNED) != 0;
        bool shift = operation == C_CONDITIONAL_SHIFT_LEFT || operation == C_CONDITIONAL_SHIFT_RIGHT;
        bool division = operation == C_CONDITIONAL_DIVIDE || operation == C_CONDITIONAL_REMAINDER;
        if (operation == C_CONDITIONAL_COMMA)
        {
            // Both operands are evaluated; the value and type are the right
            // operand's, and a deferred fault in either one survives.
            flags = (u8)((right_flags & C_CONDITIONAL_VALUE_UNSIGNED) | (flags & C_CONDITIONAL_VALUE_FAULT));
            *left = right;
        }
        else if (operation == C_CONDITIONAL_LOGICAL_AND || operation == C_CONDITIONAL_LOGICAL_OR)
        {
            // Only an operand the operator evaluates contributes its fault.
            bool conjunction = operation == C_CONDITIONAL_LOGICAL_AND;
            flags = (u8)((left_flags | ((*left != 0) == conjunction ? right_flags : 0)) & C_CONDITIONAL_VALUE_FAULT);
            *left = conjunction ? *left && right : *left || right;
        }
        else if (division && (flags & C_CONDITIONAL_VALUE_FAULT))
        {
            // Never divide under an operand's fault, even in a subtree an
            // enclosing short circuit will discard.
            *left = 0;
        }
        else
        {
            // Preprocessing arithmetic is C arithmetic at intmax_t/uintmax_t
            // (C17 6.10.1p4): 64 bits, unsigned when either operand is, and a
            // shift takes the left operand's signedness with its count read
            // as the unsigned 64-bit value it carries.
            bool is_signed = shift ? !(left_flags & C_CONDITIONAL_VALUE_UNSIGNED) : !mixed_unsigned;
            CIntegerConstantResult computed = c_integer_constant_binary(operation, (IrInteger){.low = *left}, (IrInteger){.low = right}, 64, is_signed, 64);
            valid = !(computed.faults & IR_INTEGER_FAULT_UNSUPPORTED);
            if (division && (computed.faults & (IR_INTEGER_FAULT_DIVIDE_BY_ZERO | IR_INTEGER_FAULT_SIGNED_OVERFLOW)))
            {
                // A division by zero and INT64_MIN / -1 (and their remainders)
                // are this evaluator's deferred faults.
                flags |= C_CONDITIONAL_VALUE_FAULT;
                *left = 0;
            }
            else if (shift && (computed.faults & IR_INTEGER_FAULT_SHIFT_COUNT))
            {
                // A count of 64 or more shifts every bit out: zero, or the
                // sign for an arithmetic right shift.
                *left = operation == C_CONDITIONAL_SHIFT_RIGHT && is_signed ? (u64)((s64)*left >> 63) : 0;
            }
            else
            {
                *left = computed.bits.low;
            }
            if (shift)
            {
                flags = (u8)((flags & C_CONDITIONAL_VALUE_FAULT) | (left_flags & C_CONDITIONAL_VALUE_UNSIGNED));
            }
            else if (computed.comparison)
            {
                flags &= C_CONDITIONAL_VALUE_FAULT;
            }
        }
        value_flags[index] = flags;
    }
    return valid;
}

typedef enum CIncludeSearchKind
{
    C_INCLUDE_SEARCH_NONE,
    C_INCLUDE_SEARCH_QUOTED,
    C_INCLUDE_SEARCH_INCLUDE_PATH,
    C_INCLUDE_SEARCH_BUILTIN,
    C_INCLUDE_SEARCH_SYSTEM_PATH,
} CIncludeSearchKind;

typedef struct CIncludeSearchOrigin CIncludeSearchOrigin;
struct CIncludeSearchOrigin
{
    CIncludeSearchKind kind;
    u32 index;
};

// One translation unit's record of probing `directory`/`name`. Repeated
// inclusions and __has_include queries consult it instead of the file system,
// so each missing path is opened at most once per TU, and a hit is reopened
// only by an inclusion that lexes it. A hit keeps the resolved spelling and the
// identity captured from the descriptor that probed it.
typedef struct CIncludeProbe CIncludeProbe;
struct CIncludeProbe
{
    String8 directory;
    String8 name;
    String8 path;
    CIncludeFileIdentity identity;
    u64 hash;
    bool found;
};

typedef struct CIncludeProbeTable CIncludeProbeTable;
struct CIncludeProbeTable
{
    Arena* arena;
    CIncludeProbe* entries;
    u32 count;
    u32 capacity;
};

#define C_INCLUDE_PROBE_INITIAL_CAPACITY 128

BUSTER_C_INTERNAL bool c_include_resolve(Arena* arena, CIncludeProbeTable* probes, CPreprocessOptions options, String8 including_path, String8 name,
                                           bool quoted, String8* path_out, String8* source_out, FileMapRead* map_out, CIncludeProbe** cached_out,
                                           CIncludeSearchOrigin* origin_out);

BUSTER_C_INTERNAL bool c_include_resolve_next(Arena* arena, CIncludeProbeTable* probes, CPreprocessOptions options, String8 name, String8* path_out,
                                                String8* source_out, FileMapRead* map_out, CIncludeProbe** cached_out, CIncludeSearchOrigin origin,
                                                CIncludeSearchOrigin* origin_out);

BUSTER_C_INTERNAL bool c_include_name(Arena* arena, char8 const* base, CToken* tokens, u32 token_count, bool preserve_characters,
                                        String8* name_out, bool* quoted_out);
BUSTER_C_INTERNAL u32 c_include_name_token_count(CToken* tokens, u32 token_count);

BUSTER_C_INTERNAL bool c_conditional_builtin_supported(String8 name, CpuArch cpu_arch, OperatingSystem os)
{
    static char const* supported[] = {
        "__builtin___clear_cache", "__builtin_abs",
        "__builtin_labs",          "__builtin_llabs",
        "__builtin_acos",
        "__builtin_acosf",         "__builtin_ceil",
        "__builtin_ceilf",         "__builtin_clrsb",
        "__builtin_clrsbl",        "__builtin_clrsbll",
        "__builtin_clz",
        "__builtin_clzl",          "__builtin_clzll",
        "__builtin_cos",           "__builtin_cosf",
        "__builtin_ctz",           "__builtin_ctzl",
        "__builtin_ctzll",         "__builtin_debugtrap",
        "__builtin_ffs",
        "__builtin_ffsl",
        "__builtin_ffsll",
        "__builtin_sadd_overflow",
        "__builtin_saddl_overflow",
        "__builtin_saddll_overflow",
        "__builtin_uadd_overflow",
        "__builtin_uaddl_overflow",
        "__builtin_uaddll_overflow",
        "__builtin_ssub_overflow",
        "__builtin_ssubl_overflow",
        "__builtin_ssubll_overflow",
        "__builtin_usub_overflow",
        "__builtin_usubl_overflow",
        "__builtin_usubll_overflow",
        "__builtin_smul_overflow",
        "__builtin_smull_overflow",
        "__builtin_smulll_overflow",
        "__builtin_umul_overflow",
        "__builtin_umull_overflow",
        "__builtin_umulll_overflow",
        "__builtin_popcount",      "__builtin_popcountl",
        "__builtin_popcountll",
        "__builtin_parity",        "__builtin_parityl",
        "__builtin_parityll",      "__builtin_bswap16",
        "__builtin_bswap32",       "__builtin_bswap64",
        "__builtin_copysign",      "__builtin_copysignf",
        "__builtin_copysignl",
        "__builtin_assume_aligned", "__builtin_choose_expr",
        "__builtin_constant_p",    "__builtin_object_size",
        "__builtin_expect",        "__builtin_expect_with_probability",
        "__builtin_fabs",          "__builtin_fabsf",
        "__builtin_fabsl",
        "__builtin_fmax",          "__builtin_fmaxf",
        "__builtin_fmaxl",         "__builtin_fmin",
        "__builtin_fminf",         "__builtin_fminl",
        "__builtin_powi",          "__builtin_powif",
        "__builtin_powil",
        "__builtin_floor",         "__builtin_floorf",
        "__builtin_fmod",          "__builtin_fmodf",
        "__builtin_pow",           "__builtin_powf",
        "__builtin_prefetch",
        "__builtin_round",         "__builtin_roundf",
        "__builtin_inf",           "__builtin_inff", "__builtin_nan", "__builtin_nanf", "__builtin_huge_val", "__builtin_isnan", "__builtin_isnanf",
        "__builtin_isinf_sign",
        "__builtin_isinf",         "__builtin_isinff", "__builtin_isfinite",
        "__builtin_isnormal",      "__builtin_fpclassify", "__builtin_isgreater", "__builtin_isgreaterequal",
        "__builtin_isless",        "__builtin_islessequal", "__builtin_islessgreater", "__builtin_isunordered",
        "__builtin_signbit",       "__builtin_signbitf", "__builtin_signbitl",
        "__builtin_memcpy",        "__builtin_memmove",
        "__builtin_memset",        "__builtin_memcmp",
        "__builtin___memcpy_chk", "__builtin___memmove_chk", "__builtin___memset_chk",
        "__builtin_sqrt",          "__builtin_sqrtf",
        "__builtin_strchr",        "__builtin_strcmp",
        "__builtin_strcpy",
        "__builtin_strlen",        "__builtin_trap",
        "__builtin_ia32_pause",
        "__builtin_ia32_pslldi128", "__builtin_ia32_psllqi128",
        "__builtin_ia32_psradi128", "__builtin_ia32_psrldi128",
        "__builtin_ia32_psrlqi128",
        "__builtin_types_compatible_p", "__builtin_offsetof",
        "__builtin_unreachable",   "__builtin_frame_address",
        "__builtin_alloca",
        "__builtin_c23_va_start",
        "__builtin_va_arg",        "__builtin_va_copy",
        "__builtin_va_end",        "__builtin_va_start",
        "__is_target_arch",        "__is_target_environment",
        "__is_target_os",          "__is_target_vendor",
    };
    bool result = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(supported) && !result; index += 1)
    {
        u64 length = strlen(supported[index]);
        result = name.length == length && memcmp(name.pointer, supported[index], length) == 0;
    }

    if (!result)
    {
        // These exact-name classes match the implemented complex constructor
        // and c_ir_atomic_builtin_spelling, not arbitrary __atomic_* prefixes.
        // Do not advertise every recognized builtin: other classes include
        // aliases and target intrinsics outside this query's support contract.
        // Native backends implement atomic IR; Wasm64 and eBPF reject it, and
        // eBPF also rejects floating-point operations. Operand type/width
        // restrictions remain the responsibility of semantic lowering.
        CSymbolBuiltin builtin = c_symbol_builtin_from_spelling(name);
        bool native = cpu_arch == CPU_ARCH_X86_64 || cpu_arch == CPU_ARCH_AARCH64;
        // __builtin_return_address reads the frame record of the System V and
        // Darwin native backends; Win64/Windows-AArch64 frames, Wasm64 and
        // eBPF refuse it, so those targets answer 0.
        result = (builtin == C_SYMBOL_BUILTIN_ATOMIC && native) ||
                 // Byte swaps lower through generic shifts on every target; the
                 // rotate builtins stay native-only.
                 (builtin == C_SYMBOL_BUILTIN_INTEGER_TRANSFORM &&
                  (native || string_starts_with_sequence(name, S8("__builtin_bswap")))) ||
                 ((builtin == C_SYMBOL_BUILTIN_VENDOR_TARGET || builtin == C_SYMBOL_BUILTIN_VENDOR_GENERIC) &&
                  c_semantic_vendor_builtin_supported((Target){.cpu_arch = cpu_arch}, name)) ||
                 (builtin == C_SYMBOL_BUILTIN_COMPLEX && (native || cpu_arch == CPU_ARCH_WASM64)) ||
                 // The libm rounding/fma/ldexp calls are floating imports: eBPF has
                 // no floating point, and Wasm64 lowers no long double (#1394).
                 (builtin == C_SYMBOL_BUILTIN_MATH && c_semantic_math_libm_shape(name).arity &&
                  (native || (cpu_arch == CPU_ARCH_WASM64 && c_semantic_math_libm_shape(name).argument_kind != C_TYPE_LONG_DOUBLE))) ||
                 (builtin == C_SYMBOL_BUILTIN_RETURN_ADDRESS && native && os != OPERATING_SYSTEM_WINDOWS);
    }

    return result;
}

// GNU queries use the same spelling predicates/sets as layout and semantic
// lowering, not a list of attributes the parser merely skips (#639, #666).
// The selected target matters: the COFF writer cannot preserve weak
// definitions, and UEFI images refuse lifecycle registrations even though a
// UEFI relocatable object can carry their arrays. Keep those answers false.
// `visibility` is claimed only where an ELF object records it.
BUSTER_C_INTERNAL bool c_conditional_attribute_supported(char8 const* base, CToken token, Target target)
{
    String8 name = c_token_spelling(base, token);
    u64 binding_words = 0;
    if (c_attribute_native_binding_target(target))
    {
        binding_words = C_ATTRIBUTE_WORDS_ALIAS;
        if (target.os != OPERATING_SYSTEM_WINDOWS && target.os != OPERATING_SYSTEM_UEFI)
        {
            binding_words |= C_ATTRIBUTE_WORDS_WEAK;
        }
        if (target.os != OPERATING_SYSTEM_UEFI)
        {
            binding_words |= C_ATTRIBUTE_WORDS_CONSTRUCTOR | C_ATTRIBUTE_WORDS_DESTRUCTOR;
        }
        // Only the ELF writer turns IrSymbol.is_hidden into st_other; Mach-O
        // and COFF objects drop it, so the query stays false there.
        if (target.os != OPERATING_SYSTEM_WINDOWS && target.os != OPERATING_SYSTEM_UEFI && target.os != OPERATING_SYSTEM_MACOS &&
            target.os != OPERATING_SYSTEM_IOS)
        {
            binding_words |= C_ATTRIBUTE_WORDS_VISIBILITY;
        }
    }
    return c_parse_packed_word(name) || c_parse_aligned_attribute_word(name) || c_parse_vector_size_word(name) ||
           c_attribute_noreturn_word(name) || c_token_in_well_known_set(base, token, binding_words | C_ATTRIBUTE_WORDS_RETURNS_TWICE);
}

// `__has_c_attribute` is a different operator over a different namespace, and
// is deliberately not the query above. It asks about C's bracketed attributes,
// answering the standard attribute's version number -- clang 18 answers
// 202003L for `nodiscard` and 201910L for `fallthrough` -- 1 for a supported
// vendor attribute written with its namespace (`gnu::packed`), and 0
// otherwise, including for every GNU attribute named without one: clang
// answers 0 for a bare `packed`, `aligned` and `vector_size`.
//
// This frontend recognizes `[[ ... ]]` in c_parse_c23_attribute_at and steps
// over it; no bracketed attribute changes layout, diagnostics or code
// generation here, and c_parse_layout_attributes only ever reads
// `__attribute__` groups. Nothing is implemented, so the truthful answer is 0
// for every spelling, and a bare GNU name answers 0 here even though the GNU
// query above answers 1 for it. Implementing one of these attributes means
// returning its version number from here, not 1.
BUSTER_C_INTERNAL bool c_conditional_c_attribute_supported(void)
{
    return false;
}

// `__is_target_os` asks about the selected compilation target, and answers the
// spellings clang accepts for it rather than this compiler's internal enum
// names. The alias sets were read back out of clang 18 with `-target` and
// `-E`, which parses the spelling as a triple's OS component and compares it
// with the target's own: a Windows target answers `windows` and `win32`; a
// Darwin target answers `darwin` as well as its own `macos`/`macosx` or `ios`;
// UEFI answers `uefi`.
//
// Android is the case that cannot be answered from the enum name. Its triple
// carries Linux as the OS and Android as the environment, so clang answers
// `linux` for an Android target and never answers `android` at all -- the
// spelling is not an OS. A freestanding target has no triple OS and answers
// nothing (#640).
BUSTER_C_INTERNAL bool c_conditional_target_os_supported(OperatingSystem os, String8 spelling)
{
    bool result;
    switch (os)
    {
    case OPERATING_SYSTEM_LINUX:
    case OPERATING_SYSTEM_ANDROID:
        result = string_equal(spelling, S8("linux"));
        break;
    case OPERATING_SYSTEM_MACOS:
        result = string_equal(spelling, S8("macos")) || string_equal(spelling, S8("macosx")) || string_equal(spelling, S8("darwin"));
        break;
    case OPERATING_SYSTEM_IOS:
        result = string_equal(spelling, S8("ios")) || string_equal(spelling, S8("darwin"));
        break;
    case OPERATING_SYSTEM_WINDOWS:
        result = string_equal(spelling, S8("windows")) || string_equal(spelling, S8("win32"));
        break;
    case OPERATING_SYSTEM_UEFI:
        result = string_equal(spelling, S8("uefi"));
        break;
    case OPERATING_SYSTEM_WASI:
        result = string_equal(spelling, S8("wasip1")) || string_equal(spelling, S8("wasi"));
        break;
    case OPERATING_SYSTEM_FREESTANDING:
    case OPERATING_SYSTEM_COUNT:
    default:
        result = false;
        break;
    }
    return result;
}

BUSTER_C_INTERNAL bool c_conditional_feature_operators(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro* first_macro,
                                                         CPreprocessTokenNode* first, CPreprocessOptions* options, CIncludeProbeTable* probes,
                                                         String8 including_path, CIncludeSearchOrigin including_origin)
{
    char8 const* base = space->base;
    for (CPreprocessTokenNode* node = first; node; node = node->next)
    {
        CToken token = node->token.token;
        if (token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("defined")))
        {
            CPreprocessTokenNode* name = node->next;
            bool parenthesized = name && c_token_is_punctuator(&name->token.token, C_PUNCTUATOR_LEFT_PARENTHESIS);
            name = parenthesized ? name->next : name;
            if (!name || name->token.token.kind != C_TOKEN_IDENTIFIER)
            {
                return false;
            }
            CPreprocessTokenNode* after = name->next;
            if (parenthesized)
            {
                if (!after || !c_token_is_punctuator(&after->token.token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    return false;
                }
                after = after->next;
            }
            CMacro* macro = c_macro_find_token(first_macro, symbols, base, &name->token.token);
            node->token.token.kind = C_TOKEN_PREPROCESSING_NUMBER;
            node->token.token.punctuator = C_PUNCTUATOR_NONE;
            node->token.token.offset = macro && macro->definition.defined ? C_SPELLING_ONE : C_SPELLING_ZERO;
            node->token.token.length = 1;
            node->token.token.symbol = 0;
            node->next = after;
            continue;
        }
        bool has_include = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__has_include"));
        bool has_include_next = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__has_include_next"));
        bool has_builtin = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__has_builtin"));
        bool has_attribute = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__has_attribute"));
        bool has_c_attribute = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__has_c_attribute"));
        bool has_feature =
            token.kind == C_TOKEN_IDENTIFIER && (c_token_spelling_equal(base, token, S8("__has_feature")) || c_token_spelling_equal(base, token, S8("__has_extension")) ||
                                                 c_token_spelling_equal(base, token, S8("__building_module")));
        bool is_target_arch = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__is_target_arch"));
        bool is_target_environment = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__is_target_environment"));
        bool is_target_os = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__is_target_os"));
        bool is_target_vendor = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__is_target_vendor"));
        if (!has_include && !has_include_next && !has_builtin && !has_attribute && !has_c_attribute && !has_feature && !is_target_arch &&
            !is_target_environment && !is_target_os && !is_target_vendor)
        {
            continue;
        }
        CPreprocessTokenNode* open = node->next;
        if (!open || !c_token_is_punctuator(&open->token.token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            return false;
        }
        CPreprocessTokenNode* close = 0;
        CPreprocessTokenNode* scan = open->next;
        u32 depth = 0;
        u32 argument_count = 0;
        for (; scan; scan = scan->next)
        {
            if (c_token_is_punctuator(&scan->token.token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&scan->token.token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                if (!depth)
                {
                    close = scan;
                    break;
                }
                depth -= 1;
            }
            argument_count += 1;
        }
        if (!close)
        {
            return false;
        }
        CToken* arguments = arena_allocate(arena, CToken, argument_count);
        u32 argument_index = 0;
        bool literal_header = open->next && open->next->token.no_expand;
        for (scan = open->next; scan != close; scan = scan->next)
        {
            arguments[argument_index++] = scan->token.token;
            literal_header = literal_header && scan->token.no_expand;
        }
        bool supported = false;
        if (has_include || has_include_next)
        {
            String8 include_name = {0};
            bool quoted = false;
            String8 resolved_path = {0};
            String8 resolved_source = {0};
            supported = c_include_name(arena, base, arguments, argument_count, literal_header, &include_name, &quoted) &&
                        options &&
                        (has_include_next ? c_include_resolve_next(arena, probes, *options, include_name, &resolved_path, &resolved_source, 0, 0,
                                                                   including_origin, 0)
                                          : c_include_resolve(arena, probes, *options, including_path, include_name, quoted, &resolved_path,
                                                              &resolved_source, 0, 0, 0));
        }
        else if ((has_builtin || has_attribute) && argument_count == 1 && arguments[0].kind == C_TOKEN_IDENTIFIER)
        {
            supported = has_builtin ? c_conditional_builtin_supported(c_token_spelling(base, arguments[0]),
                                                                    options ? options->target.cpu_arch : CPU_ARCH_COUNT,
                                                                    options ? options->target.os : OPERATING_SYSTEM_FREESTANDING)
                                    : c_conditional_attribute_supported(base, arguments[0],
                                                                        options ? options->target : target_native);
        }
        else if (has_c_attribute && argument_count)
        {
            // Any non-empty argument shape the balanced scan above accepted,
            // so that the namespaced spelling this operator's own contract
            // admits -- `__has_c_attribute(gnu::packed)` -- answers 0 instead
            // of failing the directive, however the `::` is tokenized. An
            // empty argument list is still malformed and still fails below.
            supported = c_conditional_c_attribute_supported();
        }
        else if ((is_target_arch || is_target_environment || is_target_os || is_target_vendor) && argument_count == 1 &&
                 arguments[0].kind == C_TOKEN_IDENTIFIER && options)
        {
            String8 argument = c_token_spelling(base, arguments[0]);
            supported = is_target_arch        ? (options->target.cpu_arch == CPU_ARCH_AARCH64
                                                      ? string_equal(argument, S8("arm64")) || string_equal(argument, S8("aarch64"))
                                                  : options->target.cpu_arch == CPU_ARCH_WASM32 ? string_equal(argument, S8("wasm32"))
                                                  : options->target.cpu_arch == CPU_ARCH_WASM64 ? string_equal(argument, S8("wasm64"))
                                                  : options->target.cpu_arch == CPU_ARCH_BPFEL
                                                      ? string_equal(argument, S8("bpfel")) || string_equal(argument, S8("bpf")) ||
                                                            string_equal(argument, S8("ebpf"))
                                                      : string_equal(argument, S8("x86_64")))
                        : is_target_os          ? c_conditional_target_os_supported(options->target.os, argument)
                        : is_target_vendor      ? (options->target.os == OPERATING_SYSTEM_MACOS || options->target.os == OPERATING_SYSTEM_IOS) &&
                                                      string_equal(argument, S8("apple"))
                        : is_target_environment ? false
                                                : false;
        }
        else if (!has_feature)
        {
            return false;
        }
        node->token.token.kind = C_TOKEN_PREPROCESSING_NUMBER;
        node->token.token.punctuator = C_PUNCTUATOR_NONE;
        node->token.token.offset = supported ? C_SPELLING_ONE : C_SPELLING_ZERO;
        node->token.token.length = 1;
        node->token.token.symbol = 0;
        node->next = close->next;
    }
    return true;
}

// Parse-side constant queries share the reducer but retain their existing
// semantics; only conditional-inclusion arithmetic widens decoded types.
BUSTER_C_INTERNAL bool c_integer_expression_evaluate_with_features(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro* first_macro,
                                                                     CPpStampTable* stamps, CPpToken* tokens, u32 token_count, u32 expansion_limit,
                                                                     bool preprocessor_arithmetic, CPreprocessResult* result,
                                                                     CPreprocessOptions* options, CIncludeProbeTable* probes, String8 including_path,
                                                                     CIncludeSearchOrigin including_origin, u64* value_out)
{
    IR_SEMANTIC_RECORD(PREPROCESSOR_EVALUATIONS, 1);
    bool valid = true;
    char8 const* base = space->base;
    CPpToken* transformed = arena_allocate(arena, CPpToken, token_count);
    u32 transformed_count = 0;
    for (u32 token_index = 0; valid && token_index < token_count; token_index += 1)
    {
        CPpToken token = tokens[token_index];
        if (token.token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token.token, S8("defined")))
        {
            u32 name_index = token_index + 1;
            bool parenthesized = name_index < token_count && c_token_is_punctuator(&tokens[name_index].token, C_PUNCTUATOR_LEFT_PARENTHESIS);
            name_index += parenthesized;
            if (name_index >= token_count || tokens[name_index].token.kind != C_TOKEN_IDENTIFIER)
            {
                valid = false;
            }
            else
            {
                CMacro* macro = c_macro_find_token(first_macro, symbols, base, &tokens[name_index].token);
                CPpToken replacement = token;
                replacement.token.kind = C_TOKEN_PREPROCESSING_NUMBER;
                replacement.token.punctuator = C_PUNCTUATOR_NONE;
                replacement.token.offset = macro && macro->definition.defined ? C_SPELLING_ONE : C_SPELLING_ZERO;
                replacement.token.length = 1;
                replacement.token.symbol = 0;
                transformed[transformed_count++] = replacement;
                token_index = name_index;
                if (parenthesized)
                {
                    valid = token_index + 1 < token_count && c_token_is_punctuator(&tokens[token_index + 1].token, C_PUNCTUATOR_RIGHT_PARENTHESIS);
                    token_index += 1;
                }
            }
        }
        else
        {
            transformed[transformed_count++] = token;
        }
    }
    CPreprocessTokenNode* first_expanded = 0;
    CPreprocessTokenNode* last_expanded = 0;
    u64 expanded_count = 0;
    if (valid)
    {
        CMacroExpansionStorage expansion_storage = {0};
        valid = c_preprocess_expand(arena, &expansion_storage, space, symbols, first_macro, 0, 0, stamps, transformed, transformed_count, &first_expanded,
                                    &last_expanded, &expanded_count, expansion_limit, result, 0);
        c_macro_expansion_storage_destroy(&expansion_storage, result->detail);
    }
    if (valid)
    {
        valid = c_conditional_feature_operators(arena, space, symbols, first_macro, first_expanded, options, probes, including_path, including_origin);
    }
    if (valid)
    {
        u64* values = arena_allocate(arena, u64, expanded_count + 1);
        u8* value_flags = arena_allocate(arena, u8, expanded_count + 1);
        CConditionalOperator* operations = arena_allocate(arena, CConditionalOperator, expanded_count + 1);
        u32 value_count = 0;
        u32 operation_count = 0;
        bool expect_operand = true;
        for (CPreprocessTokenNode* node = first_expanded; valid && node; node = node->next)
        {
            CToken token = node->token.token;
            if (token.kind == C_TOKEN_PREPROCESSING_NUMBER)
            {
                u64 value = 0;
                String8 number_spelling = c_token_spelling(base, token);
                valid = expect_operand && c_conditional_number(number_spelling, &value);
                if (valid)
                {
                    // The literal is uintmax_t when it says so or when its
                    // value does not fit intmax_t. Character constants carry
                    // their decoded type only in preprocessing arithmetic.
                    bool literal_unsigned = value > (u64)INT64_MAX;
                    for (u64 suffix_index = 0; suffix_index < number_spelling.length; suffix_index += 1)
                    {
                        literal_unsigned |= number_spelling.pointer[suffix_index] == 'u' || number_spelling.pointer[suffix_index] == 'U';
                    }
                    value_flags[value_count] = literal_unsigned ? C_CONDITIONAL_VALUE_UNSIGNED : 0;
                    values[value_count++] = value;
                    expect_operand = false;
                }
            }
            else if (token.kind == C_TOKEN_CHARACTER_LITERAL)
            {
                u64 character = 0;
                CTypeKind character_kind = C_TYPE_INVALID;
                valid = expect_operand && c_ir_decode_character_value(arena, base, token, result->target, &character, &character_kind);
                if (expect_operand && !valid && preprocessor_arithmetic)
                {
                    c_preprocess_diagnostic_push(arena, result, c_pp_stamp_location(stamps, node->token.stamp), C_DIAGNOSTIC_INVALID_CONDITIONAL,
                        string_format(arena, S8("invalid character literal {S8} in preprocessing conditional"),
                                      c_token_spelling(base, token)));
                }
                bool character_unsigned = false;
                if (valid && preprocessor_arithmetic)
                {
                    IrTypeKind character_ir_kind;
                    u32 character_width = 0;
                    u32 character_alignment = 0;
                    bool character_signed = true;
                    valid = c_ir_scalar_type_properties(result->target, character_kind, &character_ir_kind, &character_width,
                                                        &character_signed, &character_alignment);
                    character_unsigned = valid && !character_signed;
                    if (valid && character_signed && character_width && character_width < 64 && (character >> (character_width - 1)) & 1)
                    {
                        // A signed wide constant such as L'\xffffffff' is negative.
                        character |= ~(u64)0 << character_width;
                    }
                }
                if (valid)
                {
                    value_flags[value_count] = character_unsigned ? C_CONDITIONAL_VALUE_UNSIGNED : 0;
                    values[value_count++] = character;
                    expect_operand = false;
                }
            }
            else if (token.kind == C_TOKEN_IDENTIFIER)
            {
                valid = expect_operand;
                if (valid)
                {
                    value_flags[value_count] = 0;
                    values[value_count++] = c_preprocess_dialect_is_c23(result->dialect) && c_token_spelling_equal(base, token, S8("true"));
                    expect_operand = false;
                }
            }
            else if (token.kind != C_TOKEN_PUNCTUATOR)
            {
                valid = false;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                valid = expect_operand;
                if (valid)
                {
                    operations[operation_count++] = C_CONDITIONAL_OPEN;
                }
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                valid = !expect_operand;
                while (valid && operation_count && operations[operation_count - 1] != C_CONDITIONAL_OPEN)
                {
                    valid = c_conditional_apply(operations[--operation_count], values, value_flags, &value_count);
                }
                valid = valid && operation_count != 0;
                if (valid)
                {
                    operation_count -= 1;
                }
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_QUESTION))
            {
                valid = !expect_operand;
                u32 precedence = c_conditional_precedence(C_CONDITIONAL_QUESTION);
                while (valid && operation_count && operations[operation_count - 1] != C_CONDITIONAL_OPEN &&
                       c_conditional_precedence(operations[operation_count - 1]) > precedence)
                {
                    valid = c_conditional_apply(operations[--operation_count], values, value_flags, &value_count);
                }
                if (valid)
                {
                    operations[operation_count++] = C_CONDITIONAL_QUESTION;
                    expect_operand = true;
                }
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_COLON))
            {
                valid = !expect_operand;
                while (valid && operation_count && operations[operation_count - 1] != C_CONDITIONAL_QUESTION)
                {
                    CConditionalOperator previous = operations[--operation_count];
                    valid = previous != C_CONDITIONAL_OPEN && c_conditional_apply(previous, values, value_flags, &value_count);
                }
                valid = valid && operation_count != 0;
                if (valid)
                {
                    operations[operation_count - 1] = C_CONDITIONAL_SELECT;
                    expect_operand = true;
                }
            }
            else
            {
                CConditionalOperator operation = C_CONDITIONAL_OPERATOR_COUNT;
                valid = c_conditional_operator(token, expect_operand, &operation);
                bool unary = c_conditional_is_unary(operation);
                valid = valid && expect_operand == unary;
                u32 precedence = c_conditional_precedence(operation);
                while (valid && operation_count && operations[operation_count - 1] != C_CONDITIONAL_OPEN)
                {
                    CConditionalOperator previous = operations[operation_count - 1];
                    u32 previous_precedence = c_conditional_precedence(previous);
                    if (previous_precedence < precedence || (unary && previous_precedence == precedence))
                    {
                        break;
                    }
                    operation_count -= 1;
                    valid = c_conditional_apply(previous, values, value_flags, &value_count);
                }
                if (valid)
                {
                    operations[operation_count++] = operation;
                    expect_operand = true;
                }
            }
        }
        valid = valid && !expect_operand;
        while (valid && operation_count)
        {
            CConditionalOperator operation = operations[--operation_count];
            valid = operation != C_CONDITIONAL_OPEN && operation != C_CONDITIONAL_QUESTION &&
                    c_conditional_apply(operation, values, value_flags, &value_count);
        }
        valid = valid && value_count == 1 && !(value_flags[0] & C_CONDITIONAL_VALUE_FAULT);
        if (valid)
        {
            *value_out = values[0];
        }
    }
    return valid;
}

// The parse-side entry point: no macro table, so expansion is a pass-through
// and nothing ever allocates spelling bytes — the read-only space view over
// the finished result is enough, and any allocation attempt fails loudly.
BUSTER_C_SHARED bool c_integer_expression_evaluate(Arena* arena, char8 const* spelling_base, CToken* tokens, u32 token_count, u32 expansion_limit,
                                                       CPreprocessResult* result, u64* value_out)
{
    CSpellingSpace view = {
        .base = (char8*)spelling_base,
    };
    CPpToken* wrapped = arena_allocate(arena, CPpToken, token_count);
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        wrapped[token_index] = (CPpToken){
            .token = tokens[token_index],
        };
    }
    return c_integer_expression_evaluate_with_features(arena, &view, 0, 0, 0, wrapped, token_count, expansion_limit, false, result, 0, 0, (String8){0},
                                                       (CIncludeSearchOrigin){0}, value_out);
}

BUSTER_C_INTERNAL bool c_conditional_evaluate(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro* first_macro, CPpStampTable* stamps,
                                                CPpToken* tokens, u32 token_count, u32 expansion_limit, CPreprocessResult* result, CPreprocessOptions options,
                                                CIncludeProbeTable* probes, String8 including_path, CIncludeSearchOrigin including_origin, bool* value_out)
{
    u64 value = 0;
    bool valid =
        c_integer_expression_evaluate_with_features(arena, space, symbols, first_macro, stamps, tokens, token_count, expansion_limit, true, result, &options, probes,
                                                    including_path, including_origin, &value);
    *value_out = value != 0;
    return valid;
}

typedef struct CConditionalFrame CConditionalFrame;
struct CConditionalFrame
{
    CConditionalFrame* previous;
    CSourceLocation location;
    bool parent_active;
    bool active;
    bool branch_taken;
    bool else_seen;
};

BUSTER_C_INTERNAL bool c_preprocess_is_active(CConditionalFrame* conditional)
{
    return !conditional || conditional->active;
}

// The row scan the mask projection below replaces, kept as the definition of
// a line's end for a run that carries no shape sidecar and as the reference
// c_test_pp_class_masks_agree holds the mask path to.
BUSTER_C_INTERNAL u64 c_preprocess_line_end(CLexResult lex, u64 token_index)
{
    while (token_index < lex.token_count && lex.tokens[token_index].kind != C_TOKEN_NEWLINE && lex.tokens[token_index].kind != C_TOKEN_END_OF_FILE)
    {
        token_index += 1;
    }
    return token_index;
}

// A block comment becomes one space. Its retained physical newline rows
// therefore do not end a directive. Start at the first physical line end,
// then inspect only comment gaps on any continuation; token spelling bytes
// (including literal comment delimiters) are never scanned as comments.
BUSTER_C_INTERNAL u64 c_preprocess_directive_line_end(CLexResult lex, u64 token_index)
{
    bool comment_open = false;
    u64 gap_start = token_index ? (u64)lex.tokens[token_index - 1].offset +
                                     c_token_length(lex.spelling_base, lex.tokens[token_index - 1])
                               : 0;
    while (token_index < lex.token_count && lex.tokens[token_index].kind != C_TOKEN_END_OF_FILE)
    {
        CToken current = lex.tokens[token_index];
        bool line_comment = false;
        for (u64 byte_index = gap_start; byte_index < current.offset; byte_index += 1)
        {
            char8 byte = lex.spelling_base[byte_index];
            if (comment_open)
            {
                if (byte == '*' && byte_index + 1 < current.offset && lex.spelling_base[byte_index + 1] == '/')
                {
                    comment_open = false;
                    byte_index += 1;
                }
            }
            else if (!line_comment && byte == '/' && byte_index + 1 < current.offset)
            {
                if (lex.spelling_base[byte_index + 1] == '*')
                {
                    comment_open = true;
                    byte_index += 1;
                }
                else if (lex.spelling_base[byte_index + 1] == '/')
                {
                    line_comment = true;
                    byte_index += 1;
                }
            }
        }
        if (current.kind == C_TOKEN_NEWLINE && !comment_open)
        {
            break;
        }
        gap_start = (u64)current.offset + c_token_length(lex.spelling_base, current);
        token_index += 1;
    }
    return token_index;
}

// The per-64-token class projection of one lexed run, built once when the run
// becomes a source frame and read by every per-line scan of the driver below.
//
// Every one of those scans asks the same closed set of questions of a token —
// does the line end here, is this a parenthesis, can this name a macro — and
// the one-byte shape sidecar answers all of them without touching a 12-byte
// row, so 64 tokens are one masked load and four compares. What that buys is
// not the compare but the advance: a line end becomes a trailing-zero count
// instead of a nine-step row loop with a mispredicted exit, and the
// parenthesis and macro-name scans visit only the lanes that can answer
// (12% and 20% of the stream on this workload) instead of every token of
// every line. C_TOKEN_INVALID is 0, so the masked tail lanes of the last
// window match no class and need no separate trim.
#define C_PP_CLASS_MASK_WINDOW 64

typedef struct CPpClassMasks CPpClassMasks;
struct CPpClassMasks
{
    Mask64* stop;
    Mask64* identifier;
    Mask64* parenthesis_left;
    Mask64* parenthesis_right;
    // 0 when the run carried no shape sidecar; every consumer falls back to
    // the row scans above on that run.
    u64 word_count;
};

BUSTER_C_INTERNAL void c_pp_class_masks_build(Arena* arena, CPpClassMasks* masks, CTokenShape const* shapes, u64 token_count)
{
    u64 word_count = (token_count + (C_PP_CLASS_MASK_WINDOW - 1)) / C_PP_CLASS_MASK_WINDOW;
    *masks = (CPpClassMasks){0};
    C_CENSUS_RECORD(CLASS_MASK_TOKENS, shapes ? token_count : 0);
    if (shapes && word_count)
    {
        masks->stop = arena_allocate(arena, Mask64, word_count);
        masks->identifier = arena_allocate(arena, Mask64, word_count);
        masks->parenthesis_left = arena_allocate(arena, Mask64, word_count);
        masks->parenthesis_right = arena_allocate(arena, Mask64, word_count);
        Simd512 newline_shape = simd512_splat((u8)C_TOKEN_NEWLINE);
        Simd512 end_shape = simd512_splat((u8)C_TOKEN_END_OF_FILE);
        Simd512 identifier_shape = simd512_splat((u8)C_TOKEN_IDENTIFIER);
        Simd512 left_shape = simd512_splat(c_token_shape_from_fields(C_TOKEN_PUNCTUATOR, C_PUNCTUATOR_LEFT_PARENTHESIS));
        Simd512 right_shape = simd512_splat(c_token_shape_from_fields(C_TOKEN_PUNCTUATOR, C_PUNCTUATOR_RIGHT_PARENTHESIS));
        for (u64 word_index = 0; word_index < word_count; word_index += 1)
        {
            u64 window_base = word_index * C_PP_CLASS_MASK_WINDOW;
            Mask64 window_mask = mask64_prefix(token_count - window_base);
            Simd512 window = simd512_load_masked(shapes + window_base, window_mask);
            masks->stop[word_index] = mask64_or(simd512_equal_u8(window, newline_shape), simd512_equal_u8(window, end_shape));
            masks->identifier[word_index] = simd512_equal_u8(window, identifier_shape);
            masks->parenthesis_left[word_index] = simd512_equal_u8(window, left_shape);
            masks->parenthesis_right[word_index] = simd512_equal_u8(window, right_shape);
        }
        masks->word_count = word_count;
    }
}

// The lanes of `word_index` that lie in [first, end): the whole word except
// at the two ends of the range.
BUSTER_C_INTERNAL Mask64 c_pp_class_range_mask(u64 word_index, u64 first, u64 end)
{
    Mask64 result = mask64_not(0);
    if (word_index == first / C_PP_CLASS_MASK_WINDOW)
    {
        result = mask64_and(result, mask64_shift_left(mask64_not(0), first % C_PP_CLASS_MASK_WINDOW));
    }
    if (word_index == (end - 1) / C_PP_CLASS_MASK_WINDOW)
    {
        result = mask64_and(result, mask64_prefix(((end - 1) % C_PP_CLASS_MASK_WINDOW) + 1));
    }
    return result;
}

// The first newline or end-of-file token at or after `token_index`, which is
// what c_preprocess_line_end answers by walking rows.
BUSTER_C_INTERNAL u64 c_pp_line_end_masked(CPpClassMasks const* masks, u64 token_count, u64 token_index)
{
    u64 result = token_count;
    if (token_index < token_count)
    {
        u64 word_index = token_index / C_PP_CLASS_MASK_WINDOW;
        Mask64 pending = mask64_and(masks->stop[word_index], mask64_shift_left(mask64_not(0), token_index % C_PP_CLASS_MASK_WINDOW));
        while (!pending && word_index + 1 < masks->word_count)
        {
            word_index += 1;
            pending = masks->stop[word_index];
        }
        if (pending)
        {
            result = word_index * C_PP_CLASS_MASK_WINDOW + mask64_first_set(pending);
        }
    }
    return result;
}

// The clamped parenthesis depth after [first, end), entered at `depth`: a
// ')' with nothing open is ignored, which is what the row scan did.
BUSTER_C_INTERNAL u32 c_pp_parenthesis_depth(CPpClassMasks const* masks, u64 first, u64 end, u32 depth)
{
    u32 result = depth;
    if (first < end)
    {
        u64 last_word = (end - 1) / C_PP_CLASS_MASK_WINDOW;
        for (u64 word_index = first / C_PP_CLASS_MASK_WINDOW; word_index <= last_word; word_index += 1)
        {
            Mask64 range = c_pp_class_range_mask(word_index, first, end);
            Mask64 opens = mask64_and(masks->parenthesis_left[word_index], range);
            Mask64 closes = mask64_and(masks->parenthesis_right[word_index], range);
            Mask64 pending = mask64_or(opens, closes);
            while (pending)
            {
                u32 lane = mask64_first_set(pending);
                bool opening = mask64_and(mask64_shift_right(opens, lane), 1) != 0;
                result = opening ? result + 1 : (result ? result - 1 : 0);
                pending = mask64_and(pending, pending - 1);
            }
        }
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
// The differential gate on the mask projection: every answer the driver takes
// from a mask must equal the row scan it replaced, on a run whose lines cross
// the 64-token windows at every offset.
bool c_test_pp_class_masks_agree(Arena* arena, String8 source)
{
    bool result = true;
    CLexResult lex = c_lex(arena, source);
    CPpClassMasks masks;
    c_pp_class_masks_build(arena, &masks, lex.token_shapes, lex.token_count);
    if (!masks.word_count && lex.token_count)
    {
        result = false;
    }
    for (u64 token_index = 0; result && token_index < lex.token_count; token_index += 1)
    {
        Mask64 word = masks.identifier[token_index / C_PP_CLASS_MASK_WINDOW];
        bool named = mask64_and(mask64_shift_right(word, token_index % C_PP_CLASS_MASK_WINDOW), 1) != 0;
        if (named != (lex.tokens[token_index].kind == C_TOKEN_IDENTIFIER))
        {
            result = false;
        }
        u64 masked_end = c_pp_line_end_masked(&masks, lex.token_count, token_index);
        u64 row_end = c_preprocess_line_end(lex, token_index);
        if (masked_end != row_end ||
            c_preprocess_directive_line_end(lex, masked_end) != c_preprocess_directive_line_end(lex, row_end))
        {
            result = false;
        }
    }
    for (u64 line_start = 0; result && line_start < lex.token_count;)
    {
        u64 line_end = c_preprocess_line_end(lex, line_start);
        u32 rows = 0;
        for (u64 scan = line_start; scan < line_end; scan += 1)
        {
            if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                rows += 1;
            }
            else if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_RIGHT_PARENTHESIS) && rows)
            {
                rows -= 1;
            }
        }
        if (c_pp_parenthesis_depth(&masks, line_start, line_end, 0) != rows)
        {
            result = false;
        }
        line_start = line_end + 1;
    }
    return result;
}
#endif

// The whole-file include-guard proof, per source frame: SEARCHING until the
// file's first top-level directive, GUARDED while inside a leading
// `#ifndef NAME` conditional, CLOSED once its `#endif` returns to the
// frame's base with the shape still intact, and DISQUALIFIED as soon as any
// token or directive is seen at the frame's top level outside that one
// conditional. A frame that reaches end of file CLOSED has proven that
// re-lexing it while NAME is defined can only produce an empty token
// sequence, which is what lets the next inclusion be dropped unread.
typedef enum CIncludeGuardState
{
    C_INCLUDE_GUARD_SEARCHING = 0,
    C_INCLUDE_GUARD_GUARDED,
    C_INCLUDE_GUARD_CLOSED,
    C_INCLUDE_GUARD_DISQUALIFIED,
} CIncludeGuardState;

typedef struct CPreprocessSourceFrame CPreprocessSourceFrame;
struct CPreprocessSourceFrame
{
    CPreprocessSourceFrame* previous;
    CConditionalFrame* conditional_base;
    // The leading top-level conditional while the guard proof is GUARDED,
    // and the interned symbol of its `#ifndef` name (see CIncludeGuardState).
    CConditionalFrame* guard;
    CLexResult lex;
    u64 lex_diagnostic_index;
    // The class projection of `lex` (see CPpClassMasks); built once when the
    // frame is pushed, read by every per-line scan of the driver.
    CPpClassMasks class_masks;
    String8 path;
    // Suppression keys are deliberately separate from diagnostic/source-map
    // spellings. Filesystem frames carry descriptor identity while the resolved
    // path remains what diagnostics and source maps show.
    CIncludeFileIdentity identity;
    String8 logical_path;
    s64 line_delta;
    u64 token_index;
    // Index of the frame's current source-map entry (the file span or the
    // latest #line split); its file id is assigned lazily when the region
    // first stages a token, so files that contribute nothing never enter
    // the file table — include resolution differs between hosts (the
    // self-hosted stages lack the clang resource headers), and an eagerly
    // registered but token-free header would break the fixed point.
    u32 map_entry;
    u32 depth;
    u32 guard_symbol;
    u8 guard_state;
    bool line_start;
    FileMapRead source_map;
    CIncludeSearchOrigin include_origin;
};

BUSTER_C_INTERNAL u32 c_preprocess_builtin_include_level(CMacro* first)
{
    return first->builtin_frame ? first->builtin_frame->depth : 0;
}

// A frame's lexer diagnostics belong to the spelling that raised them, so the
// walk releases them a line at a time: kept on a live line, ignored in a
// skipped group, where only comments and conditional nesting matter
// (C11 6.10.1p6). An unterminated block comment is structural and is always
// kept. A byte that starts no token is itself a preprocessing token (6.4p1);
// its diagnostic waits until the token survives into the output stream
// (see the C_TOKEN_INVALID scan at the end of c_preprocess_run).
BUSTER_C_INTERNAL void c_preprocess_lex_diagnostics_release(Arena* arena, CPreprocessResult* result, char8 const* base,
                                                            CPreprocessSourceFrame* frame, u64 end_offset, bool live)
{
    while (frame->lex_diagnostic_index < frame->lex.diagnostic_count &&
           frame->lex.diagnostics[frame->lex_diagnostic_index].location.map_offset < end_offset)
    {
        CDiagnostic diagnostic = frame->lex.diagnostics[frame->lex_diagnostic_index];
        bool invalid_byte = diagnostic.kind == C_DIAGNOSTIC_INVALID_CHARACTER && base[diagnostic.location.map_offset] != '\\';
        if (diagnostic.kind == C_DIAGNOSTIC_UNTERMINATED_BLOCK_COMMENT || diagnostic.kind == C_DIAGNOSTIC_SOURCE_TOO_LARGE ||
            (live && !invalid_byte))
        {
            c_preprocess_diagnostic_copy(arena, result, diagnostic);
        }
        frame->lex_diagnostic_index += 1;
    }
}

// Test instrumentation observes the real probe loops without adding state or
// work to tests-disabled compiler builds.
#if BUSTER_INCLUDE_TESTS
#define C_INCLUDE_FILE_SLOT_HASH(table, entries, slot) ((table)->probe_count += 1, (entries)[slot].hash)
#else
#define C_INCLUDE_FILE_SLOT_HASH(table, entries, slot) ((entries)[slot].hash)
#endif

// The table stays at most half full. Growth first proves both the u32 doubling
// and this arena's remaining reservation, so neither an overflowing capacity
// nor a short allocation can turn into a smaller table followed by writes.
BUSTER_C_INTERNAL bool c_include_file_table_grow(CIncludeFileTable* table)
{
    bool result = false;
    u32 capacity = C_INCLUDE_FILE_INITIAL_CAPACITY;
    if (table->capacity)
    {
        if (table->capacity <= UINT32_MAX / 2)
        {
            capacity = table->capacity * 2;
        }
        else
        {
            capacity = 0;
        }
    }
    u64 byte_count = (u64)capacity * sizeof(*table->entries);
    u64 position = align_forward(table->arena->position, BUSTER_ALIGN_OF(CIncludeFileEntry));
    bool allocation_fits = capacity && position <= table->arena->reserved_size && byte_count <= table->arena->reserved_size - position;
    if (allocation_fits)
    {
        CIncludeFileEntry* entries = arena_allocate_zeroed(table->arena, CIncludeFileEntry, capacity);
        for (u32 old_slot = 0; old_slot < table->capacity; old_slot += 1)
        {
            CIncludeFileEntry entry = table->entries[old_slot];
            if (entry.hash)
            {
                u32 slot = (u32)entry.hash & (capacity - 1);
                while (C_INCLUDE_FILE_SLOT_HASH(table, entries, slot))
                {
                    slot = (slot + 1) & (capacity - 1);
                }
                entries[slot] = entry;
            }
        }
        table->entries = entries;
        table->capacity = capacity;
        result = true;
    }
    return result;
}

// Filesystem includes use identity captured from the descriptor that supplied
// their bytes. Builtin headers and non-filesystem namespaces retain a path key;
// neither case changes the resolved spelling kept for diagnostics/source maps.
BUSTER_C_INTERNAL CIncludeFileIdentity c_include_file_identity(String8 path, FileIdentity file_identity)
{
    CIncludeFileIdentity result = {
        .path = path,
    };
    if (file_identity.valid)
    {
        result = (CIncludeFileIdentity){
            .device = file_identity.device,
            .index = file_identity.index,
            .physical = true,
        };
    }
    return result;
}

BUSTER_C_INTERNAL bool c_include_file_identity_equal(CIncludeFileEntry const* entry, CIncludeFileIdentity identity)
{
    bool result = entry->physical == identity.physical;
    if (result)
    {
        result = identity.physical ? entry->device == identity.device && entry->index == identity.index : string_equal(entry->spelling, identity.path);
    }
    return result;
}

BUSTER_C_INTERNAL u64 c_include_file_identity_hash(CIncludeFileIdentity identity)
{
    u64 result;
    if (identity.physical)
    {
        u64 words[2] = {identity.device, identity.index};
        result = buster_hash_64((u8*)words, sizeof(words));
    }
    else
    {
        result = buster_hash_64((u8*)identity.path.pointer, identity.path.length);
    }
    // Zero marks an empty table entry. Preserve every bit of nonzero hashes
    // instead of forcing all home buckets odd.
    return result ? result : 1;
}

// Find or create the record for `identity`. The output is cleared on failure;
// callers must diagnose the status rather than silently losing once semantics.
BUSTER_C_INTERNAL CIncludeFileStatus c_include_file_entry(CIncludeFileTable* table, CIncludeFileIdentity identity, String8 spelling,
                                                          CIncludeFileEntry** entry_out)
{
    CIncludeFileStatus result = C_INCLUDE_FILE_INVALID_IDENTITY;
    *entry_out = 0;
    bool valid_identity = identity.physical || (identity.path.pointer && identity.path.length);
    if (valid_identity)
    {
        u64 hash = c_include_file_identity_hash(identity);
        u32 slot = 0;
        bool found = false;
        if (table->capacity)
        {
            slot = (u32)hash & (table->capacity - 1);
            while (!found && C_INCLUDE_FILE_SLOT_HASH(table, table->entries, slot))
            {
                CIncludeFileEntry* entry = table->entries + slot;
                found = entry->hash == hash && c_include_file_identity_equal(entry, identity);
                if (!found)
                {
                    slot = (slot + 1) & (table->capacity - 1);
                }
            }
        }
        bool capacity_ready = found || (table->capacity && table->count + 1 <= table->capacity / 2);
        if (!capacity_ready)
        {
            capacity_ready = c_include_file_table_grow(table);
            if (capacity_ready)
            {
                slot = (u32)hash & (table->capacity - 1);
                while (C_INCLUDE_FILE_SLOT_HASH(table, table->entries, slot))
                {
                    slot = (slot + 1) & (table->capacity - 1);
                }
            }
        }
        if (capacity_ready)
        {
            if (!found)
            {
                table->entries[slot] = (CIncludeFileEntry){
                    .spelling = spelling,
                    .hash = hash,
                    .device = identity.device,
                    .index = identity.index,
                    .physical = identity.physical,
                };
                table->count += 1;
            }
            *entry_out = table->entries + slot;
            result = C_INCLUDE_FILE_OK;
        }
        else
        {
            result = C_INCLUDE_FILE_ALLOCATION_FAILED;
        }
    }
    return result;
}

#undef C_INCLUDE_FILE_SLOT_HASH

// Whether an inclusion of `entry` produces no tokens: #pragma once, a repeated
// #import (the root is entered before the table exists, so it is compared
// lazily), or a whole-file guard proven by an earlier lex whose macro is
// defined. Reads only; the caller records the inclusion.
BUSTER_C_INTERNAL bool c_include_suppressed(CIncludeFileEntry const* entry, bool is_import, CIncludeFileIdentity root_identity, CMacro* first_macro)
{
    bool result = entry->once || (is_import && (entry->included || c_include_file_identity_equal(entry, root_identity)));
    if (!result && entry->guard_symbol)
    {
        CMacro* guard_macro = c_macro_find(first_macro, entry->guard_symbol);
        result = guard_macro && guard_macro->definition.defined;
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
CIncludeFileStatus c_test_include_file_entry(CIncludeFileTable* table, CIncludeFileIdentity identity, String8 spelling,
                                            CIncludeFileEntry** entry_out)
{
    return c_include_file_entry(table, identity, spelling, entry_out);
}

u32 c_test_symbol_intern(CSymbolTable* table, String8 name)
{
    return c_symbol_intern(table, name);
}

u32 c_test_symbol_find(CSymbolTable const* table, String8 name)
{
    return c_symbol_find(table, name);
}

u32 c_test_symbol_count(CSymbolTable const* table)
{
    return table->count;
}

bool c_test_include_file_table_grow(CIncludeFileTable* table)
{
    return c_include_file_table_grow(table);
}
#endif

BUSTER_C_INTERNAL void c_include_file_diagnostic(Arena* arena, CPreprocessResult* preprocess, CSourceLocation location,
                                                  CIncludeFileStatus status, String8 spelling)
{
    String8 message = status == C_INCLUDE_FILE_INVALID_IDENTITY
                          ? string_format(arena, S8("included file has no usable identity: {S8}"), spelling)
                          : string_format(arena, S8("included-file tracking allocation failed: {S8}"), spelling);
    c_preprocess_diagnostic_push(arena, preprocess, location, C_DIAGNOSTIC_INVALID_INCLUDE, message);
}

typedef struct CPragmaPackStack CPragmaPackStack;
struct CPragmaPackStack
{
    CPragmaPackStack* previous;
    u16 alignment;
};

// One #pragma GCC visibility push, holding the state it replaced.
typedef struct CPragmaVisibilityStack CPragmaVisibilityStack;
struct CPragmaVisibilityStack
{
    CPragmaVisibilityStack* previous;
    u8 visibility;
};

// The pragma state change list under construction. Entries are appended lazily
// at output-append time — the first token that lands after a state change
// carries the new value's span start — so pragmas on directive lines and
// _Pragma markers mid-expansion record through the same comparison, and
// consecutive changes with no token between them collapse to one entry.
typedef struct CPragmaStateRecorder CPragmaStateRecorder;
struct CPragmaStateRecorder
{
    Arena* arena;
    CPragmaState* changes;
    u32 count;
    u32 capacity;
    u16 recorded;
    u8 recorded_visibility;
};

BUSTER_C_INTERNAL void c_pragma_state_record(CPragmaStateRecorder* recorder, u64 token_index, u16 alignment, u8 visibility)
{
    if (alignment == recorder->recorded && visibility == recorder->recorded_visibility)
    {
        return;
    }
    if (recorder->count == recorder->capacity)
    {
        u32 capacity = recorder->capacity ? recorder->capacity * 2 : 16;
        CPragmaState* changes = arena_allocate(recorder->arena, CPragmaState, capacity);
        if (recorder->count)
        {
            memcpy(changes, recorder->changes, sizeof(*changes) * recorder->count);
        }
        recorder->changes = changes;
        recorder->capacity = capacity;
    }
    recorder->changes[recorder->count++] = (CPragmaState){
        .token_index = (u32)token_index,
        .alignment = alignment,
        .visibility = visibility,
    };
    recorder->recorded = alignment;
    recorder->recorded_visibility = visibility;
}

// The last entry at or before the token, or the all-zero state before the first.
BUSTER_C_INTERNAL CPragmaState c_preprocess_pragma_state(CPreprocessResult const* preprocess, u64 token_index)
{
    u32 low = 0;
    u32 high = preprocess->pragma_change_count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        if (preprocess->pragma_changes[middle].token_index <= token_index)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    CPragmaState result = {0};
    if (low)
    {
        result = preprocess->pragma_changes[low - 1];
    }
    return result;
}

u32 c_preprocess_pack_alignment(CPreprocessResult const* preprocess, u64 token_index)
{
    return c_preprocess_pragma_state(preprocess, token_index).alignment;
}

u32 c_preprocess_symbol_visibility(CPreprocessResult const* preprocess, u64 token_index)
{
    return c_preprocess_pragma_state(preprocess, token_index).visibility;
}

struct CPreprocessPragmaContext
{
    Arena* arena;
    CPreprocessResult* preprocess;
    CSymbolTable* symbols;
    CMacro** first_macro;
    CMacro** last_macro;
    CMacroPushMacro** macro_push_stack;
    CPragmaPackStack** pack_stack;
    u16* pack_alignment;
    CPragmaVisibilityStack** visibility_stack;
    u8* visibility;
    CPragmaStateRecorder* pragma_changes;
    CIncludeFileTable* include_files;
    String8 current_path;
    CIncludeFileIdentity current_identity;
    CSourceLocation current_location;
};

BUSTER_C_INTERNAL bool c_preprocess_pragma_macro_name(char8 const* base, CToken* tokens, u32 token_count, String8* name_out)
{
    if (token_count == 4 && tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_is_punctuator(&tokens[1], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
        tokens[2].kind == C_TOKEN_STRING_LITERAL && tokens[2].length >= 2 && c_token_is_punctuator(&tokens[3], C_PUNCTUATOR_RIGHT_PARENTHESIS))
    {
        String8 spelling = c_token_spelling(base, tokens[2]);
        *name_out = (String8){
            .pointer = spelling.pointer + 1,
            .length = spelling.length - 2,
        };
        return true;
    }
    return false;
}

BUSTER_C_INTERNAL void c_macro_push_definition(CPreprocessPragmaContext context, String8 name)
{
    CMacro* macro = c_macro_find(*context.first_macro, c_symbol_intern(context.symbols, name));
    CMacroPushMacro* entry = arena_allocate(context.arena, CMacroPushMacro, 1);
    *entry = (CMacroPushMacro){
        .previous = *context.macro_push_stack,
        .macro = macro,
        .name = string_duplicate_arena(context.arena, name, false),
        .definition = macro ? macro->definition : (CMacroDefinition){0},
    };
    if (macro && macro->definition.replacement_count)
    {
        entry->definition.replacement = arena_allocate(context.arena, CToken, macro->definition.replacement_count);
        memcpy(entry->definition.replacement, macro->definition.replacement,
               sizeof(*entry->definition.replacement) * macro->definition.replacement_count);
        if (macro->definition.replacement_space)
        {
            entry->definition.replacement_space = arena_allocate(context.arena, u8, macro->definition.replacement_count);
            memcpy(entry->definition.replacement_space, macro->definition.replacement_space,
                   sizeof(*entry->definition.replacement_space) * macro->definition.replacement_count);
        }
        entry->definition.parameter_index = arena_allocate(context.arena, u32, macro->definition.replacement_count);
        memcpy(entry->definition.parameter_index, macro->definition.parameter_index,
               sizeof(*entry->definition.parameter_index) * macro->definition.replacement_count);
    }
    if (macro && macro->definition.parameter_count)
    {
        entry->definition.parameters = arena_allocate(context.arena, String8, macro->definition.parameter_count);
        memcpy(entry->definition.parameters, macro->definition.parameters,
               sizeof(*entry->definition.parameters) * macro->definition.parameter_count);
        u32 expand_slots = macro->definition.variadic ? macro->definition.parameter_count * 2 : macro->definition.parameter_count;
        entry->definition.parameter_expand_count = arena_allocate(context.arena, u32, expand_slots);
        memcpy(entry->definition.parameter_expand_count, macro->definition.parameter_expand_count,
               sizeof(*entry->definition.parameter_expand_count) * expand_slots);
    }
    *context.macro_push_stack = entry;
}

BUSTER_C_INTERNAL void c_macro_clear_definition(CMacro* macro)
{
    if (!macro)
    {
        return;
    }
    macro->definition = (CMacroDefinition){0};
    macro->disabled = false;
}

BUSTER_C_INTERNAL void c_macro_pop_definition(CPreprocessPragmaContext context, String8 name)
{
    CMacroPushMacro** cursor = context.macro_push_stack;
    while (*cursor && !string_equal((*cursor)->name, name))
    {
        cursor = &(*cursor)->previous;
    }
    if (!(!*cursor))
    {
        CMacroPushMacro* entry = *cursor;
        *cursor = entry->previous;
        if (!entry->macro)
        {
            c_macro_clear_definition(c_macro_find(*context.first_macro, c_symbol_intern(context.symbols, name)));
            return;
        }
        CMacro* macro = entry->macro;
        macro->definition = entry->definition;
        macro->disabled = false;
        // An in-flight pop recomputes this generation's active ENABLE state.
    }
}

// The bound is UINT16_MAX because CToken carries the alignment in a u16, and
// no ABI has a structure alignment that large to begin with.
BUSTER_C_INTERNAL bool c_preprocess_pragma_pack_value(char8 const* base, CToken token, u16* value_out)
{
    u64 value = 0;
    bool valid =
        token.kind == C_TOKEN_PREPROCESSING_NUMBER && c_conditional_number(c_token_spelling(base, token), &value) && value <= UINT16_MAX && value && !(value & (value - 1));
    if (valid)
    {
        *value_out = (u16)value;
    }
    return valid;
}

BUSTER_C_INTERNAL void c_preprocess_pragma_pack(CPreprocessPragmaContext context, char8 const* base, CToken* tokens, u32 token_count)
{
    if (token_count < 3 || tokens[0].kind != C_TOKEN_IDENTIFIER || !c_token_spelling_equal(base, tokens[0], S8("pack")) ||
        !c_token_is_punctuator(&tokens[1], C_PUNCTUATOR_LEFT_PARENTHESIS) || !c_token_is_punctuator(&tokens[token_count - 1], C_PUNCTUATOR_RIGHT_PARENTHESIS))
    {
        return;
    }
    CToken* arguments = tokens + 2;
    u32 argument_count = token_count - 3;
    if (!argument_count)
    {
        *context.pack_alignment = 0;
        return;
    }
    bool push = argument_count >= 1 && arguments[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, arguments[0], S8("push"));
    bool pop = argument_count >= 1 && arguments[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, arguments[0], S8("pop"));
    u32 value_index = UINT32_MAX;
    if (push)
    {
        if (argument_count == 1)
        {
            value_index = UINT32_MAX;
        }
        else if (argument_count == 3 && c_token_is_punctuator(&arguments[1], C_PUNCTUATOR_COMMA))
        {
            value_index = 2;
        }
        else if (argument_count == 5 && c_token_is_punctuator(&arguments[1], C_PUNCTUATOR_COMMA) && arguments[2].kind == C_TOKEN_IDENTIFIER &&
                 c_token_is_punctuator(&arguments[3], C_PUNCTUATOR_COMMA))
        {
            value_index = 4;
        }
        else
        {
            return;
        }
        u16 value = 0;
        bool valid_value = value_index != UINT32_MAX && c_preprocess_pragma_pack_value(base, arguments[value_index], &value);
        if (value_index != UINT32_MAX && !valid_value)
        {
            return;
        }
        CPragmaPackStack* entry = arena_allocate(context.arena, CPragmaPackStack, 1);
        *entry = (CPragmaPackStack){
            .previous = *context.pack_stack,
            .alignment = *context.pack_alignment,
        };
        *context.pack_stack = entry;
        if (valid_value)
        {
            *context.pack_alignment = value;
        }
        return;
    }
    if (pop)
    {
        if (argument_count != 1 &&
            !(argument_count == 3 && c_token_is_punctuator(&arguments[1], C_PUNCTUATOR_COMMA) && arguments[2].kind == C_TOKEN_IDENTIFIER))
        {
            return;
        }
        if (*context.pack_stack)
        {
            *context.pack_alignment = (*context.pack_stack)->alignment;
            *context.pack_stack = (*context.pack_stack)->previous;
        }
        return;
    }
    if (argument_count == 1)
    {
        u16 value = 0;
        if (c_preprocess_pragma_pack_value(base, arguments[0], &value))
        {
            *context.pack_alignment = value;
        }
    }
}

// `#pragma GCC visibility push(name)` and `pop`, with name one of default,
// hidden, internal or protected. The state is the CSymbolVisibility that
// declarations reach through c_preprocess_symbol_visibility. A push nests, a
// pop restores what its push replaced, and a pop with nothing pushed is
// ignored, as in GCC. A malformed pragma is ignored with a warning (GCC's
// spelling of it); protected has no object-model representation, so it is
// an error rather than a silently weaker default. The push still nests so a
// matching pop stays balanced.
BUSTER_C_INTERNAL void c_preprocess_pragma_visibility(CPreprocessPragmaContext context, char8 const* base, CToken* tokens, u32 token_count)
{
    bool is_push = token_count == 6 && tokens[2].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[2], S8("push")) &&
                   c_token_is_punctuator(&tokens[3], C_PUNCTUATOR_LEFT_PARENTHESIS) && tokens[4].kind == C_TOKEN_IDENTIFIER &&
                   c_token_is_punctuator(&tokens[5], C_PUNCTUATOR_RIGHT_PARENTHESIS);
    bool is_pop = token_count == 3 && tokens[2].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[2], S8("pop"));
    u8 requested = C_SYMBOL_VISIBILITY_UNSPECIFIED;
    if (is_push)
    {
        String8 name = c_token_spelling(base, tokens[4]);
        requested = string_equal(name, S8("default"))     ? C_SYMBOL_VISIBILITY_DEFAULT
                    : string_equal(name, S8("hidden"))    ? C_SYMBOL_VISIBILITY_HIDDEN
                    : string_equal(name, S8("internal"))  ? C_SYMBOL_VISIBILITY_INTERNAL
                    : string_equal(name, S8("protected")) ? C_SYMBOL_VISIBILITY_PROTECTED
                                                          : C_SYMBOL_VISIBILITY_UNSPECIFIED;
    }
    if (is_push && requested != C_SYMBOL_VISIBILITY_UNSPECIFIED)
    {
        CPragmaVisibilityStack* entry = arena_allocate(context.arena, CPragmaVisibilityStack, 1);
        *entry = (CPragmaVisibilityStack){
            .previous = *context.visibility_stack,
            .visibility = *context.visibility,
        };
        *context.visibility_stack = entry;
        if (requested == C_SYMBOL_VISIBILITY_PROTECTED)
        {
            c_preprocess_diagnostic_push(context.arena, context.preprocess, context.current_location, C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                         S8("#pragma GCC visibility push(protected) is not supported: protected visibility has no object-model representation"));
        }
        else
        {
            *context.visibility = requested;
        }
    }
    else if (is_pop)
    {
        if (*context.visibility_stack)
        {
            *context.visibility = (*context.visibility_stack)->visibility;
            *context.visibility_stack = (*context.visibility_stack)->previous;
        }
    }
    else
    {
        c_preprocess_diagnostic_push_severity(context.arena, context.preprocess, context.current_location, C_DIAGNOSTIC_PREPROCESSOR_WARNING, C_DIAGNOSTIC_WARNING,
                                              S8("#pragma GCC visibility must be followed by push(default|hidden|internal|protected) or pop; ignored"));
    }
}

BUSTER_C_INTERNAL void c_preprocess_handle_pragma(CPreprocessPragmaContext context, char8 const* base, CToken* tokens, u32 token_count)
{
    if (token_count)
    {
        if (token_count == 1 && tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[0], S8("once")))
        {
            CIncludeFileEntry* entry = 0;
            CIncludeFileStatus status =
                c_include_file_entry(context.include_files, context.current_identity, context.current_path, &entry);
            if (status == C_INCLUDE_FILE_OK)
            {
                entry->once = true;
            }
            else
            {
                c_include_file_diagnostic(context.arena, context.preprocess, context.current_location, status, context.current_path);
            }
            return;
        }
        if (tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[0], S8("push_macro")))
        {
            String8 name = {0};
            if (c_preprocess_pragma_macro_name(base, tokens, token_count, &name))
            {
                c_macro_push_definition(context, name);
            }
            return;
        }
        if (tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[0], S8("pop_macro")))
        {
            String8 name = {0};
            if (c_preprocess_pragma_macro_name(base, tokens, token_count, &name))
            {
                c_macro_pop_definition(context, name);
            }
            return;
        }
        if (tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[0], S8("pack")))
        {
            c_preprocess_pragma_pack(context, base, tokens, token_count);
        }
        if (token_count >= 2 && tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[0], S8("GCC")) &&
            tokens[1].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[1], S8("visibility")))
        {
            c_preprocess_pragma_visibility(context, base, tokens, token_count);
        }
        // GCC/Clang/MSVC diagnostic, system-header, warning, comment, region,
        // and OpenMP pragmas are compatibility no-ops. Unknown pragma bodies
        // are intentionally ignored as well.
    }
}

BUSTER_C_INTERNAL void c_preprocess_pragma_marker(CPreprocessPragmaContext context, char8 const* base, CToken marker)
{
    if (!marker.length)
    {
        return;
    }
    // The marker relex is standalone: its tokens' offsets are relative to its
    // own translated copy, so the handler reads them through that base.
    CLexResult lex = c_lex(context.arena, c_token_spelling(base, marker));
    u32 token_count = 0;
    while (token_count < lex.token_count && lex.tokens[token_count].kind != C_TOKEN_NEWLINE && lex.tokens[token_count].kind != C_TOKEN_END_OF_FILE)
    {
        token_count += 1;
    }
    c_preprocess_handle_pragma(context, lex.spelling_base, lex.tokens, token_count);
}

// Macro-state pragmas take effect at the outer rescan cursor. Argument prescan
// preserves markers, so each substituted occurrence executes in that cursor's
// order. Other markers stay in the output for their token-position reader.
BUSTER_C_INTERNAL bool c_preprocess_expansion_pragma(CPreprocessPragmaContext* pragma_context,
                                                       char8 const* base, CPpToken marker, CPpStampTable const* stamps, CMacroExpansionTaskStack const* tasks)
{
    bool handled = false;
    if (pragma_context && marker.token.length)
    {
        CLexResult lex = c_lex(pragma_context->arena, c_token_spelling(base, marker.token));
        u32 token_count = 0;
        while (token_count < lex.token_count && lex.tokens[token_count].kind != C_TOKEN_NEWLINE && lex.tokens[token_count].kind != C_TOKEN_END_OF_FILE)
        {
            token_count += 1;
        }
        bool pop = token_count && c_token_spelling_equal(lex.spelling_base, lex.tokens[0], S8("pop_macro"));
        handled = pop || (token_count && c_token_spelling_equal(lex.spelling_base, lex.tokens[0], S8("push_macro")));
        if (handled)
        {
            String8 name = {0};
            CMacro* popped_macro = 0;
            if (pop && c_preprocess_pragma_macro_name(lex.spelling_base, lex.tokens, token_count, &name))
            {
                popped_macro = c_macro_find(*pragma_context->first_macro, c_symbol_intern(pragma_context->symbols, name));
            }
            CPreprocessPragmaContext context = *pragma_context;
            context.current_location = c_pp_stamp_location(stamps, marker.stamp);
            c_preprocess_handle_pragma(context, lex.spelling_base, lex.tokens, token_count);
            if (popped_macro)
            {
                // Only the restored generation's ENABLE markers disable it.
                // Inspect all active batches without consuming any task or
                // changing an argument context's floor.
                for (u64 index = 0; index < tasks->count; index += 1)
                {
                    CMacroExpansionTask task = tasks->data[index];
                    if (task.kind == C_MACRO_EXPANSION_ENABLE && task.macro == popped_macro &&
                        task.generation == popped_macro->definition.generation)
                    {
                        popped_macro->disabled = true;
                    }
                }
            }
        }
    }
    return handled;
}

// Materialize an expanded line into the output stream. Foreign tokens (their
// location is a macro-invocation stamp, not a function of their spelling
// offset) get their spellings copied to fresh space offsets under an
// expansion source-map entry; one invocation's tokens share one stamped
// location, so runs of equal locations share one entry.
typedef struct COutputSpacingBlock COutputSpacingBlock;
struct COutputSpacingBlock
{
    COutputSpacingBlock* next;
    u8* boundaries;
    u64 first;
    u64 count;
};

BUSTER_C_INTERNAL void c_preprocess_process_expanded_line(CPreprocessPragmaContext context, CSpellingSpace* space, CSourceMap* map,
                                                            CPpStampTable const* stamps, CPreprocessTokenNode* first_line, u64 line_output_count,
                                                            CTokenStream* token_stream, u64* output_count, COutputSpacingBlock* spacing)
{
    CTokenShape* shapes = 0;
    CToken* tokens = c_token_stream_reserve(token_stream, line_output_count, &shapes);
    u64 count = 0;
    u64 foreign_length = 0;
    for (CPreprocessTokenNode* node = first_line; node; node = node->next)
    {
        foreign_length += node->token.foreign ? c_token_length(space->base, node->token.token) : 0;
        C_CENSUS_RECORD(SPACE_FOREIGN_COPIES, node->token.foreign);
    }
    C_CENSUS_RECORD(SPACE_FOREIGN_BYTES, foreign_length);
    char8* copy = foreign_length ? c_space_allocate(space, foreign_length) : 0;
    bool run_open = false;
    // Every stamp of a line is pushed from a distinct token, so two tokens
    // carry one location exactly when they carry one stamp.
    u32 run_stamp = 0;
    // The pack alignment can only change where a pragma marker fires, so the
    // sample is taken at the line's first token and again at the first token
    // after each marker, instead of asking the recorder once per token.
    bool pack_pending = true;
    for (CPreprocessTokenNode* node = first_line; node; node = node->next)
    {
        if (node->token.token.kind == C_TOKEN_PRAGMA)
        {
            context.current_location = c_pp_stamp_location(stamps, node->token.stamp);
            c_preprocess_pragma_marker(context, space->base, node->token.token);
            pack_pending = true;
            continue;
        }
        CPpToken item = node->token;
        if (pack_pending)
        {
            c_pragma_state_record(context.pragma_changes, *output_count + count, *context.pack_alignment, *context.visibility);
            pack_pending = false;
        }
        if (item.foreign)
        {
            String8 spelling = c_token_spelling(space->base, item.token);
            BUSTER_CHECK(!spelling.length || copy); // Counted in foreign_length above.
            u32 spelling_offset = copy ? c_space_offset(space, copy) : (u32)space->used;
            for (u64 byte_index = 0; byte_index < spelling.length; byte_index += 1)
            {
                copy[byte_index] = spelling.pointer[byte_index];
            }
            if (!run_open || run_stamp != item.stamp)
            {
                CSourceLocation location = c_pp_stamp_location(stamps, item.stamp);
                c_source_map_append(map, (IrSourceRegion){
                                             .start = spelling_offset,
                                             .source = location.file,
                                             .stamp = c_position_from_source_location(location),
                                             .kind = IR_SOURCE_REGION_STAMP,
                                             .origin_plus_one = location.map_offset + 1,
                                         });
                run_open = true;
                run_stamp = item.stamp;
            }
            item.token.offset = spelling_offset;
            if (spelling.length)
            {
                copy += spelling.length;
            }
        }
        else
        {
            run_open = false;
        }
        tokens[count] = item.token;
        shapes[count] = c_token_shape_from_token(item.token);
        if (spacing)
        {
            spacing->boundaries[count] = item.preceded_by_space ? C_OUTPUT_SPACING_SEPARATED : C_OUTPUT_SPACING_ADJACENT;
        }
        count += 1;
    }
    c_token_stream_shrink(token_stream, line_output_count - count);
    if (spacing)
    {
        spacing->count = count;
    }
    *output_count += count;
}

#if BUSTER_INCLUDE_TESTS
bool c_test_expanded_empty_foreign_token_source_map(Arena* arena)
{
    bool result = false;
    Arena* token_arena = arena_create((ArenaCreation){.flags = {.no_pool = 1}});
    Arena* shape_arena = arena_create((ArenaCreation){.flags = {.no_pool = 1}});
    if (token_arena && shape_arena)
    {
        char8 spelling_base[] = "x";
        CSpellingSpace space = {.base = spelling_base, .used = 1, .capacity = sizeof(spelling_base)};
        u32 expected_offset = 1;
        CSourceLocation location = {.offset = 17, .line = 3, .column = 5, .file = 7, .map_offset = expected_offset};
        CPpStampTable stamps = {.entries = &location, .count = 1};
        CSourceMap map = {.arena = arena};
        CPragmaStateRecorder pragma_changes = {.arena = arena};
        u16 pack_alignment = 0;
        u8 visibility = C_SYMBOL_VISIBILITY_UNSPECIFIED;
        CPreprocessPragmaContext context = {
            .arena = arena,
            .pack_alignment = &pack_alignment,
            .visibility = &visibility,
            .pragma_changes = &pragma_changes,
        };
        CPreprocessTokenNode node = {
            .token = {
                .token = {.offset = expected_offset, .kind = C_TOKEN_IDENTIFIER},
                .stamp = 1,
                .foreign = 1,
            },
        };
        CTokenStream token_stream = {
            .arena = token_arena,
            .shape_arena = shape_arena,
            .base = (CToken*)((char8*)token_arena + arena_minimum_position),
            .shape_base = (CTokenShape*)((char8*)shape_arena + arena_minimum_position),
        };
        u64 output_count = 0;
        c_preprocess_process_expanded_line(context, &space, &map, &stamps, &node, 1, &token_stream, &output_count, 0);
        CToken token = token_stream.base[0];
        IrSourcePosition stamp = map.count ? map.regions[0].stamp : (IrSourcePosition){0};
        IrSourcePosition expected_stamp = c_position_from_source_location(location);
        result = output_count == 1 && token.offset == expected_offset && token.length == 0 && map.count == 1 &&
                 map.regions[0].start == expected_offset && map.regions[0].source == location.file &&
                 stamp.source == expected_stamp.source && stamp.offset == expected_stamp.offset && stamp.line == expected_stamp.line &&
                 stamp.column == expected_stamp.column;
    }
    if (shape_arena)
    {
        arena_destroy(shape_arena, 1);
    }
    if (token_arena)
    {
        arena_destroy(token_arena, 1);
    }
    return result;
}
#endif

BUSTER_C_INTERNAL u32 c_preprocess_tokens_from_nodes(CPreprocessTokenNode* first, Arena* arena, CToken** tokens_out)
{
    u32 token_count = 0;
    for (CPreprocessTokenNode* node = first; node; node = node->next)
    {
        token_count += node->token.token.kind != C_TOKEN_PRAGMA;
    }
    CToken* tokens = arena_allocate(arena, CToken, token_count);
    u32 output = 0;
    for (CPreprocessTokenNode* node = first; node; node = node->next)
    {
        if (node->token.token.kind != C_TOKEN_PRAGMA)
        {
            tokens[output++] = node->token.token;
        }
    }
    *tokens_out = tokens;
    return token_count;
}

BUSTER_C_INTERNAL String8 c_preprocess_message_from_tokens(Arena* arena, char8 const* base, CToken* tokens, u32 token_count)
{
    CToken first = {0};
    CToken last = {0};
    bool has_message = false;
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        if (tokens[token_index].kind != C_TOKEN_PRAGMA)
        {
            if (!has_message) { first = tokens[token_index]; }
            last = tokens[token_index];
            has_message = true;
        }
    }
    String8 result = {0};
    if (has_message)
    {
        // These tokens belong to one directive line in the original spelling
        // space. Keep punctuation and the gaps between tokens as written.
        String8 spelling = {.pointer = (char8*)base + first.offset,
                            .length = last.offset + c_token_length(base, last) - first.offset};
        result = string_duplicate_arena(arena, spelling, true);
    }
    return result;
}

BUSTER_C_INTERNAL CSourceLocation c_preprocess_logical_location(CPreprocessSourceFrame* frame, CSourceLocation location);

// Wrap a frame's lex tokens for the expansion machinery (directive lines
// only; text lines wrap unstamped in the main loop and stamp on demand).
// Directive-line expansion output is transient — only diagnostics ever read
// these locations — so one recovery of the line head, the line's only
// stamp, serves every token.
BUSTER_C_INTERNAL CPpToken* c_frame_wrap_tokens(Arena* arena, CPpStampTable* stamps, CPreprocessSourceFrame* frame, u64 start, u64 end)
{
    CPpToken* wrapped = arena_allocate(arena, CPpToken, end - start);
    stamps->count = 0;
    u32 stamp = 0;
    if (start < end)
    {
        stamp = c_pp_stamp_push(stamps, c_preprocess_logical_location(frame, c_lex_token_location(&frame->lex, frame->lex.tokens[start])));
    }
    for (u64 index = start; index < end; index += 1)
    {
        wrapped[index - start] = (CPpToken){
            .token = frame->lex.tokens[index],
            .stamp = stamp & C_PP_STAMP_MASK,
        };
    }
    return wrapped;
}

// Conditional ranges may cross physical lines inside a block comment.
// Drop those whitespace rows before defined/feature/macro processing, and
// start a new location stamp for the first real token on each physical line.
BUSTER_C_INTERNAL CPpToken* c_frame_wrap_conditional_tokens(Arena* arena, CPpStampTable* stamps, CPreprocessSourceFrame* frame,
                                                           u64 start, u64 end, u32* token_count)
{
    CPpToken* wrapped = arena_allocate(arena, CPpToken, end - start);
    stamps->count = 0;
    u32 stamp = 0;
    u32 count = 0;
    bool line_start = true;
    for (u64 index = start; index < end; index += 1)
    {
        CToken token = frame->lex.tokens[index];
        if (token.kind == C_TOKEN_NEWLINE)
        {
            line_start = true;
        }
        else
        {
            if (line_start)
            {
                stamp = c_pp_stamp_push(stamps, c_preprocess_logical_location(frame, c_lex_token_location(&frame->lex, token)));
                line_start = false;
            }
            wrapped[count++] = (CPpToken){.token = token, .stamp = stamp & C_PP_STAMP_MASK};
        }
    }
    *token_count = count;
    return wrapped;
}

typedef enum CPreprocessConditionalDirective
{
    C_PREPROCESS_CONDITIONAL_IF,
    C_PREPROCESS_CONDITIONAL_IFDEF,
    C_PREPROCESS_CONDITIONAL_IFNDEF,
    C_PREPROCESS_CONDITIONAL_ELIF,
    C_PREPROCESS_CONDITIONAL_ELIFDEF,
    C_PREPROCESS_CONDITIONAL_ELIFNDEF,
    C_PREPROCESS_CONDITIONAL_ELSE,
    C_PREPROCESS_CONDITIONAL_ENDIF,
    C_PREPROCESS_CONDITIONAL_COUNT,
} CPreprocessConditionalDirective;

BUSTER_C_INTERNAL CSourceLocation c_preprocess_logical_location(CPreprocessSourceFrame* frame, CSourceLocation location);

BUSTER_C_INTERNAL CPreprocessConditionalDirective c_preprocess_conditional_directive_kind(char8 const* base, CToken directive)
{
    CPreprocessConditionalDirective result = C_PREPROCESS_CONDITIONAL_COUNT;
    if (c_token_spelling_equal(base, directive, S8("if")))
    {
        result = C_PREPROCESS_CONDITIONAL_IF;
    }
    else if (c_token_spelling_equal(base, directive, S8("ifdef")))
    {
        result = C_PREPROCESS_CONDITIONAL_IFDEF;
    }
    else if (c_token_spelling_equal(base, directive, S8("ifndef")))
    {
        result = C_PREPROCESS_CONDITIONAL_IFNDEF;
    }
    else if (c_token_spelling_equal(base, directive, S8("elif")))
    {
        result = C_PREPROCESS_CONDITIONAL_ELIF;
    }
    else if (c_token_spelling_equal(base, directive, S8("elifdef")))
    {
        result = C_PREPROCESS_CONDITIONAL_ELIFDEF;
    }
    else if (c_token_spelling_equal(base, directive, S8("elifndef")))
    {
        result = C_PREPROCESS_CONDITIONAL_ELIFNDEF;
    }
    else if (c_token_spelling_equal(base, directive, S8("else")))
    {
        result = C_PREPROCESS_CONDITIONAL_ELSE;
    }
    else if (c_token_spelling_equal(base, directive, S8("endif")))
    {
        result = C_PREPROCESS_CONDITIONAL_ENDIF;
    }
    return result;
}

BUSTER_C_INTERNAL void c_preprocess_conditional_directive(Arena* arena, CSpellingSpace* space, CSymbolTable* symbol_table,
                                                          CMacro* first_macro, CPpStampTable* stamps, CPreprocessSourceFrame* source_frame,
                                                          CPreprocessConditionalDirective directive_kind, CToken directive, u64 token_index,
                                                          u64 line_end, u32 expansion_limit, CPreprocessResult* result, CPreprocessOptions options,
                                                          CIncludeProbeTable* probes, CConditionalFrame** conditional_pointer)
{
    CConditionalFrame* conditional = *conditional_pointer;
    CLexResult lex = source_frame->lex;
    char8 const* base = space->base;
    bool active = c_preprocess_is_active(conditional);
    u64 diagnostic_count_before = result->diagnostic_count;
    CSourceLocation directive_location = c_preprocess_logical_location(source_frame, c_lex_token_location(&source_frame->lex, directive));
    if (directive_kind == C_PREPROCESS_CONDITIONAL_IF || directive_kind == C_PREPROCESS_CONDITIONAL_IFDEF ||
        directive_kind == C_PREPROCESS_CONDITIONAL_IFNDEF)
    {
        bool condition_value = false;
        bool valid = true;
        if (directive_kind == C_PREPROCESS_CONDITIONAL_IF)
        {
            if (active)
            {
                u32 expression_count = 0;
                CPpToken* expression = c_frame_wrap_conditional_tokens(arena, stamps, source_frame, token_index, line_end, &expression_count);
                valid = c_conditional_evaluate(arena, space, symbol_table, first_macro, stamps,
                                               expression, expression_count, expansion_limit, result, options, probes, source_frame->path,
                                               source_frame->include_origin, &condition_value);
            }
        }
        else if (token_index == line_end || lex.tokens[token_index].kind != C_TOKEN_IDENTIFIER)
        {
            valid = false;
        }
        else
        {
            if (token_index + 1 != line_end && active)
            {
                c_preprocess_diagnostic_push_severity(arena, result, directive_location, C_DIAGNOSTIC_EXTRA_DIRECTIVE_TOKENS,
                                                      C_DIAGNOSTIC_WARNING,
                                                      string_format(arena, S8("extra tokens at end of '#{S8}' directive"),
                                                                    c_token_spelling(base, directive)));
            }
            CMacro* macro = c_macro_find_token(first_macro, symbol_table, base, &lex.tokens[token_index]);
            condition_value = macro && macro->definition.defined;
            condition_value ^= directive_kind == C_PREPROCESS_CONDITIONAL_IFNDEF;
        }
        if (!valid)
        {
            if (result->diagnostic_count == diagnostic_count_before)
                c_preprocess_diagnostic_push(arena, result, directive_location, C_DIAGNOSTIC_INVALID_CONDITIONAL,
                                             string_format(arena, S8("invalid preprocessing conditional expression in {S8}"), source_frame->path));
            condition_value = false;
        }
        CConditionalFrame* frame = arena_allocate(arena, CConditionalFrame, 1);
        *frame = (CConditionalFrame){
            .previous = conditional,
            .location = directive_location,
            .parent_active = active,
            .active = active && condition_value,
            .branch_taken = active && condition_value,
        };
        conditional = frame;
        if (conditional->previous == source_frame->conditional_base)
        {
            if (source_frame->guard_state == C_INCLUDE_GUARD_SEARCHING && directive_kind == C_PREPROCESS_CONDITIONAL_IFNDEF && valid)
            {
                CToken guard_name = lex.tokens[token_index];
                source_frame->guard_state = C_INCLUDE_GUARD_GUARDED;
                source_frame->guard = conditional;
                source_frame->guard_symbol =
                    guard_name.symbol ? guard_name.symbol : c_symbol_intern(symbol_table, c_token_spelling(base, guard_name));
            }
            else
            {
                source_frame->guard_state = C_INCLUDE_GUARD_DISQUALIFIED;
            }
        }
    }
    else if (directive_kind == C_PREPROCESS_CONDITIONAL_ELIF || directive_kind == C_PREPROCESS_CONDITIONAL_ELIFDEF ||
             directive_kind == C_PREPROCESS_CONDITIONAL_ELIFNDEF)
    {
        if (conditional == source_frame->conditional_base || conditional->else_seen)
        {
            c_preprocess_diagnostic_push(arena, result, directive_location, C_DIAGNOSTIC_UNMATCHED_CONDITIONAL,
                                         string_format(arena, S8("'#{S8}' has no matching '#if', or follows '#else'"), c_token_spelling(base, directive)));
        }
        else
        {
            if (source_frame->guard_state == C_INCLUDE_GUARD_GUARDED && conditional == source_frame->guard)
            {
                source_frame->guard_state = C_INCLUDE_GUARD_DISQUALIFIED;
            }
            bool condition_value = false;
            bool evaluate = conditional->parent_active && !conditional->branch_taken;
            bool valid = true;
            if (evaluate && directive_kind == C_PREPROCESS_CONDITIONAL_ELIF)
            {
                u32 expression_count = 0;
                CPpToken* expression = c_frame_wrap_conditional_tokens(arena, stamps, source_frame, token_index, line_end, &expression_count);
                valid = c_conditional_evaluate(arena, space, symbol_table, first_macro, stamps,
                                               expression, expression_count, expansion_limit, result, options, probes, source_frame->path,
                                               source_frame->include_origin, &condition_value);
            }
            else if (evaluate)
            {
                valid = token_index != line_end && lex.tokens[token_index].kind == C_TOKEN_IDENTIFIER;
                if (valid)
                {
                    if (token_index + 1 != line_end)
                    {
                        c_preprocess_diagnostic_push_severity(arena, result, directive_location, C_DIAGNOSTIC_EXTRA_DIRECTIVE_TOKENS,
                                                              C_DIAGNOSTIC_WARNING,
                                                              string_format(arena, S8("extra tokens at end of '#{S8}' directive"),
                                                                            c_token_spelling(base, directive)));
                    }
                    CMacro* macro = c_macro_find_token(first_macro, symbol_table, base, &lex.tokens[token_index]);
                    condition_value = macro && macro->definition.defined;
                    condition_value ^= directive_kind == C_PREPROCESS_CONDITIONAL_ELIFNDEF;
                }
            }
            if (!valid)
            {
                if (result->diagnostic_count == diagnostic_count_before)
                    c_preprocess_diagnostic_push(arena, result, directive_location, C_DIAGNOSTIC_INVALID_CONDITIONAL,
                                                 string_format(arena, S8("invalid '#{S8}' expression"), c_token_spelling(base, directive)));
                condition_value = false;
            }
            conditional->active = evaluate && condition_value;
            conditional->branch_taken |= conditional->active;
        }
    }
    else if (directive_kind == C_PREPROCESS_CONDITIONAL_ELSE)
    {
        if (conditional == source_frame->conditional_base || conditional->else_seen)
        {
            c_preprocess_diagnostic_push(arena, result, directive_location, C_DIAGNOSTIC_UNMATCHED_CONDITIONAL,
                                         S8("invalid or unmatched '#else' directive"));
        }
        else
        {
            if (token_index != line_end && conditional->parent_active)
            {
                c_preprocess_diagnostic_push_severity(arena, result, directive_location, C_DIAGNOSTIC_EXTRA_DIRECTIVE_TOKENS,
                                                      C_DIAGNOSTIC_WARNING,
                                                      string_format(arena, S8("extra tokens at end of '#{S8}' directive"),
                                                                    c_token_spelling(base, directive)));
            }
            if (source_frame->guard_state == C_INCLUDE_GUARD_GUARDED && conditional == source_frame->guard)
            {
                source_frame->guard_state = C_INCLUDE_GUARD_DISQUALIFIED;
            }
            conditional->else_seen = true;
            conditional->active = conditional->parent_active && !conditional->branch_taken;
            conditional->branch_taken |= conditional->active;
        }
    }
    else
    {
        if (conditional == source_frame->conditional_base)
        {
            c_preprocess_diagnostic_push(arena, result, directive_location, C_DIAGNOSTIC_UNMATCHED_CONDITIONAL,
                                         S8("invalid or unmatched '#endif' directive"));
        }
        else
        {
            if (token_index != line_end && conditional->parent_active)
            {
                c_preprocess_diagnostic_push_severity(arena, result, directive_location, C_DIAGNOSTIC_EXTRA_DIRECTIVE_TOKENS,
                                                      C_DIAGNOSTIC_WARNING,
                                                      string_format(arena, S8("extra tokens at end of '#{S8}' directive"),
                                                                    c_token_spelling(base, directive)));
            }
            if (source_frame->guard_state == C_INCLUDE_GUARD_GUARDED && conditional == source_frame->guard)
            {
                source_frame->guard_state = C_INCLUDE_GUARD_CLOSED;
            }
            conditional = conditional->previous;
        }
    }
    *conditional_pointer = conditional;
}

typedef struct CPreprocessSourceSegment CPreprocessSourceSegment;
struct CPreprocessSourceSegment
{
    CPreprocessSourceSegment* next;
    u64 first;
    u64 end;
};

BUSTER_C_INTERNAL void c_preprocess_source_segment_append(Arena* arena, CPreprocessSourceSegment** first_segment,
                                                          CPreprocessSourceSegment** last_segment, u64 first, u64 end)
{
    if (first != end)
    {
        CPreprocessSourceSegment* segment = arena_allocate(arena, CPreprocessSourceSegment, 1);
        *segment = (CPreprocessSourceSegment){
            .first = first,
            .end = end,
        };
        if (*last_segment)
        {
            (*last_segment)->next = segment;
        }
        else
        {
            *first_segment = segment;
        }
        *last_segment = segment;
    }
}

BUSTER_C_INTERNAL CSourceLocation c_preprocess_logical_location(CPreprocessSourceFrame* frame, CSourceLocation location)
{
    s64 line = (s64)location.line + frame->line_delta;
    if (line < 1)
    {
        line = 1;
    }
    if (line > (s64)UINT32_MAX)
    {
        line = (s64)UINT32_MAX;
    }
    location.line = (u32)line;
    return location;
}

// A text-line token's location, recovered from its offset through its
// frame's checkpoints only when an invocation needs it; the zero location
// stands in when there is no frame (the parse-side evaluator, which has no
// macros to invoke).
BUSTER_C_INTERNAL CSourceLocation c_preprocess_recover_location(CPreprocessSourceFrame* frame, u32 file, CToken token)
{
    CSourceLocation result = {0};
    if (frame)
    {
        result = c_preprocess_logical_location(frame, c_lex_token_location(&frame->lex, token));
        result.file = file;
    }
    return result;
}

// __LINE__'s value, recovered from the main loop's two-store breadcrumb only
// when the macro actually expands.
BUSTER_C_INTERNAL u32 c_preprocess_builtin_line(CMacro* first)
{
    CPreprocessSourceFrame* frame = first->builtin_frame;
    u32 result;
    if (!frame)
    {
        result = first->builtin_line;
    }
    else
    {
        CToken probe = {
            .offset = first->builtin_token_offset,
        };
        result = c_preprocess_logical_location(frame, c_lex_token_location(&frame->lex, probe)).line;
    }

    return result;
}

// Distinct file identities in first-seen order. `slots` is an open-addressing
// index (index + 1, zero marking empty) over `files`, kept at most half full
// and rebuilt at double capacity, so a lookup compares only paths whose full
// hash slot chain it walks instead of every file seen so far.
typedef struct CPreprocessFileTable CPreprocessFileTable;
struct CPreprocessFileTable
{
    String8* files;
    u32* slots;
    String8 memo_path;
    u32 memo_index;
    u32 count;
    u32 capacity;
    u32 slot_capacity;
#if BUSTER_INCLUDE_TESTS
    // Path comparisons performed, for scaling fixtures.
    u64 compare_count;
#endif
};

BUSTER_C_INTERNAL u32 c_preprocess_file_index(Arena* arena, CPreprocessFileTable* table, String8 path)
{
    u32 result;
    if (table->count && table->memo_path.pointer == path.pointer && table->memo_path.length == path.length)
    {
        result = table->memo_index;
    }
    else
    {
        if ((u64)(table->count + 1) * 2 > table->slot_capacity)
        {
            u32 slot_capacity = table->slot_capacity ? table->slot_capacity * 2 : 32;
            u32* slots = arena_allocate(arena, u32, slot_capacity);
            memset(slots, 0, slot_capacity * sizeof(*slots));
            for (u32 existing = 0; existing < table->count; existing += 1)
            {
                u32 slot = c_name_hash(table->files[existing]) & (slot_capacity - 1);
                while (slots[slot])
                {
                    slot = (slot + 1) & (slot_capacity - 1);
                }
                slots[slot] = existing + 1;
            }
            table->slots = slots;
            table->slot_capacity = slot_capacity;
        }
        u32 mask = table->slot_capacity - 1;
        u32 slot = c_name_hash(path) & mask;
        u32 index = table->count;
        while (table->slots[slot] && index == table->count)
        {
            u32 candidate = table->slots[slot] - 1;
#if BUSTER_INCLUDE_TESTS
            table->compare_count += 1;
#endif
            if (string_equal(table->files[candidate], path))
            {
                index = candidate;
            }
            else
            {
                slot = (slot + 1) & mask;
            }
        }
        if (index == table->count)
        {
            if (table->count == table->capacity)
            {
                u32 capacity = table->capacity ? table->capacity * 2 : 16;
                String8* files = arena_allocate(arena, String8, capacity);
                if (table->count)
                {
                    memcpy(files, table->files, table->count * sizeof(*files));
                }
                table->files = files;
                table->capacity = capacity;
            }
            table->files[table->count++] = path;
            table->slots[slot] = table->count;
        }
        table->memo_path = path;
        table->memo_index = index;
        result = index;
    }

    return result;
}

// Regions are sorted before this cold publication step. Error locations may
// belong to a directive-only header, so register those names before resolving
// the compact original-source metadata. Successful token-free headers remain
// absent from the canonical file table.
BUSTER_C_INTERNAL void c_source_map_finish_origins(Arena* arena, CSourceMap* map, CPreprocessFileTable* files, CPreprocessResult* result)
{
    for (u64 diagnostic = 0; diagnostic < result->diagnostic_count; diagnostic += 1)
    {
        CDiagnostic* item = result->diagnostics + diagnostic;
        u32 offset = item->location.map_offset;
        bool done = false;
        for (u32 step = 0; step < map->count && !done; step += 1)
        {
            u32 low = 0;
            u32 high = map->count;
            while (low < high)
            {
                u32 middle = low + (high - low) / 2;
                if (map->regions[middle].start <= offset) low = middle + 1;
                else high = middle;
            }
            IrSourceRegion* region = map->regions + (low ? low - 1 : 0);
            if (!region->origin_plus_one)
            {
                done = true;
            }
            else if (region->kind == IR_SOURCE_REGION_TEXT)
            {
                CSourceRegionNames names = map->names[region->origin_plus_one - 1];
                if (region->source == UINT32_MAX)
                {
                    region->source = c_preprocess_file_index(arena, files, names.logical);
                }
                item->location.file = region->source;
                item->location.map_offset = offset;
                done = true;
            }
            else
            {
                offset = region->origin_plus_one - 1;
            }
        }
    }
    for (u32 index = 0; index < map->count; index += 1)
    {
        IrSourceRegion* region = map->regions + index;
        if (region->kind == IR_SOURCE_REGION_TEXT && region->origin_plus_one)
        {
            CSourceRegionNames names = map->names[region->origin_plus_one - 1];
            region->origin_plus_one = 0;
            if (region->source != UINT32_MAX)
            {
                u32 physical = string_equal(names.physical, names.logical) ? region->source : c_preprocess_file_index(arena, files, names.physical);
                region->origin_plus_one = physical + 1;
            }
        }
    }
}

BUSTER_C_INTERNAL String8 c_path_directory(String8 path)
{
    for (u64 index = path.length; index; index -= 1)
    {
        char8 character = path.pointer[index - 1];
        if (character == '/' || character == '\\')
        {
            return (String8){
                .pointer = path.pointer,
                .length = index - 1,
            };
        }
    }
    return S8(".");
}

BUSTER_C_INTERNAL bool c_path_is_absolute(String8 path)
{
    return path.length && (path.pointer[0] == '/' || path.pointer[0] == '\\' || (path.length >= 2 && c_ascii_alpha(path.pointer[0]) && path.pointer[1] == ':'));
}

BUSTER_C_INTERNAL u64 c_include_probe_hash(String8 directory, String8 name)
{
    u64 words[2] = {
        buster_hash_64((u8*)directory.pointer, directory.length),
        buster_hash_64((u8*)name.pointer, name.length),
    };
    u64 result = buster_hash_64((u8*)words, sizeof(words));
    // Zero marks an empty slot.
    return result ? result : 1;
}

BUSTER_C_INTERNAL CIncludeProbe* c_include_probe_find(CIncludeProbeTable const* table, String8 directory, String8 name, u64 hash)
{
    CIncludeProbe* result = 0;
    if (table->capacity)
    {
        u32 slot = (u32)hash & (table->capacity - 1);
        while (!result && table->entries[slot].hash)
        {
            CIncludeProbe* entry = table->entries + slot;
            if (entry->hash == hash && string_equal(entry->name, name) && string_equal(entry->directory, directory))
            {
                result = entry;
            }
            slot = (slot + 1) & (table->capacity - 1);
        }
    }
    return result;
}

// Insert a probe known to be absent. Allocation failure leaves the table
// unchanged and returns null: the include is then simply probed again later.
BUSTER_C_INTERNAL CIncludeProbe* c_include_probe_insert(CIncludeProbeTable* table, CIncludeProbe probe)
{
    CIncludeProbe* result = 0;
    bool capacity_ready = table->capacity && table->count + 1 <= table->capacity / 2;
    if (!capacity_ready)
    {
        // As in c_include_file_table_grow, prove the doubling and the arena's
        // remaining reservation before allocating.
        u32 capacity = C_INCLUDE_PROBE_INITIAL_CAPACITY;
        if (table->capacity)
        {
            capacity = table->capacity <= UINT32_MAX / 2 ? table->capacity * 2 : 0;
        }
        u64 byte_count = (u64)capacity * sizeof(*table->entries);
        u64 position = align_forward(table->arena->position, BUSTER_ALIGN_OF(CIncludeProbe));
        bool allocation_fits = capacity && position <= table->arena->reserved_size && byte_count <= table->arena->reserved_size - position;
        if (allocation_fits)
        {
            CIncludeProbe* entries = arena_allocate_zeroed(table->arena, CIncludeProbe, capacity);
            for (u32 old_slot = 0; old_slot < table->capacity; old_slot += 1)
            {
                CIncludeProbe entry = table->entries[old_slot];
                if (entry.hash)
                {
                    u32 slot = (u32)entry.hash & (capacity - 1);
                    while (entries[slot].hash)
                    {
                        slot = (slot + 1) & (capacity - 1);
                    }
                    entries[slot] = entry;
                }
            }
            table->entries = entries;
            table->capacity = capacity;
            capacity_ready = true;
        }
    }
    if (capacity_ready)
    {
        u32 slot = (u32)probe.hash & (table->capacity - 1);
        while (table->entries[slot].hash)
        {
            slot = (slot + 1) & (table->capacity - 1);
        }
        table->entries[slot] = probe;
        table->count += 1;
        result = table->entries + slot;
    }
    return result;
}

// Probe `directory`/`name`. A cached hit reports the path without opening the
// file: `*cached_out` names its probe, `*map_out` stays empty, and a caller
// that wants the bytes maps the path itself once it decides to lex it. Every
// other hit transfers its mapping to `map_out`, or releases it when the caller
// passes none (feature queries).
BUSTER_C_INTERNAL bool c_include_read(Arena* arena, CIncludeProbeTable* probes, String8 directory, String8 name, String8* path_out, String8* source_out,
                                        FileMapRead* map_out, CIncludeProbe** cached_out)
{
    if (map_out)
    {
        *map_out = (FileMapRead){0};
    }
    bool absolute = c_path_is_absolute(name);
    if (absolute)
    {
        directory = S8(".");
    }
    u64 probe_hash = probes ? c_include_probe_hash(directory, name) : 0;
    CIncludeProbe* cached = probes ? c_include_probe_find(probes, directory, name, probe_hash) : 0;
    bool result;
    if (cached)
    {
        result = cached->found;
        if (result)
        {
            *path_out = cached->path;
            *source_out = (String8){0};
            if (cached_out)
            {
                *cached_out = cached;
            }
        }
    }
    else
    {
        String8 path = absolute ? string_format_z(arena, S8("{S8}"), name) : string_format_z(arena, S8("{S8}/{S8}"), directory, name);
        // A mapping exists only when bytes do, so a miss owns nothing.
        FileMapRead map = file_map_read(arena, path, (FileReadOptions){0});
        ByteSlice bytes = map.bytes;
        result = bytes.pointer != 0;
        if (probes)
        {
            c_include_probe_insert(probes, (CIncludeProbe){
                                               .directory = directory,
                                               .name = name,
                                               .path = result ? path : (String8){0},
                                               .identity = c_include_file_identity(path, map.identity),
                                               .hash = probe_hash,
                                               .found = result,
                                           });
        }
        if (result)
        {
            *path_out = path;
            if (map_out)
            {
                *source_out = BYTE_SLICE_TO_STRING(8, bytes);
                *map_out = map;
            }
            else
            {
                *source_out = (String8){0};
                file_map_unmap(map);
            }
        }
    }

    return result;
}

BUSTER_C_INTERNAL bool c_include_builtin(String8 name, String8* path_out, String8* source_out)
{
    String8 source = {0};
    if (string_equal(name, S8("stdbool.h")))
    {
        source = S8("#ifndef __STDBOOL_H\n"
                    "#define __STDBOOL_H\n"
                    "#if __STDC_VERSION__ < 202311L\n"
                    "#define bool _Bool\n"
                    "#define true 1\n"
                    "#define false 0\n"
                    "#endif\n"
                    "#define __bool_true_false_are_defined 1\n"
                    "#endif\n");
    }
    else if (string_equal(name, S8("stdalign.h")))
    {
        source = S8("#ifndef __STDALIGN_H\n"
                    "#define __STDALIGN_H\n"
                    "#if __STDC_VERSION__ < 202311L\n"
                    "#define alignas _Alignas\n"
                    "#define alignof _Alignof\n"
                    "#endif\n"
                    "#endif\n");
    }
    else if (string_equal(name, S8("stdarg.h")))
    {
        // The Windows CRT may declare its public pointer-sized va_list in
        // vadefs.h before stdarg.h is included. Keep that public representation
        // and bridge its storage to the builtin cursor explicitly; typedef
        // spelling must not confer builtin type identity.
        source = S8("#ifndef __STDARG_H\n"
                    "#define __STDARG_H\n"
                    "typedef __builtin_va_list __gnuc_va_list;\n"
                    "#if defined(_WIN32)\n"
                    "#ifndef _VA_LIST_DEFINED\n"
                    "#define _VA_LIST_DEFINED\n"
                    "typedef char *va_list;\n"
                    "#endif\n"
                    "#define __BUSTER_STDARG_PLACE(arguments) (*((__builtin_va_list *)&(arguments)))\n"
                    "#else\n"
                    "typedef __gnuc_va_list va_list;\n"
                    "#define __BUSTER_STDARG_PLACE(arguments) (arguments)\n"
                    "#endif\n"
                    "#define va_start(arguments, last) "
                    "__builtin_va_start(__BUSTER_STDARG_PLACE(arguments), last)\n"
                    "#define va_end(arguments) "
                    "__builtin_va_end(__BUSTER_STDARG_PLACE(arguments))\n"
                    "#define va_arg(arguments, type) "
                    "__builtin_va_arg(__BUSTER_STDARG_PLACE(arguments), type)\n"
                    "#define va_copy(destination, source) "
                    "__builtin_va_copy(__BUSTER_STDARG_PLACE(destination), __BUSTER_STDARG_PLACE(source))\n"
                    "#if defined(_WIN32)\n"
                    "#ifdef __crt_va_start\n"
                    "#undef __crt_va_start\n"
                    "#define __crt_va_start(arguments, last) va_start(arguments, last)\n"
                    "#endif\n"
                    "#ifdef __crt_va_arg\n"
                    "#undef __crt_va_arg\n"
                    "#define __crt_va_arg(arguments, type) va_arg(arguments, type)\n"
                    "#endif\n"
                    "#ifdef __crt_va_end\n"
                    "#undef __crt_va_end\n"
                    "#define __crt_va_end(arguments) va_end(arguments)\n"
                    "#endif\n"
                    "#endif\n"
                    "#endif\n");
    }
    else if (string_equal(name, S8("stddef.h")))
    {
        // Partial resource-header requests must not claim unrelated names.
        // Each definition has its own guard so later full includes complete it.
        source = S8("#if !defined(__need_ptrdiff_t) && !defined(__need_size_t) && "
                    "!defined(__need_rsize_t) && !defined(__need_wchar_t) && "
                    "!defined(__need_NULL) && !defined(__need_max_align_t) && "
                    "!defined(__need_offsetof) && !defined(__need_nullptr_t)\n"
                    "#define __BUSTER_STDDEF_ALL\n"
                    "#endif\n"
                    "#if defined(__BUSTER_STDDEF_ALL) || defined(__need_ptrdiff_t)\n"
                    "#ifndef __BUSTER_PTRDIFF_T\n"
                    "#define __BUSTER_PTRDIFF_T\n"
                    "typedef __PTRDIFF_TYPE__ ptrdiff_t;\n"
                    "#endif\n"
                    "#endif\n"
                    "#if defined(__BUSTER_STDDEF_ALL) || defined(__need_size_t)\n"
                    "#ifndef __BUSTER_SIZE_T\n"
                    "#define __BUSTER_SIZE_T\n"
                    "typedef __SIZE_TYPE__ size_t;\n"
                    "#endif\n"
                    "#endif\n"
                    "#if defined(__need_rsize_t) || (defined(__BUSTER_STDDEF_ALL) && "
                    "defined(__STDC_WANT_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__ >= 1)\n"
                    "#ifndef __BUSTER_RSIZE_T\n"
                    "#define __BUSTER_RSIZE_T\n"
                    "typedef __SIZE_TYPE__ rsize_t;\n"
                    "#endif\n"
                    "#endif\n"
                    "#if defined(__BUSTER_STDDEF_ALL) || defined(__need_wchar_t)\n"
                    "#ifndef __BUSTER_WCHAR_T\n"
                    "#define __BUSTER_WCHAR_T\n"
                    "typedef __WCHAR_TYPE__ wchar_t;\n"
                    "#endif\n"
                    "#endif\n"
                    "#if defined(__BUSTER_STDDEF_ALL) || defined(__need_max_align_t)\n"
                    "#ifndef __BUSTER_MAX_ALIGN_T\n"
                    "#define __BUSTER_MAX_ALIGN_T\n"
                    "#if defined(_MSC_VER)\n"
                    "typedef double max_align_t;\n"
                    "#elif defined(__APPLE__)\n"
                    "typedef long double max_align_t;\n"
                    "#else\n"
                    "typedef struct {\n"
                    "    long long __max_align_ll __attribute__((__aligned__(__alignof__(long long))));\n"
                    "    long double __max_align_ld __attribute__((__aligned__(__alignof__(long double))));\n"
                    "} max_align_t;\n"
                    "#endif\n"
                    "#endif\n"
                    "#endif\n"
                    "#if __STDC_VERSION__ >= 202311L && "
                    "(defined(__BUSTER_STDDEF_ALL) || defined(__need_nullptr_t))\n"
                    "#ifndef __BUSTER_NULLPTR_T\n"
                    "#define __BUSTER_NULLPTR_T\n"
                    "typedef typeof(nullptr) nullptr_t;\n"
                    "#endif\n"
                    "#endif\n"
                    "#if defined(__BUSTER_STDDEF_ALL) || defined(__need_NULL)\n"
                    "#ifndef NULL\n"
                    "#define NULL ((void *)0)\n"
                    "#endif\n"
                    "#endif\n"
                    "#if defined(__BUSTER_STDDEF_ALL) || defined(__need_offsetof)\n"
                    "#define offsetof(type, member) "
                    "__builtin_offsetof(type, member)\n"
                    "#endif\n"
                    "#undef __BUSTER_STDDEF_ALL\n"
                    "#undef __need_ptrdiff_t\n"
                    "#undef __need_size_t\n"
                    "#undef __need_rsize_t\n"
                    "#undef __need_wchar_t\n"
                    "#undef __need_NULL\n"
                    "#undef __need_max_align_t\n"
                    "#undef __need_offsetof\n"
                    "#undef __need_nullptr_t\n");
    }
    else if (string_equal(name, S8("limits.h")))
    {
        // The ISO constants below are ours, but <limits.h> is also where a
        // hosted platform publishes its POSIX limits (PATH_MAX, NAME_MAX,
        // _POSIX_ARG_MAX, ...). Pull the system header in first, exactly as
        // Clang and GCC do, then restate the ISO names so this frontend's
        // model of the target wins. _GCC_LIMITS_H_ is what glibc tests before
        // it reaches back for the compiler's header, so defining it here stops
        // that round trip.
        source = S8("#ifndef __LIMITS_H\n"
                    "#define __LIMITS_H\n"
                    "#if !defined(_GCC_LIMITS_H_)\n"
                    "#define _GCC_LIMITS_H_\n"
                    "#endif\n"
                    "#if __STDC_HOSTED__ && __has_include_next(<limits.h>)\n"
                    "#include_next <limits.h>\n"
                    "#endif\n"
                    "#undef CHAR_BIT\n"
                    "#undef SCHAR_MIN\n"
                    "#undef SCHAR_MAX\n"
                    "#undef UCHAR_MAX\n"
                    "#undef CHAR_MIN\n"
                    "#undef CHAR_MAX\n"
                    "#undef SHRT_MIN\n"
                    "#undef SHRT_MAX\n"
                    "#undef USHRT_MAX\n"
                    "#undef INT_MIN\n"
                    "#undef INT_MAX\n"
                    "#undef UINT_MAX\n"
                    "#undef LONG_MIN\n"
                    "#undef LONG_MAX\n"
                    "#undef ULONG_MAX\n"
                    "#undef LLONG_MIN\n"
                    "#undef LLONG_MAX\n"
                    "#undef ULLONG_MAX\n"
                    "#define CHAR_BIT 8\n"
                    "#define SCHAR_MIN (-128)\n"
                    "#define SCHAR_MAX 127\n"
                    "#define UCHAR_MAX 255\n"
                    // Plain char's range follows the target: signed on
                    // every SysV target, and the predefine block above
                    // spells __CHAR_UNSIGNED__ only where it is not.
                    // _testbuffer's `return CHAR_MAX;` sentinel through a
                    // plain char compared unequal to the unsigned spelling,
                    // and every error path fell through it.
                    "#ifdef __CHAR_UNSIGNED__\n"
                    "#define CHAR_MIN 0\n"
                    "#define CHAR_MAX UCHAR_MAX\n"
                    "#else\n"
                    "#define CHAR_MIN SCHAR_MIN\n"
                    "#define CHAR_MAX SCHAR_MAX\n"
                    "#endif\n"
                    "#define SHRT_MIN (-32768)\n"
                    "#define SHRT_MAX 32767\n"
                    "#define USHRT_MAX 65535\n"
                    "#define INT_MIN (-2147483647 - 1)\n"
                    "#define INT_MAX 2147483647\n"
                    "#define UINT_MAX 4294967295U\n"
                    "#if defined(_WIN64)\n"
                    "#define LONG_MIN INT_MIN\n"
                    "#define LONG_MAX INT_MAX\n"
                    "#define ULONG_MAX UINT_MAX\n"
                    "#else\n"
                    "#define LONG_MIN (-9223372036854775807L - 1)\n"
                    "#define LONG_MAX 9223372036854775807L\n"
                    "#define ULONG_MAX 18446744073709551615UL\n"
                    "#endif\n"
                    "#define LLONG_MIN "
                    "(-9223372036854775807LL - 1)\n"
                    "#define LLONG_MAX 9223372036854775807LL\n"
                    "#define ULLONG_MAX 18446744073709551615ULL\n"
                    "#ifndef MB_LEN_MAX\n"
                    "#define MB_LEN_MAX 16\n"
                    "#endif\n"
                    "#endif\n");
    }
    else if (string_equal(name, S8("tgmath.h")))
    {
        // Clang's resource <tgmath.h> overloads with __attribute__((overloadable)),
        // which this frontend does not implement, and glibc ships none of its
        // own, so a glibc host gets this _Generic version. Each arm calls its
        // function directly: a selected function designator would be an indirect
        // call, and glibc's IFUNC-resolved libm functions (floor, sin, ...) have
        // no usable address here. Hosts without __GLIBC__ (musl, Darwin, MinGW)
        // fall through to the next <tgmath.h> on the search path, as before.
        // <math.h> comes first so __GLIBC__ is known before the test.
        source = S8(
                    "#ifndef __BUSTER_TGMATH_H\n"
                    "#define __BUSTER_TGMATH_H\n"
                    "#include <math.h>\n"
                    "#if defined(__GLIBC__) && !defined(__cplusplus)\n"
                    "#include <complex.h>\n"
                    "#define __TG_V(x) _Generic((x), float: (float)0, long double: (long double)0, float _Complex: (float _Complex)0, double _Complex: (double _Complex)0, long double _Complex: (long double _Complex)0, default: (double)0)\n"
                    "#define __TG_R(t, n, a) _Generic(t, float: n##f a, long double: n##l a, default: n a)\n"
                    "#define __TG_C(t, n, c, a) _Generic(t, float: n##f a, long double: n##l a, float _Complex: c##f a, double _Complex: c a, long double _Complex: c##l a, default: n a)\n"
                    "#define acos(x) __TG_C((x), acos, cacos, (x))\n"
                    "#define asin(x) __TG_C((x), asin, casin, (x))\n"
                    "#define atan(x) __TG_C((x), atan, catan, (x))\n"
                    "#define acosh(x) __TG_C((x), acosh, cacosh, (x))\n"
                    "#define asinh(x) __TG_C((x), asinh, casinh, (x))\n"
                    "#define atanh(x) __TG_C((x), atanh, catanh, (x))\n"
                    "#define cos(x) __TG_C((x), cos, ccos, (x))\n"
                    "#define sin(x) __TG_C((x), sin, csin, (x))\n"
                    "#define tan(x) __TG_C((x), tan, ctan, (x))\n"
                    "#define cosh(x) __TG_C((x), cosh, ccosh, (x))\n"
                    "#define sinh(x) __TG_C((x), sinh, csinh, (x))\n"
                    "#define tanh(x) __TG_C((x), tanh, ctanh, (x))\n"
                    "#define exp(x) __TG_C((x), exp, cexp, (x))\n"
                    "#define log(x) __TG_C((x), log, clog, (x))\n"
                    "#define sqrt(x) __TG_C((x), sqrt, csqrt, (x))\n"
                    "#define fabs(x) __TG_C((x), fabs, cabs, (x))\n"
                    "#define cbrt(x) __TG_R((x), cbrt, (x))\n"
                    "#define ceil(x) __TG_R((x), ceil, (x))\n"
                    "#define erf(x) __TG_R((x), erf, (x))\n"
                    "#define erfc(x) __TG_R((x), erfc, (x))\n"
                    "#define exp2(x) __TG_R((x), exp2, (x))\n"
                    "#define expm1(x) __TG_R((x), expm1, (x))\n"
                    "#define floor(x) __TG_R((x), floor, (x))\n"
                    "#define lgamma(x) __TG_R((x), lgamma, (x))\n"
                    "#define log10(x) __TG_R((x), log10, (x))\n"
                    "#define log1p(x) __TG_R((x), log1p, (x))\n"
                    "#define log2(x) __TG_R((x), log2, (x))\n"
                    "#define logb(x) __TG_R((x), logb, (x))\n"
                    "#define nearbyint(x) __TG_R((x), nearbyint, (x))\n"
                    "#define rint(x) __TG_R((x), rint, (x))\n"
                    "#define round(x) __TG_R((x), round, (x))\n"
                    "#define tgamma(x) __TG_R((x), tgamma, (x))\n"
                    "#define trunc(x) __TG_R((x), trunc, (x))\n"
                    "#define lrint(x) __TG_R((x), lrint, (x))\n"
                    "#define llrint(x) __TG_R((x), llrint, (x))\n"
                    "#define lround(x) __TG_R((x), lround, (x))\n"
                    "#define llround(x) __TG_R((x), llround, (x))\n"
                    "#define ilogb(x) __TG_R((x), ilogb, (x))\n"
                    "#define pow(x, y) __TG_C(__TG_V(x) + __TG_V(y), pow, cpow, (x, y))\n"
                    "#define atan2(x, y) __TG_R(__TG_V(x) + __TG_V(y), atan2, (x, y))\n"
                    "#define copysign(x, y) __TG_R(__TG_V(x) + __TG_V(y), copysign, (x, y))\n"
                    "#define fdim(x, y) __TG_R(__TG_V(x) + __TG_V(y), fdim, (x, y))\n"
                    "#define fmax(x, y) __TG_R(__TG_V(x) + __TG_V(y), fmax, (x, y))\n"
                    "#define fmin(x, y) __TG_R(__TG_V(x) + __TG_V(y), fmin, (x, y))\n"
                    "#define fmod(x, y) __TG_R(__TG_V(x) + __TG_V(y), fmod, (x, y))\n"
                    "#define hypot(x, y) __TG_R(__TG_V(x) + __TG_V(y), hypot, (x, y))\n"
                    "#define nextafter(x, y) __TG_R(__TG_V(x) + __TG_V(y), nextafter, (x, y))\n"
                    "#define remainder(x, y) __TG_R(__TG_V(x) + __TG_V(y), remainder, (x, y))\n"
                    "#define remquo(x, y, q) __TG_R(__TG_V(x) + __TG_V(y), remquo, (x, y, q))\n"
                    "#define fma(x, y, z) __TG_R(__TG_V(x) + __TG_V(y) + __TG_V(z), fma, (x, y, z))\n"
                    "#define ldexp(x, n) __TG_R((x), ldexp, (x, n))\n"
                    "#define scalbn(x, n) __TG_R((x), scalbn, (x, n))\n"
                    "#define scalbln(x, n) __TG_R((x), scalbln, (x, n))\n"
                    "#define frexp(x, p) __TG_R((x), frexp, (x, p))\n"
                    "#define modf(x, p) __TG_R((x), modf, (x, p))\n"
                    "#define nexttoward(x, y) __TG_R((x), nexttoward, (x, y))\n"
                    "#define carg(x) __TG_C((x), carg, carg, (x))\n"
                    "#define cimag(x) __TG_C((x), cimag, cimag, (x))\n"
                    "#define conj(x) __TG_C((x), conj, conj, (x))\n"
                    "#define cproj(x) __TG_C((x), cproj, cproj, (x))\n"
                    "#define creal(x) __TG_C((x), creal, creal, (x))\n"
                    "#else\n"
                    "#if __has_include_next(<tgmath.h>)\n"
                    "#include_next <tgmath.h>\n"
                    "#endif\n"
                    "#endif\n"
                    "#endif\n");
    }
    else if (string_equal(name, S8("buster_test_builtin_include_next.h")))
    {
        source = S8("#include_next <buster_test_builtin_include_next.h>\n");
    }
    bool result;
    if (!source.length)
    {
        result = false;
    }
    else
    {
        *path_out = name;
        *source_out = source;
        result = true;
    }

    return result;
}

BUSTER_C_INTERNAL bool c_include_resolve(Arena* arena, CIncludeProbeTable* probes, CPreprocessOptions options, String8 including_path, String8 name,
                                           bool quoted, String8* path_out, String8* source_out, FileMapRead* map_out, CIncludeProbe** cached_out,
                                           CIncludeSearchOrigin* origin_out)
{
    if (map_out)
    {
        *map_out = (FileMapRead){0};
    }
    if (cached_out)
    {
        *cached_out = 0;
    }
    if (origin_out)
    {
        *origin_out = (CIncludeSearchOrigin){0};
    }
    if (options.disable_external_includes)
    {
        if (c_include_builtin(name, path_out, source_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_BUILTIN};
            }
            return true;
        }
        return false;
    }
    if (c_path_is_absolute(name))
    {
        return c_include_read(arena, probes, S8("."), name, path_out, source_out, map_out, cached_out);
    }
    if (quoted && c_include_read(arena, probes, c_path_directory(including_path), name, path_out, source_out, map_out, cached_out))
    {
        if (origin_out)
        {
            *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_QUOTED};
        }
        return true;
    }
    for (u32 index = 0; index < options.include_path_count; index += 1)
    {
        if (c_include_read(arena, probes, options.include_paths[index], name, path_out, source_out, map_out, cached_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_INCLUDE_PATH, .index = index};
            }
            return true;
        }
    }
    if (c_include_builtin(name, path_out, source_out))
    {
        if (origin_out)
        {
            *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_BUILTIN};
        }
        return true;
    }
    for (u32 index = 0; index < options.system_include_path_count; index += 1)
    {
        if (c_include_read(arena, probes, options.system_include_paths[index], name, path_out, source_out, map_out, cached_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_SYSTEM_PATH, .index = index};
            }
            return true;
        }
    }
    return false;
}

BUSTER_C_INTERNAL bool c_include_resolve_next(Arena* arena, CIncludeProbeTable* probes, CPreprocessOptions options, String8 name, String8* path_out,
                                                String8* source_out, FileMapRead* map_out, CIncludeProbe** cached_out, CIncludeSearchOrigin origin,
                                                CIncludeSearchOrigin* origin_out)
{
    if (map_out)
    {
        *map_out = (FileMapRead){0};
    }
    if (cached_out)
    {
        *cached_out = 0;
    }
    if (origin_out)
    {
        *origin_out = (CIncludeSearchOrigin){0};
    }
    if (options.disable_external_includes)
    {
        if (c_include_builtin(name, path_out, source_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_BUILTIN};
            }
            return true;
        }
        return false;
    }
    u32 include_start = 0;
    u32 system_start = 0;
    if (origin.kind == C_INCLUDE_SEARCH_INCLUDE_PATH)
    {
        include_start = origin.index < options.include_path_count ? origin.index + 1 : options.include_path_count;
    }
    else if (origin.kind == C_INCLUDE_SEARCH_SYSTEM_PATH)
    {
        include_start = options.include_path_count;
        system_start = origin.index < options.system_include_path_count ? origin.index + 1 : options.system_include_path_count;
    }
    else if (origin.kind == C_INCLUDE_SEARCH_BUILTIN)
    {
        include_start = options.include_path_count;
    }
    for (u32 index = include_start; index < options.include_path_count; index += 1)
    {
        if (c_include_read(arena, probes, options.include_paths[index], name, path_out, source_out, map_out, cached_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_INCLUDE_PATH, .index = index};
            }
            return true;
        }
    }
    if (origin.kind != C_INCLUDE_SEARCH_SYSTEM_PATH && origin.kind != C_INCLUDE_SEARCH_BUILTIN && c_include_builtin(name, path_out, source_out))
    {
        if (origin_out)
        {
            *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_BUILTIN};
        }
        return true;
    }
    for (u32 index = system_start; index < options.system_include_path_count; index += 1)
    {
        if (c_include_read(arena, probes, options.system_include_paths[index], name, path_out, source_out, map_out, cached_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_SYSTEM_PATH, .index = index};
            }
            return true;
        }
    }
    return false;
}

BUSTER_C_INTERNAL bool c_include_name(Arena* arena, char8 const* base, CToken* tokens, u32 token_count, bool preserve_characters,
                                        String8* name_out, bool* quoted_out)
{
    bool recognized = false;
    if (token_count == 1 && tokens[0].kind == C_TOKEN_STRING_LITERAL && tokens[0].length >= 2)
    {
        String8 spelling = c_token_spelling(base, tokens[0]);
        *name_out = (String8){
            .pointer = spelling.pointer + 1,
            .length = spelling.length - 2,
        };
        *quoted_out = true;
        recognized = true;
    }
    else if (token_count >= 3 && c_token_is_punctuator(&tokens[0], C_PUNCTUATOR_LESS) && c_token_is_punctuator(&tokens[token_count - 1], C_PUNCTUATOR_GREATER))
    {
        u64 end = (u64)tokens[0].offset + 1;
        for (u32 index = 1; preserve_characters && index < token_count; index += 1)
        {
            // Wrapper arguments may already have been prescanned. Preserve a
            // source span only when every surviving spelling still belongs
            // to that span; synthesized replacements combine as tokens.
            preserve_characters = tokens[index].offset >= end && tokens[index].offset <= tokens[token_count - 1].offset;
            end = (u64)tokens[index].offset + c_token_length(base, tokens[index]);
        }
        if (preserve_characters)
        {
            // Direct header-name characters belong to one translated source
            // span. Token concatenation would erase spaces, tabs and comments
            // that are actual filename characters in this lexical context.
            *name_out = (String8){.pointer = c_token_spelling(base, tokens[0]).pointer + 1,
                                 .length = tokens[token_count - 1].offset - tokens[0].offset - 1};
        }
        else
        {
            u64 length = 0;
            for (u32 index = 1; index + 1 < token_count; index += 1)
            {
                length += c_token_length(base, tokens[index]);
            }
            char8* name = arena_allocate(arena, char8, length + 1);
            u64 output = 0;
            for (u32 index = 1; index + 1 < token_count; index += 1)
            {
                String8 spelling = c_token_spelling(base, tokens[index]);
                if (spelling.length)
                {
                    memcpy(name + output, spelling.pointer, spelling.length);
                }
                output += spelling.length;
            }
            name[output] = 0;
            *name_out = (String8){
                .pointer = name,
                .length = output,
            };
        }
        *quoted_out = false;
        recognized = true;
    }
    return recognized;
}

BUSTER_C_INTERNAL u32 c_include_name_token_count(CToken* tokens, u32 token_count)
{
    u32 result = token_count;
    if (token_count && tokens[0].kind == C_TOKEN_STRING_LITERAL)
    {
        result = 1;
    }
    else if (token_count && c_token_is_punctuator(&tokens[0], C_PUNCTUATOR_LESS))
    {
        for (u32 index = 1; index < token_count && result == token_count; index += 1)
        {
            if (c_token_is_punctuator(&tokens[index], C_PUNCTUATOR_GREATER))
            {
                result = index + 1;
            }
        }
    }
    return result;
}

BUSTER_C_INTERNAL void c_preprocess_define_directive(Arena* arena, CSymbolTable* symbols, CLexResult lex, u64* token_index, CMacro** first_macro, CMacro** last_macro,
                                                       CPreprocessResult* result, CSourceLocation directive_location, u32 command_name_start,
                                                       u32 command_name_end, bool assembly);
BUSTER_C_INTERNAL void c_preprocess_undefine_directive(Arena* arena, CSymbolTable* symbols, CLexResult lex, u64* token_index, CMacro* first_macro,
                                                         CPreprocessResult* result, CSourceLocation directive_location, u32 command_name_start,
                                                         u32 command_name_end, bool allow_builtin);

BUSTER_C_INTERNAL CPreprocessorDefinition c_preprocess_command_operand(String8 operand)
{
    CPreprocessorDefinition result = {
        .name = operand,
        .value = S8("1"),
    };
    bool split = false;
    for (u64 index = 0; index < operand.length && !split; index += 1)
    {
        if (operand.pointer[index] == '=')
        {
            result.name = string_slice(operand, 0, index);
            result.value = string_slice(operand, index + 1, operand.length);
            split = true;
        }
    }
    return result;
}

BUSTER_C_INTERNAL void c_preprocess_command_definition(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols,
                                                         CPreprocessorDefinition definition, CMacro** first_macro, CMacro** last_macro,
                                                         CPreprocessResult* result)
{
    String8 prefix = S8("#define ");
    // Separate the synthetic directive terminator from a trailing backslash so translation cannot splice the replacement away.
    String8 text = string_format(arena, S8("{S8}{S8} {S8} \n"), prefix, definition.name, definition.value);
    CLexResult lex = c_lex_space(arena, space, text, false, result->dialect);
    for (u64 diagnostic_index = 0; diagnostic_index < lex.diagnostic_count; diagnostic_index += 1)
    {
        c_preprocess_diagnostic_copy(arena, result, lex.diagnostics[diagnostic_index]);
    }
    c_symbols_intern_tokens(symbols, lex.spelling_base, lex.tokens, lex.token_shapes, lex.token_count);
    u64 token_index = 2;
    u32 name_start = lex.translated_offset + (u32)prefix.length;
    u32 name_end = name_start + (u32)definition.name.length;
    c_preprocess_define_directive(arena, symbols, lex, &token_index, first_macro, last_macro, result,
                                  (CSourceLocation){.line = 1, .column = 1}, name_start, name_end, false);
}

BUSTER_C_INTERNAL void c_preprocess_command_undefinition(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, String8 name,
                                                           CMacro* first_macro, CPreprocessResult* result)
{
    String8 prefix = S8("#undef ");
    String8 text = string_format(arena, S8("{S8}{S8}\n"), prefix, name);
    CLexResult lex = c_lex_space(arena, space, text, false, result->dialect);
    for (u64 diagnostic_index = 0; diagnostic_index < lex.diagnostic_count; diagnostic_index += 1)
    {
        c_preprocess_diagnostic_copy(arena, result, lex.diagnostics[diagnostic_index]);
    }
    c_symbols_intern_tokens(symbols, lex.spelling_base, lex.tokens, lex.token_shapes, lex.token_count);
    u64 token_index = 2;
    u32 name_start = lex.translated_offset + (u32)prefix.length;
    u32 name_end = name_start + (u32)name.length;
    c_preprocess_undefine_directive(arena, symbols, lex, &token_index, first_macro, result,
                                    (CSourceLocation){.line = 1, .column = 1}, name_start, name_end, true);
}

BUSTER_C_INTERNAL void c_preprocess_command_operations(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CPreprocessOptions options,
                                                         CMacro** first_macro, CMacro** last_macro, CPreprocessResult* result)
{
    if (options.macro_operation_count)
    {
        for (u32 operation_index = 0; operation_index < options.macro_operation_count; operation_index += 1)
        {
            CPreprocessorOperation operation = options.macro_operations[operation_index];
            switch (operation.kind)
            {
            case C_PREPROCESSOR_OPERATION_DEFINE:
                c_preprocess_command_definition(arena, space, symbols, c_preprocess_command_operand(operation.operand), first_macro, last_macro, result);
                break;
            case C_PREPROCESSOR_OPERATION_UNDEFINE:
                c_preprocess_command_undefinition(arena, space, symbols, operation.operand, *first_macro, result);
                break;
            case C_PREPROCESSOR_OPERATION_COUNT:
                c_preprocess_diagnostic_push(arena, result, (CSourceLocation){.line = 1, .column = 1}, C_DIAGNOSTIC_INVALID_MACRO_DEFINITION,
                                             S8("invalid command-line macro operation"));
                break;
            }
        }
    }
    else
    {
        // Compatibility for pre-ordered API callers is the historical order:
        // all definitions first, followed by all undefinitions.
        for (u32 definition_index = 0; definition_index < options.definition_count; definition_index += 1)
        {
            c_preprocess_command_definition(arena, space, symbols, options.definitions[definition_index], first_macro, last_macro, result);
        }
        for (u32 undefinition_index = 0; undefinition_index < options.undefinition_count; undefinition_index += 1)
        {
            c_preprocess_command_undefinition(arena, space, symbols, options.undefinitions[undefinition_index], *first_macro, result);
        }
    }
}

// Dynamic builtins are defined once as builtin-kind macros; the main
// preprocess loop refreshes head-of-list frame/path state each iteration and
// c_macro_replacement_tokens materializes their values only on expansion.
BUSTER_C_INTERNAL void c_preprocess_builtins(Arena* arena, CSymbolTable* symbols, CMacro** first_macro, CMacro** last_macro, String8 path, CSourceLocation location)
{
    CMacro* line_macro = c_macro_define(arena, 0, symbols, first_macro, last_macro, S8("__LINE__"), 0, 0, 0, 0, false, false);
    line_macro->definition.builtin = C_MACRO_BUILTIN_LINE;
    CMacro* file_macro = c_macro_define(arena, 0, symbols, first_macro, last_macro, S8("__FILE__"), 0, 0, 0, 0, false, false);
    file_macro->definition.builtin = C_MACRO_BUILTIN_FILE;
    CMacro* counter_macro = c_macro_define(arena, 0, symbols, first_macro, last_macro, S8("__COUNTER__"), 0, 0, 0, 0, false, false);
    counter_macro->definition.builtin = C_MACRO_BUILTIN_COUNTER;
    CMacro* include_level_macro = c_macro_define(arena, 0, symbols, first_macro, last_macro, S8("__INCLUDE_LEVEL__"), 0, 0, 0, 0, false, false);
    include_level_macro->definition.builtin = C_MACRO_BUILTIN_INCLUDE_LEVEL;
    CMacro* base_file_macro = c_macro_define(arena, 0, symbols, first_macro, last_macro, S8("__BASE_FILE__"), 0, 0, 0, 0, false, false);
    base_file_macro->definition.builtin = C_MACRO_BUILTIN_BASE_FILE;
    CMacro* file_name_macro = c_macro_define(arena, 0, symbols, first_macro, last_macro, S8("__FILE_NAME__"), 0, 0, 0, 0, false, false);
    file_name_macro->definition.builtin = C_MACRO_BUILTIN_FILE_NAME;
    (*first_macro)->builtin_line = location.line;
    (*first_macro)->builtin_path = path;
    (*first_macro)->builtin_base_path = path;
}

BUSTER_C_INTERNAL void c_preprocess_define_directive(Arena* arena, CSymbolTable* symbols, CLexResult lex, u64* token_index, CMacro** first_macro, CMacro** last_macro,
                                                       CPreprocessResult* result, CSourceLocation directive_location, u32 command_name_start,
                                                       u32 command_name_end, bool assembly)
{
    if (*token_index >= lex.token_count || lex.tokens[*token_index].kind != C_TOKEN_IDENTIFIER)
    {
        c_preprocess_diagnostic_push(arena, result, directive_location, C_DIAGNOSTIC_EXPECTED_MACRO_NAME, S8("expected macro name after '#define'"));
        return;
    }
    CToken name = lex.tokens[(*token_index)++];
    bool defined_name = string_equal(c_token_spelling(lex.spelling_base, name), S8("defined"));
    u32 parsed_name_end = name.offset + name.length;
    // Adjacency in translated offsets: a '(' that starts a parameter list
    // must follow the name with nothing between (a line splice deletes its
    // bytes in translation, which matches the standard's post-splice view).
    bool function_like = *token_index < lex.token_count && c_token_is_punctuator(&lex.tokens[*token_index], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                         lex.tokens[*token_index].offset == name.offset + name.length;
    String8* parameters = 0;
    CMacroParameterMap parameter_map = {0};
    u32 parameter_count = 0;
    bool variadic = false;
    bool valid = !defined_name;
    bool command_name_valid = true;
    if (function_like)
    {
        *token_index += 1;
        u64 parameter_capacity = 0;
        while (*token_index + parameter_capacity < lex.token_count)
        {
            CToken token = lex.tokens[*token_index + parameter_capacity];
            if (token.kind == C_TOKEN_NEWLINE || token.kind == C_TOKEN_END_OF_FILE)
            {
                break;
            }
            parameter_capacity += 1;
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                break;
            }
        }
        parameters = arena_allocate(arena, String8, parameter_capacity);
        parameter_map = c_macro_parameter_map_create(arena, parameter_capacity);
        bool expect_parameter = true;
        while (*token_index < lex.token_count)
        {
            CToken token = lex.tokens[*token_index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                valid = valid && (!expect_parameter || parameter_count == 0);
                parsed_name_end = token.offset + (u32)c_token_length(lex.spelling_base, token);
                *token_index += 1;
                break;
            }
            if (!expect_parameter)
            {
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
                {
                    if (variadic)
                    {
                        valid = false;
                        break;
                    }
                    else
                    {
                        expect_parameter = true;
                        *token_index += 1;
                        continue;
                    }
                }
                valid = false;
                break;
            }
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_ELLIPSIS))
            {
                String8 parameter = S8("__VA_ARGS__");
                valid = valid && c_macro_parameter_map_find(&parameter_map, parameters, parameter_count, parameter) < 0;
                parameters[parameter_count] = parameter;
                c_macro_parameter_map_insert(&parameter_map, parameters, parameter_count);
                parameter_count += 1;
                variadic = true;
                expect_parameter = false;
                *token_index += 1;
                continue;
            }
            if (token.kind != C_TOKEN_IDENTIFIER)
            {
                valid = false;
                break;
            }
            String8 parameter = token.symbol ? symbols->names[token.symbol] : c_symbol_ucn_name(symbols, c_token_spelling(lex.spelling_base, token));
            valid = valid && c_macro_parameter_map_find(&parameter_map, parameters, parameter_count, parameter) < 0;
            parameters[parameter_count] = parameter;
            c_macro_parameter_map_insert(&parameter_map, parameters, parameter_count);
            parameter_count += 1;
            *token_index += 1;
            if (*token_index < lex.token_count && c_token_is_punctuator(&lex.tokens[*token_index], C_PUNCTUATOR_ELLIPSIS))
            {
                variadic = true;
                *token_index += 1;
            }
            expect_parameter = false;
        }
        if (*token_index > lex.token_count || (*token_index && !c_token_is_punctuator(&lex.tokens[*token_index - 1], C_PUNCTUATOR_RIGHT_PARENTHESIS)))
        {
            valid = false;
        }
    }
    if (command_name_start != UINT32_MAX)
    {
        command_name_valid = name.offset == command_name_start && parsed_name_end == command_name_end;
        valid = valid && command_name_valid;
    }
    u64 replacement_start = *token_index;
    *token_index = c_preprocess_directive_line_end(lex, c_preprocess_line_end(lex, replacement_start));
    if (!valid)
    {
        String8 message = defined_name ? S8("'defined' cannot be used as a macro name")
                                      : command_name_valid ? S8("invalid function-like macro parameter list")
                                                           : S8("invalid macro name after '#define'");
        c_preprocess_diagnostic_push(arena, result, c_lex_token_location(&lex, name), C_DIAGNOSTIC_INVALID_MACRO_DEFINITION,
                                     message);
        return;
    }
    u64 replacement_capacity = *token_index - replacement_start;
    CToken* replacement = arena_allocate(arena, CToken, replacement_capacity);
    u64 replacement_count = 0;
    for (u64 index = 0; index < replacement_capacity; index += 1)
    {
        CToken candidate = lex.tokens[replacement_start + index];
        if (candidate.kind != C_TOKEN_NEWLINE)
        {
            replacement[replacement_count++] = candidate;
        }
    }
    bool valid_replacement = true;
    CToken invalid_hash = {0};
    if (function_like && !assembly)
    {
        for (u32 index = 0; index < replacement_count && valid_replacement; index += 1)
        {
            CToken token = replacement[index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_HASH))
            {
                bool followed_by_parameter = false;
                if (index + 1 < replacement_count && replacement[index + 1].kind == C_TOKEN_IDENTIFIER)
                {
                    CToken parameter_token = replacement[index + 1];
                    // `# __VA_OPT__` stringifies the optional content (C23 6.10.5.2).
                    followed_by_parameter = variadic && c_macro_is_va_opt(parameter_token);
                    String8 parameter = parameter_token.symbol ? symbols->names[parameter_token.symbol]
                                                               : c_symbol_ucn_name(symbols, c_token_spelling(lex.spelling_base, parameter_token));
                    followed_by_parameter |= c_macro_parameter_map_find(&parameter_map, parameters, parameter_count, parameter) >= 0;
                }
                if (!followed_by_parameter)
                {
                    valid_replacement = false;
                    invalid_hash = token;
                }
            }
        }
    }
    u32 va_opt_index = 0;
    String8 va_opt_violation = c_macro_va_opt_violation(replacement, (u32)replacement_count, variadic, &va_opt_index);
    if (va_opt_violation.length)
    {
        c_preprocess_diagnostic_push(arena, result, c_lex_token_location(&lex, replacement[va_opt_index]), C_DIAGNOSTIC_INVALID_MACRO_DEFINITION,
                                     va_opt_violation);
    }
    else if (!valid_replacement)
    {
        c_preprocess_diagnostic_push(arena, result, c_lex_token_location(&lex, invalid_hash), C_DIAGNOSTIC_INVALID_MACRO_DEFINITION,
                                     S8("'#' is not followed by a macro parameter"));
    }
    else
    {
        CMacro* macro = c_macro_define_indexed(arena, lex.spelling_base, symbols, first_macro, last_macro, c_token_spelling(lex.spelling_base, name),
                                               replacement, (u32)replacement_count, parameters, parameter_count, function_like, variadic,
                                               function_like ? &parameter_map : 0);
#if BUSTER_INCLUDE_TESTS
        result->detail->macro_parameter_compare_count += parameter_map.compare_count;
#endif
        macro->definition.replacement_space = c_macro_replacement_spaces(arena, lex.spelling_base, replacement, (u32)replacement_count);
    }
}

BUSTER_C_INTERNAL void c_preprocess_undefine_directive(Arena* arena, CSymbolTable* symbols, CLexResult lex, u64* token_index, CMacro* first_macro,
                                                         CPreprocessResult* result, CSourceLocation directive_location, u32 command_name_start,
                                                         u32 command_name_end, bool allow_builtin)
{
    bool valid = *token_index < lex.token_count && lex.tokens[*token_index].kind == C_TOKEN_IDENTIFIER;
    if (!valid)
    {
        c_preprocess_diagnostic_push(arena, result, directive_location, C_DIAGNOSTIC_EXPECTED_MACRO_NAME, S8("expected macro name after '#undef'"));
    }
    else
    {
        CToken name = lex.tokens[(*token_index)++];
        bool defined_name = string_equal(c_token_spelling(lex.spelling_base, name), S8("defined"));
        if (command_name_start != UINT32_MAX)
        {
            valid = valid && name.offset == command_name_start && name.offset + name.length == command_name_end;
        }
        valid = valid && !defined_name;
        if (!valid)
        {
            c_preprocess_diagnostic_push(arena, result, c_lex_token_location(&lex, name), C_DIAGNOSTIC_INVALID_MACRO_DEFINITION,
                                         defined_name ? S8("'defined' cannot be used as a macro name") : S8("invalid macro name after '#undef'"));
        }
        else
        {
            CMacro* macro = c_macro_find_token(first_macro, symbols, lex.spelling_base, &name);
            if (macro && (allow_builtin || !macro->definition.builtin))
            {
                macro->definition.defined = false;
            }
        }
    }
}

bool c_preprocess_dialect_is_gnu(CPreprocessDialect dialect)
{
    return dialect == C_PREPROCESS_DIALECT_GNU89 || dialect == C_PREPROCESS_DIALECT_GNU99 || dialect == C_PREPROCESS_DIALECT_GNU11 ||
           dialect == C_PREPROCESS_DIALECT_GNU17 || dialect == C_PREPROCESS_DIALECT_GNU23;
}

BUSTER_C_INTERNAL String8 c_preprocess_standard_version(CPreprocessDialect dialect)
{
    switch (dialect)
    {
    case C_PREPROCESS_DIALECT_GNU99:
    case C_PREPROCESS_DIALECT_C99:
        return S8("199901L");
    case C_PREPROCESS_DIALECT_GNU11:
    case C_PREPROCESS_DIALECT_C11:
        return S8("201112L");
    case C_PREPROCESS_DIALECT_GNU17:
    case C_PREPROCESS_DIALECT_C17:
        return S8("201710L");
    case C_PREPROCESS_DIALECT_GNU23:
    case C_PREPROCESS_DIALECT_C23:
        return S8("202311L");
    case C_PREPROCESS_DIALECT_GNU89:
    case C_PREPROCESS_DIALECT_COUNT:
        return (String8){0};
    }
    return (String8){0};
}

BUSTER_C_SHARED bool c_preprocess_dialect_is_c23(CPreprocessDialect dialect)
{
    return dialect == C_PREPROCESS_DIALECT_GNU23 || dialect == C_PREPROCESS_DIALECT_C23;
}

// Respell the published token at `index` in place: the new spelling goes to
// the end of the space under a source-map stamp at the token's old location,
// and the symbol and shape travel with it.
BUSTER_C_INTERNAL void c_preprocess_respell_token(CSpellingSpace* space, CSourceMap* map, CPreprocessResult* result, u64 index, String8 spelling,
                                                  CTokenKind kind, CPunctuator punctuator, IrSourceMapCursor* cursor)
{
    CToken* token = result->tokens + index;
    CSourceLocation location = c_preprocess_token_location_cursor(result, *token, cursor);
    CToken copy = c_space_token(space, spelling, kind, punctuator);
    c_source_map_append(map, (IrSourceRegion){
                                 .start = copy.offset,
                                 .source = location.file,
                                 .stamp = c_position_from_source_location(location),
                                 .kind = IR_SOURCE_REGION_STAMP,
                                 .origin_plus_one = location.map_offset + 1,
                             });
    // Appends can move the region array; keep the result's view (the
    // recovery above reads through it) current. The new region is at
    // the tail, past every key already published, so the lookups keep
    // answering through the keys until the map is republished.
    result->recovery->map.regions = map->regions;
    token->offset = copy.offset;
    token->length = copy.length;
    token->kind = copy.kind;
    token->punctuator = copy.punctuator;
    result->recovery->token_shapes[index] = c_token_shape_from_token(*token);
    // The symbol travels with the spelling: a respelled token must
    // re-intern or every symbol-keyed consumer would classify it as
    // the old name.
    token->symbol = result->symbols && kind == C_TOKEN_IDENTIFIER ? c_symbol_intern(result->symbols, c_token_spelling(space->base, copy)) : 0;
}

// After macro replacement, canonical identifier bytes and C23 underscore
// aliases travel to consumers that retain names as well as symbol ids. Fuse
// both respellings in the existing final pass; ordinary pre-C23 units whose
// names contain no UCNs do not enter it. Expansion stamps retain raw columns.
BUSTER_C_INTERNAL void c_preprocess_respell_identifiers(CSpellingSpace* space, CSourceMap* map, CPreprocessResult* result)
{
    bool c23 = c_preprocess_dialect_is_c23(result->dialect);
    if (!c23 && (!result->symbols || !result->symbols->has_ucn_names))
    {
        return;
    }
    char8 const* base = space->base;
    IrSourceMapCursor cursor = IR_SOURCE_MAP_CURSOR_EMPTY;
    for (u64 index = 0; index < result->token_count; index += 1)
    {
        CToken* token = result->tokens + index;
        if (token->kind != C_TOKEN_IDENTIFIER)
        {
            continue;
        }
        String8 spelling = c_token_spelling(base, *token);
        String8 respelled = {0};
        if (result->symbols && token->symbol && result->symbols->names[token->symbol].length != spelling.length)
        {
            respelled = result->symbols->names[token->symbol];
        }
        else if (c23 && string_equal(spelling, S8("bool")))
        {
            respelled = S8("_Bool");
        }
        else if (c23 && string_equal(spelling, S8("alignas")))
        {
            respelled = S8("_Alignas");
        }
        else if (c23 && string_equal(spelling, S8("alignof")))
        {
            respelled = S8("_Alignof");
        }
        else if (c23 && string_equal(spelling, S8("static_assert")))
        {
            respelled = S8("_Static_assert");
        }
        else if (c23 && string_equal(spelling, S8("thread_local")))
        {
            respelled = S8("_Thread_local");
        }
        if (respelled.length)
        {
            c_preprocess_respell_token(space, map, result, index, respelled, C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE, &cursor);
        }
    }
}

// True when `token` is an identifier spelling `name`; `symbol` is the id the
// name was interned under (0 when it never was), and uninterned tokens fall
// back to the spelling.
BUSTER_GLOBAL_LOCAL bool c_local_label_word(char8 const* base, CToken token, u32 symbol, String8 name)
{
    bool result = token.kind == C_TOKEN_IDENTIFIER;
    if (result)
    {
        result = token.symbol && symbol ? token.symbol == symbol : c_token_spelling_equal(base, token, name);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool c_local_label_punctuator(CToken token, CPunctuator punctuator)
{
    return token.kind == C_TOKEN_PUNCTUATOR && token.punctuator == punctuator;
}

// True when `index` is in a label position for the local-label rename: a
// definition `name :` after a statement boundary, `goto name`, or the GNU
// address `&&name` in unary position. Labels share no namespace with
// ordinary identifiers, so every other use keeps its spelling.
BUSTER_GLOBAL_LOCAL bool c_local_label_use(char8 const* base, CToken const* tokens, u64 index, u64 end)
{
    CToken previous = tokens[index - 1];
    bool result = false;
    if (index + 1 < end && c_local_label_punctuator(tokens[index + 1], C_PUNCTUATOR_COLON))
    {
        result = c_local_label_punctuator(previous, C_PUNCTUATOR_SEMICOLON) || c_local_label_punctuator(previous, C_PUNCTUATOR_LEFT_BRACE) ||
                 c_local_label_punctuator(previous, C_PUNCTUATOR_RIGHT_BRACE) || c_local_label_punctuator(previous, C_PUNCTUATOR_COLON) ||
                 c_local_label_word(base, previous, 0, S8("else")) || c_local_label_word(base, previous, 0, S8("do"));
    }
    if (!result && c_local_label_word(base, previous, 0, S8("goto")))
    {
        result = true;
    }
    if (!result && c_local_label_punctuator(previous, C_PUNCTUATOR_AMPERSAND_AMPERSAND))
    {
        // Binary `&&` follows an operand: a non-keyword identifier, a
        // literal, `)`, `]`, or a postfix increment.
        CToken operand = tokens[index - 2];
        switch ((CTokenKind)operand.kind)
        {
        case C_TOKEN_IDENTIFIER:
            result = c_local_label_word(base, operand, 0, S8("return"));
            break;
        case C_TOKEN_PUNCTUATOR:
            result = operand.punctuator != C_PUNCTUATOR_RIGHT_PARENTHESIS && operand.punctuator != C_PUNCTUATOR_RIGHT_BRACKET &&
                     operand.punctuator != C_PUNCTUATOR_PLUS_PLUS && operand.punctuator != C_PUNCTUATOR_MINUS_MINUS;
            break;
        default:
            break;
        }
    }
    return result;
}

// The end (exclusive) of the label list of the `asm goto` whose `goto`
// qualifier is at `index`, with the list's first token in *start_out, or 0
// when the statement has no label list: the operands after the fourth
// top-level colon inside the parentheses.
BUSTER_GLOBAL_LOCAL u64 c_local_label_asm_goto_list(CToken const* tokens, u64 index, u64 end, u64* start_out)
{
    u64 result = 0;
    u64 open = index + 1;
    while (open < end && tokens[open].kind == C_TOKEN_IDENTIFIER)
    {
        open += 1;
    }
    u32 depth = 0;
    u32 colons = 0;
    bool closed = !(open < end && c_local_label_punctuator(tokens[open], C_PUNCTUATOR_LEFT_PARENTHESIS));
    for (u64 scan = open; scan < end && !closed; scan += 1)
    {
        CToken token = tokens[scan];
        if (c_local_label_punctuator(token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_local_label_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            depth -= 1;
            closed = depth == 0;
            result = closed && colons == 4 ? scan : 0;
        }
        else if (depth == 1 && c_local_label_punctuator(token, C_PUNCTUATOR_COLON))
        {
            colons += 1;
            *start_out = scan + 1;
        }
    }
    return result;
}

// GNU local labels. `__label__ a, b;` at the start of a block scopes those
// label names to the block, so a statement-expression macro can define its
// labels once per expansion. Lowering keys a function's labels by spelling,
// so the block's label uses -- definitions, `goto`, `&&` and `asm goto`
// lists -- are respelled to a name unique in the translation unit and the
// declaration becomes empty statements. Declarations are visited innermost
// (last) first: an inner redeclaration renames its own uses before the
// enclosing one scans the same range, and those no longer match. A
// malformed or file-scope declaration is left for the parser to diagnose.
// Only units that intern `__label__` enter the pass.
BUSTER_C_INTERNAL void c_preprocess_rename_local_labels(Arena* arena, CSpellingSpace* space, CSourceMap* map, CPreprocessResult* result)
{
    String8 keyword = S8("__label__");
    u32 keyword_symbol = result->symbols ? c_symbol_find(result->symbols, keyword) : 0;
    if (keyword_symbol && result->token_count)
    {
        CToken* tokens = result->tokens;
        u64 count = result->token_count;
        // Declarations, then each declaration's block end; `pending` holds
        // the declarations whose block is still open and `opens` the pending
        // depth at each open brace.
        u64* declarations = arena_allocate(arena, u64, count);
        u64* block_ends = arena_allocate(arena, u64, count);
        u64* pending = arena_allocate(arena, u64, count);
        u64* opens = arena_allocate(arena, u64, count);
        u64 declaration_count = 0;
        u64 pending_count = 0;
        u64 open_count = 0;
        for (u64 index = 0; index < count; index += 1)
        {
            CToken token = tokens[index];
            if (c_local_label_punctuator(token, C_PUNCTUATOR_LEFT_BRACE))
            {
                opens[open_count++] = pending_count;
            }
            else if (c_local_label_punctuator(token, C_PUNCTUATOR_RIGHT_BRACE) && open_count)
            {
                u64 floor = opens[--open_count];
                while (pending_count > floor)
                {
                    block_ends[pending[--pending_count]] = index;
                }
            }
            else if (open_count && c_local_label_word(space->base, token, keyword_symbol, keyword))
            {
                block_ends[declaration_count] = count - 1;
                pending[pending_count++] = declaration_count;
                declarations[declaration_count++] = index;
            }
        }
        IrSourceMapCursor cursor = IR_SOURCE_MAP_CURSOR_EMPTY;
        for (u64 declaration = declaration_count; declaration-- > 0;)
        {
            u64 start = declarations[declaration];
            u64 end = block_ends[declaration];
            u64 semicolon = start + 1;
            bool valid = semicolon < end && tokens[semicolon].kind == C_TOKEN_IDENTIFIER;
            while (valid && semicolon < end && !c_local_label_punctuator(tokens[semicolon], C_PUNCTUATOR_SEMICOLON))
            {
                bool name_slot = ((semicolon - start) & 1) != 0;
                valid = name_slot ? tokens[semicolon].kind == C_TOKEN_IDENTIFIER : c_local_label_punctuator(tokens[semicolon], C_PUNCTUATOR_COMMA);
                semicolon += 1;
            }
            valid = valid && semicolon < end && ((semicolon - start) & 1) == 0;
            if (valid)
            {
                for (u64 name_index = start + 1; name_index < semicolon; name_index += 2)
                {
                    char8 const* base = space->base;
                    CToken name_token = tokens[name_index];
                    String8 name = c_token_spelling(base, name_token);
                    u32 name_symbol = name_token.symbol;
                    String8 renamed = string_format(arena, S8("__local_label_{u64}_{S8}"), declaration, name);
                    u64 list_start = 0;
                    u64 list_end = 0;
                    for (u64 index = semicolon + 1; index < end; index += 1)
                    {
                        CToken token = tokens[index];
                        if (index >= list_end && c_local_label_word(base, token, 0, S8("goto")) &&
                            (c_local_label_word(base, tokens[index - 1], 0, S8("asm")) || c_local_label_word(base, tokens[index - 1], 0, S8("__asm")) ||
                             c_local_label_word(base, tokens[index - 1], 0, S8("__asm__")) || c_local_label_word(base, tokens[index - 1], 0, S8("volatile")) ||
                             c_local_label_word(base, tokens[index - 1], 0, S8("__volatile__")) || c_local_label_word(base, tokens[index - 1], 0, S8("inline"))))
                        {
                            list_end = c_local_label_asm_goto_list(tokens, index, end, &list_start);
                        }
                        bool in_asm_list = index >= list_start && index < list_end;
                        if (c_local_label_word(base, token, name_symbol, name) && (in_asm_list || c_local_label_use(base, tokens, index, end)))
                        {
                            c_preprocess_respell_token(space, map, result, index, renamed, C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE, &cursor);
                            base = space->base;
                        }
                    }
                }
                for (u64 index = start; index < semicolon; index += 1)
                {
                    c_preprocess_respell_token(space, map, result, index, S8(";"), C_TOKEN_PUNCTUATOR, C_PUNCTUATOR_SEMICOLON, &cursor);
                }
            }
        }
    }
}

// GNU's obsolete field designator `member: value` means `.member = value`;
// GCC and Clang accept it in every dialect (#2855). Rewriting it in the final
// stream gives every initializer scanner the one designator spelling it
// already handles, and leaves no identifier-colon pair for label discovery to
// turn into a phantom label. A pair qualifies only directly after an element
// boundary ('{' or ',') whose innermost open delimiter is an initializer
// brace. Nearly every unit has no candidate, so a one-byte shape scan gates
// the delimiter walk, and the walk gates the copy. Returns the tokens added.
enum
{
    C_OBSOLETE_DESIGNATOR_BRACE = 1 << 0,
    C_OBSOLETE_DESIGNATOR_INITIALIZER = 1 << 1,
    // A parenthesis whose closer may be the type of a compound literal: not a
    // call, declarator, attribute, or control-statement head.
    C_OBSOLETE_DESIGNATOR_COMPOUND_LITERAL = 1 << 2,
};

BUSTER_C_INTERNAL bool c_obsolete_designator_compound_literal_parenthesis(CPreprocessResult const* result, CTokenShape const* shapes, u64 open)
{
    CTokenShape before = open ? shapes[open - 1] : (CTokenShape)C_TOKEN_END_OF_FILE;
    bool capable = before != (CTokenShape)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_RIGHT_PARENTHESIS) &&
                   before != (CTokenShape)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_RIGHT_BRACKET);
    if (before == C_TOKEN_IDENTIFIER)
    {
        String8 keyword = c_token_spelling(result->spelling_base, result->tokens[open - 1]);
        capable = string_equal(keyword, S8("return")) || string_equal(keyword, S8("sizeof")) || string_equal(keyword, S8("case")) ||
                  string_equal(keyword, S8("__extension__"));
    }
    return capable;
}

BUSTER_C_INTERNAL u64 c_preprocess_rewrite_obsolete_designators(Arena* arena, CSpellingSpace* space, CSourceMap* map, CPreprocessResult* result)
{
    CSourceMapRecovery* recovery = result->recovery;
    CTokenShape const* shapes = recovery->token_shapes;
    u64 count = result->token_count;
    CTokenShape const colon = (CTokenShape)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_COLON);
    CTokenShape const comma = (CTokenShape)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_COMMA);
    CTokenShape const left_brace = (CTokenShape)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_LEFT_BRACE);
    u64 candidates = 0;
    for (u64 index = 2; index < count; index += 1)
    {
        candidates += (u64)(shapes[index] == colon && shapes[index - 1] == C_TOKEN_IDENTIFIER &&
                            (shapes[index - 2] == left_brace || shapes[index - 2] == comma));
    }
    u64 accepted = 0;
    u32* positions = 0;
    if (candidates)
    {
        positions = arena_allocate(arena, u32, candidates);
        u8* stack = arena_allocate(arena, u8, count);
        u64 depth = 0;
        bool closed_compound_literal = false;
        for (u64 index = 0; index < count; index += 1)
        {
            CPunctuator punctuator = c_token_shape_punctuator(shapes[index]);
            CTokenShape before = index ? shapes[index - 1] : (CTokenShape)C_TOKEN_END_OF_FILE;
            u8 top = depth ? stack[depth - 1] : 0;
            bool in_initializer = (top & C_OBSOLETE_DESIGNATOR_INITIALIZER) != 0;
            if (punctuator == C_PUNCTUATOR_LEFT_BRACE)
            {
                CPunctuator previous = c_token_shape_punctuator(before);
                bool initializer = previous == C_PUNCTUATOR_ASSIGN ||
                                   (previous == C_PUNCTUATOR_RIGHT_PARENTHESIS && closed_compound_literal) ||
                                   (in_initializer && (previous == C_PUNCTUATOR_LEFT_BRACE || previous == C_PUNCTUATOR_COMMA ||
                                                       previous == C_PUNCTUATOR_COLON || previous == C_PUNCTUATOR_RIGHT_BRACKET));
                stack[depth++] = (u8)(C_OBSOLETE_DESIGNATOR_BRACE | (initializer ? C_OBSOLETE_DESIGNATOR_INITIALIZER : 0));
            }
            else if (punctuator == C_PUNCTUATOR_LEFT_PARENTHESIS)
            {
                stack[depth++] = (u8)(c_obsolete_designator_compound_literal_parenthesis(result, shapes, index) ? C_OBSOLETE_DESIGNATOR_COMPOUND_LITERAL : 0);
            }
            else if (punctuator == C_PUNCTUATOR_LEFT_BRACKET)
            {
                stack[depth++] = 0;
            }
            else if (punctuator == C_PUNCTUATOR_RIGHT_PARENTHESIS || punctuator == C_PUNCTUATOR_RIGHT_BRACKET || punctuator == C_PUNCTUATOR_RIGHT_BRACE)
            {
                // Mismatches are the parser's to diagnose; the walk only has
                // to stay bounded.
                closed_compound_literal = punctuator == C_PUNCTUATOR_RIGHT_PARENTHESIS && (top & C_OBSOLETE_DESIGNATOR_COMPOUND_LITERAL) != 0;
                depth = depth ? depth - 1 : 0;
            }
            else if (shapes[index] == C_TOKEN_IDENTIFIER && in_initializer && (top & C_OBSOLETE_DESIGNATOR_BRACE) && index + 1 < count &&
                     shapes[index + 1] == colon && (before == left_brace || before == comma))
            {
                BUSTER_CHECK(accepted < candidates);
                positions[accepted++] = (u32)index;
            }
        }
    }
    if (accepted)
    {
        bool strict = !c_preprocess_dialect_is_gnu(result->dialect);
        u64 new_count = count + accepted;
        CToken* tokens = arena_allocate(recovery->token_arena, CToken, new_count);
        CTokenShape* new_shapes = arena_allocate(recovery->token_shape_arena, CTokenShape, new_count);
        IrSourceMapCursor cursor = IR_SOURCE_MAP_CURSOR_EMPTY;
        u64 source = 0;
        u64 destination = 0;
        for (u64 rewrite = 0; rewrite < accepted; rewrite += 1)
        {
            u64 position = positions[rewrite];
            u64 run = position - source;
            memcpy(tokens + destination, result->tokens + source, run * sizeof(CToken));
            memcpy(new_shapes + destination, shapes + source, run * sizeof(CTokenShape));
            destination += run;
            CToken member = result->tokens[position];
            CSourceLocation location = c_preprocess_token_location_cursor(result, member, &cursor);
            // One two-byte spelling serves both synthesized punctuators, and
            // one stamp maps both to the member name the user wrote.
            CToken dot = c_space_token(space, S8(".="), C_TOKEN_PUNCTUATOR, C_PUNCTUATOR_DOT);
            c_source_map_append(map, (IrSourceRegion){
                                         .start = dot.offset,
                                         .source = location.file,
                                         .stamp = c_position_from_source_location(location),
                                         .kind = IR_SOURCE_REGION_STAMP,
                                         .origin_plus_one = location.map_offset + 1,
                                     });
            // Appends can move the region array; see c_preprocess_respell_identifiers.
            recovery->map.regions = map->regions;
            dot.length = 1;
            CToken assign = {
                .offset = dot.offset + 1,
                .length = 1,
                .kind = C_TOKEN_PUNCTUATOR,
                .punctuator = C_PUNCTUATOR_ASSIGN,
            };
            tokens[destination] = dot;
            new_shapes[destination] = c_token_shape_from_token(dot);
            tokens[destination + 1] = member;
            new_shapes[destination + 1] = shapes[position];
            tokens[destination + 2] = assign;
            new_shapes[destination + 2] = c_token_shape_from_token(assign);
            destination += 3;
            source = position + 2;
            if (strict)
            {
                c_preprocess_diagnostic_push_severity(arena, result, location, C_DIAGNOSTIC_OBSOLETE_DESIGNATOR, C_DIAGNOSTIC_WARNING,
                                                      string_format(arena, S8("use of GNU obsolete field designator '{S8}:'; ISO C spells it '.{S8} ='"),
                                                                    c_token_spelling(result->spelling_base, member),
                                                                    c_token_spelling(result->spelling_base, member)));
            }
        }
        memcpy(tokens + destination, result->tokens + source, (count - source) * sizeof(CToken));
        memcpy(new_shapes + destination, shapes + source, (count - source) * sizeof(CTokenShape));
        // A pack change at or past an inserted '.' moves with its token.
        u64 shifted = 0;
        for (u32 change = 0; change < result->pragma_change_count; change += 1)
        {
            while (shifted < accepted && positions[shifted] <= result->pragma_changes[change].token_index)
            {
                shifted += 1;
            }
            result->pragma_changes[change].token_index += (u32)shifted;
        }
        result->tokens = tokens;
        recovery->token_shapes = new_shapes;
        result->token_count = new_count;
    }
    return accepted;
}

typedef struct CTargetFeatureMacro CTargetFeatureMacro;
struct CTargetFeatureMacro
{
    String8 name;
    TargetCpuFeature feature;
    CpuArch cpu_arch;
};

// Exactly the features `<buster/lib/simd.h>` tests, in the GNU spelling.  The
// list is deliberately short: every entry is a promise that the frontend and
// the canonical backend can compile code written behind it.
BUSTER_C_INTERNAL CTargetFeatureMacro const c_target_feature_macros[] = {
    { S8_INITIALIZER("__SSE2__"), TARGET_CPU_FEATURE_X86_SSE2, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX__"), TARGET_CPU_FEATURE_X86_AVX, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX2__"), TARGET_CPU_FEATURE_X86_AVX2, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512F__"), TARGET_CPU_FEATURE_X86_AVX512F, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512BW__"), TARGET_CPU_FEATURE_X86_AVX512BW, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512VL__"), TARGET_CPU_FEATURE_X86_AVX512VL, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512DQ__"), TARGET_CPU_FEATURE_X86_AVX512DQ, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512VBMI__"), TARGET_CPU_FEATURE_X86_AVX512VBMI, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512VBMI2__"), TARGET_CPU_FEATURE_X86_AVX512VBMI2, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__ARM_NEON"), TARGET_CPU_FEATURE_AARCH64_NEON, CPU_ARCH_AARCH64 },
};

// The preprocessor's end-of-phase boundary. Everything c_preprocess builds is
// allocated in its phase arena; before that arena is released, the seal
// copies the graph a later phase can reach from CPreprocessResult into the
// caller's arena, at exact sizes, and leaves the rest behind: per-file lexed
// rows and shapes, macro records and their argument/expansion staging, class
// masks, include identity tables, line staging, the region-name table and the
// worst-case checkpoint reservations. Tokens, token shapes and spellings live
// in their own recovery arenas and are not part of the phase arena.
//
// The copied graph is complete by construction of these structures, and the
// size checks below fail the build when one of them changes shape, so a new
// pointer field cannot cross the boundary without a matching rule here. A
// pointer the phase arena does not own -- a caller's path, a static spelling,
// the spelling space -- is left as it is.
typedef struct CPreprocessSeal CPreprocessSeal;
struct CPreprocessSeal
{
    Arena* destination;
    Arena* phase_arena;
    u64 phase_start;
    CPhaseBoundaryMetrics metrics;
};

BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(CPreprocessResult) == 184);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(CSourceMapRecovery) == 80);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrSourceMap) == 32);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrSourceRegion) == 80);
// has_ucn_names is a value field copied with the table; its six pointer
// fields still follow the explicit rehoming rules below.
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(CSymbolTable) == 72);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(CDiagnostic) == 48);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(CPreprocessDetail) == 632 + 32 * BUSTER_INCLUDE_TESTS);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(CSourceFileMetrics) == 32);
BUSTER_CT_CHECK(sizeof(CPragmaState) == 8);

BUSTER_C_INTERNAL bool c_preprocess_seal_owns(CPreprocessSeal const* seal, void const* pointer)
{
    return arena_range_contains(seal->phase_arena, seal->phase_start, seal->phase_arena->position, pointer);
}

// One surviving array: copied when the phase arena owns it, kept otherwise.
BUSTER_C_INTERNAL void* c_preprocess_seal_array(CPreprocessSeal* seal, void const* pointer, u64 element_size, u64 count, u64 alignment)
{
    void* result = (void*)pointer;
    if (pointer)
    {
        seal->metrics.references += 1;
        if (c_preprocess_seal_owns(seal, pointer))
        {
            u64 size = arena_array_size(element_size, count);
            result = arena_allocate_bytes(seal->destination, size, alignment);
            if (size)
            {
                memcpy(result, pointer, size);
            }
            seal->metrics.sealed_bytes += size;
            seal->metrics.sealed_objects += 1;
        }
    }
    return result;
}

#define c_preprocess_seal_typed(seal, pointer, T, count) ((T*)c_preprocess_seal_array((seal), (pointer), sizeof(T), (count), BUSTER_ALIGN_OF(T)))

BUSTER_C_INTERNAL String8 c_preprocess_seal_string(CPreprocessSeal* seal, String8 string)
{
    String8 result = string;
    if (string.pointer)
    {
        result.pointer = (char8*)c_preprocess_seal_array(seal, string.pointer, 1, string.length, 1);
    }
    return result;
}

// Regions of one lexed file share its checkpoint arrays -- every include it
// makes splits it into another region -- so each array is copied once and the
// later regions are pointed at that copy. The table is keyed by the old
// address and sized for at most half occupancy.
typedef struct CPreprocessSealCheckpoints CPreprocessSealCheckpoints;
struct CPreprocessSealCheckpoints
{
    IrSourceCheckpoint* old_checkpoints;
    IrSourceCheckpoint* checkpoints;
    u32* checkpoint_offsets;
    u32* checkpoint_pages;
    u32 checkpoint_count;
    u32 checkpoint_page_count;
};

BUSTER_C_INTERNAL void c_preprocess_seal_map(CPreprocessSeal* seal, IrSourceMap* map)
{
    map->keys = c_preprocess_seal_typed(seal, map->keys, IrSourceRegionKey, map->keys ? (u64)map->count + 1 : 0);
    map->pages = c_preprocess_seal_typed(seal, map->pages, u32, map->page_count);
    map->regions = c_preprocess_seal_typed(seal, map->regions, IrSourceRegion, map->count);
    Arena* conflicts[] = {
        seal->destination,
        seal->phase_arena,
    };
    TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32 slot_count = 16;
    while (slot_count < (u64)map->count * 2)
    {
        slot_count <<= 1;
    }
    CPreprocessSealCheckpoints* slots = arena_allocate_zeroed(temporary.arena, CPreprocessSealCheckpoints, slot_count);
    for (u32 index = 0; index < map->count; index += 1)
    {
        IrSourceRegion* region = &map->regions[index];
        if (!region->checkpoints)
        {
            continue;
        }
        u64 hash = ((u64)region->checkpoints >> 4) * 0x9E3779B97F4A7C15ull;
        u32 slot = (u32)(hash >> 32) & (slot_count - 1);
        while (slots[slot].old_checkpoints && slots[slot].old_checkpoints != region->checkpoints)
        {
            slot = (slot + 1) & (slot_count - 1);
        }
        CPreprocessSealCheckpoints* entry = &slots[slot];
        // Every region of one lex carries that lex's counts; a larger count
        // would only ever get a larger copy, never a read past a shorter one.
        if (!entry->old_checkpoints || region->checkpoint_count > entry->checkpoint_count ||
            region->checkpoint_page_count > entry->checkpoint_page_count)
        {
            entry->old_checkpoints = region->checkpoints;
            entry->checkpoints = c_preprocess_seal_typed(seal, region->checkpoints, IrSourceCheckpoint, region->checkpoint_count);
            entry->checkpoint_offsets = c_preprocess_seal_typed(seal, region->checkpoint_offsets, u32, region->checkpoint_count);
            entry->checkpoint_pages = c_preprocess_seal_typed(seal, region->checkpoint_pages, u32, region->checkpoint_page_count);
            entry->checkpoint_count = region->checkpoint_count;
            entry->checkpoint_page_count = region->checkpoint_page_count;
        }
        else
        {
            seal->metrics.references += (u64)(region->checkpoint_offsets != 0) + (u64)(region->checkpoint_pages != 0) + 1;
        }
        region->checkpoints = entry->checkpoints;
        region->checkpoint_offsets = entry->checkpoint_offsets;
        region->checkpoint_pages = entry->checkpoint_pages;
    }
    scratch_end(temporary);
}

BUSTER_C_INTERNAL void c_preprocess_seal(CPreprocessSeal* seal, CPreprocessResult* result)
{
    if (result->recovery)
    {
        CSourceMapRecovery* recovery = c_preprocess_seal_typed(seal, result->recovery, CSourceMapRecovery, 1);
        c_preprocess_seal_map(seal, &recovery->map);
        // Nothing appends a region once the phase ends; the copy is exact.
        recovery->capacity = recovery->map.count;
        result->recovery = recovery;
    }
    if (result->symbols)
    {
        // Parsing and lowering keep interning into the table, so it moves to
        // the caller's arena whole, spare capacity included, and grows there.
        CSymbolTable* table = c_preprocess_seal_typed(seal, result->symbols, CSymbolTable, 1);
        table->arena = seal->destination;
        table->names = c_preprocess_seal_typed(seal, table->names, String8, table->name_capacity);
        table->slots = c_preprocess_seal_typed(seal, table->slots, CSymbolSlot, table->slot_capacity);
        table->builtin_kinds = c_preprocess_seal_typed(seal, table->builtin_kinds, u8, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
        table->class_bits = c_preprocess_seal_typed(seal, table->class_bits, u8, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
        table->word_bits = c_preprocess_seal_typed(seal, table->word_bits, u16, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
        for (u32 id = 1; id <= table->count; id += 1)
        {
            table->names[id] = c_preprocess_seal_string(seal, table->names[id]);
        }
        result->symbols = table;
    }
    result->diagnostics = c_preprocess_seal_typed(seal, result->diagnostics, CDiagnostic, result->diagnostic_count);
    result->diagnostic_capacity = result->diagnostic_count;
    for (u64 index = 0; index < result->diagnostic_count; index += 1)
    {
        result->diagnostics[index].message = c_preprocess_seal_string(seal, result->diagnostics[index].message);
    }
    result->files = c_preprocess_seal_typed(seal, result->files, String8, result->file_count);
    for (u32 index = 0; index < result->file_count; index += 1)
    {
        result->files[index] = c_preprocess_seal_string(seal, result->files[index]);
    }
    result->pragma_changes = c_preprocess_seal_typed(seal, result->pragma_changes, CPragmaState, result->pragma_change_count);
    if (result->detail)
    {
        CPreprocessDetail* detail = c_preprocess_seal_typed(seal, result->detail, CPreprocessDetail, 1);
        detail->output_spacing = c_preprocess_seal_typed(seal, detail->output_spacing, u8, result->token_count);
        detail->lexed_files = c_preprocess_seal_typed(seal, detail->lexed_files, CSourceFileMetrics, detail->lexed_file_count);
        for (u32 index = 0; index < detail->lexed_file_count; index += 1)
        {
            detail->lexed_files[index].path = c_preprocess_seal_string(seal, detail->lexed_files[index].path);
        }
        detail->macro_dump = c_preprocess_seal_string(seal, detail->macro_dump);
        result->detail = detail;
    }
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL u64 c_test_pointer_in_range(void const* pointer, void const* start, void const* end)
{
    u8 const* address = (u8 const*)pointer;
    return address >= (u8 const*)start && address < (u8 const*)end;
}

u64 c_test_preprocess_references_range(CPreprocessResult const* result, void const* start, void const* end)
{
    u64 count = c_test_pointer_in_range(result->tokens, start, end) + c_test_pointer_in_range(result->spelling_base, start, end) +
                c_test_pointer_in_range(result->recovery, start, end) + c_test_pointer_in_range(result->symbols, start, end) +
                c_test_pointer_in_range(result->diagnostics, start, end) + c_test_pointer_in_range(result->files, start, end) +
                c_test_pointer_in_range(result->pragma_changes, start, end) + c_test_pointer_in_range(result->detail, start, end);
    CSourceMapRecovery const* recovery = result->recovery;
    if (recovery)
    {
        IrSourceMap const* map = &recovery->map;
        count += c_test_pointer_in_range(recovery->token_shapes, start, end) + c_test_pointer_in_range(map->keys, start, end) +
                 c_test_pointer_in_range(map->regions, start, end) + c_test_pointer_in_range(map->pages, start, end);
        for (u32 index = 0; index < map->count; index += 1)
        {
            IrSourceRegion const* region = &map->regions[index];
            count += c_test_pointer_in_range(region->checkpoints, start, end) + c_test_pointer_in_range(region->checkpoint_offsets, start, end) +
                     c_test_pointer_in_range(region->checkpoint_pages, start, end);
        }
    }
    CSymbolTable const* table = result->symbols;
    if (table)
    {
        count += c_test_pointer_in_range(table->arena, start, end) + c_test_pointer_in_range(table->names, start, end) +
                 c_test_pointer_in_range(table->slots, start, end) + c_test_pointer_in_range(table->builtin_kinds, start, end) +
                 c_test_pointer_in_range(table->class_bits, start, end) + c_test_pointer_in_range(table->word_bits, start, end);
        for (u32 id = 1; id <= table->count; id += 1)
        {
            count += c_test_pointer_in_range(table->names[id].pointer, start, end);
        }
    }
    for (u64 index = 0; index < result->diagnostic_count; index += 1)
    {
        count += c_test_pointer_in_range(result->diagnostics[index].message.pointer, start, end);
    }
    for (u32 index = 0; index < result->file_count; index += 1)
    {
        count += c_test_pointer_in_range(result->files[index].pointer, start, end);
    }
    if (result->detail)
    {
        count += c_test_pointer_in_range(result->detail->lexed_files, start, end) +
                 c_test_pointer_in_range(result->detail->output_spacing, start, end);
        for (u32 index = 0; index < result->detail->lexed_file_count; index += 1)
        {
            count += c_test_pointer_in_range(result->detail->lexed_files[index].path.pointer, start, end);
        }
    }
    return count;
}
#endif

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL CFrontendReservationPhase c_test_reservation_phase;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL u32 c_test_reservation_ordinal;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL u64 c_test_lowering_reservation;

void c_test_fail_frontend_reservation(CFrontendReservationPhase phase, u32 ordinal)
{
    c_test_reservation_phase = phase;
    c_test_reservation_ordinal = ordinal;
}

bool c_test_frontend_reservation_pending(void)
{
    return c_test_reservation_ordinal != 0;
}

void c_test_lowering_initial_reservation(u64 size)
{
    c_test_lowering_reservation = size;
}
#endif

Arena* c_frontend_arena_create(ArenaCreation creation, CFrontendReservationPhase phase)
{
#if BUSTER_INCLUDE_TESTS
    if (phase == C_FRONTEND_RESERVATION_LOWERING && !creation.reserved_size && c_test_lowering_reservation)
    {
        creation.reserved_size = c_test_lowering_reservation;
        creation.initial_size = BUSTER_MIN(c_test_lowering_reservation, BUSTER_KB(256));
        creation.flags.no_pool = 1;
    }
    if (c_test_reservation_ordinal && c_test_reservation_phase == phase)
    {
        c_test_reservation_ordinal -= 1;
        if (!c_test_reservation_ordinal)
        {
            creation.flags.no_pool = 1;
            arena_test_fail_next_reserve();
        }
    }
#else
    BUSTER_UNUSED(phase);
#endif
    return arena_create(creation);
}

void c_phase_arena_retire(Arena* arena)
{
    arena_retire(arena, C_PHASE_ARENA_RETAINED_SIZE);
}

// `-dM` text: one `#define NAME BODY` line per macro still defined when
// preprocessing ends, in first-definition order (predefines first, then the
// command line and the source; a redefinition keeps its slot). Dynamic
// builtins and compiler-internal helpers are omitted, as GCC and Clang omit
// them. `out` null measures; otherwise the text is written, and both passes
// produce the same length.
BUSTER_C_INTERNAL u64 c_macro_dump_append(char8* out, u64 position, String8 text)
{
    if (out && text.length)
    {
        memcpy(out + position, text.pointer, text.length);
    }
    return position + text.length;
}

BUSTER_C_INTERNAL u64 c_macro_dump_pass(CMacro* first, char8 const* spelling_base, char8* out)
{
    u64 position = 0;
    for (CMacro* macro = first; macro; macro = macro->next)
    {
        CMacroDefinition const* definition = &macro->definition;
        if (definition->defined && definition->builtin == C_MACRO_BUILTIN_NONE && !definition->pragma_like && !definition->dump_hidden)
        {
            position = c_macro_dump_append(out, position, S8("#define "));
            position = c_macro_dump_append(out, position, macro->name);
            if (definition->function_like)
            {
                position = c_macro_dump_append(out, position, S8("("));
                for (u32 parameter_index = 0; parameter_index < definition->parameter_count; parameter_index += 1)
                {
                    String8 parameter = definition->parameters[parameter_index];
                    bool variadic_parameter = definition->variadic && parameter_index + 1 == definition->parameter_count;
                    if (parameter_index)
                    {
                        position = c_macro_dump_append(out, position, S8(","));
                    }
                    if (variadic_parameter && string_equal(parameter, S8("__VA_ARGS__")))
                    {
                        parameter = S8("");
                    }
                    position = c_macro_dump_append(out, position, parameter);
                    if (variadic_parameter)
                    {
                        position = c_macro_dump_append(out, position, S8("..."));
                    }
                }
                position = c_macro_dump_append(out, position, S8(")"));
            }
            position = c_macro_dump_append(out, position, S8(" "));
            for (u32 token_index = 0; token_index < definition->replacement_count; token_index += 1)
            {
                // A null space array reads as spaced, like the builtin markers.
                if (token_index && (!definition->replacement_space || definition->replacement_space[token_index]))
                {
                    position = c_macro_dump_append(out, position, S8(" "));
                }
                position = c_macro_dump_append(out, position, c_token_spelling(spelling_base, definition->replacement[token_index]));
            }
            position = c_macro_dump_append(out, position, S8("\n"));
        }
    }
    return position;
}

BUSTER_C_INTERNAL String8 c_macro_dump_text(Arena* arena, CMacro* first, char8 const* spelling_base)
{
    String8 result = {0};
    u64 length = c_macro_dump_pass(first, spelling_base, 0);
    if (length)
    {
        result.pointer = arena_allocate(arena, char8, length);
        result.length = c_macro_dump_pass(first, spelling_base, result.pointer);
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL void c_test_unit_register(Arena* spelling, Arena* tokens, Arena* shapes, Arena* owner, u64 owner_position);
#endif

BUSTER_C_INTERNAL CPreprocessResult c_preprocess_run(Arena* result_arena, String8 source, CPreprocessOptions options)
{
    if (options.dialect >= C_PREPROCESS_DIALECT_COUNT)
    {
        options.dialect = C_PREPROCESS_DIALECT_GNU17;
    }
    if (!target_data_layout_is_valid(options.data_layout))
    {
        options.data_layout = target_data_layout(options.target);
    }
    CPreprocessResult result = {
        .target = options.target,
        .dialect = options.dialect,
    };
    if (!result_arena)
    {
        return result;
    }
    // The detail block outlives this call in the caller's arena: the result is
    // returned and then copied by value all the way down the frontend, and
    // every copy shares this one block.
#if BUSTER_INCLUDE_TESTS
    u64 result_arena_start = result_arena->position;
#endif
    result.detail = arena_allocate(result_arena, CPreprocessDetail, 1);
    *result.detail = (CPreprocessDetail){
        .data_layout = options.data_layout,
    };
    // The spelling space lives in its own commit-on-demand arena so its
    // offsets stay contiguous under one base without stealing reserve from
    // the caller's arena; the result carries the arena so a caller that
    // compiles many units can release it. The fixed prelude seeds the
    // well-known spellings, and its expansion entry gives synthesized-token
    // offsets the same all-zero location eagerly-built tokens used to carry.
    Arena* spelling_arena = c_frontend_arena_create((ArenaCreation){
        .reserved_size = BUSTER_GB(1),
        .flags = {.pool_reuse = 1},
    }, C_FRONTEND_RESERVATION_PREPROCESS);
    // The output token stream gets the same private-arena treatment as the
    // spelling space, and for the same reason: the final token count is
    // unknown until the last line lands, and the caller's arena interleaves
    // every other preprocessing allocation between output appends. A private
    // reservation keeps the stream contiguous, so every surviving token is
    // written into its final slot exactly once instead of staged per line
    // and copied at the end.
    Arena* token_arena = spelling_arena ? c_frontend_arena_create((ArenaCreation){
                                              .reserved_size = BUSTER_GB(1),
                                              .flags = {.pool_reuse = 1},
                                          }, C_FRONTEND_RESERVATION_PREPROCESS)
                                        : 0;
    Arena* token_shape_arena = token_arena ? c_frontend_arena_create((ArenaCreation){
                                                               .reserved_size = BUSTER_GB(1),
                                                               .flags = {.pool_reuse = 1},
                                                           }, C_FRONTEND_RESERVATION_PREPROCESS)
                                          : 0;
    // Everything below that names `arena` is phase-local: it is allocated in
    // the phase arena above its entry position, and what a later phase needs
    // is copied out by c_preprocess_seal before the release at the end.
    Arena* phase_arena = options.phase_arena;
    bool phase_arena_owned = !phase_arena && token_shape_arena;
    if (phase_arena_owned)
    {
        phase_arena = c_frontend_arena_create((ArenaCreation){
            .reserved_size = C_PHASE_ARENA_RESERVED_SIZE,
            .flags = {.pool_reuse = 1},
        }, C_FRONTEND_RESERVATION_PREPROCESS);
    }
    if (!token_shape_arena || !phase_arena)
    {
        String8 storage = !spelling_arena ? S8("spelling") : !token_arena ? S8("token") :
                          !token_shape_arena ? S8("token-shape") : S8("phase");
        u64 requested_size = !token_shape_arena ? BUSTER_GB(1) : C_PHASE_ARENA_RESERVED_SIZE;
        if (token_shape_arena)
        {
            arena_destroy(token_shape_arena, 1);
        }
        if (token_arena)
        {
            arena_destroy(token_arena, 1);
        }
        if (spelling_arena)
        {
            arena_destroy(spelling_arena, 1);
        }
        result.files = arena_allocate(result_arena, String8, 1);
        result.files[0] = string_duplicate_arena(result_arena, options.source_path.length ? options.source_path : S8("."), false);
        result.file_count = 1;
        c_preprocess_diagnostic_push(result_arena, &result, (CSourceLocation){.line = 1, .column = 1}, C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                     string_format(result_arena, S8("could not reserve {u64} bytes for C preprocessing {S8} arena"), requested_size, storage));
        return result;
    }
    u64 phase_start = phase_arena->position;
    Arena* arena = phase_arena;
    CSpellingSpace space_storage = {
        .base = (char8*)spelling_arena + arena_minimum_position,
        .arena = spelling_arena,
    };
    CSpellingSpace* space = &space_storage;
#if BUSTER_BENCH_ALLOCATIONS
    c_census_space_begin(space->base, spelling_arena->reserved_size - arena_minimum_position);
#endif
    CSourceMapRecovery* recovery = arena_allocate(arena, CSourceMapRecovery, 1);
    *recovery = (CSourceMapRecovery){
        .spelling_arena = spelling_arena,
        .token_arena = token_arena,
        .token_shape_arena = token_shape_arena,
        .phase_arena = options.phase_arena,
    };
    result.recovery = recovery;
#if BUSTER_INCLUDE_TESTS
    c_test_unit_register(spelling_arena, token_arena, token_shape_arena, result_arena, result_arena_start);
#endif
    result.spelling_base = space->base;
    memcpy(c_space_allocate(space, C_SPELLING_PRELUDE_LENGTH), C_SPELLING_PRELUDE_TEXT, C_SPELLING_PRELUDE_LENGTH);
    CSourceMap map = {
        .arena = arena,
    };
    c_source_map_append(&map, (IrSourceRegion){
                                  .start = 0,
                                  .kind = IR_SOURCE_REGION_STAMP,
                              });
    // Preprocessed input already completed phase one; synthesized spellings
    // and command definitions likewise lex with replacement disabled.
    bool trigraphs = !options.already_preprocessed && c_translate_trigraphs_enabled(options.dialect);
    CLexResult root_lex = c_source_cache_lex(arena, space, source, trigraphs, options.dialect, options.source_cache);
    CSourceMetricsFileSet metrics_files = {0};
    c_source_metrics_add(&result.detail->source_lexed, &root_lex.metrics);
    {
        u32 root_row = c_source_metrics_file_row(arena, &metrics_files, options.source_path);
        BUSTER_CHECK(metrics_files.rows && root_row < metrics_files.count);
        if (metrics_files.rows[root_row].lex_count == 0)
        {
            c_source_metrics_add(&result.detail->source_unique, &root_lex.metrics);
        }
        metrics_files.rows[root_row].translated_bytes = root_lex.metrics.translated_bytes;
        metrics_files.rows[root_row].lex_count += 1;
    }
    CSymbolTable* symbol_table = arena_allocate(arena, CSymbolTable, 1);
    *symbol_table = c_symbol_table_create(arena);
    result.symbols = symbol_table;
    // Argument and expansion scratch of macro invocations, released per
    // invocation and destroyed with the unit's preprocessing.
    CMacroExpansionStorage expansion_storage = {0};
    c_symbols_intern_tokens(symbol_table, root_lex.spelling_base, root_lex.tokens, root_lex.token_shapes, root_lex.token_count);
    CPpClassMasks root_class_masks;
    c_pp_class_masks_build(arena, &root_class_masks, root_lex.token_shapes, root_lex.token_count);
    result.diagnostic_capacity = BUSTER_MIN(source.length + options.macro_operation_count + options.definition_count + 1, UINT64_C(64));
    C_DIAGNOSTIC_RESERVATION_CENSUS(PREPROCESS, result.diagnostic_capacity);
    result.diagnostics = arena_allocate(arena, CDiagnostic, result.diagnostic_capacity);
    CMacro* first_macro = 0;
    CMacro* last_macro = 0;
    CToken* standard_replacement = arena_allocate(arena, CToken, 2);
    standard_replacement[0] = (CToken){
        .offset = C_SPELLING_ONE,
        .length = 1,
        .kind = C_TOKEN_PREPROCESSING_NUMBER,
    };
    String8 standard_version = c_preprocess_standard_version(options.dialect);
    standard_replacement[1] = standard_version.length ? c_space_token(space, standard_version, C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE) : (CToken){0};
    c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__STDC__"), standard_replacement, 1, 0, 0, false, false);
    c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__BUSTER__"), standard_replacement, 1, 0, 0, false, false);
    // Transitional word-idiom builtin spellings (#129). The frozen
    // native-retirement fixtures still call these, and their bytes are pinned
    // until a support-policy transition migrates them; delete these aliases in
    // that change. Object-like macros keep the predefined-symbol range and the
    // builtin table to the bit-width names only.
    static char const* simd_word_idiom_aliases[][2] = {
        {"__builtin_buster_simd_splat_byte", "__builtin_buster_simd_splat_u8"},
        {"__builtin_buster_simd_equal_byte", "__builtin_buster_simd_equal_u8"},
        {"__builtin_buster_simd_less_byte", "__builtin_buster_simd_less_u8"},
        {"__builtin_buster_simd_sign_byte", "__builtin_buster_simd_sign_u8"},
        {"__builtin_buster_simd_test_byte", "__builtin_buster_simd_test_u8"},
        {"__builtin_buster_simd_permute2_byte", "__builtin_buster_simd_permute2_u8"},
        {"__builtin_buster_simd_compress_byte", "__builtin_buster_simd_compress_u8"},
        {"__builtin_buster_simd_compress_store_byte", "__builtin_buster_simd_compress_store_u8"},
        {"__builtin_buster_simd_widen_byte", "__builtin_buster_simd_widen_u8"},
        {"__builtin_buster_simd_shift_left_word", "__builtin_buster_simd_shift_left_u32"},
        {"__builtin_buster_simd_ternary_word", "__builtin_buster_simd_ternary_u32"},
        {"__builtin_buster_simd_equal_word", "__builtin_buster_simd_equal_u32"},
        {"__builtin_buster_simd_splat_word", "__builtin_buster_simd_splat_u32"},
        {"__builtin_buster_simd_less_word", "__builtin_buster_simd_less_u32"},
        {"__builtin_buster_simd_compress_word", "__builtin_buster_simd_compress_u32"},
    };
    for (u32 alias_index = 0; alias_index < BUSTER_ARRAY_LENGTH(simd_word_idiom_aliases); alias_index += 1)
    {
        c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)simd_word_idiom_aliases[alias_index][0]),
                                   string_from_pointer((char8*)simd_word_idiom_aliases[alias_index][1]));
    }
    // The GNU version macros are not a dialect switch.  Both reference
    // compilers predefine them in every standard mode -- `clang -std=c99
    // -dM -E` reports `__GNUC__ 4` beside `__STRICT_ANSI__ 1`, and gcc does
    // the same with its own version -- because they describe which language
    // extensions the compiler implements, not which ones the dialect
    // permits.  Gating them on the GNU dialect made `ide cc -std=c99` read a
    // *different* source than clang did from the same header: musl's
    // <tgmath.h> drops its `__typeof__` return casts without `__GNUC__`, so
    // every type-generic macro then has the type of its widest arm and
    // `sizeof pow(2.0, 0.5)` came back as `long double _Complex`.
    // __STRICT_ANSI__ is the dialect switch, and it stays one.
    c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8("__GNUC__"), S8("4"));
    c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8("__GNUC_MINOR__"), S8("2"));
    c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8("__GNUC_PATCHLEVEL__"), S8("1"));
    if (!c_preprocess_dialect_is_gnu(options.dialect))
    {
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__STRICT_ANSI__"), standard_replacement, 1, 0, 0, false, false);
    }
    static char const* atomic_order_names[] = {
        "__ATOMIC_RELAXED", "__ATOMIC_CONSUME", "__ATOMIC_ACQUIRE", "__ATOMIC_RELEASE", "__ATOMIC_ACQ_REL", "__ATOMIC_SEQ_CST",
    };
    for (u32 order = 0; order < BUSTER_ARRAY_LENGTH(atomic_order_names); order += 1)
    {
        CToken* replacement = arena_allocate(arena, CToken, 1);
        replacement[0] = c_space_token(space, string_format(arena, S8("{u32}"), order), C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)atomic_order_names[order]), replacement, 1, 0, 0, false, false);
    }
    static char const* lock_free_macro_names[] = {
        "__CLANG_ATOMIC_BOOL_LOCK_FREE",     "__CLANG_ATOMIC_CHAR_LOCK_FREE",    "__CLANG_ATOMIC_CHAR8_T_LOCK_FREE", "__CLANG_ATOMIC_CHAR16_T_LOCK_FREE",
        "__CLANG_ATOMIC_CHAR32_T_LOCK_FREE", "__CLANG_ATOMIC_WCHAR_T_LOCK_FREE", "__CLANG_ATOMIC_SHORT_LOCK_FREE",   "__CLANG_ATOMIC_INT_LOCK_FREE",
        "__CLANG_ATOMIC_LONG_LOCK_FREE",     "__CLANG_ATOMIC_LLONG_LOCK_FREE",   "__CLANG_ATOMIC_POINTER_LOCK_FREE",
    };
    CToken* lock_free_replacement = arena_allocate(arena, CToken, 1);
    lock_free_replacement[0] = c_space_token(space, S8("2"), C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
    for (u32 macro_index = 0; macro_index < BUSTER_ARRAY_LENGTH(lock_free_macro_names); macro_index += 1)
    {
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)lock_free_macro_names[macro_index]), lock_free_replacement, 1, 0, 0, false,
                       false);
    }
    static char const* gcc_lock_free_macro_names[] = {
        "__GCC_ATOMIC_BOOL_LOCK_FREE", "__GCC_ATOMIC_CHAR_LOCK_FREE", "__GCC_ATOMIC_CHAR16_T_LOCK_FREE", "__GCC_ATOMIC_CHAR32_T_LOCK_FREE",
        "__GCC_ATOMIC_WCHAR_T_LOCK_FREE", "__GCC_ATOMIC_SHORT_LOCK_FREE", "__GCC_ATOMIC_INT_LOCK_FREE", "__GCC_ATOMIC_LONG_LOCK_FREE",
        "__GCC_ATOMIC_LLONG_LOCK_FREE", "__GCC_ATOMIC_POINTER_LOCK_FREE",
    };
    for (u32 macro_index = 0; macro_index < BUSTER_ARRAY_LENGTH(gcc_lock_free_macro_names); macro_index += 1)
    {
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)gcc_lock_free_macro_names[macro_index]),
                       lock_free_replacement, 1, 0, 0, false, false);
    }
    c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8("__GCC_ATOMIC_TEST_AND_SET_TRUEVAL"), S8("1"));
    String8* feature_parameters = arena_allocate(arena, String8, 1);
    feature_parameters[0] = S8("feature");
    static char const* feature_macro_names[] = {
        "__building_module", "__has_attribute", "__has_builtin", "__has_c_attribute", "__has_extension", "__has_feature", "__has_include", "__has_include_next",
        "__is_target_arch", "__is_target_environment", "__is_target_os", "__is_target_vendor",
    };
    for (u32 feature_index = 0; feature_index < BUSTER_ARRAY_LENGTH(feature_macro_names); feature_index += 1)
    {
        String8 feature_name = string_from_pointer((char8*)feature_macro_names[feature_index]);
        CToken* feature_replacement = arena_allocate(arena, CToken, 4);
        feature_replacement[0] = c_space_token(space, feature_name, C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
        feature_replacement[1] = (CToken){
            .offset = C_SPELLING_LEFT_PARENTHESIS,
            .length = 1,
            .kind = C_TOKEN_PUNCTUATOR,
            .punctuator = C_PUNCTUATOR_LEFT_PARENTHESIS,
        };
        feature_replacement[2] = c_space_token(space, S8("feature"), C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
        feature_replacement[3] = (CToken){
            .offset = C_SPELLING_RIGHT_PARENTHESIS,
            .length = 1,
            .kind = C_TOKEN_PUNCTUATOR,
            .punctuator = C_PUNCTUATOR_RIGHT_PARENTHESIS,
        };
        CMacro* feature_macro = c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, feature_name, feature_replacement, 4, feature_parameters, 1, true, false);
        feature_macro->definition.header_query = string_equal(feature_name, S8("__has_include")) || string_equal(feature_name, S8("__has_include_next"));
        feature_macro->definition.target_os_query = string_equal(feature_name, S8("__is_target_os"));
        feature_macro->definition.dump_hidden = true;
    }
    String8* pragma_parameters = arena_allocate(arena, String8, 1);
    pragma_parameters[0] = S8("value");
    CMacro* pragma_macro = c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("_Pragma"), 0, 0, pragma_parameters, 1, true, false);
    pragma_macro->definition.pragma_like = true;
    pragma_macro->definition.dump_hidden = true;
    if (options.target.os == OPERATING_SYSTEM_WINDOWS)
    {
        CMacro* windows_pragma_macro = c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__pragma"), 0, 0, pragma_parameters, 1, true, false);
        windows_pragma_macro->definition.pragma_like = true;
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("C_ASSERT"), 0, 0, pragma_parameters, 1, true, false);
        static char const* c_windows_calling_convention_names[] = {
            "__cdecl", "__stdcall", "__fastcall", "__thiscall", "__vectorcall", "__ptr32", "__ptr64", "__unaligned", "_W64",
        };
        for (u32 calling_convention_index = 0; calling_convention_index < BUSTER_ARRAY_LENGTH(c_windows_calling_convention_names);
             calling_convention_index += 1)
        {
            c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)c_windows_calling_convention_names[calling_convention_index]), 0, 0,
                           0, 0, false, false);
        }
    }
    TargetDataLayout layout = options.data_layout;
    bool apple_target = options.target.os == OPERATING_SYSTEM_MACOS || options.target.os == OPERATING_SYSTEM_IOS;
    bool wasm_target = options.target.cpu_arch == CPU_ARCH_WASM32 || options.target.cpu_arch == CPU_ARCH_WASM64;
    bool int64_uses_long = layout.long_integer.bit_width == 64 && !apple_target && !wasm_target;
    bool intmax_uses_long = layout.long_integer.bit_width == 64 && !wasm_target;
    CToken* constant_parameter_replacement = arena_allocate(arena, CToken, 1);
    constant_parameter_replacement[0] = c_space_token(space, S8("value"), C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
    String8* constant_parameters = arena_allocate(arena, String8, 1);
    constant_parameters[0] = S8("value");
    static char const* constant_macro_names[] = {
        "__INT8_C", "__UINT8_C", "__INT16_C", "__UINT16_C", "__INT32_C", "__UINT32_C", "__INT64_C", "__UINT64_C", "__INTMAX_C", "__UINTMAX_C",
    };
    for (u32 macro_index = 0; macro_index < BUSTER_ARRAY_LENGTH(constant_macro_names); macro_index += 1)
    {
        // SDK stdint headers use these functions as their literal constructors.
        // Keep the promoted small types, unsigned int, and target 64-bit types;
        // identity replacements make UINT64_C(1) << lane a signed int shift.
        String8 suffix = S8("");
        if (macro_index == 5)
        {
            suffix = S8("U");
        }
        else if (macro_index >= 6)
        {
            bool is_unsigned = (macro_index & 1) != 0;
            bool uses_long = macro_index < 8 ? int64_uses_long : intmax_uses_long;
            suffix = uses_long ? (is_unsigned ? S8("UL") : S8("L")) : (is_unsigned ? S8("ULL") : S8("LL"));
        }
        CToken* replacement = constant_parameter_replacement;
        u32 replacement_count = 1;
        if (suffix.length)
        {
            replacement = arena_allocate(arena, CToken, 3);
            replacement[0] = constant_parameter_replacement[0];
            replacement[1] = c_space_token(space, S8("##"), C_TOKEN_PUNCTUATOR, C_PUNCTUATOR_HASH_HASH);
            replacement[2] = c_space_token(space, suffix, C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
            replacement_count = 3;
        }
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)constant_macro_names[macro_index]), replacement, replacement_count,
                       constant_parameters, 1, true, false);
    }
    bool windows_target = options.target.os == OPERATING_SYSTEM_WINDOWS;
    bool short_wchar_target = target_uses_16_bit_wchar(options.target);
    String8 signed_pointer_type = layout.long_integer.size == layout.pointer.size ? S8("long") : S8("long long");
    String8 unsigned_pointer_type = layout.unsigned_long_integer.size == layout.pointer.size ? S8("unsigned long") : S8("unsigned long long");
    bool unsigned_wchar_target = target_uses_unsigned_wchar(options.target);
    bool unsigned_wint_target = !short_wchar_target && !apple_target && !wasm_target;
    // Equal widths do not select the platform's typedef identity. Darwin
    // uses long long for int64_t but long for intmax_t; Wasm uses long long
    // for both, including its LP64 target. Literal constructors must agree.
    String8 signed_64_type = int64_uses_long ? S8("long") : S8("long long");
    String8 unsigned_64_type = int64_uses_long ? S8("unsigned long") : S8("unsigned long long");
    String8 signed_max_type = intmax_uses_long ? S8("long") : S8("long long");
    String8 unsigned_max_type = intmax_uses_long ? S8("unsigned long") : S8("unsigned long long");
#define C_DEFINE_TYPE_MACRO(name, replacement) c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8(name), (replacement))
    if (options.target.cpu_arch == CPU_ARCH_X86_64)
    {
        C_DEFINE_TYPE_MACRO("__amd64__", S8("1"));
        C_DEFINE_TYPE_MACRO("__amd64", S8("1"));
        C_DEFINE_TYPE_MACRO("__x86_64", S8("1"));
        C_DEFINE_TYPE_MACRO("__MMX__", S8("1"));
        C_DEFINE_TYPE_MACRO("__SSE__", S8("1"));
        C_DEFINE_TYPE_MACRO("__FXSR__", S8("1"));
        C_DEFINE_TYPE_MACRO("__SSE_MATH__", S8("1"));
        C_DEFINE_TYPE_MACRO("__SSE2_MATH__", S8("1"));
        C_DEFINE_TYPE_MACRO("__code_model_small__", S8("1"));
        if (options.target.cpu_model == CPU_MODEL_BASELINE)
        {
            C_DEFINE_TYPE_MACRO("__k8", S8("1"));
            C_DEFINE_TYPE_MACRO("__k8__", S8("1"));
        }
    }
    if (options.target.cpu_arch == CPU_ARCH_X86_64 || apple_target)
    {
        C_DEFINE_TYPE_MACRO("__REGISTER_PREFIX__", S8(""));
    }
    if ((options.target.os == OPERATING_SYSTEM_LINUX || options.target.os == OPERATING_SYSTEM_ANDROID) &&
        c_preprocess_dialect_is_gnu(options.dialect))
    {
        C_DEFINE_TYPE_MACRO("linux", S8("1"));
        C_DEFINE_TYPE_MACRO("unix", S8("1"));
    }
    C_DEFINE_TYPE_MACRO("__STDC_UTF_16__", S8("1"));
    C_DEFINE_TYPE_MACRO("__STDC_UTF_32__", S8("1"));
    if (options.dialect == C_PREPROCESS_DIALECT_GNU89)
    {
        C_DEFINE_TYPE_MACRO("__GNUC_GNU_INLINE__", S8("1"));
    }
    else
    {
        C_DEFINE_TYPE_MACRO("__GNUC_STDC_INLINE__", S8("1"));
    }
    C_DEFINE_TYPE_MACRO("__CHAR_BIT__", S8("8"));
    C_DEFINE_TYPE_MACRO("__SIZEOF_SIZE_T__", string_format(arena, S8("{u32}"), layout.pointer.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_PTRDIFF_T__", string_format(arena, S8("{u32}"), layout.pointer.size));
    // This is Clang-suitable alignment, intentionally not abi_max_alignment;
    // #2513 changes that independent ABI property.
    C_DEFINE_TYPE_MACRO("__BIGGEST_ALIGNMENT__",
                        options.target.cpu_arch == CPU_ARCH_BPFEL || (apple_target && options.target.cpu_arch == CPU_ARCH_AARCH64) ? S8("8") : S8("16"));
    String8 pointer_signed_max = layout.pointer.size == 8 ? S8("9223372036854775807") : S8("2147483647");
    String8 pointer_unsigned_max = layout.pointer.size == 8 ? S8("18446744073709551615") : S8("4294967295");
    String8 pointer_signed_suffix = string_equal(signed_pointer_type, S8("long")) ? S8("L")
                                        : string_equal(signed_pointer_type, S8("long long")) ? S8("LL")
                                                                                            : S8("");
    String8 pointer_unsigned_suffix = string_equal(unsigned_pointer_type, S8("unsigned long")) ? S8("UL")
                                          : string_equal(unsigned_pointer_type, S8("unsigned long long")) ? S8("ULL")
                                                                                                        : string_equal(unsigned_pointer_type, S8("unsigned int")) ? S8("U") : S8("");
    String8 int64_signed_max = int64_uses_long ? S8("9223372036854775807L") : S8("9223372036854775807LL");
    String8 int64_unsigned_max = int64_uses_long ? S8("18446744073709551615UL") : S8("18446744073709551615ULL");
    String8 intmax_signed_max = intmax_uses_long ? S8("9223372036854775807L") : S8("9223372036854775807LL");
    String8 intmax_unsigned_max = intmax_uses_long ? S8("18446744073709551615UL") : S8("18446744073709551615ULL");
    C_DEFINE_TYPE_MACRO("__SIZE_MAX__", string_format(arena, S8("{S8}{S8}"), pointer_unsigned_max, pointer_unsigned_suffix));
    C_DEFINE_TYPE_MACRO("__UINTPTR_MAX__", string_format(arena, S8("{S8}{S8}"), pointer_unsigned_max, pointer_unsigned_suffix));
    C_DEFINE_TYPE_MACRO("__PTRDIFF_MAX__", string_format(arena, S8("{S8}{S8}"), pointer_signed_max, pointer_signed_suffix));
    C_DEFINE_TYPE_MACRO("__INTPTR_MAX__", string_format(arena, S8("{S8}{S8}"), pointer_signed_max, pointer_signed_suffix));
    C_DEFINE_TYPE_MACRO("__INTMAX_MAX__", intmax_signed_max);
    C_DEFINE_TYPE_MACRO("__UINTMAX_MAX__", intmax_unsigned_max);
    C_DEFINE_TYPE_MACRO("__INT64_MAX__", int64_signed_max);
    C_DEFINE_TYPE_MACRO("__UINT64_MAX__", int64_unsigned_max);
    C_DEFINE_TYPE_MACRO("__INT8_MAX__", S8("127"));
    C_DEFINE_TYPE_MACRO("__INT16_MAX__", S8("32767"));
    C_DEFINE_TYPE_MACRO("__INT32_MAX__", S8("2147483647"));
    C_DEFINE_TYPE_MACRO("__UINT8_MAX__", S8("255"));
    C_DEFINE_TYPE_MACRO("__UINT16_MAX__", S8("65535"));
    C_DEFINE_TYPE_MACRO("__UINT32_MAX__", S8("4294967295U"));
    String8 integer_widths[] = {S8("8"), S8("16"), S8("32"), S8("64")};
    String8 signed_integer_types[] = {S8("signed char"), S8("short"), S8("int"), signed_64_type};
    String8 unsigned_integer_types[] = {S8("unsigned char"), S8("unsigned short"), S8("unsigned int"), unsigned_64_type};
    String8 signed_integer_maxima[] = {S8("127"), S8("32767"), S8("2147483647"), int64_signed_max};
    String8 unsigned_integer_maxima[] = {S8("255"), S8("65535"), S8("4294967295U"), int64_unsigned_max};
    for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(integer_widths); width_index += 1)
    {
        for (u32 kind_index = 0; kind_index < 2; kind_index += 1)
        {
            String8 family = kind_index ? S8("FAST") : S8("LEAST");
            String8 signed_name = string_format(arena, S8("__INT_{S8}{S8}_TYPE__"), family, integer_widths[width_index]);
            String8 unsigned_name = string_format(arena, S8("__UINT_{S8}{S8}_TYPE__"), family, integer_widths[width_index]);
            String8 signed_max_name = string_format(arena, S8("__INT_{S8}{S8}_MAX__"), family, integer_widths[width_index]);
            String8 unsigned_max_name = string_format(arena, S8("__UINT_{S8}{S8}_MAX__"), family, integer_widths[width_index]);
            String8 signed_width_name = string_format(arena, S8("__INT_{S8}{S8}_WIDTH__"), family, integer_widths[width_index]);
            c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, signed_name, signed_integer_types[width_index]);
            c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, unsigned_name, unsigned_integer_types[width_index]);
            c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, signed_max_name, signed_integer_maxima[width_index]);
            c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, unsigned_max_name, unsigned_integer_maxima[width_index]);
            c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, signed_width_name, integer_widths[width_index]);
        }
    }
    C_DEFINE_TYPE_MACRO("__SIG_ATOMIC_TYPE__", wasm_target ? S8("long") : S8("int"));
    C_DEFINE_TYPE_MACRO("__SIG_ATOMIC_MAX__",
                        wasm_target ? (layout.long_integer.bit_width == 64 ? S8("9223372036854775807L") : S8("2147483647L")) : S8("2147483647"));
    C_DEFINE_TYPE_MACRO("__SIG_ATOMIC_WIDTH__", wasm_target ? string_format(arena, S8("{u32}"), layout.long_integer.bit_width) : S8("32"));
    C_DEFINE_TYPE_MACRO("__PTRDIFF_WIDTH__", string_format(arena, S8("{u32}"), layout.pointer.bit_width));
    C_DEFINE_TYPE_MACRO("__INTMAX_WIDTH__", S8("64"));
    C_DEFINE_TYPE_MACRO("__SHRT_WIDTH__", string_format(arena, S8("{u32}"), layout.short_integer.bit_width));
    C_DEFINE_TYPE_MACRO("__USER_LABEL_PREFIX__", apple_target ? S8("_") : S8(""));
    C_DEFINE_TYPE_MACRO("__FINITE_MATH_ONLY__", S8("0"));
    C_DEFINE_TYPE_MACRO("__ORDER_PDP_ENDIAN__", S8("3412"));
    if (options.position_independent_level)
    {
        String8 level = string_format(arena, S8("{u32}"), options.position_independent_level);
        C_DEFINE_TYPE_MACRO("__PIC__", level);
        C_DEFINE_TYPE_MACRO("__pic__", level);
        if (options.position_independent_executable)
        {
            C_DEFINE_TYPE_MACRO("__PIE__", level);
            C_DEFINE_TYPE_MACRO("__pie__", level);
        }
    }
    // __GCC_HAVE_SYNC_COMPARE_AND_SWAP_* is not defined: the generic __sync compare-and-swap
    // builtins exist but their sized `_1`..`_16` forms do not (#1394),
    // __SIZEOF_FLOAT128__ is unmodeled, __SEG_FS/__SEG_GS have no keywords,
    // and __PRAGMA_REDEFINE_EXTNAME is an unimplemented pragma.
    C_DEFINE_TYPE_MACRO("__SIZE_TYPE__", unsigned_pointer_type);
    C_DEFINE_TYPE_MACRO("__PTRDIFF_TYPE__", signed_pointer_type);
    C_DEFINE_TYPE_MACRO("__INTPTR_TYPE__", signed_pointer_type);
    C_DEFINE_TYPE_MACRO("__UINTPTR_TYPE__", unsigned_pointer_type);
    C_DEFINE_TYPE_MACRO("__INTMAX_TYPE__", signed_max_type);
    C_DEFINE_TYPE_MACRO("__UINTMAX_TYPE__", unsigned_max_type);
    C_DEFINE_TYPE_MACRO("__INT8_TYPE__", S8("signed char"));
    C_DEFINE_TYPE_MACRO("__UINT8_TYPE__", S8("unsigned char"));
    C_DEFINE_TYPE_MACRO("__INT16_TYPE__", S8("short"));
    C_DEFINE_TYPE_MACRO("__UINT16_TYPE__", S8("unsigned short"));
    C_DEFINE_TYPE_MACRO("__INT32_TYPE__", S8("int"));
    C_DEFINE_TYPE_MACRO("__UINT32_TYPE__", S8("unsigned int"));
    C_DEFINE_TYPE_MACRO("__INT64_TYPE__", signed_64_type);
    C_DEFINE_TYPE_MACRO("__UINT64_TYPE__", unsigned_64_type);
    C_DEFINE_TYPE_MACRO("__WCHAR_TYPE__", short_wchar_target ? S8("unsigned short") : unsigned_wchar_target ? S8("unsigned int") : S8("int"));
    C_DEFINE_TYPE_MACRO("__WINT_TYPE__", short_wchar_target ? S8("unsigned short") : unsigned_wint_target ? S8("unsigned int") : S8("int"));
    C_DEFINE_TYPE_MACRO("__SIZEOF_WCHAR_T__", short_wchar_target ? S8("2") : S8("4"));
    C_DEFINE_TYPE_MACRO("__SIZEOF_WINT_T__", short_wchar_target ? S8("2") : S8("4"));
    C_DEFINE_TYPE_MACRO("__WCHAR_MAX__", short_wchar_target ? S8("65535") : unsigned_wchar_target ? S8("4294967295U") : S8("2147483647"));
    C_DEFINE_TYPE_MACRO("__WINT_MAX__", short_wchar_target ? S8("65535") : unsigned_wint_target ? S8("4294967295U") : S8("2147483647"));
    C_DEFINE_TYPE_MACRO("__CHAR8_TYPE__", S8("unsigned char"));
    C_DEFINE_TYPE_MACRO("__CHAR16_TYPE__", S8("unsigned short"));
    C_DEFINE_TYPE_MACRO("__CHAR32_TYPE__", S8("unsigned int"));
    C_DEFINE_TYPE_MACRO("__SIZEOF_POINTER__", string_format(arena, S8("{u32}"), layout.pointer.size));
    C_DEFINE_TYPE_MACRO("__POINTER_WIDTH__", string_format(arena, S8("{u32}"), layout.pointer.bit_width));
    C_DEFINE_TYPE_MACRO("__SIZE_WIDTH__", string_format(arena, S8("{u32}"), layout.pointer.bit_width));
    C_DEFINE_TYPE_MACRO("__INTPTR_WIDTH__", string_format(arena, S8("{u32}"), layout.pointer.bit_width));
    C_DEFINE_TYPE_MACRO("__INT_WIDTH__", string_format(arena, S8("{u32}"), layout.integer.bit_width));
    C_DEFINE_TYPE_MACRO("__LONG_WIDTH__", string_format(arena, S8("{u32}"), layout.long_integer.bit_width));
    C_DEFINE_TYPE_MACRO("__LONG_LONG_WIDTH__", string_format(arena, S8("{u32}"), layout.long_long_integer.bit_width));
    C_DEFINE_TYPE_MACRO("__SCHAR_MAX__", S8("127"));
    C_DEFINE_TYPE_MACRO("__SHRT_MAX__", S8("32767"));
    C_DEFINE_TYPE_MACRO("__INT_MAX__", S8("2147483647"));
    C_DEFINE_TYPE_MACRO("__LONG_MAX__", layout.long_integer.bit_width == 64 ? S8("9223372036854775807L") : S8("2147483647L"));
    C_DEFINE_TYPE_MACRO("__LONG_LONG_MAX__", S8("9223372036854775807LL"));
    C_DEFINE_TYPE_MACRO("__SIZEOF_SHORT__", string_format(arena, S8("{u32}"), layout.short_integer.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_INT__", string_format(arena, S8("{u32}"), layout.integer.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_LONG__", string_format(arena, S8("{u32}"), layout.long_integer.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_LONG_LONG__", string_format(arena, S8("{u32}"), layout.long_long_integer.size));
    if (layout.has_128_bit_integer)
    {
        C_DEFINE_TYPE_MACRO("__SIZEOF_INT128__", string_format(arena, S8("{u32}"), layout.integer128.size));
        // Clang and GCC predefine these two spellings as builtin typedef names
        // for the 128-bit integer keyword rather than declaring them in a
        // header, and code that uses 128-bit arithmetic reaches for them
        // directly (SQLite's decimal and integer-overflow helpers do).  The
        // keyword itself is already understood, so retain it in the expansion.
        C_DEFINE_TYPE_MACRO("__int128_t", S8("__int128"));
        C_DEFINE_TYPE_MACRO("__uint128_t", S8("unsigned __int128"));
    }
    if (options.target.cpu_arch == CPU_ARCH_AARCH64)
    {
        // Clang predefines the sizeless SVE builtin types on AArch64, and
        // glibc's <bits/math-vector.h> (reached from <math.h>) typedefs them
        // whenever the compiler claims Clang 11+, to declare its SVE vector
        // math entry points. Buster has no SVE vector types; incomplete
        // structs keep those prototypes declarable and make any real use a
        // diagnosed incomplete-type error rather than a parse failure.
        C_DEFINE_TYPE_MACRO("__SVFloat32_t", S8("struct __buster_sve_float32"));
        C_DEFINE_TYPE_MACRO("__SVFloat64_t", S8("struct __buster_sve_float64"));
        C_DEFINE_TYPE_MACRO("__SVBool_t", S8("struct __buster_sve_bool"));
    }
    C_DEFINE_TYPE_MACRO("__SIZEOF_FLOAT__", string_format(arena, S8("{u32}"), layout.float_type.size));
    // The `_Float16` half of the <float.h> vocabulary, with clang's own
    // spellings and values. They describe IEEE-754 binary16, which is what
    // `_Float16` is on every target here, and they carry the C23 `F16`
    // suffix exactly as clang's do -- a resource header that reaches for
    // FLT16_MAX gets a constant of the right type, not a double.
    C_DEFINE_TYPE_MACRO("__FLT16_MANT_DIG__", S8("11"));
    C_DEFINE_TYPE_MACRO("__FLT16_DIG__", S8("3"));
    C_DEFINE_TYPE_MACRO("__FLT16_DECIMAL_DIG__", S8("5"));
    C_DEFINE_TYPE_MACRO("__FLT16_MAX__", S8("6.5504e+4F16"));
    C_DEFINE_TYPE_MACRO("__FLT16_NORM_MAX__", S8("6.5504e+4F16"));
    C_DEFINE_TYPE_MACRO("__FLT16_MIN__", S8("6.103515625e-5F16"));
    C_DEFINE_TYPE_MACRO("__FLT16_DENORM_MIN__", S8("5.9604644775390625e-8F16"));
    C_DEFINE_TYPE_MACRO("__FLT16_EPSILON__", S8("9.765625e-4F16"));
    C_DEFINE_TYPE_MACRO("__FLT16_MAX_EXP__", S8("16"));
    C_DEFINE_TYPE_MACRO("__FLT16_MIN_EXP__", S8("(-13)"));
    C_DEFINE_TYPE_MACRO("__FLT16_MAX_10_EXP__", S8("4"));
    C_DEFINE_TYPE_MACRO("__FLT16_MIN_10_EXP__", S8("(-4)"));
    C_DEFINE_TYPE_MACRO("__FLT16_HAS_DENORM__", S8("1"));
    C_DEFINE_TYPE_MACRO("__FLT16_HAS_INFINITY__", S8("1"));
    C_DEFINE_TYPE_MACRO("__FLT16_HAS_QUIET_NAN__", S8("1"));
    C_DEFINE_TYPE_MACRO("__SIZEOF_FLOAT16__", string_format(arena, S8("{u32}"), layout.float16_type.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_DOUBLE__", string_format(arena, S8("{u32}"), layout.double_type.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_LONG_DOUBLE__", string_format(arena, S8("{u32}"), layout.long_double_type.size));
    // The hosted <float.h> supplied by Clang/GCC spells DBL_EPSILON in terms
    // of this predefined macro.  Keep the value in the compiler's prelude so
    // an otherwise ordinary system header does not depend on a host compiler
    // having supplied it first (cJSON uses DBL_EPSILON directly).
    C_DEFINE_TYPE_MACRO("__DBL_EPSILON__", S8("2.220446049250313080847263336181640625e-16"));
    // Lua's number formatting and conversion code queries the corresponding
    // mantissa width through <float.h>'s __DBL_MANT_DIG__ spelling.  Keep the
    // IEEE-754 double contract explicit in the builtin prelude, just as the
    // epsilon above, so an external resource header cannot leave it undefined.
    C_DEFINE_TYPE_MACRO("__DBL_MANT_DIG__", S8("53"));
    // Lua's string formatter also uses the decimal exponent bound from
    // <float.h>; this is the IEEE-754 binary64 value required by
    // DBL_MAX_10_EXP on every hosted target currently supported here.
    C_DEFINE_TYPE_MACRO("__DBL_MAX_10_EXP__", S8("308"));
    // C99's decimal-roundtrip precision macros are exposed by <float.h> in
    // terms of the compiler predefined spellings.  Keep the IEEE-754 values
    // in the prelude so external headers (and yyjson's number tests) do not
    // inherit a host compiler's macro set.
    C_DEFINE_TYPE_MACRO("__FLT_DECIMAL_DIG__", S8("9"));
    C_DEFINE_TYPE_MACRO("__DBL_DECIMAL_DIG__", S8("17"));
    // The rest of the <float.h> vocabulary, with Clang's own spellings: the
    // hosted header defines every FLT_/DBL_/LDBL_ constant in terms of these
    // predefines, so a compiler that lacks one turns an ordinary
    // `double m = DBL_MAX;` into an undeclared identifier -- which is where
    // CPython's Objects/floatobject.c stopped.  float and double are IEEE
    // binary32/64 on every hosted target here; the long double family
    // follows the target's layout below.
    // Supported targets evaluate float/double at their declared precision.
    C_DEFINE_TYPE_MACRO("__FLT_EVAL_METHOD__", S8("0"));
    C_DEFINE_TYPE_MACRO("__FLT_RADIX__", S8("2"));
    C_DEFINE_TYPE_MACRO("__FLT_MANT_DIG__", S8("24"));
    C_DEFINE_TYPE_MACRO("__FLT_DIG__", S8("6"));
    C_DEFINE_TYPE_MACRO("__FLT_MAX__", S8("3.40282347e+38F"));
    C_DEFINE_TYPE_MACRO("__FLT_NORM_MAX__", S8("3.40282347e+38F"));
    C_DEFINE_TYPE_MACRO("__FLT_MIN__", S8("1.17549435e-38F"));
    C_DEFINE_TYPE_MACRO("__FLT_DENORM_MIN__", S8("1.40129846e-45F"));
    C_DEFINE_TYPE_MACRO("__FLT_EPSILON__", S8("1.19209290e-7F"));
    C_DEFINE_TYPE_MACRO("__FLT_MAX_EXP__", S8("128"));
    C_DEFINE_TYPE_MACRO("__FLT_MIN_EXP__", S8("(-125)"));
    C_DEFINE_TYPE_MACRO("__FLT_MAX_10_EXP__", S8("38"));
    C_DEFINE_TYPE_MACRO("__FLT_MIN_10_EXP__", S8("(-37)"));
    C_DEFINE_TYPE_MACRO("__FLT_HAS_DENORM__", S8("1"));
    C_DEFINE_TYPE_MACRO("__FLT_HAS_INFINITY__", S8("1"));
    C_DEFINE_TYPE_MACRO("__FLT_HAS_QUIET_NAN__", S8("1"));
    C_DEFINE_TYPE_MACRO("__DBL_DIG__", S8("15"));
    C_DEFINE_TYPE_MACRO("__DBL_MAX__", S8("1.7976931348623157e+308"));
    C_DEFINE_TYPE_MACRO("__DBL_NORM_MAX__", S8("1.7976931348623157e+308"));
    C_DEFINE_TYPE_MACRO("__DBL_MIN__", S8("2.2250738585072014e-308"));
    C_DEFINE_TYPE_MACRO("__DBL_DENORM_MIN__", S8("4.9406564584124654e-324"));
    C_DEFINE_TYPE_MACRO("__DBL_MAX_EXP__", S8("1024"));
    C_DEFINE_TYPE_MACRO("__DBL_MIN_EXP__", S8("(-1021)"));
    C_DEFINE_TYPE_MACRO("__DBL_MIN_10_EXP__", S8("(-307)"));
    C_DEFINE_TYPE_MACRO("__DBL_HAS_DENORM__", S8("1"));
    C_DEFINE_TYPE_MACRO("__DBL_HAS_INFINITY__", S8("1"));
    C_DEFINE_TYPE_MACRO("__DBL_HAS_QUIET_NAN__", S8("1"));
    C_DEFINE_TYPE_MACRO("__DECIMAL_DIG__", S8("__LDBL_DECIMAL_DIG__"));
    {
        // <float.h> spells FLT_ROUNDS as `(__builtin_flt_rounds())`, a call
        // that reads the dynamic rounding mode.  The prelude answers 1 --
        // round to nearest, every hosted process's startup mode, and GCC's
        // own historical answer -- as a zero-parameter function-like macro,
        // so the spelling folds before lowering ever meets a builtin call.
        // The documented cost: a program that calls fesetround and then
        // reads FLT_ROUNDS sees a stale 1, exactly as it did under GCC
        // before the builtin existed.
        CLexResult flt_rounds_lex = c_lex_space(arena, space, S8("1"), false, options.dialect);
        c_symbols_intern_tokens(symbol_table, flt_rounds_lex.spelling_base, flt_rounds_lex.tokens, flt_rounds_lex.token_shapes, flt_rounds_lex.token_count);
        CMacro* flt_rounds_macro =
            c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__builtin_flt_rounds"), flt_rounds_lex.tokens, 1, 0, 0, true, false);
        flt_rounds_macro->definition.replacement_space = c_macro_replacement_spaces(arena, flt_rounds_lex.spelling_base, flt_rounds_lex.tokens, 1);
    }
    C_DEFINE_TYPE_MACRO("__LDBL_HAS_DENORM__", S8("1"));
    C_DEFINE_TYPE_MACRO("__LDBL_HAS_INFINITY__", S8("1"));
    C_DEFINE_TYPE_MACRO("__LDBL_HAS_QUIET_NAN__", S8("1"));
    if (layout.long_double_type.bit_width == 80)
    {
        // The x87 extended format, x86-64 Linux's long double.
        C_DEFINE_TYPE_MACRO("__LDBL_MANT_DIG__", S8("64"));
        C_DEFINE_TYPE_MACRO("__LDBL_DIG__", S8("18"));
        C_DEFINE_TYPE_MACRO("__LDBL_DECIMAL_DIG__", S8("21"));
        C_DEFINE_TYPE_MACRO("__LDBL_MAX__", S8("1.18973149535723176502e+4932L"));
        C_DEFINE_TYPE_MACRO("__LDBL_NORM_MAX__", S8("1.18973149535723176502e+4932L"));
        C_DEFINE_TYPE_MACRO("__LDBL_MIN__", S8("3.36210314311209350626e-4932L"));
        C_DEFINE_TYPE_MACRO("__LDBL_DENORM_MIN__", S8("3.64519953188247460253e-4951L"));
        C_DEFINE_TYPE_MACRO("__LDBL_EPSILON__", S8("1.08420217248550443401e-19L"));
        C_DEFINE_TYPE_MACRO("__LDBL_MAX_EXP__", S8("16384"));
        C_DEFINE_TYPE_MACRO("__LDBL_MIN_EXP__", S8("(-16381)"));
        C_DEFINE_TYPE_MACRO("__LDBL_MAX_10_EXP__", S8("4932"));
        C_DEFINE_TYPE_MACRO("__LDBL_MIN_10_EXP__", S8("(-4931)"));
    }
    else if (layout.long_double_type.bit_width == 128)
    {
        // IEEE binary128, AArch64 Linux's long double.
        C_DEFINE_TYPE_MACRO("__LDBL_MANT_DIG__", S8("113"));
        C_DEFINE_TYPE_MACRO("__LDBL_DIG__", S8("33"));
        C_DEFINE_TYPE_MACRO("__LDBL_DECIMAL_DIG__", S8("36"));
        C_DEFINE_TYPE_MACRO("__LDBL_MAX__", S8("1.18973149535723176508575932662800702e+4932L"));
        C_DEFINE_TYPE_MACRO("__LDBL_NORM_MAX__", S8("1.18973149535723176508575932662800702e+4932L"));
        C_DEFINE_TYPE_MACRO("__LDBL_MIN__", S8("3.36210314311209350626267781732175260e-4932L"));
        C_DEFINE_TYPE_MACRO("__LDBL_DENORM_MIN__", S8("6.47517511943802511092443895822764655e-4966L"));
        C_DEFINE_TYPE_MACRO("__LDBL_EPSILON__", S8("1.92592994438723585305597794258492732e-34L"));
        C_DEFINE_TYPE_MACRO("__LDBL_MAX_EXP__", S8("16384"));
        C_DEFINE_TYPE_MACRO("__LDBL_MIN_EXP__", S8("(-16381)"));
        C_DEFINE_TYPE_MACRO("__LDBL_MAX_10_EXP__", S8("4932"));
        C_DEFINE_TYPE_MACRO("__LDBL_MIN_10_EXP__", S8("(-4931)"));
    }
    else
    {
        // long double is double: Windows, and any other 64-bit layout.
        C_DEFINE_TYPE_MACRO("__LDBL_MANT_DIG__", S8("53"));
        C_DEFINE_TYPE_MACRO("__LDBL_DIG__", S8("15"));
        C_DEFINE_TYPE_MACRO("__LDBL_DECIMAL_DIG__", S8("17"));
        C_DEFINE_TYPE_MACRO("__LDBL_MAX__", S8("1.7976931348623157e+308L"));
        C_DEFINE_TYPE_MACRO("__LDBL_NORM_MAX__", S8("1.7976931348623157e+308L"));
        C_DEFINE_TYPE_MACRO("__LDBL_MIN__", S8("2.2250738585072014e-308L"));
        C_DEFINE_TYPE_MACRO("__LDBL_DENORM_MIN__", S8("4.9406564584124654e-324L"));
        C_DEFINE_TYPE_MACRO("__LDBL_EPSILON__", S8("2.2204460492503131e-16L"));
        C_DEFINE_TYPE_MACRO("__LDBL_MAX_EXP__", S8("1024"));
        C_DEFINE_TYPE_MACRO("__LDBL_MIN_EXP__", S8("(-1021)"));
        C_DEFINE_TYPE_MACRO("__LDBL_MAX_10_EXP__", S8("308"));
        C_DEFINE_TYPE_MACRO("__LDBL_MIN_10_EXP__", S8("(-307)"));
    }
    C_DEFINE_TYPE_MACRO("__SIZEOF_VA_LIST__", string_format(arena, S8("{u32}"), layout.va_list.size));
    C_DEFINE_TYPE_MACRO("__LONG_DOUBLE_WIDTH__", string_format(arena, S8("{u32}"), layout.long_double_type.bit_width));
    C_DEFINE_TYPE_MACRO("__WCHAR_WIDTH__", short_wchar_target ? S8("16") : S8("32"));
    C_DEFINE_TYPE_MACRO("__WINT_WIDTH__", short_wchar_target ? S8("16") : S8("32"));
    C_DEFINE_TYPE_MACRO("__ORDER_LITTLE_ENDIAN__", S8("1234"));
    C_DEFINE_TYPE_MACRO("__ORDER_BIG_ENDIAN__", S8("4321"));
    C_DEFINE_TYPE_MACRO("__BYTE_ORDER__", layout.endianness == TARGET_ENDIAN_LITTLE ? S8("__ORDER_LITTLE_ENDIAN__") : S8("__ORDER_BIG_ENDIAN__"));
    C_DEFINE_TYPE_MACRO("__clang__", S8("1"));
    C_DEFINE_TYPE_MACRO("__clang_major__", S8("18"));
    C_DEFINE_TYPE_MACRO("__clang_minor__", S8("0"));
    C_DEFINE_TYPE_MACRO("__clang_patchlevel__", S8("0"));
    // The string form beside the three numbers: CPython's Python/getcompiler.c
    // builds sys.version's compiler field from it.
    C_DEFINE_TYPE_MACRO("__clang_version__", S8("\"18.0.0 (buster)\""));
    C_DEFINE_TYPE_MACRO("__VERSION__", S8("__clang_version__"));
    // No inliner runs at any accepted -O level. Do not expose glibc's
    // optimized extern-inline paths by advertising __OPTIMIZE__ or
    // __OPTIMIZE_SIZE__; tell its guards explicitly that inlining is absent.
    C_DEFINE_TYPE_MACRO("__NO_INLINE__", S8("1"));
    // C11 6.10.8.1 mandates both, in exactly these spellings ("Mmm dd yyyy"
    // and "hh:mm:ss").  A fixed epoch keeps builds reproducible; consumers
    // parse the SHAPE -- CPython's platform.py rejects sys.version when the
    // date field carries anything outside [\w ], which is what the
    // getbuildinfo fallback ("xx/xx/xx") for a missing __DATE__ does.
    // __TIMESTAMP__ follows the same fixed epoch in asctime shape ("Ddd Mmm dd hh:mm:ss yyyy").
    C_DEFINE_TYPE_MACRO("__DATE__", S8("\"Jan  1 1970\""));
    C_DEFINE_TYPE_MACRO("__TIME__", S8("\"00:00:00\""));
    C_DEFINE_TYPE_MACRO("__TIMESTAMP__", S8("\"Thu Jan  1 00:00:00 1970\""));
    if (!layout.plain_char_is_signed)
    {
        C_DEFINE_TYPE_MACRO("__CHAR_UNSIGNED__", S8("1"));
    }
    if (windows_target)
    {
        C_DEFINE_TYPE_MACRO("__int8", S8("signed char"));
        C_DEFINE_TYPE_MACRO("__int16", S8("short"));
        C_DEFINE_TYPE_MACRO("__int32", S8("int"));
        C_DEFINE_TYPE_MACRO("__int64", S8("long long"));
        C_DEFINE_TYPE_MACRO("_MSC_VER", S8("1940"));
        C_DEFINE_TYPE_MACRO("_MSC_FULL_VER", S8("194000000"));
        C_DEFINE_TYPE_MACRO("_MSC_EXTENSIONS", S8("1"));
        // Preserve the Windows header spelling for needed body retention.
        // Neither spelling injects a storage class or COMDAT semantics.
        C_DEFINE_TYPE_MACRO("__forceinline", S8("__inline"));
        C_DEFINE_TYPE_MACRO("SORTPP_PASS", S8("1"));
        C_DEFINE_TYPE_MACRO("_WIN64", S8("1"));
        if (options.target.cpu_arch == CPU_ARCH_X86_64)
        {
            C_DEFINE_TYPE_MACRO("_AMD64_", S8("1"));
            C_DEFINE_TYPE_MACRO("_M_AMD64", S8("100"));
            C_DEFINE_TYPE_MACRO("_M_X64", S8("100"));
        }
        else if (options.target.cpu_arch == CPU_ARCH_AARCH64)
        {
            C_DEFINE_TYPE_MACRO("_ARM64_", S8("1"));
            C_DEFINE_TYPE_MACRO("_M_ARM64", S8("1"));
        }
    }
    if (layout.pointer.size == 8 && layout.long_integer.size == 8)
    {
        C_DEFINE_TYPE_MACRO("__LP64__", S8("1"));
        C_DEFINE_TYPE_MACRO("_LP64", S8("1"));
    }
    else if (layout.pointer.size == 4 && layout.long_integer.size == 4)
    {
        C_DEFINE_TYPE_MACRO("__ILP32__", S8("1"));
        C_DEFINE_TYPE_MACRO("_ILP32", S8("1"));
    }
    if (options.target.os == OPERATING_SYSTEM_ANDROID && options.target.os_version_major)
    {
        C_DEFINE_TYPE_MACRO("__ANDROID_MIN_SDK_VERSION__", string_format(arena, S8("{u32}"), (u32)options.target.os_version_major));
        C_DEFINE_TYPE_MACRO("__ANDROID_API__", S8("__ANDROID_MIN_SDK_VERSION__"));
    }
#undef C_DEFINE_TYPE_MACRO
    String8 operating_system_macros[7] = {0};
    u32 operating_system_macro_count = 0;
    switch (options.target.os)
    {
    case OPERATING_SYSTEM_WINDOWS:
    {
        operating_system_macros[operating_system_macro_count++] = S8("_WIN32");
    }
    break;
    case OPERATING_SYSTEM_UEFI:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__UEFI__");
    }
    break;
    case OPERATING_SYSTEM_MACOS:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__APPLE__");
        operating_system_macros[operating_system_macro_count++] = S8("__MACH__");
        operating_system_macros[operating_system_macro_count++] = S8("__BUSTER_TARGET_MACOS__");
    }
    break;
    case OPERATING_SYSTEM_IOS:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__APPLE__");
        operating_system_macros[operating_system_macro_count++] = S8("__MACH__");
    }
    break;
    case OPERATING_SYSTEM_ANDROID:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__ANDROID__");
        operating_system_macros[operating_system_macro_count++] = S8("__linux");
        operating_system_macros[operating_system_macro_count++] = S8("__linux__");
        operating_system_macros[operating_system_macro_count++] = S8("__unix");
        operating_system_macros[operating_system_macro_count++] = S8("__unix__");
        operating_system_macros[operating_system_macro_count++] = S8("__ELF__");
    }
    break;
    case OPERATING_SYSTEM_LINUX:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__gnu_linux__");
        operating_system_macros[operating_system_macro_count++] = S8("__linux");
        operating_system_macros[operating_system_macro_count++] = S8("__linux__");
        operating_system_macros[operating_system_macro_count++] = S8("__unix");
        operating_system_macros[operating_system_macro_count++] = S8("__unix__");
        operating_system_macros[operating_system_macro_count++] = S8("__ELF__");
    }
    break;
    case OPERATING_SYSTEM_WASI:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__wasi__");
        operating_system_macros[operating_system_macro_count++] = S8("__wasip1__");
    }
    break;
    case OPERATING_SYSTEM_FREESTANDING:
    case OPERATING_SYSTEM_COUNT:
    {
    }
    break;
    }
    for (u32 macro_index = 0; macro_index < operating_system_macro_count; macro_index += 1)
    {
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, operating_system_macros[macro_index], standard_replacement, 1, 0, 0, false, false);
    }
    if (options.target.os == OPERATING_SYSTEM_MACOS || options.target.os == OPERATING_SYSTEM_IOS)
    {
        c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8("__APPLE_CC__"), S8("6000"));
    }
    String8 architecture_macro = options.target.cpu_arch == CPU_ARCH_AARCH64 ? S8("__aarch64__")
                               : options.target.cpu_arch == CPU_ARCH_WASM32  ? S8("__wasm32__")
                               : options.target.cpu_arch == CPU_ARCH_WASM64  ? S8("__wasm64__")
                               : options.target.cpu_arch == CPU_ARCH_BPFEL   ? S8("__bpfel__")
                                                                             : S8("__x86_64__");
    c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, architecture_macro, standard_replacement, 1, 0, 0, false, false);
    if (options.target.cpu_arch == CPU_ARCH_WASM32 || options.target.cpu_arch == CPU_ARCH_WASM64)
    {
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__wasm__"), standard_replacement, 1, 0, 0, false, false);
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__wasm"), standard_replacement, 1, 0, 0, false, false);
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro,
                       options.target.cpu_arch == CPU_ARCH_WASM32 ? S8("__wasm32") : S8("__wasm64"), standard_replacement, 1, 0, 0, false,
                       false);
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__wasm_mutable_globals__"), standard_replacement, 1, 0, 0, false, false);
    }
    if (options.target.cpu_arch == CPU_ARCH_WASM64)
    {
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__wasm_memory64__"), standard_replacement, 1, 0, 0, false, false);
    }
    if (options.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__bpf__"), standard_replacement, 1, 0, 0, false, false);
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__BPF__"), standard_replacement, 1, 0, 0, false, false);
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__BUSTER_EBPF__"), standard_replacement, 1, 0, 0, false, false);
    }
    if ((options.target.os == OPERATING_SYSTEM_MACOS || options.target.os == OPERATING_SYSTEM_IOS) && options.target.cpu_arch == CPU_ARCH_AARCH64)
    {
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__arm64__"), standard_replacement, 1, 0, 0, false, false);
    }
    // The vector-extension macros the GNU family predefines from -march, so a
    // header guarding an explicit SIMD kernel writes one condition that holds
    // for clang, gcc, and the self-hosted stages alike.  Only the features the
    // buster SIMD vocabulary can actually lower are published; announcing more
    // would invite a host header down a path this frontend cannot compile.
    for (u32 feature_index = 0; feature_index < BUSTER_ARRAY_LENGTH(c_target_feature_macros); feature_index += 1)
    {
        CTargetFeatureMacro entry = c_target_feature_macros[feature_index];
        if (entry.cpu_arch == options.target.cpu_arch && target_cpu_feature_has(options.target, entry.feature))
        {
            c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, entry.name, standard_replacement, 1, 0, 0, false, false);
        }
    }
    CToken hosted_replacement = standard_replacement[0];
    if (options.target.os == OPERATING_SYSTEM_FREESTANDING || options.target.os == OPERATING_SYSTEM_UEFI ||
        options.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        hosted_replacement.offset = C_SPELLING_ZERO;
    }
    c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__STDC_HOSTED__"), &hosted_replacement, 1, 0, 0, false, false);
    // C90 predates __STDC_VERSION__, so gnu89 leaves it undefined as gcc does.
    if (standard_version.length)
    {
        c_macro_define(arena, space->base, symbol_table, &first_macro, &last_macro, S8("__STDC_VERSION__"), standard_replacement + 1, 1, 0, 0, false, false);
    }
    CTokenStream token_stream = {
        .arena = token_arena,
        .shape_arena = token_shape_arena,
        .base = (CToken*)((char8*)token_arena + arena_minimum_position),
        .shape_base = (CTokenShape*)((char8*)token_shape_arena + arena_minimum_position),
    };
    u64 output_count = 0;
    COutputSpacingBlock* first_spacing = 0;
    COutputSpacingBlock* last_spacing = 0;
    CPragmaPackStack* pack_stack = 0;
    CPragmaVisibilityStack* visibility_stack = 0;
    CMacroPushMacro* macro_push_stack = 0;
    u16 pack_alignment = 0;
    u8 visibility = C_SYMBOL_VISIBILITY_UNSPECIFIED;
    CPragmaStateRecorder pragma_changes = {
        .arena = arena,
    };
    u32 expansion_limit = options.expansion_limit ? options.expansion_limit : 65536;
    bool expansion_ok = true;
    CConditionalFrame* conditional = 0;
    CPreprocessSourceFrame root_frame = {
        .lex = root_lex,
        .lex_diagnostic_index = 0,
        .class_masks = root_class_masks,
        .path = options.source_path.length ? options.source_path : S8("."),
        .identity = c_include_file_identity(options.source_path.length ? options.source_path : S8("."), options.source_identity),
        .logical_path = options.source_path.length ? options.source_path : S8("."),
        .line_start = true,
    };
    CPreprocessSourceFrame* source_frame = &root_frame;
    CPreprocessFileTable file_table = {0};
    root_frame.map_entry = map.count;
    c_source_map_append(&map, (IrSourceRegion){
                                  .start = root_lex.translated_offset,
                                  .source = c_preprocess_file_index(arena, &file_table, root_frame.logical_path),
                                  .checkpoints = root_lex.checkpoints,
                                  .checkpoint_offsets = root_lex.checkpoint_offsets,
                                  .checkpoint_pages = root_lex.checkpoint_pages,
                                  .checkpoint_page_count = root_lex.checkpoint_page_count,
                                  .checkpoint_count = root_lex.checkpoint_count,
                                  .base = root_lex.translated_offset,
                                  .kind = IR_SOURCE_REGION_TEXT,
                              });
    c_source_map_name(&map, root_frame.path, root_frame.logical_path);
    u32 include_depth_limit = options.include_depth_limit ? options.include_depth_limit : 256;
    c_preprocess_builtins(arena, symbol_table, &first_macro, &last_macro, root_frame.logical_path,
                          (CSourceLocation){.line = 1, .column = 1});
    if (!options.already_preprocessed)
    {
        c_preprocess_command_operations(arena, space, symbol_table, options, &first_macro, &last_macro, &result);
    }
    CIncludeFileTable include_files = {
        .arena = arena,
    };
    CIncludeProbeTable include_probes = {
        .arena = arena,
    };
    CPpStampTable stamps = {
        .arena = arena,
    };
    CPreprocessPragmaContext pragma_context = {
        .arena = arena,
        .preprocess = &result,
        .symbols = symbol_table,
        .first_macro = &first_macro,
        .last_macro = &last_macro,
        .macro_push_stack = &macro_push_stack,
        .pack_stack = &pack_stack,
        .pack_alignment = &pack_alignment,
        .visibility_stack = &visibility_stack,
        .visibility = &visibility,
        .pragma_changes = &pragma_changes,
        .include_files = &include_files,
    };
    while (source_frame)
    {
        pragma_context.current_path = source_frame->path;
        pragma_context.current_identity = source_frame->identity;
        CLexResult lex = source_frame->lex;
        char8 const* base = space->base;
        CPpClassMasks const* class_masks = &source_frame->class_masks;
        u64 token_index = source_frame->token_index;
        bool line_start = source_frame->line_start;
        CToken token = lex.tokens[token_index];
        if (token.kind == C_TOKEN_END_OF_FILE)
        {
            c_preprocess_lex_diagnostics_release(arena, &result, base, source_frame, UINT64_MAX, true);
            while (conditional != source_frame->conditional_base)
            {
                c_preprocess_diagnostic_push(arena, &result, conditional->location, C_DIAGNOSTIC_UNMATCHED_CONDITIONAL, S8("unterminated preprocessing conditional"));
                conditional = conditional->previous;
            }
            if (source_frame != &root_frame)
            {
                if (source_frame->guard_state == C_INCLUDE_GUARD_CLOSED)
                {
                    CIncludeFileEntry* entry = 0;
                    CIncludeFileStatus status =
                        c_include_file_entry(&include_files, source_frame->identity, source_frame->path, &entry);
                    if (status == C_INCLUDE_FILE_OK)
                    {
                        entry->guard_symbol = source_frame->guard_symbol;
                    }
                    else
                    {
                        CSourceLocation location = c_preprocess_logical_location(source_frame, c_lex_token_location(&source_frame->lex, token));
                        c_include_file_diagnostic(arena, &result, location, status, source_frame->path);
                    }
                }
                file_map_unmap(source_frame->source_map);
            }
            source_frame = source_frame->previous;
            continue;
        }
        if (token.kind == C_TOKEN_NEWLINE)
        {
            source_frame->line_start = true;
            source_frame->token_index += 1;
            continue;
        }
        first_macro->builtin_frame = source_frame;
        first_macro->builtin_token_offset = token.offset;
        first_macro->builtin_path = source_frame->logical_path;
        if (line_start && c_token_is_punctuator(&token, C_PUNCTUATOR_HASH))
        {
            bool directive_live = c_preprocess_is_active(conditional);
            bool directive_diagnostics_released = false;
            CPreprocessSourceFrame* include_frame = 0;
            token_index += 1;
            bool is_line_marker = token_index < lex.token_count && lex.tokens[token_index].kind == C_TOKEN_PREPROCESSING_NUMBER;
            if (token_index >= lex.token_count || (lex.tokens[token_index].kind != C_TOKEN_IDENTIFIER && !is_line_marker))
            {
                // A null directive is valid even in an inactive group and
                // at EOF. It emits nothing; punctuation such as '#+' remains
                // an invalid directive rather than an assembly comment.
                bool null_directive = token_index < lex.token_count &&
                                      (lex.tokens[token_index].kind == C_TOKEN_NEWLINE || lex.tokens[token_index].kind == C_TOKEN_END_OF_FILE);
                if (c_preprocess_is_active(conditional) && !null_directive && !options.assembly_comment_lines && !options.already_preprocessed)
                {
                    c_preprocess_diagnostic_push(arena, &result, c_lex_token_location(&source_frame->lex, token), C_DIAGNOSTIC_EXPECTED_DIRECTIVE,
                                                 S8("expected preprocessing directive after '#'"));
                }
            }
            else
            {
                CToken directive = lex.tokens[token_index];
                if (!is_line_marker)
                {
                    token_index += 1;
                }
                IR_DIAGNOSTIC_CENSUS_RECORD(C_DIRECTIVE_LOCATIONS, 2);
                u32 physical_directive_line = c_lex_token_location(&source_frame->lex, directive).line;
                CSourceLocation directive_location = c_preprocess_logical_location(source_frame, c_lex_token_location(&source_frame->lex, directive));
                u64 line_end = class_masks->word_count ? c_pp_line_end_masked(class_masks, lex.token_count, token_index) : c_preprocess_line_end(lex, token_index);
                bool active = c_preprocess_is_active(conditional);
                bool is_if = c_token_spelling_equal(base, directive, S8("if"));
                bool is_ifdef = c_token_spelling_equal(base, directive, S8("ifdef"));
                bool is_ifndef = c_token_spelling_equal(base, directive, S8("ifndef"));
                bool is_elif = c_token_spelling_equal(base, directive, S8("elif")) || c_token_spelling_equal(base, directive, S8("elifdef")) ||
                               c_token_spelling_equal(base, directive, S8("elifndef"));
                bool is_else = c_token_spelling_equal(base, directive, S8("else"));
                bool is_endif = c_token_spelling_equal(base, directive, S8("endif"));
                bool is_include = c_token_spelling_equal(base, directive, S8("include"));
                bool is_include_next = c_token_spelling_equal(base, directive, S8("include_next"));
                bool is_import = c_token_spelling_equal(base, directive, S8("import"));
                bool is_line = is_line_marker || c_token_spelling_equal(base, directive, S8("line"));
                bool is_pragma = c_token_spelling_equal(base, directive, S8("pragma"));
                bool is_error = c_token_spelling_equal(base, directive, S8("error"));
                bool is_warning = c_token_spelling_equal(base, directive, S8("warning"));
                CPreprocessConditionalDirective conditional_directive = c_preprocess_conditional_directive_kind(base, directive);
                if (is_if || is_elif)
                {
                    line_end = c_preprocess_directive_line_end(lex, line_end);
                }
                directive_live =
                    directive_live || (is_elif && conditional != source_frame->conditional_base && !conditional->else_seen &&
                                       conditional->parent_active && !conditional->branch_taken);
                c_preprocess_lex_diagnostics_release(arena, &result, base, source_frame, lex.tokens[line_end].offset, directive_live);
                directive_diagnostics_released = true;
                // Any directive at the frame's top level other than the
                // conditionals themselves sits outside a candidate include
                // guard, so the file cannot be guard-shaped.
                if (!(is_if || is_ifdef || is_ifndef || is_elif || is_else || is_endif) && conditional == source_frame->conditional_base)
                {
                    source_frame->guard_state = C_INCLUDE_GUARD_DISQUALIFIED;
                }
                if (options.already_preprocessed && !is_line && !is_pragma)
                {
                    // A cpp-output stream has already resolved conditionals,
                    // definitions, includes and diagnostics. Unknown or stale
                    // directive lines are tokenizer metadata here, not a
                    // second preprocessing program.
                }
                else if (is_if || is_ifdef || is_ifndef)
                {
                    c_preprocess_conditional_directive(arena, space, symbol_table, first_macro, &stamps, source_frame, conditional_directive,
                                                       directive, token_index, line_end, expansion_limit, &result, options, &include_probes, &conditional);
                    token_index = line_end;
                }
                else if (is_elif)
                {
                    c_preprocess_conditional_directive(arena, space, symbol_table, first_macro, &stamps, source_frame, conditional_directive,
                                                       directive, token_index, line_end, expansion_limit, &result, options, &include_probes, &conditional);
                    token_index = line_end;
                }
                else if (is_else)
                {
                    c_preprocess_conditional_directive(arena, space, symbol_table, first_macro, &stamps, source_frame, conditional_directive,
                                                       directive, token_index, line_end, expansion_limit, &result, options, &include_probes, &conditional);
                }
                else if (is_endif)
                {
                    c_preprocess_conditional_directive(arena, space, symbol_table, first_macro, &stamps, source_frame, conditional_directive,
                                                       directive, token_index, line_end, expansion_limit, &result, options, &include_probes, &conditional);
                }
                else if (active && (is_error || is_warning))
                {
                    c_preprocess_diagnostic_push_severity(arena, &result, directive_location,
                                                           is_error ? C_DIAGNOSTIC_PREPROCESSOR_ERROR : C_DIAGNOSTIC_PREPROCESSOR_WARNING,
                                                           is_error ? C_DIAGNOSTIC_ERROR : C_DIAGNOSTIC_WARNING,
                                                           c_preprocess_message_from_tokens(arena, base, lex.tokens + token_index,
                                                                                            (u32)(line_end - token_index)));
                }
                else if (active && is_line)
                {
                    u64 line_token_count = line_end - token_index;
                    CToken* line_tokens = lex.tokens + token_index;
                    bool line_expanded = true;
                    if (!options.already_preprocessed)
                    {
                        CPreprocessTokenNode* first_line = 0;
                        CPreprocessTokenNode* last_line = 0;
                        line_token_count = 0;
                        line_expanded = c_preprocess_expand(arena, &expansion_storage, space, symbol_table, first_macro, 0, 0, &stamps,
                                                            c_frame_wrap_tokens(arena, &stamps, source_frame, token_index, line_end),
                                                            (u32)(line_end - token_index), &first_line, &last_line, &line_token_count,
                                                            expansion_limit, &result, 0);
                        line_tokens = arena_allocate(arena, CToken, line_token_count);
                        u32 line_index = 0;
                        for (CPreprocessTokenNode* node = first_line; node; node = node->next)
                        {
                            line_tokens[line_index++] = node->token.token;
                        }
                    }
                    u64 requested_line = 0;
                    bool has_file_name = line_token_count >= 2 && line_tokens[1].kind == C_TOKEN_STRING_LITERAL &&
                                         c_token_spelling(base, line_tokens[1]).pointer[0] == '"';
                    CIrDecodedString decoded_name = {0};
                    bool valid_line = line_expanded && line_token_count >= 1 && line_tokens[0].kind == C_TOKEN_PREPROCESSING_NUMBER &&
                                      c_conditional_number(c_token_spelling(base, line_tokens[0]), &requested_line) && requested_line >= 1 &&
                                      requested_line <= UINT32_MAX &&
                                      ((!is_line_marker && (line_token_count == 1 || (line_token_count == 2 && has_file_name))) ||
                                       (is_line_marker && (line_token_count == 1 || has_file_name)));
                    if (valid_line && is_line_marker && line_token_count > 2)
                    {
                        for (u32 flag_index = 2; flag_index < line_token_count; flag_index += 1)
                        {
                            u64 flag = 0;
                            valid_line &= line_tokens[flag_index].kind == C_TOKEN_PREPROCESSING_NUMBER &&
                                          c_conditional_number(c_token_spelling(base, line_tokens[flag_index]), &flag) && flag >= 1 && flag <= 4;
                        }
                    }
                    if (valid_line && has_file_name)
                    {
                        // Line-control filenames follow string-literal escape
                        // rules, unlike include header names. Decode once for
                        // every source-map and builtin consumer of the path.
                        CPreprocessResult name_input = {.spelling_base = base, .tokens = line_tokens + 1, .token_count = 1};
                        valid_line = c_ir_decode_string_literal_range_for_target(arena, name_input, result.target, 0, 1, 0, &decoded_name);
                    }
                    if (!valid_line)
                    {
                        c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INVALID_LINE,
                                                     S8("expected '#line' followed by a positive line number and optional file name"));
                    }
                    else
                    {
                        source_frame->line_delta = (s64)requested_line - ((s64)physical_directive_line + 1);
                        if (has_file_name)
                        {
                            source_frame->logical_path = (String8){
                                .pointer = (char8*)decoded_name.bytes.pointer,
                                .length = decoded_name.bytes.length,
                            };
                        }
                        // The region after the directive maps through the new
                        // delta and logical file; entries partition the file's
                        // span by offset, so the split is one append.
                        source_frame->map_entry = map.count;
                        c_source_map_append(&map, (IrSourceRegion){
                                                      .start = lex.tokens[line_end].offset,
                                                      .source = UINT32_MAX,
                                                      .checkpoints = source_frame->lex.checkpoints,
                                                      .checkpoint_offsets = source_frame->lex.checkpoint_offsets,
                                                      .checkpoint_pages = source_frame->lex.checkpoint_pages,
                                                      .checkpoint_page_count = source_frame->lex.checkpoint_page_count,
                                                      .checkpoint_count = source_frame->lex.checkpoint_count,
                                                      .base = source_frame->lex.translated_offset,
                                                      .line_delta = source_frame->line_delta,
                                                      .kind = IR_SOURCE_REGION_TEXT,
                                                  });
                        c_source_map_name(&map, source_frame->path, source_frame->logical_path);
                    }
                }
                else if (active && c_token_spelling_equal(base, directive, S8("define")))
                {
                    c_preprocess_define_directive(arena, symbol_table, lex, &token_index, &first_macro, &last_macro, &result, directive_location,
                                                  UINT32_MAX, UINT32_MAX, options.assembly_comment_lines);
                    // Directives reached, not macros surviving: a header
                    // included twice defines its macros twice.
                    result.detail->preprocessed.definitions += 1;
                }
                else if (active && c_token_spelling_equal(base, directive, S8("undef")))
                {
                    c_preprocess_undefine_directive(arena, symbol_table, lex, &token_index, first_macro, &result, directive_location, UINT32_MAX,
                                                    UINT32_MAX, false);
                }
                else if (active && (is_include || is_include_next || is_import))
                {
                    CPreprocessTokenNode* first_include = 0;
                    CPreprocessTokenNode* last_include = 0;
                    u64 include_token_count = 0;
                    String8 include_name = {0};
                    bool quoted = false;
                    u32 raw_include_count = (u32)(line_end - token_index);
                    u32 include_name_count = c_include_name_token_count(lex.tokens + token_index, raw_include_count);
                    bool include_expanded = c_include_name(arena, base, lex.tokens + token_index, include_name_count, true, &include_name, &quoted);
                    bool extra_include_tokens = include_expanded && include_name_count < raw_include_count;
                    if (!include_expanded)
                    {
                        include_expanded = c_preprocess_expand(arena, &expansion_storage, space, symbol_table, first_macro, 0, 0, &stamps,
                                                               c_frame_wrap_tokens(arena, &stamps, source_frame, token_index, line_end), raw_include_count,
                                                               &first_include, &last_include, &include_token_count, expansion_limit, &result, 0);
                        CToken* include_tokens = arena_allocate(arena, CToken, include_token_count);
                        u32 include_index = 0;
                        for (CPreprocessTokenNode* node = first_include; node; node = node->next)
                        {
                            include_tokens[include_index++] = node->token.token;
                        }
                        include_name_count = c_include_name_token_count(include_tokens, include_index);
                        include_expanded = include_expanded &&
                                           c_include_name(arena, base, include_tokens, include_name_count, false, &include_name, &quoted);
                        extra_include_tokens = include_expanded && include_name_count < include_index;
                    }
                    if (include_expanded && extra_include_tokens)
                    {
                        c_preprocess_diagnostic_push_severity(arena, &result, directive_location, C_DIAGNOSTIC_EXTRA_DIRECTIVE_TOKENS,
                                                              C_DIAGNOSTIC_WARNING,
                                                              string_format(arena, S8("extra tokens at end of '#{S8}' directive"),
                                                                            c_token_spelling(base, directive)));
                    }
                    if (!include_expanded)
                    {
                            c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INVALID_INCLUDE,
                                                     S8("expected a quoted or angle-bracket header name"));
                    }
                    else if (source_frame->depth >= include_depth_limit)
                    {
                        c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INCLUDE_DEPTH, S8("preprocessor include depth limit exceeded"));
                    }
                    else
                    {
                        String8 include_path = {0};
                        String8 include_source = {0};
                        FileMapRead include_source_map = {0};
                        CIncludeProbe* include_cached = 0;
                        CIncludeSearchOrigin include_origin = {0};
                        bool include_resolved =
                            is_include_next ? c_include_resolve_next(arena, &include_probes, options, include_name, &include_path, &include_source,
                                                                      &include_source_map, &include_cached, source_frame->include_origin, &include_origin)
                                            : c_include_resolve(arena, &include_probes, options, source_frame->path, include_name, quoted, &include_path,
                                                                &include_source, &include_source_map, &include_cached, &include_origin);
                        // Identity comes from the descriptor that supplied
                        // these bytes; the resolved spelling remains separate for
                        // diagnostics, source maps and per-path attribution. A
                        // cached probe has not opened the file: the identity its
                        // first probe captured decides suppression, so a
                        // suppressed re-include makes no system call, and only
                        // an inclusion that lexes maps the path again.
                        CIncludeFileIdentity include_identity =
                            include_cached ? include_cached->identity : c_include_file_identity(include_path, include_source_map.identity);
                        CIncludeFileEntry* include_file = 0;
                        CIncludeFileStatus include_file_status = include_resolved
                                                                     ? c_include_file_entry(&include_files, include_identity, include_path, &include_file)
                                                                     : C_INCLUDE_FILE_OK;
                        bool include_once = include_file && c_include_suppressed(include_file, is_import, root_frame.identity, first_macro);
                        if (include_file && include_cached && !include_once)
                        {
                            include_source_map = file_map_read(arena, include_path, (FileReadOptions){0});
                            include_source = BYTE_SLICE_TO_STRING(8, include_source_map.bytes);
                            include_resolved = include_source_map.bytes.pointer != 0;
                            CIncludeFileIdentity mapped_identity = c_include_file_identity(include_path, include_source_map.identity);
                            if (include_resolved && !c_include_file_identity_equal(include_file, mapped_identity))
                            {
                                // Replaced since its probe: the new descriptor's
                                // identity governs, as for an uncached include.
                                include_cached->identity = mapped_identity;
                                include_identity = mapped_identity;
                                include_file_status = c_include_file_entry(&include_files, include_identity, include_path, &include_file);
                                include_once = include_file && c_include_suppressed(include_file, is_import, root_frame.identity, first_macro);
                            }
                        }
                        if (!include_resolved)
                        {
                            c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INCLUDE_NOT_FOUND,
                                                         string_format(arena, S8("included file was not found: {S8}"), include_name));
                        }
                        else
                        {
                            if (include_file_status != C_INCLUDE_FILE_OK)
                            {
                                c_include_file_diagnostic(arena, &result, directive_location, include_file_status, include_path);
                                file_map_unmap(include_source_map);
                            }
                            else
                            {
                                if (is_import)
                                {
                                    // Mark before descending so a recursive
                                    // #import of this identity is suppressed.
                                    include_file->once = true;
                                }
                                include_file->included = true;
                                CLexResult include_lex = include_once ? (CLexResult){0} : c_source_cache_lex(arena, space, include_source, trigraphs, options.dialect, options.source_cache);
                                // A suppressed include lexed nothing and adds
                                // zeroes; its path was already counted by the
                                // inclusion that did the lexing, and its
                                // attribution row keeps that lex count.
                                c_source_metrics_add(&result.detail->source_lexed, &include_lex.metrics);
                                u32 include_row = c_source_metrics_file_row(arena, &metrics_files, include_path);
                                if (metrics_files.rows[include_row].lex_count == 0)
                                {
                                    c_source_metrics_add(&result.detail->source_unique, &include_lex.metrics);
                                }
                                if (!include_once)
                                {
                                    metrics_files.rows[include_row].translated_bytes = include_lex.metrics.translated_bytes;
                                    metrics_files.rows[include_row].lex_count += 1;
                                }
                                c_symbols_intern_tokens(symbol_table, include_lex.spelling_base, include_lex.tokens, include_lex.token_shapes, include_lex.token_count);
                                CPpClassMasks include_class_masks;
                                c_pp_class_masks_build(arena, &include_class_masks, include_lex.token_shapes, include_lex.token_count);
                                if (!include_once)
                                {
                                    include_frame = arena_allocate(arena, CPreprocessSourceFrame, 1);
                                    *include_frame = (CPreprocessSourceFrame){
                                        .previous = source_frame,
                                        .conditional_base = conditional,
                                        .lex = include_lex,
                                        .lex_diagnostic_index = 0,
                                        .class_masks = include_class_masks,
                                        .path = include_path,
                                        .identity = include_identity,
                                        .logical_path = include_path,
                                        .source_map = include_source_map,
                                        .depth = source_frame->depth + 1,
                                        .line_start = true,
                                        .include_origin = include_origin,
                                    };
                                    include_frame->map_entry = map.count;
                                    c_source_map_append(&map, (IrSourceRegion){
                                                                  .start = include_lex.translated_offset,
                                                                  .source = UINT32_MAX,
                                                                  .checkpoints = include_lex.checkpoints,
                                                                  .checkpoint_offsets = include_lex.checkpoint_offsets,
                                                                  .checkpoint_pages = include_lex.checkpoint_pages,
                                                                  .checkpoint_page_count = include_lex.checkpoint_page_count,
                                                                  .checkpoint_count = include_lex.checkpoint_count,
                                                                  .base = include_lex.translated_offset,
                                                                  .kind = IR_SOURCE_REGION_TEXT,
                                                              });
                                    c_source_map_name(&map, include_path, include_path);
                                }
                                else
                                {
                                    file_map_unmap(include_source_map);
                                }
                            }
                        }
                    }
                }
                else if (active && is_pragma)
                {
                    bool pragma_expanded = true;
                    CToken* pragma_tokens = lex.tokens + token_index;
                    u32 expanded_pragma_count = (u32)(line_end - token_index);
                    if (!options.already_preprocessed)
                    {
                        CPreprocessTokenNode* first_pragma = 0;
                        CPreprocessTokenNode* last_pragma = 0;
                        u64 pragma_token_count = 0;
                        pragma_expanded = c_preprocess_expand(arena, &expansion_storage, space, symbol_table, first_macro, 0, 0, &stamps,
                                                              c_frame_wrap_tokens(arena, &stamps, source_frame, token_index, line_end),
                                                              (u32)(line_end - token_index), &first_pragma, &last_pragma, &pragma_token_count,
                                                              expansion_limit, &result, 0);
                        expanded_pragma_count = c_preprocess_tokens_from_nodes(first_pragma, arena, &pragma_tokens);
                    }
                    if (pragma_expanded)
                    {
                        pragma_context.current_location = directive_location;
                        c_preprocess_handle_pragma(pragma_context, base, pragma_tokens, expanded_pragma_count);
                    }
                }
                else if (active && !options.assembly_comment_lines)
                {
                    c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_UNSUPPORTED_DIRECTIVE,
                                                 string_format(arena, S8("{S8}: preprocessing directive is not implemented yet: {S8}"), source_frame->path,
                                                               c_token_spelling(base, directive)));
                }
            }
            while (token_index < lex.token_count && lex.tokens[token_index].kind != C_TOKEN_NEWLINE && lex.tokens[token_index].kind != C_TOKEN_END_OF_FILE)
            {
                token_index += 1;
            }
            if (!directive_diagnostics_released)
            {
                c_preprocess_lex_diagnostics_release(arena, &result, base, source_frame, lex.tokens[token_index].offset, directive_live);
            }
            source_frame->token_index = token_index;
            source_frame->line_start = true;
            if (include_frame)
            {
                source_frame = include_frame;
            }
            continue;
        }
        source_frame->line_start = false;
        // A token line at the frame's top level sits outside any candidate
        // include guard, so tokens would survive a re-include.
        if (conditional == source_frame->conditional_base)
        {
            source_frame->guard_state = C_INCLUDE_GUARD_DISQUALIFIED;
        }
        bool classified = class_masks->word_count != 0;
        u64 line_end = classified ? c_pp_line_end_masked(class_masks, lex.token_count, token_index) : c_preprocess_line_end(lex, token_index);
        c_preprocess_lex_diagnostics_release(arena, &result, base, source_frame, lex.tokens[line_end].offset, c_preprocess_is_active(conditional));
        if (!c_preprocess_is_active(conditional))
        {
            source_frame->token_index = line_end;
            continue;
        }
        u64 logical_end = line_end;
        u32 parenthesis_depth = 0;
        bool source_conditionals = false;
        CPreprocessSourceSegment* first_segment = 0;
        CPreprocessSourceSegment* last_segment = 0;
        if (!options.already_preprocessed)
        {
            if (classified)
            {
                parenthesis_depth = c_pp_parenthesis_depth(class_masks, token_index, logical_end, 0);
            }
            else
            {
                for (u64 scan = token_index; scan < logical_end; scan += 1)
                {
                    if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_LEFT_PARENTHESIS))
                    {
                        parenthesis_depth += 1;
                    }
                    else if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_RIGHT_PARENTHESIS) && parenthesis_depth)
                    {
                        parenthesis_depth -= 1;
                    }
                }
            }
        }
        while (logical_end < lex.token_count && lex.tokens[logical_end].kind == C_TOKEN_NEWLINE && !options.already_preprocessed)
        {
            u64 next_line_start = logical_end + 1;
            // Before an invocation has opened, an ordinary newline run is
            // whitespace between the macro's rescan tail and a following `(`.
            // Skip the run once; directives keep their separate ordered batch.
            while (next_line_start < lex.token_count && lex.tokens[next_line_start].kind == C_TOKEN_NEWLINE)
            {
                next_line_start += 1;
            }
            if (!parenthesis_depth &&
                (next_line_start >= lex.token_count || !c_token_is_punctuator(&lex.tokens[next_line_start], C_PUNCTUATOR_LEFT_PARENTHESIS)))
            {
                break;
            }
            if (next_line_start < lex.token_count && c_token_is_punctuator(&lex.tokens[next_line_start], C_PUNCTUATOR_HASH))
            {
                u64 directive_index = next_line_start + 1;
                if (directive_index >= lex.token_count || lex.tokens[directive_index].kind != C_TOKEN_IDENTIFIER)
                {
                    if (!c_preprocess_is_active(conditional))
                    {
                        logical_end = classified ? c_pp_line_end_masked(class_masks, lex.token_count, next_line_start)
                                                 : c_preprocess_line_end(lex, next_line_start);
                        c_preprocess_lex_diagnostics_release(arena, &result, base, source_frame, lex.tokens[logical_end].offset, false);
                        continue;
                    }
                    break;
                }
                CToken directive = lex.tokens[directive_index];
                CPreprocessConditionalDirective directive_kind = c_preprocess_conditional_directive_kind(space->base, directive);
                if (directive_kind == C_PREPROCESS_CONDITIONAL_COUNT)
                {
                    if (!c_preprocess_is_active(conditional))
                    {
                        directive_index += 1;
                        logical_end = classified ? c_pp_line_end_masked(class_masks, lex.token_count, directive_index)
                                                 : c_preprocess_line_end(lex, directive_index);
                        c_preprocess_lex_diagnostics_release(arena, &result, base, source_frame, lex.tokens[logical_end].offset, false);
                        continue;
                    }
                    break;
                }
                if (!source_conditionals)
                {
                    c_preprocess_source_segment_append(arena, &first_segment, &last_segment, token_index, logical_end);
                    source_conditionals = true;
                }
                directive_index += 1;
                u64 directive_end = classified ? c_pp_line_end_masked(class_masks, lex.token_count, directive_index)
                                               : c_preprocess_line_end(lex, directive_index);
                bool directive_is_elif = directive_kind == C_PREPROCESS_CONDITIONAL_ELIF || directive_kind == C_PREPROCESS_CONDITIONAL_ELIFDEF ||
                                         directive_kind == C_PREPROCESS_CONDITIONAL_ELIFNDEF;
                if (directive_kind == C_PREPROCESS_CONDITIONAL_IF || directive_is_elif)
                {
                    directive_end = c_preprocess_directive_line_end(lex, directive_end);
                }
                bool directive_live =
                    c_preprocess_is_active(conditional) ||
                    (directive_is_elif && conditional != source_frame->conditional_base &&
                     !conditional->else_seen && conditional->parent_active && !conditional->branch_taken);
                c_preprocess_lex_diagnostics_release(arena, &result, base, source_frame, lex.tokens[directive_end].offset, directive_live);
                first_macro->builtin_token_offset = directive.offset;
                c_preprocess_conditional_directive(arena, space, symbol_table, first_macro, &stamps, source_frame, directive_kind, directive,
                                                   directive_index, directive_end, expansion_limit, &result, options, &include_probes, &conditional);
                first_macro->builtin_token_offset = token.offset;
                logical_end = directive_end;
                continue;
            }
            u64 next_line_end = classified ? c_pp_line_end_masked(class_masks, lex.token_count, next_line_start)
                                           : c_preprocess_line_end(lex, next_line_start);
            c_preprocess_lex_diagnostics_release(arena, &result, base, source_frame, lex.tokens[next_line_end].offset,
                                                 c_preprocess_is_active(conditional));
            if (c_preprocess_is_active(conditional))
            {
                if (conditional == source_frame->conditional_base)
                {
                    source_frame->guard_state = C_INCLUDE_GUARD_DISQUALIFIED;
                }
                if (source_conditionals)
                {
                    c_preprocess_source_segment_append(arena, &first_segment, &last_segment, next_line_start, next_line_end);
                }
                if (classified)
                {
                    parenthesis_depth = c_pp_parenthesis_depth(class_masks, next_line_start, next_line_end, parenthesis_depth);
                }
                else
                {
                    for (u64 scan = next_line_start; scan < next_line_end; scan += 1)
                    {
                        if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_LEFT_PARENTHESIS))
                        {
                            parenthesis_depth += 1;
                        }
                        else if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_RIGHT_PARENTHESIS) && parenthesis_depth)
                        {
                            parenthesis_depth -= 1;
                        }
                    }
                }
            }
            logical_end = next_line_end;
        }
        if (source_conditionals && logical_end < lex.token_count && lex.tokens[logical_end].kind == C_TOKEN_END_OF_FILE)
        {
            while (conditional != source_frame->conditional_base)
            {
                c_preprocess_diagnostic_push(arena, &result, conditional->location, C_DIAGNOSTIC_UNMATCHED_CONDITIONAL,
                                             S8("unterminated preprocessing conditional"));
                conditional = conditional->previous;
            }
        }
        // A line whose identifiers name no defined macro expands to itself,
        // so its lexed tokens stream straight to the output with the
        // newlines stripped and the expansion machinery (a task node and an
        // output node per token) is skipped. A stray `__VA_OPT__` takes the
        // expansion path, which diagnoses it. Locations are not materialized
        // at all on this path: the file's source-map entry recovers them
        // from the offsets on demand.
        bool needs_expansion = false;
        if (source_conditionals)
        {
            needs_expansion = true;
        }
        else if (classified && !options.already_preprocessed)
        {
            u64 last_word = (logical_end - 1) / C_PP_CLASS_MASK_WINDOW;
            for (u64 word_index = token_index / C_PP_CLASS_MASK_WINDOW; word_index <= last_word && !needs_expansion; word_index += 1)
            {
                Mask64 names = mask64_and(class_masks->identifier[word_index], c_pp_class_range_mask(word_index, token_index, logical_end));
                while (names && !needs_expansion)
                {
                    u64 scan = word_index * C_PP_CLASS_MASK_WINDOW + mask64_first_set(names);
                    CMacro* line_macro = c_macro_find_token(first_macro, symbol_table, space->base, &lex.tokens[scan]);
                    needs_expansion = (line_macro && line_macro->definition.defined) || c_macro_is_va_opt(lex.tokens[scan]);
                    names = mask64_and(names, names - 1);
                }
            }
        }
        else if (!options.already_preprocessed)
        {
            for (u64 scan = token_index; scan < logical_end && !needs_expansion; scan += 1)
            {
                if (lex.tokens[scan].kind == C_TOKEN_IDENTIFIER)
                {
                    CMacro* line_macro = c_macro_find_token(first_macro, symbol_table, space->base, &lex.tokens[scan]);
                    needs_expansion = (line_macro && line_macro->definition.defined) || c_macro_is_va_opt(lex.tokens[scan]);
                }
            }
        }
        // The line holds at least one token: this path is only entered on a
        // token that is neither a newline nor the end of the file.
        if (map.regions[source_frame->map_entry].source == UINT32_MAX)
        {
            map.regions[source_frame->map_entry].source = c_preprocess_file_index(arena, &file_table, source_frame->logical_path);
        }
        if (!needs_expansion)
        {
            // Pragmas cannot fire inside a text line on this path (only
            // directive lines and _Pragma markers change pack state), so
            // one sample covers the whole batch.
            c_pragma_state_record(&pragma_changes, output_count, pack_alignment, visibility);
            CTokenShape* line_shapes = 0;
            CToken* line_output = c_token_stream_reserve(&token_stream, logical_end - token_index, &line_shapes);
            u64 written = 0;
            for (u64 scan = token_index; scan < logical_end;)
            {
                u64 segment_start = scan;
                if (classified)
                {
                    scan = BUSTER_MIN(c_pp_line_end_masked(class_masks, lex.token_count, scan), logical_end);
                }
                else
                {
                    while (scan < logical_end && lex.tokens[scan].kind != C_TOKEN_NEWLINE)
                    {
                        scan += 1;
                    }
                }
                u64 segment_count = scan - segment_start;
                if (segment_count)
                {
                    memcpy(line_output + written, lex.tokens + segment_start, segment_count * sizeof(*line_output));
                    if (lex.token_shapes)
                    {
                        memcpy(line_shapes + written, lex.token_shapes + segment_start, segment_count * sizeof(*line_shapes));
                    }
                    else
                    {
                        for (u64 segment_index = 0; segment_index < segment_count; segment_index += 1)
                        {
                            line_shapes[written + segment_index] = c_token_shape_from_token(line_output[written + segment_index]);
                        }
                    }
                    written += segment_count;
                }
                if (scan < logical_end)
                {
                    scan += 1;
                }
            }
            c_token_stream_shrink(&token_stream, (logical_end - token_index) - written);
            output_count += written;
        }
        else
        {
            u32 token_file = c_preprocess_file_index(arena, &file_table, source_frame->logical_path);
            u64 wrapped_capacity = logical_end - token_index;
            CPpToken* wrapped_tokens = arena_allocate(arena, CPpToken, wrapped_capacity);
            u32 wrapped_count = 0;
            stamps.count = 0;
            if (source_conditionals)
            {
                for (CPreprocessSourceSegment* segment = first_segment; segment; segment = segment->next)
                {
                    for (u64 scan = segment->first; scan < segment->end; scan += 1)
                    {
                        if (lex.tokens[scan].kind != C_TOKEN_NEWLINE)
                        {
                            wrapped_tokens[wrapped_count] = (CPpToken){
                                .token = lex.tokens[scan],
                                .preceded_by_space = options.retain_output_spacing &&
                                    (!scan || c_token_preceded_by_space(space->base, lex.tokens[scan - 1], lex.tokens[scan])),
                            };
                            wrapped_count += 1;
                        }
                    }
                }
            }
            else if (classified)
            {
                // Wrapping runs of tokens between the line's newlines, the
                // same segments the fast path copies, so the newline test is
                // one mask lookup per segment instead of one per token.
                for (u64 scan = token_index; scan < logical_end;)
                {
                    u64 segment_end = BUSTER_MIN(c_pp_line_end_masked(class_masks, lex.token_count, scan), logical_end);
                    for (; scan < segment_end; scan += 1)
                    {
                        // Unstamped: an invocation recovers its own location
                        // through the frame (c_preprocess_recover_location).
                        wrapped_tokens[wrapped_count] = (CPpToken){
                            .token = lex.tokens[scan],
                            .preceded_by_space = options.retain_output_spacing &&
                                (!scan || c_token_preceded_by_space(space->base, lex.tokens[scan - 1], lex.tokens[scan])),
                        };
                        wrapped_count += 1;
                    }
                    scan += 1;
                }
            }
            else
            {
                for (u64 scan = token_index; scan < logical_end; scan += 1)
                {
                    if (lex.tokens[scan].kind != C_TOKEN_NEWLINE)
                    {
                        wrapped_tokens[wrapped_count] = (CPpToken){
                            .token = lex.tokens[scan],
                            .preceded_by_space = options.retain_output_spacing &&
                                (!scan || c_token_preceded_by_space(space->base, lex.tokens[scan - 1], lex.tokens[scan])),
                        };
                        wrapped_count += 1;
                    }
                }
            }
            CPreprocessTokenNode* first_line = 0;
            CPreprocessTokenNode* last_line = 0;
            u64 line_output_count = 0;
            expansion_ok = c_preprocess_expand(arena, &expansion_storage, space, symbol_table, first_macro, source_frame, token_file, &stamps, wrapped_tokens, wrapped_count, &first_line, &last_line,
                                               &line_output_count, expansion_limit, &result, &pragma_context);
            if (expansion_ok)
            {
                COutputSpacingBlock* spacing = 0;
                if (options.retain_output_spacing && line_output_count)
                {
                    spacing = arena_allocate(arena, COutputSpacingBlock, 1);
                    *spacing = (COutputSpacingBlock){
                        .boundaries = arena_allocate(arena, u8, line_output_count),
                        .first = output_count,
                    };
                    if (last_spacing)
                    {
                        last_spacing->next = spacing;
                    }
                    else
                    {
                        first_spacing = spacing;
                    }
                    last_spacing = spacing;
                }
                c_preprocess_process_expanded_line(pragma_context, space, &map, &stamps, first_line, line_output_count, &token_stream, &output_count, spacing);
            }
        }
        source_frame->token_index = logical_end;
        if (!expansion_ok)
        {
            break;
        }
    }
    CTokenShape* end_of_file_shape = 0;
    CToken* end_of_file = c_token_stream_reserve(&token_stream, 1, &end_of_file_shape);
    *end_of_file = (CToken){
        .offset = root_lex.tokens[root_lex.token_count - 1].offset,
        .kind = C_TOKEN_END_OF_FILE,
    };
    end_of_file_shape[0] = c_token_shape_from_token(*end_of_file);
    result.tokens = token_stream.base;
    recovery->token_shapes = token_stream.shape_base;
    result.token_count = output_count + 1;
    if (first_spacing)
    {
        u8* boundaries = arena_allocate(result_arena, u8, result.token_count);
        memset(boundaries, 0, result.token_count);
        for (COutputSpacingBlock* block = first_spacing; block; block = block->next)
        {
            memcpy(boundaries + block->first, block->boundaries, block->count);
        }
        result.detail->output_spacing = boundaries;
    }
    result.pragma_changes = pragma_changes.changes;
    result.pragma_change_count = pragma_changes.count;
    // The stream is contiguous, so the spellings sum in one linear pass
    // rather than one add per token as the lines were appended.
    result.detail->preprocessed.tokens = output_count;
    if (!options.omit_spelled_bytes)
    {
        // The accumulator and both bases are locals on purpose. c_token_length
        // keeps a call in its oversized arm, so a member accumulator has to be
        // reloaded and stored once per token against that call's possible
        // writes, and the stream and spelling bases with it: three extra memory
        // operations per token over a stream that is already the largest cold
        // read of the phase.
        char8 const* spelling_base = space->base;
        CToken const* stream = result.tokens;
        u64 spelled_bytes = 0;
        IR_DIAGNOSTIC_CENSUS_RECORD(C_METRICS_TOKEN_VISITS, output_count);
        for (u64 token_index = 0; token_index < output_count; token_index += 1)
        {
            spelled_bytes += c_token_length(spelling_base, stream[token_index]);
        }
        result.detail->preprocessed.bytes += spelled_bytes;
    }
    result.detail->preprocessed.spelling_bytes = space->used;
    C_CENSUS_RECORD(OUTPUT_TOKEN_ROWS, result.token_count);
    C_CENSUS_RECORD(OUTPUT_TOKEN_ROW_BYTES, result.token_count * (sizeof(CToken) + sizeof(CTokenShape)));
    C_CENSUS_RECORD(SPACE_TOTAL_BYTES, space->used);
    result.detail->lexed_files = metrics_files.rows;
    result.detail->lexed_file_count = metrics_files.count;
    if (options.dump_macros)
    {
        result.detail->macro_dump = c_macro_dump_text(arena, first_macro, space->base);
    }
#if BUSTER_INCLUDE_TESTS
    result.detail->include_file_probe_count = include_files.probe_count;
#endif
    // Origin recovery and publication both require the stable key order.
    c_source_map_sort(arena, map.regions, map.count);
    c_source_map_finish_origins(arena, &map, &file_table, &result);
    result.files = file_table.files;
    result.file_count = file_table.count;
#if BUSTER_INCLUDE_TESTS
    result.detail->file_table_compare_count = file_table.compare_count;
#endif
    c_source_map_publish(arena, recovery, &map);
    // Respelling queries the published prefix and may append regions (moving
    // their backing array). Rebuild keys only for that appended tail; without
    // it, a second publication would retain an identical key array in the TU.
    c_preprocess_respell_identifiers(space, &map, &result);
    u64 designator_tokens = options.preserve_spellings || options.assembly_comment_lines
                                ? 0 : c_preprocess_rewrite_obsolete_designators(arena, space, &map, &result);
    output_count += designator_tokens;
    result.detail->preprocessed.tokens += designator_tokens;
    if (!options.preserve_spellings && !options.assembly_comment_lines)
    {
        c_preprocess_rename_local_labels(arena, space, &map, &result);
    }
    c_source_map_publish_appended(arena, recovery, &map);
    u32 page_count = (u32)((space->used >> IR_SOURCE_MAP_PAGE_SHIFT) + 1);
    u32* pages = arena_allocate(arena, u32, page_count);
    u32 fill_entry = 0;
    for (u32 page = 0; page < page_count; page += 1)
    {
        u32 page_start = page << IR_SOURCE_MAP_PAGE_SHIFT;
        while (fill_entry + 1 < map.count && map.regions[fill_entry + 1].start <= page_start)
        {
            fill_entry += 1;
        }
        pages[page] = fill_entry;
    }
    recovery->map.pages = pages;
    recovery->map.page_count = page_count;
    // Phase 7 diagnoses invalid preprocessing tokens that survive expansion.
    for (u64 index = 0; index < output_count; index += 1)
    {
        if (c_token_shape_kind(recovery->token_shapes[index]) == C_TOKEN_INVALID)
        {
            c_preprocess_diagnostic_push(arena, &result, (CSourceLocation){.map_offset = result.tokens[index].offset},
                                         C_DIAGNOSTIC_INVALID_CHARACTER,
                                         string_format(arena, S8("invalid character byte {u32} in C source"),
                                                       (u32)(u8)space->base[result.tokens[index].offset]));
        }
    }
    for (u64 index = 0; index < result.diagnostic_count; index += 1)
    {
        CDiagnostic* diagnostic = result.diagnostics + index;
        IrSourcePosition position = ir_source_map_position(&recovery->map, diagnostic->location.map_offset, 0);
        if (position.line)
        {
            diagnostic->location = c_source_location_from_position(diagnostic->location.map_offset, position);
        }
    }
    c_macro_expansion_storage_destroy(&expansion_storage, result.detail);
    CPreprocessSeal seal = {
        .destination = result_arena,
        .phase_arena = phase_arena,
        .phase_start = phase_start,
    };
    c_preprocess_seal(&seal, &result);
    seal.metrics.released_bytes = arena_release_to_position(phase_arena, phase_start);
    result.detail->boundary = seal.metrics;
    if (phase_arena_owned)
    {
        c_phase_arena_retire(phase_arena);
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
#define C_TEST_UNIT_REGISTRY_RESERVED_SIZE BUSTER_MB(64)
// One unit this thread preprocessed and has not yet released: its three
// private arenas, the arena its result lives in, and that arena's position
// when preprocessing began (rewinding below it kills the result).
typedef struct CTestUnitEntry CTestUnitEntry;
struct CTestUnitEntry
{
    Arena* arenas[3];
    Arena* owner;
    u64 owner_position;
};
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL Arena* c_test_unit_registry_arena;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL CTestUnitEntry* c_test_unit_registry;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL u64 c_test_unit_registry_count;

BUSTER_GLOBAL_LOCAL void c_test_unit_register(Arena* spelling, Arena* tokens, Arena* shapes, Arena* owner, u64 owner_position)
{
    if (!c_test_unit_registry_arena)
    {
        c_test_unit_registry_arena = arena_create((ArenaCreation){.reserved_size = C_TEST_UNIT_REGISTRY_RESERVED_SIZE, .flags = {.no_pool = 1}});
    }
    if (c_test_unit_registry_arena)
    {
        CTestUnitEntry* entry = arena_allocate(c_test_unit_registry_arena, CTestUnitEntry, 1);
        if (!c_test_unit_registry)
        {
            c_test_unit_registry = entry;
        }
        *entry = (CTestUnitEntry){.arenas = {spelling, tokens, shapes}, .owner = owner, .owner_position = owner_position};
        c_test_unit_registry_count += 1;
    }
}

// The address ranges c_preprocess_release returned on this thread since the
// last reset. A result that still points into one holds released memory. The
// log is per thread so concurrent tests cannot reset it or inject ranges (a
// range another thread released may be unmapped and later remapped under this
// thread's result). It therefore covers the compiles that run on the calling
// thread: single inputs and serial multi-input compiles. Units a multi-input
// compile runs on lane workers (compile_jobs above one) release on those
// threads and are not seen; that only loses detections, never invents any.
#define C_TEST_RELEASED_LOG_CAPACITY 256
typedef struct CTestReleasedRange CTestReleasedRange;
struct CTestReleasedRange
{
    u8 const* begin;
    u64 size;
};
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL CTestReleasedRange c_test_released_log[C_TEST_RELEASED_LOG_CAPACITY];
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL u64 c_test_released_log_count;

BUSTER_GLOBAL_LOCAL void c_test_released_log_record(Arena const* arena)
{
    if (c_test_released_log_count < C_TEST_RELEASED_LOG_CAPACITY)
    {
        c_test_released_log[c_test_released_log_count] = (CTestReleasedRange){.begin = (u8 const*)arena, .size = arena->reserved_size};
    }
    c_test_released_log_count += 1;
}

void c_test_released_log_reset(void)
{
    c_test_released_log_count = 0;
}

bool c_test_released_log_contains(void const* pointer, u64 length)
{
    bool result = false;
    u64 count = BUSTER_MIN(c_test_released_log_count, (u64)C_TEST_RELEASED_LOG_CAPACITY);
    for (u64 index = 0; length != 0 && index < count; index += 1)
    {
        u8 const* address = (u8 const*)pointer;
        CTestReleasedRange range = c_test_released_log[index];
        result = result || (address >= range.begin && address < range.begin + range.size);
    }
    return result;
}

// Removes entry `index`, moving the last entry into its slot.
BUSTER_GLOBAL_LOCAL void c_test_unit_remove(u64 index)
{
    c_test_unit_registry[index] = c_test_unit_registry[c_test_unit_registry_count - 1];
    c_test_unit_registry_count -= 1;
    arena_set_position(c_test_unit_registry_arena, c_test_unit_registry_arena->position - sizeof(CTestUnitEntry));
}

BUSTER_GLOBAL_LOCAL void c_test_unit_unregister(Arena* spelling)
{
    for (u64 index = c_test_unit_registry_count; index > 0; index -= 1)
    {
        if (c_test_unit_registry[index - 1].arenas[0] == spelling)
        {
            c_test_unit_remove(index - 1);
            break;
        }
    }
}

void c_test_release_preprocess_arenas_from(Arena* owner, u64 position)
{
    u64 index = c_test_unit_registry_count;
    while (index > 0)
    {
        index -= 1;
        CTestUnitEntry entry = c_test_unit_registry[index];
        if (!owner || (entry.owner == owner && entry.owner_position >= position))
        {
            c_test_unit_remove(index);
            for (u32 arena_index = 0; arena_index < BUSTER_ARRAY_LENGTH(entry.arenas); arena_index += 1)
            {
                arena_retire(entry.arenas[arena_index], C_PHASE_ARENA_RETAINED_SIZE);
            }
        }
    }
}

void c_test_release_preprocess_arenas(void)
{
    c_test_release_preprocess_arenas_from(0, 0);
}

void c_test_scratch_end(TemporalArena temporary)
{
    c_test_release_preprocess_arenas_from(temporary.arena, temporary.position);
    scratch_end(temporary);
}
#endif

void c_preprocess_release(CPreprocessResult* result)
{
    CSourceMapRecovery* recovery = result->recovery;
    if (recovery)
    {
        // The record lives in the caller's arena and every copy of the result
        // shares it, so clearing the arena pointers here makes a second
        // release, through any copy, a no-op.
        Arena* arenas[] = {recovery->token_shape_arena, recovery->token_arena, recovery->spelling_arena};
#if BUSTER_INCLUDE_TESTS
        if (recovery->spelling_arena)
        {
            c_test_unit_unregister(recovery->spelling_arena);
        }
#endif
        recovery->token_shape_arena = 0;
        recovery->token_arena = 0;
        recovery->spelling_arena = 0;
        recovery->token_shapes = 0;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(arenas); index += 1)
        {
            if (arenas[index])
            {
#if BUSTER_INCLUDE_TESTS
                c_test_released_log_record(arenas[index]);
#endif
                arena_retire(arenas[index], C_PHASE_ARENA_RETAINED_SIZE);
            }
        }
        result->tokens = 0;
        result->token_count = 0;
        result->spelling_base = 0;
    }
}

CPreprocessResult c_preprocess(Arena* arena, String8 source, CPreprocessOptions options)
{
    C_CENSUS_PHASE_BEGIN(PREPROCESS);
    CPreprocessResult result;
    CSourceAllocationPlan plan;
    if (arena && !c_source_allocation_plan(source.length, &plan))
    {
        // Reject before phase setup and any source-derived capacity sums.
        // A sentinel length is sufficient to exercise this path: no byte of
        // the source is read and no source-sized storage is requested.
        result = (CPreprocessResult){
            .target = options.target,
            .dialect = options.dialect < C_PREPROCESS_DIALECT_COUNT ? options.dialect : C_PREPROCESS_DIALECT_GNU17,
        };
        result.detail = arena_allocate(arena, CPreprocessDetail, 1);
        *result.detail = (CPreprocessDetail){
            .data_layout = target_data_layout_is_valid(options.data_layout) ? options.data_layout : target_data_layout(options.target),
        };
        result.files = arena_allocate(arena, String8, 1);
        result.files[0] = string_duplicate_arena(arena, options.source_path.length ? options.source_path : S8("."), false);
        result.file_count = 1;
        result.diagnostic_capacity = 1;
        result.diagnostics = arena_allocate(arena, CDiagnostic, 1);
        C_DIAGNOSTIC_RESERVATION_CENSUS(PREPROCESS, 1);
        c_preprocess_diagnostic_push(arena, &result, (CSourceLocation){.line = 1, .column = 1}, C_DIAGNOSTIC_SOURCE_TOO_LARGE,
                                     S8("C source exceeds the 4294967293-byte translation limit"));
    }
    else
    {
        result = c_preprocess_run(arena, source, options);
    }
    C_CENSUS_PHASE_END();
    return result;
}

BUSTER_C_SHARED bool c_parse_auto_type_word(String8 spelling)
{
    return string_equal(spelling, S8("__auto_type"));
}

// The keyword set is probed once per identifier from several parser scan
// loops, so it is a hashed set built once from the spellings table rather
// than a linear walk that re-derived every keyword's length per query.

BUSTER_C_SHARED u8 c_declaration_keyword_slots[C_DECLARATION_KEYWORD_SLOT_COUNT];
BUSTER_C_SHARED bool c_declaration_keyword_slots_built;

BUSTER_C_SHARED void c_declaration_keyword_slots_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    for (u32 keyword_index = 0; keyword_index < BUSTER_ARRAY_LENGTH(c_declaration_keyword_spellings); keyword_index += 1)
    {
        u32 slot = (u32)(c_macro_name_hash(c_declaration_keyword_spellings[keyword_index]) & (C_DECLARATION_KEYWORD_SLOT_COUNT - 1));
        while (c_declaration_keyword_slots[slot])
        {
            slot = (slot + 1) & (C_DECLARATION_KEYWORD_SLOT_COUNT - 1);
        }
        c_declaration_keyword_slots[slot] = (u8)(keyword_index + 1);
    }
    c_declaration_keyword_slots_built = true;
}

// Every C frontend table that is built on first use is filled here on the
// calling thread. The character-class tables remain runtime-derived because
// spelling their predicates through the preprocessor measurably inflates the
// frontend translation unit.
void c_prewarm(void)
{
#if BUSTER_C_LEX_COMPACT
    if (!c_lex_compact_tables_built)
    {
        c_lex_compact_tables_build();
    }
#endif
    if (!c_identifier_continue_table_built)
    {
        c_identifier_continue_table_build();
    }
    if (!c_literal_plain_table_built)
    {
        c_literal_plain_table_build();
    }
    if (!c_punctuator_dispatch_built)
    {
        c_punctuator_dispatch_build();
    }
    if (!c_declaration_keyword_slots_built)
    {
        c_declaration_keyword_slots_build();
    }
}

#if !BUSTER_UNITY_BUILD
String8 c_token_kind_name(CTokenKind kind)
{
    switch (kind)
    {
    case C_TOKEN_INVALID:
        return S8("invalid");
    case C_TOKEN_END_OF_FILE:
        return S8("end of file");
    case C_TOKEN_IDENTIFIER:
        return S8("identifier");
    case C_TOKEN_PREPROCESSING_NUMBER:
        return S8("preprocessing number");
    case C_TOKEN_CHARACTER_LITERAL:
        return S8("character literal");
    case C_TOKEN_STRING_LITERAL:
        return S8("string literal");
    case C_TOKEN_PUNCTUATOR:
        return S8("punctuator");
    case C_TOKEN_NEWLINE:
        return S8("newline");
    case C_TOKEN_PRAGMA:
        return S8("pragma");
    case C_TOKEN_KIND_COUNT:
        return S8("invalid token kind");
    }
    return S8("invalid token kind");
}
#endif
