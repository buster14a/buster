/* The retirement context chain (#881 PR 3): what the producer publishes
 * beside its receipt authority so the coordinator can derive, rather than
 * trust, the authority's plan and final context.
 *
 * Ownership: the worker-unit producer writes it (retirement_worker_compose.c,
 * `context-chain-job-<id>-<token>.txt`, mode 0400, in the attempt's
 * retirement-authority/); the coordinator re-formats it from its own facts
 * and the values it carries and requires the stored bytes to be exactly that
 * (retirement_coordinator.c, bq_retirement_coordinator_chain_check), then
 * derives the plan and the pre-sample and post-sample contexts at
 * finalization (bq_retirement_coordinator_derive). Compiled into the service
 * translation unit only.
 *
 * The record, BQ-RETIREMENT-CONTEXT-CHAIN-V2, one `key=value` line each:
 *   job, attempt              the coordinator's job and attempt
 *   preparation, ready        the coordinator's A digest and the ready digest
 *                             the RETIREMENT_READY packet carried
 *   row-plan                  the profile's row-plan-sha256= pin
 *   plan, context             the authority's plan (lane D's execution-plan
 *                             document) and final (post-sample) context
 * and the values the coordinator cannot observe itself, which the
 * derivation binds by digest:
 *   bound-at                  the campaign's bind time (monotonic ns)
 *   pre-sample, post-sample   lane D's pre-sample and post-sample contexts
 *   logs                      the untimed, A/A and A/B launch-log chains
 *   stage-0, stage-1          each stage's facts
 *                             (BqRetirementUnitCampaignStageFacts: transcript
 *                             shard chain, invocations, completion time, raw
 *                             numeric digest, row and batch shard digests,
 *                             samples, metrics artifacts and bytes)
 *   binding                   the #511 binding document's digest
 *
 * Map: BqRetirementContextChainCarried, bq_retirement_context_chain_format,
 * bq_retirement_context_chain_parse, bq_retirement_context_chain_name.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CONTEXT_CHAIN_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CONTEXT_CHAIN_H

#ifdef __linux__
#define BQ_RETIREMENT_CONTEXT_CHAIN_MAGIC "BQ-RETIREMENT-CONTEXT-CHAIN-V2"
/* Fifteen lines of at most three digests and a few decimals; the stage lines
 * are the longest (four digests and five decimals). */
#define BQ_RETIREMENT_CONTEXT_CHAIN_CAP 2048u
#define BQ_RETIREMENT_CONTEXT_CHAIN_LINES 15u

typedef struct BqRetirementContextChainCarried
{
    uint64_t bound_at_ns;
    char pre_sample[65], post_sample[65], binding[65];
    char logs[3][65];
    BqRetirementUnitCampaignStageFacts stages[TP_RETIREMENT_CAMPAIGN_STAGES];
} BqRetirementContextChainCarried;

/* `context-chain-job-<id>-<token>.txt`. */
static inline bool bq_retirement_context_chain_name(char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], u64 job_id,
    u64 attempt_token)
{
    int named = snprintf(name, TP_RETIREMENT_STORE_PATH_BYTES + 1, "context-chain-job-%" PRIu64 "-%" PRIu64 ".txt",
                         (uint64_t)job_id, (uint64_t)attempt_token);
    bool ok = job_id && attempt_token && named > 0 && named <= (int)TP_RETIREMENT_STORE_PATH_BYTES;
    return ok;
}

/* The canonical record. Returns its length, or 0 when a field is not a
 * lowercase digest (or the record does not fit). */
static inline u32 bq_retirement_context_chain_format(char chain[BQ_RETIREMENT_CONTEXT_CHAIN_CAP], u64 job_id,
    u64 attempt_token, char const* preparation_sha256, char const* ready_sha256, char const* row_plan_sha256,
    char const* plan_sha256, char const* context_sha256, BqRetirementContextChainCarried const* carried)
{
    char const* digests[] = {preparation_sha256, ready_sha256, row_plan_sha256, plan_sha256, context_sha256,
                             carried ? carried->pre_sample : NULL, carried ? carried->post_sample : NULL,
                             carried ? carried->binding : NULL, carried ? carried->logs[0] : NULL,
                             carried ? carried->logs[1] : NULL, carried ? carried->logs[2] : NULL};
    bool ok = chain && job_id && attempt_token && carried && carried->bound_at_ns;
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(digests); index += 1)
        ok = digests[index] && tp_retirement_digest(digests[index]);
    for (u32 stage = 0; ok && stage < TP_RETIREMENT_CAMPAIGN_STAGES; stage += 1)
    {
        BqRetirementUnitCampaignStageFacts const* facts = &carried->stages[stage];
        ok = tp_retirement_digest(facts->transcript_shards) && tp_retirement_digest(facts->raw) &&
             tp_retirement_digest(facts->row_shards) && tp_retirement_digest(facts->batch_shards);
    }
    int length = ok ? snprintf(chain, BQ_RETIREMENT_CONTEXT_CHAIN_CAP, BQ_RETIREMENT_CONTEXT_CHAIN_MAGIC
        "\njob=job-%" PRIu64 "\nattempt=%" PRIu64 "\npreparation=%s\nready=%s\nrow-plan=%s\nplan=%s\ncontext=%s"
        "\nbound-at=%" PRIu64 "\npre-sample=%s\nlogs=%s %s %s\n", (uint64_t)job_id, (uint64_t)attempt_token,
        preparation_sha256, ready_sha256, row_plan_sha256, plan_sha256, context_sha256, carried->bound_at_ns,
        carried->pre_sample, carried->logs[0], carried->logs[1], carried->logs[2]) : -1;
    for (u32 stage = 0; length > 0 && stage < TP_RETIREMENT_CAMPAIGN_STAGES; stage += 1)
    {
        BqRetirementUnitCampaignStageFacts const* facts = &carried->stages[stage];
        int line = (size_t)length < BQ_RETIREMENT_CONTEXT_CHAIN_CAP ?
            snprintf(chain + length, BQ_RETIREMENT_CONTEXT_CHAIN_CAP - (size_t)length, "stage-%u=%s %" PRIu64 " %"
                     PRIu64 " %s %s %s %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", stage, facts->transcript_shards,
                     facts->invocations, facts->completed_at_ns, facts->raw, facts->row_shards, facts->batch_shards,
                     facts->samples, facts->metrics_artifacts, facts->metrics_bytes) : -1;
        length = line > 0 ? length + line : -1;
    }
    int tail = length > 0 && (size_t)length < BQ_RETIREMENT_CONTEXT_CHAIN_CAP ?
        snprintf(chain + length, BQ_RETIREMENT_CONTEXT_CHAIN_CAP - (size_t)length, "post-sample=%s\nbinding=%s\n",
                 carried->post_sample, carried->binding) : -1;
    length = tail > 0 ? length + tail : -1;
    u32 result = length > 0 && length < (int)BQ_RETIREMENT_CONTEXT_CHAIN_CAP ? (u32)length : 0;
    return result;
}

/* The value after `key` on line `line` of `text` (lines split at LF), as a
 * NUL-terminated copy of at most capacity - 1 bytes. */
static inline bool bq_retirement_context_chain_value(char const* text, u32 length, u32 line, char const* key,
    char* value, size_t capacity)
{
    u32 start = 0, current = 0;
    while (current < line && start < length)
    {
        char const* end = memchr(text + start, '\n', length - start);
        start = end ? (u32)(end - text) + 1u : length;
        current += 1;
    }
    char const* end = start < length ? memchr(text + start, '\n', length - start) : NULL;
    size_t key_length = strlen(key);
    size_t line_length = end ? (size_t)(end - (text + start)) : 0;
    bool ok = end && line_length >= key_length && line_length - key_length < capacity &&
              !memcmp(text + start, key, key_length);
    if (ok)
    {
        memcpy(value, text + start + key_length, line_length - key_length);
        value[line_length - key_length] = 0;
    }
    return ok;
}

static inline bool bq_retirement_context_chain_digest(char const* text, char digest[65])
{
    size_t length = text ? strlen(text) : 0;
    bool ok = length == 64;
    if (ok)
    {
        memcpy(digest, text, 65);
        ok = tp_retirement_digest(digest);
    }
    return ok;
}

/* The carried values of a stored record. The parse is lenient: the caller
 * re-formats the record from these values and its own facts and requires
 * the stored bytes to equal it, which refuses every other spelling. */
static inline bool bq_retirement_context_chain_parse(char const* text, u32 length,
    BqRetirementContextChainCarried* carried)
{
    char value[512], extra[8];
    unsigned long long bound = 0;
    *carried = (BqRetirementContextChainCarried){0};
    bool ok = text && length && length < BQ_RETIREMENT_CONTEXT_CHAIN_CAP && !memchr(text, 0, length) &&
              bq_retirement_context_chain_value(text, length, 8, "bound-at=", value, sizeof(value)) &&
              sscanf(value, "%20llu%1s", &bound, extra) == 1 && bound;
    carried->bound_at_ns = ok ? (uint64_t)bound : 0;
    ok = ok && bq_retirement_context_chain_value(text, length, 9, "pre-sample=", value, sizeof(value)) &&
         bq_retirement_context_chain_digest(value, carried->pre_sample);
    char logs[3][65];
    ok = ok && bq_retirement_context_chain_value(text, length, 10, "logs=", value, sizeof(value)) &&
         sscanf(value, "%64s %64s %64s%1s", logs[0], logs[1], logs[2], extra) == 3;
    for (u32 index = 0; ok && index < 3; index += 1) ok = bq_retirement_context_chain_digest(logs[index], carried->logs[index]);
    for (u32 stage = 0; ok && stage < TP_RETIREMENT_CAMPAIGN_STAGES; stage += 1)
    {
        char key[16];
        char digests[4][65];
        unsigned long long numbers[5] = {0};
        BqRetirementUnitCampaignStageFacts* facts = &carried->stages[stage];
        snprintf(key, sizeof(key), "stage-%u=", stage);
        ok = bq_retirement_context_chain_value(text, length, 11u + stage, key, value, sizeof(value)) &&
             sscanf(value, "%64s %20llu %20llu %64s %64s %64s %20llu %20llu %20llu%1s", digests[0], &numbers[0],
                    &numbers[1], digests[1], digests[2], digests[3], &numbers[2], &numbers[3], &numbers[4], extra) == 9 &&
             bq_retirement_context_chain_digest(digests[0], facts->transcript_shards) &&
             bq_retirement_context_chain_digest(digests[1], facts->raw) &&
             bq_retirement_context_chain_digest(digests[2], facts->row_shards) &&
             bq_retirement_context_chain_digest(digests[3], facts->batch_shards);
        facts->invocations = numbers[0];
        facts->completed_at_ns = numbers[1];
        facts->samples = numbers[2];
        facts->metrics_artifacts = numbers[3];
        facts->metrics_bytes = numbers[4];
    }
    ok = ok && bq_retirement_context_chain_value(text, length, 13, "post-sample=", value, sizeof(value)) &&
         bq_retirement_context_chain_digest(value, carried->post_sample) &&
         bq_retirement_context_chain_value(text, length, 14, "binding=", value, sizeof(value)) &&
         bq_retirement_context_chain_digest(value, carried->binding);
    if (!ok) *carried = (BqRetirementContextChainCarried){0};
    return ok;
}
#endif
#endif
