/* #881-E production result composer (A1). Linux service code only.
 *
 * Ownership: lane E. The composer turns lane D's published A/B-stage streams
 * (transcript shards, row/batch numeric shards, per-batch metrics shards,
 * untimed batch records and their metrics shards) into the #511 binding's
 * sealed result: the #615 result-input manifests, the code-byte record set,
 * the #619 statistics input, the reviewed `bench_throughput
 * retirement-replay` output, the post-sample execution receipt, the result
 * bundle and the `workflow.phases.sealed_result` record. It never measures,
 * never chooses a trust anchor and never issues a verdict: the independent
 * validator (tools/native_retirement_performance_binding.py) re-derives every
 * join, and the producer authority is issued separately from the live store
 * (tp_retirement_store_receipt_authority).
 *
 * Entry points:
 *   tp_retirement_compose_bounds   byte/file bounds of the composer outputs
 *   tp_retirement_compose_plan     plan the result store before any timing
 *   tp_retirement_compose          verify every input and publish the result
 *
 * Every store file must be classified by the request (sealed input or
 * retained file); a missing, extra, reordered or mismatched shard, a digest
 * mismatch, or any bound excess refuses the whole composition and poisons the
 * store. Nothing is repaired or overwritten. Definitions live in
 * tools/bench_service/retirement_compose.c.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_COMPOSE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_COMPOSE_H
/* Lane D's collection primitives are header-only `static` functions written
 * for a translation unit that uses all of them (tools/throughput/tests.c).
 * The composer is its own translation unit and uses only the schedule cursor,
 * the canonical encoders and the store preflight, so only the unused-function
 * diagnostic is relaxed, and only across this one include. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../throughput/retirement_campaign.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef __linux__
/* The six #619 slice dimensions, in the validator's STATISTICAL_DIMENSIONS
 * order: target, cpu, allocator, frontend_lowering, PIC, artifact_stage. */
#define TP_RETIREMENT_COMPOSE_DIMENSIONS 6u
/* The five #619 variable metrics, in TpRetirementMetric index order. */
#define TP_RETIREMENT_COMPOSE_METRICS 5u
/* Statistical-family caps of the validator's _family_member_counts. */
#define TP_RETIREMENT_COMPOSE_BOOTSTRAP_MEMBERS 80u
#define TP_RETIREMENT_COMPOSE_CELL_MEMBERS 300000u
/* The #615 immutable partition bound across both populations. */
#define TP_RETIREMENT_COMPOSE_PARTITIONS 3u
/* A #619 adapter member identity is read with `%127s`. */
#define TP_RETIREMENT_COMPOSE_MEMBER_BYTES 128u
#define TP_RETIREMENT_COMPOSE_NAME_BYTES 128u
/* Composer-owned store leaves (the manifests use the plan's paths and the
 * sealed record uses the request's path). */
#define TP_RETIREMENT_COMPOSE_CODE_PATH "retirement-code-records.jsonl"
#define TP_RETIREMENT_COMPOSE_SERIES_PATH "retirement-statistics-series.txt"
#define TP_RETIREMENT_COMPOSE_REPLAY_PATH "retirement-statistics-replay.json"
#define TP_RETIREMENT_COMPOSE_BUNDLE_PATH "retirement-result-bundle.json"
/* Composer outputs besides the result-input manifests: code records, series,
 * replay output, bundle, execution receipt and sealed-result record. */
#define TP_RETIREMENT_COMPOSE_FIXED_OUTPUTS 6u

/* One row of the native-host timed projection, in ascending census order.
 * group is its campaign batch-group ordinal (ascending smallest member);
 * runtime is 1 for a runtime-eligible (singleton link/self-host) row. The
 * dimension strings are the row identity's frozen values. */
typedef struct TpRetirementComposeRow
{
    unsigned id, group, runtime;
    char const* dimensions[TP_RETIREMENT_COMPOSE_DIMENSIONS];
} TpRetirementComposeRow;

/* The frozen A1 layout the gate authenticated before timing. group_kinds are
 * TP_RETIREMENT_GROUP_OBJECT or TP_RETIREMENT_GROUP_SINGLETON per campaign
 * group; population_rows is the canonical performance-row count;
 * untimed_groups is the untimed code-artifact group count. */
typedef struct TpRetirementComposeLayout
{
    TpRetirementComposeRow const* rows;
    unsigned const* group_kinds;
    unsigned row_count, group_count, population_rows, untimed_groups;
} TpRetirementComposeLayout;

/* One predeclared #615 partition of the pre-sample result-input plan. */
typedef struct TpRetirementComposePartition
{
    char identity[TP_RETIREMENT_COMPOSE_NAME_BYTES + 1];
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    uint64_t start, records;
} TpRetirementComposePartition;

/* One frozen code-observed row (every target) and its two variants' facts. */
typedef struct TpRetirementComposeCode
{
    unsigned row;
    TpRetirementCodeSide sides[2];
} TpRetirementComposeCode;

/* One pre-existing sealed-closure artifact (the binding's pre-sample and
 * post-A/A identities, `contract.source`, `workflow.execution_plan`, census
 * projections): the validator's _sealed_closure_files name and descriptor. */
typedef struct TpRetirementComposeClosure
{
    char const* name;
    char const* path;
    uint64_t bytes;
    char const* sha256;
} TpRetirementComposeClosure;

/* Sizes known before timing that bound the composer outputs. */
typedef struct TpRetirementComposeShape
{
    TpRetirementComposeLayout const* layout;
    unsigned pairs, code_rows, prior_entries;
} TpRetirementComposeShape;

typedef struct TpRetirementComposeBounds
{
    uint64_t manifests, code, series, replay, bundle, receipt, seal, total;
    unsigned manifest_count, files, members, bootstrap_members, cell_members;
} TpRetirementComposeBounds;

typedef struct TpRetirementComposeRequest
{
    TpRetirementStore* store;
    /* Directory under which every prior closure path is re-read (it may be
     * the store root). */
    int evidence_root;
    /* Private scratch directory for the adapter's input copy and output. */
    int scratch_root;
    char const* adapter_path;
    TpRetirementComposeLayout const* layout;
    TpRetirementPlan const* statistics;
    /* The campaign's authenticated identity: the transcript's process
     * instances and the receipt bind exactly these. */
    char const* job;
    char const* boot;
    uint64_t attempt, bound_at_ns, completed_at_ns;
    char const* execution_plan_sha256;
    char const* source_rows_sha256;
    char const* result_input_plan_sha256;
    char const* family_sha256;
    char const* post_aa_binding_sha256;
    /* Canonical JSON of the validator's _execution_context with
     * raw_measurements_sha256 set to 64 '0' placeholder digits. The composer
     * substitutes the digest of the streamed numeric shards, so the receipt's
     * context is post-sample. The campaign freeze's pre-sample context is not
     * accepted here. */
    unsigned char const* context_template;
    size_t context_template_bytes;
    TpRetirementComposePartition const* partitions[2];
    unsigned partition_counts[2];
    char const* const* transcript_paths;
    unsigned transcript_count;
    char const* const* sample_paths[2];
    unsigned sample_counts[2];
    char const* const* metrics_paths;
    unsigned metrics_count;
    char const* untimed_path;
    char const* const* untimed_metrics_paths;
    unsigned untimed_metrics_count;
    TpRetirementComposeCode const* code;
    unsigned code_count;
    TpRetirementComposeClosure const* prior;
    unsigned prior_count;
    /* Store files that are retained evidence but not sealed (A/A streams,
     * logs, lifecycle records). */
    char const* const* retained_paths;
    unsigned retained_count;
    /* Store files the caller still publishes after composition. */
    unsigned later_files;
    char const* sealed_path;
} TpRetirementComposeRequest;

typedef struct TpRetirementComposeArtifact
{
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char sha256[65];
    uint64_t bytes;
} TpRetirementComposeArtifact;

typedef struct TpRetirementComposeResult
{
    char raw_measurements_sha256[65], context_sha256[65];
    TpRetirementComposeArtifact receipt, bundle, sealed, series, replay, code;
    unsigned members, seal_entries;
    uint64_t invocations;
    /* On refusal: the first failing check (a static diagnostic name). */
    char const* refused;
} TpRetirementComposeResult;

/* Derive the family and every composer output bound from pre-timing facts.
 * Rejects a family outside the #619 caps and any output above the store's
 * per-file cap; A1's single-file adapter input is included in that check. */
int tp_retirement_compose_bounds(TpRetirementComposeShape const* shape, TpRetirementComposeBounds* bounds);
/* Before any timing: reserve both campaign stages (capacity), the composer's
 * outputs and the caller's retained files through
 * tp_retirement_campaign_store_preflight, then tp_retirement_store_plan. */
int tp_retirement_compose_plan(TpRetirementStore* store, TpRetirementCampaignCapacity const* capacity,
    TpRetirementComposeShape const* shape, unsigned retained_files, uint64_t retained_bytes,
    unsigned external_entries, uint64_t external_bytes, TpRetirementCampaignStorePlan* plan);
/* After the A/B stage and the untimed batches are published into the planned
 * store. On success every composer output is sealed in the store and the
 * store validates at its settled exact inventory (less later_files). The
 * caller then issues the producer authority with result->context_sha256. */
int tp_retirement_compose(TpRetirementComposeRequest const* request, TpRetirementComposeResult* result);
#endif
#endif
