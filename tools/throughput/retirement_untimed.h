/* Untimed code-artifact batches for #881 (A1). Code-eligible rows outside the
 * native-host timed projection are compiled outside timing, in batches of the
 * timed form (one target, configuration and frozen argv per object batch,
 * multi-input `-c` through the response file, frozen order; one singleton per
 * non-object stage row). Per untimed group and variant the runner launches at
 * most one production batch (its objects are the frozen artifacts) and exactly
 * one reproduction batch (a further independent compile that must reproduce
 * them byte for byte), writing one sealed record per batch: executable,
 * supervisor-bound process instance, PID, start token, interval, output
 * digest and the metrics artifact inside the untimed metrics shards. These
 * records are the ones `_check_untimed_batches` in the binding validator
 * authenticates. Batches run inside the job reservation but outside the timed
 * collection window, serially, in (group, variant, purpose) order.
 *
 * Map: TpRetirementUntimedBatch, TpRetirementUntimed, tp_retirement_untimed_init,
 * tp_retirement_untimed_record, tp_retirement_untimed_run,
 * tp_retirement_untimed_finish.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_UNTIMED_H
#define BUSTER_THROUGHPUT_RETIREMENT_UNTIMED_H
#include "retirement_measurement.h"
#include "retirement_budget.h"

#define TP_RETIREMENT_UNTIMED_PRODUCTION 0u
#define TP_RETIREMENT_UNTIMED_REPRODUCTION 1u
/* The untimed metrics shard writer's tag; the timed stages' writers (for
 * example `aa` and `ab`) must use other tags, so no shard name is shared. */
#define TP_RETIREMENT_UNTIMED_METRICS_TAG "untimed"
/* Largest untimed batch record, including LF; pinned by the native test and
 * below the validator's 8,192-byte record line cap. */
#define TP_RETIREMENT_UNTIMED_RECORD_BYTES_MAX 747u
#define TP_RETIREMENT_UNTIMED_LINE_CAP 8192u
BUSTER_CT_CHECK(TP_RETIREMENT_UNTIMED_RECORD_BYTES_MAX < TP_RETIREMENT_UNTIMED_LINE_CAP);

#ifdef __linux__
/* One untimed batch: the frozen command of untimed group `group` (unit ==
 * group, kind 0) for `variant`, and whether it produces or reproduces. */
typedef struct TpRetirementUntimedBatch
{
    TpRetirementMeasuredCommand command;
    unsigned group, variant, purpose, group_kind;
} TpRetirementUntimedBatch;

/* reproduced has two bytes per untimed group (one per variant). The timed
 * window is [timed_bound_at_ns, timed_completed_at_ns]; before collection
 * completes, timed_completed_at_ns is zero and a batch must finish before
 * timed_bound_at_ns. */
typedef struct TpRetirementUntimed
{
    FILE* stream;
    TpRetirementMetricsShards* metrics;
    TpRetirementCampaignBudget const* budget;
    unsigned char* reproduced;
    Sha256 hash;
    uint64_t bytes, records, attempt, reserved_at_ns, timed_bound_at_ns, timed_completed_at_ns, last_end;
    unsigned groups, last_key;
    int cpu, failed, finished;
    char job[129], boot[129];
} TpRetirementUntimed;

static inline int tp_retirement_untimed_init(TpRetirementUntimed* untimed, FILE* stream,
    TpRetirementMetricsShards* metrics, TpRetirementCampaignBudget const* budget, unsigned groups,
    unsigned char* reproduced, char const* job, uint64_t attempt, char const* boot, int cpu,
    uint64_t reserved_at_ns, uint64_t timed_bound_at_ns, uint64_t timed_completed_at_ns)
{
    int ok = untimed && tp_retirement_metrics_stream_empty(stream) && tp_retirement_budget_valid(budget) &&
        groups && groups <= TP_RETIREMENT_MAX_CELLS && reproduced && tp_retirement_token(job) &&
        tp_retirement_token(boot) && attempt && cpu >= 0 && reserved_at_ns &&
        timed_bound_at_ns > reserved_at_ns && (!timed_completed_at_ns || timed_completed_at_ns > timed_bound_at_ns) &&
        (!metrics || (!metrics->failed && !metrics->finished && !metrics->artifacts && metrics->stream &&
                      metrics->stream != stream && !strcmp(metrics->tag, TP_RETIREMENT_UNTIMED_METRICS_TAG)));
    if (untimed)
    {
        *untimed = (TpRetirementUntimed){.failed = !ok};
        if (ok)
        {
            memset(reproduced, 0, (size_t)groups * 2);
            untimed->stream = stream;
            untimed->metrics = metrics;
            untimed->budget = budget;
            untimed->reproduced = reproduced;
            untimed->groups = groups;
            untimed->attempt = attempt;
            untimed->cpu = cpu;
            untimed->reserved_at_ns = reserved_at_ns;
            untimed->timed_bound_at_ns = timed_bound_at_ns;
            untimed->timed_completed_at_ns = timed_completed_at_ns;
            untimed->last_end = reserved_at_ns;
            untimed->last_key = UINT32_MAX;
            strcpy(untimed->job, job);
            strcpy(untimed->boot, boot);
            sha256_init(&untimed->hash);
        }
    }
    return ok;
}

/* Encode one canonical sorted-key record, the validator's
 * UNTIMED_BATCH_FIELDS; the metrics artifact is null for a singleton. */
static inline size_t tp_retirement_untimed_record(char* bytes, size_t capacity, TpRetirementUntimedBatch const* batch,
    TpProcessObservation const* observed, char const* executable_sha256, char const* command_sha256,
    char const* output_sha256, TpRetirementMetricsArtifact const* metrics, char const* job, uint64_t attempt,
    char const* boot)
{
    size_t result = 0;
    char instance[65], start[32], artifact[TP_RETIREMENT_METRICS_PATH_CAP + 160] = "null";
    int ok = bytes && capacity && capacity <= TP_RETIREMENT_UNTIMED_LINE_CAP && batch && observed &&
        observed->valid && observed->pid && observed->start_token && observed->started_ns &&
        observed->finished_ns > observed->started_ns && batch->group < TP_RETIREMENT_MAX_CELLS &&
        batch->variant < 2 && batch->purpose < 2 && batch->command.exit_status >= 0 &&
        batch->command.exit_status <= 255 && tp_retirement_digest(executable_sha256) &&
        tp_retirement_digest(command_sha256) && tp_retirement_digest(output_sha256) &&
        (batch->group_kind == TP_RETIREMENT_GROUP_OBJECT ? tp_retirement_metrics_artifact_valid(metrics) :
         !metrics && !batch->command.exit_status);
    if (ok)
    {
        snprintf(start, sizeof(start), "%" PRIu64, observed->start_token);
        ok = tp_retirement_process_instance(instance, job, attempt, boot, observed->pid, start);
    }
    if (ok && metrics)
    {
        int length = snprintf(artifact, sizeof(artifact),
            "{\"bytes\":%" PRIu64 ",\"offset\":%" PRIu64 ",\"path\":\"%s\",\"sha256\":\"%s\"}",
            metrics->bytes, metrics->offset, metrics->path, metrics->sha256);
        ok = length > 0 && (size_t)length < sizeof(artifact);
    }
    if (ok)
    {
        int count = snprintf(bytes, capacity,
            "{\"command_sha256\":\"%s\",\"executable_sha256\":\"%s\",\"exit_status\":%d,"
            "\"finished_ns\":%" PRIu64 ",\"group\":%u,\"metrics_artifact\":%s,\"output_sha256\":\"%s\","
            "\"pid\":%" PRIu64 ",\"process_instance_sha256\":\"%s\",\"process_start_token\":\"%s\","
            "\"purpose\":\"%s\",\"started_ns\":%" PRIu64 ",\"variant\":\"%s\"}\n",
            command_sha256, executable_sha256, batch->command.exit_status, observed->finished_ns, batch->group,
            artifact, output_sha256, observed->pid, instance, start,
            batch->purpose ? "reproduction" : "production", observed->started_ns,
            batch->variant ? "candidate" : "baseline");
        if (count > 0 && (size_t)count < capacity) result = (size_t)count;
    }
    if (!result && bytes && capacity) bytes[0] = 0;
    return result;
}

/* Launch the next untimed batch and append its record. Records are strictly
 * ordered by (group, variant, purpose), which also makes a production batch
 * precede its reproduction and a reproduction distinct from it; an object
 * batch's contract must carry the budget's reviewed metrics bound. A failure
 * poisons the runner: there is no retry. */
static inline int tp_retirement_untimed_run(TpRetirementUntimed* untimed, TpRetirementUntimedBatch const* batch,
    TpRetirementExecutable const* executable, TpProcessInputs const* inputs, int output_directory,
    TpRetirementMeasurementResult* result)
{
    TpRetirementMeasurementResult outcome = {.status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
        .process = {.exit_code = -1}};
    unsigned key = batch ? (batch->group * 2 + batch->variant) * 2 + batch->purpose : 0;
    TpRetirementBatchContract const* contract = batch ? batch->command.batch : NULL;
    uint64_t bound = 0;
    int object = batch && batch->group_kind == TP_RETIREMENT_GROUP_OBJECT;
    int ok = untimed && !untimed->failed && !untimed->finished && batch && result && executable &&
        batch->group < untimed->groups && batch->variant < 2 && batch->purpose < 2 &&
        (untimed->last_key == UINT32_MAX || key > untimed->last_key) &&
        batch->command.unit == batch->group && !batch->command.kind && batch->command.variant == batch->variant &&
        (object ? contract && untimed->metrics &&
            tp_retirement_budget_metrics_bytes(untimed->budget, contract->input_count, &bound) &&
            contract->metrics_bytes_max == bound :
         batch->group_kind == TP_RETIREMENT_GROUP_SINGLETON && !contract);
    TpRetirementMemberSample members[TP_RETIREMENT_BATCH_INPUTS];
    unsigned member_count = 0;
    for (unsigned i = 0; ok && object && i < contract->input_count; ++i) member_count += contract->inputs[i].member;
    char command_digest[65];
    TpRetirementLaunch launch = {batch ? &batch->command : NULL, executable, inputs,
        untimed ? untimed->metrics : NULL, output_directory, untimed ? untimed->cpu : -1,
        batch ? batch->group_kind : TP_RETIREMENT_GROUP_SINGLETON};
    if (ok) ok = tp_retirement_launch(&launch, members, member_count, &outcome, command_digest);
    char line[TP_RETIREMENT_UNTIMED_LINE_CAP];
    size_t count = 0;
    if (ok)
    {
        TpProcessObservation const* observed = &outcome.observed;
        outcome.status = TP_RETIREMENT_MEASUREMENT_COLLECTION_FAILED;
        ok = observed->started_ns > untimed->last_end && observed->started_ns > untimed->reserved_at_ns &&
            (observed->finished_ns < untimed->timed_bound_at_ns ||
             (untimed->timed_completed_at_ns && observed->started_ns > untimed->timed_completed_at_ns));
        count = ok ? tp_retirement_untimed_record(line, sizeof(line), batch, observed, executable->sha256,
            command_digest, outcome.output_sha256, object ? &outcome.metrics : NULL, untimed->job,
            untimed->attempt, untimed->boot) : 0;
        ok = ok && count && count <= TP_RETIREMENT_UNTIMED_RECORD_BYTES_MAX &&
            fwrite(line, 1, count, untimed->stream) == count && !ferror(untimed->stream);
    }
    if (ok)
    {
        sha256_add(&untimed->hash, line, (u64)count);
        untimed->bytes += count;
        ++untimed->records;
        untimed->last_end = outcome.observed.finished_ns;
        untimed->last_key = key;
        if (batch->purpose == TP_RETIREMENT_UNTIMED_REPRODUCTION)
            untimed->reproduced[batch->group * 2 + batch->variant] = 1;
        outcome.status = TP_RETIREMENT_MEASUREMENT_COMPLETE;
    }
    else if (untimed)
    {
        untimed->failed = 1;
        if (untimed->metrics) untimed->metrics->failed = 1;
    }
    if (result) *result = outcome;
    return ok;
}

/* Every untimed group needs its reproduction batch for both variants. The
 * record descriptor's `records` is the batch count; the metrics shard writer
 * is finished separately by its owner. */
static inline int tp_retirement_untimed_finish(TpRetirementUntimed* untimed, TpRetirementShard* records)
{
    int ok = untimed && !untimed->failed && !untimed->finished && records && untimed->records &&
        fflush(untimed->stream) == 0 && !ferror(untimed->stream);
    for (unsigned i = 0; ok && i < untimed->groups * 2; ++i) ok = untimed->reproduced[i] == 1;
    if (records) *records = (TpRetirementShard){0};
    if (ok)
    {
        records->bytes = untimed->bytes;
        records->records = untimed->records;
        sha256_finish_hex(&untimed->hash, records->sha256);
        untimed->finished = 1;
    }
    else if (untimed) untimed->failed = 1;
    return ok;
}
#endif
#endif
