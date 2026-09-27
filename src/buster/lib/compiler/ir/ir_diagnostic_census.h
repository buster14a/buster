#pragma once

// Calling-thread census of the work a compilation spends on information that
// only a diagnostic, a debug consumer or a failure path reads: source-map
// position recovery (line/column conversions), checkpoint searches, original
// provenance walks, source bytes rescanned for positions, source locations
// resolved into semantic records, and diagnostic storage reserved before any
// diagnostic exists. Owned by ir.c beside the construction census and present
// only in the existing allocation diagnostic build (BUSTER_BENCH_ALLOCATIONS),
// so normal builds neither evaluate recording arguments nor retain counters.
//
// These are cumulative event and byte counts since process start, including
// failed attempts; they are not timings, resident memory, or all-lane totals.
// Reserved bytes are logical arena requests, not committed or touched pages.
#include <buster/lib/base.h>

#if BUSTER_BENCH_ALLOCATIONS
#define IR_DIAGNOSTIC_CENSUS_COUNTERS(X) \
    X(POSITION_QUERIES, position_queries) \
    X(POSITION_MEMO_HITS, position_memo_hits) \
    X(POSITION_CHECKPOINT_SEARCHES, position_checkpoint_searches) \
    X(SOURCE_QUERIES, source_queries) \
    X(ORIGINAL_QUERIES, original_queries) \
    X(ORIGINAL_STEPS, original_steps) \
    X(TEXT_POSITION_QUERIES, text_position_queries) \
    X(TEXT_BYTES_SCANNED, text_bytes_scanned) \
    X(C_RECORD_SITES, c_record_sites) \
    X(C_SITE_RESOLUTIONS, c_site_resolutions) \
    X(C_VISIBILITY_LOCATIONS, c_visibility_locations) \
    X(C_DIRECTIVE_LOCATIONS, c_directive_locations) \
    X(C_STAMP_LOCATIONS, c_stamp_locations) \
    X(C_METRICS_TOKEN_VISITS, c_metrics_token_visits) \
    X(C_DIAGNOSTICS_RECORDED, c_diagnostics_recorded) \
    X(C_DIAGNOSTIC_RESERVATIONS, c_diagnostic_reservations) \
    X(C_DIAGNOSTIC_ROWS_RESERVED, c_diagnostic_rows_reserved) \
    X(C_DIAGNOSTIC_BYTES_RESERVED, c_diagnostic_bytes_reserved) \
    X(C_DIAGNOSTIC_LEX_ROWS, c_diagnostic_lex_rows) \
    X(C_DIAGNOSTIC_PREPROCESS_ROWS, c_diagnostic_preprocess_rows) \
    X(C_DIAGNOSTIC_SEMANTIC_ROWS, c_diagnostic_semantic_rows) \
    X(C_DIAGNOSTIC_EVALUATION_ROWS, c_diagnostic_evaluation_rows) \
    X(C_DIAGNOSTIC_LOWERING_ROWS, c_diagnostic_lowering_rows) \
    X(C_LEX_DIAGNOSTIC_ARENAS, c_lex_diagnostic_arenas) \
    X(C_LEX_DIAGNOSTIC_ARENA_BYTES, c_lex_diagnostic_arena_bytes)

typedef enum IrDiagnosticCensusCounter
{
#define IR_DIAGNOSTIC_CENSUS_ENUM(id, name) IR_DIAGNOSTIC_CENSUS_##id,
    IR_DIAGNOSTIC_CENSUS_COUNTERS(IR_DIAGNOSTIC_CENSUS_ENUM)
#undef IR_DIAGNOSTIC_CENSUS_ENUM
    IR_DIAGNOSTIC_CENSUS_COUNT,
} IrDiagnosticCensusCounter;

typedef struct IrDiagnosticCensus IrDiagnosticCensus;
struct IrDiagnosticCensus
{
    u64 values[IR_DIAGNOSTIC_CENSUS_COUNT];
    bool overflowed;
};

BUSTER_F_DECL void ir_diagnostic_census_record(IrDiagnosticCensusCounter counter, u64 amount);
BUSTER_F_DECL IrDiagnosticCensus ir_diagnostic_census(void);
BUSTER_F_DECL String8 ir_diagnostic_census_counter_name(IrDiagnosticCensusCounter counter);
#define IR_DIAGNOSTIC_CENSUS_RECORD(counter, amount) ir_diagnostic_census_record(IR_DIAGNOSTIC_CENSUS_##counter, (u64)(amount))
#else
#define IR_DIAGNOSTIC_CENSUS_RECORD(counter, amount) ((void)0)
#endif
