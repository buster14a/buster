/* #881-C cross-check between the throughput budget encoder and the service
 * worker's budget reader. Included only by retirement_prepare_tests.c, the one
 * test unit that links both tools/throughput/retirement_budget.h and
 * worker_linux.c; the service unit itself never links the encoder.
 *
 * bq_test_worker_budget_crosscheck: records written by
 * tp_retirement_budget_encode, pinned by tp_retirement_budget_digest, are
 * accepted by bq_worker_retirement_runtime with the ceiling rounded down to
 * whole seconds; a record pinned for another budget, or one whose rounded
 * ceiling misses the fixed-phase floor that tp_retirement_budget_preflight
 * sums, is refused.
 */
#ifndef BUSTER_BENCH_WORKER_BUDGET_CROSSCHECK_TESTS_H
#define BUSTER_BENCH_WORKER_BUDGET_CROSSCHECK_TESTS_H

/* Encode `budget`, pin `pinned`'s digest in a one-line profile, and return
 * the worker's verdict with its limit. */
BUSTER_GLOBAL_LOCAL BqError bq_test_worker_budget_verdict(TpRetirementCampaignBudget const* budget,
                                                          TpRetirementCampaignBudget const* pinned,
                                                          u64* runtime)
{
    char record[TP_RETIREMENT_BUDGET_BYTES], digest[65], profile[128];
    size_t size = tp_retirement_budget_encode(budget, record, sizeof(record));
    bool ok = size && tp_retirement_budget_digest(pinned, digest) &&
              snprintf(profile, sizeof(profile), "campaign-budget-sha256=%s\n", digest) == 88;
    BqError error = ok ? bq_worker_retirement_runtime(string_from_pointer(profile),
                                                      (String8){(char8*)record, (u64)size}, runtime) : BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_budget_crosscheck(void)
{
    TpRetirementCampaignBudget budget = {.reviewed_ns = UINT64_C(8640500000000),
        .reservation_ns = 1000000000, .materialization_ns = 2000000000, .baseline_build_ns = 3000000000,
        .candidate_build_ns = 3000000000, .correctness_ns = 4000000000, .settling_per_stage_ns = 500000000,
        .aa_qualification_ns = 600000000, .aa_receipt_sealing_ns = 700000000,
        .sample_export_per_stage_ns = 800000000, .final_statistics_ns = 900000000,
        .final_sealing_ns = 1000000000, .cleanup_ns = 20000000000, .runtime_process_ns = 50000000,
        .metrics_header_bytes = 4096, .metrics_input_bytes = 16384, .aa_attestation_ns_per_mib = 8000000,
        .timed = {2, {{1, 40000000}, {TP_RETIREMENT_BATCH_INPUTS, 2000000000}},
                  {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 45000000, [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 900000000}},
        .untimed = {2, {{1, 50000000}, {TP_RETIREMENT_BATCH_INPUTS, 2500000000}},
                    {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 60000000,
                     [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 1200000000}}};
    u64 runtime = 1;
    BQ_PREP_CHECK(tp_retirement_budget_valid(&budget) &&
                  bq_test_worker_budget_verdict(&budget, &budget, &runtime) == BQ_OK &&
                  runtime == UINT64_C(8640000000));
    /* The worker authenticates the raw bytes; for an encoded record that is
     * exactly the throughput digest of the decoded budget. */
    char record[TP_RETIREMENT_BUDGET_BYTES], digest[65], raw[SHA256_HEX_CAPACITY];
    size_t size = tp_retirement_budget_encode(&budget, record, sizeof(record));
    TpRetirementCampaignBudget decoded = {0};
    BQ_PREP_CHECK(size && tp_retirement_budget_digest(&budget, digest) &&
                  tp_retirement_budget_decode(record, size, &decoded) && !memcmp(&decoded, &budget, sizeof(budget)));
    if (size) bq_digest(record, (u32)size, (char8*)raw);
    BQ_PREP_CHECK(size && !memcmp(raw, digest, 64));
    /* A pin for another budget refuses. */
    TpRetirementCampaignBudget other = budget;
    other.cleanup_ns += 1;
    BQ_PREP_CHECK(bq_test_worker_budget_verdict(&budget, &other, &runtime) == BQ_RECIPE_MISMATCH && runtime == 0);
    /* The floor is the preflight's fixed term, against the rounded limit. */
    TpRetirementBudgetGroups timed = {.inputs = (unsigned[]){1}, .kinds = (unsigned[]){TP_RETIREMENT_GROUP_OBJECT},
                                      .stages = (unsigned[]){TP_RETIREMENT_BUDGET_STAGE_OBJECT}, .count = 1};
    TpRetirementBudgetCounts counts = {.timed = timed, .pairs = TP_RETIREMENT_MIN_PAIRS_PER_ROUND};
    TpRetirementBudgetPreflight preflight = {0};
    /* A 60 s cleanup bound lifts the fixed term above the one-minute range. */
    TpRetirementCampaignBudget floor = budget;
    floor.cleanup_ns = UINT64_C(60000000000);
    BQ_PREP_CHECK(tp_retirement_budget_preflight(&floor, &counts, &preflight) &&
                  preflight.fixed_ns > BQ_SYSTEMD_RETIREMENT_RUNTIME_MIN_USEC * 1000);
    floor.reviewed_ns = (preflight.fixed_ns / UINT64_C(1000000000) + 1) * UINT64_C(1000000000);
    BQ_PREP_CHECK(bq_test_worker_budget_verdict(&floor, &floor, &runtime) == BQ_OK &&
                  runtime * 1000 >= preflight.fixed_ns);
    floor.reviewed_ns = preflight.fixed_ns + (preflight.fixed_ns % UINT64_C(1000000000) ?
                                              0 : UINT64_C(999999999));
    u64 rounded = floor.reviewed_ns / UINT64_C(1000000000) * UINT64_C(1000000000);
    BQ_PREP_CHECK(bq_test_worker_budget_verdict(&floor, &floor, &runtime) ==
                  (rounded >= preflight.fixed_ns ? BQ_OK : BQ_RECIPE_MISMATCH));
    floor.reviewed_ns = preflight.fixed_ns - 1;
    BQ_PREP_CHECK(bq_test_worker_budget_verdict(&floor, &floor, &runtime) == BQ_RECIPE_MISMATCH);
}

#endif
