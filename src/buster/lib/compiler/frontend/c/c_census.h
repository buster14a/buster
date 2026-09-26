#pragma once

// Calling-thread source-fact census for the C frontend, present only in the
// existing allocation diagnostic build (BUSTER_BENCH_ALLOCATIONS). It counts
// the work the source-to-canonical-IR path spends on source bytes and the
// facts derived from them: bytes examined and copied per stage, token rows
// written, identifier hashes and probes, spelling rereads, byte comparisons,
// literal conversions and the arena traffic of each frontend phase. Normal
// builds do not evaluate recording arguments or retain counter storage.
//
// Global counters (C_CENSUS_COUNTERS) belong to one stage by construction.
// Phase counters (C_CENSUS_PHASE_COUNTERS) are attributed to the frontend
// phase active on the calling thread: preprocessing, AST parsing, semantic
// analysis or lowering (c_census_phase_enter/exit bracket the public stage
// entry points; nested entries restore the outer phase). Work outside those
// brackets is attributed to the `other` phase.
//
// "Distinct" and "repeat" counters key a fact by its spelling-space offset:
// the preprocessor registers its space (c_census_space_begin) and a later
// fact at the same offset and of the same kind is a repeat. Spellings outside
// the registered space (standalone lexes, hand-built tokens, synthesized
// strings) are counted as untracked instead. This is a diagnostic population,
// not a timing or a live-memory measurement, and it resets nothing but the
// per-space fact map at c_census_space_begin.
#include <buster/lib/base.h>
#include <buster/lib/string.h>

#if BUSTER_BENCH_ALLOCATIONS
#define C_CENSUS_COUNTERS(X) \
    X(TRANSLATE_CALLS, translate_calls) \
    X(TRANSLATE_INPUT_BYTES, translate_input_bytes) \
    X(TRANSLATE_COPIED_BYTES, translate_copied_bytes) \
    X(TRANSLATE_CHECKPOINTS, translate_checkpoints) \
    X(TRANSLATE_CHECKPOINT_BYTES, translate_checkpoint_bytes) \
    X(LEX_CALLS, lex_calls) \
    X(LEX_INPUT_BYTES, lex_input_bytes) \
    X(LEX_TOKEN_ROWS, lex_token_rows) \
    X(LEX_TOKEN_ROW_BYTES, lex_token_row_bytes) \
    X(LEX_RESERVED_ROW_BYTES, lex_reserved_row_bytes) \
    X(INTERN_PASS_TOKENS, intern_pass_tokens) \
    X(CLASS_MASK_TOKENS, class_mask_tokens) \
    X(OUTPUT_TOKEN_ROWS, output_token_rows) \
    X(OUTPUT_TOKEN_ROW_BYTES, output_token_row_bytes) \
    X(SPACE_SYNTHESIZED_COPIES, space_synthesized_copies) \
    X(SPACE_SYNTHESIZED_BYTES, space_synthesized_bytes) \
    X(SPACE_FOREIGN_COPIES, space_foreign_copies) \
    X(SPACE_FOREIGN_BYTES, space_foreign_bytes) \
    X(SPACE_TOTAL_BYTES, space_total_bytes)

#define C_CENSUS_PHASE_COUNTERS(X) \
    X(SPELLING_READS, spelling_reads) \
    X(SPELLING_READ_BYTES, spelling_read_bytes) \
    X(SPELLING_READ_DISTINCT, spelling_read_distinct) \
    X(SPELLING_READ_UNTRACKED, spelling_read_untracked) \
    X(SPELLING_EQUAL_CALLS, spelling_equal_calls) \
    X(SPELLING_EQUAL_BYTES, spelling_equal_bytes) \
    X(STRING_EQUAL_CALLS, string_equal_calls) \
    X(STRING_EQUAL_BYTES, string_equal_bytes) \
    X(HASH_CALLS, hash_calls) \
    X(HASH_BYTES, hash_bytes) \
    X(INTERN_CALLS, intern_calls) \
    X(INTERN_KEY_BYTES, intern_key_bytes) \
    X(INTERN_PROBES, intern_probes) \
    X(INTERN_MIDDLE_COMPARES, intern_middle_compares) \
    X(INTERN_MIDDLE_BYTES, intern_middle_bytes) \
    X(INTERN_INSERTS, intern_inserts) \
    X(INTERN_REHASH_SLOTS, intern_rehash_slots) \
    X(INTEGER_CONVERSIONS, integer_conversions) \
    X(INTEGER_CONVERSION_BYTES, integer_conversion_bytes) \
    X(INTEGER_CONVERSION_DISTINCT, integer_conversion_distinct) \
    X(INTEGER_CONVERSION_UNTRACKED, integer_conversion_untracked) \
    X(FLOAT_CONVERSIONS, float_conversions) \
    X(FLOAT_CONVERSION_BYTES, float_conversion_bytes) \
    X(FLOAT_CONVERSION_DISTINCT, float_conversion_distinct) \
    X(FLOAT_CONVERSION_UNTRACKED, float_conversion_untracked) \
    X(CHARACTER_DECODES, character_decodes) \
    X(CHARACTER_DECODE_DISTINCT, character_decode_distinct) \
    X(CHARACTER_DECODE_UNTRACKED, character_decode_untracked) \
    X(STRING_DECODES, string_decodes) \
    X(STRING_DECODE_BYTES, string_decode_bytes) \
    X(STRING_DECODE_OUTPUT_BYTES, string_decode_output_bytes) \
    X(STRING_DECODE_DISTINCT, string_decode_distinct) \
    X(STRING_DECODE_UNTRACKED, string_decode_untracked) \
    X(STRING_COUNTS, string_counts) \
    X(STRING_COUNT_BYTES, string_count_bytes) \
    X(STRING_COUNT_DISTINCT, string_count_distinct) \
    X(STRING_COUNT_UNTRACKED, string_count_untracked) \
    X(STRING_MEMO_HITS, string_memo_hits) \
    X(STRING_RANGE_DECODES, string_range_decodes) \
    X(STRING_RANGE_COUNTS, string_range_counts) \
    X(TEMP_SPACES, temp_spaces) \
    X(TEMP_SPACE_BYTES, temp_space_bytes) \
    X(TEMP_TOKENS, temp_tokens) \
    X(TEMP_SPELLING_BYTES, temp_spelling_bytes) \
    X(LOCATION_RECOVERIES, location_recoveries) \
    X(ARENA_CALLS, arena_calls) \
    X(ARENA_BYTES, arena_bytes)

typedef enum CCensusCounter
{
#define C_CENSUS_ENUM(id, name) C_CENSUS_##id,
    C_CENSUS_COUNTERS(C_CENSUS_ENUM)
#undef C_CENSUS_ENUM
    C_CENSUS_COUNT,
} CCensusCounter;

typedef enum CCensusPhaseCounter
{
#define C_CENSUS_PHASE_ENUM(id, name) C_CENSUS_PHASE_##id,
    C_CENSUS_PHASE_COUNTERS(C_CENSUS_PHASE_ENUM)
#undef C_CENSUS_PHASE_ENUM
    C_CENSUS_PHASE_COUNTER_COUNT,
} CCensusPhaseCounter;

typedef enum CCensusPhase
{
    C_CENSUS_PHASE_OTHER,
    C_CENSUS_PHASE_PREPROCESS,
    C_CENSUS_PHASE_PARSE,
    C_CENSUS_PHASE_SEMANTIC,
    C_CENSUS_PHASE_LOWER,
    C_CENSUS_PHASE_COUNT,
} CCensusPhase;

// The kinds of fact the offset map tracks. Spelling reads are tracked per
// phase (bits 0..4); conversions are tracked across the whole unit.
typedef enum CCensusFact
{
    C_CENSUS_FACT_INTEGER,
    C_CENSUS_FACT_FLOAT,
    C_CENSUS_FACT_CHARACTER,
    C_CENSUS_FACT_STRING_DECODE,
    C_CENSUS_FACT_STRING_COUNT,
    C_CENSUS_FACT_COUNT,
} CCensusFact;

typedef struct CCensusCounters CCensusCounters;
struct CCensusCounters
{
    u64 values[C_CENSUS_COUNT];
    u64 phase_values[C_CENSUS_PHASE_COUNT][C_CENSUS_PHASE_COUNTER_COUNT];
    bool overflowed;
};

BUSTER_F_DECL void c_census_record(CCensusCounter counter, u64 amount);
BUSTER_F_DECL void c_census_phase_record(CCensusPhaseCounter counter, u64 amount);
BUSTER_F_DECL CCensusPhase c_census_phase_enter(CCensusPhase phase);
BUSTER_F_DECL void c_census_phase_exit(CCensusPhase previous);
BUSTER_F_DECL void c_census_space_begin(char8 const* base, u64 reserved_size);
BUSTER_F_DECL void c_census_spelling_read(char8 const* pointer, u64 length);
// Records one conversion of kind `fact` over `length` spelling bytes at
// `pointer`, classifying it as distinct, repeat or untracked.
BUSTER_F_DECL void c_census_fact(CCensusFact fact, char8 const* pointer, u64 length);
BUSTER_F_DECL CCensusCounters c_census_counters(void);
BUSTER_F_DECL String8 c_census_counter_name(CCensusCounter counter);
BUSTER_F_DECL String8 c_census_phase_counter_name(CCensusPhaseCounter counter);
BUSTER_F_DECL String8 c_census_phase_name(CCensusPhase phase);
#define C_CENSUS_RECORD(counter, amount) c_census_record(C_CENSUS_##counter, (u64)(amount))
#define C_CENSUS_PHASE_RECORD(counter, amount) c_census_phase_record(C_CENSUS_PHASE_##counter, (u64)(amount))
#define C_CENSUS_FACT(fact, pointer, length) c_census_fact(C_CENSUS_FACT_##fact, (char8 const*)(pointer), (u64)(length))
#define C_CENSUS_PHASE_BEGIN(phase) CCensusPhase c_census_previous_phase = c_census_phase_enter(C_CENSUS_PHASE_##phase)
#define C_CENSUS_PHASE_END() c_census_phase_exit(c_census_previous_phase)
#else
#define C_CENSUS_RECORD(counter, amount) ((void)0)
#define C_CENSUS_PHASE_RECORD(counter, amount) ((void)0)
#define C_CENSUS_FACT(fact, pointer, length) ((void)0)
#define C_CENSUS_PHASE_BEGIN(phase) ((void)0)
#define C_CENSUS_PHASE_END() ((void)0)
#endif
