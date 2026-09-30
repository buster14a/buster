/* #881-E production result composer (A1). Linux service code only.
 *
 * Ownership: lane E. The composer turns lane D's published A/B-stage streams
 * (transcript shards, row/batch numeric shards, per-batch metrics shards,
 * untimed batch records and their metrics shards) into the #511 binding's
 * sealed result: the #615 result-input manifests, the code-byte record set,
 * the #619 statistics input (a manifest over ordered series shards, #1880),
 * the reviewed `bench_throughput
 * retirement-replay` output, the post-sample execution receipt, the result
 * bundle, the retained-file manifest and the `workflow.phases.sealed_result`
 * record. It never measures, never chooses a trust anchor and never issues a
 * verdict: the independent validator
 * (tools/native_retirement_performance_binding.py) re-derives every join, and
 * the producer authority is issued separately from the live store
 * (tp_retirement_store_receipt_authority), binding the retained manifest.
 *
 * Entry points (tools/bench_service/retirement_compose.c):
 *   tp_retirement_compose_bounds   byte/file bounds of the composer outputs
 *   tp_retirement_compose_plan     plan the result store before any timing and
 *                                  bind the retained-file declaration
 *   tp_retirement_compose          verify every input and publish the result
 * Canonical JSON: retirement_compose_json.h.
 *
 * Every store file must be a sealed input or match the retained declaration
 * bound at plan time; a missing, extra, reordered or mismatched shard, a
 * sample that differs from its transcript observation, a digest mismatch, or
 * any bound excess refuses the whole composition and poisons the store.
 * Nothing is repaired or overwritten.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_COMPOSE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_COMPOSE_H
#include "../throughput/retirement_campaign.h"
#include "retirement_compose_json.h"

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
/* The external closure (TpRetirementComposeRequest.closure) is at most one
 * store's worth of entries. */
#define TP_RETIREMENT_COMPOSE_CLOSURE_ENTRIES TP_RETIREMENT_STORE_FILES
/* Composer-owned store leaves (the manifests use the plan's paths and the
 * sealed record uses the request's path). */
#define TP_RETIREMENT_COMPOSE_CODE_PATH "retirement-code-records.jsonl"
#define TP_RETIREMENT_COMPOSE_SERIES_PATH "retirement-statistics-series.txt"
#define TP_RETIREMENT_COMPOSE_REPLAY_PATH "retirement-statistics-replay.json"
#define TP_RETIREMENT_COMPOSE_BUNDLE_PATH "retirement-result-bundle.json"
/* (#1880) The #619 adapter input (`sealed_result_bundle.adapter_input`) is the
 * series manifest at TP_RETIREMENT_COMPOSE_SERIES_PATH over ordered shards
 * `retirement-statistics-series-NNNN.txt` beside it. The series stream is
 * unchanged; its canonical split is greedy over whole lines, each shard at
 * most one store file (the header line opens shard 0 and a shard ends only
 * where the next line would not fit). The manifest binds each shard's index,
 * offset, bytes and SHA-256 and the whole series' bytes and SHA-256; the
 * binding validator's ADAPTER_SERIES_* documents the format. */
#define TP_RETIREMENT_COMPOSE_SERIES_SHARD_PREFIX "retirement-statistics-series-"
#define TP_RETIREMENT_COMPOSE_SERIES_SHARD_SUFFIX ".txt"
#define TP_RETIREMENT_COMPOSE_SERIES_SHARD_BYTES TP_RETIREMENT_STORE_FILE_BYTES
#define TP_RETIREMENT_COMPOSE_SERIES_SHARDS 1024u
#define TP_RETIREMENT_COMPOSE_SERIES_MANIFEST_HEADER "BQ-RETIREMENT-STATISTICS-SERIES-V1\n"
/* Composer outputs besides the result-input manifests and the series shards:
 * code records, series manifest, replay output, bundle, execution receipt,
 * retained manifest and the sealed-result record. */
#define TP_RETIREMENT_COMPOSE_FIXED_OUTPUTS 7u
/* Retained declaration: entries, kind length and a group's index width
 * (`<prefix>NNNN<suffix>`, contiguous from 0000). */
#define TP_RETIREMENT_COMPOSE_RETAINED_ENTRIES 64u
#define TP_RETIREMENT_COMPOSE_KIND_BYTES 16u
#define TP_RETIREMENT_COMPOSE_GROUP_FILES 10000u
/* The adapter's default wall-clock limit when the request names none. */
#define TP_RETIREMENT_COMPOSE_ADAPTER_TIMEOUT_NS (UINT64_C(3600) * 1000000000u)

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
 * untimed_groups is the untimed code-artifact group count. An object group's
 * members are its rows in ascending census order and are the first metrics
 * inputs of each of its batches (D's TpRetirementBatchInput order). */
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
 * projections): the validator's _sealed_closure_files name and descriptor.
 * Its path is below the store root and is not a store file. */
typedef struct TpRetirementComposeClosure
{
    char const* name;
    char const* path;
    uint64_t bytes;
    char const* sha256;
} TpRetirementComposeClosure;

/* One retained (unsealed) store file kind, declared before timing. With a
 * NULL suffix, `prefix` is one exact path that must exist at composition;
 * otherwise the group holds `<prefix>NNNN<suffix>` files numbered
 * contiguously from 0000, at most files_max of them. reserved is 1 when the
 * campaign capacity already reserves the files (A/A transcript and sample
 * shards are exact; A/A metrics shards are within its metrics-shard bound);
 * otherwise files_max files of bytes_max total are reserved here. kind is
 * [a-z]{1,16}. */
typedef struct TpRetirementComposeRetained
{
    char const* kind;
    char const* prefix;
    char const* suffix;
    unsigned files_max, reserved;
    uint64_t bytes_max;
} TpRetirementComposeRetained;

/* Everything the store holds besides the composer's inputs and outputs, and
 * the prior closure it re-reads: bound by digest into the store at plan
 * time and required unchanged at composition. */
typedef struct TpRetirementComposeDeclaration
{
    TpRetirementComposeRetained const* retained;
    unsigned retained_count, prior_entries;
    uint64_t prior_bytes;
} TpRetirementComposeDeclaration;

/* Sizes known before timing that bound the composer outputs. */
typedef struct TpRetirementComposeShape
{
    TpRetirementComposeLayout const* layout;
    unsigned pairs, code_rows, prior_entries;
} TpRetirementComposeShape;

/* series bounds the whole series stream and series_shards its shard count
 * (both upper bounds before timing); series_manifest bounds the manifest.
 * files counts every composer output, series shards included. */
typedef struct TpRetirementComposeBounds
{
    uint64_t manifests, code, series, series_manifest, replay, bundle, receipt, retained, seal, total;
    unsigned manifest_count, files, members, bootstrap_members, cell_members, series_shards;
} TpRetirementComposeBounds;

typedef struct TpRetirementComposeRequest
{
    /* The prior closure and the binding are re-read below its root. */
    TpRetirementStore* store;
    /* Private scratch directory for the adapter's input copies (the series
     * manifest and shards) and its output. */
    int scratch_root;
    /* The reviewed adapter executable and its authenticated digest; it is
     * executed from the descriptor that was hashed. 0 selects
     * TP_RETIREMENT_COMPOSE_ADAPTER_TIMEOUT_NS. */
    char const* adapter_path;
    char const* adapter_sha256;
    uint64_t adapter_timeout_ns;
    TpRetirementComposeLayout const* layout;
    TpRetirementPlan const* statistics;
    TpRetirementComposeDeclaration const* declaration;
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
    /* The post-A/A binding document (below the store root, not a store file)
     * and its authenticated digest. The composer derives the validator's
     * _execution_context from it with the streamed numeric digest, so the
     * receipt's context is post-sample and never caller-supplied. */
    char const* binding_path;
    char const* binding_sha256;
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
    /* The A/A stage: its transcript shards (declared retained files, in
     * order) and its metrics writer tag. Every retained
     * `retirement-metrics-<tag>-NNNN.txt` shard must be tiled, in order and
     * completely, by those transcripts' metrics artifacts, as the A/B
     * shards are by the A/B transcript. A NULL tag means the A/A stage wrote
     * no metrics. */
    char const* const* aa_transcript_paths;
    unsigned aa_transcript_count;
    char const* aa_metrics_tag;
    TpRetirementComposeCode const* code;
    unsigned code_count;
    TpRetirementComposeClosure const* prior;
    unsigned prior_count;
    /* The rest of the binding's pre-replay closure (the validator's
     * _sealed_closure_files): files below the store root that are neither
     * store files nor declared prior entries but external entries of the
     * store plan, such as the evidence the binding names and its A/A
     * admission receipt. Each is rehashed and sealed under its name like a
     * prior entry; none may name a workflow phase or repeat a store path or
     * a prior or earlier closure name or path. At most
     * TP_RETIREMENT_COMPOSE_CLOSURE_ENTRIES; may be empty. */
    TpRetirementComposeClosure const* closure;
    /* Where each closure file is stored below the root when that differs
     * from the path it is sealed under (lane F's replay layout moves it
     * back); NULL, or a NULL entry, means the path itself. */
    char const* const* closure_stored;
    unsigned closure_count;
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
    /* series is the adapter input's manifest; series_shards its shards. */
    TpRetirementComposeArtifact receipt, bundle, sealed, series, replay, code, retained;
    unsigned members, seal_entries, retained_files, series_shards;
    uint64_t invocations, untimed_records, untimed_production;
    /* On refusal: the first failing check (a static diagnostic name). */
    char const* refused;
} TpRetirementComposeResult;

/* Derive the family and every composer output bound from pre-timing facts.
 * Rejects a family outside the #619 caps and any output above the store's
 * per-file cap; the adapter input is bounded as series shards of at most one
 * store file each (#1880), plus their manifest. */
int tp_retirement_compose_bounds(TpRetirementComposeShape const* shape, TpRetirementComposeBounds* bounds);
/* Before any timing: reserve both campaign stages (capacity), the composer's
 * outputs (every series shard up to its bound), the declaration's unreserved
 * retained files and, as external entries, the prior closure; plan the
 * store; mark the upper-bounded kinds (metrics shards, retained groups and
 * the series shards beyond the first) as the only slack settle may release;
 * and bind the declaration's digest. */
int tp_retirement_compose_plan(TpRetirementStore* store, TpRetirementCampaignCapacity const* capacity,
    TpRetirementComposeShape const* shape, TpRetirementComposeDeclaration const* declaration,
    unsigned external_entries, uint64_t external_bytes, TpRetirementCampaignStorePlan* plan);
/* After the A/B stage, the untimed batches and every retained file are
 * published into the planned store. On success every composer output is
 * sealed and the store validates at its settled exact inventory. The caller
 * then issues the producer authority with result->context_sha256. */
int tp_retirement_compose(TpRetirementComposeRequest const* request, TpRetirementComposeResult* result);

#endif
#endif
