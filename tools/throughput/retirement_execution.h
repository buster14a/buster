/* Native producer side of #568's execution transcript for #881 (A1).
 * Compilation is one serial campaign over the G batch groups of the native-host
 * timed projection (dense group ordinals, ascending smallest member row); native
 * runtime is a second campaign over the runtime-eligible timed rows (census
 * IDs). The cursor uses the reviewed #619 block schedule and retains only O(G)
 * state. peek/commit makes a failed or unwritten invocation non-resumable.
 * tp_retirement_transcript_append couples checked bytes and cursor advancement;
 * tp_retirement_transcript_finish checks complete collection and flushed output.
 * (M4) TpRetirementMetricsShards packs per-batch metrics artifacts into
 * greedy 64 MiB metrics shards; each transcript record names its artifact's
 * shard, offset, length and SHA-256.
 * These are collection primitives, not service admission or a verdict. The
 * supervisor must own the plan, process launcher, output and receipt authority.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_EXECUTION_H
#define BUSTER_THROUGHPUT_RETIREMENT_EXECUTION_H
#include "retirement_stats.h"
#include "retirement_metrics.h"
#include "platform.h"
#include <buster/lib/hash.h>
#include <inttypes.h>

#define TP_RETIREMENT_WARMUPS 2u
/* The 254-pair collection maximum, equal to the validator's
 * SAMPLING_MAX_PAIRS. A1's smaller native population would fit the record
 * ceiling at 256, so this cap, not that ceiling, keeps the maximum. */
#define TP_RETIREMENT_EXECUTION_MAX_PAIRS 254u
#define TP_RETIREMENT_EXECUTION_LINE_CAP 8192u
/* An identity field that does not apply to an invocation kind (JSON null). */
#define TP_RETIREMENT_NONE 0xffffffffu
/* A metrics shard leaf: `retirement-metrics-<tag>-<NNNN>.txt`, where the tag
 * is 1-8 lowercase letters naming its writer (for example `aa`, `ab` or
 * `untimed`) and NNNN is the shard index. At most 36 bytes plus NUL. */
#define TP_RETIREMENT_METRICS_PATH_CAP 56u
#define TP_RETIREMENT_METRICS_TAG_BYTES 8u
/* One metrics shard is one store entry: at most the store's 64 MiB file cap
 * (TP_RETIREMENT_STORE_FILE_BYTES), and so at most one artifact at the cap. */
#define TP_RETIREMENT_METRICS_SHARD_BYTES UINT64_C(67108864)
#define TP_RETIREMENT_METRICS_SHARDS 2048u
BUSTER_CT_CHECK(TP_RETIREMENT_METRICS_ARTIFACT_BYTES <= TP_RETIREMENT_METRICS_SHARD_BYTES);

/* A compiler invocation is one batch process of group `group`; a runtime
 * invocation is one process of timed row `row`. The other field is NONE.
 * `dense` is the cell index within that invocation's campaign. */
typedef struct TpRetirementInvocation
{
    uint64_t sequence;
    unsigned group, row, dense, kind, phase, variant;
    int round, pair, warmup, position;
} TpRetirementInvocation;

typedef struct TpRetirementExecution
{
    uint64_t seed, sequence, expected;
    unsigned groups, runtime_count, population_rows, pairs;
    unsigned* runtime_rows;
    unsigned* first_orders;
    unsigned* first_cells;
    unsigned* second_cells;
    unsigned kind, phase, cell, repeat, position, round, block, pair_in_block;
    int failed, pending, scheduled;
    TpRetirementInvocation current;
} TpRetirementExecution;

typedef enum TpRetirementNext
{
    TP_RETIREMENT_NEXT_INVALID, TP_RETIREMENT_NEXT_READY, TP_RETIREMENT_NEXT_DONE
} TpRetirementNext;

/* groups is the frozen batch-group count G of the timed projection.
 * runtime_rows are the runtime-eligible timed rows' census IDs, ascending and
 * below population_rows. Each is its own singleton group, so there are at most
 * G. The list is copied: later mutation of producer input cannot change a
 * frozen schedule. workspace holds exactly 3*G + U slots. */
static int tp_retirement_execution_init(TpRetirementExecution* state, uint64_t seed,
                                        unsigned groups, unsigned const* runtime_rows,
                                        unsigned runtime_count, unsigned population_rows,
                                        unsigned pairs, unsigned* workspace, size_t workspace_count)
{
    int ok = state && seed && groups && groups <= TP_RETIREMENT_MAX_CELLS &&
             population_rows >= groups && population_rows <= TP_RETIREMENT_MAX_CELLS &&
             runtime_count <= groups && (!runtime_count || runtime_rows) &&
             pairs >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND &&
             pairs <= TP_RETIREMENT_EXECUTION_MAX_PAIRS && !(pairs & 1) &&
             workspace && workspace_count == (size_t)groups * 3 + runtime_count;
    for (unsigned i = 0; ok && i < runtime_count; ++i)
        ok = runtime_rows[i] < population_rows && (!i || runtime_rows[i] > runtime_rows[i - 1]);
    if (state)
    {
        *state = (TpRetirementExecution){.failed = !ok};
        if (ok)
        {
            state->seed = seed;
            state->groups = groups;
            state->population_rows = population_rows;
            state->runtime_rows = workspace + (size_t)groups * 3;
            if (runtime_count) memmove(state->runtime_rows, runtime_rows, sizeof(*runtime_rows) * runtime_count);
            state->runtime_count = runtime_count;
            state->pairs = pairs;
            state->first_orders = workspace;
            state->first_cells = workspace + groups;
            state->second_cells = workspace + (size_t)groups * 2;
            state->expected = (uint64_t)(groups + runtime_count) * 2 *
                              (TP_RETIREMENT_WARMUPS + TP_RETIREMENT_ROUNDS * pairs);
        }
    }
    return ok;
}

static TpRetirementNext tp_retirement_execution_peek(TpRetirementExecution* state,
                                                    TpRetirementInvocation* invocation)
{
    TpRetirementNext result = TP_RETIREMENT_NEXT_INVALID;
    if (state && invocation && !state->failed && state->groups)
    {
        unsigned count = state->kind ? state->runtime_count : state->groups;
        if (state->sequence == state->expected)
            result = TP_RETIREMENT_NEXT_DONE;
        else if (state->pending)
            result = TP_RETIREMENT_NEXT_READY;
        else if (count && state->kind < 2)
        {
            TpRetirementInvocation next = {.sequence = state->sequence, .kind = state->kind,
                .phase = state->phase, .round = -1, .pair = -1, .warmup = -1, .position = -1};
            unsigned dense = state->cell;
            int ok = 1;
            if (!state->phase)
            {
                next.warmup = (int)state->repeat;
                next.variant = state->position;
            }
            else
            {
                if (!state->scheduled)
                {
                    ok = tp_retirement_block_schedule(state->seed, state->round, state->block, count,
                        state->first_orders, state->first_cells, state->second_cells, count);
                    state->scheduled = ok;
                }
                if (ok)
                {
                    dense = (state->pair_in_block ? state->second_cells : state->first_cells)[state->cell];
                    next.variant = state->first_orders[dense] ^ state->pair_in_block ^ state->position;
                    next.round = (int)state->round;
                    next.pair = (int)(state->block * 2 + state->pair_in_block);
                    next.position = (int)state->position;
                }
            }
            if (ok)
            {
                next.dense = dense;
                next.group = state->kind ? TP_RETIREMENT_NONE : dense;
                next.row = state->kind ? state->runtime_rows[dense] : TP_RETIREMENT_NONE;
                state->current = next;
                state->pending = 1;
                result = TP_RETIREMENT_NEXT_READY;
            }
            else state->failed = 1;
        }
        else state->failed = 1;
        if (result == TP_RETIREMENT_NEXT_READY) *invocation = state->current;
    }
    return result;
}

/* A failed launch, failed oracle, failed write or cancelled invocation poisons
 * this attempt. There is deliberately no skip, rewind, import or resume API. */
static int tp_retirement_execution_commit(TpRetirementExecution* state, int complete)
{
    int ok = state && !state->failed && state->pending && complete;
    if (ok)
    {
        unsigned count = state->kind ? state->runtime_count : state->groups;
        state->pending = 0;
        ++state->sequence;
        if (++state->position == 2)
        {
            state->position = 0;
            if (!state->phase)
            {
                if (++state->repeat == TP_RETIREMENT_WARMUPS)
                {
                    state->repeat = 0;
                    if (++state->cell == count) { state->cell = 0; state->phase = 1; }
                }
            }
            else if (++state->cell == count)
            {
                state->cell = 0;
                if (++state->pair_in_block == 2)
                {
                    state->pair_in_block = 0;
                    state->scheduled = 0;
                    if (++state->block == state->pairs / 2)
                    {
                        state->block = 0;
                        if (++state->round == TP_RETIREMENT_ROUNDS)
                        {
                            state->round = 0;
                            state->phase = 0;
                            ++state->kind;
                        }
                    }
                }
            }
        }
    }
    if (state && !ok) state->failed = 1;
    return ok;
}

static int tp_retirement_execution_complete(TpRetirementExecution const* state)
{
    int result = state && state->groups && !state->failed && !state->pending &&
                 state->expected && state->sequence == state->expected;
    return result;
}

static int tp_retirement_token(char const* text)
{
    size_t count = text ? strlen(text) : 0;
    int ok = count > 0 && count <= 128;
    for (size_t i = 0; ok && i < count; ++i)
    {
        unsigned char c = (unsigned char)text[i];
        int alphanumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        ok = alphanumeric || (i && (c == '_' || c == '.' || c == ':' || c == '-'));
    }
    return ok;
}

static int tp_retirement_process_instance(char output[65], char const* job, uint64_t attempt,
                                         char const* boot, uint64_t pid, char const* start_token)
{
    char bytes[640];
    int ok = output && attempt && pid && tp_retirement_token(job) &&
             tp_retirement_token(boot) && tp_retirement_token(start_token);
    int length = ok ? snprintf(bytes, sizeof(bytes),
        "{\"attempt\":%" PRIu64 ",\"boot_id\":\"%s\",\"job_id\":\"%s\",\"pid\":%" PRIu64
        ",\"process_start_token\":\"%s\"}", attempt, boot, job, pid, start_token) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(bytes);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, bytes, (u64)length);
        sha256_finish_hex(&hash, output);
    }
    else if (output) output[0] = 0;
    return ok;
}

/* Render an exact nanosecond interval in Python's canonical JSON number form.
 * Limiting one invocation to a day keeps at most 14 significant decimal digits;
 * every such decimal round-trips without changing its shortest representation.
 * Integer seconds remain JSON integers. Tiny intervals use the same exponent
 * spelling as json.dumps. No locale-dependent floating-point formatting runs
 * in collection. The service's per-invocation deadline is stricter than this
 * representation bound; this is not an execution-policy override. */
static int tp_retirement_seconds(char output[32], uint64_t nanoseconds)
{
    int ok = output && nanoseconds && nanoseconds <= UINT64_C(86400000000000);
    if (ok)
    {
        if (nanoseconds < 100000)
        {
            char digits[16];
            int count = snprintf(digits, sizeof(digits), "%" PRIu64, nanoseconds);
            int exponent = 10 - count;
            while (count > 1 && digits[count - 1] == '0') digits[--count] = 0;
            if (count == 1) snprintf(output, 32, "%ce-0%d", digits[0], exponent);
            else snprintf(output, 32, "%c.%se-0%d", digits[0], digits + 1, exponent);
        }
        else
        {
            uint64_t whole = nanoseconds / 1000000000;
            unsigned fraction = (unsigned)(nanoseconds % 1000000000);
            if (!fraction) snprintf(output, 32, "%" PRIu64, whole);
            else
            {
                int count = snprintf(output, 32, "%" PRIu64 ".%09u", whole, fraction);
                while (count > 0 && output[count - 1] == '0') output[--count] = 0;
            }
        }
    }
    else if (output) output[0] = 0;
    return ok;
}

static int tp_retirement_digest(char const* text)
{
    int ok = text && strlen(text) == 64;
    for (unsigned i = 0; ok && i < 64; ++i)
        ok = (text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f');
    return ok;
}

/* A metrics shard leaf names its writer tag and shard index; the validator
 * applies the same pattern. */
static int tp_retirement_metrics_shard_path(char output[TP_RETIREMENT_METRICS_PATH_CAP], char const* tag,
    unsigned index)
{
    size_t length = 0;
    while (tag && length <= TP_RETIREMENT_METRICS_TAG_BYTES && tag[length]) ++length;
    int ok = output && length && length <= TP_RETIREMENT_METRICS_TAG_BYTES && index < TP_RETIREMENT_METRICS_SHARDS;
    for (size_t i = 0; ok && i < length; ++i) ok = tag[i] >= 'a' && tag[i] <= 'z';
    int written = ok ? snprintf(output, TP_RETIREMENT_METRICS_PATH_CAP, "retirement-metrics-%s-%04u.txt", tag,
                                index) : -1;
    ok = ok && written > 0 && (unsigned)written < TP_RETIREMENT_METRICS_PATH_CAP;
    if (!ok && output) output[0] = 0;
    return ok;
}

static int tp_retirement_metrics_shard_leaf(char const* path)
{
    size_t length = 0;
    while (path && length < TP_RETIREMENT_METRICS_PATH_CAP && path[length]) ++length;
    int ok = length > 28 && length < TP_RETIREMENT_METRICS_PATH_CAP && !memcmp(path, "retirement-metrics-", 19) &&
        !strcmp(path + length - 4, ".txt") && path[length - 9] == '-';
    size_t tag = ok ? length - 9 - 19 : 0;
    ok = ok && tag >= 1 && tag <= TP_RETIREMENT_METRICS_TAG_BYTES;
    for (size_t i = 0; ok && i < tag; ++i) ok = path[19 + i] >= 'a' && path[19 + i] <= 'z';
    unsigned index = 0;
    for (size_t i = length - 8; ok && i < length - 4; ++i)
    {
        ok = path[i] >= '0' && path[i] <= '9';
        index = index * 10 + (unsigned)(path[i] - '0');
    }
    return ok && index < TP_RETIREMENT_METRICS_SHARDS;
}

/* One batch's per-input metrics artifact: a byte range of a metrics shard.
 * Each artifact stays individually addressable and authenticated by its own
 * shard, offset, length and SHA-256; the validator streams exactly that range. */
typedef struct TpRetirementMetricsArtifact
{
    char path[TP_RETIREMENT_METRICS_PATH_CAP];
    uint64_t offset, bytes;
    char sha256[65];
} TpRetirementMetricsArtifact;

static int tp_retirement_metrics_artifact_valid(TpRetirementMetricsArtifact const* artifact)
{
    int ok = artifact && tp_retirement_metrics_shard_leaf(artifact->path) && tp_retirement_digest(artifact->sha256) &&
        artifact->bytes && artifact->bytes <= TP_RETIREMENT_METRICS_ARTIFACT_BYTES &&
        artifact->offset <= TP_RETIREMENT_METRICS_SHARD_BYTES - artifact->bytes;
    return ok;
}

/* (A1) Code bytes are measured once from frozen artifacts, never per
 * invocation. A compiler invocation binds the batch output digest over its
 * per-input object digests and, for an object batch, its metrics artifact;
 * exit_status is the frozen expected exit status (nonzero only when a frozen
 * control fails). */
typedef struct TpRetirementOutput
{
    char const* executable_sha256;
    char const* command_sha256;
    char const* output_sha256;
    TpRetirementMetricsArtifact const* metrics;
    int exit_status;
} TpRetirementOutput;

/* Only the supervisor can supply authenticated plan/output identities. This
 * encoder provides bounded bytes, ordering and failure handling, not authority.
 * It writes the A1 #568 invocation schema and adds no result schema. */
static size_t tp_retirement_execution_record(char* bytes, size_t capacity,
    TpRetirementInvocation const* invocation, TpProcessObservation const* observed,
    TpProcess const* process, TpRetirementOutput const* output,
    char const* job, uint64_t attempt, char const* boot, int cpu)
{
    size_t result = 0;
    char instance[65], start[32], seconds[32];
    int ok = bytes && capacity && capacity <= TP_RETIREMENT_EXECUTION_LINE_CAP &&
        invocation && invocation->kind < 2 && invocation->phase < 2 && invocation->variant < 2 &&
        (invocation->kind ? invocation->group == TP_RETIREMENT_NONE && invocation->row < TP_RETIREMENT_MAX_CELLS :
                            invocation->row == TP_RETIREMENT_NONE && invocation->group < TP_RETIREMENT_MAX_CELLS) &&
        observed && observed->valid && observed->pid && observed->start_token &&
        observed->started_ns && observed->finished_ns > observed->started_ns &&
        process && output && output->exit_status >= 0 && output->exit_status <= 255 &&
        (!invocation->kind || !output->exit_status) &&
        !process->launch_error && process->exit_code == output->exit_status && !process->signal_number &&
        !process->timed_out && isfinite(process->wall_seconds) && process->wall_seconds > 0 &&
        cpu >= 0 && tp_retirement_digest(output->executable_sha256) &&
        tp_retirement_digest(output->command_sha256) && tp_retirement_digest(output->output_sha256);
    if (ok)
    {
        ok = invocation->phase ? invocation->round >= 0 && (unsigned)invocation->round < TP_RETIREMENT_ROUNDS &&
            invocation->pair >= 0 && (unsigned)invocation->pair < TP_RETIREMENT_EXECUTION_MAX_PAIRS &&
            invocation->warmup == -1 && invocation->position >= 0 && invocation->position < 2 :
            invocation->round == -1 && invocation->pair == -1 && invocation->position == -1 &&
            invocation->warmup >= 0 && (unsigned)invocation->warmup < TP_RETIREMENT_WARMUPS;
        ok = ok && (!output->metrics || (!invocation->kind && tp_retirement_metrics_artifact_valid(output->metrics)));
        ok = ok && (invocation->kind || (isfinite(process->peak_rss_bytes) &&
            process->peak_rss_bytes > 0 && process->peak_rss_bytes <= 9007199254740991.0 &&
            floor(process->peak_rss_bytes) == process->peak_rss_bytes));
    }
    if (ok)
    {
        uint64_t elapsed = observed->finished_ns - observed->started_ns;
        snprintf(start, sizeof(start), "%" PRIu64, observed->start_token);
        ok = tp_retirement_process_instance(instance, job, attempt, boot, observed->pid, start) &&
            tp_retirement_seconds(seconds, elapsed) &&
            fabs(process->wall_seconds * 1000000000.0 - (double)elapsed) <= 1.0;
    }
    char metrics[TP_RETIREMENT_METRICS_PATH_CAP + 160] = "null";
    if (ok && output->metrics)
    {
        TpRetirementMetricsArtifact const* artifact = output->metrics;
        int length = snprintf(metrics, sizeof(metrics),
            "{\"bytes\":%" PRIu64 ",\"offset\":%" PRIu64 ",\"path\":\"%s\",\"sha256\":\"%s\"}",
            artifact->bytes, artifact->offset, artifact->path, artifact->sha256);
        ok = length > 0 && (size_t)length < sizeof(metrics);
    }
    if (ok)
    {
        char rss[32] = "null", group[16] = "null", row[16] = "null";
        char round[16] = "null", pair[16] = "null", warmup[16] = "null", position[16] = "null";
        if (!invocation->kind)
        {
            snprintf(rss, sizeof(rss), "%" PRIu64, (uint64_t)process->peak_rss_bytes);
            snprintf(group, sizeof(group), "%u", invocation->group);
        }
        else snprintf(row, sizeof(row), "%u", invocation->row);
        if (invocation->phase)
        {
            snprintf(round, sizeof(round), "%d", invocation->round);
            snprintf(pair, sizeof(pair), "%d", invocation->pair);
            snprintf(position, sizeof(position), "%d", invocation->position);
        }
        else snprintf(warmup, sizeof(warmup), "%d", invocation->warmup);
        int count = snprintf(bytes, capacity,
            "{\"cancelled\":false,\"code_section_bytes\":null,\"code_section_sha256\":null,"
            "\"command_sha256\":\"%s\",\"cpu\":%d,\"executable_sha256\":\"%s\",\"exit_code\":%d,"
            "\"finished_ns\":%" PRIu64 ",\"group\":%s,\"kind\":\"%s\",\"metrics_artifact\":%s,"
            "\"output_sha256\":\"%s\",\"pair\":%s,"
            "\"peak_rss_bytes\":%s,\"phase\":\"%s\",\"pid\":%" PRIu64 ",\"position\":%s,"
            "\"process_instance_sha256\":\"%s\",\"process_start_token\":\"%s\",\"round\":%s,"
            "\"row\":%s,\"sequence\":%" PRIu64 ",\"signal\":0,\"started_ns\":%" PRIu64 ","
            "\"timed_out\":false,\"variant\":\"%s\",\"wall_seconds\":%s,\"warmup\":%s}\n",
            output->command_sha256, cpu, output->executable_sha256, output->exit_status,
            observed->finished_ns, group, invocation->kind ? "runtime" : "compiler", metrics,
            output->output_sha256, pair, rss, invocation->phase ? "sample" : "warmup", observed->pid,
            position, instance, start, round, row, invocation->sequence, observed->started_ns,
            invocation->variant ? "candidate" : "baseline", seconds, warmup);
        if (count > 0 && (size_t)count < capacity) result = (size_t)count;
    }
    if (!result && bytes && capacity) bytes[0] = 0;
    return result;
}

/* Largest line tp_retirement_execution_record can emit for any legal
 * schedule/token/digest domain, including LF. tests.c pins the maximal
 * object-batch warmup (1018 bytes) and sampled record (1017 bytes), each with
 * an 8-digit metrics offset and length at the longest shard leaf; the proof
 * assumes at most nine sequence digits, which the total record cap below
 * preserves. Append rejects a wider line rather than trusting the proof. */
#define TP_RETIREMENT_TRANSCRIPT_RECORD_BYTES_MAX 1018u
#define TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS 65536u
#define TP_RETIREMENT_TRANSCRIPT_SHARDS 2048u
#define TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES UINT64_C(67108864)
/* 2048 * 65536 == 134217728 records: sequences stay below 2^27. */
BUSTER_CT_CHECK((uint64_t)TP_RETIREMENT_TRANSCRIPT_SHARDS * TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS <=
                UINT64_C(134217728));
BUSTER_CT_CHECK(TP_RETIREMENT_TRANSCRIPT_RECORD_BYTES_MAX < TP_RETIREMENT_EXECUTION_LINE_CAP);
BUSTER_CT_CHECK((uint64_t)TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS * TP_RETIREMENT_TRANSCRIPT_RECORD_BYTES_MAX <=
                TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES);
#define TP_RETIREMENT_RECEIPT_BYTES UINT64_C(1048576)
#define TP_RETIREMENT_RECEIPT_PATH_BYTES 192u

typedef struct TpRetirementShard
{
    uint64_t bytes, records;
    char sha256[65];
} TpRetirementShard;

typedef struct TpRetirementShardFile
{
    char const* path;
    TpRetirementShard contents;
} TpRetirementShardFile;

typedef struct TpRetirementTranscript
{
    TpRetirementExecution* execution;
    FILE* stream;
    Sha256 hash;
    uint64_t bytes, records, total_records, last_end, attempt, bound_at_ns, completed_at_ns;
    unsigned shards;
    int cpu, failed, finished, receipt_written;
    char job[129], boot[129];
} TpRetirementTranscript;

/* The caller owns exclusive service-created streams and their durable/no-replace
 * publication. A local shard digest is only an integrity descriptor, never the
 * independently authenticated execution receipt. No stream is closed here. */
static inline int tp_retirement_transcript_init(TpRetirementTranscript* transcript,
    TpRetirementExecution* execution, char const* job, uint64_t attempt,
    char const* boot, int cpu, uint64_t bound_at_ns)
{
    int ok = transcript && execution && execution->groups && !execution->failed &&
        !execution->pending && !execution->sequence && execution->expected &&
        execution->expected <= (uint64_t)TP_RETIREMENT_TRANSCRIPT_SHARDS * TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS &&
        tp_retirement_token(job) && tp_retirement_token(boot) && attempt && cpu >= 0 && bound_at_ns;
    if (transcript)
    {
        *transcript = (TpRetirementTranscript){.failed = !ok};
        if (ok)
        {
            transcript->execution = execution;
            transcript->attempt = attempt;
            transcript->cpu = cpu;
            transcript->last_end = bound_at_ns;
            transcript->bound_at_ns = bound_at_ns;
            strcpy(transcript->job, job);
            strcpy(transcript->boot, boot);
        }
    }
    return ok;
}

static int tp_retirement_transcript_begin_shard(TpRetirementTranscript* transcript, FILE* stream)
{
    int ok = transcript && !transcript->failed && !transcript->finished && !transcript->stream &&
        transcript->execution && !transcript->execution->failed &&
        transcript->total_records == transcript->execution->sequence &&
        transcript->total_records < transcript->execution->expected &&
        transcript->shards < TP_RETIREMENT_TRANSCRIPT_SHARDS && stream;
    if (ok)
    {
        transcript->stream = stream;
        transcript->bytes = transcript->records = 0;
        sha256_init(&transcript->hash);
    }
    else if (transcript)
    {
        transcript->failed = 1;
        if (transcript->execution) transcript->execution->failed = 1;
    }
    return ok;
}

static int tp_retirement_transcript_append(TpRetirementTranscript* transcript,
    TpProcessObservation const* observed, TpProcess const* process, TpRetirementOutput const* output)
{
    char bytes[TP_RETIREMENT_EXECUTION_LINE_CAP];
    TpRetirementInvocation invocation;
    int ok = transcript && !transcript->failed && !transcript->finished && transcript->stream &&
        transcript->execution && transcript->total_records == transcript->execution->sequence &&
        transcript->records < TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS && observed &&
        observed->started_ns > transcript->last_end &&
        tp_retirement_execution_peek(transcript->execution, &invocation) == TP_RETIREMENT_NEXT_READY;
    size_t count = ok ? tp_retirement_execution_record(bytes, sizeof(bytes), &invocation, observed,
        process, output, transcript->job, transcript->attempt, transcript->boot, transcript->cpu) : 0;
    ok = ok && count && count <= TP_RETIREMENT_TRANSCRIPT_RECORD_BYTES_MAX &&
        transcript->bytes <= TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES &&
        count <= TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES - transcript->bytes;
    if (ok) ok = fwrite(bytes, 1, count, transcript->stream) == count && !ferror(transcript->stream);
    if (ok)
    {
        sha256_add(&transcript->hash, bytes, (u64)count);
        transcript->bytes += count;
        ++transcript->records;
        ++transcript->total_records;
        transcript->last_end = observed->finished_ns;
        ok = tp_retirement_execution_commit(transcript->execution, 1);
    }
    if (transcript && !ok)
    {
        transcript->failed = 1;
        if (transcript->execution) transcript->execution->failed = 1;
    }
    return ok;
}

static int tp_retirement_transcript_end_shard(TpRetirementTranscript* transcript, TpRetirementShard* shard)
{
    int ok = transcript && !transcript->failed && !transcript->finished && transcript->stream &&
        transcript->records && shard && transcript->execution && !transcript->execution->failed &&
        transcript->total_records == transcript->execution->sequence &&
        (transcript->records == TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS ||
         tp_retirement_execution_complete(transcript->execution));
    if (shard) *shard = (TpRetirementShard){0};
    if (ok) ok = fflush(transcript->stream) == 0 && !ferror(transcript->stream);
    if (ok)
    {
        shard->bytes = transcript->bytes;
        shard->records = transcript->records;
        sha256_finish_hex(&transcript->hash, shard->sha256);
        transcript->stream = NULL;
        ++transcript->shards;
    }
    else if (transcript)
    {
        transcript->failed = 1;
        if (transcript->execution) transcript->execution->failed = 1;
    }
    return ok;
}

static int tp_retirement_transcript_finish(TpRetirementTranscript* transcript, uint64_t completed_at_ns)
{
    int ok = transcript && !transcript->failed && !transcript->finished && !transcript->stream &&
        transcript->shards && completed_at_ns > transcript->last_end &&
        tp_retirement_execution_complete(transcript->execution) &&
        transcript->total_records == transcript->execution->expected;
    if (transcript)
    {
        transcript->finished = ok;
        if (ok) transcript->completed_at_ns = completed_at_ns;
        if (!ok) transcript->failed = 1;
    }
    return ok;
}

/* Per-batch metrics artifacts packed into sharded store entries (the
 * transcript/sample shard pattern). A writer owns one tag (`aa`, `ab`,
 * `untimed`, ...) and appends each accepted artifact at the current shard's
 * end, returning its shard, offset, length and SHA-256. Rotation is greedy and
 * internal: an artifact that does not fit the current shard's remaining bytes
 * starts the next shard on the service-supplied spare stream, so any two
 * consecutive shards together exceed TP_RETIREMENT_METRICS_SHARD_BYTES and a
 * writer of T bytes has at most 2 * ceil(T / cap) shards (the store preflight's
 * entry bound). The completed shard's descriptor must be taken, and a new spare
 * supplied, before the next rotation. The caller owns every stream, fsync,
 * no-replace publication and final revalidation; nothing is closed here. */
typedef struct TpRetirementMetricsShards
{
    FILE* stream;
    FILE* spare;
    Sha256 hash;
    uint64_t bytes, artifacts, shard_artifacts, total_bytes;
    unsigned index;
    int failed, finished, completed_ready;
    TpRetirementShardFile completed;
    char completed_path[TP_RETIREMENT_METRICS_PATH_CAP];
    char path[TP_RETIREMENT_METRICS_PATH_CAP];
    char tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1];
} TpRetirementMetricsShards;

static int tp_retirement_metrics_stream_empty(FILE* stream)
{
    int ok = stream && fseek(stream, 0, SEEK_END) == 0 && ftell(stream) == 0;
    return ok;
}

static inline int tp_retirement_metrics_shards_init(TpRetirementMetricsShards* shards, char const* tag, FILE* stream)
{
    char path[TP_RETIREMENT_METRICS_PATH_CAP];
    int ok = shards && tp_retirement_metrics_shard_path(path, tag, 0) && tp_retirement_metrics_stream_empty(stream);
    if (shards)
    {
        *shards = (TpRetirementMetricsShards){.failed = !ok};
        if (ok)
        {
            shards->stream = stream;
            memcpy(shards->path, path, sizeof(path));
            memcpy(shards->tag, tag, strlen(tag) + 1);
            sha256_init(&shards->hash);
        }
    }
    return ok;
}

/* The next shard's stream; it must be empty and distinct from the current one. */
static inline int tp_retirement_metrics_shards_spare(TpRetirementMetricsShards* shards, FILE* stream)
{
    int ok = shards && !shards->failed && !shards->finished && !shards->spare && shards->stream &&
        stream != shards->stream && tp_retirement_metrics_stream_empty(stream);
    if (ok) shards->spare = stream;
    else if (shards) shards->failed = 1;
    return ok;
}

static int tp_retirement_metrics_shards_close(TpRetirementMetricsShards* shards)
{
    int ok = shards->shard_artifacts && fflush(shards->stream) == 0 && !ferror(shards->stream);
    if (ok)
    {
        memcpy(shards->completed_path, shards->path, sizeof(shards->path));
        shards->completed = (TpRetirementShardFile){shards->completed_path,
            {shards->bytes, shards->shard_artifacts, {0}}};
        sha256_finish_hex(&shards->hash, shards->completed.contents.sha256);
        shards->completed_ready = 1;
    }
    return ok;
}

static inline int tp_retirement_metrics_shards_append(TpRetirementMetricsShards* shards, unsigned char const* data,
    uint64_t size, TpRetirementMetricsArtifact* artifact)
{
    int ok = shards && !shards->failed && !shards->finished && !shards->completed_ready && shards->stream &&
        data && artifact && size && size <= TP_RETIREMENT_METRICS_ARTIFACT_BYTES;
    if (ok && size > TP_RETIREMENT_METRICS_SHARD_BYTES - shards->bytes)
    {
        ok = shards->spare && shards->index + 1 < TP_RETIREMENT_METRICS_SHARDS &&
            tp_retirement_metrics_shards_close(shards) &&
            tp_retirement_metrics_shard_path(shards->path, shards->tag, shards->index + 1);
        if (ok)
        {
            ++shards->index;
            shards->stream = shards->spare;
            shards->spare = NULL;
            shards->bytes = shards->shard_artifacts = 0;
            sha256_init(&shards->hash);
        }
    }
    if (artifact) *artifact = (TpRetirementMetricsArtifact){0};
    if (ok) ok = fwrite(data, 1, (size_t)size, shards->stream) == (size_t)size && !ferror(shards->stream);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, data, (u64)size);
        sha256_finish_hex(&hash, artifact->sha256);
        sha256_add(&shards->hash, data, (u64)size);
        memcpy(artifact->path, shards->path, sizeof(shards->path));
        artifact->offset = shards->bytes;
        artifact->bytes = size;
        shards->bytes += size;
        shards->total_bytes += size;
        ++shards->shard_artifacts;
        ++shards->artifacts;
    }
    if (!ok && shards) shards->failed = 1;
    return ok;
}

/* Take the descriptor of the shard a rotation completed (its path, bytes,
 * artifact count as `records`, and SHA-256). */
static inline int tp_retirement_metrics_shards_take(TpRetirementMetricsShards* shards, TpRetirementShardFile* completed)
{
    int ok = shards && !shards->failed && shards->completed_ready && completed;
    if (completed) *completed = (TpRetirementShardFile){0};
    if (ok)
    {
        *completed = shards->completed;
        shards->completed_ready = 0;
    }
    return ok;
}

/* Close the final shard after the last artifact; any rotated shard must have
 * been taken first. */
static inline int tp_retirement_metrics_shards_finish(TpRetirementMetricsShards* shards, TpRetirementShardFile* last)
{
    int ok = shards && !shards->failed && !shards->finished && !shards->completed_ready && last &&
        tp_retirement_metrics_shards_close(shards) && tp_retirement_metrics_shards_take(shards, last);
    if (ok) shards->finished = 1;
    else if (shards) shards->failed = 1;
    return ok;
}

static int tp_retirement_receipt_path(char const* path)
{
    size_t size = 0;
    if (path)
        while (size <= TP_RETIREMENT_RECEIPT_PATH_BYTES && path[size]) ++size;
    int ok = size && size <= TP_RETIREMENT_RECEIPT_PATH_BYTES && path[0] != '/' && path[size - 1] != '/';
    size_t start = 0;
    for (size_t i = 0; ok && i <= size; ++i)
    {
        if (i == size || path[i] == '/')
        {
            size_t count = i - start;
            ok = count && !(count == 1 && path[start] == '.') &&
                !(count == 2 && path[start] == '.' && path[start + 1] == '.');
            start = i + 1;
        }
        else
        {
            unsigned char c = (unsigned char)path[i];
            ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        }
    }
    return ok;
}

static int tp_retirement_receipt_write(FILE* stream, Sha256* hash, uint64_t* bytes,
                                       char const* data, size_t count)
{
    int ok = stream && hash && bytes && data && count &&
        *bytes <= TP_RETIREMENT_RECEIPT_BYTES && count <= TP_RETIREMENT_RECEIPT_BYTES - *bytes;
    if (ok) ok = fwrite(data, 1, count, stream) == count && !ferror(stream);
    if (ok)
    {
        sha256_add(hash, data, (u64)count);
        *bytes += count;
    }
    return ok;
}

/* The service supplies the frozen plan/context digests and the separately
 * published transcript-shard paths. This only encodes their bounded, canonical
 * receipt bytes. The service must fsync, publish without replacement and
 * authenticate the resulting receipt digest out of band for independent replay.
 * A failed write poisons the complete attempt; partial receipt bytes remain. */
static inline int tp_retirement_transcript_receipt(TpRetirementTranscript* transcript,
    char const* plan_sha256, char const* context_sha256,
    TpRetirementShardFile const* shards, unsigned count, FILE* stream, TpRetirementShard* receipt)
{
    int ok = transcript && transcript->finished && !transcript->failed && !transcript->receipt_written &&
        transcript->execution && tp_retirement_execution_complete(transcript->execution) &&
        tp_retirement_digest(plan_sha256) && tp_retirement_digest(context_sha256) &&
        shards && count == transcript->shards && count && count <= TP_RETIREMENT_TRANSCRIPT_SHARDS &&
        stream && stream != transcript->stream && receipt;
    uint64_t records = 0;
    for (unsigned i = 0; ok && i < count; ++i)
    {
        TpRetirementShardFile const* shard = shards + i;
        ok = tp_retirement_receipt_path(shard->path) &&
            (!i || strcmp(shards[i - 1].path, shard->path) < 0) &&
            tp_retirement_digest(shard->contents.sha256) &&
            shard->contents.records && shard->contents.records <= TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS &&
            (i + 1 == count || shard->contents.records == TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS) &&
            shard->contents.bytes >= shard->contents.records &&
            shard->contents.bytes <= TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES &&
            records <= transcript->total_records &&
            shard->contents.records <= transcript->total_records - records;
        if (ok) records += shard->contents.records;
    }
    if (ok) ok = records == transcript->total_records && fseek(stream, 0, SEEK_END) == 0 && ftell(stream) == 0;
    if (receipt) *receipt = (TpRetirementShard){0};
    Sha256 hash;
    sha256_init(&hash);
    uint64_t written = 0;
    char buffer[768];
    int length = ok ? snprintf(buffer, sizeof(buffer),
        "{\"attempt\":%" PRIu64 ",\"boot_id\":\"%s\",\"bound_at_ns\":%" PRIu64
        ",\"completed_at_ns\":%" PRIu64 ",\"context_sha256\":\"%s\",\"execution_plan_sha256\":\"%s\""
        ",\"invocations\":%" PRIu64 ",\"job_id\":\"%s\",\"schema\":\"buster-native-retirement-execution-receipt-v1\""
        ",\"shards\":[", transcript->attempt, transcript->boot, transcript->bound_at_ns,
        transcript->completed_at_ns, context_sha256, plan_sha256, transcript->total_records, transcript->job) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(buffer) &&
        tp_retirement_receipt_write(stream, &hash, &written, buffer, (size_t)length);
    for (unsigned i = 0; ok && i < count; ++i)
    {
        TpRetirementShardFile const* shard = shards + i;
        length = snprintf(buffer, sizeof(buffer),
            "%s{\"bytes\":%" PRIu64 ",\"path\":\"%s\",\"records\":%" PRIu64 ",\"sha256\":\"%s\"}",
            i ? "," : "", shard->contents.bytes, shard->path,
            shard->contents.records, shard->contents.sha256);
        ok = length > 0 && (size_t)length < sizeof(buffer) &&
            tp_retirement_receipt_write(stream, &hash, &written, buffer, (size_t)length);
    }
    if (ok) ok = tp_retirement_receipt_write(stream, &hash, &written, "],\"version\":1}\n", 15) &&
                 fflush(stream) == 0 && !ferror(stream) && ftell(stream) == (long)written;
    if (ok)
    {
        receipt->bytes = written;
        receipt->records = 1;
        sha256_finish_hex(&hash, receipt->sha256);
        transcript->receipt_written = 1;
    }
    else if (transcript)
    {
        transcript->failed = 1;
        transcript->finished = 0;
        if (transcript->execution) transcript->execution->failed = 1;
    }
    return ok;
}
#endif
