#pragma once

// Calling-thread construction work, owned by ir.c and present only in the
// existing allocation diagnostic build. These are cumulative event/row counts,
// not timings, live memory, all-lane totals, or a second IR representation.
// Normal builds do not evaluate recording arguments or retain counter storage.
#include <buster/lib/base.h>

#if BUSTER_BENCH_ALLOCATIONS
#define IR_CONSTRUCTION_COUNTERS(X) \
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
    X(CFG_COPY_SOURCES, cfg_copy_sources)

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
