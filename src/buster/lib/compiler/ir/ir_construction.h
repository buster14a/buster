#pragma once

// Calling-thread construction, preparation, and validation work, owned by ir.c
// and present only in the existing allocation diagnostic build. These are
// cumulative event/row counts, not timings, live memory, all-lane totals, or a
// second IR representation.
// Normal builds do not evaluate recording arguments or retain counter storage.
#include <buster/lib/base.h>

#if BUSTER_BENCH_ALLOCATIONS
#define IR_CONSTRUCTION_COUNTERS(X) \
    X(PROGRAM_STARTS, program_starts) \
    X(TYPE_APPENDS, type_appends) \
    X(SYMBOL_APPENDS, symbol_appends) \
    X(GLOBAL_APPENDS, global_appends) \
    X(BLOCK_APPENDS, block_appends) \
    X(VALUE_APPENDS, value_appends) \
    X(INSTRUCTION_APPENDS, instruction_appends) \
    X(OPERAND_SLOTS_APPENDED, operand_slots_appended) \
    X(BLOCK_GROWS, block_grows) \
    X(VALUE_GROWS, value_grows) \
    X(INSTRUCTION_GROWS, instruction_grows) \
    X(BLOCK_ROWS_COPIED, block_rows_copied) \
    X(VALUE_ROWS_COPIED, value_rows_copied) \
    X(INSTRUCTION_ROWS_COPIED, instruction_rows_copied) \
    X(SOURCE_ROWS_COPIED, source_rows_copied) \
    X(SOURCE_ROWS_CLEARED, source_rows_cleared) \
    X(DEBUG_FUNCTION_INDEX_ROWS, debug_function_index_rows) \
    X(DEBUG_FUNCTION_SEED_SCAN_ROWS, debug_function_seed_scan_rows) \
    X(FUNCTION_STARTS, function_starts) \
    X(BODY_TOKENS, body_tokens) \
    X(PARAMETERS, parameters) \
    X(INITIAL_BLOCK_SLOTS, initial_block_slots) \
    X(INITIAL_INSTRUCTION_SLOTS, initial_instruction_slots) \
    X(INITIAL_VALUE_SLOTS, initial_value_slots) \
    X(SSA_SLOT_GROWS, ssa_slot_grows) \
    X(SSA_SLOT_CLEAR_BYTES, ssa_slot_clear_bytes) \
    X(SSA_SLOT_GROW_VISITS, ssa_slot_grow_visits) \
    X(SSA_FINISH_SLOT_GROWS, ssa_finish_slot_grows) \
    X(SSA_SLOT_REHASH_PROBES, ssa_slot_rehash_probes) \
    X(SSA_SLOT_LOOKUPS, ssa_slot_lookups) \
    X(SSA_SLOT_PROBES, ssa_slot_probes) \
    X(SSA_FINISH_SLOT_PROBES, ssa_finish_slot_probes) \
    X(SSA_SLOT_INSERTS, ssa_slot_inserts) \
    X(SSA_FORWARD_STEPS, ssa_forward_steps) \
    X(SSA_VALUE_CLEAR_BYTES, ssa_value_clear_bytes) \
    X(SSA_VALUE_SCRATCH_BYTES, ssa_value_scratch_bytes) \
    X(SSA_INITIALIZATION_WORK_VISITS, ssa_initialization_work_visits) \
    X(SSA_CFG_TARGET_VISITS, ssa_cfg_target_visits) \
    X(SSA_REACHABLE_TARGET_VISITS, ssa_reachable_target_visits) \
    X(SSA_PENDING_VISITS, ssa_pending_visits) \
    X(SSA_PENDING_PREDECESSOR_VISITS, ssa_pending_predecessor_visits) \
    X(SSA_REPLACEMENT_ROWS, ssa_replacement_rows) \
    X(SSA_SIMPLIFY_PASSES, ssa_simplify_passes) \
    X(SSA_SIMPLIFY_BLOCK_VISITS, ssa_simplify_block_visits) \
    X(SSA_SIMPLIFY_EMPTY_BLOCK_VISITS, ssa_simplify_empty_block_visits) \
    X(SSA_SIMPLIFY_PARAMETER_VISITS, ssa_simplify_parameter_visits) \
    X(SSA_SIMPLIFY_INCOMING_VISITS, ssa_simplify_incoming_visits) \
    X(SSA_LIVE_WORK_VISITS, ssa_live_work_visits) \
    X(SSA_REMAP_VALUE_ROWS, ssa_remap_value_rows) \
    X(SSA_REMAP_INSTRUCTION_ROWS, ssa_remap_instruction_rows) \
    X(SSA_REMAP_OPERAND_SLOTS, ssa_remap_operand_slots) \
    X(SSA_REMAP_INCOMING_VISITS, ssa_remap_incoming_visits) \
    X(SSA_FINISH_CALLS, ssa_finish_calls) \
    X(SSA_FINISH_FAILURES, ssa_finish_failures) \
    X(BEFORE_SSA_BLOCK_ROWS, before_ssa_block_rows) \
    X(BEFORE_SSA_INSTRUCTION_ROWS, before_ssa_instruction_rows) \
    X(BEFORE_SSA_VALUE_ROWS, before_ssa_value_rows) \
    X(AFTER_SSA_BLOCK_ROWS, after_ssa_block_rows) \
    X(AFTER_SSA_INSTRUCTION_ROWS, after_ssa_instruction_rows) \
    X(AFTER_SSA_VALUE_ROWS, after_ssa_value_rows) \
    X(FINAL_BLOCK_SLOTS, final_block_slots) \
    X(FINAL_INSTRUCTION_SLOTS, final_instruction_slots) \
    X(FINAL_VALUE_SLOTS, final_value_slots) \
    X(PLACE_LOAD_RETRACTIONS, place_load_retractions) \
    X(PLACE_ATOMIC_LOAD_RETRACTIONS, place_atomic_load_retractions) \
    X(SSA_READ_RETRACTIONS, ssa_read_retractions) \
    X(PLACE_REPAIR_STEPS, place_repair_steps) \
    X(CFG_BUILDS, cfg_builds) \
    X(CFG_FAILURES, cfg_failures) \
    X(CFG_SCRATCH_SLOTS, cfg_scratch_slots) \
    X(CFG_TARGET_VISITS, cfg_target_visits) \
    X(CFG_UNIQUE_EDGES, cfg_unique_edges) \
    X(CFG_PARAMETER_VISITS, cfg_parameter_visits) \
    X(CFG_INCOMING_VISITS, cfg_incoming_visits) \
    X(CFG_COPY_SOURCES, cfg_copy_sources) \
    X(VALIDATION_CALLS, validation_calls) \
    X(VALIDATION_OWNERSHIP_FUNCTION_SCANS, validation_ownership_function_scans) \
    X(VALIDATION_PUBLISHED_CFG_CHECKS, validation_published_cfg_checks) \
    X(VALIDATION_OWNERSHIP_FUNCTIONS, validation_ownership_functions) \
    X(VALIDATION_OWNERSHIP_BLOCKS, validation_ownership_blocks) \
    X(VALIDATION_OWNERSHIP_INSTRUCTIONS, validation_ownership_instructions) \
    X(VALIDATION_OWNERSHIP_BYTES_CLEARED, validation_ownership_bytes_cleared) \
    X(VALIDATION_GLOBALS, validation_globals) \
    X(VALIDATION_GLOBAL_RELOCATIONS, validation_global_relocations) \
    X(VALIDATION_GLOBAL_RELOCATION_PAIRS, validation_global_relocation_pairs) \
    X(VALIDATION_ALIASES, validation_aliases) \
    X(VALIDATION_INITIALIZERS, validation_initializers) \
    X(VALIDATION_FUNCTIONS, validation_functions) \
    X(VALIDATION_VALUE_BLOCKS, validation_value_blocks) \
    X(VALIDATION_VALUE_PARAMETERS, validation_value_parameters) \
    X(VALIDATION_VALUES, validation_values) \
    X(VALIDATION_VALUE_PROVENANCE_CHECKS, validation_value_provenance_checks) \
    X(VALIDATION_VALUE_PROVENANCE_BLOCKS, validation_value_provenance_blocks) \
    X(VALIDATION_BLOCKS, validation_blocks) \
    X(VALIDATION_PARAMETERS, validation_parameters) \
    X(VALIDATION_INCOMING_VALUES, validation_incoming_values) \
    X(VALIDATION_PARAMETER_PROVENANCE_CHECKS, validation_parameter_provenance_checks) \
    X(VALIDATION_INSTRUCTIONS, validation_instructions) \
    X(VALIDATION_OPERAND_IDS, validation_operand_ids) \
    X(VALIDATION_TARGET_IDS, validation_target_ids) \
    X(VALIDATION_RESULT_RELATIONSHIPS, validation_result_relationships) \
    X(VALIDATION_OPERATION_CHECKS, validation_operation_checks) \
    X(VALIDATION_CONVERSION_CHECKS, validation_conversion_checks) \
    X(VALIDATION_CALL_CHECKS, validation_call_checks) \
    X(VALIDATION_CALL_ARGUMENTS, validation_call_arguments) \
    X(VALIDATION_INSTRUCTION_PROVENANCE_CHECKS, validation_instruction_provenance_checks) \
    X(VALIDATION_TERMINATOR_CHECKS, validation_terminator_checks) \
    X(PREPARATION_CALLS, preparation_calls) \
    X(PREPARATION_INPUT_VALIDATIONS, preparation_input_validations) \
    X(PREPARATION_PROMOTION_OUTPUT_VALIDATIONS, preparation_promotion_output_validations) \
    X(PREPARATION_FAST_INPUT_VALIDATIONS, preparation_fast_input_validations) \
    X(PREPARATION_FAST_OUTPUT_VALIDATIONS, preparation_fast_output_validations) \
    X(PREPARATION_PROMOTION_FUNCTIONS, preparation_promotion_functions) \
    X(PREPARATION_FAST_FUNCTIONS, preparation_fast_functions) \
    X(PREPARATION_PUBLICATION_FUNCTIONS, preparation_publication_functions) \
    X(VALIDATION_GLOBAL_RELOCATION_SORTS, validation_global_relocation_sorts) \
    X(VALIDATION_GLOBAL_RELOCATION_SORT_ROWS, validation_global_relocation_sort_rows)

typedef enum IrConstructionCounter
{
#define IR_CONSTRUCTION_ENUM(id, name) IR_CONSTRUCTION_##id,
    IR_CONSTRUCTION_COUNTERS(IR_CONSTRUCTION_ENUM)
#undef IR_CONSTRUCTION_ENUM
    IR_CONSTRUCTION_COUNT,
} IrConstructionCounter;

typedef struct IrConstructionCounters IrConstructionCounters;
struct IrConstructionCounters
{
    u64 values[IR_CONSTRUCTION_COUNT];
    bool overflowed;
};

BUSTER_F_DECL void ir_construction_record(IrConstructionCounter counter, u64 amount);
BUSTER_F_DECL IrConstructionCounters ir_construction_counters(void);
BUSTER_F_DECL String8 ir_construction_counter_name(IrConstructionCounter counter);
#define IR_CONSTRUCTION_RECORD(counter, amount) ir_construction_record(IR_CONSTRUCTION_##counter, (u64)(amount))
#else
#define IR_CONSTRUCTION_RECORD(counter, amount) ((void)0)
#endif

#if BUSTER_UNITY_BUILD && BUSTER_INCLUDE_TESTS
// ide.c includes the construction surface before entering the test-only Clang
// optnone region. Preload the production append inline here so test headers
// cannot stamp optnone onto it before c_gen.c is included.
#include <buster/lib/compiler/ir/ir_append.h>
#endif
