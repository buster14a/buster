#pragma once

// Whole-compiler work ledger: exact, machine-independent work counts grouped
// by mechanism rather than by function, present only in the existing
// allocation diagnostic build (BUSTER_BENCH_ALLOCATIONS). Storage and the
// phase accountant live in ir.c beside ir_construction_record; ide.c writes
// every counter to `-fsource-metrics` as `work.<mechanism>.<name>=<count>`
// and every phase as `work.phase.<phase>.<field>=<count>`.
//
// These are cumulative calling-thread event, row and byte counts. They are not
// timings, not live memory and not all-lane totals. A counter names the work a
// mechanism performed, so a candidate that only moves work shows up as the same
// total under another name. Normal builds evaluate no recording argument and
// retain no storage: WORK_LEDGER_RECORD expands to nothing.
//
// Map of the families (key prefix, then what one unit is):
//   rederive  semantic and lowering answers re-derived from token ranges
//             because there is no AST: root queries, their tokens, repeated
//             initializer walks and per-element checks
//   snapshot  rollback state materialized for the semantic type machine:
//             whole-result checkpoints and by-value frame rows, in bytes
//   population per-query work proportional to a whole table (type table,
//             layout solve) instead of to the query's own data
//   literal   string-literal bytes counted/decoded and numbers converted
//   lookup    symbol interning calls, probes and bytes hashed
//   target    per-process x86-64 metadata preparation versus what a compile
//             reads back
//   machine   machine records selected, placed, encoded and described
//   output    object and linked-image bytes produced
// Phases (WorkLedgerPhase) additionally attribute minor page faults (touched
// pages) and arena request traffic to the pipeline stage that caused them.
#include <buster/lib/base.h>

#if BUSTER_BENCH_ALLOCATIONS
#define WORK_LEDGER_COUNTERS(X) \
    X(REDERIVE_TYPE_QUERY_ROOTS, rederive, type_query_roots) \
    X(REDERIVE_TYPE_QUERY_CACHE_HITS, rederive, type_query_cache_hits) \
    X(REDERIVE_TYPE_QUERY_UNCACHED, rederive, type_query_uncached) \
    X(REDERIVE_TYPE_QUERY_UNCACHED_TOKENS, rederive, type_query_uncached_tokens) \
    X(REDERIVE_TYPE_QUERY_LITERAL_ANSWERS, rederive, type_query_literal_answers) \
    X(REDERIVE_TYPE_MACHINE_RUNS, rederive, type_machine_runs) \
    X(REDERIVE_INITIALIZER_WALKS, rederive, initializer_walks) \
    X(REDERIVE_INITIALIZER_WALK_TOKENS, rederive, initializer_walk_tokens) \
    X(REDERIVE_INITIALIZER_ELEMENTS, rederive, initializer_elements) \
    X(REDERIVE_INITIALIZER_LITERAL_ELEMENTS, rederive, initializer_literal_elements) \
    X(REDERIVE_LOWER_QUERY_ROOTS, rederive, lower_query_roots) \
    X(REDERIVE_LOWER_TYPE_PREDICTIONS, rederive, lower_type_predictions) \
    X(SNAPSHOT_QUERY_CHECKPOINTS, snapshot, query_checkpoints) \
    X(SNAPSHOT_QUERY_CHECKPOINT_BYTES, snapshot, query_checkpoint_bytes) \
    X(SNAPSHOT_FRAME_PUSHES, snapshot, frame_pushes) \
    X(SNAPSHOT_FRAME_BYTES, snapshot, frame_bytes) \
    X(SNAPSHOT_FRAME_CHECKPOINT_SAVES, snapshot, frame_checkpoint_saves) \
    X(SNAPSHOT_ROLLBACKS, snapshot, rollbacks) \
    X(POPULATION_MEMBER_PROMOTED_SEARCHES, population, member_promoted_searches) \
    X(POPULATION_MEMBER_VISITED_BYTES_CLEARED, population, member_visited_bytes_cleared) \
    X(POPULATION_LAYOUT_SOLVES, population, layout_solves) \
    X(POPULATION_LAYOUT_ROWS_SEEDED, population, layout_rows_seeded) \
    X(POPULATION_LAYOUT_SCRATCH_BYTES, population, layout_scratch_bytes) \
    X(POPULATION_TYPES_COMPATIBLE_CALLS, population, types_compatible_calls) \
    X(POPULATION_TYPES_COMPATIBLE_PAIRS, population, types_compatible_pairs) \
    X(LITERAL_STRING_COUNT_CALLS, literal, string_count_calls) \
    X(LITERAL_STRING_COUNT_BYTES, literal, string_count_bytes) \
    X(LITERAL_STRING_DECODE_CALLS, literal, string_decode_calls) \
    X(LITERAL_STRING_DECODE_BYTES, literal, string_decode_bytes) \
    X(LITERAL_NUMBER_CONVERSIONS, literal, number_conversions) \
    X(LOOKUP_SYMBOL_INTERNS, lookup, symbol_interns) \
    X(LOOKUP_SYMBOL_INTERN_PROBES, lookup, symbol_intern_probes) \
    X(LOOKUP_SYMBOL_INTERN_BYTES_HASHED, lookup, symbol_intern_bytes_hashed) \
    X(TARGET_X86_POOL_BYTES_COPIED, target, x86_pool_bytes_copied) \
    X(TARGET_X86_NUL_DISTANCE_ENTRIES, target, x86_nul_distance_entries) \
    X(TARGET_X86_NUL_DISTANCE_READS, target, x86_nul_distance_reads) \
    X(TARGET_X86_BASE64_BYTES_DECODED, target, x86_base64_bytes_decoded) \
    X(TARGET_X86_RECORDS_ASSEMBLED, target, x86_records_assembled) \
    X(TARGET_X86_METADATA_ENCODES, target, x86_metadata_encodes) \
    X(TARGET_X86_FORM_SELECTIONS, target, x86_form_selections) \
    X(TARGET_X86_GPR_TABLES_BUILT, target, x86_gpr_tables_built) \
    X(TARGET_X86_GPR_TABLE_READS, target, x86_gpr_table_reads) \
    X(TARGET_X86_VARIABLE_MEMORY_TABLES_BUILT, target, x86_variable_memory_tables_built) \
    X(TARGET_X86_VARIABLE_MEMORY_TABLE_READS, target, x86_variable_memory_table_reads) \
    X(MACHINE_FUNCTIONS_SELECTED, machine, functions_selected) \
    X(MACHINE_ROWS, machine, rows) \
    X(MACHINE_VIRTUAL_REGISTERS, machine, virtual_registers) \
    X(MACHINE_BLOCKS, machine, blocks) \
    X(MACHINE_PLACEMENT_EDITS, machine, placement_edits) \
    X(MACHINE_LIVENESS_WORD_UPDATES, machine, liveness_word_updates) \
    X(MACHINE_ENCODED_BYTES, machine, encoded_bytes) \
    X(MACHINE_DEBUG_LOCATION_ROWS, machine, debug_location_rows) \
    X(OUTPUT_OBJECT_BYTES, output, object_bytes) \
    X(OUTPUT_LINK_IMAGE_BYTES, output, link_image_bytes)

typedef enum WorkLedgerCounter
{
#define WORK_LEDGER_ENUM(id, mechanism, name) WORK_LEDGER_##id,
    WORK_LEDGER_COUNTERS(WORK_LEDGER_ENUM)
#undef WORK_LEDGER_ENUM
    WORK_LEDGER_COUNT,
} WorkLedgerCounter;

// Pipeline stages in the order a native compile reaches them. STARTUP covers
// process start through reading the input; the phase that is current when the
// metrics are written is closed then, so every fault and arena request of the
// invocation lands in exactly one phase.
#define WORK_LEDGER_PHASES(X) \
    X(STARTUP, startup) \
    X(PREPROCESS, preprocess) \
    X(PARSE, parse) \
    X(SEMANTIC, semantic) \
    X(LOWER, lower) \
    X(PREPARE, prepare) \
    X(TARGET_PREWARM, target_prewarm) \
    X(CODEGEN, codegen) \
    X(OBJECT, object) \
    X(OUTPUT, output)

typedef enum WorkLedgerPhase
{
#define WORK_LEDGER_PHASE_ENUM(id, name) WORK_LEDGER_PHASE_##id,
    WORK_LEDGER_PHASES(WORK_LEDGER_PHASE_ENUM)
#undef WORK_LEDGER_PHASE_ENUM
    WORK_LEDGER_PHASE_COUNT,
} WorkLedgerPhase;

typedef struct WorkLedgerPhaseTotals WorkLedgerPhaseTotals;
struct WorkLedgerPhaseTotals
{
    u64 marks;
    u64 minor_faults;
    u64 arena_calls;
    u64 arena_bytes;
    u64 arena_zero_written;
};

typedef struct WorkLedgerCounters WorkLedgerCounters;
struct WorkLedgerCounters
{
    u64 values[WORK_LEDGER_COUNT];
    WorkLedgerPhaseTotals phases[WORK_LEDGER_PHASE_COUNT];
    bool overflowed;
};

BUSTER_F_DECL void work_ledger_record(WorkLedgerCounter counter, u64 amount);
// Closes the current phase at this point and makes `phase` current.
BUSTER_F_DECL void work_ledger_phase(WorkLedgerPhase phase);
// Snapshot with the current phase closed up to now; the phase stays current.
BUSTER_F_DECL WorkLedgerCounters work_ledger_counters(void);
BUSTER_F_DECL String8 work_ledger_counter_mechanism(WorkLedgerCounter counter);
BUSTER_F_DECL String8 work_ledger_counter_name(WorkLedgerCounter counter);
BUSTER_F_DECL String8 work_ledger_phase_name(WorkLedgerPhase phase);
#define WORK_LEDGER_RECORD(counter, amount) work_ledger_record(WORK_LEDGER_##counter, (u64)(amount))
#define WORK_LEDGER_PHASE(phase) work_ledger_phase(WORK_LEDGER_PHASE_##phase)
#else
#define WORK_LEDGER_RECORD(counter, amount) ((void)0)
#define WORK_LEDGER_PHASE(phase) ((void)0)
#endif
