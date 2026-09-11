#pragma once

#include <buster/lib/base.h>

// Private QUALITY work census. The existing allocation-instrumented compiler
// supplies these counters to its source-metrics file; ordinary compilers have
// neither storage nor recorder calls. Counts are cumulative on the calling OS
// thread, like the allocation snapshot, not a cross-worker aggregate.
#if BUSTER_BENCH_ALLOCATIONS || BUSTER_INCLUDE_TESTS
#define BUSTER_QUALITY_CENSUS_FIELDS(X) \
    X(functions) \
    X(invalid_target_functions) \
    X(switch_fallback_functions) \
    X(prepass_failures) \
    X(prepassed_functions) \
    X(rows) \
    X(global_values) \
    X(local_values) \
    X(local_values_zero_functions) \
    X(local_values_small_functions) \
    X(local_values_tiled_functions) \
    X(local_values_large_functions) \
    X(raw_loop_spans) \
    X(merged_regions) \
    X(closure_value_tests) \
    X(closure_intersections) \
    X(region_values_zero_regions) \
    X(region_values_small_regions) \
    X(region_values_tiled_regions) \
    X(region_values_large_regions) \
    X(candidates) \
    X(candidate_cap_functions) \
    X(empty_candidate_functions) \
    X(candidate_region_cells) \
    X(candidate_region_nonzero_cells) \
    X(candidate_region_updates) \
    X(candidate_region_clear_bytes) \
    X(region_table_zero_functions) \
    X(region_table_sparse_functions) \
    X(region_table_mixed_functions) \
    X(region_table_dense_functions) \
    X(initial_value_clear_bytes) \
    X(heap_snapshot_bytes) \
    X(heap_restore_bytes) \
    X(attempts) \
    X(candidate_pops) \
    X(marginal_rejections) \
    X(whole_budget_rows) \
    X(whole_budget_rejections) \
    X(whole_register_probes) \
    X(whole_foreclosure_rejections) \
    X(whole_overlap_rejections) \
    X(whole_assignments) \
    X(split_candidates) \
    X(region_selection_passes) \
    X(region_selection_cells) \
    X(selected_regions) \
    X(region_legality_rejections) \
    X(split_budget_rows) \
    X(split_budget_rejections) \
    X(split_register_probes) \
    X(split_foreclosure_rejections) \
    X(split_overlap_rejections) \
    X(split_cost_rejections) \
    X(split_assignments) \
    X(prefix_rows_built) \
    X(prefix_cells_written) \
    X(attempt_reset_bytes) \
    X(pin_mask_clear_bytes) \
    X(placement_probes) \
    X(placement_cost_rejections) \
    X(pin_verification_rejections) \
    X(accepted_placements)

typedef struct MachineQualityCensus MachineQualityCensus;
struct MachineQualityCensus
{
#define BUSTER_QUALITY_CENSUS_FIELD(name) u64 name;
    BUSTER_QUALITY_CENSUS_FIELDS(BUSTER_QUALITY_CENSUS_FIELD)
#undef BUSTER_QUALITY_CENSUS_FIELD
};

// A failed addition preserves the old value. The recorder fails closed rather
// than publishing a wrapped or silently saturated population/work count.
BUSTER_UNUSED_DECL BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool machine_quality_census_try_add(u64* counter, u64 amount)
{
    bool result = amount <= UINT64_MAX - *counter;
    if (result)
    {
        *counter += amount;
    }
    return result;
}

#if BUSTER_BENCH_ALLOCATIONS
BUSTER_F_DECL MachineQualityCensus machine_quality_census_snapshot(void);
#endif
#endif
